"""Dev tool: a small window to pick a room or fight; a key pressed in a game window warps the host there (the friend follows).

Usage: pyw tools/warp_key.py [host's listen port, default 27701] [key: delete | insert | home | end | f5..f12]
       py tools/warp_key.py --list [words]     (prints the matching destinations, opens nothing)

The list is every room of build/catalog/rooms.json (`dotnet run --project tools/catalog` makes it): "as the save has
it" (the save's programs) and one row per battle program, with its mission's name. Type words to narrow it (e.g.
"timeless 52", "coliseum mission", "wi02"), or a raw warp as for debug_cmd.py: <world hex> <room hex> [map btl evt],
e.g. "0D 01 2 0 0". Click a row (or Enter for the first one): that is where the key warps to, until you pick another;
the pick is kept for next time (build/warp_key.json). "Warp now" sends it at once.

It sends debug_cmd.py's warp, so the game needs KH2COOP_DEBUG=1 (start_pair.bat sets it). A battle program is sent
as all three programs, as the benches do. The key only counts while the window in front belongs to the game. The
window starts minimised and closes by itself once the game it saw is gone.
"""
import ctypes
import ctypes.wintypes as wt
import json
import pathlib
import re
import socket
import struct
import subprocess
import sys
import time

KEYS = {"delete": 0x2E, "insert": 0x2D, "home": 0x24, "end": 0x23, **{f"f{n}": 0x6F + n for n in range(5, 13)}}
GAME = "KINGDOM HEARTS II FINAL MIX"
ROOT = pathlib.Path(__file__).resolve().parent.parent
WORLDS = {"zz": "World ZZ", "es": "End of Sea", "tt": "Twilight Town", "di": "Destiny Islands", "hb": "Hollow Bastion",
          "bb": "Beast's Castle", "he": "Olympus Coliseum", "al": "Agrabah", "mu": "Land of Dragons",
          "po": "100 Acre Wood", "lk": "Pride Lands", "lm": "Atlantica", "dc": "Disney Castle", "wi": "Timeless River",
          "nm": "Halloween Town", "wm": "World Map", "ca": "Port Royal", "tr": "Space Paranoids",
          "eh": "The World That Never Was"}
DEFAULT = "wi02) - battle 52"  # the steamboat fight, until something else is picked
user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32


def destinations():
    """[(label, world, room, (map, btl, evt))] from the room catalog."""
    path = ROOT / "build" / "catalog" / "rooms.json"
    if not path.exists():
        sys.exit(f"no {path}: run  dotnet run --project tools/catalog  first")
    out = []
    for r in json.loads(path.read_text(encoding="utf-8")):
        if not r["has_file"]:
            continue
        name = (r["name"] or "?").replace("´", "'")
        place = f"{WORLDS.get(r['code'][:2], r['code'][:2])}: {name} ({r['code']})"
        out.append((f"{place} - as the save has it", r["world"], r["room"], (0xFFFF,) * 3))
        for n, lines in sorted((r.get("programs") or {}).get("btl", {}).items(), key=lambda kv: int(kv[0])):
            quoted = lambda word: [l.split('"')[1] for l in lines if l.startswith(word) and '"' in l]
            what = "mission " + quoted("Mission")[0] if quoted("Mission") else "spawns " + ", ".join(quoted("Spawn"))
            out.append((f"{place} - battle {n}: {what}", r["world"], r["room"], (int(n),) * 3))
    return out


def matches(dests, text):
    """The raw warp the text spells, if any, then the destinations holding every word of it."""
    m = re.fullmatch(r"\s*([0-9a-fA-F]{1,2})\s+([0-9a-fA-F]{1,2})(?:\s+(\d+)\s+(\d+)\s+(\d+))?\s*", text)
    raw = []
    if m:
        progs = tuple(int(v) for v in m.groups()[2:]) if m.group(3) else (0xFFFF,) * 3
        shown = "the save's programs" if progs[0] == 0xFFFF else "programs %d/%d/%d" % progs
        raw = [(f"raw warp {int(m.group(1), 16):02X}/{int(m.group(2), 16):02X}, {shown}", int(m.group(1), 16),
                int(m.group(2), 16), progs)]
    words = text.lower().split()
    return raw + [d for d in dests if all(w in d[0].lower() for w in words)]


