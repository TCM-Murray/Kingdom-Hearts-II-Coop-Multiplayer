# Crash risk review: the copy's Keyblades and Drive Forms, and the DLL's weak spots

Written 2026-10-25 (after session 44), from the code as committed at 3e1872e and the crash history in `MODLOG.md`,
`TODO.md` and `VERIFIED_OFFSETS.md`. No game was run for this review. It answers two questions: which parts of the
mod are most likely to end the game, and what I would change to make them less so. The order of the fixes is my
recommendation; the picks are yours.

Revised 2026-10-26 with your notes: a check per read would cost frame time (a region check instead, section 4);
dropping the timers trades crash risk for Drive-area room, so it goes last; moving our files out of the Drive area
removes most rebuilds at the source, so its research starts now; the `blade_combat` bench as first written would
have opened the battle menu; and the root question, why a fault our code catches still ends the game, gets its own
section (4) and the biggest expected payoff.

Short version: the crashes that began with the other player's Drive Forms (TODO 1.24) and Keyblades (3.3, 1.110)
come from one mechanism, not from many bugs. The copy's body, Keyblade and second Keyblade are built by hand from
the game's own pieces, and they hold raw pointers into the game's file heap, which the game frees, moves and
re-lays whenever it likes (a Keyblade change, any Drive, a Revert, a room change). The game keeps its own weapons
consistent through bookkeeping spread over many functions; `copy_blade.cpp` and `form_look.cpp` re-do that
bookkeeping from the outside, one observed path at a time. Each crash so far was one more path the game takes
that our bookkeeping did not cover, and the fix for each was one more hook or one more rule. That works until the
next path, which is why the crashes keep coming. The way out is fewer, quieter rebuilds, one life cycle for both
weapons, and faults that stay ours: today a fault inside our DLL can end the game even inside `__try` (F335),
because the .NET runtime's handler sees it before our `__except` does. Section 4 says why, and what our own
handler can do about it; that is the one change that would protect the whole DLL at once.

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
other player in Valor. See fix 5.2.5.

**H. Faulting reads.** `Readable` is used 2 times in copy_blade and 0 times in form_look. Every walk of the actor
list reads `+0x158`, `+0x920..0x930`, `+0xA90` of every actor; every walk of the effect list reads `+0x40`,
`+0x48`, `+0x50`, `+0x74` of every effect and `+0x30` of each instance and `+0x10` of each definition; every walk
of the file list reads `+0x44..0x70` of every entry. All of that is inside the game's heaps and has never faulted
on the bench; but F335 means one fault is one lost game, and the two F335 crashes were reads of this kind. A
`VirtualQuery` before each of them is not the answer: it is a system call, and these walks read thousands of
pointers a frame. Section 4 has the two cheaper ones.

**I. The riskiest work at the riskiest instant.** `DropUnused` (let go of our group-5099 files no actor reads)
runs right after every build: every actor in the list, two weapon slots each, a `VirtualQuery` per slot. It is
memory hygiene, it is where both F335 faults were, and nothing needs it to run in that frame.

**J. Bookkeeping that outlives the room.** `g_wornEntry[]` kept the previous room's file-list entries after a room
change; a new room's file can take the same entry, and `HookPlaceFile` would then hard-stop that file's effects as
if the copy wore it. Fixed in commit b7dd11d (section 7). The same check for the rest: `g_wait[1]` and
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

## 4. Why a fault our code catches still ends the game, and what our handler could do

F335 is treated above as a rule to live by (never fault). It deserves a look at the root, because a fix there
would make all 290 `__try` blocks mean what they say, instead of protecting the DLL one read at a time.

**How Windows hands out a fault (x64).** The kernel passes a fault to ntdll's dispatcher on the faulting thread.
The dispatcher first calls every vectored exception handler (VEH) in list order (`AddVectoredExceptionHandler`
with the first flag puts a handler at the head), and only when all of them answer "continue search" does it walk
the stack's frame handlers, where `__try` / `__except` filters live. So no `__except` of ours can ever run before
the .NET runtime's VEH, whatever the order of the VEHs. One thing cuts the chain short: a VEH that answers
"continue execution" ends the dispatch there and then, no later VEH and no frame handler runs, and the thread
resumes from the CONTEXT the handler hands back, which it may change (Rip, Rax, anything).

