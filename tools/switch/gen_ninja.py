#!/usr/bin/env python3
"""Generate a devkitA64 ninja build for the Nintendo Switch port.

The Meson build is the source of truth for source lists and per-target flags.
This script reads a configured host build directory (compile_commands.json and
build.ninja), takes the engine, GLES renderer and single-player game groups,
and re-emits them for aarch64-none-elf. Each group is partially linked and its
symbols are localized, so the three idlib copies can live in one NRO the same
way they live in separate shared modules on other platforms.

    python3 tools/switch/gen_ninja.py --host-build ../build-host --out ../build-switch
    ninja -C ../build-switch -k 0
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import sys
from pathlib import Path

DKP = os.environ.get("DEVKITPRO", "/opt/devkitpro")
BIN = f"{DKP}/devkitA64/bin/aarch64-none-elf-"

# Engine group keeps its symbols; module groups export only their entry points.
GROUPS = {
    "engine": {"target": "openQ4-client_x64", "keep": None},
    "renderer": {"target": "renderer-gles_x64.so", "keep": ["GetRenderAPI", "openQ4_Mem_GetModuleStats_renderer"], "rename": "openQ4_Mem_GetModuleStats=openQ4_Mem_GetModuleStats_renderer"},
    "game": {"target": "content/baseoq4/game-sp_x64.so", "keep": ["GetGameAPI", "openQ4_Mem_GetModuleStats_game"], "rename": "openQ4_Mem_GetModuleStats=openQ4_Mem_GetModuleStats_game"},
}

# Files the Switch build replaces; they are listed in switch_overrides.txt.
# GL entry points the renderer group's profiling wrappers (gles_RenderThread.cpp) intercept.
GL_WRAP = ["glDrawElements", "glDrawArrays", "glUseProgram", "glBindTexture", "glActiveTexture", "glTexParameteri",
           "glUniform1f", "glUniform1i", "glUniform2f", "glUniform3f", "glUniform4f", "glUniform1fv", "glUniform2fv",
           "glUniform3fv", "glUniform4fv", "glUniformMatrix4fv", "glVertexAttribPointer", "glEnableVertexAttribArray",
           "glDisableVertexAttribArray", "glEnable", "glDisable", "glBlendFunc", "glDepthFunc", "glDepthMask",
           "glColorMask", "glStencilFunc", "glStencilOp", "glCullFace", "glScissor", "glViewport", "glPolygonOffset",
           "glLinkProgram", "glDeleteProgram", "glDeleteTextures", "glDeleteBuffers", "glDeleteVertexArrays", "glVertexAttribIPointer", "glTexImage2D", "glTexSubImage2D", "glCompressedTexImage2D", "glCompressedTexSubImage2D", "glTexStorage2D", "glBufferData", "glGenerateMipmap", "glReadPixels", "glFinish", "glBindBuffer", "glBufferSubData", "glBindVertexArray", "glBindFramebuffer", "glClear"]
ARCH_FLAGS = ["-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE", "-ftls-model=local-exec"]
# Third-party trees the Switch build does not use (SDL3 and GLEW are replaced by
# src/sys/switch and the GLES loader).
SKIP_SUBSTR = ["/subprojects/SDL3-", "/subprojects/glew", "/libXNVCtrl/"]
DEFINES = ["-D__SWITCH__", "-D__linux__", "-D_GNU_SOURCE", "-include", "alloca.h", "-include", str(Path(__file__).resolve().parents[2] / "src/sys/switch/compat/switch_prelude.h"), "-I" + str(Path(__file__).resolve().parents[2] / "src/sys/switch/compat")]


SDL_VERSION = "3.4.18"
# Driver directories of the stock Linux SDL3 build that have no Horizon equivalent.
SDL_EXCLUDE = ["/video/x11/", "/video/wayland/", "/audio/pipewire/", "/core/linux/", "/dialog/unix/", "/tray/unix/",
               "/gpu/", "/render/opengl/", "/render/vulkan/", "/render/gpu/", "/joystick/linux/", "/haptic/linux/",
               "/hidapi/", "/power/", "/loadso/dlopen/", "/process/posix/", "/video/SDL_vulkan_utils.c",
               "-protocol.c", "/camera/", "/core/unix/SDL_gtk.c"]
SDL_EXTRA = ["tray/dummy/SDL_tray.c", "haptic/dummy/SDL_syshaptic.c", "loadso/dummy/SDL_sysloadso.c", "process/dummy/SDL_dummyprocess.c",
             "dialog/dummy/SDL_dummydialog.c"]
# macros that select Linux-only SDL drivers; the Switch config switches them off
SDL_CONFIG_STRIP = ("X11", "WAYLAND", "PIPEWIRE", "DBUS", "IBUS", "FCITX", "LIBUDEV", "EVDEV", "ALSA", "PULSE", "JACK",
                    "SNDIO", "KMSDRM", "LIBDECOR", "HIDAPI", "VULKAN", "OPENGL_GLX", "OPENGL_EGL", "OPENGL_ES", "RENDER_OGL",
                    "RENDER_GPU", "DIALOG_UNIX", "TRAY_UNIX", "POWER_LINUX", "LINUX", "DLOPEN", "STEAM", "SDL_PROCESS_POSIX",
                    "SDL_JOYSTICK_VIRTUAL", "TM_GMTOFF", "SDL_VIDEO_DRIVER", "SDL_CAMERA", "SDL_LOADSO", "SDL_HAPTIC", "SDL_SENSOR")
SDL_CONFIG_APPEND = """
/* ---- Nintendo Switch (devkitA64/libnx) ---- */
#define SDL_VIDEO_DRIVER_SWITCH 1
#define SDL_VIDEO_OPENGL_EGL 1
#define SDL_VIDEO_OPENGL_ES2 1
#define SDL_VIDEO_STATIC_ANGLE 1
#define SDL_JOYSTICK_SWITCH 1
#define SDL_AUDIO_DRIVER_SWITCH 1
#define SDL_HAPTIC_DUMMY 1
#define SDL_SENSOR_DUMMY 1
#define SDL_LOADSO_DUMMY 1
#define SDL_PROCESS_DUMMY 1
#define SDL_CAMERA_DISABLED 1
#define SDL_POWER_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_DIALOG_DUMMY 1
#define SDL_TRAY_DUMMY 1
"""


def build_sdl_config(hb: Path, out: Path) -> Path:
    import re
    cfg = out / "sdl3cfg"
    cfg.mkdir(exist_ok=True)
    src = (hb / f"subprojects/SDL3-{SDL_VERSION}/SDL_build_config.h").read_text().splitlines()
    kept = []
    for line in src:
        m = re.match(r"\s*#\s*define\s+(\w+)", line)
        if m and any(k in m.group(1) for k in SDL_CONFIG_STRIP):
            line = "/* switch: disabled */ // " + line
        kept.append(line)
    # the closing #endif of the include guard must stay last
    text = "\n".join(kept)
    idx = text.rfind("#endif")
    text = text[:idx] + SDL_CONFIG_APPEND + text[idx:]
    (cfg / "SDL_build_config.h").write_text(text)
    return cfg


def patch_sdl_tables(sdl_src: Path, out: Path):
    """Register the Switch drivers in SDL's static driver tables (compiled copies only)."""
    pdir = out / "sdl3patch"
    pdir.mkdir(exist_ok=True)
    v = (sdl_src / "video/SDL_video.c").read_text()
    anchor = "#ifdef SDL_VIDEO_DRIVER_N3DS\n    &N3DS_bootstrap,"
    assert anchor in v, "SDL_video.c driver table moved"
    v = v.replace(anchor, "#ifdef SDL_VIDEO_DRIVER_SWITCH\n    &SWITCH_bootstrap,\n#endif\n" + anchor, 1)
    v = v.replace("#include \"SDL_sysvideo.h\"", "#include \"SDL_sysvideo.h\"\n#ifdef SDL_VIDEO_DRIVER_SWITCH\nextern VideoBootStrap SWITCH_bootstrap;\n#endif", 1)
    (pdir / "SDL_video.c").write_text(v)
    j = (sdl_src / "joystick/SDL_joystick.c").read_text()
    anchor = "#ifdef SDL_JOYSTICK_N3DS\n    &SDL_N3DS_JoystickDriver,"
    assert anchor in j, "SDL_joystick.c driver table moved"
    j = j.replace(anchor, "#ifdef SDL_JOYSTICK_SWITCH\n    &SDL_SWITCH_JoystickDriver,\n#endif\n" + anchor, 1)
    j = j.replace("#include \"SDL_sysjoystick.h\"", "#include \"SDL_sysjoystick.h\"\n#ifdef SDL_JOYSTICK_SWITCH\nextern SDL_JoystickDriver SDL_SWITCH_JoystickDriver;\n#endif", 1)
    (pdir / "SDL_joystick.c").write_text(j)
    a = (sdl_src / "audio/SDL_audio.c").read_text()
    anchor = "#ifdef SDL_AUDIO_DRIVER_N3DS\n    &N3DSAUDIO_bootstrap,"
    assert anchor in a, "SDL_audio.c driver table moved"
    a = a.replace(anchor, "#ifdef SDL_AUDIO_DRIVER_SWITCH\n    &SWITCHAUDIO_bootstrap,\n#endif\n" + anchor, 1)
    a = a.replace("#include \"SDL_sysaudio.h\"", "#include \"SDL_sysaudio.h\"\n#ifdef SDL_AUDIO_DRIVER_SWITCH\nextern AudioBootStrap SWITCHAUDIO_bootstrap;\n#endif", 1)
    (pdir / "SDL_audio.c").write_text(a)
    return pdir


