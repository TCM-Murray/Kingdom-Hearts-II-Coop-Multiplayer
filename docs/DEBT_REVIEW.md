# Debt review: the time windows, the network branch, and the DLL's shared foundations

Written 2026-11-06 from branch `network` at `ce73974` (the end of session 56), in a cloud session. I read the code
and the documents, built and ran the reliable channel's self-test (g++ on Linux, not MSVC), and made the counts
below with `grep` / `git grep` (section 5 has the commands). I didn't run the game or build the DLL.

It answers your question: "we are trying to tackle the project's debt: are we on the right track?" It follows the
session 54 debt review (MODLOG session 54; TODO 8.11 and 6.3) and builds on
[`CRASH_RISK_REVIEW.md`](CRASH_RISK_REVIEW.md) (2026-10-26).

**Short version: partly.** The work on `network` is the right fix for the debt session 54 found (time windows that
guess a game state), and it's done well. But session 54 measured debt narrowly: it looked for hack / FIXME /
workaround notes and timing constants. That missed the structural debt the crash risk review had already named,
which has grown since and is what the two open P0 crashes (1.113, 1.119) point at. Suggested order (section 4):
fix 8.6, then run the two-PC check of `network` as a per-change checklist; then make one mechanical pass that puts
the shared addresses and read helpers in one place; then build the liveness registry (crash review 5.3.4).

---

## 1. What's on track

- **Real events instead of guessed windows.** Sessions 55 and 56 moved one-off events onto the reliable channel:
  hits and heals (26a5e08), the friend's RC press (7d33dd0), the load barrier (b1641ca), scene sync (e314133),
  Mickey (e165403), skip (b69131e), the shared move (7b950b9), grabs (862ca7c), motion events (f98f0a7), world
  mirror calls (339a81d), hit claims (7105737), crowd knock-aways (900409f), companion gains (ea84414). Silence
  timers became long caps behind explicit end messages: Limit (969c51b), companion holds (cfcbc5c), grab (92a8feb),
  RC offer (fb74a32). The `shortcut:` comments went from 32 lines (at 26c3980, when 8.11 labeled them) to 24.
- **The reliable channel is built the right way.** `src/dll/reliable.hpp` is pure logic, with no sockets and no game
  reads, and `tests/reliable_test.cpp` runs it under heavy loss. Built here with g++ (`-std=c++20 -Wall -Wextra`,
  no warnings): **PASS (0 failed)**.
- **The risky work is on its own branch,** and `main` stays at the last state tested on two PCs.
- **An idea that didn't help was measured and reverted:** the round-trip resend wait (06b6a77, reverted in
  df23dc1). It wasn't kept just because it was built.

---

## 2. What the session 54 review missed

### 2.1 The same game addresses are copied into many files

No header holds the game's addresses. Each file declares the ones it uses, under its own name, with its own comment.

| Address | What it is | Files with it | Names used (`constexpr` declarations) |
|---|---|---|---|
| `exe+0x717008` | world, room, door, programs | 30 | `kNow` (26), `kNowRoom` (1) |
| `exe+0x2A171C8` | the actor list's head (our Sora first) | 24 | `kListHead` (15), `kPlayerActor` (4), `kSoraPtr` (2) |
| `exe+0x9BA8D0` | in the field (0 while a room loads) | 19 | `kInField` |
| `exe+0x4AD270` | the handle decode | 11 | `kDecodeHandle`, `kHandle` |
| `exe+0x2A23598` | Sora's stat slot (HP, max HP) | 10 | `kSoraSlot`, `kSoraHp` |

What it costs:
- A correction, or a game update that moves an address, means up to 30 edits, and a missed one is a silent wrong
  read.
- The names hide that two files mean the same thing: `kListHead` in `copy_blade.cpp` is `kPlayerActor` in
  `downed.cpp`.
- Each copy has its own comment, so what's known about an address is spread out (one file notes
  "next = handle +0xA90", another "list head = Sora").

### 2.2 Thirty private `Read<T>` helpers that don't agree

30 files define their own `template <typename T> T Read(std::uintptr_t)`. They have the same name and the same
signature, but two meanings. Almost none use the safe read built in session 45 (crash review 5.2.1):