**What that gives us.** Our crash logger is a VEH, and `KeepCrashLoggerFirst` puts it back at the head every 300
frames (puppet.cpp). It is therefore the only code of ours that runs before clr's handler, and the only place a
protection against clr can live. Three ways to use it, from cheap to heavy:

1. **A read the handler can recover (concrete, small).** Emit a tiny read routine into an executable page at
   init, with known bytes (no assembler file needed): for 8 bytes, `mov rax,[rcx]; mov [rdx],rax; mov eax,1; ret`,
   and a landing `xor eax,eax; ret`; the same for 1, 2 and 4 bytes, so a read never crosses the end of a page its
   caller did not ask about. Call it as `bool SafeRead(const void* at, T* out)`. In the VEH, before anything else:
   if the fault's Rip lies in that page, set Rip to the landing and answer "continue execution". On the success
   path the read costs a call and a compare, no system call; on a fault, the handler lands it on `false` and clr
   never hears of it. Every walk in copy_blade and form_look (actors, effects, files) can use it, which answers
   your frame-time point, and so can the other 28 files, read by read, as they are touched. The game's own faults
   (inside the exe) are not touched: they still end the game as before, with the same log. The four conditions
   below apply before any of this is relied on.
2. **A region check for reads that stay plain.** Where a read is not worth routing through the routine, the
   pointer can be checked against areas we already know are mapped: the file heap (`exe+0xBF33C0` to
   `exe+0x29F33C0`, fixed 30 MB), our effect pool (fx_pool.cpp knows its buffer), the actor memory (`exe+0x152430`'s
   pool; its bounds are not in VERIFIED_OFFSETS yet). A compare, no system call; `Readable` only for a pointer
   outside all of them. Cheaper than nothing to write, and good enough for the per-frame walks until they use 1.
3. **Generic recovery for `__try` from the VEH (research, heavy).** In principle our VEH could do for our frames
   what ntdll does after the VEHs: walk the stack with `RtlVirtualUnwind` from the faulting context, find the
   first frame of ours whose unwind data has a scope table (`__C_specific_handler`), run its filter, and if it
   says handle, set the context to the `__except` block's landing (the scope entry's jump target, the frame's
   registers from the virtual unwind) and answer "continue execution". The one thing it must not do is call
   `RtlUnwindEx` from inside the VEH: the dispatcher holds the VEH list's lock while it calls us, and unwinding
   out of it leaves that lock held for the next fault. Emulating the landing instead is a few hundred lines
   against structures that are known but not documented, and a mistake turns a recoverable fault into a corrupt
   stack. Worth it only if 1 is not enough; 1 covers the reads, and the reads are where our faults come from.

**Four conditions on the rescue** (your notes of 2026-10-26, each checked against the code or the Windows rules):

1. **The continue handlers.** Windows keeps a second list, the vectored continue handlers
   (`AddVectoredContinueHandler`), and the dispatcher calls them after a handler, vectored or frame, has answered
   "continue execution", before the thread resumes. So our "continue execution" does not keep everything else out:
   a continue handler of the runtime's would still run, with the context we set. I know of none that clr
   registers, but both lists are ntdll's private data, so the check is at run time: at `OnInit`, log whether
   clr.dll is already loaded (if it isn't, a hook on ntdll's `RtlAddVectoredContinueHandler` and
   `RtlAddVectoredExceptionHandler` would log every later registration with the module it comes from); and a test
   command that faults through the read routine a hundred times on the bench, with both games alive at the end,
   is the proof that the rescue works in this process. Until that passes, the rescue is a hope, not a fix.
