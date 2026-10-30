"""Automated test: Final Xemnas up to his grab (the Riku part), two copies on this PC (TODO 1.30).

Usage: py tools/test_final_xemnas.py [--direct] [--hp 200] [--keep] [--kill] [--env KEY=VALUE ...]

1. Launches a bench pair (copy A = host, copy B = friend, both on the virtual pad) with the friend
   package's co-op settings (tools/package_friend.py) plus test settings: KH2COOP_DEBUG=1 (the debug
   commands) and KH2COOP_GOD=1 (hits on the party cost no HP, so nobody dies while it waits).
2. Loads save 45 of the co-op save copy (build/test_coop_saves: TWTNW, Altar of Naught 0x12/0x12) in both.
3. Warps the host through the ship-fight room (0x12/0x16 program 72) into Final Xemnas (0x12/0x14
   program 74); the friend copy follows. As in play: the ship fight's NO_FRIEND puts the Sora copy in
   Riku's slot. --direct warps straight from the save room instead (Riku comes, no Sora copy: the game
   as without the mod).
4. Sets Xemnas's HP to --hp in both games and makes the host's Sora attack until one hit lands
   (low HP alone never starts the grab; it comes about 50 s after a hit, VERIFIED_OFFSETS).
5. Watches both logs for the grab (scene re_b_ex170-50) or the crash at exe+0x3D76DF (Riku missing).

PASS: both games reach the Riku part and still run 20 s later. Screenshots and a summary go to
build/test_final_xemnas/. The games are closed at the end unless --keep.
--kill: close KH2 copies that are already running first (else the test refuses to start).
--env: one more setting for both copies (repeatable), e.g. --env KH2COOP_GOD=0.
The real Steam save is never written (SAVE_GUARD=1); the co-op save copy is only read.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launch_pair  # noqa: E402
import package_friend  # noqa: E402
import peek  # noqa: E402
import scene  # noqa: E402
from bench import check_save_guard  # noqa: E402
from nav import Game, LOGS  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "test_final_xemnas")
SAVE_SLOT = 45
SAVE_ROOM = (0x12, 0x12)
SHIP = (0x12, 0x16, 72)
FINAL = (0x12, 0x14, 74)
GRAB_SCENE = "re_b_ex170-50"
CRASH = "CRASH (first-chance) code 0xC0000005 at KINGDOM HEARTS II FINAL MIX.exe+0x3D76DF"
DEBUG_MAGIC = 0x4432484B  # "KH2D", tools/debug_cmd.py
COPIES = {"A": {"vpad": 27801, "debug": 27701, "peer": "auto"},
          "B": {"vpad": 27802, "debug": 27702, "peer": "127.0.0.1:27701"}}


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
    import socket
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(struct.pack(fmt, DEBUG_MAGIC, *fields),
                                                            ("127.0.0.1", COPIES[copy]["debug"]))


def warp(world, room, program):
    debug("A", 12, world, room, 0, 0, program, program, program, 0, fmt="<IiBBBBHHHH")


class Copy:
    def __init__(self, name, proc, hwnd):
        self.name, self.proc, self.hwnd = name, proc, hwnd
        self.log = os.path.join(LOGS, f"kh2coop_{proc.pid}.log")
        self.pos = 0
        self.game = None
        self.party = ""
        self.copy = False  # the puppet has a Sora copy in this room
        self.grab = self.crash = None

    def lines(self):
        """Log lines written since the last call."""
        try:
            with open(self.log, encoding="utf-8", errors="replace") as fh:
                fh.seek(self.pos)
                text = fh.read()
                self.pos = fh.tell()
        except OSError:
            return []
        return text.splitlines()

    def alive(self):
        return self.proc.poll() is None


def wait(what, cond, timeout, copies):
    end = time.time() + timeout
    while time.time() < end:
        for c in copies:
            if not c.alive():
                raise SystemExit(f"copy {c.name} closed while waiting for {what}")
        if cond():
            return
        time.sleep(0.5)
    raise SystemExit(f"timed out waiting for {what}")


def in_room(c, world, room):
    return c.game.room()[:2] == (world, room) and c.game.sora() is not None


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


def running_copies():
    return peek.find_pids(peek.EXE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--direct", action="store_true")
    ap.add_argument("--hp", type=int, default=200)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--kill", action="store_true")
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--monitor-x", type=int, default=-2560)  # the left screen (your Monitor 2)
    ap.add_argument("--monitor-y", type=int, default=0)
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=560)
    a = ap.parse_args()

    if running_copies():
        if not a.kill:
            raise SystemExit("KH2 is already running: close it first (or use --kill)")
        for pid in running_copies():
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
        time.sleep(3)
    os.makedirs(OUT, exist_ok=True)
    started = time.time()

    settings = package_settings() + ["KH2COOP_DEBUG=1", "KH2COOP_GOD=1", "KH2COOP_DISPLAY_MODE=2"] + a.env
    copies = []
    for i, (name, cfg) in enumerate(COPIES.items()):
        listen = cfg["debug"]
        proc = launch_pair.launch(name, 0 if name == "A" else 2, listen, cfg["peer"], cfg["vpad"], settings)
        hwnd = launch_pair.wait_window(proc)
        launch_pair.wait_windowed(hwnd)
        launch_pair.place(hwnd, a.monitor_x + i * a.width, a.monitor_y, a.width, a.height)
        copies.append(Copy(name, proc, hwnd))
        print(f"copy {name}: pid {proc.pid}")
        time.sleep(5)
    host, friend = copies

    try:
        for c in copies:  # the log names the virtual pad once the DLL is up
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
        errors = []  # both games load at the same time (you, 2026-10-14)

        def load(c):
            try:
                scene.load_slot(c.game, SAVE_SLOT, SAVE_ROOM, "the save room 0x12/0x12")
            except BaseException as e:  # noqa: BLE001 (SystemExit from the scene helpers)
                errors.append(f"copy {c.name}: {e}")

        loads = [threading.Thread(target=load, args=(c,)) for c in copies]
        for t in loads:
            t.start()
        for t in loads:
            t.join()
        if errors:
            raise SystemExit("; ".join(errors))
        for c in copies:
            c.lines()  # skip what came before the warp
        time.sleep(5)  # the link settles

        if not a.direct:
            warp(*SHIP)
            wait("both games in the ship-fight room", lambda: all(in_room(c, *SHIP[:2]) for c in copies), 60, copies)
            time.sleep(4)
        warp(*FINAL)
        wait("both games in Final Xemnas", lambda: all(in_room(c, *FINAL[:2]) for c in copies), 60, copies)

        log = {c.name: [] for c in copies}

        def read_logs():
            for c in copies:
                for line in c.lines():
                    log[c.name].append(line)
                    if "hud: party list =" in line:
                        c.party = line.split("party list = ", 1)[1]
                    if "puppet: Sora copy found" in line:
                        c.copy = True
                    if "puppet: Sora copy gone" in line:
                        c.copy = False
                    if GRAB_SCENE in line and "scene log" in line and not c.grab:
                        c.grab = time.time()
                    if CRASH in line and not c.crash:
                        c.crash = time.time()

        time.sleep(8)  # the room's opening scenes
        read_logs()
        mark = len(log["A"])
        for c in copies:
            debug(c.name, 13, a.hp)

        def hit_seen():  # anyone's (Riku's too) since the HP was set
            return any("HP change: other" in line and "team 2" in line for line in log["A"][mark:])

        time.sleep(1)
        read_logs()

        # one hit from the host's Sora unless one landed already (the grab only comes after a hit)
        hit_at = time.time() if hit_seen() else None
        end = time.time() + 60
        while time.time() < end and not hit_at:
            host.game.pad.run(["tap", "cross", "wait", "250"] * 4)
            read_logs()
            if hit_seen():
                hit_at = time.time()
        print("first hit on Xemnas:", "yes" if hit_at else "none in 60 s")

        # the grab or the crash, in each game
        end = time.time() + 150
        while time.time() < end:
            read_logs()
            if all(c.grab or c.crash or not c.alive() for c in copies):
                break
            time.sleep(1)
        if any(c.grab for c in copies):
            time.sleep(20)
            read_logs()
        for c in copies:
            if c.alive():
                screenshot(c.hwnd, os.path.join(OUT, f"{c.name}.png"))

        results = []
        passed = True
        for c in copies:
            riku = "party member" in c.party
            twin = c.copy
            if c.crash:
                what = "CRASHED at exe+0x3D76DF (Xemnas's grab without Riku)"
            elif not c.alive():
                what = f"closed (exit code {c.proc.returncode})"
            elif c.grab:
                what = f"grab after {c.grab - hit_at:.0f} s, Riku part running 20 s later" if hit_at else "grab"
            else:
                what = "no grab in 150 s"
            ok = bool(c.grab) and not c.crash and c.alive()
            passed &= ok
            results.append(f"copy {c.name} ({'host' if c is host else 'friend'}): Riku {'yes' if riku else 'no'}, "
                           f"Sora copy {'yes' if twin else 'no'}: {what}")
        summary = [f"Final Xemnas test ({'direct warp' if a.direct else 'through the ship-fight room'}, "
                   f"Xemnas HP {a.hp}{', ' + ' '.join(a.env) if a.env else ''}), {time.time() - started:.0f} s"] + results + \
                  [f"logs: kh2coop_{host.proc.pid}.log (A), kh2coop_{friend.proc.pid}.log (B)",
                   "PASS" if passed else "FAIL"]
        with open(os.path.join(OUT, "summary.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(summary) + "\n")
        print("\n".join(summary))
        return 0 if passed else 1
    finally:
        if not a.keep:
            for c in copies:
                if c.alive():
                    c.proc.kill()


if __name__ == "__main__":
    sys.exit(main())