def parse_ninja(path: Path):
    text = path.read_text().replace("$\n", "")
    builds = {}
    for line in text.splitlines():
        if not line.startswith("build "):
            continue
        head, _, rest = line[6:].partition(": ")
        parts = rest.split(" ")
        rule, ins = parts[0], parts[1:]
        explicit = []
        for tok in ins:
            if tok == "||":
                break
            if tok == "|":
                continue
            if tok:
                explicit.append(tok)
        for out in head.split(" | ")[0].split():
            builds[out] = (rule, explicit)
    return builds


def collect_objects(builds, target, seen=None):
    seen = seen if seen is not None else set()
    objs = []
    rule, ins = builds[target]
    for i in ins:
        if i in seen:
            continue
        seen.add(i)
        if i.endswith(".o"):
            objs.append(i)
        elif i.endswith(".a") and i in builds:
            objs += collect_objects(builds, i, seen)
    return objs


def convert(command: str, build_dir: Path, src: str):
    args = shlex.split(command)[1:]
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-MD", "-MMD", "-Winvalid-pch", "-fpch-preprocess", "-fPIC", "-fPIE"):
            continue
        if a.startswith(("-fstack-protector", "-fcf-protection", "-D_FORTIFY_SOURCE", "-D_GLIBCXX_ASSERTIONS", "-fsanitize", "-fno-sanitize")):
            continue
        if a.startswith("-m") and not a.startswith("-mt"):
            continue
        if not a.startswith("-") and a.endswith((".cpp", ".c", ".cc")):
            continue
        if a.startswith(("-DOPENQ4_HAVE_X11_HELPERS", "-fdiagnostics-color")):
            continue
        if a == "precompiled.h":
            a = str(Path(src).parent.parent) if False else "@PRECOMPILED@"
        if a.startswith("-I") and not a.startswith("-I/"):
            a = "-I" + str((build_dir / a[2:]).resolve())
        out.append(a)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host-build", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--source-root", type=Path)
    ap.add_argument("--skip", type=Path, help="file listing source paths (relative to src/) to exclude")
    ap.add_argument("--extra", nargs="*", default=[], help="extra sources as group:path")
    ap.add_argument("--diag", action="store_true", help="build with OPENQ4_SWITCH_DIAG: on-device profiler, audio tests and per-frame timing lines")
    a = ap.parse_args()
    if a.diag:
        DEFINES.append("-DOPENQ4_SWITCH_DIAG")

    hb = a.host_build.resolve()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = json.loads((hb / "compile_commands.json").read_text())
    by_output = {e["output"]: e for e in cc}
    builds = parse_ninja(hb / "build.ninja")
    src_root = (a.source_root or hb.parent / "openQ4_S").resolve()
    pch = src_root / "src/idlib/precompiled.h"
    skip = set()
    if a.skip:
        skip = {l.strip() for l in a.skip.read_text().splitlines() if l.strip() and not l.startswith("#")}

    lines = [
        "ninja_required_version = 1.10",
        f"cxx = {BIN}g++", f"cc = {BIN}gcc", f"ld = {BIN}g++", f"objcopy = {BIN}objcopy",
        "",
        "rule cxx", "  command = $cxx $flags -MD -MF $out.d -c $in -o $out", "  depfile = $out.d", "  deps = gcc", "  description = CXX $out",
        "rule cc", "  command = $cc $flags -MD -MF $out.d -c $in -o $out", "  depfile = $out.d", "  deps = gcc", "  description = CC $out",
        "rule partial", "  command = $ld -r -nostdlib $wrap -o $out $in", "  description = PARTIAL $out",
        # Module groups own private copies of inline/vtable/typeinfo code: dissolve COMDAT groups
        # so the linker cannot discard one module's copy in favour of another's local symbol.
        "rule partial_private", "  command = $ld -r -nostdlib -Wl,--force-group-allocation $wrap -o $out $in", "  description = PARTIAL(private) $out",
        "rule localize", "  command = $objcopy $keep $in $out", "  description = LOCALIZE $out",
        "rule link", "  command = $ld -specs=" + DKP + "/libnx/switch.specs " + " ".join(ARCH_FLAGS) + " -Wl,-Map,$out.map -o $out $in $libs", "  description = LINK $out",
        "rule openal_sdl3", "  command = rm -rf openal_tmp && mkdir openal_tmp && cd openal_tmp && " + BIN + "ar x " + DKP + "/portlibs/switch/lib/libopenal.a sdl2.o && " + BIN + "objcopy " + " ".join(f"--redefine-sym SDL_{n}=SWITCH_SDL2_{n}" for n in ("OpenAudioDevice", "CloseAudioDevice", "PauseAudioDevice", "GetAudioDeviceName", "GetNumAudioDevices", "InitSubSystem")) + " sdl2.o sdl2.o && cp " + DKP + "/portlibs/switch/lib/libopenal.a ../$out && " + BIN + "ar r ../$out sdl2.o && cd .. && rm -rf openal_tmp", "  description = OPENAL(SDL3) $out",
        "rule nro", "  command = " + DKP + "/tools/bin/elf2nro $in $out", "  description = NRO $out",
        "",
    ]
    group_objs = {}
    missing = []
    for g, spec in GROUPS.items():
        objs = collect_objects(builds, spec["target"])
        built = []
        for o in objs:
            e = by_output.get(o)
            if not e:
                missing.append(o)
                continue
            src = e["file"]
            if not os.path.isabs(src):
                src = os.path.normpath(os.path.join(e["directory"], src))
            if any(x in src for x in SKIP_SUBSTR):
                continue
            rel = os.path.relpath(src, src_root / "src") if str(src).startswith(str(src_root / "src")) else None
            if rel in skip:
                continue
            is_c = src.endswith(".c")
            flags = convert(e["command"], hb, src)
            flags = [str(pch) if f == "@PRECOMPILED@" else f for f in flags]
            if is_c:
                flags = [f for f in flags if not f.startswith("-std=c++")]
            flags = DEFINES + ARCH_FLAGS + [f"-I{DKP}/libnx/include", f"-I{DKP}/portlibs/switch/include",
                                             f"-I{hb}", "-w", "-fpermissive"] + flags
            # the -include precompiled.h flag arrives as two tokens
            fixed, i = [], 0
            while i < len(flags):
                if flags[i] == "-include" and i + 1 < len(flags) and flags[i + 1] == str(pch):
                    fixed += ["-include", str(pch)]
                    i += 2
                    continue
                fixed.append(flags[i])
                i += 1
            obj = f"obj/{g}/{Path(o).name}"
            lines += [f"build {obj}: {'cc' if is_c else 'cxx'} {src}", f"  flags = {' '.join(shlex.quote(f) for f in fixed)}"]
            built.append(obj)
        for extra in a.extra:
            eg, _, ep = extra.partition(":")
            if eg == g:
                obj = f"obj/{g}/extra_{Path(ep).stem}.o"
                flags = DEFINES + ARCH_FLAGS + [f"-I{DKP}/libnx/include", f"-I{DKP}/portlibs/switch/include", f"-I{hb}",
                                                f"-I{src_root}/src", "-std=c++20", "-w", "-fpermissive", "-include", str(pch)]
                lines += [f"build {obj}: cxx {ep}", f"  flags = {' '.join(shlex.quote(f) for f in flags)}"]
                built.append(obj)
        part = f"{g}.part.o"
        lines += [f"build {part}: {'partial_private' if spec['keep'] else 'partial'} " + " ".join(built)]
        if g == "renderer":
            lines.append("  wrap = " + " ".join(f"-Wl,--wrap={n}" for n in GL_WRAP))
        if spec["keep"]:
            keep = f"--redefine-sym {spec['rename']} " + " ".join(f"--keep-global-symbol={k}" for k in spec["keep"])
            lines += [f"build {g}.o: localize {part}", f"  keep = {keep}"]
            group_objs[g] = f"{g}.o"
        else:
            group_objs[g] = part
        lines.append("")

    # ---- SDL3 for Horizon: stock core + Switch video/joystick drivers ----
    sdl_root = src_root / f"subprojects/SDL3-{SDL_VERSION}"
    # SDL refuses SDL_DYNAMIC_API=0 on the command line. Statically linked Horizon builds
    # have no dlopen, so relax that guard in the extracted (git-ignored) tree, idempotently.
    dyn = sdl_root / "src/dynapi/SDL_dynapi.h"
    txt = dyn.read_text()
    guard = "#ifdef SDL_DYNAMIC_API // Tried to force it on the command line?"
    if guard in txt:
        dyn.write_text(txt.replace(guard, "#if defined(SDL_DYNAMIC_API) && SDL_DYNAMIC_API // Tried to force it on the command line?", 1))
    cfg = build_sdl_config(hb, out)
    pdir = patch_sdl_tables(sdl_root / "src", out)
    switch_dir = src_root / "src/sys/switch/sdl3"
    sdl_flags = ["-D__SWITCH__", "-D_GNU_SOURCE", "-DUSING_GENERATED_CONFIG_H", "-DSDL_BUILD_MAJOR_VERSION=3",
                 "-DSDL_BUILD_MINOR_VERSION=4", "-DSDL_BUILD_MICRO_VERSION=18", "-D__TM_GMTOFF=tm_gmtoff", "-DSDL_DYNAMIC_API=0", "-D_REENTRANT", "-O2", "-w"] + ARCH_FLAGS + [
                 f"-I{cfg}", f"-I{sdl_root}/include", f"-I{sdl_root}/src", f"-I{sdl_root}", f"-I{DKP}/libnx/include",
                 f"-I{DKP}/portlibs/switch/include", f"-I{src_root}/src/sys/switch/compat", "-include", str(src_root / "src/sys/switch/compat/switch_prelude.h")]
    sdl_srcs = []
    for o in collect_objects(builds, f"subprojects/SDL3-{SDL_VERSION}/libSDL3.a"):
        e = by_output.get(o)
        if not e:
            continue
        f = e["file"] if os.path.isabs(e["file"]) else os.path.normpath(os.path.join(e["directory"], e["file"]))
        if any(x in f for x in SDL_EXCLUDE) or not f.endswith(".c"):
            continue
        sdl_srcs.append(f)
    sdl_srcs += [str(sdl_root / "src" / x) for x in SDL_EXTRA if (sdl_root / "src" / x).exists()]
    sdl_srcs += [str(switch_dir / "SDL_switchvideo.c"), str(switch_dir / "SDL_switchjoystick.c"), str(switch_dir / "SDL_switchaudio.c")]
    sdl_objs = []
    for f in sdl_srcs:
        name = Path(f).name
        extra_inc = []
        if (name == "SDL_video.c" and "/src/video/" in f) or (name == "SDL_joystick.c" and "/src/joystick/" in f) or (name == "SDL_audio.c" and "/src/audio/" in f):
            extra_inc = [f"-I{Path(f).parent}"]
            f = str(pdir / name)
        obj = f"obj/sdl3/{Path(f).name}.{abs(hash(f)) % 10**6}.o"
        lines += [f"build {obj}: cc {f}", f"  flags = {' '.join(shlex.quote(x) for x in sdl_flags + extra_inc)} -std=gnu11"]
        sdl_objs.append(obj)
    lines += ["build sdl3.part.o: partial " + " ".join(sdl_objs), ""]
    group_objs["sdl3"] = "sdl3.part.o"

    # Switch support code: libc shims and the SDL2 audio entry points OpenAL expects.
    sw = src_root / "src/sys/switch"
    support = []
    for name, extra in (("switch_libc_shims.c", []), ("switch_audio_diag.c", []), ("sdl2_audio_compat.c", sdl_flags)):
        obj = f"obj/support/{name}.o"
        flags = (extra or (["-D__SWITCH__", "-D_GNU_SOURCE", "-O2", "-w"] + ARCH_FLAGS + [f"-I{DKP}/libnx/include"])) + ["-std=gnu11"]
        lines += [f"build {obj}: cc {sw / name}", f"  flags = {' '.join(shlex.quote(x) for x in flags)}"]
        support.append(obj)
    lines += ["build support.part.o: partial " + " ".join(support), "build libopenal_sdl3.a: openal_sdl3", ""]
    group_objs["support"] = "support.part.o"

    libs = "-L{0}/libnx/lib -L{0}/portlibs/switch/lib libopenal_sdl3.a -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lnx -lm".format(DKP)
    lines += [f"build openQ4.elf: link " + " ".join(group_objs.values()) + " libopenal_sdl3.a", f"  libs = {libs}",
              "build openQ4.nro: nro openQ4.elf", "default openQ4.nro", ""]
    (out / "build.ninja").write_text("\n".join(lines))
    print(f"wrote {out/'build.ninja'}; unresolved objects: {len(missing)}")
    for m in missing[:5]:
        print("  missing", m)


if __name__ == "__main__":
    sys.exit(main())
