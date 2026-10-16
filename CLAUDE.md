## Research findings
- Every address, offset, function, struct layout or game rule found during research goes into `VERIFIED_OFFSETS.md` in the same session, as soon as it's found, not only in MODLOG.
- Confirmed live on this PC's build (seen working in a log, a hook, a bench or the game) → the table, with how it was verified and the date. Only from Ghidra, a crash, or another project → "Leads (not yet verified)". Move a lead into the table once it's confirmed; fix or remove an entry that turns out wrong.
- Before reporting a finding to me as done, check that it's in `VERIFIED_OFFSETS.md`.

## Git
- After each feature or bugfix that I have confirmed works in-game, commit it. "Builds without errors" does not count as confirmed.
- One commit per feature or fix. Check `git status` first and stage only the files that change touched. Never commit logs, save files or other generated files.
- Commit message: what changed, why, and how it was tested (for example "two-copy bench" or "tested with friend"), plus the MODLOG session number.
- Do not push until I say "push". Then push all commits made since the last push.

## Public repo
- The repo is public. Never write IP addresses, Steam IDs, real names, email addresses, Windows user names or `C:\Users\<name>` paths into tracked files or commit messages. Use placeholders such as `<host Radmin IP>`, `<SteamID64>`, `%USERPROFILE%`.
- Before each commit, check the staged diff for these. Logs, save files and the friend package (`dist/`) stay out of git as before.

## Testing: automatic or manual
- Before each test, decide whether it is best done automatically (my bench scripts on this PC: `tools/test_census.py`, `tools/test_extra_copy.py`, two game copies) or manually by you and the friend on two PCs. Tell you which and why, and ask before starting either.
- Automatic fits when the situation can be set up from a save or a script and the result can be read from the logs (counts, crashes, positions, who got what), with no judgment of how it looks or feels.
- Manual fits when it needs how it looks or feels, real player timing (pressing a reaction command at the right moment, dodging), two real PCs and their network, Steam, or a spot the bench can't reach.
- Some tests need both: a bench first to catch crashes and clear errors, then your manual check. Say so.
- Never start a bench or deploy while your game runs (check `tasklist` first); "while we test" means no bench and no deploy until you say you're done.

## Prior art
- Before coding a new feature or fix, check Volpestyle's repo (https://github.com/Volpestyle/kh2-multiplayer, mostly `docs/`) for hints on the same problem; treat what's there as leads until verified on this build.
- A local copy lives in `D:\tools\kh2-multiplayer` (outside this repo). At the start of every session, update it with `git -C D:/tools/kh2-multiplayer pull` before searching it. Only read and search it; never run code from it and never push to it.
