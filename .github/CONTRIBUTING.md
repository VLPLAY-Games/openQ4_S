# Contributing to openQ4 for Nintendo Switch

Thanks for your interest in the Switch port. Bug reports, performance reports,
testing feedback and code contributions are welcome.

This repository contains no Quake 4 game data and never will. Do not attach
retail files to issues or pull requests.

## Reporting bugs

Use the **Switch bug or performance report** issue template. A useful report has:

- the openQ4 version (release tag or commit),
- the console model, play mode and the CPU / GPU / RAM clocks you used (the CPU
  needs 1785 MHz or higher),
- `sdmc:/switch/openQ4/log.txt` as a file, not a screenshot,
- for crashes, the newest file from `sdmc:/atmosphere/crash_reports/`,
- where in the game it happened and how to reproduce it.

## Building

See [Building from source](../README.md#building-from-source). You need
devkitPro (devkitA64 and libnx), Meson, Ninja and Python 3 on Linux.

Release builds contain no diagnostics. For profiling or audio bring-up, build
with `OPENQ4_SWITCH_DIAG` defined; the analysis scripts are in `tools/switch/`.

## Code changes

- Work on the `switch` branch and open the pull request against it.
- Keep a change focused, and describe what it does and how you tested it on
  hardware. For performance work, give before and after frame times from the
  same route in the game.
- Match the surrounding code: naming, comment density and idiom.
- Do not add dependencies without discussing them first.
- Code that is not used on the Switch should not be added back; this fork
  targets one platform.

## Licensing and attribution

The engine is under the GNU GPL version 3 and the SDK-derived game libraries are
under the Quake 4 SDK Limited Use License Agreement. Read
[LICENSING.md](../LICENSING.md) before contributing. By submitting a change you
agree that it is provided under the licence of the files it modifies. Keep
existing copyright notices intact.
