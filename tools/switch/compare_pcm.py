#!/usr/bin/env python3
"""
Compare a pcm_N.raw dump from the Switch with the same sound decoded on the PC.

    python3 -I compare_pcm.py switch.raw pc.raw [channels]

Both are little-endian s16. Reports how many samples differ, the largest difference, and the
first place they diverge. Identical output means the Switch decodes correctly and the noise is
somewhere else; differences point at the decoder.
"""
import array
import sys

ch = int(sys.argv[3]) if len(sys.argv) > 3 else 1
a = array.array("h")
b = array.array("h")
a.frombytes(open(sys.argv[1], "rb").read())
b.frombytes(open(sys.argv[2], "rb").read())
n = min(len(a), len(b))
print("samples: switch %d, pc %d" % (len(a), len(b)))
diff = [abs(a[i] - b[i]) for i in range(n)]
bad = [i for i, d in enumerate(diff) if d > 0]
print("differing samples: %d of %d (%.2f%%)" % (len(bad), n, 100.0 * len(bad) / max(n, 1)))
if bad:
    print("largest difference: %d (of 32768)" % max(diff))
    print("first difference at frame %d" % (bad[0] // ch))
    rms = (sum(d * d for d in diff) / n) ** 0.5
    sig = (sum(v * v for v in b[:n]) / n) ** 0.5
    print("rms of the difference: %.1f, rms of the signal: %.1f (ratio %.4f)" % (rms, sig, rms / (sig + 1e-9)))
else:
    print("identical")
