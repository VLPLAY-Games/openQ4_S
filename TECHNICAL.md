# openQ4 for Nintendo Switch: technical notes

How the port is put together. For installing, see the [README](README.md); for
building, [BUILDING.md](BUILDING.md).

## Process layout

Everything is one NRO. The engine, the OpenGL ES renderer and the single-player
game module are normally separate shared modules. On the Switch there is no
dynamic loader for them, so each is linked as a **group**:

- `ld -r` combines a group's objects, then `objcopy` localises every symbol
  except the module entry points (`GetRenderAPI`, `GetGameAPI`, memory-stats
  hooks). Each group therefore keeps its own copy of idlib, as it would as a
  shared module.
- The module loader (`Sys_DLL_*` seam in the engine) returns the static entry
  points instead of calling `dlopen`.

`tools/switch/gen_ninja.py` produces this build; see [BUILDING.md](BUILDING.md).

## Platform layer (`src/sys/switch/`)

| File | Role |
| --- | --- |
| `switch_libc_shims.c` | start-up (`userAppInit`: sockets, working directory, SD-card log), libc gaps (`openat`-style calls emulated through directory handles), the log flusher thread, the fast-exit path |
| `sdl3/SDL_switchvideo.c` | SDL3 video driver: EGL window for the GLES renderer, event pump (`appletMainLoop`), system software keyboard for text fields |
| `sdl3/SDL_switchjoystick.c` | HID controller as one gamepad (handheld, Joy-Con pair, Pro Controller), motion (gyro) sensor |
| `sdl3/SDL_switchaudio.c` | audio output through the audio renderer (`audren`), with an `audout` fallback |
| `sdl2_audio_compat.c` | adapter that lets the devkitPro OpenAL Soft (which only knows an SDL2 backend) run on SDL3 audio streams |
| `switch_audio_diag.c` | diagnostics (compiled out unless `OPENQ4_SWITCH_DIAG`) |

The Switch is built with `-D__SWITCH__ -D__linux__ -D_GNU_SOURCE`. `__linux__`
is defined deliberately: the engine's POSIX layer is reused.

## Rendering

- OpenGL ES 3.0 through switch-mesa (nouveau). The renderer is the shared
  `renderer/GLES` module: the same front end as the desktop renderer with GL
  resolved through `qgl_gles.h`.
- **Render thread** (`renderer/GLES/gles_RenderThread.cpp`): the back end of a
  frame runs on a second core while the main thread starts the next game tic.
  The GL context is handed over with `GLimp_ReleaseContextNoFinish` /
  `GLimp_ActivateContext`. Only gameplay frames are threaded; loading and menus
  draw inline. A trace or dynamic-model request that needs up-to-date entity
  definitions waits for the render thread (`R_RenderThread_Sync`).
- GL call wrappers (`-Wl,--wrap`) cache uniforms, binds and attribute state and
  skip redundant calls; each GL call costs about 10 microseconds in this driver,
  so removing redundant ones matters.
- Light budget: `r_glesMaxLights` limits the lights drawn per view (24 on the
  Switch).
- Effects (BSE) default to a lighter profile on the Switch (`bse_scale 0.6`,
  culled looping effects are not serviced, cached frustum culling).

## CPU work

The game is CPU-bound on the Switch. The measures that made the difference:

- NEON kernels for skeletal-model bounds and vertex transforms, with a start-up
  self-test against the scalar code.
- Memoised bounds for animated models and cached area lookups.
- Throttled liquid probes for AI and sound, no redundant clip relinking.
- Particles collide with the world only, not with actors (`bse_particleActorTrace`).
- The idle player focus search runs a few times per second instead of every tic.

## Audio

OpenAL Soft renders float stereo at 48 kHz into an SDL3 audio stream; the Switch
driver feeds it to the audio renderer as one two-channel 16-bit voice with three
wave buffers. The legacy `audout` service produced audible noise on loud low
tones even though the data handed to it was verified clean; `audren` does not.

## Memory

Roughly 3 GB are available to the application in title takeover mode. Typical
use is 1.5 to 1.8 GB of heap on the early levels. The Mesa driver and textures
account for the rest.

## Input

- Controller: standard SDL gamepad mapping by label (A confirms, B cancels).
- Gyro: the console's (or controller's) six-axis sensor is exposed as an SDL gyro
  sensor; the engine's `in_gyro` path turns it into look input. There is no
  relative-mouse mode on the Switch, so the gyro path is allowed whenever no menu
  has the pointer. Yaw is computed about the vertical shown by the accelerometer,
  which works held upright or flat.
- Text: when the engine starts text input, the video driver opens the system
  software keyboard from the event pump and sends the result as text followed by
  Return.

## Exit

`Posix_Exit` ends in `switch_fast_exit()`: stop the log thread, flush, run libnx's
service shutdown (`__appExit`) and return to the loader (`__nx_exit`). It skips
`exit()` because running every static destructor of the three module groups
crashed on the way out.

## Diagnostics

All of it sits behind `OPENQ4_SWITCH_DIAG` (see [BUILDING.md](BUILDING.md)). With
it, `log.txt` carries per-frame timing, GL call counts and render-thread waits;
a sampling profiler and audio tests are available. Analysis scripts are in
`tools/switch/`.

## Licensing

See [LICENSING.md](LICENSING.md). The provenance records for the engine and the
game libraries are kept in `docs/dev/`.
