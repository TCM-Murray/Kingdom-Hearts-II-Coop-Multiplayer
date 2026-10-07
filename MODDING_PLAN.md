# MODDING_PLAN: Kingdom Hearts II Final Mix Online Co-op

Status: **decided 2026-10-04:** Option A (each player is the hero on their own PC); build our own code using Volpestyle's docs as re-verified leads; all §6 proposals accepted.

## Progress (updated 2026-10-05)
- **Phase 1 done.** Your build matches the one other projects mapped; key addresses are verified in VERIFIED_OFFSETS.md.
- **Phase 2 nearly done (one PC, two game copies):**
  - each copy obeys only its own pad;
  - the copies stream their Sora to each other;
  - each shows the other player as a real Sora copy in Donald's party slot (Donald benched), copying movement, jumps, attacks and spells;
  - enemies target the copy (nearest Sora), and hits on the copy go to its owner's own HP;
  - save guard, menu crash and room-change crash fixed; I can run scripted tests without you.
- Still open in Phase 2: your manual test; pickups (orbs, munny) grabbed by the copy must not count for the local Sora; P2 knocked out = "down 20 s", not Game Over.
- Moved to Phase 3: P2 damaging enemies. The copy's swings are animation only, so P2's hits must count in P2's own game and be sent to the host's matching enemy (needs enemy matching between games).
- **Character decision (you, 2026-10-04):** a Sora copy for P2 for now. Roxas worked as a dressed-up Donald, but with a backwards keyblade. A Roxas player-class copy comes later.
- **HP decision (you, 2026-10-05):** each Sora has its own HP pool (true co-op).

---

## 1. Step 0 result: does this already exist?

**No working, downloadable mod does this.** There are two serious attempts, both young and both unfinished:

