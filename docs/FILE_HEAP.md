# The file heap of Kingdom Hearts II (PC)

How the game keeps loaded files in memory, in enough detail to read the DLL's loader hooks and the crash
notes. The evidence lives in `VERIFIED_OFFSETS.md` (the F and L numbers below) and `MODLOG.md`; this page
only joins it up. Addresses are `exe+offset` on the Steam build.

## 1. One fixed region, inherited from the PS2

Every file the game loads for a room (models, textures, motion sets, spawn files, Keyblades, forms) goes into
one 30 MB region, `exe+0xBF33C0` to `exe+0x29F33C0`, inside the exe's own `.data` section (section 2 of
VERIFIED_OFFSETS, row `exe+0x713000`). It is not Windows heap memory: it is a static buffer the PS2 game had,
kept as it was by the port, and it is mapped for the whole run. Three things do not live in it:

- Sounds (`.win32.scd`) go to ordinary `_aligned_malloc` memory (L154).
- Effect particles live in the effect pool, 512 KB of the game's own, 4 MB of ours (`fx_pool.cpp`, L55).
- Since the event block, a cutscene's work block and its 2 MB pool come from a buffer of ours (section 5).

The renderer and the object code reach any memory through handles: bit 31 set, bits 25-30 an index into a
table of 64 bases 32 MB apart (`exe+0x2B0D720`), bits 0-24 the offset (F3, F96). The encoder adds a base for
any new address, which is why memory outside the heap works without the game noticing.

## 2. The file list

One entry per file, in a list headed at `exe+0x29F33C8`, kept sorted by group: 0 and up ascending, then the
negative groups ascending (L141). An entry is a small record; the fields the DLL reads (L138, L141, L147):

| Field | Meaning |
|---|---|
| `+1` | state: 0 queued, 1 loading, 2 and 3 loaded (`exe+0x39DBB0` reads it) |
| `+2` | users |
| `+4` | the file's name (`obj/W_EX010_N0.mdlx` and so on) |
| `+0x44` | per-layout flags: bit 0 placed this layout, bit 1 to free; cleared on every entry at each layout (no pin bit, L151) |
| `+0x48` | group |
| `+0x50` | size |
| `+0x58` | the place the last layout gave |
| `+0x60` | where the data is now |

Asking for a file (`exe+0x3E0660`, then `exe+0x39E000(name, group, size)`) does not load it. Find-or-add
first reuses an entry of that name already in that group; otherwise it joins any listed file of that name
whose group is under 10000, leaving that entry's group as it is, unless the group asked for is 0x8FD or 5002,
where a second entry is made; a let-go entry of that name is taken back into the group asked (L138). The entry
is inserted after the files already in its group (`exe+0x39DD70`) and sits at state 0 until a file pass runs.

Joins cut both ways: a request of ours can land on the game's entry, and a later request of the game's can
land on ours (F348, F350). An actor's group is the group of the first listed entry of its model's name
(`exe+0x39DB20`, L152), which is how the host's Valor actor once took one of our groups (F351).

## 3. Groups are life cycles, and the layout puts them in order

A group number says when a file is let go, not where it sits. The drop `exe+0x39CCA0(from)` lets go of every
group `>= from` when `from` is 0 or more, and of every group `<= from` when `from` is negative (L148). A
let-go entry keeps its data and gets a group of 10000 or more: group + 10000 for most, 15400 + i for the 13
groups listed at `exe+0x749760`, 29000 for the two at `exe+0x749758`, 30000 for a negative group; one already
let go ages by 50 at each drop. Only the Drive's drop (`from` = 5000) frees the 30000 entries at once; the
rest are freed when a layout finds them past the live files.

The layout places every entry in list order, so in group order, each area starting where the files before it
end. Six areas reserve a fixed size, so what changes inside them does not move the files after them (L141,
L147; the sizes from Ghidra, still to verify on the bench):

