# Roxas model for the friend's Sora (research, session 39, 2026-10-21)

Your question: how hard would it be to show the friend's Sora as **Roxas**, in the friend's own game and as the Sora
copy in the host's game, while keeping **Sora's moveset**? Only the look changes.

**Status: parked until after the first public release (your call, 2026-10-21); the open questions below are decided
then.** Nothing here is built or tested in the game yet. File facts were read from this PC's OpenKH extraction and from the
community mods' files. Everything else is a lead.

## Short answer

- **Moves: easy.** The community "Roxas as Sora" models are built on **Sora's own skeleton** (checked below). They play
  Sora's motion file (`P_EX100.mset`) unchanged, so our motion mirror, combos, magic, Drive and Limits stay the same.
- **One Sora as Roxas and the other as Sora in the same game: medium.** Every community mod swaps Sora **for the whole
  game**: it replaces the `P_EX100*` files, so every Sora in that game turns into Roxas. We need it **per actor**. In
  the host's game, the host stays Sora and only the copy is Roxas. In the friend's game, the friend is Roxas and the
  host's copy stays Sora. That needs new object table rows that point to differently named model files, plus our DLL
  building the copy (or the player) from those rows.
- **Drive Forms and world costumes: medium to hard.** Sora has 43 model files: 6 forms × base, Halloween Town,
  Christmas Town, Space Paranoids and Timeless River, plus specials. Each needs a Roxas version and a row. The copy
  doesn't show Drive Forms yet anyway (TODO 1.24).
- **The real blocker is the model files, not the code.** None of the community Roxas repos has a license, which means
  "all rights reserved". We can't put their files in our public repo or our friend package without the authors'
  permission. Making our own Roxas model rigged to Sora's skeleton is Blender work, which is hard for a beginner.

## What the files say (checked 2026-10-21)

Bone counts were read with a small throwaway parser (not kept in the repo): it reads the BAR file, then the model entry
(type 4) and its header after 0x90 reserved bytes. Bone count is the u16 at header +0x10, the bone list offset the u32
at +0x14, and each 0x40-byte bone record has its parent id at +4.

