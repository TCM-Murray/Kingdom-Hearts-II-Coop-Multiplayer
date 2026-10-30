# Effect sync: the plan for the other game's missing particles

Written 2026-10-28 (session 46), TODO 1.128. A plan, not a record: what happened goes to `MODLOG.md`, the facts found
on the way to `VERIFIED_OFFSETS.md`, the rules picked to `docs/DECISIONS.md` (E1: by cause). Built: steps 0-2 (step 2: the companions' effects, E2).

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
