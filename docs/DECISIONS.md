# Decisions register

Every rule you (and your friend) decided for how co-op should behave, what the code does today, and whether the two
agree. Kept up to date: a new decision gets a row the day it's made, and a fix that touches a decision updates its
status. The rows used to live in three places (the Q1-Q18 table at the top of TODO.md, "your pick" notes inside TODO
items, and MODLOG); they were gathered here on 2026-10-14 (session 35), and the code was checked against each one.

**Status:** **Matches** = the code does what was decided. **Conflicts** = the code does something else (listed first,
below). **Not built** = decided, no code yet (the TODO item says so). **Partly** = some of it is built.
**Superseded?** = a later decision seems to replace it; needs your word. **Not checked** = code exists, but nobody
has looked at whether it still holds (e.g. after one brain).

"Code today" names the file or setting; settings are on by default unless a value is given (both PCs must match;
`start_host.bat` and the friend's `kh2coop.ini` from `tools/package_friend.py` set the same values).

## Conflicts and open questions (audit 2026-10-14)

1. **Limits (rule L1-L3 below).** Your rule (2026-10-14): during a Limit every ally leaves with its HUD except the
   Limit's partner and the Sora copy, and only one Limit runs at a time. The code today:
   - **(a) and (b) fixed on the bench 2026-10-14 (session 35, `limit_sync.cpp`); two-PC run 2026-10-14: as expected (you),
     committed f4a737b.** Before:
   - **(a) The other game keeps its allies.** During the host's Limit the friend's game lets go of the mirrored
     ally that left the host's game, and it runs on its own AI with its HUD (bench 2026-10-14: 'companion in slot 1
     released' at the host's Limit, Donald running locally until it ended). During the friend's Limit the host's
     own allies (other than the borrowed partner, 1.57) stay.
   - **(b) The copy gets pulled into the Limit.** When a Limit empties party slot 1, our GetFriend(1) hook
     (`puppet.cpp` `HookGetFriend`) hands the Sora copy to the Limit's own code (callers `exe+0x3DBF3E`,
     `0x3A8837`, `0x42FA20`, `0x40084F`); the friend's screen showed the host's copy in a T-pose during the friend's
     Limit (bench 2026-10-14).
   - **(c) fixed on the bench 2026-10-14 (refused in the menu, rule L2)**; before:
   - **(c) Two Limits at once are not stopped.** Only the same ally can't be in two Limits; anything else runs
     together.
   - It applies **in both games** (you, 2026-10-14): whoever uses the Limit, both screens lose the allies, which is
     why only one Limit can run.
2. **Summons (Q13).** Decided: only the AI members leave, the copy stays, one summon at a time. Today: on the
   friend's screen Donald and Goofy stay during the host's summon (TODO 1.61, known), and nothing stops a second
   summon.
3. **Reaction commands (Q8) vs one player at a time (1.62): settled 2026-10-14, 1.62 replaces Q8's boss part** (the
   boss offers its RC to the nearer player; the 'watch the scene' option is dropped). Still not checked: Q8's "RCs
   that only move a player are free for both" now that field objects are under one brain too.
4. **Hollow Bastion turrets (Q7).** Decided: the friend's turrets off, the host's shown. Built: the friend's turret
   hits don't count (1.40). Not checked: what one brain does to the friend's turrets (team 3 objects are bound too).

## Rules of play (decided 2026-10-07 with your friend, Q1-Q18)

