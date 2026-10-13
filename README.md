# Kingdom Hearts II Co-op Multiplayer

Online two-player co-op for Kingdom Hearts II Final Mix (PC, Steam). Each player is Sora in their own game, and the other player appears as a second Sora in the party. The host's game owns rooms and enemies; the second game follows it.

**Status:** in development. Tested between two PCs, over Steam and over Radmin VPN. Current work is on the `extra-copy` branch: there the second Sora is spawned **beside** Donald and Goofy (or the world's ally, such as Riku), while on `main` it takes Donald's or Goofy's place.

## Route checklist: Sora's story, world by world
Fights and special parts are written **from memory**. I'll check each one against the game's own mission files with the catalog tool (2.3) before we work on that world.
Status: ✅ played in co-op · 🟡 partly · ⬜ not tried. "Played" means on two PCs. Since session 28, most worlds are reached by loading a co-op save near the fight, not by playing the story up to it.

| # | Part | Required fights and special parts | Co-op plan | Status |
|---|---|---|---|---|
| 1 | Twilight Town (Sora wakes up) and the Mysterious Tower | first Nobody/Heartless fights, the tower climb, Yen Sid's events | together | ✅ played in co-op (story runs, sessions 20 and 24, on to Hollow Bastion); the return visit is row 14 |
| 2 | Hollow Bastion, 1st visit | Bailey Nobodies | together | 🟡 our test area (Borough, Merlin's House; also played with the extra copy, session 28); Bailey fought in co-op on two PCs (forced-solo fight, sessions 26 and 27; open: 1.47, 5.5a); after Bailey not yet |
| 3 | Land of Dragons 1 | missions with Mulan (morale gauge), Shan-Yu | together; gauge mirrored (1.22) | 🟡 a few rooms and Shan-Yu fought together (session 29); open: the wind gusts only show in the host's game (1.53), Shan-Yu's 'Takedown' reaction command shows in the wrong spot on the other screen (1.26); the morale gauge missions not tried |
| 4 | Beast's Castle 1 | Thresholder and Possessor, Shadow Stalker / Dark Thorn | together | 🟡 Shadow Stalker and Dark Thorn (Ballroom) fought together (session 29); open: an enemy missing and the boss out of step on the second player's screen (1.54); Thresholder and Possessor not tried |
| 5 | Olympus Coliseum 1 | Cerberus, Hades escape (timed), Pete, Hydra | together | 🟡 Cerberus beaten together (session 13); Auron's leaving synced; host's Cerberus RC no crash (session 18b, animations off, 1.26) |
| 6 | Disney Castle and Timeless River | protecting Minnie, the Windows of Time, Pete; retro costume | together | ⬜ (bench: the extra copy gets the retro look, see below) |
| 7 | Port Royal 1 | cursed pirates (moonlight rule), Barbossa | together | ⬜ |
| 8 | Agrabah 1 | Abu escort, carpet ride, Volcanic Lord and Blizzard Lord | together; carpet ride: together if feasible, else solo each (Q9) | 🟡 Volcanic Lord and Blizzard Lord fought together (session 29), and the end of the visit on to the World Map (2026-10-12); Abu escort and carpet ride not tried |
| 9 | Halloween Town 1 | costume, Prison Keeper, Oogie Boogie | together | ⬜ (bench: the extra copy wears the costume) |
| 10 | Hollow Bastion 2 and Space Paranoids 1 | Demyx (clones, timer), Hostile Program, **Light Cycle** minigame, the 1000 Heartless battle | together; Light Cycle: together if feasible, else solo each (Q9) | ⬜ |
| 11 | Second visits: Land of Dragons, Beast's Castle, Olympus, Port Royal, Agrabah, Halloween Town / Christmas Town | Storm Rider, Xaldin (Beast's Jump RC), Hades, Grim Reaper ×2 (medallion), Genie Jafar, Lock/Shock/Barrel, The Experiment | together | ⬜ |
| 12 | Pride Lands 1 and 2 | Sora is a lion (different body), Scar, Groundshaker (riding Simba) | together if the lion copy works (5.6) | ⬜ (bench: the extra copy is a lion) |
| 13 | Space Paranoids 2 | Sark and the MCP | together | ⬜ |
| 14 | Twilight Town again, Betwixt and Between | Nobody fights with Axel | together | ⬜ |
| 15 | The World That Never Was | Roxas (Final Mix), Xigbar, Luxord (time gauge instead of HP), Saïx (Berserk RC), Riku joins (1.30) | together | ⬜ |
| 16 | Final fights | Xemnas, Armored Xemnas, Dragon Xemnas (ship shooting stage), Final Xemnas (Sora and Riku, reaction commands) | together; ship stage: together if feasible, else solo each (Q9) | 🟡 played together from the last save point (session 28): every room in step, fights end together. Open: the ship fight gets stuck on the second player's side (1.50); Armored Xemnas II is out of step there (1.49); Final Xemnas isn't finished yet (1.30, see Known issues) |
| — | Between worlds | world map, Gummi Ship routes (each needed once) | host picks the world; Gummi together if feasible, else solo each (Q9) | 🟡 World Map: the host picks the world and the second player follows, loads in step (two PCs, 2026-10-12: Hollow Bastion and the Land of Dragons); Gummi routes not tried |

**Bench only** (two game copies on one PC, session 28): with the extra copy, all 62 saves in the co-op save file load in both games, and the second Sora takes each world's own form: the lion in Pride Lands, the retro look in Timeless River, the costumes in Halloween Town, Christmas Town and Space Paranoids. This isn't counted as played until it's tried on two PCs.

**Progress: about 35–40%** toward the goal in `TODO.md`, which is two players finishing the whole story together on two PCs (re-estimated 2026-10-12, session 29; it was 25–30% on 2026-10-08, session 18b). Item counts below are from the same day, sub-items included.

How the estimate was made: counting P0 items three times, P1 twice, P2 once, and a built-but-untested item as half done, about a third of the `TODO.md` work is done now, against about a fifth on 2026-10-08. The estimate sits a bit above that, because the riskiest systems are done (link, saves, Steam, disconnects, cutscenes, fights ending together), and boss fights in five worlds plus the final fights have been played together. It isn't higher, because story-progress sync and the world map haven't been started, Final Xemnas can't be finished yet, and every new world played so far has turned up new problems.

| `TODO.md` items | Done | Built, not tested | Open |
|---|---|---|---|
| P0 (blocks the start-to-finish run) | 15 | 3 | 21 |
| P1 (needed to play well) | 17 | 3 | 51 |
| P2 (polish) | 5 | 0 | 18 |

The open count went up since session 27 because the two-PC runs of sessions 28 and 29 (the final fights, Land of Dragons, Beast's Castle, Agrabah) found new problems (1.49 to 1.54, 5.20), and the world mirror plan was added (1.55).

- **Done:** the parts with the most risk. These are the two-PC link (over Steam or Local), the second Sora in the party (on `extra-copy`, beside Donald and Goofy), shared enemies and hits, following the host's rooms (also through the World Map), loading screens that fade in together, the other player's spells shown on their Sora with the game's own particles and sounds, the downed state, shared Game Over, a shared pause, the other Sora in the party HUD and in story cutscenes, the two Soras stepping apart after a load, cutscenes, story scenes started by walking into an area (either player can start them), story fights that end at the same time in both games, fights where Sora must be alone (both players fight, no companions), the host's Skip Scene and a shared cutscene pause, boss fights played together (Cerberus, Bailey, Shan-Yu, Shadow Stalker and Dark Thorn, Volcanic Lord and Blizzard Lord, the final fights up to Final Xemnas; some with open issues), co-op saves kept apart from single-player saves, Steam achievements blocked during co-op, and disconnects: a 'Connection lost' screen with Continue Alone and Title Screen, and rejoining.
- **Left:** story progress sync, the Gummi Ship, bosses with phases and objects only the host's game shows, missions and timers, world costumes on two PCs, Drive Forms shown on the other Sora, a separate inventory for each Sora, disconnect edge cases during boss fights, and the dev/test tools. Apart from the Twilight Town opening, no world has been played start to finish in co-op yet, and the ending hasn't been reached.
- **Built, waiting for an in-game OK:** the personal settings file `kh2coop_player.ini` (TODO 9.7) and EXP from Drive Forms (4.5). Forced party changes (5.5) are half done: fights where Sora is alone work; with the extra copy, Riku and the second Sora are both in Final Xemnas (bench and two PCs), and Pride Lands works on the bench only. Bench-tested only: summons with the extra copy (1.23), late network packets no longer making bosses vanish under lag (6.3), and 'Title Screen' waiting for a room change in progress (6.11).
- **In progress:** the other player's Limits and reaction commands show on their Sora in your game, motions only, no particles yet (1.31, seen on two PCs on 2026-10-12). When the second player uses a Limit, the partner (e.g. Mulan) stays with the host in the host's game (1.57). Also a "world mirror" experiment, off by default (1.55): the second player's bosses follow the host's game instead of running their own AI, which should fix many of the open sync problems at once.

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

The host starts the game with `start_host.bat`, which sets the host's side (`CONNECT=local` at the top for Radmin VPN or LAN, or `CONNECT=steam`). With Local, step 4 isn't needed: the second player's game connects to the host's address by itself. On `extra-copy`, `start_host.bat` also turns the extra copy on (`KH2COOP_EXTRA_COPY=1`), and the second player's `kh2coop.ini` must say `EXTRA_COPY=1` too; if the two differ, the log says `SETTINGS DIFFER`.

The second player sets the connection in `kh2coop.ini` with `PEER=steam` or `PEER=local` (LAN). The host's addresses sit on their own lines, so switching is one word:

```ini
PEER=steam
; the host's SteamID64 (connect straight to that account); empty = wait for Join Game/invites
STEAM_ID=<SteamID64>
; the host's Local/LAN IP (port 27701 unless written as ip:port)
LOCAL_IP=<host Local IP>
LISTEN=27702
```

The older one-line forms (`PEER=steam:<SteamID64>`, `PEER=<host Local IP>:27701`) still work.

**If the connection drops** (a game closes or crashes, or the network goes): after 3 seconds the other game pauses on a **Connection lost. Reconnecting to Host...** (or **...to Client...**) screen. Leave it open to wait; when the other player starts their game again and loads LOAD (CO-OP), co-op resumes by itself. Or pick:
- **Continue Alone**: the room reloads without the other Sora (on `main`, Donald or Goofy comes back in its place). A fight or boss fight starts over. When the other player returns, the room reloads again with their Sora.
- **Title Screen**: back to the title screen through the game's own soft reset.

## Known issues and limits
- **Steam achievements are blocked by default** while co-op is on for testing. Set `ACHIEVEMENTS=1` in `kh2coop_player.ini` to allow them on that PC. With co-op off (normal LOAD or NEW GAME), they work as usual.
- Closing the game with Alt+F4 after a Steam session once logged an error during exit. It did not affect play.
- **Final Xemnas can't be finished yet** (TODO 1.30). Without the extra copy, both games crash when Xemnas grabs a Sora, because Riku is missing. With the extra copy, Riku and the second Sora are both there, but Xemnas's special moves (the grab, the desperation attack) aren't shared: each game starts them on its own, so in one two-PC run the host got a Game Over alone. On the bench, the host's game also sometimes runs out of effect memory in this fight and crashes.
- Final fights: in the ship fight, the second player's game gets stuck in its own part of the fight until the host's part ends (1.50); in Armored Xemnas II, the boss sinks into the ground on the second player's screen (1.49).
- Some things only show in the host's game: the wind gusts in the Land of Dragons and the flying buildings in The World That Never Was (1.53). In Beast's Castle, an enemy can be missing on the second player's screen (1.54).
- Swapping a companion through the Party command isn't copied to the second player's game (1.51), and in that game hits by others (companions, the host's Sora) can count twice (1.40).
- Older open bugs: Shadows that go underground sometimes can't be hit on the second player's screen after they re-emerge (1.2); Drive Forms aren't shown on the other Sora (1.24); enemy HP bars on the second player's screen don't always update (1.21); after one player uses the Samurai's reaction command, the other can still use it and then freezes (1.47).
- The Gummi Ship and minigames aren't handled yet. The World Map works: the host picks the world and the second player follows.

## Steam Connection and Gameplay Showcase

https://github.com/user-attachments/assets/7729c35f-9265-43c8-bd39-6e0143e6bd12

The video shows the mod's current progress as of October 11, 2026.

The demo uses two PCs, each running its own game: Host on the left, Client on the right. The client connects directly through Steam servers by opening the Steam Overlay and clicking "Join Game" on the Host's Steam profile. The video shows combat and the downed/revival animation.

Higher quality video here: https://www.youtube.com/watch?v=IJ1MecbHULk

## Cinematics

https://github.com/user-attachments/assets/e788f2e3-3344-443e-82d8-c7cf5cf82415

The video shows the mod's current progress in cinematics as of October 11, 2026.

The demo uses one PC with 2 game copies: Host on the left, Client on the right. Cinematics work by inserting a "Twin Sora" in every cinematic present in the game that contains Sora. The twin will follow Sora's movements and animations and will remain around the original Sora. Close-up shots show only one Sora. There's an option to disable Twin Sora in cutscenes if required/preferred (`CUTSCENE_TWIN=0` in `kh2coop_player.ini`). 


## Layout
- `src/dll/`: the C++ DLL. OpenKH Panacea loads it from the OpenKH mod in `openkh_mod/`. Hooks use MinHook.
- `tools/`: Python helpers for building, launching test copies, packaging, and research.
- Settings: `kh2coop.ini` (shared, must match on both PCs; also holds the connection, `PEER`) and `kh2coop_player.ini` (personal, written by the DLL with defaults on first start).
- `start_host.bat`: starts the host's game; `CONNECT=steam` or `CONNECT=local` (Local/LAN) picks how the second player connects.

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