| Takes | How it reads | Files |
|---|---|---|
| an exe offset (`ExeBase() +` inside) | raw dereference | 16: companion_sync, coop_save, downed, follow, fx_pool, game_over, limit_sync, link_menu, link_watch, mickey_sync, motion_mirror, pause_sync, skip_sync, story_sync, title_menu, world_census |
| a full address | raw dereference | 12: auto_limit, avatar_link, companion_heal, grab_sync, mission_sync, reaction_sync, scene_sync, shared_move, spawn_sync, stand_in, world_mirror, world_sync |
| a full address | `ReadAny` (the safe read) | 2: copy_blade, form_look |

`kh2coop.cpp` has a third helper, `ReadExe<T>(offset)`.

What it costs:
- **A copied line compiles and reads the wrong place.** `Read<std::uint8_t>(kNow)` is right in `downed.cpp`. In
  `world_sync.cpp` the same line reads address 0x717008 itself, which isn't mapped: a fault, caught only if a
  `__try` happens to be around it.
- **28 of 30 skip the safe read.** F335 says a fault in our DLL can end the game even inside `__try`. The safe read
  is the fix for that, and today it protects only the Keyblade and form look code.

### 2.3 The crash risk review's patterns have grown since

Section 3 of the crash risk review (2026-10-26) named four patterns, any of which "can produce the next family of
crashes":
1. game objects held across frames, each file with its own liveness rule;
2. `__try` as the guard;
3. the game's own sequences replayed by hand;
4. the host's script calls run in the friend's game.

The review's fix for pattern 1, **one liveness registry (5.3.4)**, isn't built. Section 7 ('Changed so far')
lists no work on 5.3.2 or 5.3.4.

| | At the crash review (b7dd11d, 2026-10-26) | Now (ce73974, 2026-11-05) |
|---|---|---|
| Lines in `src/dll` | 27,802 | 31,937 |
| `__try` blocks | 292 | 332 |
| Hook calls | 107 | 121 |

The two open P0 crashes belong to these patterns. The crash review files **1.113** under pattern 4 ("a crash
inside a pass-through script call, not yet understood"), and for **1.119** it names a dangling file pointer as
"the first family to suspect". So this debt sits on the P0 path, and nothing in the current plan pays it down.

### 2.4 The network branch is one big batch, not yet run on two PCs

`main..network` is 57 commits. 29 of them touch `src`, changing 25 files in `src` and `tests` (+1,027 / −265
lines). They touch hits, heals, RC presses and offers, the load barrier, scene sync, Mickey, skip, the shared
move, grabs, motion events, world mirror calls, Limits, companions and the crowd.

None of it has run on two PCs. Two commits haven't run on the bench either (TODO 'Suggested order', session 56):
the scene end (73d63c7: its new paths) and the RC offer number (55505c5: it needs a 1/237 boss). If the two-PC
check fails, the logs have to tell you which of about 14 changes caused it. Step 1 in section 4 is the checklist
for that.

### 2.5 TODO 8.6 matters more now

The link's build ID (`BuildId()` in `avatar_link.cpp`) is a hash of `__DATE__ " " __TIME__`: the moment
`avatar_link.cpp` was compiled. `tools/deploy.py` builds incrementally (`cmake --build build --config Release`),
so the ID changes only when `avatar_link.cpp`, or a header it includes, is recompiled. The README says "builds
that differ ignore each other". That holds only when `avatar_link.cpp` was rebuilt.

On `network`, `avatar_link.cpp` last changed in df23dc1 and `avatar_link.hpp` in 339a81d. The 12 `src` commits
after 339a81d (7105737 to 73d63c7) change only other `.cpp` files and no header. So a DLL built at 339a81d and
one built at 73d63c7 carry the same build ID and talk to each other without a warning.
- The modules' own `kVersion` checks catch a packet layout change only where someone bumped the version.
- A change in behaviour isn't caught at all: for example the scene end waiting for END, or the RC repeats held for
  a round trip.
- The friend package's name (the DLL's SHA-256, e.g. `1c10d36cab0d`) does differ, so a person comparing names can
  see the mismatch. The link can't.

The fix, two ways:
- **Stopgap, for the next friend package:** a clean rebuild, or touch `avatar_link.cpp`, before packaging.
- **Lasting:** take the ID from the DLL file itself at start-up, for example the same SHA-256 that
  `package_friend.py` names the package by. Then every code change changes the ID, and the log and the package
  name show the same 12 characters.

