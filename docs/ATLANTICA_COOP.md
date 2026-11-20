# Atlantica in co-op

Your design for TODO 5.27 (2026-11-20), then what the research found, what went wrong in the first two-PC
run, the plan, and the questions it raises, with your answers as they come. Answered decisions also go into
`docs/DECISIONS.md` (the Atlantica row). Game facts carry their numbers in `VERIFIED_OFFSETS.md`: an **F**
number is confirmed on this PC, an **L** number is still a lead (Ghidra, the game's files or one crash).

## Your design (2026-11-20)

Atlantica has no combat: its story is six rhythm songs. For each song, in sync with the friend's game:

1. **An extra HUD** shows the other Sora's song HUD.
2. **Each Good / Great / Bad** gets an animation on the actors.
3. **Each player presses for their own Sora** (your pick 'Each plays own Sora'): the friend's game only plays
   the friend's own hits on its Sora; the host keeps authority over the other actors (Sebastian, Flounder,
   Ariel...).
4. **Timing matters most:** both games must start each song at exactly the same moment.

Your other picks: Atlantica moves out of Parked into P1; **the host's score decides pass or fail, but the
friend's presses can affect the host's score.**

## How the game runs a song

From the first run's logs, the game's files (read with OpenKH) and the exe (Ghidra), 2026-11-20.

### The six songs (L1998)

| Song | Room / program | Mission | Event | Prompt commands | To pass |
|---|---|---|---|---|---|
| Sebastian's practice | 0x0B/0x02, 63 | `LM02_MS102` | `lm_event_900` | 7 | score above 0 |
| Swim This Way | 0x0B/0x04, 64 | `LM04_MS103` | 901 | 42 | the miss meter never full |
| Part of Your World | 0x0B/0x01, 51 | `LM01_MS201` | 902 | 35 | a combo of 5 |
| Under the Sea | 0x0B/0x03, 53 | `LM03_MS301` | 903 | 38 | 6000 points |
| Ursula's Revenge | 0x0B/0x09, 65 | `LM09_MS401` | 904 | 25 | 8000 points |
| A New Day is Dawning | 0x0B/0x04, 55 | `LM04_MS501` | 905 | 59 | 30000 points |

`LM02_MS101` (programs 52) is the swim tutorial before the first song, not a song (F775, F778). Each song
also has a free-play version (programs 0x42-0x46) with no fail. Your English files and the friend's Spanish
ones hold the same charts.

### A song is a cutscene (L1993, F776)

- The chart is inside the song's own event: one **prompt** command per button prompt (when it shows, which
  button, its points, where it sits), and **section checks** through the song. No game script runs during a
  song, and no Sora is spawned (F780): the other player shows only as the cutscene twin, which today copies
  our own Sora's moves.
- At each section check the scene **jumps**: to the section's **OK** part if a prompt was hit since the last
  check, else to its **NG** part. Sebastian's "Dat's it!" and Sora's dance on a hit are the OK part; "Dat
  timing's a little off" is the NG part. Ariel, Sebastian, Flounder and Sora react together, once per section.
- **No animation per grade.** A grade shows as a sprite, **BAD**, **GOOD** or **EXCELLENT**, with its sound,
  on the prompt's object. Most prompts sit on a small marker object (`f_lm770`), one or two per song on Sora,
  a few on a character (Donald, Ariel...) (L2001).
- **Your Donald observation** (a Great and a Bad gave the same length): GOOD and EXCELLENT lead to the same OK
  part, and every OK part is as long as its NG part. Any mix of hits gives each song the same length (L2001).
  So it wasn't chance.

### Grades and score (L1995, L1996)

- Each prompt is judged once, from its own timer: a press in the last 3 frames up to the beat is
  **EXCELLENT** (points x1.5, combo +1, miss meter -1), a little earlier **GOOD** (points, combo back to 0),
  too early, late or missing **BAD** (combo 0, miss meter +1; in Under the Sea also minus its points).
- One prompt kind is a decoy (pressing it costs points), another a mash prompt (every press scores).
- At the end the game checks the song's rule (table above), shows CLEAR or FAILED, and the mission then plays
  the next scene (pass) or a fail scene (fail). Retry reloads the room; Quit likely leaves to 0x0B/0x02 (L1999).

### The clock (L1994)

- Prompts are timed on the **scene's frame count** (30 per second), not on the music. Each game runs its own.
- The clock waits until the song's music is ready, stops during loads and pauses, and likely during a dialog
  box. One run hints at that: the friend's first run of song 900 took about 2 minutes of unpaused play for
  2206 frames (74 s).
- A frame that takes too long (3+ screen refreshes) loses song time, and nothing pulls it back. Two PCs can
  drift apart in a long song.

## What went wrong in the first run (2026-11-20)

1. **A pause split the two songs** (TODO 5.29, F779, L2000). The friend's Retry from its pause was refused (only
   the host's counts) and closed only the friend's pause. Our pause sync could not close the host's pause with
   Start presses: this pause ignores Start. The host's song stayed paused 2 minutes while the friend's ran.
