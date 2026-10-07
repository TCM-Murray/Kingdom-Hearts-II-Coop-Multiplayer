# KH2 Co-op: master to-do list

Last updated: 2026-10-07 (session 13b: drops decision, drop bonus check). This file is **the plan**: what's left, in what order, and what we're ignoring on purpose.
`MODLOG.md` stays the diary (what happened, with evidence) and `VERIFIED_OFFSETS.md` the address book.

## The goal: what "playable" means
Two players on two PCs play KH2FM **from Sora's first steps (right after the Roxas prologue) to the end of the final Xemnas fight**:
- no crashes or soft locks along the story route;
- every required fight can be fought together;
- cutscenes play on both screens (the host starts them);
- each player keeps **their own Sora**: level, HP, items, Keyblades, gear, abilities, munny;
- both saves move through the story together;
- parts that can't be co-op (minigames, Gummi Ship, a few forced-solo scenes) don't break anything. The second player waits, then gets pulled back in automatically.

## How to read this file
- **P0** = blocks the start-to-finish run. **P1** = needed for it to play well. **P2** = polish / later. **Parked** = ignored on purpose for now (see "Parked").
- `[ ]` open, `[~]` built but not tested yet, `[x]` done. **(Qn)** = follows decision Qn in "Decisions".
- Numbers (1.3, 4.2...) stay fixed, so we can say "let's do 4.2 next". Tracks 1 to 4 are the four priorities you and your friend agreed on, in that order.
- **Host** = the PC that owns the world (yours). **Friend** = the second PC. **Copy** = the other player's Sora as it appears in your game.

---

