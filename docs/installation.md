# Installation

You need a Nintendo Switch that runs homebrew (for example with Atmosphère), the
Homebrew Menu, and your own copy of Quake 4. This project does not provide game
data.

## 1. SD card layout

```text
sdmc:/switch/openQ4/
├── openQ4.nro
├── q4base/        your Quake 4 data
└── baseoq4/       openQ4's content packs
```

- `openQ4.nro`: from a release, or built following [BUILDING.md](../BUILDING.md).
- `q4base/`: copy the whole `q4base` folder of your Quake 4 installation (Steam or
  GOG). The `.pk4` files are what the game needs; the folder structure must be
  kept.
- `baseoq4/`: the openQ4 content packs `pak0.pk4`, `pak1.pk4` and `pak2.pk4`
  (from a release, or `build-host/baseoq4/` after building).

## 2. Start it in title takeover mode

Open the Homebrew Menu by **holding R while launching a game** from the console's
home screen, then pick **openQ4**. Do not start it through the Album: that mode
leaves the application with far too little memory.

## 3. Clocks

The game is limited by the CPU. Set the CPU to **1785 MHz or higher** with an
overclocking tool (for example Horizon OC) before starting. The GPU and RAM can
stay at their defaults; the first levels were tested with 1785 MHz CPU, 460 MHz
GPU and 1600 MHz RAM. The game never changes clocks itself.

## 4. First run

The first start prepares caches and takes longer. Settings are saved in
`baseoq4/openQ4Config.cfg`; saved games and logs are written under `baseoq4/`
and next to the NRO (`log.txt`).

## Controls and options

- **Settings -> Controls**: bindings.
- **Settings -> Game Options -> Controller**: stick dead zone and sensitivity,
  gyro aiming and its sensitivity, rumble.
- Saving a game opens the system keyboard for the save name.

## Updating

Replace `openQ4.nro` (and the content packs if the release changed them). Your
settings and saves stay in place.
