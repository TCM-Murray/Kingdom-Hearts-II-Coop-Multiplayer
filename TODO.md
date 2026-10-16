# KH2 Co-op: master to-do list

Last updated: 2026-10-14 night (session 35: trimmed to short items; the full history of every item, word for word, is in [docs/TODO_HISTORY.md](docs/TODO_HISTORY.md)).

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
- Each item says the problem, where it stands and the next step. For how we got there, look the number up in `docs/TODO_HISTORY.md`.

---

## Where we are (session 35, 2026-10-14)
- **Works on two PCs:** connecting over Steam or Radmin; co-op saves kept apart from Steam saves; the copy beside Donald and Goofy, with its own party gauge; rooms, loads and cutscenes in step; shared enemies and hits (each hit counted once); downed state, shared Game Over and pause; the 'Connection lost' screen and rejoining; spells and Limit motions shown on the other Sora (Trinity too); world allies' Limits.
- **One brain** (1.55): the friend's enemies copy the host's instead of thinking for themselves. It is the only way since session 35, and it has played the whole final boss on two PCs with no crash. Armored Xemnas II's platform and push and Final Xemnas's desperation move are in step in both games.
- **Story played in co-op:** Twilight Town to Hollow Bastion's first visit, and boss fights in Olympus, the Land of Dragons, Beast's Castle, Agrabah and The World That Never Was (from co-op saves), to the credits.
- **Built, waiting for a test:** the host's field objects made for the friend (Armored Xemnas II's shield-break buildings, 1.49); the friend's Sora freed after Data Roxas's steal (1.62); a log to find why a Goofy Limit doesn't show (1.65).
- **Biggest gaps:** story progress sync between the two saves (5.1), the Gummi Ship (5.2), Drive Forms and summons shown in the other game (1.24, 1.61), missions and timers (1.22), the dev suite (Track 2).

---

## Decisions
Every rule decided for how co-op behaves, what the code does today and whether the two agree: **[docs/DECISIONS.md](docs/DECISIONS.md)**. Its 'Conflicts and open questions' list comes first.

---

## Track 1: combat and world sync (priority 1)

### Open bugs (from sessions 5 to 9)
- [ ] **1.1 P0. Hollow Bastion turrets (Q7).** The friend's turret hits no longer count twice. With one brain the friend's turret follows the host's (bench). **Next:** see on screen that the friend sees the host's turret fire, not its own.
- [ ] **1.2 P0. Shadows going underground on the friend's side, and enemies with similar states.** The flat look is fixed (height copied from the host); one brain fixed their timing on the bench and on two PCs (session 33). Neoshadows disappear and come back instead. **Next:** check Neoshadows and other enemies with special states (hidden, shielded, flying) with one brain on two PCs.
- [ ] **1.3 P0. Mickey rescue in co-op (Q6).** The crash when Mickey's rescue ends is fixed and the friend's Game Over choice is locked while the host decides (two PCs). **Next:** when the host's Mickey revives the host, close the friend's Game Over too and revive the friend; then the rules for the friend getting Mickey (one roll for both, the other player frozen meanwhile).
- [x] **1.4 P1. Enemies spawning and vanishing in a loop when the friend is far from the host.** Fixed: far enemies treat the copy as the player (two PCs, session 18).
- [x] **1.5 P1. Lock-on on enemies.** Works (session 11).
- [x] **1.6 P1. Hit reaction on the friend's screen.** The friend flinches and loses HP (session 11).
- [x] **1.7 P1. World allies' heals.** Auron heals the copies (session 11).
- [x] **1.8 P2. Limit gauge drains normally** (session 11).
- [ ] **1.9 P2. Cure reach for healing the partner:** 500 units is a guess; tune it by feel.
- [x] **1.10 P2. Continue after Game Over counts as a new room for world sync** (bench, session 28).

### Built but never tested in a real situation
- [x] **1.11** The host's Auron Limit seen from the friend: works (session 11). Real animations = 1.31.
- [x] **1.12** Both players entering a spawn area at once: enemies appear once (session 11).
- [ ] **1.13** Rooms with random enemy sets: the friend should get the host's pick.
- [ ] **1.14** Enemies that come in waves; groups started by missions or signals.
- [ ] **1.15** "Spawn near the player" enemies triggered by the friend.
- [ ] **1.16 P0. Battle barriers** (invisible walls until every enemy is dead) must drop on both screens together. Parked until we meet one in play.
- [x] **1.17 P0. EXP on the friend:** shared, both players get it at any distance (two PCs, session 18b). Form EXP is 4.5.
- [ ] **1.18 Pause edge cases.** A very fast pause-unpause-pause can still leave one game unpaused; your call: minor, leave it. Left to try: both pressing pause at once, the host's pause menu with enemies alive.
- [ ] **1.19** Enemies that exist on the host but never spawn on the friend (invisible attackers).

