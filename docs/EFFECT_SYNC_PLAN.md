# Effect sync: the plan for the other game's missing particles

Written 2026-10-28 (session 46), TODO 1.128. A plan, not a record: what happened goes to `MODLOG.md`, the facts found
on the way to `VERIFIED_OFFSETS.md`, the rules picked to `docs/DECISIONS.md` (E1: by cause). Built: steps 0-2 (step 2: the companions' effects, E2). Round 2 (Keyblade, forms, Limits, summons): section 7, planned.

## 1. The problem

The friend's game feels unresponsive: most of the particles the host's side makes are not drawn there. Your
friend's report (2026-10-28):

- sees their own spells and Limits, and the host's spells;
- Donald has never cast a spell on their screen;
- Goofy's shield stays in his hand when he throws it;
- no impact effects from the host's hits;
- the enemies react correctly to the host's hits;
- no doubled effects noticed (some may look too bright).

## 2. Why: effects are side effects, not messages

KH2 draws almost no effect on its own. Each comes from something the game runs: a motion's trigger frames, an attack
that lands, a script call. Whatever the friend's game does not run, it does not draw, and our design removed several
causes on purpose:

| Missing | Its cause, and why the friend's game doesn't run it |
|---|---|
| The host's hit sparks | The hit lands only in the host's game; the host's copy hits nothing in the friend's game, so each hit counts once |
| Donald's spells | The companions in the friend's game don't think (companion sync, DECISIONS C1-C3), so they never start a cast |
| Goofy's shield throw | The thrown shield is a separate flying attack the host's Goofy makes; the friend's Goofy plays the throw motion only |
| Limit and reaction-command particles (TODO 1.31) | The copy plays the motion with an empty effect instance (F155) |

What already works, and why:

| Works | How |
|---|---|
| Each player's own effects | Their own game runs them |
| The host's spells on the copy | `motion_mirror.cpp` replays the cast on the copy, with no hit |
| The host's enemies' and field objects' script effects | `world_mirror.cpp` replays their effect calls (start, move, scale, colour, end) |
| The enemies' reactions to the host's hits | The host's game sends their motions |

Tools that exist: `KH2COOP_FX_LOG=1` (`world_census.cpp`) logs every effect made, `exe+0x2BD510` (F157), with its
owner (an actor's own instance, the motion it plays, the running Limit), its PAX, the code that made it and the
script call running; `form_look.cpp` `StopEffectsOf` / `StopEffectsIn`; the 4 MB effect pool (`fx_pool.cpp`, 1.59).

## 3. The plan

Two mechanisms, because the report shows two kinds of gap: consequences of a hit (send the effect) and actions never
started (replay the action, whose own script makes the right effects, sounds and timing).

0. **Measure (bench, logs only).** Save 20 (Twin Lords, Donald and Goofy, enemies invincible): `FX_LOG` in both
   games while the host attacks, Donald casts and Goofy throws. `tools/fx_diff.py` groups the effects by owner,
   source and PAX entry and lists what the host's game made that the friend's didn't, the reverse, and doubles (the
   same effect on the same owner twice within a few frames: the 'too bright' ones). This ranks the rest.
1. **Hit sparks, host to friend: the effect event.** Hook `exe+0x2BD510`; for an effect on an actor both games have
   (a world-sync enemy by its net id, the Sora copy, a companion), send its owner, its PAX by file name and entry
   number (addresses differ between the games), its attach point and starting values. The friend's game makes the
   same effect from its own instance of that file. (Volpestyle's plan names this 'effects as events'; not built
   there.)
2. **The companions, host to friend: replay the action.** Extend `motion_mirror.cpp`'s cast replay to Donald and
   Goofy: their spells and the shield throw play on the friend's companion copies, with no hit and no MP (TODO 1.60,
   DECISIONS C3).
3. **The friend's sparks on the host's screen**: step 1 the other way.
4. **Limit and reaction-command particles (1.31).**

Each source gets its own switch, so a bad one can go off without the rest. After each: the bench diff (the list of
missing effects shrinks), then your two-PC check of how it looks.

## 4. Safety rules (each from a past crash or bug)

- **No doubles:** an event is dropped when the receiving game made the same effect on the same owner within a few
  frames (a motion's own triggers); every drop is logged. Doubles look tolerable (none seen on two PCs,
  2026-10-28), but they are counted.
- **Effect mode:** replay only while the effect system is in its field mode, `exe+0xB63628` = 1 (F292; the 1.68
  crash replayed an effect in another mode).
- **Files:** replay only when that PAX is loaded in the receiving game, with its whole effect instance (F155).
- **Lifetime:** a looping effect sends its end too (the 1.106 shine stayed on the copy).
- **Budget:** a cap on events per second, and the effect pool watched (F51: Final Xemnas filled it).

## 5. Open questions

- ~~Which effects step 0 finds missing~~ Step 0 (2026-10-30, Borough fight, F392, F393): no doubles today; the friend's game lacks only Donald's
  own effects; both Soras' swing effects (owned by their Keyblades) show in both games. Not settled: the hit sparks (shorter than
  the 0.2 s sampler), Goofy's throw (not clear in that fight), and a boss fight's effects per second.