| # | Decision | Code today | Status | TODO |
|---|---|---|---|---|
| Q1 | **Drops per player**: each game drops its own loot from the shared kill | No drop sync; the copy is kept out of the drop code's party count (`puppet.cpp` `kNoCloneExactCallers` 0x3DC208, 0x3DC2E7, 0x3ABA67) | Matches | 4.2-4.5, 4.8 |
| Q2 | **Chests and fixed rewards: both players get everything** | No code | Not built | 4.6, 4.7 |
| Q3 | Items, Keyblades, gear, abilities, munny only in each player's own save; your Keyblade, Drive Form and costume show on your copy; your gear viewable read-only on the copy's card; menus don't treat the copy as Donald/Goofy | Own save: yes. The copy's Keyblade (3.3) and the menus (3.5) not done; `HookMenuPartyId` covers part of the menus | Partly | 3.x |
| Q4 | A PC control panel first (dev tools) | Not started | Not built | 2.11 |
| Q5 | **Drive Forms: the copy stays in the fight**; both players can use forms, also at the same time | Nothing blocks forms; the other screen shows a normal-form Sora (1.24) | Partly | 1.23, 1.24 |
| Q6 | **Mickey: both games roll, the bigger margin wins; the other player is frozen and invulnerable** while Mickey is out | No code; the rescue's end can crash the host (1.3) | Not built | 1.3 |
| Q7 | **Turrets: the friend's off, the host's shown** | Friend's turret hits dropped (`world_sync.cpp` hit sides) | Partly, Not checked (one brain) | 1.1 |
| Q8 | ~~Boss RCs go to whoever presses first; the other player watches the scene~~ **replaced by 1.62 (you, 2026-10-14): the boss offers its RC to the nearer player; no watch option.** RCs that only move a player are free for both | Boss RCs go to the nearer player (`reaction_sync.cpp`, `aggro.cpp`); an RC from something that grabbed a player goes to that player (2026-10-14); a field object's RC (`F_...`) the friend presses from its own game's list plays in the friend's game, its object on its own brain for it (your pick 2026-10-14) | Matches (boss part); move-only RCs: built 2026-10-14, not yet tried (two-PC run: the cylinder jumps did nothing for the friend) | 1.26, 1.62 |
| Q9 | Parts that aren't normal co-op: case by case, solo where it can't be shared | Forced-solo fights shared (`solo_copy.cpp`); the rest open | Partly | 5.2, 5.5 |
| Q10 | **The host's story is the truth**; per-player things (drops, synthesis, shops) never overwritten | No story-flag sync yet | Not built | 5.1 |
| Q11 | Co-op saves apart from single-player, in the mod's own co-op save file; LOAD (CO-OP) on the title; 'the friend saves when the host saves' dropped (session 25) | `coop_save.cpp`, `title_menu.cpp`, `TITLE_COOP=1` | Matches | 5.7 |
| Q12 | The host hands the save over once at Sora's first save point | Instructions not written | Not built | 5.8 |
| Q13 | **Summons: the copy stays, the summon joins; one summon at a time** | No one-summon rule; the friend's Donald and Goofy stay during the host's summon | Conflicts (conflict 2) | 1.32, 1.61 |
| Q14 | Whatever a Sora picks up goes to that game's player; no synced drops | As decided | Matches | 4.2 |
| Q15 | Out-of-combat menu keeps your game running (later) | Parked | Not built | 9.3 |
| Q16 | **Save points also refill the Drive gauge**, ini switch, on | No code | Not built | 9.4 |
| Q17 | Achievements blocked only while co-op is set up; `ACHIEVEMENTS=1` allows them | `ACHIEVEMENTS` default 0 | Matches | 9.5 |
| Q18 | **The friend's hits fill revenge too, each hit adds less** | No code | Not built | 9.6 |

## Players and the copy

