"""Read-only: list the motion triggers (BAR type 16 inside a motion's ANB) of Sora's rescue motions and of
any Sora motion, from this PC's extracted files.

Usage: py tools/anb_triggers.py [P_EX100 slot ...]
Layout (fits every file checked): u8 range count, u8 frame count, u16 offset of the frame triggers;
range trigger = s16 start, s16 end (-1 = whole motion), u8 type, u8 param count, s16 params;
frame trigger = s16 frame, u8 type, u8 param count, s16 params.
"""
import struct
import sys

import make_mset as m


def triggers(anb):
    for t, _, _, off, size in m.entries(anb):
        if t == 16:
            return anb[off:off + size]
    return b""


def parse(d):
    if not d:
        return []
    rc, fc, fo = struct.unpack_from("<BBH", d, 0)
    out, p = [], 4
    for _ in range(rc):
        s, e, tr, ps = struct.unpack_from("<hhBB", d, p)
        out.append(f"range {s}..{e} type 0x{tr:02X} {list(struct.unpack_from(f'<{ps}h', d, p + 6))}")
        p += 6 + 2 * ps
    p = fo
    for _ in range(fc):
        f, tr, ps = struct.unpack_from("<hBB", d, p)
        out.append(f"frame {f} type 0x{tr:02X} {list(struct.unpack_from(f'<{ps}h', d, p + 4))}")
        p += 4 + 2 * ps
    return out


def main():
    mick = (m.OBJ / "P_EX200.mset").read_bytes()
    me = m.entries(mick)
    for motion, idx, sub in [(252, 1008, 2), (253, 1012, 1), (253, 1012, 5)]:
        _, _, _, off, size = me[idx]
        inner = mick[off:off + size]
        _, _, _, so, ss = m.entries(inner)[sub]
        print(f"rescue {motion} (Mickey entry {idx} sub {sub}):")
        for line in parse(triggers(inner[so:so + ss])):
            print("   ", line)
    sora = (m.OBJ / "P_EX100.mset").read_bytes()
    se = m.entries(sora)
    for slot in map(int, sys.argv[1:]):
        _, _, name, off, size = se[slot]
        print(f"P_EX100 slot {slot} ({name.decode()}):")
        if size:
            for line in parse(triggers(sora[off:off + size])):
                print("   ", line)


if __name__ == "__main__":
    main()
