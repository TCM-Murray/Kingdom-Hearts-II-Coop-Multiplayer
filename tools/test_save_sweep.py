"""Automated test: load every co-op save in a bench pair and check the co-op basics in each (extra copy).

Usage: py tools/test_save_sweep.py [--slots 1,5,9-12] [--env KEY=VALUE ...] [--no-reload]

For each save (default: every save in the co-op save copy, build/bench/coop_saves.txt):
1. Both copies load it through LOAD (CO-OP) (host = A, friend = B).
2. Checks in each game, from its log: the Sora copy was made ('extra copy: ... made'), our Sora kept
   the player pointer, the actor list head and the camera ('after our Sora: ... our Sora' x3), the
   puppet found it; no crash; both games in the same room.
3. Reloads the room once (debug warp to the same room, the friend follows) and checks the copy again.
4. Screenshots of both, then both back to the title (debug link 4) for the next save.
Results: build/bench/save_sweep/summary.txt (one line per save, written as it goes) and screenshots.
A crash or a stuck copy restarts the pair and the sweep goes on with the next save.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import scene  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "save_sweep")


def parse_slots(text):
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


both = bench.both  # fn(c) for each copy in parallel


def copy_checks(c, mark):
    """What this copy's log says about the Sora copy since mark."""
    made = after = found = None
    party = ""
    for line in c.since(mark):
        if "extra copy:" in line and ": made" in line:
            made = line
        if "extra copy: after our Sora:" in line:
            after = line.split("after our Sora: ", 1)[1]
        if "puppet: Sora copy found" in line:
            found = line
        if "hud: party list =" in line:
            party = line.split("party list = ", 1)[1]
    problems = []
    if not made:
        problems.append("no copy made")
    if after is not None and after.count("our Sora") != 3:
        problems.append("player/list/camera NOT ours: " + after)
    if made and not found:
        problems.append("copy made but the puppet didn't find it")
    return problems, party


def test_slot(copies, slot, room, reload):
    host, friend = copies
    marks = {c.name: c.mark() for c in copies}
    both(lambda c: scene.load_slot(c.game, slot, room), copies)
    bench.wait("both in the save's room", lambda: all(c.room() == room for c in copies), 60, copies)
    time.sleep(8)
    notes = []
    for c in copies:
        problems, party = copy_checks(c, marks[c.name])
        notes.append(f"{c.name}: {', '.join(problems) if problems else 'OK'} | party {short(party)}")
    for c in copies:
        bench.screenshot(c.hwnd, os.path.join(OUT, f"{slot:02d}_{c.name}.png"))
    ok = all(": OK" in n for n in notes)
    if reload:
        marks = {c.name: c.mark() for c in copies}
        bench.warp(*room)
        time.sleep(3)
        bench.wait("both back in the room after the reload",
                   lambda: all(c.room() == room and c.game.sora() for c in copies), 60, copies)
        time.sleep(8)
        for c in copies:
            new = c.since(marks[c.name])
            if c is friend and not any("puppet: Sora copy gone" in l for l in new):
                notes.append("reload B: not reloaded (the friend follows room changes only)")
                continue
            if c is host and not any("debug warp: asked" in l for l in new):
                notes.append("reload A: warp not done")
                ok = False
                continue
            problems, _ = copy_checks(c, marks[c.name])
            notes.append(f"reload {c.name}: {', '.join(problems) if problems else 'OK'}")
            ok &= not problems
    return ok, notes


def short(party):
    return party.replace(" exe+0x0", "").replace("party member", "member").split(" (flags")[0]


def back_to_title(copies):
    for c in copies:
        bench.to_title(c.name)
    both(lambda c: scene.title_menu(c.game, 90), copies)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--slots", default="")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--no-reload", action="store_true")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    saves = {}
    with open(os.path.join(bench.ROOT, "build", "bench", "coop_saves.txt")) as fh:
        for line in fh:
            s, w, r = line.split()
            saves[int(s)] = (int(w, 16), int(r, 16))
    slots = parse_slots(a.slots) if a.slots else sorted(saves)
    summary = os.path.join(OUT, "summary.txt")
    with open(summary, "a", encoding="utf-8") as fh:
        fh.write(f"=== save sweep {time.strftime('%Y-%m-%d %H:%M:%S')} slots {slots[0]}..{slots[-1]} "
                 f"env {' '.join(a.env) or '-'}\n")
    copies = None
    passed = failed = 0
    for slot in slots:
        room = saves[slot]
        t0 = time.time()
        try:
            if copies is None:
                copies = bench.launch(["KH2COOP_EXTRA_COPY=1"] + a.env)
                with open(summary, "a", encoding="utf-8") as fh:
                    fh.write(f"  pair started: A pid {copies[0].proc.pid}, B pid {copies[1].proc.pid}\n")
            ok, notes = test_slot(copies, slot, room, not a.no_reload)
            back_to_title(copies)
        except (bench.Failed, SystemExit) as e:
            ok, notes = False, [f"ERROR: {e}"]
            for c in copies or []:
                if c.crash:
                    notes.append(f"{c.name} crash: {c.crash}")
            if copies:
                for c in copies:
                    if c.alive():
                        bench.screenshot(c.hwnd, os.path.join(OUT, f"{slot:02d}_{c.name}_error.png"))
                bench.close(copies)
            copies = None
        passed += ok
        failed += not ok
        line = (f"slot {slot:2d} {room[0]:02X}/{room[1]:02X}: {'PASS' if ok else 'FAIL'} ({time.time() - t0:.0f} s) | "
                + " | ".join(notes))
        print(line, flush=True)
        with open(summary, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")
    if copies:
        bench.close(copies)
    with open(summary, "a", encoding="utf-8") as fh:
        fh.write(f"  done: {passed} PASS, {failed} FAIL\n")
    print(f"done: {passed} PASS, {failed} FAIL")


if __name__ == "__main__":
    main()
