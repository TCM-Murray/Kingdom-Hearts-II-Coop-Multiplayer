"""Build the folder to copy to the second PC (the "friend" side of a co-op session).

Usage: py tools/package_friend.py <host ip> [host port=27701]   (Radmin VPN / IP)
       py tools/package_friend.py steam [<host SteamID64>]      (Steam: Join Game, or that account directly)

Also copies the host's KH2 save container (read only) to host-save/, for the
friend to re-key with Julumisan/kh-pc-save-transfer (MIT; script reviewed
2026-10-05: local only, backs up their container first).

Creates dist/friend-pc/ with:
  KH2 Coop/        the OpenKH mod (mod.yml + kh2coop.dll + P_EX100.mset + W_EX010.mset + title label textures), same build as this PC
  kh2coop.ini      settings for the friend's game (where the host is, which features are on);
                   personal settings are in kh2coop_player.ini, which the DLL writes itself
  INSTALL.txt      steps for the second PC
  licenses/        the mod's license (GPL-3.0-or-later) and MinHook's (BSD-2-Clause, it's inside the DLL)
Both PCs must run the same kh2coop.dll: run deploy.py and this script after every change,
then copy the folder again.
"""
import hashlib
import ipaddress
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DLL = ROOT / "build" / "Release" / "kh2coop.dll"
MINHOOK_LICENSE = ROOT / "build" / "_deps" / "minhook-src" / "LICENSE.txt"
SAVES = pathlib.Path.home() / "Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX/Steam"

INI = """; KH2 Coop settings for the FRIEND's PC. Put this file next to
; "KINGDOM HEARTS II FINAL MIX.exe". Rename it (e.g. kh2coop.ini.off) to play
; normal single-player KH2 again.
; This file holds the connection and the co-op features that must be the same on
; both PCs; every update replaces it. Your own settings (how long you stay down,
; Steam achievements) are in kh2coop_player.ini in the same folder: the mod writes
; it on the first start and updates never touch it.
[kh2coop]
{connection}; the host's Sora appears as a real Sora copy in Donald's slot
PUPPET=1
PUPPET_LOOK=clone
PUPPET_COLLIDE=1
CLONE_TARGET=1
; enemies may target the host's copy; its hits come off the host's own HP
COPY_AGGRO=1
; log who hits enemies and whom they target (for testing; makes the log bigger)
AGGRO_LOG=1
FORWARD_HITS=1
; show the other player ~100 ms late, blended smoothly (0 = snap)
INTERP_MS=100
; host-owned world: Goofy and enemies copy the host's, your hits on enemies count in the host's game
WORLD_SYNC=1
; the in-combat "PAUSE" is shared with the host (the full menu stays yours)
PAUSE_SYNC=1
; shared spawning (needs WORLD_SYNC): whoever walks into an encounter, it appears in both games
SPAWN_SYNC=1
; a lethal hit while the other player is in the room = down instead of dead; both down = Game Over
; (how long you stay down: DOWN_SECONDS in kh2coop_player.ini)
DOWNED=1
; enemies far from the host but near you treat you as the player, so they stay and fight you
; instead of vanishing and coming back (must match the host)
AI_NEAREST_PLAYER=1
; both games leave a room's loading screen together (the host waits for you, at most LOAD_BARRIER_MS)
LOAD_BARRIER=1
LOAD_BARRIER_MS=5000
"""

