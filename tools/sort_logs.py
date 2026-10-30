"""Move the mod's logs into this project: logs/host and logs/client (git ignores logs/).

Usage: py tools/sort_logs.py [--dry-run]

Takes every kh2coop_*.log from this PC's game folder (where the DLL writes them, benches' copies included) and from
%USERPROFILE%\\Downloads (the friend's logs, as downloaded). Which game a log comes from is its own 'avatar link: role
HOST / FRIEND' line; a log without one counts as its folder's (game folder = host, Downloads = client). Nothing is
overwritten (a name already there gets ' (2)', ' (3)', ...), and a log a running game still writes is left in place.
"""
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GAME_LOGS = pathlib.Path(r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\mod\kh2\dll")  # nav.py's LOGS
SOURCES = [(GAME_LOGS, "host"), (pathlib.Path.home() / "Downloads", "client")]
ROLE = re.compile(rb"avatar link: role (HOST|FRIEND)")


def role_of(path, default):
    with open(path, "rb") as fh:
        m = ROLE.search(fh.read(200_000))
    return default if not m else "host" if m.group(1) == b"HOST" else "client"


def free_name(folder, name):
    dest = folder / name
    n = 2
    while dest.exists():
        dest = folder / f"{pathlib.Path(name).stem} ({n}){pathlib.Path(name).suffix}"
        n += 1
    return dest


def main():
    dry = "--dry-run" in sys.argv
    moved = {"host": 0, "client": 0}
    for folder, default in SOURCES:
        for path in sorted(folder.glob("kh2coop_*.log")):
            side = role_of(path, default)
            dest_dir = ROOT / "logs" / side
            dest = free_name(dest_dir, path.name)
            if dry:
                print(f"{path} -> logs/{side}/{dest.name}")
                moved[side] += 1
                continue
            dest_dir.mkdir(parents=True, exist_ok=True)
            try:
                try:
                    path.rename(dest)  # same drive
                except OSError as e:
                    if getattr(e, "winerror", None) != 17:  # 17: another drive (Downloads on C:)
                        raise
                    shutil.copy2(path, dest)
                    try:
                        path.unlink()
                    except OSError:
                        dest.unlink()  # the original stays: no half copy left behind
                        raise
            except OSError as e:  # a running game keeps its log open
                print(f"left in place (in use?): {path.name}: {e}")
                continue
            moved[side] += 1
    print(f"{'would move' if dry else 'moved'}: host {moved['host']}, client {moved['client']}")


main()
