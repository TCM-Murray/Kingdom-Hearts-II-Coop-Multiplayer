"""Build the folder to copy to the second PC (the "friend" side of a co-op session).

Usage: py tools/package_friend.py <host ip> [host port=27701]

Also copies the host's KH2 save container (read only) to host-save/, for the
friend to re-key with Julumisan/kh-pc-save-transfer (MIT; script reviewed
2026-10-05: local only, backs up their container first).

Creates dist/friend-pc/ with:
  KH2 Coop/        the OpenKH mod (mod.yml + kh2coop.dll + P_EX100.mset + W_EX010.mset), same build as this PC
  kh2coop.ini      settings for the friend's game (where the host is, which features are on)
  INSTALL.txt      steps for the second PC
Both PCs must run the same kh2coop.dll: run deploy.py and this script after every change,
then copy the folder again.
"""
import hashlib
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DLL = ROOT / "build" / "Release" / "kh2coop.dll"

INI = """; KH2 Coop settings for the FRIEND's PC. Put this file next to
; "KINGDOM HEARTS II FINAL MIX.exe". Rename it (e.g. kh2coop.ini.off) to play
; normal single-player KH2 again.
[kh2coop]
; where the host listens (the host's local network address)
PEER={host}:{port}
; where this PC listens for the host
LISTEN=27702
; the host's Sora appears as a real Sora copy in Donald's slot
PUPPET=1
PUPPET_LOOK=clone
PUPPET_COLLIDE=1
CLONE_TARGET=1
; enemies may target the host's copy; its hits come off the host's own HP
COPY_AGGRO=1
; log who hits enemies and whom they target (for testing; makes the log bigger)
AGGRO_LOG=1
FORWARD_HITS=1
; saves go to a sandbox next to the mod's DLL, never to the real save
SAVE_GUARD=1
; show the other player ~100 ms late, blended smoothly (0 = snap)
INTERP_MS=100
; host-owned world: Goofy and enemies copy the host's, your hits on enemies count in the host's game
WORLD_SYNC=1
; the in-combat "PAUSE" is shared with the host (the full menu stays yours)
PAUSE_SYNC=1
; shared spawning (needs WORLD_SYNC): whoever walks into an encounter, it appears in both games
SPAWN_SYNC=1
; a lethal hit while the other player is in the room = down (up after 20 s, or 6 s after the fight); both down = Game Over
DOWNED=1
DOWN_SECONDS=20
"""

INSTALL = """KH2 Coop: second PC setup (test build {build})
=======================================================

What you need
- KINGDOM HEARTS HD 1.5+2.5 ReMIX on Steam, up to date (not the Epic version).
- OpenKH Mods Manager with Panacea installed, and these KH2 mods enabled like the host:
  at least UncensoredPatchKH2 (it changes room scripts); textures/voices don't matter.
- A save where Sora is in Hollow Bastion (any room there; Merlin's House is ideal).
- Radmin VPN, joined to the host's Radmin network (the host gives you its name and password).

Every update replaces BOTH the "KH2 Coop" folder AND kh2coop.ini: the settings
change too. Different settings on the two PCs make the game half-synced (the
logs then say "SETTINGS DIFFER").

Setup
1. Copy the folder "KH2 Coop" into the Mods Manager's KH2 mods folder:
     <OpenKH folder>\\mods\\kh2\\KH2 Coop\\
   In Mods Manager, tick "KH2 Coop" and click Build (or Build & Run).
2. Copy kh2coop.ini next to "KINGDOM HEARTS II FINAL MIX.exe"
   (Steam > KH 1.5+2.5 > right click > Manage > Browse local files).
   It points at the host: {host}. If the host's address changes, edit the PEER line.
3. Start KH2 from Steam as usual. If Windows Firewall asks about KH2, click Allow
   (tick both Private and Public: Radmin VPN counts as a Public network).
4. Load your Hollow Bastion save. You'll see the host's Sora in Donald's place once
   you're both in the same room.

No Hollow Bastion save? Use the host's (folder host-save)
A save copied from another PC shows as "corrupted": each save file is tied to its
Steam account. The free tool KH PC Save Transfer fixes that:
  https://github.com/Julumisan/kh-pc-save-transfer
1. First back up your own saves: copy the whole folder
     Documents\\My Games\\KINGDOM HEARTS HD 1.5+2.5 ReMIX\\
   somewhere safe. The tool REPLACES ALL your KH2 save slots with the host's
   (it also keeps its own backup, in its "backups" folder).
2. Start KH 1.5+2.5 from Steam once (to the title screen), then close it.
3. Drag host-save\\KHIIFM_WW.png onto KH_Save_Transfer.bat.
4. Start the game; if Steam reports a cloud conflict, keep the LOCAL files.
   Load slot 33 (Merlin's House).
To get your own saves back: copy your backup folder back.

What the mod does on your PC
- Sends your Sora's position, animation and HP to the host and receives theirs (UDP port 27702).
- Saving is redirected to a sandbox folder; your real save file is never written.
  (Saves you make during co-op are lost when the game closes.)
- To play normal KH2 again: rename kh2coop.ini to kh2coop.ini.off (or untick the mod).

After a test, please send the host this file (newest one):
  <OpenKH folder>\\mod\\kh2\\dll\\kh2coop_<number>.log
"""


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    host = sys.argv[1]
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 27701
    if not DLL.exists():
        raise SystemExit("build first: py tools/deploy.py")
    out = ROOT / "dist" / "friend-pc"
    if out.exists():
        shutil.rmtree(out)
    (out / "KH2 Coop").mkdir(parents=True)
    shutil.copy2(ROOT / "openkh_mod" / "mod.yml", out / "KH2 Coop" / "mod.yml")
    shutil.copy2(DLL, out / "KH2 Coop" / "kh2coop.dll")
    # Sora's motion set + rescue motions, built from this PC's game files (private test between owners
    # of the game; a public release must build it on the player's PC instead).
    for name in ("P_EX100.mset", "W_EX010.mset"):
        shutil.copy2(ROOT / "build" / "mod_assets" / "obj" / name, out / "KH2 Coop" / name)
    save = pathlib.Path.home() / "Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX/Steam/<SteamID64>/KHIIFM_WW.png"
    if save.exists():
        (out / "host-save").mkdir()
        shutil.copy2(save, out / "host-save" / "KHIIFM_WW.png")  # read only: never write the real save
    build = hashlib.sha256(DLL.read_bytes()).hexdigest()[:12]
    (out / "kh2coop.ini").write_text(INI.format(host=host, port=port), encoding="utf-8")
    (out / "INSTALL.txt").write_text(INSTALL.format(host=host, build=build), encoding="utf-8")
    print(f"{out}  (kh2coop.dll sha256 {build}...)")


if __name__ == "__main__":
    main()
