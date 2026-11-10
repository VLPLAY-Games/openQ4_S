# Roadmap

What is planned after the 1.0.0 release of the Switch port. Items are not
promises or dates; they are listed roughly in the order they are expected to be
tackled.

## Multiplayer (LAN first, then online)

**Today:** not supported. The Switch build links only the single-player game
module (`game_sp`). Starting or joining a multiplayer game, and the bot matches
of the Arena campaign, need the multiplayer module (`game_mp`), so the game
refuses them with a message instead of failing to load a module. The network
code of the engine is still compiled in but has not been tested on the Switch.

**Plan:**

1. **Build and link `game_mp`** as a second static group next to the
   single-player game, and let the module loader choose between them. Check the
   additional memory use and the extra start-up cost.
2. **LAN play.** Join a server on the local network (a PC host first), then
   check hosting a listen server on the console. LAN is the first target because
   it is independent of internet conditions and is the easiest place to measure
   performance with several players.
3. **Menus and input.** Server browser, create and join screens on a gamepad,
   address entry with the system keyboard (already available).
4. **Online play.** Joining internet servers: network setup on the console
   (`nifm`), NAT and port handling, and which servers the openQ4 protocol can
   actually talk to.
5. **Arena campaign.** The bot matches run on the multiplayer module, so they
   follow the same work.

**Open risks:** frame rate (the game is CPU-bound and heavy single-player fights
already drop well below 30 FPS), memory (the application has about 3 GB), and
protocol compatibility with existing servers. A dedicated server cannot run on
the Switch.

## Performance

- Strong frame-rate drops in heavy fights: the cost is in game logic (clip-model
  queries and traces, AI), effect updates and waiting for the render thread.
- Closing the game takes a while.
- Long sessions: memory is tight (1.5 to 1.8 GB of heap on the early levels).

## Testing

- Only the first three levels have been played on hardware, on a Switch Lite in
  handheld mode. Later levels, docked mode and other controllers need testing.
- Verify the continuous-integration build workflow on GitHub (`switch-build.yml`).

## Housekeeping

- Remove or rework the code kept for the multiplayer game tree if multiplayer is
  not pursued.
- Keep user documentation in `docs/` up to date with each release.