2. **The host's Retry didn't restart the friend** (TODO 1.214). The friend's game saw the same room and programs
   and stayed ("already there"); the Retry it was sent waits for a safe moment, and a playing song is never one.
3. **The friend's old ending crashed the host** (TODO 5.28, L1992). The friend's first run ended; scene sync takes
   the first game's ending for both, so it ended the host's retried song 61 s in. Starting the next scene while
   a song still plays crashes the game itself (the song's end routine waits on a missing task).
4. **Flounder's Talk didn't wait for the other player** (TODO 1.236, F778): the swim tutorial ends from its own
   script after the Talk, which scene sync treats as a fight's end, not as your interaction.

## What we already have

- **The song's start:** the load barrier releases both games' room load together, and the song's event starts
  with the load (about 35 ms apart in the first run). When each clock really starts (once each PC's music is
  ready) is not measured yet.
- **The other Sora** is already placed in every song as the cutscene twin.
- **The link:** per-frame packets, ordered reliable streams, and the clock offset between the two PCs.

## The plan

### Stage 0: general fixes first (each benched, then its own commit)

All five are general, not Atlantica-only. The first two also protect every fight:

- **A.** An ending never starts the next scene while an event still plays: it waits for that event to end. (5.28)
- **B.** An ending counts only for the try of the mission it came from (a Retry starts a new try). (5.28)
- **C.** The friend reloads too when the host reloads the same room for a Retry. (1.214)
- **D.** A pause with no Continue (a song's: only Retry and Quit, F781) pauses both games and stays open until
  the host picks: the friend's refused Retry / Quit no longer closes its pause, and pause sync stops pressing
  Start on it; the host's pick runs in both (with C). (5.29)
- **E.** A Talk after which the script ends the mission waits for both players. (1.236)

Bench: two game copies on this PC, a Retry in the middle of a song (`cup_retry --pause` passed only with no
scene playing).

### Stage 1: a log-only build (no change to play)

Both games log the song's frame and the link time at each prompt and each section check, every grade with its
points, the score, combo and miss meter, and each time the clock waits (music, dialog). One bench on two copies
confirms the leads above and measures how far apart the two songs really start and drift; a two-PC song then
measures real frame drops.

### Stage 2: the song sync (after your answers below)

- **Start:** each game holds its song's first frame until both games' music is ready, then both start at a
  time the host gives.
- **The end:** only the host's result counts (your pick). The friend's own CLEAR / FAILED doesn't end its
  mission; the host's end runs in both games.
- **Sections:** the friend's game takes the host's hit / miss result at each section check; if it comes over
  ~100 ms late, the friend's own counts for that section (answer 1).
- **Score:** each of the friend's grades sends its points; the host's game adds them to its score in the four
  point songs; the miss meter and the combo stay the host's own (answer 2).
- **The other's grades:** the game's BAD / GOOD / EXCELLENT sprite and its sound over the twin (answer 3).
- **Pause:** stage 0's D (answer 4).
- **The extra HUD:** the other Sora's score, combo and miss meter; its look later, with you.
- **Drift:** if stage 1 shows it, the friend's song frame follows the host's.

### Risks

- A prompt freezes if its object is hidden or missing (L1995): co-op must not hide or swap song objects.
- A section's result has 3-5 frames (100-170 ms) to cross between the PCs (L2001); a network spike beyond that
  needs a fallback (question 1).
- A dialog that waits for a press inside a song would let each player's timing differ: stage 1 checks it.

## Questions

1. **Whose hits pick each section's OK / NG part in the friend's game?** Sora's dance and Sebastian's reaction
   are in the same part, so "own Sora" and "the rest follow the host" meet here.
   - **The host's (my pick):** both screens show the same scene, as you asked for the other actors; but the
     friend's Sora dances, and Sebastian answers, to the host's hits. The friend's own grades still show.
     If the host's result is late (over ~100 ms), the friend's game uses its own for that section.
   - **Each game its own player's:** each player's Sora and Sebastian react to that player's presses; the two
     screens differ during the song but stay in time (L2001).
2. **How the friend's presses count in the host's score**, for each song's rule:
   - **Added points:** the friend's points add to the host's score (the four point songs); the miss meter
     (Swim This Way) and the combo (Part of Your World) stay the host's own.
   - **Every grade counts:** each of the friend's grades counts as if the host made it (points, combo, miss
     meter): a miss by either player breaks the combo and fills the meter.
3. **How the other player's grades show:** the game's own sprite and sound over the twin; the sprite only; or
   the sprite plus a short motion on the twin.
4. **The pause in a song:** a pause pauses both games and only the host's Continue resumes both (the friend's
   Retry / Quit stay refused); or the friend can't pause during a song.

## Answers

Your answers, 2026-11-20:

1. **Sections:** the host's.
2. **Score:** add the friend's points.
3. **The other's grades:** the sprite and its sound over the twin.
4. **Pause:** "Pause both. Songs, unless you install a separated mod that allows it, can only be Restarted or
   Quit after pause, so no 'Resume' exists." (F781)
