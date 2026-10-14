"""Effect pool peaks per room (TODO 1.59): reads kh2coop logs and merges each room's highest effect pool use into
docs/EFFECT_POOL_PEAKS.md.

The DLL (src/dll/fx_pool.cpp) logs lines like
    effect pool: room 0x12/0x15 programs 99/99/99: peak 612345 B (598 KB) of 4096 KB, room left
The highest peak per room (world, room, map/battle/event programs) and role (HOST / FRIEND, from the log's
"avatar link: role" line) is kept; the file keeps the highest ever seen, and the names already written in it.

Usage: py tools/pool_peaks.py [--source TEXT] [--dry-run] LOG_OR_FOLDER ...
  --source  where the logs come from, written next to each new peak with the log's date (default "bench";
            e.g. "two-PC").
Only numbers go into the file, never log lines (the logs hold addresses and names that stay private).
"""
import argparse
import datetime
import glob
import io
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "EFFECT_POOL_PEAKS.md")
LINE = re.compile(r"effect pool: room 0x([0-9A-F]{2})/0x([0-9A-F]{2}) programs (\d+)/(\d+)/(\d+): "
                  r"peak (\d+) B \(\d+ KB\) of (\d+) KB")
ROLE = re.compile(r"avatar link: role (HOST|FRIEND)")
ROW = re.compile(r"^\| (0x[0-9A-F]{2}/0x[0-9A-F]{2}) \| (\d+/\d+/\d+) \| (.*?) \| (.*?) \| (.*?) \|\s*$")
GAME_POOL_KB = 512

WORLDS = {
    0x01: "End of Sea", 0x02: "Twilight Town", 0x03: "Destiny Islands", 0x04: "Hollow Bastion",
    0x05: "Beast's Castle", 0x06: "Olympus Coliseum", 0x07: "Agrabah", 0x08: "The Land of Dragons",
    0x09: "100 Acre Wood", 0x0A: "Pride Lands", 0x0B: "Atlantica", 0x0C: "Disney Castle", 0x0D: "Timeless River",
    0x0E: "Halloween Town", 0x0F: "World Map", 0x10: "Port Royal", 0x11: "Space Paranoids",
    0x12: "The World That Never Was",
}
# Boss fights found in the room files (ard/us/*.ard spawn points, session 33): (world, room, battle program).
FIGHTS = {
    (0x02, 0x14, 213): "Data Axel (B_EX110_LV99)",
    (0x04, 0x04, 114): "Data Demyx (B_EX120_HB_LV99)",
    (0x05, 0x0F, 97): "Data Xaldin (B_EX130_LV99)",
    (0x12, 0x0A, 100): "Data Xigbar (B_EX140_LV99)",
    (0x12, 0x0E, 101): "Data Saix (B_EX150_LV99, claymores B_EX210)",  # bench 2026-10-14: BERSERK gauge
    (0x12, 0x0F, 102): "Data Luxord (B_EX160_LV99, cards B_EX220_LV99)",
    (0x12, 0x13, 97): "Data Xemnas (B_EX170_LV99)",
    (0x12, 0x14, 98): "Data Xemnas, last part (B_EX170_LAST_LV99)",
    (0x12, 0x15, 99): "Data Roxas (B_EX390)",
    (0x12, 0x15, 65): "Roxas (B_EX390)",
    (0x04, 0x21, 143): "Larxene, 1065 HP (B_EX400)",
    (0x04, 0x21, 148): "Data Larxene, 2715 HP (B_EX400)",
    (0x04, 0x21, 155): "Larxene, 1065 HP (B_EX400)",
    (0x04, 0x22, 151): "B_EX370 (Marluxia?) with B_EX380 and B_EX410, 708 HP",
    (0x04, 0x22, 152): "B_EX370 (Marluxia?) with B_EX380 and B_EX410, 2172 HP",
    (0x0C, 0x07, 67): "B_EX420 (Lingering Will?), 3400 HP",
    (0x0C, 0x07, 73): "B_EX420 (Lingering Will?), 3400 HP",
}

