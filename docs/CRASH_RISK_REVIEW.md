# Crash risk review: the copy's Keyblades and Drive Forms, and the DLL's weak spots

Written 2026-10-25 (after session 44), from the code as committed at 3e1872e and the crash history in `MODLOG.md`,
`TODO.md` and `VERIFIED_OFFSETS.md`. No game was run for this review. It answers two questions: which parts of the
mod are most likely to end the game, and what I would change to make them less so. The order of the fixes is my
recommendation; the picks are yours.

Short version: the crashes that began with the other player's Drive Forms (TODO 1.24) and Keyblades (3.3, 1.110)
come from one mechanism, not from many bugs. The copy's body, Keyblade and second Keyblade are built by hand from
the game's own pieces, and they hold raw pointers into the game's file heap, which the game frees, moves and
re-lays whenever it likes (a Keyblade change, any Drive, a Revert, a room change). The game keeps its own weapons
consistent through bookkeeping spread over many functions; `copy_blade.cpp` and `form_look.cpp` re-do that
bookkeeping from the outside, one observed path at a time. Each crash so far was one more path the game takes
that our bookkeeping did not cover, and the fix for each was one more hook or one more rule. That works until the
next path, which is why the crashes keep coming. The way out is fewer, quieter rebuilds, one life cycle for both
weapons, and no faulting reads at all (a fault inside our DLL can end the game even inside `__try`, F335).

## 1. The crash history of this mechanism

Every crash below came from the copy's look or Keyblades. Each got a fix; the fixes are right, but the list itself
is the finding: nine distinct crash sites in five days, all in one mechanism.

