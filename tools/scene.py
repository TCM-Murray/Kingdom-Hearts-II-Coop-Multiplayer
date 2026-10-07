"""Scripted test scenes for a copy driven by the virtual pad (no human needed).

Usage: py tools/scene.py <vpad port> <scene>
Scenes:
  load        title screen -> Cargar -> the highlighted save (slot 33, Merlin's House)
  outside     from Merlin's House (04/0D) walk out to Hollow Bastion 04/09 (Heartless there)
  hb          load + outside
  street      from HB 04/09: walk away from the door to the open street
              (x z optional, default 400 -500), so test patterns can't re-enter the house
  bench       hb + street

Never presses triangle on the load screen (that is "delete file").
Menu steps are timed for this PC; each step checks the world/room bytes where it can.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nav import Game  # noqa: E402


def wait_for(g, cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return
        g.pad.send(ms=200)
    raise SystemExit(f"timed out waiting for {what} ({g.where()})")


def load(g):
    # Left alone at the title, the game plays its intro movie (world 01/01): start skips back to the title.
    end = time.time() + 90
    while g.room()[:2] != (0xFF, 0xFF):
        if time.time() > end:
            raise SystemExit(f"timed out waiting for the title screen ({g.where()})")
        if g.room()[:2] == (0x01, 0x01):
            g.pad.run(["tap", "start", "wait", "2000"])
        g.pad.send(ms=500)
    g.pad.send(ms=6000)               # title menu fades in
    g.pad.run(["tap", "down", "wait", "500", "tap", "cross", "wait", "5000"])  # Cargar, save list
    g.pad.run(["tap", "cross"])       # highlighted slot (the last one used: 33)
    wait_for(g, lambda: g.room()[:2] == (0x04, 0x0D) and g.sora(), 60, "Merlin's House")
    g.pad.send(ms=3000)
    print("loaded:", g.where())


def outside(g):
    if g.room()[:2] != (0x04, 0x0D):
        raise SystemExit(f"not in Merlin's House: {g.where()}")
    # Exit trigger found by sweeping the west wall: walking -x at z = -600 (or -650) from x = -540.
    for z in (-600, -650, -560):
        g.goto(-450, z, 25, 8)
        g.goto(-540, z, 20, 5)
        result = g.goto(-800, z, 20, 3)
        if result == "room changed":
            break
    if result != "room changed":
        raise SystemExit(f"door not reached ({result}): {g.where()}")
    wait_for(g, lambda: g.room()[:2] == (0x04, 0x09) and g.sora(), 30, "HB 04/09")
    print("outside:", g.where())


def street(g, x=400.0, z=-500.0):
    if g.room()[:2] != (0x04, 0x09):
        raise SystemExit(f"not in HB 04/09: {g.where()}")
    g.goto(1000, -500, 40, 10)  # straight off the door spot first
    g.goto(x, z, 40, 12)
    print("street:", g.where())


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    g = Game(int(sys.argv[1]))
    scene = sys.argv[2]
    if scene in ("load", "hb", "bench"):
        load(g)
    if scene in ("outside", "hb", "bench"):
        outside(g)
    if scene in ("street", "bench"):
        args = [float(v) for v in sys.argv[3:5]]
        street(g, *args)


if __name__ == "__main__":
    main()
