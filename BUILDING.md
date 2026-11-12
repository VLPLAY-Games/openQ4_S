# Building openQ4 for Nintendo Switch

This builds `openQ4.nro` and the content packs from source. The build runs on
Linux. Windows and macOS hosts are not supported by this fork.

## How the build works

The Switch build reuses the project's Meson build as the single source of truth
for source lists and compiler flags:

1. **Meson configures a host tree** (`build-host`). The host tree compiles for the
   machine you build on. It is not run; it exists to generate headers and the
   content packs and to record, in `compile_commands.json` and `build.ninja`,
   exactly which files make up each target and with which flags.
2. **`tools/switch/gen_ninja.py` reads that host tree** and writes a second Ninja
   file (`build-switch`) that compiles the same files with the devkitA64
   cross compiler for `aarch64-none-elf`.
3. **Ninja builds the NRO.** Three groups (engine, OpenGL ES renderer,
   single-player game) are each partially linked and have their symbols
   localised, so the three copies of idlib can live in one binary. SDL3 is built
   from the bundled subproject with the Switch drivers from `src/sys/switch/`.

## Prerequisites

- Linux (developed and tested on Ubuntu).
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with
  - `devkitA64` (r29 or newer)
  - `libnx` (4.12 or newer)
  - the `switch-mesa` and `switch-openal-soft` packages
  - their dependencies (installed by `dkp-pacman`)
- `DEVKITPRO` set to the devkitPro prefix (`/opt/devkitpro` by default).
- Host tools: `meson` (1.6 or newer), `ninja`, `python3`, a C/C++ compiler.
- Host development packages that the bundled SDL3 and the host renderer module
  need (X11 or Wayland, EGL and OpenGL ES 2). Meson reports what is missing.

Install the Switch libraries:

```bash
sudo dkp-pacman -S switch-mesa switch-openal-soft switch-sdl2
```

## Build

From the repository root:

```bash
# 1. Host tree: generates headers and the content packs, records the build.
meson setup build-host --buildtype release
ninja -C build-host

# 2. Generate the devkitA64 build from it.
python3 tools/switch/gen_ninja.py --host-build build-host --out build-switch

# 3. Build the NRO.
ninja -C build-switch
```

Outputs:

| File | What it is |
| --- | --- |
| `build-switch/openQ4.nro` | the application, copy to `sdmc:/switch/openQ4/` |
| `build-switch/openQ4.elf` | the same binary with symbols (needed to read profiles) |
| `build-host/baseoq4/pak0.pk4`, `pak1.pk4`, `pak2.pk4` | openQ4 content packs, copy to `sdmc:/switch/openQ4/baseoq4/` |

`gen_ninja.py` expects the repository to sit next to the host tree as
`<parent>/openQ4_S`. If your checkout has another name or location, pass
`--source-root <path>`.

## Release and diagnostic builds

Release builds contain no diagnostics. For development there is one switch:

```bash
python3 tools/switch/gen_ninja.py --host-build build-host --out build-switch --diag
```

`--diag` defines `OPENQ4_SWITCH_DIAG`, which compiles in:

- the on-device sampling profiler (enabled by an empty `profile.txt` next to the
  NRO; writes `prof.bin`, read with `tools/switch/analyze_prof.py` and the ELF of
  the same build),
- the audio output self-test and dumps (`tools/switch/analyze_audio.py`),
- per-frame timing lines in `log.txt` (`com_speeds 1`).

## Meson options

| Option | Default | Meaning |
| --- | --- | --- |
| `use_pch` | true | precompiled `idlib/precompiled.h` |
| `build_engine` | true | build the engine target |
| `build_games` | true | build the game module targets |
| `build_game_sp` | true | build the single-player game module |
| `build_game_mp` | true | build the multiplayer game module (needed for local multiplayer) |
| `linux_x11` | auto | host SDL3 X11 driver |
| `gles_root` | empty | prefix with `lib/` containing GLES for the host module |
| `version_track`, `version_iteration`, `version_base_override` | `stable` | version string control (`stable` gives the plain version, 1.1.1) |
| `idlib_asserts` | false | enable `assert()`/`verify()` in idlib |
| `prebuilt_content_packs` | empty | use packs built elsewhere |

## Troubleshooting the build

- *`unresolved objects: N` from `gen_ninja.py`*: the host tree is stale or
  incomplete. Run `ninja -C build-host` first.
- *Missing `switch-*` headers*: install the devkitPro packages above.
- *Link errors about `SDL_*` from OpenAL*: the OpenAL Soft static library is
  rewritten by the generator to use SDL3 audio. Make sure `switch-openal-soft`
  (and not another OpenAL) is installed.
