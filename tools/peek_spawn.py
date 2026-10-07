"""Read-only check of the shared-spawning leads (MODLOG "Shared spawning: research").

Usage: py tools/peek_spawn.py [--watch]

For every running KH2 copy prints:
  - whether exe+0x2A10420 (SpawnTask's actor) is the entity-list head (Sora)
  - the spawn group table exe+0x2A10010 (count exe+0x2A10418): file name, flags,
    descriptor type/id/entity count
  - every live actor with a spawn record (actor+0x9F0): model, objId and serial
    (u16 at record+0x1E), position
Never writes game memory.
"""
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
import peek  # noqa: E402

LIST_HEAD = 0x2A171C8
SPAWN_ACTOR = 0x2A10420
GROUPS = 0x2A10010
GROUP_COUNT = 0x2A10418
HANDLE_BASES = 0x2B0D720
NEXT = 0xA90
RECORD = 0x9F0


def q(h, a):
    return struct.unpack("<Q", peek.read(h, a, 8))[0]


def u32(h, a):
    return struct.unpack("<I", peek.read(h, a, 4))[0]


def decode(h, base, hd):
    if not hd & 0x80000000:
        return 0
    return q(h, base + HANDLE_BASES + 8 * ((hd >> 25) & 0x3F)) | (hd & 0x1FFFFFF)


def model(h, actor):
    try:
        row = q(h, actor + 0x918)
        return peek.read(h, row + 8, 32).split(b"\0")[0].decode(errors="replace")
    except OSError:
        return "?"


def describe(h, base):
    out = []
    head = q(h, base + LIST_HEAD)
    if not head:
        return "  no actor (title / loading)"
    w, r, d = peek.read(h, base + 0x717008, 3)
    sp = q(h, base + SPAWN_ACTOR)
    out.append(f"  room {w:02X}/{r:02X} door {d:02X}; spawn actor 0x{sp:X} == head 0x{head:X}: {sp == head}")
    n = u32(h, base + GROUP_COUNT)
    out.append(f"  groups: {n}")
    for i in range(min(n, 64)):
        e = base + GROUPS + 16 * i
        name = peek.read(h, e, 4).decode(errors="replace")
        eflags = u32(h, e + 4)
        g = q(h, e + 8)
        try:
            gflags = u32(h, g + 4)
            desc = q(h, g + 8)
            typ, flag, gid, nent = struct.unpack("<BBhh", peek.read(h, desc, 6))
            wave = peek.read(h, g + 0x2C, 1)[0]
            out.append(f"    [{i:2}] {name} entryFlags {eflags:#x} group 0x{g:X} flags {gflags:#x} "
                       f"type {typ} id {gid} entities {nent} wave {wave}")
        except OSError:
            out.append(f"    [{i:2}] {name} entryFlags {eflags:#x} group 0x{g:X} (unreadable)")
    actor, seen = head, 0
    while actor and seen < 200:
        seen += 1
        try:
            rec = q(h, actor + RECORD)
            if rec:
                oid = u32(h, rec)
                serial = struct.unpack("<H", peek.read(h, rec + 0x1E, 2))[0]
                x, y, z = struct.unpack("<3f", peek.read(h, actor + 0x640 + 0x30, 12))
                out.append(f"    actor 0x{actor:X} {model(h, actor):10} objId {oid:#x} serial {serial} "
                           f"rec 0x{rec:X} pos ({x:.0f},{y:.0f},{z:.0f})")
            actor = decode(h, base, u32(h, actor + NEXT))
        except OSError:
            out.append(f"    walk stopped at 0x{actor:X} (unreadable)")
            break
    out.append(f"  actors walked: {seen}")
    return "\n".join(out)


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
            print(f"pid {pid}:\n{describe(h, base)}", flush=True)
        if "--watch" not in sys.argv:
            break
        time.sleep(2)


if __name__ == "__main__":
    main()
