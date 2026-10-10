# Kingdom Hearts II Co-op Multiplayer

Online two-player co-op for Kingdom Hearts II Final Mix (PC, Steam). Each player is Sora in their own game, and the other player appears as a second Sora in the party. The host's game owns rooms and enemies; the second game follows it.

**Status:** in development. Tested between two PCs in Hollow Bastion and Olympus Coliseum (up to and including the Cerberus fight), and in a story run from Sora's start through Twilight Town up to the train to Yen Sid's tower (2026-10-09). Not ready for public play: there's no installer, and the mod needs motion files built from your own game files (`tools/make_mset.py`).

**Progress: about 25–30%** toward the goal in `TODO.md`, which is two players finishing the whole story together on two PCs (estimate from 2026-10-08, session 18b; item counts below updated 2026-10-10, session 24).

| `TODO.md` items | Done | Built, not tested | Open |
|---|---|---|---|
| P0 (blocks the start-to-finish run) | 11 | 1 | 24 |
| P1 (needed to play well) | 13 | 1 | 41 |
| P2 (polish) | 2 | 0 | 13 |

- **Done:** the parts with the most risk. These are the two-PC link, the second Sora in the party, shared enemies and hits, following the host's rooms, the downed state, shared Game Over, a shared pause, the other Sora in the party HUD and in story cutscenes, the two Soras stepping apart after a load, cutscenes and a real boss fight (Cerberus).
- **Left:** story progress sync, the world map and Gummi Ship, bosses with phases, missions and timers, forced party changes and world costumes, a separate inventory for each Sora, disconnect and rejoin, and the dev/test tools. No world has been played start to finish in co-op yet.
- **Built, waiting for a two-PC test:** the fix for the friend stuck on a black screen after the host skips a chain of cutscenes (TODO 5.13).

- `TODO.md`: the plan and what's left.
- `MODLOG.md`: the development diary, with evidence.
- `VERIFIED_OFFSETS.md`: addresses, structures and game rules found on the Steam build.
- `MODDING_PLAN.md`: the original plan and design decisions.

## Layout
- `src/dll/`: the C++ DLL. OpenKH Panacea loads it from the OpenKH mod in `openkh_mod/`. Hooks use MinHook.
- `tools/`: Python helpers for building, launching test copies, packaging and research.
- Settings: `kh2coop.ini` (shared, must match on both PCs) and `kh2coop_player.ini` (personal, written by the DLL with defaults on first start).

## License
Copyright (C) 2026 TCM-Murray

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see [`LICENSE`](LICENSE) for details. SPDX: `GPL-3.0-or-later`.

Third-party code: [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause), downloaded at build time.

## Credits
- **[Volpestyle/kh2-multiplayer](https://github.com/Volpestyle/kh2-multiplayer)** (GPL-3.0-or-later): this project uses their documentation and research (pointer maps, hook points, input, enemy and save-guard notes) as leads; every address taken from it is re-checked on our build before use (`VERIFIED_OFFSETS.md`).
- **[OpenKH](https://github.com/OpenKH/OpenKh)**: Panacea loads the mod's DLL, and its documentation and file-format code (e.g. the event script layout) are used as references.
- **[Expert595/kh2-multiplayer](https://github.com/Expert595/kh2-multiplayer)** and the KH2 Lua library: a few addresses used as leads.
- **[Julumisan/kh-pc-save-transfer](https://github.com/Julumisan/kh-pc-save-transfer)** (MIT): recommended to the second player for moving the host's save between Steam accounts; not included in this repository.

Kingdom Hearts and Kingdom Hearts II are the property of Square Enix and Disney. This is an unofficial fan project, not affiliated with or endorsed by them. The repository contains no game files; you need your own copy of the game.

Developed with the help of Claude (Anthropic).
