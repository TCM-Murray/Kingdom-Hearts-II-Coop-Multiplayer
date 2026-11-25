# Plan: the world flags and puzzles in the story copy (temporary)

**Temporary.** TODO 5.32, researched in session 87 (2026-11-25), **not built**. Evidence: VERIFIED_OFFSETS L1549,
L2078-L2099 (all Ghidra or save-read leads until a bench confirms them), MODLOG session 87 (the save comparison, the
research workflow `wf_7bcc22b8-052`: 4 researchers and 4 skeptics, then the advisor). Delete this file in the last
step's commit.

## The problem
The friend's game copies the host's story areas before each of its room loads (`story_sync.cpp`, kAreas). The two
players' real co-op saves (compared with `tools/coop_save_diff.py`) match in those areas but differ in every pair in
the world-flag area `save+0x4038`-`0x41A0`, in the jigsaw pieces `+0x24A0`, in the save flags `+0x2400` and in 40 bytes
of story-script pairs past what the copy reaches (L2095).

## Decided (DECISIONS, 2026-11-25, your picks)
1. The host's **end state** of the world-flag area joins the story copy (not the setter in the friend's game: it reruns
   each flag's script and reads the caller's items and Drive gauge, L2085). The queue was to be left out.
2. Only story and world state is shared; Sora's build, level, EXP, items and munny stay per player.
3. Puzzles shared both ways, like chests: the jigsaw pieces and the room puzzles' save flags.

## What the research found (leads)
- **The queue is drained by the World Map** (L2090): at each World Map setup, likely after CommitLocation (where our
  copy is written), a task pops the queued ids and runs each one's ops (L2091), then plays each id's reveal event with
  the map locked. `exe+0x3DEEF0`, which I first named the drain, is a debug window (L2078).
- **Bits and queue are one pair** (L2078): a setter sets the bit and queues the id in one step, only while the bit is
  clear. So copying the host's bits without its queue means the friend's game never runs those ids itself, and the
  friend's own stale queue drains over the copied state at its next World Map visit (fixed again at its next load).
- **What the ops write** (L2091): ops 1-7 the world records (`+0x3FEC`, already copied; op 7 adds 1, so it can't run
  twice), 8-11 the 21 bytes at `+0x4158` (indexed arg - 1, L2082), 12/13 the episode bits `+0x4178` and their 'new'
  marks `+0x4188` (likely story state, L2079), 15 the World Map's DARK stage `+0x4199` (L2079).
- **`+0x419C` and `+0x419D` are per save** (L2092): the save writer sets them (a random pick, a world-4 flag). They
  are not story state: the copy should end at `+0x419C`, as the game's own save-copy block does (L2093).
- **The room-start snapshot** (L2093): the game copies the live save at room start and restores it on a retry, then
  runs CommitLocation again (our hook runs twice then).
- **Story-script pairs** (L2095): the copy's `{0x1EF0, 0xE0}` stops at `+0x1FCF`; the game's own array runs to
  `+0x22F7`. 40 differing bytes in the co-op saves are pairs of rooms past it (Hollow Bastion and others). A range bug
  in the existing story sync (TODO 5.1's areas): a general fix.
- **Jigsaw pieces** (L2096): bit p x 64 + i at `+0x24A0`, 0x50 bytes; a pickup gives no item (the crown leaves, a text
  shows). **The reward** (L2097) is per player: each player solves their own board (`+0x4E20`, done bits `+0x4E1C`),
  which no copy touches. So sharing pieces can't pay a reward twice or block it.
- **Save flags** (L1549): set / reset / check by script traps 0/57-59 at `+0x2400` (0x40 bytes). A reset (0/58) exists
  but never showed in our logs. Flags 37-42 are also read by unnamed scripts at session start, and 65 is set by an event
  script: the area isn't only room puzzles. Today only flag 66 differs (set in the host's game by F_AL030 in 07/0D).
- **Chests, a gap found on the way** (TODO 5.34): a chest the friend opens is resent to the host for 30 s, 16 at most;
  after that, or for a chest opened before the session, the friend's next copy closes it again and it can be opened a
  second time (a second item).

## Proposed changes to your picks (your word needed)
1. **Copy the host's queue too**, as one row `+0x3FEC`-`0x419C` (the world records plus the whole area): the only
   option where the friend ends at the host's exact state in both orders seen in the logs (the friend arriving with
   the host: same base, same queue, same ops and reveal events; arriving late: count 0 and the finished bytes). Its
   costs: the friend's own queued ids are dropped (both games raise most of them anyway); a late friend sees no reveal
   events. **Ask only after step 0** below: the order it rests on is inferred.
2. **End at `+0x419C`**, not `0x41A0` (per-save bytes, L2092).
3. **Pieces: union both ways, never clear.** The friend writes host OR own; the friend keeps sending every piece bit
   it has that the host's latest block lacks (no pending list, no give-up); the host ORs it in. The first copy gives
   the friend the host's 4 pieces and the host the friend's 8. A crown already spawned stays until the next load
   (picking it again shows the text again, no item).
4. **Save flags: held** until two facts are known: whether any script resets one (trap 0/58), and what flags 64-66
   gate in Agrabah (ask you how that puzzle pays). If a set flag skips a scene or a reward in the other game, a union
   costs that player the reward.
5. **Pairs to `+0x22F7`**: a general fix under the existing story sync decision, its own commit and bench.

## Steps
0. **Measure first (no DLL change; your OK for the bench).** Two copies on this PC, fixture below, both taken to the
   World Map; sample from both games' memory the count `+0x4157`, the queued ids and `+0x3FEC`-`0x419B` through the
   load and after both drains. Confirms (or not) that the drain runs after our copy, and whether the game opens the
   spliced file (stop on the log line 'started ... as a copy of your Steam save').
1. Pairs to `+0x22F7` (story_sync kAreas), bench: the H88/C93 fixture, the pairs equal after one load.
2. The world-flag row with the drift report (the friend's bits and queue read before the write: 'ours has, the
   host's lacks', ids not names) and a guard: a queued id or `+0x4199` missing from the friend's own tables would be a
   null read at its next World Map, so skip and log it. Bench: drift fixture, then a World Map visit with a pending id.
3. Pieces by union both ways. Bench: one pickup by the friend reaches the host and survives the next copy.
4. Save flags, after the facts in 4 above.
5. Chests (TODO 5.34) with the pieces' rule, if you want it.
Each step: its own commit, packet layout changes marked 'not two-PC tested' until a two-PC run.

## Bench fixture (designed, not run)
- Copy B can't load the friend's file on this PC (a per-account block, L2099), and today both copies load the host's
  save, so a bench shows no drift.
- Build `build/test_coop_saves/drift/A` and `drift/B` from `latest/` each run (an in-game save would write there):
  A = a copy of the host's file; B = the host's file with the friend's slot 93 over slot 88, re-signed (the CRC table:
  L2098; 98 of 98 records check offline).
- H88/C93, Underworld Entrance 06/03: copied areas equal, queues empty, world area 10 bytes apart (host-only flags 19,
  20, 27, 64; friend-only 29, 194). For a pending id: H87/C92 (the host's count 1, the friend's 0).
- `tools/launch_pair.py` needs a per-copy `KH2COOP_COOP_SAVE_DIR`; read the slot list with `bench.save_slots`
  (coop_saves.txt is stale); reuse `test_save_sweep.py`'s warp-and-follow; check with memory reads after one load and
  again after a second, not with an in-game save.