### New work needed for the whole route
- [ ] **1.20 P0. Boss support.** With one brain the friend's bosses follow the host's. Final Xemnas: his desperation move plays in both games (two PCs, session 35), the friend's hits can't kill a boss holding a player, and Xemnas always grabs the host's Sora for the Riku part (your option 3; options 1 and 2 kept for later). **Data Xemnas (2026-10-16 two-PC run):** the rule matched only the story model `B_EX170_LAST`, so Data Xemnas's second part (`B_EX170_LAST_LV99`) grabbed the friend's copy and the host crashed (`exe+0x1A8E67`, as on the session 35 bench). Fixed: the rule (and the shared desperation move) now match the Data version too; works on two PCs (2026-10-17, commit 54e2ab9). **Next:** walk the rest of the route boss by boss and log each one's problems here.
- [ ] **1.21 P0. Boss HP bars and boss name** on the friend's screen. The bar follows the host's hits, about 1 s behind (two PCs, session 33). **Open:** the boss name, the delay.
- [ ] **1.22 P0. Missions and special rules** owned by the host and shown on the friend: timers (Hades escape), gauges (Land of Dragons morale, protecting Minnie), counters (1000 Heartless), Luxord's time gauge. Cleared or failed in both games. **Built (session 36, committed 4f54fa6):** `mission_sync.cpp` sends the host's timer, counters and gauges; mission failed is the host's in both games; benched with Data Demyx and Luxord. Land of Dragons' first morale fight works on two PCs (2026-10-16, commit 4f54fa6). **Next:** the other story missions (Hades escape, Land of Dragons, 1000 Heartless) as the playthrough reaches them.
- [ ] **1.23 P0. Drive Forms and summons don't break co-op.** Drives, Valor and summons: no crash on two PCs (session 33); downed in a form ends the form first. **Open:** Anti Form on two PCs, Mickey in a form.
- [ ] **1.24 P1. Drive Forms shown on the other Sora (Q5).** The copy still shows a normal Sora playing the form's motions. **To do:** the form's model, two Keyblades and motions on the copy; both players in a form at once; a form gauge per player.
- [ ] **1.25 P1. Limits with every partner.** Auron, Mulan, Aladdin and Trinity work. **Next:** check the others, Riku included (see 1.60).
- [ ] **1.26 P0/P1. Reaction commands (Q8).** Works on two PCs: the TWTNW cylinder jumps and the shield break (session 35), Cerberus's (session 18b). A boss's command for the friend goes to the friend's menu (1.62). The friend's presses of commands from the game's own per-enemy list now run on the host, first press wins (session 36; the Twin Lords' 505 / 506 on the two-copy bench, you saw it; Data Xaldin's and Data Luxord's not retested). **Open:** Shan-Yu's sword clash shows in the wrong place on the other screen; the "watch the other player's command" option.
- [ ] **1.27 P1. Grabs, stun, freeze, knockback, guard and reflect** on the copy and on mirrored enemies. Enemy grabs of the copy already run on the friend's own Sora (1.62). **Next:** check the rest.
- [ ] **1.28 P1. Projectiles and area attacks:** who gets hit where (the copy is a little behind the real player). Measure first.
- [ ] **1.29 P1. Fights with a required ally** (Beast, Simba, Mulan, Riku, Mickey): the ally behaves the same on both screens.
- [x] **1.30 P0. The World That Never Was party:** Riku and the copy are both there; the final fights reach the credits on two PCs with no crash (session 33).
- [ ] **1.31 P1. The other player's Limits and reaction commands on the copy.** Spells and Limit and reaction-command motions show on the other screen. **Open:** the Limit and reaction-command particles (the copy gets an empty effect).
- [ ] **1.32 P1. Summons fully co-op (Q13):** the copy stays and the summon joins; one summon at a time across both players; the summon shows on the other screen. See 1.61.
- [x] **1.33 P0. Crash on a spell cast by the copy:** fixed (two PCs, session 13).
- [x] **1.34 P0. Lone Continue after Game Over loops on a white screen:** fixed, the friend's Game Over is locked while the host is connected (session 13).
- [x] **1.35 P0. The copy jumping to a leftover Drive Form body:** fixed (two PCs, session 18).
- [x] **1.36 P1. Drop bonuses counted twice:** fixed, normal drop rates (two PCs, session 18).
- [x] **1.37 P0. Enemies that vanish on the host giving the friend EXP and drops:** fixed (two PCs, session 18b).
- [ ] **1.38 P2. EXP mismatch with EXP Boost** between the two games (bench, session 18): two readings don't fit the boost. **Next:** find which HP the boost reads.
- [x] **1.39 P1. Enemies stay put when a player goes down alone:** they wait near the downed player (two PCs, session 18b).
- [x] **1.40 P1. Hits counted twice:** the friend's damage counts only through its own hits (two PCs, session 33).
- [x] **1.41 P0. Friend crash with a Shadow in a T-pose:** fixed (story run, session 20).
- [x] **1.42 P1. Waiting enemies ignore companions:** a companion who is up wakes them (two PCs, session 20).
- [ ] **1.43 P1. A companion knocked out on the host isn't knocked out on the friend's screen.** **Your call:** (a) the friend's HUD only shows the KO, or (b) the full KO state in the friend's game (riskier).
- [ ] **1.44 P1. A group the friend triggers comes out one enemy at a time, far apart** (Hollow Bastion, session 20). The friend's position does trigger the group; the cause isn't found yet. **Suspect:** the host's game removing and remaking enemies near the friend (1.45).
- [ ] **1.45 P1. An immortal enemy on the friend's screen** (session 20): an enemy removed on the friend's side kept running. **Your plan:** (a) the friend's side hides and removes such an enemy again; (b) find why the host's game removes and remakes live enemies near the friend.
- [x] **1.46 P0. Friend crash when a companion uses an item on a low Sora:** guard added, no crash with Potions on two PCs (session 33).
- [ ] **1.47 P1. Samurai duel reaction command:** after one player uses it, the other can still use it and then freezes until the fight ends. **Next:** retest with one brain and 1.62.
- [x] **1.48 P1. Ship fight: the friend's screen ignored positions past 100,000:** fixed (two PCs, session 28).
- [ ] **1.49 P1. Armored Xemnas II.** On two PCs (session 35): the platform on the dragon's head and the push are in step; the friend never had the shield-break buildings, so only the host could break the shield. **Built, not tried:** the host's field objects are made for the friend too. **Next:** a two-PC run; then the friend using the shield command (646) on its own building.
- [ ] **1.50 P1. Ship fight: the friend's game stays in its own part of the fight.** Each game runs its own copy of the fight; the friend's stays at the tail while the host moves on, and the ship's HP bar isn't in step. **Next:** research a fight-part sync.
- [ ] **1.51 P1. A companion swapped with the Party command isn't swapped in the friend's game.** Both ways of swapping (command and menu) need catching. Goes with 1.52.
- [ ] **1.52 P1. The friend's companions follow only the host's** (your idea): same members, the host's moves and voices, no decisions of their own. Today their brain still picks heals that never land (why the friend's Donald keeps calling "Sora!").
- [ ] **1.53 P1. Objects and effects only the host's game shows.** With one brain, objects the host's scripts make (Luxord's cards) now appear for the friend, and the friend saw the buildings that hurt on contact (session 35). **Open:** the Land of Dragons wind gusts, Larxene's clones, the Twin Lords' minions. Effects that move now follow the host's (built, session 35; see 1.55).
- [ ] **1.54 P1. Beast's Castle out of step on the friend's screen:** an enemy failing to spawn on the friend, the boss going invisible. **Next:** retest with one brain.
- [ ] **1.55 P1. One brain, the host's (the general world mirror).** The only way since session 35; played on two PCs through the final fights. Each fix so far copied one more kind of host script call to the friend (fades, effects, motion speed and timing, strikes and pulls, grabs, reaction commands). Built and benched, code not committed yet, not yet seen on two PCs (session 35): effects that move, turn, scale and change colour; enemy tilt; texture animations, shadows, room parts and lights. **Open:** save points, chests and other field objects under one brain; the gaps listed in 1.53 and 1.63.
- [ ] **1.56 P2. The copy's motion jumps at the end of a spell** (a hard cut, no blending). **Idea:** a short blend for the copy's motion changes.
- [x] **1.57 P1. The friend's Limit partner stays with the host in the host's game:** fixed, Mulan and Aladdin go to whoever uses the Limit (two PCs, session 33).
- [ ] **1.58 P1. Companions heal the other player's copy:** built and benched (session 34). Two-PC run 2026-10-15: the host's companions' items and Cures reach the friend (12 times needed, 3 Cures and 1 item picked, 5 heals sent). **Open (your friend's report):** in the friend's game Donald keeps saying 'Sora!' as if trying to Cure him; items are fine. Nothing about it in the logs. Likely (from the code, not verified): the friend's mirrored companions get only the host's pose each frame, so their own AI still runs; at a quarter HP the friend's Donald picks Cure (the voice line), the host's pose overwrites the cast, and he picks it again. **Next:** check that live, then keep the friend's mirrored companions from picking heals (the host's companions heal the friend through 1.58).
- [x] **1.59 P0. Friend crash in Data Roxas (effect pool full):** the game gets a 4 MB pool; no crash on the bench, and none in Data Roxas on two PCs since (session 35). Done (your OK, session 36; the 2026-10-15 run peaked at 165 KB).
- [ ] **1.60 P1. Donald's and Goofy's Limits on two PCs.** Your rule: one Limit at a time, and every ally but the partner and the copy leaves; built, and Limits looked as expected on two PCs (session 35). **Open:** Donald's Comet and Goofy's Knocksmash particles on the host's screen; the friend's Limit with Riku on the host's screen.
- [ ] **1.61 P1. Summons in the other game:** the companions don't leave in the other game, and the other player doesn't see the summon.
- [ ] **1.62 P1. Reaction commands and grabs for one player at a time.** Data Roxas's steal reaches the friend on two PCs, and the Keyblades stay with the player he stole from. The friend's Sora then stayed frozen in the steal: **fix built, not tried.** A grabber's own scene camera and its reset also play in the friend's game now (the Twin Lords' reaction commands, session 36, you saw it). **Open:** your call on the grab camera (both games for every boss / an exception for Final Xemnas / research first); prompts a boss turns on for itself (e.g. Reflect against Final Xemnas's lasers) may not reach the friend. **2026-10-16 (two-PC):** the Blizzard Lord's 506 / 662 on the friend run with its camera, but the friend's camera is 'almost the same as the host's, still needs calibrating' (details asked). **Steal delay (two-PC 2026-10-16):** the friend's Sora stayed ~1 s in the steal after the host's let go: 0.5 s host quiet wait (`kQuietFrames` 30) + ~0.1 s network + 0.5 s friend wait before `EndStuckGrab`. **Planned fix (after your tests):** free the friend's Sora at the let go itself, and a shorter host wait (checked against the Twin Lords' grab-to-grab chains). **Camera (later the same run):** still 'feels off in some' reaction commands (Data Xemnas phase 1 among them); which ones and how asked. **Shorter wait built and benched 2026-10-16 (DLL 5fc414f99b62):** `kQuietFrames` 10, and the friend checks for a stuck grab motion one frame after the let go; Data Roxas bench: let go sent 0.18 s after the script freed the copy. Twin Lords' grab-to-grab chains not seen on the bench (no grab that run): watch them in your test.
- [ ] **1.63 P1. Data Saïx's Berserk shows a T-pose on the friend's screen** (bench). **Lead:** his script loads a second motion file that the friend's game never loads. **Next:** compare both games at Berserk.
- [ ] **1.64 P1. Held and attached things drawn in the wrong place on the friend's screen** (Roxas slightly rotated in the clash). Built and benched: the sender sends where the actor is drawn. **Next:** your check in game; the bone's tilt is not sent yet.
- [ ] **1.65 P1. Limit motions that show idle on the other player's copy.** Trinity works (its file is found by its contents). **Open:** a Goofy Limit's file wasn't found in the host's game; logging added to find out why.
- [ ] **1.66 P1. Some of Larxene's attacks don't hurt the friend's Sora** (your report, 2026-10-15; the fight inside the portal, her Absent Silhouette, not Data Larxene): the ones where she throws 3-4 thunderbolts at a chosen target. **Next:** bench Larxene and check how those bolts hit (script strike, separate object or effect).
- [x] **1.67 P1. Status effects on allies (frozen, burning) don't show in the friend's game** (your report and screenshot, 2026-10-15, Twin Lords bench). It's a capture (script call 1/29), the call bosses use to grab Sora; motion_mirror.cpp found the grabber by its memory address, which fails for objects made during the fight. **Fixed (session 36):** the grabber is found through world sync's host-to-copy pairing; two-copy bench, you saw Aladdin and Donald frozen in the friend's game. Not yet tried on two PCs.
- [ ] **1.68 P0. The friend's game crashed entering Imperial Square (Land of Dragons)** (two-PC run 2026-10-15 23:31, 'Connection lost' got you both back). The host broke a field object (`F_MU060`) 3 s after the friend entered that room; the friend's game ran the host's effect call for it while its effects were still in another mode, the call failed half way, the object could no longer be removed, and leaving the room crashed on it. **Fix built (session 36):** replayed effect starts wait (up to 1 s) for the field's mode and are dropped otherwise. Bench: no effect start held in a normal fight. Not yet tried on two PCs (the crash needs the same timing).
- [ ] **1.69 P0. Missions not shared between an English and a Spanish game** (your report: missions not entirely synchronized). Mission files sit in a language folder ('msn/us/...', 'msn/sp/...') and mission sync compared the whole name, so it never took over: each game ran its missions on its own the whole run. **Fix built (session 36):** only the file name counts. Same-language bench still works; needs a two-PC run.
- [ ] **1.70 P2. Smaller things from the two-PC run of 2026-10-15** (Twilight Town, Hollow Bastion, Land of Dragons): (1) the friend's reaction commands on regular enemies (`M_EX990` 69, `M_EX010` 34, `M_EX910` 301, `M_EX660_RAW` 269 / 285) were refused 76 times ('not offered to the friend lately'): that build didn't have the own-list fix yet (6ce21bc), retest; (2) Rapid Thrusters (`M_EX660_RAW`) stay in the friend's game after the host's are gone (up to 11 at once, VERIFIED_OFFSETS row); (3) the host's object 0x558 (no spawn record) could not be made in the friend's game 54 times (Land of Dragons); (4) many Land of Dragons field objects of the friend's game have no host match and are parked (`F_MU080` 21, `M_EX660_RAW` 18, `F_MU090` 12, `F_EX000` 12, ...); (5) an exception in the friend's mission task at a Game Over (caught, VERIFIED_OFFSETS lead).
- [ ] **1.71 P1. Shan-Yu's hawk lifts the friend's Sora but doesn't carry it** (your report, two-PC run 2026-10-15: the friend's Sora played the grab but stood still, never flung). The hawk attaches the grabbed Sora to itself; grab sync only sends attaches the other way round (the grabber on Sora, Data Roxas). Same kind of attach in the logs: Dark Thorn, the Beast's Castle object `F_BB080`, Final Xemnas (Sora onto Riku); the Dancer not logged yet. **Next:** send an attach of the copy to an enemy too, run on our Sora with our copy of the enemy; the let go detaches it.
- [x] **1.72 P0. The friend's game stays on a black screen when it loads a save after a Game Over** (your report, two-PC run 2026-10-16: the host picks Load first, then the friend). The friend's save pick asks for the save's room (door 0x63); the door lock refused it because the host was already in another room (friend log 19:58:30, 'refused our own move to world 0x05 room 0x01 door 0x63'). **Fix built (session 37, not tried):** a room change asked for while the friend's Game Over load screen is open (or was, within 1 s) passes the door lock; follow then takes the friend to the host's room, as after LOAD (CO-OP) on the title. **Works on two PCs (your test 2026-10-17, commit 7c1de6c).**
- [x] **1.73 P1. The friend's yellow target (auto target, before R1) may pick the enemy near the host's Sora** (your question, 2026-10-16). Ghidra: the yellow target measures from the lock-on's player (command menu +0xC20), which every newly built Sora sets to itself; the copy is built before our Sora at room loads, so it should be ours (VERIFIED_OFFSETS lead 'Who the yellow target measures from'). Not reproduced. **Next:** your description of what you saw, then a bench that reads +0xC20 against our Sora in both games. **Closed 2026-10-16 (your call):** most likely the missing hit feedback (1.79); the game's auto target stays as it is.
- [x] **1.74 P1. Land of Dragons, first morale-gauge fight: no enemies in the friend's game** (your report, 2026-10-16). The friend's log of that run shows the host's enemies made and bound in the friend's game in both morale fights (`MU02_MS103A`, `MU02_MS103B`, room 0x08/0x02: 6-10 enemies matched each 10 s; some made up to 5 s late after 'FAILED to spawn here'). **Next:** your details (which fight, none at all or late/few), then compare both logs at that moment. **Found (2026-10-16, your hint 'the first morale fight, before the Captain's three missions'):** that is `MU01_MS102` in the Bamboo Grove (0x08/0x01, 19:51:17-19:51:55). The host's game had 35 enemies and field objects there; the world packet for that (~1,900 bytes) is bigger than the friend's 1,500-byte receive buffer, so the friend's game got none of them and kept its own 35 parked ('enemies host 0, ours 35'). Hits every room with ~28+ enemies and objects (VERIFIED_OFFSETS row). Fix: your pick (bigger buffer, or the packet split in parts). **Built (your pick 'split in parts'):** world_sync.cpp sends each frame in parts of at most 1,200 bytes (packet v6, `kMaxPartBytes`), up to 64 enemies instead of 40; the friend drops an entity only when it was missing from 4 of the host's frames (a lost part only delays). Builds clean; needs a bench in a big room, then your two-PC test (both PCs need the new build). **Works on two PCs (your run 2026-10-16 23:48, package 14bfd290270e):** in `MU01_MS102` the friend's game had 'enemies host 35, ours 35, matched 35' (0 before).
- [~] **1.75 P0. The friend takes damage (and can go down) during its own Limit** (your friend's report, two-PC run 2026-10-16). Host log: 20 hits on the friend's copy forwarded during the friend's Limit (18 of them in the finishing move). With one brain the host's game decides the hits, and the copy in the host's game has no Limit protection; the friend's game applies the forwarded hit straight to HP. **Options:** (a) quick: the host drops hits on the copy while the friend's Limit runs (limit_sync already knows); (b) general: the friend's game drops a forwarded hit whenever its own Sora can't be hit in its own game (Limits, reaction commands, other protected moments), needs the game's check (VERIFIED_OFFSETS lead 'How the game decides a hit lands') and a bench. **Built and benched 2026-10-16 (session 37, DLL 5fc414f99b62), option (b):** the friend's game asks the game's own protection check (`exe+0x3A7360`, +0x9B8 bit 2) on its own Sora before a forwarded hit; during its Limit +0x126 bit 0 is set and all 7 hits in it were dropped (VERIFIED_OFFSETS row). Needs your two-PC test.
- [ ] **1.76 P1. The friend's Reflect doesn't block enemy hits** (your friend's report, 2026-10-16). Hits on the copy in the host's game right after the friend's Reflect casts still reach the friend (VERIFIED_OFFSETS row). Same root as 1.75: the host's game decides hits on the copy, which doesn't have the friend's protection; the general fix (1.75 b) would cover both. **Next:** after your tests, bench what Reflect, a Limit and Guard set on Sora. **Bench 2026-10-16:** Reflect sets none of the protection flags; its barrier is an attack object, made in both games (the host's copy casts it ~0.1 s after the friend), so the 1.75 fix doesn't cover it. No forwarded hit landed inside a Reflect window on the bench. A guess, not checked: the friend's reports were hits the host's copy took before its own barrier was up (the cast reaches it ~0.1 s plus network later). **Next:** watch your next run (the friend's log has every forwarded hit); if hits still get through, the friend's game drops a forwarded hit while its own barrier is up (needs a way to see the barrier). Guard not tried yet.
- [~] **1.77 P0. Second Chance and Once More don't work for the friend** (your report, Data Xemnas, 2026-10-16: the friend combo'd to death with both equipped; the host is fine). The forwarded hit reaches the downed rule as a lethal hit and the friend goes down; the abilities act on the damage in the host's game, on the copy, with the host's HP (VERIFIED_OFFSETS row). **Fix (after your tests):** the friend's game applies its own Sora's damage rules (these abilities, and with 1.75 b its protection) to a forwarded hit before the downed rule; first find where the game applies them (Ghidra + bench). **Built and benched 2026-10-16 (session 37, DLL 5fc414f99b62):** the host hooks the game's hit apply (`exe+0x3D3BA0`) on the copy and sends the raw damage and the hit's details; the friend's game runs the player's survive rule on its own Sora before the downed rule: 'Second Chance: lethal 16 trimmed to 11 (HP 12)', then 'Once More ... trimmed to 0 (HP 1)' 7 times in one combo (VERIFIED_OFFSETS row). Needs your two-PC test.
- [x] **1.78 P2. Your call: the friend's Limit works in rooms without companions** (your observation on the bench, 2026-10-16: Trinity in Data Xemnas, where Sora is alone). Likely because the Sora copy fills the empty party slot (puppet.cpp GetFriend), so the menu sees a partner (VERIFIED_OFFSETS row). Keep it (the other player counts as the partner) or grey the Limit out like the game does when Sora is alone? **Your call (2026-10-16): like the game, no Trinity without Donald and Goofy.** Ghidra: the Limit list `exe+0x3E7B30` / `exe+0x3E81A0` and the Limit start's check `exe+0x3E7800` find each needed character through the party slots, where our GetFriend(1) hook hands out the Sora copy (VERIFIED_OFFSETS lead 'Which Limits the command menu lists'). Probe 'limit gate:' built (puppet.cpp, temporary), waiting for a bench; then the copy is kept out of those checks. **Built and benched 2026-10-16 23:34:** the copy didn't count as Donald or Goofy in the Limit check; it is what puts a Limit in the menu when Sora is alone (VERIFIED_OFFSETS row). Now a Limit (command type 0x51) is greyed (`exe+0x3D88E0` hook) and refused while neither party slot holds a real companion. Benches: Data Xemnas greyed, no Limit; Agrabah (Aladdin + Donald) the Limit runs. **Works on two PCs (your run 2026-10-16):** no Limit in Data Xemnas's first part, Riku's Limit in the second.
- [ ] **1.81 P0. Data Xigbar: the host fell out of the arena after an arena change** (your two-PC run 2026-10-16 23:11). Xigbar's arena change places "the player" in the new arena (VERIFIED_OFFSETS row); it was the friend's copy, so the host's Sora stayed over a part that had just gone. **Built, not benched (you test it):** grab_sync.cpp: a script that swaps the room's parts and places one Sora in the same frame now places the other Sora there too (the host's own one, or the friend's through a new 'arena place' event, grab packet v3) and sets it idle. Logs: 'changed the arena and placed ...'. **Your two-PC run 2026-10-17 (00:16-00:18):** the host's side works (3 arena changes, the host's Sora on the ground after each), but **the friend's Sora fell for the rest of the fight after the third** (Xigbar placed the copy): ArenaCall sends 'arena place' to the friend only when the host's Sora was placed. **Next:** send it in both cases (small fix, not made yet), then your two-PC test.
- [ ] **1.83 P1. The friend's reaction command against Data Xigbar's projectile does nothing** (your two-PC run 2026-10-17 00:16:55; your friend: a command against one of his moving shots in the normal fight, not the sniper one; the friend was in Final Form). The friend's game offered command 492 from its own list; the host's game faulted running the press on Xigbar (`exe+0x432800`: the handle his script passed names no object there; VERIFIED_OFFSETS lead). Likely the projectile the friend's game saw is missing from the host's run of the press. **Next:** research (Ghidra + Xigbar's script: what the handle is), then a fix; a test needs someone to get the command offered (manual).
- [ ] **1.82 P1. Data Xigbar's sniper view shows only in the host's game** (your two-PC run 2026-10-16). Its scope camera calls (VERIFIED_OFFSETS row) run only in the host's game, also when Xigbar aims at the friend. **Plan:** like grab sync's cameras: when his player is the friend's copy, the scope calls run in the friend's game (on its copy of Xigbar) and not in the host's. Not built.
- [ ] **1.79 P1. The friend gets no flinch or knockback from most hits; 'the friend's game decides the hits its Sora takes' (Dark Souls style)** (your idea and your friend's report, 2026-10-16). **Benches 2026-10-16 (VERIFIED_OFFSETS row 'Many host attacks...'):** a full switch would make the friend immune to projectiles and spawned attacks, which never hit in its own game (Data Xigbar 0 of 97, Luxord's cards 0 of 14, the Twin Lords' B_AL110 0 of 20); melee hits mostly do (Data Xemnas 34 of 55 matched). The missing feedback has the same cause: the friend's Sora flinches only on its own game's hits; a forwarded hit changes HP only. **Proposed:** (1) a forwarded hit plays the game's own reaction (`exe+0x3D2FE0`) on the friend's Sora, with the hit's direction, kind and reaction from the host, skipped when its own game just played one; (2) later, maybe, melee hits the friend's game sees decided there. Tool: tools/hit_compare.py. **(1) built and benched 2026-10-16 (session 37):** the host sends the hit's direction and knockback values (HitShape); the friend's game plays the game's own reaction 250 ms after the hit arrives unless its own game hit its Sora within 300 ms of it (Xigbar: 46-51 replayed; Xemnas: 3 replayed, 54 left to its own game). **Changed after your '0.25 s too much?' (2026-10-16):** the hit's damage and reaction now come together after the playback delay (100 ms, the time our copy of the host's enemy needs to reach the hit on the friend's screen), and the friend's own game's reaction up to 300 ms after a replayed one is stopped (the same attack seen late). Benches: damage + reaction a median 105-116 ms after arrival; double flinches 0 (Xemnas: 11 replayed, 44 left to its own game, 4 late own reactions stopped; Xigbar: 23 replayed, 1 stopped). Needs your two-PC test.
- [x] **1.80 P1. In the friend's game the host's enemies' aimed attacks go for the wrong player** (your report on the Data Xigbar bench, 2026-10-16: his shots flew at the host's Sora). The shots copy the boss's target slot; in the friend's game COPY_AGGRO had put the host's copy there. **Built and benched (session 37):** the host sends whom each enemy goes for (its boss pick or target slot); the friend's game writes it into its copy's target slot every frame and uses it as that enemy's player in its scripts. Data Xigbar: 5 of 5 switches followed, 13 shot hits on the friend in its own game (0 before). Other bosses' aimed attacks (Luxord's cards, the Twin Lords) go through the same path but weren't looked at. Needs your two-PC test. **Works on two PCs (2026-10-17, commit 49b9ca1):** Xigbar's shots went for and hit the friend's Sora on both screens.

---

## Track 2: dev and test suite (priority 2)
**Goal:** test any room, enemy, boss or animation in seconds instead of playing all the way there.

**What exists:** the bench harness `tools/bench.py` with `test_census.py` (fight scenarios), `test_extra_copy.py`, `test_save_sweep.py` and `test_final_xemnas.py`; `debug_cmd.py` (warp, boss HP, god mode, teleport); `scene.py` (title, save slot, room); virtual pads; `parity_compare.py`, `pool_peaks.py`. Q4 decided: a PC control panel first.
- [ ] **2.1 P0. Command channel v2:** one "command + arguments" message with a reply, so tools can read things back. Dev builds and this PC only.
- [x] **2.2 P0. Warp** to any world, room or door with chosen programs: built (session 28). **Next:** pick rooms by name (2.3).
- [ ] **2.3 P0. Catalogs** from this PC's own files: worlds, rooms and programs; objects; motions per model; item and ability ids; mission files. JSON, stays on this PC.
- [ ] **2.4 P0. Actor inspector:** every actor in the room, host and friend side by side, live.
- [ ] **2.5 P1. Spawn an enemy** by object id at Sora's position (needs one more game function).
- [ ] **2.6 P1. Player test switches:** HP/MP/Drive/level, kill all, freeze enemy AI, no Game Over, force Mickey, endless items. God mode and boss HP exist.
- [ ] **2.7 P1. Inventory tools:** give items, Keyblades, gear, abilities (running game only; the save guard protects the real save).
- [ ] **2.8 P1. Story-flag tools:** show and set story flags (needed for 5.1).
- [ ] **2.9 P1. Animation lab:** play any motion on any actor, frame by frame.
- [ ] **2.10 P1. Memory tools:** read, write, watch and search memory safely.
- [ ] **2.11 P1. The control panel (Q4)** for 2.2 to 2.10.
- [ ] **2.12 P1. Smoke test:** one command runs a fixed checklist on two copies and prints PASS/FAIL before every friend package. Most pieces exist in the bench scenarios.
- [ ] **2.13 P1. Boss gym:** one preset per boss of the route.
- [x] **2.14 P1. Test saves later in the story:** the bench uses a copy of your co-op saves (45 = before the final fight). Later bosses need saves near them.
- [ ] **2.15 P2. In-game menu** (maybe later); check whether the game's own debug menu still exists.

---

## Track 3: each Sora's own inventory and character (priority 3)
Each player's Sora lives in their own save, so items, gear, level and munny are already separate. What's missing is how the *other* game shows you.
- [x] **3.1 P0. Q3 confirmed** (2026-10-07): own save only; your Keyblade, form and costume on your copy; the partner's gear viewable read-only.
- [ ] **3.2 P0. What the copy uses today:** in each game the copy uses the local player's own stats, gear and Keyblade (bench, session 31). **Left:** confirm with two different Keyblades.
- [ ] **3.3 P0. Your Keyblade on your copy.** The game's Keyblade swap is found, but the copy shares our own Sora's weapon slot. **Next:** research a separate slot first.
- [ ] **3.4 P1. The copy's numbers come from its owner** (HP, MP, level) for everything the game checks.
- [ ] **3.16 P1. Plan: the friend's own stats in co-op, in three steps** (agreed 2026-10-16, session 37; the companions stay as they are). The friend's attacks already use its own stats (its game works out its hits, the host takes the claims, 1.40); only the hits it takes are worked out with the host's Sora's data. **Step 1 (medium, = the fix for 1.75, 1.76, 1.77):** the host sends what hit the copy (attack, attacker), and the friend's game runs the game's own damage rules on its own Sora (Defense, Second Chance, Once More, Reflect, Limit protection, Guard). **Step 2 (medium-large, = 3.4):** the copy gets its own HP/MP record with the friend's values in the host's game (enemy AI, heals, items, Drive see the real numbers; today only the HUD gauge shows them). **Step 1 partly built 2026-10-16 (1.75, 1.77):** Limit protection and Second Chance / Once More; not covered: Defense (the host still works out the damage with its own Sora's Defense), Reflect (1.76), Guard (not tried), the Drive gauge from damage taken (the game's `exe+0x3D5E50`, skipped by a forwarded hit). **Step 3 (large, research first; = 3.3, 3.5):** the friend's Keyblade, gear and abilities on the copy; the game looks them up by character (Sora) from the save, so this needs a separate weapon slot / character record.
- [ ] **3.5 P1. Menus (Q3):** nobody can equip the other's copy; the copy's card shows the partner's real gear, read-only.
- [ ] **3.6 P1. Movement and combo abilities** (Glide, High Jump, Aerial Dodge...) shown right on the copy.
- [ ] **3.7 P1. Items on the partner:** add MP items and whole-party items; the sparkles show on the wrong Sora (cosmetic).
- [ ] **3.8 P1. Shops, synthesis and munny** stay local; nothing breaks while the partner is in a menu.
- [ ] **3.9 P1. Gear changed mid-session** shows on the other screen at once.
- [ ] **3.10 Parked.** Giving or trading items between players.
- [x] **3.11 P1. A party HUD gauge for the copy:** its face, the other player's HP and the KO look (two PCs, session 20).
- [ ] **3.12 P2. The copy keeps the original Sora look when a player uses a Sora model or texture mod.** Plan: give the copy its own object row built from the player's own game files.
- [x] **3.13 P2. The host's Sora wears the gold crown, the friend's none,** in the field and in cutscenes (session 26).
  - [ ] **3.13b P2.** Option: the friend's own crown shows again (a setting).
  - [ ] **3.13c P2.** The crown on the cutscene close-up Sora (needs a model edit).
  - [ ] **3.13d P2.** The crown on the world costume models (same model edit).
- [x] **3.14 P1. A third party gauge,** so Donald, Goofy and the copy show at once (two PCs, session 33).
- [ ] **3.15 P2. The copy's gauge should take the top spot when it is the only one** (a TWTNW save with no party members).

---

## Track 4: drops and rewards (priority 4)
A host kill is mirrored as a real kill in the friend's game, so each game drops its own orbs, munny and items.
- [x] **4.1 Decide Q1.** Per player (2026-10-07).
- [x] **4.2 P0. Decided (Q14):** what the copy picks up goes to that game's own player.
- [x] **4.3 P0. Every drop type appears on the friend** from a mirrored kill; luck abilities apply to each player's own drops.
- [ ] **4.4 P0. Kills by the friend's Limit, summon or form count on the host.** They go through the friend's hits (bench), and a form kill gave both players the EXP (session 35). **Next:** see a Limit and a summon kill.
- [x] **4.5 P0. EXP from Drive Forms:** Valor EXP works for the friend (bench, session 35). **Left:** the other forms, the host in a form.
- [ ] **4.6 P1. Chests (Q2):** a chest opened by either player gives its item to both and is marked opened in both saves.
- [ ] **4.7 P1. Story rewards and boss bonuses** ("Obtained..." items, HP/MP bonuses, abilities) given in the friend's game too.
- [ ] **4.8 P2. Breakable objects and treasure blocks** drop per player, like enemies.

---

## Track 5: story, rooms, cutscenes, progression
- [ ] **5.1 P0. Story progress sync (Q10):** the host sends its story flags at join and on change, the friend writes them; each player's own parts (drops, synthesis, shops) stay out. Opened chests are synced (Q2). **First step:** find the story flags in the save.
- [ ] **5.2 P0. World Map and Gummi Ship (Q9).** The host picks the world and the friend follows, loads in step (two PCs). **Open:** the Gummi routes (together if feasible, else each flies solo).
- [x] **5.3 P0. Cutscenes:** play on both, only the host skips, the pause is shared (session 27). Not checked: pre-rendered movies.
  - [ ] **5.3a P1. The shared cutscene pause reaches the other game 0.3 to 1 s late**, so two presses can cross. **Next:** measure where the time goes; send at the button press.
- [ ] **5.4 P0. Events started by talking or examining:** only the host starts them. Talk and Examine on the friend are harmless. **Open:** yes/no prompts that start a story event and move you (the train to Yen Sid's tower, before the final Xemnas fight). **Your preferred fix:** a "Waiting for host / friend" pop-up; both go only when both have accepted.
- [x] **5.5 P0. Forced party changes.** Fights where Sora is alone: both Soras fight together (session 27). **Left:** fixed parties (Riku, Pride Lands) and event programs.
  - [ ] **5.5a P2. Enemies go idle after the Sora they were attacking falls** (Bailey), instead of going back to the gates.
- [ ] **5.6 P0. World costumes:** the copy takes each world's own form, and the downed state works there (bench); Pride Lands' lion uses a fallback pose. **Left:** a two-PC check of each world.
- [x] **5.7 P1. Co-op saves (Q11):** the LOAD (CO-OP) row and the co-op save file, never the Steam save (two PCs, session 25).
- [ ] **5.8 P1. Starting a co-op run (Q12):** written step-by-step instructions (the host hands its save over once).
- [ ] **5.9 P1. Joining late or rejoining after a crash:** the friend gets the story flags (5.1) and is pulled into the host's room.
- [x] **5.10 P1. Load barrier:** both games come out of the black screen together (two PCs, session 18).
- [ ] **5.11 P2. The copy T-poses in cutscenes that show the party** (e.g. Cerberus's). **Simplest:** the copy follows the host's Sora there.
- [x] **5.12 P2. The other player's Sora in story cutscenes** (the cutscene twin, session 23).
- [x] **5.13 P0. Friend stuck on a black screen after a cutscene chain the host skipped:** fixed (session 24).
- [x] **5.14 P1. A story fight ending earlier in one game:** whichever game ends it first ends it in both (two PCs, session 24).
- [x] **5.15 P1. The two Soras appearing on one spot and sliding apart:** each steps aside after a load (session 23).
- [x] **5.16 P0. Walk-in story scenes:** either player can start them, both in step (session 24).
- [ ] **5.17 P1. World Map: only the host flies the ship,** the friend watches it move live. Not researched yet.
- [ ] **5.18 P2. A 4th chair in the Gummi Ship cockpit** with the other Sora sitting in it. Not researched yet.
- [x] **5.19 P0. A save made during co-op crashing the menu:** fixed, saves never carry the copy and old ones are repaired (two PCs, session 25).
- [ ] **5.20 P2. The cutscene twin has no Keyblade in its hand** (it sits in the middle of the room, in the floor).
- [ ] **5.21 P2. The cutscene twin mirrored,** not just copied to Sora's right.

---

## Track 6: network and stability
- [x] **6.1 P0. Disconnect and reconnect:** the 'Connection lost' screen with Continue Alone and Title Screen, and rejoining (two PCs, session 27).
  - [x] **6.1a P1. The friend pulled back into the last fight after the host left:** fixed (session 27).
  - [ ] **6.1b P1. Disconnects during a boss fight:** to talk through before any code. Should a mid-boss rejoin restart the fight, wait for the next room, or put the player straight in (6.9)?
- [ ] **6.2 P1. On-screen message when the settings differ** (the link-drop part is done by 6.8).
- [ ] **6.3 P1. Lag tests on boss fights** with the network simulator. Late packets are now dropped (bench), but that has never triggered in a two-PC run. **Next:** other bosses under lag.
- [ ] **6.4 P1. Engine memory pools:** the effect pool is now 4 MB and each room's peak is logged (`docs/EFFECT_POOL_PEAKS.md`). **Next:** find and watch the other pools.
- [ ] **6.5 P1. The friend's logs in one click:** a script that zips the newest logs to send over.
- [ ] **6.6 P2. A stable controller order** for local two-pad tests.
- [x] **6.7 P1. Connecting through Steam** (Join Game / Invite), with Radmin kept (two PCs, session 27).
- [x] **6.8 P2. The 'Connection lost. Reconnecting...' screen** (two PCs, session 27).
- [ ] **6.9 P2. Rejoin where you left,** not at the room's entrance.
- [x] **6.10 P0. Friend crash with two room changes in one frame:** fixed (two PCs, session 28).
- [x] **6.11 P1. 'Title Screen' crashing during a room change:** fixed (two PCs, session 33).
- [x] **6.12 P1. Diagnostic panel** (your request 2026-10-16): F3 in game shows / hides a small see-through panel on the middle left of the game's window: connection GOOD / UNSTABLE / BAD / LOST, ping (a new 'KH2Q' ping twice a second), jitter, loss over the last 5 s, late packets, packets/s, the last packet's age, the playback delay, both players' rooms (yellow when they differ), the enemies (host: sent and parts a frame; friend: the host's, ours, how many copy one) and FPS. `DIAG_HUD=1` in kh2coop_player.ini shows it from the start. It's a window of ours over the game's, so it shows with the game windowed or borderless, not in exclusive fullscreen. Works on two PCs (2026-10-17, commit 535afa9).

---

## Track 7: setup, in-game messages, packaging
- [ ] **7.1 P1. In-game text:** "Waiting for the host", "Your friend joined/left", "The host is choosing...", "Settings differ". Needs research into the game's messages.
- [ ] **7.2 P1. Easier friend updates** in one step, and the mod version shown in game.
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
- [ ] **8.6** The link build ID doesn't change with every build, so two different DLLs can talk without the warning. **Fix:** an ID made at every build.
- [x] **8.8** `PeerSetting()` warning logged twice: fixed.
- [x] **8.9** `package_friend.py` crash on a bad port number: fixed.
- [x] **8.10** The old modes removed (Donald dressed as the other player, the copy in Donald's slot, two brains), session 35.

---

## Track 9: co-op rules and options (Discord ideas, 2026-10-08)
Ideas from you and Aomi. Each gameplay rule gets a switch in an ini file.
- [ ] **9.1 Parked (decide later). HP orbs halved while a partner is connected:** half the orbs, or each orb heals half.
- [x] **9.2 P1. Downed for 30 s** (two PCs, session 18).
- [ ] **9.3 P2. Opening the menu out of combat doesn't stop your game** (Q15). Heavy research, later.
- [ ] **9.4 P1. Save points refill the Drive gauge** (Q16, a switch, on).
- [x] **9.5 P1. Steam achievements blocked during co-op** (Q17; `ACHIEVEMENTS=1` in `kh2coop_player.ini` allows them).
- [ ] **9.6 P1. Revenge value with two players (Q18):** the friend's hits probably add no revenge, so bosses never break the friend's combos. **Next:** check on the bench.
- [x] **9.7 P1. Personal settings per player in `kh2coop_player.ini`:** built and benched (session 16). **Next:** your OK in game.
- [ ] **9.8 Parked. The cutscene twin lagging Sora by a small random delay,** so the two don't move in perfect sync.

---

## Parked: ignore these for now
- **8.7** Old sandbox folders kept when Windows reuses the process number: harmless, no fix.
- **Waiting to show up in play:** 1.16 battle barriers.
- **Optional content:** Atlantica, 100 Acre Wood minigames, Olympus tournaments, Lingering Will, Sephiroth, Mushroom XIII, Cavern of Remembrance, optional Gummi missions, journal completion. (The Data Organization fights are used on the bench.)
- **Roxas or other characters for player 2,** more than two players, the Roxas prologue in co-op.
- **Giving or trading items** between players (3.10).
- **Difficulty for two players** (an enemy HP or damage setting): only once the route is playable.
- **"Watch the host's screen"** during non-co-op parts: each player plays those solo (Q9).
- **The friend starting events or bosses:** only the host does (session 9). Exception: reaction commands (Q8, 1.62).
- **A public release** (Nexus etc.): needs the motion files built on each player's PC, a license check, an installer.

---

## Suggested order
**Next (after session 35):**
1. Rebuild the friend package and play a two-PC run of Armored Xemnas II: do the shield-break buildings show for the friend (1.49), and can the friend use them?
2. Data Roxas on two PCs: is the friend's Sora free after the steal (1.62)? Any Goofy Limit in that run gives the log 1.65 needs.
3. Your call on the grab camera (1.62).
4. Then: save points and chests under one brain (1.55), Data Saïx's T-pose (1.63), summons and Donald's and Goofy's Limits in the other game (1.61, 1.60).
5. Bigger steps after that: story progress sync (5.1), the dev suite core (2.1 to 2.4), the rest of the route world by world, using the boss gym (2.13).

Earlier sessions' plans are in `docs/TODO_HISTORY.md`.
