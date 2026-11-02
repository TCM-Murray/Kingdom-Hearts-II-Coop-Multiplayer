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
