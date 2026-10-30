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


def both(fn, copies):
    """fn(c) for each copy at the same time, one thread each; raises Failed with every copy's error."""
    import threading
    errors = []

    def run(c):
        try:
            fn(c)
        except BaseException as e:  # noqa: BLE001 (SystemExit from the scene helpers too)
            errors.append(f"copy {c.name}: {e}")

    ts = [threading.Thread(target=run, args=(c,)) for c in copies]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    if errors:
        raise Failed("; ".join(errors))


def load_slot_both(copies, slot, room=None, name=None):
    """Load save <slot> in every copy at the same time (each through its own pad; you, 2026-10-14: one after the
    other doubled the wait). The co-op link takes both loading together (test_save_sweep.py has always done so)."""
    both(lambda c: scene.load_slot(c.game, slot, room, name), copies)


STARTED = set()  # pids of the copies this run started: the only KH2 a bench may close


def check_save_guard(c, timeout=30):
    """Stops the bench unless the copy's save guard is on (save_guard.cpp): without it a test copy writes your real
    Steam save. Reads the log file itself, so it fits any bench's Copy."""
    end, line = time.time() + timeout, None
    while not line and time.time() < end and c.alive():
        try:
            with open(c.log, encoding="utf-8", errors="replace") as fh:
                line = next((l for l in fh if "save guard: " in l and ("on (" in l or "INCOMPLETE" in l
                                                                        or "NOT active" in l)), None)
        except OSError:
            pass
        time.sleep(0.5)
    if not (line and "save guard: on (" in line and "(SAVE_GUARD=1)" in line):
        raise SystemExit(f"copy {c.name}: the save guard is not on ({line.strip() if line else 'no line'}): stopped")


def launch(extra=(), names=("A", "B"), keep_running=False):
    """Starts the copies and waits until their pads answer and their save guards are on. A KH2 this run did not start
    stops it (it may be your game, with unsaved progress); ours still open are closed first unless keep_running."""
    running = set(peek.find_pids(peek.EXE))
    if running - STARTED and not keep_running:
        raise SystemExit("KH2 is already running: close it first (a bench only closes the copies it started)")
    if running and not keep_running:
        for pid in running:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        time.sleep(3)
    settings = package_settings() + ["KH2COOP_DEBUG=1", "KH2COOP_GOD=1", "KH2COOP_DISPLAY_MODE=2"] + list(extra)
    copies = []
    for name in names:
        cfg, i = COPIES[name], list(COPIES).index(name)
        proc = launch_pair.launch(name, 0 if name == "A" else 2, cfg["debug"], cfg["peer"], cfg["vpad"], settings)
        STARTED.add(proc.pid)
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
        check_save_guard(c)
    return copies


def close(copies):
    for c in copies:
        if c.alive():
            c.proc.kill()
    for c in copies:  # gone before the next bench looks for a running KH2 (launch refuses one it did not start)
        try:
            c.proc.wait(30)
        except subprocess.TimeoutExpired:
            pass


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


def model_of(c, a):
    """The model name in an actor's object table record (*(actor+0x918)), or '?'."""
    import re
    try:
        rec = struct.unpack("<Q", peek.read(c.game.h, a + OBJ_ENTRY, 8))[0]
        m = re.search(rb"[A-Z]_[A-Z0-9_]{3,}", peek.read(c.game.h, rec, 0x40)) if rec else None
        return m.group().decode() if m else "?"
    except OSError:
        return "?"


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
EFFECT_POOL_SIZE = 521008  # the game's own; the DLL makes a bigger one (fx_pool.cpp, KH2COOP_FX_POOL_KB)


def pool_size(c):
    """Size of the effect pool the game uses now (end - start), or None."""
    try:
        pool = struct.unpack("<Q", peek.read(c.game.h, c.game.base + EFFECT_POOL, 8))[0]
        start, end = struct.unpack("<QQ", peek.read(c.game.h, pool + 8, 16)) if pool else (0, 0)
        return end - start if end > start else None
    except OSError:
        return None