2. **Our other first handlers.** The write watch (watch.cpp) and the cutscene trace (cutscene_twin.cpp) each add
   their own handler at the head when on (research switches, off by default); both take only single-step
   exceptions and pass everything else on, so they don't break the rescue, but they show the order is not ours to
   assume. The rescue therefore lives in its own handler of a dozen lines (one range compare, nothing else), put
   back at the head once a frame from `OnFrame` (a remove and an add; the list's lock is cheap), not every 300
   frames from `PuppetFrame`, and the logger stays a separate handler behind it. A fault on one of our own
   threads (the diagnostic panel, the watch helpers) in the window between the remove and the add only loses the
   rescue, as today.
3. **Stack guard pages.** A read that lands on another thread's stack guard page raises a guard-page violation
   (0x80000001), and the system has already cleared the guard by the time any handler runs: landing the read on
   `false` saves the game now, but that thread's stack can no longer grow past that page (common.hpp's note; the
   game has died on exactly that). So the area check comes first for any pointer that could point at a stack,
   which is any pointer outside the areas we know (the file heap, the actor memory, the effect pool, the exe):
   those go through `Readable`, which reports a guard page without touching it. The rescue read is for pointers
   inside known areas, where a fault means a bad offset, not a guard page. One more thing to test rather than
   rely on: on a 0x80000001 inside the routine the handler knows the page and could put the guard back
   (`VirtualProtect` with `PAGE_GUARD` added, which the system allows); whether that thread's own stack growth then
   behaves is a bench question.
4. **The heavy way stays shelved.** Way 3 rebuilds the catch-block logic by hand inside a handler, against
   structures Windows doesn't promise. It stays research until the measurements say the reads are not the
   problem; the one trigger to open it would be a crash with the fault in our DLL but not in a read (a call
   through a stale function pointer, a write), after the safe reads are in. Nothing before that.

