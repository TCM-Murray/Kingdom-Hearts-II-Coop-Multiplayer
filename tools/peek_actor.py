"""Read-only check of the player-actor leads in every running KH2 copy.

Usage: py tools/peek_actor.py [--watch]

Cross-checks (leads from Volpestyle/kh2-multiplayer, see MODLOG.md):
  - active entity list head (exe+0x2A171C8) == camera's followed actor (exe+0x718C60 +0x50)
  - entity = actor+0x640: position +0x30 (x,y,z,w) with w == 1.0, angle +0x4C,
    cos +0x40 / sin +0x48 consistent with the angle
  - animation id actor+0x180, motion clock actor+0x19C, airborne entity+0x104
  - model name: ASCII found in the objentry record at *(actor+0x918)
"""
import math
import re
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import peek  # noqa: E402

LIST_HEAD = 0x2A171C8
CAMERA = 0x718C60
CAM_ACTOR = 0x50


def q(h, a):
    return struct.unpack("<Q", peek.read(h, a, 8))[0]


def f(h, a, n=1):
    return struct.unpack(f"<{n}f", peek.read(h, a, 4 * n))


def u32(h, a):
    return struct.unpack("<I", peek.read(h, a, 4))[0]


def describe(h, base):
    head = q(h, base + LIST_HEAD)
    cam_actor = q(h, base + CAMERA + CAM_ACTOR)
    if not head:
        return "no actor (title screen / loading)"
    ent = head + 0x640
    x, y, z, w = f(h, ent + 0x30, 4)
    cos_, = f(h, ent + 0x40)
    sin_, = f(h, ent + 0x48)
    ang, = f(h, ent + 0x4C)
    anim = u32(h, head + 0x180)
    clock, = f(h, head + 0x19C)
    air = u32(h, ent + 0x104)
    name = "?"
    try:
        rec = q(h, head + 0x918)
        blob = peek.read(h, rec, 0x60)
        words = re.findall(rb"[A-Za-z0-9_]{4,}", blob)
        name = ",".join(wd.decode() for wd in words[:2]) or "?"
    except OSError:
        pass
    trig = abs(cos_ - math.cos(ang)) < 1e-3 and abs(sin_ - math.sin(ang)) < 1e-3
    return (f"head=0x{head:X} camActor=0x{cam_actor:X} same={head == cam_actor} model={name} | "
            f"pos=({x:.1f},{y:.1f},{z:.1f}) w={w:.2f} ang={ang:.3f} cos/sin match={trig} | "
            f"anim={anim} clock={clock:.0f} airborne={air}")


def main():
    pids = peek.find_pids(peek.EXE)
    if not pids:
        sys.exit("KH2 is not running")
    handles = []
    for pid in pids:
        base = peek.module_base(pid, peek.EXE)
        h = peek.k32.OpenProcess(peek.PROCESS_VM_READ | peek.PROCESS_QUERY_INFORMATION, False, pid)
        handles.append((pid, h, base))
    while True:
        for pid, h, base in handles:
            print(f"pid {pid}: {describe(h, base)}", flush=True)
        if "--watch" not in sys.argv:
            break
        time.sleep(0.5)


if __name__ == "__main__":
    main()
