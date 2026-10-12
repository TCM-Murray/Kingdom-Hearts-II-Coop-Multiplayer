"""Shared helpers for the automated two-copy bench tests (tools/test_*.py).

A bench pair: copy A = host, copy B = friend, both on the virtual pad, on this PC, with the friend
package's co-op settings (tools/package_friend.py) plus test settings (debug commands, god mode).
The real Steam save is never written (SAVE_GUARD=1); the co-op save copies in build/test_coop_saves are
only read.

Also: the co-op save list (read from a running copy's memory once the LOAD (CO-OP) list has been opened).
"""
import ctypes
import ctypes.wintypes as wt
import os
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launch_pair  # noqa: E402
import package_friend  # noqa: E402
import peek  # noqa: E402
import scene  # noqa: E402
from nav import Game, LOGS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRASH = "CRASH (first-chance)"
DEBUG_MAGIC = 0x4432484B  # "KH2D", tools/debug_cmd.py
COPIES = {"A": {"vpad": 27801, "debug": 27701, "peer": "auto"},
          "B": {"vpad": 27802, "debug": 27702, "peer": "127.0.0.1:27701"}}
# Monitor 1 (the main screen, 2560x1440 at 0,0): the two windows side by side.
MONITOR_X, MONITOR_Y, WIDTH, HEIGHT = 0, 0, 1280, 760

# Save container (VERIFIED_OFFSETS 'Save container'): exe+0x79CB10 -> container, +0x10 data*;
# data = 0x10 header, slot count x 0x158 slot info, then the slots (0x10FC0 each, magic KH2J).
CONTAINER, SLOT_COUNT, SLOT_SIZE = 0x79CB10, 0x79CD34, 0x79CC2C
SAVE_MAGIC = 0x4A32484B


def package_settings():
    """The co-op features the friend package turns on, as KH2COOP_* settings."""
    out = []
    for line in package_friend.INI.splitlines():
        line = line.strip()
        if not line or line[0] in ";[" or "{" in line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        out.append(f"KH2COOP_{key.strip()}={value.strip()}")
    return out


def debug(copy, *fields, fmt="<Iii"):
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack(fmt, DEBUG_MAGIC, *fields),
                                                            ("127.0.0.1", COPIES[copy]["debug"]))


def warp(world, room, program=0xFFFF, door=0, copy="A"):
    debug(copy, 12, world, room, door, 0, program, program, program, 0, fmt="<IiBBBBHHHH")


def to_title(copy):
    debug(copy, 10, 4)  # link 4: the title screen now (link_watch.cpp, test only)


class Copy:
    """One running game copy: its process, window, log (read incrementally) and memory (nav.Game)."""

    def __init__(self, name, proc, hwnd):
        self.name, self.proc, self.hwnd = name, proc, hwnd
        self.log = os.path.join(LOGS, f"kh2coop_{proc.pid}.log")
        self.pos = 0
        self.game = None
        self.all = []      # every log line read so far
        self.crash = None  # first crash line

    def lines(self):
        """Log lines written since the last call (also kept in self.all)."""
        try:
            with open(self.log, encoding="utf-8", errors="replace") as fh:
                fh.seek(self.pos)
                text = fh.read()
                self.pos = fh.tell()
        except OSError:
            return []
        new = text.splitlines()
        self.all.extend(new)
        for line in new:
            if CRASH in line and not self.crash:
                self.crash = line
        return new

    def mark(self):
        self.lines()
        return len(self.all)

    def since(self, mark):
        self.lines()
        return self.all[mark:]

    def alive(self):
        return self.proc.poll() is None

    def room(self):
        return self.game.room()[:2]


class Failed(Exception):
    pass


def wait(what, cond, timeout, copies, step=0.5):
    end = time.time() + timeout
    while time.time() < end:
        for c in copies:
            if not c.alive():
                raise Failed(f"copy {c.name} closed while waiting for {what}")
            c.lines()
            if c.crash:
                raise Failed(f"copy {c.name} crashed while waiting for {what}: {c.crash}")
        if cond():
            return
        time.sleep(step)
    raise Failed(f"timed out waiting for {what}")


def kill_running():
    for pid in peek.find_pids(peek.EXE):
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
    time.sleep(3)


def launch(extra=(), names=("A", "B"), keep_running=False):
    """Starts the copies (closing any KH2 already running unless keep_running) and waits until their pads answer."""
    if peek.find_pids(peek.EXE) and not keep_running:
        kill_running()
    settings = package_settings() + ["KH2COOP_DEBUG=1", "KH2COOP_GOD=1", "KH2COOP_DISPLAY_MODE=2"] + list(extra)
    copies = []
    for name in names:
        cfg, i = COPIES[name], list(COPIES).index(name)
        proc = launch_pair.launch(name, 0 if name == "A" else 2, cfg["debug"], cfg["peer"], cfg["vpad"], settings)
        hwnd = launch_pair.wait_window(proc)
        launch_pair.wait_windowed(hwnd)
        launch_pair.place(hwnd, MONITOR_X + i * WIDTH, MONITOR_Y, WIDTH, HEIGHT)
        copies.append(Copy(name, proc, hwnd))
        time.sleep(5)
    for c in copies:
        end = time.time() + 60
        while True:
            try:
                c.game = Game(COPIES[c.name]["vpad"])
                break
            except SystemExit:
                if time.time() > end or not c.alive():
                    raise
                time.sleep(1)
    return copies


def close(copies):
    for c in copies:
        if c.alive():
            c.proc.kill()