## Where we are (end of session 9)
Working, tested on two PCs or on two game copies on one PC:
- Radmin VPN link and settings handshake. Each player is Sora in their own game. The other player shows up as a Sora copy in the party, replacing the active Donald or Goofy (the world's guest ally, like Auron or Beast, stays).
- The copy mirrors movement, jumps, attacks, magic, items and Limits. Enemies target both players, and a hit on the copy goes to its owner.
- The host owns rooms and enemies. The friend follows every room change of the host, including same-room reloads for events and bosses, and the friend's doors are locked. Enemy groups spawn from the host, and enemy position, motion and HP are mirrored. The friend's hits count on the host's enemies, and deaths are mirrored.
- Cutscenes play on both (Hades and Cerberus checked). Shared combat pause. Downed state (20 s, Mickey-rescue animations), revive with Cure or items, both down = Game Over, and the Game Over choice follows the host.
- Places tested: Hollow Bastion (Borough, Merlin's House), Olympus up to and including Cerberus, Beast's Castle (party only), the 100 Acre Wood entrance.
- Tools: save guard (the real save is never written), two-copy launcher, virtual pads, scene scripts, debug commands (damage, motion), effect-pool monitor, friend packager.

Not started: anything past those rooms, Drive Forms and Summons in co-op, story-progress sync, the drops rules, equipment on the copy, the dev suite.

---

## Decisions (made 2026-10-07, you + your friend)

| # | Question | Decision | Affects |
|---|---|---|---|
| Q1 | **Drops** (HP/MP/Drive orbs, munny, items enemies drop) | **Per player**: each game drops its own loot from the shared kill. Nobody fights over orbs; a mirrored kill already drops loot in the friend's game | 4.2 to 4.5, 4.8 |
| Q2 | **Chests and fixed rewards** (chests, "Obtained..." story items, bonus level-ups after bosses, a world's Keyblade) | **Both players get everything.** A chest opened by either player is opened for both, and both get the item | 4.6, 4.7 |
| Q3 | **What "own inventory" means** | Items, Keyblades, armor, accessories, abilities and munny live **only in each player's own save** (already true). On top of that: **(b)** your Keyblade, Drive Form and costume show on your copy in the other game, updated as soon as you change them; **your full equipment** (armor, accessories, abilities) can be viewed in the other game's menu on the copy's card, read-only; **(c)** the host's menus stop treating the friend's copy as Donald/Goofy (no Donald/Goofy gear on it, no Donald/Goofy stats). Giving/trading items stays parked | 3.x |
| Q4 | **Dev suite interface** | **A control-panel window on the PC first**, outside the game, talking to the mod. In-game menu maybe later; the game's old debug menu is a research side quest | 2.x |
| Q5 | **Drive Forms** | **The copy stays in the fight**: forms absorb only the AI members. Fallback if the game won't allow it: the copy counts as absorbed (hidden and invulnerable on both screens until the form ends). **Both players can use Drive Forms, also at the same time** | 1.23, 1.24 |
| Q6 | **Mickey rescue** | **Both games roll, the bigger margin wins** (the roll that beat its chance by more decides for both). **While Mickey is out, the other player is frozen and invulnerable and watches** until the rescue ends | 1.3 |
| Q7 | **Hollow Bastion defense turrets** (and similar world-side helpers) in the friend's game | **The friend's own turrets are switched off and the host's are shown** (same rule as enemies: the host owns the world) | 1.1 |
| Q8 | **Reaction commands (triangle prompts)** | **RCs that only move a player are free for both. Boss RCs** (Xaldin's Jump, Groundshaker, Saïx, Final Xemnas...) **go to whoever presses first; the other player watches the RC's scene.** Note: a boss RC pressed by the friend has to play in the host's game too (the host owns the boss), so this is the one exception to "only the host starts events" (session 9) | 1.26 |
| Q9 | **Parts that aren't normal co-op** (Light Cycle, carpet chases, Gummi Ship, Dragon Xemnas ship stage, forced-solo scenes) | **Look at each one separately.** The aim is both players in the same space, in sync. **Where that isn't feasible, each player plays that part solo in their own game**, then both carry on together | 5.2, 5.5, route checklist |
| Q10 | **Story progress** | **Yes, the host's story is the truth**: the friend's save gets the host's story flags (world progress, unlocked worlds, opened chests, journal) when joining and on change; the friend's character data is never touched. **But per-player things are tracked separately and never overwritten**: enemy drops, synthesis/crafting, shop purchases, and anything else each player does on their own | 5.1 |
| Q11 | **Saves** | Co-op saves are kept apart from single-player, and **the friend saves when the host saves**. **A co-op save starts as a copy of a normal save: you choose which save becomes the co-op save**, and the save/load menu shows it flagged as **"Co-op"** (an icon or a text label) | 5.7 |
| Q12 | **Starting a fresh co-op run** after the prologue | **The host hands the save to the friend once** at Sora's first save point (kh-pc-save-transfer, as now); Q10 keeps both in step after that | 5.8 |
| Q13 | **Summons** (Chicken Little, Genie, Stitch, Peter Pan; they send Donald and Goofy away) | **The copy stays and the summon joins** (only the AI members leave). Fallback: the copy is hidden and invulnerable until the summon ends. **Only one summon at a time**: while one player's summon is out, the other can't summon | 1.23, 1.32 |
| Q14 | **Orbs, munny and items the Sora copy picks up**, and whether enemy drops should be shared between games | **Keep as it is (2026-10-07):** no synced drops; each game rolls and drops its own loot (Q1), and whatever either Sora picks up in a game goes to that game's player (the copy shares the local Sora's stats). Synced drops would halve everyone's orbs and force raising drop rates. Chests stay "both get the item" (Q2) | 4.2 |

---

## Track 1: combat and world sync (priority 1)

### Open bugs (from sessions 5 to 9)
- [ ] **1.1 P0. Hollow Bastion turrets (Q7).** The friend's own turrets fire too, and their hits are sent to the host as the friend's hits, so host enemies take turret damage from both games. First find which attacker each claim comes from (the RecordAttacker hook can tell) and how turrets pick targets, then switch the friend's turrets off and show the host's (Q7).
- [ ] **1.2 P1. Shadows after sinking underground** are flat and can't be hit on the friend's side: likely the friend's local AI stays "underground" when the host's Shadow gets knocked out. Do it after 1.1. Trace with `KH2COOP_ENEMY_TRACE=1`.
- [ ] **1.3 P0. Mickey rescue in co-op (Q6). Now also a host crash (session 11):** when Mickey's rescue ends, the game rebuilds the party from the party list, which has the copy (a Sora) in Donald's slot, and crashes building it as an AI member (exe+0x1CCCF6). **Crash fix confirmed on two PCs (session 13: Mickey end skipped the copy's entry, no crash).** the game no longer rebuilds the copy's entry as an AI member when the rescue ends. **User's rules (2026-10-07):** only the host decides at Game Over, the friend's choice is locked, also while the host plays Mickey (built, session 12). Lock confirmed on two PCs (session 13: the friend couldn't press anything, no crash, but had to restart its game). Still to build: after the host's Mickey revives the host, **the friend's Game Over screen closes by itself and the friend is revived** too (not a reload; research how the game leaves Game Over); polish: play the revival animation on the friend's Sora. If the friend gets Mickey: the friend may only press "I won't give up", the host still decides Load/Continue, and if the host continues while the friend takes Mickey, the friend alone controls Mickey until both players are revived (the host's game owns the boss, so it can't sit on a Continue load meanwhile: research). Rule: both games roll, the bigger margin wins; the other player is frozen and invulnerable and watches while Mickey is out. Seen in the Cerberus test: both games rolled Mickey separately; meanwhile the host's copy played the friend's Mickey motions on a Sora body, and the host's Auron used an item on the dead host Sora. Also find where the "Mickey appearances" counter goes up (if only one game raises it, the two saves' chances drift apart).
- [ ] **1.4 P1. Enemy spawn/despawn loop when the friend is far from the host** (Olympus 06/0F, HB 04/09 in session 13; user: "constant spawning and despawning"). Session 13 logs: the friend walks into a group's box far from the host → the host's game spawns it → **the host's game removes those enemies (no death) within ~4 s because they're far from the host's Sora**, the group re-creates them, and the friend mirrors each cycle. Fix direction: find the host-side removal check and make it count the friend's position too (as the trigger already does).
- [x] **1.5 P1. Lock-on on enemies** (works, session 11): ask your friend what goes wrong. It may already be fixed by the command-menu fix (session 6b).
- [x] **1.6 P1. Hit reaction on the friend's screen** (the friend flinches and loses HP, session 11): when the host's enemy hits the friend's copy, the friend loses HP but their Sora doesn't flinch.
- [x] **1.7 P1. World allies' heals** (Auron heals the copies, session 11) (Auron, Beast...): the friend's mirrored ally can't use items (that was a crash). Check the friend still gets healed when the host's ally heals the copy.
- [x] **1.8 P2. Limit gauge** (drains normally, session 11) (top right) barely drains: compare with an unmodded Limit before calling it a bug.
- [ ] **1.9 P2. Cure reach** for healing the partner: 500 units is a guess, tune it by feel.
- [ ] **1.10 P2.** Continue after Game Over doesn't count as a new room load for world sync (harmless so far; verify).

### Built but never tested in a real situation
- [x] **1.11** The host's Auron Limit (Bushido) as seen from the friend: Auron stays visible and idle; the Limit reaction commands work for both (session 11). Showing the real animations = 1.31.
- [x] **1.12** Both players walking into a spawn area at the same moment: enemies appear once (session 11).
- [ ] **1.13** Rooms with random enemy sets (the friend should get the host's pick).
- [ ] **1.14** Enemies that come in waves; groups started by missions or signals.
- [ ] **1.15** "Spawn near the player" enemies triggered by the friend (they appear near the host's Sora).
- [ ] **1.16 P0. Battle barriers**: invisible walls that lock a room until every enemy is dead must drop on both screens at the same time.
- [~] **1.17 P0. EXP on the friend** from mirrored kills: shared, design accepted (session 11). Form EXP not checked yet.
- [ ] **1.18** Pause edge cases: host pause menu with enemies alive, both pressing pause at once, pause on the real friend PC.
- [ ] **1.19** Enemies that exist on the host but never spawn on the friend (invisible attackers).

### New work needed for the whole route
- [ ] **1.20 P0. Boss support.** Bosses have more going on than "position + motion + HP": phases, invulnerable moments, grabs, projectiles, summoned helpers, weak points. Give world sync a way to carry extra per-boss data, then go boss by boss (see "Route checklist"): fight it on the bench, compare host and friend, fix.
- [ ] **1.21 P0. Boss HP bars and boss name** on the friend follow the host's numbers (including bosses with several bars and multi-enemy bosses). Session 13: the friend's Cerberus bar had trouble updating. Likely: world sync writes mirrored HP straight into memory, not through the game's HP change, so the bar (which probably moves on damage events) doesn't follow; also the friend skipped the cutscene first, so its Cerberus sat parked out of sight for 5 s until the host's battle began (see 5.3).
- [ ] **1.22 P0. Missions and special rules**, owned by the host and shown on the friend: timers (Hades escape), protect/escort gauges (Land of Dragons morale bar, protecting Minnie), counters (1000 Heartless), Luxord's time gauge, "defeat all" goals. Cleared/failed must happen on both. First step: find the mission state in memory.
- [ ] **1.23 P0. Drive Forms and Summons don't break co-op**: no crash, no lost copy, no soft lock, for either player. (Already seen once: the host went into Valor and back with the copy present, no crash.) Session 13 (two PCs, Valor): no crash; **downed in a form = T-pose and no revival animation** (the form's motion set has neither the rescue motions nor the fallback collapse 54); fix idea: end the form when downed, or use motions the form set has. Forms end through the game's party rebuild (exe+0x400440), which our Mickey fix already makes safe for the copy's entry.
- [ ] **1.24 P1. Drive Forms fully co-op (Q5)** (session 13: neither screen shows the other player's form; the copy stays a normal Sora playing the form's motion ids as normal-form attacks): the copy stays in the fight (fallback: hidden and invulnerable until the form ends); both players can be in a form at the same time; the friend's form shows on the copy (model, two Keyblades, motions), form hits are claimed on the host, Anti Form, the form gauge per player.
- [ ] **1.25 P1. Limits with every partner**: Auron's works. Check the others, especially ones that need both Donald and Goofy (Trinity Limit) while the copy replaces one of them, plus every world ally and Riku.
- [ ] **1.26 P0/P1. Reaction commands (Q8).** P0: boss RCs pressed by the host (Xaldin, Groundshaker, Saïx, Final Xemnas...) work with the friend present and play on the friend's screen. P1 (Q8): movement RCs free for both; boss RCs go to whoever presses first and the other watches, so a friend's boss RC must be sent to the host and play there.
- [ ] **1.27 P1.** Being grabbed or carried by an enemy, stun, freeze, knockback, guard/reflect, on the copy and on mirrored enemies.
- [ ] **1.28 P1.** Projectiles and area attacks: who gets hit where. The copy is a little behind the real friend, so some hits will feel unfair; measure first.
- [ ] **1.29 P1.** Fights with a required ally (Beast, Simba, Mulan, Riku, Mickey): the ally must behave the same on both screens.
- [ ] **1.30 P0. The World That Never Was party**: Riku joins (and Mickey in some scenes). Which member does the copy replace there?
- [ ] **1.31 P2.** Show the real Limit and reaction-command animations on the copy (today it shows idle for motions that come from another file).
- [x] **1.33 P0. Crash on a spell cast by the copy (session 11; fixed in session 12; confirmed on two PCs in session 13: the skip fired twice in real play, no crash):** the friend crashed when the host's Ice cast played on the copy (exe+0x3C6161, the cast's "spell goes out" trigger reads a leftover link on the copy). Applies both ways. Reproduce on the bench, then skip that trigger on the copy.
- [x] **1.34 P0. Lone Continue after Game Over (session 11; session 12: the friend's Game Over is locked while the host is connected; confirmed on two PCs in session 13; the follow-up "friend revived with the host" is in 1.3):** if only the friend is at Game Over (the host got Mickey), the friend can press Continue; our door lock refuses that room load and the friend loops on a white loading screen. Fold into 1.3's rule (the friend waits/is frozen while the host's Mickey runs).
- [ ] **1.36 P1. Drop bonuses counted twice** (user question, session 13: "each encounter drops at least one item"). The enemy drop code (exe+0x3DC170) adds up Lucky Lucky (+0x6C) and Jackpot (+0x68) over the 3 party members and multiplies the Drive Converter factor (+0x74); the mod hands it the copy as member 2, and the copy shares our Sora's stat record, so **our own Sora's bonuses count twice** (Donald's no longer count). Live read on the user's save: Sora's Lucky Lucky value 1.0 → item drop factor 1 + 1 + 1 + Goofy 0 = **3.0 instead of 2.0 (+50 %)**; Jackpot 0 (not equipped, so munny/orbs are normal for now). Not shared between games: each game doubles its own player's. **User decision (2026-10-07): fix it, normal drop rates.** Fix (small): add the drop code's callers 0x3DC208, 0x3DC2E7 and 0x3ABA67 to the "never give the copy" list (puppet.cpp kNoCloneCallers or exact-caller list), so drops count Sora + the AI members like solo; the copy still picks up prizes for the local player (Q14). Check on the bench: GetFriend caller stats show 0x3DC208/0x3DC2E7/0x3ABA67 as refused; then a two-PC session.
- [ ] **1.35 P0. Copy detection picks a leftover Drive Form body** (session 13). The mod takes "any Sora-type actor that isn't the player" as the copy; when a form ends, the old form actor stays in the room, and the mod switched to it: for the rest of the room the other player was shown on that form body (the user saw the friend "in Valor" while they were normal), and hits/heals on it count as the copy's. Fix: keep the copy found at the room load (don't switch to another candidate while it's alive) and ignore actors that were ever the player.
- [ ] **1.32 P1. Summons fully co-op (Q13)**: the copy stays and the summon joins (fallback: copy hidden and invulnerable until the summon ends); only one summon out at a time across both players; the summon shows on the other screen and its hits are claimed on the host. Session 13: **summon commands are greyed out with the copy in the party** (likely the game wants more AI members; the copy isn't one): find that check first.

---

## Track 2: dev and test suite (priority 2)
**Goal:** test any room, enemy, boss or animation in seconds instead of playing all the way there.

**What exists:** `KH2COOP_DEBUG=1` plus `tools/debug_cmd.py` (damage, motion); `scene.py` (title, save slot, room); `launch_pair.py`; virtual pads (`vpad.py`); `nav.py`; `peek*.py`; `pool_stat.py`; `anb_triggers.py`. In the exe there's the game's leftover debug flag list: "no gameover", "anytime mickey", "stop enemy", "infinity item", "ignore zone", "test limit"... (MODLOG, Mickey rescue research). Some of these flags no longer do anything in this build, so most of the suite will be our own commands.

Q4 decided: a PC control panel first.
- [ ] **2.1 P0. Command channel v2** in the DLL: one general "command + arguments" message with a reply, so tools can also read things back. Only in dev builds, only from this PC (localhost), never in the friend's package.
- [ ] **2.2 P0. Warp**: load any world, room or door with chosen programs (map, battle, event). This is how we "spawn a boss": load the boss room with its battle program. The game function for it is already in use by room following.
- [ ] **2.3 P0. Catalogs** built from this PC's own OpenKH extraction: every world and room with its programs (from the .ard files); the 1900 rows of the object table (enemies, bosses, NPCs, model names); motions per model; item, Keyblade, armor, accessory and ability ids; mission files per room. Saved as JSON for the panel's lists. Stays on this PC.
- [ ] **2.4 P0. Actor inspector**: every actor in the room (object, model name, team, HP/max, motion, position, flags), for host and friend side by side; pick one to watch it live.
- [ ] **2.5 P1. Spawn an enemy** by object id at Sora's position. Needs research: spawn sync can only re-create an enemy from its room's spawn record, so a free spawn needs one more game function.
- [ ] **2.6 P1. Player test switches**: set HP/MP/Drive/level, god mode, kill all enemies, freeze enemy AI, no Game Over, force Mickey, endless items. Use the game's own flags where they still work.
- [ ] **2.7 P1. Inventory tools**: give an item, Keyblade, armor, accessory or ability. They change the running game's save data only; the save guard keeps the real save untouched.
- [ ] **2.8 P1. Story-flag tools**: show the difference between host and friend story progress; set a flag. Needed to build and test 5.1.
- [ ] **2.9 P1. Animation lab**: play motion N on any actor, step frame by frame, list a motion's triggers, play bank motions (Limits, reaction commands).
- [ ] **2.10 P1. Memory tools**: read/write/watch an address safely; search by value; effect/engine pool usage; "who wrote this address" (partly in `watch.cpp`).
- [ ] **2.11 P1. The control panel (Q4)**: buttons and lists for 2.2 to 2.10, connected to both game copies on this PC.
- [ ] **2.12 P1. Smoke test**: one command launches two copies, runs a fixed checklist (follow, spawn, hits, downed, Game Over, cutscene) and prints PASS/FAIL. Run before every friend package.
- [ ] **2.13 P1. Boss gym**: one preset per boss in the route (world, room, programs, needed story state), one click each.
- [ ] **2.14 P1. Test saves for later in the story**: your save reaches Hollow Bastion, Olympus and Beast's Castle. Later bosses need saves there (your own as you play, your friend's if further, or 2.8's story flags).
- [ ] **2.15 P2.** In-game menu (maybe later, Q4); look at whether the game's own debug menu screen still exists.

---

## Track 3: each Sora's own inventory and character (priority 3)
**Background:** each player's Sora lives in their own save on their own PC, so items, Keyblades, gear, abilities, level and munny are already separate. What's missing is how the *other* game sees you, and the places where the copy still borrows Donald's or Goofy's slot.
- [x] **3.1 P0. Confirm Q3.** Done 2026-10-07: own save only + Keyblade/form/costume on the copy + partner's equipment viewable read-only + copy no longer treated as Donald/Goofy in menus.
- [ ] **3.2 P0. Check what the copy uses today** in the other game: whose HP/stat slot (it reads HP 1), whose equipment and abilities, which Keyblade model; what the Status/Equip/Ability screens show on its card (it's reported as Donald or Goofy).
- [ ] **3.3 P0. Your Keyblade on your copy**: send your equipped Keyblade in the avatar packet; the other game changes the copy's weapon when it changes. Includes the world variants (Halloween Town, Timeless River...) and both weapons in forms.
- [ ] **3.4 P1. The copy's numbers come from its owner**: the party HUD shows your partner's real HP bar; HP/MP/level are right for anything the game checks (e.g. "is this party member able to fight").
- [ ] **3.5 P1. Menus (Q3)**: neither player can equip or customize the other's copy (no Donald/Goofy gear on it); the copy's card shows the partner's real equipment (armor, accessories, abilities) read-only, with a proper picture and name.
- [ ] **3.6 P1. Movement and combo abilities** (Glide, High Jump, Aerial Dodge, Quick Run, Dodge Roll, combo finishers): your own game already uses yours. Check the copy can play them on the other screen; the copy may lack the right ability level and show the wrong animation.
- [ ] **3.7 P1. Items on the partner**: healing items used on the copy already reach the other player. Add MP items (Ether...) and whole-party items (Megalixir). The item sparkles still show on the wrong Sora (cosmetic).
- [ ] **3.8 P1. Shops, Moogle synthesis, munny** stay local to each player. Check nothing breaks while the partner is in a menu.
- [ ] **3.9 P1. Equipment changed mid-session** (gear or Keyblade) shows up on the other screen right away (3.3/3.4/3.5 resend on change).
- [ ] **3.10 Parked.** Giving or trading items between players.
- [ ] **3.11 P1. Party HUD widget for the Sora copy** (user request 2026-10-07). Today the party HUD (bottom right) only shows the AI members (Goofy, or whoever else is in the party); the other player's Sora copy has no widget at all. Wanted: a working widget for the copy like any party member's: the other player's face/icon, their real HP bar (and MP if we send it), updated live, greyed or flagged while they're downed. Likely cause: the HUD builds its list from the friend slots, and the copy isn't in them (slots hold {Goofy, empty}; the copy is only handed out through GetFriend(1) to some callers). Research: find the HUD's party-panel code and what it reads (slots, GetFriend callers, stat slots), then either let it see the copy or draw the widget from the peer packet's HP (3.4). Works together with 3.4 (the copy's numbers come from its owner) and 3.5 (menus).

---

## Track 4: drops and rewards (priority 4)
**Facts today:** a host kill is mirrored as a real kill in the friend's game, so the friend's game drops its own orbs/munny/items (confirmed for HP orbs). The copy can also pick up orbs in the other game: the HP goes to the copy (no effect), but the orb is gone for the local player. Munny picked up by the copy probably goes into that game's player's wallet (not checked).
- [x] **4.1 Decide Q1.** Per player (2026-10-07).
- [x] **4.2 P0. Decided (Q14): keep it.** Whatever the copy picks up goes to that game's own player (session 13). Old plan, dropped: the copy must not pick up anything (orbs, munny, items) in the other game; prizes stay for the local player. Find the game's prize-pickup function.
- [x] **4.3 P0. Every drop type appears on the friend** (each game drops its own, accepted, session 11) from a mirrored kill: HP/MP/Drive orbs, munny, synthesis materials, item drops. Luck abilities apply to each player's own drops.
- [ ] **4.4 P0. Kills by the friend's Limit, Summon or form** count on the host like normal hits (check they go through the claim path).
- [~] **4.5 P0. EXP** (same as 1.17: shared, accepted; form EXP not checked).
- [ ] **4.6 P1. Chests (Q2)**: a chest opened by either player gives its item to both and is marked opened in both saves. Find the chest-open and reward functions.
- [ ] **4.7 P1. Story rewards and boss bonuses** ("Obtained..." items, Keyblades, HP/MP/slot bonuses, new abilities): check the friend's game gives them (it plays the same events, so it probably does); mirror them if not.
- [ ] **4.8 P2.** Breakable objects and treasure blocks that drop munny: per player like enemy drops.

---

## Track 5: story, rooms, cutscenes, progression
- [ ] **5.1 P0. Story progress sync (Q10)**: find the story-flag part of the save (OpenKH's save docs are leads; verify on this build); the host sends it at join and on change, the friend writes it; character data untouched. List the per-player parts that must never be overwritten (drops, synthesis/crafting progress, shop purchases...) and keep them out of the sync. Opened chests are synced (Q2).
- [ ] **5.2 P0. World map and Gummi Ship (Q9)**: the host picks the world. Gummi routes: together if feasible, otherwise each player flies them solo in their own game, then both land in the new world together. Test world-map following and arriving in a new world.
- [ ] **5.3 P0. Cutscenes**, beyond Hades/Cerberus: pre-rendered movies; skipping (host skips, friend skips); cutscenes that move Sora to another room; the friend pressing buttons during one. Checked world by world in the route checklist. Session 13 (user): cutscenes play almost seamlessly on both now; but **pausing a cutscene isn't shared** (each player can pause alone) and **skipping isn't synced**: the friend skipped first, its boss battle started 5 s before the host's (Cerberus parked meanwhile, boss bar trouble, 1.21). Needs a rule (e.g. one skip skips both, the cutscene pause is shared like the combat pause); ask the user.
- [ ] **5.4 P0. Events started by talking or examining** (NPCs, doors with a prompt, story objects): only the host starts them (decided in session 9); make sure the friend's prompts don't half-start something.
- [ ] **5.5 P0. Forced party changes**: where Donald/Goofy leave, the party is fixed, or Sora is alone (Pride Lands, Space Paranoids, Timeless River, The World That Never Was with Riku, solo scenes): look at each one; keep both players together and in sync where feasible, otherwise each plays it solo in their own game and they regroup after (Q9).
- [ ] **5.6 P0. World costumes**: Halloween Town, Christmas Town, Space Paranoids, Timeless River, Pride Lands (lion). The copy must load the right body and motions, and each variant needs the rescue motions for the downed state (`make_mset.py` per variant).
- [ ] **5.7 P1. Saves (Q11)**: co-op saves kept apart from single-player; you pick which normal save becomes the co-op save (it starts as a copy of it); the save/load menu shows it flagged "Co-op" (icon or text); the friend saves when the host saves; the save guard stays on.
- [ ] **5.8 P1. Starting a co-op run (Q12)**: written step-by-step instructions. The host hands the save over once at Sora's first save point (kh-pc-save-transfer), then 5.1 keeps both in step.
- [ ] **5.9 P1. Joining late / rejoining after a crash**: the friend loads its save, connects, gets the story flags (5.1) and is pulled into the host's room.
- [ ] **5.10 P1. Load barrier: both games come out of the black screen together** (user idea 2026-10-07, replaces "cutscene text one line behind"). Today the friend loads only after the host's load ended + 0.5 s settle, so the friend is ~1-1.5 s behind (cutscenes, enemies, boss start). Plan:
  1. As soon as the host's game knows its destination (room + programs), it tells the friend, which starts loading at once (parallel loads).
  2. The host finishes loading but holds on the black screen until the friend reports "loaded"; then the host sends "go" and both fade in together (the friend also waits for "go").
  3. Safety: a time limit (a few seconds) after which the host carries on alone; no friend connected = no wait; if the friend loaded another room version, the current follow corrects it.
  4. Same-room reloads (event -> boss battle) go through the same barrier.
  Research first: where to hold the host (after the load, before fade-in / event start / enemies waking) without upsetting music, timers or the event; when the final programs are known (NOW exe+0x717008 vs the request). Test scene: Olympus 06/0A save, walk out of the safe zone into 06/06 (Hades cutscene).
  **Research done (session 12b):** the hold point exists and works: the room-load task waits on exe+0x156770 (room ready) before starting the room; answering "not ready" there holds the game on a black screen and the cutscene then plays normally (3 s test, Hades scene). NOW holds the final destination + programs the moment the load starts. Experiment switch: `KH2COOP_LOAD_HOLD_MS`. Still to check: audio during a hold. Next: build the host/friend messages and the timeout.
  Also check: friend-side enemies came in groups 5 s apart in the Borough (session 11 log): the game's waves or our 5 s spawn retry?

---

## Track 6: network and stability
- [ ] **6.1 P0. Disconnect and reconnect**: if one game crashes or closes, the other keeps playing solo (no stuck pause, no door lock left on), and the friend can rejoin (5.9).
- [ ] **6.2 P1. On-screen message** when the link drops or the settings differ (today it's only in the log).
- [ ] **6.3 P1. Lag tests** on boss fights with the built-in network simulator (100 to 250 ms, packet loss).
- [ ] **6.4 P1. Engine memory pools**: one crash came from the effect pool filling up (fixed). Watch the pools during big boss fights (`pool_stat.py`) and find the other pools.
- [ ] **6.5 P1. Friend's logs in one click**: a script that zips the newest logs and crash logs to send over.
- [ ] **6.6 P2.** Stable controller order for local two-pad tests (XInput slot numbers change after reconnecting a pad).

## Track 7: setup, in-game messages, packaging
- [ ] **7.1 P1. In-game text**: "Waiting for the host", "Your friend joined/left", "The host is choosing..." on the Game Over screen, "Settings differ". Needs research into the game's message system.
- [ ] **7.2 P1. Easier friend updates**: today it's replace the folder + the ini + Build in Mods Manager. Make it one step, and show the mod version in game so a mismatch is obvious.
- [ ] **7.3 P1. Separate play and dev settings**: the play config has the extra logging off (`AGGRO_LOG`, research logs); the dev config keeps it.
- [ ] **7.4 P2.** A small launcher (Host / Join buttons, IP field) instead of the .bat and ini.
- [ ] **7.5 P2.** A short player README: install, start, known limits.

## Track 8: housekeeping
- [ ] **8.1** `AGGRO_LOG=1` off in `start_host.bat` and the friend's ini for play sessions.
- [ ] **8.2** Trim the research logging ("command: ..." first 30, "heal: item N lands", copy-heal call stacks).
- [ ] **8.3** The HP log line shows the boss in the "goofy" slot (cosmetic).
- [x] **8.4** Version control: git repository since 2026-10-07, pushed to a private GitHub repo.
- [ ] **8.5** Cosmetic: Cure's effect drawn on the copy's Keyblade.

---

## Route checklist: Sora's story, world by world
Fights and special parts are written **from memory**. I'll check each one against the game's own mission files with the catalog tool (2.3) before we work on that world.
Status: ✅ played in co-op · 🟡 partly · ⬜ not tried.

| # | Part | Required fights and special parts | Co-op plan | Status |
|---|---|---|---|---|
| 1 | Twilight Town (Sora wakes up) and the Mysterious Tower | first Nobody/Heartless fights, the tower climb, Yen Sid's events | together | ⬜ |
| 2 | Hollow Bastion, 1st visit | Bailey Nobodies | together | 🟡 our test area (Borough, Merlin's House) |
| 3 | Land of Dragons 1 | missions with Mulan (morale gauge), Shan-Yu | together; gauge mirrored (1.22) | ⬜ |
| 4 | Beast's Castle 1 | Thresholder and Possessor, Shadow Stalker / Dark Thorn | together | 🟡 party only |
| 5 | Olympus Coliseum 1 | Cerberus, Hades escape (timed), Pete, Hydra | together | 🟡 Cerberus beaten together (session 13); Auron's leaving synced |
| 6 | Disney Castle and Timeless River | protecting Minnie, the Windows of Time, Pete; retro costume | together | ⬜ |
| 7 | Port Royal 1 | cursed pirates (moonlight rule), Barbossa | together | ⬜ |
| 8 | Agrabah 1 | Abu escort, carpet ride, Volcanic Lord and Blizzard Lord | together; carpet ride: together if feasible, else solo each (Q9) | ⬜ |
| 9 | Halloween Town 1 | costume, Prison Keeper, Oogie Boogie | together | ⬜ |
| 10 | Hollow Bastion 2 and Space Paranoids 1 | Demyx (clones, timer), Hostile Program, **Light Cycle** minigame, the 1000 Heartless battle | together; Light Cycle: together if feasible, else solo each (Q9) | ⬜ |
| 11 | Second visits: Land of Dragons, Beast's Castle, Olympus, Port Royal, Agrabah, Halloween Town / Christmas Town | Storm Rider, Xaldin (Beast's Jump RC), Hades, Grim Reaper ×2 (medallion), Genie Jafar, Lock/Shock/Barrel, The Experiment | together | ⬜ |
| 12 | Pride Lands 1 and 2 | Sora is a lion (different body), Scar, Groundshaker (riding Simba) | together if the lion copy works (5.6) | ⬜ |
| 13 | Space Paranoids 2 | Sark and the MCP | together | ⬜ |
| 14 | Twilight Town again, Betwixt and Between | Nobody fights with Axel | together | ⬜ |
| 15 | The World That Never Was | Roxas (Final Mix), Xigbar, Luxord (time gauge instead of HP), Saïx (Berserk RC), Riku joins (1.30) | together | ⬜ |
| 16 | Final fights | Xemnas, Armored Xemnas, Dragon Xemnas (ship shooting stage), Final Xemnas (Sora and Riku, reaction commands) | together; ship stage: together if feasible, else solo each (Q9) | ⬜ |
| — | Between worlds | world map, Gummi Ship routes (each needed once) | host picks the world; Gummi together if feasible, else solo each (Q9) | ⬜ |

---

## Parked: ignore these for now
- **Optional content**: Atlantica musicals, 100 Acre Wood minigames, Olympus tournaments, Data Organization, Lingering Will, Sephiroth, Mushroom XIII, Cavern of Remembrance, optional Gummi missions, journal/puzzle completion.
- **Roxas or other characters for player 2** (Sora copy for now, decided 2026-10-04), and their own movesets.
- **More than two players.**
- **The Roxas prologue in co-op** (the run starts after it).
- **Giving/trading items** between players (3.10).
- **Difficulty for two players** (an enemy HP/damage multiplier setting): only once the route is playable.
- **"Watch the host's screen"** during non-co-op parts: not planned; Q9 says each player plays those parts solo when they can't be shared.
- **The friend starting events or bosses itself**: decided, only the host does (session 9). Exception: boss reaction commands go to whoever presses first (Q8).
- **Fully mirroring enemy AI**: the friend's enemies still run their own AI with motions blocked. Only revisit if a boss needs it.
- **Steam networking / Steam invites**: Radmin VPN works.
- **A public release** (Nexus etc.): needs the motion files built on each player's PC, a license check of everything, an installer.

---

## Suggested order
**Next session (planned 2026-10-08):** 1.36 drop bonus fix (small) and 1.35 copy detection after a Drive Form (P0), then downed in a form (1.23), then 5.10 load barrier (research done) and 1.4 spawn loop. One rebuild + friend package for all, then a two-PC test.

1. ~~**Decisions round**~~: done 2026-10-07 (Q1 to Q13).
2. **Finish the open combat bugs** that block normal play: 1.1 turrets, then 1.3 Mickey. Friend package after.
3. **Dev suite core**: 2.1 to 2.4, a bare 2.11 panel, 2.12 smoke test. Every later world and boss gets much cheaper to test after this.
4. **Drops and EXP**: 4.2 to 4.5 (with 1.17), then the copy audit and Keyblade (3.2, 3.3).
5. **The big unknowns, researched before they become blockers**: 1.23 forms/summons safe, 5.1 story-flag sync, 5.2 world map, 1.22 mission state.
6. **Walk the route** (route checklist) one world per session, using the boss gym (2.13); log each boss's issues under 1.20 and fix them there.
