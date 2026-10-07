"""Measure how smoothly a Sora copy follows the real Sora (read-only memory reads).

Usage: py tools/netbench.py <mover vpad port> <viewer vpad port> [seconds=10] [-v] [--goofy]

--goofy: compare the mover's companion in friend slot 0 (Goofy) with the
viewer's slot-0 companion (mirrored from the host by world sync) instead of
Sora and his copy.

Drives the mover's Sora in a fixed pattern (run in a square, with stops) and,
about every 4 ms, samples the mover's real position and the position of its copy
in the viewer's game. Both copies must be in the same room. Reports:
  lag        delay that best lines the copy's path up with the real path
  error      average / 95th-percentile distance from the real path at that lag
  stalls     % of time the copy stood still for 2+ game frames while the real Sora ran
  max jump   biggest copy movement between two consecutive game frames
"""
import ctypes
import math
import os
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import peek  # noqa: E402
from nav import Game  # noqa: E402

HANDLE_BASES = 0x2B0D720
NEXT_ACTOR = 0xA90


def q(g, a):
    return struct.unpack("<Q", peek.read(g.h, a, 8))[0]


def decode(g, h):
    if not h & 0x80000000:
        return 0
    base = q(g, g.base + HANDLE_BASES + 8 * ((h >> 25) & 0x3F))
    return 0 if base in (0, 0xFFFFFFFFFFFFFFFF) else base | (h & 0x1FFFFFF)


def find_copy(g):
    """The player-class actor that isn't Sora (object table type byte 0)."""
    head = q(g, g.base + 0x2A171C8)
    a = head
    for _ in range(512):
        if not a:
            break
        if a != head:
            row = q(g, a + 0x918)
            if g.base < row < g.base + 0x3000000 and peek.read(g.h, row + 4, 1)[0] == 0:
                return a
        a = decode(g, struct.unpack("<I", peek.read(g.h, a + NEXT_ACTOR, 4))[0])
    return 0


def pos(g, actor):
    return struct.unpack("<3f", peek.read(g.h, actor + 0x640 + 0x30, 12))


def drive(mover, seconds):
    end = time.time() + seconds
    legs = [(1, 0), (0, 1), (-1, 0), (0, -1)]
    i = 0
    while time.time() < end:
        lx, ly = legs[i % 4]
        mover.pad.send(lx=lx, ly=ly, ms=900)
        mover.pad.send(ms=250)  # stop: the copy should stop too
        i += 1
    mover.pad.send()


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mover, viewer = Game(int(sys.argv[1])), Game(int(sys.argv[2]))
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    goofy = "--goofy" in sys.argv
    copy = q(viewer, viewer.base + 0x2A239B0) if goofy else find_copy(viewer)
    if not copy:
        raise SystemExit("no Sora copy in the viewer's game (same room?)")
    ctypes.windll.winmm.timeBeginPeriod(1)
    real, seen = [], []
    t = threading.Thread(target=drive, args=(mover, seconds))
    t0 = time.perf_counter()
    t.start()
    while t.is_alive():
        now = time.perf_counter() - t0
        src = q(mover, mover.base + 0x2A239B0) if goofy else q(mover, mover.base + 0x2A171C8)
        real.append((now, pos(mover, src)))
        seen.append((now, pos(viewer, copy)))
        time.sleep(0.004)
    ctypes.windll.winmm.timeEndPeriod(1)

    def at(track, t):
        lo, hi = 0, len(track) - 1
        if t <= track[0][0]:
            return track[0][1]
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if track[mid][0] <= t:
                lo = mid
            else:
                hi = mid
        return track[lo][1]

    def dist(a, b):
        return math.hypot(a[0] - b[0], a[2] - b[2])

    best = None
    for lag_ms in range(0, 400, 5):
        errs = [dist(p, at(real, t - lag_ms / 1000)) for t, p in seen if t > 0.5]
        mean = sum(errs) / len(errs)
        if best is None or mean < best[1]:
            best = (lag_ms, mean, sorted(errs)[int(0.95 * len(errs))])

    # Copy updates once per game frame (~16.7 ms): collapse samples into frames.
    frames = []
    for t, p in seen:
        if not frames or p != frames[-1][1]:
            frames.append((t, p))
    jumps = [dist(frames[i][1], frames[i - 1][1]) for i in range(1, len(frames))]
    stall_time = 0.0
    for i in range(1, len(frames)):
        gap = frames[i][0] - frames[i - 1][0]
        t_mid = frames[i - 1][0] + gap / 2
        speed = dist(at(real, t_mid - best[0] / 1000 + 0.02), at(real, t_mid - best[0] / 1000 - 0.02)) / 0.04
        if gap > 0.030 and speed > 100:  # stood still 2+ frames while the real Sora ran
            stall_time += gap
            if "-v" in sys.argv:
                print(f"  stall at {frames[i - 1][0]:.3f}s for {gap * 1000:.0f} ms, real speed {speed:.0f}")
    total = seen[-1][0] - seen[0][0]
    print(f"samples {len(seen)}, copy frames {len(frames)} over {total:.1f} s")
    print(f"lag {best[0]} ms | error mean {best[1]:.1f}, p95 {best[2]:.1f} (game units; Sora runs ~400/s)")
    print(f"stalls {100 * stall_time / total:.1f}% of the time | max jump {max(jumps):.1f} per frame")


if __name__ == "__main__":
    main()