def pool_used(c):
    """Bytes in use in the game's effect pool (the one whose overflow crashes at exe+0x4B00DA), or None."""
    try:
        pool = struct.unpack("<Q", peek.read(c.game.h, c.game.base + EFFECT_POOL, 8))[0]
        return struct.unpack("<I", peek.read(c.game.h, pool + 0x18, 4))[0] if pool else None
    except OSError:
        return None


# The enemy HP bar (VERIFIED_OFFSETS 'Enemy HP bar'): exe+0x2A24E90 -> its target (+4 actor handle, +0x28 the
# gauge while shown, +0x30 stat channel); exe+0xABD260 -> dk::GAUGE_ENEMY (+0x4B08 HP shown, +0x4B0C max,
# +0x4B28 on).
BAR_TARGET, ENEMY_GAUGE = 0x2A24E90, 0xABD260


def hp_bar(c):
    """(enemy actor, its HP, the HP the bar shows) while the enemy HP bar is up, else None."""
    h, base = c.game.h, c.game.base
    try:
        target = struct.unpack("<Q", peek.read(h, base + BAR_TARGET, 8))[0]
        if not target:
            return None
        handle, = struct.unpack("<I", peek.read(h, target + 4, 4))
        gauge_on, = struct.unpack("<Q", peek.read(h, target + 0x28, 8))
        channel, = struct.unpack("<i", peek.read(h, target + 0x30, 4))
        gauge = struct.unpack("<Q", peek.read(h, base + ENEMY_GAUGE, 8))[0]
        if not gauge_on or not gauge or not 0 <= channel < 8:
            return None
        shown, = struct.unpack("<i", peek.read(h, gauge + 0x4B08, 4))
        on = peek.read(h, gauge + 0x4B28, 1)[0]
        actor = decode(c, handle)
        if not on or not actor:
            return None
        stats = struct.unpack("<Q", peek.read(h, actor + 0x5C0, 8))[0]
        hp, = struct.unpack("<i", peek.read(h, stats + 0xC * channel, 4)) if stats else (-1,)
        return actor, hp, shown
    except OSError:
        return None


class BarWatch:
    """Samples each copy's enemy HP bar every `step` s in a thread: how often the bar shows the enemy's HP, and
    the longest time it showed something else (TODO 1.21)."""

    def __init__(self, copies, step=0.1):
        import threading
        self.copies, self.step = copies, step
        self.stats = {c.name: {"up": 0, "same": 0, "worst": 0.0, "since": None, "changes": []} for c in copies}
        self.t0 = time.time()
        self.running = True
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        last = {}
        while self.running:
            now = time.time() - self.t0
            for c in self.copies:
                st = self.stats[c.name]
                b = hp_bar(c) if c.alive() else None
                if not b:
                    st["since"] = None
                    continue
                st["up"] += 1
                if b[1] == b[2] or b[1] <= 0:
                    st["same"] += 1
                    st["since"] = None
                else:
                    st["since"] = st["since"] if st["since"] is not None else now
                    st["worst"] = max(st["worst"], now - st["since"])
                if last.get(c.name) != b and len(st["changes"]) < 60:
                    st["changes"].append((now, b))
                last[c.name] = b
            time.sleep(self.step)

    def stop(self):
        self.running = False
        self.thread.join(2)

    def summary(self, name):
        st = self.stats[name]
        if not st["up"]:
            return "the enemy HP bar never showed"
        tail = ", ".join(f"{t:.1f}s {a & 0xFFFFFF:06X} hp {hp} bar {bar}" for t, (a, hp, bar) in st["changes"][-12:])
        return (f"bar up {st['up'] * self.step:.0f} s, showing the enemy's HP {100 * st['same'] // st['up']}% of it; "
                f"longest wrong {st['worst']:.1f} s; last changes: {tail}")


