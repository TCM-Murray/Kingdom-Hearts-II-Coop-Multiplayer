# World mirror: research (session 29, 2026-10-12)

Your question: instead of fixing each case (a missing gust, a boss that goes invisible, a companion swap), can the
host send the friend's game everything the host's game shows (animations, objects, whether an object can be used)
in one general way?

Short answer: yes, there is a general route, and the research found the place in the game where it starts. It is
a big job, so this note lays out what we send today, why it misses things, the route, and the first small steps.
Steps 1 and 2 (logging only) are built and were run on the bench; results in section 9.

---

## 1. What the host sends today

Everything goes through one link (UDP over Radmin, or Steam), `avatar_link.cpp`.

| Packet | Direction | When | What it carries |
|---|---|---|---|
| KH2A avatar | both ways | every frame (~60 bytes) | your own Sora: room, position, facing, motion id + time, HP, flags (airborne, downed, motion from another file) |
| KH2W world | host -> friend | every frame (up to ~1.9 KB) | the two companions + up to 38 enemies: object id, position, facing, motion id, HP, spawn spot and serial |
| KH2E claim | friend -> host | per hit | the friend hit a host enemy (damage, reaction) |
| KH2H / KH2R | both ways | per hit / heal | a hit or heal on the other player's Sora copy |
| KH2L load barrier | both ways | room loads | both games leave the black screen together |
| KH2T scene areas | both ways | walk-in scenes, fight ends | a story area started; a mission (fight) ended |
| KH2K skip | host -> friend | cutscene skip | the host skipped |
| KH2P pause | both ways | combat pause | pause on/off |
| KH2G Game Over | host -> friend | Game Over | the host's choice |
| KH2F features | both ways | at connect | settings check ('SETTINGS DIFFER') |

Plus the friend follows the host's room changes (`follow.cpp`) and spawns the host's enemies from the room's
spawn file (`spawn_sync.cpp`).

## 2. What the friend's game does with it (the "two brains" problem)

- **Companions** (`world_sync.cpp`, mirror): matched to the host's by object id. When matched, their own AI is
  skipped and they copy the host's position, motion and HP. When the object ids differ (the host swapped someone
  in, TODO 1.51), the friend's own member runs on its own and the host's never appears.
- **Enemies** (`world_sync.cpp`, bind): each host enemy is bound to a friend enemy with the same object id and
  spawn spot. On the friend, a bound enemy gets the host's position, motion and HP, its own motion changes and the
  damage it takes are blocked. **But its own AI script keeps running** (only companions skip theirs; code
  reading: `WorldSyncSkipAi` is true only for mirrored companions, `puppet.cpp:520`, `:545`). So the
  friend's boss still decides for itself when to hide, show, fade, start an effect, fire a projectile, play a voice,
  or allow a reaction command, on its own timing, while its body follows the host's. Two brains drive one boss.
- **Anything that isn't a companion or on the enemy team (team 2)** is not sent at all: world helpers and
  hazards (team 3, e.g. the Land of Dragons `F_MU080` / `F_MU090` seen hitting enemies), objects on team 0,
  and enemies or projectiles made by a script rather than by the room's spawn file (they have no spawn serial,
  so the friend can't make them; its own ones are "parked out of sight" because the host has no match).

### Today's reports, explained by the above

| Report (TODO) | Why |
|---|---|
| Party swap not shown (1.51) | party members are never sent; mirrors match by object id |
| Donald calls "Sora!" only on the friend (1.52) | the friend's Donald is still the friend's game's own party member (own logic in some moments) |
| Gusts / buildings only on the host (1.53, 1.26) | not team 2, or made by a script: never sent; the friend's own copies are hidden |
| Beast's Castle boss invisible, vanishing, large (1.54) | the friend's boss script runs on its own (hide/show/fade at other moments); some of the room's boss objects parked |
| Twin Lords: the friend's own `M_AL020_FIRE2` / `B_AL110` parked, the host's never shown | made by the bosses' scripts in each game: no shared identity |
| Reaction command shown in another place / the other Sora idle (1.26, 1.31) | the reaction command runs only in the presser's game; the other game neither loads its motions nor plays it |
| Ship fight: the friend's fight stuck in its own part (1.50) | the friend's fight script runs its own phases |

## 3. What "everything the host shows" is made of

In KH2 almost everything on screen comes from three sources:

