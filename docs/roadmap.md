# Roadmap

What is planned after the 1.1.1 release of the Switch port. Items are not
promises or dates; they are listed roughly in the order they are expected to be
tackled.

## Multiplayer (LAN first, then online)

**Today:** local multiplayer with bots works. The Switch build links both game
modules (`game_sp` and `game_mp`). Creating a server from the Multiplayer menu
restarts the application into the multiplayer module (switching modules inside one
process left the renderer and the statically linked modules in a broken state), and
coming back to the campaign restarts it again. Joining servers and the Arena
campaign are not available yet; joining is refused with a message.

**Plan:**

1. **Done:** `game_mp` is linked as a second static group, and the application
   restarts into the module a command needs.
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

## The Awakening and the move to openQ4 0.14.1

The main developer released openQ4 **0.14.1** (11 November 2026) with support for
**Quake 4: The Awakening**, the single-player expansion that Raven Software and
Ritual Entertainment never released (its alpha was recovered and published by
Justin Marshall; you supply its files, in a `q4xbase` folder, and openQ4 plays it
on its own game code). It also has a new interface, shadow mapping, an official
GLES renderer, smarter bots and many fixes.

**Today:** our base is an upstream snapshot of 9 November, which already builds
the Awakening game code into `game_sp` (the Pain Lord, Valkaryne, Tank and Retch,
the goob, freeze and spike guns, the speeder bike) and has the campaign choice and
`fs_awakeningpath`. None of it has been tried on a console, and 0.14.1 is 120
commits and about 300 files ahead, many of them Awakening repairs.

**Plan:**

1. **Merge 0.14.1** into a separate branch and count the conflicts first. Expect
   work in `meson.build` and `tools/switch/gen_ninja.py` (our trimmed build), the
   settings pages (upstream rebuilt them as tabs; we cut rows a console cannot
   use, so `tools/ui` must be re-applied), the content packs (0.14.1 splits them
   into a core pack plus light-grid and loadscreen packs, and the NRO has the MD5
   of `pak0.pk4` built in), and the GLES renderer (upstream now has its own; our
   Forget functions and caches must be kept or replaced).
2. **Try the campaign on a console** with `q4xbase` on the SD card: that it is
   offered, all 13 maps load, saves load, DXT3 textures draw.
3. **Choosing the campaign mounts the expansion and restarts the file system.**
   Make sure that does not repeat the stale-state problems of the module swap
   (black screen), and that it works with our relaunch for multiplayer.
4. **Memory and speed:** the Valkaryne, Tank and the space battles on 4 GB, when
   heavy fights are already below 30 FPS.

## Online: what is not ready

- **Joining a server is refused** (`Connect_f` answers that it is not available
  on the Switch yet). Only a local game with bots works.
- **No local addresses.** Horizon cannot list network interfaces
  (`SIOCGIFCONF`), so there is no LAN discovery and no address to show a host.
  Use `nifm` for the console's own address.
- **No server browser, no address entry and no join screens** designed for a
  gamepad.
- **Pure servers are not possible:** pure mode is forced off, because the build
  does not produce the game pak checksums.
- **A dedicated server cannot run** on the console; update checks are disabled.
- **Switching between the campaign and multiplayer restarts the application**
  (about 20 seconds): modules cannot be swapped inside one process.
- **Not tested:** two consoles, a console with a PC host, voice chat (there is no
  microphone input) and several players for frame rate and memory. The Arena
  campaign has not been tried either.

## Performance

- Strong frame-rate drops in heavy fights: the cost is in game logic (clip-model
  queries and traces, AI), effect updates and waiting for the render thread.
- Closing the game takes a while.
- Long sessions: memory is tight (1.5 to 1.8 GB of heap on the early levels).

## Testing

- Only the first three levels have been played on hardware, on a Switch Lite in
  handheld mode. Later levels, docked mode and other controllers need testing.
- Vibration (hits and shots, through the HID rumble of the Switch joystick driver) works with an 8BitDo Lite 2 on a Switch Lite. Other controllers (Joy-Con pair, Pro Controller) and docked mode have not been tried.
- Verify the continuous-integration build workflow on GitHub (`switch-build.yml`).

## Housekeeping

- Remove or rework the code kept for the multiplayer game tree if multiplayer is
  not pursued.
- Keep user documentation in `docs/` up to date with each release.