| When | Crash site | What happened | The fix made | Refs |
|---|---|---|---|---|
| 2026-10-21 22:10 | pure virtual call (abort) | the game's own Drive re-laid the Drive area; the copy's motion object had moved | `FilePlace` and `FileLoadWait` hooks: the look goes on again from the new places | form_look.cpp header |
| bench 23:03 (form_look.cpp comments; the date isn't in MODLOG) | `exe+0x4B0242` effect update | an effect read the old place of the second Keyblade's `.a.us` after it moved | effects reading a worn file are hard-stopped before the move | form_look `StopEffectsIn` |
| bench 23:13 (same) | `exe+0x3A6425` | the copy's motion change walked into its ended second Keyblade's controller, still linked | the controller is unlinked by hand before the end | form_look `EndSecondBlade` |
| bench 23:20 (same) | `exe+0x3C8F7E` | the ended Keyblade's teardown unlinked it a second time and walked off the list | its motion file pointer is cleared so the teardown skips the unlink | same |
| 2026-10-22 | `exe+0x3C859E` motion step | the pause menu's Keyblade swap freed group 2301, which the copy's Keyblade shared with our Sora | the copy gets its own Keyblade files (group 5099); `FileGroupFree` hook ends its Keyblade before a free | 1.110, F332, F333 |
| 2026-10-25 16:05 | `exe+0x3C7B89` | the game deleted an ended second Keyblade 750 ms after its end, after our 500 ms hold had let its files go | the files stay until `exe+0x3B4E80` ran on it (`ActorFree` hook), 30 s cap | 1.123, L137 |
| 2026-10-25 bench 17:15 | `exe+0x3B43FE` | the delete pass freed the ended Keyblade's model instance after its files went | same hold | 1.123 |
| 2026-10-25 bench 21:2x | `exe+0x39DA55` | the Keyblade was ended between the layout's place pass and its moves; the release by place found no entry and wrote through null | the weapon's set is pointed at the new places first | F334 |
| 2026-10-25 21:59 (friend), 23:12 (host) | clr.dll stack overflow, after `kh2coop.dll+0x4A729` | a guarded read of a weapon slot on a short actor faulted; the .NET runtime's handler died before our `__except` ran | `Readable` (VirtualQuery) before those reads | F335, F336 |

Two things stand out. First, five of the nine are about a pointer that was right when we took it and wrong a few
frames later (freed, moved, torn down, unlinked). Second, the last one shows that our usual safety net, `__try`,
is not one: any fault in our DLL can end the game.

## 2. How the mechanism works, and where it is fragile

`copy_blade.cpp` (the Keyblade) and `form_look.cpp` (the form body and its second Keyblade) run from three places:

- the pose packet each frame (`FormLookFrame`, from `PuppetFrame`): picks the look the copy should have;
- the game's spawn task each frame (`HookSpawnTask` in spawn_sync.cpp, `exe+0x3A4F90`, game thread):
  `CopyBladeSpawnStep` then `FormLookSpawnStep` carry out ends, builds, file requests and drops;
- six hooks inside the game's file loader and actor delete: `FileGroupFree` (`exe+0x39D010`), `FilePlacePass`
  (`exe+0x39D590`), `FileDrop` (`exe+0x39CCA0`), `FilePlace` (`exe+0x39E690`), `FileLoadWait` (`exe+0x39E340`),
  `ActorFree` (`exe+0x3B4E80`).

The fragile properties, each with where it bites:

**A. Raw pointers into a heap the game re-lays.** The copy's set (`copy+0x920/0x928/0x930`), both weapons' sets,
three motion controllers (`+0x158`, with pointers at `+0`, `+0x18`, `+0x20`), the render instance, and every live
effect's definition (`effect+0x48`) point into the 30 MB file heap. The game fixes its own pointers after a move by
name (`exe+0x12DF90`) and through its own structures; ours it does not know. form_look follows eight list entries
(`g_wornEntry[8]`) and shifts the controllers by hand (`ShiftControllers`); copy_blade instead ends the Keyblade
when a file it reads moves and builds it again afterwards. Anything not on those lists (an effect started between
the record and the move, a pointer at an offset we do not know) is a dangling pointer after the next layout.

**B. Rebuilds happen at any moment of play.** The game swaps a Keyblade only in the pause menu, with every actor
update stopped. Our end-and-build (`EndBlade` then `Build`, `exe+0x3EFF20` then `exe+0x3EF750`) runs inside the
spawn task, whenever the other player's packet arrives or a file moves: mid-combo, during a grab, in a Limit or a
reaction command (a motion from another bank), while downed, with a trail effect live. `Build` then sets the new
weapon's motion to the copy's current motion id on the Keyblade's own motion file (`W_EX010.mset`), even when
that id belongs to another bank or to a form (when the copy wears a form look, the form's weapon motion file is
put on only afterwards by `FormLookBladeRebuilt`). The 21:59:48 crash was in the same millisecond as a build with
the Keyblade out (flags 0) and the copy moving; the bench never has that case because it never fights.

**C. Every Drive by either player forces a rebuild.** Our Keyblade files live in group 5099, in the Drive area
(5000-5099), which the game's own Drive re-lays every time (F334: at each Drive the group-5099 files had a new
place). So every Drive, Revert and form change in either game is an end-and-build of the copy's Keyblade, and
often of its second one. The `blade` bench counted 14 in one run. Each one is a B event.

**D. Two life cycles for two weapons.** Weapon 0 (copy_blade) is ended with the game's immediate destroy
`exe+0x3EFF20` and nothing else. Weapon 1 (form_look) is ended by hand in six steps: unlink its controller from
the copy's, stop every effect it owns, release its files by place, call the weapon class's end, remember it until
`ActorFree` ran, clear its motion file so the teardown does not unlink again. Two of those steps exist because a
crash showed the game needed them; weapon 0 does not get them. In particular weapon 0's effects (its trail, the
appear flash) are not stopped before its end, and the session 44 log line `before its end: ... N effects of its
own, M reading` was added to find out whether that is what killed the friend's game at 21:59:48. If a crash log
shows M > 0 there, the fix is already written in `EndSecondBlade`.

