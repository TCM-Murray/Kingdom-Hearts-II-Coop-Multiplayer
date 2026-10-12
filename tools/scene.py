"""Scripted test scenes for a copy driven by the virtual pad (no human needed).

Usage: py tools/scene.py <vpad port> <scene>
Scenes:
  slot <n> [world room]  title screen -> LOAD (CO-OP) -> save n; waits for that room (hex) when given
              (the co-op save copy in build/test_coop_saves, since session 28: 45 = TWTNW Altar of
              Naught 12/12, before the final fight)
  load        title screen -> LOAD (CO-OP) -> the highlighted save (the last one saved: 97, Merlin's House)
  load-normal title screen -> LOAD (the mod stays off) -> the highlighted save
  oc          title screen -> LOAD (CO-OP) -> one save up (made for the old test files in
              build/test_coop_saves/hb_old: index 31, Olympus 06/0A with Auron; not in the co-op save copy)
  outside     from Merlin's House (04/0D) walk out to Hollow Bastion 04/09 (Heartless there)
  hb          load + outside
  street      from HB 04/09: walk away from the door to the open street
              (x z optional, default 400 -500), so test patterns can't re-enter the house
  bench       hb + street

Never presses triangle on the load screen (that is "delete file").
Menu steps are timed for this PC; each step checks the world/room bytes where it can.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import peek  # noqa: E402
from nav import Game  # noqa: E402

TITLE_ROWS_LAYOUT = 0xB1D5D8  # u64: non-zero while the title menu is up (VERIFIED_OFFSETS 'Title menu state')
TITLE_CURSOR = 0xB1D5E4       # s32: highlighted row (2 = LOAD (CO-OP))


def wait_for(g, cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return
        g.pad.send(ms=200)
    raise SystemExit(f"timed out waiting for {what} ({g.where()})")


def title_menu(g, timeout=120):
    """Get to the title menu. The world bytes can't tell (after the intro movie the title shows with
    01/01, not FF/FF), the menu's rows layout can. Left alone, the title plays the intro movie again:
    start skips it. Start is pressed only while the menu is not up (on the menu it would pick NEW GAME)."""
    def up():
        return struct.unpack("<Q", peek.read(g.h, g.base + TITLE_ROWS_LAYOUT, 8))[0] != 0
    end = time.time() + timeout
    while not up():
        if time.time() > end:
            raise SystemExit(f"timed out waiting for the title menu ({g.where()})")
        g.pad.run(["tap", "start"])
        for _ in range(30):
            if up():
                break
            time.sleep(0.1)
    g.pad.send(ms=1000)               # the rows finish fading in


def open_save_list(g, coop=True):
    title_menu(g)
    rows = ["tap", "down", "wait", "500"] * (2 if coop else 1)  # LOAD (CO-OP) or LOAD
    g.pad.run(rows)
    row = struct.unpack("<i", peek.read(g.h, g.base + TITLE_CURSOR, 4))[0]
    if row != (2 if coop else 1):
        raise SystemExit(f"title menu: row {row} highlighted, not {'LOAD (CO-OP)' if coop else 'LOAD'}")
    g.pad.run(["tap", "cross", "wait", "8000"])  # 'Checking PC for save data', then the save list


def load_slot(g, slot, expect=None, name=None):
    """Load save <slot> (1-100) through LOAD (CO-OP). The list opens on the last saved slot; holding up
    stops at slot 1 (no wrap), so: hold up, then slot-1 taps down."""
    open_save_list(g)
    g.pad.run(["hold", "up", "8000", "wait", "500"] + ["tap", "down", "wait", "100"] * (slot - 1) + ["wait", "500"])
    g.pad.run(["tap", "cross"])
    if expect:
        wait_for(g, lambda: g.room()[:2] == expect and g.sora(), 90, name or f"{expect[0]:02X}/{expect[1]:02X}")
    else:
        wait_for(g, lambda: g.room()[:2] not in ((0x01, 0x01), (0xFF, 0xFF)) and g.sora(), 90, f"save {slot}")
    g.pad.send(ms=3000)
    print(f"loaded save {slot}:", g.where())


def load(g, ups=0, expect=(0x04, 0x0D), name="Merlin's House", coop=True):
    open_save_list(g, coop)
    for _ in range(ups):              # older saves sit above the highlighted one
        g.pad.run(["tap", "up", "wait", "400"])
    g.pad.run(["tap", "cross"])       # highlighted slot (the last one saved)
    wait_for(g, lambda: g.room()[:2] == expect and g.sora(), 60, name)
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
    if scene == "slot":
        expect = (int(sys.argv[4], 16), int(sys.argv[5], 16)) if len(sys.argv) > 5 else None
        load_slot(g, int(sys.argv[3]), expect)
    if scene in ("load", "hb", "bench"):
        load(g)
    if scene == "load-normal":
        load(g, coop=False)
    if scene == "oc":
        load(g, 1, (0x06, 0x0A), "Olympus 06/0A")
    if scene in ("outside", "hb", "bench"):
        outside(g)
    if scene in ("street", "bench"):
        args = [float(v) for v in sys.argv[3:5]]
        street(g, *args)


if __name__ == "__main__":
    main()
