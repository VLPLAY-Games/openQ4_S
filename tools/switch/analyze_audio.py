#!/usr/bin/env python3
"""
Summarize an audio dump written by the Switch build (audio_N_f32.raw / audio_N_s16.raw / audio_N.txt).

    python3 -I analyze_audio.py audio_1            (the common prefix, without _f32.raw / _s16.raw)

Both streams are stereo at 48 kHz. f32 is what OpenAL rendered, s16 what was submitted to the
hardware. Plain Python, no extra packages. For every 100 ms block the report gives the level, a
high-frequency measure (the rms of the second difference relative to the rms of the signal: hiss
and aliasing raise it), the largest step between neighbouring samples (clicks) and clipping, then
lists the blocks that look wrong and compares the two streams.
"""
import array
import math
import sys

RATE = 48000
BLOCK = RATE // 10


def load(path, typecode, scale):
    a = array.array(typecode)
    with open(path, "rb") as f:
        a.frombytes(f.read())
    # left channel and right channel
    return [v * scale for v in a[0::2]], [v * scale for v in a[1::2]]


def block_stats(left, right):
    out = []
    for b in range(len(left) // BLOCK):
        l = left[b * BLOCK:(b + 1) * BLOCK]
        r = right[b * BLOCK:(b + 1) * BLOCK]
        mono = [(x + y) * 0.5 for x, y in zip(l, r)]
        rms = math.sqrt(sum(v * v for v in mono) / len(mono))
        peak = max(max(map(abs, l)), max(map(abs, r)))
        d2 = [mono[i + 1] - 2 * mono[i] + mono[i - 1] for i in range(1, len(mono) - 1)]
        hf = math.sqrt(sum(v * v for v in d2) / len(d2)) / (4.0 * rms + 1e-9)
        step = max(abs(l[i + 1] - l[i]) for i in range(len(l) - 1))
        clip = sum(1 for v in l if abs(v) >= 0.999) + sum(1 for v in r if abs(v) >= 0.999)
        out.append((b, rms, peak, hf, step, clip))
    return out


def median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0.0


def main():
    prefix = sys.argv[1]
    f32 = load(prefix + "_f32.raw", "f", 1.0)
    s16 = load(prefix + "_s16.raw", "h", 1.0 / 32768.0)
    try:
        print(open(prefix + ".txt").read().strip())
    except OSError:
        pass
    print("f32 frames %d (%.1f s), s16 frames %d (%.1f s)" % (len(f32[0]), len(f32[0]) / RATE, len(s16[0]), len(s16[0]) / RATE))
    results = {}
    for name, (l, r) in (("f32", f32), ("s16", s16)):
        st = block_stats(l, r)
        results[name] = st
        loud = [s for s in st if s[1] > 0.01]
        print("\n== %s: %d blocks of 100 ms, %d with signal (rms > 0.01)" % (name, len(st), len(loud)))
        if not loud:
            continue
        his = sorted(s[3] for s in loud)
        print("   rms median %.3f  peak max %.3f  clipped samples %d" % (median([s[1] for s in loud]), max(s[2] for s in loud), sum(s[5] for s in st)))
        print("   high-frequency measure: median %.3f  p90 %.3f  max %.3f  (music/voice sits low; hiss pushes it up)" % (his[len(his) // 2], his[int(len(his) * 0.9)], his[-1]))
        print("   largest sample-to-sample step per block: median %.3f  max %.3f" % (median([s[4] for s in loud]), max(s[4] for s in loud)))
        print("   blocks with the highest high-frequency measure (t, rms, hf, maxstep):")
        for s in sorted(loud, key=lambda s: -s[3])[:6]:
            print("     t=%5.1fs rms=%.3f hf=%.3f step=%.3f" % (s[0] / 10.0, s[1], s[3], s[4]))
    a = [s[1] for s in results["f32"]]
    b = [s[1] for s in results["s16"]]
    if a and b:
        print("\nmedian rms: f32 %.4f  s16 %.4f (ratio %.3f; about 1.0 means the conversion keeps the level)" % (median(a), median(b), (median(b) + 1e-9) / (median(a) + 1e-9)))
    run = longest = runs = 0
    for x, y in zip(f32[0], f32[1]):
        if x == 0.0 and y == 0.0:
            run += 1
        else:
            if run > 0:
                runs += 1
                longest = max(longest, run)
            run = 0
    print("f32 silent runs: %d, longest %d frames (%.1f ms)" % (runs, longest, 1000.0 * longest / RATE))


if __name__ == "__main__":
    main()
