"""World mirror research (TODO 1.55): a bench pair in a boss room with the actor census and the script call log
on (KH2COOP_CENSUS=1, KH2COOP_TRAP_LOG=1, src/dll/world_census.cpp), so both games' logs show what each one has
and does.

Usage: py tools/test_census.py <scenario> [--seconds N] [--shot-every S] [--no-triangle] [--friend-attacks]
                                [--magic] [--cast] [--walk X,Z ...] [--env KEY=VALUE ...]

Scenarios (save from the co-op save copy, then the host warps; programs map/battle/event from the session 29
two-PC log):
  bc       Beast's Castle ballroom 0x05/0x05 program 78 (Shadow Stalker; then 79, Dark Thorn), save 13
  bc2      the same room, program 79 (Dark Thorn) directly
  twins    Agrabah 0x07/0x03 program 59 (Volcanic Lord and Blizzard Lord), save 20
  shanyu   Land of Dragons 0x08/0x09 program 75 (Shan-Yu), save 10
  gusts    Land of Dragons 0x08/0x08 programs 0/1/0 (the room before the summit), save 10
  borough  Hollow Bastion Borough 0x04/0x09 programs 1/1/0 (first visit: Shadows, Soldiers), save 97; both Soras
           walk onto the street, into the 4 Shadows' trigger (group 245)
  neo      the Borough with battle program 4 (second visit: Neoshadows M_EX420), save 97; both are teleported
           into the 2 Neoshadows' trigger (group 550, z -2367; test command, walking there gets stuck)
  room     no warp: stay in the save's room (--slot N)
--walk X,Z (repeatable) replaces the scenario's walk: both Soras walk these points in turn after the warp.
--teleport X,Y,Z replaces the scenario's teleport (before the walk; y grows downward).

Parity (TODO 1.55, one brain): with --env KH2COOP_PARITY=1 the run ends with tools/parity_compare.py on the two
logs (e.g. --env KH2COOP_ONE_BRAIN=1 --env KH2COOP_ONE_BRAIN_TYPES=M_EX020 for the Shadows with one brain).

--only-friend: only the friend's Sora attacks; the host's stands still.
--friend-menu valor|limit|summon: the friend uses it --menu-at seconds into the fight (TODO 4.4: do its kills count on
the host); --env KH2COOP_TRACE_STAT2=1 logs the stat change twin (exe+0x3D2C50) calls.
--lock-on: the attackers press R1 (lock on) every 4 s (a normal enemy's HP bar shows while locked on).
--bar-watch: both games' enemy HP bar is sampled during the fight (does it show the enemy's HP; TODO 1.21).
--pool: both games' effect pool is sampled during the fight (peak bytes in use; TODO 1.30).

The host's Sora attacks (cross) and presses triangle now and then (reaction commands, Limits; not with
--no-triangle); with --friend-attacks the friend's does too. With --magic the host's Sora uses its four shortcuts
(L1 + square / triangle / circle / cross) in turn instead of attacking; with --cast it casts the spells of its
six spell slots in turn through the test command (no menu, no MP; --cast-by friend: the friend's Sora does). God
mode on. Screenshots (every 20 s, or --shot-every)
and a summary go to build/bench/census/.
The real Steam save is never written (SAVE_GUARD=1); the co-op save copy is only read.
"""
import argparse
import os
import re
import subprocess
import sys
import threading
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
    "borough": (97, (0x04, 0x0D), [(0x04, 0x09, 2, 1, 1, 0)]),
    "neo": (97, (0x04, 0x0D), [(0x04, 0x09, 2, 1, 4, 0)]),
    "room": (None, None, []),
}
# Where both Soras walk after the warp (x, z; spawn groups from ard/us/hb09.ard, VERIFIED_OFFSETS 'Borough
# battle programs'). Door 2 puts them at (-605, -450); straight off the door spot first (scene.py street).
WALKS = {
    "borough": [(1000, -500), (400, -500)],
}
# --friend-menu: the friend's command menu keys (from tools/test_extra_copy.py forms / limit / summon); each step
# is (keys, then seconds to wait).
DRIVE = ["tap", "down", "wait", "300"] * 3  # Attack -> Magic -> Items -> Drive
PAGE2 = DRIVE + ["tap", "left", "wait", "1000"]  # Attack / Summon / Party / Limit, cursor on Limit
MENUS = {
    "valor": [(DRIVE + ["tap", "cross", "wait", "800", "tap", "cross", "wait", "500"], 2)],
    "limit": [(PAGE2, 1)] + [(["tap", "cross", "wait", "300"], 2.5)] * 4,
    "summon": [(PAGE2 + ["tap", "up", "wait", "500", "tap", "up", "wait", "500"], 1)] + [(["tap", "cross", "wait", "300"], 2.5)] * 4,
}
# Teleports (test command 15) before the walk: x, y (a little above the ground), z.
TELEPORTS = {
    "neo": (466, -30, -2300),
}


