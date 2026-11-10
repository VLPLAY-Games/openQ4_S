# openQ4 for Nintendo Switch

**Quake 4 on the Nintendo Switch.** A port of the [openQ4](https://github.com/themuffinator/openQ4) engine and single-player game libraries to the Switch homebrew platform (devkitA64 / libnx), built around your own retail Quake 4 data.

[![Platform](https://img.shields.io/badge/platform-Nintendo%20Switch-e60012.svg)](#requirements)
[![Status](https://img.shields.io/badge/status-1.0.0%20release%20candidate-d97a1f.svg)](#status)
[![Licensing](https://img.shields.io/badge/Licensing-GPL%20engine%20%7C%20SDK%20game%20code-blue.svg)](LICENSING.md)

> [!IMPORTANT]
> This repository contains **no Quake 4 game data**. You need your own legitimate copy of Quake 4 (Steam or GOG) and you copy its files to your own console. Nothing from the retail game is distributed here.

- [Status](#status)
- [Requirements](#requirements)
- [Installation](#installation)
- [Playing](#playing)
- [Known limitations](#known-limitations)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Credits](#credits)
- [Licence](#licence)
- [Кратко по-русски](#кратко-по-русски)

---

## Status

The single-player campaign is playable and saves and loads work. This is the 1.0.0 release line of the Switch port, on the `switch` branch.

**What was tested:** the first three levels, on a Switch Lite in handheld mode, with a CPU clock of **1785 MHz**, a GPU clock of **460 MHz** and a RAM clock of **1600 MHz**. Later levels have not been played through yet. **In heavy fights the frame rate drops a lot**, well below 30 FPS in the worst moments.

| Area | State |
| --- | --- |
| Single-player campaign | Playable, saves and loads work |
| Rendering | OpenGL ES 3.0 renderer (Mesa/nouveau), render thread overlapping game logic, NEON-accelerated skeletal model and culling code |
| Frame rate | Around 45 FPS median on the first levels at the clocks above; **heavy fights have strong frame-rate drops** |
| Audio | OpenAL Soft on top of the audio renderer (`audren`), 48 kHz stereo |
| Input | Handheld, Joy-Con and Pro Controller; **gyro aiming**; system on-screen keyboard for text fields (save names, player name) |
| Multiplayer | Not supported (see [Known limitations](#known-limitations)) |

---

## Requirements

- A Nintendo Switch with custom firmware that runs homebrew (for example Atmosphère) and the Homebrew Menu (`hbmenu`).
- **Launch in title takeover mode**: start `hbmenu` by holding **R** while opening a game, not through the Album applet. Applet mode gives the application far too little memory for Quake 4.
- **CPU overclock to 1785 MHz or higher.** The game is CPU-bound, so the stock 1020 MHz is not enough for a comfortable frame rate. Use an overclocking tool such as Horizon OC; the game never changes clocks itself.
- GPU and RAM overclocking is **optional**. The GPU is mostly idle in this port and the stock memory clock works.
- 4 GB of RAM is what the console has; the game uses most of what it is given. Do not run other memory-hungry homebrew in the background.
- An SD card with a few GB free.
- A legitimate copy of Quake 4 (its `q4base` folder).

---

## Installation

The game lives in `sdmc:/switch/openQ4/`:

```text
sdmc:/switch/openQ4/
├── openQ4.nro            the application
├── q4base/               your retail Quake 4 data (pak001.pk4 ... pak0xx.pk4 and the rest of the folder)
└── baseoq4/              openQ4's own content packs (pak0.pk4, pak1.pk4, pak2.pk4, ...)
```

1. Copy `openQ4.nro` to `sdmc:/switch/openQ4/`.
2. Copy the `q4base` folder from your Quake 4 installation next to it.
3. Copy the openQ4 content packs into `baseoq4/` (they are built from `content/baseoq4`, see [Building from source](#building-from-source)).
4. Start `hbmenu` in title takeover mode and launch **openQ4**.

Settings, saves and the log are written inside `baseoq4/` and next to the application (`log.txt`). If something goes wrong, `log.txt` is the file to attach to a bug report.

---

## Playing

### Controls

The sticks move and look, and the triggers and buttons follow the standard Quake 4 controller layout. Every binding can be checked and changed in **Settings -> Controls**.

### Gyro aiming

Gyro aiming adds the motion of the console (or of the connected controller) to the stick look, so you can fine-tune your aim by turning the console. The sticks keep working at the same time.

1. Open **Settings -> Game Options -> Controller**.
2. Switch **Gyro aiming** on.
3. Adjust **Gyro sensitivity** to taste.

It only applies during gameplay, never in menus.

### On-screen keyboard

When a text field needs input (for example the name of a save game) the system software keyboard opens. Type the name, confirm, and the field is confirmed as well.

### Settings worth knowing

- **Game Options -> Controller**: stick dead zone, look sensitivity, gyro aiming and sensitivity, rumble.
- **System**: resolution and quality options. The defaults are tuned for the Switch.

---

## Known limitations

- **No multiplayer.** Network play and the dedicated server are not part of this port.
- **Closing the game takes a while.** Shutdown is complete and safe, just not instant.
- **Heavy fights drop the frame rate a lot.** Large fights with many enemies and effects can fall well below 30 FPS; exploring is usually well above that. Higher CPU clocks help.
- **Overclocking is up to you.** The application never requests clock changes. A CPU clock of 1785 MHz or more is needed.
- Features that need a PC (VR, external editors, level compilers' GUI tools) are not included.
- Memory is tight: if you see the game close during a very long session, restart it.

---

## Building from source

The Switch build reuses the upstream Meson build as the source of truth for source lists and compiler flags, and re-emits the engine, the OpenGL ES renderer and the single-player game libraries for `aarch64-none-elf`. The three groups are linked into a single NRO.

### Prerequisites

- A Linux machine (the build is developed and tested on Ubuntu).
- [devkitPro](https://devkitpro.org/wiki/Getting_Started) with **devkitA64** (r29 or newer) and **libnx** (4.12 or newer), plus the packages the port links against (`switch-mesa`, `switch-sdl2` dependencies such as `switch-openal-soft` and the usual `switch-*` libraries).
- `meson`, `ninja`, `python3` and a host C/C++ compiler for the generator step.
- Your own Quake 4 data to run it.

### Steps

```bash
# 1. Configure and build the host tree (it generates headers and the compile database
#    that the Switch generator reads).
meson setup build-host --buildtype release -Dplatform_backend=sdl3 -Dbuild_renderer_gles=enabled
ninja -C build-host

# 2. Generate the devkitA64 build from it.
python3 tools/switch/gen_ninja.py --host-build build-host --out build-switch

# 3. Build the NRO.
ninja -C build-switch        # produces build-switch/openQ4.nro
```

The content packs (`baseoq4/pak*.pk4`) are built by the Meson tree from `content/baseoq4`.

### Development diagnostics

Release builds contain no diagnostics. For performance and audio bring-up there are optional tools behind the `OPENQ4_SWITCH_DIAG` compile flag (an on-device sampling profiler, audio test tones and dumps, per-frame timing lines). Offline analysis scripts are in `tools/switch/`.

---

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Engine, renderer, single-player game, UI |
| `src/sys/switch/` | Switch platform layer: libnx services, SDL3 video, joystick (with gyro), audio and libc shims |
| `src/renderer/GLES/`, `src/renderer/GLES_D3/` | OpenGL ES renderer and the render thread |
| `tools/switch/` | NRO generator and analysis scripts |
| `content/baseoq4/` | openQ4 content that becomes `baseoq4/pak*.pk4` |
| `LICENSES/`, `LICENSING.md` | Licence texts and the licensing overview |

---

## Credits

- **id Software** and **Raven Software** for Quake 4, and for releasing the engine and SDK sources.
- **[themuffinator](https://github.com/themuffinator) and the openQ4 contributors** for the openQ4 engine and game libraries this port is built on.
- **devkitPro**, **libnx**, **switch-mesa** (nouveau) and the wider Switch homebrew community.
- **SDL**, **OpenAL Soft**, **stb_vorbis**, **libjpeg** and the other third-party libraries listed in [LICENSES](LICENSES/).

---

## Licence

openQ4 contains components under different licences. The engine is covered by the GNU GPL version 3; the SDK-derived game libraries are under the Quake 4 SDK Limited Use License Agreement. See [LICENSING.md](LICENSING.md) for the full overview. Quake 4 game data is not included and is not licensed by this repository.

---

## Кратко по-русски

**openQ4 для Nintendo Switch** это порт движка openQ4 и одиночной игры Quake 4 на Switch (homebrew). Игровые данные в репозитории **не** лежат: нужна своя лицензионная копия Quake 4.

**Установка:** скопируйте `openQ4.nro` в `sdmc:/switch/openQ4/`, рядом положите папку `q4base` из вашей копии игры и контент-паки openQ4 в `baseoq4/`. Запускайте `hbmenu` в режиме перехвата игры (удерживая **R** при запуске игры), а не через альбом.

**Что есть:** одиночная кампания, отрисовка на OpenGL ES, звук через `audren`, управление геймпадом, **гироскоп для прицеливания** (Настройки -> Game Options -> Controller), экранная клавиатура системы для ввода имён сохранений.

**Чего нет:** сетевой игры (мультиплеера) и выделенного сервера.

**Требования к разгону:** нужен разгон процессора до **1785 МГц и выше**; видеокарту и память разгонять необязательно. **Тестировалось:** первые 3 уровня на частотах 1785 / 460 / 1600 МГц (CPU / GPU / RAM). **В тяжёлых боях сильные просадки FPS.**

**Известные особенности:** выход из игры занимает некоторое время.

**Сборка:** см. раздел [Building from source](#building-from-source) (devkitPro, meson, `tools/switch/gen_ninja.py`).
