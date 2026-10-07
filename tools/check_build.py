"""Read-only check: do the code bytes at known addresses in the KH2 exe match
what another project recorded for its build? Never modifies the exe.

Usage: py tools/check_build.py "<path to KINGDOM HEARTS II FINAL MIX.exe>"
"""
import struct
import sys

# (name, RVA, expected bytes). Leads from Volpestyle/kh2-multiplayer
# (inject/src/Warp.cpp, EntityHook.cpp), recorded for "Steam Global 1.0.0.10".
# Entries marked pin=True contain RIP-relative operands, so they only match
# that exact build.
CHECKS = [
    ("RequestTransition", 0x152990, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57 48 83 ec 20 80 3d 26 7f", True),
    ("LoadComplete", 0x152CD0, "40 53 48 83 ec 20 80 3d 2b 43 5c 00 0f 48 8b d9 75 05 e8 59 ac fa ff c6", True),
    ("LoadCompleteDirect", 0x152F40, "48 83 ec 28 80 3d bd 40 5c 00 0f 75 05 e8 ee a9 fa ff c6 05 77 79 86 00", True),
    ("LimitByCmd", 0x3E7C30, "4c 8b 1d 59 da 6f 02 45 33 c9 4d 63 53 04 4d 85", True),
    ("LimitMenuState", 0x3D88E0, "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 57", False),
    ("ApplyStatDelta", 0x3D2EB0, "48 89 5c 24 10 48 89 6c 24 18 56 48 83 ec 20 48", False),
    ("ApplyHitDamage", 0x3D3BA0, "48 89 74 24 18 57 48 83 ec 20 48 8b f1 48 8b fa", False),
]


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0", "not a PE file"
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    off = pe + 24 + opt_size
    for i in range(nsec):
        name, vsize, va, rawsize, rawptr = struct.unpack_from("<8sIIII", data, off + i * 40)
        yield name.rstrip(b"\0").decode(), va, vsize, rawptr, rawsize


def main():
    data = open(sys.argv[1], "rb").read()
    secs = list(sections(data))
    for s in secs:
        print(f"section {s[0]:8} va=0x{s[1]:X} vsize=0x{s[2]:X} raw=0x{s[3]:X}")
    ok_all = True
    for name, rva, hexbytes, pin in CHECKS:
        want = bytes.fromhex(hexbytes)
        sec = next((s for s in secs if s[1] <= rva < s[1] + max(s[2], s[4])), None)
        got = data[sec[3] + rva - sec[1]: sec[3] + rva - sec[1] + len(want)] if sec else b""
        ok = got == want
        ok_all &= ok
        print(f"{'MATCH' if ok else 'DIFF '} {name:20} rva=0x{rva:X} {'(build-pinning)' if pin else ''}")
        if not ok:
            print(f"      want {want.hex(' ')}\n      got  {got.hex(' ')}")
    print("ALL MATCH: same build as the leads" if ok_all else "MISMATCH: leads need re-finding on this build")


if __name__ == "__main__":
    main()
