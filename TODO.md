# KH2 Co-op: master to-do list

Last updated: 2026-10-28 (session 40; trimmed again so that every item is a few lines. The full text of every item as it stood before this trim, word for word, is in [docs/TODO_HISTORY.md](docs/TODO_HISTORY.md), which also holds the 2026-10-14 snapshot).

This file is **the plan**: what's left, in what order, and what we're ignoring on purpose. `MODLOG.md` is the diary (what happened, with evidence), `VERIFIED_OFFSETS.md` the address book, `docs/DECISIONS.md` the rules we decided.

## The goal: what "playable" means
Two players on two PCs play KH2FM **from Sora's first steps (right after the Roxas prologue) to the end of the final Xemnas fight**:
- no crashes or soft locks along the story route;
- every required fight can be fought together;
- cutscenes play on both screens (the host starts them);
- each player keeps **their own Sora**: level, HP, items, Keyblades, gear, abilities, munny;
- both saves move through the story together;
- parts that can't be co-op (minigames, Gummi Ship, a few forced-solo scenes) don't break anything. The second player waits, then gets pulled back in automatically.

## How to read this file
- **P0** = blocks the start-to-finish run. **P1** = needed for it to play well. **P2** = polish / later. **Parked** = ignored on purpose for now.
- `[ ]` open, `[~]` built but not tested yet, `[x]` done.
- Numbers (1.3, 4.2...) never change, so we can say "let's do 4.2 next". Tracks 1 to 4 are the four priorities you and your friend agreed on, in that order.
- **Host** = the PC that owns the world (yours). **Friend** = the second PC. **Copy** = the other player's Sora as it appears in your game.
- Each item says the problem, where it stands and the next step, in a few lines. For how we got there (runs, benches, logs, causes), look the number up in `docs/TODO_HISTORY.md`.

---

