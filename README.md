# Kingdom Hearts II Co-op Multiplayer

Online two-player co-op for Kingdom Hearts II Final Mix (PC, Steam). Each player is Sora in their own game, and the other player appears as a second Sora in the party. The host's game owns rooms and enemies; the second game follows it.

**Status:** in development. Tested between two PCs, over Steam and over Radmin VPN. 

The second Sora is spawned **beside** Donald and Goofy (or the world's ally, such as Riku): the 'extra copy', on `main` since 2026-10-13 and the only way since 2026-10-14 (the older way, in Donald's or Goofy's place, was removed). 

Since 2026-10-14, the second player's enemies copy the host's instead of thinking for themselves ('one brain'). It is the only way since session 35, and it has been played on two PCs through the final fights.

## Route checklist: Sora's story, world by world
Fights and special parts are written **from memory**. A 4 MB effect pool was added (From 516 KB) to prevent out-of-memory crashes with particles.

Status: ✅ played in co-op · 🟡 partly · ⬜ not tried. "Played" means on two PCs. Since session 28, most worlds are reached by loading a co-op save near the fight, not by playing the story up to it.

| # | Part | Required fights and special parts | Co-op plan | Status |
|---|---|---|---|---|
| 1 | Twilight Town (Sora wakes up) and the Mysterious Tower | first Nobody/Heartless fights, the tower climb, Yen Sid's events | together | ✅ played in co-op (story runs, sessions 20 and 24, on to Hollow Bastion) |
| 2 | Hollow Bastion, 1st visit | Bailey Nobodies | together | ✅ played in co-op (story runs, session 36) |
| 3 | Land of Dragons 1 | missions with Mulan (morale gauge), Shan-Yu | together; gauge mirrored (1.22) | 🟡 played in co-op. Story runs. The first morale fight works on two PCs: the second player's game has all the host's enemies and the host's gauge (session 37) |
| 4 | Beast's Castle 1 | Thresholder and Possessor, Shadow Stalker / Dark Thorn | together | 🟡 Shadow Stalker and Dark Thorn (Ballroom) fought together (session 29) and were defeated (session 33) despite many issues; Thresholder and Possessor not tried |
| 5 | Olympus Coliseum 1 | Cerberus, Hades escape (timed), Pete, Hydra | together | 🟡 Cerberus beaten together (session 13); Auron's leaving synced; host's Cerberus RC no crash (session 18b, animations off, 1.26) |
| 6 | Disney Castle and Timeless River | protecting Minnie, the Windows of Time, Pete; retro costume | together | 🟡 The extra copy gets the retro look. |
| 7 | Port Royal 1 | cursed pirates (moonlight rule), Barbossa | together | ⬜ |
| 8 | Agrabah 1 | Abu escort, carpet ride, Volcanic Lord and Blizzard Lord | together; carpet ride: together if feasible, else solo each (Q9) | 🟡 Volcanic Lord and Blizzard Lord fought together (session 29) and defeated (session 33), and the end of the visit on to the World Map (2026-10-12) |
| 9 | Halloween Town 1 | costume, Prison Keeper, Oogie Boogie | together | 🟡 The extra copy wears the costume |
| 10 | Hollow Bastion 2 and Space Paranoids 1 | Demyx (clones, timer), Hostile Program, **Light Cycle** minigame, the 1000 Heartless battle | together; Light Cycle: together if feasible, else solo each (Q9) | ⬜ |
| 11 | Second visits: Land of Dragons, Beast's Castle, Olympus, Port Royal, Agrabah, Halloween Town / Christmas Town | Storm Rider, Xaldin (Beast's Jump RC), Hades, Grim Reaper ×2 (medallion), Genie Jafar, Lock/Shock/Barrel, The Experiment | together | ⬜ |
| 12 | Pride Lands 1 and 2 | Sora is a lion (different body), Scar, Groundshaker (riding Simba) | together if the lion copy works (5.6) | 🟡 The extra copy is a lion |
| 13 | Space Paranoids 2 | Sark and the MCP | together | ⬜ |
| 14 | Twilight Town again, Betwixt and Between | Nobody fights with Axel | together | ⬜ |
| 15 | The World That Never Was | Roxas (Final Mix), Xigbar, Luxord (time gauge instead of HP), Saïx (Berserk RC), Riku joins (1.30) | together | 🟡 the story fights not tried. Their optional Data versions, from the last save: Data Roxas fought on two PCs (session 33: the second player's game crashed twice, fixed with a bigger effect pool, 1.59; session 35: no crash, his Keyblade steal reaches the second player, who then stayed frozen in it, fix built, 1.62); all but Data Xigbar fought on the bench with one brain (session 34), no crash; open: Data Saïx's Berserk T-pose on the second player's screen (1.63). On two PCs (session 35): the chase before Xemnas (a crash on the second player's side fixed) and the cylinder jumps' reaction commands work for both players. Session 37, two PCs: Data Xemnas to the end (his Riku grab crashed the host until it was kept on the host's Sora, fixed); Data Xigbar: his shots go for and hit the second player, his arena changes no longer drop the host, but the second player can still fall after one (1.81), his sniper view only shows on the host's screen (1.82) and the second player's reaction command against his shots does nothing (1.83). Session 38, on the bench: both players stay on the floor after his arena changes (1.81 fixed), and the second player's sniper commands now hit him (1.84) with that player's Sora on the bullet (1.89); open: that player never gets the round's third command (1.85), and that player's Sora turns invisible after the round, on both screens (1.86) |
| 16 | Final fights | Xemnas, Armored Xemnas, Dragon Xemnas (ship shooting stage), Final Xemnas (Sora and Riku, reaction commands) | together; ship stage: together if feasible, else solo each (Q9) | 🟡 played together from the last save point (session 28), and to the credits on two PCs with no crash (session 33, Riku and the second Sora both there). Session 35, two PCs with one brain, the whole final boss with no crash: Armored Xemnas II's platform and his push are in step in both games, the buildings that hurt on contact show on the second player's side, and Final Xemnas's desperation move plays in both games. Open, on the second player's side: the shield-break buildings never showed there, so only the host could break Armored Xemnas II's shield (fix built, 1.49); the ship fight's parts and HP bar aren't in step (1.50); Final Xemnas's other reaction commands not checked yet (1.20). Final fight officially beaten |
| — | Between worlds | world map, Gummi Ship routes (each needed once) | host picks the world; Gummi together if feasible, else solo each (Q9) | 🟡 World Map: the host picks the world and the second player follows, loads in step. Both players move the ship; the host decides where to drop. (two PCs, 2026-10-12: Hollow Bastion and the Land of Dragons); Gummi routes not tried |

