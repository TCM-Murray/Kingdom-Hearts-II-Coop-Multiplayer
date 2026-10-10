# Kingdom Hearts II Co-op Multiplayer

Online two-player co-op for Kingdom Hearts II Final Mix (PC, Steam). Each player is Sora in their own game, and the other player appears as a second Sora in the party. The host's game owns rooms and enemies; the second game follows it.

**Status:** in development. Tested between two PCs in Hollow Bastion and Olympus Coliseum (up to and including the Cerberus fight), and in a story run from Sora's start through Twilight Town up to arriving at Hollow Bastion, inside Merlin's House (2026-10-10). 

Since 2026-10-10, co-op has its own save file (see Saves below): saving, quitting, and reloading a co-op game works on both PCs. On the same day, the second player can connect through Steam (Join Game or Invite to Game from the Steam overlay, or a typed Steam ID); Radmin VPN or a local network still works as a backup. Not ready for public play: there's no installer, and the mod needs motion files built from your own game files (`tools/make_mset.py`).

**Progress: about 25–30%** toward the goal in `TODO.md`, which is two players finishing the whole story together on two PCs (estimate from 2026-10-08, session 18b, not re-estimated since; item counts below updated 2026-10-11, session 25).

| `TODO.md` items | Done | Built, not tested | Open |
|---|---|---|---|
| P0 (blocks the start-to-finish run) | 12 | 1 | 25 |
| P1 (needed to play well) | 15 | 1 | 40 |
| P2 (polish) | 2 | 0 | 13 |

- **Done:** the parts with the most risk. These are the two-PC link (over Steam or Radmin), the second Sora in the party, shared enemies and hits, following the host's rooms, loading screens that fade in together, the downed state, shared Game Over, a shared pause, the other Sora in the party HUD and in story cutscenes, the two Soras stepping apart after a load, cutscenes, story scenes started by walking into an area (either player can start them), story fights that end at the same time in both games, a real boss fight (Cerberus), co-op saves kept apart from single-player saves, and Steam achievements blocked during co-op.
- **Left:** story progress sync, the world map and Gummi Ship, bosses with phases, missions and timers, forced party changes and world costumes, a separate inventory for each Sora, disconnect and rejoin, and the dev/test tools. No world has been played start to finish in co-op yet.
- **Built, waiting for an in-game OK:** the personal settings file `kh2coop_player.ini` (TODO 9.7).

- `TODO.md`: the plan and what's left.
- `MODLOG.md`: the development diary, with evidence.
- `VERIFIED_OFFSETS.md`: addresses, structures, and game rules found on the Steam build.
- `MODDING_PLAN.md`: the original plan and design decisions.

## Saves
The title screen gets a **LOAD (CO-OP)** row under LOAD.

![The title screen with the LOAD (CO-OP) row](docs/images/title-load-coop.png)

- **LOAD (CO-OP)** loads and saves the co-op save file, `KH2 Coop saves\KHIIFM_WW.png` in the game folder (next to the game's exe), and turns co-op on. The first time, the file starts as a copy of your normal saves.
- **NEW GAME** and **LOAD** use your normal Steam saves. The mod stays off, and the game plays as plain KH2.
- Co-op saves never contain the second Sora, so they load like any other save. Playing in co-op never writes your normal saves.

## Playing a session
There's no installer or player guide yet; this is how a test session works today.

**Requirements (both PCs):**
- Kingdom Hearts II Final Mix on Steam (the PC release).
- OpenKH Mods Manager with Panacea, and the mod from `openkh_mod/` installed in it.
- The motion files, built on each PC from that PC's own game files with `tools/make_mset.py`.

**Steps:**
1. In Mods Manager, put KH2 Coop at the top of the list (above texture or voice mods), then Build. Otherwise, those mods cover the LOAD (CO-OP) label.
2. Start the game through Steam or Mods Manager, not by running the game's .exe directly: the mod uses the game's own Steam connection, which only starts that way.
3. Both players pick **LOAD (CO-OP)** on the title screen and load a save.
4. The second player opens the Steam overlay and clicks **Join Game** on the host's profile (or accepts the host's **Invite to Game**). Only the first Steam friend who joins gets in.

