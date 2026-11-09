# Nintendo Switch port (work in progress)

Status: **builds and links; never run on hardware.** This branch (`switch`) cross-compiles the
engine, the OpenGL ES renderer and the single-player game module into one NRO with
devkitA64/libnx. Nothing here has booted on a console or an emulator, so treat every runtime
statement below as unverified.

## Build

Prerequisites: devkitPro with `devkitA64`, `libnx`, `switch-mesa`, `switch-openal-soft`
(no root needed beyond the original install), and a configured *host* build, which supplies the
source lists and per-target flags:

```bash
meson setup ../build-host -Dbuild_renderer_gles=enabled -Dbuild_renderer_vk=false -Dopenxr=disabled --buildtype=release
ninja -C ../build-host
python3 tools/switch/gen_ninja.py --host-build ../build-host --out ../build-switch
ninja -C ../build-switch        # produces openQ4.elf and openQ4.nro
```

`tools/switch/gen_ninja.py` is deliberately outside Meson for now: it reads the host build's
`compile_commands.json` and `build.ninja`, so the Switch build always compiles the same sources with
the same defines as the desktop build. Folding it into `meson.build` as a `horizon` host system is
future work.

## How it fits together

| Piece | Where | Notes |
|---|---|---|
| Three code groups | `gen_ninja.py` | engine, GLES renderer and SP game are partially linked (`ld -r`). The two modules get `--force-group-allocation` (private inline/vtable copies), then `objcopy` keeps only their entry points, so the three `idlib` copies cannot collide. |
| Module loading | `Sys_DLL_*` in `src/sys/posix/posix_main.cpp` | Under `__SWITCH__` a "load" selects the linked-in module by file name (`game-sp`, `renderer-gles`). |
| SDL3 | `gen_ninja.py` plus `src/sys/switch/sdl3/` | Stock SDL3 core with Linux drivers removed and three new drivers: video (EGL window), joystick (libnx pad) and audio (`audout`). The driver tables in `SDL_video.c`, `SDL_joystick.c` and `SDL_audio.c` are patched in compiled copies only. |
| OpenAL | `gen_ninja.py` | devkitPro's OpenAL Soft outputs through an SDL2 backend. Its five SDL2 audio references are renamed to `SWITCH_SDL2_*` and answered by `src/sys/switch/sdl2_audio_compat.c` over SDL3 audio streams, because SDL3 reuses those names with different signatures. |
| Missing libc | `src/sys/switch/compat/`, `switch_libc_shims.c` | Header stubs (`sys/mman.h`, `dlfcn.h`, `ifaddrs.h`, ...), `sysinfo()` backed by the process memory limit, and fixed answers for `sysconf`, `geteuid`, ... |
| Start-up | `userAppInit` in `switch_libc_shims.c` | Sockets, `nxlink` log streaming, `chdir("sdmc:/switch/openQ4")`. |

Platform macros: the Switch build defines `__SWITCH__ __linux__ _GNU_SOURCE`, the same trick the
Android port relies on, so the existing POSIX paths are used.

## Known gaps

- **Untested.** No console, no game data were available. Expect runtime failures.
- **Saves.** `framework/DurableFile.cpp` renames through directory file descriptors; the `*at()`
  shims only support `AT_FDCWD` and absolute paths. It needs a Switch branch.
- **Input.** Only the controller is wired. Keyboard and mouse (libnx HID) and text input are not.
- **Memory.** Launching from the Album applet gives far less memory than title takeover (hold R while
  starting a game). Quake 4 needs takeover mode.
- **Multiplayer.** `game-mp` is not linked; IPv6 is absent (`getifaddrs` is a stub).
- **UI.** The retained RmlUi interface is compiled in; start with `+set ui_retained 0`.
- **Renderer.** Needs OpenGL ES 3.0 from Mesa's nouveau driver; whether that driver reaches playable
  speed on Tegra X1 is the main open question.
- **Size.** `openQ4.elf` carries debug info (53 MB); strip it for a smaller NRO.

## Game data

Quake 4 data is not part of this repository and must not be published. Copy your own
`q4base/` next to the NRO (`sdmc:/switch/openQ4/`).