**Bench only** (two game copies on one PC, session 28): with the extra copy, all 62 saves in the co-op save file load in both games, and the second Sora takes each world's own form: the lion in Pride Lands, the retro look in Timeless River, the costumes in Halloween Town, Christmas Town and Space Paranoids. This isn't counted as played until it's tried on two PCs. Since session 31, the bench also plays fights: the third party gauge in one save per world (12 PASS), Final Xemnas up to his last phase, (session 34) the Data Organization fights from the last save with one brain, and (session 35) Armored Xemnas II, with a player driving the host's game.

**Progress: about 50%** toward the goal in `TODO.md`, which is two players finishing the whole story together on two PCs (re-checked 2026-10-17, session 38; about 50% since the TODO review of session 37 that closed 23 items, about 40% before it that day and on 2026-10-14, sessions 34 and 35, 35–40% on 2026-10-12, session 29, and 25–30% on 2026-10-08, session 18b). The item counts below are from 2026-10-17.

How the estimate was made: counting P0 items three times, P1 twice, P2 once, and a built-but-untested item as half done (crossed-out items left out), about 51% of the `TODO.md` work is done now, against 38% before the review, about 35% on 2026-10-14, 33% on 2026-10-12 and a fifth on 2026-10-08. (Two-PC runs keep adding new items, which holds the share down.) The estimate now matches that count: the riskiest systems are done (link, saves, Steam, disconnects, cutscenes, fights ending together), boss fights in five worlds have been played together, and one brain now plays the whole final boss on two PCs with no crash. It isn't higher, because story-progress sync and the Gummi Ship haven't been started, the bosses' special parts still differ on the second player's side in places, and every new world played so far has turned up new problems.