| Decision | When | Code today | Status |
|---|---|---|---|
| Each player is Sora; the other shows as a Sora copy | 2026-10-04 | `puppet.cpp` | Matches |
| The copy is spawned beside the party ('extra copy'), not in Donald's or Goofy's place | merged 2026-10-13 | `solo_copy.cpp` (the only way since session 35: the copy in Donald's slot and `EXTRA_COPY` removed) | Matches |
| Each Sora has its own HP ("true co-op"); hits on the copy go to its player | 2026-10-05 | `FORWARD_HITS=1` | Matches |
| The host's Sora wears the gold crown, the friend's none; close-ups the same for both | session 26 | `crown.cpp`, `CROWN=1` | Matches |
| Downed instead of dead; only a Cure or an item revives early, HP orbs do nothing | 2026-10-06 | `downed.cpp`, `puppet.cpp` | Matches |
| Downed in a Drive Form: the form ends first | session 17 | `downed.cpp` | Matches |
| The copy's party gauge sits in the top position when it is the only one (TODO 3.15, P2) | 2026-10-14 | `party_hud.cpp`: in The World That Never Was it takes the second position with the first empty | Not built |

## Enemies and combat

| Decision | When | Code today | Status |
|---|---|---|---|
| Enemies are shared: the host's game owns them; one brain (the host's) for every enemy | 2026-10-05, 2026-10-13 | `world_sync.cpp`, `world_mirror.cpp` (the only way since session 35: two brains, `ONE_BRAIN` and `ONE_BRAIN_TYPES` removed) | Matches |
| The friend's damage counts only through its claims (1.40 option a) | 2026-10-13 | `OWN_HITS_ONLY=1` | Matches |
| A companion near waiting enemies wakes them (1.42) | session 18 | `aggro.cpp` | Matches; Not checked (one brain) |
| Enemies near a downed friend far from the host stay near the friend | session 18 | `DOWNED_STAY=1`, `AI_NEAREST_PLAYER=1` | Matches |
| Bosses stay out of that swap (option a) | 2026-10-13 | `AI_NEAREST_PLAYER_BOSSES=0` | Matches |
| A boss goes for the nearer player and holds its pick 4 s (1.62) | 2026-10-13 | `BOSS_NEAREST_PLAYER=1`, `aggro.cpp` | Matches |
| Fixes by game mechanism for every boss first; one-boss fixes only for real exceptions | 2026-10-14 | (way of working) | - |
| Host objects the friend's game has no match for are made there (Luxord's cards) | 2026-10-14 | `MAKE_UNMATCHED=1`, `spawn_sync.cpp` | Matches |
| The effect pool is 4 MB | 2026-10-13 | `FX_POOL_KB=4096` | Matches |

## Limits, summons, spells

| # | Decision | When | Code today | Status |
|---|---|---|---|---|
| L1 | **During a Limit every ally leaves with its HUD, except the Limit's partner and the Sora copy, in both games** (the Q&A assumed only the chosen ally left; in the game everyone does) | 2026-10-14 | `limit_sync.cpp` (`LIMIT_SYNC=1`): the other game sends its allies away with the game's own leave call and brings them back at the Limit's end; `puppet.cpp` GetFriend gives no copy during any Limit. Bench 2026-10-14 (both directions, HUD screenshots); two-PC run 2026-10-14: as expected (you) | Matches |
| L2 | **Only one Limit at a time** ("only ONE person should be able to use Limit. Period."). If both press within the moment before either game hears of the other, both Limits may start: accepted (2026-10-14). The refusal should come from the game itself: with the allies gone during the other player's Limit, the Limit option should be unavailable; a refusal of our own only if that turns out not to hold (2026-10-14). It doesn't hold: the ally playing the other Limit's partner stays, so the game offers that ally's Limit (bench 2026-10-14) | 2026-10-13, 2026-10-14 | `puppet.cpp` `HookCmdExecute`: a Limit command is refused while the other player's Limit runs (`limit_sync.cpp` `LimitSyncPeerLimit`); the list stays open and the press does nothing (no grey-out, no message: fine, you 2026-10-14). Bench both directions; two-PC run 2026-10-14: as expected (you) | Matches |
| L3 | The Limit's partner stays for the Limit, next to the Sora who used it, on both screens: the friend's partner played by the host's ally with that id (1.57), the host's partner shown the same way on the friend's screen | 2026-10-13, 2026-10-14 | `PARTNER_LIMIT=1`, `world_sync.cpp`; Trinity's two partners: only one was shown on the host's screen (your test 2026-10-14), fixed for two: both follow (two-PC run 2026-10-14); their Trinity motions don't play (L5, TODO 1.65) | Matches (the partners); animations: L5 |
| L4 | The friend's own Limit runs locally in its game ('local Limit') | 2026-10-06 | `world_sync.cpp` `UpdateMirrors` | Matches |
| L5 | Limit and reaction-command animations show on the other player's copy | session 29 | `MIRROR_MOTIONS=1`, `motion_mirror.cpp` (gaps: TODO 1.65) | Partly |
| L6 | Spells: cast on the clone (the other game casts the same spell on the copy; it hits and heals nothing there) | session 29 | `motion_mirror.cpp` | Matches |
| - | Summons: see Q13 | 2026-10-07 | | Conflicts |

