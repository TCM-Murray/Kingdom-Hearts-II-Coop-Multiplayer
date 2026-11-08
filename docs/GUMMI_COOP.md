# World Map and Gummi Ship missions in co-op

Your design for TODO 5.2 (your notes of 2026-11-07, first written as `World Map - Gummi Ship missions.txt`),
then my analysis and the questions it raised, with your answers as they come. The decisions also go into
`docs/DECISIONS.md` (the 'World Map and Gummi' row) once answered. **Client** below = the friend's game.

## Your design

### 1. The key doors on the World Map

- Clicking one of the **key doors** that lead to a Gummi Ship mission opens the **World menu**. On "Proceed
  through X", an INFO bar message appears top left:
  - the player who started: "Waiting for the other player to start Gummi Ship mission";
  - the other player: "The Host/Client wants to start a Gummi Ship mission".
- Meanwhile the player who started keeps free roam (the World menu closes), as with Talk / Examine events.
- It is cancelled by going inside a world or by selecting a different Gummi Ship mission.

### 2. The Gummi menu

Both players move around the menu freely, but:

- **World Map:** an INFO bar message, "Host/Client wants to leave the Gummi Ship mission".
- **Gummi Editor:** the Gummi Garage opens. Both players have total freedom there and create and select their
  own Gummi ships.
- **Start:** only the Host can start a mission.
  - When the Client clicks Start:
    - Client: "Waiting for Host to select a mission". Circle cancels the wait.
    - Host: "Client ready for the Gummi Ship mission".
  - Only the Host selects the mission, and it starts only once both players are ready. When the Host selects
    a mission:
    - Client: "Host selected mission".
    - Host: "Waiting for Client to Start". Circle cancels the wait.

### 3. Ship selection and launch

- Once the mission is selected, both players are brought to the Gummi Ship selection, with total freedom:
  each picks any Gummi ship.
- After the pick, the game plays the selected ship going up to the launch area.
- Two buttons, **Start** and **Abort**:
  - both players must press Start; on Start, an INFO bar message: "Host/Client is ready!";
  - if both press Abort, the game goes back where the vanilla game goes.

## Analysis

### What exists today

- **Solo flights work** (F448): each player has their own ship on their own World Map and can fly missions
  alone; a cleared mission counts only in that save.
- **The friend stays with the host:** the door lock keeps the friend on the map while the host is there; when
  the host lands, follow pulls the friend into the host's room (`follow.cpp`).
- **The waiting pattern of section 1 exists:** Talk / Examine events (TODO 5.4, `scene_sync.cpp`) already
  hold the first player's choice, leave them free to move, and show 'Waiting for the other player to accept.'
  in the INFO bar until the other player picks the same event. The key door can reuse it.

### What needs research first

Nobody has looked at the Gummi menus yet. OpenKH only documents the mission map format (GBX) and the map's
ship object (`WM_CURSOR`, 929). Volpestyle only covers the flight's state (L193). Every step below needs the
place in the game's code where that choice is made, so we can hold it (wait) or apply the host's choice:

| Step | Needs |
|---|---|
| World menu "Proceed through X" | catch the choice and which route it is; hold it; let it go on the other player's pick |
| Gummi menu Start / World Map / Gummi Editor | catch Start and World Map; leave the Editor alone |
| Mission list (host) | catch the host's pick; in the client's game, skip its own list and use the host's mission |
| Ship selection, launch Start / Abort | hold Start until both are ready; send Abort |
| The flight | start both at once; then whatever your answers below ask for |

### Risks