def warp(world, room, door, map_, btl, evt):
    bench.debug("A", 12, world, room, door, 0, map_, btl, evt, 0, fmt="<IiBBBBHHHH")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--slot", type=int)
    ap.add_argument("--friend-attacks", action="store_true")
    ap.add_argument("--only-friend", action="store_true", help="only the friend's Sora attacks (the host's stands)")
    ap.add_argument("--shot-every", type=float, default=20)
    ap.add_argument("--no-triangle", action="store_true")
    ap.add_argument("--triangle-by", choices=["all", "host", "friend"], default="all",
                    help="who presses triangle (Limits, reaction commands); with --friend-attacks both attack")
    ap.add_argument("--magic", action="store_true", help="the host uses its shortcuts (spells) instead of attacking")
    ap.add_argument("--cast", action="store_true", help="the host casts its six spell slots in turn (test command)")
    ap.add_argument("--cast-by", choices=["host", "friend"], default="host", help="who casts with --cast")
    ap.add_argument("--walk", action="append", default=[], help="X,Z: a point both Soras walk to after the warp")
    ap.add_argument("--teleport", help="X,Y,Z: both Soras are put there after the warp (before the walk)")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--friend-menu", choices=sorted(MENUS),
                    help="the friend goes into Valor Form / uses its first Limit / its first summon --menu-at s into the fight")
    ap.add_argument("--menu-at", type=float, default=5)
    ap.add_argument("--menu-by", choices=("friend", "host", "both"), default="friend")  # who uses --friend-menu
    ap.add_argument("--both-offset", type=float, default=0)  # --menu-by both: the host starts this many s later
    ap.add_argument("--both-after", choices=("friend", "host"))  # --menu-by both: the other uses its menu during this one's
    ap.add_argument("--lock-on", action="store_true", help="the attackers press R1 (lock on) every 4 s")
    ap.add_argument("--bar-watch", action="store_true",
                    help="sample both games' enemy HP bar during the fight: does it show the enemy's HP (TODO 1.21)")
    ap.add_argument("--pool", action="store_true", help="sample both games' effect pool during the fight (TODO 1.30)")
    a = ap.parse_args()
    walk = [tuple(float(v) for v in w.split(",")) for w in a.walk] or WALKS.get(a.scenario, [])
    teleport = tuple(float(v) for v in a.teleport.split(",")) if a.teleport else TELEPORTS.get(a.scenario)
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
        for c in copies:  # the host first: its game spawns the groups
            if teleport:
                bench.debug(c.name, 15, *teleport, fmt="<Iifff")
                time.sleep(1.5)
                out.append(f"{c.name} teleport to {teleport}: {c.game.where()}")
            for x, z in walk:
                result = c.game.goto(x, z, 60, 12)
                out.append(f"{c.name} walk to ({x:.0f}, {z:.0f}): {result} | {c.game.where()}")
        marks = {c.name: c.mark() for c in copies}
        bars = bench.BarWatch(copies) if a.bar_watch else None
        pools = bench.PoolWatch(copies) if a.pool else None
        start = time.time()
        shot = 0
        casts = 0
        menu_done = not a.friend_menu
        while time.time() - start < a.seconds:
            t = time.time() - start
            if not menu_done and t >= a.menu_at:
                menu_done = True
                users = [friend, host] if a.menu_by == "both" else [host if a.menu_by == "host" else friend]
                fms = [u.mark() for u in users]
                errors = []

                def use_menu(u, delay):
                    try:
                        time.sleep(delay)
                        for keys, wait in MENUS[a.friend_menu]:
                            u.game.pad.run(keys)
                            time.sleep(wait)
                    except Exception as e:  # noqa: BLE001 (a thread: report it in the summary)
                        errors.append(f"{u.name}: {e!r}")

                if a.menu_by == "both" and a.both_after:
                    # no threads: the one named by --both-after uses its whole menu (its Limit starts and runs on),
                    # then the other uses its menu while that Limit still runs
                    first, second = (host, friend) if a.both_after == "host" else (friend, host)
                    use_menu(first, 0)
                    use_menu(second, 0)
                else:
                    # both: each pad in its own thread; the host starts --both-offset s after the friend
                    threads = [threading.Thread(target=use_menu, args=(u, a.both_offset if u is host and len(users) > 1 else 0))
                               for u in users]
                    for th in threads:
                        th.start()
                    for th in threads:
                        th.join()
                if errors:
                    out.append(f"menu errors: {errors}")
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{a.friend_menu}_{c.name}.png"))
                for u, fm in zip(users, fms):
                    cmds = [l[13:110] for l in u.since(fm) if "carries out command" in l or "menu: command" in l]
                    out.append(f"{u.name}'s {a.friend_menu} at {t:.0f} s: commands {cmds[:6]}")
                continue
            if t >= shot * a.shot_every:
                for c in copies:
                    bench.screenshot(c.hwnd, os.path.join(OUT, f"{a.scenario}_{c.name}_{shot:02d}.png"))
                shot += 1
            fighters = [friend] if a.only_friend else copies if a.friend_attacks else [host]
            for c in fighters:
                if a.cast and c is host:
                    bench.debug("A" if a.cast_by == "host" else "B", 14, casts % 6)
                    c.game.pad.run(["wait", "900"])
                    for d in copies:  # the spell on both screens
                        bench.screenshot(d.hwnd, os.path.join(OUT, f"cast{casts:02d}_{d.name}.png"))
                    casts += 1
                    c.game.pad.run(["wait", "1600"])
                    continue
                if a.magic and c is host:
                    face = ("square", "triangle", "circle", "cross")[casts % 4]
                    casts += 1
                    c.game.pad.run(["hold", "l1", "400", "tap", "l1+" + face, "150", "wait", "1200"])
                    continue
                if a.lock_on and int(t) % 4 == 0:
                    c.game.pad.run(["tap", "r1", "wait", "150"])
                triangle = int(t) % 6 == 0 and not a.no_triangle and a.triangle_by in ("all", "host" if c is host else "friend")
                c.game.pad.run(["tap", "triangle" if triangle else "cross", "wait", "250"])
            for c in copies:
                c.lines()
                if c.crash or not c.alive():
                    raise bench.Failed(f"copy {c.name}: {c.crash or 'closed'}")
            time.sleep(0.3)
        if bars:
            bars.stop()
            for c in copies:
                out.append(f"{c.name} enemy HP bar: {bars.summary(c.name)}")
        if pools:
            pools.stop()
            for c in copies:
                out.append(f"{c.name} effect pool: {pools.summary(c.name)}")
        hits = [l for l in host.since(marks["A"]) if "friend hit our#" in l]
        out.append(f"host: friend's hits applied {len(hits)}, last: {[l[:120] for l in hits[-3:]]}")
        for c in copies:
            twin = [l for l in c.since(marks[c.name]) if "stat change twin on" in l]
            if twin:
                out.append(f"{c.name}: stat change twin calls {len(twin)}: {[l[13:160] for l in twin[:6]]}")
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
    if "KH2COOP_PARITY=1" in a.env:
        r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "parity_compare.py"),
                            host.log, friend.log], capture_output=True, text=True)
        out.append("parity_compare.py:\n" + (r.stdout + r.stderr).rstrip())
    out.append("PASS" if ok else "FAIL")
    text = "\n".join(out)
    print(text)
    with open(os.path.join(OUT, f"{a.scenario}.txt"), "w", encoding="utf-8") as fh:
        fh.write(text + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