### 2.6 The documents' own cost

There's about 2.2 MB of Markdown: `MODLOG.md` 776 KB, `docs/VERIFIED_OFFSETS_HISTORY.md` 500 KB,
`docs/TODO_HISTORY.md` 370 KB, `VERIFIED_OFFSETS.md` 328 KB, `TODO.md` 82 KB. `tools/check_docs.py` warns that
`TODO.md` is 80 KB (budget 60) and `VERIFIED_OFFSETS.md` is 320 KB (budget 100). Since 2026-10-20, 51 of 241
commits touched only the docs, and about 5,100 Markdown lines were added against about 11,400 in `src`.

The evidence trail is worth having: it's how a new session picks up where the last one stopped. So this is the
smallest item. Trim to the budgets the checker already sets; no new process is needed.

---

## 3. Session 54's conclusion, revisited

Session 54 concluded "no cleanup project". That still holds: nothing here calls for a rewrite or a pause in
gameplay work. What changes is the definition of the debt. The time windows were part of it. The copied addresses,
the disagreeing read helpers and the crash patterns are the larger part, and they need a bounded pass (step 2
below), done one file per commit between gameplay fixes.

---

## 4. Suggested order

### Step 1: 8.6, then the two-PC check of `network`

1. **8.6 first,** before building the friend package for the check (section 2.5): at least the stopgap; the
   lasting fix when convenient.
2. **The check as a checklist,** one row per change, so a failure points at its commits. The channel commits
   (26a5e08, 065dc4a, df23dc1) underlie every row.

   | Area | Commits | Bench on this PC | What to see on two PCs |
   |---|---|---|---|
   | Hits and heals | 26a5e08, 7105737 | the lag baseline (`KH2COOP_NET_SIM`) | each hit and heal counted once, both ways |
   | RC presses and offers | 7d33dd0, fb74a32, 5685947, 55505c5 | `rc_dusk`, `rc_lords` | no double runs; the friend keeps an offer until the host ends it; a late press of the same offer runs (55505c5: a 1/237 boss, never benched) |
   | Load barrier | b1641ca | | both games come out of the black screen together |
   | Scene sync | e314133, 73d63c7 | `neg_group --scene` | walk-in scenes in step; the friend waits for the host's END, END_NO outside that fight (73d63c7: new paths never benched) |
   | Mickey | e165403 | `mickey_coop` | the winner's choice, the revive, Mickey down, once each |
   | Skip | b69131e | `skip` | the host's Skip Scene in both games |
   | Final Xemnas's shared move | 7b950b9, b743025, 9dd62e7 | `xend` | starts once; no clone dance on the friend's side (1.176 open) |
   | Grabs | 862ca7c, 92a8feb | | grab calls in order; the held stream ends on the host's let go |
   | Motion events | f98f0a7 | `limit` | Limit and RC motions, captures, spells, companions' hit effects |
   | World mirror and stream | 339a81d, e54acb9 | | the host's script calls in the friend's game; e54acb9 showed no measurable gain, so it can go if it gets in the way |
   | Limits | 969c51b | `limit` | the Limit ends on both screens |
   | Companions | cfcbc5c, ea84414, 231aaaa | `gain`, `heal` | holds, gains, and the heal hold by round trip |
   | Crowd | 900409f | | the 1000 Heartless knock-aways count once |

3. **Merge to `main` once it passes.** If one area fails, revert that area's commits on `network` rather than hold
   back the rest (the channel commits stay).

### Step 2: one mechanical pass on the foundations

1. **`src/dll/game.hpp`:** the shared addresses, declared once, with their VERIFIED_OFFSETS names and one comment
   each. For example:

   ```cpp
   namespace kh2coop::game {
   constexpr std::uintptr_t kNow = 0x717008;           // u8 world, room, door, pad, u16 map, btl, evt programs
   constexpr std::uintptr_t kActorList = 0x2A171C8;    // u64: the actor list's head, our Sora first; next = handle +0xA90
   constexpr std::uintptr_t kInField = 0x9BA8D0;       // u8: 0 while a room loads
   constexpr std::uintptr_t kDecodeHandle = 0x4AD270;  // (u32 handle) -> object or 0
   constexpr std::uintptr_t kSoraStats = 0x2A23598;    // u32 HP, u32 max HP (Donald's and Goofy's slots below)
   }
   ```

   Each file then drops its own copy. This is a pure rename with no change in behaviour, so a bench run is only a
   smoke check. Start with the five rows of section 2.1, and move the others as their files get touched anyway.