**The next step after the rescue, parked (your idea, 2026-10-26): a wrapper in clr's place.** The registration
hook of condition 1 sees clr's call (F338: one exception handler, at the head, `clr.dll+0x11DC00`), with its function
pointer and the handle it gets back. It could register a thin wrapper instead, which calls clr's handler unchanged for
every fault except one with Rip inside our DLL, where it answers "continue search" itself. clr then never sees a fault
of ours, and the frame handlers, all 290 `__try` blocks, work again, with no re-implementation of SEH (way 3 stays
shelved): a healthy clr's own behaviour for a native fault, done on its behalf. It needs its own test and its own
thought about shutdown, when clr removes its handler by the handle we returned (the wrapper's). Opened once the
measurements of section 4 say how often faults outside the read routine still happen.

**Two cheap measurements first**, in the logger only, to learn why clr's handler dies at all: (a) for a fault in our
DLL, log the stack left on the thread (Rsp minus the TEB's stack limit): if it is small, the handler is dying of
depth where our hooks run (inside the loader's frames, plus our walks, plus our own VEH's buffers), and the fixes
are less stack in the hooks and `SetThreadStackGuarantee` on the game thread (today it is set only on the thread
that ran `OnInit`; two lines in `OnFrame`'s first call would cover the frame thread); (b) log every exception code
for a fault with Rip in our DLL or in clr.dll, not only the five the logger keeps today, with a per-thread depth
counter: a guard-page violation (0x80000001) or a re-entered handler would show whether clr's handler faults on
its own and recurses, which would make 1 the whole answer. `common.hpp`'s note already says clr has died on a
guard-page read of ours; (b) would say whether that is the usual way.

## 5. What I would fix, in order

### 5.1 Done in this commit (no risk: bookkeeping only)
- form_look forgets the worn files' list entries and the "moved" flag when the copy is gone or the room changes
  (property J). No game function is called; nothing changes while a copy is in the room.

### 5.2 Now: small changes, each one bench first, then two PCs; and one research track in parallel
1. **The two measurements of section 4** in the crash logger (log lines only), plus the clr.dll load check at
   `OnInit`, so the next crash says why clr died. Then **the recoverable read** (section 4, way 1) in `common.hpp`,
   with its own handler put back at the head each frame (condition 2), proven first by the test command of
   condition 1; used first by the walks in copy_blade and form_look, for pointers inside the known areas; the
   region check and `Readable` (way 2, condition 3) for every other pointer. This turns a lost game into a log
   line, at no frame time.
2. **Move `DropUnused` off the build.** Run it from the spawn step 2 s after a build, loader idle, copy quiet, and
   only when the Drive area is short of room (`LogDriveArea`'s free bytes under one form's size); else never. The
   files are let go at the next room change anyway. Removes property I.
3. **Rebuild only when the copy is quiet, unless forced.** An item change (a "KH2J" packet) waits until the copy
   plays an own-bank motion that is not an attack (150-199), is not held by a script (`CopyHeldByScript`), not
   grabbed, not downed, not in a Limit, not airborne, and has had no build in the last second. A forced end (a
   free or a move) still ends the Keyblade at once, as now, but the build after it waits for the same quiet moment
   (the copy goes a few frames without its Keyblade, as it already does between a forced end and the next spawn
   step). And in `Build`, when the copy's motion is from another bank or the copy wears a form look, set motion 0
   rather than the copy's id. This halves property B and removes the worst cases of it. Log each forced end that
   happens in a non-quiet state, with the motion, so a crash after one can be matched.
4. **Stop weapon 0's effects before its end**, as `EndSecondBlade` does for weapon 1 (`StopEffectsOf`), once a log
   shows `M reading > 0` before a crash, or at once if you prefer to act first: the sequence is the one weapon 1
   has passed the `formlook` and `blade` benches with. Property D, the half of it that matters most.
5. **`FollowMovedFiles` uses the actor-list liveness** (copy_blade's `Live`) instead of `PuppetCloneActor()`,
   and `HookLoadWait` keeps `g_wornMoved` set when it cannot act. Then a move during the pause menu is followed
   at the next spawn step. Property G; verify first with the `blade_menu_move` bench of section 6.
6. **Research, started now and run beside the above: a file group the Drive's layout does not move** (property
   C). Every Drive in either game is a rebuild today; a group the layout leaves in place would make rebuilds rare
   (a real item change, or a Keyblade freed by name), and fewer rebuilds means fewer chances for every other
   problem on this list. Steps: Ghidra on `exe+0x39D460`'s area table and on what `exe+0x39DC50` fixes at 3 MB;
   whether the room's own area, the from-the-end groups below `exe+0x29F33F0`, or a pinned group exists and how
   big it may grow; then a bench with Drives in both games counting `FilePlace` on our files. The change itself is
   5.3.1.

**5.2.6, step 4: the cutscene's work block (my call, 2026-10-26, session 45, after F353 and L153).** The summon's
move is the cutscene system's work block: taken from the heap's top by the event's setup task (`exe+0x2CDADC`),
sized from the event's script and capped at 4 MB, given back at the event's end. My call is option 1, the block
served from memory of ours, with the interception at the allocator rather than at the caller. Reasons:

- Option 2 (the look off while an event plays) is not only the plain-Sora cost. L150 wipes our files inside the
  same call that takes the block, so the look has to be off, and the copy drawn without the old files for the
  renderer's lag, before `exe+0x39D300` runs. The summon gives 0.5 s of warning (its drop from 5000); a boss scene
  (L65) gives whatever lies between `exe+0x2CC4B0` and the setup task's take, unmeasured and maybe a frame. So
  option 2 needs a hook on `exe+0x39D300` too, which is option 1's hook with the harder half left in.
- Option 3 (real B) stays the fallback if option 1 fails the benches below, not a thing to start now.
- The precedent is in the DLL already: `fx_pool.cpp` has given the game a 4 MB buffer of ours in place of its
  512 KB effect pool since session 33, outside the file heap, and the engine's handles take any pointer through
  the 64-slot base table (F3, `exe+0x4AD240`). The cutscene code is another subsystem, so the trace below is
  still owed, but memory outside the heap is not new to this engine.

The design, three rules that make it safe by construction:

1. **A wrapper on `exe+0x39D300(size)`.** When the return address is the cutscene's call site (`exe+0x2CDADC`'s,
   keyed as puppet.cpp keys callers), our buffer is free and `size` fits: round the size up to 0x80 and clear the
   buffer as the game does (it hands out a cleared block; the event code may count on zeros), mark the buffer
   out, log the size and the event, return the buffer. In every other case (another caller, the buffer already
   out, a size over the buffer): the game's own path, with a loud line when the caller was the cutscene's, since
   that is today's behaviour (our files wiped) and the bench must see it. The worst case of the interception is
   then today's behaviour, never a buffer handed out twice. Co-op off: pass through, as fx_pool does.
2. **A wrapper on `exe+0x39D400(block)`.** When the argument is our buffer: mark it free, skip the pop, log. Else
   the game's path. This covers every give-back path (the end `exe+0x2C85B0`, a skip, an abort, a room change
   mid-event) without listing them, as long as the cutscene code passes the pointer it was given. L150 says the
   pop goes by index, so the game may ignore the argument; the trace must show it is passed and is the stored
   pointer, not a null or a global cleared first. If it isn't, key on the give-back callers the xref finds
   instead, and log any pop while our buffer is out whose argument isn't ours.
3. **The balance in the log.** At each take and give-back of ours: the stack index `*(u32*)(exe+0x749750)` and
   the heap's end before and after. The bench asserts both equal before and after every event. With push and pop
   both skipped the stack never sees an event, so balance follows as long as every give-back of our buffer
   reaches rule 2, and the log says whether it did.

The buffer: 4 MB (L153's cap), `VirtualAlloc` once at init, committed, never freed (as the effect pool: a late
reader of the event's data after its end must not fault), on a 32 MB line so it takes one handle slot (fx_pool
counts them). A hint address inside 2 GB of the exe image is cheap insurance against a 32-bit store the trace
misses. Nothing else changes: the look stays on through events; the budget, the gate and the decline rule as
built.

The Ghidra hour before any bench, the three things that could sink it:

- **(a) The pointer's life.** Where `exe+0x2CCAB0` stores the block (a global, the event object), every reader of
  that store, and whether any reader narrows it to 32 bits, compares it with the heap's bounds (`exe+0x749740`,
  `exe+0x29F33F0`, the heap's base) or hands it to the file loader as a destination the loader checks against
  the heap; also whether anything reads the pushed size back from `exe+0x29FB400` by index. A 32-bit store: the
  2 GB hint and look again. A check against the heap's bounds is the one finding that ends the external buffer;
  then the in-heap variant below.
- **(b) The give-back.** Every caller of `exe+0x39D400` the cutscene code reaches (the end, the skip, the abort,
  the room change mid-event) and whether each passes the stored pointer; the three other writers of the size
  (`exe+0x2C86C0`, `exe+0x2CB380`, `exe+0x2D9AA0`): the same cap, the same take.
- **(c) Nesting.** Whether a second event's setup can run before the first's end (a summon's entry during a boss
  scene; a Limit, if it plays through `exe+0x2CC4B0`). One size global says probably not; rule 1's fall-through
  covers it either way, and the 1 MB block log, kept on for the Limit bench, will say whether a Limit takes a
  block at all.

The bench (both games, a look of ours in the negative region, the end and our bottom asserted still, rule 3's
balance asserted at every event):

1. The summon, the `neg_group` run that fails today: the entry event, its dismissal, a second summon after it.
   The scene must look right on screen (models, textures, voice), by eye.
2. A boss in-fight scene (L65): the Pete walk-in from save 11 and the tower, a Data battle's intro, a scene
   skipped with Start, and one that ends in a room change.
3. The fall-through, once, with the buffer forced small (an env switch for its size): the loud line and what the
   game does after, for information. The fall-through is a bug report, not a safety valve (5.3.5's wording).
4. Pass: no crash, no `fault in our DLL`, no `BUG:`, no MOVED line, the stack index and the heap's end equal
   before and after each event, ours all let go at the room's own drop. Then two PCs: the summon and a Data
   fight with a look in both games.

The in-heap variant, second choice, if (a) finds a bounds check: take one 4 MB block through the game's own
`exe+0x39D300` at the gate's moment (the room's files placed, before our first request), keep it for the room,
serve events from it (rules 1 and 2 as above, the buffer now inside the heap), and give it back through
`exe+0x39D400` just before the room's own 0x700000 give-back at `exe+0x152B51`: the pop is by index, so ours must
come off before the room's. The block is at a heap address, so no address question; the cost is 4 MB of the free
room in every room, so the tightest rooms (F345, 7.3 MB) decline every look. Hence second.

### 5.3 Later: design changes, each a session with research first
1. **Files out of the Drive area**, once 5.2.6 names the group: `kGroup` in copy_blade and `kCopyGroup` in
   form_look move there, `HookDrop` keeps or stops keeping them as the group needs, and the `blade`, `blade_swap`
   and `formlook` benches count ends and builds before and after.
2. **One life cycle for both weapons.** One `EndCopyWeapon(copy, slot, why, moving)` and one
   `BuildCopyWeapon(copy, slot, object, group)` in one file, used for weapon 0 and weapon 1. The end does
   everything weapon 1's does today (unlink, stop effects, point the set at the new places when moving, release,
   class end, remember until the free, clear the slot); the build does what `Build` and `BuildSecondBlade` both
   do, then tells form_look. Every future fix lands once. This is a refactor of code that works, so it needs the
   full bench set (`blade`, `blade_swap`, `formlook --auto --one`, `--mixed`, `companions --what form`) before and
   after.
