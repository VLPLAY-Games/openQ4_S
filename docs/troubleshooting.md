# Troubleshooting

## What to attach to a bug report

- `sdmc:/switch/openQ4/log.txt` (as a file).
- For crashes, the newest file in `sdmc:/atmosphere/crash_reports/`.
- Console model, play mode, CPU/GPU/RAM clocks, firmware and CFW version.
- Where in the game it happened.

## Common problems

**The game closes right after the Homebrew Menu starts it.**
It was launched through the Album. Start the Homebrew Menu by holding R while
launching a game (title takeover).

**Errors about missing files at start.**
`q4base/` is missing or incomplete, or `baseoq4/` has no `pak*.pk4`. Check the
layout in [installation.md](installation.md).

**Low frame rate or stutter.**
Raise the CPU clock to 1785 MHz or more. See [performance.md](performance.md).

**Gyro aiming does nothing.**
Switch it on in Settings -> Game Options -> Controller. It only applies during
gameplay, not in menus. If horizontal turning is mirrored, report the console model.

**The save-name keyboard does not open.**
The system keyboard is an applet and is supported in title takeover mode, which is
the supported way to launch the game.

**Closing the game takes a while.**
Shutdown is complete but not instant. Wait for the return to the Homebrew Menu or
home screen.

**The game ends during a very long session.**
Memory is tight. Restart the game; saves are not affected.