## Where we are (end of session 40, 2026-10-22)
- **Works on two PCs:** connecting over Steam or Radmin; co-op saves kept apart from Steam saves; the copy beside Donald and Goofy, with its own party gauge; rooms, loads, cutscenes and the World Map in step; shared enemies and hits (each counted once), sent in parts in big fights; the host's missions, timers and gauges; flinches and knockback from the host's hits, enemies aiming at the right player; downed state, shared Game Over and pause; Mickey's rescue with Mickey on both screens; the 'Connection lost' screen and rejoining; spells, Limits and Drive Forms shown on the other Sora; companions leaving and returning together, their KO, HP and MP from the host; grabs letting go at once; Data Xigbar's arena changes and sniper commands; Data Luxord's die and card; the diagnostic panel.
- **One brain** (1.55): the friend's enemies copy the host's instead of thinking for themselves. The only way since session 35; it has played the whole final boss on two PCs with no crash, and each session since copied more of the bosses' script calls to the friend's game.
- **Story played in co-op:** Twilight Town to Hollow Bastion's first visit; the Land of Dragons' first visit through Shan-Yu; boss fights in Olympus, Beast's Castle and Agrabah, the final fights to the credits, and the Data Organization fights (Roxas, Xemnas, Xigbar, Luxord, Larxene, Xaldin, Final Xemnas), all from co-op saves.
- **Built, waiting for a test (all committed, none tried on two PCs):** the session 40 fixes 1.106, 1.107, 1.108 (P0), 1.109, 1.112, 1.114, the crash report of 1.113, the KO face while down (9.2), the camera guard (1.99); older: Once More (1.77), Data Xigbar's attack-carried commands (1.83, 1.85), the swarm budget refund (1.96), Armored Xemnas II's buildings for the friend (1.49), the Goofy Limit log (1.65).
- **Progress estimate** (the README's count: P0 three times, P1 twice, P2 once, built-but-untested as half, crossed-out left out): 51% on 2026-10-22 (50% after the TODO review earlier that day, 47% before it with the old markers), 51% on 2026-10-17 after the session 37 review (38% before it), 35% on 2026-10-14, 33% on 2026-10-12, about 20% on 2026-10-08. Two-PC runs keep adding items, which holds the share down.
- **Biggest gaps:** two open crashes, the Keyblade change in the pause menu (1.110, P0, built, bench passed) and the friend's game in Data Final Xemnas (1.113, P0); story progress sync (5.1); the Gummi Ship (5.2); the friend's game short of actors in swarm fights (1.96, P0); the summon and the forms' own attacks in the other game (1.61, 1.24); the bosses' special parts on the friend's side (1.82, 1.86-1.88, 1.92, 1.94, 1.97, 1.104); missions and timers in the other worlds (1.22); the dev suite (Track 2).

---

## Decisions
Every rule decided for how co-op behaves, what the code does today and whether the two agree: **[docs/DECISIONS.md](docs/DECISIONS.md)**. Its 'Conflicts and open questions' list comes first.

---

## Track 1: combat and world sync (priority 1)

### Open bugs (from sessions 5 to 9)
- [x] **1.1 P0. Hollow Bastion turrets (Q7).** The friend's turret hits no longer count twice; with one brain, the friend's turret follows the host's (bench).
- [ ] **1.2 P0. Shadows going underground on the friend's side, and enemies with similar states.** The flat look is fixed, and one brain fixed their timing (two PCs, session 33); Neoshadows disappear and come back instead. **Next:** check other enemies with special states (hidden, shielded, flying) with one brain on two PCs. Dark Thorn's invisibility does not vanish in the game's client, remaining invisible through the whole second phase.
- [x] **1.3 P0. Mickey rescue in co-op (Q6).** Done on two PCs 2026-10-22 (commits 90c298b, 74f38d6, `mickey_sync.cpp`): one roll for both, the loser's white screen has no choices, and its Sora stays down, a real Mickey on both screens puppeted by the winner's, Healing Light gets both up together, Mickey's last HP leaves the loser in a normal down, the loser's HUD and menu show its own Sora. Left in their own items: 1.109, 1.95, 1.23 (Mickey in a form).
- [x] **1.4 P1. Enemies spawning and vanishing in a loop when the friend is far from the host.** Fixed: far enemies treat the copy as the player (two PCs, session 18).
- [x] **1.5 P1. Lock-on on enemies.** Works (session 11).
- [x] **1.6 P1. Hit reaction on the friend's screen.** The friend flinches and loses HP (session 11).
- [x] **1.7 P1. World allies' heals.** Auron heals the copies (session 11).
- [x] **1.8 P2. Limit gauge drains normally** (session 11).
- [x] ~~**1.9 P2. Cure reach for healing the partner:** 500 units is a guess; tune it by feel.~~
- [x] **1.10 P2. Continue after Game Over counts as a new room for world sync** (bench, session 28).

### Built but never tested in a real situation
- [x] **1.11** The host's Auron Limit seen from the friend: works (session 11). Real animations = 1.31.
- [x] **1.12** Both players entering a spawn area at once: enemies appear once (session 11).
- [ ] **1.13** Rooms with random enemy sets: the friend should get the host's pick.
- [x] **1.14** Enemies that come in waves; groups started by missions or signals.
- [ ] **1.15** "Spawn near the player" enemies triggered by the friend.
- [x] **1.16 P0. Battle barriers** (invisible walls until every enemy is dead) must drop on both screens together.
- [x] **1.17 P0. EXP on the friend:** shared; both players get it at any distance (two PCs, session 18b). Form EXP is 4.5.
- [ ] **1.18 Pause edge cases.** A very fast pause-unpause-pause can still leave one game unpaused; your call: minor, leave it. Left to try: both pressing pause at once, the host's pause menu with enemies alive.
- [x] **1.19** Enemies that exist on the host but never spawn on the friend (invisible attackers).

### New work needed for the whole route
- [ ] **1.20 P0. Boss support.** With one brain the friend's bosses follow the host's; Final Xemnas's desperation move plays in both games, and he always grabs the host's Sora for the Riku part (story and Data versions, commit 54e2ab9). Found on two PCs 2026-10-22: Data Xaldin's Jump to the wrong player (1.107) and Data Final Xemnas's 1.111-1.114. **Next:** walk the rest of the route boss by boss and log each one's problems here.
- [x] **1.21 P0. Boss HP bars and boss name** on the friend's screen. The bar follows the host's hits, about 1 s behind (two PCs, session 33). **Open:** the boss name, the delay.
- [ ] **1.22 P0. Missions and special rules** owned by the host and shown on the friend: timers, gauges, counters, Luxord's time gauge, cleared or failed in both games. Built (session 36, commit 4f54fa6, `mission_sync.cpp`); the Land of Dragons' first morale fight works on two PCs. **Next:** the other story missions (Hades escape, protecting Minnie, the 1000 Heartless) as the playthrough reaches them.
- [ ] **1.23 P0. Drive Forms and summons don't break co-op.** Drives, Valor and summons: no crash on two PCs (session 33); downed in a form ends the form first; the Valor level-up crash is 1.102. **Open:** Anti Form on two PCs, Mickey in a form.
- [ ] **1.24 P1. Drive Forms shown on the other Sora (Q5: the copy takes the form's model and motions, all forms).** Done on two PCs 2026-10-22 (commit d8252f4, `form_look.cpp`): model, motions and Keyblades on the copy, the files loaded on demand and put on again when the game's file layout moves them, the shimmer stopped. **Open:** Wisdom's shots and each form's own attack effects, Anti Form on the copy, the shine's fade, two forms in bigger rooms, a form gauge per player; see 1.104, 1.106.
- [x] **1.25 P1. Limits with every partner.** Auron, Mulan, Aladdin, and Trinity work. **Next:** check the others, Riku included (see 1.60).
- [ ] **1.26 P0/P1. Reaction commands (Q8).** Work on two PCs: the TWTNW cylinder jumps, the shield break, Cerberus's; a boss's command for the friend goes to the friend's menu (1.62); the friend's presses from the per-enemy list run on the host, first press wins. Routing since 2026-10-22: see 1.107. **Open:** Shan-Yu's clash shows in the wrong place on the other screen; a 'watch the other player's command' option.
- [ ] **1.27 P1. Grabs, stun, freeze, knockback, guard, and reflect** on the copy and on mirrored enemies. Enemy grabs of the copy already run on the friend's own Sora (1.62). **Next:** check the rest.
- [~] **1.28 P1. Projectiles and area attacks:** who gets hit where (the copy is a little behind the real player). Many enemies now aim at the correct Sora. Needs exhaustive testing.
- [x] **1.29 P1. Fights with a required ally** (Beast, Simba, Mulan, Riku, Mickey): the ally behaves the same on both screens.
- [x] **1.30 P0. The World That Never Was party:** Riku and the copy are both there; the final fights reach the credits on two PCs with no crash (session 33).
- [ ] **1.31 P1. The other player's Limits and reaction commands on the copy.** Spells and Limit and reaction-command motions show on the other screen. **Open:** the Limit and reaction-command particles (the copy gets an empty effect).
- [ ] **1.32 P1. Summons fully co-op (Q13):** the copy stays, and the summon joins; one summon at a time across both players; the summon shows on the other screen. See 1.61.
- [x] **1.33 P0. Crash on a spell cast by the copy:** fixed (two PCs, session 13).
- [x] **1.34 P0. Lone Continue after Game Over loops on a white screen:** fixed, the friend's Game Over is locked while the host is connected (session 13).
- [x] **1.35 P0. The copy jumping to a leftover Drive Form body:** fixed (two PCs, session 18).
- [x] **1.36 P1. Drop bonuses counted twice:** fixed, normal drop rates (two PCs, session 18).
- [x] **1.37 P0. Enemies that vanish on the host giving the friend EXP and drops:** fixed (two PCs, session 18b).
- [x] ~~**1.38 P2. EXP mismatch with EXP Boost** between the two games (bench, session 18): two readings don't fit the boost. **Next:** find which HP the boost reads.~~
- [x] **1.39 P1. Enemies stay put when a player goes down alone:** they wait near the downed player (two PCs, session 18b).
- [x] **1.40 P1. Hits counted twice:** the friend's damage counts only through its own hits (two PCs, session 33).
- [x] **1.41 P0. Friend crash with a Shadow in a T-pose:** fixed (story run, session 20).
- [x] **1.42 P1. Waiting enemies ignore companions:** a companion who is up wakes them (two PCs, session 20).
- [x] **1.43 P1. A companion knocked out on the host isn't knocked out on the friend's screen.** Your call: the full KO state, the game's way (DECISIONS C3). Done with companion sync (1.98 step 2, commit 2ef3cd8): the host sends each companion's HP, MP and KO, and the friend's copies take them through the game's own HP change. Two PCs 2026-10-22.
- [ ] **1.44 P1. A group the friend triggers comes out one enemy at a time, far apart** (Hollow Bastion, session 20). The friend's position does trigger the group; the cause isn't found yet. **Suspect:** the host's game removing and remaking enemies near the friend (1.45).
- [x] **1.45 P1. An immortal enemy on the friend's screen** (session 20): an enemy removed on the friend's side kept running. Closed. Reopen if retriggered.
- [x] **1.46 P0. Friend crash when a companion uses an item on a low Sora:** guard added, no crash with Potions on two PCs (session 33).
- [ ] **1.47 P1. Samurai duel reaction command:** after one player uses it, the other can still use it and then freezes until the fight ends. **Next:** retest with one brain and 1.62.
- [x] **1.48 P1. Ship fight: the friend's screen ignored positions past 100,000:** fixed (two PCs, session 28).
- [~] **1.49 P1. Armored Xemnas II.** Two PCs 2026-10-24: the friend's building commands (645, 646) ran in its own game only: the host's shield never broke (F324); pressing with the host left the friend held (F325). Built (committed 319a2f0; your pick A): the friend's field-object commands go to the host, whose object runs them with the copy (`FIELD_RC_LOCAL=1`: the old way). **Test:** two PCs: the friend breaks the shield; both press at once; the cylinder jumps.
- [ ] **1.50 P1. Ship fight: the friend's game stays in its own part of the fight.** Each game runs its own copy of the fight; the friend's stays at the tail while the host moves on, and the ship's HP bar isn't in sync. **Next:** research a fight-part sync.
- [ ] **1.51 P1. A companion swapped with the Party command isn't swapped in the friend's game.** Both ways of swapping (command and menu) need catching. Planned in the companion sync round (DECISIONS C3), not built.
- [x] **1.52 P1. The friend's companions follow only the host's** (your idea): same members, the host's moves and voices, no decisions of their own; since 1.98 step 2 their copies don't think at all (two PCs 2026-10-22).
- [ ] **1.53 P1. Objects and effects only the host's game shows.** With one brain, objects the host's scripts make appear for the friend (Luxord's cards, the buildings that hurt on contact; Data Larxene's clones on two PCs 2026-10-21). **Open:** the Land of Dragons wind gusts, the Twin Lords' minions.
- [x] **1.54 P1. Beast's Castle out of step on the friend's screen:** an enemy failing to spawn on the friend, the boss going invisible. Closed with one brain.
- [ ] **1.55 P1. One brain, the host's (the general world mirror).** The only way since session 35; played on two PCs through the final fights. Each fix copied one more kind of host script call to the friend (fades, effects and their moves, motion speed and timing, strikes, grabs, reaction commands, lights, drops, damage sheets, each enemy's target). **Open:** save points, chests and other field objects under one brain; the gaps in 1.53 and 1.63.
- [ ] **1.56 P2. The copy's motion jumps at the end of a spell** (a hard cut, no blending). **Idea:** a short blend for the copy's motion changes.
- [x] **1.57 P1. The friend's Limit partner stays with the host in the host's game:** fixed, Mulan and Aladdin go to whoever uses the Limit (two PCs, session 33).
- [x] **1.58 P1. Companions heal the other player's copy:** built and benched (session 34), works on two PCs (2026-10-15). The friend's Donald calling 'Sora!' while trying to Cure came from its own think, which 1.98 step 2 stopped (two PCs 2026-10-22).
- [x] **1.59 P0. Friend crash in Data Roxas (effect pool full):** the game gets a 4 MB pool (`fx_pool.cpp`, `docs/EFFECT_POOL_PEAKS.md`); no crash since. Done (your OK, session 36).
- [ ] **1.60 P1. Donald's and Goofy's Limits on two PCs.** Your rule: one Limit at a time, and every ally but the partner and the copy leaves; built, and Limits looked as expected on two PCs (session 35). **Open:** Donald's Comet and Goofy's Knocksmash particles on the host's screen; the friend's Limit with Riku on the host's screen.
- [ ] **1.61 P1. Summons in the other game:** the companions now leave and come back there (1.98 step 1, two PCs 2026-10-22); the other player still doesn't see the summon. Planned in the companion sync round (DECISIONS C3), not built.
- [ ] **1.62 P1. Reaction commands and grabs for one player at a time.** Works: Data Roxas's steal reaches the friend and lets go at once (commit 82d1ed1), a grabber's scene camera plays in the friend's game. **Open:** your call on the grab camera (both games for every boss, an exception for Final Xemnas, or research first); the friend's camera stuck on Data Xemnas's shot after his second reaction command: fixed (hold cap 15 s -> 60 s, F365; your dual bench 2026-10-28); the friend's chain prompts on the host's screen: built, not tried (F366); prompts a boss turns on for itself may not reach the friend; Data Roxas's stolen Keyblades follow the host (no item yet).
- [ ] **1.63 P1. Data Saïx's Berserk shows a T-pose on the friend's screen** (bench). **Lead:** his script loads a second motion file that the friend's game never loads. **Next:** compare both games at Berserk.
- [ ] **1.64 P1. Held and attached things drawn in the wrong place on the friend's screen** (Roxas slightly rotated in the clash). Built and benched: the sender sends where the actor is drawn; the bone's tilt is not sent yet. **Next:** your check in game. **Open:** many other reaction commands and clashes still have the wrong camera view.
- [ ] **1.65 P1. Limit motions that show idle on the other player's copy.** Trinity works (its file is found by its contents). **Open:** a Goofy Limit's file wasn't found in the host's game; logging added to find out why.
- [x] **1.66 P1. Some of Larxene's attacks (the 3-4 thunderbolts, her Absent Silhouette) didn't hurt the friend's Sora** (2026-10-15). Closed in the session 37 review; reopen if seen again.
- [x] **1.67 P1. Status effects on allies (frozen, burning) didn't show in the friend's game:** the capture's grabber was found by memory address, which fails for objects made during the fight. Fixed (session 36): found through world sync's host-to-copy pairing; two-copy bench. Not yet seen on two PCs.
- [x] **1.68 P0. The friend's game crashed entering Imperial Square** (2026-10-15): it ran the host's effect call for a broken field object while its effects were in another mode. Fixed (session 36): replayed effect starts wait up to 1 s for the field's mode and are dropped otherwise. Not yet tried on two PCs (needs the same timing).
- [x] **1.69 P0. Missions not shared between an English and a Spanish game:** mission files sit in a language folder and mission sync compared the whole path. Fixed (session 36): only the file name counts. Needs a two-PC run in two languages.
- [ ] **1.70 P2. Smaller things from the two-PC run of 2026-10-15:** (1) the friend's reaction commands on regular enemies refused 76 times ('not offered lately'; that build lacked the own-list fix 6ce21bc, retest); (2) Rapid Thrusters stay in the friend's game after the host's are gone; (3) the host's object 0x558 couldn't be made in the friend's game; (4) many Land of Dragons field objects parked with no host match; (5) an exception in the friend's mission task at a Game Over (caught).
- [ ] **1.71 P1. Shan-Yu's hawk lifts the friend's Sora but doesn't carry it** (2026-10-15): the hawk attaches the grabbed Sora to itself, and grab sync only sends attaches the other way round (Dark Thorn, `F_BB080` and Final Xemnas's Riku do the same). **Next:** send an attach of the copy to an enemy too, run on the friend's Sora with its copy of the enemy; the let go detaches it.
- [x] **1.72 P0. The friend's game stayed on a black screen loading a save after a Game Over:** the door lock refused the save's room because the host was elsewhere. Fixed: a room change asked for while the friend's Game Over load screen is open passes the lock. Works on two PCs (2026-10-17, commit 7c1de6c).
- [x] **1.73 P1. The friend's yellow target may pick the enemy near the host's Sora:** not reproduced; the game measures from the lock-on's player, which is ours. Closed 2026-10-16 (your call): most likely the missing hit feedback (1.79).
- [x] **1.74 P1. Land of Dragons' first morale fight had no enemies in the friend's game:** the host's world packet (~1,900 bytes) exceeded the friend's 1,500-byte buffer. Fixed (your pick 'split in parts'): world sync sends each frame in parts of at most 1,200 bytes (packet v6), up to 64 enemies. Works on two PCs (2026-10-16: host 35, ours 35, matched 35).
- [x] **1.75 P0. The friend took damage during its own Limit:** the host decides hits on the copy, which has no Limit protection. Fixed (session 37, option b): the friend's game asks the game's own protection check on its own Sora before a forwarded hit. Closed in the session 37 review after the two-PC runs.
- [ ] **1.76 P1. The friend's Reflect doesn't block enemy hits** (2026-10-16). Bench: Reflect sets no protection flag; its barrier is an attack object made in both games (the host's copy casts it ~0.1 s later), so 1.75 doesn't cover it, and no forwarded hit landed inside a Reflect window. Guess: the reported hits came before the host's barrier was up. **Next:** watch the friend's log in a run; if hits get through, drop forwarded hits while its barrier is up. Guard not tried.
- [x] **1.77 P0. Second Chance and Once More for the friend:** a forwarded hit skipped them. Fixed (session 37, commit a510f28): the host sends the raw damage and the friend's game runs the player's survive rule on its own Sora, with the hit-reaction state from just before the friend's own hit. Two PCs 2026-10-24: both work (your report).
- [x] **1.78 P2. The friend's Limit worked in rooms without companions,** because the Sora copy filled the empty party slot. Your call: like the game. Fixed: a Limit is greyed and refused while neither party slot holds a real companion. Works on two PCs (2026-10-16).
- [x] **1.79 P1. The friend got no flinch or knockback from most hits:** its Sora reacted only to its own game's hits. Fixed (session 37, commit 37a5310): the host sends the hit's direction and knockback and the friend's game plays the game's own reaction with the damage ~100 ms after arrival, unless its own game hit its Sora within 300 ms. Two PCs 2026-10-17: 'substantially better'. A full 'the friend decides its own hits' switch stays an idea.
- [x] **1.80 P1. In the friend's game the host's enemies' aimed attacks went for the wrong player** (the copy sat in the target slot). Fixed (session 37, commit 49b9ca1): the host sends whom each enemy goes for and the friend's game writes it into its copy's target slot each frame. Two PCs 2026-10-17: Xigbar's shots went for and hit the friend's Sora.
- [x] **1.81 P0. Data Xigbar's arena change placed only one Sora, so the other fell out.** Fixed (commit 6bc3cc8): a script that swaps the room's parts and places one Sora now places the other too ('arena place'). Pair bench 2026-10-17: 7 arena changes, both on the floor.
- [ ] **1.82 P1. Data Xigbar's sniper view shows only in the host's game,** and his sniper phase shoots the player he aims at, the nearer one at its start (two PCs 2026-10-21). **Proposal** (session 39 MODLOG): the scope calls run in the game of the player he aims at, like grab sync's cameras; the other keeps a free camera. Your pick pending; not built.
- [x] **1.83 P1. The friend's reaction command against Data Xigbar's bouncing shot faulted on the host:** the host's run named no attack, and Xigbar's script asks the pressed attack for its receiver. Fixed (commit ff87a62): the host runs the friend's press with its matching attack in the second handle and the part the friend named. Two PCs 2026-10-24: works (your report); a side effect on the friend's bullets is its own item.
- [x] **1.84 P1. The friend's sniper reaction commands against Data Xigbar did no damage:** his script's hits for the copy were dropped by the 1.40 rule. Fixed (commit 72b2cf9): a copy's hit counts on the host while a script holds the copy, and the friend claims nothing meanwhile. Pair bench 2026-10-17: -4 per hit, as in the host's own round.
- [x] **1.85 P1. The friend never got the third sniper command against Data Xigbar:** the second 598 comes from an attack that carries its own command, which the friend's game doesn't have. Fixed (commit ff87a62, `reaction_sync.cpp` OfferAttackCommands): the host runs the game's own checks on its attacks for the copy and offers them to the friend. Two PCs 2026-10-24: the friend does the third command (your report).
- [ ] **1.86 P1. After the friend's sniper commands its Sora turns invisible, and so does the copy** (pair bench 2026-10-17). The script's end (sysjump + fall motion 4) is cut by puppet.cpp on the copy and never sent to the friend; at motion 256 the copy is drawn at (0, -50, 0). No hide call seen yet (the script-call log never came on). **Next:** a run with `KH2COOP_TRAP_LOG=1` on the host.
- [ ] **1.87 P1. Sora 'invincible' after the sniper reaction command: flinches but takes no damage** (your report, pair bench 2026-10-17). Not seen in the logs. Waiting for which Sora and when (during the command is normal).
- [ ] **1.88 P1. Data Xigbar disappeared from the fight** (pair bench 2026-10-17, both games; he still existed in both). Likely hidden or stuck in his script after a failed 492 (1.83); not seen since. Reopen if it happens again.
- [x] **1.89 P1. The sniper command's animations played where the friend's Sora stood, not on the bullet.** Fixed (commit 0dbf0bb, 'held place'): while a grab holds the copy the host sends where it is drawn, and the friend's game moves its Sora onto it. Pair bench 2026-10-17: onto the bullet on both screens, no jitter.
- [ ] **1.90 P1. Data Luxord's die and card for the friend.** Built (commits 8fda15b, 1d94c08): his 'the player' calls go to the friend while the copy is the player. Two PCs 2026-10-24: the friend never became the die or card (stuck until time ran out), and after a lost desperation minigame the host's Sora became the die (F329): those calls come from a script thread holding the copy, where 'the player' is still the host's Sora. **Next:** proposed: point 'the player' at the copy in that thread.
- [ ] **1.91 P2. Second Chance: the friend keeps it on a forwarded lethal hit, the host's debug lethal hit skips it** (the `downed` bench). Likely bench-only: the debug hit calls our HP hook after the game's hit handler, where Second Chance applies. Not yet checked with a real hit on the host.
- [ ] **1.92 P1. Data Luxord died early in co-op with no desperation move** (two PCs 2026-10-17): something put him at 1 HP in the host's game and the friend's game claimed a 1013 hit at the same moment. Pair bench later that night: the move worked, no early death. **Next:** if it recurs, benches with `bosshp` and the script-call and claim logs on.
- [x] **1.93 P1. A downed die / card:** the friend lay as the die and vanished after the revive. Your rule: back to Sora, then down. Done (commit d27ee7d): downed.cpp ends the capture like a Drive Form before the down; pair bench 2026-10-17. Not yet seen on two PCs.
- [ ] **1.94 P1. Data Luxord stalls in his desperation phase:** his script asked `trap_obj_is_star` about the host's Sora, got 1 (protected), and stopped. Why the host's Sora reads protected: not known; `KH2COOP_STAR_LOG=1` logs the check's fields (commit cc13dec). **Next:** a bench with the log on.
- [ ] **1.95 P2 (polish). Mickey's help stars over both Soras:** while the winner plays Mickey, the stars show only over the winner's own Sora, not over the loser's copy. Not needed for 1.3.
- [ ] **1.96 P0. The friend's game runs out of actor budget in swarm fights** (the Land of Dragons climb; the 1000 Heartless are at risk too): removed copies keep their weight. Built (commit fb6a46f): they get the game's shutdown start 1.5 s after removal. Two PCs 2026-10-24: the climb plays (your report), enemies ~1 s late on the friend's side; the budget stays near full (235 failed makes, F327). **Next:** proposed: free the budget sooner; then a 1000 Heartless bench.
- [x] **1.97 P1. Shan-Yu's clash: the friend's Sora doesn't push him back** (2026-10-18). Cause: his script pushes the held copy with trap_obj_hitback, which grab sync didn't send (L136). Fixed (commit 54870f0): the push goes to the friend while an enemy holds the copy; your one-PC run 2026-10-25: 'night and day'.
- [x] **1.98 P1. Companion sync (DECISIONS C1-C3):** Drives and summons send the companions away and bring them back on both screens; their KO, HP, MP and MP recharge come from the host's game; the friend's copies don't think, and the friend's Cures and items on them go to the host. Done on two PCs 2026-10-22 (commit 2ef3cd8, `companion_sync.cpp`). Steps 3-4 are 1.51 and 1.61; open from the runs: 1.103, 1.108; Goofy's move effects on the friend's screen not looked at.
- [x] **1.99 P1. The host's boss reaction command took the friend's camera** (Shan-Yu's clash, 2026-10-21): the friend's game ran the host's script thread on the copy. Fixed (commit 1233cba; `KH2COOP_COPY_HOOK_GUARD=0` turns it off): such a thread is ended unrun. Two PCs 2026-10-24: the camera stays with each player (your report). Its sister case on the friend's own Sora is 1.115.
- [ ] **1.100 P1. The host's ground combos stopped after the first swing for ~2.5 min** (Land of Dragons, 2026-10-21); no cause in the logs. Built (commit bee978e): `KH2COOP_COMBO_TRACE` logs every Attack press and attack motion, and who 'the player' is. Didn't recur on 2026-10-22. **Next:** if it comes back, read the 'combo:' lines around it.
- [ ] **1.101 P1. A reaction command the friend presses twice runs twice in the host's game** (Data Xigbar, 2026-10-21): the friend's game keeps our offer in its menu for a few frames. **Proposed (not built):** one friend's use per offer; the friend's menu drops the offer once pressed.
- [x] **1.102 P0. Crash: a Valor level-up with the copy in the room** (2026-10-21): the copy shares our Sora's stat sheet and wrote 'no form' into its form link. Fixed (commit b64e6ad, `KH2COOP_SORA_SHEET_FIX`): the copy's sheet refreshes skip the form write and our link is set back each frame. Two PCs 2026-10-24: Drive level-ups work (your report).
- [ ] **1.103 P2. The friend still saw his Goofy while the host was in Valor** (2026-10-21), though his game sent Goofy away and his HUD dropped him. Bench `companions --slot 10`: Goofy left and came back with the Revert. **Open:** which of Goofy's returns the friend saw (the 08/02 load, or the host's crash at 20:07:58).
- [x] **1.104 P1. The friend's own Wisdom Sora gets stuck in a T-pose when jumping after a room change in Wisdom** (two PCs 2026-10-22). Not seen 2026-10-24 (your report: Wisdom jumps with the other player in Valor); no fix of its own. Reopen if the T-pose comes back.
- [ ] **1.105 P2. After a Game Over Continue the friend came back in Wisdom** (2026-10-22; it had entered that room in Wisdom). Probably the game's own room-entry checkpoint. **Next:** check without the mod; nothing to fix if the game does the same.
- [~] **1.106 P1. The Drive's shine stays on the copy after a room change in a form** (2026-10-22): the new copy plays motion 53 after our look goes on, and the stop ran only at the look change. Built (commit cdb9f18): the 1.5 s check stops those effects again while the copy wears a form; bench `form_room` prepared. **Test:** two PCs, a room change in a form: the copy stays clean.
- [x] **1.107 P1. Data Xaldin: the host's Learn gave the Jump to the friend** (2026-10-22). Routing built (commit 6bb9d9d). Two PCs 2026-10-24: it doesn't follow the rule built, but you keep what it does: when one Sora won't use all its Jumps, the other's Learn takes the rest (DECISIONS Q8).
- [~] **1.108 P0. The friend's Limit kept the host's companion away after it ended** (Mulan's Limit, 2026-10-22: Goofy didn't come back, Valor greyed until the next room): limit sync cleared its end flag after the bring-back. Built (commit 36c27f1): cleared first; bench `limit --by B` checks it. **Test:** two PCs, the friend's Limit with a world ally.
- [x] **1.109 P1. Mickey can finish a boss through the friend's claims** (Shan-Yu, 2026-10-22). Fixed (commit 6559cf0): while the friend's Mickey is out, its claims leave a boss at 1 HP or more. Two PCs 2026-10-24: works (your report).
- [x] **1.110 P0. Crash: changing Keyblades in the pause menu with the Sora copy in the room** (2026-10-22, alone): the game freed the old Keyblade's files, which the copy's Keyblade still used. **Fixed (committed 1085b5b, with 3.3):** the copy has its own Keyblade files; when the game frees or moves them, its Keyblade is ended and built again. Bench `blade` PASS; your two-PC play 2026-10-25: Keyblade changes and Drives on both sides, no crash.
- [x] **1.111 P1. Data Final Xemnas's desperation move played in the friend's game only after it ended in the host's** (2026-10-22): the friend's game played the host's capture on the copy with that Xemnas as the captor, which held its own brain's move back. Done (commit fab6d1e, motion_mirror.cpp): a capture whose source runs its own brain for the shared move isn't played on the copy; the second run started the move together in both games.
- [~] **1.112 P1. The friend's hits can finish a boss the game keeps alive** (Data Final Xemnas killed before his desperation move, 2026-10-22): the game's 1 HP floor comes from a 'can't die' flag (+0xD10 bit 9) his script sets, and the friend's claims skipped that check. Built (commit 8e225f1, world_sync.cpp `ApplyClaims`): a claim that would pass the floor on a flagged enemy leaves min + 1. **Test:** two PCs (the friend finishing Data Final Xemnas) or a bench with claims on a flagged boss.
- [ ] **1.113 P0. The friend's game crashed in Data Final Xemnas** (2026-10-22 22:00), 1.3 s after the 1.99 guard ended a Xemnas thread on the host's copy for the first time: an access violation inside a script call reached through our pass-through; the call wasn't logged. Your choice: keep the guard and catch the call. Built (commit 4d84e40): a crash during a script call logs the call, its arguments and whose script ran it. **Next:** fight him again; if the friend crashes, read the 'in script call' line.
- [~] **1.114 P1. After the shared desperation move the friend's Sora stayed stuck and their Riku sat on the host's copy** (2026-10-22): in the friend's game Xemnas's HP was frozen (its hits were blocked and the host's HP not applied), so the move's last hit never landed and his script never let go. Built (commit 8129e03; your choice: their hits count): such an enemy takes the friend's hits there, no claim, and a killing hit leaves 1 HP. **Test:** the move to its end on two PCs.
- [~] **1.115 P0. Shan-Yu's clash on the friend never offers the push** (two PCs 2026-10-24, F320): the friend's own Shan-Yu started the clash on the friend's own Sora first, which broke the host's (no 270, let go after 30 s). Built (committed 319a2f0; `KH2COOP_OWN_HOOK_GUARD=0` turns it off): the 1.99 guard also ends such a thread on the friend's own Sora. Your one-PC run 2026-10-25: 270 offered, the clash goes through (MODLOG 43). **Test:** two PCs; also Data Xaldin and Data Roxas.
- [ ] **1.116 P1. The friend's Mickey takes no damage** (two PCs 2026-10-24, F321): the host's enemies hit the host's stand-in Mickey and nothing is forwarded; the friend's game drops its own enemies' hits on its player. **Next:** forward hits on the stand-in Mickey to the friend, as for the copy. Parked by you: the gate gauge during the friend's Mickey (F323).
- [~] **1.117 P1. No friend hit counted on a 1-HP boss** (Shan-Yu, two PCs 2026-10-24, F322): at 1 HP only a finisher can kill (bit 10), and nothing the friend did reached the host. Cause not proven. Built (committed 319a2f0): a 'finish check' log line in the friend's game for each of its lethal hits on a host-run enemy. **Test:** two PCs, the friend lands a combo finisher on a boss at 1 HP; send the friend's log.
- [~] **1.118 P0. The friend's game crashed during the host's Limit Form Limits** (Data Roxas, two PCs 2026-10-24, F156): the copy plays the host's Limit Form motions from its own bank; their attacks have no effects in a game with no Limit running and crash `exe+0x3D1C99` when they land. Built (319a2f0): such an attack hits nothing (HookCanHit). Two PCs 2026-10-27: the host's ones in the friend's game 3 times, no crash (MODLOG 46). **Test:** the friend's Limit Form Limits in the host's game.
- [ ] **1.119 P0. The host's game quit through abort() in Final Xemnas** (two PCs 2026-10-24 01:12:58, L128): no crash line (abort skips our logger); 1 s after the host's Limit Form ended, during the friend's Riku Limit. Cause not known: a failed Direct3D call, a pure virtual call or an uncaught C++ exception. Diagnostic in (commit df5a34c, on in both games 2026-10-24): 'ABORT:' lines with the call chain. **Next:** read them after the next such quit.
- [ ] **1.120 P1. The friend's camera stays on a boss's scene after it ends** (Data Luxord's minigame, two PCs 2026-10-24, F328): the boss ends it with trap_camera_cancel (1/223), which grab sync doesn't send. **Next:** proposed: send it like trap_camera_reset, and close any such camera at the let-go.
- [ ] **1.121 P1. Data Xigbar's bouncing shots stay visible and loud in the friend's game after the friend's command** (two PCs 2026-10-24, L130): its own boss never hears of the press, which goes to the host. **Next:** find what ends a shot after 492 (bench with the script-call log), then end our own matching attack when the press goes.
- [~] **1.122 P1. Both Soras could do Dark Thorn's Step Vault** (Beast's Castle, two PCs 2026-10-24, F330): command 148 is on each game's own per-enemy list; 'first press wins' dropped the other press for only 1 s, shorter than the chain. Built (installed, DLL `f516ce94c385`; committed 319a2f0): the use stays the first player's while a script holds that player, plus 1 s; Data Xaldin's Learn holds no one, so it is unchanged. **Test:** two PCs, both press Step Vault; also Data Xaldin's Learn.
- [~] **1.123 P0. Host crash when the friend's Valor ends** (one PC 2026-10-25, L137): the game's delete of an ended Keyblade (750 ms late that time) reads its files, let go after a fixed 500 ms. Built (form_look.cpp, installed, not committed): the files stay until the game's free `exe+0x3B4E80` ran on it; benches `companions` and `formlook` PASS (MODLOG 43). **Test:** two PCs, the friend's Valor runs out.
- [ ] **1.124 P2. A light piles up on Sora's right foot at each Valor finisher, gone at the Revert** (one PC 2026-10-25, the friend's own Sora in its own game, Fenrir and a short combo): not seen before because that finisher was rarely reached; our code doesn't rebuild a player's own Keyblade, so cause (game or ours) open. **Next:** with the general particles work: list that Sora's effects at a finisher.
- [ ] **1.125 P1. An enemy invisible in the friend's game, still hit and killed there** (Bolt Tower M_EX570, Land of Dragons 08/01, two PCs 2026-10-25): it's the host's serial 70, whose spawn first FAILED in the friend's game and came 5 s later at the host's current place; its own appearance ran, fade 1.00 in the log. Cause open. **Next:** log why a host spawn fails, and the late actor's draw state.
- [~] **1.126 P1. The friend can't steer a tornado it rides** (Land of Dragons 08/08, two PCs 2026-10-25, F337): the host's tornado took the copy and the host's stick steered the friend's Sora. Built (session 44, your pick: each game runs its own tornadoes): the friend's are no longer bound to the host's, and no tornado takes the copy (committed 6400b54; VERIFIED_OFFSETS L144). **Next:** a bench with the copy walking into a tornado, then your two-PC ride.
- [~] **1.127 P0. Mixed and shared Drive Forms crash the other game** (F342, F343 fixed; F356, F358 gone with B): built, not committed: option B (session 46): the copy's files in negative groups of ours, one look at a time, declined over the budget (F360, F361 fixed); benches `form_share`, `limit_gap` PASS 2026-10-28. **Next:** two PCs with Limits equipped: Drive, Revert, both in one form, room changes.
- [ ] **1.128 P1. Most of the other side's particles are missing in the friend's game** (your friend's report 2026-10-28: no hit sparks from the host's hits, Donald never casts, Goofy's shield stays in his hand). Plan: [docs/EFFECT_SYNC_PLAN.md](docs/EFFECT_SYNC_PLAN.md) (hit effects as events, the companions' casts replayed; overlaps 1.31, 1.60). **Next:** step 0, the bench diff of both games' effect logs.

---

## Track 2: dev and test suite (priority 2)
**Goal:** test any room, enemy, boss, or animation in seconds instead of playing all the way there.

**What exists:** the bench harness `tools/bench.py` with `test_census.py` (fight scenarios), `test_extra_copy.py`, `test_save_sweep.py` and `test_final_xemnas.py`; `debug_cmd.py` (warp, boss HP, god mode, teleport); `scene.py` (title, save slot, room); virtual pads; `parity_compare.py`, `pool_peaks.py`. Q4 decided: a PC control panel first.
- [ ] **2.1 P0. Command channel v2:** one "command + arguments" message with a reply, so tools can read things back. Dev builds and this PC only.
- [x] **2.2 P0. Warp** to any world, room, or door with chosen programs: built (session 28). **Next:** pick rooms by name (2.3).
- [ ] **2.3 P0. Catalogs** from this PC's own files: worlds, rooms and programs; objects; motions per model; item and ability IDs; mission files. JSON stays on this PC.
- [ ] **2.4 P0. Actor inspector:** every actor in the room, host and friend side by side, live.
- [x] **2.5 P1. Spawn an enemy** by object ID in front of the host's Sora: `debug_cmd.py <port> spawn <id> [count]` loads the files, then makes them; the friend's game loads them too (MakeUnmatched). Committed ffa200d (session 42; two-copy bench: 1 then 3 Dusks in the gym, both games, no crash). **Next:** use it for boss and enemy benches (2.13).
- [ ] **2.6 P1. Player test switches:** HP/MP/Drive/level, kill all, freeze enemy AI, no Game Over, force Mickey, endless items. God mode, boss HP and one-hit kills exist.
- [ ] **2.7 P1. Inventory tools:** give items, Keyblades, gear, abilities (running game only; the safeguard protects the real save).
- [ ] **2.8 P1. Story-flag tools:** show and set story flags (needed for 5.1).
- [ ] **2.9 P1. Animation lab:** play any motion on any actor, frame by frame.
- [ ] **2.10 P1. Memory tools:** read, write, watch, and search memory safely.
- [ ] **2.11 P1. The control panel (Q4)** for 2.2 to 2.10.
- [ ] **2.12 P1. Smoke test:** one command runs a fixed checklist on two copies and prints PASS/FAIL before every friend package. Most pieces exist in the bench scenarios.
- [ ] **2.13 P1. Boss gym:** one preset per boss of the route.
- [x] **2.14 P1. Test saves later in the story:** the bench uses a copy of your co-op saves (45 = before the final fight). Later bosses need saves near them.
- [ ] **2.15 P2. In-game menu** (maybe later); check whether the game's own debug menu still exists.
- [x] **2.16 P1. Battle without enemies** (benches only): `debug_cmd.py <port> battle on|off` holds the game's forced battle state, so Limits work in the gym; `test_extra_copy.py gym_limit` equips the save's Limit abilities in memory (save 59 has them off). Committed e04cc21 (session 42; bench PASS). **Next:** use it for Limit regression benches.

---

## Track 3: each Sora's own inventory and character (priority 3)
Each player's Sora lives in their own save, so items, gear, level, and munny are already separate. What's missing is how the *other* game shows you.
- [x] **3.1 P0. Q3 confirmed** (2026-10-07): own save only; your Keyblade, form, and costume on your copy; the partner's gear viewable read-only.
- [ ] **3.2 P0. What the copy uses today:** In each game, the copy uses the local player's own stats, gear, and Keyblade (bench, session 31). **Left:** confirm with two different Keyblades.
- [x] **3.3 P0. Your Keyblade on your copy.** Your picks 2026-10-25 (DECISIONS). **Done (committed 1085b5b, `copy_blade.cpp`):** the partner's own Keyblade, in this world's look, changed when their menu closes; the partner's form Keyblades. Bench `blade` PASS. Crashes friend 21:59, host 23:12: a weapon-slot read faulting after a build (F335, F336); fixed (checked reads). Your two-PC play 2026-10-25: no crash since. Stats next: 3.16.
- [ ] **3.4 P1. The copy's numbers come from its owner** (HP, MP, level) for everything the game checks.
- [ ] **3.16 P1. The partner's own stats on the copy** (agreed 2026-10-16). Step 1 (1.75-1.77) built. Your picks 2026-10-25: the whole Sora record on the copy's own sheet (research L121-L127), after the Keyblade (3.3). **Next:** build once 3.3 is committed.
- [ ] **3.5 P1. Menus (Q3):** nobody can equip the other's copy; the copy's card shows the partner's real gear, read-only.
- [x] **3.6 P1. Movement and combo abilities** (Glide, High Jump, Aerial Dodge...) shown right on the copy.
- [ ] **3.7 P1. Items on the partner:** add MP items and whole-party items; the sparkles show on the wrong Sora (cosmetic).
- [x] **3.8 P1. Shops, synthesis, and munny** stay local; nothing breaks while the partner is in a menu.
- [ ] **3.9 P1. Gear changed mid-session** shows on the other screen at once.
- [x] ~~**3.10 Parked.** Giving or trading items between players.~~
- [x] **3.11 P1. A party HUD gauge for the copy:** its face, the other player's HP and the KO look (two PCs, session 20).
- [ ] **3.12 P2. The copy keeps the original Sora look when a player uses a Sora model or texture mod.** Plan: give the copy its own object row built from the player's own game files.
- [x] **3.13 P2. The host's Sora wears the gold crown, the friend's none,** in the field and in cutscenes (session 26).
  - [ ] **3.13b P2.** Option: the friend's own crown shows again (a setting).
  - [ ] **3.13c P2.** The crown on the cutscene close-up Sora (needs a model edit).
  - [ ] **3.13d P2.** The crown on the world costume models (same model edit).
- [x] **3.14 P1. A third-party gauge,** so Donald, Goofy and the copy show at once (two PCs, session 33).
- [ ] **3.15 P2. The copy's gauge should take the top spot when it is the only one** (a TWTNW save with no party members).
- [ ] **3.17 P1. Extend the Command menu to allow the plus party member to show up**

---

## Track 4: drops and rewards (priority 4)
A host kill is mirrored as a real kill in the friend's game, so each game drops its own orbs, munny and items.
- [x] **4.1 Decide Q1.** Per player (2026-10-07).
- [x] **4.2 P0. Decided (Q14):** what the copy picks up goes to that game's own player.
- [x] **4.3 P0. Every drop type appears on the friend** from a mirrored kill; luck abilities apply to each player's own drops.
- [x] **4.4 P0. Kills by the friend's Limit, summon, or form count on the host.** They go through the friend's hits (bench), and a form kill gives both players the EXP (session 35). **Next:** see a Limit and a summon kill.
- [x] **4.5 P0. EXP from Drive Forms:** Valor EXP works for the friend (bench, session 35). **Left:** the other forms, the host in a form (its Valor level-up crashed with the copy in the room, 1.102, fix built).
- [x] **4.6 P1. Chests (Q2):** a chest opened by either player gives its item to both and is marked as opened in both saves; in a shared room it opens live in the other game, with the item window (DECISIONS, 2026-10-25). Status: built in `story_sync.cpp`, plus no second item from a chest already taken; bench_chest 2026-10-25 PASS (MODLOG session 43), committed 6d38d85. Next: your check of the look (real 'Open' presses, chest in view); the 'Friend Obtained' heading is 4.9.
- [x] **4.7 P1. Story rewards and boss bonuses** ("Obtained..." items, HP/MP bonuses, abilities) are given in the friend's game too. Works by default. To check if it breaks with sync changes.
- [x] **4.8 P2. Breakable objects and treasure blocks** drop per player, like enemies. Works by default. To check if it breaks with sync changes.
- [ ] **4.9 P2. 'FRIEND OBTAINED' on the other player's chest window** (your pick 2026-10-25, put off the same day). Status: the gold 'OBTAINED' heading is a picture in the field HUD atlas (VERIFIED_OFFSETS section 4), so editing it changes every box. Next: find which of its two copies the chest window draws, and whether a box can use another picture.

---

## Track 5: story, rooms, cutscenes, progression
- [x] **5.1 P0. Story progress sync (Q10):** the host's story is the truth; each player's own parts (drops, synthesis, shops) stay out; opened chests synced (Q2). Built (committed 6d38d85; `story_sync.cpp`, STORY_SYNC, session 42; DECISIONS rows 'Story sync'): the friend copies the host's story areas before each room load; a chest the host opens gives the friend its item. Bench 2026-10-25 PASS (host save 53, friend 52: equal after the load; chest 301 gave item 1, once). A friend's save with story the host's lacks gets the 'different save' screen (Continue Playing / Load) and the host an information-bar note: bench PASS 2026-10-25 (session 42). Mid-session it now waits 5 s (61d1e7c: it flashed at a boss's end). **Next:** two-PC check.
  **Next:** two-PC play check (chests: 4.6).