1. **Actors** (the active entity list, head `exe+0x2A171C8`): Sora, companions, enemies, bosses, their parts,
   projectiles that are actors, field objects (`F_...`), NPCs. Each has a position, facing, motion (id, time,
   and which file the motion comes from), HP, team, visibility and fade, scale.
2. **Script actions**: enemies, bosses, companions, rooms and missions run small scripts (OpenKH calls them
   bdscript / BDX). A script decides; when it wants something to happen in the game it calls a native function
   (a "syscall" or "trap"): start an effect, play a voice, hide a part, spawn a projectile, allow a reaction
   command, shake the camera, show a message, start a timer, set a story flag...
3. **Motion triggers**: frame events inside a motion file (`.mset`) that start effects, sounds, hit boxes when a
   given frame of an animation plays. These follow the motion: if the friend's actor plays the same motion,
   most of these happen by themselves.

A general mirror therefore needs: (1) every actor's state, (2) the script actions, and (3) for free, the motion
triggers.

## 4. Finding: one choke point for every script action

Verified on this PC's exe, first from the file, then live in four fights (VERIFIED_OFFSETS 'Script system calls (traps): the tables, live'):

- The script interpreter is `exe+0x41B400` (already known as the 'AI command runner'). Its opcode 10 is the
  syscall: it takes a table from a table-of-tables (`exe+0x753490`, passed by `exe+0x3E1C80`), an entry
  {function, flags: argument count + 'returns a value'}, pops the arguments and calls the function, all at
  **one call site**.
- There are 9 tables with **828 functions**. OpenKH's `BdxTraps.cs` names all 828, and every one of their flags
  matches our exe's tables, so the names fit this build. Three we already knew match: table 1 #3
  `trap_sysobj_player` = our 'get the player' `exe+0x42B230`, 1 #87 `trap_obj_effect_start_bind` = our 'start
  effect' `exe+0x42D540`, 1 #121 `trap_target_search` = Volpestyle's `exe+0x4303A0`.
- Rough grouping by name: ~74 effects, ~9 sounds/voices, ~43 spawn/remove objects, ~55 motions, ~21
  visibility/fades, ~33 reaction commands/buttons, ~62 HUD/mission/messages, ~24 camera, ~18 story flags, ~97
  movement, ~66 hits, ~52 pure queries, ~270 other (maths, debug, specific bosses).
- Names that match today's reports: `trap_obj_hide` / `trap_obj_show` / `trap_obj_hide_part`,
  `trap_sysobj_fadeout` / `fadein` (the invisible boss), `method_obj_enable_reaction_command` / `disable`
  (who may use a reaction command), `trap_obj_voice` (voices, likely Donald's call), `trap_sysobj_appear` /
  `trap_obj_leave` (objects appearing and leaving), `trap_special_last_xemnus_laser_*` (Final Xemnas's lasers),
  `trap_sysobj_party` (party).

So the host can see every script action in one hook, with its name, its actor and its arguments, and the friend's
game can run the same function. That's what makes a general approach possible.

## 5. Options

**A. Full actor mirror.** Extend world sync from "companions + team 2" to every actor in the host's room: send
each one's state; on the friend, match it (or create it with the game's actor factory `exe+0x3DF930`), switch its
own scripts off completely, and copy the host's state every frame.
Gets: positions, motions, HP, visibility, scale, plus motion-trigger effects and sounds. Misses: one-off actions
that aren't actor state (screen effects, camera shakes, messages, timers, reaction-command prompts, effects not
tied to a motion).

**B. Script-action replay.** Hook the syscall call site on the host; send the "visible" actions (effects, sounds,
show/hide, fades, spawns, reaction-command on/off, HUD, camera shake); on the friend, run the same function on
the matching actor. Gets: exactly the one-offs A misses. Needs: actor matching (A), and argument translation
(actor handles and vectors, not just numbers).

**C. Both (recommended): one brain, the host's.** The friend's world actors (everything except the friend's own
Sora) stop thinking for themselves: no scripts, no AI. Their state comes from the host (A), and the host's
visible script actions are replayed on them (B). When the friend does something to the world (hits an enemy,
presses a reaction command, opens a chest, uses a switch), it asks the host, as hit claims already do today; the
host's game does it, and the result comes back through A and B.
This is the standard design for online games (state for things that change all the time, events for one-off
things), and it's what Volpestyle planned too ("enemies = puppets, AI off").

**Rejected:**
- *Run both games' scripts the same way (lockstep):* the scripts read random numbers (3 random syscalls, plus a
  game-wide random generator with ~270 users per Volpestyle's notes), the player's position, timers; with two
  different Soras the two games can't stay identical.