## Bosses

| Decision | When | Code today | Status |
|---|---|---|---|
| A grab plays on the grabbed player's own Sora, with its close-up and flash on that player's screen; the host's camera stays on the host's Sora | 2026-10-14 | `grab_sync.cpp`, `GRAB_HOLD_CAMERA=1` | Matches (Final Xemnas's held camera crashes the host: your call a/b/c pending, 1.62) |
| Data Roxas's extra Keyblades stay with the player he stole from | 2026-10-14 | `aggro.cpp` | Matches |
| Final Xemnas's desperation move plays in both games; the friend's hits can't kill a boss that holds a player | 2026-10-14 | `shared_move.cpp`, `world_sync.cpp` | Matches |
| Final Xemnas grab: option 3 for now (options 1 and 2 kept for later) | 2026-10-14 | `aggro.cpp` | Matches |
| Final Xemnas: keep Riku, the two Soras stay; when the game switches to Riku, Riku follows the copy and the copy rescues the host | 2026-10-12 | Pending, no priority | Not built |

## Story, cutscenes, rooms

| Decision | When | Code today | Status |
|---|---|---|---|
| Only the host starts events and bosses (exception: boss RCs, Q8) | session 9 | `follow.cpp`, `scene_sync.cpp` | Matches |
| Scenes started by walking into an area are shared; doors and talk/examine stay host-only | session 24 | `scene_sync.cpp`, `load_barrier.cpp` | Matches |
| The friend doesn't take doors itself; it follows the host, also through rooms the host passed since | 2026-10-05 (N2), session 20 | `LOCK_DOORS=1`, `FOLLOW=1` | Matches |
| Only the host skips cutscenes; the friend's game skips at the same moment | sessions 20, 27 | `skip_sync.cpp` (the friend's own skip is refused, not greyed out) | Matches |
| The pause is shared, also in cutscenes | 2026-10-05, session 20 | `PAUSE_SYNC=1` in both launch files (code default 0) | Matches |
| The other player's Sora acts in story cutscenes as a twin a step beside Sora; `CUTSCENE_TWIN=0` turns it off | session 20 | `cutscene_twin.cpp` | Matches (mirrored twin: 5.21) |
| Either player's fight ending ends the fight for both | session 24 | mission commands shared | Matches |
| Both Soras step aside when they appear on one spot | session 22 | `SPAWN_STEP=1` | Matches |
| Forced-solo fights: both players fight, only Donald and Goofy leave | session 27 | `solo_copy.cpp`: the extra copy is spawned in every room, forced-solo ones too (`SOLO_COPY` removed in session 35) | Matches |

## Connection and saves

| Decision | When | Code today | Status |
|---|---|---|---|
| When the other game is gone: wait, Title Screen, or Continue Alone; Continue Alone reloads the room in fights; on rejoin the room reloads | session 27 | `link_watch.cpp`, `link_menu.cpp` | Matches |
| Steam (Join Game / Invite) and Radmin both kept; any Steam friend may join, the first one only | session 25 | `steam_link.cpp` | Matches |
| Saves never carry the copy; broken saves get Donald or Goofy back on load | session 24 | the extra copy is never in the party table, so saves can't carry it (the save cleaner in `coop_save.cpp` removed in session 35); `puppet.cpp` `RepairSavedCopy` for old saves | Matches |
| The real Steam Cloud save is never written in co-op | session 24 | `coop_save.cpp`, save guard | Matches |
| The DLL's own defaults are the co-op modes in use (extra copy, one brain, world sync, pause sync, the copy's collision / targeting / aggro); the old modes (Donald dressed as the other player, copy in Donald's slot, two brains) are removed; test and research switches stay | 2026-10-14 (cleanup, your picks 'Both', 'Keep them', 'Remove it too' for `ONE_BRAIN_TYPES`) | defaults in `puppet.cpp`, `world_sync.cpp`, `load_barrier.cpp`, `pause_sync.cpp`, `aggro.cpp`, `avatar_link.cpp` (feature check), `tools/launch_pair.py`; old modes removed (commits eb5b778, 524e6d2, 687447f) | Matches (builds; not installed or tried in game yet) |
