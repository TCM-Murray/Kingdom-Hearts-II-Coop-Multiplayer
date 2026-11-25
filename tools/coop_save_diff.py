"""TODO 5.32: compare the host's and the client's co-op save files offline (read only; MODLOG session 87).
Pairs each co-op save (slots past 60) with the other player's from the same moment (room and story-bit count)
and sorts every differing byte: the story copy's areas (story_sync.cpp kAreas: should match), per player
(OpenKH SaveDataFinalMix), the world-flag area +0x4038..0x41A0 (5.32), the rest unknown.
Usage: py tools/coop_save_diff.py [host file] [client file]
Layout (VERIFIED_OFFSETS F697): record i at 0x19690 + i * 0x10FC0 (magic KH2J), its name BISLPM-66675FM-NN at
0x1C8 + i * 0x158 = screen slot NN + 1. Fields: OpenKH SaveDataFinalMix."""
import re
from collections import defaultdict
import struct
import sys

ROOT = "D:/Codex Projects/Kingdom Hearts 2 Multiplayer Mod/build/test_coop_saves/latest/"
FILES = {"HOST": sys.argv[1] if len(sys.argv) > 1 else ROOT + "KHIIFM_WW HOST.png",
         "CLIENT": sys.argv[2] if len(sys.argv) > 2 else ROOT + "KHIIFM_WW CLIENT.png"}
S, B = 0x10FC0, 0x19690


def records(path):
    f = open(path, "rb").read()
    out = {}
    for i in range(120):
        o = B + i * S
        if o + S > len(f):
            break
        r = f[o:o + S]
        if r[:4] != b"KH2J":
            continue
        name = f[0x1C8 + i * 0x158:0x1C8 + i * 0x158 + 32].split(b"\0")[0].decode("ascii", "replace")
        m = re.search(r"FM-(\d+)", name)
        slot = int(m.group(1)) + 1 if m else None
        out[slot if slot is not None else 1000 + i] = r
    return out


def summary(r):
    u32 = lambda o: struct.unpack_from("<I", r, o)[0]
    story = sum(bin(b).count("1") for b in r[0x1C90:0x1EF0])
    return {"room": f"{r[0xC]:02X}/{r[0xD]:02X}/{r[0xE]:02X}", "story": story, "exp": u32(0x36E0),
            "munny": u32(0x2440), "t0": u32(0x2444), "t1": u32(0x2448), "wldf": r[0x4199]}


COPIED = [(0x10, 0x1C90, "room programs"), (0x1C90, 0x1EF0, "story flags"), (0x1EF0, 0x1FD0, "script bits/pairs"),
          (0x22F8, 0x2390, "rooms visited"), (0x2390, 0x23AC, "script bits 8/9"), (0x23AC, 0x23E0, "chests"),
          (0x23EC, 0x23FF, "world bytes"), (0x3FEC, 0x4038, "world records"), (0x3724, 0x3728, "battle levels")]
PLAYER = [(0x0, 0x10, "header"), (0x2440, 0x2444, "munny"), (0x2444, 0x2498, "timers"), (0x24F0, 0x32F4, "characters"),
          (0x32F4, 0x3524, "drive forms"), (0x3524, 0x3534, "summon/drive bar"), (0x3534, 0x3580, "party per world"),
          (0x3580, 0x36C0, "inventory"), (0x36E0, 0x36E4, "EXP"), (0x36F8, 0x3704, "shortcuts/bonus lv"),
          (0x41A4, 0x41A6, "settings"), (0x41AA, 0x41AC, "new-magic marks"), (0x41B2, 0x41B4, "new-magic marks"), (0x4270, 0x4272, "tutorial menu"),
          (0x4274, 0x4276, "new-status marks")]
WORLD = (0x4038, 0x41A0, "world-flag area")


def area(o):
    for a, b, n in COPIED:
        if a <= o < b:
            return "copied", n
    for a, b, n in PLAYER:
        if a <= o < b:
            return "player", n
    if WORLD[0] <= o < WORLD[1]:
        return "world", WORLD[2]
    return "unknown", ""


def pairs(h, c):
    """Each host slot past 60 to the client slot past 60 with the same room and story-bit count (closest time)."""
    out, used = [], set()
    for hs in sorted((s for s in h if s > 60), key=lambda s: summary(h[s])["story"]):
        a = summary(h[hs])
        best = None
        for cs in c:
            if cs <= 60 or cs in used or cs == 99:
                continue
            b = summary(c[cs])
            if b["room"] == a["room"] and abs(b["story"] - a["story"]) <= 1:
                if best is None or abs(b["t0"] - a["t0"]) < abs(summary(c[best])["t0"] - a["t0"]):
                    best = cs
        if best:
            used.add(best)
            out.append((hs, best))
        else:
            out.append((hs, None))
    return out, [s for s in c if s > 60 and s != 99 and s not in used]


if __name__ == "__main__":
    h, c = records(FILES["HOST"]), records(FILES["CLIENT"])
    ps, unpaired = pairs(h, c)
    per_offset = defaultdict(list)  # (kind, offset) -> [(host slot, host byte, client byte)]
    print(f"{'H':>3} {'C':>3} {'room':>9} {'story':>9} | copied  world  player unknown")
    for hs, cs in ps:
        a = summary(h[hs])
        if cs is None:
            print(f"{hs:>3}   - {a['room']:>9} {a['story']:>9} | (no client save at this moment)")
            continue
        b = summary(c[cs])
        hr, cr = h[hs], c[cs]
        counts = defaultdict(int)
        for o in range(len(hr)):
            if hr[o] != cr[o]:
                k, n = area(o)
                counts[k] += 1
                if k != "player":
                    per_offset[(k, n, o)].append((hs, hr[o], cr[o]))
        print(f"{hs:>3} {cs:>3} {a['room']:>9} {a['story']:>4}/{b['story']:<4} | {counts['copied']:>6} {counts['world']:>6} "
              f"{counts['player']:>7} {counts['unknown']:>7}")
    print("client co-op slots with no host save at that moment:",
          [(s, summary(c[s])["room"], summary(c[s])["story"]) for s in unpaired])
    print("\nnon-player offsets that differ (offset: pairs where they differ, host/client bytes in the first and last):")
    for (k, n, o), lst in sorted(per_offset.items(), key=lambda x: (x[0][0], x[0][2])):
        f, l = lst[0], lst[-1]
        print(f"  {k:7} {n:18} +0x{o:04X}: {len(lst):>2} pairs; H{f[0]} {f[1]:02X}/{f[2]:02X} .. H{l[0]} {l[1]:02X}/{l[2]:02X}")
