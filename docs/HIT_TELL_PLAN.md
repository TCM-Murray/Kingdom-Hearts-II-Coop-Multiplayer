# Plan: one damage callback per hit, in the host's game (temporary)

**Temporary.** The plan for your pick of 2026-11-24 ("Full A, planned first"; DECISIONS, 'Enemies and combat'; TODO
1.277). **Steps 0 and 1 are built, benched and committed (31f84ed); step 2 has its two rulings (below) and is
next.** Delete this file in the last step's commit. Research and review: MODLOG session 86
(five research agents, two skeptics, 2026-11-24); evidence F868, F874-F878, L347, L2044-L2053. This is the second
version: the skeptics' objections that held are in it.

## Words
- **The tell:** near its end the game's hit handler (`exe+0x410D60`) tells the enemy's scripts it was hit: the
  running act's damage callback (`exe+0x3CA470`), then the main script's (event 6, `exe+0x3E1840`). 360 of 543
  decoded scripts have one (L2051): revenge acts, hit counters, phase changes by HP, break effects.
- **The copy's swing:** in the host's game the friend's Sora copy replays the friend's motions; its attacks can land.

## What is true today (measured 2026-11-24)
| Where | What happens at a friend's hit on a host enemy with HP |
|---|---|
| Friend's game | The whole handler runs on its own copy of the enemy, HP blocked: sound, spark, and the tell. The tell also runs for the host copy's and the companions' hits (F877). Its script state is not the host's: break effect drawn again (F868), 'crash' act started again (F878). |
| Host's game, the claim | HP only. No sound, no flinch, no tell (parts: a stand-in tell with attack id 0, session 85). |
| Host's game, the copy's swing | When it lands (58 of 74 and 44 of 58 hits in two runs, F875) the whole handler runs with the real attack, HP dropped: sound, spark, flinch, the tell, and the class's revenge add. Never for the friend's spells or Limits. A part hit is told twice (F876). |

So the host's script hears about 3 of 4 friend hits already, by the copy's swing, in no fixed order with the claim's
HP. My earlier "nobody would run it" was wrong for those hits.

## Target
Each hit on a host enemy: the tell runs once, in the host's game, with the hit's real data; never in the friend's.

## Steps (each behind a switch, each benched before the next one starts)
**Step 0: measure.** The two hooks of step 1, passing everything through, with one counter and log line per event a
bound copy's script is sent in either game: event 6 and the act's callback, and the other per-object events that
could double the same way (0xE land, 0xF attack, 0x13 reflect). Baseline benches: `storm_rider --what spikes --by B`
and `--by A`, `test_census.py bc --only-friend`. No behaviour change. Hooks 165 to 167: no existing hook sees these.

**Step 1: the friend's game stops telling its copy.** For an enemy **with HP** that is bound and has its brain
skipped, the handler's tell is not run, whoever hit it. Everything else in the handler stays (sound, spark, claim).
- How: the same two hooks, with no state kept. `exe+0x3CA470` (one caller, the handler): not run for such an enemy.
  `exe+0x3E1840`: not run when the event is 6, the caller is the handler (return address `exe+0x41102C`) and the
  script's owner is such an enemy. All 36 callers are read (L2053): no other one sends event 6 to an actor's script.
- Not cut: objects with no HP (below), before binding, after the host's kill, parked copies, while the copy has its
  own brain (a shared move, our RC: TODO 1.114), every other event (motion triggers keep the spells, F447), the
  area's unit scripts.
- Fixes F868 and F878. The host's game is untouched. Switch `KH2COOP_OWN_HIT_TELL=1` brings the old way back.
- **What the friend's game stops doing, and my proposal for each** (calls its copy's callback makes that the host's
  mirror does not send; counts are scripts):
  - Voices (8: Xigbar, Final Xemnas, the Hades fight, three Halloween Town fighters) and sounds (2: Vexen, one
    Mushroom): today the friend hears its own copy's, also for the host's hits. Proposal: send them from the host
    through the mirror (two more calls in its list), so each plays once, on the host's say.
  - The HP floor (12: Shadow Stalker, Twin Lords, Hades, Scar's fight, Oogie, the Experiment, Hostile Program ...):
    the friend's game trims its own hits at its copy's floor before claiming. If its floor goes stale the friend's
    hits could stop counting in a phase. Proposal: bench it first (`bc`, Twin Lords, the floor logged in both
    games); if it differs, the host sends the floor with the HP it already sends.
  - Its own state while it has its own brain (Final Xemnas's shared move reads values the callback kept): bench
    `xend`; my reading is low risk, not proven.
  - Signals to the room's program (16), team changes (the steamboat's barrels), camera shake: these are field
    objects, most with no HP. **Left as today in step 1** (not cut) until each is looked at; a doubled one is a
    later row, not this step.
- Until step 2: a friend's hit the host never hears (no landed copy swing, spells, Limits) is answered by no
  script in either game. Today the friend's own copy answers it, where it mostly cannot show.

**Step 2: the host hears every friend hit once. Your two rulings are in (below).**
- Friend: every hit of our Sora on a bound enemy with HP is sent with the hit's data (part, reaction, kind,
  direction, knockback, the 0x30-byte attack record, the frame, the raw damage), from `HookApplyHit`, where the hit
  record is at hand; the HP-only claim of that hit is then not sent. A hit the game trims to 0 is sent too (F322).
  Summon and Limit hits stay HP-only: the host has nobody to stand in for their attacker.