def send(port, dest):
    pkt = struct.pack("<IiBBBBHHHH", 0x4432484B, 12, dest[1], dest[2], 0, 0, *dest[3], 0)  # debug_cmd.py's warp
    socket.socket(socket.AF_INET, socket.SOCK_DGRAM).sendto(pkt, ("127.0.0.1", port))


def front_is_game():
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    h = kernel32.OpenProcess(0x1000, False, pid.value)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    buf, size = ctypes.create_unicode_buffer(1024), wt.DWORD(1024)
    ok = kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size))
    kernel32.CloseHandle(h)
    return bool(ok) and GAME in buf.value.upper()


def game_runs():
    # CSV: the table form cuts the name at 25 characters. No console window from a windowless start.
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH", "/FI", f"IMAGENAME eq {GAME}.exe"], capture_output=True,
                         text=True, creationflags=0x08000000).stdout
    return GAME in out.upper()


def window(port, key_name, dests):
    import tkinter as tk
    memo = ROOT / "build" / "warp_key.json"
    try:
        last = json.loads(memo.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        last = None
    pick = [tuple(last[:3]) + (tuple(last[3]),) if last else next((d for d in dests if DEFAULT in d[0]), dests[0])]
    shown, state = [], {"down": False, "seen": False, "checked": 0.0}
    root = tk.Tk()
    root.geometry("760x420")
    query = tk.StringVar()
    entry = tk.Entry(root, textvariable=query, font=("Segoe UI", 11))
    entry.pack(fill="x", padx=6, pady=(6, 3))
    box = tk.Listbox(root, font=("Consolas", 10), exportselection=False, activestyle="none")
    box.pack(fill="both", expand=True, padx=6)
    status = tk.Label(root, anchor="w", justify="left", font=("Segoe UI", 10))
    status.pack(fill="x", padx=6, pady=3)

    def say(extra=""):
        root.title(f"Warp key: {key_name.capitalize()} -> {pick[0][0]}")
        status.config(text=f"{key_name.capitalize()} in a game window warps to: {pick[0][0]}{extra}")

    def refill(*_):
        shown[:] = matches(dests, query.get())
        box.delete(0, "end")
        box.insert("end", *[d[0] for d in shown])

    def choose(*_):
        if box.curselection():
            pick[0] = shown[box.curselection()[0]]
            try:
                memo.write_text(json.dumps(pick[0]), encoding="utf-8")
            except OSError:
                pass
            say()

    def warp():
        send(port, pick[0])
        say(f"   (sent {time.strftime('%H:%M:%S')})")

    def poll():
        pressed = bool(user32.GetAsyncKeyState(KEYS[key_name]) & 0x8000)
        if pressed and not state["down"] and front_is_game():
            warp()
        state["down"] = pressed
        if time.time() - state["checked"] > 10:
            state["checked"] = time.time()
            runs = game_runs()
            if state["seen"] and not runs:
                return root.destroy()
            state["seen"] = state["seen"] or runs
        root.after(30, poll)

    tk.Button(root, text="Warp now", command=warp).pack(anchor="e", padx=6, pady=(0, 6))
    query.trace_add("write", refill)
    box.bind("<<ListboxSelect>>", choose)
    entry.bind("<Return>", lambda _: (box.selection_clear(0, "end"), box.selection_set(0), choose()))  # the first match
    refill()
    say()
    entry.focus_set()
    root.iconify()
    root.after(30, poll)
    root.mainloop()


def main():
    args = sys.argv[1:]
    if args[:1] == ["--list"]:
        for d in matches(destinations(), " ".join(args[1:])):
            print(f"{d[1]:02X}/{d[2]:02X} {'/'.join(str(p) for p in d[3]):>17}  {d[0]}".encode("ascii", "replace").decode())
        return
    port = int(args[0]) if args else 27701
    key = args[1].lower() if len(args) > 1 else "delete"
    if key not in KEYS:
        sys.exit(__doc__)
    window(port, key, destinations())


if __name__ == "__main__":
    main()