- ~~Whether the hit spark is owned by the attacker, the victim or neither~~ The attacker's (or its weapon's) effect list, picked by a
  64-bit mask (F396); the Soras' sparks show in both games already, the companions' are sent (E2).
- Whether Goofy's thrown shield can be made by the friend's Goofy copy without it hitting (as the copy's spells).

## 6. The gaps left (review 2026-11-02, session 53; code and notes read, nothing measured)

Shown in both games already: each player's own effects; the other Sora's swings and hit sparks (its Keyblade's, F392);
spells on the copy both ways; enemies' and field objects' script effects and motions; Donald's casts and the
companions' hit effects (E2); prizes; the copy's Keyblade.

**Known** (seen in a run, or certain from the code):

| # | Gap | Screen | Cause the other game doesn't run | TODO |
|---|---|---|---|---|
| 1 | Limits on the copy: Sora's, Donald's Comet, Goofy's Knocksmash, world partners' | both | The copy plays the Limit's motion with an empty effect instance (`motion_mirror.cpp` `g_noEffects`, F155); the particles belong to the Limit object only the user's game makes; the PAX alone crashed (bench 2026-10-12) | 1.31, 1.60 |
| 2 | Drive Forms' own attack effects (Wisdom's shots, each form's effects, the shine's fade); Anti Form not shown at all | both | `form_look.cpp` swaps model, motions and form Keyblades; projectiles and form effects come from what the copy doesn't run (Wisdom: melee effects, no shots) | 1.24 |
| 3 | Summons (Chicken Little, Genie, Stitch, Peter Pan) | both | The summon isn't in the other game: an actor gap, not a particle one | 1.61 |
| 4 | Goofy's thrown shield in flight | friend's | A separate attack only the host's Goofy makes (its hits are sent, E2) | 5 above |
| 5 | Objects only the host's game makes (Land of Dragons wind gusts, the Twin Lords' minions) | friend's | No match in the friend's game | 1.53 |

**Probable** (from the code, never measured):

| # | Gap | Why |
|---|---|---|
| 6 | The other player's item use (Potion / Ether sparkle on the copy) | The copy's item trigger is blocked (`puppet.cpp` `HookItemTrigger`: item use crashed a clone in Volpestyle's tests); that trigger starts the item's effect |
| 7 | Impact sparks on the friend's own Sora from the host's enemies' projectiles and spawned attacks | Those attacks never hit in the friend's game (DECISIONS 1.79); the forwarded hit plays damage and flinch, but the spark comes from the attack-hit code (`exe+0x3F6410`, F396), which ran only in the host's game. Step 1 (hit sparks as events) is built for companions only (E2) |
| 8 | Finishers and abilities that make their own objects (Strike Raid's thrown Keyblade, Explosion, Magnet Burst, Ripple Drive) | Like Wisdom's shots; step 0 used an early save (97) without them |

**Unknown** (measure first): RC particles on the copy (1.31 says empty, but only Limits get the empty instance; RCs
replay through the source's motion set, `exe+0x3C6CC0`); the Drive's transformation burst and the Revert flash on the
copy; Glide, Quick Run, Aerial Dodge and High Jump effects in forms.

**Kinds of fix (E1, by cause):** effects without their source (1, 2, 6): give the copy what the source needs, or replay
the action as for spells; missing actors (3, 4, 5); effects of hits only the host lands (7): step 1, extended to
enemies. Gaps 1-3 and 8 are both ways: fixing the copy fixes both screens.

**Next:** measure before building, with `fx_sync0`, `KH2COOP_FX_COUNT=1` and `tools/fx_diff.py`: (a) a Drive Form with
attacks, (b) Limits (`test_extra_copy.py limit`), (c) a Potion, (d) a boss with projectiles. Most need fighting: your
pick which and when.

## 7. Round 2: Keyblade, Drive Forms, Limits, summons (planned 2026-11-10, session 66; nothing built)

Your ask (2026-11-10): many particles are missing in the friend's game, and the friend's copy shows few in the host's;
Keyblade, Drive Forms ('fusions'), summons and Limits first, the rest after. Still E1: by cause, a switch per cause, no
rewrite. Most of these gaps sit on the copy, so one fix shows on both screens.

| Area | Known | Not known | Likely fix (all general, not one moment) |
|---|---|---|---|
| Keyblade | Swings and hit sparks show in both games (F392, F394; save 97, Kingdom Key, early abilities). The copy wears the other player's own Keyblade with its effect file `obj/<blade>.a.us` (`copy_blade.cpp`) | What you see missing (which Keyblade, move, screen); finishers that make their own objects (Strike Raid, Explosion, Ripple Drive, Magnet Burst: gap 8); 1.124's light on the foot | Measure first; finishers likely as spells: replay the action |
| Drive Forms | The copy wears the form's model, motions and Keyblades (`form_look.cpp`); Wisdom's attacks make no shots | Which effect file the copy's own effect instance (`actor+0x80`) reads in a form: a real Drive rebuilds Sora as the form's object (F311) with its own `.a.us`, the copy keeps its body, and `form_look.cpp` never touches `+0x80` (read, not measured). The Drive's burst, the Revert's flash | If that's it: the copy gets an instance of the form's effect file while it wears the look, ended at the Revert. Wisdom's shots: their own step |
| Limits | The copy plays the Limit's motion with an empty effect instance (`g_noEffects`, F155): one holding only the PAX crashed (`exe+0x2C6B60`, 2026-10-12). The copy's Limit attacks hit nothing (F156, 1.118) | How many of a Limit's effects come from its motion triggers, its script (`trap_pax_start_bind` on the Limit's instance) or landed hits; whether the receiving game always has that Limit's PAX loaded (the preload loads its own party's Limits, L155) | A real instance from the receiver's own copy of the PAX, filled by the game's own call (`exe+0x2C6320`, lead L97), ended with the Limit; the script's effect calls sent as in E2 (a). Donald's Comet and Goofy's Knocksmash with them (1.60) |
| Summons | The summon isn't in the other game at all (1.61, 1.32, C3, Q13) | How the summon actor is made (its entry is a cutscene event with its own work block, F353) | Not a particle fix: an actor of its own in the other game (files, spawn, motions and place mirrored), whose own motions then make its particles. A track of its own |

**Order:**

0. **Measure** (bench, automatic, logs only). Prep: the effect counter (`KH2COOP_FX_COUNT`, off by default) also
   names each effect's file (the loaded file whose data holds the PAX). Bench `test_extra_copy.py fx_sync1`: save 59
   (all forms but Anti, Limits equipped), the infinite Drive gauge, enemies that can't die; each phase by the host,
   then by the friend: a combo with finishers, each form attacking (Drive, attacks, Revert), a Limit, a summon (save 52
   has Chicken Little). `tools/fx_diff.py` per phase: what the doer's game made on its Sora, Keyblade or Limit that the
   other game didn't make on the copy, with its cause (F394: motion trigger, script call, landed hit, motion's own
   instance). The causes pick the fixes; the table above is a guess until then.
