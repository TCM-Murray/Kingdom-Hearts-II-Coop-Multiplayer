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