| `TODO.md` items | Done | Built, not tested | Open |
|---|---|---|---|
| P0 (blocks the start-to-finish run) | 28 | 1 | 17 |
| P1 (needed to play well) | 44 | 1 | 53 |
| P2 (polish) | 8 | 0 | 17 |

- **Done:** the parts with the most risk. These are the two-PC link (over Steam or Local), the second Sora beside Donald and Goofy, shared enemies and hits (each hit counted once), following the host's rooms (also through the World Map), loading screens that fade in together, the other player's spells shown on their Sora with the game's own particles and sounds, the downed state, shared Game Over, a shared pause, the other Sora in the party HUD (a third gauge, so Donald, Goofy and the other Sora show at once) and in story cutscenes, the enemy HP bar following the host's numbers, the two Soras stepping apart after a load, cutscenes, story scenes started by walking into an area (either player can start them), story fights that end at the same time in both games, fights where Sora must be alone (both players fight, no companions), the host's Skip Scene and a shared cutscene pause, boss fights played together (Cerberus, Bailey, Shan-Yu, Shadow Stalker and Dark Thorn, Volcanic Lord and Blizzard Lord, the final fights to the credits; some with open issues), a world ally's Limit (Mulan, Aladdin) and the Trinity Limit shown with whoever uses it, co-op saves kept apart from single-player saves, Steam achievements blocked during co-op, and disconnects: a 'Connection lost' screen with Continue Alone and Title Screen, and rejoining. Added in session 37 (two PCs): the second player's Sora flinches and is knocked back by the host's hits; big fights send their enemies in parts, so the second player sees all of them (the Land of Dragons' morale fight); the host's mission timer, counters and gauges shown in the second player's game; a Limit greyed while no real companion is in the party, as in the game; loading a save after a Game Over; the host's enemies aiming at the right player in the second player's game; and a diagnostic panel (F3) with ping, loss and the enemy counts.
- **Left:** story progress sync, the Gummi Ship, the bosses' special parts on the second player's side (phases, objects and scripted moves only the host's game shows), missions and timers in the other worlds (only the Land of Dragons' first morale fight has been tried), Drive Forms shown on the other Sora, summons and Donald's and Goofy's Limits in the other game, a separate inventory for each Sora, disconnect edge cases during boss fights, and the dev/test tools. Apart from the Twilight Town opening, no world has been played start to finish in co-op yet; the credits were reached from a save near the end.
- **Built, waiting for an in-game OK:** projectiles and area attacks (1.28): many enemies now aim at the right Sora, and it needs wider testing. The second player's Once More (1.77), reopened in session 38 because it saved that player from nearly every hit at 1 HP: rebuilt, and it looks right in the logs. Closed in the TODO review of 2026-10-17 after the two-PC runs: the second player's protection during its own Limit (1.75), the host's companions healing the second player's Sora (1.58), the second player's crash entering Imperial Square (1.68) and missions shared between games in different languages (1.69). Session 36 closed the personal settings file (9.7), EXP from Drive Forms (4.5), forced party changes (5.5), the 4 MB effect pool (1.59) and the bench's warp and later test saves (2.2, 2.14). Fixes built and counted as open until tried on two PCs: the host's field objects in the second player's game (Armored Xemnas II's shield-break buildings, 1.49) and that player's Sora freed after Data Roxas's Keyblade steal (1.62), from session 35. The fix for late network packets making bosses vanish under lag (6.3) has never triggered in a two-PC run.
- **In progress:** **one brain** (1.55): the second player's enemies copy the host's instead of running their own AI; the only way since session 35, and played on two PCs through the final fights. Session 35 added the host's script calls that the second player's game must also run: a boss's strikes and pulls on the second player's Sora (Armored Xemnas II's push), animation speed and timing, and reaction commands for the player who triggered them (one player at a time, 1.62). Sessions 36 and 37 added more of the bosses' script calls (effects that move, background and lights, orbs and munny they drop, damage rules, mission set-up), objects that are the host's from birth, and the host's choice of target for each enemy. Gaps found so far: Data Saïx's Berserk shows a T-pose on the second player's screen (1.63, bench), Larxene's clones are only in the host's game (1.53), Data Xigbar's sniper view, projectile reaction command and the sniper round's third command (1.82, 1.83, 1.85). Also still missing: the particles of the other player's Limits and reaction commands (1.31).

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

The host starts the game with `start_host.bat`, which sets the host's side (`CONNECT=local` at the top for Radmin VPN or LAN, or `CONNECT=steam`). With Local, step 4 isn't needed: the second player's game connects to the host's address by itself. The extra copy and one brain are always on (the older modes were removed in session 35), so there is no setting for them. The shared settings in `kh2coop.ini` must match on both PCs (the friend package does); if they differ, the log says `SETTINGS DIFFER`.

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
- **Continue Alone**: the room reloads without the other Sora. A fight or boss fight starts over. When the other player returns, the room reloads again with their Sora.
- **Title Screen**: back to the title screen through the game's own soft reset.

## Known issues and limits
- **Steam achievements are blocked by default** while co-op is on for testing. Set `ACHIEVEMENTS=1` in `kh2coop_player.ini` to allow them on that PC. With co-op off (normal LOAD or NEW GAME), they work as usual.
- Closing the game with Alt+F4 after a Steam session once logged an error during exit. It did not affect play.
- **Final fights:** they can be played to the credits, but the second player's side isn't fully in step: Armored Xemnas II's shield-break buildings didn't show there, so only the host could use them (fix built, 1.49), and in the ship fight that game gets stuck in its own part until the host's part ends (1.50).
- **Limits:** a Limit with Goofy can show idle on the other player's screen (its file wasn't found in the other game; logging added to find out why, 1.65).
- **Data Roxas:** after his Keyblade steal the second player's Sora can stay frozen until downed and revived (fix built, 1.62).
- **Data Xigbar:** after his arena change the second player's Sora can fall out of the arena (1.81, fix found, not made yet); his sniper view only shows on the host's screen (1.82); the second player's reaction command against his shots does nothing (1.83).
- Some things only show in the host's game: the wind gusts in the Land of Dragons and, on the bench, Larxene's clones (1.53). An enemy can be missing on the second player's screen for a few seconds (1.54).
- Summons and Donald's and Goofy's Limits in the other game: during the host's summon, the second player's Donald and Goofy stay and follow the second player's Sora, and the summon isn't shown there (1.61); Donald's Comet and Goofy's Knocksmash used by the second player don't show on the host's screen, and both players can use them at the same time (1.60).
- Swapping a companion through the Party command isn't copied to the second player's game (1.51).
- Older open bugs: Drive Forms aren't shown on the other Sora (1.24); after one player uses the Samurai's reaction command, the other can still use it and then freezes (1.47). On the bench: Data Saïx shows a T-pose at his Berserk on the second player's screen (1.63).
- The Gummi Ship and minigames aren't (And most probably won't be) handled yet. The World Map works: the host picks the world and the second player follows.

## Steam Connection and Gameplay Showcase

https://github.com/user-attachments/assets/7729c35f-9265-43c8-bd39-6e0143e6bd12

The video shows the mod's current progress as of October 11, 2026.

The demo uses two PCs, each running its own game: Host on the left, Client on the right. The client connects directly through Steam servers by opening the Steam Overlay and clicking "Join Game" on the Host's Steam profile. The video shows combat and the downed/revival animation.

Higher quality video here: https://www.youtube.com/watch?v=IJ1MecbHULk

First visit: Twilight Town - Hollow Bastion - Land of the Dragons: https://www.youtube.com/watch?v=6iT6TXw1hiA

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