1. **Limits:** the cause is known and `motion_mirror.cpp` already sends the Limit's PAX.
2. **Drive Forms:** the `+0x80` question first, a cheap check.
3. **Keyblade:** what step 0 and your example leave.
4. **Summons:** TODO 1.61, its own plan.

**Rules on top of section 4:** each effect instance we make is kept only while the Limit or look that made it is the
same one (the Limit object at `exe+0x2A24CC0`, the copy's look) and is ended before its file can move (1.136 and L145
were effects outliving their files). After each step: the bench diff, then your two-PC look.

**Your picks (2026-11-10):** 'Keyblade' meant the Drive Forms' attack effects: the copy in a form shows normal
Sora's (Final Form with Explosion, Finishing Leap and Guard Break effects in its combo; Wisdom's melee effects); normal
Keyblade effects look right. Order: step 0, then Limits, then Drive Forms. Summons wait until the summon itself shows in
the other game (1.61).

**Step 0 results (`fx_sync1`, runs 1-3, 2026-11-10; MODLOG 66):**
- **Drive Forms (F473-F475):** the copy makes every effect from normal Sora's file (`P_EX100.a.us`), never the form's
  (`P_EX100_<form>.a.us`): all 5 forms, both ways. In Valor and Limit Form its trigger entries are mostly the doer's,
  so the wrong file is most of it there. In Wisdom, Master and Final they share only entries 1-8: the copy makes
  others the doer never does (Final 20, 38, 39 against the doer's 9, 26, 27), so there it runs other motions or
  triggers, consistent with your normal-Sora finishers (Explosion, Finishing Leap, Guard Break) and Wisdom's melee
  effects; no entry was matched to a named effect yet. Also missing on the copy: Wisdom's shots and hits (F474), the
  Drive's and Revert's own effects (F475).
- **Limits (F470, F471; one sample each):** Goofy's `0xFF` (host to friend): its effects come from the running Limit's
  instance, 197 of 205 at landed hits (the hit effect maker, as E2's companions) and 7 by motion triggers; the copy
  makes none. A Limit with no partner (`0x26C`) has no Limit-instance effects; only its Keyblade's hit sparks are
  missing. Friend to host with a partner: no data (1.119).
- **A crash found on the way (TODO 1.119, L128):** twice out of two, the friend's Donald-partner Limit aborted the host's
  game (a pure virtual call in our Limit motion replay, just after the game's Limit preload dropped the Limit files).
  The friend's no-partner Limits (run 1, 3 times) dropped nothing and didn't.

**So the fixes are:** forms: the copy's own effect instance reads the form's effect file while it wears the look (Valor,
Limit Form); for Wisdom, Master and Final first find which motions the copy plays against the doer's; then the Drive /
Revert effects and Wisdom's shots. Limits: 1.119 first (the same replay); then the copy's Limit motion gets a real
instance of the Limit's PAX for its triggers, and the Limit's landed-hit effects are sent like E2 (b).
