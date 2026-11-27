# Plan: the shared Start menu (TODO 1.286, step 2)

**Temporary.** The build plan for your design of 2026-11-25 and your picks of 2026-11-26 (DECISIONS, the row 'The
shared pause, redesigned'). Step 1 is done: the menu lab (`link_menu.cpp` LinkMenuDebug, bench `menu_lab`, F936-F940).
Reviewed 2026-11-26 (session 90: four research agents and three skeptics, 7 agents; their Ghidra reads are leads
L2114, L2116, L2118, L2121-L2127); this is the second version. Nothing of step 2 is built. Delete this file in the last
step's commit.

## The rules (DECISIONS)
1. When either player opens the Start menu, the other game opens its own Start menu too.
2. Whoever leaves first gets PAUSE with 'Waiting for the Client.' (host) / 'Waiting for the Host.' (friend) and one
   item, 'Back to Menu'; Start does nothing there. It is queued inside the closing Start menu (no world frame). It stays
   until both are out; then both games resume.
3. Where the game itself refuses a Start menu, the other game opens none:
   - a fight, a cutscene, an RC or Limit, a minigame, downed: our PAUSE with the line and **no items** ('Held'), closed
     when the other player leaves their Start menu;
   - a shop, a save point's menu, the World Map, Gummi flight and menus: keep going; follow once that ends, if the
     other is still in their menu;
   - a load or a fade: only wait.
4. A player who taps the menu open and shut within the link delay waits on the PAUSE until the other closes the menu
   that popped up (rule 2 as is).
5. The fight and cutscene pauses stay as they are; the followed menu doesn't say who paused.

## What the game gives us
Measured (one copy, one calm room, F936-F940): `exe+0x2E4950(0, -1)` opens the Start menu like the pad's Start; a
PAUSE request over the Start menu queues (`exe+0x716B48` = 0x32) and opens at the Start menu's close; 'Back to Menu'
queues task 0 and opens the Start menu at the PAUSE's close; `exe+0x716884` read 2 at every frame's sample across both.
Leads the build must respect:
- **The Start watcher's gate (L2114):** the whole mask dword `exe+0xABB878` 0, `exe+0x9BA8D0` set, busy `exe+0x9006B0`,
  kBusy2 and kBusy3 0; then a pause list from `exe+0x3AD540` (fight, cutscene, mission, RC / Limit via `exe+0x3ABBB0`,
  L2115, L2126) wins over the Start menu, and a pending named request (`exe+0x2A0D628`) blocks both.
- **A dropped or lost PAUSE request breaks the game (L2121, L2122):** `exe+0x2E4C20` sets mask bit 0 and pushes the
  sound pause before the opener decides; dropped over a save point or shop (L2116), lost if kMenuLock, field state 0,
  the `exe+0x749830` remap, or a later `exe+0x151770` hits the queue. Then Start does nothing and the sound stays paused.
- **Task 8 is not only the Start menu (L2123):** an event's party select is task 8 too.
- **The done flag also rises without a close (L2124);** the close can be held (party swap).
- **Queue the Start menu as task 0, never 8 (L2125).**

## Phases and the packet
| Phase | Meaning |
|---|---|
| 0 Out | No Start menu of a session on our side. |
| 1 Menu | Our Start menu is open (own Start, followed, or 'Back to Menu'), or our PAUSE is queued but not yet open. |
| 2 Waiting | Our 'Back to Menu' PAUSE is open (kMenuList `exe+0xBEF540` = our list, byte 0x08). |
| 3 Held | Our no-item PAUSE is open for the other's menu (rule 3, first case). |

- **Origin.** Phase 1 counts only for a task-8 menu we know the origin of: the player's raw Start in the frames
  before the task went -1 -> 8 (`InputRawButtons`, as `g_startAt` does today) with `exe+0x744A50` == 0, our follow
  call, or the 'Back to Menu' latch. Any other task-8 menu (a party select) is phase 0, never followed or queued over.
- **Packet v3:** keep `menuSeq` as the per-change counter (accept >= the last one); `startMenu`'s byte becomes the
  phase; add `session` (u32). Version 3 (the build check already drops other builds).
- **Session:** our own open or 'Back to Menu' raises it to max(ours, theirs) + 1; a newer one is adopted on receipt.
  We follow only a session above the highest we have seen or raised (so a leader never follows its own session back).
  `PauseSyncOnPeerRestart` resets the peer's session and seq and our follow and leave flags.

## The moves (PauseSyncFrame, each frame)
- **Follow.** Their phase is 1 with a new session, ours is 0. Read our state, read-only (never call `exe+0x3AD540`, it
  can write):
  - *Held* (rule 3, first case): kBattle `exe+0x2A11404` set, a pause-list slot used (`exe+0x2A114A0` + 0x40 i), the
    `exe+0x749804` bit 25, scene state 3 (`exe+0xB65210`), a minigame, our downed flag, or an RC / Limit (the
    `exe+0x3ABBB0` tests read directly if 2a shows how): open our no-item PAUSE when MayOpen passes; phase 3.
  - *Keep going* (second case): a game menu busy with a task other than ours, kNow = World Map, Gummi: nothing now;
    the follow stays armed while their phase is 1 with that session.
  - *Wait* (third case): a load task (kLoadTask `exe+0x9BA928`), the fade, our own screen still closing, the pending
    list `exe+0x2A0D628`: try again next frame (no try count).
  - Otherwise, when the full watcher gate passes and kMenuLock is 0: `exe+0x2E4950(0, -1)` through a link_menu function;
    phase 1 once the task is 8. Cross, Circle and Start are hidden from the followed menu until released (the
    kPickWaitMs rule), and a close in that window isn't 'leave first'.
- **Leave first.** Ours 1 with origin, theirs 1, and our player closes (raw Start or Circle in the closing frames, not
  our own injected press): queue our PAUSE in the same tick, from PauseSyncFrame into link_menu (LinkWatchFrame runs
  first, so a screen want would come a frame late). When: menu scene state `exe+0x716A54` = 3 if 2a shows that one
  frame suffices; else the done flag with `exe+0x744A50` == 0 and `exe+0x744A54` == -1. Guards (L2121, L2122): task 8,
  field state 2, kMenuLock 0, `exe+0x749830` bit 2 clear, `exe+0x716B48` -1, one call per leave-first; never
  `exe+0x151770` while 0x32 is queued. Report phase 1 until our list is open, then 2.
- **Lost queue.** We queued, then busy is 0 and our list isn't open: recover (mask bit 0 cleared with `exe+0x157260(0)`,
  one sound pop with `exe+0x2EB790`, if 2a proves both), log it with `exe+0xABB878` and `exe+0xAD803C`, open the PAUSE
  directly. A guard that fails at the close: open directly as well (a world frame or two pass; logged).