class PoolWatch:
    """Samples the effect pool of each copy every `step` s in a thread: max, last, and a coarse timeline."""

    def __init__(self, copies, step=0.5):
        import threading
        self.copies, self.step = copies, step
        self.samples = {c.name: [] for c in copies}  # (seconds since start, bytes)
        self.size = {}  # copy -> pool size last read
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
                        self.size[c.name] = pool_size(c) or self.size.get(c.name)
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
        size = self.size.get(name) or EFFECT_POOL_SIZE
        return (f"peak {peak[1] // 1024} KB of {size // 1024} KB at {peak[0]:.0f} s; "
                f"last samples: {tail}")


# The live effect list (VERIFIED_OFFSETS 'Live effects and their particles'): *(exe+0xB1D988) = first effect;
# effect +0x8 kind, +0xC flags, +0x10 owner (actor+0x80 when an actor started it), +0x20 effect data, +0x30 its
# particle system, +0x40 next (handle). Particle system +0x68 group count, +0x88 groups (0x18 each: +0 group data,
# +0x14 u16 live particles); a particle takes data+0x20 + (data+0x26) * 8 bytes of the effect pool.
EFFECT_LIST = 0xB1D988


def fx_starts(c, owner):
    """{effect: (id, attached actor, age, flags)} from an actor's list of effect starts (owner = actor+0x80): +0x18
    newest record, 0x78 bytes each: +0x40 the effect, +0x48 its entry (u16 id at +2), +0x50 the actor it is attached
    to, +0x58 f32 age (exe+0x2C5BE0 adds the frame time), +0x70 flags, +0x74 next (handle)."""
    h, out = c.game.h, {}
    r = struct.unpack("<Q", peek.read(h, owner + 0x18, 8))[0]
    for _ in range(2000):
        if not r:
            break
        raw = peek.read(h, r, 0x78)
        eff, entry, att = struct.unpack_from("<QQQ", raw, 0x40)
        fx_id = struct.unpack("<H", peek.read(h, entry + 2, 2))[0] if entry else -1
        out[eff] = (fx_id, att, struct.unpack_from("<f", raw, 0x58)[0], struct.unpack_from("<I", raw, 0x70)[0])
        r = decode(c, struct.unpack_from("<I", raw, 0x74)[0])
    return out


def fx_census(c, detail=None):
    """{(owner model, owner, effect data, id, attached model, attached): [effects, particles, bytes]} for the live
    effects, or None. With a list as `detail`, one tuple per effect is added to it: (effect, id, owner, effect
    flags, particles, the start record's age and flags, the particle system's +0x58 / +0xB0 / +0xBC and its
    bytes +0xA8 / +0xC1..+0xC4)."""
    h, base = c.game.h, c.game.base
    out, sizes, starts, names = {}, {}, {}, {}
    name = lambda a: names.setdefault(a, model_of(c, a) if a else "-")
    try:
        e = struct.unpack("<Q", peek.read(h, base + EFFECT_LIST, 8))[0]
        for _ in range(4000):
            if not e:
                break
            raw = peek.read(h, e, 0x48)
            owner, data, psys = struct.unpack_from("<Q", raw, 0x10)[0], struct.unpack_from("<Q", raw, 0x20)[0], \
                struct.unpack_from("<Q", raw, 0x30)[0]
            parts = size = 0
            ps = b""
            if psys:
                ps = peek.read(h, psys, 0xC8)
                count, groups = struct.unpack_from("<i", ps, 0x68)[0], struct.unpack_from("<Q", ps, 0x88)[0]
                if 0 < count < 256 and groups:
                    g = peek.read(h, groups, count * 0x18)
                    for k in range(count):
                        gdata, live = struct.unpack_from("<Q", g, k * 0x18)[0], struct.unpack_from("<H", g, k * 0x18 + 0x14)[0]
                        if live and gdata:
                            if gdata not in sizes:
                                d = peek.read(h, gdata, 0x28)
                                sizes[gdata] = struct.unpack_from("<i", d, 0x20)[0] + struct.unpack_from("<h", d, 0x26)[0] * 8
                            parts += live
                            size += live * sizes[gdata]
            actor = owner - 0x80 if owner > 0x80 else 0
            if actor and owner not in starts:
                try:
                    starts[owner] = fx_starts(c, owner)
                except (OSError, struct.error):
                    starts[owner] = {}
            fx_id, att, age, tflags = starts.get(owner, {}).get(e, (-1, 0, -1.0, 0))
            if detail is not None:
                sysv = (struct.unpack_from("<i", ps, 0x58)[0], round(struct.unpack_from("<f", ps, 0xB0)[0], 3),
                        round(struct.unpack_from("<f", ps, 0xBC)[0], 3), ps[0xA8], ps[0xC1], ps[0xC2], ps[0xC3],
                        ps[0xC4]) if len(ps) == 0xC8 else ()
                detail.append((e, fx_id, actor - base if actor > base else actor,
                               struct.unpack_from("<I", raw, 0xC)[0], parts, round(age, 2), tflags) + sysv)
            key = (name(actor), actor - base if actor > base else actor, data, fx_id, name(att),
                   att - base if att > base else att)
            row = out.setdefault(key, [0, 0, 0])
            row[0] += 1
            row[1] += parts
            row[2] += size
            e = decode(c, struct.unpack_from("<I", raw, 0x40)[0])
    except (OSError, struct.error):
        return None
    return out


