# Roadmap

## The Awakening

The Switch port is based on openQ4 0.14.1. The Awakening appears in the
campaign menu when compatible `q4xbase` data is installed. The first map
loads on Switch Lite, but the expansion is currently unplayable. The
intro has animation defects and gameplay immediately after it can drop
to around 5 FPS.

- Capture frame timings after the first cutscene hands control to the player.
- Diagnose the intro animation defects, including characters in T-poses.
- After fixing the early gameplay issues, test all 13 maps, saves and loads,
  compatibility when returning to the base campaign, and memory in later fights.

## Multiplayer

Local games with bots, LAN discovery and direct connections are available.
Switching between the campaign and multiplayer still restarts the application.

- Test more Switch-to-PC and Switch-to-Switch games, including internet play,
  server compatibility and performance with several players.
- Test the Arena campaign. Pure mode, dedicated servers and microphone input
  remain unsupported on Switch.

## Performance and testing

- Settings transitions and the weapon wheel are lighter in 1.2.0. The Switch
  uses the classic crosshair after profiling found a substantial slowdown in
  heavy fights from the animated one.
- Profile combat and objective popups. Collision queries, effects and render
  work can still cause heavy drops. Investigate memory over long sessions and
  the time it takes to exit the game.
- Test later campaign levels, docked mode, Joy-Con and Pro Controller, and
  verify the GitHub Switch build workflow.

## Housekeeping

The initial platform cleanup is complete. Removing the remaining VR code is
a separate task because it touches gameplay, rendering and save compatibility.