def save_slots(c):
    """[(slot as numbered on screen, world, room, door, raw header bytes)] of the co-op save file, read from memory,
    sorted by slot.
    The LOAD (CO-OP) list must have been opened once (the co-op container is made then)."""
    h, base = c.game.h, c.game.base
    cont = struct.unpack("<Q", peek.read(h, base + CONTAINER, 8))[0]
    count = struct.unpack("<i", peek.read(h, base + SLOT_COUNT, 4))[0]
    size = struct.unpack("<i", peek.read(h, base + SLOT_SIZE, 4))[0]
    data = struct.unpack("<Q", peek.read(h, cont + 0x10, 8))[0]
    first = data + 0x10 + count * 0x158
    out = []
    for i in range(count):
        # Entry i's 0x158-byte info starts with its file name, 'BISLPM-66675FM-NN' (screen slot NN + 1) or '...-SYS';
        # the entries are not in screen order (saves 97-99 come after the system file).
        name = peek.read(h, data + 0x10 + i * 0x158, 0x20).split(b"\0")[0].decode("ascii", "replace")
        if not name[-2:].isdigit():
            continue
        head = peek.read(h, first + i * size, 0x40)
        if struct.unpack("<I", head[:4])[0] != SAVE_MAGIC:
            continue
        out.append((int(name[-2:]) + 1, head[0x0C], head[0x0D], head[0x0E], head))
    return sorted(out)


user32 = ctypes.WinDLL("user32")
gdi32 = ctypes.WinDLL("gdi32")
user32.GetDC.restype = wt.HDC
user32.GetDC.argtypes = [wt.HWND]
user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
gdi32.CreateCompatibleDC.restype = wt.HDC
gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
gdi32.CreateCompatibleBitmap.restype = wt.HBITMAP
gdi32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.restype = wt.HGDIOBJ
gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
gdi32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wt.HDC]


def screenshot(hwnd, path):
    """PrintWindow with PW_RENDERFULLCONTENT: works while the window is covered."""
    from PIL import Image
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    if w <= 0 or h <= 0:
        return False
    hdc = user32.GetDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mem, bmp)
    user32.PrintWindow(hwnd, mem, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    gdi32.SelectObject(mem, old)      # GetDIBits wants the bitmap out of any DC
    header = ctypes.create_string_buffer(struct.pack("<IiiHHIIiiII", 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0), 44)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc, bmp, 0, h, buf, header, 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1).save(path)
    return True


NEXT_ACTOR, OBJ_ENTRY = 0xA90, 0x918  # aggro.cpp kNextActor (compressed pointer); *(actor+0x918) = object table record (model name)


def actors(c):
    """[(actor address, model name)] in the active actor list, list order."""
    import re
    h, base = c.game.h, c.game.base
    out, a, seen = [], struct.unpack("<Q", peek.read(h, base + 0x2A171C8, 8))[0], set()
    while a and a not in seen and len(out) < 300:
        seen.add(a)
        name = "?"
        try:
            rec = struct.unpack("<Q", peek.read(h, a + OBJ_ENTRY, 8))[0]
            m = re.search(rb"[A-Z]_[A-Z0-9_]{3,}", peek.read(h, rec, 0x40)) if rec else None
            name = m.group().decode() if m else "?"
        except OSError:
            pass
        out.append((a, name))
        a = decode(c, struct.unpack("<I", peek.read(h, a + NEXT_ACTOR, 4))[0])
    return out


def decode(c, handle):
    """Compressed actor pointer (aggro.cpp Decode): bit 31 set, bits 25-30 index exe+0x2B0D720."""
    if not handle & 0x80000000:
        return 0
    bases = peek.read(c.game.h, c.game.base + 0x2B0D720 + 8 * ((handle >> 25) & 0x3F), 8)
    return struct.unpack("<Q", bases)[0] | (handle & 0x1FFFFFF)


def actor_by_model(c, prefix):
    """First actor whose model name starts with prefix, or 0."""
    for a, name in actors(c):
        if name.startswith(prefix):
            return a
    return 0


def motion(c, actor):
    """The actor's animation id (actor+0x180), or None."""
    try:
        return struct.unpack("<i", peek.read(c.game.h, actor + 0x180, 4))[0]
    except OSError:
        return None


EFFECT_POOL = 0x2B0EC08  # u64 -> the effect pool; +0x18 u32 bytes in use (tools/pool_stat.py, VERIFIED_OFFSETS)
EFFECT_POOL_SIZE = 521008


def pool_used(c):
    """Bytes in use in the game's effect pool (the one whose overflow crashes at exe+0x4B00DA), or None."""
    try:
        pool = struct.unpack("<Q", peek.read(c.game.h, c.game.base + EFFECT_POOL, 8))[0]
        return struct.unpack("<I", peek.read(c.game.h, pool + 0x18, 4))[0] if pool else None
    except OSError:
        return None


class PoolWatch:
    """Samples the effect pool of each copy every `step` s in a thread: max, last, and a coarse timeline."""

    def __init__(self, copies, step=0.5):
        import threading
        self.copies, self.step = copies, step
        self.samples = {c.name: [] for c in copies}  # (seconds since start, bytes)
        self.t0 = time.time()
        self.running = True
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while self.running:
            for c in self.copies:
                if c.alive():
                    v = pool_used(c)
                    if v is not None:
                        self.samples[c.name].append((time.time() - self.t0, v))
            time.sleep(self.step)

    def stop(self):
        self.running = False
        self.thread.join(2)

    def summary(self, name):
        s = self.samples.get(name) or []
        if not s:
            return "no samples"
        peak = max(s, key=lambda x: x[1])
        tail = ", ".join(f"{t:.0f}s {v // 1024}K" for t, v in s[-6:])
        return (f"peak {peak[1] // 1024} KB of {EFFECT_POOL_SIZE // 1024} KB at {peak[0]:.0f} s; "
                f"last samples: {tail}")