| Groups | Area | Reserved |
|---|---|---|
| 0 to about 1000 | the room's own files (`.ard` spawns, map, enemies of the room) | the general area |
| 0x3F2 (1010) | a fixed area | 0xE0000 |
| 0x8FC-0x95F (2300-2399) | the player area; 2301 holds Sora's Keyblade files | `exe+0x29FB430` |
| 0x96A-0x973 (2410-2419) | a fixed area | `exe+0x29FB434` |
| 0x974-0x97D (2420-2429) | a fixed area (party members' files seen at 2421) | `exe+0x29FB438` |
| other groups under 5000 (2700 for new enemy types, for example) | the general area | |
| 5000-5099 | the Drive area: a form's files (5000-5002), a summon's (5000), the preloaded Limits' (5000) | 0x300000 |
| 5100-5199 | a second Drive area | 0xC0000 when set |
| negative | from the heap's end downwards, in list order; their bottom kept at `exe+0x29F33F0` | |
| 10000 and up | let go: freed when a layout reaches them | |

The game uses no negative group inside a room: none in 33 rooms over 62 saves (F345). It drops -200 at a
room's load (F347), so the range is its own, used elsewhere. Nothing checks the other areas against the
negative files' bottom (L147): the areas can run into them, which is why the mod keeps a budget.

What the census measured (F345): the room's placed files end 7.3 to 19.2 MB below the heap's end, depending on
the room; the Drive area's files after a form look held 2.2 to 3.1 MB, and up to 5.4 MB with the copy's looks
in it too, so how the 3 MB reservation behaves when passed is still open.

```
bottom                                                                                        top
| room 0-1000 | 1010 | 2300-2399 | 2410-2419 | 2420-2429 | 2700... | 5000-5099 | 5100-5199 |   free   | negative | blocks |
                                                                                                        ^ the end *(exe+0x749740)
```

## 4. A file pass: queue, place, move, read

`exe+0x39D460(task or 0)` is the only thing that moves or reads files (L17, L141, F312):

1. **The place step** `exe+0x39D590`: let-go entries go to the list's end, every entry is re-inserted, the
   per-layout flags are cleared, and each entry in list order gets the next place in its area. The new place
   goes to `+0x58`; the data is still at `+0x60`. Nothing has moved yet.
2. **The moves and reads** `exe+0x39E690`, one entry at a time in list order: a loaded file whose place
   changed is copied to its new place (`+0x60` follows); a queued one has its read started.
3. **The wait** `exe+0x39E340(task)`: with a task, the pass waits before the place step and after the moves,
   so a requester's code runs again only once everything is in place. With 0 it returns with every file
   already moved and nobody waited (F356).

So a file moves whenever an earlier file of its area changes: a new entry inserted before it, one let go and
freed, one taken back. The game fixes no pointer after a move. It seems to avoid the need by design: a live
actor's files never sit behind files that change in the same area, because the game's own actors in one area
are set up together. The mod broke that once by putting the copy's files (5050, 5060, 5099) behind the player's own
Drive files (5000) in the same area, so every Drive and Revert moved them (F312, properties A and C of the
crash review).

Who runs a pass, and when:

- The room load task, with a task (the load barrier hooks its two waits).
- Anything that asks for files and then calls the pass: a Drive's task (L11), a summon, an enemy spawn, the
  pause menu's Keyblade swap (which also frees the old weapon's group 2301 at once, `exe+0x39D010`, L139).
- The Limit preload `exe+0x3E7650`, every frame while no Drive runs, whenever a usable Limit's files are not
  all in: it drops from 5000, asks the Limits' files back and runs the pass with no task, so no wait (L155).
  It runs after every Revert, summon end and form end on a down; 8 times on the host in one evening (F356).

## 5. The top of the heap: a stack of blocks

The heap's end `*(exe+0x749740)` is not the region's top. Blocks are taken from the top as a stack (L150):
`exe+0x39D300(size)` rounds the size up to 0x80, lowers the end by it, pushes the size at
`exe+0x29FB400[--index]` (the index at `exe+0x749750`), marks every file under 10000 whose data reaches above
the new end as unloaded, frees the let-go ones there, and clears the block. `exe+0x39D400(block)` first ends
every resource object whose data lies in the block (`exe+0x12EF30`, the list at `exe+0x8AEF30`, L154), then
pops the top size and raises the end by it. The pop goes by index, not by the pointer, so blocks must come
back in order.

The blocks seen (F345, F347, F353, L153, L154):

| Block | Who | When |
|---|---|---|
| 0x400000 | the title | at the title |
| 0x700000 | the room load (`exe+0x152856`) | taken at the load, given back at the room's end (`exe+0x152B51`), just before the drop from 0 |
| 0x80000 | the room load (`exe+0x3E2470`) | within the load, given back at its end |
| up to 0x400000 | a cutscene's work block (`exe+0x2CDADC`): the event script's size plus 0x40000, capped at 4 MB, kept at `exe+0xB64F88` with a sub-allocator inside | taken by the event's setup task, given back at its end |
| 0x200000 | a cutscene's pool (`exe+0x2CF090`, `0x2CF220`), only for an event at the cap | taken after the block, given back before it |

Within a room the end therefore holds still, within 226 KB from one room to another, except while an event
plays: a summon's entry scene lowered it by 1.75 MB for six seconds, and a Data battle's intro would lower it by
3.2 MB (F346, F354). Anything placed from the end down, our negative groups included, is wiped the moment such a block is
taken, before any layout. The event block module serves those two blocks from a buffer of ours instead, so the
end no longer moves for events (the crash review, 5.2.6 step 4; F354).

## 6. A room's life, in order

1. The previous room's end: the room load's 0x700000 block comes back; the drop from 0 lets go of the room's
   own groups (`exe+0x152B58`, the room-ready byte at 0), and our drop hook lets go of ours before it.
2. The load: a drop from -200; the 0x700000 and 0x80000 blocks taken (the end dips by 0x80000 for a moment);
   the room's files asked and placed by the load task's passes, with waits.