- **Quick tap (rule 4).** Ours 0 after leaving session s, theirs (1, s): open the PAUSE directly, logged; phase 2.
- **Both out.** Theirs isn't 1 and ours is 2 or 3: close our PAUSE (injected Start) only while our list is the open
  menu; a queued PAUSE no longer wanted is closed once it opens. Ours 1 and theirs not: nothing until our player leaves.
- **Back to Menu.** Only on the phase-2 PAUSE (Held has no item): queue task 0 with `exe+0x742DA4` = 1 (F938), set a
  'Back picked' latch, clear `g_up` and release the input block; PauseSyncFrame takes the latch: phase 1, a new session.
- **Link.** Keep their last phase until LinkLost() (not the 2 s kStaleMs); while lost, freeze the moves and let
  'Connection lost' cover; when it comes back, rerun with their fresh phase (re-follow the same session if ours went to
  0 because of the link). Continue Alone and Title Screen reset the state.
- **'Ours is open'** is always kMenuList = our list with byte 0x08 (the liveness proof of our PAUSE), never the menu byte
  alone: a 0xFF frame with field state 2 is a hand-over, not a close (it breaks today's `g_seen`, which would also send a
  false fight-pause epoch).

## Screens (link_menu, link_watch)
- Screen 3 keeps 'The Host paused.' for the unfollowed fight pause (rule 5) and gets 'Waiting for the Host.' for Held.
- A new screen 4, the 'Back to Menu' PAUSE, after screens 1 and 2 in link_watch's order; its list and texts move from the
  lab into BuildList / SetTexts. Today's Start-menu trigger of screen 3 (`g_remoteMenu`, `kStartMenuFrames`) goes.

## Debt and safety (CLAUDE.md)
- New reads through `ReadAny` and `game::` names; new addresses (`exe+0xABB878`, `exe+0x2A114A0`, `exe+0x2A0D628`,
  `exe+0x749804`, `exe+0x749830`, `exe+0x744A50`, `exe+0xBEF540` if not shared yet) into `game.hpp`; reuse kBattle,
  kLoadTask, kSceneState by their existing names. MayOpen exported; the two opener calls live in link_menu.
- Objects kept across frames: our PAUSE (proof above) and the followed Start menu (task 8 with our session's origin).
- Each frame count (the input window, any retry) gets a `shortcut:` comment.

## Steps
- **2a. The lab, more** (one copy, dev commands; the queue made from PauseSyncFrame's place in the tick):
  1. Leave first at scene 3: the PAUSE opens with field state 2; Sora's motion time unchanged across the hand-over
     (no world tick).
  2. Scene 3 and the done flag through a Circle close, rows 5 and 6 (likely Config), a sub-screen return, a party swap.
  3. Task 8, `exe+0x744A50` and the byte in Items, Equipment, the Journal; the origin test on the -1 -> 8 edge.
  4. The absent case: a PAUSE request forced to drop (over a save point's menu, or kMenuLock set at the close): the
     mask and sound stack before and after, then the recovery; Start must work again and the sound come back.
  5. The follow gate's reads in a fight (`battle on`), a cutscene, the World Map, a save point's menu, a shop.
- **0 (before 2b).** On your word, commit step 1 (the menu lab and `menu_lab`) on its own; 1.293's change stays out.
- **2b. The build** in pause_sync.cpp, link_menu.cpp / .hpp, link_watch.cpp, game.hpp.
- **2c. Benches (two copies, automatic):** `shared_pause` reworked: each player opens first and leaves first; 'Back to
  Menu' on each side; Held in a fight in each game; a follower in a cutscene, a save point's menu, the World Map, a
  load; both Start in one frame; a quick tap under `KH2COOP_NET_SIM` 300,100,0; 'Back to Menu' racing the other's
  leave; a forced lost queue; a 2.5 s and a 7 s link suspend; no false epoch and no 'closed without a choice' at a
  hand-over. Regressions: `pause_race`, the fight pause, the cutscene pause, Game Over, 'Connection lost',
  `test_gummi_flight`, `cup_retry`, `skip`, `menu_lab`. Every PASS also checks the mask and sound stack end at rest.
- **2d. Two PCs** with a new friend package: each opens first and leaves first; 'Back to Menu' on each side; one in a
  fight while the other opens; a quick tap; does the followed menu confuse (rule 5, your call); regressions: the
  shared fight pause, the Client's cutscene pause. Then the commit.
</content>
</invoke>