| Model file | Bones | Same skeleton as Sora's `P_EX100.mdlx`? |
|---|---|---|
| `P_EX100.mdlx` (Sora, this PC) | 228 | (reference) |
| `P_EX100_BTLF` / `_NM` / `_TR` (Sora's Valor, Halloween Town, Space Paranoids) | 228 | yes, same parents |
| `P_EX110.mdlx` (the game's own Roxas, this PC) | 229 | **no**: parents differ from bone 8 on (96 of 228) |
| `P_EX110_BTLF.mdlx` (the game's own dual-wield Roxas) | 229 | no (same as P_EX110) |
| `P_EX120.mdlx` (Sora's prologue/event model) | 238 | no |
| GeminiHero "Roxas Complete" `P_EX100.mdlx` and `P_EX100_BTLF.mdlx` | 228 | **yes, identical parents** |
| Napstio "Roxas Mod 3.16" `P_EX100.mdlx` and `P_EX100_BTLF.mdlx` | 229 | Sora's 228 bones unchanged (0 differ) + 1 extra bone at the end |

So:
- **The game's own Roxas (`P_EX110`) can't just play Sora's moves.** Its skeleton is laid out differently, and Sora's
  animations would bend the wrong bones. Volpestyle's "Roxas kit" uses `P_EX110` with **Roxas's own** moveset for this
  reason. Its limits: no magic motions, and Drive / summon / Limit load Sora's forms over him
  (Volpestyle `docs/PLAYER_KITS.md`).
- **GeminiHero's Roxas models fit Sora's moves exactly.** Napstio's very probably do too, since the extra bone is left
  out of Sora's animations and stays still. Napstio's own "Sora animation (for speedrun)" add-on uses his Roxas model
  "as a simple skin" with Sora's animations, which supports this.
- **HD textures:** the PC port takes HD textures from `remastered/obj/<model file name>/-N.dds`
  (VERIFIED_OFFSETS row "Model files on PC").
  - GeminiHero's repo has **no** `remastered/` files: low-resolution PS2 textures next to the HD Sora.
  - Napstio's repo has 2307 `remastered/` files: HD.
  - A model under a new file name needs its texture folder copied under that name too.

## Community projects found (GitHub)

All are OpenKH Mods Manager mods (`mod.yml`), the same manager our friend package already uses. **None has a license
file.** I couldn't read the Nexus Mods pages (they return 403 to automated fetches), so their permission terms are
unknown.

| Repo | What it does | Fits our goal? |
|---|---|---|
| [GeminiHero/Roxas-Complete-OpenKH](https://github.com/GeminiHero/Roxas-Complete-OpenKH) (2021-04) | "A comprehensive **model swap** of Roxas for Sora, and Sora for Roxas." It replaces 43 `P_EX100*` model files (+ `.a.fm` face files, cutscene model `H_EX500*`, shop faces, title, text). **No `.mset`**: Sora's moves stay. Combines Napstio's "Full Playable Roxas", "Sora Replacement" and "Sora and Folks" (compiled by KoalToal) | **Closest match**: look only, Sora's skeleton. No HD textures. No license (readme: Napstio and Soraiko allowed the sharing) |
| [GeminiHero/Roxas-Complete-Coat-OpenKH](https://github.com/GeminiHero/Roxas-Complete-Coat-OpenKH) (2021-04) | Same with Roxas in the Organization coat | Same as above |
| [Napstio/Roxas-Mod](https://github.com/Napstio/Roxas-Mod) (v3.16, 2026-04) | "Roxas Mod" by TieuLink, Napstio, Mikote, Soraiko, Gemini: Roxas becomes the main character, with his **own animations** (replaces `P_EX100.mset`, `P_EX100_BTLF.mset`, Keyblade `W_EX010*.mset`), 358/2 Days command menu, music, text, voices, HD textures | Model yes (HD); the moveset change is **not** what we want, and its `P_EX100.mset` would clash with ours (we ship a patched one) |
| [Napstio/Roxas-Mod-Sora-Animation-For-Speed-Run](https://github.com/Napstio/Roxas-Mod-Sora-Animation-For-Speed-Run) | Add-on to Roxas Mod 3.x: "Restore Sora's animation for those who want Roxas as a simple skin without gameplay change" (ships Sora's `.mset` files) | Shows the Napstio model works with Sora's moves |
| [Napstio/Roxas-Coat-Mod](https://github.com/Napstio/Roxas-Coat-Mod) (2025-06) | Coat version of Roxas Mod | As Roxas Mod |
| [GeminiHero/Roxas-Mod-Rando](https://github.com/GeminiHero/Roxas-Mod-Rando) (2026-09), [Roxas-Mod-G-Lite-Animations](https://github.com/GeminiHero/Roxas-Mod-G-Lite-Animations), [Roxas-Mod-G-Medium-Animations](https://github.com/GeminiHero/Roxas-Mod-G-Medium-Animations) | Roxas Mod for the GoA Randomizer; "Lite"/"Medium" keep fewer of Roxas's own animations | Variants of the above |
| [camsPatience/KH2-RoxasRFScriptFix](https://github.com/camsPatience/KH2-RoxasRFScriptFix), [KH2-RoxasOutfitRFProofs](https://github.com/camsPatience/KH2-RoxasOutfitRFProofs), [KH-Romulus/Roxas-Mod-CM-ReFined-Fix](https://github.com/KH-Romulus/Roxas-Mod-CM-ReFined-Fix), [RaXZerGamingZ/RoxasBTLF_Ledge](https://github.com/RaXZerGamingZ/RoxasBTLF_Ledge) | Small fixes for TieuLink's Roxas mod and "Roxas outfit for Sora" with Re:Fined | Not needed |
| [KH2FM-Mods-Num/Model-Rando](https://github.com/KH2FM-Mods-Num/Model-Rando) (2023-03) | Model randomizer: rebuilds `P_EX100.mdlx` from parts with OpenKH `binarc` (replaces the `p_ex` model entry and `tim_` texture entry; a Roxas coat model is in its pool) | Shows a skin is just two BAR entries; still a whole-game swap |
| [shananas/ModelSwapCombo](https://github.com/shananas/ModelSwapCombo), [Napstio/Playable-Olette](https://github.com/Napstio/Playable-Olette), [KH2FM-Mods-Num/Unhooded-Roxas](https://github.com/KH2FM-Mods-Num/Unhooded-Roxas) | Other model swaps (Olette as Sora, etc.) by file replacement | Same technique, whole game |

Other multiplayer projects:
- **Volpestyle/kh2-multiplayer** (`docs/PLAYER_KITS.md`, read only): "player kits".
  - On each area load they rewrite member 0 of the resolved party array `exe+0x2A25300` (post-hook of `exe+0x3E2EB0`).
    The local player then becomes another player-class row: Roxas `P_EX110` (0x5A) or Mickey (0x5B).
  - A "remote kit" makes the other player's clone Roxas.
  - It's the game's own Roxas with his own moveset (see the limits above), not a skin on Sora's moves. The member-0
    rewrite is still the way to make the **local** player use another row.
- **Expert595/kh2-multiplayer** (MODLOG session 1): spawns a Sora/Roxas replica in a party slot through the save's
  party table; no skin work.

I found no project that shows **two different player skins in the same game at once**.

## How it could work in our mod

1. **Model files under new names.** For example `P_EX100_RX.mdlx` (+ `.a.fm` faces, + `remastered/obj/P_EX100_RX.mdlx/`
   HD textures), one per model we cover. Same skeleton as Sora.
2. **New object table rows** (`00objentry.bin`, live at `exe+0x2A254D0`). OpenKH's `listpatch` method has an `objentry`
   type that can add rows to the game's file; our `openkh_mod/mod.yml` can carry them.
   - Each row is a copy of Sora's row (type 0 player, weapon joint, weight…) with `ModelName: P_EX100_RX` and
     `AnimationName: P_EX100.mset`, so the moves are Sora's.
   - The Keyblade sets (`went` lists Sora / SoraNM / SoraTR / SoraWI) are by row too. That's a lead to check: does a new
     row need its own Keyblade list?
3. **The copy built from the Roxas row.** Our copy is built from spawn placeholder 0x237. That reads the character →
   object table `exe+0x2A25300` (0x54 = Sora; it changes per world for costumes). Writing the Roxas row's id there
   for the copy's build only, or hooking `exe+0x3E2E50` for it, would build the copy as Roxas. Most of our code finds
   the copy by "player class and not the player", not by id. The few checks on 0x54 or `"P_EX100"`:
   - `avatar_link.cpp` `body`
   - `puppet.hpp`
   - `cutscene_twin.cpp`
4. **The friend's own Sora as Roxas.** Two ways (with the client always Roxas, way a fits best):
   - **a. Whole-game swap in the friend's game only** (the community way: replace `P_EX100*`).
     - Covers forms, costumes, cutscene models (`H_EX500*`), shop faces and the HUD face with no code.
     - But then the host's copy in the friend's game would also be Roxas. It would have to be built from rows pointing
       to **Sora's** files under new names, the mirror image of step 3.
   - **b. Member 0 rewrite** (Volpestyle's way) to the Roxas row.
     - Drive Forms, costumes and cutscenes would still load Sora's rows (`P_EX100_BTLF` …) unless we also find how the
       game picks a form's row. That's unknown, a Ghidra job.
5. **HUD and cutscenes.** The party HUD face of the copy (`party_hud.cpp`), the cutscene stand-in/twin (they use
   Sora's models), and attack voices (Sora's voice from Roxas). Small items, each needs a look.

## Rough size of the work

| Part | Effort | Notes |
|---|---|---|
| Get a Roxas-on-Sora's-skeleton model we may use | **Blocker** | Ask GeminiHero / Napstio / TieuLink for permission, or have each player download the mod themselves and let a script of ours copy and rename the files from it. Our own model in Blender is hard |
| Proof: one new row + the copy built as Roxas (base outfit only) | 1 session | Bench first (copy built, plays motions, no crash, Keyblade in hand), then you look at it on two PCs |
| The friend's own Sora as Roxas (way a or b) | 1 session | a: mostly packaging; b: Ghidra for forms |
| World costumes (Halloween Town, Christmas Town, Space Paranoids, Timeless River) | 1-2 sessions | Rows + files per costume; Pride Lands (lion) and Atlantica are separate models, not in the 43 |
| Drive Forms on the copy | Part of TODO 1.24 | The copy shows no forms today; a form's files load only when that game's own player drives (VERIFIED_OFFSETS, factory crash for 0x55) |
| HUD face, cutscene stand-in, voices | 1 session | Optional polish |

**Overall:** a base-outfit Roxas copy is about 2 sessions once we have a model file we may use. Full coverage of
costumes and forms is several more sessions. The moveset and sync code don't change.

## Decisions and open questions

- **Who is Roxas (your decision, 2026-10-21):** the client (the friend) is always Roxas; the host is always Sora ("the
  story is Sora's, after all"). No setting.
  - Friend's game: a whole-game swap gives the friend Roxas everywhere (forms, costumes, cutscenes, HUD face). The
    host's copy there must be built from rows that point to Sora's files under new names.
  - Host's game: only the copy uses the Roxas rows. The host's PC needs the Roxas files too.
- **Using other people's repos (2026-10-21):**
  - **Information** (how a mod works, file names, formats, techniques) can be used freely. We credit the source anyway.
  - **Files** (their models and textures) can't be copied into our repo or friend package without the author's
    permission. Credit alone isn't enough: a repo with no license is "all rights reserved" by default. GitHub's terms
    only allow viewing and forking on GitHub. The OpenKH mods feed's license file is still empty ("TODO"), so it adds
    no default.
  - Without permission, the legal route is: each player installs the Roxas mod themselves from the author's own page.
    A setup script of ours then copies the needed files from that local install under the new names. We never ship
    the files.
  - The fan models are themselves made from Square Enix's game files, the same gray area as every KH mod. Our repo
    already ships none of the game's files.
  - (Not legal advice; this is the general rule for unlicensed code and art.)

Still open:
1. **The model file:** ask GeminiHero / Napstio for permission to bundle, or each player installs the mod and our
   script copies from it? GeminiHero's has the exact skeleton but low-resolution textures; Napstio's is HD with one
   extra bone.
2. **Scope of the first version:** base outfit only, or costumes and forms from the start?

## Leads to check before building

- OpenKH `listpatch` / `objentry` adds new row ids (not only edits existing ones): the docs show the format but not
  appends.
- A new player row with Sora's `.mset` builds and plays motions, both as the player (member 0) and as our copy.
- Napstio's 229th bone stays still under Sora's `.mset` (no stretched mesh).
- How the game picks a Drive Form's row for the player (needed for way 4b and for forms on a Roxas copy).
- Whether the Keyblade list (`went`) is per row.