INSTALL = """KH2 Coop: second PC setup (test build {build})
=======================================================

New in this build
- You can connect through Steam instead of Radmin VPN: no VPN, no IP addresses.
  This build's kh2coop.ini connects through Steam. Radmin VPN still works as a
  backup (see "Going back to Radmin VPN" below).

What to test this time (together with the host)
Before you copy the new kh2coop.ini, rename your old one to "kh2coop.ini.radmin":
that's your way back to Radmin VPN. Radmin VPN isn't needed for these tests.
Always start KH2 through Steam (Play) or Mods Manager (Build & Run), never by opening
"KINGDOM HEARTS II FINAL MIX.exe" directly: then the game's Steam doesn't start and
the game crashes right away.
Test 1: connect by Steam ID (this kh2coop.ini already has the host's Steam ID in it)
1. Both start KH2, pick LOAD (CO-OP) on the title and load your co-op saves (the same
   place, as usual).
2. You should see each other's Sora as before. Move around a bit.
Test 2: Join Game from the Steam friends list
1. Close your game. Open kh2coop.ini with Notepad, change the PEER line to
     PEER=steam
   and save the file.
2. Start KH2 through Steam or Mods Manager, pick LOAD (CO-OP) and load your save
   (the host stays in their game).
3. Open the Steam overlay (Shift+Tab, or the Steam button on the controller), find
   the host in your friends list, right-click > Join Game. Or the host right-clicks
   you > Invite to Game, and you accept the invite.
4. You should see each other's Sora again.
If Join Game is missing, greyed out, or Steam offers to start the game again, tell
the host exactly what Steam showed.

Going back to Radmin VPN
Delete kh2coop.ini and rename kh2coop.ini.radmin back to kh2coop.ini. The host
switches too (one line in their start_host.bat).

The title menu: two kinds of saves
- LOAD (CO-OP): your co-op save file, the mod on, playing together with the host.
- LOAD or NEW GAME: your normal Steam saves, the mod off: plain single-player KH2.
The co-op save file is "KH2 Coop saves\\KHIIFM_WW.png" in the game folder (next to
"KINGDOM HEARTS II FINAL MIX.exe"). The first LOAD (CO-OP) makes it as a copy of all
your saves. You already have one from the last test: keep it, it holds your co-op
progress. Saves made in co-op go only there, never to your Steam save; saves made
after a normal LOAD go only to your Steam save.

Update (every new build)
Replace BOTH the "KH2 Coop" folder AND kh2coop.ini: the settings change too.
Different settings on the two PCs make the game half-synced (the logs then say
"SETTINGS DIFFER").
1. Delete the old "KH2 Coop" folder in <OpenKH folder>\\mods\\kh2\\ and copy the new
   one there. In Mods Manager keep "KH2 Coop" ticked and at the TOP of the list
   (above texture mods such as Texture HD, or the title shows their labels instead
   of LOAD (CO-OP)), then click Build (or Build & Run).
2. Copy kh2coop.ini over the old one next to "KINGDOM HEARTS II FINAL MIX.exe"
   (Steam > KH 1.5+2.5 > right click > Manage > Browse local files).
   {where}
3. Start KH2 from Steam (or Mods Manager) as usual, never the .exe directly.

First install (only once)
- You need: KINGDOM HEARTS HD 1.5+2.5 ReMIX on Steam, up to date (not the Epic
  version); OpenKH Mods Manager with Panacea installed and the same KH2 mods as the
  host (at least UncensoredPatchKH2, it changes room scripts; textures and voices
  don't matter); the host on your Steam friends list. Only for the Radmin VPN
  connection: Radmin VPN, joined to the host's Radmin network (the host gives you
  its name and password).
- Then do the update steps above. With Radmin VPN, the first time you start KH2, if
  Windows Firewall asks about it, click Allow and tick both Private and Public
  (Radmin VPN counts as a Public network).

Your own settings (kh2coop_player.ini)
It sits next to kh2coop.ini. The mod writes it with the defaults on the first
start; updates never replace it. Open it with Notepad:
  DOWN_SECONDS=30   seconds you stay down before getting up by yourself
  ACHIEVEMENTS=0    Steam achievements during co-op: 0 = blocked, 1 = allowed
These may differ from the host's. Delete the file to get the defaults back.

The host's save, for a single-player test (folder host-save, optional)
This only changes your normal Steam saves (the ones LOAD shows). Your co-op save
file is not touched.
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
   Pick LOAD (the normal one) and load the Hollow Bastion save.
To get your own saves back: copy your backup folder back.

What the mod does on your PC
- In co-op only: sends your Sora's position, animation and HP to the host and
  receives theirs ({via}).
- To remove the mod completely: rename kh2coop.ini to kh2coop.ini.off (or untick
  the mod in Mods Manager and Build).
- KH2 Coop is free software (GPL-3.0-or-later); the licenses are in the "licenses"
  folder.

After a test, please send the host this file (the newest one):
  <OpenKH folder>\\mod\\kh2\\dll\\kh2coop_<number>.log
"""


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1].lower() == "steam":
        steam_id = sys.argv[2] if len(sys.argv) > 2 else ""
        if steam_id and not (steam_id.isdigit() and len(steam_id) == 17):
            raise SystemExit(f"not a SteamID64 (17 digits): {steam_id!r}")
        peer = f"steam:{steam_id}" if steam_id else "steam"
        connection = ("; connect through Steam (TODO 6.7): steam = wait for Join Game (Steam overlay, right-click\n"
                      "; the host, Join Game) or the host's Invite to Game; steam:<host SteamID64> = connect to\n"
                      "; that Steam account right away. Radmin VPN instead: PEER=<host Radmin IP>:27701 and LISTEN=27702\n"
                      f"PEER={peer}\n")
        where = ("It connects through Steam" + (" to the host's Steam account." if steam_id else ": Join Game on the host.")
                 + " To use Radmin VPN instead, see the PEER line.")
        via = "through Steam"
    else:
        # Just the address: a value copied from an old PEER line ("ip:port") would add a second port
        host = sys.argv[1].split(":")[0]
        try:
            ipaddress.IPv4Address(host)
        except ValueError:
            raise SystemExit(f"not an IPv4 address: {sys.argv[1]!r}")
        port = int(sys.argv[2]) if len(sys.argv) > 2 else 27701
        connection = ("; where the host listens (the host's local network address)\n"
                      f"PEER={host}:{port}\n"
                      "; where this PC listens for the host\n"
                      "LISTEN=27702\n")
        where = f"It points at the host: {host}. If the host's address changes, edit the PEER line."
        via = "UDP port 27702"
    if not DLL.exists() or not MINHOOK_LICENSE.exists():
        raise SystemExit("build first: py tools/deploy.py")
    out = ROOT / "dist" / "friend-pc"
    if out.exists():
        shutil.rmtree(out)
    (out / "KH2 Coop").mkdir(parents=True)
    shutil.copy2(ROOT / "build" / "mod_assets" / "mod.yml", out / "KH2 Coop" / "mod.yml")  # with the label entries
    shutil.copy2(DLL, out / "KH2 Coop" / "kh2coop.dll")
    # Sora's motion set + rescue motions, built from this PC's game files (private test between owners
    # of the game; a public release must build it on the player's PC instead).
    for name in ("P_EX100.mset", "W_EX010.mset"):
        shutil.copy2(ROOT / "build" / "mod_assets" / "obj" / name, out / "KH2 Coop" / name)
    # Title LOAD (CO-OP) label textures in every language (tools/make_title_label.py, same note as above).
    shutil.copytree(ROOT / "build" / "mod_assets" / "labels" / "remastered", out / "KH2 Coop" / "remastered")
    # one folder per Steam account (named by its Steam ID); only copy when there's exactly one
    saves = sorted(SAVES.glob("*/KHIIFM_WW.png"))
    if len(saves) == 1:
        (out / "host-save").mkdir()
        shutil.copy2(saves[0], out / "host-save" / "KHIIFM_WW.png")  # read only: never write the real save
    else:
        print(f"host save not copied: {len(saves)} KHIIFM_WW.png found under {SAVES}")
    (out / "licenses").mkdir()
    shutil.copy2(ROOT / "LICENSE", out / "licenses" / "KH2-Coop-LICENSE.txt")
    shutil.copy2(MINHOOK_LICENSE, out / "licenses" / "MinHook-LICENSE.txt")
    build = hashlib.sha256(DLL.read_bytes()).hexdigest()[:12]
    (out / "kh2coop.ini").write_text(INI.format(connection=connection), encoding="utf-8")
    (out / "INSTALL.txt").write_text(INSTALL.format(where=where, via=via, build=build), encoding="utf-8")
    print(f"{out}  (kh2coop.dll sha256 {build}...)")


if __name__ == "__main__":
    main()