- *Stream the host's screen to the friend:* everything would show, but there'd be one camera for both players,
  video lag on the friend's own moves, and the friend's Sora would have to be controlled inside the host's game,
  which KH2 can't do (one player controller).

## 6. What C costs and risks

- **Matching actors.** The room's spawn-file enemies already match by serial. Script-made actors need a new key,
  for example "made by host actor X's script, the Nth time". The syscall hook sees who made what.
- **Files the friend hasn't loaded.** A model or motion the friend's room never loaded can't be shown (a plain Sora couldn't
  be built in the ship fight: its model isn't loaded there). Both games load the same room and programs, so most should be
  there; reaction-command and Limit motions are the known exception (1.31): we need the loader the game uses for
  them.
- **The script debt** (Volpestyle's lesson 7): scripts do more than look. Switching them off on the friend means
  the host must send everything they did that matters, and anything we forget goes missing on the friend's
  screen. The syscall names give us the full list to check against.
- **Interactions** become requests to the host: reaction commands, chests, switches, the friend's own hits on
  objects. Each needs a small "friend asks, host does" path, like hit claims.
- **Bandwidth.** Today's world packet can reach ~1.9 KB a frame, already above one network packet (~1.4 KB).
  Every actor at 60 frames a second won't fit in one; it needs splitting, sending only what changed, and slower
  rates for still objects. Steam or Radmin can carry it (~1-2 Mbit/s at most), but it has to be designed.
- **Pools.** Creating actors or effects on the friend uses the friend's memory pools (we've overflowed the effect
  pool before). Mirroring instead of running both brains should lower the load, not raise it.
- **Lag.** The friend sees the host's world ~100 ms late (as now). Hits the friend lands are still resolved on the
  host (as now).