- [ ] **5.2 P0. World Map and Gummi Ship (Q9).** The host picks the world, and the friend follows, loads in step (two PCs). **Open:** the Gummi routes (together if feasible, else each flies solo).
- [x] **5.3 P0. Cutscenes:** play on both; only the host skips, the pause is shared (session 27).
  - [x] **5.3a P1. The shared cutscene pause reaches the other game 0.3 to 1 s late**, so two presses can cross. **Next:** measure where the time goes; send at the button press.
- [ ] **5.4 P0. Events started by talking or examining:** only the host starts them. Talk and Examine on the friend are harmless. **Open:** yes/no prompts that start a story event and move you (the train to Yen Sid's tower, before the final Xemnas fight). **Your preferred fix:** a "Waiting for host/friend" pop-up; both go only when both have accepted.
- [x] **5.5 P0. Forced party changes.** Fights where Sora is alone: both Soras fight together (session 27).
  - [ ] **5.5a P1. Enemies go idle after the Sora they were attacking falls instead of following their main goal** (Bailey), instead of going back to the gates. This must apply only to forced fights.
- [ ] **5.6 P0. World costumes:** the copy takes each world's own form, and the downed state works there (bench); Pride Lands' lion uses a fallback pose. **Left:** a two-PC check of each world.
- [x] **5.7 P1. Co-op saves (Q11):** the LOAD (CO-OP) row and the co-op save file, never the Steam save (two PCs, session 25).
- [ ] **5.8 P1. Starting a co-op run from zero (Q12):** research how save file creation works (with no co-op save file, the offline one is used), a way to reset co-op saves to the moment Sora becomes active, and New Game options: Skip Tutorial (Roxas) and Single Player or Co-op.
- [x] **5.9 P1. Joining late or rejoining after a crash:** the friend gets the story flags (5.1) and is pulled into the host's room.
- [x] **5.10 P1. Load barrier:** both games come out of the black screen together (two PCs, session 18).
- [x] **5.11 P2. The copy T-poses in cutscenes that show the party** (e.g. Cerberus's). **Simplest:** the copy follows the host's Sora there.
- [x] **5.12 P2. The other player's Sora in story cutscenes** (the cutscene twin, session 23).
- [x] **5.13 P0. Friend stuck on a black screen after a cutscene chain the host skipped:** fixed (session 24).
- [x] **5.14 P1. A story fight ending earlier in one game:** whichever game ends it first ends it in both (two PCs, session 24).
- [x] **5.15 P1. The two Soras appearing on one spot and sliding apart:** each steps aside after a load (session 23).
- [x] **5.16 P0. Walk-in story scenes:** either player can start them, both in step (session 24).
- [ ] **5.17 P1. World Map: only the host flies the ship.** The friend watches it move live. Not researched yet.
- [ ] **5.18 P2. A 4th chair in the Gummi Ship cockpit** with the other Sora sitting in it. Not researched yet.
- [x] **5.19 P0. A save made during co-op crashes the menu:** fixed; saves never carry the copy, and old ones are repaired (two PCs, session 25).
- [ ] **5.20 P2. The cutscene twin has no Keyblade in its hand** (it sits in the middle of the room, on the floor).
- [ ] **5.21 P2. The cutscene twin mirrored in the final cutscene after beating Xemnas,** not just copied to Sora's right.

---

## Track 6: network and stability
- [x] **6.1 P0. Disconnect and reconnect:** the 'Connection lost' screen with Continue Alone and Title Screen, and rejoining (two PCs, session 27).
  - [x] **6.1a P1. The friend pulled back into the last fight after the host left:** fixed (session 27).
  - [x] **6.1b P1. Disconnects during a boss fight:** decided: a mid-boss rejoin restarts the boss fight.
- [ ] **6.2 P1. On-screen message when the settings differ** (the link-drop part is done by 6.8).
- [ ] **6.3 P1. Lag tests on boss fights** with the network simulator. Late packets are now dropped (bench), but that has never triggered in a two-PC run. **Next:** other bosses under lag.
- [x] **6.4 P1. Engine memory pools:** the effect pool is now 4 MB and each room's peak is logged (`docs/EFFECT_POOL_PEAKS.md`). **Next:** find and watch the other pools (the actor budget is 1.96, the file heap 1.24).
- [ ] **6.5 P1. The friend's logs in one click:** a script that zips the newest logs to send over.
- [ ] **6.6 P2. A stable controller order** for local two-pad tests.
- [x] **6.7 P1. Connecting through Steam** (Join Game / Invite), with Radmin kept (two PCs, session 27).
- [x] **6.8 P2. The 'Connection lost. Reconnecting...' screen** (two PCs, session 27).
- [ ] **6.9 P2. Rejoin where you left off** not at the room's entrance. During boss fights, only the disconnected player has to rewatch the starting cutscene; joins mid-fight.
- [x] **6.10 P0. Friend crash with two room changes in one frame:** fixed (two PCs, session 28).
- [x] **6.11 P1. 'Title Screen' crashing during a room change:** fixed (two PCs, session 33).
- [x] **6.12 P1. Diagnostic panel** (your request): F3 shows or hides a see-through panel with the connection state, ping, jitter, loss, late packets, packet rate, the playback delay, both players' rooms, the enemy counts and FPS; `DIAG_HUD=1` shows it from the start. It's our own window over the game's, so not in exclusive fullscreen. Works on two PCs (2026-10-17, commit 535afa9).
- [~] **6.13 P0. Faults in our DLL end the game** through the .NET runtime's handler, even inside `__try` (F335); plan: `docs/CRASH_RISK_REVIEW.md` section 5.2. 5.2.1 built (session 45): measurements, the safe read, the Keyblade and form-look walks on it; benches `saferead`, `blade`, `formlook` PASS; your two-PC run 2026-10-27: no fault of ours in either game (MODLOG 46). 5.2.2 gone with option B (3caa746); 5.2.3, 5.2.4 built, the section 6 benches PASS (MODLOG 46). **Next:** two PCs.

---

## Track 7: setup, in-game messages, packaging
- [ ] **7.1 P1. In-game text:** "Waiting for the host", "Your friend joined/left", "The host is choosing...", "Settings differ". Needs research into the game's messages.
- [ ] **7.2 P1. Easier friend updates** in one step, and the mod version shown in game (builds that differ ignore each other).
- [ ] **7.3 P1. Separate play and dev settings** (extra logging off for play).
- [ ] **7.4 P2. A small launcher** (Host / Join buttons) instead of the .bat and ini.
- [ ] **7.5 P2. A short player README:** install, start, known limits.

---

## Track 8: housekeeping
- [ ] **8.1** `AGGRO_LOG=1` off in `start_host.bat` and the friend's ini for play sessions.
- [ ] **8.2** Trim the research logging.
- [ ] **8.3** The HP log line shows the boss in the "goofy" slot (cosmetic).
- [x] **8.4** Version control: on GitHub, public, GPL-3.0-or-later.
- [ ] **8.5** Cosmetic: Cure's effect drawn on the copy's Keyblade.
- [ ] **8.6** The link build ID doesn't change with every build, so two different DLLs can talk without the warning. **Fix:** an ID generated at every build.
- [x] **8.8** `PeerSetting()` warning logged twice: fixed.
- [x] **8.9** `package_friend.py` crash on a bad port number: fixed.
- [x] **8.10** The old modes removed (Donald dressed as the other player, the copy in Donald's slot, two brains), session 35.

---

## Track 9: co-op rules and options (Discord ideas, 2026-10-08)
Ideas from you and Aomi. Each gameplay rule gets a switch in an ini file.
- [ ] **9.1 Parked (decide later). HP orbs halved while a partner is connected:** half the orbs, or each orb heals half.
- [x] **9.2 P1. Downed for 30 s** (two PCs, session 18). Session 38 additions: the self get-up at 26% of max HP, just above the low-HP line so no beep after it (works on two PCs 2026-10-22, commit e315c8b); the KO face with the low-HP beep silent while down (`KH2COOP_DOWNED_FACE`, commit ca00fbe; works on two PCs 2026-10-24, your report).
- [ ] **9.3 P2. Opening the menu out of combat doesn't stop your game** (Q15). Heavy research, later.
- [ ] **9.4 P1. Save points refill the Drive gauge** (Q16, a switch, on).
- [x] **9.5 P1. Steam achievements blocked during co-op** (Q17; `ACHIEVEMENTS=1` in `kh2coop_player.ini` allows them).
- [ ] **9.6 P1. Revenge value with two players (Q18):** the friend's hits probably add no revenge, so bosses never break the friend's combos. **Next:** check on the bench.
- [x] **9.7 P1. Personal settings per player in `kh2coop_player.ini`:** built and benched (session 16), in use since.
- [ ] **9.8 Parked. The cutscene twin lags Sora by a small random delay,** so the two don't move in perfect sync.

---

## Parked: ignore these for now
- **8.7** Old sandbox folders kept when Windows reuses the process number: harmless, no fix.
- **Waiting to show up in play:** 1.16 battle barriers.
- **Optional content:** Atlantica, 100 Acre Wood minigames, Olympus tournaments, Lingering Will, Sephiroth, Mushroom XIII, Cavern of Remembrance, optional Gummi missions, journal completion. (The Data Organization fights are used on the bench.)
- **Roxas or other characters for player 2,** more than two players, the Roxas prologue in co-op. (The client as Roxas: research in `docs/Roxas_Model.md`, parked until after the first public release.)
- **Giving or trading items** between players (3.10).
- **Difficulty for two players** (an enemy HP or damage setting): only once the route is playable.
- **"Watch the host's screen"** during non-co-op parts: each player plays those solo (Q9).
- **The friend starting events or bosses:** only the host does (session 9). Exception: reaction commands (Q8, 1.62).
- **A public release** (Nexus etc.): needs the motion files built on each player's PC, a license check, an installer.

---

## Suggested order
**Session 40's two-PC test over Steam (2026-10-22) is done:** the ten checks passed (Drive Forms on the copy, the companions, Mickey, the 26% get-up; the camera guard never triggered, the combo stop didn't recur); the six bugs it found are 1.104-1.109, and the Data Final Xemnas night added 1.110-1.114. Everything from that list is committed.

**Next: two-PC test of the session 40 fixes** (all committed, none tried; install the same build on both PCs: builds that differ ignore each other). Each item you confirm gets its 'done'.
1. **Data Xaldin (1.107):** each player Learns and Jumps in turn; one presses Jump while the other is mid-Jump (should do nothing).
2. **The friend's Limit with a world ally (1.108, P0):** after it the host's companion is back and Valor isn't greyed.
3. **A room change in a form (1.106, 1.104):** the shine stays off the copy; the friend's own Wisdom Sora jumps after the room change without a T-pose (1.104 has no fix yet: say when it happens).
4. **Mickey played by the friend on a boss near death (1.109):** the boss stays at 1 HP until a real hit.
5. **Data Final Xemnas (1.112, 1.114, 1.113):** the friend finishing him doesn't skip his desperation move; after the move the friend's Sora is free and their Riku in place; if the friend's game crashes, send its log (the crash report names the script call).
6. **Down (9.2):** the KO face shows and the low-HP beep is silent while down.
7. **Still to confirm from earlier:** Data Xigbar's bouncing-shot command and third sniper command (1.83, 1.85); the Land of Dragons climb's swarm in the friend's game (1.96); Data Roxas's steal letting go (1.62); Once More at 1 HP (1.77).

Known, no need to report: changing Keyblades in the pause menu with the copy in the room crashes that game (1.110, P0, fix proposed, not built: pick the Keyblade before the session); Wisdom's attacks on the copy show melee effects and no shots (1.24); the flash cuts out; the friend may come back in a form after a Game Over Continue (1.105).

**Still pending from earlier plans, plus the open picks:**
1. Armored Xemnas II on two PCs: do the shield-break buildings show for the friend (1.49), and can the friend use them?
2. Data Roxas on two PCs: is the friend's Sora free after the steal (1.62)? Any Goofy Limit in that run gives the log 1.65 needs.
3. Your picks: the grab camera (1.62); Data Xigbar's scope camera (1.82, proposal in the session 39 MODLOG).
4. Then: save points and chests under one brain (1.55), Data Saïx's T-pose (1.63), the summon shown in the other game and party swaps (1.61, 1.51), Donald's and Goofy's Limits (1.60).
5. The friend's game short of actors in swarm fights (1.96, P0): find what clears bit 28 on the removed copies, then the climb on two PCs and a Hollow Bastion bench for the 1000 Heartless.
6. Fixes proposed, not built: the Keyblade swap with the copy in the room (1.110, P0); the Wisdom T-pose after a room change (1.104, bench `form_room` first); one friend's use per reaction-command offer (1.101); the friend's Sora following the held copy smoothly in Shan-Yu's clash (1.97).
7. After the form look: Wisdom's shots and the forms' own effects on the copy (1.24).
8. Bigger steps after that: story progress sync (5.1), the dev suite core (2.1 to 2.4), the rest of the route world by world, using the boss gym (2.13).

Earlier sessions' plans and every item's full history are in `docs/TODO_HISTORY.md`.