class FxCensus:
    """Samples fx_census of each copy every `step` s in a thread, next to the pool's bytes in use."""

    def __init__(self, copies, step=1.0):
        import threading
        self.copies, self.step = copies, step
        self.samples = {c.name: [] for c in copies}  # (seconds since start, pool bytes, census)
        self.detail = {c.name: [] for c in copies}  # (seconds since start, [per effect tuples])
        self.t0 = time.time()
        self.running = True
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while self.running:
            for c in self.copies:
                if c.alive():
                    det = []
                    cen = fx_census(c, det)
                    if cen is not None:
                        t = time.time() - self.t0
                        self.samples[c.name].append((t, pool_used(c) or 0, cen))
                        self.detail[c.name].append((t, det))
            time.sleep(self.step)

    def stop(self):
        self.running = False
        self.thread.join(5)

    def summary(self, name, top=8):
        s = self.samples.get(name) or []
        if not s:
            return ["no samples"]
        t, pool, cen = max(s, key=lambda x: x[1])
        total = sum(v[2] for v in cen.values())
        lines = [f"at the pool peak ({pool // 1024} KB, {t:.0f} s): {len(cen)} kinds, "
                 f"{sum(v[0] for v in cen.values())} effects, particles {sum(v[1] for v in cen.values())} "
                 f"= {total // 1024} KB"]
        for (model, owner, data, fx_id, amodel, att), (n, parts, size) in sorted(cen.items(), key=lambda x: -x[1][2])[:top]:
            lines.append(f"  {size // 1024:4d} KB {parts:5d} particles in {n:3d} effects, owner {model} "
                         f"exe+{owner:#x}, effect data {data:#x} id {fx_id}, attached to {amodel} exe+{att:#x}")
        return lines

    def write(self, path):
        with open(path, "w") as fh:
            for name, s in self.samples.items():
                for t, pool, cen in s:
                    for (model, owner, data, fx_id, amodel, att), (n, parts, size) in cen.items():
                        fh.write(f"{name},{t:.1f},{pool},{model},{owner:#x},{data:#x},{fx_id},{amodel},{att:#x},"
                                 f"{n},{parts},{size}\n")
        with open(path.replace(".csv", "_detail.csv"), "w") as fh:
            for name, d in self.detail.items():
                for t, rows in d:
                    for r in rows:
                        fh.write(f"{name},{t:.1f}," + ",".join(hex(v) if k in (0, 3, 6) else str(v)
                                                               for k, v in enumerate(r)) + "\n")
