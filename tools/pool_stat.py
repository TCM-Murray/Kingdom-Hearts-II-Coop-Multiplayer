"""Read-only: how full the game's effect pool is (the heap that ran out in the host crash of 2026-10-06 23:50).

Usage: py tools/pool_stat.py <pid> [<pid> ...] [--every <s>]

Pool struct pointer at exe+0x2B0EC08 (allocator exe+0x4AF240): +0 current chunk, +8 first chunk,
+0x18 u32 bytes in use (bench 2026-10-07: rises by each allocation, falls back when freed; plus
the free bytes = 521008 on this build). Chunks have 16-byte headers: u32 handle of the chunk's end,
u32 handle of the previous chunk, u32 in use; walking them from the first chunk only finds the free ones. Handles decode with
exe+0x4AD3F0: bit 31 cleared, base table exe+0x2B0D720[h >> 25] | (h & 0x1FFFFFF).
"""
import struct
import sys
import time

import peek

POOL = 0x2B0EC08
HANDLE_TABLE = 0x2B0D720


def stat(h, base):
    table = struct.unpack("<64Q", peek.read(h, base + HANDLE_TABLE, 64 * 8))

    def dec(v):
        if not v:
            return 0
        v &= 0x7FFFFFFF
        return table[v >> 25] | (v & 0x1FFFFFF)

    pool = struct.unpack("<Q", peek.read(h, base + POOL, 8))[0]
    if not pool:
        return "no pool"
    cur, first, alt, counter = struct.unpack("<QQQI", peek.read(h, pool, 28))
    head = dec(struct.unpack("<I", peek.read(h, first, 4))[0])
    nxt = struct.unpack("<I", peek.read(h, head + 4, 4))[0]
    used = free = n_used = n_free = largest = 0
    seen = 0
    while nxt and seen < 200000:
        c = dec(nxt)
        end_h, nxt, inuse = struct.unpack("<III", peek.read(h, c, 12))
        size = dec(end_h) - c
        if inuse:
            used += size
            n_used += 1
        else:
            free += size
            n_free += 1
            largest = max(largest, size)
        seen += 1
    return f"in use {counter} B, free {free} B in {n_free} chunks (largest {largest})"


def main():
    args = sys.argv[1:]
    every = 0
    if "--every" in args:
        i = args.index("--every")
        every = float(args[i + 1])
        del args[i:i + 2]
    procs = []
    for a in args:
        pid = int(a)
        procs.append((pid, peek.k32.OpenProcess(peek.PROCESS_VM_READ | peek.PROCESS_QUERY_INFORMATION, False, pid),
                      peek.module_base(pid, peek.EXE)))
    while True:
        for pid, h, base in procs:
            try:
                print(time.strftime("%H:%M:%S"), pid, stat(h, base), flush=True)
            except OSError as e:
                print(time.strftime("%H:%M:%S"), pid, "read failed:", e, flush=True)
        if not every:
            break
        time.sleep(every)


if __name__ == "__main__":
    main()