3. **Follow moves instead of ending.** For weapon 0, record its set files in `g_wornEntry` too and let
   `FollowMovedFiles` fix its pointers after the moves (as it already does for the body and for weapon 1's
   controller); then `HookPlacePass`'s "moving" end, the one that runs between the pass and the moves (F334), is
   not needed, and the only forced end left is the group free, which has to be immediate. Property F. Less
   pressing once 5.3.1 makes moves rare.
4. **One liveness registry.** A small table of the game objects the DLL holds (actor, weapon, effect, file entry),
   each with the one rule that says it is still the same object (in the actor list and not going, for actors; the
   entry's name and place, for files), consulted by every file instead of five private rules. Pattern 1 of section
   3; the Keyblade code is the place to start it.
5. **Holds without caps, last, and watched.** Property E is a trade, not a pure win: the files of an ended
   Keyblade stay in the 3 MB Drive area until the game frees the weapon, and the copy's form look (about 2.5 MB)
   lives there too; a full area does not crash the game, it makes the next request give up after 15 s and the copy
   loses its look. So: keep a cap, but a long and loud one (minutes, with a log line that names the weapon; the
   game's delete has only ever been 750 ms late, so a cap that fires is a bug report, not a safety valve), and
   watch `LogDriveArea`'s free bytes over a session before deciding whether the cap can go.

## 6. Benches to add before the next two-PC test

The `blade` and `blade_swap` scenarios never fight, so property B is untested. Three scenarios would cover it. In
battle, Start opens the battle menu, not the pause menu (your note), so the host's game must stay out of battle in
every scenario that uses the host's menu: as `blade_swap` does, only the friend's game takes the forced battle
state (so the host's copy has its Keyblade out there), and the host swings at nothing.

- **`blade_combat`:** the rebuilds are forced by the friend's Drives and Reverts (each moves the Drive area, so the
  friend's game ends and rebuilds the host's copy's Keyblade), not by the host's menu, while the host attacks the
  air without a target and the friend's game is in the forced battle state; two minutes, then the same with the
  host grabbed (Data Roxas's steal), downed, and in a Limit. A menu variant: the host closes its menu and presses
  Attack in the same second, so the packet's rebuild lands in the swing.
- **`blade_drive_storm`:** both players Drive and Revert at random for five minutes with the copy moving; count
  ends and builds in the log and assert no `fault in our DLL` line and no crash.
- **`blade_menu_move`:** the friend in Valor; the host, out of battle, swaps to the Keyblade with the biggest
  files; assert `FilePlace` on the worn files either did not happen or was followed by `the look goes on again`.

A pass is a run with no crash, no `fault in our DLL` line, no `given up`, no `not seen freed`, and the Drive area's
free bytes back where they started.

## 7. Changed so far

- `src/dll/form_look.cpp` (commit b7dd11d): `FormLookFrame`'s room-gone branch also clears `g_wornEntry[]`,
  `g_wornPlace[]` and `g_wornMoved` (property J). Nothing else in the DLL changed; the fixes of section 5 are
  proposals.
- `README.md`: this document in the documents list.
- 5.2.1 (session 45, not committed yet): the two measurements and the clr.dll check (`crash_log.cpp`); the safe read
  (`safe_read.cpp`, `common.hpp`) with the rescue handler re-added each frame, new copy first; the guard put back and
  logged as `BUG:`; `Read<T>` in copy_blade and form_look through `ReadAny`. Benches `saferead`, `blade`,
  `formlook --auto --one` PASS (MODLOG 45; F338-F341). Two-PC check still to do.
- 5.2.6 step 4 (session 45, 2026-10-26): my call on the cutscene's work block (option 1, the block served from a
  buffer of ours, the two wrappers and the balance check), in 5.2 after item 6, with the Ghidra hour and the bench
  before it. No code.

