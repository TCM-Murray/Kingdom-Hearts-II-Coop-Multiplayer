"""Bench for the World Map's key door in co-op (TODO 5.2): save 96 starts both games on the map, near an open key hole.

Usage: py tools/test_gummi_door.py

Two rounds, the host first and then the friend first:
  1. one player picks Proceed: held, still free on the map, the other is told, the long INFO message scrolls to its end
     and starts over;
  2. the other picks Proceed too: both reach the Gummi menu;
  3. the first leaves by its World Map: back on the map within LEAVE_S while the other stays in the Gummi menu;
  4. the other leaves the same way.
Everything is read from the logs and memory. Exit code 0 = every check passed. The real saves are never written (bench.py).
"""
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench  # noqa: E402
from bench import Failed, wait  # noqa: E402
import peek  # noqa: E402

IN_FIELD = 0x9BA8D0                 # u8: 0 in Gummi mode
HOVER, HOVER_LABEL = 0xBF3010, 0x2EA0   # -> the map's label of the key hole under the ship; +0x2EA0 i32 route + 1
WORLD_MENU = 0xBF2FC0               # -> the key hole's World Menu while it's open
INFO_BAR, INFO_X, INFO_END = 0xABE828, 0x240, 0x498  # -> the INFO bar; f32 text x, i32 x where the text has passed
GUMMI_MENU, GUMMI_SCREEN = (0xAFB420, 0xAF9FA0), 0xAF9FB0  # both non-zero while a Gummi menu lives; its screen's name
HOLE, LABEL = (5458.0, -5744.0), 1  # save 96's open key hole: Asteroid Sweep, route 0
LEAVE_S = 8  # Yes to the map: about 4 s; the bug this guards held the black screen until the other game left too
FAILS = []


def check(ok, text):
    print(f"  {'ok  ' if ok else 'FAIL'} {text}", flush=True)
    if not ok:
        FAILS.append(text)


def q(c, off):
    return c.game.q(c.game.base + off)


def hover(c):
    o = q(c, HOVER)
    return struct.unpack("<i", peek.read(c.game.h, o + HOVER_LABEL, 4))[0] if o else None


def in_field(c):
    return peek.read(c.game.h, c.game.base + IN_FIELD, 1)[0] != 0


def screen(c):
    if not all(q(c, off) for off in GUMMI_MENU):
        return None
    return peek.read(c.game.h, c.game.base + GUMMI_SCREEN, 12).split(b"\0")[0].decode("ascii", "replace")


def info(c):
    """(text x, x where the text has passed), or None without an INFO bar."""
    b = q(c, INFO_BAR)
    if not b:
        return None
    return (struct.unpack("<f", peek.read(c.game.h, b + INFO_X, 4))[0],
            struct.unpack("<i", peek.read(c.game.h, b + INFO_END, 4))[0])


def fly(c):
    """Steers the ship to the key hole by its position. On the map stick up = +z, right = +x, whatever the camera does."""
    g, end = c.game, time.time() + 30
    while time.time() < end:
        s = g.sora()
        dx, dz = HOLE[0] - s[0], HOLE[1] - s[2]
        d = math.hypot(dx, dz)
        if hover(c) == LABEL and d < 700:  # the key hole's collision stops the ship about 450 away
            g.pad.send(ms=300)
            return
        k = min(1.0, max(0.3, d / 800))
        g.pad.send(lx=k * dx / d, ly=k * dz / d, ms=40)
    g.pad.send()
    raise Failed(f"copy {c.name}: the key hole wasn't reached ({g.where()})")


def logged(c, mark, text):
    return any(text in line for line in c.since(mark))


def proceed(c, copies):
    c.game.pad.run(["tap", "triangle"])
    wait(f"copy {c.name}'s World Menu", lambda: q(c, WORLD_MENU), 10, copies, step=0.1)
    time.sleep(0.7)
    c.game.pad.run(["tap", "cross"])  # the menu opens on Proceed


def scrolls(c, copies):
    """Waits until the INFO text has moved to its end and is back at its start; returns (start, lowest x)."""
    start = low = None

    def restarted():
        nonlocal start, low
        v = info(c)
        if not v:
            return False
        start = v[0] if start is None else start
        low = v[0] if low is None else min(low, v[0])
        return v[0] >= start - 1 and low < start - 100
    wait(f"copy {c.name}'s INFO text to scroll and start over", restarted, 45, copies, step=0.1)
    return start, low


def leave(c, copies):
    """Top menu: Circle = cursor to World Map, Cross = its question, which starts on No. Returns the seconds from Yes."""
    c.game.pad.run(["tap", "circle", "wait", "700", "tap", "cross", "wait", "1000", "tap", "left", "wait", "500",
                    "tap", "cross"])
    t0 = time.time()
    wait(f"copy {c.name} back on the map", lambda: in_field(c), 60, copies, step=0.1)
    return time.time() - t0


def one_round(first, second, copies):
    print(f"--- copy {first.name} goes first", flush=True)
    marks = {c.name: c.mark() for c in copies}
    bench.both(fly, copies)

    proceed(first, copies)
    wait("the first player held", lambda: logged(first, marks[first.name], "held, waiting for the other player"), 10,
         copies)
    check(in_field(first) and not q(first, WORLD_MENU), "held: the first player is on the map, its menu closed")
    a = first.game.sora()
    first.game.pad.send(lx=1, ms=300)
    first.game.pad.send(ms=300)
    b = first.game.sora()
    check(abs(b[0] - a[0]) > 50, f"held: its ship still flies (moved {b[0] - a[0]:+.0f})")
    fly(first)
    wait("the other player told", lambda: (info(second) or (0, 0))[1] < 0, 10, copies, step=0.1)
    start, low = scrolls(first, copies)
    check(True, f"the INFO text scrolled from x {start:.0f} to {low:.0f} and started over")

    proceed(second, copies)
    for c in copies:
        wait(f"copy {c.name} to proceed", lambda: logged(c, marks[c.name], "proceeds for both players"), 15, copies)
    wait("both in the Gummi menu", lambda: all(screen(c) == "top" for c in copies), 40, copies)
    check(not any(in_field(c) for c in copies), "both reached the Gummi menu")
    time.sleep(4)  # the top menu's opening animation eats presses

    secs = leave(first, copies)
    check(secs <= LEAVE_S, f"the first player left in {secs:.1f} s")
    check(screen(second) == "top" and not in_field(second), "the other player stayed in the Gummi menu")
    secs = leave(second, copies)
    check(secs <= LEAVE_S, f"the other player left in {secs:.1f} s")
    wait("both ships back at the key hole", lambda: all(hover(c) == LABEL for c in copies), 15, copies)
    for c in copies:
        check(not c.crash, f"copy {c.name}: no crash")


def main():
    copies = bench.launch()
    try:
        bench.load_slot_both(copies, 96, (0x0F, 0x00), "the World Map")
        one_round(copies[0], copies[1], copies)
        one_round(copies[1], copies[0], copies)
    except Failed as e:
        check(False, str(e))
    finally:
        bench.close(copies)
    print("PASS" if not FAILS else f"FAILED, {len(FAILS)}: " + "; ".join(FAILS))
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
