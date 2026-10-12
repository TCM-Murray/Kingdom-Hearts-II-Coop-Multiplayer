"""World mirror research (TODO 1.55): a bench pair in a boss room with the actor census and the script call log
on (KH2COOP_CENSUS=1, KH2COOP_TRAP_LOG=1, src/dll/world_census.cpp), so both games' logs show what each one has
and does.

Usage: py tools/test_census.py <scenario> [--seconds N] [--shot-every S] [--no-triangle] [--friend-attacks]
                                [--env KEY=VALUE ...]

Scenarios (save from the co-op save copy, then the host warps; programs map/battle/event from the session 29
two-PC log):
  bc       Beast's Castle ballroom 0x05/0x05 program 78 (Shadow Stalker; then 79, Dark Thorn), save 13
  bc2      the same room, program 79 (Dark Thorn) directly
  twins    Agrabah 0x07/0x03 program 59 (Volcanic Lord and Blizzard Lord), save 20
  shanyu   Land of Dragons 0x08/0x09 program 75 (Shan-Yu), save 10
  gusts    Land of Dragons 0x08/0x08 programs 0/1/0 (the room before the summit), save 10
  room     no warp: stay in the save's room (--slot N)

The host's Sora attacks (cross) and presses triangle now and then (reaction commands, Limits; not with
--no-triangle); with --friend-attacks the friend's does too. God mode on. Screenshots (every 20 s, or --shot-every)
and a summary go to build/bench/census/.
The real Steam save is never written (SAVE_GUARD=1); the co-op save copy is only read.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
import scene  # noqa: E402

OUT = os.path.join(bench.ROOT, "build", "bench", "census")
# scenario: (save, the room that save loads, warps)
SCENARIOS = {
    "bc": (13, (0x05, 0x01), [(0x05, 0x05, 0, 78, 78, 78)]),
    "bc2": (13, (0x05, 0x01), [(0x05, 0x05, 0, 79, 79, 79)]),
    "twins": (20, (0x07, 0x02), [(0x07, 0x03, 0, 59, 59, 59)]),
    "shanyu": (10, (0x08, 0x00), [(0x08, 0x09, 0, 75, 75, 75)]),
    "gusts": (10, (0x08, 0x00), [(0x08, 0x08, 0, 0, 1, 0)]),
    "room": (None, None, []),
}


def warp(world, room, door, map_, btl, evt):
    bench.debug("A", 12, world, room, door, 0, map_, btl, evt, 0, fmt="<IiBBBBHHHH")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--slot", type=int)
    ap.add_argument("--friend-attacks", action="store_true")
    ap.add_argument("--shot-every", type=float, default=20)
    ap.add_argument("--no-triangle", action="store_true")
    ap.add_argument("--env", action="append", default=[])
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    slot, room, warps = SCENARIOS[a.scenario]
    if a.slot:
        slot, room = a.slot, None
    if not slot:
        raise SystemExit("--slot needed")
    out = [f"scenario {a.scenario}, save {slot}, warps {warps}, {a.seconds} s"]
    copies = bench.launch(extra=["KH2COOP_CENSUS=1", "KH2COOP_TRAP_LOG=1"] + a.env)
    host, friend = copies
    ok = False
    try:
        for c in copies:
            mark = c.mark()
            scene.load_slot(c.game, slot)
            # A press lost in the save list loads the save next to it (bench 2026-10-12 18:25: the friend got save 12,
            # Donald instead of Beast). The friend follows the host's room within ~2 s, so take the first room its
            # log shows after the pick.
            first = next((re.search(r"world 0x(..) room 0x(..)", l) for l in c.since(mark)
                          if re.search(r" frame \d+ world 0x.. room 0x..", l) and "world 0xFF" not in l), None)
            loaded = (int(first.group(1), 16), int(first.group(2), 16)) if first else None
            if room and loaded != room:
                raise bench.Failed(f"copy {c.name} loaded the wrong save: first room {loaded}, save {slot} is {room}")
        bench.wait("both in the save's room", lambda: host.room() == friend.room(), 60, copies)
        time.sleep(6)
        for w in warps:
            warp(*w)
            bench.wait(f"both in 0x{w[0]:02X}/0x{w[1]:02X}",
                       lambda: all(c.room() == (w[0], w[1]) and c.game.sora() for c in copies), 90, copies)
        time.sleep(8)
        marks = {c.name: c.mark() for c in copies}
        start = time.time()
        shot = 0
        while time.time() - start < a.seconds:
            t = time.time() - start
            if t >= shot * a.shot_every:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{c.name}_{shot:02d}.png"))
                shot += 1
            fighters = copies if a.friend_attacks else [host]
            for c in fighters:
                c.game.pad.run(["tap", "triangle" if int(t) % 6 == 0 and not a.no_triangle else "cross", "wait", "250"])
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name}: {c.crash or 'closed'}")
            time.sleep(0.3)
        for c in copies:
            new = c.since(marks[c.name])
            traps = [l for l in new if " trap " in l[:20]]
            census = [l for l in new if "census:" in l]
            out.append(f"{c.name}: log {c.log}, {len(new)} lines in the fight ({len(traps)} trap calls, "
                       f"{len(census)} census), room {c.room()}, alive {c.alive()}, crash {c.crash or '-'}")
        ok = all(c.alive() and not c.crash for c in copies)
    except bench.Failed as e:
        out.append(f"FAILED: {e}")
    finally:
        bench.close(copies)
    out.append("PASS" if ok else "FAIL")
    text = "\n".join(out)
    print(text)
    with open(os.path.join(OUT, f"{a.scenario}.txt"), "w", encoding="utf-8") as fh:
        fh.write(text + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