HEADER = """# Effect pool peaks per room

The most of the game's effect pool each room has used, as logged by the DLL (`src/dll/fx_pool.cpp`, TODO 1.59).
The game's own pool is 512 KB; since session 34 co-op gives it 4 MB (`FX_POOL_KB` in kh2coop.ini). When a pool is
full, new particles fail and some effects crash the game (`exe+0x4B00DA`, `exe+0x4D3A84`; VERIFIED_OFFSETS).

- Each peak is the bytes in use at the busiest moment in that room (with the pool's 16-byte block headers), in
  KB, then the size of the pool it was seen in. **Bold** = more than 512 KB, more than the game's own pool holds.
- Host and friend are kept apart: each game has its own pool (the friend's game also shows the host's effects,
  and the other way around).
- Programs = the room's map / battle / event programs (`exe+0x717008`); the same room in another story step is
  another row.
- Filled in by `py tools/pool_peaks.py [--source two-PC] <logs>` (keeps the highest ever seen per row and the
  names written here; edit the Name column by hand as you like).

| Room | Programs | Name | Host | Friend |
|---|---|---|---|---|
"""


def log_role(lines):
    for l in lines:
        m = ROLE.search(l)
        if m:
            return m.group(1)
    return None


def read_logs(paths):
    """{(room, programs): {role: (peak bytes, pool KB, date)}} from the logs."""
    found = {}
    files = []
    for p in paths:
        files += sorted(glob.glob(os.path.join(p, "kh2coop_*.log"))) if os.path.isdir(p) else [p]
    for f in files:
        with io.open(f, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        role = log_role(lines)
        if not role:
            continue
        date = datetime.date.fromtimestamp(os.path.getmtime(f)).isoformat()
        for l in lines:
            m = LINE.search(l)
            if not m:
                continue
            key = (f"0x{m.group(1)}/0x{m.group(2)}", f"{m.group(3)}/{m.group(4)}/{m.group(5)}")
            peak, pool = int(m.group(6)), int(m.group(7))
            have = found.setdefault(key, {}).get(role)
            if not have or peak > have[0]:
                found[key][role] = (peak, pool, date)
    return found, len(files)


def cell(peak, pool, seen):
    kb = (peak + 1023) // 1024
    text = f"**{kb}**" if kb > GAME_POOL_KB else str(kb)
    return f"{text} of {pool} ({seen})"


def parse_cell(text):
    m = re.match(r"\**(\d+)\** of (\d+) \((.*)\)$", text.strip())
    return (int(m.group(1)) * 1024, int(m.group(2)), m.group(3)) if m else None


def default_name(room, programs):
    world, rm = (int(x, 16) for x in room.split("/"))
    btl = int(programs.split("/")[1])
    return FIGHTS.get((world, rm, btl), WORLDS.get(world, ""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--source", default="bench")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    rows = {}  # (room, programs) -> [name, host cell, friend cell]
    if os.path.exists(DOC):
        with io.open(DOC, encoding="utf-8") as fh:
            for l in fh:
                m = ROW.match(l.rstrip("\n"))
                if m:
                    rows[(m.group(1), m.group(2))] = [m.group(3), m.group(4), m.group(5)]
    found, nfiles = read_logs(a.logs)
    changed = 0
    for key, roles in found.items():
        row = rows.setdefault(key, [default_name(*key), "-", "-"])
        for i, role in ((1, "HOST"), (2, "FRIEND")):
            if role not in roles:
                continue
            peak, pool, date = roles[role]
            old = parse_cell(row[i])
            # the cell keeps KB: a new peak counts when it's a higher KB
            if not old or (peak + 1023) // 1024 > old[0] // 1024:
                row[i] = cell(peak, pool, f"{a.source} {date}")
                changed += 1
    out = HEADER + "".join(f"| {r} | {p} | {v[0]} | {v[1]} | {v[2]} |\n"
                           for (r, p), v in sorted(rows.items(),
                                                   key=lambda kv: (kv[0][0], [int(x) for x in kv[0][1].split("/")])))
    print(f"{nfiles} logs, {len(found)} rooms with peaks, {changed} cells new or higher, {len(rows)} rows")
    if a.dry_run:
        print(out)
        return
    os.makedirs(os.path.dirname(DOC), exist_ok=True)
    with io.open(DOC, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(out)


if __name__ == "__main__":
    main()
