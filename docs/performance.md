# Performance

## What to expect

| Situation | Frame rate |
| --- | --- |
| Exploring the early levels | around 45 FPS median (about 22 ms per frame) |
| Heavy fights with many enemies and effects | strong drops, well below 30 FPS in the worst moments |

Measured on a Switch Lite in handheld mode, CPU 1785 MHz, GPU 460 MHz, RAM
1600 MHz, over the first three levels. Later levels have not been played through.

## Why it is CPU-bound

The GPU is mostly idle: the render thread spends most of a heavy frame waiting.
The time goes to the main thread:

- game logic and physics (clip-model queries and traces),
- AI and animation,
- preparing the frame (model and effect surfaces),
- waiting for the render thread at the frame boundary.

That is why a higher CPU clock helps and a higher GPU or RAM clock helps little.

## Settings that help

- Keep the CPU at 1785 MHz or more.
- `r_glesMaxLights` limits the lights drawn per view (24 by default). Lower it in
  heavy scenes.
- `bse_scale` scales particle effects (0.6 by default on the Switch).
- Lower resolution or quality options in **Settings -> System** reduce GPU load;
  they matter less than the CPU clock.

## Measuring

Build with `--diag` (see [BUILDING.md](../BUILDING.md)). The `log.txt` then has a
timing line per frame, and the sampling profiler (`profile.txt`) can be read with
`tools/switch/analyze_prof.py` using the ELF of the same build.
