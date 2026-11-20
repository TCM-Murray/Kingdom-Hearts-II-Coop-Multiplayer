"""Automated checks of the extra copy (the Sora copy beside the party) on a bench pair, one scenario at a time.

Usage: py tools/test_extra_copy.py <scenario> [--slot N] [--env KEY=VALUE ...]

Scenarios:
  ship     save 45, warp into the ship fight (0x12/0x16 program 72), the host attacks for a while: no crash.
           No Sora copy is made there (the room spawns its own Sora, P_EX100_SIDECAR, not the PLAYER
           placeholder); both ways of adding one crashed (bench 2026-10-12 07:03 and 07:06, VERIFIED_OFFSETS).
  alone    the friend's game closes: the host sees the link lost, picks 'play on with Donald and Goofy'
           (debug link 3), reloads a room (no copy expected); the friend comes back and loads the
           same save: co-op again, the copy back in both games after the next room load.
  downed   god mode off. The friend's Sora takes a lethal hit (debug damage): down, not dead; then
           the host's too: both down = Game Over in both games.
  forms    the host goes into Valor Form (command menu: Drive, first form), then Reverts: the player
           switches bodies and back, the Sora copy stays in both games, no crash; then a room reload
           makes the copy again.
  limit    the host opens Limit (the command menu's second page) and uses the first Limit with the first partner;
           screenshot after each step; no crash, the copy still there after it.
  summon   the same with Summon (the first summon in the list).
  party    the command menu's Party entry: swap the first companion for the first one offered (a world ally,
           e.g. Auron in Olympus: --slot 14); then a room reload. No crash, the copy stays.
  companions  TODO 1.98 (companion_sync.cpp): --by A/B uses a Drive Form (--what form --pick N) or a summon
           (--what summon) in HB Borough (no enemies), then Reverts / dismisses: the same companions leave and come
           back in both games (sampled every 0.25 s).
  xemnas   save 45, warp into Final Xemnas (0x12/0x14 program 74; --ship: through the ship-fight room first, as
           in play), Xemnas's HP set to 200 in both games, the host hits him: both games reach his grab
           (scene re_b_ex170-50) and still run 20 s later (session 28 two-PC run: both crashed there at
           exe+0x3D76DF, old method). Riku and the Sora copy both expected in both games.
  xlimit   Final Xemnas as above, then the host opens Limit (second menu page) and uses the first one.
  menu     opens the command menu's Drive entry on the host and takes screenshots (to find the
           Drive Form / Summon entries for the next scenarios).
  hud      the third party gauge (party_hud.cpp): with Donald and Goofy both in the party the Sora copy's gauge
           shows above theirs (god mode off); Valor Form (Goofy leaves): it slides down a place; Revert: back up; then
           a 5 HP hit on each Sora: its gauge in the other game shakes. Stills hud_<copy>_<step>.png (*_crop.png
           enlarged) and frame strips hud_<copy>_<step>_strip.png of each move.
  hud2     the third gauge in use (god mode off): the host opens the pause menu (the gauge dims with the HUD), the
           host's Sora takes 30 damage (the friend's third gauge drains), then the friend's copy in the host's game
           (forwarded: the friend's Sora loses it, the host's third gauge drains), a room change (04/09, the copy
           comes back into it), the host's Sora takes a lethal hit (downed: the KO face on the friend's).
           Screenshots hud_<copy>_<step>.
  item     TODO 1.46 (god mode off): the host uses its first item on itself twice (Items, first item, first
           target) at 10 HP. With --env KH2COOP_TEST_BAD_ITEM=1 the first use gets an item with no table row and
           the item guard (puppet.cpp) skips it; the second heals. (--env KH2COOP_ITEM_GUARD=0 too: the crash the
           guard prevents, at exe+0x3F6C94.)
  costume  TODO 5.6 (god mode off), --slot 17/21/22/23/25 (a world with its own Sora): the host attacks (shots of the
           copy on the friend's screen), then the friend's Sora goes down and gets up (shots of both screens).
  mickey   TODO 1.3 (god mode off): Mickey forced in the host's game only (in memory: MickeyAllowed returns 1, the
           appearance count 0), slot 52 (no enemies). The friend down, then the host: Game Over; the host picks 'I
           won't give up', the bench presses triangle (Healing Light) until the host's Sora gets up, then walks and
           attacks. Each game's lines in mickey_<copy>.txt, screenshots mickey_<copy>_<step>.png.
  mickey_coop  TODO 1.3 / Q6 (mickey_sync.cpp): Mickey allowed in both games (memory), the roll's winner set (--winner
           A/B); both down; only the winner chooses; the loser comes back down; Healing Light gets both up. --trace.
           --end over / dies / nobody: 'It's all over', Mickey's death, no winner: both at Continue / Load, host Continues.
  mickey_hit  TODO 1.116: the Twin Lords' real hits on Mickey (--winner B: through the host's stand-in to the
           friend's Mickey; A: the host's own): his HP drops and he flinches.
  getup    god mode off, save 52: the friend down (KO face, no low-HP beep), up on its own with 26% of max HP.
  mickey_real  save 20, the Twin Lords fight: Mickey (the host's, both down) left at 2 HP, then the bosses' real hits;
           what the game does at his 0 HP is logged for 120 s (mreal_<copy>_<s>.png, mreal_<copy>.txt).
  heal     TODO 1.52 / 1.58 (save 20, the Twin Lords fight, god mode for the party and the enemies, the companions'
           heal choice logged with KH2COOP_HEAL_TRACE=1): (1) the friend's Sora held at --low HP (debug lowhp): who
           sees it as needing help, who heals it; (2) the friend down (a 9999 hit): does a companion get it up;
           (3, 4) the same for the host's Sora (the control: the game's own heals). Screenshots heal_<phase>_<copy>_<n>,
           every trace and heal line in heal_<copy>.txt. PASS = no crash (the findings are in the summary).
  swarm    your setup 2026-10-18 (save 10, warp to the Land of Dragons climb 08/07, Shan-Yu's swarm): god mode,
           one-hit kills (KH2COOP_TEST_ONE_HIT=1), both lock on, attack and jump until the mission ends; each
           game's actor budget and enemy count every 2 s (swarm_timeline.txt), screenshots swarm_<copy>_<s>.png.
  keyblade TODO 3.3 / 1.110 research, read only: each game's Keyblade items in the save, both Soras' weapon actors
           (object, group, the file entries they use) and every listed weapon file with its group.
  blade    TODO 3.3 / 1.110 (copy_blade.cpp): Keyblade changes in the pause menu (the friend, then the host twice, the
           second freeing files the host's copy shares), then the friend's form Keyblades in its Drives; each copy
           must take the other player's Keyblade, no crash; --forms-only: only the Drives.
  gym_limit  Track 2: the gym (02/28, no enemies, save 59, its Limit abilities equipped in memory), the forced battle
           state in both games (debug battle on), the --by player's first Limit (--pick), then battle off: state 2
           held, the Limit runs, 0 after, no crash.
  saferead docs/CRASH_RISK_REVIEW.md section 4 (safe_read.cpp): the saferead test command in both games (100 faulting
           reads, our guard page, another thread's stack guard page); both still running 20 s later, no BUG line.
  blend_actors  TODO 1.243 (research, no DLL change): every actor's motion changes in both games in the Borough
           fight (save 97 + its warp), read from outside: the blend the game applied (+0x1A0), its permission (+0x2C0
           bit 0), per actor name. PASS = no crash.
  ended_loop  TODO 5.2d (world_census.cpp): 25 s in the Twin Lords' fight (save 20, the warp to 07/03: their scripts end
           effect loops), then the endedloop test command in both games. PASS = both still run, each test PASS, the room's loop ends kept running and
           none skipped before the test.
  oogie_break  TODO 1.247 (reaction_sync.cpp kKeptActs): Oogie's fight (save 22 + the warp to 0E/09) with the host's
           presents count set to one below a break; the friend jumps belts through his fall and return. PASS = Oogie
           climbs back in and his container rises after the break, no crash (the control on a DLL without kKeptActs fails).
           --breaks N: each next break after the last one's rise.
  bag_leave  Oogie's bags (save 22 + the warp to 0E/09): the friend breaks them; per host leave call, when each
           game's bag left its actor list, with shots of both windows. PASS = no crash, a leave paired.
  rodeo      TODO 1.254 (save 23, Pride Lands): the friend's Rodeo and Grand Cross on a spawned Living Bone; per RC
           each Sora's attach share and offset from its own Living Bone, the friend's detach trace. PASS = both ran.
  rodeo_self TODO 1.255 control (save 23, 0A/05): the host's own Rodeo, then nothing pressed; how the game ends the ride.
  rc_state   TODO 1.255 research (save 23, 0A/05): the Living Bone's Rodeo on / off against its motion, read from outside.
  rapid_blow TODO 1.256 (save 23, 0A/05): the friend's Rapid Blow on a spawned Aerial Knocker; its drop as it dies.
  twin_lk    TODO 5.26 (save 23, then 0A/00 programs 1/0/1): the cutscene twin in lk_event_106. PASS = twinned in both.
  belt_capture  TODO 1.250 (save 22 + the warp to 0E/09): the friend jumps belts (--air: put at the belt's height)
           while its Sora and the host's copy are sampled from outside. FAIL = a capture source on with a land motion
           it has no entry for (the state in which the land handler faults).
  belt_zones  TODO 1.248 (save 22 + the warp to 0E/09): each belt's collision rows, then both games' belt group bits
           sampled. PASS = the friend's belts follow the host's (98% of samples within 0.5 s).
  zones    TODO 1.248 elsewhere: --slot, --warp W,R,P, --near <model>: the same check on those actors.
  form_share  TODO 1.127 (save 59): for each form and both orders, one player Drives, the other into the same form, the
           first Reverts while the second stays in (the friend's crash at Data Roxas, F342); no crash.
  larxene_mem  TODO 1.127 (save 59): Data Larxene (04/21 program 148), her and both Soras invincible, both lock on and
           attack through four rounds of Drives (mixed, swapped, shared, Valor / Wisdom) and Reverts; no crash, every
           look the other game asked in our buffer, nothing declined or moved.
  file_groups  the review's 5.2.6 research, read only (save 59): both games' file lists by group at the room load, each
           player's Drive and Revert, the host's menu swap and a room change; new, gone and moved negative / 5100-5199 files.
  neg_group  the review's 5.2.6 write test (save 59, 'negfiles' test command): a look's files in a negative group of
           ours through Drives, Reverts, the menu swap, a summon, a Limit, a spawn and the room change; nothing of
           ours may move or join another group. --decline: a budget too small. --scene: the Pete walk-in (save 11).
  limit_gap  F356's repro (TODO 1.127, save 59 in the gym, Limits equipped in memory): one player Drives into entry n,
           the other into n + 1, the first Reverts: the game's Limit preload lays the heap out with no load wait; both
           ways, n = 0..--pick-1 (default 2); 'limitgap' first proves the preload runs. PASS = no crash, every case set
           up, no file the copy wears moved by the preload.
  blade_combat  the crash review's section 6 (save 59): the host swaps Keyblade and attacks as its menu closes while
           the friend's game is in battle, then the friend Drives and Reverts; --pick rounds (default 4).
  blade_drive_storm  both players Drive and Revert at random for 300 s while the other walks and attacks.
  room_smoke  an audit's smoke run of one room: --warp W,R,P1,P2,P3 (hex), both attack and mash triangle, enemies
           kept alive; RCs used and refused, grabs, unmatched objects, hits counted and dropped, from both logs.
  talk_events  TODO 1.236 (save 20): --warp W,R,P1,P2,P3 (hex; none: the save's room), the --by player examines
           each --at spot ('--at=X,Y,Z;...'), --keys: vpad steps after it (a choice's other option); --both: then
           the other player does the same (it agrees). Both games' settings calls (caller, depth), holds, cutscenes.
  capacity  TODO 1.44 (NO_ENEMY_LIMIT): --warp W,R,P1,P2,P3 (hex; default the Borough 04,09,1,1,0, save 97; --slot
           for another world's save), the host's Sora put at --host-at X,Y,Z, the friend's at each --friend-at in turn
           (4 s each, it stays at the last); both lock on and attack (--stand: nobody presses) for --seconds (default
           90). Every second in both games: the factory's limit and used (F234), the enemies in the actor list, the
           frame rate. Run it with and without --env KH2COOP_NO_ENEMY_LIMIT=1; --env KH2COOP_TEST_ONE_HIT=1 to kill.
  apart    TODO 1.146 (save 97, warp to the 1000 Heartless 04/11 programs 66): the Soras apart, nobody presses;
           enemies near each Sora, those that left, the swap count (--env KH2COOP_AI_NEAREST_PLAYER=0: control).
  fx_sync0  TODO 1.128 step 0 (save 97, the Borough fight): the host attacks, then the friend; both games' live
           effects compared by role (tools/fx_diff.py): what one game shows and the other doesn't, and doubles.
  fx_sync1  EFFECT_SYNC_PLAN section 7 step 0 (save 59, the Underdrome with Dusks): each player's Limit, then each
           Drive Form attacking; every effect made in both games by cause and file (tools/fx_diff.py made_report).
  blade_menu_move  the friend in Valor; the host swaps to the --pick-th Keyblade (default 3) and back.
  rc_dusk  network plan step 2 (your design 2026-11-04): a normal enemy's RC over the link. The Underdrome arena
           (06/0D), --pick Dusks (default 6) made by the host and kept alive (enemy god mode), both Soras invincible,
           not attacking, mashing triangle (the Dusk's Reversal) for --seconds (default 90). Counts from both logs:
           offers sent to the friend, offers in its menu, its presses sent, run and refused (and why), the host's own.
           With --env KH2COOP_NET_SIM=latency,jitter,loss for lag. PASS = no crash and a press of the friend's ran.
  barrels  TODO 1.225 (save 59, the warp into Port Royal's barrel mission, 10/03 programs 56, CA03_MS106): the
           friend's Sora is put beside each barrel F_CA050 in the HUD's order (the first icon explodes first) and
           presses triangle (command 273, the throw), then waits for the host's counter, for --seconds (default 90).
           Auto Limit is off (the mash started Jack's). --host-only: the host's Sora throws (the control). Counts from
           both logs: the friend's presses sent by serial, run and refused by the host, its own-game throws (the old
           way), barrels parked in its game, the host's counter moves, mission complete. PASS = no crash, a throw
           moved the host's counter, the host ran a press of the friend's, none of its throws ran in its own game.
  barbossa  TODO 1.217 (save 59, the warp into Barbossa's fight, 10/0A programs 60): the friend's Sora is put beside
           B_CA000 and attacks until it dies. Screenshots of both games in the dark and after. PASS = the friend's
           game saw the kill and neither game crashed.
  pete_curtain  TODO 1.215 (save 17, the warp into Pete's last fight, 0D/03 programs 53): Pete's HP is written down in
           five steps in the host's game. Reports each game's curtain bit changes and camera '20' starts, with
           screenshots at the first two closes. PASS = the friend's game closed the curtain as often as the host's and
           both games started his camera at every change.
  cup_retry  TODO 1.213 (save 59, the warp into an Underdrome round, 06/09 programs 189): both Soras are downed by
           debug hits. Both must pick the same row (TODO 1.228): the host picks Retry, the friend Quit, then Retry;
           both down again, the friend picks Quit, then the host. Reports each menu's row kinds and cursor from
           memory. PASS = nothing ran until both picks matched, then that row ran in both games, both ended in
           the same room, no crash. --pause (TODO 1.214): nobody down, the host picks Retry, then Quit, in its
           pause; PASS = the friend's game ran the Retry too and both ended in the same room.
  limit_out  TODO 1.180 (save 59, the Underdrome 06/0D with Auron and 4 Dusks kept alive; --warp W,R,P1,P2,P3 in hex
           for another room, e.g. --slot 13 --warp 5,5,4E,4E,4E for Beast): --by starts the list's --pick-th Limit and
           nobody presses, so its gauge runs out (--triangle: mash its commands instead). PASS = its end ran, no crash.
  rc_lords  a boss's RC over the link (your tip 2026-11-05: the Volcano Lord opens the Twin Lords fight with an RC
           prompt, once a fight). Save 20, the warp into the fight (07/03), the friend mashes triangle from the
           arrival until the host runs its press (at most --seconds, default 30), then until the grab reaches the
           friend (at most 10 s) + 1 s and out. Counts: the friend's presses sent, repeats held back, run on the
           host, the press's trip (both logs share this PC's clock), the host's grab calls on the friend's Sora.
           With --env KH2COOP_NET_SIM for lag. PASS = exactly one run (a repeat restarts the RC, F444).
  gain     network plan step 4: the friend's item on the host's companion (companion_sync.cpp's gain, reliable).
           Save 52 at 04/09: the host's two companions lose 30 HP (the friend's copies follow), then the friend uses
           its first item on its second target; --pick rounds (default 3). PASS = no crash and every gain the friend
           sent was applied on the host.
  copy_row  TODO 3.17 / 3.7 (a Mods Manager Build first: the 03system.bin patch): save 52 at 04/09, full party. The
           friend, then the host, uses on the other's copy through its own row ('Sora (P1)' / 'Sora (P2)', the
           cursor down to it, then up once: the wrap) a Hi-Potion, an Ether and an Elixir (the bench puts them and
           the Mega items in Sora's slots), then a Mega-Potion and a Mega-Ether, Cure (MP filled), and a Hi-Potion
           on itself; then the host's Goofy KO'd while it hovers the row, and a room change with the list open.
           PASS = each pick queued on the copy, the item or Cure sent, the item's sparkle moved to the copy, the
           receiver healed (MP for the Ethers and Elixir) with its sparkle on its own Sora (and on the copy for a
           party item), the self-use sparkle on the copy in the other game, no crash. Screenshots copy_row_*.
           --warp 2,0,0,0,0: the void (02/00, no companions): the game lists the copy as 'Sora' and our row takes
           its place; a party item's sparkle loop is kept off the copy (no double sparkle on our own Sora).
           --attack-mash: only an HP item and a Cure on our row, each followed at once by up and cross
           mashed through its flight: PASS = the first landing after the pick still on the copy and sent, the
           receiver healed; the presses the game took before the landing are listed (none on 2026-11-13).
  skip     network plan step 4: the host's Skip Scene over the link (skip_sync.cpp). Save 11, the warp to the
           Mysterious Tower 02/19, the walk-in (Pete) scene in both games; the host pauses and picks Skip Scene.
           PASS = the host's skip logged, the friend's game skipped its scene like the host, both left the scene;
           and before it (TODO 1.227) each player pauses once: the host's pause has 2 rows, the friend's 1 (no Skip
           Scene), both logs name who paused, and the friend's Continue resumes both (screenshots scene_pause_by_*).
  chest_menu  TODO 1.237 (save 52): the host opens its Start menu (any menu holds a gift back), the friend's
           game opens chest 359 by the test command. PASS = the host is given nothing while its menu is open,
           and gets the item and its window once the menu has closed.
  chest_pose  TODO 1.234 (save 8, the warp to 02/09; --friend-slot N loads another save in the friend's copy, so
           the two games make the room's chests at other addresses): the host opens chest 304 by the test command
           ('Open', as a press) standing at it, the friend 305, then the host 306 standing far from it. PASS = each
           near opening's pose (motion 252 captured from the chest) played once on its copy in the other game, none
           skipped, and the chests sat at different addresses in the two games (else SETUP FAILURE: the old
           same-address lookup finds them and the fix isn't what ran). The far one is listed, not judged.
           --together (F669's case): first the host opens 306 alone (the control: one box in each game, else SETUP
           FAILURE); then the host at 304 and the friend at 305, each until its game offers 'Open', press Triangle
           at once; then the friend's game takes chest 359 by the test command. PASS = after the pair each game
           queued and showed two item boxes and went idle, both chests open in both, and 359 showed its box in the
           host's game (screenshots chest_together_*). --gap S: the friend presses S seconds after the host.
           --kind: after the control the host's game opens 304 and 305 20 ms apart (two of its own boxes).
           --leave: the host warps to 02/02 0.3 s after both press (PASS = no fault, both in 02/02). --third S:
           the friend's game takes 359 S seconds after the pair's presses, on top of a box waiting for its icon
           (TODO 1.222's re-ask): PASS = the host shows 3 boxes, the friend 2, both idle, no 'given up' line.
           --third S --on-ask: instead the host's game opens 359 the moment its icon slot starts a load S seconds
           or more after the presses (the re-ask's): PASS = 3 boxes in each game, both idle, no 'given up' line.
           Today a SETUP FAILURE: the host's game has no chest object of 359 (2026-11-18), and the room's three
           chests are taken by the control and the pair.
  vexen_freeze  TODO 1.229, 1.230 (save 59, the warp to Absent Silhouette Vexen, 04/20 programs 115): the friend is
           kept next to him and the host far, until he freezes the friend, which then mashes Struggle (RC 718).
           PASS = the host ran at least 3 of the presses and neither screen is white 3 s after the let go.
           --want host: the host is frozen instead (screenshots of both games while it is).
  pause_race  TODO 1.18 (pause_sync.cpp): in the same walk-in scene, three pauses: the host pauses right after its
           own pause closed to match the friend's resume; the host resumes and pauses again at once; a plain pause.
           PASS = both games end paused each time and stay so; reports how long each took.
  twin_blade  TODO 5.20 (cutscene_twin.cpp): the same walk-in scene played past Sora's Keyblade summon (frame 4279,
           ~2.4 min at 30 scene frames a second) and its effect (4338); PASS = in both games Sora's Keyblade is held by Sora and the twin's by the
           twin, on the same bone, an effect is tied to each Keyblade, and no twin effect was skipped.
  same_room  TODO 5.23: save 11, the warp to 02/1B programs 1/0/1 (tt_event_805 ends with a jump into the same room),
           PAUSE_SYNC=0; --by A (default) pauses the host's scene so the friend's ends first, --by B the friend's.
           --then resume (default) / skip (the host picks Skip Scene) / close (the host's game is killed while the
           friend's move is held). PASS: --by A: the friend plays the scene once, holds its move and does it later;
           --by B: each plays it once, nothing held; close: also the friend alive.
  npc_late  TODO 1.192: save 17 (Timeless River 0D/00), the friend opens its pause menu (PAUSE_SYNC=0), the host
           reloads 0D/00 in programs 0/0/18 (the crash's version) and goes in alone; 6 s later the friend closes the
           menu and follow brings it in late. PASS: no crash and none of the room's four _RTN NPCs made by the friend.
  scaffold  TODO 1.193: save 17, the warp into the scaffold mission (0D/05 programs 55/55/55, ENEMY_GOD=1); --ride friend
           (default: the host's Sora kept in the air) / host (the friend's kept up) / both. A 50 ms sampler: what each
           Sora's +0x9D8 rides (the host's copy too), heights and motions, through the first wire's shake. The rider
           (the host for both) presses triangle for Air Slash; --mash both: both players; --mash-delay S: from S s into
           the shake. PASS = the shake came, no crash, the friend peaked within y (-1400, -900) and each launch sent
           (--ride both --mash both, TODO 1.212: no crash, both Soras' Air Slash ran, no press dropped for the other's;
           --mash none: nobody presses, and the friend's Sora must be launched at least twice)
           to it had its own flight (host: the control, nothing sent; both: the host's peak too).
  hydra    TODO 1.191: save 16 (Olympus), the warp into the Hydra fight (06/12 programs 171/171/171, HE18_MS108).
           Phase 1: the friend waits away, the host stands in front of the Hydra, locked on, and mashes attack and
           triangle until its Vanquish (238) runs; then, once the Hydra is down (motion 194), the host is parked about
           1000 away and the friend is put on its back, where it mashes triangle for Phil One-Two (346) and then
           Urninator (420). A sampler logs the copy's hook +0xD48, +0x18C bit 21 and chain +0x638 in the host's game
           and the friend's +0x6C8 bit 25. PASS = the friend's 420 ran once on the host's Hydra (triangle mashed on
           to +7 s after 346: a repeat is refused, 'taken once'); the report also says
           whether F532's signature came (the host's switch to our Sora, then its 1358 poll and 1413 free).
  cerberus  TODO 1.195: save 14 (Olympus 06/0A), the warp into the Cerberus fight (06/07 programs 114/114/114;
           --setup-only: stop there). The host is parked away and the friend's Sora held in the air 400 in front of
           Cerberus; once his pick is the copy and he stands on the floor, his bite is started with the dev command
           'act <address> atk_bite_press' (tools/kh2cmd.py), up to 120 s of tries (a bite can hit Hercules). Once
           grabbed, the friend mashes triangle for the three RCs (38, 39, 41) until its Sora is out of the grab pose.
           A 3 ms trace of both Soras' head bones: each motion change's biggest head step (the friend's against the
           host's copy's: a snap, 200+ over the copy's from the grab on) and release_report (blink, turn). Controls:
           --env KH2COOP_RELEASE_NOBLEND=0 (the release), --env KH2COOP_GRAB_PLACE=0 (the start and the throw).
           Head steps count only drawn frames (a head read in two rows in a row; one read once is our frame start
           before the Sora's update). PASS = no crash, the friend out of the grab, no snap from its grab pose on, its
           release smooth with the fix's lines logged, under 16 ms on its grab spot before the grab pose, and the
           DLL's next 5 held writes after each script place correcting 300 or less.
           Each window's title names its role, the scenario and the --env settings (what you are watching).
  timer_freeze  TODO 1.196: save 15, the warp into Pete with Hercules (06/08 programs 116, HE08_MS107, a 90 s
           countdown). Once both timers count down within 15 frames, the friend's pause word gets bit 4 (an event
           playing, L335) for 3 s, then loses it: the friend's own RC scene without the RC. A 50 ms sampler reads both
           timers. --rc: instead the friend is put by its Hercules and mashes triangle until its RC 307 runs (40 s).
           PASS = while the friend's own pause held its clock its start never moved, within 1 s after it the friend's
           timer is within 15 frames of the host's and still running, no 'failed the mission' line, no crash.
           SETUP FAILURE if the timers never ran together or the written pause didn't stop the friend's clock.
  bolt     TODO 1.197 (KH2COOP_DEBUG, the dev command 'act'): save 52, the warp into a gym room (--where gym 02/28,
           void 02/00, arena 06/0D; programs 0/0/0), one Bolt Tower spawned (--obj, default 8BE M_EX800_MU; ENEMY_GOD,
           TRAP_LOG). No teleport (it dropped a Sora off the floor): both stay where the warp puts them, and 'act
           <tower> atk_laser' is sent to the host's tower until it hooks a Sora (in the host's game); --want friend
           (default) / host / any: a catch of the other Sora is waited out and not counted. --solo: TODO 1.197's
           report (F579): flag 8 is written on that Sora in the host's game only around each try, so the host's catch
           bails (obj_is_star) while the friend's tower copy runs its own; seen from the friend's game. --press mash: the caught player mashes triangle (Bolt Reversal 380 / 663); none: the 3.0 s
           timeout and its strikes (F577). --catches N (default 2), then 22 s with no act (two 10 s trap-count blocks).
           A 5 ms sampler (bolt_trace.txt): each Sora and copy's motion, flag 8, hook, bit 14, RC window, +0x638, both
           towers; every 0.5 s the tower's live effects per game (4, 5, 7 = the beam, F580). Reported per catch: the
           timelines, when the beam was live in each game, whose script set flag 8 / idle / threads (trap 'by'), the
           friend's refused own catch (its thread), the presses sent / dropped / run, 'protected' strikes; at the end,
           'BEAM LEFT' per game. Screenshots bolt_<n>_mid_* / _after_*. FAIL: a beam live 2.5 s past the host's let
           go or at the end, flag 8 on a friend-side Sora 1 s past it, the friend's tower copy making its own catch
           calls (set_flag 8, idle, magic threads), its thread count (+0x5B8) below the setup's at the end, with
           --press mash no capture 254 (not for 8F0, F589).
Results: build/bench/extra_copy/<scenario>.txt and screenshots. PASS/FAIL exit code.
"""
import argparse
import os
import re
import math
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import peek  # noqa: E402
import scene  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "extra_copy")


def find(lines, text):
    return [l for l in lines if text in l]


def copies_made(lines):
    return [l for l in lines if "extra copy:" in l and ": made" in l]


def ours(lines):
    after = [l for l in lines if "extra copy: after our Sora:" in l]
    return bool(after) and all(l.count("our Sora") == 4 for l in after)  # 'after our Sora' + 3x


def load_both(copies, slot, room):
    bench.load_slot_both(copies, slot, room)
    bench.wait("both in the save's room", lambda: all(c.room() == room for c in copies), 60, copies)
    time.sleep(6)


def ship(copies, a, out):
    load_both(copies, 45, (0x12, 0x12))
    marks = {c.name: c.mark() for c in copies}
    bench.warp(0x12, 0x16, 72)
    bench.wait("both in the ship fight", lambda: all(c.room() == (0x12, 0x16) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(10)
    host = copies[0]
    for _ in range(30):  # the host attacks for a while (the 07:03 crash came at the first attack)
        host.game.pad.run(["tap", "cross", "wait", "300"])
    time.sleep(10)
    ok = True
    for c in copies:
        new = c.since(marks[c.name])
        made = copies_made(new)
        good = c.alive() and not c.crash and c.room() == (0x12, 0x16)
        ok &= good
        out.append(f"{c.name}: copy made {len(made)} (0 expected), alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"ship_{c.name}.png"))
    return ok


def alone(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    m = host.mark()
    friend.proc.kill()
    bench.wait("the host sees the link lost", lambda: find(host.since(m), "link watch: connection lost"), 20, [host])
    time.sleep(2)
    bench.debug("A", 10, 3)  # play on with Donald and Goofy
    bench.wait("playing on", lambda: find(host.since(m), "link watch: playing on with Donald and Goofy"), 20, [host])
    time.sleep(8)
    m2 = host.mark()
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m2), "debug warp: asked"), 30, [host])
    time.sleep(10)
    solo_new = host.since(m2)
    solo_ok = not copies_made(solo_new) and host.alive() and not host.crash
    out.append(f"alone: playing on, room reloaded: copy made {len(copies_made(solo_new))} (expected 0), "
               f"alive {host.alive()}, crash {host.crash or '-'}")
    bench.screenshot(host.hwnd, os.path.join(OUT, "alone_A_solo.png"))
    # the friend comes back
    m3 = host.mark()
    new = bench.launch(names=("B",), keep_running=True)[0]
    copies[1] = new
    scene.load_slot(new.game, a.slot, a.room)
    bench.wait("co-op again on the host", lambda: find(host.since(m3), "co-op again") or
               find(host.since(m3), "game is back"), 60, [host, new])
    time.sleep(5)
    if not copies_made(host.since(m3)):  # the copy comes with the next room load
        bench.warp(*a.room)
    bench.wait("the copy back in the host's game", lambda: copies_made(host.since(m3)), 60, [host, new])
    time.sleep(10)
    back = host.since(m3)
    fnew = new.since(0)
    back_ok = bool(copies_made(back)) and ours(back) and bool(copies_made(fnew)) and not host.crash and not new.crash
    out.append(f"rejoin: host copy made {len(copies_made(back))}, ours {ours(back)}, friend copy made "
               f"{len(copies_made(fnew))}, crash {host.crash or new.crash or '-'}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"alone_{c.name}_back.png"))
    return solo_ok and back_ok


def downed(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    # The friend's Sora: a lethal hit on its copy in the host's game, forwarded (the friend's game drops other damage on
    # its own Sora). Its own hit rules may keep it at 1 HP (Second Chance, Once More: TODO 1.77 / 1.91); then another.
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        fl = friend.since(marks["B"])
        down = find(fl, "down instead of dead")
        saved = [l[13:] for l in find(fl, "the peer's hit (-9999) on our Sora:")]
        if down or len(saved) <= n:
            break
        out.append(f"friend lethal hit {n + 1}: kept at 1 HP by its own rules ({saved[n][:90]})")
        time.sleep(3)  # out of the hit reaction, so Once More doesn't apply to the next one
    out.append(f"friend lethal hit: {'down' if down else 'NOT down'} ({(down or ['-'])[0][13:120]})")
    # Donald while the friend lies down (two-PC run: the friend heard Donald call 'Sora!' over and over)
    donald = {c.name: bench.actor_by_model(c, "P_EX020") for c in copies}
    seq = {c.name: [] for c in copies}
    for _ in range(4):  # short: the host stands next to the friend, which revives it within ~6 s
        for c in copies:
            m = bench.motion(c, donald[c.name]) if donald[c.name] else None
            if not seq[c.name] or seq[c.name][-1] != m:
                seq[c.name].append(m)
        time.sleep(0.5)
    for c in copies:
        out.append(f"{c.name}: Donald's motions over 3 s while the friend is down: {seq[c.name]}")
    bench.debug("A", 0, 9999)  # now the host too: both down
    time.sleep(20)
    over = {c.name: find(c.since(marks[c.name]), "re_gameover") + find(c.since(marks[c.name]), "both players are down")
            for c in copies}
    for c in copies:
        out.append(f"{c.name}: game over lines {len(over[c.name])} ({(over[c.name] or ['-'])[0][13:110]}), "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"downed_{c.name}.png"))
    return bool(down) and all(over.values()) and all(c.alive() and not c.crash for c in copies)


DRIVE = ["tap", "down", "wait", "300"] * 3  # Attack -> Magic -> Items -> Drive


def forms(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    p = friend if a.by == "B" else host  # who goes into the form
    marks = {c.name: c.mark() for c in copies}
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                      ["tap", "cross", "wait", "500"])  # Drive -> Valor (--pick 1..4: Wisdom, Limit, Master, Final)
    time.sleep(12)
    bench.screenshot(host.hwnd, os.path.join(OUT, "forms_A_valor.png"))
    bench.screenshot(friend.hwnd, os.path.join(OUT, "forms_B_valor.png"))
    hl = p.since(marks[p.name])
    went = find(hl, "the player switched actors")
    out.append(f"form screenshot: forms_A_valor.png; commands {[l[13:90] for l in hl if 'menu: command' in l]}")
    out.append(f"form {a.pick}: player switched {len(went)} ({(find(hl, 'command 0x6') or ['no command 0x6 line'])[0][13:100]})")
    p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert (or the forms list)
    time.sleep(10)
    hl = p.since(marks[p.name])
    back = find(hl, "the player switched actors")
    out.append(f"Revert: player switched {len(back)} in all ({(find(hl, 'command 0x5') or ['no command 0x5 line'])[0][13:100]})")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"forms_{c.name}_revert.png"))
    lost = {c.name: find(c.since(marks[c.name]), "Sora copy lost") + find(c.since(marks[c.name]), "copy gone") for c in copies}
    m2 = {c.name: c.mark() for c in copies}
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m2["A"]), "debug warp: asked"), 30, [host])
    time.sleep(12)
    again = copies_made(host.since(m2["A"]))
    out.append(f"after a room reload: host copy made {len(again)}, ours {ours(host.since(m2['A']))}")
    for c in copies:
        out.append(f"{c.name}: copy lost lines {len(lost[c.name])}, alive {c.alive()}, crash {c.crash or '-'}")
    return (len(went) >= 1 and len(back) >= 2 and bool(again) and ours(host.since(m2["A"]))
            and all(c.alive() and not c.crash for c in copies))


def hud_shot(c, step):
    from PIL import Image
    path = os.path.join(OUT, f"hud_{c.name}_{step}.png")
    bench.screenshot(c.hwnd, path)
    try:
        im = Image.open(path)
        w, h = im.size
        im.crop((int(w * 0.62), int(h * 0.45), w, h)).resize((int(w * 0.38 * 2), int(h * 0.55 * 2))).save(
            path.replace(".png", "_crop.png"))
    except OSError:
        pass


def hud_strip(copies, step, seconds, action, box=(0.80, 0.30, 1.0, 0.86), name="hud", width=None):
    """Each game's HUD corner (`box`: the part of the window, as fractions) as fast as screenshots come for `seconds`
    while `action` runs: one sheet per copy (<name>_<copy>_<step>_strip.png: frames left to right, then down, the
    seconds since the start in each corner; `width`: each frame scaled to it), to see a gauge's slide or shake, or a
    move. Returns the frames taken per copy."""
    import threading
    from PIL import Image, ImageDraw
    frames = {c.name: [] for c in copies}
    t0 = time.time()

    def grab(c):
        while time.time() - t0 < seconds:
            path = os.path.join(OUT, f"_strip_{c.name}_{len(frames[c.name])}.png")
            t = time.time() - t0
            if bench.screenshot(c.hwnd, path):
                frames[c.name].append((t, path))

    threads = [threading.Thread(target=grab, args=(c,)) for c in copies]
    for t in threads:
        t.start()
    action()
    for t in threads:
        t.join()
    for c in copies:
        crops = []
        for t, path in frames[c.name]:
            try:
                im = Image.open(path)
                w, h = im.size
                crop = im.crop((int(w * box[0]), int(h * box[1]), int(w * box[2]), int(h * box[3])))
                if width:
                    crop = crop.resize((width, crop.size[1] * width // crop.size[0]))
                ImageDraw.Draw(crop).text((3, 3), f"{t:.2f}", fill=(255, 255, 0))
                crops.append(crop)
                os.remove(path)
            except OSError:
                pass
        if crops:
            cols, (cw, ch) = 8, crops[0].size
            sheet = Image.new("RGB", (cw * cols, ch * ((len(crops) + cols - 1) // cols)))
            for i, crop in enumerate(crops):
                sheet.paste(crop, ((i % cols) * cw, (i // cols) * ch))
            sheet.save(os.path.join(OUT, f"{name}_{c.name}_{step}_strip.png"))
    return {n: len(f) for n, f in frames.items()}


def blend(copies, a, out):
    """TODO 1.56 (research, L352): the blend the game passes at Sora's own motion changes. Each player in turn walks,
    stops, walks back, jumps, lands and attacks; the DLL's 'blend log' lines (motion from -> to, blend, start, the
    controller's blend flag, the copy's) are tallied per kind of change. PASS = no crash."""
    load_both(copies, a.slot, a.room)
    time.sleep(8)
    marks = {c.name: c.mark() for c in copies}
    moves = ["stick", "0", "1", "1500", "wait", "1200", "stick", "0", "-1", "1500", "wait", "1200", "tap", "circle",
             "wait", "1500", "stick", "0", "1", "800", "tap", "circle", "wait", "1500"] + [
             "tap", "cross", "wait", "250"] * 3 + ["wait", "1500"]
    for c in copies:
        c.game.pad.run(moves)
        time.sleep(2)
    for c in copies:
        tally = {}
        for line in c.since(marks[c.name]):
            m = re.search(r"blend log: our Sora motion (-?\d+) -> (-?\d+), blend ([\d.]+), start ([\d.-]+), "
                          r"blend flag (\d+), the copy's (-?\d+)", line)
            if m:
                tally[m.groups()] = tally.get(m.groups(), 0) + 1
        out.append(f"{c.name}: {len(tally)} kinds of motion change")
        for k, n in sorted(tally.items(), key=lambda kv: (int(kv[0][0]), int(kv[0][1]))):
            out.append(f"  {k[0]:>4} -> {k[1]:>4}  blend {k[2]:>5}  start {k[3]:>5}  flag {k[4]}  copy's flag {k[5]}  x{n}")
        sets = {}  # the copy's own motion changes in this game, with the blend it was given
        for line in c.since(marks[c.name]):
            m = re.search(r"puppet: motion (-?\d+) -> (\d+) \(peer \d+, blend (\d+)\)", line)
            if m:
                sets[m.groups()] = sets.get(m.groups(), 0) + 1
        out.append(f"{c.name}: its copy's motion changes: " + ", ".join(
            f"{k[0]}->{k[1]} blend {k[2]} x{n}" for k, n in sorted(sets.items(), key=lambda kv: (int(kv[0][0]), int(kv[0][1])))))
    # Does the game blend the copy? Its motion controller's blend frames (+0x158 +0x48, L352) read in the watcher's
    # game while the other Sora starts walking and stops; a strip of the watcher's window for the look.
    import threading
    for mover, watcher in ((copies[0], copies[1]), (copies[1], copies[0])):
        acts = bench.actors(watcher)
        copy = next((x for x, n in acts[1:] if n.startswith("P_EX100")), 0)
        seen = []
        walk = threading.Thread(target=lambda: mover.game.pad.run(["stick", "0", "1", "900", "wait", "900"]))
        walk.start()
        end = time.time() + 1.8
        while copy and time.time() < end:
            seen.append(struct.unpack("<f", peek.read(watcher.game.h, copy + 0x158 + 0x48, 4))[0])
            time.sleep(0.005)
        walk.join()
        out.append(f"{watcher.name} watching {mover.name}: the copy's blend frames read {len(seen)} times, above 0 in "
                   f"{sum(v > 0 for v in seen)}, highest {max(seen, default=0):.1f}")
        hud_strip([watcher], f"watch_{mover.name}", 3, lambda: mover.game.pad.run(
            ["stick", "0", "1", "900", "wait", "700", "stick", "0", "-1", "900"]), box=(0.15, 0.2, 0.85, 0.95), name="blend", width=300)
    return all(c.alive() and not c.crash for c in copies)


def blend_actors(copies, a, out):
    """TODO 1.243 (research, L1564, L1565; no DLL change): every actor's motion changes in both games, read from outside
    about every 5 ms in a fight (save 97 and its warp to the Borough fight 04/09 by default: Donald, Goofy, Heartless;
    enemies can't die): its motion id (actor+0x180), the blend frames the game applied (+0x1A0 = ctl+0x48, the highest
    seen while that motion lasts), its blend permission (+0x2C0 bit 0) and its clock (+0x19C). The host's Sora walks,
    stops, jumps and attacks so the companions follow and the enemies answer. Tallied per game and actor name: motion
    changes, how many had a blend, the blends seen, the permission bit. PASS = no crash (research)."""
    import threading
    host = copies[0]
    load_both(copies, a.slot, a.room)
    if a.slot == 97:
        bench.warp(*LIMIT_FIGHTS[97])
        bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 90, copies)
    time.sleep(6)
    moves = (["stick", "0", "1", "1500", "wait", "1000", "stick", "0", "-1", "1500", "wait", "1000",
              "stick", "1", "0", "1200", "wait", "800", "tap", "circle", "wait", "1200"]
             + ["tap", "cross", "wait", "300"] * 4 + ["wait", "1500"]) * 2
    walk = threading.Thread(target=lambda: host.game.pad.run(moves))
    lists = {c.name: bench.actors(c) for c in copies}
    last = {c.name: {} for c in copies}    # actor -> [name, motion, highest blend seen for it, motion before]
    done = {c.name: [] for c in copies}    # (name, from, to, blend, permission)
    polls, listed = 0, time.time()
    walk.start()
    while walk.is_alive():
        if time.time() - listed > 2:
            lists, listed = {c.name: bench.actors(c) for c in copies}, time.time()
        for c in copies:
            for x, name in lists[c.name]:
                try:
                    raw = peek.read(c.game.h, x + 0x180, 0x144)
                except OSError:
                    continue
                motion = struct.unpack_from("<i", raw, 0)[0]
                blend = struct.unpack_from("<f", raw, 0x20)[0]
                perm = raw[0x140] & 1
                st = last[c.name].get(x)
                if st is None or st[0] != name:
                    last[c.name][x] = [name, motion, 0.0, None, perm]
                elif motion != st[1]:
                    if st[3] is not None:
                        done[c.name].append((name, st[3], st[1], st[2], st[4]))
                    last[c.name][x] = [name, motion, blend if blend > 0 else 0.0, st[1], perm]
                elif blend > st[2]:
                    st[2] = blend
        polls += 1
        time.sleep(0.005)
    for c in copies:
        for name, motion, blend, before, perm in last[c.name].values():
            if before is not None:
                done[c.name].append((name, before, motion, blend, perm))
    out.append(f"{polls} polls")
    for c in copies:
        out.append(f"{c.name} ({'host' if c is host else 'friend'}): {len(done[c.name])} motion changes")
        per = {}
        for name, frm, to, blend, perm in done[c.name]:
            t = per.setdefault(name, {"n": 0, "blended": 0, "blends": {}, "perm": set(), "kinds": {}})
            t["n"] += 1
            t["blended"] += blend > 2.0
            t["blends"][round(blend)] = t["blends"].get(round(blend), 0) + 1
            t["perm"].add(perm)
            k = (frm, to, round(blend))
            t["kinds"][k] = t["kinds"].get(k, 0) + 1
        for name, t in sorted(per.items(), key=lambda kv: -kv[1]["n"]):
            out.append(f"  {name:<22} changes {t['n']:>4}, with a blend {t['blended']:>4}, blends "
                       + ", ".join(f"{b}:{n}" for b, n in sorted(t["blends"].items())) + f", permission {sorted(t['perm'])}")
            top = sorted(t["kinds"].items(), key=lambda kv: -kv[1])[:6]
            out.append("      " + "; ".join(f"{f}->{to} blend {b} x{n}" for (f, to, b), n in top))
    return all(c.alive() and not c.crash for c in copies)

def hud(copies, a, out):
    """The third party gauge (party_hud.cpp): the other Sora's own gauge on top of the stack. --by Drives (--pick: the
    list entry) and Reverts: its gauge slides down a place as the companion leaves and back up at the Revert (the
    game's own slide); then each Sora takes a 5 HP hit: its gauge in the other game shakes. Stills hud_<copy>_<step>,
    strips hud_<copy>_<step>_strip (the slides and the shakes are judged from them). PASS = the gauge built and shown,
    a slide down and one up logged in each game, no crash."""
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    time.sleep(10)
    for c in copies:
        hud_shot(c, "1_party")
    p = friend if a.by == "B" else host
    n = hud_strip(copies, "2_form", 7, lambda: p.game.pad.run(
        DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick + ["tap", "cross", "wait", "500"]))
    out.append(f"strip frames during the Drive: {n}")
    time.sleep(6)
    for c in copies:
        hud_shot(c, "2_form")
    hud_strip(copies, "3_revert", 7, lambda: p.game.pad.run(
        DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"]))  # Revert
    time.sleep(5)
    for c in copies:
        hud_shot(c, "3_revert")
    # A hit on each Sora, one test command per frame: the host's own, then the friend's through its copy (forwarded).
    hud_strip(copies, "4_hit_host", 3, lambda: (time.sleep(0.5), bench.debug("A", 0, 5)))
    hud_strip(copies, "5_hit_friend", 3, lambda: (time.sleep(0.5), bench.debug("A", 3, 5)))
    ok = True
    for c in copies:
        lines = [l for l in c.since(marks[c.name]) if " hud: " in l]
        built = find(lines, "third gauge built")
        third = find(lines, "shown in the third gauge")
        down, up = find(lines, "slides down"), find(lines, "slides up")
        hits = find(lines, "played hit event")  # none from these test hits: they change HP without a hit (3.18)
        out.append(f"{c.name}: built {len(built)}, shown in the third gauge {len(third)}, slides down {len(down)}, up "
                   f"{len(up)}, hit events {len(hits)}, alive {c.alive()}, crash {c.crash or '-'}")
        for l in lines[:40]:
            out.append("   " + l[:200])
        ok = ok and bool(built) and bool(third) and bool(down) and bool(up) and c.alive() and not c.crash
    return ok


def hud2(copies, a, out):
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    time.sleep(10)
    for c in copies:
        hud_shot(c, "1_start")
    host.game.pad.run(["tap", "start", "wait", "1500"])
    for c in copies:
        hud_shot(c, "2_pause")
    host.game.pad.run(["tap", "start", "wait", "1500"])
    bench.warp(0x04, 0x09)  # away from the save point (it refills HP within a second)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 60, copies)
    time.sleep(12)
    for c in copies:
        hud_shot(c, "3_room")
    bench.debug("A", 0, 30)  # the host's Sora (a hit on the friend's own Sora there is dropped: the host owns it)
    time.sleep(1)  # one test command per frame
    bench.debug("A", 3, 20)  # the friend's copy in the host's game: forwarded to the friend's Sora
    time.sleep(1)
    for c in copies:
        hud_shot(c, "4_hurt")
    bench.debug("A", 0, 999)
    time.sleep(3)
    for c in copies:
        hud_shot(c, "5_down")
    time.sleep(8)
    ok = True
    for c in copies:
        lines = [l for l in c.since(marks[c.name]) if " hud: " in l]
        third = find(lines, "shown in the third gauge")
        out.append(f"{c.name}: shown in the third gauge {len(third)}, HUD builds {len(find(lines, 'built its HUD'))}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        for l in lines[:40]:
            out.append("   " + l[:200])
        ok = ok and bool(third) and c.alive() and not c.crash
    return ok


PAGE2 = DRIVE + ["tap", "left", "wait", "1000"]  # Attack / Summon / Party / Limit, cursor on Limit


def special(copies, a, out, name, keys, picks=None, warp=None, steps=4, during=None, prep=None, loaded=None,
            until=None):
    load_both(copies, a.slot, a.room)
    if loaded:  # e.g. equip the Limit abilities before the warp's room load builds the ability bits (gym_limit)
        loaded()
    if warp:  # (world, room, program): e.g. into a fight
        bench.warp(*warp)
        bench.wait(f"both in {warp[0]:02X}/{warp[1]:02X}", lambda: all(c.room() == warp[:2] for c in copies), 60, copies)
        time.sleep(12)
    if prep:  # e.g. the forced battle state (gym_limit)
        prep()
    host = copies[1] if a.by == "B" else copies[0]  # the one who presses
    marks = {c.name: c.mark() for c in copies}
    host.game.pad.run(PAGE2 + keys)
    step = 0
    # until: past `steps`, up to 6 more presses (~15 s) while it's false. A hit or a frozen partner swallows presses
    # (limit_friend at 300 ms, 2026-11-05: a hit at the 2nd press, Aladdin in the Blizzard Lord's ice at the 3rd).
    while step < steps or (until and step < steps + 6 and not until()):
        time.sleep(2.5)
        for c in (copies if a.shots else [host]):  # --shots: the other game too (TODO 1.57: the Limit partner)
            bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_{step}.png"))
        if step == 1 and picks:
            host.game.pad.run(picks)  # move down the opened list
        if step == 3 and during:
            during()
        host.game.pad.run(["tap", "cross", "wait", "300"])
        step += 1
    if a.shots:
        for k in range(6):
            time.sleep(2)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_{4 + k}.png"))
    time.sleep(8 if a.shots else 20)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_after.png"))
    hl = host.since(marks[host.name])
    cmds = [l[13:110] for l in hl if "carries out command" in l or "menu: command" in l]
    out.append(f"host commands: {cmds[:8]}")
    for c in copies:
        gone = find(c.since(marks[c.name]), "Sora copy gone")
        out.append(f"{c.name}: copy gone lines {len(gone)} ({(gone or ['-'])[0][13:110]}), alive {c.alive()}, crash {c.crash or '-'}")
    return all(c.alive() and not c.crash for c in copies)


# Fights to use a Limit in, by save: 20 = Agrabah's Twin Lords (Donald + Aladdin), 97 = Hollow Bastion's Borough
# Heartless (Donald + Goofy: Trinity; test_census.py 'borough'). (world, room, program, door)
LIMIT_FIGHTS = {20: (0x07, 0x03, 59), 97: (0x04, 0x09, 1, 2)}


def limit(copies, a, out):
    marks = {c.name: c.mark() for c in copies}
    # A Limit only starts in a fight: on save 52's Borough save point and on the street next to it the Limit list
    # never opened (session 32, 'host commands: []' in every earlier run). Save 20 -> the Twin Lords fight.
    # 6 presses: a hit on the presser's Sora can swallow one (the Lords go for the host's).
    user, other = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    tried = {}

    def other_tries():  # --other-tries: the other player picks a Limit 2 s into this one (DECISIONS L2)
        import threading

        def run():
            end = time.time() + 180  # the save load and warp come first
            while time.time() < end and not find(user.since(marks0[user.name]), "our Limit started"):
                time.sleep(0.2)
            time.sleep(2)
            tried["mark"] = other.mark()
            tried["user_started"] = bool(find(user.since(marks0[user.name]), "our Limit started"))
            try_limit()

        marks0 = {c.name: c.mark() for c in copies}
        threading.Thread(target=run, daemon=True).start()

    def try_limit():
        other.game.pad.run(PAGE2)
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try.png"))
        other.game.pad.run(["tap", "cross", "wait", "600"])
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try2.png"))
        other.game.pad.run(["tap", "cross", "wait", "600"])
        bench.screenshot(other.hwnd, os.path.join(OUT, f"limit_{other.name}_try3.png"))

    if a.other_tries:
        other_tries()
    ok = special(copies, a, out, "limit", [], ["tap", "down", "wait", "300"] * a.pick,
                 warp=LIMIT_FIGHTS.get(a.slot), steps=6, during=None,
                 until=lambda: find(user.since(marks[user.name]), "our Limit started"))
    if "mark" in tried:
        ol = other.since(tried["mark"])
        second = find(ol, "type 0x51)")
        refused = find(ol, "refused: the other player's Limit runs")
        out.append(f"{other.name}: tried a Limit during the other's (its start seen {tried['user_started']}): Limit "
                   f"commands {len(second)} (0 expected), refused {len(refused)}, screenshots "
                   f"limit_{other.name}_try*.png")
        ok = ok and tried["user_started"] and not second
    started = find(user.since(marks[user.name]), "type 0x51)")  # a Limit command
    out.append(f"{user.name}: Limit commands {len(started)}")
    for c in copies:  # limit_sync.cpp: the other game's allies leave and come back with the Limit (DECISIONS L1)
        sync = [l[13:150] for l in c.since(marks[c.name]) if "limit sync:" in l]
        out.append(f"{c.name}: limit sync lines {len(sync)}: {sync[:6]}")
    ok = ok and bool(started)
    if a.by == "B":  # TODO 1.57: the host's companion plays the friend's Limit partner
        lines = other.since(marks[other.name])
        borrowed = find(lines, "plays the friend's Limit partner")
        back = find(lines, "is back with our Sora")
        out.append(f"{other.name}: partner borrowed {len(borrowed)}, back {len(back)} ({(back or ['-'])[0][13:140]})")
        has_partner = find(user.since(marks[user.name]), "plays its partner's part")  # a Limit with no partner: none
        ok = ok and (bool(borrowed) or not has_partner)
        # TODO 1.108: after the friend's Limit the host's companions come back (the bring-back kept Goofy away while
        # limit sync's 'other Limit' flag was still set; two-PC run 2026-10-22)
        time.sleep(4)
        kept = find(other.since(marks[other.name]), "Limit holds it")
        party = party_now(other)
        out.append(f"{other.name}: places kept away for the friend's Limit after it ended {len(kept)}, Donald / Goofy "
                   f"here {party}")
        ok = ok and not kept
    return ok


BATTLE = 0x2A11404  # u32 battle state: 0 calm, 1 a fight, 2 forced (VERIFIED_OFFSETS L132)
RECORDS = 0x9ABDA0  # character records, 0x114 bytes each from Sora (id 1); +0x54 80 abilities, top bit = equipped
LIMIT_ABILITIES = range(0xC6, 0xD4)  # Trinity Limit 0xC6, Donald's 0xC7/0xC8, Goofy's 0xC9/0xCA, the allies' up to 0xD3


def battle_state(c):
    return struct.unpack("<I", peek.read(c.game.h, c.game.base + BATTLE, 4))[0]


def limit_out(copies, a, out):
    """TODO 1.180: a Limit left to run out. Save 59, the Limit abilities of Sora, Donald, Goofy and Auron (id 5)
    equipped in memory, the Underdrome (06/0D, programs 0/0/0: Auron in Donald's place) with 4 Dusks kept alive as
    targets; --by opens Limit, takes the list's --pick-th, then nobody presses. From the user's trap log: what 2/47
    returned against the 1/238 ids, the 2/63 counts, whether the gauge-out path ran. PASS = its end ran, no crash."""
    import re
    load_both(copies, a.slot, a.room)
    out.append(f"Limit abilities equipped (memory only): {[equip_limits(c, (1, 2, 3, 5, 9)) for c in copies]}")
    w = [int(x, 16) for x in a.warp.split(",")] if a.warp else [0x06, 0x0D, 0, 0, 0]  # the Underdrome, 0/0/0
    bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(8)
    bench.debug("A", 19, 0x13E | 4 << 16)  # 4 Dusks in front of the host's Sora; the friend's game copies them
    time.sleep(5)
    for c in copies:
        bench.debug(c.name, 20, 1)  # the battle state, so the Limit command is offered
    time.sleep(2)
    user = copies[1] if a.by == "B" else copies[0]
    out.append(f"{user.name}'s party: " + ", ".join(f"{n} {bool(bench.actor_by_model(user, m))}" for n, m in
               (("Auron", "P_HE000"), ("Beast", "P_BB000"), ("Donald", "P_EX020"), ("Goofy", "P_EX030"))))
    m = user.mark()
    user.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick
                      + ["tap", "cross", "wait", "500"])

    def wait_for(text, secs, keys=None):
        end = time.time() + secs
        while time.time() < end:
            if find(user.since(m), text):
                return True
            if keys:
                user.game.pad.run(keys)
            else:
                time.sleep(0.2)
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        return False

    if not wait_for("our Limit started", 4):
        user.game.pad.run(["tap", "cross", "wait", "500"])
    started = wait_for("our Limit started", 4)
    t0 = time.time()
    # no presses: the gauge runs out; --triangle: its commands and finisher instead (Beast's attack loop)
    ended = started and wait_for("our Limit's end ran", 45, bench.MASH_TRIANGLE if a.triangle else None)
    out.append(f"Limit started {started}, its end ran {ended}" + (f" after {time.time() - t0:.1f} s" if ended else ""))
    time.sleep(3)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"limit_out_{c.name}_after.png"))
    lines = user.since(m)
    starts = [re.search(r"trap_limit_motion_start\(\S+, (\S+), (\d+), \S+\) = (-?\d+) .*pc (\d+)", l) for l in lines]
    starts = [s.groups() for s in starts if s]
    ids = [re.search(r"trap_obj_motion_capture_id\((\S+)\) = (-?\d+)", l) for l in lines]
    ids = [i.groups() for i in ids if i]
    refs = [re.search(r"trap_limit_reference\(\S+\) = (-?\d+)", l) for l in lines]
    out.append(f"partner: {[l[13:110] for l in lines if 'plays its partner' in l][:1]}")
    out.append(f"2/47 (actor, motion, result, pc): {starts[:12]}; distinct results {len({s[2] for s in starts})}")
    out.append(f"1/238 (actor, id): {ids[:12]}")
    out.append(f"2/63 counts in order: {[r.group(1) for r in refs if r]}")
    out.append(f"gauge-out path: {[s for s in starts if s[3] == '1267'] or 'not seen (no Auron 258 at pc 1267)'}")
    return ended and all(c.alive() and not c.crash for c in copies)


def equip_limits(c, chars=(1, 2, 3)):
    """Equips every Limit ability the character records hold, in memory only (save 59 has them all off; the stat
    sheet's ability bits come from these records at the next room load). Returns how many it switched on."""
    n = 0
    for cid in chars:
        at = c.game.base + RECORDS + 0x114 * (cid - 1) + 0x54
        ab = list(struct.unpack("<80H", peek.read(c.game.h, at, 160)))
        for i, v in enumerate(ab):
            if (v & 0x7FFF) in LIMIT_ABILITIES and not v & 0x8000:
                ab[i] = v | 0x8000
                n += 1
        write_mem(c, at, struct.pack("<80H", *ab))
    return n


def gym_limit(copies, a, out):
    """Track 2: a Limit with no enemies. The gym (Betwixt and Between 02/28, programs 0/0/0, Donald + Goofy), the
    forced battle state in both games (debug battle on), the --by player's first Limit from the menu; then battle
    off. Save 59 has every Limit ability unequipped: they are equipped in memory before the warp. Expected: the state 2 while held, the Limit runs and syncs (limit sync lines in both), 0 after off, no crash."""
    marks = {c.name: c.mark() for c in copies}
    user = copies[1] if a.by == "B" else copies[0]
    states = {}

    def prep():
        states["before"] = {c.name: battle_state(c) for c in copies}
        for c in copies:
            bench.debug(c.name, 20, 1)
        time.sleep(1.5)
        states["on"] = {c.name: battle_state(c) for c in copies}

    def loaded():
        out.append(f"Limit abilities equipped (memory only): {[equip_limits(c) for c in copies]}")

    ok = special(copies, a, out, "gym_limit", [], ["tap", "down", "wait", "300"] * a.pick, warp=(0x02, 0x28, 0, 0),
                 steps=6, prep=prep, loaded=loaded)
    states["after Limit"] = {c.name: battle_state(c) for c in copies}
    for c in copies:
        bench.debug(c.name, 20, 0)
    time.sleep(1.5)
    states["off"] = {c.name: battle_state(c) for c in copies}
    time.sleep(3)
    states["off +3 s"] = {c.name: battle_state(c) for c in copies}
    out.append(f"battle state: {states}")
    started = find(user.since(marks[user.name]), "type 0x51)")
    out.append(f"{user.name}: Limit commands {len(started)}")
    for c in copies:
        lines = c.since(marks[c.name])
        out.append(f"{c.name}: battle test lines {[l[13:120] for l in lines if 'battle test' in l or 'debug command: battle' in l]}")
        out.append(f"{c.name}: limit sync lines {[l[13:150] for l in lines if 'limit sync:' in l][:6]}")
    held = all(v == 2 for v in states["on"].values()) and all(v == 2 for v in states["after Limit"].values())
    released = all(v == 0 for v in states["off +3 s"].values())
    return ok and held and released and bool(started) and all(c.alive() and not c.crash for c in copies)


def summon(copies, a, out):
    ok = special(copies, a, out, "summon", ["tap", "up", "wait", "500", "tap", "up", "wait", "500"],
                 ["tap", "down", "wait", "300"] * a.pick)
    host = copies[1] if a.by == "B" else copies[0]
    m = host.mark()
    host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss (second entry)
    time.sleep(15)
    bench.screenshot(host.hwnd, os.path.join(OUT, f"summon_{host.name}_dismissed.png"))
    hl = host.since(m)
    built = [l[13:80] for l in hl if "build member" in l]
    back = {n: bool(bench.actor_by_model(host, n)) for n in ("P_EX020", "P_EX030")}
    out.append(f"dismiss: members rebuilt {built}, Donald/Goofy back {back}, alive {host.alive()}, crash {host.crash or '-'}")
    return ok and all(back.values()) and all(c.alive() and not c.crash for c in copies)


def summon_view(c):
    """(state word, SUMN row index, the two summon slots, the two party slots), slots as 'model:objentry id'
    (leads L1505-L1508: exe+0x2A24EDC, exe+0x9ACDD5, exe+0x2A24EE8, exe+0x2A239B0)."""
    h, b = c.game.h, c.game.base

    def name(p):
        if not p:
            return "-"
        try:
            rec = struct.unpack("<Q", peek.read(h, p + bench.OBJ_ENTRY, 8))[0]
            return f"{bench.model_of(c, p)}:{struct.unpack('<I', peek.read(h, rec, 4))[0]:#x}"
        except OSError:
            return "?"

    def pair(off):
        return tuple(name(p) for p in struct.unpack("<QQ", peek.read(h, b + off, 16)))

    return (struct.unpack("<I", peek.read(h, b + 0x2A24EDC, 4))[0], peek.read(h, b + 0x9ACDD5, 1)[0],
            pair(0x2A24EE8), pair(0x2A239B0))


def summon_probe(copies, a, out):
    """TODO 1.61 research, no pass rule beyond 'nobody crashed': --by summons (--pick in its list); both games'
    summon_view is sampled every 0.25 s and each change listed. Then the other game makes the same object with
    the dev spawn (test command 19; --spawn HEX for another object) and is sampled 8 s; then the summoner
    dismisses. Screenshots summon_probe_<copy>_<phase>.png."""
    load_both(copies, a.slot, a.room)
    p, o = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    marks = {c.name: c.mark() for c in copies}
    start, last = time.time(), {}

    def sample(secs, phase):
        end = time.time() + secs
        while time.time() < end:
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({phase}): {c.crash or ''}")
                v = summon_view(c)
                if last.get(c.name) != v:
                    last[c.name] = v
                    out.append(f"{time.time() - start:6.2f} {phase:8} {c.name}: state {v[0]} row {v[1]} "
                               f"summons {v[2]} party {v[3]}")
            time.sleep(0.25)
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_probe_{c.name}_{phase}.png"))

    sample(1, "before")
    p.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500", "tap", "cross", "wait", "1500"] +
                   ["tap", "down", "wait", "300"] * a.pick + ["tap", "cross", "wait", "300"])
    sample(14, "summon")
    slot0 = last[p.name][2][0]
    obj = a.spawn or (int(slot0.split(":")[1], 16) if ":" in slot0 else 0)
    if obj and a.build:  # summon_sync.cpp's build (KH2COOP_SUMMON_SYNC=1): 8 s, then our removal, before the Dismiss
        bench.debug(o.name, 27, obj)
        out.append(f"{o.name}: test command summon {obj:#x}")
        sample(8, "built")
        out.append(f"{o.name}: actors of that model while built: "
                   f"{[hex(x) for x, n in bench.actors(o) if n == slot0.split(':')[0]]}")
        bench.debug(o.name, 27, 0)
        sample(5, "removed")
    elif obj:
        bench.debug(o.name, 19, obj | 1 << 16)
        out.append(f"{o.name}: dev spawn of object {obj:#x}")
        sample(8, "spawned")
        out.append(f"{o.name}: actors of that object's model: "
                   f"{[hex(x) for x, n in bench.actors(o) if ':' in slot0 and n == slot0.split(':')[0]]}")
    else:
        out.append(f"no summon in {p.name}'s slot ({slot0}): nothing to spawn")
    p.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss, as in summon()
    sample(15, "dismiss")
    for c in copies:
        lines = c.since(marks[c.name])
        out.append(f"{c.name}: log {[l[13:140] for l in lines if 'summon' in l.lower() or 'spawn' in l.lower()][:14]}")
        for l in lines:
            if "summon sync:" in l:
                out.append(f"  {c.name} {l[:230]}")
    back = {c.name: party_now(c) for c in copies}
    out.append(f"after the Dismiss (Donald, Goofy): {back}")
    ok = all(c.alive() and not c.crash for c in copies)
    if a.build:  # PASS: built from our files, freed after our removal, the companions back in both games
        wl = o.since(marks[o.name])
        ok = ok and any("summon sync: built" in l for l in wl) and any("summon sync: our summon" in l and "freed" in l for l in wl)             and not any("THE GAME'S" in l for l in wl) and all(all(v) for v in back.values())
    return ok


def summon_sync(copies, a, out):
    """TODO 1.61 (summon_sync.cpp; KH2COOP_SUMMON_SYNC=1 set here): --by summons (--pick in its list); the other game
    builds the same summon by itself and it follows the real one. While the summon is out, every 0.25 s: the real
    one's place and motion (the summoner's summon slot) against the built one's (found by its model in the other
    game's actor list); the summoner's Sora walks for 1.5 s halfway, so the summon moves. Then the Dismiss.
    PASS = built from our files, median distance under 100, the same motion in 70% of the samples, gone within 6 s of
    the Dismiss, Donald and Goofy back in both games, no crash. Screenshots summon_sync_<copy>_<phase>.png."""
    load_both(copies, a.slot, a.room)
    p, o = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    marks = {c.name: c.mark() for c in copies}
    out.append(f"before: {p.name} {summon_view(p)}, {o.name} {summon_view(o)}")
    p.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500", "tap", "cross", "wait", "1500"] +
                   ["tap", "down", "wait", "300"] * a.pick + ["tap", "cross", "wait", "300"])
    # Through the summoner's entry: the summoner's copy in the other game (its second P_EX100) should stay in the
    # summon call (motion 138), not idle (your ask 2026-11-15); a screenshot 2.5 s in.
    t0, seen, shot, states = time.time(), [], False, []
    while time.time() - t0 < 40:
        state = summon_view(p)[0]
        if not states or states[-1] != state:
            states.append(state)
        soras = [x for x, n in bench.actors(o) if n == "P_EX100"]
        if len(soras) > 1 and state == 1:
            seen.append(bench.motion(o, soras[1]))
        if not shot and time.time() - t0 > 2.5:
            shot = True
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_sync_{c.name}_entry.png"))
        if state == 2 and 1 in states:
            break
        if state == 2 and time.time() - t0 > 12:  # no entry cutscene played (skipped by the game's own setting)
            break
        time.sleep(0.1)
    else:
        raise bench.Failed(f"the summon never came out: states {states}")
    call = sum(1 for m in seen if m == 138)
    out.append(f"the summoner's states {states}; its copy in {o.name} during the entry: motion 138 in {call}/{len(seen)} "
               f"samples, motions seen {sorted(set(seen))}")
    model = summon_view(p)[2][0].split(":")[0]
    out.append(f"{p.name}'s summon: {summon_view(p)[2]}")
    place = lambda c, x: struct.unpack("<3f", peek.read(c.game.h, x + 0x670, 12))
    drawn = lambda c, x: struct.unpack("<3f", peek.read(c.game.h, x + 0x70, 12))  # its world matrix's place
    samples, walked, start = [], False, time.time()
    draws = []  # (real drawn, built drawn, the summoner's Sora)
    dumps = []  # --dump: (the real one's actor bytes, the built one's), to find what moves in one and not the other
    pointed = ([], [])  # --dump: per sample {pointer field offset: 0x800 bytes it points at}, real and built
    while time.time() - start < 14:
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        real = struct.unpack("<Q", peek.read(p.game.h, p.game.base + 0x2A24EE8, 8))[0]
        ours = bench.actor_by_model(o, model)
        if real and ours:
            samples.append((place(p, real), bench.motion(p, real), place(o, ours), bench.motion(o, ours)))
            draws.append((drawn(p, real), drawn(o, ours), place(p, struct.unpack("<Q", peek.read(
                p.game.h, p.game.base + 0x2A171C8, 8))[0])))
            if a.dump:
                dumps.append((peek.read(p.game.h, real, 0xFA8), peek.read(o.game.h, ours, 0xFA8)))
                # the blocks the actor points at (its model instance +0x7B0, model +0xA88, entity link +0x8, ...):
                # 0x800 bytes of each pointer field that looks like a heap pointer, by the field's offset
                for k, (c, x) in enumerate(((p, real), (o, ours))):
                    blocks = {}
                    for off in (0x7B0, 0xA88, 0x7E0, 0x148, 0x150, 0x808, 0x888, 0xDC8, 0xDF8, 0xE00, 0xE08, 0xE10):
                        ptr = struct.unpack("<Q", peek.read(c.game.h, x + off, 8))[0]
                        if 0x10000 < ptr < 0x7FFFFFFFFFFF:
                            try:
                                blocks[off] = peek.read(c.game.h, ptr, 0x800)
                            except OSError:
                                pass
                    pointed[k].append(blocks)
        if not walked and time.time() - start > 6:
            walked = True
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_sync_{c.name}_out.png"))
            p.game.pad.run(["stick", "1", "0", "1500", "wait", "200"])
        time.sleep(0.25)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_sync_{c.name}_moved.png"))
        out.append(f"{c.name} while it is out: view {summon_view(c)}, actors {sorted({n for _, n in bench.actors(c)})}")
    dist = sorted(math.dist(s[0], s[2]) for s in samples)
    same = sum(1 for s in samples if s[1] == s[3])
    motions = sorted({(s[1], s[3]) for s in samples})
    out.append(f"{len(samples)} samples with both summons: distance median {dist[len(dist) // 2]:.0f}, max {dist[-1]:.0f}; "
               f"same motion {same}/{len(samples)}; (real, built) motions seen {motions[:12]}" if samples
               else f"no sample with both summons: {o.name} never had a {model}")
    if draws:  # where each is drawn (the matrix) against where its entity is: a script or an attachment can part them
        span = lambda pts: tuple(round(max(q[k] for q in pts) - min(q[k] for q in pts)) for k in range(3))
        off = sorted(math.dist(s[0], d[0]) for s, d in zip(samples, draws))
        apart = sorted(math.dist(d[0], d[1]) for d in draws)
        out.append(f"drawn places: the real one moved over {span([d[0] for d in draws])} (its entity over "
                   f"{span([s[0] for s in samples])}), up to {off[-1]:.0f} from its entity; the built one over "
                   f"{span([d[1] for d in draws])}; real to built drawn: median {apart[len(apart) // 2]:.0f}, max "
                   f"{apart[-1]:.0f}; a few real drawn places {[tuple(round(v) for v in d[0]) for d in draws[::8]][:6]}, "
                   f"the summoner's Sora at {tuple(round(v) for v in draws[-1][2])}")
    if dumps:  # float fields that take many values in the real one and few in the built one
        def only_real(real, built, size):
            rows = []
            for off in range(0, size - 3, 4):
                r = [struct.unpack_from("<I", d, off)[0] for d in real]
                b = [struct.unpack_from("<I", d, off)[0] for d in built]
                if len(set(b)) == 1 and len(set(r)) >= 2:
                    order = [v for i, v in enumerate(r) if i == 0 or v != r[i - 1]]
                    rows.append(f"+{off:#x} real {' '.join(f'{v:x}' for v in order[:10])} ({len(set(r))} values), built {b[0]:x}")
            return rows
        rows = only_real([d[0] for d in dumps], [d[1] for d in dumps], 0xFA8)
        out.append(f"actor fields changing in the real one only ({len(rows)}): " + "; ".join(rows[:70]))
        for field in sorted(set(pointed[0][0]) & set(pointed[1][0])):  # the same search in each pointed block
            rows = only_real([s[field] for s in pointed[0] if field in s], [s[field] for s in pointed[1] if field in s], 0x800)
            out.append(f"block at actor+{field:#x}: changing in the real one only ({len(rows)}): " + "; ".join(rows[:50]))
    moved = True
    if a.leave:  # the summoner (the host only: the friend follows it) changes room with the summon out
        mark = o.mark()
        bench.warp(0x04, 0x09, door=2)
        bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 90, copies)
        t1, again = time.time(), None
        while time.time() - t1 < 25 and again is None:
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} after the room change: {c.crash or ''}")
            if summon_view(p)[0] == 2 and bench.actor_by_model(o, model):
                again = time.time() - t1
            time.sleep(0.25)
        time.sleep(3)
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_sync_{c.name}_newroom.png"))
        lines = [l[13:150] for l in o.since(mark) if "summon sync:" in l and ("forgotten" in l or "leaves" in l or "built" in l
                                                                             or "freed" in l or "not built" in l)]
        out.append(f"room change: the summoner's view in 04/09 {summon_view(p)}; {o.name} has a {model} again after "
                   f"{again if again is None else round(again, 1)} s; {o.name}'s lines {lines[:8]}")
        moved = again is not None
    p.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss, as in summon()
    t0, gone = time.time(), None
    while time.time() - t0 < 10:
        if gone is None and not bench.actor_by_model(o, model):
            gone = time.time() - t0
        time.sleep(0.25)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"summon_sync_{c.name}_after.png"))
    back = {c.name: party_now(c) for c in copies}
    out.append(f"after the Dismiss: {o.name}'s {model} gone after {gone if gone is None else round(gone, 1)} s; "
               f"(Donald, Goofy) {back}; views {p.name} {summon_view(p)}, {o.name} {summon_view(o)}")
    wl = o.since(marks[o.name])
    for l in wl:
        if "summon sync:" in l:
            out.append(f"  {o.name} {l[:230]}")
    built = any("summon sync: built" in l for l in wl) and not any("THE GAME'S" in l for l in wl)
    ok = bool(samples) and built and dist[len(dist) // 2] < 100 and same >= 0.7 * len(samples) and gone is not None \
        and gone <= 6 and all(all(v) for v in back.values()) and moved
    return ok and all(c.alive() and not c.crash for c in copies)


def party_now(c):
    """Which of Donald (P_EX020) and Goofy (P_EX030) are in this game's actor list."""
    return tuple(bool(bench.actor_by_model(c, m)) for m in ("P_EX020", "P_EX030"))


def companions(copies, a, out):
    """TODO 1.98 / DECISIONS C1 (companion_sync.cpp): --by A/B uses a Drive Form (--what form, --pick: 0 Valor, 1
    Wisdom, ... as listed) or a summon (--what summon, --pick in its list) in a room without enemies (save 52, HB
    Borough); then Reverts / dismisses. Who of Donald and Goofy is in each game is sampled every 0.25 s, before,
    during and after. PASS = during it the same companions are gone in both games (at least one), after it both are
    back in both games, no crash. Timelines and screenshots companions_<copy>_<phase>.png."""
    load_both(copies, a.slot, a.room)
    p = copies[1] if a.by == "B" else copies[0]
    marks = {c.name: c.mark() for c in copies}
    out.append(f"before: (Donald, Goofy) present " + ", ".join(f"{c.name} {party_now(c)}" for c in copies))
    timeline = {c.name: [] for c in copies}
    start = time.time()

    def sample(secs):
        end = time.time() + secs
        while time.time() < end:
            for c in copies:
                now = party_now(c)
                if not timeline[c.name] or timeline[c.name][-1][1] != now:
                    timeline[c.name].append((round(time.time() - start, 2), now))
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            time.sleep(0.25)

    o = copies[0] if p is copies[1] else copies[1]
    if a.what == "form":
        p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                       ["tap", "cross", "wait", "500"])
        sample(10)
        if a.overlap:  # DECISIONS C2: the other player's Drive list without the form whose companion is away
            om = o.mark()
            o.game.pad.run(DRIVE + ["tap", "cross", "wait", "1200"])
            bench.screenshot(o.hwnd, os.path.join(OUT, f"companions_{o.name}_drive_list.png"))
            o.game.pad.run(["tap", "cross", "wait", "500"])  # the first form offered
            sample(10)
            out.append(f"overlap: {o.name}'s form: {[l[13:150] for l in o.since(om) if 'our Drive Form sends' in l]}")
            p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # first one reverts
            sample(15)
            out.append(f"overlap: after {p.name}'s Revert (Donald, Goofy): " +
                       ", ".join(f"{c.name} {party_now(c)}" for c in copies))
            p = o  # the other one reverts below
    else:
        p.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            sample(2.5)
            if step == 1:
                p.game.pad.run(["tap", "down", "wait", "300"] * a.pick)
            p.game.pad.run(["tap", "cross", "wait", "300"])
        sample(10)
    during = {c.name: party_now(c) for c in copies}
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"companions_{c.name}_during.png"))
    if a.what == "form":
        p.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
    else:
        p.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])  # Dismiss
    sample(15)
    after = {c.name: party_now(c) for c in copies}
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"companions_{c.name}_after.png"))
    for c in copies:
        out.append(f"{c.name} timeline (s, (Donald, Goofy)): {timeline[c.name]}")
        lines = [l[13:170] for l in c.since(marks[c.name]) if "companion sync:" in l]
        out.append(f"{c.name} companion sync lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:25])
    out.append(f"during: {during}; after: {after}")
    same = during["A"] == during["B"] and not all(during["A"])
    back = all(all(v) for v in after.values())
    return same and back and all(c.alive() and not c.crash for c in copies)


def companion_now(c, model):
    """(HP, max HP, MP, KO'd, motion) of this game's companion with that model, or None."""
    a = bench.actor_by_model(c, model)
    if not a:
        return None
    try:
        st = read_u(c, a + 0x5C0, 8)
        hp, mx = struct.unpack("<ii", peek.read(c.game.h, st, 8))
        mp = struct.unpack("<i", peek.read(c.game.h, st + 0x180, 4))[0]
        return hp, mx, mp, bool(read_u(c, a + 0x9B8) & 4), read_u(c, a + 0x180)
    except (OSError, struct.error):
        return None


def companion_ko(copies, a, out):
    """TODO 1.98 part 2 / 1.43 (companion_sync.cpp status): in a room without enemies (save 52) the host's Goofy is
    KO'd (a 9999 debug hit), the friend's copy must go down too (its KO bit) and get up when the host's does; the host's
    Donald's MP (written) must show on the friend's Donald; the friend's Cure on a hurt Goofy must heal the host's Goofy
    (each spell slot cast in turn; only Cure heals). The friend's mirrored companions must not think (heal trace).
    PASS = KO and get-up in both within 1 s of each other, MP equal, the host's Goofy healed by the friend, no crash."""
    load_both(copies, a.slot, a.room)
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    out.append("before (HP, max, MP, KO, motion): " + ", ".join(
        f"{c.name} Donald {companion_now(c, 'P_EX020')} Goofy {companion_now(c, 'P_EX030')}" for c in copies))
    slot = 1 + [read_u(host, host.game.base + 0x2A239B0 + 8 * i, 8) for i in range(2)].index(
        bench.actor_by_model(host, "P_EX030"))
    timeline = {c.name: [] for c in copies}
    start = time.time()

    def sample(secs, model="P_EX030"):
        end = time.time() + secs
        while time.time() < end:
            for c in copies:
                now = companion_now(c, model)
                key = now and (now[3], now[0] > 0, now[4])
                if not timeline[c.name] or timeline[c.name][-1][1] != key:
                    timeline[c.name].append((round(time.time() - start, 2), key, now and now[0]))
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            time.sleep(0.1)

    sample(1)
    bench.debug("A", slot, 9999)  # the host's Goofy: KO
    sample(20)
    for c in copies:
        out.append(f"{c.name} Goofy timeline (s, (KO, HP > 0, motion), HP): {timeline[c.name]}")

    def first(name, want):
        return next((t for t, k, _ in timeline[name] if k and k[0] == want), None)
    down = {c.name: first(c.name, True) for c in copies}
    up = {c.name: next((t for t, k, _ in timeline[c.name] if k and not k[0] and down[c.name] and t > down[c.name]), None)
          for c in copies}
    out.append(f"KO at {down}, up at {up}")
    full = [(t, hp) for t, k, hp in timeline["B"] if k and k[0] and hp]
    out.append(f"friend's KO'd Goofy with HP above 0 (s, HP): {full} (none = its HUD shows the KO)")
    kos = len(find(friend.since(marks["B"]), "is KO'd: ours too"))
    out.append(f"KO lines on the friend: {kos} (1 = its copy stayed down)")
    ko_ok = all(down.values()) and all(up.values()) and abs(down["A"] - down["B"]) < 1 and abs(up["A"] - up["B"]) < 1         and kos == 1 and not full

    donald = bench.actor_by_model(host, "P_EX020")
    st = read_u(host, donald + 0x5C0, 8)
    mx = struct.unpack("<i", peek.read(host.game.h, st + 0x184, 4))[0]
    write_mem(host, st + 0x180, struct.pack("<i", mx // 3))
    time.sleep(1.5)
    mp = {c.name: companion_now(c, "P_EX020") for c in copies}
    out.append(f"host's Donald MP written {mx // 3}/{mx}: Donald now {mp}")
    mp_ok = all(v and abs(v[2] - mx // 3) <= 2 for v in mp.values())  # Donald recharges out of a fight
    write_mem(host, st + 0x180, struct.pack("<i", mx))

    bench.debug("A", slot, 10)  # the host's Goofy hurt
    time.sleep(1.5)
    hurt = companion_now(host, "P_EX030")
    out.append(f"Goofy after a 10 hit: host {hurt}, friend {companion_now(friend, 'P_EX030')}")
    data = [read_u(friend, friend.game.base + 0x2A23A80 + 0x50 * i + 0x48, 8) for i in range(6)]
    spells = [(i, read_u(friend, d + 0x26, 2)) for i, d in enumerate(data) if d]
    out.append(f"friend's spell slots (slot, id): {[(i, hex(x)) for i, x in spells]}")
    healed = None
    for i, _ in spells:
        fm = friend.mark()
        bench.debug("B", 14, i)  # the friend's Sora casts that slot
        time.sleep(4)
        if find(friend.since(fm), "sent to the host's game"):
            healed = i
            break
    after = companion_now(host, "P_EX030")
    out.append(f"friend's cast that healed Goofy: slot {healed}; host's Goofy {hurt} -> {after}, friend's "
               f"{companion_now(friend, 'P_EX030')}")
    heal_ok = healed is not None and hurt and after and after[0] > hurt[0]
    revive_ok = True
    if healed is not None:  # the friend's Cure on a KO'd Goofy: the host's gets up before its timer (~4.2 s)
        bench.debug("A", slot, 9999)
        time.sleep(1)
        ko = {c.name: companion_now(c, "P_EX030") for c in copies}
        t0 = time.time()
        bench.debug("B", 14, healed)
        up_at = None
        while time.time() - t0 < 3 and up_at is None:
            now = companion_now(host, "P_EX030")
            if now and not now[3]:
                up_at = round(time.time() - t0, 2)
            time.sleep(0.05)
        time.sleep(0.5)
        out.append(f"KO'd Goofy {ko}, the friend's Cure: host's up after {up_at} s; now host "
                   f"{companion_now(host, 'P_EX030')}, friend {companion_now(friend, 'P_EX030')}")
        revive_ok = up_at is not None and all(v and v[3] for v in ko.values())
        friend_now = companion_now(friend, "P_EX030")
        revive_ok = revive_ok and friend_now and not friend_now[3]
    for c in copies:
        lines = [l[13:170] for l in c.since(marks[c.name]) if "companion sync:" in l and "places" not in l]
        out.append(f"{c.name} companion sync lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:20])
        trace = [l[13:200] for l in c.since(marks[c.name]) if "heal trace:" in l and "in 10 s" in l]
        out.append(f"{c.name} think counts: {trace[-2:]}")
    skipped = any("skipped" in l and "0 thinks (" in l for l in friend.since(marks["B"]) if "in 10 s" in l)
    out.append(f"KO {ko_ok}, MP {mp_ok}, friend's heal {heal_ok}, friend's Cure revives {revive_ok}, friend's "
               f"companions not thinking {skipped}")
    return ko_ok and mp_ok and heal_ok and revive_ok and all(c.alive() and not c.crash for c in copies)


def stat_dump(c, actor, n=0x1D0):
    """The actor's stat block (actor+0x5C0) as i32s, or None."""
    try:
        st = struct.unpack("<Q", peek.read(c.game.h, actor + 0x5C0, 8))[0]
        return st, struct.unpack(f"<{n // 4}i", peek.read(c.game.h, st, n))
    except (OSError, struct.error):
        return None


def mp_probe(copies, a, out):
    """Research (part 2 of companion sync): where MP is in the stat block. The host's Sora casts the first spell
    of its Magic menu; Sora's and Donald's stat blocks are read before and after, and the changed fields listed."""
    load_both(copies, a.slot, a.room)
    host = copies[0]
    sora = head(host)
    donald = bench.actor_by_model(host, "P_EX020")
    before = {"Sora": stat_dump(host, sora), "Donald": stat_dump(host, donald)}
    for who, d in before.items():
        if d:
            out.append(f"{who} stat block exe+0x{d[0] - host.game.base:X}: first 8 {d[1][:8]}, +0x180.. {d[1][0x60:0x68]}")
    host.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
    time.sleep(4)
    bench.screenshot(host.hwnd, os.path.join(OUT, "mp_probe_after.png"))
    for who in ("Sora", "Donald"):
        actor = sora if who == "Sora" else donald
        b, now = before[who], stat_dump(host, actor)
        if b and now:
            diffs = [(f"+0x{4 * i:X}", x, y) for i, (x, y) in enumerate(zip(b[1], now[1])) if x != y]
            out.append(f"{who} changed after the spell: {diffs}")
    return all(c.alive() and not c.crash for c in copies)


def party(copies, a, out):
    host = copies[0]
    ok = special(copies, a, out, "party", ["tap", "up", "wait", "500"])  # Party -> FRIENDS -> first -> first offered
    hl = host.since(0)
    out.append(f"members built: {[l[13:80] for l in hl if 'build member' in l][:6]}")
    m = host.mark()
    bench.warp(*a.room)
    bench.wait("the host's room reloaded", lambda: find(host.since(m), "debug warp: asked"), 30, [host])
    time.sleep(12)
    again = copies_made(host.since(m))
    out.append(f"after a room reload: host copy made {len(again)}, ours {ours(host.since(m))}, "
               f"crash {host.crash or copies[1].crash or '-'}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"party_{c.name}_reload.png"))
    return ok and bool(again) and all(c.alive() and not c.crash for c in copies)


PARTY_ENTRY = 0x9ACDE4  # save+0x3534: u8[4] per world {player, active 1, active 2, bench} as party places (F10)
FRIEND_SLOTS = 0x2A239B0  # u64[2]: the two party members


def party_state(c):
    """(the world's party entry & 0x1F, the models of the two party members in the actor list)."""
    world = c.room()[0]
    entry = tuple(b & 0x1F for b in peek.read(c.game.h, c.game.base + PARTY_ENTRY + 4 * world, 4))
    names = dict(bench.actors(c))
    members = tuple(sorted(names.get(read_u(c, c.game.base + FRIEND_SLOTS + 8 * i, 8), "-") for i in (0, 1)))
    return entry, members


def party_swap(copies, a, out):
    """TODO 1.51 (companion_sync.cpp): save 20 (Agrabah: Donald + Aladdin, Goofy on the bench), warped from its save
    point room 07/02, where the party only stands around and Party doesn't open (you, 2026-11-15), to the street
    07/00. The --by player swaps with the Party command (page 2, Party, the first member, the first offered), then
    the other player does the same. After each swap both games must hold the same party: the same bench in the party
    entry and the same two members in the actor list, changed from before. PASS = both swaps mirrored, no crash."""
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x00)
    time.sleep(4)
    bench.wait("both in 07/00", lambda: all(c.room() == (0x07, 0x00) and c.game.sora() for c in copies), 90, copies)
    time.sleep(10)
    first, second = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    before = [party_state(c) for c in copies]
    out.append(f"before: {dict(zip('AB', before))}")
    ok = before[0][0][3] == before[1][0][3] and before[0][0][3] in (1, 2, 3)
    if not ok:
        out.append("   FAIL: the two games start with different benches, or with nobody benched")
    for n, who in enumerate((first, second)):
        marks = {c.name: c.mark() for c in copies}
        was = party_state(who)
        for attempt in range(2):  # a hit can swallow a press: once more if our own entry didn't change
            who.game.pad.run(PAGE2 + ["tap", "up", "wait", "500"])
            for step in range(4):  # Party -> the list -> the member -> the one offered
                who.game.pad.run(["tap", "cross", "wait", "900"])
                if step == 1:
                    bench.screenshot(who.hwnd, os.path.join(OUT, f"party_swap_{n}_{who.name}_list{attempt}.png"))
            time.sleep(3)
            if party_state(who)[0] != was[0]:
                break
            who.game.pad.run(["tap", "circle", "wait", "500"] * 3)
        end = time.time() + 15  # the other game follows once the swapper's entry changed (its task's end)
        while time.time() < end:
            now = [party_state(c) for c in copies]
            if now[0] == now[1] and now[0] != was and "-" not in now[0][1]:
                break
            time.sleep(0.25)
        time.sleep(3)
        now = [party_state(c) for c in copies]
        same = now[0] == now[1] and now[0] != was and "-" not in now[0][1]
        out.append(f"swap {n + 1} by {who.name}: {dict(zip('AB', now))} -> {'mirrored' if same else 'FAIL'}")
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"party_swap_{n}_{c.name}_after.png"))
            out.extend(f"   {c.name}: {l[13:200]}" for l in c.since(marks[c.name])
                       if "party swap:" in l or "carries out command" in l or "mirroring our companion" in l)
        ok = ok and same
    time.sleep(5)
    return ok and all(c.alive() and not c.crash for c in copies)


def party_menu(copies, a, out):
    """TODO 1.51, the main menu way: as party_swap (save 20, Agrabah 07/00), but the --by player swaps in the main
    menu: start, then the --keys taps (a screenshot after each: party_menu_<n>.png), then the menu closed. PASS = both
    games hold the same party, changed from before, no crash."""
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x00)
    time.sleep(4)
    bench.wait("both in 07/00", lambda: all(c.room() == (0x07, 0x00) and c.game.sora() for c in copies), 90, copies)
    time.sleep(10)
    who = copies[1] if a.by == "B" else copies[0]
    was = party_state(who)
    out.append(f"before: {dict(zip('AB', [party_state(c) for c in copies]))}")
    marks = {c.name: c.mark() for c in copies}
    for n, k in enumerate(["start"] + [k for k in a.keys.split(",") if k]):
        who.game.pad.run(["tap", k, "wait", "1200"])
        bench.screenshot(who.hwnd, os.path.join(OUT, f"party_menu_{n}.png"))
    end = time.time() + 20
    while time.time() < end:
        now = [party_state(c) for c in copies]
        if now[0] == now[1] and now[0] != was and "-" not in now[0][1]:
            break
        time.sleep(0.25)
    time.sleep(3)
    now = [party_state(c) for c in copies]
    same = now[0] == now[1] and now[0] != was and "-" not in now[0][1]
    out.append(f"menu swap by {who.name}: {dict(zip('AB', now))} -> {'mirrored' if same else 'FAIL'}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"party_menu_{c.name}_after.png"))
        out.extend(f"   {c.name}: {l[13:200]}" for l in c.since(marks[c.name])
                   if "party swap:" in l or "build member" in l or "mirroring our companion" in l)
    return same and all(c.alive() and not c.crash for c in copies)


FINAL = (0x12, 0x14, 74)
GRAB = "re_b_ex170-50"


def to_final(copies, a, out):
    load_both(copies, 45, (0x12, 0x12))
    marks = {c.name: c.mark() for c in copies}
    if a.ship:
        bench.warp(0x12, 0x16, 72)
        bench.wait("both in the ship fight", lambda: all(c.room() == (0x12, 0x16) and c.game.sora() for c in copies),
                   90, copies)
        time.sleep(4)
    bench.warp(FINAL[0], FINAL[1], a.program)  # 74 story Final Xemnas, 98 Data Xemnas's second part
    bench.wait("both in Final Xemnas", lambda: all(c.room() == FINAL[:2] and c.game.sora() for c in copies), 90, copies)
    time.sleep(8)  # the room's opening scenes
    for c in copies:
        new = c.since(marks[c.name])
        riku = bool(bench.actor_by_model(c, "P_EH000"))
        out.append(f"{c.name}: copy made {len(copies_made(new))}, ours {ours(new)}, puppet found "
                   f"{bool(find(new, 'puppet: Sora copy found'))}, Riku {riku}")
    return marks


class HpKeeper:
    """--hp: keeps the party's HP full by writing it (stat slots exe+0x2A23598 - k*0x278, k = 0..2: current =
    max) every 0.2 s, so a fight runs on without god mode (GOD=0): the hits land in full, only the HP comes back."""

    def __init__(self, copies):
        import ctypes
        import threading
        self.k32, self.stop_flag, self.handles = ctypes.windll.kernel32, False, []
        self.k32.OpenProcess.restype = ctypes.c_void_p
        for c in copies:
            h = self.k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
            self.handles.append((ctypes.c_void_p(h), c.game.base))
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        import ctypes
        buf, done = ctypes.create_string_buffer(8), ctypes.c_size_t()
        while not self.stop_flag:
            for h, base in self.handles:
                for k in range(3):
                    slot = ctypes.c_void_p(base + 0x2A23598 - k * 0x278)
                    if not self.k32.ReadProcessMemory(h, slot, buf, 8, ctypes.byref(done)):
                        continue
                    cur, mx = struct.unpack("<II", buf.raw)
                    if 0 < cur < mx <= 9999:
                        self.k32.WriteProcessMemory(h, slot, struct.pack("<I", mx), 4, ctypes.byref(done))
            time.sleep(0.2)

    def stop(self):
        self.stop_flag = True
        self.thread.join(2)


class DriveKeeper:
    """Infinite Drive gauge (your ask 2026-10-26, for the summon and Limit benches): keeps our own Sora's Drive gauge
    full in each copy by writing it every 0.2 s (stat slot exe+0x2A23598: +0x1B0 the bar's fill, 100 = full; +0x1B1
    and +0x1B2 the bars and the most, lead L96, which is which unknown: both set to the larger). Logs the first
    values seen and counts the refills, so the run says whether L96's fields are the gauge."""

    def __init__(self, copies):
        import ctypes
        import threading
        self.k32, self.stop_flag, self.handles = ctypes.windll.kernel32, False, []
        self.k32.OpenProcess.restype = ctypes.c_void_p
        self.first, self.refills = {}, {c.name: 0 for c in copies}
        for c in copies:
            h = self.k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
            self.handles.append((c.name, ctypes.c_void_p(h), c.game.base))
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        import ctypes
        buf, done = ctypes.create_string_buffer(3), ctypes.c_size_t()
        while not self.stop_flag:
            for name, h, base in self.handles:
                at = ctypes.c_void_p(base + 0x2A23598 + 0x1B0)
                if not self.k32.ReadProcessMemory(h, at, buf, 3, ctypes.byref(done)):
                    continue
                fill, b1, b2 = buf.raw
                self.first.setdefault(name, (fill, b1, b2))
                most = max(b1, b2)
                if 0 < most <= 9 and (fill < 100 or b1 != most or b2 != most):
                    self.k32.WriteProcessMemory(h, at, bytes([100, most, most]), 3, ctypes.byref(done))
                    self.refills[name] += 1
            time.sleep(0.2)

    def report(self):
        return f"first seen (fill, +0x1B1, +0x1B2) {self.first}, refills {self.refills}"

    def stop(self):
        self.stop_flag = True
        self.thread.join(2)


class MotionWatch:
    """--motions: every motion change (actor+0x180) of each Final Xemnas (B_EX170*) in both games, with the time."""

    def __init__(self, copies):
        import threading
        self.copies, self.stop_flag, self.t0 = copies, False, time.time()
        self.changes = {c.name: [] for c in copies}  # (t, actor, motion)
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        found, last, refresh = {c.name: [] for c in self.copies}, {}, 0
        while not self.stop_flag:
            if time.time() > refresh:
                for c in self.copies:
                    try:
                        found[c.name] = [x for x, n in bench.actors(c) if n.startswith("B_EX170")]
                    except OSError:
                        pass
                refresh = time.time() + 2
            for c in self.copies:
                for x in found[c.name]:
                    m = bench.motion(c, x)
                    if m is not None and last.get((c.name, x)) != m:
                        last[(c.name, x)] = m
                        self.changes[c.name].append((time.time() - self.t0, x, m))
            time.sleep(0.2)

    def stop(self):
        self.stop_flag = True
        self.thread.join(2)


def xemnas(copies, a, out):
    host = copies[0]
    pools = bench.PoolWatch(copies)
    keeper = HpKeeper(copies) if a.hp else None
    motions = MotionWatch(copies) if a.motions else None
    census = bench.FxCensus(copies) if a.fx_census else None
    try:
        return xemnas_run(copies, a, out)
    finally:
        if keeper:
            keeper.stop()
        if motions:
            motions.stop()
            with open(os.path.join(OUT, "xemnas_motions.txt"), "w") as fh:
                for c in copies:
                    fh.writelines(f"{c.name} {t:6.1f} {x:#x} {m}\n" for t, x, m in motions.changes[c.name])
        pools.stop()
        for c in copies:
            out.append(f"{c.name}: effect pool {pools.summary(c.name)}")
        if census:
            census.stop()
            for c in copies:
                out.extend(f"{c.name}: {line}" for line in census.summary(c.name))
            census.write(os.path.join(OUT, "xemnas_fx_census.csv"))
        with open(os.path.join(OUT, "xemnas_pool.csv"), "w") as fh:
            for c in copies:
                fh.writelines(f"{c.name},{t:.1f},{v}\n" for t, v in pools.samples[c.name])


def xemnas_run(copies, a, out):
    host = copies[0]
    marks = to_final(copies, a, out)
    hp_mark = host.mark()
    for c in copies:
        bench.debug(c.name, 13, 200)  # Xemnas's HP
    hit = lambda: any("HP change: other" in l and "team 2" in l for l in host.since(hp_mark))
    start, end = time.time(), time.time() + 60
    while time.time() < end and (not hit() or time.time() - start < 20):  # at least 20 s of attacks
        host.game.pad.run(["tap", "cross", "wait", "250"] * 4)
    out.append(f"a hit on Xemnas: {'yes' if hit() else 'none in 60 s'}")
    grabbed = lambda c: any(GRAB in l and "scene log" in l for l in c.since(marks[c.name]))
    end, shot = time.time() + 150, 0
    while time.time() < end and not all(grabbed(c) or c.crash or not c.alive() for c in copies):
        if grabbed(host) and not grabbed(copies[1]):  # the host's Sora held: the friend's game has no grab scene
            end = min(end, time.time() + 10)
        if a.shots and shot < 12:  # what Xemnas does in the next ~36 s
            for c in copies:
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}_t{shot:02d}.png"))
            shot += 1
            time.sleep(2)
        time.sleep(1)
    on_host = grabbed(host) and not grabbed(copies[1])  # Xemnas went for the host's Sora (aggro.cpp, TODO 1.62)
    if on_host:
        freed = riku_part(copies, out)
    elif any(grabbed(c) for c in copies):
        time.sleep(20)
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_{c.name}.png"))
        out.append(f"{c.name}: grab {grabbed(c)}, alive {c.alive()}, crash {c.crash or '-'}")
    if on_host:
        return freed and all(c.alive() and not c.crash for c in copies)
    return all(grabbed(c) and c.alive() and not c.crash for c in copies)


def actor_pos(c, a):
    return struct.unpack("<3f", peek.read(c.game.h, a + 0x640 + 0x30, 12))


def riku_part(copies, out):
    """The host's Sora held by Xemnas: the host plays Riku (Xemnas's script moves him from the stick) up to the
    Xemnas holding the stand-in Sora (P_EX100_LAST) and presses triangle until Sora is free. Shots of both games."""
    import math
    host = copies[0]
    end = time.time() + 30
    while time.time() < end and not bench.actor_by_model(host, "P_EX100_LAST"):
        time.sleep(0.5)
    if not bench.actor_by_model(host, "P_EX100_LAST"):
        out.append("Riku part: no P_EX100_LAST in the host's game within 30 s")
        return False
    out.append("Riku part: on (the stand-in Sora P_EX100_LAST is in the host's game)")
    for c in copies[1:]:
        out.append(f"{c.name}: P_EX100_LAST {'yes' if bench.actor_by_model(c, 'P_EX100_LAST') else 'no'}")
    start, end, shot, near = time.time(), time.time() + 120, 0, 0
    last_shot = 0.0
    while time.time() < end:
        if any(c.crash or not c.alive() for c in copies):
            break
        try:
            stand_in = bench.actor_by_model(host, "P_EX100_LAST")
            if not stand_in:
                break
            riku = bench.actor_by_model(host, "P_EH000_LAST")
            sx, _, sz = actor_pos(host, stand_in)
            xem = [a for a, n in bench.actors(host) if n.startswith("B_EX170_LAST")]
            holder = min(xem, key=lambda a: math.hypot(actor_pos(host, a)[0] - sx, actor_pos(host, a)[2] - sz))
            tx, _, tz = actor_pos(host, holder)
            rx, _, rz = actor_pos(host, riku)
        except (OSError, ValueError):
            time.sleep(0.3)
            continue
        if time.time() - last_shot > 6:
            last_shot = time.time()
            for c in copies:
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_riku_{c.name}_{shot:02d}.png"))
            shot += 1
        d = math.hypot(tx - rx, tz - rz)
        if d < 250:
            near += 1
            # Data Xemnas holds Sora higher up: Riku jumps first (you, 2026-10-22), Rescue in the air
            host.game.pad.run(["tap", "circle", "wait", "250"] + bench.MASH_TRIANGLE)
            continue
        cx, _, cz = host.game.camera()  # stick relative to the camera, as nav.push but from Riku
        fx, fz = rx - cx, rz - cz
        n = math.hypot(fx, fz) or 1.0
        fx, fz = fx / n, fz / n
        dx, dz = (tx - rx) / d, (tz - rz) / d
        host.game.pad.send(0, lx=dx * fz - dz * fx, ly=dx * fx + dz * fz, ms=150)
        time.sleep(0.15)
    host.game.pad.send()
    freed = not bench.actor_by_model(host, "P_EX100_LAST") if host.alive() else False
    out.append(f"Riku part: {'Sora freed' if freed else 'still held'} after {time.time() - start:.0f} s, "
               f"triangle presses near Xemnas {near}, shots {shot}")
    if freed:
        time.sleep(15)
        for c in copies:
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"xemnas_freed_{c.name}.png"))
    return freed


def boss_hp(c, actor):
    """HP of an actor (u32 at *(actor+0x5C0)), or None."""
    import peek
    try:
        stats = struct.unpack("<Q", peek.read(c.game.h, actor + 0x5C0, 8))[0]
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


# Mickey's Healing Light: mash to charge (D-Charge), then stop: it only goes once the presses stop (you, 2026-11-05:
# mashed with no stop, the gauge stayed full and Mickey kept charging). ~0.9 s a round.
HEALING_LIGHT = bench.MASH_TRIANGLE + ["wait", "400"]
MASH_X_TRIANGLE = ["hold", "cross", "35", "wait", "35", "hold", "triangle", "35", "wait", "35"] * 7


def xend(copies, a, out):
    """TODO 1.20 / 1.30: Final Xemnas to the end. Both Soras mash cross and triangle at once (attacks and RCs; you,
    2026-11-04: rapid taps of both, the desperation move's Reflect is a fast RC);
    every boss's HP is set to --boss-hp at the start. Logs each game's scenes, Xemnas's HP and room side by side
    (xend_timeline.txt) until both games leave the room, one crashes, or --seconds pass."""
    pools = bench.PoolWatch(copies)
    keeper = HpKeeper(copies) if a.hp else None
    try:
        marks = to_final(copies, a, out)
        for c in copies:
            bench.debug(c.name, 13, a.boss_hp)
        t0, end, sample = time.time(), time.time() + a.seconds, 0
        timeline, bosses, left = [], {c.name: [] for c in copies}, None
        shots, next_shot = 0, 0
        while time.time() < end:
            if any(c.crash or not c.alive() for c in copies):
                break
            rooms = {c.name: c.room() for c in copies}
            if all(r != FINAL[:2] for r in rooms.values()):
                left = left or time.time()
                if time.time() - left > 25:
                    break
            if a.shots and shots < 24 and time.time() > next_shot and any(
                    "re_b_ex170-60" in l for l in copies[0].since(marks["A"])):
                for d in copies:  # what follows the scene, every 5 s
                    bench.screenshot(d.hwnd, os.path.join(OUT, f"xend_{d.name}_t{shots:02d}.png"))
                shots, next_shot = shots + 1, time.time() + 5
            # ~1 s of cross and triangle in turn, 70 ms each (as rc_dusk), both games at the same time
            pressing = [c for c in (copies[:1] if a.host_only else copies) if rooms[c.name] == FINAL[:2]]
            if pressing:
                bench.both(lambda c: c.game.pad.run(MASH_X_TRIANGLE), pressing)
            if time.time() > sample:
                sample = time.time() + 2
                row = [f"{time.time() - t0:6.1f}"]
                for c in copies:
                    try:
                        listed = bench.actors(c)
                        bosses[c.name] = [(x, n) for x, n in listed if n.startswith("B_EX170")]
                        if c is copies[0] and any(n == "P_EX100_LAST" for _, n in listed):
                            riku_part(copies, out)  # the host's Sora held: walk Riku to Xemnas, Rescue
                    except OSError:
                        pass
                    hps = ",".join(f"{n[7:] or '-'}={boss_hp(c, x)}" for x, n in bosses[c.name])
                    row.append(f"{c.name} room {rooms[c.name][0]:02X}/{rooms[c.name][1]:02X} {hps}")
                timeline.append(" | ".join(row))
        for c in copies:
            lines = c.since(marks[c.name])
            scenes = [l[:12] + " " + l.split("cutscene '")[1].split("'")[0] for l in lines if "scene log: cutscene" in l]
            out.append(f"{c.name}: scenes {scenes}")
            out.append(f"{c.name}: room {c.room() if c.alive() else '-'}, alive {c.alive()}, crash {c.crash or '-'}, "
                       f"pool {pools.summary(c.name)}")
        with open(os.path.join(OUT, "xend_timeline.txt"), "w") as fh:
            fh.write("\n".join(timeline) + "\n")
        for c in copies:
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"xend_{c.name}.png"))
        return all(c.alive() and not c.crash for c in copies) and left is not None
    finally:
        pools.stop()
        if keeper:
            keeper.stop()


def xlimit(copies, a, out):
    host = copies[0]
    marks = to_final(copies, a, out)
    host.game.pad.run(PAGE2)
    for step in range(4):
        time.sleep(2)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"xlimit_A_{step}.png"))
        host.game.pad.run(["tap", "cross", "wait", "300"])
    time.sleep(15)
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"xlimit_{c.name}_after.png"))
    hl = host.since(marks["A"])
    out.append(f"host: {[l[13:110] for l in hl if 'menu: command' in l or 'carries out command' in l or 'Limit' in l][:10]}")
    for c in copies:
        out.append(f"{c.name}: limit lines {[l[13:100] for l in c.since(marks[c.name]) if 'Limit' in l][:4]}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
    return all(c.alive() and not c.crash for c in copies)


def read_u(c, addr, size=4):
    import peek
    return int.from_bytes(peek.read(c.game.h, addr, size), "little")


def xcontinue(copies, a, out):
    """TODO 1.10: both down in Final Xemnas -> Game Over -> the host picks Continue (the friend follows, game_over.cpp).
    Checks the in-field byte (exe+0x9BA8D0) drops to 0 in both games (= world sync counts a new room instance), the
    copy is made again and the friend's Xemnas is bound to the host's again."""
    import threading
    host, friend = copies
    to_final(copies, a, out)
    marks = {c.name: c.mark() for c in copies}
    menu_up = lambda c: (lambda go: go and read_u(c, go + 0x10) == 6)(read_u(c, c.game.base + 0x2AE8050, 8))

    def both_down():
        # Again each second until the Game Over is there: Second Chance takes the friend's first hit and a
        # companion's item may get one up before the other is down (2026-11-15).
        if not read_u(host, host.game.base + 0x2AE8050, 8):
            bench.debug("A", 3, 9999)  # the friend (a lethal hit on its copy, forwarded)
            bench.debug("A", 0, 9999)  # the host: both down = Game Over
        return menu_up(host)
    bench.wait("the host's Game Over menu", both_down, 60, copies, step=1)
    out.append(f"Game Over menu: host {menu_up(host)}, friend {menu_up(friend)}")
    time.sleep(2)  # the menu fades in
    field = {c.name: [] for c in copies}
    stop = []

    def sample():
        while not stop:
            for c in copies:
                try:
                    v = read_u(c, c.game.base + 0x9BA8D0, 1)
                except OSError:
                    continue
                if not field[c.name] or field[c.name][-1] != v:
                    field[c.name].append(v)
            time.sleep(0.05)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    cont = {c.name: c.mark() for c in copies}
    host.game.pad.run(["tap", "cross", "wait", "300"])  # Continue (the default cursor)
    bench.wait("both back in the field", lambda: all(len(field[c.name]) >= 3 and field[c.name][-1] == 1
                                                         for c in copies), 60, copies)
    time.sleep(12)
    stop.append(1)
    t.join(1)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"xcontinue_{c.name}.png"))
    ok = True
    for c in copies:
        lines = c.since(cont[c.name])
        chose = [l[13:90] for l in lines if "game over:" in l][:3]
        bound = [l[13:100] for l in lines if "bound to ours" in l or "unbound" in l][:3]
        made = len(copies_made(lines))
        out.append(f"{c.name}: in-field values {field[c.name]}, game over lines {chose}, copy made {made}, "
                   f"world lines {bound}, room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        ok &= 0 in field[c.name] and field[c.name][-1] == 1 and made >= 1 and c.alive() and not c.crash
    fl = friend.since(cont["B"])
    ok &= any("bound to ours" in l for l in fl)
    return ok


def go_state(c):
    """(kind, state) of a copy's Game Over object (exe+0x2AE8050: +0, +0x10), None with no Game Over."""
    go = read_u(c, c.game.base + 0x2AE8050, 8)
    return (read_u(c, go), read_u(c, go + 0x10)) if go else None


def menu_rows(c):
    """(row kinds, cursor row) of a copy's open pause-type menu (L1522): *(exe+0xBEF538), *(exe+0xBEF528)."""
    kinds, cur = read_u(c, c.game.base + 0xBEF538, 8), read_u(c, c.game.base + 0xBEF528, 8)
    if not kinds or not cur:
        return [], -1
    row, scroll, count = (struct.unpack("<h", peek.read(c.game.h, cur + o, 2))[0] for o in (0, 0x12, 0x16))
    return list(struct.unpack("<9h", peek.read(c.game.h, kinds, 18)))[:max(0, min(count, 9))], row + scroll


def cup_retry(copies, a, out):
    """TODO 1.213: both down in an Underdrome round -> the Retry / Quit Game Over -> the host picks Retry, then (both
    down again) Quit; the friend's game must pick the same row. See the usage text."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.debug("A", 12, 0x06, 0x09, 4, 0, 189, 189, 189, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 06/09", lambda: all(c.room() == (0x06, 0x09) and c.game.sora() for c in copies), 90, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} to 06/09 programs 189)")
        return False
    ok = True
    for want, name in ((3, "Retry"), (4, "Quit")):
        time.sleep(10)  # the round's start
        marks = {c.name: c.mark() for c in copies}
        if a.pause:  # TODO 1.214: the host's pause, nobody down
            host.game.pad.run(["tap", "start", "wait", "1500"])
            kinds, cur = menu_rows(host)
            out.append(f"{name} (pause): host menu (row kinds, cursor) {(kinds, cur)}, friend {menu_rows(friend)}")
            bench.screenshot(host.hwnd, os.path.join(OUT, f"cup_retry_pause_{name}_A_menu.png"))
            if want not in kinds:
                out.append(f"SETUP FAILURE: the host's pause has no row of kind {want}")
                return False
            host.game.pad.run(["tap", "down", "wait", "300"] * ((kinds.index(want) - cur) % len(kinds)) +
                              ["tap", "cross", "wait", "300"])
            time.sleep(10)
            for c in copies:
                lines = c.since(marks[c.name])
                out.append(f"{name} (pause): {c.name} room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}, lines "
                           f"{[l[13:120] for l in lines if 'game over:' in l or 'the game asks' in l]}")
                bench.screenshot(c.hwnd, os.path.join(OUT, f"cup_retry_pause_{name}_{c.name}_after.png"))
                ok &= c.alive() and not c.crash
            fl = friend.since(marks["B"])
            ok &= host.room() == friend.room() and (want != 3 or host.room() == (0x06, 0x09))
            # Retry: the friend's game ran it too. Quit: it did, or it followed the host's load first.
            ok &= bool(find(fl, f"the host chose {name} in its pause: ours does the same")) or want == 4
            if not ok:
                return False
            continue

        def both_down():
            # Hit again each second until the Game Over is there: Second Chance takes the friend's first hit and a
            # companion's Cure may get the host up before the friend is down (the first run, 2026-11-15).
            if not all(go_state(c) for c in copies):
                # The friend first (a lethal hit on its copy, forwarded), the host only once the friend lies down:
                # hit together, the host fell first and was Cured again and again while the friend stayed up
                # (2026-11-17, three runs). Both down = Game Over.
                state = [l for l in friend.since(marks["B"])
                         if "downed: our Sora took a lethal hit" in l or "downed: getting up" in l]
                bench.debug("A", 0 if state and "took a lethal hit" in state[-1] else 3, 9999)
            return all((go_state(c) or (0, 0))[1] == 6 for c in copies)
        try:
            # 4 hits a second: a companion's Cure got the host up between slower ones (2026-11-17, two runs).
            bench.wait(f"both Game Over menus ({name})", both_down, 60, copies, step=0.25)
        except bench.Failed as e:
            out.append(f"SETUP FAILURE: {e}; Game Over (kind, state): " +
                       ", ".join(f"{c.name} {go_state(c)}" for c in copies))
            return False
        time.sleep(3)  # the menu fades in
        for c in copies:
            out.append(f"{name}: {c.name} Game Over (kind, state) {go_state(c)}, menu (row kinds, cursor) {menu_rows(c)}")
            bench.screenshot(c.hwnd, os.path.join(OUT, f"cup_retry_{name}_{c.name}_menu.png"))
        if any(want not in menu_rows(c)[0] for c in copies):
            out.append(f"SETUP FAILURE: a menu has no row of kind {want}")
            return False

        def press(c, kind):
            kinds, cur = menu_rows(c)
            c.game.pad.run(["tap", "down", "wait", "300"] * ((kinds.index(kind) - cur) % len(kinds)) +
                           ["tap", "cross", "wait", "300"])

        def menus_up():
            return all((go_state(c) or (0, 0))[1] == 6 for c in copies)
        # TODO 1.228: both pick. Retry: the host first, then the friend picks Quit (nothing runs), then Retry.
        # Quit: the friend first, then the host.
        one, two = (host, friend) if want == 3 else (friend, host)
        press(one, want)
        time.sleep(3)
        held = menus_up()
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"cup_retry_{name}_{c.name}_one_pick.png"))
        if want == 3:
            press(two, 4)
            time.sleep(3)
            held = held and menus_up()
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"cup_retry_{name}_{c.name}_split.png"))
        out.append(f"{name}: nothing ran before both picked the same: {'ok' if held else 'WRONG'}")
        ok &= held
        press(two, want)
        try:
            bench.wait("both Game Overs gone", lambda: all(go_state(c) is None for c in copies), 30, copies)
            gone = True
        except bench.Failed:
            gone = False
        time.sleep(8)
        for c in copies:
            lines = c.since(marks[c.name])
            out.append(f"{name}: {c.name} room {c.room()}, Game Over {go_state(c)}, alive {c.alive()}, crash "
                       f"{c.crash or '-'}, lines {[l[13:105] for l in lines if 'game over:' in l]}")
            bench.screenshot(c.hwnd, os.path.join(OUT, f"cup_retry_{name}_{c.name}_after.png"))
            ok &= c.alive() and not c.crash
        ok &= (gone and host.room() == friend.room() and (want != 3 or host.room() == (0x06, 0x09))
               and bool(find(host.since(marks["A"]), f"both players picked {name}"))
               and bool(find(host.since(marks["A"]), f"chose the mission's row kind {want}"))
               and bool(find(friend.since(marks["B"]), f"chose {name} like the host")))
        if not ok:
            return False
    return ok


def pete_curtain(copies, a, out):
    """TODO 1.215: Pete's last fight (0D/03 programs 53). His HP is written down in steps in the host's game so his
    script changes the scenery; the curtain's status bit (exe+0x2A11400 bit 0x100000, L1524) is sampled in both games.
    See the usage text."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.debug("A", 12, 0x0D, 0x03, 0, 0, 53, 53, 53, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 0D/03", lambda: all(c.room() == (0x0D, 0x03) and c.game.sora() for c in copies), 90, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} to 0D/03 programs 53)")
        return False
    time.sleep(12)  # the fight's start
    pete = bench.actor_by_model(host, "N_EX760_BTL")
    stats = read_u(host, pete + 0x5C0, 8) if pete else 0
    if not stats:
        out.append("SETUP FAILURE: no N_EX760_BTL with stats in the host's game")
        return False
    hp, top = struct.unpack("<ii", peek.read(host.game.h, stats, 8))
    out.append(f"Pete in the host's game: HP {hp} of {top}")
    marks = {c.name: c.mark() for c in copies}
    bits = {c.name: [] for c in copies}  # (seconds from the start, bit) at each change
    stop, t0 = [], time.time()

    def shots(n):
        last = 0
        for at in (1.0, 2.0, 2.8, 3.5):
            time.sleep(at - last)
            last = at
            for d in copies:
                bench.screenshot(d.hwnd, os.path.join(OUT, f"pete_curtain_{n}_{at}s_{d.name}.png"))

    def sample():
        while not stop:
            for c in copies:
                try:
                    v = bool(read_u(c, c.game.base + 0x2A11400) & 0x100000)
                except OSError:
                    continue
                if (bits[c.name][-1][1] if bits[c.name] else False) != v:
                    bits[c.name].append((round(time.time() - t0, 2), v))
                    n = len([b for b in bits[c.name] if b[1]])
                    if v and c is host and n <= 2:  # the host's first two closes: what each game shows, four times
                        threading.Thread(target=shots, args=(n,), daemon=True).start()
            time.sleep(0.05)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    for part in (0.85, 0.7, 0.55, 0.4, 0.25):
        write_mem(host, stats, struct.pack("<i", max(1, int(top * part))))
        time.sleep(11)
    stop.append(1)
    t.join(2)
    closes = {n: len([b for b in v if b[1]]) for n, v in bits.items()}
    shots = {}
    for c in copies:
        lines = c.since(marks[c.name])
        cams = sorted({l[:12] for l in lines if "re_n_ex760-20' starts" in l})
        shots[c.name] = len(cams)
        out.append(f"{c.name}: curtain bit changes {bits[c.name]}, camera '20' starts {cams}, room {c.room()}, alive "
                   f"{c.alive()}, crash {c.crash or '-'}, other Sora unprotected "
                   f"{[l[:12] for l in lines if 'unprotected as well' in l]}, waits on a protected player "
                   f"{len([l for l in lines if 'trap_obj_is_star' in l and '= 1 by N_EX760' in l])}")
    ok = all(c.alive() and not c.crash for c in copies)
    # TODO 1.215's exception row (grab_sync.cpp kBothCameras): his camera '20' in both games at every change
    return ok and closes["A"] >= 1 and closes["B"] == closes["A"] and shots["A"] == shots["B"] == closes["A"]


def barrels(copies, a, out):
    """TODO 1.225: Port Royal's burning barrels (10/03 programs 56). See the usage text."""
    host, friend = copies
    doer = host if a.host_only else friend  # --host-only: the host's Sora throws (the game's own way, for comparison)
    load_both(copies, a.slot, a.room)
    bench.debug("A", 12, 0x10, 0x03, 0, 0, 56, 56, 56, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 10/03", lambda: all(c.room() == (0x10, 0x03) and c.game.sora() for c in copies), 90, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} to 10/03 programs 56)")
        return False
    time.sleep(8)  # the mission's start scene
    marks = {c.name: c.mark() for c in copies}
    seconds = 90 if a.seconds == 420 else a.seconds
    end, rounds, thrown = time.time() + seconds, 0, 0
    count = lambda lines, text: sum(text in l for l in lines)
    presses = lambda: count(doer.since(marks[doer.name]), "Sora carries out command 0x111")
    while time.time() < end and all(c.alive() and not c.crash for c in copies) and host.room() == (0x10, 0x03):
        # The HUD's order: its first icon (the first to explode, which fails the mission) is the last in the actor list.
        spots = [actor_pos(doer, b) for b, name in bench.actors(doer) if name == "F_CA050"][::-1]
        spots = [p for p in spots if abs(p[0]) < 1500 and abs(p[2]) < 2500]  # not one put out of sight
        if not spots:
            time.sleep(1)
            continue
        for p in spots:
            if time.time() > end or find(host.since(marks["A"]), "mission complete"):
                break
            bench.debug(doer.name, 15, p[0] + 60.0, p[1], p[2], fmt="<Iifff")  # the friend's Sora beside it
            before, moved = presses(), count(host.since(marks["A"]), "mission sync: counter")
            for _ in range(4):  # a press, then hands off: the throw takes a few seconds and must not be cut short
                doer.game.pad.run(["tap", "triangle", "wait", "150"] * 3)
                if presses() > before:
                    break
            if presses() == before:
                continue  # this barrel offered nothing
            t = time.time()
            while time.time() - t < 9 and count(host.since(marks["A"]), "mission sync: counter") == moved:
                time.sleep(0.3)
            thrown += count(host.since(marks["A"]), "mission sync: counter") > moved
            time.sleep(1.5)
        rounds += 1
        if find(host.since(marks["A"]), "mission complete"):
            break
    time.sleep(6)
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    sent = count(fl, "sent to the host by its serial")
    ran = count(hl, "the friend used command 273")
    refused = count(hl, "the friend's command 273 on")
    own = count(fl, "F_CA050 (exe+") and sum("holds our own Sora in our game: runs" in l and "F_CA050" in l for l in fl)
    parked = count(fl, "F_CA050 has no host match; parked")
    moves = count(hl, "mission sync: counter")
    out.append(f"rounds {rounds}, presses followed by a counter move {thrown}; friend: presses sent by serial {sent}, throws run in its own game {own}, barrels "
               f"parked {parked}; host: friend's presses run {ran}, refused {refused}, counter moves {moves}, mission "
               f"complete {bool(find(hl, 'mission complete'))}")
    for text in ("the friend used command 273", "the friend's command 273 on", "scene camera on the copy"):
        hit = find(hl, text)
        if hit:
            out.append("  host: " + hit[0][13:230])
    for text in ("sent to the host by its serial", "listed for the host's", "F_CA050 bound to ours",
                 "grab sync: ran the host's grab motion", "grab sync: ran the host's camera watch",
                 "not run: no enemy of ours bound"):
        hit = find(fl, text)
        if hit:
            out.append("  friend: " + hit[0][13:230])
    ok = True
    for c in copies:
        out.append(f"{c.name}: room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"barrels_{c.name}.png"))
        ok &= c.alive() and not c.crash
    return ok and thrown > 0 and (a.host_only or (ran > 0 and own == 0))


def barbossa(copies, a, out):
    """TODO 1.217: Barbossa's fight (10/0A programs 60): the friend's Sora kills B_CA000, whose light put the room in the
    dark; the friend's game crashed in its copy's own trap_light_fadeout(0). See the usage text."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.debug("A", 12, 0x10, 0x0A, 0, 0, 60, 60, 60, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 10/0A", lambda: all(c.room() == (0x10, 0x0A) and c.game.sora() for c in copies), 90, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} to 10/0A programs 60)")
        return False
    time.sleep(12)  # the room goes dark about 5 s in
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"barbossa_{c.name}_dark.png"))
    marks = {c.name: c.mark() for c in copies}
    done = lambda: bool(find(friend.since(marks["B"]), "B_CA000 killed") or
                        find(friend.since(marks["B"]), "light call") or not friend.alive())
    end = time.time() + 90
    while time.time() < end and not done():
        beast = bench.actor_by_model(friend, "B_CA000")
        if not beast:
            break
        p = actor_pos(friend, beast)
        bench.debug("B", 15, p[0] + 60.0, p[1], p[2], fmt="<Iifff")  # the friend's Sora beside it
        friend.game.pad.run(["tap", "cross", "wait", "150"] * 6)
    time.sleep(8)
    ok = True
    for c in copies:
        lines = c.since(marks[c.name])
        out.append(f"{c.name}: room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}, B_CA000 lines "
                   f"{[l[13:110] for l in lines if 'B_CA000 killed' in l or 'light call' in l][:4]}")
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"barbossa_{c.name}_after.png"))
        ok &= c.alive() and not c.crash
    return ok and bool(find(friend.since(marks["B"]), "B_CA000 killed"))


def write_mem(c, addr, data):
    """Writes bytes into a copy's memory (code too: the page is made writable for it)."""
    import ctypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = ctypes.c_void_p
    h = k32.OpenProcess(0x0008 | 0x0010 | 0x0020, False, c.game.pid)  # VM_OPERATION | READ | WRITE
    old, done = ctypes.c_uint32(), ctypes.c_size_t()
    k32.VirtualProtectEx(ctypes.c_void_p(h), ctypes.c_void_p(addr), len(data), 0x40, ctypes.byref(old))
    ok = k32.WriteProcessMemory(ctypes.c_void_p(h), ctypes.c_void_p(addr), data, len(data), ctypes.byref(done))
    k32.VirtualProtectEx(ctypes.c_void_p(h), ctypes.c_void_p(addr), len(data), old.value, ctypes.byref(old))
    k32.CloseHandle(ctypes.c_void_p(h))
    if not ok or done.value != len(data):
        raise bench.Failed(f"write to {c.name} exe+0x{addr - c.game.base:X} failed")


MICKEY_ALLOWED, MICKEY_COUNT, GAME_OVER = 0x3A3BD0, 0x9AD660, 0x2AE8050  # VERIFIED_OFFSETS 'Mickey rescue'


MICKEY_SPAWN = 0x4158C0  # VERIFIED_OFFSETS 'Mickey rescue' (Mickey spawn), Game Over state 7
LOW_HP_BEEP = 0xAD8040  # u8: the low-HP beep is on (VERIFIED_OFFSETS lead 'The player's own gauge ... low-HP beep')


def actor_hp(c, a):
    """An actor's current HP (its stats at +0x5C0), or None."""
    try:
        stats = read_u(c, a + 0x5C0, 8)
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


def player(c):
    """The actor the game controls (exe+0x2A105D0): our Sora, or Mickey during his rescue (not the list head then)."""
    return read_u(c, c.game.base + 0x2A105D0, 8)


def head(c):
    """The actor list head: our Sora, Mickey during his rescue (puppet.cpp NotePlayer reads the same)."""
    return read_u(c, c.game.base + 0x2A171C8, 8)


def mickey_trace(copies, stop):
    """--trace: every 50 ms, the fades and Game Over flags in each game; a line per change (mickey_trace_<copy>.txt)."""
    import threading
    fields = [("fadeA.mode", 0xABB290 + 0x90, 8), ("fadeA.white", 0xABB290 + 0x100, 1), ("fadeA.state", 0xABB290 + 0x108, 4),
              ("fadeA.len", 0xABB290 + 0x10C, 4), ("fadeB.mode", 0xABB3B0 + 0x90, 8), ("fadeB.white", 0xABB3B0 + 0x100, 1),
              ("fadeB.state", 0xABB3B0 + 0x108, 4), ("fadeB.len", 0xABB3B0 + 0x10C, 4), ("f500", 0x2A10500, 4),
              ("f504", 0x2A10504, 4), ("f508", 0x2A10508, 4), ("f11400", 0x2A11400, 4), ("pause18", 0xABAD18, 4),
              ("abb878", 0xABB878, 4), ("ad93c0", 0xAD93C0, 1), ("menu", 0x7435D0, 4), ("player", 0x2A105D0, 8),
              ("gameover", GAME_OVER, 8)]

    def run(c):
        last, lines, t0 = {}, [], time.time()
        while not stop:
            try:
                vals = {n: read_u(c, c.game.base + a, z) for n, a, z in fields}
                g = vals["gameover"]
                vals["go.state"] = read_u(c, g + 0x10) if g else -1
            except OSError:
                break
            ch = [f"{n}={v:#x}" for n, v in vals.items() if last.get(n) != v]
            if ch:
                lines.append(f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d} +{time.time() - t0:6.2f} "
                             + " ".join(ch))
                last = vals
            time.sleep(0.05)
        with open(os.path.join(OUT, f"mickey_trace_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
    ts = [threading.Thread(target=run, args=(c,), daemon=True) for c in copies]
    for t in ts:
        t.start()
    return ts


def mickey(copies, a, out):
    """TODO 1.3 (god mode off): Mickey forced in the host's game only (MickeyAllowed exe+0x3A3BD0 -> mov al,1; ret and
    the appearance count 0 = a 100% roll, as in the 2026-10-06 single-copy test), in a room with no enemies. The friend
    goes down, then the host: both down = Game Over; the host picks 'I won't give up', Mickey comes, the bench presses
    triangle (Healing Light) until the host's Sora gets up. Logs each step in both games; PASS = the host's Sora is back
    with HP and control, no crash in either game (the friend's side is recorded, not judged: it stays at its Game Over)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    # off the save point the save loads us on (it refills HP: the first run's friend was back at 60 before each hit)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)
    for c in copies:
        out.append(f"{c.name}: room {c.room()}, enemies {len(c.game.enemies())}, Sora HP {sora_hp(c)}, "
                   f"{c.game.where()}")
    count = read_u(host, host.game.base + MICKEY_COUNT, 2)
    write_mem(host, host.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
    write_mem(host, host.game.base + MICKEY_COUNT, b"\x00\x00")
    out.append(f"host: Mickey allowed forced, appearance count {count} -> 0 (in memory only)")
    if a.friend_back:  # Q6 probe: the friend's game takes the Mickey branch too (it keeps the field), with no Mickey
        fcount = read_u(friend, friend.game.base + MICKEY_COUNT, 2)
        write_mem(friend, friend.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(friend, friend.game.base + MICKEY_COUNT, b"\x00\x00")
        write_mem(friend, friend.game.base + MICKEY_SPAWN, b"\xC3")
        out.append(f"friend: Mickey allowed forced, appearance count {fcount} -> 0, Mickey's spawn exe+0x{MICKEY_SPAWN:X} "
                   f"-> ret (in memory only)")
    sora, fsora = head(host), head(friend)  # each game's own Sora, before Mickey
    stop = []
    tracers = mickey_trace(copies, stop) if a.trace else []
    marks = {c.name: c.mark() for c in copies}
    for n in range(3):  # the friend down (as in 'downed': its own rules may keep it at 1 HP once)
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    bench.debug("A", 0, 9999)  # the host too: both down
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("the host's Game Over", lambda: go(host), 40, copies)
    time.sleep(3)
    for c in copies:
        g = go(c)
        out.append(f"{c.name}: Game Over object {'yes' if g else 'no'}, Mickey chosen "
                   f"{read_u(c, g + 0x14, 1) if g else '-'}, state {read_u(c, g + 0x10) if g else '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_1_gameover.png"))
    if not go(host) or read_u(host, go(host) + 0x14, 1) != 1:
        out.append("host: no Mickey on the Game Over (the roll or a companion check said no)")
        return False
    time.sleep(4)  # the white screen fades in
    host.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up' (the first choice)
    bench.wait("Mickey is the player", lambda: head(host) not in (0, sora), 40, copies)
    time.sleep(3)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_2_mickey.png"))
    out.append(f"host: Mickey is the player; host's Sora motion {bench.motion(host, sora)} (252 = lying)")
    if a.friend_back:
        fstate = lambda: read_u(friend, go(friend) + 0x10) if go(friend) else -1
        bench.wait("the friend's white screen", lambda: fstate() == 6 or not go(friend), 30, copies)
        out.append(f"friend: Game Over state {fstate()}, Mickey chosen {read_u(friend, go(friend) + 0x14, 1) if go(friend) else '-'}")
        time.sleep(1)
        friend.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up' in the friend's game, no Mickey there
        t0 = time.time()
        bench.wait("the friend's Game Over to end", lambda: not go(friend), 20, copies)
        out.append(f"friend: Game Over ended {time.time() - t0:.1f} s after its 'I won't give up'")
        fseen = []
        for _ in range(16):  # 8 s of the friend's own Sora
            m = (bench.motion(friend, fsora), actor_hp(friend, fsora))
            if not fseen or fseen[-1] != m:
                fseen.append(m)
            time.sleep(0.5)
        bench.screenshot(friend.hwnd, os.path.join(OUT, "mickey_B_2b_back.png"))
        out.append(f"friend back: player {bench.model_of(friend, head(friend))} (own Sora {head(friend) == fsora}), "
                   f"own Sora (motion, HP) {fseen}, Donald {bool(bench.actor_by_model(friend, 'P_EX020'))}, "
                   f"Goofy {bool(bench.actor_by_model(friend, 'P_EX030'))}, copy {bench.model_of(friend, head(friend))}, "
                   f"actors {[n for _, n in bench.actors(friend)][:12]}")
    mickey_actor = head(host)
    out.append(f"host: the player is now {bench.model_of(host, mickey_actor)} (exe+0x{mickey_actor - host.game.base:X}), "
               f"was exe+0x{sora - host.game.base:X}; the player pointer has {bench.model_of(host, player(host))}")
    seen, presses, start = {"sora": [], "mickey": []}, 0, time.time()
    up = lambda: bench.motion(host, sora) == 253 or head(host) == sora
    while time.time() - start < 150 and not up():
        host.game.pad.run(HEALING_LIGHT)
        presses += 1
        for key, act in (("sora", sora), ("mickey", mickey_actor)):
            m = bench.motion(host, act)
            if not seen[key] or seen[key][-1] != m:
                seen[key].append(m)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} during Mickey: {c.crash or ''}")
        if presses == 5:  # ~4.5 s
            bench.screenshot(host.hwnd, os.path.join(OUT, "mickey_A_3_healing.png"))
    out.append(f"host: {presses} triangle mashes in {time.time() - start:.0f} s, Sora up {up()}; "
               f"motions Sora {seen['sora']}, Mickey {seen['mickey']}")
    time.sleep(10)  # the get-up (253, ~5 s), then control back
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_4_after.png"))
    host.game.pad.run(["stick", "0", "1", "1500", "wait", "300", "tap", "cross", "wait", "500"])  # walk, attack
    time.sleep(3)
    ok = head(host) == sora and (sora_hp(host) or 0) > 0
    keys = ("game over", "downed:", "switched actors", "build member", "not built", "extra copy:", "grab sync",
            "exception", "CRASH")
    for c in copies:
        lines = c.since(marks[c.name])
        g = go(c)
        out.append(f"{c.name}: player {bench.model_of(c, head(c))} (pointer {bench.model_of(c, player(c))}), "
                   f"Sora HP {sora_hp(c)}, Game Over object {'yes, state %d' % read_u(c, g + 0x10) if g else 'no'}, "
                   f"room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        with open(os.path.join(OUT, f"mickey_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in lines if any(k in l for k in keys)) + "\n")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_{c.name}_5_end.png"))
        ok &= c.alive() and not c.crash
    out.append(f"host: walked and attacked; HP {sora_hp(host)}")
    if a.friend_back:
        out.append(f"friend at the end: own Sora motion {bench.motion(friend, fsora)}, HP {actor_hp(friend, fsora)}, "
                   f"Game Over {'yes' if go(friend) else 'no'}")
        ok &= not go(friend)
    stop.append(1)
    for t in tracers:
        t.join(2)
    return ok


def jump_test(c, actor):
    """Your idea 2026-10-18 (the Mickey loser floated after a knock-up): tap jump, sample the height (actor+0x670 y)
    every 0.1 s for 3 s. Returns (start, highest change, end change)."""
    y = lambda: struct.unpack("<f", peek.read(c.game.h, actor + 0x674, 4))[0]
    y0, ys = y(), []
    c.game.pad.run(["tap", "circle", "wait", "100"])
    end = time.time() + 3
    while time.time() < end:
        ys.append(y())
        time.sleep(0.1)
    far = max(ys, key=lambda v: abs(v - y0))
    return round(y0, 1), round(far - y0, 1), round(ys[-1] - y0, 1)


DUMP_ACTOR, DUMP_MOVE = 0x1000, 0xD00  # bytes of the actor and of its movement object (actor+0xDC0)


def dump_sora(c, actor):
    """--dump: the actor's first DUMP_ACTOR bytes and its movement object's (actor+0xDC0) first DUMP_MOVE bytes."""
    move = read_u(c, actor + 0xDC0, 8)
    return peek.read(c.game.h, actor, DUMP_ACTOR), peek.read(c.game.h, move, DUMP_MOVE) if move else b""


def dump_diff(before, after, ref_before, ref_after, out, label):
    """u32 fields that changed on the loser (before -> after) but not on the winner (its own before -> after)."""
    rows = []
    for part, (b, a, rb, ra) in (("actor", (before[0], after[0], ref_before[0], ref_after[0])),
                                 ("move", (before[1], after[1], ref_before[1], ref_after[1]))):
        for off in range(0, min(len(b), len(a), len(rb), len(ra)) - 3, 4):
            x, y = b[off:off + 4], a[off:off + 4]
            if x != y and rb[off:off + 4] == ra[off:off + 4]:
                rows.append(f"{part}+0x{off:X}: {x[::-1].hex()} -> {y[::-1].hex()} (winner {rb[off:off + 4][::-1].hex()})")
    with open(os.path.join(OUT, f"mcoop_dump_{label}.txt"), "w", encoding="utf-8") as fh:
        fh.write("\n".join(rows) + "\n")
    out.append(f"dump {label}: {len(rows)} u32 fields changed only on the loser (mcoop_dump_{label}.txt)")


def mcoop_lines(copies, marks, stop, tracers):
    """Stops the --trace samplers; each game's Mickey / downed / Game Over lines in mcoop_<copy>.txt."""
    stop.append(1)
    for t in tracers:
        t.join(2)
    keys = ("mickey", "downed:", "game over", "build member", "switched actors", "exception", "CRASH")
    for c in copies:
        lines = c.since(marks[c.name])
        with open(os.path.join(OUT, f"mcoop_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in lines if any(k in l for k in keys)) + "\n")


def mcoop_end(copies, out, marks, stop, tracers, go):
    """mickey_coop --end: both games should reach the normal Continue / Load menu (state 6, Mickey branch 0 in our
    notes; logged as it goes); the host presses Continue; PASS = both games back in the field, a Sora with HP."""
    host, friend = copies
    seq, end = {c.name: [] for c in copies}, time.time() + 10
    while time.time() < end:
        for c in copies:
            g = go(c)
            s = (read_u(c, g + 0x10), read_u(c, g + 0x14, 1)) if g else None
            if not seq[c.name] or seq[c.name][-1] != s:
                seq[c.name].append(s)
        time.sleep(0.25)
    for c in copies:
        out.append(f"{c.name}: Game Over (state, Mickey branch) over 10 s: {seq[c.name]}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_5_menu.png"))
    host.game.pad.run(["tap", "cross", "wait", "300"])  # Continue
    back = True
    try:
        bench.wait("both games back in the field", lambda: not go(host) and not go(friend), 60, copies)
    except bench.Failed as e:
        back = False
        out.append(f"not back: {e}")
    time.sleep(8)
    ok = back
    for c in copies:
        h = head(c)
        hp = actor_hp(c, h) if h else None
        g = go(c)
        out.append(f"{c.name}: after Continue: Game Over {'state %d' % read_u(c, g + 0x10) if g else 'no'}, HP {hp}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_6_continue.png"))
        ok &= c.alive() and not c.crash and not g and (hp or 0) > 0
    mcoop_lines(copies, marks, stop, tracers)
    return ok


CAMERA_TARGET = 0x718CB0  # actor the camera follows (mickey_sync.cpp: the loser's own Sora during the rescue)
HUD_PLAYER = 0xABCFE8  # actor the HUD's player gauge shows (same)
CMD_MENU = 0x2A10620  # YS::FIELD_COMMAND: page stack top at +0x9A8; pages of 0x80 from +0x28 (count +0x10,
# 12-byte entries at +0x14, command at +2); an actor's own command set (Mickey's) is page +0x728


def menu_cmds(c):
    """The page the command menu shows (offset in the menu object) and its commands (mickey_sync.cpp: the loser's
    should be its Sora's top page, not Mickey's own set at +0x728)."""
    fc = c.game.base + CMD_MENU
    top = read_u(c, fc + 0x9A8, 8)
    page = read_u(c, top, 8) if top else 0
    if not page:
        return 0, []
    n = read_u(c, page + 0x10)
    return page - fc, [read_u(c, page + 0x14 + i * 0xC + 2, 2) for i in range(min(n, 8))]


def mickey_coop(copies, a, out):
    """TODO 1.3 / Q6 (mickey_sync.cpp, god mode off): Mickey allowed in this room in both games (MickeyAllowed -> 1,
    appearance count 0, in memory only), the roll's winner set with KH2COOP_TEST_MICKEY_WINNER (--winner A/B). The
    friend down, then the host: both games at Game Over; only the winner's has choices; the winner picks 'I won't give
    up'; the loser's game goes back to the field (white fades in), its Sora down; the winner presses triangle until its
    Sora is up; the loser gets up with full HP and its allies back; a 10 HP hit then hurts both Soras. Each step logged;
    PASS = all of that, no crash. --end over / dies / nobody: the winner picks 'It's all over' / Mickey takes a lethal
    hit / nobody wins the roll; then both games should be at the normal Game Over and follow the host's Continue."""
    host, friend = copies
    win, lose = (host, friend) if a.winner == "A" else (friend, host)
    load_both(copies, a.slot, a.room)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)  # off the save point
    for c in copies:
        write_mem(c, c.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(c, c.game.base + MICKEY_COUNT, b"\x00\x00")
    out.append(f"both: Mickey allowed here, appearance count 0 (in memory only); winner set: {win.name}")
    menu_before = menu_cmds(lose)  # before a --drive: the down ends the form, so the rescue shows the plain menu
    # TODO 1.23, Mickey in a form: --by's Sora goes down in the first Drive Form of its list. --mixed: both, the host
    # first (Valor), then the friend's first one still offered (Wisdom: Goofy is away), your test 2026-11-15.
    for d in ([host, friend] if a.mixed else [friend if a.by == "B" else host]) if a.drive else []:
        d.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
        time.sleep(5)
        out.append(f"{d.name} Drove: form byte {read_u(d, d.game.base + 0x9ACDD4, 1)}")
    soras = {c.name: head(c) for c in copies}
    marks = {c.name: c.mark() for c in copies}
    stop = []
    tracers = mickey_trace(copies, stop) if a.trace else []
    dumps = {c.name: dump_sora(c, soras[c.name]) for c in copies} if a.dump else {}
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    time.sleep(2)
    if a.drive:  # a down in a form ends the form first: the Sora is a new actor
        soras["B"] = head(friend)
        out.append(f"friend down: form byte now {read_u(friend, friend.game.base + 0x9ACDD4, 1)}")
    out.append(f"friend down: low-HP beep byte {read_u(friend, friend.game.base + LOW_HP_BEEP, 1)} (0 = silent; "
               f"party_hud.cpp red face), HP {actor_hp(friend, soras['B'])}")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "mcoop_B_0_down.png"))
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    for n in range(3 if a.drive else 1):  # in a form Second Chance can take the first hit
        bench.debug("A", 0, 9999)
        time.sleep(4 if a.drive else 0)
        if go(host):
            break
    bench.wait("both Game Overs", lambda: go(host) and go(friend), 40, copies)
    time.sleep(3)
    if a.drive:
        soras = {c.name: head(c) for c in copies}
        out.append("at Game Over: form bytes " + ", ".join(f"{c.name} {read_u(c, c.game.base + 0x9ACDD4, 1)}" for c in copies))
    for c in copies:
        g = go(c)
        out.append(f"{c.name}: Game Over: Mickey branch {read_u(c, g + 0x14, 1) if g else '-'}, state "
                   f"{read_u(c, g + 0x10) if g else '-'}")
    if a.end == "nobody":  # no white screen in either game
        bench.wait("both Continue / Load menus", lambda: all(go(c) and read_u(c, go(c) + 0x10) == 6 for c in copies),
                   30, copies)
        return mcoop_end(copies, out, marks, stop, tracers, go)
    bench.wait("the winner's white screen choices", lambda: go(win) and read_u(win, go(win) + 0x10) == 6, 30, copies)
    time.sleep(2)
    out.append(f"loser's Game Over state while the winner chooses: {read_u(lose, go(lose) + 0x10) if go(lose) else '-'} "
               f"(5 = held, no choices)")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_1_choice.png"))
    if a.end == "over":
        win.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "300"])  # 'It's all over'
        return mcoop_end(copies, out, marks, stop, tracers, go)
    win.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up'
    bench.wait("Mickey in the winner's game", lambda: head(win) not in (0, soras[win.name]), 40, copies)
    bench.wait("the loser's Game Over to end", lambda: not go(lose), 20, copies)
    time.sleep(4)
    fade = read_u(lose, lose.game.base + 0xABB290 + 0x108)
    lm = player(lose)
    stand_in = lm not in (0, soras[lose.name]) and bench.model_of(lose, lm).startswith("P_EX2")
    out.append(f"loser's own Mickey (step c, MICKEY_PUPPET): player {bench.model_of(lose, lm) if lm else '-'}, "
               f"stand-in {stand_in}")
    out.append(f"loser back: fade state {fade} (0 = faded in), own Sora {head(lose) == soras[lose.name]}, motion "
               f"{bench.motion(lose, soras[lose.name])}, HP {actor_hp(lose, soras[lose.name])}, low-HP beep byte "
               f"{read_u(lose, lose.game.base + LOW_HP_BEEP, 1)}, Donald "
               f"{bool(bench.actor_by_model(lose, 'P_EX020'))}, Goofy {bool(bench.actor_by_model(lose, 'P_EX030'))}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_2_rescue.png"))
    # TODO 1.116: a hit on the loser's stand-in Mickey (debug damage 5, like an enemy's hit). The host's is forwarded:
    # the friend's Mickey loses it. The friend's is dropped: the host's game owns the hits on its Mickey.
    mhp = [actor_hp(win, head(win))]
    bench.debug(lose.name, 5, 3)
    time.sleep(2)
    mhp.append(actor_hp(win, head(win)))
    want = 3 if lose is host else 0
    standin_hit = None not in mhp and mhp[0] - mhp[1] == want
    out.append(f"a 3 HP hit on the loser's stand-in Mickey: the winner's Mickey HP {mhp[0]} -> {mhp[1]} (wanted -{want}, "
               f"{'the host forwards it' if lose is host else 'the friend never forwards'}): {'ok' if standin_hit else 'WRONG'}")
    if a.end == "dies":
        # What the game does when Mickey's HP reaches 0 (no Game Over within 40 s, bench 01:36): watched, not assumed.
        mickey = head(win)
        if a.by == "B":  # --by B: the lethal hit lands on the loser's stand-in (TODO 1.116), not on the winner's Mickey
            bench.debug(lose.name, 5, 9999)
        else:
            bench.debug(win.name, 0, 9999)  # the winner's list head = Mickey
        seq, start, shots, last = [], time.time(), [2, 6, 12, 20, 30, 40, 50], None
        steps = [(8, "a second hit at 0 HP", lambda: bench.debug(win.name, 0, 1)),
                 (20, "Healing Light x4", lambda: win.game.pad.run(HEALING_LIGHT * 4))]
        while time.time() - start < 55:
            if steps and time.time() - start >= steps[0][0]:
                seq.append((round(time.time() - start, 1), steps[0][1]))
                steps.pop(0)[2]()
            g = go(win)
            s = (bench.motion(win, mickey), actor_hp(win, mickey), head(win) == mickey,
                 bench.motion(win, soras[win.name]), actor_hp(win, soras[win.name]),
                 read_u(win, g + 0x10) if g else None, bench.motion(lose, soras[lose.name]), bool(go(lose)))
            if s != last:
                seq.append((round(time.time() - start, 1), s))
                last = s
            if shots and time.time() - start >= shots[0]:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_3_dies{shots[0]:02d}s.png"))
                shots.pop(0)
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} after Mickey's 0 HP")
            time.sleep(0.2)
        out.append("after Mickey's 0 HP, (s, (Mickey motion, Mickey HP, Mickey is the player, winner's Sora motion, "
                   "its HP, winner's Game Over state, loser's Sora motion, loser at Game Over)):")
        out.extend(f"  {t}: {s}" for t, s in seq)
        mcoop_lines(copies, marks, stop, tracers)
        return False  # an observation run
    presses, start = 0, time.time()
    gaps, same_motion, samples, cam_on_sora, hud_on_sora = [], 0, 0, 0, 0
    menu_on_sora, menus = 0, set()
    while time.time() - start < 150 and head(win) != soras[win.name]:
        win.game.pad.run(HEALING_LIGHT)
        presses += 1
        wm, lm = head(win), player(lose)
        if stand_in and wm and lm and wm != soras[win.name] and lm != soras[lose.name]:
            try:
                wp, lp = actor_pos(win, wm), actor_pos(lose, lm)
                gaps.append(sum((x - y) ** 2 for x, y in zip(wp, lp)) ** 0.5)
                samples += 1
                same_motion += bench.motion(win, wm) == bench.motion(lose, lm)
                cam_on_sora += read_u(lose, lose.game.base + CAMERA_TARGET, 8) == soras[lose.name]
                hud_on_sora += read_u(lose, lose.game.base + HUD_PLAYER, 8) == soras[lose.name]
                m = menu_cmds(lose)
                menus.add((hex(m[0]), tuple(m[1])))
                menu_on_sora += m[0] != 0x728 and m == menu_before
            except OSError:
                pass
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} during Mickey: {c.crash or ''}")
        if presses == 11:  # ~10 s
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_3_healing.png"))
            out.append(f"command menus at 10 s (top commands, slot 1): winner {menu_cmds(win)}, loser {menu_cmds(lose)}")
    out.append(f"winner: {presses} triangle mashes in {time.time() - start:.0f} s, Sora back {head(win) == soras[win.name]}")
    gaps.sort()
    follow = bool(gaps) and gaps[len(gaps) // 2] < 30
    if stand_in:
        out.append(f"loser's Mickey vs the winner's: {samples} samples, distance median "
                   f"{gaps[len(gaps) // 2] if gaps else '-':.6}, max {gaps[-1] if gaps else '-':.6}, same motion "
                   f"{same_motion}/{samples}; loser's camera on its own Sora {cam_on_sora}/{samples}, HUD {hud_on_sora}/{samples}, its Sora's menu {menu_on_sora}/{samples} "
                   f"(before: {menu_before}, seen: {sorted(menus)}); loser's player "
                   f"after: {bench.model_of(lose, player(lose))}")
    seq, end = [], time.time() + 12
    while time.time() < end:  # the loser's get-up
        m = (bench.motion(lose, soras[lose.name]), actor_hp(lose, soras[lose.name]))
        if not seq or seq[-1] != m:
            seq.append(m)
        time.sleep(0.25)
    # Your request 2026-10-21: both get-ups start together. Read from the logs ('getup start' / 'control' marks).
    def first(c, text):
        l = find(c.since(marks[c.name]), text)
        return l[0][:12] if l else None
    w_up, l_up = first(win, "Healing Light: our Sora is up"), first(lose, "our Mickey left")
    out.append(f"get-up start: winner {w_up}, loser {l_up}")
    out.append(f"loser after Healing Light: (motion, HP) {seq}, Donald {bool(bench.actor_by_model(lose, 'P_EX020'))}, "
               f"Goofy {bool(bench.actor_by_model(lose, 'P_EX030'))}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mcoop_{c.name}_4_after.png"))
    time.sleep(2)
    if a.dump:
        after = {c.name: dump_sora(c, soras[c.name]) for c in copies}
        dump_diff(dumps[lose.name], after[lose.name], dumps[win.name], after[win.name], out, "loser_vs_winner")
    landed = True
    for c in (lose, win):
        y0, peak, back = jump_test(c, soras[c.name])
        out.append(f"{c.name} ({'loser' if c is lose else 'winner'}) jumps: height {y0}, furthest {peak:+}, after 3 s {back:+}")
        landed &= abs(peak) > 5 and abs(back) < 5
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "1500", "wait", "300", "tap", "cross", "wait", "500"]), copies)
    time.sleep(2)
    # A Sora brought back from 0 HP by a direct write may still count as dead to the game: does a hit still hurt?
    before = {c.name: actor_hp(c, soras[c.name]) for c in copies}
    bench.debug("A", 0, 10)
    time.sleep(1)  # one debug command per frame: a second one sent at once replaces the first
    bench.debug("A", 3, 10)
    time.sleep(2)
    hurt = {c.name: (before[c.name] or 0) > (actor_hp(c, soras[c.name]) or 0) for c in copies}
    out.append(f"a 10 HP hit after the rescue: " + ", ".join(
        f"{c.name} {before[c.name]} -> {actor_hp(c, soras[c.name])}" for c in copies))
    mcoop_lines(copies, marks, stop, tracers)
    ok = all(hurt.values()) and landed
    for c in copies:
        hp = actor_hp(c, soras[c.name])
        out.append(f"{c.name}: player is own Sora {head(c) == soras[c.name]}, HP {hp}, Game Over {'yes' if go(c) else 'no'}, "
                   f"alive {c.alive()}, crash {c.crash or '-'}")
        ok &= c.alive() and not c.crash and head(c) == soras[c.name] and (hp or 0) > 0 and not go(c)
    ok &= fade == 0 and bool(bench.actor_by_model(lose, "P_EX020"))
    ok &= stand_in and follow and samples > 0 and cam_on_sora == samples == hud_on_sora == menu_on_sora  # step c: Mickey follows, camera on Sora
    return ok and standin_hit


def costume(copies, a, out):
    """TODO 5.6: in a world with its own Sora (--slot: 17 Timeless River, 21 Halloween Town, 22 Christmas Town, 23
    Pride Lands, 25 Space Paranoids): the host attacks (the copy on the friend's screen), then the friend's Sora goes
    down and gets up (the copy on the host's screen and the friend's own). Screenshots costume_<slot>_<step>_<copy>."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    time.sleep(4)

    def shot(step, who=copies):
        for c in who:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"costume_{a.slot}_{step}_{c.name}.png"))

    shot("0_start")
    for i in range(3):
        host.game.pad.run(["tap", "cross", "wait", "180"])
        shot(f"1_attack{i}", [friend])
    time.sleep(2)
    bench.debug("A", 3, 9999)  # the friend's Sora: a lethal hit on its copy in the host's game, forwarded
    prev = 0
    for t in (1, 3, 5, 7, 9, 12):
        time.sleep(t - prev)
        prev = t
        shot(f"2_down{t:02d}s")
    fl = friend.since(marks["B"])
    down = find(fl, "down instead of dead")
    up = find(fl, "getting up")
    out.append(f"friend lethal hit: {'down' if down else 'NOT down'} ({(down or ['-'])[0][13:140]})")
    out.append(f"friend got up: {len(up)} ({(up or ['-'])[0][13:120]})")
    for c in copies:
        lines = c.since(marks[c.name])
        odd = [l[13:160] for l in lines if "motion" in l and ("not found" in l or "missing" in l or "no motion" in l)]
        out.append(f"{c.name}: motion problems logged {len(odd)} {odd[:3]}, alive {c.alive()}, crash {c.crash or '-'}")
    return bool(down) and all(c.alive() and not c.crash for c in copies)


def mickey_real(copies, a, out):
    """What the game does when a real enemy hit takes Mickey's last HP (your question 2026-10-18: does he still revive
    Sora, with less HP?). Save 20, the Twin Lords fight (07/03), Mickey allowed in both games, the host wins the roll;
    both down, the host's 'I won't give up'; Mickey left at 2 HP by a debug hit, then he stands still for the bosses.
    Logged for 120 s, and on until the friend has been up for 25 s (the bosses' knock-ups): Mickey's motion / HP, who
    is the player, both Soras, the friend's height, Game Overs; screenshots every 10 s (mreal_<copy>_<s>).
    Your rule 2026-10-18: Mickey's last HP gets the host up with half HP, the friend (the loser) stays down, its usual
    timer starts, it gets up with 26% HP, or earlier through a heal (an ally's item or Cure is fine, your answer
    2026-10-21); it must not float (back on the floor after each knock-up, a jump lands).
    PASS = no crash, the friend's release and get-up (26% by the timer, or a heal's HP), no float, the jump lands."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x03, 59)
    bench.wait("both in 07/03", lambda: all(c.room() == (0x07, 0x03) for c in copies), 60, copies)
    time.sleep(8)
    for c in copies:
        write_mem(c, c.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(c, c.game.base + MICKEY_COUNT, b"\x00\x00")
    soras = {c.name: head(c) for c in copies}
    marks = {c.name: c.mark() for c in copies}
    for n in range(3):
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
        time.sleep(3)
    out.append(f"friend down: {bool(find(friend.since(marks['B']), 'down instead of dead'))}")
    bench.debug("A", 0, 9999)
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("both Game Overs", lambda: go(host) and go(friend), 40, copies)
    bench.wait("the host's white screen choices", lambda: go(host) and read_u(host, go(host) + 0x10) == 6, 30, copies)
    time.sleep(2)
    host.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up'
    bench.wait("Mickey", lambda: head(host) not in (0, soras["A"]), 40, copies)
    time.sleep(4)
    mickey = head(host)
    hp0 = actor_hp(host, mickey) or 0
    if hp0 > 2:
        bench.debug("A", 0, hp0 - 2)  # the list head = Mickey: left at 2 HP
    time.sleep(1)
    out.append(f"Mickey {mickey:#x}: HP {hp0} -> {actor_hp(host, mickey)}; now the bosses' hits")
    fy = lambda: round(struct.unpack("<f", peek.read(friend.game.h, soras["B"] + 0x674, 4))[0])
    seq, start, last, shot, up_at, ys = [], time.time(), None, set(), None, []
    while time.time() - start < (120 if up_at is None else max(120, up_at + 25)) and time.time() - start < 220:
        g = go(host)
        s = (bench.motion(host, mickey), actor_hp(host, mickey), head(host) == mickey, bench.motion(host, soras["A"]),
             actor_hp(host, soras["A"]), read_u(host, g + 0x10) if g else None, bench.motion(friend, soras["B"]),
             actor_hp(friend, soras["B"]), bool(go(friend)), fy() // 10 * 10)
        if up_at is None and find(friend.since(marks["B"]), "downed: getting up with HP"):
            up_at = time.time() - start
        if up_at is not None and not go(friend):
            ys.append((round(time.time() - start, 1), fy()))
        if s != last:
            seq.append((round(time.time() - start, 1), s))
            last = s
        k = int(time.time() - start)
        if k % 10 == 0 and k not in shot:
            shot.add(k)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"mreal_{c.name}_{k:03d}.png"))
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        time.sleep(0.2)
    out.append("(s, (Mickey motion, Mickey HP, Mickey is the player, host's Sora motion, its HP, host's Game Over state, "
               "friend's Sora motion, its HP, friend at Game Over, friend's height /10)):")
    out.extend(f"  {t}: {s}" for t, s in seq[:80])
    hits = [l[13:150] for l in host.since(marks["A"]) if f"exe+0x{mickey - host.game.base:X}" in l and "HP change" in l]
    out.append(f"HP changes on Mickey: {hits[:12]}")
    keys = ("mickey", "downed:", "game over", "switched actors", "HP change: Sora", "exception", "CRASH")
    for c in copies:
        with open(os.path.join(OUT, f"mreal_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(marks[c.name]) if any(k in l for k in keys)) + "\n")
    fl = friend.since(marks["B"])
    release = find(fl, "we stay down, the usual timer starts")
    gotup = find(fl, "downed: getting up with HP")
    out.append(f"friend: release {release[0][13:] if release else 'not seen'}")
    out.append(f"friend: get-up {gotup[0][13:] if gotup else 'not seen'}")
    maxhp = read_u(friend, friend.game.base + 0x2A23598 + 4)
    want = f"getting up with HP {max(1, (maxhp * 26 + 99) // 100)}/{maxhp}"
    # Floating: up is negative y, so the floor is the largest y seen once the friend was up; a Sora left in the air
    # sits more than 30 above it for over 3 s in a row.
    floor = max((y for _, y in ys), default=None)
    high = [t for t, y in ys if floor is not None and y < floor - 30]
    stuck, run0 = 0.0, None
    for i, t in enumerate(high):
        run0 = t if i == 0 or t - high[i - 1] > 0.5 else run0
        stuck = max(stuck, t - run0)
    out.append(f"friend's height once up: floor {floor}, {len(ys)} samples, {len(high)} more than 30 above it, "
               f"longest {stuck:.1f} s (first {high[:5]})")
    landed = None
    if gotup and bench.motion(friend, soras["B"]) not in (252, 253, 54) and not go(friend):
        y0, peak, back = jump_test(friend, soras["B"])
        landed = abs(peak) > 5 and abs(back) < 5
        out.append(f"friend jumps: height {y0}, furthest {peak:+}, after 3 s {back:+}")
    else:
        out.append(f"friend jump test skipped (motion {bench.motion(friend, soras['B'])}, Game Over {bool(go(friend))})")
    healed = find(fl, "reached our Sora: getting up now")  # an ally's heal before the timer
    if healed:
        out.append(f"friend: got up through a heal before the timer: {healed[0][13:120]}")
    ok = bool(release) and bool(gotup) and (want in gotup[0] or bool(healed)) and stuck <= 3 and landed is not False
    return ok and all(c.alive() and not c.crash for c in copies)


def mickey_hit(copies, a, out):
    """TODO 1.116: real enemy hits on Mickey. Save 20, the Twin Lords fight (07/03), Mickey allowed in both games, the
    roll's winner by --winner (B: the friend plays Mickey and the bosses, the host's, hit the host's stand-in; A: the
    control, the host's own Mickey). Both down, the winner's 'I won't give up', then Mickey stands still for the bosses
    for 40 s or until he leaves; the winner's Mickey's (motion, HP) every 50 ms. PASS = no crash, his HP dropped and
    he flinched within 0.3 s of a drop; with --winner B also a forwarded hit applied to him. With --env
    KH2COOP_NET_SIM=400,0,0 the forwarded hit comes too late to count as the local one: its reaction is replayed."""
    host, friend = copies
    win, lose = (host, friend) if a.winner == "A" else (friend, host)
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x03, 59)
    bench.wait("both in 07/03", lambda: all(c.room() == (0x07, 0x03) for c in copies), 60, copies)
    time.sleep(8)
    for c in copies:
        write_mem(c, c.game.base + MICKEY_ALLOWED, b"\xB0\x01\xC3")
        write_mem(c, c.game.base + MICKEY_COUNT, b"\x00\x00")
    soras = {c.name: head(c) for c in copies}
    marks = {c.name: c.mark() for c in copies}
    # The friend down, then the host at once: a companion's item gets a downed Sora up within a second (first run).
    for n in range(3):
        bench.debug("A", 3, 9999)
        end = time.time() + 4
        while time.time() < end and not find(friend.since(marks["B"]), "down instead of dead"):
            time.sleep(0.1)
        if find(friend.since(marks["B"]), "down instead of dead"):
            break
    time.sleep(0.1)  # one test command a frame
    bench.debug("A", 0, 9999)
    go = lambda c: read_u(c, c.game.base + GAME_OVER, 8)
    bench.wait("both Game Overs", lambda: go(host) and go(friend), 40, copies)
    bench.wait("the winner's white screen choices", lambda: go(win) and read_u(win, go(win) + 0x10) == 6, 30, copies)
    time.sleep(2)
    win.game.pad.run(["tap", "cross", "wait", "300"])  # 'I won't give up'
    bench.wait("Mickey", lambda: head(win) not in (0, soras[win.name]), 40, copies)
    time.sleep(3)
    mickey = head(win)
    seq, start, checked = [], time.time(), 0.0
    while time.time() - start < 40 and head(win) == mickey:
        try:
            s = (bench.motion(win, mickey), actor_hp(win, mickey))
        except OSError:
            break
        if not seq or seq[-1][1:] != s:
            seq.append((round(time.time() - start, 2),) + s)
        if time.time() - checked > 1:
            checked = time.time()
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        time.sleep(0.05)
    drops = [i for i in range(1, len(seq)) if None not in (seq[i][2], seq[i - 1][2]) and seq[i][2] < seq[i - 1][2]]
    # A flinch = a motion other than idle (0) within 0.3 s of the drop, before or after: the friend's own game plays
    # it at its copy of the boss's swing, the forwarded damage follows about 0.1 s later.
    flinches = [seq[i][0] for i in drops if any(seq[k][1] != 0 and abs(seq[k][0] - seq[i][0]) <= 0.3
                                                 for k in range(len(seq)))]
    out.append(f"{win.name}'s Mickey (winner {a.winner}): {len(drops)} HP drops, a flinch within 0.3 s of "
               f"{len(flinches)} of them; (s, motion, HP):")
    out.extend(f"   {t}" for t in seq[:45])
    wl, ll = win.since(marks[win.name]), lose.since(marks[lose.name])
    applied = find(wl, "applied the peer's hit to our Mickey")
    played = find(wl, "played on our Mickey")
    own = [l for l in wl if "hit reaction:" in l and "not played" in l]
    out.append(f"{win.name}: forwarded hits applied to our Mickey {len(applied)}, their reactions replayed {len(played)}, "
               f"not replayed (our own game's hit played one) {len(own)}; {lose.name}: stat-only forwards "
               f"{len(find(ll, 'forwarded to the friend'))}")
    out.extend("   " + l[13:170] for l in (applied[:3] + played[:4] + own[:2]))
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"mickey_hit_{c.name}.png"))
    ok = all(c.alive() and not c.crash for c in copies) and bool(drops) and bool(flinches)
    return ok and (a.winner == "A" or bool(applied))


def getup(copies, a, out):
    """Your pick 2026-10-18 (god mode off, save 52, no fight): the friend goes down off the save point; while down its
    gauge shows the KO face and the low-HP beep is off; it gets up on its own (6 s after a calm room) with 26% of max
    HP rounded up (above the game's low-HP line, max/4), beep still off. Screenshots getup_B_<step>."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.both(lambda c: c.game.pad.run(["stick", "0", "1", "2500", "wait", "1500"]), copies)  # off the save point
    sora = head(friend)
    mark = friend.mark()
    beep = lambda: read_u(friend, friend.game.base + LOW_HP_BEEP, 1)
    for n in range(3):  # its own rules may keep it at 1 HP once (Second Chance)
        bench.debug("A", 3, 9999)
        time.sleep(1)
        if find(friend.since(mark), "down instead of dead"):
            break
        time.sleep(3)
    down = find(friend.since(mark), "down instead of dead")
    time.sleep(1.5)
    out.append(f"friend down: {bool(down)}, HP {actor_hp(friend, sora)}, beep byte {beep()} (0 = off)")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "getup_B_1_down.png"))
    bench.wait("the friend's get-up", lambda: find(friend.since(mark), "downed: getting up with HP"), 45, [friend, host])
    time.sleep(6)
    up = find(friend.since(mark), "downed: getting up with HP")
    hp, maxhp = actor_hp(friend, sora), read_u(friend, friend.game.base + 0x2A23598 + 4)
    want = max(1, (maxhp * 26 + 99) // 100)
    out.append(f"friend up: '{up[0][13:]}', HP now {hp}/{maxhp} (26% rounded up = {want}, low-HP line {maxhp // 4}), "
               f"beep byte {beep()}")
    bench.screenshot(friend.hwnd, os.path.join(OUT, "getup_B_2_up.png"))
    return bool(down) and (hp or 0) == want and hp > maxhp // 4 and all(c.alive() and not c.crash for c in copies)


ADMIT_LIMIT, ADMIT_USED = 0x2A0F7DC, 0x2A0F830  # the actor factory's admission budget (float32 each)
SHEETS_FREE = 0x2A23950  # i32: free stat sheets in the pool of 80 (exe+0x3C0280 takes one, no check for none left)


def swarm(copies, a, out):
    """Your setup 2026-10-18 (Land of Dragons climb, Shan-Yu's swarm, 08/07 programs 73): both Soras invincible (the
    bench's god mode), every hit on an enemy takes all its HP (KH2COOP_TEST_ONE_HIT=1), both lock on (R1), attack
    and jump-attack until the mission ends (the room changes) or --seconds. Every 2 s in both games: the factory's
    budget (limit, used), the enemies (M_*) in the actor list, the swarm's M_EX660_RAW. Screenshots swarm_<copy>_<s>,
    the timeline in swarm_timeline.txt. PASS = no crash (the findings are in the summary)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.warp(0x08, 0x07, 73)
    bench.wait("both in 08/07", lambda: all(c.room() == (0x08, 0x07) for c in copies), 90, copies)
    time.sleep(5)
    f32 = lambda c, off: struct.unpack("<f", peek.read(c.game.h, c.game.base + off, 4))[0]
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2

    def sample(c):
        try:
            names = [n for _, n in bench.actors(c)]
            return (round(f32(c, ADMIT_LIMIT)), round(f32(c, ADMIT_USED)), sum(n.startswith("M_") for n in names),
                    names.count("M_EX660_RAW"))
        except OSError:
            return None

    timeline, start, shot, nxt, peak = [], time.time(), set(), 0.0, {c.name: 0 for c in copies}
    seconds, done = min(a.seconds, 300), None
    while time.time() - start < seconds and all(c.room() == (0x08, 0x07) for c in copies if c.alive()):
        if done is None and find(host.since(marks["A"]), "our mission script ended the fight"):
            done = time.time()  # mission complete (the timer ran out): 5 s more, then stop
        if done and time.time() - done > 5:
            break
        bench.both(lambda c: c.game.pad.run(fight), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        t = time.time() - start
        if t >= nxt:
            nxt = t + 2
            row = {c.name: sample(c) for c in copies}
            for c in copies:
                if row[c.name] and row[c.name][1] > peak[c.name]:
                    peak[c.name] = row[c.name][1]
            timeline.append(f"{t:6.1f} " + " | ".join(
                f"{n} budget {r[0]} used {r[1]}, enemies {r[2]}, M_EX660_RAW {r[3]}" if r else f"{n} -"
                for n, r in row.items()))
        k = int(t) // 15 * 15
        if k not in shot:
            shot.add(k)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"swarm_{c.name}_{k:03d}.png"))
    out.append(f"ran {time.time() - start:.0f} s; rooms now A {host.room()} B {friend.room()}")
    with open(os.path.join(OUT, "swarm_timeline.txt"), "w") as fh:
        fh.write("time   " + "per copy: budget limit, used, enemies (M_*) in the actor list, of them M_EX660_RAW\n")
        fh.write("\n".join(timeline) + "\n")
    out.extend(timeline[::5])
    out.append(f"peak used: A {peak['A']}, B {peak['B']}")
    fl = friend.since(marks["B"])
    made = find(fl, "no spawn record) has no match here: made")
    failed = find(fl, "no spawn record) has no match here: FAILED")
    last = [l for l in fl if "host objects made here" in l][-1:]
    lingering = {l.split()[2] for l in fl if "removed but still here" in l}
    parked = {l.split()[2] for l in fl if "has no host match; parked" in l}
    out.append(f"friend: made lines {len(made)}, failed lines {len(failed)} (logged up to 60), "
               f"last count '{last[0][13:] if last else '-'}'")
    out.append(f"friend: removed but still here {len(lingering)}, parked {len(parked)} (distinct)")
    out.append(f"host one-hit lines {len(find(host.since(marks['A']), 'TEST one hit'))}")
    keys = ("spawn:", "removed", "parked", "world: enemies host", "mission", "TEST one hit", "exception", "CRASH")
    for c in copies:
        with open(os.path.join(OUT, f"swarm_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(marks[c.name]) if any(k in l for k in keys)) + "\n")
    return all(c.alive() and not c.crash for c in copies)


VALOR_RECORD = 0x9ACBA4  # Valor's save record (level +2, EXP +4); forms 1-10 from exe+0x9ACBA4, 0x38 each


def sheet_link(c):
    """(our Sora's form +0xDE0, its stat sheet's form link +0x254 decoded, as an exe offset or 0)."""
    sora = read_u(c, c.game.base + 0x2A171C8, 8)
    if not sora:
        return None
    sheet = read_u(c, sora + 0x5C0, 8)
    link = bench.decode(c, read_u(c, sheet + 0x254)) if sheet else 0
    return read_u(c, sora + 0xDE0), (link - c.game.base) if link else 0


def valor_sheet(copies, a, out):
    """TODO 1.102 (host crash 2026-10-21: a Valor level-up with the Sora copy in the room). The host Drives into Valor
    in save 10's room (LoD 08/00), then warps to the swarm room (08/07 programs 73: enemies at once), so the copy is
    built after the Drive; its Valor EXP is set to 9999 and it locks on and attacks (one-hit kills) until Valor's level
    goes up or 60 s. Our Sora's form and its sheet's form link are read before and after the warp.
    PASS = Valor's level went up, no crash. Run with --env KH2COOP_SORA_SHEET_FIX=0 to see the bug."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    valor = lambda: (read_u(host, host.game.base + VALOR_RECORD + 2, 1), read_u(host, host.game.base + VALOR_RECORD + 4))
    out.append(f"before: form / link {sheet_link(host)}, Valor (level, EXP) {valor()} (Valor's record exe+0x{VALOR_RECORD:X})")
    host.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
    time.sleep(6)
    out.append(f"in Valor: form / link {sheet_link(host)}")
    bench.warp(0x08, 0x07, 73)
    bench.wait("both in 08/07", lambda: all(c.room() == (0x08, 0x07) for c in copies), 90, copies)
    time.sleep(4)
    out.append(f"after the warp: form / link {sheet_link(host)}")
    start_level = valor()[0]
    write_mem(host, host.game.base + VALOR_RECORD + 4, struct.pack("<I", 9999))
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 4
    start = time.time()
    while time.time() - start < 60 and valor()[0] == start_level:
        host.game.pad.run(fight)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    time.sleep(2)
    for c in copies:
        c.lines()
        bench.screenshot(c.hwnd, os.path.join(OUT, f"valor_sheet_{c.name}.png"))
    out.append(f"after {time.time() - start:.0f} s of attacks: Valor (level, EXP) {valor()}, form / link {sheet_link(host)}")
    for c in copies:
        lines = [l[13:200] for l in c.since(marks[c.name]) if "stat sheet" in l or "Sora sheet" in l or "CRASH" in l]
        out.append(f"{c.name} sheet lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:12])
    return valor()[0] > start_level and all(c.alive() and not c.crash for c in copies)


def formlook(copies, a, out):
    """TODO 1.24 research step 1 (form_look.cpp, test command 18): in save 59 (all forms but Anti) both players Drive
    into the same entry of their list (--pick = how many entries, from the top; the gauge is refilled first: stat
    sheet +0x1B1 = +0x1B2 full bars), so each game has that form's files and each copy plays the other's form motions
    (as in play); each game's copy takes the form's look, then each player attacks in turn; screenshots
    formlook_<n>_<step>_<copy>.png; then both copies go back (form 0) and both Revert. PASS = no crash (the look is
    judged from the screenshots).
    --one (step 2 of the plan, files on demand): only one player Drives (--by A = the host, B = the friend), so the
    other game doesn't have the form's files; its copy asks for them and takes the look once they're in (the time is
    logged). Screenshots 4 s later, after the Drive's shine. PASS = no crash and every look applied.
    --mixed: both Drive, the friend into the next entry (different forms at once).
    --auto (step 3): no test commands; each copy follows the other player's body from the pose packet (form start
    and Revert). Screenshots 1 s and 4 s after the Drive. PASS = no crash, a look per Drive and one back per Revert."""
    host, friend = copies
    drivers = copies if not a.one else [host] if a.by == "A" else [friend]
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    attack = ["tap", "cross", "wait", "250"] * 3
    tried = 0
    for n in range(a.pick or 5):
        for c in copies:
            refill(c)
        # --mixed: the friend picks the next entry, so each copy shows a form its game isn't in (files on demand while
        # its own Sora is in another form; then both Revert, which drops the Drive area's files).
        pick = lambda c: (n + 1) % (a.pick or 5) if a.mixed and c is friend else n
        bench.both(lambda c: c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] *
                                            pick(c) + ["tap", "cross", "wait", "500"]), drivers)
        if a.auto:  # just after the body swap (~0.67 s after the form change)
            time.sleep(1.2)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"formlook_{n}_early_{c.name}.png"))
            time.sleep(3.8)
        else:
            time.sleep(5)
        forms = {c.name: form_now(c) for c in drivers}
        out.append(f"drive list entry {n}: forms now {forms}")
        if not all(forms.values()):
            continue
        tried += len(drivers)
        if not a.auto:
            for c in drivers:
                # Each game's copy is the other player: it takes the form of whoever drove in the other game.
                seer = friend if c is host else host
                bench.debug(seer.name, 18, forms[c.name])
        time.sleep(1.5 if not a.one and not a.auto else 0 if a.auto else 4)
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"formlook_{n}_idle_{c.name}.png"))
        for doer, seer in ((friend, host), (host, friend)):
            doer.game.pad.run(attack[:4])
            bench.screenshot(seer.hwnd, os.path.join(OUT, f"formlook_{n}_attack_{seer.name}.png"))
            doer.game.pad.run(attack[4:])
            time.sleep(1.5)
        if not a.auto:
            for c in drivers:
                bench.debug((friend if c is host else host).name, 18, 0)
        time.sleep(1.5)
        bench.both(lambda c: c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"]),
                   drivers)  # Revert
        time.sleep(5)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    for c in copies:
        c.lines()
        lines = [l[13:200] for l in c.since(marks[c.name]) if "form look" in l or "CRASH" in l]
        out.append(f"{c.name} form look lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:30])
    looks = [l for c in copies for l in c.since(marks[c.name]) if "now looks like" in l]
    applied = sum("form 0" not in l for l in looks)
    back = len(looks) - applied
    out.append(f"looks applied: {applied} (back to Sora: {back}), Drives that worked: {tried}")
    ok = applied >= tried and (not a.auto or back >= tried)
    return all(c.alive() and not c.crash for c in copies) and (ok or not (a.one or a.auto))


def form_share(copies, a, out):
    """TODO 1.127 (F342; the friend's crash 2026-10-26 19:55:18 at exe+0x3C859E, both in Limit Form): save 59, for each
    Drive list entry (--pick = how many, default 5) and both orders: the first player Drives, then the second into the
    same form (its own Keyblade then runs from files the copy's look loaded in its game), then the first Reverts while
    the second stays in: the second's game lets go of the copy's look and must keep the files its own Sora uses. Then
    the second Reverts. Pose packets drive the looks (as in play). PASS = no crash, no game closed."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    cases = 0
    for n in range(a.pick or 5):
        for first, second in ((host, friend), (friend, host)):
            for c in copies:
                refill(c)
            drive(first, n)
            time.sleep(5)
            drive(second, n)
            time.sleep(5)
            forms = (form_now(first), form_now(second))
            check(f"entry {n}, {first.name} then {second.name} in")
            revert(first)
            time.sleep(5)
            check(f"entry {n}, {first.name} out, {second.name} still in")
            after = form_now(second)
            revert(second)
            time.sleep(5)
            check(f"entry {n}, both out")
            both_in = forms[0] and forms[0] == forms[1]
            cases += bool(both_in and after)
            out.append(f"entry {n}: {first.name} then {second.name}: forms {forms}, {second.name} after the other's "
                       f"Revert {after}, {'tested' if both_in and after else 'NOT the case (forms differ or none)'}")
    for c in copies:
        new = c.since(marks[c.name])
        kept = [l[13:200] for l in new if "used by another actor" in l or "let go of object" in l]
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, drop lines {len(kept)}:")
        out.extend(f"    {l}" for l in kept[:24])
    out.append(f"cases with both in the same form when the first Reverted: {cases} of {2 * (a.pick or 5)}")
    return all(c.alive() and not c.crash for c in copies) and cases > 0


FILE_BAD = ("DECLINED", "MOVED", "BUG", "copy files: WARNING", "MISS:", "in the region (")


def larxene_mem(copies, a, out):
    """TODO 1.127, the fight bench for the copy's files (your pick 2026-10-29: Data Larxene, the tightest fight in the
    two-PC logs, F376): save 59, warp into Data Larxene (04/21 program 148), her invincible (KH2COOP_ENEMY_GOD=1, set
    here) and both Soras (the bench's god mode). Rounds of Drives while both lock on (R1) and attack: the host Final and
    the friend Limit, swapped, both Limit (the shared look), host Valor and friend Wisdom; each round both attack 15 s
    in their forms, then both Revert (the Limit preload's layouts) and attack 5 s. PASS = no crash, no game closed, no
    DECLINED / MOVED / BUG / margin WARNING / MISS / look in the region, and each look the other game asked in our
    buffer."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.warp(0x04, 0x21, 148)
    bench.wait("both in Data Larxene's room", lambda: all(c.room() == (0x04, 0x21) for c in copies), 90, copies)
    time.sleep(15)  # her intro scene, then the fight
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 4

    def attack(seconds):
        end = time.time() + seconds
        while time.time() < end:
            bench.both(lambda c: c.game.pad.run(fight), copies)
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")

    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    rounds = [("host Final, friend Limit", (5, 4, 1), (3,)), ("host Limit, friend Final", (3,), (5, 4, 1)),
              ("both Limit", (3,), (3,)), ("host Valor, friend Wisdom", (1,), (2,))]
    attack(5)
    for name, hf, ff in rounds:
        took = (drive_form(host, hf), drive_form(friend, ff))
        attack(15)
        for c in copies:
            if form_now(c):
                revert_to_sora(c)
        attack(5)
        out.append(f"{name}: forms taken {took}, now {(form_now(host), form_now(friend))}")
    ok = True
    for c in copies:
        new = c.since(marks[c.name])
        asked = [l[13:260] for l in new if "copy files: object" in l and "files of ours in group" in l]
        bad = [l[13:260] for l in new if any(b in l for b in FILE_BAD)]
        worn = sum("form look: the copy" in l and "now looks like" in l for l in new)
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}; looks asked {len(asked)} (in our buffer "
                   f"{sum('in our buffer' in l for l in asked)}), looks worn {worn}, bad lines {len(bad)}")
        out.extend(f"    {l}" for l in asked[:8])
        out.extend(f"    BAD {l}" for l in bad[:12])
        bench.screenshot(c.hwnd, os.path.join(OUT, f"larxene_mem_{c.name}.png"))
        ok &= c.alive() and not c.crash and not bad and bool(asked) and all("in our buffer" in l for l in asked)
    return ok


def limit_gap(copies, a, out):
    """F356's repro (TODO 1.127; the host's crash 2026-10-27 00:25:09, Data Larxene): save 59 in the gym, for the Drive
    list entries 0..--pick-1 (default 2) and both ways: the reverter Drives into entry n, the other player into entry
    n + 1 (so the reverter's copy asks that look's files into the Drive area after the reverter's own form's), then the
    reverter Reverts: the Revert leaves its form's files in 5000, and on the next frame the game's Limit preload drops
    them, asks the Limits and lays the heap out with no load wait, moving the copy's files. Then the other Reverts.
    The preload only runs for usable Limits: save 59 has every Limit ability off, so they are equipped in memory and the
    gym's room load (02/28, Donald + Goofy) builds the ability bits; 'limitgap' with nobody in a form must show the
    preload's drop (the baseline), else the rest proves nothing.
    PASS = no crash, every case set up (the look worn, the preload's drop after the Revert), and no file the copy wears
    moved by the preload's layout (a 'moved the copy's' line or copy_blade's 'a file layout moves its file')."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    out.append(f"Limit abilities equipped (memory only): {[equip_limits(c) for c in copies]}")
    bench.warp(0x02, 0x28, 0)
    bench.wait("both in 02/28", lambda: all(c.room() == (0x02, 0x28) for c in copies), 60, copies)
    time.sleep(12)
    marks = {c.name: c.mark() for c in copies}
    for c in copies:
        bench.debug(c.name, 25, 0)
    time.sleep(2.5)
    base = {c.name: sum("caller exe+0x3e772e" in l for l in c.since(marks[c.name])) for c in copies}
    out.append(f"baseline (nobody in a form): the game's preload drops after limitgap {base}")
    if not all(base.values()):
        raise bench.Failed("the Limit preload didn't run after limitgap (no usable Limit?)")
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    cases, moved_all = 0, 0
    total = a.pick or 2
    for n in range(total):
        for rev, other in ((host, friend), (friend, host)):
            for c in copies:
                refill(c)
            drive(rev, n)
            time.sleep(5)
            m = rev.mark()
            drive(other, n + 1)
            time.sleep(6)
            check(f"entry {n}: {rev.name} in, {other.name} into entry {n + 1}")
            forms = (form_now(rev), form_now(other))
            worn = any("now looks like" in l and "form 0" not in l for l in rev.since(m))
            m = rev.mark()
            revert(rev)
            time.sleep(5)
            check(f"entry {n}: {rev.name} reverted with its copy in form {forms[1]}")
            new = rev.since(m)
            preload = sum("caller exe+0x3e772e" in l for l in new)
            moved = [l[13:200] for l in new if "moved the copy's" in l or "moves its file" in l]
            moved_all += len(moved)
            ok_case = forms[0] and forms[1] and forms[0] != forms[1] and worn and preload
            cases += bool(ok_case)
            out.append(f"entry {n}: {rev.name} in form {forms[0]}, {other.name} in form {forms[1]}, {rev.name}'s copy "
                       f"wore it {worn}; after {rev.name}'s Revert: the preload's drops {preload}, the copy's files "
                       f"moved {len(moved)}{'' if ok_case else ' (NOT the case)'}")
            out.extend(f"    {l}" for l in moved[:8])
            revert(other)
            time.sleep(5)
            check(f"entry {n}: both out")
    for c in copies:
        new = c.since(marks[c.name])
        lines = [l[13:200] for l in new if "limit gap:" in l or "caller exe+0x3e772e" in l or "moved the copy's" in l
                 or "moves its file" in l or "CRASH" in l]
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:40])
    out.append(f"cases set up: {cases} of {2 * total}; the copy's files moved by the preload: {moved_all}")
    return all(c.alive() and not c.crash for c in copies) and cases == 2 * total and moved_all == 0


def file_groups(copies, a, out):
    """The review's 5.2.6 research, read only (no fighting): a file group the Drive layout doesn't move. Save 59; the file
    list of both games dumped by group at: the room load, the host's Drive (--pick entry), its Revert, the friend's
    Drive, its Revert, the host's pause-menu Keyblade swap, and after a room change (the gym). Each dump: every entry
    (file_groups_<copy>.txt) and per class (negative groups, 5100-5199, the Drive area 5000-5099, let go >= 10000, the
    rest) the count and bytes; for the negative and 5100-5199 entries, which ones are new, gone or moved since the last
    dump. Also the heap's end (*(exe+0x749740)) and where the negative files begin (exe+0x29F33F0). PASS = no crash."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    q = lambda c, addr: struct.unpack("<Q", peek.read(c.game.h, addr, 8))[0]
    last = {c.name: {} for c in copies}
    full = {c.name: [] for c in copies}

    def kind(g):
        return ("negative" if g < 0 else "let go" if g >= 10000 else "5100-5199" if 5100 <= g < 5200
                else "Drive 5000-5099" if 5000 <= g < 5100 else "rest")

    def dump(moment):
        for c in copies:
            files = file_list(c)
            end, neg = q(c, c.game.base + 0x749740), q(c, c.game.base + 0x29F33F0)
            top = max((pl + sz for _, _, _, g, pl, sz, _ in files if 0 <= g < 10000), default=0)
            full[c.name].append(f"== {moment}: {len(files)} entries; heap end {end:#x}, negative files from {neg:#x}, "
                                f"top of the rest {top:#x}, room between {neg - top}")
            full[c.name] += [f"   {n} group {g} state {s} place {pl:#x} size {sz:#x} flags {f:#x}"
                             for _, n, s, g, pl, sz, f in files]
            classes = {}
            for _, n, s, g, pl, sz, f in files:
                k = classes.setdefault(kind(g), [0, 0])
                k[0] += 1
                k[1] += sz
            out.append(f"{c.name} {moment}: " + ", ".join(f"{k} {v[0]} ({v[1] // 1024} KB)" for k, v in sorted(classes.items()))
                       + f"; room between the rest and the negative files {(neg - top) // 1024} KB")
            now = {(n, g): pl for _, n, _, g, pl, _, _ in files if g < 0 or 5100 <= g < 5200}
            before = last[c.name]
            for key, pl in sorted(now.items(), key=lambda kv: kv[0][1]):
                if key not in before:
                    out.append(f"   new   {key[0]} group {key[1]} place {pl:#x}")
                elif before[key] != pl:
                    out.append(f"   MOVED {key[0]} group {key[1]} {before[key]:#x} -> {pl:#x}")
            out.extend(f"   gone  {k[0]} group {k[1]}" for k in before if k not in now)
            last[c.name] = now

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def drive(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    dump("room load")
    for who in (host, friend):
        drive(who)
        time.sleep(5)
        check(f"{who.name}'s Drive")
        dump(f"{who.name}'s Drive (form {form_now(who)})")
        revert(who)
        time.sleep(5)
        check(f"{who.name}'s Revert")
        dump(f"{who.name}'s Revert")
    host.game.pad.run(MENU_FIRST_WEAPON)
    time.sleep(4)
    check("the menu swap")
    dump("the host's menu swap")
    bench.warp(0x02, 0x28, 0)
    bench.wait("both in the gym", lambda: all(c.room() == (0x02, 0x28) for c in copies), 60, copies)
    time.sleep(6)
    check("the room change")
    dump("room change (gym 02/28)")
    for c in copies:
        with open(os.path.join(OUT, f"file_groups_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(full[c.name]) + "\n")
    return all(c.alive() and not c.crash for c in copies)


def neg_group(copies, a, out):
    """The review's 5.2.6, your pick B, write test (form_look.cpp test command 24 'negfiles', no fighting): a look's
    files asked into a negative group of ours, then the game's own file events, and nothing of ours may move.
    Main run (save 59): the request sent during the warp to the gym must be refused (the gate: the room's files not
    all placed); in the gym both games ask Valor's look (-100..-98), then Wisdom's (-97..-95); then the host Drives
    into each Drive list entry and Reverts (the Valor and Wisdom ones: our request first, then the game's own Drive
    asks the same files: crashes, L149, so only entries 2 and 3), the friend once, the host's pause-menu Keyblade swap, a summon and its dismissal twice, a Dusk spawned
    in front of the host (invincible: KH2COOP_ENEMY_GOD=1, set here), a Limit (forced battle state); after each, both games' file lists are read: our
    files' places, our bottom (exe+0x29F33F0) and the heap's end (*(exe+0x749740)) must be unchanged. Then the room
    change (back to 04/06): ours let go at the room's own drop, before its layout, no negative file left after it.
    Then the other order in the new room: the host Drives into Valor first, then asks Valor's look.
    --decline: KH2COOP_NEG_BUDGET_KB=100 (set here): the request is declined with its line, its files let go, nothing
    moved. --shared: the no-join rule, the shared form in both orders (Valor's look, then each game's own Drive into
    Valor; each game's own Drive into Wisdom, then Wisdom's look): no crash, no join, ours placed from the heap's end
    and the game's below them. --scene: save 11, the warp to the Mysterious Tower (02/19, programs 2/0/1), Valor's look asked, the host
    teleported into the walk-in area (type-3 group, the Pete scene 802); our files watched through the scene to its
    reload. PASS = no crash, no refusal once settled, no file of ours joined another group, nothing of ours moved
    (DLL 'MOVED' lines and the list reads), ours let go at the room's drop, no 'BUG' line;
    the event block (5.2.6 step 4, KH2COOP_EVENT_BLOCK_GUARD=1 set here): each event's block served from ours and
    given back swept (no resource object left in it) with the stack index and the heap's end as at the take. The
    Drive gauge is kept full through the run (DriveKeeper); the joins of the shared
    order are reported (they decide step 4's first question)."""
    host, friend = copies
    q = lambda c, addr: struct.unpack("<Q", peek.read(c.game.h, addr, 8))[0]
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    marks = {c.name: c.mark() for c in copies}
    problems = []
    state = {}
    held = {c.name: set() for c in copies}  # forms whose looks this game holds in our groups (the guard below)

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def no_join(c):
        return any("neg files: no-join rule on" in l for l in c.since(0))

    def placed_right(label):
        """Ours from the heap's end down to our bottom, every other placed file under 10000 below our bottom."""
        for c in copies:
            bottom, end = q(c, c.game.base + 0x29F33F0), q(c, c.game.base + 0x749740)
            wrong = [f"{n} group {g} {pl:#x}+{sz:#x}" for e, n, s, g, pl, sz, f in file_list(c)
                     if pl and ((g < 0 and not bottom <= pl < end) or (0 <= g < 10000 and pl + sz > bottom))]
            out.append(f"  {c.name} places after {label}: {'all right' if not wrong else 'WRONG ' + str(wrong[:6])}")
            if wrong:
                problems.append(f"{c.name} {label}: files out of their place {wrong[:3]}")

    def ours(c):
        return {(e, n): (g, pl, s) for e, n, s, g, pl, sz, f in file_list(c) if g < 0}

    def snap(c):
        return {"end": q(c, c.game.base + 0x749740), "bottom": q(c, c.game.base + 0x29F33F0), "ours": ours(c)}

    def baseline():
        for c in copies:
            state[c.name] = snap(c)

    def verify(label, new_ok=False):
        for c in copies:
            was, now = state[c.name], snap(c)
            moved = [f"{k[1]} {v[1]:#x} -> {now['ours'][k][1]:#x}" for k, v in was["ours"].items()
                     if k in now["ours"] and now["ours"][k][1] != v[1]]
            gone = [k[1] for k in was["ours"] if k not in now["ours"]]
            added = [k[1] for k in now["ours"] if k not in was["ours"]]
            bad = []
            if moved:
                bad.append(f"MOVED {moved}")
            if gone:
                bad.append(f"gone {gone}")
            if added and not new_ok:
                bad.append(f"new negative files {added}")
            if now["end"] != was["end"]:
                bad.append(f"heap end {was['end']:#x} -> {now['end']:#x}")
            if now["bottom"] != was["bottom"] and not new_ok:
                bad.append(f"bottom {was['bottom']:#x} -> {now['bottom']:#x}")
            out.append(f"  {c.name} after {label}: {len(now['ours'])} negative files, "
                       + ("; ".join(bad) if bad else "nothing moved"))
            if bad:
                problems.append(f"{c.name} {label}: {'; '.join(bad)}")
            state[c.name] = now

    def negfiles(c, form, timeout=8):
        m = c.mark()
        bench.debug(c.name, 24, form)
        end = time.time() + timeout
        while time.time() < end:
            lines = [l for l in c.since(m) if "neg files:" in l]
            done = [l for l in lines if "refused" in l or "DECLINED" in l or "files of ours" in l]
            if done:
                return lines, done[-1]
            time.sleep(0.2)
        return [l for l in c.since(m) if "neg files:" in l], ""

    def ask(c, form, expect_join=False):
        lines, result = negfiles(c, form)
        joins = [l for l in lines if "JOINED" in l]
        out.append(f"  {c.name} negfiles {form}: {result[13:260] if result else 'NO ANSWER'}")
        out.extend(f"      {l[13:200]}" for l in lines if "asked in group" in l)
        if "files of ours" not in result:
            problems.append(f"{c.name} negfiles {form}: {result[13:120] or 'no answer'}")
        else:
            held[c.name].add(form)
        if joins and not expect_join:
            problems.append(f"{c.name} negfiles {form}: {len(joins)} files JOINED another group")
        return joins

    def all_loaded(label, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if all(all(v[2] == 3 for v in ours(c).values()) for c in copies):
                return
            time.sleep(0.3)
        problems.append(f"{label}: not all of our files loaded in {timeout} s: "
                        + ", ".join(f"{c.name} {[(k[1], v[2]) for k, v in ours(c).items() if v[2] != 3]}" for c in copies))

    def refill(c):
        sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))

    def drive(c, n):
        form = ENTRY_FORM_59[n]
        if form in held[c.name] and not no_join(c):  # L149: its request would join our entries and crash
            raise bench.Failed(f"refused by the bench: {c.name}'s own Drive into form {form}, whose look its game holds "
                               "in our groups, without the no-join rule")
        refill(c)
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                       ["tap", "cross", "wait", "500"])

    def revert(c):
        c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])

    def room_change(world, room, label, progs=None):
        ms = {c.name: c.mark() for c in copies}
        if progs:
            bench.debug("A", 12, world, room, 0, 0, *progs, 0, fmt="<IiBBBBHHHH")
        else:
            bench.warp(world, room)
        bench.wait(f"both in {world:02X}/{room:02X}", lambda: all(c.room() == (world, room) for c in copies), 90, copies)
        time.sleep(8)
        check(label)
        for c in copies:
            held[c.name].clear()
            new = c.since(ms[c.name])
            drops = [l[13:170] for l in new if "file census: drop from group" in l]
            gone = [l[13:200] for l in new if "neg files: the drop from group" in l]
            bugs = [l[13:200] for l in new if "neg files: BUG" in l]
            left = ours(c)
            # The copy's own Keyblade, asked into ours again once the new room settled (option B), may be there.
            blade = any("copy files: the copy's Keyblade" in l and "files of ours" in l for l in new)
            if blade:
                left = {k: v for k, v in left.items() if not k[1].startswith("obj/W_")}
            out.append(f"  {c.name} {label}: drops {drops[:6]}; {gone or ['ours NOT let go by a drop']}; "
                       f"negative files now {len(left)}")
            if bugs or (state.get(c.name, {}).get("ours") and not gone):
                problems.append(f"{c.name} {label}: ours not let go at the room's drop ({bugs or 'no let-go line'})")
            if left:
                problems.append(f"{c.name} {label}: {len(left)} negative files left in the new room")
            state[c.name] = snap(c)

    keeper = DriveKeeper(copies)  # the infinite Drive gauge (the summons, the Limit, the Drives)
    if a.shared:
        out.append(f"The no-join rule: {[(c.name, no_join(c)) for c in copies]}")
        load_both(copies, a.slot, a.room)
        room_change(0x02, 0x28, "the warp to the gym")
        out.append("Ours first, then our own Drive (Valor's look, then each game's Sora into Valor):")
        for c in copies:
            ask(c, 1)
        all_loaded("Valor's look")
        baseline()
        placed_right("Valor's look")
        for c in copies:
            m = c.mark()
            drive(c, 0)
            time.sleep(5)
            check(f"{c.name}'s Drive into Valor with Valor's look ours")
            made = [l[13:200] for l in c.since(m) if "neg files: the game's" in l]
            out.append(f"  {c.name}: form {form_now(c)}; the game's requests given new entries: {len(made)}")
            out.extend(f"      {l}" for l in made[:10])
            verify(f"{c.name}'s Drive into Valor")
            placed_right(f"{c.name}'s Drive into Valor")
            revert(c)
            time.sleep(5)
            check(f"{c.name}'s Revert")
            verify(f"{c.name}'s Revert")
        out.append("Our own Drive first, then ours (each game's Sora into Wisdom, then Wisdom's look):")
        for c in copies:
            drive(c, 1)
            time.sleep(5)
            check(f"{c.name}'s Drive into Wisdom")
            ask(c, 2)
            all_loaded(f"{c.name}'s Wisdom look")
            verify(f"{c.name}: Wisdom's look asked while in Wisdom", new_ok=True)
            placed_right(f"{c.name}: Wisdom's look while in Wisdom")
            revert(c)
            time.sleep(5)
            check(f"{c.name}'s Revert from Wisdom")
            verify(f"{c.name}'s Revert from Wisdom")
            placed_right(f"{c.name}'s Revert from Wisdom")
        room_change(a.room[0], a.room[1], "the room change")
    elif a.scene:
        out.append("Walk-in scene (save 11, the Mysterious Tower 02/19):")
        load_both(copies, a.slot, a.room)
        arrive = {c.name: c.mark() for c in copies}  # the scene can start on arrival (the Sora lands in the area)
        room_change(0x02, 0x19, "the warp to the tower", progs=(2, 0, 1))
        for c in copies:
            ask(c, 1)
        all_loaded("Valor's look at the tower")
        baseline()
        box = walk_in_box(host)
        out.append(f"  host: walk-in area {box}")
        if not box:
            problems.append("no type-3 walk-in area found in the room")
        else:
            started = lambda c: next((l for l in c.since(arrive[c.name]) if "scene log: cutscene '" in l), None)
            if not started(host):  # not on arrival: walk the host's Sora in
                bench.debug("A", 15, *box["centre"], fmt="<Iifff")
            end = time.time() + 25
            while time.time() < end and not all(started(c) for c in copies):
                time.sleep(0.3)
            for c in copies:  # the scene must play in both games (the friend's through the host's FIRE)
                s = started(c)
                out.append(f"  {c.name}: {s[13:200] if s else 'NO walk-in scene started'}")
                if not s:
                    problems.append(f"the walk-in scene didn't start in {c.name}'s game")
            for k in range(10):  # through the scene, every 2 s, until the room reloads
                time.sleep(2)
                check(f"the scene, {2 * (k + 1)} s")
                if not all(read_u(c, c.game.base + 0x7177B8, 1) for c in copies):
                    out.append(f"  the room reloads after {2 * (k + 1)} s")
                    break
                verify(f"the scene, {2 * (k + 1)} s")
            time.sleep(10)
            check("the scene's reload")
            for c in copies:
                new = c.since(marks[c.name])
                out.append(f"  {c.name}: {[l[13:170] for l in new if 'neg files: the drop' in l or 'neg files: BUG' in l or 'MOVED' in l][:6]}")
                if any("neg files: BUG" in l or ("neg files:" in l and "MOVED" in l) for l in new):
                    problems.append(f"{c.name}: a MOVED or BUG line through the scene")
    elif a.decline:
        out.append("Decline (KH2COOP_NEG_BUDGET_KB=100):")
        load_both(copies, a.slot, a.room)
        room_change(0x02, 0x28, "the warp to the gym")
        baseline()
        for c in copies:
            lines, result = negfiles(c, 1)
            out.append(f"  {c.name} negfiles 1: {result[13:300] if result else 'NO ANSWER'}")
            if "DECLINED" not in result:
                problems.append(f"{c.name}: no decline line")
        time.sleep(2)
        verify("the declined request")
        drive(host, 0)
        time.sleep(5)
        check("the host's Drive after the decline")
        for c in copies:
            left = [(n, g) for e, n, s, g, pl, sz, f in file_list(c) if g == 30000 or g < 0]
            out.append(f"  {c.name} after a layout: negative or 30000 entries {left[:8]}")
        verify("a Drive after the decline")
        revert(host)
        time.sleep(4)
    else:
        for c in copies:  # the Limit abilities before the gym's room load builds the ability bits (gym_limit)
            equip_limits(c)
        out.append("The gate (sent during the warp to the gym):")
        load_both(copies, a.slot, a.room)
        early = {c.name: c.mark() for c in copies}
        bench.warp(0x02, 0x28)
        time.sleep(0.6)
        for c in copies:
            bench.debug(c.name, 24, 1)
        bench.wait("both in the gym", lambda: all(c.room() == (0x02, 0x28) for c in copies), 90, copies)
        time.sleep(8)
        check("the gym")
        for c in copies:  # the spawn step doesn't run during the load: the request waits for it (information only)
            res = [l[13:200] for l in c.since(early[c.name]) if "neg files: form" in l]
            out.append(f"  {c.name} (sent 0.6 s into the warp): {res[-1:] or ['NO ANSWER']}")
            if res and "files of ours" in res[-1]:
                held[c.name].add(1)
        all_loaded("Valor's look (the early request)")
        baseline()
        out.append("The gate (Wisdom's look asked 50 ms after Final's, while Final's reads are in flight):")
        for c in copies:
            gm = c.mark()
            bench.debug(c.name, 24, 5)
            time.sleep(0.05)
            bench.debug(c.name, 24, 2)
            time.sleep(3)
            res = [l[13:200] for l in c.since(gm) if ("neg files: form" in l or "copy files: form" in l
                                                      or "DECLINED: form" in l)
                   and ("refused" in l or "files of ours" in l or "DECLINED" in l)]
            out.append(f"  {c.name}: {res or ['NO ANSWER']}")
            held[c.name].update(f for f, l in zip((5, 2), res) if "files of ours" in l)
            if len(res) != 2 or "refused" not in res[1]:
                problems.append(f"{c.name}: the request during our own reads wasn't refused ({res})")
        all_loaded("Final's look")
        verify("Final's look asked (Valor's must not move)", new_ok=True)
        out.append("Wisdom's look (the next groups up):")
        for c in copies:
            ask(c, 2)
        all_loaded("Wisdom's look")
        verify("Wisdom's look asked (Valor's must not move)", new_ok=True)
        out.append("The game's events:")
        for n in (2, 3):  # not 0, 1, 4: Valor's, Wisdom's and Final's looks are ours, and our own Drive into them
            drive(host, n)    # crashes (L149: its request joins our entries, its Keyblade build finds none in its group)
            time.sleep(5)
            check(f"host Drive entry {n}")
            verify(f"the host's Drive, entry {n} (form {form_now(host)})")
            revert(host)
            time.sleep(5)
            check(f"host Revert {n}")
            verify(f"the host's Revert {n}")
        drive(friend, 2)
        time.sleep(5)
        check("friend Drive")
        verify(f"the friend's Drive, entry 2 (form {form_now(friend)})")
        revert(friend)
        time.sleep(5)
        verify("the friend's Revert")
        host.game.pad.run(MENU_FIRST_WEAPON)
        time.sleep(4)
        check("the menu swap")
        verify("the host's pause-menu Keyblade swap", new_ok=True)  # the friend's copy of the host: its new Keyblade
        host.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(12)
        check("the summon")
        verify("the host's summon")
        host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])
        time.sleep(12)
        check("the dismissal")
        verify("the summon's dismissal")
        host.game.pad.run(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"])
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(12)
        check("the second summon")
        verify("the host's second summon")
        host.game.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "500"])
        time.sleep(12)
        check("the second dismissal")
        verify("the second summon's dismissal")
        bench.debug("A", 19, 0x13E | 1 << 16)
        time.sleep(8)
        check("the Dusk")
        verify("a Dusk spawned (a new enemy type mid-room; invincible, an enemy for the Limit)")
        for c in copies:
            bench.debug(c.name, 20, 1)
        time.sleep(1.5)
        lm = host.mark()
        host.game.pad.run(PAGE2)
        for step in range(4):
            time.sleep(2.5)
            host.game.pad.run(["tap", "cross", "wait", "300"])
        time.sleep(20)
        check("the Limit")
        out.append(f"  host: Limit commands {len(find(host.since(lm), 'type 0x51)'))}")
        verify("the host's Limit")
        for c in copies:
            bench.debug(c.name, 20, 0)
        time.sleep(3)
        verify("the battle state off")
        out.append("The room change (back to 04/06):")
        room_change(a.room[0] if a.room else 0x04, a.room[1] if a.room else 0x06, "the room change")
        out.append("The other order (our Drive into Valor first, then Valor's look):")
        drive(host, 0)
        time.sleep(5)
        check("host Drive before the request")
        joins = ask(host, 1, expect_join=True)
        out.append(f"  host: {len(joins)} files joined the game's own entries")
        revert(host)
        time.sleep(5)
        check("host Revert after the shared request")
    for c in copies:
        new = c.since(marks[c.name])
        moved = [l[13:200] for l in new if "neg files: MOVED" in l or "neg files: the heap's end MOVED" in l
                 or "neg files: our bottom MOVED" in l or "neg files: BUG" in l]
        out.append(f"{c.name}: DLL move/BUG lines {len(moved)}: {moved[:8]}")
        out.append(f"{c.name}: census room summaries {[l[13:220] for l in new if 'file census: room' in l and 'left' in l][:8]}")
        if moved:
            problems.append(f"{c.name}: {len(moved)} DLL move/BUG lines")
        # The event block (5.2.6 step 4): each event's blocks served from ours, given back swept and balanced.
        served = [l[13:230] for l in new if "event block: the event's" in l and "served from ours" in l]
        given = [l[13:260] for l in new if "event block: our" in l and "given back (from" in l]
        bad = [l[13:260] for l in new if "BUG: event block" in l]
        bad += [l for l in given if "as at the take" not in l or " 0 of " not in l]
        out.append(f"{c.name}: event block: {len(served)} served, {len(given)} given back, {len(bad)} bad")
        for l in (served + given)[:8]:
            out.append(f"    {l}")
        if bad or len(served) != len(given):
            problems.append(f"{c.name}: event block: {len(served)} served, {len(given)} given back, bad {bad[:3]}")
        if c is host and not (a.shared or a.scene or a.decline) and not served:
            problems.append("host: no event block served from ours (the summons)")
    keeper.stop()
    out.append(f"Drive gauge keeper: {keeper.report()}")
    out.append(f"problems: {problems or 'none'}")
    return not problems and all(c.alive() and not c.crash for c in copies)


ENTRY_FORM_59 = {0: 1, 1: 2, 2: 3, 3: 4, 4: 5}  # save 59's Drive list: Valor, Wisdom, Limit, Master, Final


def walk_in_box(c):
    """The first type-3 spawn group (a walk-in story area) of the room with its first box: name, id and the box's
    centre in the world (the box test exe+0x421180 transforms the position by the matrix at box+8 and compares it to
    the half-sizes at +0x48, F5; the centre is where that transform gives 0; the convention is read from where the
    translation sits)."""
    base = c.game.base
    count = read_u(c, base + 0x2A10418, 4)
    for i in range(min(count, 64)):
        entry = base + 0x2A10010 + 16 * i
        group = read_u(c, entry + 8, 8)
        if not group:
            continue
        desc = read_u(c, group + 8, 8)
        if not desc or read_u(c, desc, 1) != 3:
            continue
        box = read_u(c, group + 0x10, 8)
        if not box:
            continue
        m = struct.unpack("<16f", peek.read(c.game.h, box + 8, 64))
        half = struct.unpack("<3f", peek.read(c.game.h, box + 0x48, 12))
        if abs(m[12]) + abs(m[13]) + abs(m[14]) >= abs(m[3]) + abs(m[7]) + abs(m[11]):  # local = p M (rows)
            r = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
            t = [m[12], m[13], m[14]]
        else:  # local = M p (columns): the same as rows with M transposed
            r = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
            t = [m[3], m[7], m[11]]
        det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
               + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
        if abs(det) < 1e-9:
            continue
        inv = [[(r[(j + 1) % 3][(i2 + 1) % 3] * r[(j + 2) % 3][(i2 + 2) % 3]
                 - r[(j + 1) % 3][(i2 + 2) % 3] * r[(j + 2) % 3][(i2 + 1) % 3]) / det for j in range(3)]
               for i2 in range(3)]
        centre = [-sum(t[k] * inv[k][j] for k in range(3)) for j in range(3)]
        name = peek.read(c.game.h, entry, 4).split(b"\x00")[0].decode("latin-1")
        return {"name": name, "id": read_u(c, desc + 2, 2), "centre": centre, "half": [round(v, 2) for v in half]}
    return None


def form_room(copies, a, out):
    """TODO 1.104 / 1.106 (two-PC run 2026-10-22: the friend in Wisdom, a room change, then its Sora stuck when
    jumping; the copy's Drive shine back after the room change). Save 59 (04/06): --by (B = the friend) Drives into
    the --pick'th entry (1 = Wisdom: 0 Valor, 1 Wisdom, ...; run with --by B --pick 1), then both warp to 04/09, so both games load
    the room with that form on. The driver then walks, jumps 3 times and walks again; its Sora's spot (actor+0x670),
    height and motion are logged. The other game's copy: its effect listings after the warp (form_look.cpp's 1.5 s
    check) and screenshots. PASS = no crash, the driver still in the form after the warp, it moved and every jump went
    up, and the other game's last listing shows no 0x200 effect still alive (none left or all fading out). Run with
    --env KH2COOP_SORA_SHEET_FIX=0 to see whether our stat-sheet fix is behind the stuck Sora."""
    host, friend = copies
    driver, seer = (friend, host) if a.by == "B" else (host, friend)
    pick = a.pick
    load_both(copies, a.slot, a.room)
    if a.f385:
        return form_room_f385(copies, driver, a, out)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    sheet = read_u(driver, read_u(driver, driver.game.base + 0x2A171C8, 8) + 0x5C0, 8)
    write_mem(driver, sheet + 0x1B0, bytes([100, read_u(driver, sheet + 0x1B2, 1)]))  # a full gauge
    driver.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * pick +
                        ["tap", "cross", "wait", "500"])
    time.sleep(6)
    form = form_now(driver)
    out.append(f"{driver.name} Drove into entry {pick}: form {form}, form / link {sheet_link(driver)}")
    if not form:
        return False
    m2 = {c.name: c.mark() for c in copies}
    if a.same_room:
        return form_room_same(copies, driver, seer, form, out)
    bench.warp(0x04, 0x09)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 90, copies)
    time.sleep(4)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"form_room_after_{c.name}.png"))
    sora = head(driver)
    spot = lambda: struct.unpack("<3f", peek.read(driver.game.h, sora + 0x670, 12))
    out.append(f"after the warp: {driver.name} form {form_now(driver)}, form / link {sheet_link(driver)}, motion "
               f"{bench.motion(driver, sora)}")
    moves, jumps = [], []
    for n in range(2):
        p0 = spot()
        driver.game.pad.run(["stick", "0", "1", "1200", "wait", "400"])
        p1 = spot()
        moves.append(round(((p1[0] - p0[0]) ** 2 + (p1[2] - p0[2]) ** 2) ** 0.5, 1))
        if n == 0:
            for _ in range(3):
                jumps.append(jump_test(driver, sora))
                out.append(f"  jump (start y, highest change, end change) {jumps[-1]}, motion {bench.motion(driver, sora)}")
                bench.screenshot(driver.hwnd, os.path.join(OUT, f"form_room_jump{len(jumps)}_{driver.name}.png"))
    out.append(f"{driver.name} walked {moves} (before / after the jumps), motion {bench.motion(driver, sora)}")
    for c in copies:
        c.lines()
        bench.screenshot(c.hwnd, os.path.join(OUT, f"form_room_end_{c.name}.png"))
        if c.crash or not c.alive():
            raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    for c in copies:
        lines = [l[13:200] for l in c.since(marks[c.name]) if "form look" in l or "stat sheet" in l or
                 "Sora sheet" in l or "CRASH" in l]
        out.append(f"{c.name} form look / sheet lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:40])
    listings = [l for l in seer.since(m2[seer.name]) if "the copy's effects:" in l]
    left = 0
    if listings:  # "... N of the kind that ends with the old body, M fading out"
        import re
        mm = re.search(r"(\d+) of the kind that ends with the old body, (\d+) fading out", listings[-1])
        left = int(mm.group(1)) - int(mm.group(2)) if mm else 0
    out.append(f"{seer.name}: effect listings after the warp {len(listings)}, last: "
               f"{listings[-1][13:160] if listings else '-'}; still on {left}")
    went_up = all(j[1] > 0.5 or j[1] < -0.5 for j in jumps)  # KH2's y points down: a jump is a big change either way
    return (all(c.alive() and not c.crash for c in copies) and form_now(driver) == form and all(moves) and went_up
            and left == 0)


def form_room_same(copies, driver, seer, form, out):
    """form_room --same-room (TODO 1.181): after the Drive, a warp to the room both are in: only the warper's game
    (the host's) reloads, so the other game's copy goes undriven a moment while it wears the form; then the driver
    Reverts. PASS = no crash, the seer's form_look kept the look through the pause ('isn't driven: its look ... kept'),
    then took plain Sora back after the Revert, and never forgot the look in between."""
    m = seer.mark()
    bench.warp(0x04, 0x06)
    time.sleep(8)
    revert_to_sora(driver)
    end = time.time() + 8
    while time.time() < end and read_u(driver, driver.game.base + 0x9ACDD4, 1):
        time.sleep(0.3)
    time.sleep(3)
    lines = [l[13:200] for l in seer.since(m) if "form look:" in l and ("isn't driven" in l or "should look like" in l
                                                                          or "look forgotten" in l or "now looks like" in l)]
    out.append(f"{seer.name}'s form look lines after the warp ({len(lines)}):")
    out.extend(f"    {l}" for l in lines[:20])
    kept = [l for l in lines if "isn't driven" in l and "look 0x54 kept" not in l]
    forgot = [l for l in lines if "look forgotten" in l]
    plain = any("should look like 0x54" in l for l in lines)
    out.append(f"{driver.name} form after the Revert {read_u(driver, driver.game.base + 0x9ACDD4, 1)}; {seer.name}: "
               f"kept in the form {len(kept)}, forgotten {len(forgot)}, back to plain {plain}")
    for c in copies:
        c.lines()
        bench.screenshot(c.hwnd, os.path.join(OUT, f"form_room_same_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and len(kept) > 0 and not forgot and plain


def form_room_f385(copies, driver, a, out):
    """form_room --f385 (TODO 1.104, the host's freeze 2026-10-29 23:34): both warp to 04/12 with its battle programs
    1/11/0, the driver's battle state on, a full gauge and its Drive (--pick), then the room change the game asked for
    then. --tries rounds, each a room change (04/13 door 1 and back to 04/12, in turn; the Drive again if the form
    ended), 3 s, then one jump + air combo: both real freezes came on the first air move after the room change. A
    thread logs every change of the driver's motion (actor+0x180). Freeze = motion 2 after a landing (5) for 2 s and
    an Attack press doesn't change it; then actor+0..0x1000 is saved next to a normal landing's
    (f385_ok/f385_frozen_<n>.bin) and the run stops. PASS = no crash; FROZEN is the repro (the run 'fails')."""
    import threading
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)

    def go(room, door, progs):
        bench.debug("A", 12, 0x04, room, door, 0, *progs, 0, fmt="<IiBBBBHHHH")
        bench.wait(f"both in 04/{room:02X}", lambda: all(c.room() == (0x04, room) and c.game.sora() for c in copies),
                   90, copies)
        time.sleep(3)

    def drive():
        bench.debug(driver.name, 20, 1)  # battle on: the Drive works with or without the room's enemies
        sheet = read_u(driver, head(driver) + 0x5C0, 8)
        write_mem(driver, sheet + 0x1B0, bytes([100, read_u(driver, sheet + 0x1B2, 1)]))  # a full gauge
        driver.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * a.pick +
                            ["tap", "cross", "wait", "500"])
        time.sleep(a.drive_gap)
        return form_now(driver)

    go(0x12, 0, (1, 11, 0))
    out.append(f"{driver.name} Drove into entry {a.pick} in 04/12: form {drive()}")
    if not form_now(driver):
        return False
    marks = {c.name: c.mark() for c in copies}
    sora, seen, stop = [0], [], []

    def sample():
        while not stop:
            m = bench.motion(driver, sora[0])
            if not seen or seen[-1][1] != m:
                seen.append((time.time(), m))
            time.sleep(0.008)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    frozen, ok_dump, drives = None, None, 1
    try:
        for n in range(1, a.tries + 1):
            if not form_now(driver):
                drives += 1
                if not drive():
                    out.append(f"  try {n}: the Drive again failed")
                    break
            room = (0x13, 1, (0xFFFF,) * 3) if n % 2 else (0x12, 0, (1, 11, 0))
            go(*room)
            sora[0] = head(driver)
            start = len(seen)
            # --square: Square pressed through the landing, as you did at the 2026-10-30 freeze (Valor: a lunge attack)
            land = ["tap", "square", "wait", "100"] * 20 if a.square else ["stick", "0", "1", "600"]
            driver.game.pad.run(["tap", "circle", "wait", "250"] + ["tap", "cross", "wait", "250"] * 3 + land +
                                ["wait", "2500"])
            run = [m for _, m in seen[start:]]
            if 5 in run and run[-1] != 2 and ok_dump is None:
                ok_dump = peek.read(driver.game.h, sora[0], 0x1000)
            if 5 in run and run[-1] == 2 and time.time() - seen[-1][0] >= 2:
                driver.game.pad.run(["tap", "cross", "wait", "1000"])
                if bench.motion(driver, sora[0]) == 2:
                    frozen = n
            out.append(f"  try {n} (04/{room[0]:02X}): motions {run[:24]}{' ...' if len(run) > 24 else ''}, form "
                       f"{form_now(driver)}{' FROZEN' if frozen else ''}")
            if frozen or not driver.alive():
                break
    finally:
        stop.append(1)
        t.join(1)
    out.append(f"Drives: {drives}")
    if frozen:
        with open(os.path.join(OUT, f"f385_frozen_{frozen}.bin"), "wb") as fh:
            fh.write(peek.read(driver.game.h, sora[0], 0x1000))
        if ok_dump:
            with open(os.path.join(OUT, "f385_ok.bin"), "wb") as fh:
                fh.write(ok_dump)
        bench.screenshot(driver.hwnd, os.path.join(OUT, f"f385_frozen_{driver.name}.png"))
    for c in copies:
        lines = [l[13:200] for l in c.since(marks[c.name]) if "refused" in l or "stat sheet" in l or "CRASH" in l]
        out.append(f"{c.name}: refused / sheet lines {len(lines)}: {lines[:6]}, alive {c.alive()}, crash {c.crash or '-'}")
    return all(c.alive() and not c.crash for c in copies) and not frozen


def actor_info(c, a):
    """Model, character id (*(decode(actor+8)) + 0x4C), stat block (+0x5C0) with HP/max and which stat slot it is
    (exe+0x2A23598 - k * 0x278, k = 0 Sora), team, weapons (+0xD60 / +0xD68) models."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    i32 = lambda addr: struct.unpack("<i", peek.read(h, addr, 4))[0]
    name = dict(bench.actors(c)).get(a, "?")
    rec = bench.decode(c, struct.unpack("<I", peek.read(h, a + 8, 4))[0])
    char = struct.unpack("<H", peek.read(h, rec + 0x4C, 2))[0] if rec else -1
    stats = q(a + 0x5C0)
    slot = next((k for k in range(-2, 12) if stats == base + 0x2A23598 - k * 0x278), None)
    hp = (i32(stats), i32(stats + 4)) if stats else None
    weapons = []
    for off in (0xD60, 0xD68):
        w = q(a + off)
        weapons.append(bench.model_of(c, w) if w else "-")
    return (f"{name} exe+0x{a - base:X}: character {char}, stats exe+0x{stats - base:X} (slot {slot}) HP {hp}, "
            f"team {i32(a + 0x4DC)}, weapons {weapons}")


def inspect(copies, a, out):
    """TODO 3.2: what the Sora copy uses in each game (character id, stat block, weapons), then the pause menu's
    pages on the host (screenshots inspect_menu_<n>.png)."""
    load_both(copies, a.slot, a.room)
    time.sleep(3)
    h = copies[0].game.h
    for c in copies:
        base = c.game.base
        sora = struct.unpack("<Q", peek.read(c.game.h, base + 0x2A171C8, 8))[0]
        party = [struct.unpack("<Q", peek.read(c.game.h, base + 0x2A239B0 + 8 * i, 8))[0] for i in range(2)]
        copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
        out.append(f"{c.name}:")
        for who, x in (("our Sora", sora), ("companion 1", party[0]), ("companion 2", party[1]), ("Sora copy", copy)):
            if x:
                try:
                    out.append(f"   {who:12s} {actor_info(c, x)}")
                except OSError as e:
                    out.append(f"   {who:12s} read failed: {e}")
    host = copies[0]
    steps = [("pause", ["tap", "start", "wait", "1500"])] +             [(f"down{i}", ["tap", "down", "wait", "500"]) for i in range(1, 7)]
    for name, keys in steps:
        host.game.pad.run(keys)
        time.sleep(0.6)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"inspect_menu_{name}.png"))
    return all(c.alive() and not c.crash for c in copies)


def sora_hp(c):
    """Our Sora's HP in that game, or None."""
    try:
        sora = struct.unpack("<Q", peek.read(c.game.h, c.game.base + 0x2A171C8, 8))[0]
        stats = struct.unpack("<Q", peek.read(c.game.h, sora + 0x5C0, 8))[0] if sora else 0
        return struct.unpack("<i", peek.read(c.game.h, stats, 4))[0] if stats else None
    except OSError:
        return None


def item(copies, a, out):
    """TODO 1.46: the host uses the first item on itself twice (Items, the first item, the first target). With
    KH2COOP_TEST_BAD_ITEM=1 the first use gets an item with no table row: the guard skips it (with
    KH2COOP_ITEM_GUARD=0 the game crashes at exe+0x3F6C94 ~0.5 s later); the second heals as normal."""
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    load_both(copies, a.slot, a.room)
    bench.warp(0x04, 0x09)  # away from the save point (it refills HP within a second)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 60, copies)
    time.sleep(12)
    use = ["tap", "down", "wait", "300"] * 2 + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "800",
                                                 "tap", "cross", "wait", "300"]
    ok = True
    for n in range(2):
        bench.debug("A", 0, 10)
        time.sleep(1.5)
        before = sora_hp(host)
        host.game.pad.run(use)
        time.sleep(4)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"item_{n}_A.png"))
        after = sora_hp(host)
        out.append(f"use {n + 1}: host HP {before} -> {after}, alive A {host.alive()} B {friend.alive()}")
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                out.append(f"{c.name}: {c.crash or 'closed'}")
                return False
        if n == 1:
            ok &= after is not None and before is not None and after > before
    hl = [l for l in host.since(marks["A"]) if "item" in l.lower()]
    for l in hl[:20]:
        out.append("   " + l[:220])
    skipped = find(hl, "no item table row")
    test = any("KH2COOP_TEST_BAD_ITEM=1" in e for e in a.env)
    out.append(f"guard skips: {len(skipped)} (test bad item {'on' if test else 'off'})")
    ok &= bool(skipped) == test
    return ok and all(c.alive() and not c.crash for c in copies)


def heal(copies, a, out):
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0x07, 0x03, 59)  # the Twin Lords fight: companions only weigh heals in a fight
    bench.wait("both in 07/03", lambda: all(c.room() == (0x07, 0x03) for c in copies), 60, copies)
    time.sleep(12)
    start = {c.name: c.mark() for c in copies}

    def friend_low():
        bench.debug("B", 16, a.low)
        time.sleep(0.5)
        bench.debug("A", 3, 1)  # a hit on the friend's copy, forwarded: the friend's Sora drops to the hold

    def friend_down():
        bench.debug("A", 3, 9999)

    def host_low():
        bench.debug("B", 16, 0)
        bench.debug("A", 16, a.low)
        time.sleep(0.5)
        bench.debug("A", 0, 1)

    def host_down():
        # never both down (= Game Over): wait for the friend's get-up first
        bench.wait("the friend up again", lambda: find(friend.since(start["B"]), "downed: getting up") or
                   find(friend.since(start["B"]), "reached our Sora: getting up"), 45, copies)
        time.sleep(3)
        bench.debug("A", 0, 9999)

    phases = [("friend_low", friend_low, 40), ("friend_down", friend_down, 36), ("host_low", host_low, 40),
              ("host_down", host_down, 36)]
    keys = ["heal trace:", "heal: ", "healed our Sora", "downed: ", "debug: low-HP", "the peer's hit"]
    for name, act, secs in phases:
        marks = {c.name: c.mark() for c in copies}
        act()
        hp = {c.name: [] for c in copies}
        for k in range(secs // 2):
            time.sleep(2)
            for c in copies:
                hp[c.name].append(sora_hp(c))
                if k % 3 == 0:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"heal_{name}_{c.name}_{k // 3}.png"))
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    out.append(f"{name}: {c.name} {c.crash or 'closed'}")
                    return False
        out.append(f"== {name}")
        for c in copies:
            lines = c.since(marks[c.name])
            trace = [l for l in lines if "heal trace:" in l]
            def count(text):
                return len([l for l in trace if text in l])
            heals = [l[13:150] for l in lines if ("heal: " in l or "healed our Sora" in l or "reached our Sora" in l)
                     and "heal trace" not in l]
            ups = [l[13:120] for l in lines if "downed: getting up" in l or "reached our Sora: getting up" in l]
            out.append(f"{c.name}: HP every 2 s {hp[c.name]}")
            out.append(f"{c.name}: trace lines: needs help our Sora {count('needs help our Sora')}, the Sora copy "
                       f"{count('needs help the Sora copy')}, itself {count('needs help itself')}; items used "
                       f"{count(' uses item ')}; mirrored brain lines {count('mirrored')}")
            for l in [l for l in trace if " in 10 s:" in l][-3:]:
                out.append(f"   {l[13:260]}")
            out.append(f"{c.name}: heal lines {len(heals)}: {heals[:6]}")
            def n(text):
                return len([l for l in lines if text in l])
            got_cure, got_item = n("the other player's Cure healed our Sora"), n("the other player's item healed our Sora")
            out.append(f"{c.name}: companion heal (TODO 1.58): looks after the copy {n('looks after the other player')}, "
                       f"Cure picked {n('picks Cure for the Sora copy')}, HP item picked "
                       f"{n('picks an HP item for the Sora copy')}, Cure on the copy sent "
                       f"{n('a Cure on the Sora copy heals the other player')}, item on the copy sent "
                       f"{n('on the Sora copy by a companion')}; healed by the other player's Cure {got_cure} / "
                       f"item {got_item}; the other player's heal got us up {n('reached our Sora: getting up')}")
            out.append(f"{c.name}: get-ups {ups[:2]}")
    for c in copies:
        with open(os.path.join(OUT, f"heal_{c.name}.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(l for l in c.since(start[c.name]) if any(k in l for k in keys)) + "\n")
    return all(c.alive() and not c.crash for c in copies)


def menu(copies, a, out):
    load_both(copies, a.slot, a.room)
    host = copies[0]
    page2 = ["tap", "down", "wait", "300"] * 3 + ["tap", "left", "wait", "1000"]  # Drive -> the Summon/Limit page
    shots = [("page2", page2), ("limit_open", ["tap", "cross", "wait", "1500"]), ("limit_back", ["tap", "circle", "wait", "800"]),
             ("summon", ["tap", "up", "wait", "500"]), ("summon_open", ["tap", "cross", "wait", "1500"])]
    for name, keys in shots:
        if keys:
            host.game.pad.run(keys)
        time.sleep(1)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"menu_{name}.png"))
        out.append(f"screenshot menu_{name}.png")
    return True


def file_list(c):
    """The game's file list (exe+0x29F33C8, next = decode(+0x70)): [(entry, name, state +1, group +0x48, place +0x58,
    size +0x50, flags +0x44)]."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    out, r, seen = [], q(base + 0x29F33C8), set()
    while r and r not in seen and len(out) < 4000:
        seen.add(r)
        raw = peek.read(h, r, 0x78)
        name = raw[4:0x44].split(b"\x00")[0].decode("latin-1")
        flags, group = struct.unpack_from("<Ii", raw, 0x44)
        size, place = struct.unpack_from("<QQ", raw, 0x50)
        out.append((r, name, raw[1], group, place, size, flags))
        r = bench.decode(c, struct.unpack_from("<I", raw, 0x70)[0])
    return out


def blade_files(c, w, files):
    """A weapon actor's object, group (+0xC0C) and the list entries its set (+0x920/+0x928/+0x930) and motion
    controller (+0x158) point into."""
    h = c.game.h
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    i32 = lambda addr: struct.unpack("<i", peek.read(h, addr, 4))[0]
    parts = [f"weapon exe+0x{w - c.game.base:X} {bench.model_of(c, w)} object {i32(w + 0x914):#x} group {i32(w + 0xC0C)}"]
    for off in (0x920, 0x928, 0x930, 0x158):
        p = q(w + off)
        hit = next(((r, n, s, g) for r, n, s, g, pl, sz, f in files if pl and pl <= p < pl + max(sz, 1)), None)
        parts.append(f"+{off:#x} -> " + (f"{hit[1]} (entry exe+0x{hit[0] - c.game.base:X}, group {hit[3]}, state {hit[2]})"
                                         if hit else ("-" if not p else f"{p:#x} (no entry)")))
    return parts


def keyblade(copies, a, out):
    """TODO 3.3 / 1.110 research (read only): in each game, the Keyblade items in the save (Sora's record +0/+2 at
    exe+0x9ABDA0, the 10 form records' first u16 at +0xE04 + 0x38 n), our Sora's and the copy's weapon actors with
    their object, group and the file entries they use, and every listed weapon file (W_*) with its group."""
    load_both(copies, a.slot, a.room)
    time.sleep(3)
    for c in copies:
        h, base = c.game.h, c.game.base
        q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
        u16 = lambda addr: struct.unpack("<H", peek.read(h, addr, 2))[0]
        rec = base + 0x9ABDA0
        out.append(f"{c.name}: Sora's record weapons {u16(rec):#x} {u16(rec + 2):#x}; form records' items "
                   + " ".join(f"{u16(rec + 0xE04 + 0x38 * n):#x}" for n in range(10)))
        files = file_list(c)
        sora = q(base + 0x2A171C8)
        copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
        for who, x in (("our Sora", sora), ("Sora copy", copy)):
            if not x:
                out.append(f"   {who}: none")
                continue
            rec_h = struct.unpack("<I", peek.read(h, x + 8, 4))[0]
            entry = bench.decode(c, rec_h)
            kind = u16(entry + 0x4E) if entry else -1
            out.append(f"   {who} exe+0x{x - base:X} {bench.model_of(c, x)}: kind {kind:#x}, form byte +0x57 "
                       f"{peek.read(h, entry + 0x57, 1)[0] if entry else -1}")
            for k, off in enumerate((0xD60, 0xD68)):
                w = q(x + off)
                if w:
                    for line in blade_files(c, w, files):
                        out.append(f"      [{k}] {line}")
        out.append(f"   weapon files in the list ({len(files)} entries):")
        for r, n, s, g, pl, sz, f in files:
            if "/W_" in n or n.startswith("W_"):
                out.append(f"      exe+0x{r - base:X} {n} group {g} state {s} flags {f:#x} place {pl:#x} size {sz:#x}")
    return all(c.alive() and not c.crash for c in copies)


def saferead(copies, a, out):
    """docs/CRASH_RISK_REVIEW.md section 4 (safe_read.cpp, crash_log.cpp): both games in a room, then the saferead
    test command in each (100 faulting reads landed as false, our guard page and another thread's stack guard page
    put back, that thread's stack growing past it); both games must still run 20 s later. Also prints the start-up
    lines of the crash logger's measurements (clr.dll, the frame thread, other handlers added)."""
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    for c in copies:
        bench.debug(c.name, 23, 0)
        time.sleep(3)
    time.sleep(20)
    ok = True
    for c in copies:
        start = [l for l in c.all[:marks[c.name]] if "crash logger" in l or "safe read:" in l][:12]
        new = c.since(marks[c.name])
        result = [l for l in new if "saferead test" in l and "expected" not in l]
        bugs = find(new, "BUG: safe read")
        good = c.alive() and not c.crash and bool(find(new, "saferead test: PASS")) and not bugs
        ok &= good
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, unexpected guard hits {len(bugs)}")
        out += [f"   {l}" for l in start + result + find(new, "safe read:") + find(new, "fault in our DLL")]
    return ok


def ended_loop(copies, a, out):
    """TODO 5.2d (world_census.cpp): both games in the Twin Lords' fight (save 20 and --warp 7,3,3B by default; the Lords
    can't die), 25 s there, then the endedloop test command in each (0/44 and 0/85 on records the test makes: one with no part not
    run, 0/85 only the kill's stop, one playing run, no record not run; then a Potion's sparkle on our Sora through
    the hooked 0/44 and 0/85: run while it plays, both skipped once it's stopped).
    --slot / --warp W,R,P (hex) pick another room whose scripts end loops. PASS = both games still
    run 10 s later, each logged 'ended loop test: ... PASS', the room's scripts ended loops in the host's game before
    the test (its 10 s trap counts; the friend's bound enemies don't think), and none of them was skipped (a skip there is listed: a real ended effect, or a fault)."""
    load_both(copies, a.slot, a.room)
    if a.warp:  # W,R,P in hex: a fight whose scripts end loops (save 20, 7,3,3B: the Twin Lords)
        w = [int(x, 16) for x in a.warp.split(",")]
        bench.warp(w[0], w[1], w[2] if len(w) > 2 else 0xFFFF)
        bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) for c in copies), 90, copies)
        time.sleep(4)
    marks0 = {c.name: c.mark() for c in copies}
    time.sleep(25)  # the room's own loop ends
    marks = {c.name: c.mark() for c in copies}
    for c in copies:
        bench.debug(c.name, 28, 0)
        time.sleep(2)
    time.sleep(10)
    ok = True
    for c in copies:
        before = c.all[marks0[c.name]:marks[c.name]]
        new = c.since(marks[c.name])
        loops = sum(int(m.group(1)) for l in before for m in [re.search(r"effect_loop_end(?:_kill)? (\d+)", l)] if m)
        skipped_before = find(before, "already run out")
        result = find(new, "ended loop test:")
        # the room's own loop ends: the host's game only (one brain: the friend's bound enemies don't think)
        good = (c.alive() and not c.crash and any("PASS" in l for l in result) and not skipped_before
                and (loops > 0 or c.name != "A"))
        ok &= good
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}, the room's own loop ends before the test "
                   f"{loops}, skipped before the test {len(skipped_before)}")
        out += [f"   {l[:240]}" for l in skipped_before[:5] + result + find(new, "already run out")]
    return ok


def equip_menu(copies, a, out):
    """Research for the blade bench: the host opens the pause menu and presses through it (cross, then the --keys
    list), a screenshot after each press (equip_<n>.png)."""
    host = copies[0]
    load_both(copies, a.slot, a.room)
    keys = ["start"] + (a.keys.split(",") if a.keys else ["cross", "cross", "cross"])
    for n, k in enumerate(keys):
        host.game.pad.run(["tap", k, "wait", "1200"])
        time.sleep(0.5)
        bench.screenshot(host.hwnd, os.path.join(OUT, f"equip_{n}.png"))
        out.append(f"{n}: {k}")
    return all(c.alive() and not c.crash for c in copies)


SORA_RECORD = 0x9ABDA0  # Sora's character record: u16 +0 the Keyblade item (VERIFIED_OFFSETS L121, F332)


def copy_blade_state(c, label, out):
    """Our Sora's and the copy's Keyblades in one game: model, group, the files they use."""
    h, base = c.game.h, c.game.base
    q = lambda addr: struct.unpack("<Q", peek.read(h, addr, 8))[0]
    files = file_list(c)
    sora = q(base + 0x2A171C8)
    copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
    models = {}
    out.append(f"   {c.name} {label}: record item {read_u(c, base + SORA_RECORD, 2):#x}")
    for who, x in (("our Sora", sora), ("copy", copy)):
        w = q(x + 0xD60) if x else 0
        models[who] = bench.model_of(c, w) if w else None
        if not w:
            out.append(f"      {who}: no Keyblade")
            continue
        parts = blade_files(c, w, files)
        out.append(f"      {who}: {parts[0]}")
        out.extend(f"         {p}" for p in parts[1:])
    return models


MENU_FIRST_WEAPON = (["tap", "start", "wait", "1500"] + ["tap", "cross", "wait", "1000"] * 3 +
                     ["tap", "cross", "wait", "1500"] + ["tap", "circle", "wait", "800"] * 4)  # Items, Sora, Weapons,
# the first Keyblade in the list (save 59: Kingdom Key while Ultima Weapon is equipped), then back out of the menu


def blade(copies, a, out):
    """TODO 3.3 / 1.110 (copy_blade.cpp), every change through the real pause menu (Items, Sora, Weapons, the first
    in the list, then out): (1) the friend changes Keyblade: the host's copy of the friend takes it (its files in group
    5099); (2) the host changes to the same one (both carry it; the friend's copy of the host shares the friend's own
    files); (3) the host changes again: the game frees the old Keyblade's files at once, which the host's copy of the
    friend shares: no crash, the copy keeps a Keyblade; (4) the friend's form Keyblades (Valor's and Final's records
    written: the game builds a form's second Keyblade from its own new files) on the host's copy during the friend's
    Drives. The other player attacks after each change. (Writing the main Keyblade into the record without the menu
    made the game's own Drive crash, exe+0x39CC40: the form body takes the record's Keyblade from our Sora's files.)
    --forms-only: only step 4. Run with --env KH2COOP_COPY_BLADE=0 for the old behaviour (1.110: step 3 crashed)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    ok = True

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def attack(c):
        c.game.pad.run(["tap", "cross", "wait", "350"] * 4)

    def shots(step):
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"blade_{step}_{c.name}.png"))

    def menu_change(c, label):
        c.game.pad.run(MENU_FIRST_WEAPON)
        time.sleep(3)
        check(f"{label}, after the menu")
        out.append(f"   {c.name}'s item now {read_u(c, c.game.base + SORA_RECORD, 2):#x}, menu byte "
                   f"{read_u(c, c.game.base + 0x7435D0, 1):#x}")
        other = friend if c is host else host
        attack(other)
        attack(c)
        time.sleep(1)
        check(label)
        shots(label)

    if a.forms_only:
        return blade_forms(copies, host, friend, marks, out, check, attack, shots) and ok
    out.append("start:")
    before = {c.name: copy_blade_state(c, "start", out) for c in copies}
    out.append("step 1: the friend changes Keyblade in the pause menu")
    menu_change(friend, "1")
    m = copy_blade_state(host, "after step 1", out)
    if m["copy"] == before["A"]["copy"]:
        out.append("   FAIL: the host's copy of the friend kept its Keyblade")
        ok = False
    out.append("step 2: the host changes Keyblade in the pause menu (to the friend's)")
    menu_change(host, "2")
    for c in copies:
        if not copy_blade_state(c, "after step 2", out)["copy"]:
            out.append(f"   FAIL: {c.name}'s copy has no Keyblade")
            ok = False
    out.append("step 3: the host changes Keyblade again (its old files, shared by its copy of the friend, freed)")
    menu_change(host, "3")
    for c in copies:
        if not copy_blade_state(c, "after step 3", out)["copy"]:
            out.append(f"   FAIL: {c.name}'s copy has no Keyblade")
            ok = False
    return blade_forms(copies, host, friend, marks, out, check, attack, shots) and ok


def menu_weapon(n, last_wait):
    """Pause menu: Items, Sora, Weapons, the n-th Keyblade in the list, then out (the last wait after closing)."""
    return (["tap", "start", "wait", "1500"] + ["tap", "cross", "wait", "1000"] * 3 +
            ["tap", "down", "wait", "400"] * (n - 1) + ["tap", "cross", "wait", "1500"] +
            ["tap", "circle", "wait", "800"] * 3 + ["tap", "circle", "wait", str(last_wait)])


def blade_restart(copies, a, out):
    """copy_blade.cpp's seq window: the friend equips another Keyblade (--peer-blade n, default 2), 40 s pass (the host's
    newest friend seq ~20), then the friend's game closes, starts again and loads the save (its own Keyblade back, its
    seq from 1). PASS = the host takes the friend's Keyblade change within 15 s of the friend's load (before, it waited
    until the new seq passed the old one), the same in the other table (peer sora), no crash."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    m = host.mark()
    friend.game.pad.run(menu_weapon(a.peer_blade or 2, 3000))
    bench.wait("the host takes the friend's menu Keyblade",
               lambda: find(host.since(m), "copy blade: the other player's Keyblade"), 30, copies)
    menu = find(host.since(m), "copy blade: the other player's Keyblade")[-1]
    time.sleep(40)
    m = host.mark()
    friend.proc.kill()
    bench.wait("the host sees the link lost", lambda: find(host.since(m), "link watch: connection lost"), 20, [host])
    time.sleep(2)
    bench.debug("A", 10, 3)  # play on with Donald and Goofy
    bench.wait("playing on", lambda: find(host.since(m), "link watch: playing on with Donald and Goofy"), 20, [host])
    new = bench.launch(names=("B",), keep_running=True)[0]
    copies[1] = new
    m = host.mark()
    scene.load_slot(new.game, a.slot, a.room)
    loaded = time.time()
    took = lambda: find(host.since(m), "copy blade: the other player's Keyblade")
    while time.time() - loaded < 15 and not took():
        time.sleep(0.5)
    blade = took()
    sora = find(host.since(m), "peer sora: the other Sora's record changed")
    out.append(f"before the restart: {menu[13:150]}")
    out.append(f"after the friend's load ({time.time() - loaded:.0f} s): {blade[0][13:150] if blade else 'nothing'}")
    out.append(f"peer sora after the load: {sora[0][13:150] if sora else 'nothing'}")
    return bool(blade) and bool(sora) and host.alive() and not host.crash and not new.crash


def blade_swap(copies, a, out):
    """TODO 3.3: your crash of 2026-10-25 21:59 (friend kh2coop_22044). The friend changes Keyblade in the menu (the
    friend's copy of the host then keeps the host's Ultima Weapon in its own files, group 5099); the host changes to
    one the friend doesn't carry (the second in the list) and runs off the moment its menu closes, so the friend's game
    swaps a visible, moving copy's Keyblade from files of ours to new files of ours. Then both attack. Before the
    host's change only the friend's game goes into the forced battle state (debug battle on; in battle Start opens
    the battle menu, no Keyblades), so its copy has the Keyblade out, as in your fight. --pick <n>: the host's pick is
    the n-th in the list (default 2)."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    ok = True

    def check(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    pick = menu_weapon
    out.append("start:")
    copy_blade_state(friend, "start", out)
    out.append("step 1: the friend changes Keyblade in the pause menu (the first in its list)")
    friend.game.pad.run(pick(1, 800))
    time.sleep(3)
    check("step 1")
    out.append(f"   friend's item now {read_u(friend, friend.game.base + SORA_RECORD, 2):#x}")
    copy_blade_state(friend, "after step 1", out)
    if a.drive:  # as in your run: the friend's own Valor and Revert first (the copy's Keyblade files move)
        out.append("step 1b: the friend Drives into Valor, attacks, Reverts")
        sheet = read_u(friend, read_u(friend, friend.game.base + 0x2A171C8, 8) + 0x5C0, 8)
        write_mem(friend, sheet + 0x1B0, bytes([100, read_u(friend, sheet + 0x1B2, 1)]))
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
        time.sleep(6)
        friend.game.pad.run(["tap", "cross", "wait", "350"] * 6)
        check("step 1b, Valor")
        friend.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])  # Revert
        time.sleep(5)
        check("step 1b, after the Revert")
    bench.debug(friend.name, 20, 1)
    time.sleep(2)
    out.append(f"battle state (host, friend): {[battle_state(c) for c in copies]}")
    n = a.pick or 2

    def copy_flags():
        sora = read_u(friend, friend.game.base + 0x2A171C8, 8)
        copy = next((x for x, nm in bench.actors(friend) if nm.startswith("P_EX100") and x != sora), 0)
        w = read_u(friend, copy + 0xD60, 8) if copy else 0
        return f"{read_u(friend, w + 0x124, 4):#x}" if w else "no Keyblade"

    out.append(f"   the friend's copy's Keyblade flags +0x124 (0x400 away, 0x700 hidden): {copy_flags()}")
    out.append("step 2: the host attacks (its copy draws the Keyblade), changes to the "
               f"{n}-th in its list at once and runs off as the menu closes")
    host.game.pad.run(["tap", "cross", "wait", "350"] * 4)
    out.append(f"   after the attacks: {copy_flags()}")
    host.game.pad.run(pick(n, 0))
    host.game.run(90, 1500)
    host.game.run(270, 1500)
    time.sleep(1)
    check("step 2")
    item = read_u(host, host.game.base + SORA_RECORD, 2)
    out.append(f"   host's item now {item:#x}, friend's {read_u(friend, friend.game.base + SORA_RECORD, 2):#x}")
    m = copy_blade_state(friend, "after step 2", out)
    if not m["copy"]:
        out.append("   FAIL: the friend's copy has no Keyblade")
        ok = False
    for c in copies:
        c.game.pad.run(["tap", "cross", "wait", "350"] * 6)
    time.sleep(2)
    check("attacks")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"blade_swap_{c.name}.png"))
        lines = [l[13:220] for l in c.since(marks[c.name]) if "copy blade" in l or "CRASH" in l]
        out.append(f"{c.name} lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:30])
    return ok and all(c.alive() and not c.crash for c in copies)

def blade_forms(copies, host, friend, marks, out, check, attack, shots):
    """The blade bench's step 4: the friend's own form Keyblades (Master's, Valor's and Final's records) on the host's
    copy. Master first, with a Keyblade no earlier step loaded: the host's copy must wait for its files before it wears
    Master (TODO 1.185: worn without its second Keyblade, Master's appear effects crashed the game)."""
    ok = True
    # Master is offered only after a room reload (a world ally in the save's party, F44): the warp fx_sync1 makes.
    bench.warp(0x04, 0x06)
    time.sleep(4)
    bench.wait("both back in 04/06", lambda: all(c.room() == (0x04, 0x06) and c.game.sora() for c in copies), 90, copies)
    time.sleep(8)
    forms = ((4, 0x2A, "Master"), (1, 0x2B, "Valor"), (5, 0x29, "Final"))  # form byte = form record, item
    for rec, item, _ in forms:
        write_mem(friend, friend.game.base + SORA_RECORD + 0xE04 + 0x38 * (rec - 1), struct.pack("<H", item))
    time.sleep(3)
    for rec, item, name in forms:
        out.append(f"step 4: the friend Drives into {name} (its {name} Keyblade set to {item:#x})")
        got = drive_form(friend, [rec])
        if got != rec:
            out.append(f"   FAIL: the friend's Drive took form {got}, not {name}")
            ok = False
            continue
        time.sleep(6)
        attack(friend)
        time.sleep(1)
        check(f"step 4, {name}")
        shots(f"4{name}")
        h, base = host.game.h, host.game.base
        sora = read_u(host, base + 0x2A171C8, 8)
        copy = next((x for x, n in bench.actors(host) if n.startswith("P_EX100") and x != sora), 0)
        w1 = read_u(host, copy + 0xD68, 8) if copy else 0
        out.append(f"   host's copy: body {bench.model_of(host, copy) if copy else '-'}, second Keyblade "
                   f"{bench.model_of(host, w1) if w1 else 'none'}")
        if not w1:
            out.append(f"   FAIL: no second Keyblade on the host's copy in {name}")
            ok = False
        revert_to_sora(friend)
        time.sleep(5)
        check(f"step 4, after {name}'s Revert")
    for c in copies:
        lines = [l[13:220] for l in c.since(marks[c.name])
                 if "copy blade" in l or "second Keyblade" in l or "CRASH" in l or "form Keyblade" in l or
                 "weapon slot" in l or "form appear" in l or "now looks like" in l]
        out.append(f"{c.name} lines ({len(lines)}):")
        out.extend(f"    {l}" for l in lines[:50])
    return ok and all(c.alive() and not c.crash for c in copies)


# The crash review's section 6 (5.2.3 / 5.2.4, copy_blade.cpp): Keyblade rebuilds while the copy moves and fights.
BLADE_BAD = ("BUG:", "given up", "not seen freed", "neg files: MOVED")


def blade_check(copies, label):
    for c in copies:
        c.lines()
        if c.crash or not c.alive():
            raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")


def drive_into(c, n):
    sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
    write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))  # a full Drive gauge
    c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * n +
                   ["tap", "cross", "wait", "500"])


def revert_form(c):
    c.game.pad.run(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])


# The Forms page's commands (bench menu_cursor 2026-10-29, save 59): form byte (exe+0x9ACDD4) -> command.
FORM_CMD = {1: 0x6, 2: 0x7, 3: 0x2A1, 4: 0xB, 5: 0xC}


def menu_page(c):
    """The command menu's page shown: (its address, its commands, the cursor's index +0x74, the command under it +0x78;
    bench menu_cursor 2026-10-29: the cursor wraps and stays where it was left)."""
    off, cmds = menu_cmds(c)
    if not off:
        return 0, [], -1, 0
    p = c.game.base + CMD_MENU + off
    return p, cmds, read_u(c, p + 0x74), read_u(c, p + 0x78)


def cursor_to(c, ok):
    """Down until ok(index, command, count) holds for the cursor on the page shown, at most once round the page."""
    for _ in range(9):
        p, cmds, i, cmd = menu_page(c)
        if p and ok(i, cmd, len(cmds)):
            return True
        c.game.pad.run(["tap", "down", "wait", "300"])
    return False


def drive_form(c, forms):
    """Drives into the first of forms (form bytes, 1 Valor .. 5 Final) the party allows, by the command under the
    cursor, not a count of downs: a world ally in Donald's place leaves Wisdom, Master and Final out and the cursor
    lands elsewhere (bench room sweep 2026-10-29, Beast's Castle). Returns the form taken, 0 none."""
    sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
    write_mem(c, sheet + 0x1B0, bytes([100, read_u(c, sheet + 0x1B2, 1)]))  # a full Drive gauge
    if not cursor_to(c, lambda i, cmd, n: i == n - 1):  # the top page's last entry: Drive
        return 0
    c.game.pad.run(["tap", "cross", "wait", "800"])
    for f in forms:
        if not cursor_to(c, lambda i, cmd, n: cmd == FORM_CMD[f]):
            continue
        c.game.pad.run(["tap", "cross", "wait", "500"])
        end = time.time() + 6
        while time.time() < end and not read_u(c, c.game.base + 0x9ACDD4, 1):
            time.sleep(0.2)
        if read_u(c, c.game.base + 0x9ACDD4, 1):
            return read_u(c, c.game.base + 0x9ACDD4, 1)
    if menu_page(c)[0] and len(menu_page(c)[1]) == 5:  # still on the Forms page: back to the top
        c.game.pad.run(["tap", "circle", "wait", "300"])
    return 0


def revert_to_sora(c):
    """Revert: the top page's last entry in a form, then its confirm."""
    if cursor_to(c, lambda i, cmd, n: i == n - 1):
        c.game.pad.run(["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])


def blade_report(copies, marks, out):
    """Each game's Keyblade ends and builds, its quiet waits and the bad lines; True when no crash and no bad line."""
    ok = True
    for c in copies:
        new = c.since(marks[c.name])
        ends = [l for l in new if "copy blade: the copy's Keyblade" in l and " ended (" in l]
        builds = sum("copy blade: the copy's Keyblade" in l and " built: " in l for l in new)
        stopped = sum(", 0 of its effects stopped" not in l for l in ends)
        seconds = sum("second Keyblade" in l and " built: " in l for l in new)
        waits = [l[13:220] for l in new if "waits for a quiet moment" in l or "built anyway" in l or "is quiet" in l]
        bad = [l[13:220] for l in new if any(b in l for b in BLADE_BAD)]
        out.append(f"{c.name}: alive {c.alive()}, crash {c.crash or '-'}; the copy's Keyblade ended {len(ends)} "
                   f"(effects stopped at {stopped}), built {builds}, second Keyblades built {seconds}, quiet-wait "
                   f"lines {len(waits)}, bad lines {len(bad)}")
        out.extend(f"    BAD {l}" for l in bad[:12])
        out.extend(f"    {l}" for l in waits[:20])
        ok &= c.alive() and not c.crash and not bad
    return ok


def blade_combat(copies, a, out):
    """Section 6's blade_combat (save 59): only the friend's game in the forced battle state (the host's copy has its
    Keyblade out there; the host stays out of battle so Start opens its pause menu). --pick rounds (default 4), each:
    the host swaps Keyblade (the 1st / 2nd in its list in turn) and attacks the moment its menu closes; the friend's
    game builds the host's copy's Keyblade 1 s after the change (KH2COOP_TEST_BLADE_DELAY_MS=1000), so mid-swing, and
    5.2.3 makes it wait for a quiet moment; then the friend Drives and Reverts while the host attacks the air; then a
    room change with the host attacking on arrival. PASS = no crash, no BLADE_BAD line, a build in the friend's game for
    every swap, at least one quiet wait. Not covered: the host grabbed, downed or in a Limit during a swap."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug(friend.name, 20, 1)
    time.sleep(2)
    out.append(f"battle state (host, friend): {[battle_state(c) for c in copies]}")
    attack = ["tap", "cross", "wait", "300"] * 6
    rounds = a.pick or 4
    for i in range(rounds):
        host.game.pad.run(menu_weapon(1 + i % 2, 0) + attack)
        time.sleep(2)
        blade_check(copies, f"round {i}, the host's swap")
        out.append(f"round {i}: host's item {read_u(host, host.game.base + SORA_RECORD, 2):#x}")
        drive_into(friend, i % 2)
        host.game.pad.run(attack)
        time.sleep(4)
        blade_check(copies, f"round {i}, the friend's Drive")
        revert_form(friend)
        host.game.pad.run(attack)
        time.sleep(4)
        blade_check(copies, f"round {i}, the friend's Revert")
    # The busy case of play: a room entry, where the friend's game builds the host's copy's Keyblade while it swings.
    out.append("room change to the gym: the host attacks from its arrival on")
    room = {c.name: c.mark() for c in copies}
    bench.warp(0x02, 0x28)
    bench.wait("the host in 02/28", lambda: host.room() == (0x02, 0x28), 90, copies)
    for _ in range(12):
        host.game.pad.run(attack)
    blade_check(copies, "the room change")
    new = friend.since(room[friend.name])
    out.append(f"   the friend's game after the room change: builds {sum(' built: ' in l and 'copy blade:' in l for l in new)}")
    ok = any("waits for a quiet moment" in l for l in friend.since(marks[friend.name]))
    if not ok:
        out.append("   FAIL: no build waited for a quiet moment (5.2.3 not exercised)")
    if not copy_blade_state(friend, "end", out)["copy"]:
        out.append("   FAIL: the friend's copy of the host has no Keyblade")
        ok = False
    builds = sum("copy blade: the copy's Keyblade" in l and " built: " in l for l in friend.since(marks[friend.name]))
    if builds < rounds:
        out.append(f"   FAIL: {builds} builds in the friend's game for {rounds} swaps")
        ok = False
    return blade_report(copies, marks, out) and ok


def blade_drive_storm(copies, a, out):
    """Section 6's blade_drive_storm (save 59): for 300 s (or --seconds) both players Drive and Revert at random
    (Drive list entries 0-4, seed 1) while the other walks and attacks, so both copies change looks and Keyblades
    while they move. PASS = no crash, no BLADE_BAD line."""
    import random
    rnd = random.Random(1)
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    form_now = lambda c: read_u(c, c.game.base + 0x9ACDD4, 1)
    end = time.time() + (a.seconds if a.seconds != 420 else 300)
    steps = 0
    while time.time() < end:
        c = rnd.choice(copies)
        other = friend if c is host else host
        if form_now(c):
            revert_form(c)
        else:
            drive_into(c, rnd.randrange(5))
        other.game.run(rnd.choice((0, 90, 180, 270)), 1200)
        other.game.pad.run(["tap", "cross", "wait", "300"] * 4)
        time.sleep(rnd.uniform(0.5, 3))
        steps += 1
        blade_check(copies, f"step {steps}")
    out.append(f"{steps} Drives and Reverts at random")
    for c in copies:
        if form_now(c):
            revert_form(c)
    time.sleep(4)
    blade_check(copies, "the last Reverts")
    return blade_report(copies, marks, out)


def blade_menu_move(copies, a, out):
    """Section 6's blade_menu_move (save 59): the friend in Valor (both copies wear looks: the host's copy of the
    friend, and the friend's copy of the host keeps its Keyblade); the host, out of battle, swaps to the --pick-th
    Keyblade in its list (default 3) and back to the 1st. PASS = no crash, no BLADE_BAD line, and a 'moved the copy's'
    file line only with 'the look goes on again' after it."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    drive_into(friend, 0)
    time.sleep(6)
    blade_check(copies, "the friend in Valor")
    for n in (a.pick or 3, 1):
        host.game.pad.run(menu_weapon(n, 800))
        time.sleep(3)
        blade_check(copies, f"the host's swap to the {n}-th")
        out.append(f"host's item {read_u(host, host.game.base + SORA_RECORD, 2):#x}")
        friend.game.pad.run(["tap", "cross", "wait", "300"] * 4)
    revert_form(friend)
    time.sleep(4)
    blade_check(copies, "the friend's Revert")
    ok = True
    for c in copies:
        new = c.since(marks[c.name])
        moved = sum("moved the copy's" in l for l in new)
        again = sum("the look goes on again from the files' new places" in l for l in new)
        out.append(f"{c.name}: worn files moved {moved}, the look put on again {again}")
        ok &= again >= (1 if moved else 0)
    return blade_report(copies, marks, out) and ok


def fx_made(c, mark, w0, w1):
    """Effects made in [w0, w1) (wall clock) in copy c, from the DLL's 5 s 'fx count:' lines (KH2COOP_FX_COUNT), each
    line in the span it ends in (+5 s: it covers the 5 s before), named by role: 'Sora A' / 'Sora B' is each player's
    Sora (its own game's, its form body, or the copy in the other game)."""
    other = "B" if c.name == "A" else "A"
    day = lambda w: (lambda s: s.tm_hour * 3600 + s.tm_min * 60 + s.tm_sec + w % 1)(time.localtime(w))
    made = {}
    for l in c.since(mark):
        if " fx count: " not in l or " x " not in l:
            continue
        h, m, s = l[:12].split(":")
        if not day(w0) <= int(h) * 3600 + int(m) * 60 + float(s) < day(w1) + 5:
            continue
        n, key = l.split(" fx count: ", 1)[1].split(" x ", 1)
        key = key.replace("our Sora", f"Sora {c.name}").replace("the copy", f"Sora {other}")
        made[key] = made.get(key, 0) + int(n)
    return made


ANTI_POINTS = 0x9ACCC0  # save+0x3410, i32 (F173): set to 0 before each bench Drive, so Anti Form is unlikely
FORM_NAMES = {1: "Valor", 2: "Wisdom", 3: "Limit Form", 4: "Master", 5: "Final"}


def fx_room(copies, a, out, equip=None):
    """fx_sync1's room: save 59 (all forms but Anti, Donald + Goofy) with its Limit abilities equipped in memory (only
    in the `equip` copies' games when given), back in HB Borough 04/06, --pick Dusks (default 4) made by the host, the
    battle state forced in both games."""
    load_both(copies, a.slot, a.room)
    out.append(f"Limit abilities equipped (memory only): {[equip_limits(c) for c in equip or copies]}")
    # The save's own room (HB Borough 04/06): the Underdrome put Hercules in Donald's place, and with a world ally there
    # Wisdom, Master and Final aren't offered (F44; run 1 2026-11-10). The room reload builds the equipped Limits.
    bench.warp(0x04, 0x06)
    time.sleep(4)  # the reload under way
    bench.wait("both back in 04/06", lambda: all(c.room() == (0x04, 0x06) and c.game.sora() for c in copies), 90, copies)
    time.sleep(8)
    bench.debug("A", 19, 0x13E | (a.pick or 4) << 16)  # Dusks in front of the host's Sora; the friend's game copies them
    time.sleep(5)
    for c in copies:
        bench.debug(c.name, 20, 1)  # the battle state, so the Limit command is offered
    time.sleep(2)
    out.append(f"party: {[party_now(c) for c in copies]}")


CURRENT_LIMIT = 0x2A24CC0  # the running Limit's object; its +0x54 bit 2 is the end mark (F509)


def limit_end_mark(c):
    lim = read_u(c, c.game.base + CURRENT_LIMIT, 8)
    return bool(lim) and bool(read_u(c, lim + 0x54) & 4)


def script_counts(lines):
    """motion mirror's last 30 s numbers in these lines: (Limit script effect starts sent, made here)."""
    import re
    got = (0, 0)
    for line in lines:
        m = re.search(r"Limit script effects on: starts sent (\d+), end calls sent \d+, made (\d+)", line)
        if m:
            got = (int(m.group(1)), int(m.group(2)))
    return got


MOVES_DOER = (r"our Limit script effect #(\d+) \(2/(\d+) entry (\d+), record flags 0x([0-9A-F]+)(, not on the holder)?\) "
              r"(.*?): changed (\d+) frames; last at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) turned \((-?[\d.]+), "
              r"(-?[\d.]+)\)(?:, before at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) turned \((-?[\d.]+), (-?[\d.]+)\))?")
MOVES_WATCHER = (r"the other player's Limit script effect #(\d+) end 0/(\d+): made at \((-?[\d.]+), (-?[\d.]+), "
                 r"(-?[\d.]+)\)(?:, now at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\) turned \((-?[\d.]+), (-?[\d.]+)\))?")


def moves_check(doer, watcher, doer_lines, watcher_lines, out):
    """TODO 1.189: motion mirror's trace (KH2COOP_LIMIT_FX_MOVES=1) of the doer's Limit script effects at their end
    against the watcher's copies of them at their kill, paired by number. Moved = the doer's polls saw it change after
    its start (Duck Flare's opening rockets are only turned) or its last place is over 100 from where the watcher made
    it, and its copy is right when it matches the doer's last value or the one before (the watcher plays an end
    before the states due in the same frame); effects the game re-places itself (record flags bit 1, or bits 0 and 4 = 1 and 0: L303) are skipped. The watcher's
    last line with a place for a number counts (it traces each end call and its holder's end). PASS = 3 or more moved, each now within
    max(40, a quarter of its travel) of the doer's last place and both angles within 0.15 rad; none moved (Comet,
    Whirli-Goof) = nothing to check, PASS; none paired = FAIL. Stage 1 (nothing sent) must FAIL: proof the check sees the
    bug. Also
    logged: both sides' per-Limit lines and the most upload in one second."""
    import math
    import re
    ours = {}
    for line in doer_lines:
        m = re.search(MOVES_DOER, line)
        if m:
            g = m.groups()
            ours[int(g[0])] = dict(start=int(g[1]), entry=int(g[2]), flags=int(g[3], 16), listed=not g[4], why=g[5],
                                   changed=int(g[6]), last=tuple(map(float, g[7:10])), rot=tuple(map(float, g[10:12])),
                                   before=tuple(map(float, g[12:15])) if g[12] else None,
                                   brot=tuple(map(float, g[15:17])) if g[15] else None)
    theirs = {}
    for line in watcher_lines:
        m = re.search(MOVES_WATCHER, line)
        if m:
            g = m.groups()
            if not g[5] and theirs.get(int(g[0]), {}).get("now"):
                continue  # ran out since its last end call: that call's place counts
            theirs[int(g[0])] = dict(made=tuple(map(float, g[2:5])),
                                     now=tuple(map(float, g[5:8])) if g[5] else None,
                                     rot=tuple(map(float, g[8:10])) if g[8] else None)
    out.extend(f"{doer.name}: {l[13:400]}" for l in doer_lines if "our Limit's script effects moved" in l)
    out.extend(f"{watcher.name}: {l[13:400]}" for l in watcher_lines
               if "the other player's Limit script effects moved" in l)
    for c, lines in ((doer, doer_lines), (watcher, watcher_lines)):
        ups = [int(x) for l in lines for x in re.findall(r"upload at most (\d+) B/s", l)]
        out.append(f"{c.name}: most upload in one second {max(ups) if ups else '?'} B")

    def wrap(x):
        return (x + math.pi) % (2 * math.pi) - math.pi

    moved = good = placed = 0
    for n in sorted(set(ours) & set(theirs)):
        o, t = ours[n], theirs[n]
        if o["flags"] & 2 or o["flags"] & 0x11 == 1:
            placed += 1
            continue
        travel = math.dist(o["last"], t["made"])
        if travel <= 100 and not o["changed"]:
            continue
        moved += 1
        def miss(place, rot):  # the watcher's copy against one of the doer's values
            off = math.dist(place, t["now"]) if t["now"] else None
            turn = max(abs(wrap(rot[i] - t["rot"][i])) for i in (0, 1)) if t["rot"] else None
            return off, turn, off is not None and off <= max(40, travel / 4) and turn is not None and turn <= 0.15

        off, turn, fine = miss(o["last"], o["rot"])
        if not fine and o["before"]:  # the watcher plays an end before that frame's states: one value behind
            off2, turn2, fine = miss(o["before"], o["brot"])
            if fine:
                off, turn = off2, turn2
        good += fine
        if moved <= 12:
            out.append(f"   #{n} 2/{o['start']} entry {o['entry']} flags {o['flags']:#x}: changed {o['changed']}, "
                       f"travel {travel:.0f}, the "
                       f"watcher's off by {'gone' if off is None else f'{off:.0f}'}, turned off by "
                       f"{'?' if turn is None else f'{turn:.3f}'} rad{'' if fine else '  <-'}")
    paired = len(set(ours) & set(theirs))
    ok = paired > 0 and (moved == 0 or (moved >= 3 and good == moved))
    out.append(f"moves_check ({doer.name} doing, {watcher.name} watching): paired {paired}, game-placed skipped {placed}, "
               f"moved {moved}, right {good}: {'PASS' if ok else 'FAIL'}{' (nothing moved)' if ok and not moved else ''}")
    return ok


def limit_back(copies, a, out):
    """TODO 1.186 and 1.188, in fx_sync1's room (fx_room). The first player (--by, default A = the host) uses the Limit
    with command id --limit (default 0x158 Duck Flare: its bring-back came 1.65 s after the end mark, F509); the other
    player waits on its Limit command and, as soon as the first's end mark is set (read from memory), uses its list's
    first Limit, inside the gap before the first's game builds its allies again. --hold: the first player Drives into
    Valor instead (Goofy away in both games) and only the other uses a Limit; Valor's place must stay away after it.
    PASS = no crash; the gap hit (the first's game kept a place for the other's Limit; with --hold: Goofy kept for our
    own form); 8 s after the second Limit's end both of the first game's companions here (with --hold: Goofy still
    away, then here 8 s after the Revert). Also (1.188): the first Limit's script effects made in the other game against
    those sent, and motion mirror's 'lost to full lists' lines."""
    import threading
    first, second = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    fx_room(copies, a, out)
    base = {c.name: script_counts(c.since(0)) for c in copies}
    marks = {c.name: c.mark() for c in copies}
    ok = True

    def alive(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def until(c, mark, text, secs):
        end = time.time() + secs
        while time.time() < end and not find(c.since(mark), text):
            time.sleep(0.1)
            alive(text)
        return bool(find(c.since(mark), text))

    def mash_until_end(c, mark, secs=40):  # the Limit's steps, until its end mark
        end = time.time() + secs
        while time.time() < end and not limit_end_mark(c) and not find(c.since(mark), "our Limit's end ran"):
            c.game.pad.run(bench.MASH_TRIANGLE)

    if a.hold:
        got = drive_form(first, [1])
        out.append(f"{first.name} Drives into Valor: form {got}; party {[party_now(c) for c in copies]}")
        if got != 1:
            out.append("   FAIL: no Valor")
            return False
        time.sleep(4)
    else:
        # The second's Limit takes the partner the first's kept (F509's case: no ally of the first's game leaves for it,
        # so limit sync sent none away): its list's cursor is left on it, then back to the page's Limit command.
        second.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"])
        if not cursor_to(second, lambda i, cmd, n: cmd == a.second):
            out.append(f"   FAIL: {second.name}'s Limit list has no command {a.second:#x}: {menu_page(second)[1]}")
            return False
        second.game.pad.run(["tap", "circle", "wait", "500"])
        cursor_to(second, lambda i, cmd, n: cmd == 0x51)  # back on the page, the cursor was on Attack (run 3)
        m1 = first.mark()
        first.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"])
        if not cursor_to(first, lambda i, cmd, n: cmd == a.limit):
            out.append(f"   FAIL: {first.name}'s Limit list has no command {a.limit:#x}: {menu_page(first)[1]}")
            return False
        first.game.pad.run(["tap", "cross", "wait", "300"])
        if not until(first, m1, "our Limit started", 5):
            out.append(f"   FAIL: {first.name}'s Limit didn't start")
            return False
        out.append(f"{first.name} Limit: {[l[13:110] for l in first.since(m1) if 'type 0x51)' in l][:1]}")
        mash = threading.Thread(target=mash_until_end, args=(first, m1), daemon=True)
        mash.start()
        end = time.time() + 45
        while time.time() < end and not limit_end_mark(first):
            time.sleep(0.01)
        t_mark = time.time()
        if not limit_end_mark(first):
            out.append(f"   FAIL: {first.name}'s Limit end mark never came")
            return False
        time.sleep(0.15)  # the second's allies come back with the first's end
    m2 = second.mark()
    t_go = time.time()
    if a.hold:
        second.game.pad.run(PAGE2)
        t_mark = time.time()
    if menu_page(second)[3] != 0x51:
        cursor_to(second, lambda i, cmd, n: cmd == 0x51)
    end = time.time() + 3  # its Limit command comes back once its allies are built again: cross until the list shows
    while time.time() < end and a.second not in menu_page(second)[1]:
        second.game.pad.run(["tap", "cross", "wait", "120"])
    p, cmds, i, cmd = menu_page(second)
    out.append(f"{second.name}'s Limit list {time.time() - t_mark:.2f} s after the end mark: "
               f"{[hex(x) for x in cmds]}, cursor on {cmd:#x}")
    if cmd != a.second:  # the list came back with another cursor
        cursor_to(second, lambda i, x, n: x == a.second)
    second.game.pad.run(["tap", "cross", "wait", "200"])
    if not until(second, m2, "our Limit started", 1.5):
        second.game.pad.run(["tap", "cross", "wait", "350", "tap", "cross", "wait", "200"])
    started = until(second, m2, "our Limit started", 2)
    t_start = time.time()
    if not a.hold:
        mash.join(5)
        out.append(f"{second.name}'s Limit asked {t_go - t_mark:.2f} s after the end mark, seen started "
                   f"{t_start - t_mark:.2f} s after it: {started}")
    if not started:
        out.append(f"   FAIL: {second.name}'s Limit didn't start")
        if a.moves and not a.hold:  # the first Limit's numbers still count (TODO 1.189)
            time.sleep(3)
            moves_check(first, second, first.since(marks[first.name]), second.since(marks[second.name]), out)
        return False
    out.append(f"{second.name} Limit: {[l[13:110] for l in second.since(m2) if 'type 0x51)' in l][:1]}")
    mash_until_end(second, m2)
    until(second, m2, "our Limit's end ran", 10)
    time.sleep(8)
    alive("after the second Limit")
    lines = first.since(marks[first.name])
    kept_peer = find(lines, "the other player's Limit holds it")
    kept_ours = find(lines, "our own form or summon holds it")
    party = party_now(first)
    away = find(lines, "leaves for the other player's Limit")  # 0 = F509's case: limit sync sent nobody away
    out.append(f"{first.name}: sent away for the other's Limit {len(away)}, kept for it {len(kept_peer)}, kept for our "
               f"own form {len(kept_ours)}, Donald / Goofy here 8 s after the second Limit {party}")
    out.extend(f"    {l[13:200]}" for l in lines if "limit sync:" in l or "companion sync:" in l)
    if a.hold:
        ok = bool(kept_ours) and party == (True, False)
        revert_to_sora(first)
        time.sleep(8)
        party = party_now(first)
        out.append(f"{first.name}: Donald / Goofy here 8 s after the Revert {party}")
        ok = ok and party == (True, True)
    else:
        ok = bool(kept_peer) and party == (True, True)
    out.append(f"{second.name}: Donald / Goofy here {party_now(second)}")
    ok = ok and party_now(second) == (True, True)
    for c in copies:  # motion mirror's 30 s line after the Limits: the other game's starts against those made here
        until(c, marks[c.name], "motion mirror: sent", 35)
    time.sleep(31)
    now = {c.name: script_counts(c.since(0)) for c in copies}
    for c, o in ((first, second), (second, first)):
        sent = now[c.name][0] - base[c.name][0]
        made = now[o.name][1] - base[o.name][1]
        out.append(f"{c.name}'s Limit script effects: sent {sent}, made in {o.name}'s game {made}")
        if c is first and not a.hold:
            ok = ok and sent > 0 and made > 0
        if c is second and not a.hold:  # TODO 1.190: the second's first effects, begun with our partner still busy
            out.append(f"   1.190: {o.name}'s game missed {sent - made} of the second Limit's {sent}")
            # 55 before the fix; 1-3 left: an effect on our still-busy partner, the last starts after our holder ended
            ok = ok and sent > 0 and made >= sent - 5
    for c in copies:
        out.extend(f"{c.name}: {l[13:260]}" for l in c.since(marks[c.name]) if "lost to full lists" in l
                   or "too many waiting" in l)
    if a.moves and not a.hold:  # TODO 1.189: the first Limit only (1.190 hits the second's first effects)
        moved = moves_check(first, second, first.since(marks[first.name]), second.since(marks[second.name]), out)
        ok = ok and moved
    return ok and all(c.alive() and not c.crash for c in copies)


HUD_HITS = (r"hud: hit events so far: our Sora's gauge (\d+), the other Sora's gauge (\d+), our companions' gauges "
            r"(\d+) \(from the host (\d+), our own game's dropped (\d+)\)")


def hud_hits(copies, a, out):
    """TODO 3.18 (party_hud.cpp): the gauges' hit events as the game raises them, in fx_sync1's room (fx_room: god
    mode, --pick Dusks that can't die). Both Soras stand among them for --seconds (default 60 here); for the second
    half the host's Sora is held 1500 above them (test command 15, each second), so they turn on the friend's. Each game counts
    the hit events its own gauges played and those it showed for the other game: the other Sora's gauge here must
    follow that Sora's own gauge there, and the friend's companion gauges the host's. PASS = no crash, every pair
    that saw hits within a quarter (events inside one packet are merged) and never more shown than played."""
    import re
    fx_room(copies, a, out)
    host, friend = copies
    secs = 60 if a.seconds == 420 else a.seconds
    spot = next((actor_pos(host, act) for act, model in bench.actors(host) if model.startswith("M_EX")), None)
    for i, c in enumerate(copies if spot else ()):
        bench.debug(c.name, 15, spot[0] + 80.0 * i, spot[1], spot[2], fmt="<Iifff")
    out.append(f"both Soras put by the Dusks at {spot and [round(v) for v in spot[:3]]}")
    start = time.time()
    end = start + secs
    n = 0
    while time.time() < end:
        for _ in range(5):
            time.sleep(1)
            if spot and time.time() - start > secs / 2:  # the host's Sora out of reach: the Dusks go for the friend's
                bench.debug("A", 15, spot[0], spot[1] - 1500.0, spot[2], fmt="<Iifff")
        if n % 4 == 1:
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"hud_hits_{c.name}_{n // 4}.png"))
        n += 1
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    got = {}
    for c in copies:
        last = [m for m in (re.search(HUD_HITS, l) for l in c.since(0)) if m]
        got[c.name] = tuple(int(x) for x in last[-1].groups()) if last else None
        out.append(f"{c.name}: own gauge, the other Sora's, companions', from the host, own dropped = {got[c.name]}")
    if not got["A"] or not got["B"]:
        out.append("   FAIL: no hit event line in a log (nobody was hit?)")
        return False

    def follows(shown, played, what):
        fine = shown <= played + 2 and shown >= played * 0.75 - 2
        out.append(f"   {what}: played {played}, shown in the other game {shown} -> {'ok' if fine else 'FAIL'}")
        return fine

    ok = follows(got["B"][1], got["A"][0], "the host's Sora")
    ok = follows(got["A"][1], got["B"][0], "the friend's Sora") and ok
    ok = follows(got["B"][3], got["A"][2], "the host's companions") and ok
    seen = got["A"][0] + got["B"][0] + got["A"][2] > 0
    if not seen:
        out.append("   FAIL: no hit events at all")
    return ok and seen and all(c.alive() and not c.crash for c in copies)


def limit_watch(copies, a, out):
    """TODO 1.65, in fx_sync1's room (fx_room) with only the --by player's Limit abilities equipped (the other's stay
    off, as save 59 has them): --by uses the Limit with command id --limit (default 0x158 Duck Flare; 0xFF Whirli-Goof).
    The other game must hold that Limit's file and play it. PASS = no crash, the watcher played its motions and
    skipped none ('not played'), and its own menu offers no Limit. Without the fix it must FAIL (no file there)."""
    doer, watcher = (copies[1], copies[0]) if a.by == "B" else (copies[0], copies[1])
    fx_room(copies, a, out, equip=[doer])
    out.extend(f"{watcher.name}: {l[13:200]}" for l in watcher.since(0) if "Limit preload also takes" in l)
    m, mw = doer.mark(), watcher.mark()
    doer.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"])
    if not cursor_to(doer, lambda i, cmd, n: cmd == a.limit):
        out.append(f"   FAIL: {doer.name}'s Limit list has no command {a.limit:#x}: {menu_page(doer)[1]}")
        return False
    doer.game.pad.run(["tap", "cross", "wait", "300"])
    end = time.time() + 40
    while time.time() < end and not find(doer.since(m), "our Limit's end ran"):
        doer.game.pad.run(bench.MASH_TRIANGLE)
    time.sleep(5)
    out.append(f"{doer.name} Limit: {[l[13:110] for l in doer.since(m) if 'type 0x51)' in l][:1]}, its end ran "
               f"{bool(find(doer.since(m), 'end ran'))}")
    lines = watcher.since(mw)
    missed = find(lines, "not played")
    played = [l for l in lines if "motion mirror: played" in l and "Limit motion" in l]
    out.extend(f"{watcher.name}: {l[13:220]}" for l in missed[:6])
    out.append(f"{watcher.name}: the Limit's motions played {len(played)}, not played {len(missed)}, effects not made "
               f"{len(find(lines, 'not made'))}")
    # The watcher's own menu must not offer what it hasn't equipped: its Limit command greyed, or a list without them.
    watcher.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"])
    cmds = menu_page(watcher)[1]
    watcher.game.pad.run(["tap", "circle", "wait", "500"])
    offered = sorted(set(cmds) & {0xFF, 0x152, 0x11A, 0x158, 0x1E4})
    out.append(f"{watcher.name}'s menu after Limit + cross: {[hex(x) for x in cmds]}, Limits offered {offered}")
    return bool(played) and not missed and not offered and all(c.alive() and not c.crash for c in copies)


def form_cast(copies, a, out):
    """TODO 1.187, in fx_sync1's room (fx_room): each player in turn Drives into Wisdom, casts the spell in magic slot 0
    twice (test command 14; the second a finisher, its combo count +0xD74 written full, F517), Reverts, the same in
    Final, then casts once as plain Sora. The spell script's attacks (trap_attack_new by the body, P_EX100*: group,
    variant) in the doer's game and those its copy's cast made in the other game are compared per phase: Wisdom adds 10
    to the variant, Final 30, the finisher 1 (F514). PASS = no crash, every phase cast, the other game's variants in the
    form's band and the doer's own where its log has them."""
    import re
    fx_room(copies, a, out)
    host, friend = copies
    # The spell script's owner is named after the body: P_EX100_MAGF in Wisdom, _ULTF in Final, P_EX100 plain (run 1).
    pat = re.compile(r"trap_attack_new\(P_EX100\w*#\w+, (\d+), -?\d+, (\d+)\)")
    ok = True
    for who, other in ((host, friend), (friend, host)):
        for name, form in (("Wisdom", 2), ("Final", 5), ("plain", 0)):
            if form:
                write_mem(who, who.game.base + ANTI_POINTS, struct.pack("<i", 0))
                got = drive_form(who, [form])
                if got != form:
                    out.append(f"   FAIL: {who.name}'s Drive took form {got}, not {name}")
                    ok = False
                    continue
                time.sleep(3)
            marks = {c.name: c.mark() for c in copies}
            for k in range(2 if form else 1):
                if k:  # the finisher: exe+0x3D7780 sets the cast's flag once +0xD74 (combo count) reaches the sheet's max
                    write_mem(who, read_u(who, who.game.base + 0x2A171C8, 8) + 0xD74, bytes([0x7F]))
                bench.debug(who.name, 14, 0)
                time.sleep(2.5)
            time.sleep(3)
            mine = [m.groups() for l in who.since(marks[who.name]) for m in [pat.search(l)] if m]
            theirs = [m.groups() for l in other.since(marks[other.name]) for m in [pat.search(l)] if m]
            casts = [l[13:150] for l in other.since(marks[other.name]) if "cast as form" in l or "cast the" in l]
            # The copy's variants in the form's band (L301); the doer's own when its log has them (run 1: one missing).
            low, high = {0: (0, 10), 2: (10, 20), 5: (30, 40)}[form]
            same = bool(theirs) and all(low <= int(v) < high for _, v in theirs) and (not mine or sorted(mine) == sorted(theirs))
            out.append(f"{who.name} {name}: its attacks {mine}, the copy's in {other.name}'s game {theirs}: "
                       f"{'right' if same else 'WRONG'}; {casts[:3]}")
            ok = ok and same
            if form:
                revert_to_sora(who)
                time.sleep(5)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                out.append(f"   FAIL: {c.name} {'crashed' if c.crash else 'closed'}")
                return False
    return ok


# Limit Form's own Limit commands (OpenKH docs/kh2/dictionary/commands.md).
LIMIT_SPECIALS = {0x2AB: "Ragnarok", 0x2BA: "Sonic Blade", 0x2BD: "Ars Arcanum", 0x2C0: "Strike Raid"}


def special_cursor(who, cmd):
    """In Limit Form: its Limit list open, the cursor on cmd, not pressed. The form's own 'Limit' entry (OpenKH lists
    two beside the companions' 0x51: 0x2A2 and 0x2C3) takes Magic's place on the top page (you, 2026-11-15); else the
    page's second entry is tried. Returns (the entry opened, the list's commands, the cursor is on cmd)."""
    who.game.pad.run(["tap", "r1", "wait", "200"])
    if not cursor_to(who, lambda i, x, n: x in (0x2A2, 0x2C3)):
        cursor_to(who, lambda i, x, n: i == 1)
    entry = menu_page(who)[3]
    who.game.pad.run(["tap", "cross", "wait", "800"])
    return entry, menu_page(who)[1], cursor_to(who, lambda i, x, n: x == cmd)


def limit_both(copies, a, out):
    """fx_sync1 --specials --both (read only; DECISIONS L1 says one Limit at a time, the code doesn't stop two): both
    players in Limit Form, then each of two of its Limits started by both 0.3 s apart, triangle
    mashed 8 s in both games. Per game: Limits started and ended, commands carried out, screenshots. PASS = no crash."""
    host, friend = copies
    gap = 0.3
    for c in copies:
        write_mem(c, c.game.base + ANTI_POINTS, struct.pack("<i", 0))
        out.append(f"{c.name}: form taken {drive_form(c, [3])}")
        time.sleep(5)
    for cmd in (0x2BA, 0x2AB):
        label = LIMIT_SPECIALS[cmd]
        marks = {c.name: c.mark() for c in copies}
        ready = []
        for c in copies:
            sheet = read_u(c, read_u(c, c.game.base + 0x2A171C8, 8) + 0x5C0, 8)
            write_mem(c, sheet + 0x180, peek.read(c.game.h, sheet + 0x184, 4))  # a full MP gauge
            ready.append(special_cursor(c, cmd)[2])
        out.append(f"both {label}: the cursor on it in {ready}")
        host.game.pad.run(["tap", "cross"])
        time.sleep(gap)
        friend.game.pad.run(["tap", "cross"])
        time.sleep(0.5)
        # triangle only in a game whose Limit started: a refused one leaves its Sora free (a save point, an RC)
        going = [c for c in copies if any("our Limit started" in l for l in c.since(marks[c.name]))]
        for step in range(16):
            bench.both(lambda c: c.game.pad.run(bench.MASH_TRIANGLE if c in going else ["wait", "490"]), copies)
            if step % 5 == 1:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"fx_both_{label.replace(' ', '_')}_{step // 5}_{c.name}.png"))
        time.sleep(4)
        for c in copies:
            new = c.since(marks[c.name])
            used = [l[l.find("command 0x"):][:14] for l in new if "carries out command" in l]
            out.append(f"{c.name} {label}: refused {sum('refused: the other player' in l for l in new)}, "
                       f"our Limit started {sum('our Limit started' in l for l in new)}, ended "
                       f"{sum(chr(39) + 's end ran' in l for l in new)}; the other's Limit seen "
                       f"{sum('leaves for the other player' in l for l in new)} leave lines, preload waits "
                       f"{sum('our Limit preload waits' in l for l in new)}; commands {used[:10]}; form byte "
                       f"{read_u(c, c.game.base + 0x9ACDD4, 1)}; party {party_now(c)}; alive {c.alive()}, crash {c.crash or '-'}")
    time.sleep(6)
    for c in copies:
        out.append(f"{c.name} at the end: party {party_now(c)}, exception lines "
                   f"{sum('EXCEPTION' in l or 'exception' in l for l in c.lines())}")
    return all(c.alive() and not c.crash for c in copies)


def limit_specials(copies, who, name, walls, out):
    """fx_sync1 --specials (TODO 1.183, research): in Limit Form, each of its four Limit commands with a full MP gauge,
    triangle mashed 8 s; four screenshots of both windows per command; the menu pages read are in the summary.
    Returns the top page's commands (the menu may be left on another page). The commands are under the form's own
    'Limit' entry on the top page, not the companions' Limit on the second page."""
    base = who.game.base
    sheet = read_u(who, read_u(who, base + 0x2A171C8, 8) + 0x5C0, 8)
    top = menu_page(who)[1]
    out.append(f"{name} Limit Form: top page {[hex(x) for x in top]}")
    for cmd, label in LIMIT_SPECIALS.items():
        write_mem(who, sheet + 0x180, peek.read(who.game.h, sheet + 0x184, 4))  # a full MP gauge
        entry, listed, found = special_cursor(who, cmd)
        out.append(f"{name} {label}: form byte {read_u(who, base + 0x9ACDD4, 1)}, opened entry {entry:#x}, list "
                   f"{[hex(x) for x in listed]}")
        t0, m = time.time(), who.mark()
        if not found:
            out.append(f"{name} {label}: not in the list")
            who.game.pad.run(["tap", "circle", "wait", "300"])
            continue
        who.game.pad.run(["tap", "cross", "wait", "300"])
        for step in range(16):
            who.game.pad.run(bench.MASH_TRIANGLE)
            if step % 4 == 1:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"fx_specials_{name}_{label.replace(' ', '_')}_{step // 4}_{c.name}.png"))
        time.sleep(3)
        new = who.since(m)
        used = [l[l.find("command 0x"):][:30] for l in new if "carries out command" in l]
        body = {}
        for l in new:  # the form body's own script calls the trap log shows (first of each kind, with its repeats)
            if "by P_EX100_KH1F" in l and " trap " in l:
                k = l.split(" trap ", 1)[1].split("(", 1)[0]
                body[k] = body.get(k, 0) + 1
        out.append(f"{name} {label}: commands {used[:8]}; body script trap lines {body or 'none'}")
        walls[f"{name} {label}"] = (t0, time.time())
    return top


def fx_sync1(copies, a, out):
    """Round 2 step 0 of docs/EFFECT_SYNC_PLAN.md (section 7), read only: save 59 (all forms but Anti, Donald + Goofy),
    its Limit abilities equipped in memory, the Underdrome (06/0D) with --pick Dusks (default 4) made by the host, the
    enemies and both Soras invincible, the battle state forced in both games. Per player (the host, then the friend),
    each its own phase: each form (Drive, --seconds of attacks (default 12), Revert), then a Limit (the host the list's
    1st, the friend its 3rd: two partners' Limits). Every effect made (KH2COOP_FX_COUNT: cause, owner and the file its
    effect comes from), summed per phase; tools/fx_diff.py's made report in the summary, the data in fx_sync1.json.
    --combo: each form's Revert comes right after two hits (no screenshots first), so the copy's look change ends
    live form effects; the summary counts form_look's holder ends that had any.
    --forms 2: only those form bytes. --pause: midway through each form's attacks the other game pauses 4 s.
    PASS = no crash (phases that didn't happen are listed)."""
    import json
    import fx_diff
    host, friend = copies
    fx_room(copies, a, out)
    if a.specials:  # both Soras by the Dusks, off the save point: a triangle there opens its menu (you, 2026-11-15)
        spot = next((actor_pos(host, act) for act, model in bench.actors(host) if model.startswith("M_EX")), None)
        for i, c in enumerate(copies if spot else ()):
            bench.debug(c.name, 15, spot[0] + 80.0 * i, spot[1], spot[2], fmt="<Iifff")
        out.append(f"both Soras put by the Dusks at {spot and [round(v) for v in spot[:3]]}")
        time.sleep(2)
    if a.specials and a.both:
        return limit_both(copies, a, out)
    seconds = 12 if a.seconds == 420 else a.seconds
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2
    marks = {c.name: c.mark() for c in copies}
    walls, skipped = {}, []

    def alive(label):
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'} ({label}): {c.crash or ''}")

    def until(c, mark, text, secs, keys=None):
        end = time.time() + secs
        while time.time() < end:
            if find(c.since(mark), text):
                return True
            if keys:
                c.game.pad.run(keys)
            else:
                time.sleep(0.2)
            alive(text)
        return False

    # Per player its forms first, its Limit last: a Limit leaves that game's menu on its second page, whose last entry
    # is Limit, so drive_form would start Limits (run 1 2026-11-10); going back with right / circle didn't (run 2).
    try:
        for who, name in ((host, "host"), (friend, "friend")):
            forms = {f: n for f, n in FORM_NAMES.items() if not a.forms or str(f) in a.forms}
            for f, form in ({} if a.limits else forms).items():
                time.sleep(6)
                write_mem(who, who.game.base + ANTI_POINTS, struct.pack("<i", 0))
                t0 = time.time()
                got = drive_form(who, [f])
                if got != f:
                    skipped.append(f"{name} {form} (form {got} taken)")
                    bench.screenshot(who.hwnd, os.path.join(OUT, f"fx_sync1_{name}_{f}_not_taken.png"))
                    if got == 6:  # Anti Form: no Revert command; it ends with its gauge
                        end = time.time() + 60
                        while time.time() < end and read_u(who, who.game.base + 0x9ACDD4, 1):
                            time.sleep(0.5)
                    continue
                time.sleep(4)  # the change
                top = limit_specials(copies, who, name, walls, out) if a.specials and f == 3 else None
                for key in (("right", "left", "circle") if top else ()):  # back on the top page for the Revert
                    if menu_page(who)[1] != top:
                        who.game.pad.run(["tap", key, "wait", "1000"])
                end = time.time() + (0 if top else seconds)
                paused = not a.pause
                while time.time() < end:
                    who.game.pad.run(fight)
                    alive(f"{name} {form}")
                    if not paused and time.time() > end - seconds / 2:  # the other game's pause, shots in flight
                        paused = True
                        seer = friend if who is host else host
                        seer.game.pad.run(["tap", "start", "wait", "4000", "tap", "start", "wait", "500"])
                if a.combo:  # the Revert right after two hits, the copy's form effects still alive (the advisor, 2026-11-11)
                    who.game.pad.run(["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 2 +
                                     ["tap", "up", "wait", "120"])  # the cursor wraps from Attack to Revert
                    p, cmds, i, cmd = menu_page(who)
                    if p and i == len(cmds) - 1:
                        who.game.pad.run(["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"])
                    else:
                        out.append(f"{name} {form}: the cursor wasn't on Revert after up ({i} of {len(cmds)})")
                        revert_to_sora(who)
                else:
                    for c in copies:
                        bench.screenshot(c.hwnd, os.path.join(OUT, f"fx_sync1_{name}_{f}_{c.name}.png"))
                    revert_to_sora(who)
                end = time.time() + 8
                while time.time() < end and read_u(who, who.game.base + 0x9ACDD4, 1):
                    time.sleep(0.3)
                time.sleep(3)
                walls[f"{name} {form}"] = (t0, time.time())
                alive(f"{name} {form}'s Revert")
            time.sleep(6)  # both stand: the 5 s count lines don't mix the phases
            if a.specials:  # --specials: no companion Limit after the forms
                continue
            t0, m = time.time(), who.mark()
            who.game.pad.run(PAGE2 + ["tap", "cross", "wait", "800"] + ["tap", "down", "wait", "300"] * (0 if who is host else 2)
                             + ["tap", "cross", "wait", "500"])
            if not until(who, m, "our Limit started", 4):
                who.game.pad.run(["tap", "cross", "wait", "500"])
            if until(who, m, "our Limit started", 3):
                until(who, m, "our Limit's end ran", 30, bench.MASH_TRIANGLE)  # its steps
                time.sleep(3)
                walls[f"{name} Limit"] = (t0, time.time())
                out.append(f"{name} Limit: {[l[13:120] for l in who.since(m) if 'type 0x51)' in l][:2]}")
            else:
                skipped.append(f"{name} Limit (not started)")
                who.game.pad.run(["tap", "circle", "wait", "300"] * 3)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"fx_sync1_{name}_limit_{c.name}.png"))
        if a.limits:  # motion_mirror's 30 s count line after the last Limit: its sent / made / end call counts
            ends = {c.name: c.mark() for c in copies}
            for c in copies:
                until(c, ends[c.name], "motion mirror: sent", 35)
                out.append(f"{c.name}: {[l[l.find('Limit hit effects'):] for l in c.since(ends[c.name]) if 'motion mirror: sent' in l][:1]}")
    finally:  # a game that dies mid-run (1.119, run 3) still leaves the phases it had
        for c in copies:
            if c.alive():
                bench.debug(c.name, 20, 0)
        time.sleep(6)  # the last count lines
        phases = {p: {c.name: fx_made(c, marks[c.name], w0, w1) for c in copies} for p, (w0, w1) in walls.items()}
        for p, games in phases.items():
            out.append(f"{p}: effects made A {sum(games['A'].values())}, B {sum(games['B'].values())}")
        out.append(f"phases not run: {skipped or 'none'}")
        for c in copies:  # form_look.cpp's holder ends: how many held live effects (KH2COOP_FORM_LOOK_FX)
            ends = [l for l in c.since(marks[c.name]) if "the copy's effect holder ended" in l]
            live = [l[13:200] for l in ends if ": 0 effects, 0 of them" not in l]
            bad = [l[13:200] for l in c.since(marks[c.name]) if "form look: BUG" in l or "motion mirror: BUG" in l]
            out.append(f"{c.name}: holder ends {len(ends)}, with live effects {len(live)}, BUG lines {len(bad)}")
            out.extend(f"  {l}" for l in (live[:12] + bad[:5]))
            new = c.since(marks[c.name])  # motion_mirror's form shots (KH2COOP_FORM_FX_SHOTS)
            shots = [l[13:] for l in new if "motion mirror: form shots:" in l]
            waits = sum("too many waiting" in l for l in new)
            out.append(f"{c.name}: {shots[-1] if shots else 'no form shots line'}; 'too many waiting' {waits}")
        with open(os.path.join(OUT, "fx_sync1.json"), "w", encoding="utf-8") as fh:
            json.dump(phases, fh, indent=1)
        out.extend(fx_diff.made_report(phases))
    return all(c.alive() and not c.crash for c in copies)


def blend_video(copies, a, out):
    """TODO 1.56: a clip of the copy's motion changes for a look (ffmpeg's desktop grab of the watcher's window, 30
    fps): Keyblades out (battle state), the friend's Sora walks ahead of the host's, then steps left, stops, steps
    right, stops, four times; the host's window is recorded: build/bench/extra_copy/blend_<label>.mp4, label 'off'
    with --env KH2COOP_COPY_BLEND=0, else 'on'. PASS = the file exists and no crash."""
    import ctypes
    import ctypes.wintypes as wt
    import subprocess
    host, friend = copies
    load_both(copies, a.slot, a.room)
    time.sleep(8)
    for c in copies:
        bench.debug(c.name, 20, 1)  # the battle state: Keyblades in hand
    time.sleep(2)
    friend.game.pad.run(["stick", "0", "1", "1100", "wait", "800"])  # ahead of the host's Sora, in its camera
    label = "off" if "KH2COOP_COPY_BLEND=0" in (a.env or []) else "on"
    path = os.path.join(OUT, f"blend_{label}.mp4")
    r, pt = wt.RECT(), wt.POINT(0, 0)
    bench.user32.GetClientRect(host.hwnd, ctypes.byref(r))
    bench.user32.ClientToScreen(host.hwnd, ctypes.byref(pt))
    w, h = r.right & ~1, r.bottom & ~1
    bench.user32.SetWindowPos(host.hwnd, ctypes.c_void_p(-1), 0, 0, 0, 0, 3)  # topmost while recorded
    time.sleep(0.5)
    rec = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "gdigrab", "-framerate", "30", "-offset_x",
                            str(pt.x), "-offset_y", str(pt.y), "-video_size", f"{w}x{h}", "-i", "desktop", "-t", "15",
                            "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "22", path])
    time.sleep(1)
    friend.game.pad.run((["stick", "-1", "0", "600", "wait", "900", "stick", "1", "0", "600", "wait", "900"]) * 4)
    rec.wait(30)
    bench.user32.SetWindowPos(host.hwnd, ctypes.c_void_p(-2), 0, 0, 0, 0, 3)
    bench.screenshot(host.hwnd, os.path.join(OUT, f"blend_{label}_end.png"))
    ok = os.path.exists(path) and os.path.getsize(path) > 20000
    out.append(f"clip {path}: {os.path.getsize(path) if os.path.exists(path) else 0} bytes, window {w}x{h} at ({pt.x}, {pt.y})")
    sets = [l[13:] for l in host.lines() if "puppet: motion" in l][-6:]
    out.append(f"the host's copy's last motion changes: {sets}")
    return ok and all(c.alive() and not c.crash for c in copies)


def orbs(copies, a, out):
    """TODO 1.206 (research): the companions' heal sparkles that stay in the friend's game. fx_room (save 59, the host's
    Dusks; run with --env KH2COOP_ENEMY_GOD=0 --env KH2COOP_TEST_ONE_HIT=1 so they die and drop orbs), both Soras
    attack 25 s, then stand 20 s while each game's live effects are sampled (bench.FxCensus, 0.5 s): those owned by or
    attached to a companion at the start and at the end of the stand, and the orb gains each log has. PASS = no crash."""
    fx_room(copies, a, out)
    marks = {c.name: c.mark() for c in copies}
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3
    end = time.time() + 25
    while time.time() < end:
        bench.both(lambda c: c.game.pad.run(fight), copies)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"orbs_{c.name}_after_fight.png"))
    census = bench.FxCensus(copies, step=0.5)
    time.sleep(20)
    census.stop()
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"orbs_{c.name}_after_stand.png"))
        new = c.since(marks[c.name])
        out.append(f"{c.name}: orb gains logged {sum('HP gain (orb' in l for l in new)}, samples {len(census.samples[c.name])}")
        if not census.samples[c.name]:
            continue
        first, last = census.samples[c.name][0][2], census.samples[c.name][-1][2]
        mine = lambda k: any(str(k[i]).startswith(("P_EX020", "P_EX030")) for i in (0, 4))
        for label, cen in (("start of the stand", first), ("end of the stand", last)):
            comp = {k: v for k, v in cen.items() if mine(k)}
            out.append(f"{c.name}: {label}: {sum(v[0] for v in comp.values())} live effects on its companions, "
                       f"{sum(v[0] for v in cen.values())} in all")
            for k, v in sorted(comp.items(), key=lambda kv: -kv[1][1]):
                out.append(f"    owner {k[0]}, effect data {k[2]:#x} id {k[3]}, attached to {k[4]}: {v[0]} effects, "
                           f"{v[1]} particles")
    return all(c.alive() and not c.crash for c in copies)


def fx_sync0(copies, a, out):
    """Step 0 of docs/EFFECT_SYNC_PLAN.md (TODO 1.128), read only: save 97, Hollow Bastion's Borough fight (04/09
    program 1, Donald + Goofy), the party and the enemies invincible. Phase 1 the host locks on and attacks while the
    friend stands; phase 2 the other way round (--seconds each, default 40). Both games' live effects every 0.2 s
    (bench.FxCensus), averaged per phase and key, named by role (tools/fx_diff.py), and every effect made (the DLL's
    KH2COOP_FX_COUNT lines, summed per phase: short ones too); the diff in the summary, the data
    in fx_sync0.json and fx_sync0.csv. PASS = no crash."""
    import json
    import fx_diff
    load_both(copies, a.slot, a.room)
    bench.warp(*LIMIT_FIGHTS[97])
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 90, copies)
    time.sleep(8)
    roles = {}
    for c, other in ((copies[0], "B"), (copies[1], "A")):
        acts = bench.actors(c)
        copy = next((x for x, n in acts[1:] if n.startswith("P_EX100")), 0)
        roles[c.name] = {acts[0][0] - c.game.base: f"Sora {c.name}", copy - c.game.base: f"Sora {other}"}
        out.append(f"{c.name}: our Sora exe+{acts[0][0] - c.game.base:#x} {acts[0][1]}, the copy exe+"
                   f"{copy - c.game.base:#x}; {len(acts)} actors: {sorted({n for _, n in acts})}")
    seconds = 40 if a.seconds == 420 else a.seconds
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2
    census, spans, walls = bench.FxCensus(copies, step=0.2), {}, {}
    marks = {c.name: c.mark() for c in copies}
    try:
        for phase, who in (("host attacks", copies[0]), ("friend attacks", copies[1])):
            time.sleep(6)  # both stand: the 5 s count lines (KH2COOP_FX_COUNT) don't mix the phases
            t0 = time.time()
            while time.time() - t0 < seconds:
                who.game.pad.run(fight)
                for c in copies:
                    c.lines()
                    if c.crash or not c.alive():
                        raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            spans[phase] = (t0 - census.t0, time.time() - census.t0)
            walls[phase] = (t0, time.time())
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"fx_sync0_{c.name}_{len(spans)}.png"))
    finally:
        census.stop()
    phases = {}
    for phase, (t0, t1) in spans.items():
        games = phases.setdefault(phase, {})
        for c in copies:
            got = [cen for t, _, cen in census.samples[c.name] if t0 <= t < t1]
            sums = {}
            for cen in got:
                for (model, owner, _, fx_id, amodel, att), (n, _, _) in cen.items():
                    key = (f"{roles[c.name].get(owner, model)} id {fx_id} on "
                           f"{roles[c.name].get(att, amodel) if att else '-'}")
                    sums[key] = sums.get(key, 0) + n
            games[c.name] = {k: v / max(len(got), 1) for k, v in sums.items()}
            out.append(f"{phase}: {c.name} {len(got)} samples")
    for phase, (w0, w1) in walls.items():
        games = phases.setdefault(f"{phase}, effects made", {})
        for c in copies:
            games[c.name] = fx_made(c, marks[c.name], w0, w1)
            out.append(f"{phase}: {c.name} {sum(games[c.name].values())} effects made, {len(games[c.name])} kinds")
    with open(os.path.join(OUT, "fx_sync0.json"), "w", encoding="utf-8") as fh:
        json.dump(phases, fh, indent=1)
    census.write(os.path.join(OUT, "fx_sync0.csv"))
    out.extend(fx_diff.report(phases))
    return all(c.alive() and not c.crash for c in copies)


def room_smoke(copies, a, out):
    """An audit's smoke run of one room (Timeless River audit, 2026-11-14): the save's room, then --warp W,R,P1,P2,P3
    (hex) into a fight or a mission; both Soras lock on, attack and mash triangle for --seconds (default 100), god
    mode, enemies kept alive (ENEMY_GOD). From both logs: the RCs each player used and the ones refused, grabs,
    objects with no host match, the friend's hits that counted and the ones dropped, by model, the mission lines,
    exceptions. PASS = no crash (the lists are the finding)."""
    import collections
    import re
    host, friend = copies
    load_both(copies, a.slot, a.room)
    w = [int(x, 16) for x in a.warp.split(",")]
    bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(10)  # the room's start scene
    marks = {c.name: c.mark() for c in copies}
    if a.spawn:  # --spawn OBJ (hex): one of that object 300 in front of the host's Sora (the friend's game copies it)
        bench.debug("A", 19, a.spawn | 1 << 16)
        time.sleep(4)
    slot = lambda b: ["hold", b, "35", "wait", "35"]
    keys = ("cross", "cross", "cross", "circle", "cross", "cross") if a.near else (
        "r1", "cross", "cross", "cross", "circle", "cross", "cross")  # --near: no lock-on, it would turn to an enemy
    # --tri host / friend: only that player presses triangle (an RC only one of them can take, e.g. the steamboat's hook)
    fights = {c.name: [t for b in keys for t in (slot(b + "+triangle") + slot("triangle") * 3
                                                if a.tri in ("both", "host" if c is host else "friend")
                                                else slot(b) + ["wait", "210"])] for c in copies}
    if a.tri_only:  # --tri-only: no attacks, no lock-on (an attack parries the steamboat's barrels too)
        fights = {c.name: slot("triangle") if a.tri in ("both", "host" if c is host else "friend") else ["wait", "70"]
                  for c in copies}
    if a.keys:  # --keys: e.g. a skateboard's jump, then its trick in the air (TODO 1.239)
        fights = {c.name: a.keys.split(",") for c in copies}
    mover = host if a.near_host else friend
    watcher = friend if a.near_host else host
    if a.watch:  # --watch: the other player presses nothing and is put beside the mover every 2 s (to have it on screen)
        fights[watcher.name] = ["wait", "500"]
    seconds = 100 if a.seconds == 420 else a.seconds
    t0, shots, put, watched, placed = time.time(), 0, 0.0, 0.0, False
    while time.time() - t0 < seconds:
        if host.room() != (w[0], w[1]):  # the fight ended and the game left: no mashing at a save point
            out.append(f"the host left the room after {time.time() - t0:.0f} s")
            break
        # --near MODEL: the friend's Sora is put beside its nearest actor of that model every 5 s and attacks it
        # --later KEY,SECONDS: from then on the --near player only taps KEY (a ride's Get Off: square,35)
        if a.later and time.time() - t0 >= float(a.later.split(",")[1]):
            fights[mover.name] = ["hold", a.later.split(",")[0], "35", "wait", "465"]
        if a.watch and time.time() - watched >= 2:
            watched = time.time()
            p = mover.game.sora()
            if p:
                bench.debug(watcher.name, 15, p[0] + 150.0, p[1], p[2] + 150.0, fmt="<Iifff")
        # --near-once: put there only until it worked once (a ride: the board then moves with the rider)
        if a.near and time.time() - put >= 5 and not (a.near_once and placed):
            put = time.time()
            me = mover.game.sora()
            near = [q for q in (actor_pos(mover, act) for act, model in bench.actors(mover) if model.startswith(a.near))
                    if q]
            if me and near:
                q = min(near, key=lambda q: sum((q[i] - me[i]) ** 2 for i in range(3)))
                bench.debug(mover.name, 15, q[0] + 60.0, q[1], q[2], fmt="<Iifff")
                placed = True
        bench.both(lambda c: c.game.pad.run(fights[c.name]), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        if time.time() - t0 > 30 * (shots + 1):
            shots += 1
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"smoke_{w[0]:02X}{w[1]:02X}_{c.name}_{shots}.png"))
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    names = {}
    try:
        import pathlib
        for line in pathlib.Path(r"D:\tools\openkh\docs\kh2\dictionary\commands.md").read_text(
                encoding="utf-8", errors="replace").splitlines():
            m = re.match(r"\|\s*(\d+)\s*\|\s*(.*?)\s*\|", line)
            if m:
                names[m.group(1)] = m.group(2)
    except OSError:
        pass

    def tally(lines, pattern, fmt=lambda m: " ".join(m.groups())):
        c = collections.Counter()
        for l in lines:
            m = re.search(pattern, l)
            if m:
                c[fmt(m)] += 1
        return ", ".join(f"{k} x{v}" for k, v in c.most_common(12)) or "-"
    cmd = lambda m: f"{m.group(1)} {names.get(m.group(1), '?')} ({m.group(2)})"
    out.append(f"room {w[0]:02X}/{w[1]:02X} programs {w[2]}/{w[3]}/{w[4]}, {seconds} s; host's room now {host.room()}, "
               f"friend's {friend.room()}")
    out.append("host's own RCs: " + tally(hl, r"our own command (\d+) on (\S+?):", cmd))
    out.append("friend's RCs sent: " + tally(fl, r"we used command (\d+) of our (\S+)", cmd))
    out.append("friend's RCs run by the host: " + tally(hl, r"the friend used command (\d+) of our (\S+?)[ ,(]", cmd))
    out.append("friend's RCs not run: " + tally(hl, r"the friend's command (\d+) on host#\d+ not run: (.*)$",
                                                lambda m: f"{m.group(1)} {names.get(m.group(1), '?')}: {m.group(2)[:60]}"))
    out.append("friend's repeats dropped: " + str(len(find(fl, "dropped: sent to the host"))) +
               "; host's presses dropped for the friend's: " + str(len(find(hl, "dropped: a script holds the friend"))))
    out.append("grabs of the copy (host): " + tally(hl, r"grab sync: (\S+) grabs the friend's Sora copy", lambda m: m.group(1)))
    out.append("grab calls not sent or refused: " + tally(hl + fl, r"grab sync: .*?(not sent|refused|no such|couldn't)[^:;]*",
                                                         lambda m: m.group(0)[11:80]))
    out.append("friend's hits counted by the host: " + tally(hl, r"world: friend hit our#\d+ (\S+) for", lambda m: m.group(1)))
    out.append("friend's hits dropped in its game: " + tally(fl, r"claim: our#\d+ (\S+) -\d+, attacker (.*?) exe.*: (.*)$",
                                                           lambda m: f"{m.group(1)} by {m.group(2)}: {m.group(3)[:40]}"))
    # TODO 1.201: a hit on an object without HP changes none, so it reaches the host only as the copy's replayed swing
    nohp = r"no-HP hit: on our (\S+) \(host#\d+\), by %s, reaction (\d+)"
    out.append("friend's hits on objects without HP: its Sora's: " + tally(fl, nohp % "our Sora") +
               " || run in the host's game (hit claims; with KH2COOP_HIT_CLAIMS=0 the copy's own swings): " +
               tally(hl, nohp % "the copy"))
    # TODO 1.200: a ride each game runs for its own player, shown on the other's copy
    ride = r"motion mirror: (the (?:host|friend) rides our \S+?):|motion mirror: (the rider's motion \d+ -> \d+|the ride ended)"
    out.append("rides shown: host's game: " + tally(hl, ride, lambda m: m.group(1) or m.group(2)) + " || friend's game: " +
               tally(fl, ride, lambda m: m.group(1) or m.group(2)))
    out.append("friend's objects with no host match: " + tally(fl, r"world: ours#\d+ (\S+) has no host match", lambda m: m.group(1)))
    for key, lines in (("census", find(fl, "world: enemies host")), ("made", find(fl, "host objects made here")),
                       ("one brain", find(fl, "world mirror: one brain:"))):
        out.append(f"friend {key}: {lines[-1][13:260] if lines else '-'}")
    for c, lines in (("host", hl), ("friend", fl)):
        ms = [l[13:230] for l in lines if "mission sync:" in l or "scene sync:" in l]
        out.append(f"{c} mission / scene lines ({len(ms)}): " + (" || ".join(ms[-3:]) if ms else "-"))
        bad = [l[13:200] for l in lines if re.search(r"exception|EXCEPTION|fault|CRASH|BUG", l) and "exceptions 0" not in l]
        out.append(f"{c} exception lines ({len(bad)}): " + (" || ".join(bad[:3]) if bad else "-"))
    return all(c.alive() and not c.crash for c in copies)


TEAM_ROWS, TEAM_COUNT = 0x2AE75D0, 0x2AE7650  # script team blocks: 8 rows {s32 id, +8 block}, then a count (L1577)
OOGIE_TEAM = 4  # trap_teamwork_alloc(4, 52): Oogie's, his container's and the belts' shared block (L1571)


def team_block(c, team):
    """(that script team's block in a copy's game or 0, the table's count), read from outside: the game's own lookup
    allocates on a miss."""
    h, base = c.game.h, c.game.base
    n = struct.unpack("<i", peek.read(h, base + TEAM_COUNT, 4))[0]
    for i in range(8):  # all 8 rows: the count is printed, not trusted (L1577: unchecked)
        tid, block = struct.unpack("<i4xQ", peek.read(h, base + TEAM_ROWS + 16 * i, 16))
        if tid == team and block:
            return block, n
    return 0, n


def oogie_fields(c, block):
    """L1571's fields: +4 the main player's belt, +12 presents in, +16 needed, +20 the switch act's belt, +32 the
    container's flag, +36 0 while a belt jump runs, +44 belt jumps allowed, +48 the container's ready signal."""
    v = struct.unpack("<13i", peek.read(c.game.h, block, 52))
    return {k: v[k // 4] for k in (4, 12, 16, 20, 32, 36, 44, 48)}


def oogie_break(copies, a, out):
    """TODO 1.247 (reaction_sync.cpp kKeptActs): Oogie's fight (save 22 = 0E/08, then 0E/09 programs 0x37) with the
    break brought forward: the host's team block 4 (L1571, L1577) is found and, only if its presents needed (+16) reads
    12, its presents in (+12) is set to 11 (the bench's one write), so the next present in breaks him. Both Soras attack
    and mash triangle (presents, belt jumps) as in room_smoke; from the break on the friend presses only triangle and is
    put by its nearest belt every 4 s, so its jumps come while Oogie falls, escapes and returns. --seconds: the wait for
    the break (default 240; the break is the game's: +12 reaching +16), then 45 s more. From the host's parity lines:
    the container sinks (164), then Oogie climbs back in (234 or 236) and the container rises (163). PASS = his climb
    and the rise after the sink, no crash. SETUP FAILURE = no break, or no friend belt jump run in the host's game from
    3 s before the sink to his climb (or the end). Listed: both games' block fields, +12 as it changes, our B_NM000
    lines with their times, the motions from the break. On a DLL without kKeptActs a jump in that window is expected to
    leave him out (TODO 1.247: the control). With TODO 1.248's drop (ff38d44a2334 on) the friend has no belt prompt in a
    break window, as the host has none: run with --env KH2COOP_RC_ZONES_FROM_HOST=0, or the SETUP FAILURE is expected."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0xE, 9, 0x37)
    bench.wait("both in 0E/09", lambda: all(c.room() == (0xE, 9) and c.game.sora() for c in copies), 90, copies)
    time.sleep(10)  # the room's start scene
    blocks = {}
    for c in copies:
        blocks[c.name], n = team_block(c, OOGIE_TEAM)
        out.append(f"{c.name}: team count {n}, block 4 at {blocks[c.name]:#x}"
                   + (f", fields {oogie_fields(c, blocks[c.name])}" if blocks[c.name] else ""))
    hb = blocks["A"]
    if not hb or oogie_fields(host, hb)[16] != 12:
        raise bench.Failed("SETUP FAILURE: the host's block 4 not found or its +16 isn't 12 (L1571); nothing written")
    marks = {c.name: c.mark() for c in copies}
    write_mem(host, hb + 12, struct.pack("<i", 11))
    slot = lambda b: ["hold", b, "35", "wait", "35"]
    mash = [t for b in ("r1", "cross", "cross", "cross", "circle", "cross", "cross")
            for t in slot(b + "+triangle") + slot("triangle") * 3]
    jumps = slot("triangle") * 8
    wait = 240 if a.seconds == 420 else a.seconds
    t0 = time.time()
    deadline, broke, put, last = t0 + wait, 0.0, 0.0, None
    breaks, armed, rose, mark_b = 0, True, 0.0, None  # --breaks N: after each rise +12 is set to 11 again
    while time.time() < deadline:
        if host.room() != (0xE, 9):
            out.append(f"the host left the room after {time.time() - t0:.0f} s")
            break
        if broke and not rose and any(re.search(r"obj=F_NM080 [^;]* mot=163 ", l) for l in host.since(mark_b)):
            rose = time.time()  # his container rose: he is back in, the break's window is over
            out.append(f"{rose - t0:.0f} s: the container rose")
        if rose and not armed and breaks < a.breaks and time.time() - rose >= 4:
            write_mem(host, hb + 12, struct.pack("<i", 11))  # the next break (the second one takes another branch)
            armed, deadline = True, time.time() + wait
            out.append(f"{time.time() - t0:.0f} s: the host's +12 set to 11 again")
        window = broke and not rose  # between a break and the container's rise: the friend only jumps belts
        if window and time.time() - put >= 4:  # the friend by its nearest belt, so its jumps keep coming
            put = time.time()
            me = friend.game.sora()
            belts = [q for q in (actor_pos(friend, act) for act, model in bench.actors(friend)
                                 if model.startswith("F_NM130")) if q]
            if me and belts:
                q = min(belts, key=lambda q: sum((q[i] - me[i]) ** 2 for i in range(3)))
                # its own height: the belt object sits 200 above the floor, and a jump pressed mid-air crashed (1.250)
                bench.debug("B", 15, q[0] + 60.0, me[1], q[2], fmt="<Iifff")
        bench.both(lambda c: c.game.pad.run(jumps if window and c is friend else mash), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        f = oogie_fields(host, hb)
        if f[12] != last:
            out.append(f"{time.time() - t0:.0f} s: the host's presents in {f[12]} of {f[16]}, +32 {f[32]}, "
                       f"+36 {f[36]}, +44 {f[44]}")
            last = f[12]
        if armed and f[12] >= f[16]:  # the game's own break (+12 reached +16), not our log line: the control has none
            broke, armed, rose, mark_b = time.time(), False, 0.0, host.mark()
            breaks += 1
            deadline = broke + 45
            out.append(f"{broke - t0:.0f} s: break {breaks} (+12 reached +16)")
    hl = host.since(marks["A"])
    ours = find(hl, "reaction sync: B_NM000")
    out.append(f"our B_NM000 lines {len(ours)}:")
    out += [f"   {l[:200]}" for l in ours[:30]]
    if friend.alive():
        out.append(f"friend's block now: {oogie_fields(friend, blocks['B'])}" if blocks["B"] else "friend: no block 4")
    secs = lambda t: int(t[0:2]) * 3600 + int(t[3:5]) * 60 + float(t[6:12])
    seen = {}
    for l in hl:  # Oogie's and the container's motion changes since the write (the host's parity lines)
        if "parity: H" in l:
            for m in re.finditer(r"obj=(B_NM000|F_NM080) ser=\d+ hp=-?\d+ pos=(\S+) mot=(\d+)", l):
                ch = seen.setdefault(m.group(1), [])
                if not ch or ch[-1][1] != m.group(3):
                    ch.append((l[:12], m.group(3), m.group(2)))
    box, oogie = seen.get("F_NM080", []), seen.get("B_NM000", [])
    sinks = [t for t, mot, _ in box if mot == "164"]  # the container sinks at a break (F715)
    jumps_run = [l[:12] for l in find(hl, "reaction sync: the friend used command") if "of our F_NM130" in l]
    last_t = hl[-1][:12] if hl else None
    for model, ch in seen.items():
        shown = [c for c in ch if not sinks or secs(c[0]) >= secs(sinks[0]) - 3]
        out.append(f"{model} motions: " + ", ".join(f"{t} {mot} at {pos}" for t, mot, pos in shown[:40]))
    ok, setup = True, []
    for k, sink in enumerate(sinks):
        stop = sinks[k + 1] if k + 1 < len(sinks) else last_t  # this break's stretch ends at the next sink
        within = lambda t: secs(sink) < secs(t) and (not stop or secs(t) <= secs(stop))
        climb = next((t for t, mot, _ in oogie if mot in ("234", "236") and within(t)), None)  # his climb back in
        rise = next((t for t, mot, _ in box if mot == "163" and within(t)), None)  # the container rising
        end = climb or stop
        jumped = [t for t in jumps_run if end and secs(sink) - 3 <= secs(t) <= secs(end)]
        out.append(f"break {k + 1}: the container sank {sink}, Oogie's climb {climb or '-'}, the container rose "
                   f"{rise or '-'}; the friend's belt jumps run from 3 s before the sink to "
                   f"{'the climb' if climb else 'the next sink or the end'}: {', '.join(jumped) or '-'}")
        ok &= bool(climb) and bool(rise)
        if not jumped:
            setup.append(f"no friend belt jump in break {k + 1}")
    if len(sinks) < a.breaks:
        setup.append(f"{len(sinks)} of {a.breaks} breaks")
    if setup:
        out.append("SETUP FAILURE: " + "; ".join(setup))
        return False
    return ok and all(c.alive() and not c.crash for c in copies)


def bag_leave(copies, a, out):
    """Oogie's bags (F_NM110, '(F) Oogie's bag (NM)'; a broken one lets a Wight Knight out), save 22 + the warp to
    0E/09: the friend's Sora is put by its nearest bag every 1.5 s and attacks. Both games' bags are sampled from
    outside every tick; each host bag's trap_obj_leave comes from the host's log. Listed per leave: how long after it
    the host's bag and the friend's nearest bag left their actor lists. Shots of both windows 0.3 s and 1.2 s after
    the first three leaves. --seconds (default 150). PASS = no crash and at least one leave paired (the lags are the
    result, not a verdict)."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0xE, 9, 0x37)
    bench.wait("both in 0E/09", lambda: all(c.room() == (0xE, 9) and c.game.sora() for c in copies), 90, copies)
    time.sleep(10)  # the room's start scene
    sod = lambda t: (lambda lt: lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + t % 1)(time.localtime(t))
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    bags = {c.name: [] for c in copies}  # [address, first, last, pos] per bag seen (an address reused = a new bag)
    live = {c.name: {} for c in copies}
    leaves, shots, stop = [], [], threading.Event()

    def press():  # the friend attacks the whole time; its Sora is moved to the bags
        while not stop.is_set():
            friend.game.pad.run(["tap", "cross", "wait", "8"])

    presser = threading.Thread(target=press, daemon=True)
    presser.start()
    t0, put, removed = time.time(), 0.0, []
    try:
        while time.time() - t0 < (150 if a.seconds == 420 else a.seconds):
            now = sod(time.time())
            for c in copies:
                for act, model in bench.actors(c):
                    if not model.startswith("F_NM110"):
                        continue
                    try:
                        p = actor_pos(c, act)
                    except OSError:
                        continue
                    e = live[c.name].get(act)
                    if not e or now - e[2] > 0.5:
                        e = live[c.name][act] = [act, now, now, p]
                        bags[c.name].append(e)
                    e[2], e[3] = now, p
            for c in copies:
                new = c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                for l in new:
                    m = re.search(r"trap 1/28 trap_obj_leave\(F_NM110#([0-9A-F]+)\)", l)
                    if c is host and m:
                        leaves.append((secs(l), int(m.group(1), 16)))
                        if len(leaves) <= 3:
                            shots += [(secs(l) + 0.3, len(leaves), "0.3s"), (secs(l) + 1.2, len(leaves), "1.2s")]
                    if c is friend and re.search(r"world: ours#\d+ F_NM110 removed without a death", l):
                        removed.append(secs(l))
            for s in [s for s in shots if s[0] <= now]:
                shots.remove(s)
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"bag_leave_{s[1]}_{s[2]}_{c.name}.png"))
            mine = [e for e in live["B"].values() if now - e[2] < 0.2 and now - e[1] > 1.0]  # landed, still there
            me = friend.game.sora()
            if mine and me and time.time() - put >= 1.5:
                put = time.time()
                q = min(mine, key=lambda e: sum((e[3][i] - me[i]) ** 2 for i in range(3)))[3]
                bench.debug("B", 15, q[0] + 60.0, q[1], q[2], fmt="<Iifff")
            time.sleep(0.03)
    finally:
        stop.set()
        presser.join(5)
    out.append(f"bags seen: host {len(bags['A'])}, friend {len(bags['B'])}; host leave calls {len(leaves)}; "
               f"friend's 'removed without a death' lines {len(removed)}")
    paired = 0
    for t, low in leaves:
        hb = next((e for e in bags["A"] if e[0] & 0xFFFFFF == low and e[1] - 0.2 <= t <= e[2] + 0.5), None)
        if not hb:
            out.append(f"leave at {t:.2f}: host bag #{low:X} not in the samples")
            continue
        alive = [e for e in bags["B"] if e[1] <= t <= e[2] + 0.2]
        fb = min(alive, key=lambda e: sum((e[3][i] - hb[3][i]) ** 2 for i in range(3)), default=None)
        dist = math.dist(fb[3], hb[3]) if fb else -1
        rm = next((r for r in removed if r >= t), None)
        out.append(f"leave at {t:.2f} (host bag #{low:X}): the host's gone {hb[2] - t:+.2f} s, the friend's nearest "
                   + (f"({dist:.0f} apart) gone {fb[2] - t:+.2f} s" if fb else "- none alive")
                   + (f"; the friend's next removal line {rm - t:+.2f} s" if rm else ""))
        paired += bool(fb)
    return paired > 0 and all(c.alive() and not c.crash for c in copies)


def rodeo(copies, a, out):
    """TODO 1.254 (F732, F733, L1600): the friend's Rodeo (368) and Grand Cross (369) on a Living Bone (M_EX670, obj
    0xF) spawned in front of the host's Sora: save 23 (Pride Lands; Rodeo is lion Sora only), then the warp to the
    Elephant Graveyard 0A/05 with programs 0/0/0 (no save or world point there to catch the triangle presses, and
    none of the room's own Living Bones). Until Rodeo runs
    the friend's Sora is put beside the Living Bone every 1.5 s (four sides in turn); it mashes triangle throughout.
    A sampler reads both games from outside: the friend's Sora drawn (+0x70) and its parent (+0x6A0), the host's copy
    drawn and its parent, each game's Living Bone drawn. Per RC: the share of frames each Sora is attached, and each
    Sora's offset from its own Living Bone (the friend's against the host copy's), its largest step between two
    samples, the friend's grab lines. Round 1 stops pressing once Rodeo runs (the ride ends by itself, no Grand Cross;
    a Grand Cross under ENEMY_GOD leaves the Living Bone stuck), round 2 goes on to Grand Cross. Two rounds or --seconds
    (default 120). Also FAIL: the two Soras' mean heights off their Living Bones more than 60 apart, or the friend's
    Sora out of its grab motion (+0x18C bit 14) in more than 5% of Rodeo's samples (F745). --race (TODO 1.255): Rodeo is pressed only while the host's Living Bone moves (any motion but idle 0;
    F738's was 202, one run showed only 2 / 201) and the friend's still shows 0 (the late press of F738). PASS = a round ran, each RC's two attached shares within 20 points of each other (the two games
    agree), no crash; the shares and steps are the result."""
    import netbench
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    sod = lambda t: (lambda lt: lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + t % 1)(time.localtime(t))
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    try:
        bench.debug("A", 12, 0x0A, 0x05, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")
        bench.wait("both in 0A/05", lambda: all(c.room() == (0x0A, 0x05) and c.game.sora() for c in copies), 90, copies)
        time.sleep(4)
        old = {c.name: bench.actor_by_model(c, "M_EX670") for c in copies}
        bench.debug("A", 19, 0xF | 1 << 16)
        bench.wait("a new Living Bone in both games", lambda: all(
            (lambda t: t and t != old[c.name])(bench.actor_by_model(c, "M_EX670")) for c in copies), 40, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    time.sleep(2)
    lb = {c.name: bench.actor_by_model(c, "M_EX670") for c in copies}
    fs, copy = head(friend), netbench.find_copy(host.game)
    out.append(f"Living Bone: host {lb['A']:#x}, friend {lb['B']:#x}; the friend's Sora {fs:#x}, the host's copy {copy:#x}")
    rows, stop = [], threading.Event()

    def sample():
        while not stop.is_set():
            try:
                rows.append((sod(time.time()), f3(friend, fs + 0x70), read_u(friend, fs + 0x6A0), f3(friend, lb["B"] + 0x70),
                             f3(host, copy + 0x70), read_u(host, copy + 0x6A0), f3(host, lb["A"] + 0x70),
                             read_u(friend, lb["B"] + 0x18C) >> 15 & 1, read_u(host, lb["A"] + 0x18C) >> 15 & 1,
                             read_u(friend, fs + 0x18C) >> 14 & 1))
            except OSError:
                pass
            time.sleep(0.005)

    quiet = threading.Event()  # round 1: no presses after Rodeo, so the ride ends with no Grand Cross
    racing = threading.Event()  # --race: Rodeo pressed only while the host's Living Bone moves, the friend's shows 0
    if a.race:
        racing.set()

    def press():
        while not stop.is_set():
            if quiet.is_set():
                time.sleep(0.05)
            elif racing.is_set():
                try:
                    late = bench.motion(host, lb["A"]) not in (0, -1) and bench.motion(friend, lb["B"]) == 0
                except OSError:
                    late = False
                if late:
                    friend.game.pad.run(["hold", "triangle", "35", "wait", "35"])
                else:
                    time.sleep(0.002)
            else:
                friend.game.pad.run(["tap", "triangle", "wait", "8"])

    threads = [threading.Thread(target=sample, daemon=True), threading.Thread(target=press, daemon=True)]
    for t in threads:
        t.start()
    rounds, cur, side, put, shots = [], {}, 0, 0.0, []
    t0 = time.time()
    try:
        while time.time() - t0 < (120 if a.seconds == 420 else a.seconds) and len(rounds) < 2:
            for c in copies:
                new = c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                for l in new if c is host else []:
                    if "the friend used command 368 of our M_EX670" in l and "368" not in cur:
                        cur["368"] = secs(l)
                        racing.clear()
                        if not rounds:  # round 1: Rodeo alone, its ride ends by itself
                            shots += [(cur["368"] + 1.0, "rodeo")]
                            quiet.set()
                    elif "the friend used command 369 of our M_EX670" in l and "368" in cur and "369" not in cur:
                        cur["369"] = secs(l)
                        if len(rounds) == 1:
                            shots += [(cur["369"] + 0.6, "grand_cross")]
                    elif "'let go' sent to the friend" in l and "368" in cur:
                        cur["go"] = secs(l)
                        rounds.append(cur)
                        cur = {}
                        quiet.clear()
                        if a.race:
                            racing.set()
            now = sod(time.time())
            for s in [s for s in shots if s[0] <= now]:
                shots.remove(s)
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"rodeo_{s[1]}_{c.name}.png"))
            if not cur and time.time() - put >= 1.5:  # beside it until Rodeo runs, never during a ride
                put = time.time()
                p = actor_pos(friend, lb["B"])
                dx, dz = ((150.0, 0.0), (0.0, 150.0), (-150.0, 0.0), (0.0, -150.0))[side % 4]
                side += 1
                bench.debug("B", 15, p[0] + dx, p[1], p[2] + dz, fmt="<Iifff")
            time.sleep(0.03)
        time.sleep(2)
    finally:
        stop.set()
        for t in threads:
            t.join(5)
    rel = lambda s, i, j: tuple(s[i][k] - s[j][k] for k in range(3))
    mean = lambda vs: tuple(sum(v[k] for v in vs) / len(vs) for k in range(3)) if vs else (0.0, 0.0, 0.0)
    apart_shares = []  # (round, RC, friend's share, host copy's share) more than 20 points apart: the games disagree
    apart_pose = []    # the friend's Sora at another height on its Living Bone, or out of its grab in Rodeo
    for n, r in enumerate(rounds + ([cur] if cur.get("368") else []), 1):
        for who, i in (("friend's", 7), ("host's", 8)):  # the parent's +0x18C bit 15 around Rodeo's start (L1602)
            near = [s for s in rows if r["368"] - 0.5 <= s[0] <= r["368"] + 1.0]
            flips = [f"{s[0] - r['368']:+.3f} -> {s[i]}" for k, s in enumerate(near) if k and s[i] != near[k - 1][i]]
            out.append(f"round {n}: the {who} Living Bone's +0x18C bit 15 from -0.5 s: {near[0][i] if near else '?'}, "
                       f"then {', '.join(flips) or 'no change'} (seconds after the host's Rodeo line)")
        out.append(f"round {n}: Rodeo at {r['368']:.2f}" + (f", Grand Cross at {r['369']:.2f}" if "369" in r else "")
                   + (f", let go at {r['go']:.2f}" if "go" in r else ""))
        for name, lo, hi in (("Rodeo", r["368"] + 0.3, r.get("369", r.get("go", r["368"] + 4))),
                             ("Grand Cross", r.get("369", 0) + 0.2, r.get("go", r.get("369", 0) + 2))):
            part = [s for s in rows if lo <= s[0] <= hi]
            if not part or (name == "Grand Cross" and "369" not in r):
                continue
            fr, hr = [rel(s, 1, 3) for s in part], [rel(s, 4, 6) for s in part]
            fm, hm = mean(fr), mean(hr)
            fs_, hs_ = sum(1 for s in part if s[2]) / len(part), sum(1 for s in part if s[5]) / len(part)
            if abs(fs_ - hs_) > 0.2:
                apart_shares.append((n, name, fs_, hs_))
            if abs(fm[1] - hm[1]) > 60:  # the ride pose lost: the friend's Sora stood under its Living Bone (F745)
                apart_pose.append(f"round {n} {name}: the friend's Sora {fm[1]:.0f} off its Living Bone in height, the "
                                  f"host's copy {hm[1]:.0f}")
            free = sum(1 for s in part if not s[9]) / len(part)
            if name == "Rodeo" and free > 0.05:
                apart_pose.append(f"round {n} Rodeo: the friend's Sora out of its grab motion in {free:.0%} of the samples")
            gap = max(math.dist(f, h) for f, h in zip(fr, hr))
            apart = mean([rel(s, 3, 6) for s in part])
            out.append(f"  {name} ({len(part)} samples): attached friend {sum(1 for s in part if s[2]) / len(part):.0%}, "
                       f"host's copy {sum(1 for s in part if s[5]) / len(part):.0%}; off its Living Bone: friend "
                       f"({fm[0]:.0f}, {fm[1]:.0f}, {fm[2]:.0f}), host's copy ({hm[0]:.0f}, {hm[1]:.0f}, {hm[2]:.0f}), "
                       f"at most {gap:.0f} apart; the friend's Living Bone off the host's ({apart[0]:.0f}, {apart[1]:.0f}, "
                       f"{apart[2]:.0f})")
            k = max(range(len(part)), key=lambda k: math.dist(fr[k], hr[k]))
            out.append(f"    the largest gap at {part[k][0] - r['368']:+.3f} s after Rodeo: friend off its Living Bone "
                       f"{tuple(round(v) for v in fr[k])}, the host's copy {tuple(round(v) for v in hr[k])}")
            for who, i in (("the friend's Sora", 1), ("the host's copy", 4)):  # one sample to the next: a blink
                steps = [(math.dist(part[k][i], part[k - 1][i]), part[k][0]) for k in range(1, len(part))]
                big = max(steps, default=(0.0, lo))
                out.append(f"    {who}: its largest step between two samples {big[0]:.0f} at {big[1] - r['368']:+.3f} s "
                           f"after Rodeo")
    fl = friend.since(marks["B"])
    hl = host.since(marks["A"])
    out.extend("   host " + l[:200] for l in hl if "before it carries" in l or "until it carried" in l or "doesn't carry" in l
               or "left its parent while it waited" in l or "trap_obj_is_attach" in l and "= 0" in l)
    keys = ("ran the host's attach", "our held Sora follows", "our Sora on our M_EX670", "detach trace", "held stream ended",
            "to carry", "doesn't carry")
    out.extend("   friend " + l[:220] for l in fl if any(k in l for k in keys))
    out.extend(f"FAIL round {n} {name}: the friend's Sora attached {f:.0%}, the host's copy {h:.0%}"
               for n, name, f, h in apart_shares)
    out.extend("FAIL " + t for t in apart_pose)
    return bool(rounds) and not apart_shares and not apart_pose and all(c.alive() and not c.crash for c in copies)


def rodeo_self(copies, a, out):
    """TODO 1.255 control (F738): the game's own Rodeo. As rodeo (save 23, warp 0A/05 programs 0/0/0, a spawned Living
    Bone), but the host's own Sora rides: put beside the Living Bone every 1.5 s and mashing triangle until its own Rodeo
    (368) runs, then nothing pressed for --seconds (default 40). The friend stands 1500 away. Sampled from outside: the
    host's Sora attached (+0x6A0) and its motion, the Living Bone's motion, place and +0x18C bit 15. Listed: each change,
    how long the ride lasted and what ended it. PASS = its Rodeo ran, no crash (the ride's end is the result)."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    sod = lambda t: (lambda lt: lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + t % 1)(time.localtime(t))
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    try:
        bench.debug("A", 12, 0x0A, 0x05, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")
        bench.wait("both in 0A/05", lambda: all(c.room() == (0x0A, 0x05) and c.game.sora() for c in copies), 90, copies)
        time.sleep(4)
        old = {c.name: bench.actor_by_model(c, "M_EX670") for c in copies}
        bench.debug("A", 19, 0xF | 1 << 16)
        bench.wait("a new Living Bone in both games", lambda: all(
            (lambda t: t and t != old[c.name])(bench.actor_by_model(c, "M_EX670")) for c in copies), 40, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    time.sleep(2)
    lb, hs = bench.actor_by_model(host, "M_EX670"), head(host)
    p = actor_pos(host, lb)
    bench.debug("B", 15, p[0] + 1500.0, p[1], p[2], fmt="<Iifff")
    rows, stop, quiet = [], threading.Event(), threading.Event()

    def sample():
        while not stop.is_set():
            try:
                rows.append((sod(time.time()), read_u(host, hs + 0x6A0) != 0, bench.motion(host, hs), bench.motion(host, lb),
                             read_u(host, lb + 0x18C) >> 15 & 1, actor_pos(host, lb)))
            except OSError:
                pass
            time.sleep(0.005)

    def press():
        while not stop.is_set():
            if quiet.is_set():
                time.sleep(0.05)
            else:
                host.game.pad.run(["tap", "triangle", "wait", "8"])

    threads = [threading.Thread(target=sample, daemon=True), threading.Thread(target=press, daemon=True)]
    for t in threads:
        t.start()
    rodeo, side, put, t0 = None, 0, 0.0, time.time()
    try:
        while time.time() - t0 < 90 and (rodeo is None or sod(time.time()) - rodeo < (40 if a.seconds == 420 else a.seconds)):
            for c in copies:
                new = c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                for l in new if c is host else []:
                    if rodeo is None and "our own command 368 on M_EX670" in l:
                        rodeo = secs(l)
                        quiet.set()
            if rodeo is None and time.time() - put >= 1.5:
                put = time.time()
                q = actor_pos(host, lb)
                dx, dz = ((150.0, 0.0), (0.0, 150.0), (-150.0, 0.0), (0.0, -150.0))[side % 4]
                side += 1
                bench.debug("A", 15, q[0] + dx, q[1], q[2] + dz, fmt="<Iifff")
            time.sleep(0.03)
        bench.screenshot(host.hwnd, os.path.join(OUT, "rodeo_self_end_A.png"))
    finally:
        stop.set()
        for t in threads:
            t.join(5)
    if rodeo is None:
        out.append("SETUP FAILURE: the host's own Rodeo never ran (no 'our own command 368' line)")
        return False
    out.append(f"the host's own Rodeo at {rodeo:.2f}")
    last = None
    for s in rows:
        key = (s[1], s[2], s[3], s[4])
        if s[0] >= rodeo - 0.3 and key != last:
            out.append(f"  {s[0] - rodeo:+.3f} s: Sora attached {int(s[1])}, motion {s[2]}; Living Bone motion {s[3]}, "
                       f"bit 15 {s[4]}, at ({s[5][0]:.0f}, {s[5][1]:.0f}, {s[5][2]:.0f})")
            last = key
            if len(out) > 80:
                out.append("  (more changes left out)")
                break
    ride = [s for s in rows if s[0] >= rodeo and s[1]]
    out.append(f"attached from {ride[0][0] - rodeo:+.3f} to {ride[-1][0] - rodeo:+.3f} s after Rodeo" if ride
               else "the host's own Sora never attached")
    hl = host.since(marks["A"])
    out.extend("   host " + l[:200] for l in hl if "trap_obj_is_attach" in l and "M_EX670" in l and "= 0" in l)
    return all(c.alive() and not c.crash for c in copies)


def rc_state(copies, a, out):
    """TODO 1.255 research (L1603, L1605): is the Living Bone's Rodeo switched off during its moves? Save 23, warp 0A/05
    as rodeo_self, a spawned Living Bone; the host's own Sora put beside it every 1.5 s. Phase 1 (--seconds, default 40)
    no presses; phase 2 (15 s) triangle mashed until its own Rodeo. Read from outside only: the Living Bone's collision
    rows once (*(+0xAB8): count, default mask +4, rows from +0x40 of 0x14: group, command, kind), then about every 5 ms
    its motion, group words +0x5A0 / +0x5A4 / +0x5A8 / +0x5AC, +0xCF0, the command lists +0x4E0 / +0x540 (8 of 12
    bytes: command, group or -1, on flag) and the skip flags of exe+0x3BA4C0 (+0x9B8, +0x120, +0x18C bit 27). Each RC
    row's 'group on' (exe+0x3BA550) and 'command on' (exe+0x3BA600) worked out as the game does (Ghidra), listed with
    the motion each time one changes. PASS = the rows were read and samples taken; the table is the result."""
    import struct
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    sod = lambda t: (lambda lt: lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + t % 1)(time.localtime(t))
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    try:
        bench.debug("A", 12, 0x0A, 0x05, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")
        bench.wait("both in 0A/05", lambda: all(c.room() == (0x0A, 0x05) and c.game.sora() for c in copies), 90, copies)
        time.sleep(4)
        old = bench.actor_by_model(host, "M_EX670")
        bench.debug("A", 19, 0xF | 1 << 16)
        bench.wait("a new Living Bone", lambda: (lambda t: t and t != old)(bench.actor_by_model(host, "M_EX670")), 40, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    time.sleep(2)
    import peek
    lb, hs = bench.actor_by_model(host, "M_EX670"), head(host)
    p = actor_pos(host, lb)
    bench.debug("B", 15, p[0] + 2500.0, p[1], p[2], fmt="<Iifff")  # the friend out of the way
    data = read_u(host, lb + 0xAB8, 8)
    count, default = read_u(host, data, 4), read_u(host, data + 4, 4)
    rows = []
    for k in range(min(count, 32)):
        raw = peek.read(host.game.h, data + 0x40 + 0x14 * k, 0x14)
        rows.append((raw[0], struct.unpack_from("<H", raw, 2)[0], raw[4]))
    out.append(f"collision rows ({count}, default mask {default:#x}): "
               + ", ".join(f"(group {g}, command {c}, kind {k})" for g, c, k in rows))
    rcs = [(g, c, k) for g, c, k in rows if k in (5, 14)]
    if not rcs:
        out.append("no RC rows (kinds 5 / 14)")
        return False

    def lists(base):
        raw = peek.read(host.game.h, lb + base, 96)
        return [struct.unpack_from("<iiB", raw, 12 * i) for i in range(8)]

    def match(lst, cmd, g, on):
        return any(c == cmd and (gg == -1 or gg == g) and (f != 0) == on for c, gg, f in lst)

    def group_on(w, g):  # exe+0x3BA550
        bit = lambda word: word >> (g & 31) & 1
        if bit(w[0x5A8]): return 1
        if bit(w[0x5AC]): return 0
        if bit(w[0x5A0]): return 1
        if bit(w[0x5A4]): return 0
        return default >> (g & 31) & 1

    def cmd_on(l540, l4e0, w, cmd, g):  # exe+0x3BA600
        if match(l540, cmd, g, True): return 1
        if match(l540, cmd, g, False): return 0
        if match(l4e0, cmd, g, True): return 1
        if match(l4e0, cmd, g, False): return 0
        return group_on(w, g)

    samples, stop, mash = [], threading.Event(), threading.Event()

    def sample():
        while not stop.is_set():
            try:
                w = {o: read_u(host, lb + o) for o in (0x5A0, 0x5A4, 0x5A8, 0x5AC)}
                l540, l4e0, cf0 = lists(0x540), lists(0x4E0), read_u(host, lb + 0xCF0)
                skip = (read_u(host, lb + 0x9B8), read_u(host, lb + 0x120), read_u(host, lb + 0x18C) >> 27 & 1)
                state = tuple((group_on(w, g), cmd_on(l540, l4e0, w, c if k == 5 else cf0, g)) for g, c, k in rcs)
                samples.append((sod(time.time()), bench.motion(host, lb), state, cf0, skip,
                                tuple(e for e in l540 + l4e0 if e[0])))
            except OSError:
                pass
            time.sleep(0.005)

    def press():
        while not stop.is_set():
            if mash.is_set():
                host.game.pad.run(["tap", "triangle", "wait", "8"])
            else:
                time.sleep(0.05)

    threads = [threading.Thread(target=sample, daemon=True), threading.Thread(target=press, daemon=True)]
    for t in threads:
        t.start()
    phase1 = 40 if a.seconds == 420 else a.seconds
    rodeo, side, put, t0 = None, 0, 0.0, time.time()
    try:
        while time.time() - t0 < phase1 + 15 and rodeo is None:
            if time.time() - t0 >= phase1:
                mash.set()
            for c in copies:
                new = c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                for l in new if c is host else []:
                    if "our own command 368 on M_EX670" in l:
                        rodeo = secs(l)
            if time.time() - put >= 1.5:
                put = time.time()
                q = actor_pos(host, lb)
                dx, dz = ((150.0, 0.0), (0.0, 150.0), (-150.0, 0.0), (0.0, -150.0))[side % 4]
                side += 1
                bench.debug("A", 15, q[0] + dx, q[1], q[2] + dz, fmt="<Iifff")
            time.sleep(0.03)
        time.sleep(0.5)
    finally:
        stop.set()
        for t in threads:
            t.join(5)
    t_mash = sod(t0 + phase1)
    out.append(f"{len(samples)} samples; RC rows {rcs}; mashing from {t_mash - sod(t0):.1f} s"
               + (f", the host's own Rodeo at {rodeo - sod(t0):.2f} s" if rodeo else ", no Rodeo"))
    last, by_motion = None, {}
    for s in samples:
        by_motion.setdefault(s[1], set()).add(s[2])
        key = (s[1], s[2], s[3], s[4][0] >> 2 & 1, s[4][0] >> 10 & 1, s[4][1] >> 6 & 1, s[4][2], s[5])
        if key != last:
            out.append(f"  {s[0] - sod(t0):7.3f} s: motion {s[1]}, (group on, command on) per RC row {s[2]}, +0xCF0 "
                       f"{s[3]}, skip flags +0x9B8 b2 {key[3]} b10 {key[4]}, +0x120 b6 {key[5]}, +0x18C b27 {s[4][2]}"
                       + (f", lists {list(s[5])}" if s[5] else ""))
            last = key
            if len(out) > 150:
                out.append("  (more changes left out)")
                break
    out.append("by motion: " + "; ".join(f"{m}: {sorted(v)}" for m, v in sorted(by_motion.items())))
    return bool(samples) and all(c.alive() and not c.crash for c in copies)


def luxord_cards(copies, a, out):
    """TODO 1.257 case A (your bench design 2026-11-19: a card pick is lost 3 times in 4): Data Luxord (save 45, the
    warp to 0x12/0x0E program 101; later tries warp out to 0x12/0x12 and back in). Each try: both Soras mash triangle
    from the arrival; the host's also attacks (cross) so his time gauge doesn't empty first. Until his card game (458)
    starts, every 2 s the host's Sora is put 80 from Luxord and the friend's 300 (with a lock-on combo it chased him and
    took the card game in the first run, so it only mashes triangle until then). A win (459) restarts the try; at the
    host's loss (460) the friend's Sora is put 120 from Luxord every 2 s and locks on and combos; the loss is watched
    until the host's Sora leaves the
    penalty and its die / card are over and it is P_EX100 again (45 s at most; the friend swings all along), sampling
    the player slot's model, motion and +0x18C bit 14 (the capture). A host loss is tested only
    if the copy's swings were stopped on him ('no hit (no reaction') after it, else NOT TESTED. Tested, it passes with
    Luxord's own die / card call in the host's trap log (`trap_player_dice` / `trap_player_capture_form` trap lines),
    none sent to the friend for its Sora meanwhile (the wrong Sora), no `trap_mission_failed`, its Sora out of 254, and
    each `func_obj_control_off` of his followed by his `control_on` (F753: a hit after the die cut it).
    A friend's loss is reported only. Up to --pick tries (default 6); stops after 2 tested host losses."""
    if a.race:
        return luxord_race(copies, a, out)
    import threading
    host, friend = copies
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    tries, tested, untested = a.pick if a.pick else 6, [], 0
    for n in range(1, tries + 1):
        try:
            if n == 1:
                load_both(copies, 45, (0x12, 0x12))
            else:
                luxord_retry(copies)
            marks = {c.name: c.mark() for c in copies}
            bench.warp(0x12, 0x0E, 101)
            bench.wait("both in Data Luxord", lambda: all(c.room() == (0x12, 0x0E) and c.game.sora() for c in copies),
                       90, copies)
        except bench.Failed as e:
            out.append(f"SETUP FAILURE (try {n}): {e}")
            return False
        stop, go = threading.Event(), threading.Event()

        def mash(c, combo):
            slot = lambda b: ["hold", b, "35", "wait", "35"]
            seq = ([t for b in ("r1", "cross", "cross", "cross") for t in slot(b + "+triangle") + slot("triangle") * 3]
                   if combo else ["tap", "triangle", "wait", "8", "tap", "cross", "wait", "8"])
            while combo and not go.is_set() and not stop.is_set():
                c.game.pad.run(["tap", "triangle", "wait", "8"])  # ~4 a second until the host's loss
            while not stop.is_set():
                c.game.pad.run(seq)

        def put_near(c, d):
            lux = bench.actor_by_model(c, "B_EX150_LV99")
            if lux:
                p = actor_pos(c, lux)
                bench.debug(c.name, 15, p[0] + d, p[1], p[2], fmt="<Iifff")

        threads = [threading.Thread(target=mash, args=(host, False), daemon=True),
                   threading.Thread(target=mash, args=(friend, True), daemon=True)]
        for t in threads:
            t.start()
        result, started, t0, put, seen, samples, back = None, False, time.time(), 0.0, marks["A"], [], False
        try:
            while time.time() - t0 < 90 and result is None:
                for c in copies:
                    if c.crash or not c.alive():
                        raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                new = host.since(seen)
                seen += len(new)
                for l in new:
                    if "command 458 " in l and "B_EX150_LV99" in l:
                        started = True
                    for who, key in (("host", "our own command"), ("friend", "the friend used command")):
                        if result is None and f"{key} 459 " in l and "B_EX150_LV99" in l:
                            result = (who, "won", secs(l))
                        elif result is None and f"{key} 460 " in l and "B_EX150_LV99" in l:
                            result = (who, "lost", secs(l))
                if not started and time.time() - put >= 2.0:  # never moved while his card game holds one
                    put = time.time()
                    put_near(host, 80.0)
                    put_near(friend, 300.0)
                time.sleep(0.03)
            if result and result[:2] == ("host", "lost"):
                end, formed = time.time() + 45, False
                go.set()
                while time.time() < end:  # the friend swings until the host's Sora is back from the die / card
                    if time.time() - put >= 2.0:
                        put = time.time()
                        put_near(friend, 120.0)
                    try:
                        sora = head(host)  # the die takes the player slot: re-read each sample
                        mdl = bench.model_of(host, sora)
                        m, f = read_u(host, sora + 0x180), read_u(host, sora + 0x18C)
                    except OSError:
                        mdl, m, f = "?", -1, 0
                    row = (round(time.time() - t0, 1), mdl, m if m < 0x80000000 else m - 0x100000000, f >> 14 & 1)
                    if not samples or row[1:] != samples[-1][1:]:
                        samples.append(row)
                    if mdl not in ("P_EX100", "?"):
                        formed = True
                    elif formed and mdl == "P_EX100":
                        back = True
                        time.sleep(2)
                        break
                    time.sleep(0.25)
        except bench.Failed as e:
            stop.set()
            out.append(f"FAIL (try {n}): {e}")
            return False
        finally:
            stop.set()
            for t in threads:
                t.join(5)
        if not result:
            out.append(f"try {n}: no card pick in 90 s")
            continue
        who, what, at = result
        out.append(f"try {n}: the {who}'s Sora {what} his card game at {at:.2f}")
        if (who, what) != ("host", "lost"):
            continue
        after = [l for l in host.since(marks["A"]) if l[:2].isdigit() and secs(l) >= at]
        own = [l for l in after if " trap " in l[:20] and ("trap_player_dice" in l or "trap_player_capture_form" in l)]
        wrong = [l for l in after if "sent to the friend for its Sora" in l and "B_EX150_LV99" in l]
        failed = [l for l in after if "trap_mission_failed" in l]
        nohit = [l for l in after if "while it holds our Sora: no hit" in l]
        bycap = [l for l in after if "holds our Sora by its capture" in l]
        coff = len([l for l in after if " trap 1/42 func_obj_control_off" in l and "B_EX150_LV99" in l])
        con = len([l for l in after if " trap 1/43 func_obj_control_on" in l and "B_EX150_LV99" in l])
        left = back
        on_at = next((i for i, l in enumerate(after) if " trap 1/43 func_obj_control_on" in l and "B_EX150_LV99" in l),
                     len(after))
        early = [l for l in after[:on_at] if "hit: B_EX150_LV99" in l and "attacker the Sora copy" in l]
        changes = samples
        out.append(f"  own die / card calls {len(own)}, sent to the friend meanwhile {len(wrong)}, Game Over "
                   f"{len(failed)}, swings stopped {len(nohit)}, 'by its capture' lines {len(bycap)}, back to P_EX100 {left}, "
                   f"his control off {coff} / on {con} (F753: on must follow), the copy's hits on him before his "
                   f"control_on {len(early)}")
        out.append(f"  host's player (s, model, motion, capture bit) changes: {changes[:14]}")
        out.extend("    " + l[:200] for l in (own + wrong + failed)[:6])
        if not nohit:
            untested += 1
            out.append("  NOT TESTED: no swing of the copy stopped on him after the loss")
            continue
        tested.append(bool(own) and not wrong and not failed and left and con >= coff and not early)
        if len(tested) >= 2:
            break
    out.append(f"host losses tested: {tested}, untested {untested}")
    if not tested:
        out.append("NOT TESTED: no host loss with the copy swinging at him")
        return False
    return all(tested) and all(c.alive() and not c.crash for c in copies)


def luxord_retry(copies):
    """Out of Data Luxord to 0x12/0x12 for the next try. Our warp waits until it's safe and gives up after 15 s (his
    die / card penalty freezes entities a while: 'debug warp NOT done', run 2026-11-19), so it is asked up to 4 times."""
    for _ in range(4):
        bench.warp(0x12, 0x12)
        try:
            bench.wait("both out of Data Luxord", lambda: all(c.room() == (0x12, 0x12) and c.game.sora() for c in copies),
                       20, copies)
            time.sleep(4)
            return
        except bench.Failed:
            pass
    raise bench.Failed("still in Data Luxord after 4 warps")


def luxord_race(copies, a, out):
    """luxord_cards --race (your idea 2026-11-19: both press triangle at the same time). Data Luxord as luxord_cards;
    both Soras put 80 from Luxord on opposite sides every 2 s until his card game (458) starts; both pads press triangle
    only (your pick: no attacks), in step (a barrier before each press). Presses: the host's own (host log), the
    friend's as its game sent it (friend log 'we used command 458'), as the host's game ran it ('the friend used') or
    dropped it ('not run: a script holds our player'), our own dropped ('our command 458 ... dropped'). A try counts
    when both 458 presses come within 0.5 s; it is watched 45 s: Luxord's die / card calls on each Sora, each Sora's
    motion and capture bit (254 = the penalty), any `trap_mission_failed`. DECISIONS row 177 (the first press wins
    while a script holds that player): a counted try passes when one card game ran from the pair (within 1 s of it)
    and the other press was dropped, the winner turned into a die or card (or won its pick), neither Sora left in 254,
    no Game Over (the pads stop 1.5 s after the pair). TODO 1.258: the host's pad presses 15-45 ms after the friend's (a different delay each try) so its
    press may land in the frame the friend's arrives in (about 0.03 s after it is sent); 'our command 458 ... dropped'
    within a frame of 'the friend used command 458' is that case. Up to --pick tries (default 8), stops after 2 of
    those. PASS = every counted try passed (the report says how many reached the same frame)."""
    import threading
    host, friend = copies
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    offsets, results = [15, 25, 35, 45, 20, 30, 40, 10], []
    for n in range(1, (a.pick or 8) + 1):
        delay = offsets[(n - 1) % len(offsets)]
        try:
            if n == 1:
                load_both(copies, 45, (0x12, 0x12))
            else:
                luxord_retry(copies)
            marks = {c.name: c.mark() for c in copies}
            bench.warp(0x12, 0x0E, 101)
            bench.wait("both in Data Luxord", lambda: all(c.room() == (0x12, 0x0E) and c.game.sora() for c in copies),
                       90, copies)
        except bench.Failed as e:
            out.append(f"SETUP FAILURE (try {n}): {e}")
            break
        stop, step = threading.Event(), threading.Barrier(2)

        def mash(c):
            while not stop.is_set():
                try:
                    step.wait(timeout=2)
                except threading.BrokenBarrierError:
                    return
                if c is host:
                    time.sleep(delay / 1000)
                c.game.pad.run(["tap", "triangle", "wait", "8"])

        threads = [threading.Thread(target=mash, args=(c,), daemon=True) for c in copies]
        for t in threads:
            t.start()
        keys = {"A": (("host", "our own command {} on B_EX150_LV99"), ("friend", "the friend used command {} of our B_EX150_LV99"),
                      ("friend dropped", "the friend's command {} on host#"), ("host dropped", "our command {} on B_EX150_LV99 dropped")),
                "B": (("friend sent", "we used command {} of our B_EX150_LV99"),)}
        presses, t0, put, first, samples, last = [], time.time(), 0.0, None, [], None
        seen = dict(marks)
        try:
            while time.time() - t0 < 90 and (first is None or time.time() - first < 45):
                for c in copies:
                    if c.crash or not c.alive():
                        raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                    new = c.since(seen[c.name])
                    seen[c.name] += len(new)
                    for l in new:
                        for who, key in keys[c.name]:
                            for cmd in (458, 459, 460):
                                if key.format(cmd) in l and (who != "friend dropped" or "not run" in l):
                                    presses.append((secs(l), who, cmd))
                                    if cmd == 458 and first is None and who in ("host", "friend sent"):
                                        first = time.time()
                if first is None and time.time() - put >= 2.0:
                    put = time.time()
                    for c, d in ((host, 80.0), (friend, -80.0)):
                        lux = bench.actor_by_model(c, "B_EX150_LV99")
                        if lux:
                            p = actor_pos(c, lux)
                            bench.debug(c.name, 15, p[0] + d, p[1], p[2], fmt="<Iifff")
                if first is not None and time.time() - first > 1.5 and not stop.is_set():
                    stop.set()  # no new card game inside the window: 254 at its end would be a fresh one (run 36796 try 2)
                    step.abort()
                if first is not None:
                    row = [round(time.time() - first, 1)]
                    for c in copies:
                        try:
                            s = head(c)
                            m, f = read_u(c, s + 0x180), read_u(c, s + 0x18C)
                            row += [m if m < 0x80000000 else m - 0x100000000, f >> 14 & 1]
                        except OSError:
                            row += [-9, 0]
                    if not samples or row[1:] != list(samples[-1][1:]):
                        samples.append(tuple(row))
                    last = row
                time.sleep(0.25)
        except bench.Failed as e:
            out.append(f"FAIL (try {n}): {e}")
            return False
        finally:
            stop.set()
            step.abort()
            for t in threads:
                t.join(5)
        out.append(f"try {n}: the host's pad {delay} ms after the friend's")
        presses.sort()
        base = presses[0][0] if presses else 0
        out.append(f"  presses (s after the first, who, command): {[(round(t - base, 2), w, c) for t, w, c in presses]}")
        h = next((t for t, w, c in presses if w in ("host", "host dropped") and c == 458), None)  # a dropped press logs no 'our own'
        fs = next((t for t, w, c in presses if w == "friend sent" and c == 458), None)
        if h is None or fs is None or abs(h - fs) > 0.5:
            out.append("  not both at once: next try")
            continue
        at = min(h, fs)
        pair = [(w, c) for t, w, c in presses if t - at <= 1.0]
        ran = [w for w, c in pair if c == 458 and w in ("host", "friend")]
        dropped = [w for w, c in pair if c == 458 and w.endswith("dropped")]
        winner = ran[0] if len(ran) == 1 else None
        after = [l for l in host.since(marks["A"]) if l[:2].isdigit() and secs(l) >= at]
        hostdie = [l for l in after if " trap " in l[:20] and ("trap_player_dice" in l or "trap_player_capture_form" in l)]
        frienddie = [l for l in after if "sent to the friend for its Sora" in l and "B_EX150_LV99" in l]
        failed = [l for l in after if "trap_mission_failed" in l]
        won = any(w == winner and c == 459 for w, c in pair) or any(
            w == winner and c == 459 for t, w, c in presses if t >= at)
        turned = bool(hostdie) if winner == "host" else bool(frienddie) if winner == "friend" else False
        stuck = {name: bool(last) and last[1 + 2 * i] == 254 for i, name in enumerate(("host", "friend"))}
        out.append(f"  card games run from the pair: {ran}; presses dropped: {dropped}; winner {winner}, turned {turned}, "
                   f"won its pick {won}; die / card calls: host's Sora {len(hostdie)}, friend's {len(frienddie)}; left in "
                   f"254 at the end: {stuck}; Game Over {len(failed)}")
        out.append(f"  (s, host motion, host capture, friend motion, friend capture) changes: {samples[:14]}")
        out.extend("    " + l[:200] for l in (hostdie + frienddie + failed)[:8])
        fu = next((t for t, w, c in presses if w == "friend" and c == 458 and t - at <= 1.0), None)
        hd = next((t for t, w, c in presses if w == "host dropped" and c == 458 and t - at <= 1.0), None)
        same = fu is not None and hd is not None and abs(hd - fu) <= 0.017
        ok = (winner is not None and len(dropped) == 1 and (turned or won) and not any(stuck.values()) and not failed
              and all(c.alive() and not c.crash for c in copies))
        out.append(f"  try passes: {ok}; the same-frame case (our press dropped within a frame of the friend's): {same}")
        results.append((ok, same))
        if sum(sm for _, sm in results) >= 2:
            break
    if not results:
        out.append("NOT TESTED: no try with both 458 presses within 0.5 s")
        return False
    out.append(f"counted tries {len(results)}, passed {sum(o for o, _ in results)}, same-frame case reached "
               f"{sum(sm for _, sm in results)}")
    return all(o for o, _ in results) and all(c.alive() and not c.crash for c in copies)


def luxord_mirror(copies, a, out):
    """TODO 1.257 case B (your pick 2026-11-20: research before the two-PC run): Luxord holds the friend's Sora while
    the host's hits him. The friend's Sora is put 80 from Luxord and taps triangle; the host's waits 300 away, its pad
    idle. When the friend's 458 runs in the host's game, the friend's pad stops (no pick: his penalty, a die or card,
    always follows) and the host's Sora is put 120 from him every 2 s and locks on and attacks with cross only (no
    triangle: 624 on his cards costs the shared gauge, F756) until the friend's player is P_EX100 again (45 s at most).
    A try where the host's 458 runs first restarts. Read from the host log after the friend's 458: his die / card calls
    sent to the friend, his control off / on, `trap_damage_is_reaction` = 1 by him before his last control_on (our
    hits' reactions), the fix's 'held copy hit' lines, any `trap_mission_failed`; at the end the host's frozen
    groups (`exe+0x2A171E8`, 0 = control back). PASS = the die or card sent, the friend's player P_EX100 again, his
    control on after each off, no reaction before it, no Game Over, the host's control back. Up to --pick tries
    (default 6), stops at the first counted one."""
    import threading
    host, friend = copies
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    for n in range(1, (a.pick or 6) + 1):
        try:
            if n == 1:
                load_both(copies, 45, (0x12, 0x12))
            else:
                luxord_retry(copies)
            marks = {c.name: c.mark() for c in copies}
            bench.warp(0x12, 0x0E, 101)
            bench.wait("both in Data Luxord", lambda: all(c.room() == (0x12, 0x0E) and c.game.sora() for c in copies),
                       90, copies)
        except bench.Failed as e:
            out.append(f"SETUP FAILURE (try {n}): {e}")
            return False
        stop, go = threading.Event(), threading.Event()

        def host_pad():
            slot = lambda b: ["hold", b, "35", "wait", "35"]
            seq = [t for b in ("r1+cross", "cross", "cross", "cross") for t in slot(b)]
            while not go.is_set() and not stop.is_set():
                time.sleep(0.05)
            while not stop.is_set():
                host.game.pad.run(seq)

        def friend_pad():
            while not go.is_set() and not stop.is_set():
                friend.game.pad.run(["tap", "triangle", "wait", "8"])

        def put_near(c, d):
            lux = bench.actor_by_model(c, "B_EX150_LV99")
            if lux:
                p = actor_pos(c, lux)
                bench.debug(c.name, 15, p[0] + d, p[1], p[2], fmt="<Iifff")

        threads = [threading.Thread(target=host_pad, daemon=True), threading.Thread(target=friend_pad, daemon=True)]
        for t in threads:
            t.start()
        who, at, t0, put, seen, samples, back = None, None, time.time(), 0.0, marks["A"], [], False
        try:
            while time.time() - t0 < 90 and who is None:
                for c in copies:
                    if c.crash or not c.alive():
                        raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                new = host.since(seen)
                seen += len(new)
                for l in new:
                    if who is None and "our own command 458 on B_EX150_LV99" in l:
                        who, at = "host", secs(l)
                    elif who is None and "the friend used command 458 of our B_EX150_LV99" in l:
                        who, at = "friend", secs(l)
                if who is None and time.time() - put >= 2.0:
                    put = time.time()
                    put_near(friend, 80.0)
                    put_near(host, 300.0)
                time.sleep(0.03)
            if who == "friend":
                end, formed = time.time() + 45, False
                go.set()
                while time.time() < end:
                    if time.time() - put >= 2.0:
                        put = time.time()
                        put_near(host, 120.0)
                    try:
                        s = head(friend)
                        mdl = bench.model_of(friend, s)
                        m = read_u(friend, s + 0x180)
                    except OSError:
                        mdl, m = "?", -1
                    row = (round(time.time() - t0, 1), mdl, m if m < 0x80000000 else m - 0x100000000)
                    if not samples or row[1:] != samples[-1][1:]:
                        samples.append(row)
                    if mdl not in ("P_EX100", "?"):
                        formed = True
                    elif formed and mdl == "P_EX100":
                        back = True
                        time.sleep(2)
                        break
                    time.sleep(0.25)
        except bench.Failed as e:
            out.append(f"FAIL (try {n}): {e}")
            return False
        finally:
            stop.set()
            go.set()
            for t in threads:
                t.join(5)
        if who is None:
            out.append(f"try {n}: no card game in 90 s")
            continue
        out.append(f"try {n}: the {who}'s 458 ran first at {at:.2f}")
        if who != "friend":
            continue
        time.sleep(1)
        frozen = read_u(host, host.game.base + 0x2A171E8)
        after = [l for l in host.since(marks["A"]) if l[:2].isdigit() and secs(l) >= at]
        sent = [l for l in after if "sent to the friend for its Sora" in l and "B_EX150_LV99" in l and
                ("trap_player_dice" in l or "trap_player_card" in l or "capture_form" in l)]
        coff = len([l for l in after if " trap 1/42 func_obj_control_off" in l and "B_EX150_LV99" in l])
        con = len([l for l in after if " trap 1/43 func_obj_control_on" in l and "B_EX150_LV99" in l])
        ons = [i for i, l in enumerate(after) if " trap 1/43 func_obj_control_on" in l and "B_EX150_LV99" in l]
        react = [l for l in after[:ons[-1] if ons else len(after)]
                 if "trap_damage_is_reaction" in l and "= 1 by B_EX150_LV99" in l]
        held = [l for l in after if "held copy hit:" in l]
        failed = [l for l in after if "trap_mission_failed" in l]
        out.append(f"  die / card calls sent to the friend {len(sent)}; his control off {coff} / on {con}; reactions "
                   f"(is_reaction = 1) before his last control_on {len(react)}; our hits logged on him {len(held)}; Game "
                   f"Over {len(failed)}; the friend's player back to P_EX100 {back}; our frozen groups at the end {frozen:#x}")
        out.append(f"  friend's player (s, model, motion) changes: {samples[:12]}")
        out.extend("    " + l[13:240] for l in held[:12])
        out.extend("    " + l[13:200] for l in (react + failed)[:6])
        return (bool(sent) and back and con >= coff and not react and not failed and frozen == 0
                and all(c.alive() and not c.crash for c in copies))
    out.append("NOT TESTED: the friend's 458 never ran first")
    return False


def rapid_blow(copies, a, out):
    """TODO 1.256 (F739, F740): the friend's Rapid Blow (370) on an Aerial Knocker (M_EX780, obj 0x14) spawned in front
    of the host's Sora: save 23, warp 0A/05 programs 0/0/0 as rodeo, no enemy god mode (the strike kills it, and its
    drop comes as it dies). Rapid Blow comes in the knocker's knock-back after hits (motions 12 / 13; two PCs
    2026-11-19), so until it runs the friend's Sora is put beside the knocker every 1.5 s, locks on and attacks with
    triangle mashed (as fight --triangle); then nothing pressed. The host's Sora is kept 2500 away so its party (which
    killed the knocker in 1 s) follows it out; 3 knockers at a time (they're brittle), 3 more when none is left (5 times at most). Sampled from outside: the friend's Sora's motion, its grab bit (+0x18C bit 14) and
    height. Listed: each change, the friend's grab lines. PASS = Rapid Blow ran, a host call ran on the dying knocker
    (its line), no 'not run' grab line, the friend's Sora out of the grab within 3 s of the host's let go, no crash."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    sod = lambda t: (lambda lt: lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec + t % 1)(time.localtime(t))
    secs = lambda s: int(s[0:2]) * 3600 + int(s[3:5]) * 60 + float(s[6:12])
    try:
        bench.debug("A", 12, 0x0A, 0x05, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")
        bench.wait("both in 0A/05", lambda: all(c.room() == (0x0A, 0x05) and c.game.sora() for c in copies), 90, copies)
        time.sleep(4)
        bench.debug("A", 19, 0x14 | 3 << 16)
        bench.wait("an Aerial Knocker in both games", lambda: all(bench.actor_by_model(c, "M_EX780") for c in copies),
                   40, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    time.sleep(2)
    kn, fs, hs = bench.actor_by_model(friend, "M_EX780"), head(friend), head(host)
    home = actor_pos(host, hs)
    away = (home[0] + 2500.0, home[1], home[2])
    bench.debug("A", 15, *away, fmt="<Iifff")
    rows, stop, quiet = [], threading.Event(), threading.Event()

    def sample():
        while not stop.is_set():
            try:
                rows.append((sod(time.time()), bench.motion(friend, fs), read_u(friend, fs + 0x18C) >> 14 & 1,
                             f3(friend, fs + 0x70)[1]))
            except OSError:
                pass
            time.sleep(0.005)

    def press():
        while not stop.is_set():
            if quiet.is_set():
                time.sleep(0.05)
            else:  # 70 ms slots (a tap takes 250 ms): each attack with triangle, then 3 of triangle alone
                slot = lambda b: ["hold", b, "35", "wait", "35"]
                friend.game.pad.run([t for b in ("r1", "cross", "cross", "cross")
                                     for t in slot(b + "+triangle") + slot("triangle") * 3])

    threads = [threading.Thread(target=sample, daemon=True), threading.Thread(target=press, daemon=True)]
    for t in threads:
        t.start()
    used = go = None
    side, put, spawns, t0 = 0, 0.0, 1, time.time()
    try:
        while time.time() - t0 < 120 and (go is None or sod(time.time()) - go < 4):
            for c in copies:
                new = c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                for l in new:
                    if c is friend and used is None and "we used command 370 of our M_EX780" in l:
                        used = secs(l)
                        quiet.set()
                    elif c is host and used is not None and go is None and "'let go' sent to the friend" in l:
                        go = secs(l)
                        bench.screenshot(friend.hwnd, os.path.join(OUT, "rapid_blow_after_B.png"))
            if used is None and time.time() - put >= 1.5:
                put = time.time()
                bench.debug("A", 15, *away, fmt="<Iifff")
                now_kn = bench.actor_by_model(friend, "M_EX780")
                if not now_kn:
                    if spawns == 5:
                        break
                    spawns += 1  # in front of the host's Sora: back home for it, then away again
                    bench.debug("A", 15, *home, fmt="<Iifff")
                    time.sleep(0.3)
                    bench.debug("A", 19, 0x14 | 3 << 16)
                    time.sleep(0.5)
                    bench.debug("A", 15, *away, fmt="<Iifff")
                    continue
                kn = now_kn
                p = actor_pos(friend, kn)
                dx, dz = ((100.0, 0.0), (0.0, 100.0), (-100.0, 0.0), (0.0, -100.0))[side % 4]
                side += 1
                bench.debug("B", 15, p[0] + dx, p[1], p[2] + dz, fmt="<Iifff")
            time.sleep(0.03)
    finally:
        stop.set()
        for t in threads:
            t.join(5)
    if used is None:
        out.append(f"SETUP FAILURE: the friend's Rapid Blow never ran (no 'we used command 370' line; {spawns} knockers)")
        return False
    out.append(f"the friend's Rapid Blow at {used:.2f}" + (f", the host's let go at {go - used:+.3f} s" if go else ", no let go"))
    last = None
    for s in rows:
        if s[0] >= used - 0.2 and (s[1], s[2]) != last:
            out.append(f"  {s[0] - used:+.3f} s: the friend's Sora motion {s[1]}, grab bit {s[2]}, height {s[3]:.0f}")
            last = (s[1], s[2])
            if len(out) > 60:
                out.append("  (more changes left out)")
                break
    fl = friend.since(marks["B"])
    grab = [l for l in fl if "grab sync:" in l and secs(l) >= used]
    out.extend("   friend " + l[:220] for l in grab[:30])
    skipped = [l for l in grab if "not run" in l]
    freed = go is not None and any(s[0] >= go and s[0] - go <= 3 and not s[2] for s in rows)
    dying = any("killed with the host's and still dying" in l for l in grab)
    out.append(f"grab calls not run in the friend's game: {len(skipped)}; run on a dying knocker: {dying}; its Sora out "
               f"of the grab within 3 s of the let go: {freed}")
    if not dying and not skipped and freed:  # its knocker was still alive at the drop: the fix wasn't needed this run
        out.append("NOT TESTED: the friend's knocker wasn't killed before its drop came (the normal path ran fine); rerun")
    return not skipped and dying and freed and all(c.alive() and not c.crash for c in copies)


def twin_lk(copies, a, out):
    """TODO 5.26 (F731, L1601): the cutscene twin in Pride Lands. Save 23, then the warp to 0A/00 with programs 1/0/1
    (lk_event_106: P_LK100 and the close-up H_EX640). PASS = both games log the scene as twinned (not 'left as is'),
    no crash; shots of both windows at +4 s and +9 s for your look."""
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x0A, 0x00, 0, 0, 1, 0, 1, 0, fmt="<IiBBBBHHHH")
    line = lambda c: next(iter(find(c.since(marks[c.name]), "cutscene twin: scene 'lk_event_106'")), None)
    try:
        bench.wait("lk_event_106 in both", lambda: all(line(c) for c in copies), 90, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    t = time.time()
    for k in (4, 9):
        time.sleep(max(0.0, t + k - time.time()))
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"twin_lk_{k}s_{c.name}.png"))
    ok = True
    for c in copies:
        l = line(c)
        out.append(f"{c.name}: {l[13:200]}")
        ok = ok and "twinned" in l and c.alive() and not c.crash
    return ok


def capture_probe(c, x):
    """TODO 1.250 (L1593): one sample of actor x's fall (+0x118), land (+0x11C), motion (+0x180), +0x194 and its
    capture source (+0x2B8 = ctl+0x160); for a source, its entry count (+4) and whether the land and the motion have an
    entry there (exe+0x40D7A0's lookup: entry n at +0x10 + 16 n, its handle at +8, a handle has bit 31 set)."""
    raw = peek.read(c.game.h, x + 0x118, 0x2C0 - 0x118)
    fall, land = struct.unpack_from("<ii", raw, 0)
    mot = struct.unpack_from("<i", raw, 0x180 - 0x118)[0]
    f194 = struct.unpack_from("<I", raw, 0x194 - 0x118)[0]
    src = struct.unpack_from("<Q", raw, 0x2B8 - 0x118)[0]
    if not src:
        return (fall, land, mot, f194 >> 4 & 1, 0, 0, None, None)
    n = read_u(c, src + 4)
    has = lambda m: 0 <= m < n and bool(read_u(c, src + 0x10 + 16 * m + 8) & 0x80000000)
    return (fall, land, mot, f194 >> 4 & 1, src, n, has(land), has(mot))


def belt_capture(copies, a, out):
    """TODO 1.250 (L1593), measured from outside: Oogie's room (save 22 + the warp to 0E/09 programs 0x37); the host
    stands, the friend is put by its nearest belt every 4 s (--air: at the belt's height, in the air as in the crash
    run; else at its own height) and presses triangle (belt jumps). A thread samples, every ~2 ms, the friend's Sora
    and the host's copy of it (capture_probe) and keeps each change. FAIL = a sample with a capture source on, a land
    motion >= 0 and not 5, +0x194 bit 4 clear, and no entry for that land in the source, on the friend's Sora (the
    state in which its land handler faults, exe+0x3CB940 -> exe+0x3C7190; the host's copy is listed, not judged). SETUP FAILURE = no capture seen, or the capture's own motion has
    no entry (the layout read would be wrong). --seconds: the jumping time (default 60)."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0xE, 9, 0x37)
    bench.wait("both in 0E/09", lambda: all(c.room() == (0xE, 9) and c.game.sora() for c in copies), 90, copies)
    time.sleep(10)  # the room's start scene
    host.lines()
    made = [m for m in (re.search(r"Sora copy .* made \(actor exe\+0x([0-9A-Fa-f]+)", l) for l in host.all) if m]
    hcopy = host.game.base + int(made[-1].group(1), 16) if made else 0
    who = {"friend's Sora": (friend, player(friend)), "host's copy": (host, hcopy)}
    out.append("actors: " + ", ".join(f"{k} {x:#x}" for k, (c, x) in who.items()))
    seen = {k: [] for k in who}
    stop = threading.Event()

    def sample():
        while not stop.is_set():
            for k, (c, x) in who.items():
                try:
                    s = capture_probe(c, x)
                except Exception:
                    continue
                ch = seen[k]
                if not ch or ch[-1][1] != s:
                    ch.append((time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}", s))
            time.sleep(0.002)

    th = threading.Thread(target=sample, daemon=True)
    th.start()
    jumps = ["hold", "triangle", "35", "wait", "35"] * 8
    t0, put = time.time(), 0.0
    try:
        while time.time() < t0 + (60 if a.seconds == 420 else a.seconds):
            if time.time() - put >= 4:
                put = time.time()
                me = friend.game.sora()
                belts = [q for q in (actor_pos(friend, act) for act, model in bench.actors(friend)
                                     if model.startswith("F_NM130")) if q]
                if me and belts:
                    q = min(belts, key=lambda q: sum((q[i] - me[i]) ** 2 for i in range(3)))
                    bench.debug("B", 15, q[0] + 60.0, q[1] if a.air else me[1], q[2], fmt="<Iifff")
            friend.game.pad.run(jumps)
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    out.append(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
                    stop.set()
                    break
            if stop.is_set():
                break
    finally:
        stop.set()
        th.join(1)
    bad, caps, self_bad = [], 0, 0
    for k, ch in seen.items():
        out.append(f"{k}: {len(ch)} changes (fall, land, motion, +0x194 bit 4, source, entries, land has one, "
                   f"motion has one):")
        out += [f"   {t} {s[:4]} {s[4]:#x} {s[5:]}" if s[4] else f"   {t} {s[:4]} -" for t, s in ch[:60]]
        for t, s in ch:
            if s[4]:
                caps += k == "friend's Sora"
                self_bad += s[7] is False
                if s[1] >= 0 and s[1] != 5 and not s[3] and s[6] is False:
                    bad.append((k, f"{k} {t}: land {s[1]} with no entry in its source {s[4]:#x} ({s[5]} entries)"))
    mine = sum(k == "friend's Sora" for k, _ in bad)
    out.append(f"samples with the faulting state: {len(bad)} (the friend's Sora {mine}, the host's copy {len(bad) - mine})")
    out += [f"   {b}" for _, b in bad[:20]]
    friend.lines()
    out += [f"   {l[:200]}" for l in find(friend.all, "set to -1 (TODO 1.250)")[:6]]
    # the guard (grab_sync.cpp ClearMissingCaptureMotions) covers the friend's own Sora only: the copy is listed
    bad = [b for k, b in bad if k == "friend's Sora"]
    if not caps or self_bad:
        out.append("SETUP FAILURE: " + ("no capture on the friend's Sora" if not caps else
                                       f"{self_bad} samples where the capture's own motion has no entry"))
        return False
    return not bad and all(c.alive() and not c.crash for c in copies)

def collision_rows(c, x):
    """L1581: actor x's collision data *(x+0xAB8): (count, default mask, [(group, command, kind)]), entries from +0x40
    of 0x14 bytes (+0 u8 group, +2 u16 command, +4 u8 kind: 5 RC, 14 RC by actor+0xCF0); None if there is none."""
    d = read_u(c, x + 0xAB8, 8)
    if not d:
        return None
    n, mask = read_u(c, d), read_u(c, d + 4)
    rows = []
    for i in range(min(n, 64)):
        e = peek.read(c.game.h, d + 0x40 + 0x14 * i, 6)
        rows.append((e[0], struct.unpack_from("<H", e, 2)[0], e[4]))
    return n, mask, rows


def belt_zones(copies, a, out):
    """TODO 1.248: Oogie's room (save 22 + the warp to 0E/09 programs 0x37), nobody presses: zones_of() on its three
    belts F_NM130. On a DLL without the drop the friend's belts keep their own 576 group on (F725): the control fails."""
    load_both(copies, a.slot, a.room)
    bench.warp(0xE, 9, 0x37)
    bench.wait("both in 0E/09", lambda: all(c.room() == (0xE, 9) and c.game.sora() for c in copies), 90, copies)
    return zones_of(copies, a, out, "F_NM130", 3)


def zones(copies, a, out):
    """TODO 1.248 on other objects: --warp W,R,P (hex; the room and its program) from --slot, then zones_of() on every
    actor whose model starts with --near (as many in both games), both Soras standing (ENEMY_GOD set)."""
    w = [int(x, 16) for x in a.warp.split(",")]
    load_both(copies, a.slot, a.room)
    bench.warp(w[0], w[1], w[2])
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies),
               90, copies)
    return zones_of(copies, a, out, a.near, 0)


def zones_of(copies, a, out, model, count):
    """10 s after the room: each actor of `model` (paired across the games by place) lists its collision rows (L1581,
    F727), then every ~5 ms for --seconds (default 30) its script group bits (+0x5A8 on, +0x5AC off) in both games. A
    friend sample matches when its pair is one the host's actor had within the 0.5 s before; a host sample with a group
    on must show on the friend's within the 0.5 s after. PASS = 98% both ways for each, no crash; the friend's lines
    naming a dropped own zone call are listed. count: how many there must be (0 = any, the same in both)."""
    import bisect
    host, friend = copies
    time.sleep(10)  # the room's start scene
    belts = {}
    for c in copies:
        belts[c.name] = sorted(((actor_pos(c, x)[0], x) for x, m in bench.actors(c) if m.startswith(model)))
        for px, x in belts[c.name]:
            out.append(f"{c.name}: {model} at x {px:.0f}: collision {collision_rows(c, x)}")
    n = len(belts["A"])
    if not n or n != len(belts["B"]) or (count and n != count):
        raise bench.Failed(f"SETUP FAILURE: {model} host {n}, friend {len(belts['B'])} ({count or 'the same'} expected)")
    marks = {c.name: c.mark() for c in copies}
    seen = {c.name: [[] for _ in range(n)] for c in copies}
    t0 = time.time()
    while time.time() < t0 + (30 if a.seconds == 420 else a.seconds):
        for c in copies:
            for i, (_, x) in enumerate(belts[c.name]):
                seen[c.name][i].append((time.time(), struct.unpack("<II", peek.read(c.game.h, x + 0x5A8, 8))))
        if not all(c.alive() and not c.crash for c in copies):
            raise bench.Failed("a copy crashed or closed")
        time.sleep(0.005)
    ok = True
    for i in range(n):
        hs, fs = seen["A"][i], seen["B"][i]
        ht = [t for t, _ in hs]
        good = 0
        for t, v in fs:
            k = bisect.bisect_right(ht, t)
            near = {hv for ht_, hv in hs[max(0, bisect.bisect_left(ht, t - 0.5)):k + 1]}
            good += v in near
        share = good / len(fs) if fs else 0
        # the other way: a host sample with a group on must show on the friend's belt within the 0.5 s after it (a
        # friend belt stuck at 0 would match the host's 0 samples above)
        ft = [t for t, _ in fs]
        lit = [(t, v) for t, v in hs if v[0]]
        follow = sum(v in {fv for _, fv in fs[bisect.bisect_left(ft, t):bisect.bisect_right(ft, t + 0.5)]} for t, v in lit)
        back = follow / len(lit) if lit else 1.0  # no group on in the host's game: the first check covers it
        ok &= share >= 0.98 and back >= 0.98
        pairs = lambda ch: sorted({f"{on:#x}/{off:#x}" for _, (on, off) in ch})
        on_share = lambda ch: sum(1 for _, v in ch if v[0]) / len(ch) if ch else 0
        out.append(f"{model} {i} (x {belts['A'][i][0]:.0f}): friend samples matching the host's {share:.1%} of {len(fs)}; "
                   f"host samples with a group on {on_share(hs):.1%}, friend {on_share(fs):.1%}; those host ones shown "
                   f"on the friend's within 0.5 s {f'{back:.1%}' if lit else '- (none on)'}; host on/off {pairs(hs)}, friend {pairs(fs)}")
    fl = friend.since(marks["B"])
    out += [f"   {l[:220]}" for l in find(fl, "own zone call")[:8]]
    return ok and all(c.alive() and not c.crash for c in copies)

def talk_events(copies, a, out):
    """TODO 1.236: --warp W,R,P1,P2,P3 (hex; none: the save's room), then the --by player is put on each --at spot
    ('X,Y,Z;X,Y,Z', from a log's parity line) and tries 8 facings, Examine (triangle) after each, until its game logs a
    Talk / Examine command; then cross a few times for the messages, and a stick push: can it still move (a held
    event leaves it free)? --both: then the other player does the same spots (it agrees by the same action). Lists both
    games' settings calls (caller, depth, the talk task's actor), holds, commands and cutscenes. PASS = no crash."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    w = [int(x, 16) for x in a.warp.split(",")] if a.warp else [a.room[0], a.room[1]]
    if a.warp:
        bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(10)  # the room's start scene
    marks = {c.name: c.mark() for c in copies}
    first = host if a.by == "A" else friend
    for me in [first] + ([friend if first is host else host] if a.both else []):
        examine_spots(copies, a, out, me, marks, w)
        time.sleep(3)
    time.sleep(6)
    for c in copies:
        c.lines()
        new = c.since(marks[c.name])
        keep = [l[:330] for l in new if "settings call" in l or "carries out command" in l or "cutscene '" in l
                or "scene sync:" in l or "CRASH" in l]
        out.append(f"{c.name} ({'host' if c is host else 'friend'}), room now {c.room()}: {len(keep)} lines")
        out.extend("  " + l for l in keep[:80])
    return all(c.alive() and not c.crash for c in copies)


def examine_spots(copies, a, out, me, marks, w):
    dirs = [(0, 1), (1, 1), (1, 0), (1, -1), (0, -1), (-1, -1), (-1, 0), (-1, 1)]
    for n, spot in enumerate(s for s in a.at.split(";") if s):
        x, y, z = (float(v) for v in spot.split(","))
        before = len(find(me.since(marks[me.name]), "(type 0x1E)") + find(me.since(marks[me.name]), "(type 0x1F)"))
        hit = None
        for lx, ly in dirs:
            bench.debug(me.name, 15, x, y, z, fmt="<Iifff")
            time.sleep(0.3)
            me.game.pad.run(["stick", str(lx * 0.4), str(ly * 0.4), "120", "tap", "triangle", "wait", "900"])
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            got = find(me.since(marks[me.name]), "(type 0x1E)") + find(me.since(marks[me.name]), "(type 0x1F)")
            if len(got) > before:
                hit = (lx, ly)
                break
        if a.keys:  # --keys: vpad steps for what follows, e.g. another option of a choice
            me.game.pad.run(a.keys.split(","))
        for _ in range(6):  # the messages that may follow
            me.game.pad.run(["tap", "cross", "wait", "700"])
        time.sleep(4)
        bench.screenshot(me.hwnd, os.path.join(OUT, f"talk_{w[0]:02X}{w[1]:02X}_{me.name}_{n + 1}.png"))
        p0 = me.game.sora()
        me.game.pad.run(["stick", "0", "-1", "600", "wait", "300"])  # free to move? (a held event must leave it so)
        p1 = me.game.sora()
        moved = p0 and p1 and sum((p1[i] - p0[i]) ** 2 for i in range(3)) ** 0.5
        out.append(f"{me.name} spot {n + 1} ({x:.0f}, {y:.0f}, {z:.0f}): "
                   f"{'examined facing ' + str(hit) if hit else 'no Examine'}; then moved {moved or 0:.0f}")


def apart(copies, a, out):
    """TODO 1.146 / 1.145: the 1000 Heartless (save 97, warp to 04/11 programs 66, as the two-PC run 2026-10-30) with
    the two Soras apart: the host's stays where it lands, the friend's is put FAR away and both are put back every
    10 s; both lock on and attack, every hit kills (KH2COOP_TEST_ONE_HIT=1, your idea 2026-10-30; god mode,
    --seconds, default 90). From the host's log (KH2COOP_PARITY=1): the enemies within 500 of each Sora per sample,
    the enemies last seen alive by their distance to each, the swap count, the mission counter; from the friend's:
    its enemies removed without a death and killed. Run it with --env KH2COOP_AI_NEAREST_PLAYER=0
    for the control. --stand: no attacks (the crowd's layout check, TODO 1.149 step 1). PASS = no crash (the counts are
    the finding)."""
    import math
    import re
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    # --warp W,R,P1,P2,P3 (hex): another room, the Soras kept together (TODO 1.142: a Large Body's Guard, 05/07 0/1/0)
    w = [int(x, 16) for x in a.warp.split(",")] if a.warp else [0x04, 0x11, 66, 66, 66]
    bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) for c in copies), 90, copies)
    time.sleep(8)
    far = None if a.warp else (1200.0, 0.0, -800.0)
    s = host.game.sora()
    home = tuple(s[:3]) if s else None
    out.append(f"host's Sora at {host.game.where()}, the friend's put at {far}")
    # --done: fight until the mission ends (the host leaves the room), then 20 s more.
    seconds = (1500 if a.done else 90) if a.seconds == 420 else a.seconds
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2
    if a.triangle:  # TODO 1.144: reaction commands too (Snag 27, then Sparkle Ray 28 on the lifted robot)
        # mashed, ~14 a second, along with the attacks: the Sparkle Ray offer lasts ~0.4 s. A tap takes 250 ms (vpad),
        # so 70 ms slots (35 on, 35 off): each attack button pressed with triangle, then 3 slots of triangle alone
        slot = lambda b: ["hold", b, "35", "wait", "35"]
        fight = [t for b in ("r1", "cross", "cross", "cross", "circle", "cross", "cross")
                 for t in slot(b + "+triangle") + slot("triangle") * 3]
    t0, put, done_at = time.time(), 0.0, None
    while time.time() - t0 < seconds and (done_at is None or time.time() - done_at < 20):
        if far and time.time() - put >= 10:
            put = time.time()
            bench.debug("B", 15, *far, fmt="<Iifff")
            if home:
                bench.debug("A", 15, *home, fmt="<Iifff")
        if a.stand or done_at is not None:
            time.sleep(0.5)
        else:
            bench.both(lambda c: c.game.pad.run(fight), copies)
        for c in copies:
            new = c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            # The mission's end takes the host out of 04/11 (bench 2026-10-31: 845 s, to 04/01).
            if a.done and done_at is None and c is host and host.room() != (0x04, 0x11):
                done_at = time.time()
                out.append(f"done after {done_at - t0:.0f} s: host left 04/11 for {host.room()}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"apart_{c.name}.png"))
    # The crowd's layout (crowd_sync.cpp, TODO 1.149 step 1): each game's area setups in order; the same = same layout.
    setups = {c.name: [re.sub(r"^\S+ ", "", l) for l in c.since(marks[c.name]) if re.search(r"crowd: area -?\d+ at", l)]
              for c in copies}
    sa, sb = setups["A"], setups["B"]
    diff = next((i for i in range(max(len(sa), len(sb))) if i >= len(sa) or i >= len(sb) or sa[i] != sb[i]), None)
    if not sa and not sb:
        verdict = "not logged"
    elif diff is None:
        verdict = "the same"
    else:
        verdict = (f"differ from setup {diff}: host {sa[diff] if diff < len(sa) else '-'} | friend "
                   f"{sb[diff] if diff < len(sb) else '-'}")
    out.append(f"crowd setups: host {len(sa)}, friend {len(sb)}; layouts {verdict}")
    out += [f"   {l}" for l in sa[-1:]]
    # Step 2 (TODO 1.149): the host's crowd driving the friend's, each game's last count line.
    for c in copies:
        sync = [l for l in c.since(marks[c.name]) if "crowd sync:" in l]
        out.append(f"{c.name}: {sync[-1][13:] if sync else 'no crowd sync line'}")
    d = lambda p, q: math.hypot(p[0] - q[0], p[2] - q[2])
    copy, last, near, swaps, counter = far, {}, [0, 0, 0], "", ""
    for l in host.since(marks["A"]):
        m = re.search(r"peer motion \d+ (?:\(other set\) )?(?:airborne )?at \((-?\d+), (-?\d+), (-?\d+)\)", l)
        if m:
            copy = tuple(map(int, m.groups()))
        if "enemy updates with the Sora copy as the player so far" in l:
            swaps = l.split("so far:", 1)[1].strip()
        if "mission sync: counter" in l:
            counter = l.split("mission sync: counter", 1)[1].strip()
        m = re.search(r"parity: H .* room=0411 inst=(\d+) .* sora=(-?\d+),(-?\d+),(-?\d+)", l)
        if not m:
            continue
        sora = tuple(map(int, m.group(2, 3, 4)))
        if copy is None:  # --warp: no place for the copy until its first motion line
            continue
        ps = re.findall(r"id=(\d+) obj=\S+ ser=\d+ hp=(\d+) pos=(-?\d+),(-?\d+),(-?\d+)", l)
        near[0] += 1
        for eid, hp, x, y, z in ps:
            p = (int(x), int(y), int(z))
            near[1] += d(p, sora) < 500
            near[2] += d(p, copy) < 500
            last[(m.group(1), eid)] = (int(hp), d(p, sora), d(p, copy), l[:12])
    end = max((v[3] for v in last.values()), default="")
    gone = [v for v in last.values() if v[0] > 0 and v[3] < end]  # alive at their last sample, not at the end
    fl = friend.since(marks["B"])
    removed = sum("removed without a death" in l for l in fl)
    killed = sum("killed (the host's enemy died)" in l for l in fl)
    out.append(f"host samples {near[0]}: enemies within 500 of the host's Sora {near[1] / max(near[0], 1):.1f}, "
               f"of the friend's {near[2] / max(near[0], 1):.1f} on average")
    out.append(f"enemies seen {len(last)}, left alive {len(gone)}: nearer the host's Sora "
               f"{sum(v[1] <= v[2] for v in gone)}, nearer the friend's {sum(v[2] < v[1] for v in gone)}")
    out.append(f"host: enemy updates with the copy as the player so far {swaps or '-'}; friend: removed without a "
               f"death {removed}, killed {killed}; the host's last mission counter: {counter or '-'}")
    return all(c.alive() and not c.crash for c in copies)


def capacity(copies, a, out):
    """TODO 1.44 (NO_ENEMY_LIMIT, spawn_sync.cpp): a room's enemies against its Capacity (the factory's limit, F234).
    --warp W,R,P1,P2,P3 (hex; default the Borough's first visit), the host's Sora put at --host-at and the friend's at
    each --friend-at in turn (4 s each; it stays at the last; both put back every 10 s), so each stands in another
    enemy group's box. Both lock on and attack unless --stand. Every second in both games: limit, used, the enemies
    (M_*, B_*) in the actor list (the list read stops at 300 actors), the frame rate (the ping command; DIAG_HUD=1).
    The timeline goes to capacity_timeline.txt. PASS = no crash (the numbers are the finding)."""
    import kh2cmd
    import re
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    w = [int(x, 16) for x in a.warp.split(",")] if a.warp else [0x04, 0x09, 1, 1, 0]
    bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) for c in copies), 90, copies)
    time.sleep(2 if a.warp else 8)  # a mission's swarm starts at once
    with open(os.path.join(OUT, "capacity_live.txt"), "a") as fh:
        fh.write(f"\n{out[0]} warp {a.warp}\n")
    at = lambda text: tuple(float(v) for v in text.split(","))
    spots = {"A": [at(a.host_at)] if a.host_at else [], "B": [at(t) for t in a.friend_at]}
    f32 = lambda c, off: struct.unpack("<f", peek.read(c.game.h, c.game.base + off, 4))[0]
    fight = ["tap", "r1", "wait", "100"] + ["tap", "cross", "wait", "150"] * 3 + [
        "tap", "circle", "wait", "150"] + ["tap", "cross", "wait", "150"] * 2

    def sample(c):
        try:
            names = [n for _, n in bench.actors(c)]
            pong = kh2cmd.query(bench.COPIES[c.name]["debug"], "ping", timeout=0.3) or [""]
            fps = re.search(r"fps (\d+)", pong[0])
            return (round(f32(c, ADMIT_LIMIT)), round(f32(c, ADMIT_USED)), sum(n[:2] in ("M_", "B_") for n in names),
                    len(names), int(fps.group(1)) if fps else -1, read_u(c, c.game.base + SHEETS_FREE))
        except OSError:
            return None

    timeline, t0, nxt, put, tour, shot = [], time.time(), 0.0, -99.0, 0, set()
    peak = {c.name: [0, 0, 0, 999, 80] for c in copies}  # used, enemies, actors, lowest frame rate, fewest free sheets
    seconds = 90 if a.seconds == 420 else a.seconds
    while time.time() - t0 < seconds and all(c.room() == (w[0], w[1]) for c in copies if c.alive()):
        t = time.time() - t0
        if t - put >= (4 if tour < len(spots["B"]) - 1 else 10):
            put = t
            for name, pts in spots.items():
                if pts:
                    bench.debug(name, 15, *pts[min(tour, len(pts) - 1)], fmt="<Iifff")
            tour += 1
        if a.stand:
            time.sleep(0.5)
        else:
            bench.both(lambda c: c.game.pad.run(fight), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        if t >= nxt:
            nxt = t + 1
            row = {c.name: sample(c) for c in copies}
            for c in copies:
                r, pk = row[c.name], peak[c.name]
                if r:
                    pk[0], pk[1], pk[2], pk[4] = max(pk[0], r[1]), max(pk[1], r[2]), max(pk[2], r[3]), min(pk[4], r[5])
                    if r[4] > 0 and t > 5:
                        pk[3] = min(pk[3], r[4])
            timeline.append(f"{t:6.1f} " + " | ".join(
                f"{n} limit {r[0]} used {r[1]}, enemies {r[2]}, actors {r[3]}, fps {r[4]}, sheets free {r[5]}" if r
                else f"{n} -" for n, r in row.items()))
            with open(os.path.join(OUT, "capacity_live.txt"), "a") as fh:  # kept if a game crashes mid-run
                fh.write(timeline[-1] + "\n")
        k = int(t) // 20 * 20
        if k not in shot:
            shot.add(k)
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"capacity_{c.name}_{k:03d}.png"))
    out.append(f"ran {time.time() - t0:.0f} s in {w[0]:02X}/{w[1]:02X} programs {w[2:]}; rooms now A {host.room()} B "
               f"{friend.room()}; host at {host.game.where()}, friend at {friend.game.where()}")
    with open(os.path.join(OUT, "capacity_timeline.txt"), "a") as fh:
        fh.write(f"{out[0]}\n" + "\n".join(timeline) + "\n\n")
    out.extend(timeline[::max(1, len(timeline) // 15)])
    for c in copies:
        pk, new = peak[c.name], c.since(marks[c.name])
        out.append(f"{c.name}: peak used {pk[0]}, peak enemies {pk[1]}, peak actors {pk[2]}, lowest fps {pk[3]}, fewest "
                   f"free sheets {pk[4]}; "
                   f"'no enemy limit' lines {len(find(new, 'no enemy limit: Capacity'))}, FAILED spawns "
                   f"{len(find(new, 'FAILED'))}, friend triggers {len(find(new, 'box; triggering it'))}, exceptions "
                   f"{len(find(new, 'exception'))}")
        for l in find(new, "no enemy limit: Capacity")[:2] + find(new, "FAILED")[:3] + find(new, "exception")[:3]:
            out.append(f"   {l[:230]}")
    return all(c.alive() and not c.crash for c in copies)


def rc_dusk(copies, a, out):
    """Network plan step 2 (your design 2026-11-04): a Dusk's Reversal, an RC with no camera or boss script, offered
    and pressed over the link. See the usage text at the top."""
    import re
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x06, 0x0D, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")  # the Underdrome, programs 0/0/0 ('gym arena')
    bench.wait("both in the Underdrome 06/0D", lambda: all(c.room() == (0x06, 0x0D) and c.game.sora() for c in copies),
               90, copies)
    time.sleep(8)
    count = a.pick or 6
    bench.debug("A", 19, 0x13E | count << 16)  # Dusks, 300 in front of the host's Sora; the friend's game copies them
    time.sleep(5)
    seconds = 90 if a.seconds == 420 else a.seconds
    # mashed, ~14 a second (70 ms slots, as apart --triangle): the Reversal offer is short
    mash = bench.MASH_TRIANGLE * 4
    t0 = time.time()
    while time.time() - t0 < seconds:
        bench.both(lambda c: c.game.pad.run(mash), copies)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    time.sleep(11)  # the 10 s count lines
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    stats = lambda lines: next((l.split("so far", 1)[1].strip() for l in reversed(lines)
                                if "reaction sync: so far" in l), "no count line")
    refused = {}
    for l in hl:
        m = re.search(r"reaction sync: the friend's command \d+ on host#\d+ not run: (.*)$", l)
        if m:
            refused[m.group(1)] = refused.get(m.group(1), 0) + 1
    sent = sum("reaction sync: we used command" in l and "sent to the host" in l for l in fl)
    local = sum("not offered by the host" in l for l in fl)
    ran = sum("reaction sync: the friend used command" in l for l in hl)
    own = sum("reaction sync: our own command" in l for l in hl)
    offers = sum("offers command" in l and "to the friend's copy" in l for l in hl)
    dusks = sum(1 for l in hl if "spawn" in l.lower() and "13e" in l.lower())
    out.append(f"Dusks asked {count} (host spawn lines {dusks}); mashed {seconds} s")
    out.append(f"host: offers to the friend's copy logged {offers}; the friend's presses run {ran}, refused "
               f"{sum(refused.values())} {refused or ''}; the host's own RC uses {own}")
    out.append(f"friend: presses sent to the host {sent}; local ones (not offered by the host) {local}")
    out.append(f"host counts: {stats(hl)}")
    out.append(f"friend counts: {stats(fl)}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"rc_dusk_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and ran > 0


def rc_lords(copies, a, out):
    """A boss's RC pressed by the friend: the Twin Lords' opening prompt. See the usage text at the top."""
    import re
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    warp = LIMIT_FIGHTS[20]
    bench.warp(*warp)
    bench.wait("both in 07/03", lambda: all(c.room() == warp[:2] for c in copies), 60, copies)
    seconds = 30 if a.seconds == 420 else a.seconds
    ran = lambda: find(host.since(marks["A"]), "reaction sync: the friend used command")
    t0 = time.time()
    # From the arrival (the prompt comes once, at the fight's start) until the host runs a press (your request
    # 2026-11-05: stop there, no fight).
    while time.time() - t0 < seconds and not ran():
        friend.game.pad.run(bench.MASH_TRIANGLE)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    took = time.time() - t0
    t1 = time.time()  # the grab's first calls on the friend's Sora come a trip after the run
    while time.time() - t1 < 10 and not find(friend.since(marks["B"]), "grab sync: ran the host"):
        time.sleep(0.2)
    time.sleep(1)
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    count = lambda lines, text: sum(text in l for l in lines)
    ms = lambda l: sum(float(x) * k for x, k in zip(l[:12].split(":"), (3600000, 60000, 1000)))
    refused = {}
    for l in hl:
        m = re.search(r"reaction sync: the friend's command \d+ on host#\d+ not run: (.*)$", l)
        if m:
            refused[m.group(1)] = refused.get(m.group(1), 0) + 1
    runs = find(hl, "reaction sync: the friend used command")
    delay = "-"
    if runs:  # both games log this PC's clock: the press's trip = the host's run - the friend's first press of it
        cmd = re.search(r"used command (\d+)", runs[0]).group(1)
        press = next((l for l in fl if f"we used command {cmd} " in l and "sent to the host" in l), None)
        if press:
            delay = f"{ms(runs[0]) - ms(press):.0f} ms"
    out.append(f"mashed {took:.1f} s from the arrival; the first press ran {delay} after the friend's game sent it")
    out.append(f"host: the friend's presses run {len(runs)}, refused {sum(refused.values())} {refused or ''}")
    out.append(f"friend: presses sent {count(fl, 'whose boss goes on with it')}, repeats held back "
               f"{count(fl, 'reaction sync: our repeat of command')}; the host's grab calls run on our Sora "
               f"{count(fl, 'grab sync: ran the host')}")
    for l in runs[:2] + find(fl, "reaction sync: our repeat of command")[:1] + find(fl, "grab sync: ran the host")[:2]:
        out.append("   " + l[:200])
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"rc_lords_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and len(runs) == 1  # a repeat restarts the RC (F444)


def gain(copies, a, out):
    """The friend's item on the host's companion, applied there. See the usage text at the top."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.warp(0x04, 0x09)  # away from the save point (it refills HP)
    bench.wait("both in 04/09", lambda: all(c.room() == (0x04, 0x09) for c in copies), 60, copies)
    time.sleep(12)
    marks = {c.name: c.mark() for c in copies}
    # Items, the first item, then the second target (a companion), as item's path with a down before the target
    use = ["tap", "down", "wait", "300"] * 2 + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "800",
                                                 "tap", "down", "wait", "300", "tap", "cross", "wait", "300"]
    for n in range(a.pick or 3):
        bench.debug("A", 1, 30)  # the host's companions hurt; the friend's copies take the host's HP
        bench.debug("A", 2, 30)
        time.sleep(2)
        friend.game.pad.run(use)
        time.sleep(4)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    time.sleep(2)
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    sent = find(fl, "sent to the host's game (its companion takes it)")
    applied = find(hl, "companion sync: the friend's HP +") + find(hl, "companion sync: the friend's MP +")
    out.append(f"friend: gains sent {len(sent)}; host: applied {len(applied)}")
    for l in (sent + applied)[:8]:
        out.append("   " + l[:200])
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"gain_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and len(sent) > 0 and len(applied) == len(sent)


def copy_row(copies, a, out):
    """TODO 3.17 / 3.7: the other Sora's own row in the item and Cure target list with a full party, and items'
    sparkles on the Sora they were used on, in both games. See the usage text at the top."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    # Away from the save point (it refills HP); --warp W,R,P1,P2,P3 (hex), e.g. 2,0,0,0,0: the void, no companions
    w = [int(x, 16) for x in a.warp.split(",")] if a.warp else [0x04, 0x09, 0xFFFF, 0xFFFF, 0xFFFF]
    bench.debug("A", 12, w[0], w[1], 0, 0, w[2], w[3], w[4], 0, fmt="<IiBBBBHHHH")
    bench.wait(f"both in {w[0]:02X}/{w[1]:02X}", lambda: all(c.room() == (w[0], w[1]) for c in copies), 60, copies)
    time.sleep(12)
    row_id = {"A": 798, "B": 797}  # "Sora (P2)" in the host's game, "Sora (P1)" in the friend's (cmd_other_sora.yml)
    for c in copies:  # the items to test, in Sora's slots (record save+0x24F0: +0x12 count, +0x34 slots, +0x44 the
        rec = c.game.base + 0x9ABDA0  # items they reload after a use; OpenKH CharacterFinalMix, Ghidra exe+0x3C4D50)
        items = [2, 3, 4, 5, 6, 7]  # Hi-Potion, Ether, Elixir, Mega-Potion, Mega-Ether, Megalixir; 9 of each in the bag
        write_mem(c, rec + 0x12, bytes([len(items)]))  # (save+0x3580, by the item's +0x12 slot: items 1-7 = 0-6)
        for at in (0x34, 0x44):
            write_mem(c, rec + at, struct.pack("<8h", *(items + [0] * (8 - len(items)))))
        write_mem(c, c.game.base + 0x9ACE30, bytes([9] * 7))
    target_page = CMD_MENU + 0x228  # page 4 (L271)
    tap = lambda key, ms=300: ["tap", key, "wait", str(ms)]

    def sheet(c):
        sora = read_u(c, c.game.base + 0x2A171C8, 8)
        return read_u(c, sora + 0x5C0, 8) if sora else 0

    def stat(c, off):
        return struct.unpack("<i", peek.read(c.game.h, sheet(c) + off, 4))[0]

    def set_stat(c, off, value):
        write_mem(c, sheet(c) + off, struct.pack("<i", value))

    def check_alive():
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")

    items_of = {}

    def item_of(c, cmd):
        """The item a command uses (Execute 3: Argument), from the live command list (F550)."""
        if c.name not in items_of:
            lst = read_u(c, c.game.base + 0x2A161E8, 8)
            raw = peek.read(c.game.h, lst + 8, read_u(c, lst + 4) * 0x30)
            items_of[c.name] = {struct.unpack_from("<H", raw, k)[0]: struct.unpack_from("<h", raw, k + 4)[0]
                                for k in range(0, len(raw), 0x30) if struct.unpack_from("<H", raw, k + 2)[0] == 3}
        return items_of[c.name].get(cmd)

    def to_targets(c, top, ok):
        """The top page's entry `top` (1 Magic, 2 Items), then the first entry with ok(command): the target list's
        commands, or None (the menu backed out)."""
        c.game.pad.run(tap("circle") * 2 + ["wait", "1200"])  # back to the top page (a jump there; landed by now)
        if not cursor_to(c, lambda i, cmd, n: i == top):
            return None
        c.game.pad.run(tap("cross", 800))
        if not cursor_to(c, lambda i, cmd, n: ok(c, cmd)):
            out.append(f"   {c.name}: page {top}'s commands {[hex(x) for x in menu_page(c)[1]]}")
            c.game.pad.run(tap("circle") * 2)
            return None
        c.game.pad.run(tap("cross", 800))
        p, rows, _, _ = menu_page(c)
        if p != c.game.base + target_page:
            c.game.pad.run(tap("circle") * 3)
            return None
        return rows

    def pick(c, top, ok, label, wrap=False, before_confirm=None, confirm=None):
        """Opens the target list and puts the cursor on our row (down from the top, or up once: the wrap), hovers 2 s,
        confirms (or runs the pad steps `confirm`). Returns the target list's commands, or None."""
        rows = to_targets(c, top, ok)
        if rows is None:
            out.append(f"{c.name} {label}: no target list (not in the save's lists?)")
            return None
        if row_id[c.name] not in rows:
            out.append(f"{c.name} {label}: target rows {rows}: NO row {row_id[c.name]}")
            c.game.pad.run(tap("circle") * 3)
            return rows
        moves = tap("up") if wrap else tap("down") * rows.index(row_id[c.name])
        c.game.pad.run(moves + ["wait", "2000"])
        if before_confirm:
            before_confirm()
        _, now, i, cmd = menu_page(c)
        out.append(f"{c.name} {label}: target rows {now}, cursor {i} on {cmd}")
        if label == "HP item":  # the list with our row, for your eyes
            bench.screenshot(c.hwnd, os.path.join(OUT, f"copy_row_list_{c.name}.png"))
        c.game.pad.run(confirm or tap("cross"))
        if label == "HP item":  # the item lands ~0.9 s after the pick: its sparkle on both screens
            time.sleep(1.1)
            for x in copies:
                bench.screenshot(x.hwnd, os.path.join(OUT, f"copy_row_sparkle_{c.name}_{x.name}.png"))
        return rows

    results = {}

    def round_(c, o, label, top, ok, prep=None, wait=5, **kw):
        """One pick by c on o's copy; the log lines since, for the checks."""
        if prep:
            prep()
        time.sleep(1)
        marks = {x.name: x.mark() for x in copies}
        rows = pick(c, top, ok, label, **kw)
        time.sleep(wait)
        check_alive()
        return rows, c.since(marks[c.name]), o.since(marks[o.name])

    def report(c, label, checks):
        ok = all(v for _, v in checks)
        results[f"{c.name} {label}"] = ok
        out.append(f"{c.name} {label}: {'PASS' if ok else 'FAIL'} " + ", ".join(f"{k} {'yes' if v else 'NO'}" for k, v in checks))

    hp_item = lambda c, cmd: item_of(c, cmd) in (1, 2)  # Potion, Hi-Potion
    if a.attack_mash:  # the pick, then up and cross mashed through the item's flight (it lands ~0.9 s after the pick)
        cure = lambda c, cmd: cmd in (0x34, 0x7D, 0x7E)
        for c, o in ((friend, host), (host, friend)):
            for label, top, ok in (("HP item, then Attack mashed", 2, hp_item), ("Cure, then Attack mashed", 1, cure)):
                def prep(c=c, o=o):
                    set_stat(c, 0x180, stat(c, 0x184))
                    set_stat(o, 0, max(1, stat(o, 4) // 3))
                # Bench 2026-11-13: the game took no press during the flight (ups ignored); later crosses may use a
                # second item on the user, so only the first landing after the pick counts.
                mash = ["tap", "cross", "wait", "40"] + ["tap", "up", "wait", "40", "tap", "cross", "wait", "40"] * 8
                rows, cl, ol = round_(c, o, label, top, ok, prep=prep, wait=6, confirm=mash)
                if rows is None:
                    continue
                landed = sorted(find(cl, " lands (") + find(cl, "a Cure on the Sora copy heals"))  # by time
                before = [l for l in find(cl, "menu: command") + find(cl, "combo: Attack press")
                          if landed and l < landed[0] and "picked" not in l and "on Sora copy" not in l]
                out.append(f"   {c.name} {label}: presses the game took before it landed: {len(before)} "
                           + "; ".join(l[:100] for l in before[:3]))
                checks = [("picked", bool(find(cl, "-> queued")))]
                if top == 2:
                    checks += [("the first landing on the copy", bool(landed) and "chosen on the copy yes" in landed[0]),
                               ("sent", bool(find(cl, "used on the Sora copy"))),
                               ("receiver healed", bool(find(ol, "the other player's item healed our Sora")))]
                else:
                    checks += [("sent", bool(find(cl, "a Cure on the Sora copy heals the other player"))),
                               ("receiver healed", bool(find(ol, "the other player's Cure healed our Sora")))]
                report(c, label, checks)
        return all(c.alive() and not c.crash for c in copies) and bool(results) and all(results.values())
    for c, o in ((friend, host), (host, friend)):
        hurt = lambda: set_stat(o, 0, max(1, stat(o, 4) // 3))
        for wrap in (False, True):
            label = "HP item (up once: the wrap)" if wrap else "HP item"
            rows, cl, ol = round_(c, o, label, 2, hp_item, prep=hurt, wrap=wrap)
            if rows is None:
                continue
            report(c, label, [("picked", bool(find(cl, "the other Sora's row (row")) and bool(find(cl, "-> queued"))),
                              ("sent", bool(find(cl, "used on the Sora copy"))),
                              ("sparkle moved", bool(find(cl, "its sparkle: moved to the copy"))),
                              ("receiver healed", bool(find(ol, "the other player's item healed our Sora"))),
                              ("receiver sparkle", bool(find(ol, "its sparkle on our Sora: made")))])
        for item_id, label in ((3, "Ether"), (4, "Elixir")):
            def low(o=o):
                set_stat(o, 0x180, max(1, stat(o, 0x184) // 4))
                hurt()
            rows, cl, ol = round_(c, o, label, 2, lambda c, cmd, i=item_id: item_of(c, cmd) == i, prep=low)
            if rows is None:
                continue
            report(c, label, [("sent", bool(find(cl, "used on the Sora copy"))),
                              ("sparkle moved", bool(find(cl, "its sparkle: moved to the copy"))),
                              ("receiver MP", bool(find(ol, "gave our Sora +"))),
                              ("receiver HP", label == "Ether" or bool(find(ol, "the other player's item healed our Sora"))),
                              ("receiver sparkle", bool(find(ol, "its sparkle on our Sora: made")))])
        for item_id, label, mp in ((5, "Mega-Potion", False), (6, "Mega-Ether", True)):  # whole party
            def low(o=o, mp=mp):
                if mp:
                    set_stat(o, 0x180, max(1, stat(o, 0x184) // 4))
                hurt()
            low()
            time.sleep(1)
            marks = {x.name: x.mark() for x in copies}
            c.game.pad.run(tap("circle") * 2 + ["wait", "1200"])
            if not (cursor_to(c, lambda i, cmd, n: i == 2) and (c.game.pad.run(tap("cross", 800)) or True) and
                    cursor_to(c, lambda i, cmd, n, k=item_id: item_of(c, cmd) == k)):
                out.append(f"{c.name} {label}: not in the Items list {[hex(x) for x in menu_page(c)[1]]}")
                c.game.pad.run(tap("circle") * 2)
                continue
            c.game.pad.run(tap("cross", 800) + tap("cross"))  # the item, then its confirm page (11)
            time.sleep(5)
            check_alive()
            cl, ol = c.since(marks[c.name]), o.since(marks[o.name])
            report(c, label, [("sent", bool(find(cl, "also heals the other player"))),
                              ("sparkle on the copy", bool(find(cl, "a sparkle on the copy: made"))),
                              ("receiver " + ("MP" if mp else "healed"),
                               bool(find(ol, "gave our Sora +" if mp else "the other player's item healed our Sora"))),
                              ("receiver sparkle on its Sora", bool(find(ol, "its sparkle on our Sora: made"))),
                              ("receiver sparkle on the copy", bool(find(ol, "its sparkle on their Sora (our copy): made"))),
                              ("no copy in the sparkle loop", not find(cl, "from exe+0x3F6C6F: gave it"))])
        def cure_prep(c=c):
            set_stat(c, 0x180, stat(c, 0x184))  # a full MP bar for the Cure
            hurt()
        rows, cl, ol = round_(c, o, "Cure", 1, lambda c, cmd: cmd in (0x34, 0x7D, 0x7E), prep=cure_prep, wait=7)
        if rows is not None:
            out.append(f"   {c.name} Cure: {len(find(cl, 'the copy in companion 2'))} swaps, "
                       f"{len(find(cl, 'never asked for party member 2'))} windows run out")
            report(c, "Cure", [("picked", bool(find(cl, "-> queued"))),
                               ("sent", bool(find(cl, "a Cure on the Sora copy heals the other player"))),
                               ("receiver healed", bool(find(ol, "the other player's Cure healed our Sora")))])
        # On itself: the first target row, our own Sora; its sparkle on our copy in the other game.
        rows = to_targets(c, 2, hp_item)
        if rows is not None:
            marks = {x.name: x.mark() for x in copies}
            c.game.pad.run(["wait", "500"] + tap("cross"))
            time.sleep(5)
            check_alive()
            ol = o.since(marks[o.name])
            report(c, "HP item on itself", [("its sparkle on the copy there", bool(find(ol, "its sparkle on their Sora (our copy): made")))])

    # The rows shift under the cursor: the host's Goofy KO'd while the host hovers our row.
    goofy = bench.actor_by_model(host, "P_EX030")
    slots = [read_u(host, host.game.base + 0x2A239B0 + 8 * i, 8) for i in range(2)]
    if goofy in slots:
        ko = lambda: (bench.debug("A", 1 + slots.index(goofy), 9999), time.sleep(1.5))
        rows, cl, ol = round_(host, friend, "HP item, Goofy KO'd while hovering", 2, hp_item, before_confirm=ko)
        if rows is not None:
            out.append("   " + "; ".join(l[:120] for l in find(cl, "target row:")[:4]))
        time.sleep(6)  # Goofy back up
    # A room change with the list open, the cursor on our row.
    rows = to_targets(host, 2, hp_item)
    if rows is not None and row_id["A"] in rows:
        host.game.pad.run(tap("down") * rows.index(row_id["A"]) + ["wait", "500"])
        bench.warp(0x04, 0x06)
        bench.wait("both in 04/06", lambda: all(c.room() == (0x04, 0x06) for c in copies), 60, copies)
        time.sleep(8)
        check_alive()
        out.append("room change with the list open: no crash")
    for c in copies:
        out.append(f"{c.name}: " + "; ".join(l[:140] for l in find(c.all, "target row:")[:3]))
        out.append(f"{c.name}: the party sparkle loop's GetFriend(1): " +
                   ("; ".join(l[13:110] for l in find(c.all, "from exe+0x3F6C6F")) or "never asked for the copy"))
        bench.screenshot(c.hwnd, os.path.join(OUT, f"copy_row_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and bool(results) and all(results.values())


def skip(copies, a, out):
    """Network plan step 4: the host's Skip Scene over the link. See the usage text at the top."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}  # the scene can start on arrival
    bench.debug("A", 12, 0x02, 0x19, 0, 0, 2, 0, 1, 0, fmt="<IiBBBBHHHH")  # programs 2/0/1, as neg_group --scene
    bench.wait("both in 02/19", lambda: all(c.room() == (0x02, 0x19) for c in copies), 90, copies)
    first = lambda lines, text: next(iter(find(lines, text)), None)
    started = lambda c: first(c.since(marks[c.name]), "scene log: cutscene '")
    for walk in (False, True):
        if walk:  # not on arrival: walk the host's Sora into the area
            box = walk_in_box(host)
            if not box:
                raise bench.Failed("no walk-in scene on arrival and no type-3 walk-in area in the room")
            bench.debug("A", 15, *box["centre"], fmt="<Iifff")
        end = time.time() + 20
        while time.time() < end and not all(started(c) for c in copies):
            time.sleep(0.3)
        if all(started(c) for c in copies):
            break
    for c in copies:
        s = started(c)
        out.append(f"{c.name}: {s[:90] if s else 'NO walk-in scene started'}")
    if not all(started(c) for c in copies):
        return False
    time.sleep(4)  # past the scene's first frames (Skip Scene is offered after frame 9)
    # TODO 1.227: a cutscene pause by either player: the Client's has no Skip Scene row, both say who paused.
    menu = lambda c: peek.read(c.game.h, c.game.base + 0x7435D0, 1)[0]
    look = True
    for pauser, who in ((friend, "the Client"), (host, "the Host")):
        if pauser is host:
            asked = {c.name: c.mark() for c in copies}
        m = {c.name: c.mark() for c in copies}
        pauser.game.pad.run(["tap", "start", "wait", "1500"])
        rows = {c.name: menu_rows(c)[0] for c in copies}
        said = {c.name: first(c.since(m[c.name]), "link menu: a pause with Skip Scene") for c in copies}
        for c in copies:
            bench.screenshot(c.hwnd, os.path.join(OUT, f"scene_pause_by_{pauser.name}_{c.name}.png"))
        ok = len(rows["A"]) == 2 and len(rows["B"]) == 1 and all(v and f"{who} paused" in v for v in said.values())
        if pauser is friend:  # its only row is Continue: the scene goes on in both games, nothing skipped
            pauser.game.pad.run(["tap", "cross", "wait", "2000"])
            ok = ok and all(menu(c) == 0xFF for c in copies) and not any(find(c.since(m[c.name]), "skip sync:") for c in copies)
        look = look and ok
        out.append(f"{who} pauses: host rows {rows['A']}, friend rows {rows['B']}: {'ok' if ok else 'WRONG'}; " +
                   "; ".join(f"{n}: {(v or 'no line')[13:110]}" for n, v in said.items()))
    host.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "400"])
    ours = lambda: first(host.since(asked["A"]), "skip sync: we skipped")
    theirs = lambda: first(friend.since(asked["B"]), "like the host")
    end = time.time() + 20
    while time.time() < end and not (ours() and theirs()):
        time.sleep(0.3)
    got = first(friend.since(asked["B"]), "skip sync: the host skipped")
    out.append(f"host: {(ours() or 'NO skip logged')[:150]}")
    out.append(f"friend: {(got or 'NO skip message')[:150]}; {(theirs() or 'NOT skipped')[:150]}")
    time.sleep(10)
    for c in copies:  # the next scene of the chain, in both games
        n = first(c.since(asked[c.name]), "scene log: cutscene '")
        out.append(f"{c.name} next: {(n or 'no next scene')[:90]}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"skip_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and bool(ours() and theirs()) and look


def pause_race(copies, a, out):
    """TODO 1.18: fast pauses in a cutscene (the Tower's walk-in scene, as skip). See the usage text."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x02, 0x19, 0, 0, 2, 0, 1, 0, fmt="<IiBBBBHHHH")
    bench.wait("both in 02/19", lambda: all(c.room() == (0x02, 0x19) for c in copies), 90, copies)
    started = lambda c: bool(find(c.since(marks[c.name]), "scene log: cutscene '"))
    try:
        bench.wait("the walk-in scene in both games", lambda: all(started(c) for c in copies), 25, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    time.sleep(4)
    menu = lambda c: peek.read(c.game.h, c.game.base + 0x7435D0, 1)[0]
    both = lambda v: all(menu(c) == v for c in copies)
    ok = True

    def settle(name, t0):
        """Both must end paused; reports how long the slower game took and what each log said."""
        nonlocal ok
        try:
            bench.wait(f"{name}: both paused", lambda: both(0x08), 6, copies, step=0.05)
            took, good = time.time() - t0, True
        except bench.Failed:
            took, good = 6.0, False
        time.sleep(2)  # a pause that closes again by itself counts as wrong
        good = good and both(0x08)
        ok = ok and good
        out.append(f"{name}: {'ok' if good else 'WRONG'}: menus host 0x{menu(host):02X} friend 0x{menu(friend):02X}, "
                   f"both paused {took * 1000:.0f} ms after the last press")
        for c in copies:  # back to the running scene for the next case
            if menu(c) == 0x08:
                c.game.pad.run(["tap", "start", "wait", "300"])
                break
        bench.wait(f"{name}: both running again", lambda: both(0xFF), 8, copies, step=0.05)
        time.sleep(2)

    # 1. The friend pauses and resumes; the host pauses just after its own pause closed to match.
    friend.game.pad.run(["tap", "start", "wait", "1500"])
    friend.game.pad.run(["tap", "start"])
    bench.wait("the host's pause closed to match", lambda: menu(host) == 0xFF, 5, copies, step=0.01)
    time.sleep(0.07)
    host.game.pad.run(["tap", "start"])
    settle("the host pauses right after a matched close", time.time())
    # 2. The host resumes and pauses again at once: the friend gets both before its own close has taken.
    host.game.pad.run(["tap", "start", "wait", "1500"])
    host.game.pad.run(["tap", "start", "wait", "60", "tap", "start"])
    settle("the host resumes and pauses again at once", time.time())
    # 3. A plain pause, for the time a request takes with nothing in its way.
    host.game.pad.run(["tap", "start"])
    settle("a plain pause by the host", time.time())
    return ok and all(c.alive() and not c.crash for c in copies)


def vexen_freeze(copies, a, out):
    """TODO 1.229, 1.230: Vexen (04/20 programs 115) freezes the friend, which mashes Struggle. See the usage text."""
    from PIL import Image
    host, friend = copies
    load_both(copies, a.slot, a.room)
    bench.debug("A", 12, 0x04, 0x20, 0, 0, 115, 115, 115, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 04/20", lambda: all(c.room() == (0x04, 0x20) and c.game.sora() for c in copies), 90, copies)
        bench.wait("N_CM040 in both games", lambda: all(bench.actor_by_model(c, "N_CM040") for c in copies), 40, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    marks = {c.name: c.mark() for c in copies}
    # --want host: the host is the one frozen (how its freeze looks in the friend's game); default the friend.
    victim, other = (host, friend) if a.want == "host" else (friend, host)
    if victim is host:
        grabbed = lambda: find(friend.since(marks["B"]), "played the host's captured motion 252")
        freed = lambda: False
    else:
        grabbed = lambda: find(host.since(marks["A"]), "grabs the friend's Sora copy")
        freed = lambda: find(host.since(marks["A"]), "the copy is free again")
    end = time.time() + 240
    while time.time() < end and not grabbed():
        v = bench.actor_by_model(host, "N_CM040")
        if v:  # the victim next to him, the other on the far side, so he goes for the victim (the nearer player)
            x, y, z = struct.unpack("<3f", peek.read(host.game.h, v + 0x670, 12))
            bench.debug(victim.name, 15, x + 120.0, y, z, fmt="<Iifff")
            bench.debug(other.name, 15, x - 1200.0 if x > 250 else x + 1200.0, y, z, fmt="<Iifff")
        victim.game.pad.run(["wait", "1000"] if a.stand or a.burst else ["tap", "triangle", "wait", "150"] * 5)
    if not grabbed():
        out.append(f"SETUP FAILURE: he never froze copy {victim.name}'s Sora in 240 s")
        return False
    t0 = time.time()
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"vexen_freeze_{c.name}_frozen.png"))
    # How long the victim's own Sora stays in the frozen motion 252, sampled in its own game every 5 ms.
    import threading
    from vpad import buttons
    span, stop = [None, None], threading.Event()

    def sample():
        g = victim.game
        while not stop.is_set():
            actor = g.q(g.base + 0x2A171C8)
            m = struct.unpack("<i", peek.read(g.h, actor + 0x180, 4))[0] if actor else -1
            if m == 252 and span[0] is None:
                span[0] = time.perf_counter()
            if m == 252:
                span[1] = time.perf_counter()
            elif span[0] is not None and time.perf_counter() - span[1] > 0.3:
                return
            time.sleep(0.005)
    th = threading.Thread(target=sample, daemon=True)
    th.start()
    presses = 0
    while time.time() - t0 < 9 and th.is_alive():
        if a.stand:  # nobody presses: how long the freeze lasts by itself
            victim.game.pad.send(ms=100)
        elif a.burst:  # as fast as the game reads a press: 2 frames down, 2 up, about 15 a second
            victim.game.pad.send(buttons("triangle"), ms=34)
            victim.game.pad.send(ms=34)
            presses += 1
        else:
            victim.game.pad.run(["tap", "triangle", "wait", "90"])
            presses += 1
    stop.set()
    th.join(1)
    held = time.time() - t0
    out.append(f"copy {victim.name}'s Sora in the frozen motion 252 for "
               f"{(span[1] - span[0]) if span[0] is not None else -1:.2f} s (it was already in it when the sampling "
               f"began); {presses} presses in that time and until the wait's end, "
               f"{'nobody pressed' if a.stand else 'as fast as read' if a.burst else 'about 3 a second'}")
    time.sleep(3)
    white = {}
    for c in copies:
        path = os.path.join(OUT, f"vexen_freeze_{c.name}_after.png")
        bench.screenshot(c.hwnd, path)
        px = list(Image.open(path).convert("L").resize((32, 18)).getdata())
        white[c.name] = sum(v > 240 for v in px) / len(px)
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    ran, refused = find(hl, "the friend used command 718"), find(hl, "the friend's command 718")
    out.append(f"the friend frozen {held:.1f} s until the let go ({'seen' if freed() else 'NOT seen in 8 s'}); Struggle: "
               f"{len(find(fl, 'we used command 718'))} sent, {len(ran)} run by the host, {len(refused)} not run"
               f"{': ' + refused[0][13:150] if refused else ''}")
    out.append("whitein: " + ("; ".join(l[13:150] for l in find(hl, "whitein")) or "no line"))
    out.append(f"share of white on screen 3 s after: host {white['A']:.2f}, friend {white['B']:.2f}")
    out.append("the friend's Sora while held: " + "; ".join(l[13:90] for l in find(fl, "blend log: our Sora motion")[-6:]))
    out.append("the friend's mirror: " + "; ".join(l[13:150] for l in find(fl, "world mirror: call")[-2:] +
                                                  find(fl, "world mirror: host calls run")[-1:]))
    return (all(c.alive() and not c.crash for c in copies) and (victim is host or a.stand or len(ran) >= 3) and
            white["A"] < 0.9 and white["B"] < 0.9)


def chest_menu(copies, a, out):
    """TODO 1.237: a chest the friend opens while the host's save menu is open is given only once it has closed."""
    host, friend = copies
    load_both(copies, a.slot, a.room)  # save 52 loads on the Borough's save point
    menu = lambda c: peek.read(c.game.h, c.game.base + 0x7435D0, 1)[0]
    bit = 359  # a chest neither bench save has opened (Agrabah's)
    host.game.pad.run(["tap", "start", "wait", "2500"])  # the full menu: any open menu holds a gift back
    if menu(host) == 0xFF:
        out.append("SETUP FAILURE: the host's menu didn't open")
        return False
    out.append(f"the host's menu is open (0x{menu(host):02X})")
    mark = host.mark()
    bench.debug("B", 21, bit)
    given = lambda: find(host.since(mark), f"the friend opened chest {bit}")
    time.sleep(4)
    early = bool(given()) and menu(host) != 0xFF
    out.append(f"4 s with the menu open (0x{menu(host):02X}): the chest "
               f"{'was GIVEN under the menu' if early else 'waits'}")
    for _ in range(4):  # back out of the menu
        if menu(host) == 0xFF:
            break
        host.game.pad.run(["tap", "circle", "wait", "900"])
    closed = menu(host) == 0xFF
    try:
        bench.wait("the chest given after the menu closed", lambda: bool(given()), 8, copies, step=0.1)
        late = True
    except bench.Failed:
        late = False
    out.append(f"menu closed: {closed}; after it: {(given() or ['NOT given'])[0][13:150]}")
    return not early and closed and late and all(c.alive() and not c.crash for c in copies)


def chests(c):
    """{chest bit: (actor, (x, y, z), opened)} for the chest objects (F_EX030 / F_EX040, F684) in the copy's room:
    +0xC00 its treasure row (+0xA the chest bit), +0x70 where it's drawn, +0x120 bit 0x20 opened (VERIFIED_OFFSETS)."""
    h, out = c.game.h, {}
    for x, name in bench.actors(c):
        if name.startswith(("F_EX030", "F_EX040")):
            row = struct.unpack("<Q", peek.read(h, x + 0xC00, 8))[0]
            bit = struct.unpack("<H", peek.read(h, row + 0xA, 2))[0] if row else -1
            out[bit] = (x, struct.unpack("<3f", peek.read(h, x + 0x70, 12)),
                        bool(struct.unpack("<I", peek.read(h, x + 0x120, 4))[0] & 0x20))
    return out


def sora_copy_at(c):
    """Where the other player's Sora copy is drawn in this game, or None: the actor the log's last 'Sora copy ... made'
    names (any model: in early Twilight Town object 0x6C1, `P_EX120` in OpenKH's list, Sora in his KH1 clothes),
    while it's still in the list."""
    c.lines()
    made = [m for m in (re.search(r"Sora copy .* made \(actor exe\+0x([0-9A-Fa-f]+)", l) for l in c.all) if m]
    x = c.game.base + int(made[-1].group(1), 16) if made else 0
    if not x or x not in (y for y, _ in bench.actors(c)):
        return None
    return struct.unpack("<3f", peek.read(c.game.h, x + 0x70, 12))


def chest_open(copies, out, opener, bit, at):
    """The opener stands at `at` and opens chest `bit` by the test command; the other game's pose lines for it."""
    watcher = copies[1] if opener is copies[0] else copies[0]
    who = "host" if opener is copies[0] else "friend"
    mine, theirs = chests(opener).get(bit), chests(watcher).get(bit)
    if not mine or not theirs or mine[2] or theirs[2]:
        out.append(f"SETUP FAILURE: chest {bit} missing or open (the {who}'s {mine}, the other game's {theirs})")
        return None
    bench.debug(opener.name, 15, at[0], at[1] - 30, at[2], fmt="<Iifff")
    near = at == mine[1]

    def gap():  # None: no copy found (a far wait must not pass on that)
        p = sora_copy_at(watcher)
        return round(math.dist(p, theirs[1])) if p else None

    try:  # the fix measures from the copy, so it must stand there first (300: under the DLL's 500, with room)
        bench.wait(f"the {who}'s copy {'at' if near else 'far from'} chest {bit} in the other game",
                   lambda: (g := gap()) is not None and (g < 300 if near else g > 800), 15, copies, step=0.2)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (gap {gap()})")
        return None
    time.sleep(1)
    before = gap()
    mo, mw = opener.mark(), watcher.mark()
    bench.debug(opener.name, 22, bit)
    pose = lambda: find(watcher.since(mw), "captured motion 252")
    try:
        bench.wait(f"the {who}'s pose for chest {bit} in the other game", lambda: bool(pose()), 8, copies, step=0.2)
    except bench.Failed:
        pass
    time.sleep(2)  # a second line for one opening would show here
    sent = find(opener.since(mo), f"test open {bit}: 'Open' on our chest object")
    played = [l for l in pose() if "played the" in l]
    skipped = [l for l in pose() if "not played" in l]
    out.append(f"the {who} opens chest {bit} {'at it' if near else 'far from it'} (its copy {before} from that "
               f"chest in the other game): sent {len(sent)}, played {len(played)}, not played {len(skipped)}")
    out.extend("  " + l[13:260] for l in sent + pose())
    return sent, played, skipped


def rc_now(c):
    """The RC its game offers now (u16 `exe+0x2A11162`, VERIFIED_OFFSETS L106: 0x20 = 'Open')."""
    return struct.unpack("<H", peek.read(c.game.h, c.game.base + 0x2A11162, 2))[0]


def item_window(c):
    """The chest item window `*(exe+0xABC3C0)`: (+0x20 1 = idle / 0 = a box shows, +0x30 queue write index, +0x34
    read index), or None while it has no record."""
    w = struct.unpack("<Q", peek.read(c.game.h, c.game.base + 0xABC3C0, 8))[0]
    return struct.unpack("<I12xII", peek.read(c.game.h, w + 0x20, 0x18)) if w else None


def stand_at_open(c, pos, inside):
    """Puts c's Sora by the chest at `pos` until its game offers 'Open', first on the side toward `inside` (a spot in
    the room: 'Open' is offered through a wall too, and a press from behind it is lost); [] when offered, else the
    RCs it read."""
    seen, toward = [], math.atan2(inside[2] - pos[2], inside[0] - pos[0])
    for k in (0, 1, -1, 2, -2, 3, -3, 4):
        r = toward + math.radians(45 * k)
        bench.debug(c.name, 15, pos[0] + 90 * math.cos(r), pos[1] - 30, pos[2] + 90 * math.sin(r), fmt="<Iifff")
        time.sleep(0.6)
        seen.append(rc_now(c))
        if seen[-1] == 0x20:
            return []
    return seen


WINDOW_PROBES = []  # (tag, copy, the icon probe) at each dump


def icon_probe(c):
    """The item window's icon slot and its head box (Ghidra leads, 2026-11-18): the slot's state u32 `exe+0x2A11880`
    (0 ready, 1 loading, 2 taken), loaded icon s32 `exe+0x74ACF0`, take count `exe+0x2A11884`; the head box
    record+0x40+0x560*(+0x34): +0x10 flags (0x40 hidden), +0x220 delay, +0x224 icon, +0x548 its image (the slot
    `exe+0x2A118C0` once taken); the next box's queued icon +0x558."""
    h, b = c.game.h, c.game.base
    r = struct.unpack("<Q", peek.read(h, b + 0xABC3C0, 8))[0]
    if not r:
        return "no window record"
    i = struct.unpack("<I", peek.read(h, r + 0x34, 4))[0] & 7
    head, nxt = r + 0x40 + 0x560 * i, r + 0x40 + 0x560 * ((i + 1) & 7)
    st, = struct.unpack("<I", peek.read(h, b + 0x2A11880, 4))
    loaded, = struct.unpack("<i", peek.read(h, b + 0x74ACF0, 4))
    took, = struct.unpack("<I", peek.read(h, b + 0x2A11884, 4))
    flags, = struct.unpack("<I", peek.read(h, head + 0x10, 4))
    delay, icon = struct.unpack("<fi", peek.read(h, head + 0x220, 8))
    image, = struct.unpack("<Q", peek.read(h, head + 0x548, 8))
    queued_next, = struct.unpack("<i", peek.read(h, nxt + 0x558, 4))
    img = "the slot" if image == b + 0x2A118C0 else hex(image)
    return (f"slot state {st}, loaded icon {loaded}, taken {took}; head box {i}: icon {icon}, delay {delay:.1f}, "
            f"hidden {bool(flags & 0x40)}, image {img}; next box's icon {queued_next}")


def dump_window(c, tag):
    """0x200 raw bytes of the chest item window's record, to chest_window_<tag>_<copy>.bin (TODO 1.222), and the
    icon probe."""
    w = struct.unpack("<Q", peek.read(c.game.h, c.game.base + 0xABC3C0, 8))[0]
    if w:
        with open(os.path.join(OUT, f"chest_window_{tag}_{c.name}.bin"), "wb") as fh:
            fh.write(peek.read(c.game.h, w, 0x200))
    WINDOW_PROBES.append((tag, c.name, icon_probe(c)))


def watch_windows(copies, pre, seconds, shots=(), tag="", dump_at=None, cue=None):
    """Both games' item windows every 50 ms from `pre` (read before the press): {copy: (boxes queued, boxes shown,
    idle at the end, [(seconds, state)] each change)}; screenshots at the `shots` seconds, a raw dump at `dump_at`,
    `cue` = (when, fn): fn() run once when `when` (seconds, or a test of the seconds called every 5 ms) is met."""
    seen = {c.name: [(0.0, pre[c.name])] for c in copies}
    t0, shots = time.time(), list(shots)
    while time.time() - t0 < seconds:
        if cue and not callable(cue[0]) and time.time() - t0 >= cue[0]:
            cue[1]()
            cue = None
        if dump_at is not None and time.time() - t0 >= dump_at:
            for c in copies:
                dump_window(c, tag.strip("_") or "pair")
            dump_at = None
        for c in copies:
            now = item_window(c)
            if now != seen[c.name][-1][1]:
                seen[c.name].append((round(time.time() - t0, 2), now))
        if shots and time.time() - t0 >= shots[0]:
            for c in copies:
                bench.screenshot(c.hwnd, os.path.join(OUT, f"chest_together{tag}_{c.name}_{shots[0]:.1f}s.png"))
            shots.pop(0)
        if cue and callable(cue[0]):  # the test every 5 ms in place of the 50 ms wait
            for _ in range(10):
                if cue[0](time.time() - t0):
                    cue[1]()
                    cue = None
                    break
                time.sleep(0.005)
        else:
            time.sleep(0.05)
    for c in copies:
        WINDOW_PROBES.append(((tag.strip("_") or "pair") + " end", c.name, icon_probe(c)))
    got = {}
    for c in copies:
        st = [x for _, x in seen[c.name] if x]
        end = seen[c.name][-1][1]
        got[c.name] = (sum((b[1] - a[1]) % 8 for a, b in zip(st, st[1:])),  # an 8-box ring
                       sum((b[2] - a[2]) % 8 for a, b in zip(st, st[1:])),
                       bool(end) and end[0] == 1 and end[1] == end[2], seen[c.name])
    return got


def chest_press(copies, out, ours, inside, who, bits, tag, shots, gap=0.0, cue=None):
    """Each of `who` stands at its chest of `bits` until 'Open' is offered, then all press Triangle at once; both
    games' windows watched. None on a setup failure."""
    for c, bit in zip(who, bits):
        ch = ours[c.name].get(bit)
        if not ch or ch[2]:
            out.append(f"SETUP FAILURE: chest {bit} missing or open in {c.name}'s game")
            return None
        miss = stand_at_open(c, ch[1], inside)
        if miss:
            out.append(f"SETUP FAILURE: no 'Open' offered to {c.name} at chest {bit} (RC ids read: {miss})")
            return None
    time.sleep(1.5)  # landed: a press while Sora lands after the teleport is lost
    if any(rc_now(c) != 0x20 for c in who):
        out.append(f"SETUP FAILURE: 'Open' not offered to all at once ({[rc_now(c) for c in who]})")
        return None
    marks = {c.name: c.mark() for c in copies}
    pre = {c.name: item_window(c) for c in copies}
    if gap and len(who) == 2:  # --gap: the second presses that many seconds after the first
        who[0].game.pad.run(["tap", "triangle"])
        time.sleep(gap)
        who[1].game.pad.run(["tap", "triangle"])
    else:
        bench.both(lambda c: c.game.pad.run(["tap", "triangle"]), who)
    got = watch_windows(copies, pre, 12, shots, tag, dump_at=1.5 if len(who) == 1 else 6.0, cue=cue)
    when = {}
    for c in copies:
        new = c.since(marks[c.name])
        own = find(new, "rc trace: our Sora motion 252")
        when[c.name] = own[0][:12] if own else None
        if c in who and not find(new, "carries out command 0x20"):
            out.append(f"SETUP FAILURE: {c.name}'s press wasn't taken (no command 0x20)")
            return None
        wrote, read, idle, states = got[c.name]
        out.append(f"  {c.name} ({'host' if c is copies[0] else 'friend'}): own open {when[c.name] or '-'}; boxes "
                   f"queued {wrote}, shown {read}, idle at the end {idle}; window states {states[:10]}")
        out.extend("    " + l[:240] for l in new if ("story sync:" in l and "chest" in l) or "command 0x20 (" in l
                   or "asked for it again" in l or "given up" in l)
    if len(who) == 2 and all(when.values()):
        ms = lambda t: sum(float(x) * f for x, f in zip(t.split(":"), (3600000, 60000, 1000)))
        out.append(f"  the two opens {abs(ms(when['A']) - ms(when['B'])):.0f} ms apart (both logs, this PC's clock)")
    return got


def chest_together(copies, a, out, ours):
    """--together (your idea, 2026-11-18; F669's case). Control: the host opens 306 alone. Then the host at 304 and
    the friend at 305 press Triangle at once: each game must show two item boxes (its own and the other's) and go
    idle. After: the friend's game takes chest 359 by the test command: the host's game must show its box (F669: the
    window stayed broken after)."""
    host, friend = copies
    inside = host.game.sora()  # where the host loaded in: the room's side of every chest
    out.append("control, the host alone at chest 306:")
    alone = chest_press(copies, out, ours, inside, [host], [306], "_alone", (0.5, 2.0))
    if not alone:
        return False
    if any(w != 1 or r != 1 for w, r, _, _ in alone.values()):
        out.append("SETUP FAILURE: one opening alone didn't show one box in each game: the windows can't be judged here")
        return False
    if a.kind:  # one kind of box per game: two of the game's own in the host's, two of ours in the friend's
        out.append("kind test: the host's game opens 304 and 305 back to back by the test command:")
        marks = {c.name: c.mark() for c in copies}
        pre = {c.name: item_window(c) for c in copies}
        bench.debug("A", 22, 304)
        time.sleep(0.02)
        bench.debug("A", 22, 305)
        got = watch_windows(copies, pre, 12, (0.5, 2.0, 3.5), "_kind", dump_at=6.0)
        for c in copies:
            w, r, idle, states = got[c.name]
            out.append(f"  {c.name}: boxes queued {w}, shown {r}, idle at the end {idle}; window states {states[:10]}")
            out.extend("    " + l[:240] for l in c.since(marks[c.name])
                       if ("story sync:" in l and "chest" in l) or "asked for it again" in l)
        out.append("icon probes (tag, copy, probe):")
        out.extend(f"  {t} {n}: {p}" for t, n, p in WINDOW_PROBES)
        return all(w == 2 and r == 2 and idle for w, r, idle, _ in got.values())
    if a.leave:  # the absent case: the host warps out 0.3 s after both open, while a box could be waiting
        out.append("leave: both open at once, then the host warps to 02/02 0.3 s later:")
        for c, bit in ((host, 304), (friend, 305)):
            if stand_at_open(c, ours[c.name][bit][1], inside):
                out.append(f"SETUP FAILURE: no 'Open' for {c.name} at chest {bit}")
                return False
        time.sleep(1.5)
        marks = {c.name: c.mark() for c in copies}
        bench.both(lambda c: c.game.pad.run(["tap", "triangle"]), copies)
        time.sleep(0.3)
        bench.warp(0x02, 0x02)
        try:
            bench.wait("both in 02/02", lambda: all(c.room() == (0x02, 0x02) and c.game.sora() for c in copies), 60,
                       copies)
        except bench.Failed as e:
            out.append(f"  {e}")
        time.sleep(5)
        for c in copies:
            new = c.since(marks[c.name])
            out.append(f"  {c.name}: room {c.room()}, asks {len(find(new, 'asked for it again'))}, window {item_window(c)}, "
                       f"{icon_probe(c)}")
            out.extend("    " + l[:240] for l in new if "asked for it again" in l or "item window" in l or "CRASH" in l)
        return all(c.alive() and not c.crash and c.room() == (0x02, 0x02) for c in copies)
    out.append(f"together, the host at chest 304 and the friend at 305 (the friend {a.gap} s later):")
    cue = None
    fired = []
    if a.third is not None and a.on_ask:  # the host's game opens 359 the moment a load starts after `third` s: the
        # re-ask's, so the third box's load lands between the ask and the box taking its icon (TODO 1.222)
        out.append(f"  and the host's game opens chest 359 by the test command when its icon slot starts a load "
                   f"{a.third} s or more after the presses")
        seen_states = {}  # the slot's states the test read after `third`, and how often

        def ask_load(t):
            if t < a.third:
                return False
            st = struct.unpack("<I", peek.read(host.game.h, host.game.base + 0x2A11880, 4))[0]
            seen_states[st] = seen_states.get(st, 0) + 1
            return st == 1
        cue = (ask_load, lambda: (bench.debug("A", 22, 359), fired.append(time.time())))
    elif a.third is not None:  # a third box's load on top of a box waiting for its icon (TODO 1.222's re-ask)
        out.append(f"  and the friend's game takes chest 359 by the test command {a.third} s after the presses")
        cue = (a.third, lambda: bench.debug("B", 21, 359))
    pair = chest_press(copies, out, ours, inside, [host, friend], [304, 305], "", (0.5, 2.0, 3.5, 4.2, 5.0, 8.0), a.gap,
                       cue)
    if not pair:
        return False
    if a.third is not None:
        gave_up = [l for c in copies for l in c.all if "still waits for icon" in l]
        out.extend("  " + l[:240] for l in gave_up)
        if a.on_ask:
            out.append(f"  the slot's states read from {a.third} s (state: reads): {seen_states}")
            out.append(f"  359 opened at {time.strftime('%H:%M:%S', time.localtime(fired[0]))}."
                       f"{int(fired[0] * 1000) % 1000:03d}" if fired else "  SETUP FAILURE: no load seen to open 359 on")
            if not fired:
                return False
            if any("test open 359: no chest object" in l for l in host.all):
                out.append("  SETUP FAILURE: the host's game has no chest object of 359 (02/09 has 304 to 306 only)")
                return False
        out.append("icon probes (tag, copy, probe):")
        out.extend(f"  {t} {n}: {p}" for t, n, p in WINDOW_PROBES)
        want = {"A": 3, "B": 3 if a.on_ask else 2}
        return not gave_up and all(pair[n][0] == w and pair[n][1] == w and pair[n][2] for n, w in want.items())
    ok = all(w == 2 and r == 2 and idle for w, r, idle, _ in pair.values())
    ok &= all(all(v[2] for b, v in chests(c).items() if b in (304, 305)) for c in copies)
    out.append("after, the friend's game takes chest 359 by the test command:")
    marks = {c.name: c.mark() for c in copies}
    pre = {c.name: item_window(c) for c in copies}
    bench.debug("B", 21, 359)
    after = watch_windows(copies, pre, 8, (1.0,), "_after")
    for c in copies:
        w, r, idle, states = after[c.name]
        out.append(f"  {c.name}: boxes queued {w}, shown {r}, idle at the end {idle}; window states {states[:10]}")
        out.extend("    " + l[13:240] for l in c.since(marks[c.name])
                   if ("story sync:" in l and "chest" in l) or "asked for it again" in l)
    out.append("icon probes (tag, copy, probe):")
    out.extend(f"  {t} {n}: {p}" for t, n, p in WINDOW_PROBES)
    return ok and after["A"][0] == 1 and after["A"][1] == 1 and after["A"][2]


def chest_pose(copies, a, out):
    """TODO 1.234: the other Sora's chest-opening pose finds its chest when the two games made it at other addresses."""
    host, friend = copies
    rooms = {host.name: a.room, friend.name: a.friend_room}
    slots = {host.name: a.slot, friend.name: a.friend_slot or a.slot}
    bench.both(lambda c: scene.load_slot(c.game, slots[c.name], rooms[c.name]), copies)
    time.sleep(6)
    bench.warp(0x02, 0x09)
    bench.wait("both in 02/09", lambda: all(c.room() == (0x02, 0x09) and c.game.sora() for c in copies), 90, copies)
    time.sleep(8)  # the room's start
    ours = {c.name: chests(c) for c in copies}
    for bit in sorted(set(ours["A"]) | set(ours["B"])):
        ha, hb = ours["A"].get(bit), ours["B"].get(bit)
        out.append(f"chest {bit}: host {hex(ha[0]) if ha else '-'} at {tuple(round(v) for v in ha[1]) if ha else '-'}"
                   f"{' open' if ha and ha[2] else ''}; friend {hex(hb[0]) if hb else '-'}{' open' if hb and hb[2] else ''}")
    if a.together:
        return chest_together(copies, a, out, ours) and all(c.alive() and not c.crash for c in copies)
    # The old lookup takes any chest of the same object at the sender's exe offset: no offset may be in both games.
    offs = {c.name: {v[0] - c.game.base for v in ours[c.name].values()} for c in copies}
    same = sorted(f"exe+0x{o:X}" for o in offs["A"] & offs["B"])
    start = host.game.sora()  # where the host loaded in: the far spot, if it is far from chest 306
    runs = [chest_open(copies, out, host, 304, ours["A"].get(304, (0, start))[1]),
            chest_open(copies, out, friend, 305, ours["B"].get(305, (0, start))[1])]
    far = ours["A"].get(306)
    if far and math.dist(start[:3], far[1]) > 1000:
        chest_open(copies, out, host, 306, start[:3])
    else:
        out.append(f"the far opening not run: the host's start is {math.dist(start[:3], far[1]) if far else 0:.0f} from chest 306")
    if same:
        out.append(f"SETUP FAILURE: a chest at {', '.join(same)} in both games: the old lookup can find it there, "
                   "so the fix may not be what ran (--friend-slot)")
    good = all(r and len(r[0]) == 1 and len(r[1]) == 1 and not r[2] for r in runs)
    return good and not same and all(c.alive() and not c.crash for c in copies)


def twin_blade(copies, a, out):
    """TODO 5.20: the cutscene twin holds its Keyblade, with its effect. The Pete walk-in (tt_event_802, as skip; the
    hold at frame 4279, effect 17 tied to the Keyblade at 4338): past both, each SORA_WEAPON_R in the scene's actor
    table (*(exe+0xB653E0), 40-byte rows {object, id, handle}) must be held (+0x6A0 holder handle, +0x6A4 bone):
    Sora's by Sora, the twin's by the twin, same bone; and the effect table (*(exe+0xB653C8), 0x50-byte rows, +4 id,
    +0x4C tied actor row) must have a row tied to each of the two Keyblades.
    --peer-blade n (TODO 5.20a; save 59, all Keyblades): first the friend equips the n-th Keyblade in the real pause
    menu; then the crowned Sora (the scene's own) holds the host's Keyblade and the twin the friend's in both games:
    the twin's Keyblade row in the host's game and the scene's own in the friend's game name the other's object."""
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    peer = {}  # by game: the other player's Keyblade object its script names (5.20a)
    if a.peer_blade:
        host, friend = copies
        friend.game.pad.run(menu_weapon(a.peer_blade, 3000))
        came = lambda: find(host.since(marks[host.name]), "peer sora: the other Sora's record")
        bench.wait("the host has the friend's new Keyblade", lambda: any("changed" in l for l in came()), 30, copies)
        out.append("host: " + came()[-1][:160])
    bench.debug("A", 12, 0x02, 0x19, 0, 0, 2, 0, 1, 0, fmt="<IiBBBBHHHH")  # programs 2/0/1, as skip
    bench.wait("both in 02/19", lambda: all(c.room() == (0x02, 0x19) for c in copies), 90, copies)
    twinned = lambda c: next(iter(find(c.since(marks[c.name]), "cutscene twin: scene 'tt_event_802'")), None)
    bench.wait("tt_event_802 twinned in both", lambda: all(twinned(c) for c in copies), 30, copies)
    for c in copies:
        line = next(iter(find(c.since(marks[c.name]), "cutscene twin: the other player's Keyblade (object")), None)
        if line:
            peer[c.name] = int(line.split("(object ")[1].split(")")[0], 16)
            out.append(f"{c.name}: {line[:160]}")
    frame = lambda c: struct.unpack("<i", peek.read(c.game.h, c.game.base + 0xB64F98, 4))[0]  # scene frame (F129)
    t0, f0 = time.time(), frame(copies[0])
    bench.wait("the effect's tie (frame 4338) in both", lambda: all(frame(c) >= 4345 for c in copies), 240, copies,
               step=0.05)  # the scene frame counts slower than 60 a second (the first run's 120 s weren't enough)
    out.append(f"scene frames per second (host): {(frame(copies[0]) - f0) / (time.time() - t0):.1f}")
    ok = True
    for c in copies:
        h, base = c.game.h, c.game.base
        first_twin = int(twinned(c).split("(ids ")[1].split("..")[0])
        table = struct.unpack("<Q", peek.read(h, base + 0xB653E0, 8))[0]
        rows = [struct.unpack_from("<iiI", peek.read(h, table + i * 40, 12)) for i in range(0x60)]
        ids = {bench.decode(c, hd): rid for obj, rid, hd in rows if obj != -1 and hd}
        blades = []
        for obj, rid, hd in rows:
            if obj & 0xFFFF not in (573, peer.get(c.name, 573)) or not hd:  # SORA_WEAPON_R, or the other's (5.20a)
                continue
            holder, bone = struct.unpack("<IH", peek.read(h, bench.decode(c, hd) + 0x6A0, 6))
            blades.append((rid, ids.get(bench.decode(c, holder), -1) if holder else -1, bone, obj & 0xFFFF))
        out.append(f"{c.name} frame {frame(c)}: Keyblades (id, held by, bone), twins from {first_twin}: {blades}")
        fx_table = struct.unpack("<Q", peek.read(h, base + 0xB653C8, 8))[0]
        fx = []  # (effect id, the actor id it's tied to)
        for i in range(0x80):
            row = peek.read(h, fx_table + i * 0x50, 0x50)
            eid, tie = struct.unpack_from("<i", row, 4)[0], struct.unpack_from("<h", row, 0x4C)[0]
            if eid != -1 and 0 <= tie < 0x60:
                fx.append((eid, rows[tie][1]))
        skipped = [l for l in c.since(marks[c.name]) if "twin effect" in l and "skipped" in l]
        out.append(f"{c.name}: {twinned(c).split('; ')[-1][:60]}; tied effects (id, actor): {fx}; skipped: {skipped[:2]}")
        own = [b for b in blades if b[0] < first_twin]
        twin = [b for b in blades if b[0] >= first_twin]
        ok = ok and len(own) == 1 and len(twin) == 1 and own[0][1] >= 0 and twin[0][1] >= first_twin \
            and twin[0][2] == own[0][2] and not skipped \
            and any(a == own[0][0] for _, a in fx) and any(a == twin[0][0] for _, a in fx)
        if a.peer_blade and len(own) == 1 and len(twin) == 1:  # the other's object on the twin (host) / own (friend)
            other, ours = (twin, own) if c is copies[0] else (own, twin)
            fell_back = find(c.since(marks[c.name]), "isn't loaded as actor")
            out.append(f"{c.name}: the other's Keyblade on actor {other[0][0]}: object {other[0][3]:#x} (wanted "
                       f"{peer.get(c.name, 0):#x}); our own on {ours[0][0]}: {ours[0][3]:#x}; fell back: {fell_back[:1]}")
            ok = ok and c.name in peer and other[0][3] == peer[c.name] and ours[0][3] == 573
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"twin_blade_{c.name}.png"))
    return ok and all(c.alive() and not c.crash for c in copies)


def copy_sheet(copies, a, out):
    """TODO 3.16 / 3.4 (copy_sheet.cpp): the copy on its own stat sheet, save 59 in both. --env
    KH2COOP_COPY_SHEET=2: built from our own record (its numbers must equal our Sora's); 1 (default): from the other
    player's, after the friend equips the n-th Keyblade in the real pause menu (--peer-blade n, default 1) so the two
    differ (its numbers must equal the other game's own Sora's); --friend-slot N: the friend loads another save
    instead (another level, HP, armor and abilities; no menu). In each game: the copy's sheet is neither our Sora's
    nor in the pool; our Sora's sheet stays the pool's last slot; our record keeps its level, gear and abilities (all
    but HP, MP and the gauges) and the pool its free list (as many free, each a slot index, none twice) through a room
    change and back, where the new copy gets its own sheet again."""
    host, friend = copies
    if a.friend_slot:
        slots = {host.name: a.slot, friend.name: a.friend_slot}
        bench.both(lambda c: scene.load_slot(c.game, slots[c.name], a.room if c is host else None), copies)
        bench.wait("the friend in the host's room", lambda: all(c.room() == a.room for c in copies), 90, copies)
        time.sleep(8)
    else:
        load_both(copies, a.slot, a.room)
    test = any("TEST: the copy's own sheet from our own record" in l for l in host.all)
    marks = {c.name: c.mark() for c in copies}
    if not test and not a.friend_slot:
        friend.game.pad.run(menu_weapon(a.peer_blade or 1, 3000))
        built = lambda: find(host.since(marks[host.name]), "copy sheet: built from the other player's record")
        bench.wait("the host's copy rebuilt from the friend's new record", built, 30, copies)
    q = lambda c, at: struct.unpack("<Q", peek.read(c.game.h, at, 8))[0]
    pool = (0x2A17290, 0x2A23810)

    def sheets(c):
        base = c.game.base
        sora = q(c, base + 0x2A171C8)
        copy = next((x for x, n in bench.actors(c) if n.startswith("P_EX100") and x != sora), 0)
        return base, q(c, sora + 0x5C0), q(c, copy + 0x5C0) if copy else 0

    def numbers(c, sheet):
        raw = peek.read(c.game.h, sheet, 0x190)
        return struct.unpack_from("<i", raw, 4)[0], struct.unpack_from("<4H", raw, 0x188)

    def record(c):  # our record but HP / MP now (+4, +6) and the gauges (+0xC..+0xE)
        r = bytearray(peek.read(c.game.h, c.game.base + 0x9ABDA0, 0x114))
        r[4] = r[6] = r[0xC] = r[0xD] = r[0xE] = 0
        return bytes(r)

    def free_list(c):  # (free count, the free slot indices) of the sheet pool (L1977)
        n = struct.unpack("<I", peek.read(c.game.h, c.game.base + 0x2A23950, 4))[0]
        return n, struct.unpack("<80I", peek.read(c.game.h, c.game.base + 0x2A23810, 320))[:min(n, 80)]

    ok = True
    before = {c.name: record(c) for c in copies}
    pools = {c.name: free_list(c) for c in copies}
    for c in copies:
        other = friend if c is host else host
        base, ours, theirs = sheets(c)
        in_pool = base + pool[0] <= theirs < base + pool[1]
        source = ours if test else sheets(other)[1]
        want = numbers(c if test else other, source)
        got = numbers(c, theirs) if theirs else None
        out.append(f"{c.name}: our sheet exe+{ours - base:#x}, the copy's {theirs:#x} (in the pool: {in_pool}); "
                   f"copy max HP / str, mag, def, AP {got}, wanted {want}")
        ok = ok and theirs and theirs != ours and not in_pool and ours == base + 0x2A23598 and got == want
    bench.warp(0x02, 0x28, 0)  # the gym, then back to the save's room
    bench.wait("both in the gym", lambda: all(c.room() == (0x02, 0x28) for c in copies), 60, copies)
    time.sleep(5)
    bench.warp(a.room[0], a.room[1], 0xFFFF)
    bench.wait("both back", lambda: all(c.room() == a.room for c in copies), 60, copies)
    time.sleep(8)
    for c in copies:
        base, ours, theirs = sheets(c)
        swaps = find(c.since(marks[c.name]), "has its own sheet")
        same = record(c) == before[c.name]
        (n0, f0), (n1, f1) = pools[c.name], free_list(c)
        clean = n1 == n0 and n1 <= 80 and all(i < 80 for i in f1) and len(set(f1)) == len(f1)
        out.append(f"{c.name}: after the round trip: copy sheet {theirs:#x}, own sheets made {len(swaps)}, "
                   f"our record unchanged: {same}; pool free {n0} -> {n1}, the list clean: {clean} "
                   f"(top {list(f1[-4:])})")
        out.extend(f"   {c.name} " + l[:200] for l in find(c.since(marks[c.name]), "copy sheet:")[:8])
        ok = ok and same and clean and theirs and theirs != ours and len(swaps) >= 1
    ok = ok and all(c.alive() and not c.crash for c in copies)
    # A copy torn down with no room load: the friend's game closes, the host plays on. Our Sora's own pool slot (79)
    # must never be on the free list (it would be if the copy had still been on our sheet).
    m, (n0, _) = host.mark(), free_list(host)
    friend.proc.kill()
    bench.wait("the host sees the link lost", lambda: find(host.since(m), "link watch: connection lost"), 20, [host])
    time.sleep(2)
    bench.debug("A", 10, 3)  # play on with Donald and Goofy
    bench.wait("playing on", lambda: find(host.since(m), "link watch: playing on with Donald and Goofy"), 20, [host])
    time.sleep(8)
    n1, f1 = free_list(host)
    gone = not sheets(host)[2]
    clean = 79 not in f1 and all(i < 80 for i in f1) and len(set(f1)) == len(f1)
    out.append(f"A: the friend closed: the copy torn down {gone}; pool free {n0} -> {n1}, slot 79 free: {79 in f1}, "
               f"the list clean: {clean}")
    return ok and clean and host.alive() and not host.crash


def same_room(copies, a, out):
    """TODO 5.23: a scene that ends with a jump into its own room (tt_event_805, F536's kind). See the usage text."""
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x02, 0x1B, 0, 0, 1, 0, 1, 0, fmt="<IiBBBBHHHH")  # programs 1/0/1: tt27 evt 1 plays 805
    bench.wait("both in 02/1B", lambda: all(c.room() == (0x02, 0x1B) for c in copies), 90, copies)
    new = lambda c: c.since(marks[c.name])
    starts = lambda c: find(new(c), "scene log: cutscene '")
    bench.wait("the scene starts in both games", lambda: all(starts(c) for c in copies), 40, copies)
    scene_name = starts(host)[0].split("'")[1]
    out.append(f"scene: {scene_name}")
    time.sleep(4)  # past the scene's first frames
    late = host if a.by == "A" else friend  # the one whose scene ends last
    first = friend if late is host else host
    paused = {c.name: c.mark() for c in copies}  # the scene ends' asks only (the host's warp asked for 02/1B too)
    late.game.pad.run(["tap", "start", "wait", "1200"])  # its own pause only (PAUSE_SYNC=0)
    asks = lambda c: find(c.since(paused[c.name]), "scene log: the game asks for world 0x02 room 0x1B")
    bench.wait(f"{first.name}'s scene ends while {late.name} is paused", lambda: asks(first), 300, copies)
    time.sleep(8)  # long enough for follow's pull-back (the old replay) to show
    if a.then == "close":
        host.proc.kill()
        bench.wait("the friend's held move goes", lambda: find(new(friend), "doing our own held move now"), 30,
                   [friend])
        time.sleep(15)
    else:
        late.game.pad.run(["tap", "down", "wait", "400", "tap", "cross", "wait", "400"] if a.then == "skip"
                          else ["tap", "start", "wait", "800"])
        bench.wait(f"{late.name}'s scene ends", lambda: asks(late) or find(new(late), "skip sync: we skipped"), 300,
                   copies)
        time.sleep(20)
    keys = ("scene log: cutscene '", "scene log: the game asks for world 0x02 room 0x1B", "follow: holding",
            "follow: our scene's move", "held move", "follow: the host goes", "follow: the host skipped",
            "load barrier:", "link watch:", "follow: the host started loading", "skip sync:")
    for c in copies:
        if c is host and a.then == "close":
            continue
        out.append(f"{c.name}:")
        for l in new(c):
            if any(k in l for k in keys):
                out.append("   " + l[:220])
    plays = {c.name: sum(1 for l in starts(c) if f"'{scene_name}" in l) for c in copies}
    held = find(new(friend), "follow: holding our scene's move")
    went = find(new(friend), "doing our own held move now")
    out.append(f"starts of {scene_name}: {plays}; friend holds {len(held)}, held moves done {len(went)}")
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"same_room_{a.by}_{a.then}_{c.name}.png"))
    alive = friend.alive() and not friend.crash and (a.then == "close" or (host.alive() and not host.crash))
    if a.by == "B":  # host first: nothing held, no replay
        return alive and plays == {"A": 1, "B": 1} and not held
    return alive and plays["B"] == 1 and bool(held) and bool(went)


def scaffold(copies, a, out):
    """TODO 1.193: Timeless River's scaffold F_WI360 (0D/05 programs 55/55/55, the mission). See the usage text."""
    import netbench
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x0D, 0x05, 0, 0, 55, 55, 55, 0, fmt="<IiBBBBHHHH")
    bench.wait("both in 0D/05", lambda: all(c.room() == (0x0D, 0x05) and c.game.sora() for c in copies), 90, copies)
    bench.wait("F_WI360 in both games", lambda: all(bench.actor_by_model(c, "F_WI360") for c in copies), 30, copies)
    time.sleep(8)  # the mission's start scene
    fp = friend.game.sora()
    if a.ride != "host" and fp and fp[1] > 100:  # under lag our scaffold was parked a frame at arrival (TODO 1.194)
        bench.debug("B", 15, -407.0, -60.0, 376.0, fmt="<Iifff")
        out.append(f"the friend's Sora fell to y {fp[1]:.0f} at arrival (TODO 1.194): put back on the scaffold")
        time.sleep(2)
    wi = {c.name: bench.actor_by_model(c, "F_WI360") for c in copies}
    copy = netbench.find_copy(host.game)
    out.append(f"F_WI360: host {wi['A']:#x}, friend {wi['B']:#x}; the host's copy {copy:#x}; ride {a.ride}")
    rides = lambda c, actor: bench.decode(c, read_u(c, actor + 0x9D8)) if actor else 0
    up = {"friend": host, "host": friend}.get(a.ride)  # the one kept in the air, off the scaffold
    stop, samples = [], []

    def keep_up():
        while not stop:
            p = up.game.sora()
            if p:
                bench.debug(up.name, 15, p[0], -1500.0, p[2], fmt="<Iifff")
            time.sleep(0.4)

    def sample():
        while not stop:
            try:
                ha, hb = head(host), head(friend)
                samples.append((time.time(), rides(host, ha) == wi["A"], rides(host, copy) == wi["A"],
                                rides(friend, hb) == wi["B"], rides(host, copy), actor_pos(friend, hb)[1],
                                actor_pos(host, ha)[1], bench.motion(host, wi["A"]), bench.motion(friend, hb)))
            except OSError:
                pass
            time.sleep(0.05)
    threads = [threading.Thread(target=sample, daemon=True)] + (
        [threading.Thread(target=keep_up, daemon=True)] if up else [])
    for t in threads:
        t.start()
    end = time.time() + 40  # snap 1 came 720 mission frames after its timer started (~16 s after the load)
    while time.time() < end and not any(s[7] == 232 for s in samples):
        time.sleep(0.5)
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                stop.append(1)
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    shook = any(s[7] == 232 for s in samples)
    # who presses triangle for Air Slash through the shake, after --mash-delay s
    mashers = ([] if a.mash == "none" else [host, friend] if a.mash == "both"
               else [{"friend": friend, "host": host, "both": host}[a.ride]])

    def mash(m):
        time.sleep(a.mash_delay)
        while not stop:
            m.game.pad.run(["tap", "triangle", "wait", "150"])
    if shook:
        for m in mashers:
            threads.append(threading.Thread(target=mash, args=(m,), daemon=True))
            threads[-1].start()
        # The first Air Slashes as each screen shows them (your report 2026-11-14: the friend's looked smaller).
        hud_strip(copies, f"slash_{a.ride}", 6 + a.mash_delay, lambda: None, box=(0, 0, 1, 1), name="scaffold", width=400)
        time.sleep(8)
    stop.append(1)
    for t in threads:
        t.join(1)
    out.append(f"shake (motion 232) seen: {shook}; samples {len(samples)}; {'+'.join(m.name for m in mashers) or 'nobody'} mashed triangle after {a.mash_delay:g} s")
    n = max(1, len(samples))
    out.append(f"rides F_WI360 (share of samples): host's Sora {sum(s[1] for s in samples) / n:.2f}, the host's copy "
               f"{sum(s[2] for s in samples) / n:.2f}, the friend's Sora {sum(s[3] for s in samples) / n:.2f}; "
               f"the copy's +0x9D8 decoded to other things: {sorted({hex(s[4]) for s in samples if s[4] and not s[2]})[:4]}")
    fy, hy = min((s[5] for s in samples), default=0), min((s[6] for s in samples), default=0)
    flights = sum(1 for i in range(1, len(samples)) if samples[i][8] == 3 and samples[i - 1][8] != 3)
    hl, fl = host.since(marks["A"]), friend.since(marks["B"])
    sent = find(hl, "a launch, sent with no grab")
    ran = find(fl, "ran the host's jump")
    picked = find(hl, "has the friend's copy as its player")
    slashes = {c.name: len(find(c.since(marks[c.name]), "command 364")) for c in copies}
    out.append(f"highest point (y grows down): the friend's Sora {fy:.0f}, the host's {hy:.0f}; the friend's flights "
               f"(entries into motion 3) {flights}; launches sent {len(sent)}, run by the friend {len(ran)}; the ride "
               f"rule's lines {len(picked)}; 'command 364' lines host {slashes['A']}, friend {slashes['B']}")
    # TODO 1.199: Air Slash's script asks for its user's lock-on target; for the friend's copy the host answers with the
    # friend's own (the friend tells it), not the host player's.
    asks = find(hl, "asked for the copy's lock-on target")
    lock_off = any("COPY_LOCKON=0" in e for e in a.env)
    out.append(f"lock-on: the scaffold's script asked for the copy's {len(asks)} times: a target given "
               f"{sum('none: the script' not in l for l in asks)}, another one than the host player's "
               f"{sum(': another one' in l for l in asks)}; the friend told its target "
               f"{len(find(fl, 'our lock-on target is now'))} times")
    out.extend("   " + l[13:240] for l in asks[:6])
    keys = ("as its player: the copy rides", "grab sync:", "command 364", "trap_obj_hook", "method_obj_jump", "is_ride")
    for c in copies:
        lines = [l for l in c.since(marks[c.name]) if any(k in l for k in keys) and "parity" not in l
                 and "act_table" not in l]
        out.append(f"{c.name}: {len(lines)} lines")
        out.extend("   " + l[:220] for l in lines[:30])
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"scaffold_{a.ride}_{c.name}.png"))
    alive = all(c.alive() and not c.crash for c in copies)
    near = lambda y: -1400 < y < -900  # the host's own launches peaked at -1153 and -1198 (two-PC run)
    if a.ride == "both" and a.mash == "both":
        # TODO 1.212's exception row (reaction_sync.cpp kBothRcs): both Soras use Air Slash, neither press refused
        # for the other's. No height rule here: a slash early in the flight ends the rise.
        ours, theirs = find(hl, "our own command 364"), find(hl, "the friend used command 364")
        lost = find(hl, "dropped: a script holds the friend") + find(hl, "a script holds our player after")
        out.append(f"both press: the host's slashes {len(ours)}, the friend's run in the host's game {len(theirs)}, "
                   f"presses dropped for the other's {len(lost)}, target frees held "
                   f"{len(find(hl, 'target pool'))}")
        return alive and shook and bool(ours) and bool(theirs) and not lost
    if a.ride == "host":  # control: the host's own launch as today, nothing sent for the copy
        return alive and shook and near(hy) and not sent
    ok = alive and shook and near(fy) and 0 < len(sent) <= flights  # one launch a flight: a relaunch would show
    if a.mash == "none":  # nobody slashes, so the friend's Sora lands again: launched each time (your look, 2026-11-16)
        ok = ok and len(sent) >= 2 and len(ran) >= 2
    # a slash of the friend's that ran in the host's game (not one dropped for the host's own): its lock-on was asked for
    ok = ok and (lock_off or not find(hl, "the friend used command 364") or bool(asks))
    return ok and (near(hy) if a.ride == "both" else True)


def f3(c, addr):
    return struct.unpack("<3f", peek.read(c.game.h, addr, 12))


def bone_place(c, actor):
    """Research (TODO 1.195): Sora's head bone (140) in the world, the draw matrix's place and bone 0's x and z headings.
    A lead from Volpestyle's HOST_CROWN.md (F561): bone model-space matrices at *(*(actor+0x7B0)+0x50) + 0x40 * bone,
    the actor's draw matrix at +0x808; world = the bone's place with y and z negated, times that matrix."""
    m = struct.unpack("<16f", peek.read(c.game.h, actor + 0x808, 64))
    bones = read_u(c, read_u(c, actor + 0x7B0, 8) + 0x50, 8)
    b = f3(c, bones + 0x40 * 140 + 0x30)
    v = (b[0], -b[1], -b[2])
    place = tuple(v[0] * m[k] + v[1] * m[4 + k] + v[2] * m[8 + k] + m[12 + k] for k in range(3))
    r = struct.unpack("<12f", peek.read(c.game.h, bones, 48))  # bone 0's rotation rows, for the body's facing
    yaws = []
    for row in (r[0:3], r[8:11]):  # its x and z rows in the world: their headings in degrees
        u = (row[0], -row[1], -row[2])
        w = [u[0] * m[k] + u[1] * m[4 + k] + u[2] * m[8 + k] for k in range(3)]
        yaws.append(math.degrees(math.atan2(w[0], w[2])))
    return place, m[12:15], tuple(yaws)


def grab_sample(host, friend, copy, friend_sora):
    """One row of a grab trace (TODO 1.195): time; the friend's Sora drawn (+0x70) and placed (+0x670); the host's copy
    drawn; the friend's motion; bone_place of the friend's Sora (5-7) and of the host's copy (8-10); the friend's
    +0x18C bit 14 (in a grab pose, 11)."""
    return (time.time(), f3(friend, friend_sora + 0x70), f3(friend, friend_sora + 0x670), f3(host, copy + 0x70),
            bench.motion(friend, friend_sora), *bone_place(friend, friend_sora), *bone_place(host, copy),
            bool(read_u(friend, friend_sora + 0x18C) & (1 << 14)))


def release_report(out, track, t0, fl, left, last=False):
    """The friend's release in a grab trace (TODO 1.195, F563): from the first (last=True: the last) row where
    left(previous, row), its head no further than 200 from its place for 0.5 s; each game's turn; the fix's lines in
    the friend's log fl. True = smooth and the fix's flag cleared."""
    d = lambda p, q: sum((p[i] - q[i]) ** 2 for i in range(3)) ** 0.5
    smooth = True
    lefts = [s[0] for i, s in enumerate(track) if i and left(track[i - 1], s)]
    go = (lefts[-1] if last else lefts[0]) if lefts else None
    if go is None:
        out.append("release: no let go seen in the trace")
        smooth = False
    else:
        after = [s for s in track if 0 <= s[0] - go <= 0.5]
        far, host_far = max(d(s[5], s[1]) for s in after), max(d(s[8], s[3]) for s in after)
        smooth = far < 200
        out.append(f"release: the friend's head at most {far:.0f} from its place in the 0.5 s after the let go "
                   f"(the host's copy {host_far:.0f}): {'smooth' if smooth else 'BLINK'} (limit 200)")
        # The turn at each game's release (its draw matrix back on its own place): bone 0's x heading in the grab
        # pose, in its first own frame and 0.5 s after the friend's let go (the host's copy follows the friend's).
        end = next((s for s in track if s[0] - go >= 0.5), track[-1])
        for who, mi, ri, yi in (("friend", 6, 1, 7), ("host's copy", 9, 3, 10)):
            own = [i for i, s in enumerate(track) if s[0] > go - 1.0 and d(s[mi], s[ri]) < 5]
            if not own or own[0] == 0:
                out.append(f"release heading ({who}): no switch to its own place seen")
                continue
            k = own[0]
            out.append(f"release heading ({who}, bone 0's x): grab pose {track[k - 1][yi][0]:.0f}, first own frame "
                       f"{track[k][yi][0]:.0f} (at {track[k][0] - t0:+.3f}), 0.5 s after the let go {end[yi][0]:.0f} deg")
    out.extend("   " + l[13:170] for l in fl if "after the let go: its" in l or "first update out of the grab" in l)
    if not any("first update out of the grab pose" in l for l in fl):
        out.append("release: no 'first update out of the grab pose' line: the no-blend flag never armed or never cleared")
        smooth = False
    elif not smooth and not any("after the let go: its" in l for l in fl):
        out.append("release: the fix armed but dropped no blend: the fall's motion didn't go through our hooked "
                   "MotionChainSet (exe+0x3C88C0; F564 names two other setters)")
    return smooth


def hydra(copies, a, out):
    """TODO 1.191: the friend's Phil One-Two (346) then Urninator (420) on the Hydra B_HE100. See the usage text."""
    import netbench
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x06, 0x12, 0, 0, 171, 171, 171, 0, fmt="<IiBBBBHHHH")
    try:
        bench.wait("both in 06/12", lambda: all(c.room() == (0x06, 0x12) and c.game.sora() for c in copies), 90, copies)
        bench.wait("B_HE100 in both games", lambda: all(bench.actor_by_model(c, "B_HE100") for c in copies), 30, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} may not reach the Hydra: refresh the bench's save copy)")
        return False
    time.sleep(10)  # the mission's start scene (re_he18_ms108-0a)
    mission = find(host.since(marks["A"]), "HE18_MS108")
    hy = {c.name: bench.actor_by_model(c, "B_HE100") for c in copies}
    copy = netbench.find_copy(host.game)
    hp = actor_pos(host, hy["A"])
    out.append(f"B_HE100: host {hy['A']:#x} at ({hp[0]:.0f}, {hp[1]:.0f}, {hp[2]:.0f}), friend {hy['B']:#x}; the host's "
               f"copy {copy:#x}; mission line {'yes' if mission else 'NO'}")
    dist = lambda c, actor: (lambda p, q: ((p[0] - q[0]) ** 2 + (p[2] - q[2]) ** 2) ** 0.5)(
        c.game.sora(), actor_pos(c, actor))

    # Phase 1: the host in front, locked on, mashing attack and triangle until its Vanquish (238) runs.
    front, park, back = (-88.0, -30.0, 0.0), (-88.0, -30.0, -200.0), (-62.0, -507.0, 809.0)  # two-PC run 2026-11-12
    bench.debug("B", 15, -88.0, -30.0, -900.0, fmt="<Iifff")  # the friend out of the way
    bench.debug("A", 15, *front, fmt="<Iifff")
    time.sleep(1)
    host.game.goto(hp[0], hp[2], 60, 1.0)  # a step towards it: facing it
    locked, vanquish = 0, []
    end = time.time() + 150
    while time.time() < end and not vanquish:
        if host.game.lock() != 2:
            host.game.pad.run(["tap", "r1", "wait", "150"])
        else:
            locked += 1
        if dist(host, hy["A"]) > 1400:
            bench.debug("A", 15, *front, fmt="<Iifff")
        host.game.pad.run(MASH_X_TRIANGLE)
        check = host.since(marks["A"])
        vanquish = [l for l in find(check, "our own command 238 on B_HE100")]
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
    out.append(f"phase 1: Vanquish (238) {'ran: ' + vanquish[0][:90] if vanquish else 'NEVER ran (setup failure)'}; "
               f"locked on in {locked} loops; the host {dist(host, hy['A']):.0f} from the Hydra")
    if not vanquish:
        out.append("SETUP FAILURE: no Vanquish in 150 s (the head-cut RC needs the host in front, facing it)")
        return False
    end = time.time() + 30
    while time.time() < end and bench.motion(host, hy["A"]) != 194:
        time.sleep(0.25)
    down = bench.motion(host, hy["A"]) == 194
    out.append(f"the Hydra down (motion 194): {down}")
    if not down:
        out.append("SETUP FAILURE: the Hydra never went down after Vanquish")
        return False

    # The control setup: the host parked ~1000 away, the friend on the back, so the boss takes the copy.
    bench.debug("A", 15, *park, fmt="<Iifff")
    mark2 = {c.name: c.mark() for c in copies}
    bench.debug("B", 15, *back, fmt="<Iifff")
    time.sleep(1)
    if friend.game.sora()[1] > -400:
        bench.debug("B", 15, *back, fmt="<Iifff")
    time.sleep(5)
    picks = [l for l in host.since(marks["A"]) if "aggro: boss B_HE100" in l and "goes for" in l]
    out.append(f"the Hydra's pick before the press: {picks[-1][13:130] if picks else 'none logged'}")

    # The friend mashes triangle: Phil One-Two (346), then Urninator (420). A sampler on the copy meanwhile.
    stop, samples, track = [], [], []
    friend_sora = head(friend)

    def sample():
        last = None
        while not stop:
            try:
                if a.trace_grab:  # where the friend's Sora is drawn and placed, and the host's copy
                    track.append(grab_sample(host, friend, copy, friend_sora))
                row = (read_u(host, copy + 0xD48, 8) != 0, bool(read_u(host, copy + 0x18C) & (1 << 21)),
                       read_u(host, copy + 0x638) & 0xFFFF, bench.motion(host, copy), bench.motion(host, hy["A"]),
                       bool(read_u(friend, friend_sora + 0x6C8) & (1 << 25)),
                       read_u(friend, friend.game.base + 0x2A24CC0) != 0)
                if row != last:
                    samples.append((time.time(), row))
                    last = row
            except OSError:
                pass
            time.sleep(0.003 if a.trace_grab else 0.004)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    frames = []  # --burst: (time, half-size image) of the friend's window around the grab start and the release

    def burst():
        import collections
        from PIL import Image
        ring = collections.deque(maxlen=30)  # ~0.35 s before each event
        hwnd = friend.hwnd
        last, after = None, 0
        while not stop:
            try:
                mot = bench.motion(friend, friend_sora)
            except OSError:
                mot = last
            if last is not None and mot != last and (mot in (258, 259) or last == 259):  # the grabs' starts, the release
                frames.extend(ring)
                ring.clear()
                after = 70  # ~0.85 s: the let go comes ~0.2 s after the fall motion
            last = mot
            r = wt.RECT()
            bench.user32.GetClientRect(hwnd, ctypes.byref(r))
            w, h = r.right, r.bottom
            hdc = bench.user32.GetDC(hwnd)
            mem = bench.gdi32.CreateCompatibleDC(hdc)
            bmp = bench.gdi32.CreateCompatibleBitmap(hdc, w, h)
            old = bench.gdi32.SelectObject(mem, bmp)
            now = time.time()
            bench.user32.PrintWindow(hwnd, mem, 3)
            bench.gdi32.SelectObject(mem, old)
            header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0), 44)
            buf = ctypes.create_string_buffer(w * h * 4)
            bench.gdi32.GetDIBits(hdc, bmp, 0, h, buf, header, 0)
            bench.gdi32.DeleteObject(bmp)
            bench.gdi32.DeleteDC(mem)
            bench.user32.ReleaseDC(hwnd, hdc)
            shot = (now, Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1).reduce(2))
            if after:
                frames.append(shot)
                after -= 1
            else:
                ring.append(shot)
    if a.burst:
        import ctypes
        import ctypes.wintypes as wt
        tb = threading.Thread(target=burst, daemon=True)
        tb.start()
    pressed, used420, shots = None, [], set()
    end = time.time() + 30
    while time.time() < end:  # mashed on to +7 s after 346, as a player would: a second 420 must not run
        friend.game.pad.run(bench.MASH_TRIANGLE)
        hl = host.since(mark2["A"])
        if not pressed and find(hl, "the friend used command 346 of our B_HE100"):
            pressed = time.time()
            end = pressed + 7
        if pressed:
            for k in (0.2, 4.2):
                if k not in shots and time.time() - pressed >= k:
                    shots.add(k)
                    for c in copies:
                        bench.screenshot(c.hwnd, os.path.join(OUT, f"hydra_{k:g}s_{c.name}.png"))
        elif friend.game.sora()[1] > -400:  # fell off the back before pressing
            bench.debug("B", 15, *back, fmt="<Iifff")
        used420 = find(hl, "the friend used command 420")
    time.sleep(5)
    stop.append(1)
    t.join(1)
    if a.burst:
        tb.join(5)
    hl, fl = host.since(mark2["A"]), friend.since(mark2["B"])
    ours = f"#{head(host) & 0xFFFFFF:X}"
    switch = [l for l in hl if "aggro: boss B_HE100 now goes for our Sora" in l]
    polls = [l for l in hl if "sysobj_player" in l and "pc 1358" in l and ours in l]
    frees = [l for l in hl if "attack_free" in l and "pc 1413" in l]
    offers = find(hl, "offers command 420 to the friend's copy")
    taken = find(hl, "the friend's command 420 on host#")  # a repeat refused (taken once, or its attack already gone)
    out.append(f"346 pressed: {'yes' if pressed else 'NO'}; 420 offered {len(offers)}, run for the friend {len(used420)} "
               f"(repeats refused: {len(taken)}); the friend's own 420 sends {len(find(fl, 'we used command 420'))}")
    out.append(f"F532's signature: the switch to our Sora {len(switch)}, the 1358 poll naming our Sora {len(polls)}, "
               f"the 1413 free {len(frees)}")
    for l in (switch + polls[:2] + frees[:2] + offers[:2] + used420[:2]):
        out.append("   " + l[:200])
    t0 = pressed or (samples[0][0] if samples else time.time())
    out.append("samples (s from the 346 line: hook, bit 21, chain, copy motion, Hydra motion, friend bit 25, Limit):")
    out.extend(f"   {s[0] - t0:+.3f} {s[1]}" for s in samples[:60])
    if a.trace_grab and track:  # the friend's screen: its Sora's biggest jumps between samples, against the host's copy
        d = lambda p, q: sum((p[i] - q[i]) ** 2 for i in range(3)) ** 0.5
        jumps = sorted(((d(track[i][1], track[i - 1][1]), i) for i in range(1, len(track))), reverse=True)[:8]
        out.append(f"grab trace: {len(track)} samples over {track[-1][0] - track[0][0]:.1f} s; the friend's Sora's "
                   f"biggest jumps (s from the 346 line: jump, drawn before -> after, place, host's copy, motion):")
        for j, i in sorted(jumps, key=lambda x: x[1]):
            s0, s1 = track[i - 1], track[i]
            out.append(f"   {s1[0] - t0:+.3f} {j:6.0f}: ({s0[1][0]:.0f}, {s0[1][1]:.0f}, {s0[1][2]:.0f}) -> "
                       f"({s1[1][0]:.0f}, {s1[1][1]:.0f}, {s1[1][2]:.0f}); place ({s1[2][0]:.0f}, {s1[2][1]:.0f}, "
                       f"{s1[2][2]:.0f}); host's copy ({s1[3][0]:.0f}, {s1[3][1]:.0f}, {s1[3][2]:.0f}); motion {s1[4]}")
        with open(os.path.join(OUT, "hydra_grab_trace.txt"), "w") as fh:
            for s in track:
                fh.write(f"{s[0] - t0:+.4f} drawn {s[1][0]:.1f} {s[1][1]:.1f} {s[1][2]:.1f} place {s[2][0]:.1f} "
                         f"{s[2][1]:.1f} {s[2][2]:.1f} host {s[3][0]:.1f} {s[3][1]:.1f} {s[3][2]:.1f} motion {s[4]} | "
                         f"head {s[5][0]:.0f} {s[5][1]:.0f} {s[5][2]:.0f} m808 {s[6][0]:.0f} {s[6][1]:.0f} {s[6][2]:.0f} "
                         f"yaw {s[7][0]:.0f} {s[7][1]:.0f} | host head {s[8][0]:.0f} {s[8][1]:.0f} {s[8][2]:.0f} m808 "
                         f"{s[9][0]:.0f} {s[9][1]:.0f} {s[9][2]:.0f} yaw {s[10][0]:.0f} {s[10][1]:.0f}\n")
        marks_f = [l for l in fl if any(k in l for k in ("grab sync:", "release trace", "detach", "let go"))]
        out.append("friend's grab sync lines:")
        out.extend("   " + l[:170] for l in marks_f[:30])
    if a.burst and frames and track:  # contact sheets of the friend's window around the grab start and the release
        from PIL import Image, ImageDraw
        moves = [(track[i][0], track[i - 1][4], track[i][4]) for i in range(1, len(track)) if track[i][4] != track[i - 1][4]]
        events = [(n, tm) for n, tm in (("start", next((x[0] for x in moves if x[2] == 258), None)),
                                        ("urninator", next((x[0] for x in moves if x[2] == 259), None)),
                                        ("release", next((x[0] for x in moves if x[1] == 259), None))) if tm]
        lg = next((l for l in fl if "held stream ended (let go)" in l), None)  # the friend's let go, by its log clock
        if lg:
            import datetime
            clock = datetime.datetime.strptime(lg[:12], "%H:%M:%S.%f").time()
            events.append(("letgo", datetime.datetime.combine(datetime.date.today(), clock).timestamp()))
        bdir = os.path.join(OUT, "hydra_burst")
        os.makedirs(bdir, exist_ok=True)
        for old in os.listdir(bdir):
            os.remove(os.path.join(bdir, old))
        span = [(f[0] - frames[0][0]) for f in frames]
        out.append(f"burst: {len(frames)} frames over {span[-1]:.1f} s ({len(frames) / max(span[-1], 0.1):.0f} a second); "
                   f"events {[(n, round(tm - t0, 3)) for n, tm in events]}")
        for name, tm in events:
            pick = [f for f in frames if -0.15 <= f[0] - tm <= 0.85]
            if not pick:
                continue
            for ft, im in pick:  # each frame on its own too, to look at one closely
                im.save(os.path.join(bdir, f"{name}_{(ft - tm) * 1000:+05.0f}ms.png"))
            pick = pick[::max(1, (len(pick) + 47) // 48)]
            tw, th = pick[0][1].width // 3, pick[0][1].height // 3
            sheet = Image.new("RGB", (tw * 6, th * ((len(pick) + 5) // 6)))
            for k, (ft, im) in enumerate(pick):
                cell = im.resize((tw, th))
                ImageDraw.Draw(cell).text((4, 4), f"{(ft - tm) * 1000:+.0f} ms", fill=(255, 255, 0))
                sheet.paste(cell, ((k % 6) * tw, (k // 6) * th))
            sheet.save(os.path.join(OUT, f"hydra_burst_{name}.png"))
            out.append(f"burst sheet hydra_burst_{name}.png: {len(pick)} frames from {(pick[0][0] - tm) * 1000:+.0f} to "
                       f"{(pick[-1][0] - tm) * 1000:+.0f} ms around the {name}")
    smooth = True
    if a.trace_grab and track:  # the friend's release: the first row out of Urninator (259)
        smooth = release_report(out, track, t0, fl, lambda p, s: p[4] == 259 and s[4] != 259)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"hydra_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and len(used420) == 1 and smooth


def cerberus(copies, a, out):
    """TODO 1.195: Cerberus B_HE020 grabs the friend's Sora (his bite started by the dev command 'act'), the friend
    presses his three RCs; the grab start, each RC's switch and the release traced. See the usage text."""
    import kh2cmd
    import netbench
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x06, 0x07, 0, 0, 114, 114, 114, 0, fmt="<IiBBBBHHHH")  # the fight's version (door 0)
    try:
        bench.wait("both in 06/07", lambda: all(c.room() == (0x06, 0x07) and c.game.sora() for c in copies), 90, copies)
        bench.wait("B_HE020 in both games", lambda: all(bench.actor_by_model(c, "B_HE020") for c in copies), 30, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} may not reach Cerberus through the warp)")
        return False
    time.sleep(5)
    ce = bench.actor_by_model(host, "B_HE020")
    copy = netbench.find_copy(host.game)
    cp = actor_pos(host, ce)
    out.append(f"B_HE020: host {ce:#x} at ({cp[0]:.0f}, {cp[1]:.0f}, {cp[2]:.0f}), friend "
               f"{bench.actor_by_model(friend, 'B_HE020'):#x}; the host's copy {copy:#x}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"cerberus_setup_{c.name}.png"))
    if a.setup_only:
        return all(c.alive() and not c.crash for c in copies)

    # The host parked away (where it stood in a two-PC fight), the friend's Sora held in the air in front of him, so
    # the aggro takes the copy and his bite (F565) can reach it.
    park = (-1541.0, 189.0, -721.0)
    friend_sora = head(friend)
    captured = lambda: bool(read_u(friend, friend_sora + 0x18C) & (1 << 14))
    grabbed = lambda: read_u(host, copy + 0xD48, 8) != 0 or bench.motion(host, ce) == 222 or captured()
    dist = lambda p, q: math.hypot(p[0] - q[0], p[2] - q[2])

    def spot():  # 400 in front of him (F98: forward = (sin a, 0, cos a)), 250 above his floor (y grows down)
        p = actor_pos(host, ce)
        ang = struct.unpack("<f", peek.read(host.game.h, ce + 0x640 + 0x4C, 4))[0]
        return (p[0] + 400 * math.sin(ang), p[1] - 250, p[2] + 400 * math.cos(ang))
    hold, stop, track = {"at": spot()}, [], []

    def keep_up():  # until the grab: the friend's Sora put back at the spot (once grabbed it's the grab's)
        while not stop:
            if hold["at"] and not grabbed():  # the host's hook first: no move in the grab's start
                bench.debug("B", 15, *hold["at"], fmt="<Iifff")
            time.sleep(0.3)

    def sample():  # grab_sample's row + Cerberus's motion (12) and the host's copy hooked (13)
        while not stop:
            try:
                track.append(grab_sample(host, friend, copy, friend_sora) +
                             (bench.motion(host, ce), read_u(host, copy + 0xD48, 8) != 0))
            except OSError:
                pass
            time.sleep(0.003)
    threads = [threading.Thread(target=f, daemon=True) for f in (keep_up, sample)]
    for t in threads:
        t.start()
    bench.debug("A", 15, *park, fmt="<Iifff")

    # His bite started by the dev command (shared_move.cpp) once his pick is the copy and he stands on the floor, not
    # in a jump (171, 172), a bite (221) or his dark breath (231). Each try waits 3 s for the grab: a bite can hit
    # Hercules instead.
    tries, replies, grab_at, last_y = 0, [], None, None
    end = time.time() + 120
    while time.time() < end and not grab_at:
        for c in copies:
            c.lines()
            if c.crash or not c.alive():
                stop.append(1)
                raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
        if grabbed():
            grab_at = time.time()
            break
        if bench.actor_by_model(host, "B_HE020") != ce:  # gone or another one: never send to a stale address
            out.append("Cerberus's actor changed in the host's game: stopped")
            break
        hs = host.game.sora()
        if hs and dist(hs, actor_pos(host, ce)) < 1200:
            bench.debug("A", 15, *park, fmt="<Iifff")
        picks = [l for l in host.since(marks["A"]) if "aggro: boss B_HE020" in l and "goes for" in l]
        p, mot = actor_pos(host, ce), bench.motion(host, ce)
        # On the floor = his height the same as last check (the setup screenshot: he drops into the arena from above).
        settled, last_y = last_y is not None and abs(p[1] - last_y) < 2, p[1]
        if not picks or "the friend's copy" not in picks[-1] or not settled or mot in (171, 172, 221, 231):
            time.sleep(0.25)
            continue
        hold["at"] = spot()
        time.sleep(0.6)  # the copy at the spot in the host's game
        if grabbed():  # his own bite got it meanwhile: never restart an act mid-grab
            grab_at = time.time()
            break
        cp, fp = actor_pos(host, ce), f3(host, copy + 0x70)  # where the copy is drawn in the host's game
        reply = kh2cmd.query(27701, f"act {ce:x} atk_bite_press")
        tries += 1
        mots, wait = [], time.time() + 3
        while time.time() < wait and not grabbed():
            m = bench.motion(host, ce)
            if not mots or mots[-1] != m:
                mots.append(m)
            time.sleep(0.02)
        if grabbed():
            grab_at = time.time()
        # A no-grab try tells apart: the act refused or ignored (no 221), replaced (221 then not 222), a miss or Hercules.
        out.append(f"try {tries}: reply '{reply[-1] if reply else 'no answer'}'; the copy {dist(cp, fp):.0f} from him, "
                   f"{cp[1] - fp[1]:.0f} above his floor; his motions {mots}; grabbed {grabbed()}")
    hold["at"] = None
    out.append(f"bite tries {tries}; grab {'at +%.1f s' % (grab_at - track[0][0]) if grab_at and track else 'NONE'}")
    if not grab_at:
        stop.append(1)
        out.append("SETUP FAILURE: no grab (the aggro lines: "
                   f"{[l[13:120] for l in host.since(marks['A']) if 'aggro: boss B_HE020' in l][-3:]})")
        return False

    # The three RCs (F566: commands 38, 39, 41): the friend mashes triangle until its Sora has been out of the grab pose
    # and the host's copy unhooked for 1 s (the pose bit may drop for a frame at an RC's switch).
    mark2 = {c.name: c.mark() for c in copies}
    seen, freed = False, None
    end = time.time() + 25
    while time.time() < end:
        friend.game.pad.run(bench.MASH_TRIANGLE)
        held = captured() or read_u(host, copy + 0xD48, 8) != 0
        seen = seen or held
        if held:
            freed = None
        elif seen and not freed:
            freed = time.time()
        if freed and time.time() - freed >= 1:
            break
    time.sleep(1)
    stop.append(1)
    for t in threads:
        t.join(1)
    fl = friend.since(mark2["B"])
    rcs = {n: len(find(fl, f"we used command {n} of our B_HE020")) for n in (38, 39, 41)}
    out.append(f"RCs the friend used (38, 39, 41): {rcs}; its Sora in a grab pose: {seen}, out of it "
               f"{'at +%.1f s' % (freed - grab_at) if freed else 'NEVER'} after the grab")
    out.extend(["   " + l[13:200] for l in host.since(marks["A"]) if "B_HE020" in l and "grab sync:" in l][:30])
    t0 = grab_at
    # Snaps: at each motion change of the friend's Sora from 1 s before the grab, the head's biggest step between two
    # rows in the next 0.3 s, against the host's copy's (a snap = a step far bigger than the copy's).
    d = lambda p, q: sum((p[i] - q[i]) ** 2 for i in range(3)) ** 0.5
    # From the friend's grab pose on, a step 200+ bigger than the copy's is a snap (the release blink was 726 / 4, the
    # third RC's 1338 / 80; RC 1's 139 / 48 is the copy's own move a little late). Not the start: there the friend's
    # Sora moved on its own through the playback delay (~0.1 s; a fall: ~240), judged by 'stood' below instead.
    # its first drawn frame in the grab pose: the bit, place and head read the same in two rows in a row (one row can be
    # our frame start between the grab's calls, before its Sora's update)
    pose = next((k for k in range(len(track) - 1) if track[k][11] and track[k + 1][11] and track[k][0] > t0 - 1
                 and d(track[k][2], track[k + 1][2]) < 1 and d(track[k][5], track[k + 1][5]) < 1), None)

    def step(lo, hi, col):  # the head's biggest step between drawn frames in rows lo..hi: a value read in two rows in
        # a row (rows ~7 ms, frames ~17 ms); one read once is our frame start before the Sora's update redraws it
        # (the script place rebuilds the draw matrix with the old pose, L332), not a frame the game shows
        frames = []
        for k in range(lo, min(hi, len(track) - 1)):
            if d(track[k][col], track[k + 1][col]) < 1 and (not frames or d(frames[-1], track[k][col]) >= 1):
                frames.append(track[k][col])
        return max((d(frames[j], frames[j - 1]) for j in range(1, len(frames))), default=0.0)
    out.append("the friend's motion changes (s from the grab; head step at most between drawn frames, the friend's / "
               "the host's copy's):")
    snaps = 0
    for i in [i for i in range(1, len(track)) if track[i][4] != track[i - 1][4] and track[i][0] > t0 - 1][:16]:
        win = [k for k in range(i, len(track)) if track[k][0] - track[i][0] <= 0.3]
        f, h = step(max(0, i - 4), win[-1] + 1, 5), step(max(0, i - 4), win[-1] + 1, 8)
        snap = pose is not None and i > pose and f > h + 200
        snaps += snap
        out.append(f"   {track[i][0] - t0:+.3f} {track[i - 1][4]} -> {track[i][4]}: {f:.0f} / {h:.0f}"
                   f"{'  SNAP' if snap else ''}")
    with open(os.path.join(OUT, "cerberus_grab_trace.txt"), "w") as fh:
        for s in track:
            fh.write(f"{s[0] - t0:+.4f} drawn {s[1][0]:.1f} {s[1][1]:.1f} {s[1][2]:.1f} place {s[2][0]:.1f} "
                     f"{s[2][1]:.1f} {s[2][2]:.1f} motion {s[4]} grab {int(s[11])} | head {s[5][0]:.0f} {s[5][1]:.0f} "
                     f"{s[5][2]:.0f} | host head {s[8][0]:.0f} {s[8][1]:.0f} {s[8][2]:.0f} | Cerberus motion {s[12]} "
                     f"copy hooked {int(s[13])}\n")
    # The release: the last row out of the grab pose (+0x18C bit 14), the fix armed there only (grab_sync.cpp).
    pose_left = lambda p, s: p[11] and not s[11]
    out.append(f"the friend's grab pose bit cleared {sum(1 for i in range(1, len(track)) if pose_left(track[i - 1], track[i]))} "
               "time(s) in the trace (more than once: it dropped at an RC's switch; the release is the last)")
    smooth = release_report(out, track, t0, fl, pose_left, last=True)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"cerberus_{c.name}.png"))
    out.append(f"snaps from the friend's grab pose on: {snaps}")
    # The start: before its grab pose, the friend's Sora shouldn't stand on the spot the grab puts it (2 frames in the
    # first runs, 2026-11-14: the held place came before the grab's calls). A frame is ~17 ms.
    if pose:  # from its last drawn frame before (a head read in two rows in a row), not an in-between read
        last = next((k for k in range(pose - 1, 0, -1) if d(track[k][5], track[k - 1][5]) < 1), pose - 1)
        out.append(f"the start: the friend's head steps {d(track[pose][5], track[last][5]):.0f} into the grab pose "
                   "from where it moved on its own in the playback delay")
    stood = 0.0
    if pose is not None:
        on = [k for k in range(pose) if track[k][0] > t0 - 1 and d(track[k][2], track[pose][2]) < 30]
        stood = (track[pose][0] - track[on[0]][0]) * 1000 if on else 0.0
    out.append(f"the friend's Sora on its grab spot before the grab pose: {stood:.0f} ms (limit 16)")
    # Each script place (F568): the DLL's own check after our Sora's update, the held stream's next 5 writes: how far
    # it was drawn from the host's copy (one frame of motion is ~90; 1070 at the throw before the fix, 1568 once with
    # a bare place write). Over 300 fails.
    worst = 0.0
    for i, l in enumerate(fl):
        if "ran the host's script place" in l:
            cs = [float(m.split("correction ")[-1]) for m in fl[i + 1:] if "held trace" in m and "correction" in m][:5]
            worst = max([worst] + cs)
            out.append(f"   script place at {l[:12]}: the next writes corrected {', '.join(f'{c:.0f}' for c in cs) or '?'}")
    return (all(c.alive() and not c.crash for c in copies) and freed is not None and smooth and not snaps
            and stood < 16 and worst <= 300)


def npc_late(copies, a, out):
    """TODO 1.192: the friend arrives late in Timeless River 0D/00 0/0/18 (F540-F542's room). See the usage text."""
    host, friend = copies
    load_both(copies, a.slot, a.room)  # 0D/00 in the save's version (0D/08 0/0/4 plays a scene: no warp from there)
    friend.game.pad.run(["tap", "start", "wait", "1500"])  # its pause menu: follow can't move it (PAUSE_SYNC=0)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x0D, 0x00, 0, 0, 0, 0, 18, 0, fmt="<IiBBBBHHHH")  # programs 0/0/18, as in the crash
    bench.wait("the host's load of 0D/00 0/0/18",
               lambda: find(host.since(marks["A"]), "load barrier: released 0x0D/0x00"), 60, copies)
    time.sleep(6)  # the host's NPCs well past world sync's 30-frame wait
    friend.game.pad.run(["tap", "start", "wait", "800"])
    bench.wait("the friend's late load", lambda: find(friend.since(marks["B"]), "follow: arrived in world 0x0D room 0x00"),
               60, copies)
    time.sleep(25)
    npcs = ("0x80e", "0x80d", "0x48d", "0x48c")  # P_WI030_RTN, P_WI020_RTN, N_WI030_RTN, N_WI020_RTN (F541)
    fl = friend.since(marks["B"])
    made = [l for l in find(fl, "has no match here: made") if any(f"object {o}" in l for o in npcs)]
    skipped = find(fl, "not made, ours comes by serial")
    keys = ("follow:", "isn't in our enemy groups", "has no match here", "not made", "_RTN", "load barrier:")
    out.append("B:")
    for l in fl:
        if any(k in l for k in keys) and "trace:" not in l:
            out.append("   " + l[:220])
    out.append(f"friend: NPCs made {len(made)}, NPC makes skipped {len(skipped)}, crash {friend.crash or '-'}")
    for c in copies:
        if c.alive():
            bench.screenshot(c.hwnd, os.path.join(OUT, f"npc_late_{c.name}.png"))
    return all(c.alive() and not c.crash for c in copies) and not made


TIMER = 0xABB850  # the mission timer: u32 clock, pause bits, start, duration, value, state, u8 mode (F271, L334, L335)
EVENT_PAUSE = 4    # the pause bit an event playing sets (L335): what the friend's own RC 307 scene froze (F574)


def timer_read(c):
    """(clock, pause, start, duration, value, state, mode) of a copy's mission timer."""
    return struct.unpack("<IIiiiiB", peek.read(c.game.h, c.game.base + TIMER, 0x19))


def timer_freeze(copies, a, out):
    """TODO 1.196: the friend's own pause freezes its mission timer; it must not pile up. See the usage text."""
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    marks = {c.name: c.mark() for c in copies}
    bench.debug("A", 12, 0x06, 0x08, 0, 0, 116, 116, 116, 0, fmt="<IiBBBBHHHH")
    running = lambda c: (lambda t: t[5] == 2 and t[6] == 1)(timer_read(c))
    try:
        bench.wait("both in 06/08", lambda: all(c.room() == (0x06, 0x08) and c.game.sora() for c in copies), 90, copies)
        bench.wait("both mission timers counting down", lambda: all(running(c) for c in copies), 60, copies)
        bench.wait("the friend's timer within 15 frames of the host's",
                   lambda: abs(timer_read(host)[4] - timer_read(friend)[4]) <= 15, 10, copies)
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e} (save {a.slot} to 06/08 programs 116 never benched before)")
        return False
    h0, f0 = timer_read(host), timer_read(friend)
    out.append(f"mission line {'yes' if find(host.since(marks['A']), 'HE08_MS107') else 'NO'}; timers: host value "
               f"{h0[4]} of {h0[3]}, friend {f0[4]} of {f0[3]}")
    if h0[3] != f0[3]:
        out.append("SETUP FAILURE: the two timers' durations differ")
        return False
    samples, stop = [], []  # (s, host tuple, friend tuple)

    def sample():
        while not stop:
            try:
                samples.append((time.time(), timer_read(host), timer_read(friend)))
            except OSError:
                pass
            time.sleep(0.05)
    t = threading.Thread(target=sample, daemon=True)
    t.start()
    time.sleep(1)
    fmark = friend.mark()
    pause_at = friend.game.base + TIMER + 4
    events = []  # (s, what)
    if a.rc:
        # The real RC 307: the friend next to its Hercules, mashing triangle until its command runs (at most 40 s).
        herc = bench.actor_by_model(friend, "N_EX760_BTL_HERCULES")
        if not herc:
            stop.append(1)
            out.append("SETUP FAILURE: no N_EX760_BTL_HERCULES in the friend's game")
            return False
        end = time.time() + 40
        used = []
        while time.time() < end and not used:
            hp = actor_pos(friend, herc)
            bench.debug("B", 15, hp[0] + 80.0, hp[1], hp[2], fmt="<Iifff")
            friend.game.pad.run(["tap", "triangle", "wait", "120"] * 4)
            used = [l for l in friend.since(fmark) if "reaction sync: we used command 307" in l]
        events.append((time.time(), "RC 307 used" if used else "no RC 307 in 40 s"))
        if not used:
            stop.append(1)
            out.append("SETUP FAILURE: the friend's RC 307 never ran in 40 s")
            return False
        time.sleep(8)  # its two scenes (about 2-3 s) and the time after
    else:
        # The friend's own pause: bit 4 set in its pause word for 3 s (rewritten if the game cleared it), then cleared.
        events.append((time.time(), "freeze"))
        rewrites = 0
        end = time.time() + 3.0
        while time.time() < end:
            p = read_u(friend, pause_at)
            if not p & EVENT_PAUSE:
                write_mem(friend, pause_at, struct.pack("<I", p | EVENT_PAUSE))
                rewrites += 1
            time.sleep(0.05)
        p = read_u(friend, pause_at)
        if p & EVENT_PAUSE:
            write_mem(friend, pause_at, struct.pack("<I", p & ~EVENT_PAUSE))
        events.append((time.time(), "clear"))
        out.append(f"the friend's pause bit 4 written {rewrites} time(s) in 3 s")
        time.sleep(5)
    stop.append(1)
    t.join(1)
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"timer_freeze_{c.name}.png"))

    # The friend's samples with any pause bit of its own (not the host's mission bit 2) set: its clock must stand.
    t0 = samples[0][0]
    held = [s for s in samples if s[2][1] & ~2]
    out.append(f"{len(samples)} samples; the friend's own pause in {len(held)} (bits "
               f"{sorted({hex(s[2][1]) for s in held})})")
    for s in samples[::10]:
        out.append(f"   {s[0] - t0:6.2f} s  host clock {s[1][0]} value {s[1][4]} state {s[1][5]}  |  friend clock "
                   f"{s[2][0]} pause {s[2][1]:#x} start {s[2][2]} value {s[2][4]} state {s[2][5]}")
    if len(held) < 3:
        out.append("SETUP FAILURE: the friend's timer was never held by its own pause (the freeze didn't take)")
        return False
    inner = held[1:]  # the first may be read before the bit took
    clocks = {s[2][0] for s in inner}
    starts = [s[2][2] for s in inner]
    out.append(f"while held: the friend's clock {'stood still' if len(clocks) == 1 else f'MOVED ({len(clocks)} values)'}"
               f", its value {'stood still' if len({s[2][4] for s in inner}) == 1 else 'moved'}, its start moved by "
               f"{max(starts) - min(starts)} frames ({len(held) * 0.05:.1f} s held)")
    if len(clocks) != 1 and not a.rc:
        out.append("SETUP FAILURE: the friend's clock ran through the written pause (L334 doesn't hold?)")
        return False
    last_held = held[-1][0]
    after = [s for s in samples if s[0] > last_held]
    gap = [abs(s[1][4] - s[2][4]) for s in after]
    back = next((s[0] - last_held for s in after if abs(s[1][4] - s[2][4]) <= 15), None)
    out.append(f"after: the friend's timer within 15 frames of the host's "
               f"{f'{back:.2f} s after the pause' if back is not None else 'NEVER'}; gaps {gap[:6]} ... {gap[-3:]}; "
               f"the friend's state at the end {after[-1][2][5] if after else '?'}")
    fl = friend.since(fmark)
    failed = find(fl, "our mission script failed the mission")
    differ = find(fl, "the timers differ")
    moved = find(fl, "our timer was")
    paused = find(fl, "paused by our own game")
    for name, lines in (("failure", failed), ("timers differ", differ), ("moves", moved), ("held by our pause", paused)):
        out.append(f"friend '{name}' lines: {len(lines)}" + (f": {lines[0][13:150]}" if lines else ""))
    stats = find(fl, "timer moved")
    if stats:
        out.append(f"friend stat: {stats[-1][13:200]}")
    for c in copies:
        c.lines()  # c.crash is only filled by reading the log (the host's isn't read after the setup)
    crash = [c.name for c in copies if c.crash or not c.alive()]
    ok = (max(starts) - min(starts) == 0 and back is not None and back <= 1.0 and after and after[-1][2][5] == 2
          and gap and gap[-1] <= 15 and not failed and not crash)
    if crash:
        out.append(f"crash or closed: {crash}")
    return ok


BOLT_ROOMS = {"gym": (0x02, 0x28), "void": (0x02, 0x00), "arena": (0x06, 0x0D)}  # debug_cmd.py GYMS, programs 0/0/0
FLAG8 = 0x100  # +0x120 bit 8: can't be hit (L293); the Bolt Tower's catch sets it on the Sora it holds (F577)


def bolt(copies, a, out):
    """TODO 1.197: a Bolt Tower's catch (F577-F582) in a gym room, started with the dev command. See the usage text."""
    import kh2cmd
    import netbench
    import threading
    host, friend = copies
    load_both(copies, a.slot, a.room)
    room = BOLT_ROOMS[a.where]
    bench.debug("A", 12, *room, 0, 0, 0, 0, 0, 0, fmt="<IiBBBBHHHH")
    tw, threads0 = {}, {}  # threads0: each game's tower's script thread count (+0x5B8) after its spawn (L340)

    def spawn():  # one tower 300 in front of the host's Sora (spawn_sync.cpp DebugSpawn); the friend's game copies it
        old = {c.name: bench.actor_by_model(c, "M_EX800") for c in copies}
        bench.debug("A", 19, a.obj | 1 << 16)
        bench.wait("a new Bolt Tower in both games", lambda: all(
            (lambda t: t and t != old[c.name])(bench.actor_by_model(c, "M_EX800")) for c in copies), 30, copies)
        time.sleep(2)
        tw.update({c.name: bench.actor_by_model(c, "M_EX800") for c in copies})
        threads0.update({c.name: read_u(c, tw[c.name] + 0x5B8) for c in copies})
    try:
        bench.wait(f"both in {room[0]:02X}/{room[1]:02X}", lambda: all(c.room() == room and c.game.sora() for c in copies),
                   90, copies)
        time.sleep(8)
        spawn()
    except bench.Failed as e:
        out.append(f"SETUP FAILURE: {e}")
        return False
    hsora, fsora = head(host), head(friend)
    hcopy, fcopy = netbench.find_copy(host.game), netbench.find_copy(friend.game)
    p = actor_pos(host, tw["A"])
    out.append(f"tower {a.obj:#x}: host {tw['A']:#x} at ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.0f}), friend {tw['B']:#x}; "
               f"copies: host's {hcopy:#x}, friend's {fcopy:#x}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"bolt_setup_{c.name}.png"))
    if a.setup_only:
        return all(c.alive() and not c.crash for c in copies)

    # No teleports (they dropped a Sora off the gym's floor, your run 2026-11-14): both stay where the warp put them
    # and the tower catches whichever Sora its aggro picks. In the host's game the friend's Sora is the host's copy.
    hooked = lambda: ("friend" if read_u(host, hcopy + 0xD48, 8) else "host" if read_u(host, hsora + 0xD48, 8)
                      else None)
    actors = {"host Sora": (host, hsora), "host's copy": (host, hcopy), "friend Sora": (friend, fsora),
              "friend's copy": (friend, fcopy)}
    stop, track, fx = [], [], []

    def sample():  # a row on change: per actor (motion, flag 8, hooked, grab pose bit 14, RC window bit 21, +0x638)
        last = None
        while not stop:
            try:
                row = []
                for name, (c, act) in actors.items():
                    f120, f18c = read_u(c, act + 0x120), read_u(c, act + 0x18C)
                    row.append((bench.motion(c, act), bool(f120 & FLAG8), read_u(c, act + 0xD48, 8) != 0,
                                bool(f18c >> 14 & 1), bool(f18c >> 21 & 1), read_u(c, act + 0x638) & 0xFFFF))
                row.append((bench.motion(host, tw["A"]), bench.motion(friend, tw["B"]),
                            bool(read_u(host, tw["A"] + 0x120) & FLAG8), bool(read_u(friend, tw["B"] + 0x120) & FLAG8),
                            read_u(friend, tw["B"] + 0x5B8)))  # the friend's tower's thread count (L340)
                if row != last:
                    track.append((time.time(), row))
                    last = row
            except OSError:
                pass
            time.sleep(0.005)

    def fx_sample():  # every 0.5 s: the tower's live effects per game, {effect id: count} (F580: 4, 5, 7 = the beam)
        while not stop:
            row = []
            for c in copies:
                ids = {}
                try:
                    for key, val in (bench.fx_census(c) or {}).items():
                        if key[0].startswith("M_EX800"):
                            ids[key[3]] = ids.get(key[3], 0) + val[0]
                except (OSError, struct.error):
                    pass
                row.append(ids)
            fx.append((time.time(), row))
            time.sleep(0.5)
    threads = [threading.Thread(target=f, daemon=True) for f in (sample, fx_sample)]
    for t in threads:
        t.start()
    hvic = hcopy if a.want == "friend" else hsora  # the wanted Sora in the host's game (--solo)
    fvic = fsora if a.want == "friend" else fcopy  # and in the friend's game

    def star(on):  # --solo: flag 8 on that Sora in the host's game only, so the host's catch bails (obj_is_star)
        v = read_u(host, hvic + 0x120)
        write_mem(host, hvic + 0x120, struct.pack("<I", v | FLAG8 if on else v & ~FLAG8))

    def local(m):  # --solo: the friend's tower copy ran its own catch (refused hook, or flag 8 set on its target)
        friend.lines()
        return bool(read_u(friend, fvic + 0x120) & FLAG8) or any("own hook" in l for l in friend.since(m))
    if a.solo and a.want == "any":
        out.append("SETUP FAILURE: --solo needs --want friend or host")
        return False
    catches, misses, hooks, loop_marks = [], 0, [], []  # hooks: every catch's time, the waited-out ones too
    while len(catches) < a.catches:
        n = len(catches) + 1
        mark = {c.name: c.mark() for c in copies}
        loop_marks.append(mark)  # a catch's log lines run to the next attempt's mark
        tries, quiet, who, solo_at = 0, 0, None, None
        end = time.time() + 120
        while time.time() < end and not who:
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    stop.append(1)
                    raise bench.Failed(f"copy {c.name} {'crashed' if c.crash else 'closed'}: {c.crash or ''}")
            who = hooked()
            if who:
                break
            if bench.motion(host, tw["A"]) in (201, 202, 204, 211):  # busy with an attack: wait
                time.sleep(0.25)
                continue
            if a.solo:
                star(True)
            reply = kh2cmd.query(27701, f"act {tw['A']:x} atk_laser")
            tries += 1
            mots, wait, seen_local = [], time.time() + 4, False
            while time.time() < wait and not hooked():
                m = bench.motion(host, tw["A"])
                if not mots or mots[-1] != m:
                    mots.append(m)
                if a.solo:
                    star(True)  # kept on through the catch motion
                    if not seen_local and local(mark["B"]):
                        seen_local = time.time()
                time.sleep(0.02)
            who = hooked()
            if a.solo:
                star(False)
                if not who and seen_local:
                    who, solo_at = a.want, seen_local
            out.append(f"catch {n} try {tries}: reply '{reply[-1] if reply else 'no answer'}'; the tower's motions "
                       f"{mots}; caught {who or 'nobody'}")
            quiet = 0 if 201 in mots else quiet + 1
            if quiet >= 3:  # 'started' but no laser: likely its antenna broke (L338): a fresh tower
                out.append("   no laser in 3 tries: a new tower")
                spawn()
                quiet = 0
        if not who:
            stop.append(1)
            out.append(f"SETUP FAILURE: no catch in catch {n}")
            return False
        if a.solo and not solo_at and who == a.want:  # the host's tower caught the flagged Sora: the setup is broken
            stop.append(1)
            out.append(f"SETUP FAILURE: the host's tower caught the {who}'s Sora despite flag 8")
            return False
        hooks.append(solo_at or time.time())
        if a.want != "any" and who != a.want:  # the other Sora (--solo: the host's own catch of it): waited out
            misses += 1
            out.append(f"   the {who}'s Sora caught, not the {a.want}'s: waited out ({misses} so far)")
            if misses > 10:
                stop.append(1)
                out.append(f"SETUP FAILURE: the tower kept catching the {who}'s Sora")
                return False
            time.sleep(8)
            continue
        t_hook = solo_at or time.time()
        time.sleep(1.0)
        for c in copies:  # the beam mid-catch: who draws it in the friend's game (the trap log's 'by' says whose)
            bench.screenshot(c.hwnd, os.path.join(OUT, f"bolt_{n}_mid_{c.name}.png"))
        if a.press == "mash" and not a.solo:  # the caught player mashes until out of the hold for 1 s (at most 8 s)
            pad = (friend if who == "friend" else host).game.pad
            freed, stop_at = None, time.time() + 8
            while time.time() < stop_at:
                pad.run(bench.MASH_TRIANGLE)
                if hooked():
                    freed = None
                elif not freed:
                    freed = time.time()
                if freed and time.time() - freed >= 1:
                    break
        else:
            time.sleep(6)  # the 3.0 s hold, then ten strikes over 1.5 s (F577)
        catches.append((t_hook, mark, who, len(loop_marks) - 1))
        time.sleep(2)
        for c in copies:  # after the catch: the beam should be gone (F580)
            bench.screenshot(c.hwnd, os.path.join(OUT, f"bolt_{n}_after_{c.name}.png"))
    # 20 s with no act: two full 10 s trap-count blocks; a beam thread left running shows there and in the effects.
    end_mark = {c.name: c.mark() for c in copies}
    t_quiet = time.time()
    time.sleep(22)
    t_end = time.time()
    stop.append(1)
    for t in threads:
        t.join(2)
    for c in copies:
        c.lines()

    keys = list(actors)
    beam = (4, 5, 7)
    fails = []
    for n, (t_hook, mark, who, li) in enumerate(catches, 1):
        nxt = loop_marks[li + 1] if li + 1 < len(loop_marks) else end_mark  # up to the next attempt (or the quiet wait)
        hl, fl = host.all[mark["A"]:nxt["A"]], friend.all[mark["B"]:nxt["B"]]
        t_next = min([h for h in hooks if h > t_hook + 0.5] + [t_end])  # the next hook of either Sora, or the end
        out.append(f"--- catch {n}: the {who}'s Sora caught, press {a.press} ---")
        rows = [r for r in track if t_hook - 1 <= r[0] <= t_hook + 8]
        vic_host = "host's copy" if who == "friend" else "host Sora"
        vic_friend = "friend Sora" if who == "friend" else "friend's copy"

        def timeline(name, field):
            i, seq = keys.index(name), []
            for t, r in rows:
                v = r[i][field]
                if not seq or seq[-1][1] != v:
                    seq.append((round(t - t_hook, 2), v))
            return seq
        for name in (vic_host, vic_friend):
            out.append(f"   {name}: motion {timeline(name, 0)[:12]}")
            out.append(f"   {name}: flag 8 {timeline(name, 1)}; hooked {timeline(name, 2)}; RC window (bit 21) "
                       f"{timeline(name, 4)}; +0x638 {timeline(name, 5)[:6]}")
        out.append(f"   towers (host, friend) motion and flag 8: {[(round(t - t_hook, 2), r[4][:4]) for t, r in rows][:10]}")
        counts = []
        for t, r in rows:
            if not counts or counts[-1][1] != r[4][4]:
                counts.append((round(t - t_hook, 2), r[4][4]))
        out.append(f"   the friend's tower's thread count (+0x5B8) around the catch: {counts[:12]}")
        ih = keys.index(vic_host)  # the host's let go: its victim unhooked (--solo: no host catch, the hook + 3.0 s)
        lets = [t for t, r in rows if t > t_hook and not r[ih][2]]
        t_let = lets[0] if lets and not a.solo else t_hook + 3.0
        out.append(f"   the host's let go at +{t_let - t_hook:.2f} s" + (" (--solo: no host catch, assumed)" if a.solo else ""))
        before = [r for r in track if r[0] <= t_let + 1]  # each friend-side actor's flag 8 from 1 s after the let go
        state = ([before[-1]] if before else []) + [r for r in track if t_let + 1 < r[0] < t_next]
        for name in ("friend Sora", "friend's copy"):
            if any(r[keys.index(name)][1] for _, r in state):
                fails.append(f"catch {n}: {name} still has flag 8 (can't be hit) 1 s after the host's let go (F581)")
        if a.press == "mash" and not a.solo and a.obj != 0x8F0 and 254 not in [m for _, m in timeline(vic_host, 0)]:
            # (the Disney Castle tower can't be mashed out of in either game, F589)
            fails.append(f"catch {n}: the {who}'s mash never got it out (no capture 254 in the host's game, F583)")
        for gi, gname in ((0, "host"), (1, "friend")):  # the beam's effects, first and last seen live after the hook
            seen = [round(t - t_hook, 1) for t, r in fx  # up to 1.5 s before the next catch (its beam starts first)
                    if t_hook - 1 <= t < t_next - 1.5 and any(r[gi].get(i) for i in beam)]
            if seen and seen[-1] > t_let - t_hook + 2.5:
                fails.append(f"catch {n}: the {gname}'s beam still live {seen[-1] - (t_let - t_hook):.1f} s after the "
                             f"host's let go (F580)")
            out.append(f"   {gname}'s game, the tower's beam effects 4/5/7 live: " +
                       (f"from +{seen[0]} to +{seen[-1]} s" if seen else "never seen") +
                       (" (still live at the next catch or the quiet wait)" if seen and seen[-1] >= round(t_next - t_hook - 2.5, 1)
                        else ""))
        hg = [l[13:170] for l in hl if "grab sync:" in l and "M_EX800" in l][:8]
        out.append(f"   host grab sync: {hg}")
        tower_traps = lambda lines, pat: [l for l in lines if " trap " in l and "by M_EX800" in l and re.search(pat, l)]
        for gname, lines in (("host", hl), ("friend", fl)):
            out.append(f"   {gname} traps by the tower: hook {len(tower_traps(lines, r'1/112 '))}, set_flag 8 on a Sora "
                       f"{len(tower_traps(lines, r'1/70 .*P_EX100.*, 8\)'))}, reset_flag 8 "
                       f"{len(tower_traps(lines, r'1/71 .*, 8\)'))}, idle {len(tower_traps(lines, r'1/74 '))}, capture "
                       f"{len(tower_traps(lines, r'1/29 '))}, magic threads {len(tower_traps(lines, r'2/20 '))}, motion_push "
                       f"{len(tower_traps(lines, r'1/13 '))}, attack 922 {len(tower_traps(lines, r'attack_new.*922'))}, "
                       f"effect_loop_end {len(tower_traps(lines, r'0/44 '))}")
        refused = [l[13:300] for l in fl if "own hook" in l or "own capture" in l]
        out.append(f"   friend: own catch refused {len(refused)}" + (f": {refused[0]}" if refused else ""))
        ftr = {k: len(tower_traps(fl, pat)) for k, pat in (("set_flag 8 on a Sora", r'1/70 .*P_EX100.*, 8\)'),
                                                        ("idle", r'1/74 '), ("magic threads", r'2/20 '))}
        if any(ftr.values()):  # TODO 1.197: the refused grab's script run ends there (KH2COOP_SCRIPT_GRAB_END)
            fails.append(f"catch {n}: the friend's tower copy ran its own catch on: {ftr} (F579)")
        if who == "friend":  # the host's strikes on its copy land on the friend's Sora (F581: refused as 'protected')
            prot, hit = find(fl, "is protected"), find(fl, "the same strike on our Sora")
            if prot:
                fails.append(f"catch {n}: the friend's Sora refused {len(prot)} host hits as protected (F581)")
            if a.press == "none" and a.obj != 0x8F0 and not a.solo and not hit:
                fails.append(f"catch {n}: no host strike reached the friend's Sora after the timeout (F577)")
            out.append(f"   friend: host strikes on our Sora {len(hit)}, refused as protected {len(prot)}")
        ended, kept = find(fl, "script run ended"), find(fl, "script run goes on")
        out.append(f"   friend: script runs ended at the refusal {len(ended)}, kept {len(kept)}" +
                   (f" ({kept[0][13:220]})" if kept else ""))
        for pat in ("ran the host's grab motion", "ran the host's let go", "set idle", "is protected",
                    "we used command 380", "we used command 663", "our repeat of command", "a chain RC of the enemy holding",
                    "ran the host's call 1/71"):
            got = find(fl, pat)
            out.append(f"   friend '{pat}': {len(got)}" + (f" ({got[0][13:150]})" if got and pat != "is protected" else ""))
        for pat in ("used command 380", "used command 663", "not run:"):
            got = find(hl, pat)
            out.append(f"   host '{pat}': {len(got)}" + (f" ({got[0][13:160]})" if got else ""))
    quiet_hooks = sorted({"friend" if r[1][1][2] else "host" if r[1][0][2] else "-" for r in track if r[0] >= t_quiet})
    out.append(f"quiet 22 s (no act): a Sora hooked then {quiet_hooks}")
    for gi, c in enumerate(copies):
        last = [r[gi] for t, r in fx if t >= t_quiet + 15]
        live = {i: max((d.get(i, 0) for d in last), default=0) for i in beam}
        out.append(f"{c.name}: the tower's beam effects at the end (last 7 s): {live}" +
                   ("  <- BEAM LEFT" if any(live.values()) else ""))
        if any(live.values()):
            fails.append(f"{c.name}: the beam left at the end (F580)")
        blocks = [l[13:330] for l in c.since(end_mark[c.name]) if "trap counts" in l or
                  (l[12:15] == "   " and "effect_set_pos" in l)]
        out.append(f"   {c.name} trap counts in the quiet wait: {blocks[-2:] or 'none'}")
    for c in copies:  # an exit at a refused grab keeps the owner's count balanced (L340): same as at the setup
        now = read_u(c, tw[c.name] + 0x5B8)
        out.append(f"{c.name}: the tower's script thread count (+0x5B8) {threads0[c.name]} at the setup, {now} at the end")
        if c.name == "B" and now < threads0["B"]:  # an act thread can still be alive (a rise); a drop = an extra -1
            fails.append(f"B: the tower's thread count dropped {threads0['B']} -> {now} (L340)")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"bolt_end_{c.name}.png"))
    with open(os.path.join(OUT, "bolt_trace.txt"), "w") as fh:
        t0 = catches[0][0] if catches else (track[0][0] if track else 0)
        for t, r in track:
            fh.write(f"{t - t0:+.3f} " + " | ".join(f"{k} {v}" for k, v in zip(keys + ["towers"], r)) + "\n")
        for t, r in fx:
            fh.write(f"{t - t0:+.3f} fx host {r[0]} friend {r[1]}\n")
    for f in fails:
        out.append(f"FAIL: {f}")
    return all(c.alive() and not c.crash for c in copies) and len(catches) == a.catches and not fails


SCENARIOS = {"chest_pose": chest_pose, "chest_menu": chest_menu, "vexen_freeze": vexen_freeze, "pause_race": pause_race, "barrels": barrels, "barbossa": barbossa, "pete_curtain": pete_curtain, "cup_retry": cup_retry, "blend": blend, "blend_actors": blend_actors, "blend_video": blend_video, "orbs": orbs, "bolt": bolt, "timer_freeze": timer_freeze,"cerberus": cerberus,"hydra": hydra,"scaffold": scaffold, "npc_late": npc_late, "same_room": same_room, "twin_blade": twin_blade, "copy_sheet": copy_sheet, "blade_restart": blade_restart, "ship": ship, "alone": alone, "downed": downed, "forms": forms, "limit": limit, "gym_limit": gym_limit, "summon": summon, "summon_probe": summon_probe, "summon_sync": summon_sync, "party": party, "companions": companions, "companion_ko": companion_ko, "mp_probe": mp_probe, "xemnas": xemnas, "xend": xend, "xlimit": xlimit, "xcontinue": xcontinue, "menu": menu, "hud": hud, "hud2": hud2, "item": item, "costume": costume, "inspect": inspect, "heal": heal, "mickey": mickey, "mickey_coop": mickey_coop, "getup": getup, "mickey_real": mickey_real, "mickey_hit": mickey_hit, "swarm": swarm, "valor_sheet": valor_sheet, "formlook": formlook, "form_room": form_room, "keyblade": keyblade, "blade": blade, "blade_swap": blade_swap, "equip_menu": equip_menu, "saferead": saferead, "ended_loop": ended_loop, "form_share": form_share, "larxene_mem": larxene_mem, "file_groups": file_groups, "neg_group": neg_group, "limit_gap": limit_gap, "blade_combat": blade_combat, "blade_drive_storm": blade_drive_storm, "blade_menu_move": blade_menu_move, "fx_sync0": fx_sync0, "fx_sync1": fx_sync1, "limit_back": limit_back, "limit_watch": limit_watch, "party_swap": party_swap, "hud_hits": hud_hits, "party_menu": party_menu, "form_cast": form_cast, "limit_out": limit_out, "apart": apart, "capacity": capacity, "room_smoke": room_smoke, "oogie_break": oogie_break, "bag_leave": bag_leave, "rodeo": rodeo, "rodeo_self": rodeo_self, "rc_state": rc_state, "rapid_blow": rapid_blow, "luxord_cards": luxord_cards, "luxord_mirror": luxord_mirror, "twin_lk": twin_lk, "belt_capture": belt_capture, "belt_zones": belt_zones, "zones": zones, "talk_events": talk_events, "rc_dusk": rc_dusk, "rc_lords": rc_lords, "gain": gain, "copy_row": copy_row, "skip": skip}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--slot", type=int, default=52)  # HB Borough 04/06, Donald + Goofy
    ap.add_argument("--friend-slot", type=int, default=0)  # chest_pose: another save in the friend's copy
    ap.add_argument("--together", action="store_true")  # chest_pose: both press Triangle at their own chest at once
    ap.add_argument("--gap", type=float, default=0.0)  # chest_pose --together: the friend presses this long after
    ap.add_argument("--third", type=float)  # chest_pose --together: the friend's game takes 359 this long after
    ap.add_argument("--on-ask", action="store_true")  # with --third: the host opens 359 at the first load after it
    ap.add_argument("--kind", action="store_true")  # chest_pose --together: after the control, two openings in the host's game
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--drive", action="store_true")  # blade_swap: the friend's Valor + Revert first
    ap.add_argument("--forms-only", action="store_true")  # blade: only step 4 (the form Keyblades)
    ap.add_argument("--limits", action="store_true")  # fx_sync1: only the two Limits (no forms), next to the Dusks
    ap.add_argument("--combo", action="store_true")  # fx_sync1: each form's Revert right after two hits
    ap.add_argument("--specials", action="store_true")  # fx_sync1: in Limit Form its four Limit commands (TODO 1.183)
    ap.add_argument("--both", action="store_true")  # fx_sync1 --specials: both in Limit Form, the same Limit at once
    ap.add_argument("--forms", default="")  # fx_sync1: only these form bytes, e.g. "2" = Wisdom (default all five)
    ap.add_argument("--pause", action="store_true")  # fx_sync1: the other game pauses 4 s mid-attacks
    ap.add_argument("--limit", type=lambda x: int(x, 0), default=0x158)  # limit_back: the first's Limit (Duck Flare)
    ap.add_argument("--second", type=lambda x: int(x, 0), default=0x158)  # limit_back: the other's (Donald's, as the first's)
    ap.add_argument("--hold", action="store_true")  # limit_back: the first player in Valor, only the other's Limit
    ap.add_argument("--moves", action="store_true")  # limit_back: moves_check on the first Limit (TODO 1.189; LIMIT_FX_MOVES=1)
    ap.add_argument("--keys", default="")  # equip_menu: comma-separated pad taps after start; room_smoke, talk_events: vpad steps
    ap.add_argument("--host-only", action="store_true")  # xend: only the host attacks (the friend's hits can't kill a busy boss)
    ap.add_argument("--build", action="store_true")  # summon_probe: summon_sync.cpp's build instead of the dev spawn
    ap.add_argument("--leave", action="store_true")  # summon_sync: the summoner (the host) changes room mid-summon
    ap.add_argument("--by", choices=("A", "B"), default="A")  # limit/summon/party: who opens the menu
    ap.add_argument("--other-tries", action="store_true")  # limit: the other player tries a Limit meanwhile
    ap.add_argument("--what", choices=("form", "summon"), default="form")
    ap.add_argument("--overlap", action="store_true")  # companions: the other player uses a form meanwhile  # companions: a Drive Form or a summon
    ap.add_argument("--pick", type=int, default=0)  # forms/summon/limit: which one in the list (0 = the first)
    ap.add_argument("--f385", action="store_true")  # form_room: the real freeze's setup (04/12 battle -> 04/13 door 1)
    ap.add_argument("--same-room", action="store_true")  # form_room: warp to the same room, then Revert (1.181)
    ap.add_argument("--tries", type=int, default=10)  # form_room --f385: jump + air combo rounds
    ap.add_argument("--square", action="store_true")  # form_room --f385: Square pressed through each landing
    ap.add_argument("--drive-gap", type=float, default=6)  # form_room --f385: seconds from the Drive to the room change
    ap.add_argument("--ship", action="store_true")  # xemnas/xlimit: through the ship-fight room first
    ap.add_argument("--hp", action="store_true")  # xemnas: keep the party HP full by writing it (for GOD=0)
    ap.add_argument("--motions", action="store_true")  # xemnas: log every motion change of each Xemnas
    ap.add_argument("--shots", action="store_true")  # xemnas: both games every 3 s after the hit; limit: both games
    ap.add_argument("--boss-hp", type=int, default=200)  # xend: every boss's HP at the start
    ap.add_argument("--program", type=int, default=74)  # xemnas/xend/...: 74 story Final Xemnas, 98 Data Xemnas (part 2)
    ap.add_argument("--stand", action="store_true")  # apart, capacity: no attacks
    ap.add_argument("--host-at", default="")  # capacity: X,Y,Z the host's Sora is put at
    ap.add_argument("--friend-at", action="append", default=[])  # capacity: X,Y,Z for the friend's; repeatable = in turn
    ap.add_argument("--seconds", type=int, default=420)  # xend: give up after this long
    ap.add_argument("--done", action="store_true")  # apart: fight until the 1000 Heartless are done
    ap.add_argument("--spawn", type=lambda x: int(x, 16), default=0)  # room_smoke: an object to make (hex id)
    ap.add_argument("--tri", choices=("both", "host", "friend"), default="both")  # room_smoke: who presses triangle
    ap.add_argument("--at", default="")  # talk_events: the Examine spots, 'X,Y,Z;X,Y,Z'
    ap.add_argument("--tri-only", action="store_true")  # room_smoke: the --tri players press triangle only, nobody attacks
    ap.add_argument("--later", default="")  # room_smoke: KEY,SECONDS: the --near player only taps KEY from then on
    ap.add_argument("--watch", action="store_true")  # room_smoke: the other player stands by the --near one
    ap.add_argument("--near-once", action="store_true")  # room_smoke: --near places its player once only
    ap.add_argument("--near-host", action="store_true")  # room_smoke: --near moves the host, not the friend
    ap.add_argument("--air", action="store_true")  # belt_capture: put the friend at the belt's height
    ap.add_argument("--breaks", type=int, default=1)  # oogie_break: how many breaks (each after the last one's rise)
    ap.add_argument("--near", default="")  # room_smoke: the friend attacks its nearest actor of this model
    ap.add_argument("--warp", default="")  # apart: W,R,P1,P2,P3 in hex instead of the 1000 Heartless; ended_loop: W,R,P
    ap.add_argument("--triangle", action="store_true")  # apart: tap triangle too (reaction commands, TODO 1.144)
    ap.add_argument("--race", action="store_true")  # rodeo: Rodeo pressed late (TODO 1.255); luxord_cards: both press at once
    ap.add_argument("--fx-census", action="store_true")  # xemnas: live effects and their particles every 1 s
    ap.add_argument("--winner", choices=("A", "B"), default="A")  # mickey_coop: who wins the roll (test setting)
    ap.add_argument("--end", choices=("rescue", "over", "dies", "nobody"), default="rescue")  # mickey_coop: how it ends
    ap.add_argument("--dump", action="store_true")  # mickey_coop: both Soras' actor / movement bytes before and after
    ap.add_argument("--trace", action="store_true")  # mickey: fades and Game Over flags every 50 ms (mickey_trace_<copy>.txt)
    ap.add_argument("--friend-back", action="store_true")  # mickey: the friend's game takes the Mickey branch, no Mickey
    ap.add_argument("--one", action="store_true")  # formlook: only one player Drives (--by), files on demand
    ap.add_argument("--auto", action="store_true")
    ap.add_argument("--decline", action="store_true")  # neg_group: a budget too small (the decline path)
    ap.add_argument("--scene", action="store_true")  # neg_group: through the Pete walk-in scene (save 11, 02/19)
    ap.add_argument("--shared", action="store_true")  # neg_group: the no-join rule, the shared form in both orders
    ap.add_argument("--mixed", action="store_true")  # formlook: the friend Drives into the next entry  # formlook: no test commands, the copy follows the pose
    ap.add_argument("--low", type=int, default=5)  # heal: the HP a Sora is held at (a quarter of max or less)
    ap.add_argument("--then", choices=("resume", "skip", "close"), default="resume")  # same_room: the paused player
    ap.add_argument("--peer-blade", type=int, default=0)  # twin_blade: the friend first equips the n-th Keyblade (5.20a)
    ap.add_argument("--ride", choices=("friend", "host", "both"), default="friend")  # scaffold: who stands on it
    ap.add_argument("--mash", choices=("rider", "both", "none"), default="rider")  # scaffold: who presses Air Slash
    ap.add_argument("--burst", action="store_true")  # hydra (with --trace-grab): the friend's window as fast as it captures
    ap.add_argument("--trace-grab", action="store_true")  # hydra: the friend's Sora's place through the grab (TODO 1.195)
    ap.add_argument("--setup-only", action="store_true")  # cerberus: only the warp, Cerberus found, screenshots
    ap.add_argument("--attack-mash", action="store_true")  # copy_row: only the picks with Attack mashed after them
    ap.add_argument("--mash-delay", type=float, default=0.0)  # scaffold: seconds into the shake before the presses
    ap.add_argument("--rc", action="store_true")  # timer_freeze: the friend's real RC 307 instead of the written pause
    ap.add_argument("--press", choices=("none", "mash"), default="none")  # bolt: the caught player mashes Bolt Reversal
    ap.add_argument("--want", choices=("friend", "host", "any"), default="friend")  # bolt: only catches of this Sora count
    ap.add_argument("--solo", action="store_true")  # bolt: flag 8 on that Sora in the host's game: the friend's catch alone (F579)
    ap.add_argument("--where", choices=sorted(BOLT_ROOMS), default="gym")  # bolt: the room (debug_cmd.py GYMS)
    ap.add_argument("--obj", type=lambda x: int(x, 16), default=0x8BE)  # bolt: M_EX800_MU (627 M_EX800, 8F0 _DC)
    ap.add_argument("--catches", type=int, default=2)  # bolt: catches in a row (the host never caught after, 1.197)
    a = ap.parse_args()
    saves = {}
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        for line in fh:
            s, w, r = line.split()
            saves[int(s)] = (int(w, 16), int(r, 16))
    if a.scenario in ("formlook", "form_room", "form_share", "file_groups", "neg_group", "limit_gap", "blade_combat",
                      "blade_drive_storm", "blade_menu_move", "larxene_mem", "fx_sync1", "limit_back",
                      "limit_watch", "hud_hits", "form_cast") and a.slot == 52:
        a.slot = 59  # all Drive Forms but Anti
    if a.scenario in ("copy_sheet", "blade_restart") and a.slot == 52:
        a.slot = 59  # all Keyblades
    if (a.scenario == "neg_group" and a.scene) or (a.scenario in ("skip", "twin_blade", "same_room", "pause_race") and a.slot == 52):
        a.slot = 11  # then the warp to the Mysterious Tower's walk-in (the Pete scene)
    if a.scenario == "hydra" and a.slot == 52:
        a.slot = 16  # Olympus (first visit), then the warp to the Hydra (06/12 programs 171)
    if a.scenario == "cerberus" and a.slot == 52:
        a.slot = 14  # Olympus 06/0A (first visit), then the warp to Cerberus (06/07 programs 114)
    if a.scenario == "timer_freeze" and a.slot == 52:
        a.slot = 15  # Olympus 06/0C, then the warp to Pete with Hercules (06/08 programs 116, HE08_MS107)
    if a.scenario in ("npc_late", "scaffold", "room_smoke", "pete_curtain") and a.slot == 52:
        a.slot = 17  # Timeless River 0D/00
    if a.scenario in ("gym_limit", "cup_retry", "barbossa", "barrels", "vexen_freeze") and a.slot == 52:
        a.slot = 59  # high level, Donald + Goofy (Trinity)
    if a.scenario in ("swarm", "valor_sheet") and a.slot == 52:
        a.slot = 10  # Land of Dragons
    if a.scenario in ("fx_sync0", "apart", "capacity", "blend_actors") and a.slot == 52:
        a.slot = 97  # Hollow Bastion's Borough fight, Donald + Goofy
    if a.scenario in ("limit", "heal", "mickey_real", "mickey_hit", "rc_lords", "party_swap", "party_menu",
                      "talk_events") and a.slot == 52:
        a.slot = 20  # a fight (see limit)
    if a.scenario == "ended_loop" and a.slot == 52:
        a.slot, a.warp = 20, a.warp or "7,3,3B"  # the Twin Lords: their scripts end effect loops
    if a.scenario in ("rodeo", "rodeo_self", "rc_state", "rapid_blow", "twin_lk") and a.slot == 52:
        a.slot = 23  # Pride Lands 0A/01 (lion Sora: Rodeo needs it)
    if a.scenario == "chest_pose" and a.slot == 52:
        a.slot = 8  # Twilight Town, chests 304-306 closed in 02/09
    a.room = saves[a.slot]
    a.friend_room = saves[a.friend_slot] if a.friend_slot else a.room
    os.makedirs(OUT, exist_ok=True)
    env = (["KH2COOP_GOD=0"] if a.scenario in ("downed", "hud", "hud2", "item", "costume", "xcontinue", "cup_retry", "mickey", "mickey_coop",
                                               "getup", "mickey_real", "mickey_hit") else []) + (
        ["KH2COOP_HEAL_TRACE=1", "KH2COOP_ENEMY_GOD=1"] if a.scenario == "heal" else []) + (
        ["KH2COOP_HEAL_TRACE=1", "KH2COOP_GOD=0"] if a.scenario == "companion_ko" else []) + (
        ["KH2COOP_ENEMY_GOD=1"] if a.scenario in ("mickey_real", "mickey_hit", "larxene_mem", "rc_dusk", "ended_loop", "blend_actors") else []) + (
        ["KH2COOP_ENEMY_GOD=1", "KH2COOP_TRAP_LOG=1"] if a.scenario == "limit_out" else []) + (
        ["KH2COOP_ENEMY_GOD=1", "KH2COOP_FX_COUNT=1"] if a.scenario in ("fx_sync0", "fx_sync1") else []) + (
        ["KH2COOP_ENEMY_GOD=1"] if a.scenario in ("limit_back", "limit_watch", "hud_hits") else []) + (
        ["KH2COOP_LIMIT_FX_MOVES=1"] if a.moves else []) + (
        ["KH2COOP_ENEMY_GOD=1", "KH2COOP_TRAP_LOG=1"] if a.scenario == "form_cast" else []) + (
        ["KH2COOP_TRAP_LOG=1"] if a.scenario in ("hydra", "cerberus", "ended_loop") else []) + (  # hydra: F532's poll and free
        ["KH2COOP_HELD_TRACE=2000"] if a.trace_grab or a.scenario == "cerberus" else []) + (
        ["KH2COOP_PARITY=1", "KH2COOP_TEST_ONE_HIT=1"] if a.scenario == "apart" else []) + (
        ["KH2COOP_TEST_ONE_HIT=1"] if a.scenario in ("swarm", "valor_sheet") else []) + (
        ["KH2COOP_DIAG_HUD=1"] if a.scenario == "capacity" else []) + (
        ["KH2COOP_AUTO_LIMIT=0"] if a.scenario == "barrels" else []) + (  # triangle is mashed: no Limit with Jack
        [f"KH2COOP_TEST_MICKEY_WINNER={'nobody' if a.end == 'nobody' else 'host' if a.winner == 'A' else 'friend'}"]
        if a.scenario in ("mickey_coop", "mickey_real", "mickey_hit") else []) + (
        ["KH2COOP_NEG_BUDGET_KB=100"] if a.scenario == "neg_group" and a.decline else []) + (
        ["KH2COOP_TEST_BLADE_DELAY_MS=1000"] if a.scenario == "blade_combat" else []) + (
        ["KH2COOP_EVENT_BLOCK_GUARD=1", "KH2COOP_ENEMY_GOD=1"] if a.scenario == "neg_group" else []) + (
        ["KH2COOP_PAUSE_SYNC=0"] if a.scenario in ("same_room", "npc_late") else []) + (
        ["KH2COOP_ENEMY_GOD=1"] if a.scenario in ("scaffold", "room_smoke", "cup_retry", "zones") else []) + (
        ["KH2COOP_ENEMY_GOD=1", "KH2COOP_TRAP_LOG=1"] if a.scenario == "bolt" else []) + (
        ["KH2COOP_SUMMON_SYNC=1"] if a.scenario == "summon_sync" else []) + (
        ["KH2COOP_ENEMY_GOD=1", "KH2COOP_TRAP_LOG=1"] if a.scenario in ("rodeo", "rodeo_self", "rc_state") else []) + (
        ["KH2COOP_TRAP_LOG=1"] if a.scenario in ("rapid_blow", "luxord_cards", "luxord_mirror") else []) + a.env
    started = time.time()
    out = [f"{a.scenario} {time.strftime('%Y-%m-%d %H:%M:%S')} slot {a.slot} by {a.by} pick {a.pick} env {' '.join(env)}"]
    copies = bench.launch(env)
    out.append(f"pids: A {copies[0].proc.pid}, B {copies[1].proc.pid}")
    for c, role in zip(copies, ("host", "friend")):  # what you watch: this run's scenario and its extra settings
        bench.set_title(c.hwnd, f"{c.name} {role}: {a.scenario} {' '.join(a.env) or '(default settings)'}")
    try:
        ok = SCENARIOS[a.scenario](copies, a, out)
    except (bench.Failed, SystemExit, OSError) as e:  # OSError: a read of a game that closed
        ok = False
        out.append(f"ERROR: {e}")
        for c in copies:
            c.lines()  # c.crash is only filled by reading the log
            if c.crash:
                out.append(f"{c.name} crash: {c.crash}")
            if c.alive():
                bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{c.name}_error.png"))
    finally:
        if not a.keep:
            bench.close(copies)
    out.append(f"{'PASS' if ok else 'FAIL'} ({time.time() - started:.0f} s)")
    with open(os.path.join(OUT, f"{a.scenario}.txt"), "a", encoding="utf-8") as fh:
        fh.write("\n".join(out) + "\n\n")
    print("\n".join(out))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
