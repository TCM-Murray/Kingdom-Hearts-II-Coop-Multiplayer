"""Walk Sora around with the virtual pad by reading his position (read-only memory reads).

Usage:
  py tools/nav.py <vpad port> where              print world/room, position, facing
  py tools/nav.py <vpad port> goto <x> <z> [tol=40] [timeout s=10]
      Walks to (x, z) in a closed loop. Stops early if the room changes.
  py tools/nav.py <vpad port> dir <world angle deg> <ms>
      Runs in a world direction (0 = +x, 90 = +z) for ms.

The copy must be launched with KH2COOP_VPAD=<port> (tools/launch_pair.py --vpad-a).
When several copies run, the one whose log says it listens on <port> is used.
Stick-to-world mapping comes from the camera: stick up = from the camera
towards Sora. Camera position at exe+0x718C60 +0x18 (x, y, z), yaw at +0x5C
(found 2026-10-05 by turning the camera with the right stick; not yet in
VERIFIED_OFFSETS.md).
"""
import glob
import math
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import peek  # noqa: E402
from vpad import Pad  # noqa: E402

LIST_HEAD = 0x2A171C8
NOW = 0x717008
CAMERA = 0x718C60
LOGS = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\mod\kh2\dll"


def pid_for_port(port):
    pids = set(peek.find_pids(peek.EXE))
    for path in glob.glob(os.path.join(LOGS, "kh2coop_*.log")):
        pid = int(re.search(r"kh2coop_(\d+)\.log", path).group(1))
        if pid not in pids:
            continue
        with open(path, encoding="utf-8", errors="replace") as fh:
            head = fh.read(4000)
        if f"virtual pad on 127.0.0.1:{port}: listening" in head:
            return pid
    raise SystemExit(f"no running copy with a virtual pad on port {port}")


class Game:
    def __init__(self, port):
        self.pid = pid_for_port(port)
        self.base = peek.module_base(self.pid, peek.EXE)
        self.h = peek.k32.OpenProcess(peek.PROCESS_VM_READ | peek.PROCESS_QUERY_INFORMATION, False, self.pid)
        self.pad = Pad(port)

    def room(self):
        return tuple(peek.read(self.h, self.base + NOW, 3))

    def sora(self):
        """(x, y, z, facing angle) or None while no actor exists."""
        head = struct.unpack("<Q", peek.read(self.h, self.base + LIST_HEAD, 8))[0]
        if not head:
            return None
        x, y, z = struct.unpack("<3f", peek.read(self.h, head + 0x640 + 0x30, 12))
        ang, = struct.unpack("<f", peek.read(self.h, head + 0x640 + 0x4C, 4))
        return x, y, z, ang

    def where(self):
        w, r, d = self.room()
        s = self.sora()
        pos = f"pos ({s[0]:.0f}, {s[1]:.0f}, {s[2]:.0f}) facing {math.degrees(s[3]):.0f} deg" if s else "no actor"
        return f"world 0x{w:02X} room 0x{r:02X} door 0x{d:02X} | {pos}"

    def camera(self):
        return struct.unpack("<3f", peek.read(self.h, self.base + CAMERA + 0x18, 12))

    def push(self, world_angle, ms=100, buttons=0):
        """Hold the stick so Sora runs toward world_angle (radians, 0 = +x, pi/2 = +z)."""
        s = self.sora()
        cx, _, cz = self.camera()
        if s:
            fx, fz = s[0] - cx, s[2] - cz
            n = math.hypot(fx, fz) or 1.0
            fx, fz = fx / n, fz / n
        else:
            fx, fz = 0.0, 1.0
        dx, dz = math.cos(world_angle), math.sin(world_angle)
        ly = dx * fx + dz * fz          # along camera forward
        lx = dx * fz - dz * fx          # along camera right = (fz, -fx)
        self.pad.send(buttons, lx=lx, ly=ly, ms=ms)

    def run(self, world_deg, ms):
        end = time.time() + ms / 1000
        while time.time() < end:
            self.push(math.radians(world_deg))
        self.pad.send()

    def goto(self, tx, tz, tol=40.0, timeout=10.0):
        start_room = self.room()
        end = time.time() + timeout
        while time.time() < end:
            if self.room()[:2] != start_room[:2]:
                self.pad.send()
                return "room changed"
            s = self.sora()
            if not s:
                self.pad.send(ms=100)
                continue
            dx, dz = tx - s[0], tz - s[2]
            if math.hypot(dx, dz) < tol:
                self.pad.send()
                return "arrived"
            self.push(math.atan2(dz, dx))
        self.pad.send()
        return "timeout"


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    g = Game(int(sys.argv[1]))
    cmd = sys.argv[2]
    if cmd == "where":
        print(g.where())
    elif cmd == "goto":
        tx, tz = float(sys.argv[3]), float(sys.argv[4])
        tol = float(sys.argv[5]) if len(sys.argv) > 5 else 40.0
        timeout = float(sys.argv[6]) if len(sys.argv) > 6 else 10.0
        print(g.goto(tx, tz, tol, timeout), "|", g.where())
    elif cmd == "dir":
        g.run(float(sys.argv[3]), int(sys.argv[4]))
        print(g.where())
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