3. Play: Drives, Reverts, summons and the Limit preload re-lay the Drive area; new enemy types add files to
   the general area; the pause menu swaps Keyblade files in 2301; events take and give back their blocks.
4. The room change: back to 1.

## 7. What this means for the DLL

- A raw pointer into the heap is good until the next pass. The copy's set (`+0x920`, `+0x928`, `+0x930`) and
  the motion controllers (`+0x158` to `+0x1B8`) are such pointers; the follow-up code fixes them after a move
  (form_look's `FollowMovedFiles`, from the load wait or the spawn step). Under option B nothing moves the
  copy's files, so it stays as a guard (0 moves in 22 logs, 2026-10-27/28).
- Ending a weapon between the place step and the moves crashes, because the release finds files by their new
  place (F334): copy_blade's place-pass hook points the set at `+0x58` first.
- Files of ours in a game area inherit that area's life: the Drive area's drop from 5000 would let them go
  (before option B a drop hook kept them); the pause menu's free of 2301 takes shared Keyblade files from the copy (F332).
- Since session 46 (option B, commit 3caa746): our files in negative groups at the heap's end, where no game
  area reaches them, under a budget taken from the free room, one look at a time, let go at the room's own drop;
  the event block keeps the end still; joins blocked both ways (the no-join rule, F350, F351); the game's own
  'any group' lookups get its own live entry before ours (F361). The Drive area hold above is gone.

Hooks the DLL has on this machinery today: the place pass and the place file (copy_blade, form_look), the load
wait (form_look, load_barrier), the drop and the group free (copy_blade, form_look), find-or-add and the
actor's group (form_look, test tooling), the top take and give-back (event_block).

## 8. Memory outside the region: what it has to respect

The region is small because the PS2 had 32 MB and no virtual memory. On PC nothing stops data from living
elsewhere, and two things of ours already do: the effect pool, 4 MB of ours in place of the game's 512 KB
since session 33, and the cutscene's work block and pool, served from a buffer of ours through 13 events on two
PCs without a crash (F354). The engine reaches both through handles, and the base table adds a slot for any new
address by itself. So "give the game Windows memory instead of its own" is not a rewrite of the allocator. It
is a question of which code still assumes the region. What is known:

- **The region's pipeline is the hard part, not the memory.** The list, the groups, the joins, the fixed areas
  and the compaction are what the loader, the Drive, the pause menu and the Limit preload walk. Replacing the
  region wholesale means re-validating every one of those paths. Moving chosen files out while the list and
  the groups stay as they are is a far smaller job.
- **The hook is one place.** The place step gives every entry its new place at `+0x58` before anything moves
  (section 4). A hook after it and before the moves, copy_blade's place-pass hook today, can rewrite the place
  of chosen entries to an address of ours; the copy step `exe+0x39E690` then moves or reads the file there,
  and nothing downstream reads the address against the region. The event's own files already load into the
  work block this way (`exe+0x2CED10`, L154), and its sounds into `_aligned_malloc` memory.
- **The address must lie below the region.** A block taken from the top marks every file whose data reaches
  above the new end as unloaded (L150). A file placed at an address above the region, where a plain
  `VirtualAlloc` lands, would be marked unloaded at every room load and every event. Below the region's bottom
  the rule never fires. Within 2 GB of the exe is cheap insurance against a 32-bit store, as the event block
  does; none has been found (the block pointer is 64-bit, L154).
- **The let-go path needs reading.** A freed entry is checked against the negative files' bottom before its
  memory is reused (L147). How an outside address compares there decides whether such a file is freed at the
  right moment, too early or never. Not traced yet.
- **The area still counts the file.** An entry placed outside still advances its area's cursor by its size,
  so the region gains no room unless the place step is also told the file takes none. That second patch is
  where the delicacy is: the size at `+0x50` is also what the read and the release use.
- **Handle slots are finite.** 64 bases of 32 MB, never cleared (`fx_pool.cpp`). Each region of ours takes
  one, two if it crosses a 32 MB line. Few regions, each on a 32 MB line, as the event block is allocated.
- **The sweep is by range.** A block's give-back ends the resource objects whose data lies in that block
  (`exe+0x12EF30`, L154). Data outside the block is untouched, so files outside the region are safe from it,
  and anything of ours that holds objects has to end them itself, as event_block does.

What it would buy this mod: the copy's look and its Keyblades outside the region would never move again, which
removes the class of crash behind F312 and F356 at the root rather than following the moves, and would need no
budget and no decline rule, since the buffer is as large as we like. The game's own files (Sora, the party, the
Drive area) are better left where they are: the pause menu's group free, the Drive area's logic and the Limit
preload walk them by place and group, and the room they would free is room our files would no longer need. The
no-join rule stays essential either way, so the game's entries keep their areas. The plan of record is the
negative region first (the crash review, 5.2.6), and this as the step after it, if the decline lines show the
ceiling reached in real fights; the hooks are the same.