2. **Two read helpers in `common.hpp`, with names that say what they take:** `ReadExe<T>(offset)` and
   `ReadAt<T>(address)`, both going through `ReadAny` and returning `T{}` when a read fails (as `copy_blade`'s does
   today). Then each file's private `Read` goes, one file per commit. This one **does** change behaviour, so for
   each file:
   - a read that used to fault into a `__try` now returns 0, so check the callers that relied on the `__try` path;
   - watch the cost: inside a known area (the exe image, the file heap) `ReadAny` is a call and a compare; outside
     one, `Readable` makes a system call per read. `SafeReadFrame` counts and logs those, so compare the count on
     the per-frame paths (`world_sync`, `motion_mirror`, `reaction_sync`) before and after;
   - run that file's own bench scenarios.

   Do the 16 exe-offset files first: all their reads are of exe globals, inside a known area, so the change is cheap
   and safe. Then do the 12 full-address files, whose pointers go into actors: that's where the safe read matters
   most and costs most.
3. **What it buys:** the costs in sections 2.1 and 2.2 go away, and every later fix to a read lands once instead of
   in up to 30 places.

### Step 3: the liveness registry (crash review 5.3.4)

As the crash review describes it: a small table of the game objects the DLL holds (actor, weapon, effect, file
entry), each with the one rule that says it's still the same object. Every file consults it, instead of having
five private rules. Start in the Keyblade code, as the review says.

Then aim it at the two P0 crashes:
- **1.113** (pattern 4: the host's script calls on actors that may differ in the friend's game): it gives one place
  to check that an actor is the one the host meant before a pass-through call.
- **1.119** (a suspected dangling file pointer): it gives one place to see a file entry go.

---

## 5. How the numbers were counted

Run in Git Bash from the repository root, on `network` at `ce73974`.
- Addresses: `git grep -l 0x717008 -- src/dll | wc -l` (likewise for the other four).
- Their names: `git grep -h "constexpr std::uintptr_t k[A-Za-z]* = 0x2A171C8" -- src/dll`.
- The `Read` helpers: `git grep -n -A4 "^T Read(std::uintptr_t" -- src/dll`. A body with `ExeBase() +` takes an
  offset; one with `ReadAny` / `SafeRead` is safe.
- `__try` blocks: `cat src/dll/*.cpp | grep -c __try`. Lines: `cat src/dll/*.cpp src/dll/*.hpp src/dll/*.inc | wc -l`.
  At the crash review: the same over `git show b7dd11d:<file>` for each file in `git ls-tree -r --name-only b7dd11d src/dll`.
- Hook calls: lines with `HookFunction(`, `HookFunctionAlways(` or `MH_CreateHook(`, minus the two definitions in
  `kh2coop.cpp`.
- `shortcut:` comments: `git grep "shortcut:" 26c3980 -- src | wc -l`, then the same with `network`.
- Commits after the last build ID change: `git log --oneline 339a81d..network -- src`, and for each one
  `git show --name-only <commit> -- "src/dll/*.hpp"`.
- The branch: `git log --oneline main..network`, `git diff --stat main network -- src tests`.
- Docs: `py tools/check_docs.py`. Commits since 2026-10-20: `git log --since=2026-10-20 --format=%s`, with subjects
  starting "Docs" counted as docs only.
- The reliable test: `g++ -std=c++20 -Wall -Wextra tests/reliable_test.cpp -o reliable_test && ./reliable_test`, or
  the CMake target `reliable_test`.

These are text searches: a helper or an address written another way (`ExeBase()+0x717008` inline, a different
signature) is missed, so the counts are lower bounds.

---

## 6. Not covered

- No game run and no DLL build (MSVC on Windows only).
- **Threading:** `OnFrame` runs on the game's thread (Panacea). Two `shortcut:` comments in `crowd_sync.cpp` note
  data filled on one thread and read on another. Not reviewed here.
- **The tools:** `tools/` has 34 files, among them `test_extra_copy.py` at 4,185 lines. Not reviewed.
