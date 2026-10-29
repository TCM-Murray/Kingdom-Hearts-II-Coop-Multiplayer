"""Room sweep (TODO 1.127; you, 2026-10-29: is the copy's look budget a few rooms' problem or a general one?):
a bench pair warps through every room file of the game and measures, in each game, the room the copy's look has.

Usage: py tools/test_room_sweep.py [--only hb12,eh19 ...] [--from hb00] [--drive] [--both] [--env KEY=VALUE ...]

Save 59 (Garden of Assemblage, every Drive Form and the Limits). The rooms are the game's room files
(ard/us/<world><room>.ard, room number in decimal); for each world the pair reloads save 59, then the host warps
(programs 0xFFFF: the game picks them from the story) and the friend follows. In each room, once both are in it
and its files are placed, each game's file list is read as form_look.cpp's NegAsk reads it: free = our bottom
(exe+0x29F33F0) minus the top of the placed files of groups 0-9999 without the Drive area 5000-5099; also the
Drive area and our negative files (the copy's Keyblade). The smallest free over a few reads counts.
--drive (pass 2): then the friend Drives into Limit Form (the largest look; found by the menu's command) and back;
the host's 'copy files:' answer (DECLINED, or its files asked with the budget) is recorded. --both: the host Drives
into Final Form first (else the largest form its party allows), so each game holds its own form's files and the
copy's look at once; both games' answers and any 'placed files ... under the margin' WARNING (form_look.cpp, shown
only then) are recorded.
A crash, a room that doesn't load in 60 s or a warp not done restarts the pair; the sweep goes on with the next room.
Results: build/bench/room_sweep/rooms.csv (one line per room and game, written as it goes), summary.txt, and the
screenshots of the rooms that failed. No fighting, no save written (SAVE_GUARD).
"""
import argparse
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import peek  # noqa: E402
import test_extra_copy as tx  # noqa: E402
from test_save_sweep import back_to_title  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "room_sweep")
ARD = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\data\kh2\ard\us"
WORLDS = {"es": 0x01, "tt": 0x02, "di": 0x03, "hb": 0x04, "bb": 0x05, "he": 0x06, "al": 0x07, "mu": 0x08, "po": 0x09,
          "lk": 0x0A, "lm": 0x0B, "dc": 0x0C, "wi": 0x0D, "nm": 0x0E, "wm": 0x0F, "ca": 0x10, "tr": 0x11, "eh": 0x12}
SLOT = 59
HOST_FORMS = (5, 4, 1, 2, 3)    # form bytes, first the party allows: Final, Master, Valor, Wisdom, Limit
FRIEND_FORMS = (3, 5, 4, 1, 2)  # Limit (the largest look; needs no ally) first


def rooms():
    """[(name, world, room)] from the room files, in world-id then room order."""
    out = [(f[:4], WORLDS[f[:2]], int(f[2:4])) for f in os.listdir(ARD) if f.endswith(".ard") and f[:2] in WORLDS]
    return sorted(out, key=lambda r: (r[1], r[2]))