| Project | What it achieved | Can we reuse it? |
|---|---|---|
| [Volpestyle/kh2-multiplayer](https://github.com/Volpestyle/kh2-multiplayer) (Mar–Oct 2026, very active; last commit 2 days ago) | C++ DLL inside the game plus a small relay server. Remote players appear as puppets in Donald/Goofy's slots, with walk/run animations synced. Shared enemy HP and deaths, and host-led room changes, tested with 3 game copies on one PC. Has not been tested between two real PCs. | **No license file**, so legally we can read it but not copy its code. Its docs (memory map, lessons learned, Ghidra traces) are the best KH2 PC reverse-engineering reference around. Treat every address in them as a *lead* we re-verify on your build, never as truth. Asking the author for permission or collaboration is an option (§3). |
| [Expert595/kh2-multiplayer](https://github.com/Expert595/kh2-multiplayer) (Sept 2026) | Downloadable alpha: movement plus "each player in their own room" sync, **no combat**. Experimental trick: spawns a native Sora/Roxas in a party slot by editing the save's party table before a room loads. | No license. The party-table trick is a useful lead. |
| [Snackya/KH2-co-op-mix](https://github.com/Snackya/KH2-co-op-mix) (2021, MIT) | Randomizer item and level sharing through the GoA hub. Not character co-op; broken on current PC versions. | MIT, but nothing relevant to reuse. |
| kh-vids "KH2 Multiplayer Hack" (PS2 cheat codes, old) | P2 moved and attacked as a party member on the PS2 PAL version; movement only on Final Mix. | Proves it can be done; nothing reusable on PC. |
| [Playable Dual Wield Roxas](https://www.nexusmods.com/kingdomhearts2finalmix/mods/368), [Roxas Mod (Napstio)](https://www.nexusmods.com/kingdomhearts2finalmix/mods/18) | Make Roxas the **player character** (replacing Sora) with his own moves. | Very useful if P2 is the "player" on their own PC (§3, option A). Check each mod's terms before depending on it. |

The universal-modder knowledge base has no KH2 notes yet.

---

## 2. How the game is built (plain language)

- **KH2FM PC is a native C++ program** (not .NET, whatever `um scan` says). There's no official mod support. The community built its own:
  - **OpenKH Mods Manager**: replaces game files (models, moves, room scripts, text) without touching the originals. *You already have it installed* at `D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\`.
  - **Panacea**: the piece of OpenKH that sits inside the game (as `DBGHELP.dll`) and feeds it the replaced files. *Installed.*
  - **LuaBackend**: runs small Lua scripts every frame that can read and write the game's memory. Good for quick experiments and simple rules (e.g. "lock this party slot"). *Installed.*
  - **Our own C++ DLL**: needed for anything frame-exact or networked, such as intercepting game functions ("hooks"), driving a puppet character every frame, and talking to the other PC. Lua can't open network connections and runs at the wrong moment in the frame for this work. Both co-op projects learned that the hard way.
- **Your build:** Steam build 15194255 (updated 2026-08-13). The exe SHA-256 is logged in MODLOG.md. Volpestyle targets "1.0.0.10 Steam Global"; we confirm it's the same build by comparing a few known code bytes once the game is running.
- **Saves sync to Steam Cloud.** Tooling never writes your real save; co-op work uses copies. A full backup is already made (see MODLOG.md).

---

## 3. The big decision: how Player 2 exists in the game ⚠️ needs your answer

Your brief puts P2 in a party slot (where Donald/Goofy go) and has a second controller drive that party member. There are two ways to build that experience:

### Option A: "each player is the hero on their own PC" (recommended)
- **Your PC:** you're Sora, completely normal. P2 appears as **Roxas in a party slot**; Donald or Goofy is benched, and that slot is locked. That Roxas is a *puppet*: we copy P2's position, animation and actions onto it every frame from the network.
- **Your friend's PC:** **they are the main character (slot 0), wearing Roxas's model**, so the game's own controls, camera, combos, guard, dodge, lock-on and HUD all just work. Your Sora appears as a puppet in a party slot on their screen.
- **Why it's better:**
  - KH2 only lets the main character take controller input. Making a Donald/Goofy-type actor jump, attack and guard on command is the hardest reverse-engineering problem in this whole project; Volpestyle has spent months on it and has walking only.
  - In Option A that problem disappears. It's also what *both* existing projects switched to.
- **Bonuses:**
  - "Generic kit" comes almost free, since Sora's base moveset is the same basic kit (move, jump, ground and air combos, guard, dodge roll).
  - Real Roxas moves can later come from existing player-Roxas mods.
  - P2's level, HP, MP, abilities and equipment are just the main-character stats in **P2's own save on P2's PC**.
  - Shared EXP happens naturally if enemy deaths are mirrored onto P2's game.
- **Cost:**
  - The single-PC prototype (Phase 2) becomes **two copies of the game on one PC**. Volpestyle confirmed that runs, with 3 copies on Steam. We'd need to give each copy its own controller, a small, well-understood DLL job that we still verify.
  - Both PCs must see the same enemies, so enemy sync moves earlier (Phase 3 either way).

### Option B: "P2 drives a party-member actor" (your brief, literally)
- One game runs the whole fight on your PC. P2's controller input is injected into the party member, and P2's PC just shows a camera on that character.
- **Why it's harder:**
  - We'd have to teach a friend actor jump, combos, guard and dodge by reverse-engineering the friend AI's action system. That's possible in principle (the old PS2 hack did attacks) but **very hard** on PC, with no known working example.
  - P2's PC still has to *render* the scene. Either it runs its own game kept in sync (back to the problems of Option A), or we stream video (simple, but laggy and not really "their own game").
- **Upside:** literally matches the brief, and the single-PC/two-controller prototype works without multiple game copies.

**Recommendation: Option A.** It reaches "two friends fighting together on their own screens" sooner, with far less risk. On *your* screen it looks exactly like your brief: Roxas in a party slot, Donald or Goofy benched, never swappable.

### Related choice: Volpestyle's project
1. **Build our own, using their docs as verified leads** (recommended), *and*
2. optionally **you** message the author (James) to ask about a license or collaboration. That's your call; I won't contact anyone.
3. Alternatively, contribute to their project instead of building ours. Fastest route to *something*, but you'd follow their roadmap (3 Soras first, Roxas later) and need their permission.

---

## 4. Networking (Phase 3 preview)

| Approach | How | Verdict |
|---|---|---|
| Input sync (lockstep) | Send only button presses; both games simulate identically | ✗ KH2 has hidden timers and randomness we can't make identical; desyncs are guaranteed |
| Full host authority | Host simulates everything, P2 sends inputs, gets results back | ✗ P2's own character would lag a full round trip behind their stick. Needs Option B's friend-control problem solved |
| **Hybrid (recommended)** | Each player's game owns **its own character** and streams it. **Host owns the world**: enemies, enemy HP and deaths, rooms, cutscenes, story | ✓ Responsive controls, consistent world. The same model the best comparable projects use |

Connection: direct, Tailscale recommended (free, no router changes). The host runs a tiny relay next to the game.

---

## 5. Phases: honest difficulty and main risks (assuming Option A)

| Phase | Difficulty | Main risks |
|---|---|---|
| **1. Recon** | Easy (mostly done) | Your build differing from Volpestyle's (most of their addresses then need re-finding) |
| **2. Single-PC prototype** (2 game copies, 2 Xbox pads, Roxas puppet in Twilight Town) | **Medium** | Party-slot puppet shows Roxas's model and plays player animations (Expert595 did Sora/Roxas this way); each game copy reads only its own controller; puppet takes damage and is targeted by enemies (puppets may not have hitboxes by default) |
| **3. Networking** | **Hard** | Enemies are where every comparable project stalls. Plan in two steps: (1) shared HP and deaths, each game runs its own enemy AI (enemies may stand in slightly different places on each screen); (2) host's enemy AI mirrored onto P2's screen (**very hard**; may never fully land). Room following already works in Volpestyle's tests, so the risk is lower there |
| **4. Progression** | **Medium** | Keeping P2's *story* progress in step with yours while their *character* stats stay their own; never corrupting a save (all work on copies) |
| **5. Full playthrough** | **Very hard, long tail** | Cutscenes (nobody has synced them, so plan "P2 waits, then resync"); rooms that force a solo or specific party; drive forms; reaction commands; ~40 bosses checked one by one; minigames; Gummi Ship. Realistic target: the whole game playable, with P2 spectating some segments |
| **6. Real movesets** | Roxas/Riku: **medium–hard** (community player mods exist; their puppet on *your* screen needs the same animations). Non-keyblade characters: **very hard** |

**Overall honesty:** a full playthrough of co-op is a months-long project. Volpestyle has worked on it since March 2026 (it looks like with several AI agents) and isn't at shared combat yet. We'll work in small slices that each do something visible, so even partial progress is playable.

---

## 6. Open design questions: my proposals ⚠️ need your answer

**Drive forms.** In vanilla KH2, Sora absorbs party members when he transforms. Proposal: **P2 counts as absorbed.** While Sora is in a form, P2's character is hidden and invulnerable on both screens. P2's camera watches Sora, and P2 reappears next to him when the form ends. Simple, safe, no new rules. *Later option:* P2 stays in the fight and only the AI member is absorbed (needs RE on form requirements).

**Cutscenes, menus and special sections.**
| Situation | Proposal |
|---|---|
| Story cutscenes | Host plays them. P2's game pauses behind a "Sora is in a cutscene" screen, then resyncs. *Later:* P2 also plays the same cutscene locally |
| Reaction commands | Sora only at first; P2's triangle does nothing. Later, per RC (e.g. team attacks) |
| Host pause / host menu | Freezes P2 too, with a "Host paused" overlay |
| P2's own menu | Local to P2; P2's character stands still and becomes invulnerable while it's open |
| Minigames (Struggle, jobs, Atlantica, Light Cycle, 100 Acre…) and Gummi Ship | P2 spectates Sora |
| Bosses with Sora-only mechanics | P2 fights as normal where it makes sense; spectates where the fight forces Sora alone |

**P2 knocked out.** Proposal: P2 goes **down** (lies on the ground, invulnerable) and gets back up after **20 seconds** with half HP. Sora using a Cure or item near them revives them early. P2 never gets a game over. If **Sora** dies, it's the normal game over on your PC, and P2's game follows your Retry/Continue.

**Progression and EXP** (your brief asked for proposals):
- **Where P2's data lives:** in P2's own co-op save on P2's PC (Option A: their main-character stats *are* P2's stats). On your PC, P2's level/HP shown on the puppet are just copies streamed during the session. Story flags always come from your save.
- **Shared EXP rule:** when an enemy dies on your PC, and P2 was in the same room at that moment, P2's game runs the same enemy death natively. P2 gets exactly the EXP KH2 would give for that enemy, with P2's own EXP-affecting abilities applied. No custom EXP math, so it stays faithful to the game. Later: switch "same room" to "within X metres" or "dealt damage recently".

---

## 7. Tools we'll use (plain language)

| Tool | What it's for | Who runs it |
|---|---|---|
| OpenKH Mods Manager | Packages the mod's file changes (Roxas model on the puppet slot, room tweaks) | Already installed; you enable the mod |
| LuaBackend scripts | Quick memory experiments, simple per-frame rules | Me (scripts), you (play) |
| C++ DLL (Visual Studio Build Tools + CMake + MinHook) | Hooks, puppet driving, networking | Me |
| Cheat Engine | Finding and verifying memory addresses live (the main way we confirm, never guess) | You install it; I guide or drive it |
| Ghidra | Reading the game's code to find functions | Me |
| Tailscale | Private network between your PC and your friend's | You and your friend install it |

Every confirmed address or format goes in `VERIFIED_OFFSETS.md` with how it was found.

---

## 8. Immediate next steps (after your answers)
1. Confirm your exe matches Volpestyle's target build (game running, I read a few code bytes at a documented location).
2. Install the build toolchain (Visual Studio Build Tools, CMake) and Cheat Engine, with your OK.
3. First slice: an empty DLL that loads into KH2 and writes "hello" to a log. Then a Lua script that reads Sora's HP and room, so we prove we can see the game.

## 9. Feature ideas for later (from the user)
- **Separate co-op saves, marked "Co-op"** (2026-10-05). Idea: in co-op mode the save guard already sends every save write to a sandbox. Make that sandbox a permanent co-op save file (its own 99 slots), read back at the next co-op start, so single-player and co-op progress never mix and the real save is never touched. A copy of the player's own container stays valid (same Steam account). Label: the load/save screen text ("Cargar", the slot's location line) comes from the game's message files, so in co-op mode the DLL could patch that text in memory to show "Co-op". Fits Phase 4 (progression). Open: should a new co-op file start as a copy of the single-player saves, or empty?