**E. Timers stand in for facts.** `kLoadTimeoutMs` (15 s, both files): after it, a look or a Keyblade is given up
and the state machine moves on. `kHoldCapMs` (30 s, form_look): after it, the files of an ended second Keyblade
are let go whether or not the game freed the weapon. `kEndedMax` (4): a fifth ended Keyblade pushes the oldest
out of the list and its files can go. The 1.123 crash came from a 500 ms version of the same cap. A cap that
expires leaves a dangling pointer with no log line near the crash that follows.

**F. Work inside the loader's own call.** `HookPlacePass` ends the Keyblade between the layout's place pass and its
moves (F334 is the crash that order produced; the fix depends on the loader's internals staying as they are).
`HookLoadWait` runs `FollowMovedFiles` inside the game's load wait, which can end and build the second Keyblade and
set the copy's motion, mid-load. Both are proven on the bench, and both are one game update away from the next
F334.

**G. Three liveness rules for one copy.** `PuppetCloneActor()` wants the copy updated in the last 2 frames, in
the same room, in the field, with a valid handle; copy_blade's `Copy()` wants it in the actor list and not going
(`exe+0x3BA720`), because the pause menu stops updates (F333); form_look compares `g_copyWas` with
`PuppetCloneActor()`. The three disagree in the pause menu, and `FollowMovedFiles` uses the strict one: a layout
during the menu (our own Keyblade swap, when the new Keyblade's files no longer fit the player area and the areas
after it move) that moves the files the copy wears is noted (`g_wornMoved`), then forgotten in `HookLoadWait`
because the copy is "not alive", and the look is never put on again from the new places. Whether the menu's swap
moves Drive-area files I have not verified; if it does, this is a crash waiting for the right Keyblade and the
other player in Valor. See fix 4.2.

**H. Faulting reads.** `Readable` is used 2 times in copy_blade and 0 times in form_look. Every walk of the actor
list reads `+0x158`, `+0x920..0x930`, `+0xA90` of every actor; every walk of the effect list reads `+0x40`,
`+0x48`, `+0x50`, `+0x74` of every effect and `+0x30` of each instance and `+0x10` of each definition; every walk
of the file list reads `+0x44..0x70` of every entry. All of that is inside the game's heaps and has never faulted
on the bench; but F335 means one fault is one lost game, and the two F335 crashes were reads of this kind.

**I. The riskiest work at the riskiest instant.** `DropUnused` (let go of our group-5099 files no actor reads)
runs right after every build: every actor in the list, two weapon slots each, a `VirtualQuery` per slot. It is
memory hygiene, it is where both F335 faults were, and nothing needs it to run in that frame.

**J. Bookkeeping that outlives the room.** `g_wornEntry[]` kept the previous room's file-list entries after a room
change; a new room's file can take the same entry, and `HookPlaceFile` would then hard-stop that file's effects as
if the copy wore it. Fixed in this commit (section 6). The same check for the rest: `g_wait[1]` and
`g_failedItem` in copy_blade are not reset for a new copy (harmless: a wait on an old object is given up or
satisfied by the same file); `g_owned[]` in form_look survives a room unload (harmless: the next `Drop` finds no
files and clears it).

## 3. The same patterns elsewhere in the DLL

The Keyblade code is where the crashes are now, but it is not unusual in how it is written. Four patterns run
through the DLL (108 hooks, about 290 `__try` blocks, 28 K lines), and any of them can produce the next family of
crashes:

1. **Game objects held across frames with a per-file liveness rule.** `g_clone` (puppet, five checks), `g_copy`
   (copy_blade), `g_copyWas` / `g_secondBlade` / `g_ended[]` (form_look), the bound enemies (world_sync), the
   grabber (grab_sync), the stand-in body (stand_in), the companion actors (companion_sync). The 2026-10-04 crash
   (`exe+0x3B5B2C`, the copy's handle read after its memory was reused) is the archetype, and 1.123 is the same
   thing for a weapon. Each file re-derives "is it still there" its own way.
2. **`__try` as the guard.** 290 blocks, most around reads of game memory. F335: a fault in our DLL can end the
   game before our handler runs, and it does not happen every time, so a bench pass proves little. `Readable`
   exists for this (5 files use it).
3. **The game's own sequences replayed by hand.** The Keyblade swap (copy_blade `Build`), the body init
   (form_look `Apply`), the chest open (story_sync), the grab calls (grab_sync), the actor factory with files
   preloaded by hand (spawn_sync `MakeUnmatched`, stand_in). Each copies the steps of one game function and misses
   the bookkeeping the game does elsewhere until a crash points at it (the owner slot an ended weapon's teardown
   clears; the controller link the end does not undo).
4. **The host's script calls run in the friend's game** (world_mirror "KH2V", grab_sync "KH2B"): game functions
   with the host's arguments on actors that may differ in the friend's game. 1.113 (a crash inside a pass-through
   script call, not yet understood) is this pattern.

Also worth watching: the budgets. The effect pool was the first (1.59, now 4 MB with a peak log); the actor count
is 1.96; the file heap is fixed at 30 MB and the Drive area at 3 MB, and the copy's form look (about 2.5 MB), its
two Keyblades and our own Drive all live in that area. `LogDriveArea` prints the free bytes; when a request does
not fit, the loader's answer is a "given up" path 15 s later, not an error. And 1.119 (the host's game ending
through `abort()` during a Limit, cause unknown): a pure virtual call from a motion object in moved memory is
exactly what form_look found on 2026-10-21, so a dangling file pointer is the first family to suspect there too.

## 4. What I would fix, in order

### 4.1 Done in this commit (no risk: bookkeeping only)
- form_look forgets the worn files' list entries and the "moved" flag when the copy is gone or the room changes
  (property J). No game function is called; nothing changes while a copy is in the room.

### 4.2 Next: small changes, each one bench first, then two PCs
1. **No faulting reads in the Keyblade paths.** A `SafeRead<T>(address, &out)` helper in `common.hpp` (`Readable`
   then the read; false on an unreadable page), and use it for every read of a pointer we did not get from the
   game this frame: the weapon slots and `+0xBF8` (already), the effect walks (`+0x40`, `+0x48`, the instance's
   `+0x30`, the definition's `+0x10`), the actor-list walks in `UsedByOthers` (both files) and the file-list
   reads in the hooks. Cost: a `VirtualQuery` per read, so keep the walks off the per-frame path (next point).
   This turns a lost game into a log line.
2. **Move `DropUnused` off the build.** Run it from the spawn step 2 s after a build, loader idle, copy quiet, and
   only when the Drive area is short of room (`LogDriveArea`'s free bytes under one form's size); else never. The
   files are let go at the next room change anyway. Removes property I.
3. **Rebuild only when the copy is quiet, unless forced.** An item change (a "KH2J" packet) waits until the copy
   plays an own-bank motion that is not an attack (150-199), is not held by a script (`CopyHeldByScript`), not
   grabbed, not downed, not in a Limit, not airborne, and has had no build in the last second. A forced end (a
   free or a move) still ends the Keyblade at once, as now, but the build after it waits for the same quiet moment
   (the copy goes a few frames without its Keyblade, as it already does between a forced end and the next spawn step). And in `Build`, when
   the copy's motion is from another bank or the copy wears a form look, set motion 0 rather than the copy's id.
   This halves property B and removes the worst cases of it. Log each forced end that happens in a non-quiet
   state, with the motion, so a crash after one can be matched.
4. **Stop weapon 0's effects before its end**, as `EndSecondBlade` does for weapon 1 (`StopEffectsOf`), once a log
   shows `M reading > 0` before a crash, or at once if you prefer to act first: the sequence is the one weapon 1
   has passed the `formlook` and `blade` benches with. Property D, the half of it that matters most.
5. **`FollowMovedFiles` uses the actor-list liveness** (copy_blade's `Live`) instead of `PuppetCloneActor()`,
   and `HookLoadWait` keeps `g_wornMoved` set when it cannot act. Then a move during the pause menu is followed
   at the next spawn step. Property G; verify first with the `blade` bench: the host swaps to a Keyblade with
   bigger files while the friend's copy is in Valor, and read whether `FilePlace` moved the worn files.
6. **Holds without caps.** Replace `kHoldCapMs` with "until `ActorFree` ran or the room changed", and raise
   `kEndedMax` to the most the log has ever shown plus a margin (or make it a small list that can grow). Property
   E. The cost is memory in the Drive area, which the free bytes log will show.

### 4.3 Later: design changes, each a session with research first
1. **One life cycle for both weapons.** One `EndCopyWeapon(copy, slot, why, moving)` and one
   `BuildCopyWeapon(copy, slot, object, group)` in one file, used for weapon 0 and weapon 1. The end does
   everything weapon 1's does today (unlink, stop effects, point the set at the new places when moving, release,
   class end, remember until the free, clear the slot); the build does what `Build` and `BuildSecondBlade` both
   do, then tells form_look. Every future fix lands once. This is a refactor of code that works, so it needs the
   full bench set (`blade`, `blade_swap`, `formlook --auto --one`, `--mixed`, `companions --what form`) before and
   after.
2. **Files in a group the Drive does not move.** Property C makes every Drive a rebuild. If the loader has a
   group the Drive's layout leaves in place (the room's own area, the from-the-end groups below `exe+0x29F33F0`,
   or a group it treats as pinned), asking for the copy's Keyblade and form files there would make rebuilds rare:
   only a real item change or a Keyblade freed by name. Research: Ghidra on `exe+0x39D460`'s area table and what
   `exe+0x39DC50` fixes at 3 MB; then a bench with Drives in both games counting `FilePlace` on our files.
3. **Follow moves instead of ending.** For weapon 0, record its set files in `g_wornEntry` too and let
   `FollowMovedFiles` fix its pointers after the moves (as it already does for the body and for weapon 1's
   controller); then `HookPlacePass`'s "moving" end, the one that runs between the pass and the moves (F334), is
   not needed, and the only forced end left is the group free, which has to be immediate. Property F.
4. **One liveness registry.** A small table of the game objects the DLL holds (actor, weapon, effect, file entry),
   each with the one rule that says it is still the same object (in the actor list and not going, for actors; the
   entry's name and place, for files), consulted by every file instead of five private rules. Pattern 1 of section
   3; the Keyblade code is the place to start it.

## 5. Benches to add before the next two-PC test

The `blade` and `blade_swap` scenarios never fight, so property B is untested. Three scenarios would cover it:

- **`blade_combat`:** the copy attacks (the other player holds Attack on an enemy from `spawn`) while the host
  changes Keyblade in the menu every 5 s for two minutes; then the same with the other player grabbed (Data
  Roxas's steal from the `xemnas`-style saves), downed, and in a Limit.
- **`blade_drive_storm`:** both players Drive and Revert at random for five minutes with the copy moving; count
  ends and builds in the log and assert no `fault in our DLL` line and no crash.
- **`blade_menu_move`:** the friend in Valor; the host swaps to the Keyblade with the biggest files; assert
  `FilePlace` on the worn files either did not happen or was followed by `the look goes on again`.

A pass is a run with no crash, no `fault in our DLL` line, no `given up`, no `not seen freed`, and the Drive area's
free bytes back where they started.

## 6. Changed in this commit

- `src/dll/form_look.cpp`: `FormLookFrame`'s room-gone branch also clears `g_wornEntry[]`, `g_wornPlace[]` and
  `g_wornMoved` (property J). Nothing else in the DLL changed; the fixes of section 4 are proposals.
- `README.md`: this document in the documents list.