The host starts the game with `start_host.bat`, which sets the host's side (`CONNECT=steam` at the top, or `CONNECT=local` for Radmin/LAN). The second player sets the connection in `kh2coop.ini`: `PEER=steam` (Join Game/invites), `PEER=steam:<SteamID64>` (connect straight to that account), or `PEER=<host Radmin IP>:27701` with `LISTEN=27702` for Radmin/LAN.

## Known issues and limits
- **Steam achievements are blocked by default** while co-op is on for testing. Set `ACHIEVEMENTS=1` in `kh2coop_player.ini` to allow them on that PC. With co-op off (normal LOAD or NEW GAME), they work as usual.
- Closing the game with Alt+F4 after a Steam session once logged an error during exit. It did not affect play.
- Biggest open bugs: Shadows that go underground sometimes can't be hit on the second player's screen after they re-emerge (TODO 1.2); Drive Forms aren't shown on the other Sora (1.24); enemy HP bars on the second player's screen don't always update (1.21).
- No world has been played start to finish in co-op yet, and the world map, Gummi Ship, minigames, and forced party changes aren't handled.

## Steam Connection and Gameplay Showcase

https://github.com/user-attachments/assets/7729c35f-9265-43c8-bd39-6e0143e6bd12

The video shows the mod's current progress as of October 11, 2026.

The demo uses two PCs, each running its own game: Host on the left, Client on the right. The client connects directly through Steam servers by opening the Steam Overlay and clicking "Join Game" on the Host's Steam profile. The video shows combat and the downed/revival animation.

Higher quality video here: https://www.youtube.com/watch?v=IJ1MecbHULk

## Cinematics

https://github.com/user-attachments/assets/e788f2e3-3344-443e-82d8-c7cf5cf82415

The video shows the mod's current progress in cinematics as of October 11, 2026.

The demo uses one PC with 2 game copies: Host on the left, Client on the right. Cinematics work by inserting a "Twin Sora" in every cinematic present in the game that contains Sora. The twin will follow Sora's movements and animations, and will remain around the original Sora all the time. There's an option to disable Twin Sora in animations if required/preferred. 


## Layout
- `src/dll/`: the C++ DLL. OpenKH Panacea loads it from the OpenKH mod in `openkh_mod/`. Hooks use MinHook.
- `tools/`: Python helpers for building, launching test copies, packaging, and research.
- Settings: `kh2coop.ini` (shared, must match on both PCs; also holds the connection, `PEER`) and `kh2coop_player.ini` (personal, written by the DLL with defaults on first start).
- `start_host.bat`: starts the host's game; `CONNECT=steam` or `CONNECT=local` (Radmin/LAN) picks how the second player connects.

## License
Copyright (C) 2026 TCM-Murray

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see [`LICENSE`](LICENSE) for details. SPDX: `GPL-3.0-or-later`.

Third-party code: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause), downloaded at build time.

## Credits
- **[Volpestyle/kh2-multiplayer](https://github.com/Volpestyle/kh2-multiplayer)** (GPL-3.0-or-later): this project uses their documentation and research (pointer maps, hook points, input, enemy and save-guard notes) as leads; every address taken from it is re-checked on our build before use (`VERIFIED_OFFSETS.md`).
- **[OpenKH](https://github.com/OpenKH/OpenKh)**: Panacea loads the mod's DLL, and its documentation and file-format code (e.g., the event script layout) are used as references.
- **[Expert595/kh2-multiplayer](https://github.com/Expert595/kh2-multiplayer)** and the KH2 Lua library: a few addresses used as leads.
- **[Julumisan/kh-pc-save-transfer](https://github.com/Julumisan/kh-pc-save-transfer)** (MIT): recommended to the second player for moving the host's save between Steam accounts; not included in this repository.

Kingdom Hearts and Kingdom Hearts II are the property of Square Enix and Disney. This is an unofficial fan project, not affiliated with or endorsed by them. The repository contains no game files; you need your own copy of the game.

Developed with the help of Claude (Anthropic).