What C would replace or simplify: the enemy parking and binding guesswork, spawn sync's retries, the companion
mirror (1.51, 1.52 become "the friend's companions are the host's"), possibly scene sync's mission-end handling
for fights (if the friend's fight scripts are switched off too), and many per-boss fixes (1.20, 1.49, 1.50, 1.54).

## 7. Prior art

- **Volpestyle** (`docs/ONLINE_COOP_PLAN.md`, `ENEMY_PARITY.md`): the same split (host owns enemies, clients
  puppet them with AI off), built enemy family by enemy family; projectiles and script-spawned attacks are listed
  as not done; they named the syscalls 'bdscript syscall bank/index' and swapped one (`trap_target_search`).
- **Other games** (Volpestyle's `docs/research/PRIOR_ART.md`): shared enemies are where most co-op mods stall.
  OoT True Coop (host owns enemies, AI off on clients, deaths as the game's own lethal hit) is the closest
  published match. Nobody we know of replays a game's script calls generally; the hook found here is what makes
  it worth trying in KH2.

## 8. Next research steps (small, each answers one question)

1. **Census** (logging only, both PCs): every actor in the room each second: object, model, team, made by the
   spawn file or by a script (and which), visible or not. Run it in the fights from today (Twin Lords, Beast's
   Castle ballroom, Shan-Yu, a Land of Dragons gust room). Answers: how many actors, which ones the friend lacks,
   how big a full mirror packet would be.
2. **Syscall log** (logging only): every syscall the host's scripts make in one boss fight, by name and actor.
   Answers: which actions are visible, how many per second, which arguments are actor handles.
3. **One brain, one boss** (bench, two copies): switch off one boss's scripts on the friend, copy the host's
   state, replay its hide/show/fade/effect syscalls. Candidate: the Beast's Castle ballroom (05/05), which showed
   invisible / vanishing / large today.
4. **Reaction-command motions** (bench): find how the game loads a reaction command's motions, so the other
   player's Sora can play them (1.31).

Steps 1 and 2 change nothing in the game. Both are done (section 9).

## 9. Results of steps 1 and 2 (bench, session 29)

Built as `src/dll/world_census.cpp`, off unless `KH2COOP_CENSUS=1` / `KH2COOP_TRAP_LOG=1`. Run with
`py tools/test_census.py <bc|twins|gusts|shanyu>`: two copies on this PC load a co-op save, the host warps into the
fight, the host's Sora attacks for 1.5-2.5 minutes (god mode), both games log. Every finding below is also in
VERIFIED_OFFSETS.

**What the logs give us now**

- Every actor in each game every 10 s and each time one appears or goes: object, model, team, HP, position,
  motion (and whether it's from another file), flags, fade, live script threads, and who made it: the room's spawn
  file (serial), a script call (which call, which actor's script), or other game code.
- Every script call by name, with the actor whose script made it and the arguments (actors by model), the first
  300 different ones of each call per room; for calls on an actor, which bytes of that actor changed.
- Counts of all calls every 10 s.

**The numbers**

| Fight | Actors in each game | Script calls per second (all / actions) |
|---|---|---|
| Beast's Castle ballroom (05/05, 78) | 12-15 | up to 2,200 / 470 |
| Twin Lords (07/03, 59) | 11-19, plus their projectiles and minions | up to 5,700 / 1,000 |
| Land of Dragons gust room (08/08) | 8-17 | up to 2,200 / - |
| Shan-Yu (08/09, 75) | 14-17 | up to 4,400 / 620 |

Most "action" calls are per-frame movement (`obj_set_movement`, `obj_wish_dir`, `obj_set_pos`, `attack_set_pos`,
`effect_set_pos`): that's state, which copying positions covers. The one-off actions (motions started, effects,
flags, actors made or removed) are tens per second at most. So: copy state every frame, send one-off events. A full
mirror of ~20 actors at 32 bytes each is ~640 bytes a frame, which fits in one network packet.

**What we learned**

1. **The same actor has the same address in both games.** Actors come from a fixed pool inside the game's memory,
   handed out in order. After a room load both games' lists were identical, model by model and address by address,
   on the bench and on your two PCs (last night's logs). That's a ready-made way to pair actors, better than today's
   "same object and spawn spot". Only actors made later by scripts get different addresses once the games' timing
   differs.
2. **Both games' boss scripts start out identical, then drift.** In the ballroom both Shadow Stalkers made the same
   calls in the same order for ~8 s, 0.1 s apart. Then targets, random numbers and pauses split them: your Limit's
   scene froze the host's boss for 3.7 s while the friend's kept going. That's the "two brains" problem, now seen
   directly.
3. **Why things are missing on the friend's screen** (three separate causes, all from today's design):
   - *Gusts in the Land of Dragons* (`F_MU070`, team 3): the friend's spawn sync skips its own spawn group ("the
     host spawns it"), but the host only sends team-2 enemies, so the gusts never appear (TODO 1.53).
   - *Field objects without HP* (ballroom `F_BB080`): world sync never pairs a host enemy whose HP reads as missing,
     so the friend hides its own copy under the floor for the whole fight.
   - *Things a boss's script makes* (Twin Lords' fireballs `B_AL110`, `B_AL120`, ice and fire minions): each game
     makes its own (~100 each); the friend hides all of its own and only 10 of the host's got paired. So on the
     friend's screen most of the Lords' attacks are invisible.
4. **Limits.** Using Beast's or Mulan's Limit removes Goofy in the host's game for ~20 s (he comes back without
   going through the actor factory), and Sora and the partner play Limit motions from another file. In the friend's
   game Goofy stays and the host's Sora and the partner stand idle.
5. **Shan-Yu's sword clash** is Shan-Yu's script grabbing the host's Sora: it moves him, turns him, makes him idle,
   then `obj_motion_capture(Shan-Yu, Sora, 252)` (the clash scene starts inside that call) and `obj_attach`. Only
   then the reaction command comes. The friend's Shan-Yu never does this to its copy of the host's Sora, so the
   friend sees him standing (TODO 1.26, 1.31).
6. **What some script calls change** (bytes of the actor before/after): flags at `+0x120`, attack patterns at
   `+0xD10`, motion at `+0x170..0x19C`, collision at `+0x5AC`. Hiding parts and stealth change nothing in the actor
   itself, so their state lives elsewhere (still to find).

**What this means for the plan (C, one brain)**

- Pairing: by address for everything from the room load; for script-made actors, the host reports "actor X's script
  made object O at P" (the log already sees that) and the friend makes the same object itself.
- Two small fixes fall out of 3 and could go in before the big job, if you want them: send team-3 objects and
  HP-less ones too, and pair them. Both get replaced by the full mirror later.
- Step 3 (one boss with one brain) now has what it needs: the ballroom's actors, the calls the boss makes, and how
  each changes the boss. Open question for it: switching a boss's scripts off on the friend also skips its setup
  calls (its stat block, `btlobj_set_sheet`, comes from a script call), so either the setup runs and then the
  thinking stops, or the host's setup calls are replayed.