- **The INFO bar exists on the World Map only.** Answered by the probe flight (2026-11-07, MODLOG session 58):
  the bar object is there on the World Map, and gone (0) from the Proceed until back on the map: the Gummi
  menus, the flight, its results and its pause. Its call writes into the object without checking it (L194),
  so section 1's key-door messages can use the INFO bar, but sections 2 and 3 (the Gummi menu, ship
  selection, launch) need another on-screen text (answer 19; leads: the `gm::` menus' own text, L200, L202).
- **Per-save data:** missions unlock per save, and the Garage's ships and the mission records are per save.
  Story sync copies parts of the host's save into the friend's before each room load. It must not touch the
  Gummi data (OpenKH's save layout doesn't name where it is; to check).
- **Two games, two flights:** the flight runs separately in each game. Anything shared inside it (seeing the
  other ship, shared enemies or score) would be a new sync for a mode we know nothing about yet.

### Not covered by the notes

The flight itself and its end; a player shot down or quitting through the flight's pause menu; one Abort and
one Start; what 'leave' does to the other player; missions one save hasn't unlocked; rewards; whether solo
flights stay possible. The questions below cover these.

## Questions and answers

Asked 2026-11-07. Answers are filled in as you give them.

1. **Inside the flight:** **a fully shared flight**: one flight like our field fights, shared enemies, the
   other's ship shown.
2. **One player finishes (or quits) first:** **wait, then map together**: the first waits on the results screen
   ('Waiting for the other player') until the other finishes, then both go back to the World Map together.
3. **A mission the Client's save hasn't unlocked:** **both must have it**: the Host can only select missions both
   saves have unlocked.
4. **Flying alone:** **yes, if the other declines**: a third button at launch, **"Stand back"**, next to Start
   and Abort; the one who stands back doesn't fly, the other flies alone.
5. **A ship shot down:** **out, the other goes on**: the downed player watches until the other finishes; the
   mission counts as cleared if the survivor clears it.
6. **Score and rewards:** **shared score, both rewarded**: one score for both; each save gets the full rewards
   and the route clear.
7. **The one who stands back:** **watches the flight** (a spectator of the other's flight).
8. **How to build it:** **in stages**: (1) the menus and a synced start, each player flying their own copy;
   (2) the other's ship shown; (3) shared enemies and score. Each stage playable by itself.
9. **Accepting at a key door:** **same door, same Proceed**: the other player goes to the same key door and
   picks 'Proceed through X' too, as with Talk / Examine events.
10. **Who can ask:** **either player** (replaces the earlier 'the friend can't start a mission the host hasn't
    started'); the Host still picks the mission in the Gummi menu.
11. **'World Map' in the Gummi menu:** **leaves alone**: the one leaving goes back to the map; the other stays in
    the Gummi menu but can't start a mission without them.
12. **One Abort, one Start at launch:** **Abort wins**: both go back where vanilla's Abort goes, with
    'Host/Client aborted'.
13. **Pause in flight:** **both pause**, as in the field; only the one who paused uses the pause menu.
14. **Both ships down, or Retry from the flight's pause menu:** a Retry from the pause **restarts the route for
    both** (vanilla Retry). Both ships down (changed 2026-11-08): **the Host picks for both** on the Game Over list:
    Retry, Gummi Menu or World Map.
15. **Quitting mid-flight ('Gummi Menu' or 'World Map' from the pause menu):** **counts as shot down**: the
    quitter is out and watches, the other goes on; both go back together at the end.
16. **'Stand back' in stage 1:** **not yet**: stage 1 has only Start and Abort; 'Stand back' comes with
    spectating.
17. **The Host lands while the Client is still in the Gummi menu:** **the Host can't land yet**.
18. **Clear and rewards in stage 1 (separate flights):** **clear shared, own rewards**: if either player clears,
    the route counts as cleared in both saves; score, rank and rewards from your own flight are yours.
19. **No INFO bar on the map or in the Gummi menus:** **find another text**: research a text box the game shows
    there and use it.
20. **This doc:** committed to the repo.
21. **A route with no mission list yet** (asked 2026-11-08; Start goes straight to ship selection there): **both
    press Start**, and once both have, both go to ship selection.
22. **Circle in ship selection, once the mission is chosen** (asked 2026-11-08): **like Abort**: both return to the
    Gummi top menu, with 'Host/Client aborted'.
23. **The list after a mission's results** (asked 2026-11-08; Retry, Gummi Menu, World Map, unknown when answer 2 was
    given): **the Host picks for both**: once both are done only the Host's list responds and the Client follows.
24. **The 'Gummi Battle Tips' window before each flight** (asked 2026-11-08; it waits for one Cross): **wait for both**:
    the first to press waits for the other, so both flights start at the same moment.

## What this means

### Earlier picks it replaces

- 'The friend can't start a Gummi mission the host hasn't started first' (2026-11-07): replaced by answers 9
  and 10, where either player asks and both must pick the same door.
- 'A friend mid-flight gets "The host landed in <world>"' (2026-11-07): no longer needed. Flights start
  together and end together (answer 2), the Host can't land while the Client is in the Gummi menu (17), and
  stage 1 has no Stand back (16), so the Host is never on the map while the Client flies.

### Stage 1: the menus and a synced start

1. **Key door:** either player's 'Proceed through X' waits, with the INFO bar messages and free roam. The other
   accepts by picking the same door. A landing (the Host's) or another route cancels it.
2. **Gummi menu:** each player is free. 'World Map' leaves alone, and the one still in the Gummi menu can't
   start. The Host can't land while the Client is in the Gummi menu. The Gummi Editor is free.
3. **Mission:** only the Host picks, and only among missions both saves have unlocked. The Client's Start means
   'ready' (Circle cancels). After the Host's pick, the Client's game uses the Host's mission (Circle cancels
   the Host's wait). On a route with no mission list yet, both press Start and that is the pick (answer 21).
4. **Ships and launch:** each picks their own ship. Both press Start, then both flights start together. One
   Abort sends both back where vanilla goes, with 'Host/Client aborted'. Circle in ship selection sends both to
   the Gummi top menu, with the same message (answer 22).
5. **Flight (each their own copy):**
   - one pause pauses both;
   - shot down, or quit through the pause menu, means out: that player waits;
   - a Retry from the pause restarts the route for both; both down, the Host picks for both (answer 14);
   - the first to finish waits on the results screen, then both go back to the map together.
6. **Clear and rewards:** if either player clears, both saves count the route. Score, rank and rewards are each
   player's own.

**State (2026-11-08):** items 1-6 pass the bench (`tools/test_gummi_door.py`, `tools/test_gummi_menu.py`,
`tools/test_gummi_flight.py`); no two-PC run yet. In the flight: nothing on screen says why a press is held (the
tips window, the Game Over and results lists have no text bar yet). The `tiny` screen (Teeny Ships, needs an Original Blueprint ship) hasn't been
seen. The bar fits about 38 letters, so the messages as built are: 'Waiting for Host to select a mission', 'Client
ready for the mission', 'Host selected mission', 'Waiting for Client to Start' (also on a route with no list),
'Host/Client is ready!', 'Host/Client aborted' (shown to the other player only), 'Host/Client wants to leave the
mission'. Added by me, yours to change: 'Host/Client isn't in the Gummi menu' on a refused Start, 'Host/Client hasn't
unlocked this mission' on a refused pick, and a Proceed goes straight in when the other player is already in that
route's Gummi menu.

### Stage 2: the other's ship shown, Stand back and watching

Stage 2 shows the other player's ship in your flight. 'Stand back' at launch arrives with it: the one standing
back watches the other's flight.

### Stage 3: one shared flight

Stage 3 shares the enemies and kills (each counted once). Score is shared and both saves get the full rewards.

### What the first research found (2026-11-07, Ghidra and the game's files, no game running)

All of these are leads until a run confirms them (VERIFIED_OFFSETS L195-L203).

- **A mission is a fixed script along a rail** (L196). Each route has one rail, a list of points, and each
  mission adds:
  - a timeline of triggers placed along that rail;
  - enemies that each belong to a trigger, at a fixed place and of a fixed kind.
- **The flight moves on time, not on input** (L201). One scroll object holds the progress along the rail and
  adds each frame's step to it. Two flights started together therefore meet the same enemies at the same
  moment, and the host's progress value is what the client's flight would follow. This is the main reason
  stage 3 looks doable.
- **The route and mission picks are two globals** (L197, L198):
  - the World Map's 'Proceed' sets the route;
  - the mission list's confirm sets the mission and moves the Gummi menu on to the next screen.

  Hooking those two places is the core of stage 1.
- **The Gummi menu is one state machine with 8 screens** (L202): top, mission list, ship and Teeny Ship
  selection, the launch (`gm::LASTCHECK`), the factory and two more selections. The launch screen keeps its
  steps in a few flag bits: the first confirm, Start with the ship's launch animation, then 'go', which closes
  the menu and starts the flight. Holding 'go' until both players pressed Start, and sending one player's
  Abort, would happen there.
- **Enemies appear by distance along the rail** (L203): the scroll turns its progress into a distance, and each
  trigger fires once that distance reaches it, creating the enemies tied to it. A route's three missions share
  the file's enemy list; two bytes per enemy give the lowest and highest mission it appears in. Two flights of
  the same mission at the same progress therefore have the same enemies alive.
- **Each save keeps its Gummi records** (L199): 64 bytes per mission at save `+0x100E0`, holding a flag, a
  rank and what looks like a high score. That place answers 'both must have it' (answer 3) and 'clear shared'
  (answer 18). Story sync doesn't touch it.
- **The game already has friendly ships in a flight** (L200): the classes `gb::FRIEND` and
  `gb::FRIEND_FORMATION`, likely the Teeny Ships (`tinyset`). The other player's ship in stage 2 may be one of
  them, much as the Sora copy is a real Sora.
- **The game names its own parts** (L200): `gm::MISSION`, `gm::SELECTBODY` and `gm::LASTCHECK` (likely the
  launch's Start / Abort) for the menus, and `worldmap::MessageWidget` for text on the World Map, a possible
  answer to question 19.

### What the second research found (2026-11-08, Ghidra, no game running)

Leads until a run confirms them (VERIFIED_OFFSETS L215-L218).

- **The key door** (L215, built, bench PASS): Proceed and cancel close the World menu the same way and differ
  in one number. A held Proceed ends as a cancel, so the player keeps free roam; when both picked the same route,
  each game runs its own Proceed.
- **The Gummi menu's top screen** (L216): Start goes straight to ship selection on a route with no mission list
  yet, so the Host's 'mission pick' only exists on routes that have one. World Map has a yes / no box first.
- **The mission list** (L217): only Mission 3 can be missing from a list, so 'both must have it' (answer 3) is one
  check, on Mission 3.
- **The launch** (L218): one function handles every confirm there, so holding Start until both are ready can
  happen in that function.
- **Holding a menu choice** (L219): a list's confirm only plays a sound, so refusing it leaves the list running;
  that covers the top menu's Start and the launch's Start / Abort. The mission list has its own input function
  (L221) to hold.
- **Circle to the top menu** (L220, answer 22): the screens are a simple stack, so stepping back can be made to
  land on its first entry, the top menu.

### Research before stage 1 code

Each of these must be found in the game ('Proceed', the mission pick, the launch's Start / Abort and the save
records have leads now):
- the World menu's 'Proceed through X' choice;
- the Gummi menu's Start and World Map;
- the Host's pick in the mission list, and how to apply it in the Client's game;
- each save's mission unlocks;
- ship selection's Start and Abort;
- the flight's start, pause, shot-down, Retry, quit and results;
- where a route's clear is kept in the save;
- whether story sync touches the Gummi data.

Also needed: a text box for the messages in the Gummi menus, where the INFO bar isn't (answer 19; the
World Map has it). **Lead (L206, Ghidra 2026-11-07, your pick: as native as possible):** the Gummi menu's own
help bar (`gm::GUMIMENUHELP`, the line that says e.g. 'Select a Mission Level to fly.'), whose text the game
sets by message ID through the same lookup our 'Connection lost' menu already answers (`link_menu.cpp`).
It exists on the `top`, `mission`, `factory`, `collection` and `tutorial` screens; ship selection (`body`,
`tiny`) and the launch (`lastcheck`) close it, so those need more research. From the probe flight, what the code can already read (VERIFIED_OFFSETS section 2): the
route at the Proceed (`exe+0x9A8788`), Gummi mode (IN_FIELD 0 in world `0F`), and the flight's clock and state
(`gb::SCROLL` `exe+0x729740`: progress per frame, state 1 starting, 2 flying, 3 goal; it isn't reset on
leaving, so read it only in Gummi mode). The mission global (`exe+0x716C00`) still needs a Mission 2 flight.