- Host: the copy's swing lands no hit on an enemy with HP, **except while a script holds the copy** (the 1.84 rule:
  the friend sends no claims then). The claim runs the game's own class hit entry: sound, attacker recorded first,
  HP by the game's own rules from the raw damage, reaction, the tell, the class flinch: one run per hit.
- Where it runs is decided by a bench, not here: inside the enemy's update (where claims take HP today) or after
  all updates (the game's own order, F818; where hits on objects with no HP are played). A kill has been benched
  only in the first; the second is the game's order. Kill benches in both, then pick.
- Carried over or new: script gone before and after; 'a held player: damage, no reaction' (1.257); Mickey can't
  finish a boss and 'spared', now as changes to the hit's damage; the claim marked as ours while it plays, so its HP
  is not dropped as the copy's; the 1000-kill crowd's HP cache; the claim queue cleared at a room change and the room's instance in the claim; a hit whose kind would
  call through the stand-in attack (kind 5) given a real one or sent HP-only; a part whose collision the host's
  script switched off takes HP only.
- Parts go the same way; the stand-in tell (`PuppetTellScriptHit`) goes (fixes F876).
- Packet: 23 to about 112 bytes per hit (about 6.9 KB/s at the busiest second seen, 49 hits); a new size, so the
  two kinds stay apart; the version goes up; both PCs need the build, as for any DLL change.

**Step 3 (part of step 2, not optional): what the host's game then lacks.** The friend's hit sparks there come
from the copy's landed swing today: sent by cause, as E1 decided (the path companions' and Limits' hits use).
Medallions and mission counts made in host callbacks are checked to reach the friend.

## The two rulings for step 2 (yours, 2026-11-24; DECISIONS)
1. **Revenge:** a friend's hit adds the same as the host's, times the friend's own number in their
   `kh2coop_player.ini` (default 1; 1.2 = 1.2 times each hit's amount; 0 = none). It travels with each hit. How:
   the value (`actor+0xD48`, L2052) is read before and after the played hit and the part added is scaled. The
   scripts' own hit counters cannot be scaled: they count each friend hit as one.
2. **Whose view counts:** the friend's. An enemy reacts to every hit that landed in the friend's game, also one
   the host saw miss. So no age limit drops a reaction; an old claim still only loses its reaction when the
   enemy's state makes it unsafe (script gone, held player).
- Classes: 20 hit entries are known to reach the common handler (L2056); a class that does not shows up in
  the bench as a played hit with no tell, and then takes HP only.

## Smaller roads, weighed
- **Stop after step 1.** Fixes what you saw (F868, F878). The host keeps hearing about 3 of 4 friend hits, with
  the real attack; spells and Limits stay unheard; a part hit stays told twice unless the stand-in tell is dropped
  (it can be: one line, then a part hit is heard only when the copy's swing lands).
- **Tell from the claim, mute the copy's swing's tell (no change to what lands).** No sparks lost, HP and deaths
  stay where they are benched. But the flinch (from the swing) and the tell (from the claim) become two separate
  moments, and the 76 scripts that cancel the game's flinch to play their own could not cancel it: both would show.
- **Pair each claim with its swing.** The order varies both ways; it would need a time window. Not proposed.

## Risks I know
1. A fault inside a script we start unwinds past the game's cleanup (thread count, the running-thread pointers):
   `__try` alone is not a guard (F335). The checks before the call must rule the bad states out.
2. Step 1's condition never compares the object's id: an old entry can name a new actor at the same address for
   up to 30 frames (true of today's damage block too). The cut checks the id.
3. Step 1, for the 76 scripts that cancel the game's flinch: a bound copy plays no motion of its own, but the
   flinch's push and turn would now run in the friend's game until the host's position arrives; an attached enemy
   could be detached locally. Bench: positions and attachments, not motions.
4. Claims travel unordered and several land in one frame; the played hit uses one static record, as the game's
   own (F758).
5. Step 2 changes how every fight feels for the host; it cannot be proven on one boss.

## Benches (all two-copy, by script; then two PCs)
- Step 1: `storm_rider --what spikes --by B` (effect 100 once in each game, no second 'crash' act, the friend's
  copy told 0 times, the host as before), `--by A`, `--what bombs`, `--what rc`, `cerberus`, `test_census.py bc
  --only-friend` and `twins` (the floors logged, the friend's claims landing through each phase), `xend` (the
  shared move the same in both), `swarm` (kills), `room_smoke --warp D,2` and `core_hit` (objects with no HP as
  before), `storm_rider --what tpose --by B` (room changes).
- Step 2 adds: tells per claim = 1 on the host (never 0, never 2), kills in both play positions (`swarm`,
  `late_death`, `barbossa`), `luxord_cards` (1.257), `hydra` or `rapid_blow` (a friend's RC that hits a second
  enemy still costs it HP), the lag switch `KH2COOP_NET_SIM`, a new step that frees an enemy while a claim waits.
- Two PCs: DEBT_REVIEW section 4's row 'Hits and heals': each hit counted once, both ways.

## Debt
Hooks 165 to 167 in step 0 (the two above). No new private `Read`, no new address copy (`kActScriptHit`,
`kScriptEvent` in `puppet.cpp`, `kThreadOwner` in `aggro.cpp` exist; moving them to `game.hpp` is its own commit).