def measure(c):
    """(free KB without the Drive area, Drive area KB, our negative files KB) as NegAsk reads the list."""
    bottom = struct.unpack("<Q", peek.read(c.game.h, c.game.base + 0x29F33F0, 8))[0]
    top = drive = neg = 0
    for e, n, s, g, pl, sz, f in tx.file_list(c):
        if 5000 <= g < 5100:
            drive += sz
        elif g < 0:
            neg += sz
        elif g < 10000 and pl:
            top = max(top, pl + sz)
    return ((bottom - top) // 1024 if top else -1), drive // 1024, neg // 1024


def settle(copies, world, room, mark):
    """Both in the room, its census 'room ready' line in each log, then the smallest free over 4 reads."""
    bench.wait(f"both in {world:02X}/{room:02X}", lambda: all(c.room() == (world, room) for c in copies), 60, copies)
    tag = f"room ready): room {world:02x}/{room:02x}:"
    bench.wait("the room's files", lambda: all(any(tag in l for l in c.since(mark[c.name])) for c in copies), 30,
               copies)
    time.sleep(6)
    best = {}
    for _ in range(4):
        for c in copies:
            m = measure(c)
            if c.name not in best or (m[0] >= 0 and (best[c.name][0] < 0 or m[0] < best[c.name][0])):
                best[c.name] = m
        time.sleep(1)
    return best


def answers(c, m, n, wait=12):
    """This game's answers to the copy's look since mark m (DECLINED, or its files asked with the budget) and the
    'placed files ... under the margin' warnings, once n answers are in or after wait seconds."""
    end = time.time() + wait
    while True:
        new = c.since(m)
        got = [l[13:300] for l in new if "copy files:" in l and ("DECLINED" in l or "files of ours in group" in l)]
        if len(got) >= n or time.time() > end:
            return got + [l[13:300] for l in new if "copy files: WARNING" in l]
        time.sleep(0.3)


def drive_test(copies, both):
    """The friend into Limit Form (the largest look) and back; with both, the host into Final Form first (else the
    largest form its party allows: a world ally in Donald's place leaves Wisdom, Master and Final out), so each game
    then holds its own form's files and the copy's look at once (the margin warning shows only then)."""
    host, friend = copies
    m = {c.name: c.mark() for c in copies}
    form_now = lambda c: tx.read_u(c, c.game.base + 0x9ACDD4, 1)
    if both:
        tx.drive_form(host, HOST_FORMS)
        answers(friend, m[friend.name], 1)
    tx.drive_form(friend, FRIEND_FORMS)
    out = {host.name: answers(host, m[host.name], 1)}
    time.sleep(3)  # the layouts after the friend's own Drive (its margin warning, if any)
    out[friend.name] = answers(friend, m[friend.name], 1 if both else 0, wait=0)
    for c in copies:
        out[c.name].append(f"form {form_now(c)}")
        if form_now(c):
            tx.revert_to_sora(c)
    time.sleep(5)
    return {k: " / ".join(v) for k, v in out.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--from", dest="start", default="")
    ap.add_argument("--drive", action="store_true")
    ap.add_argument("--both", action="store_true")  # --drive with the host in Final Form first
    ap.add_argument("--env", action="append", default=[])
    a = ap.parse_args()
    a.drive |= a.both
    os.makedirs(OUT, exist_ok=True)
    todo = rooms()
    if a.only:
        want = set(a.only.split(","))
        todo = [r for r in todo if r[0] in want]
    if a.start:
        todo = todo[[r[0] for r in todo].index(a.start):]
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        save_room = next((int(w, 16), int(r, 16)) for s, w, r in (l.split() for l in fh) if int(s) == SLOT)
    csv = os.path.join(OUT, "rooms.csv")
    summary = os.path.join(OUT, "summary.txt")
    if not os.path.exists(csv):
        with open(csv, "w", encoding="utf-8") as fh:
            fh.write("time,room,world,id,game,free_kb,drive_kb,neg_kb,copy_made,result\n")
    with open(summary, "a", encoding="utf-8") as fh:
        fh.write(f"=== room sweep {time.strftime('%Y-%m-%d %H:%M:%S')} {len(todo)} rooms from {todo[0][0]}"
                 f"{' --both' if a.both else ' --drive' if a.drive else ''} env {' '.join(a.env) or '-'}\n")
    copies, world_loaded, done, failed = None, None, 0, 0
    for name, world, room in todo:
        t0 = time.time()
        notes = []
        try:
            if copies is None:
                copies = bench.launch(a.env)
                world_loaded = None
            if world_loaded != world:  # a fresh story state for each world
                if world_loaded is not None:
                    back_to_title(copies)
                tx.load_both(copies, SLOT, save_room)
                world_loaded = world
            mark = {c.name: c.mark() for c in copies}
            bench.warp(world, room)
            got = settle(copies, world, room, mark)
            result = drive_test(copies, a.both) if a.drive else {}
            for c in copies:
                made = bool(tx.copies_made(c.since(mark[c.name])))
                free, drive, neg = got[c.name]
                with open(csv, "a", encoding="utf-8") as fh:
                    fh.write(f"{time.strftime('%H:%M:%S')},{name},{world:02X},{room:02X},{c.name},{free},{drive},{neg},"
                             f"{int(made)},\"{result.get(c.name, '')}\"\n")
                notes.append(f"{c.name} free {free} KB drive {drive} neg {neg} copy {int(made)}")
            notes.extend(f"{k}: {v}" for k, v in result.items())
            ok = True
        except (bench.Failed, SystemExit, OSError) as e:  # OSError: a game gone while we read its memory
            ok = False
            notes.append(f"ERROR: {e}")
            for c in copies or []:
                if c.crash:
                    notes.append(f"{c.name} crash: {c.crash}")
                if c.alive():
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{name}_{c.name}_error.png"))
            if copies:
                bench.close(copies)
            copies = None
        done += ok
        failed += not ok
        line = f"{name} {world:02X}/{room:02X}: {'OK' if ok else 'FAIL'} ({time.time() - t0:.0f} s) | " + " | ".join(notes)
        print(line, flush=True)
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")
    if copies:
        bench.close(copies)
    with open(summary, "a", encoding="utf-8") as fh:
        fh.write(f"  done: {done} measured, {failed} failed\n")
    print(f"done: {done} measured, {failed} failed")


if __name__ == "__main__":
    main()
