"""Launch two KH2 copies for the one-PC test and place them side by side.

Usage: py tools/launch_pair.py [--pad-a 0] [--pad-b 2] [--monitor-x 0] [--monitor-y 0] [--width 1280] [--height 760]
                               [--vpad-a PORT] [--vpad-b PORT] [--single]

--vpad-a/--vpad-b: that copy ignores real pads and takes a scripted pad from
tools/vpad.py on that UDP port (works with no KH2 window focused).
--single: launch copy A only (no peer link).
--only-b: launch copy B only (e.g. to restart the friend side while A keeps running).
--host-auto: copy A runs like the real host (KH2COOP_PEER=auto: it learns copy B's
address from B's first packet), copy B like the friend's PC (sends to A).
Any other KH2COOP_* variable set in the calling environment is passed through
(e.g. KH2COOP_NET_SIM=40,20,2 for the Wi-Fi simulator).
--env-a KEY=VALUE / --env-b KEY=VALUE (repeatable): settings for one copy only.

Copy A (left half) only reads XInput slot --pad-a, copy B (right half) only
--pad-b; both keep reading their pad while unfocused (see src/dll/kh2coop.cpp).
Each copy streams its Sora to the other over UDP on 127.0.0.1 (A: 27701,
B: 27702; see src/dll/avatar_link.hpp).
Steam must be running. Placement needs the game in windowed mode: set
KH2COOP_DISPLAY_MODE=2 (the save guard then serves the game a windowed copy of its
settings file; the guard keeps the game's own settings changes out of the real file).
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import time

GAME = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-"
EXE = "KINGDOM HEARTS II FINAL MIX.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDPIAware()
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def main_window(pid):
    found = []

    def cb(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        cls = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(hwnd, cls, 64)
        # Panacea's console (show_console=true) belongs to the game process too: skip it
        if (owner.value == pid and user32.IsWindowVisible(hwnd) and not user32.GetWindow(hwnd, 4)  # GW_OWNER
                and cls.value != "ConsoleWindowClass"):
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(WNDENUMPROC(cb), 0)
    return found[0] if found else None


def launch(pad, listen, peer, vpad=0, extra=()):
    env = dict(os.environ, SteamAppId="2552430", SteamGameId="2552430",
               KH2COOP_PAD=str(pad), KH2COOP_BACKGROUND="1",
               KH2COOP_LISTEN=str(listen), KH2COOP_PEER=peer,
               KH2COOP_PUPPET=os.environ.get("KH2COOP_PUPPET", "1"),
               KH2COOP_PUPPET_ROXAS=os.environ.get("KH2COOP_PUPPET_ROXAS", "1"),
               KH2COOP_PUPPET_LOOK=os.environ.get("KH2COOP_PUPPET_LOOK", "clone"),
               KH2COOP_PUPPET_COLLIDE=os.environ.get("KH2COOP_PUPPET_COLLIDE", "0"),
               KH2COOP_XINPUT_ONLY=os.environ.get("KH2COOP_XINPUT_ONLY", "0"),
               KH2COOP_SAVE_GUARD=os.environ.get("KH2COOP_SAVE_GUARD", "1"),
               KH2COOP_CLONE_TARGET=os.environ.get("KH2COOP_CLONE_TARGET", "0"),
               KH2COOP_VPAD=str(vpad))
    env.update(kv.split("=", 1) for kv in extra)
    return subprocess.Popen([os.path.join(GAME, EXE)], cwd=GAME, env=env)


def wait_window(proc, timeout=60):
    end = time.time() + timeout
    while time.time() < end:
        if proc.poll() is not None:
            raise SystemExit(f"pid {proc.pid} exited (code {proc.returncode})")
        hwnd = main_window(proc.pid)
        if hwnd:
            return hwnd
        time.sleep(0.5)
    raise SystemExit(f"pid {proc.pid}: no window after {timeout}s")


def wait_windowed(hwnd, timeout=15):
    """The game reads its display mode a few seconds after its window appears and then resizes and moves
    itself; wait until the window has a title bar (windowed) and kept its size for 1 s."""
    user32.GetWindowLongW.restype = ctypes.c_long
    end = time.time() + timeout
    last, since = None, time.time()
    while time.time() < end:
        r = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        style = user32.GetWindowLongW(hwnd, -16) & 0xFFFFFFFF  # GWL_STYLE
        now = (r.left, r.top, r.right, r.bottom, style)
        if now != last:
            last, since = now, time.time()
        elif style & 0x00C00000 == 0x00C00000 and time.time() - since >= 1.0:  # WS_CAPTION
            return True
        time.sleep(0.2)
    return False


def place(hwnd, x, y, w, h):
    SWP_NOZORDER, SWP_SHOWWINDOW = 0x0004, 0x0040
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetWindowPos(hwnd, None, x, y, w, h, SWP_NOZORDER | SWP_SHOWWINDOW)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pad-a", type=int, default=0)
    ap.add_argument("--pad-b", type=int, default=2)
    ap.add_argument("--monitor-x", type=int, default=0)
    ap.add_argument("--monitor-y", type=int, default=0)
    ap.add_argument("--vpad-a", type=int, default=0)
    ap.add_argument("--vpad-b", type=int, default=0)
    ap.add_argument("--single", action="store_true")
    ap.add_argument("--only-b", action="store_true")
    ap.add_argument("--host-auto", action="store_true")
    ap.add_argument("--env-a", action="append", default=[])
    ap.add_argument("--env-b", action="append", default=[])
    ap.add_argument("--width", type=int, default=1280)
    ap.add_argument("--height", type=int, default=760)
    a = ap.parse_args()

    copies = []
    # A listens on 27701 and sends to B on 27702, and vice versa.
    plan = [("A", a.pad_a, a.vpad_a, a.monitor_x, 27701, 27702),
            ("B", a.pad_b, a.vpad_b, a.monitor_x + a.width, 27702, 27701)]
    for name, pad, vpad, x, listen, peer in plan[:1] if a.single else plan[1:] if a.only_b else plan:
        peer_addr = "auto" if (a.host_auto and name == "A") else f"127.0.0.1:{peer}"
        proc = launch(pad, listen, peer_addr, vpad, a.env_a if name == "A" else a.env_b)
        hwnd = wait_window(proc)
        if not wait_windowed(hwnd):
            print(f"copy {name}: still full screen after 15 s (KH2COOP_DISPLAY_MODE=2 starts it windowed)")
        place(hwnd, x, a.monitor_y, a.width, a.height)
        copies.append((name, proc.pid, pad))
        source = f"virtual pad port {vpad}" if vpad else f"XInput slot {pad}"
        print(f"copy {name}: pid {proc.pid}, {source}, placed at x={x} y={a.monitor_y}")
        time.sleep(5)
    # Re-place every copy in case a window moved while the next one started.
    for (name, pid, _), x in zip(copies, [p[3] for p in (plan[:1] if a.single else plan[1:] if a.only_b else plan)]):
        hwnd = main_window(pid)
        if hwnd:
            place(hwnd, x, a.monitor_y, a.width, a.height)


if __name__ == "__main__":
    main()
