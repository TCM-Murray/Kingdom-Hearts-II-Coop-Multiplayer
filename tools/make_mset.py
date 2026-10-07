"""Build Sora's and his Keyblade's motion sets with the Mickey-rescue motions added (downed state,
see src/dll/downed.cpp).

Usage: py tools/make_mset.py [out folder]   (default: build/mod_assets/obj; writes P_EX100.mset, W_EX010.mset)

Reads this PC's own extracted game files (OpenKH data folder); never ships game data in the repo.
In the game, motions 252 (lying on the ground with stars) and 253 (getting up) exist only in
Mickey's motion set P_EX200.mset, as type-20 entries: small archives with per-character
sub-motions. Group "0010" holds Sora's (228 bones, like every Sora motion), "0011" the
Keyblade's. The game plays them on Sora through Mickey's motion bank during the rescue
(exe+0x4158C0 -> exe+0x3C6CC0(bank, Sora, 252)). Here Sora's own two sub-motions become plain
entries of Sora's set at the same slots (motion id * 4 + variant: 1008 and 1012), so the game's
normal SetMotion finds them on Sora. The Keyblade's part of 253 (group "0011", 12 bones: it
reappears in his hand at the end) goes into W_EX010.mset, the Keyblade set of normal Sora (same
839-slot layout as his own), so the weapon plays it along (user test 2026-10-06: without it the
Keyblade only showed once control was back).
"""
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OBJ = pathlib.Path(r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS openkh\data\kh2\obj")
OUT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "mod_assets" / "obj"

TYPE_ANB = 17
SORA_BONES, KEYBLADE_BONES = 0xE4, 12
# file -> (bones, {motion id: (entry in Mickey's set, sub-motion index inside it)}), verified 2026-10-06 by bone count
TARGETS = {
    "P_EX100.mset": (SORA_BONES, {252: (1008, 2), 253: (1012, 1)}),
    "W_EX010.mset": (KEYBLADE_BONES, {253: (1012, 5)}),
}


def entries(d, base=0):
    assert d[base:base + 4] == b"BAR\x01", "not a BAR"
    n = struct.unpack_from("<I", d, base + 4)[0]
    return [list(struct.unpack_from("<HH4sII", d, base + 0x10 + i * 16)) for i in range(n)]


def bones(anb):
    for t, _, _, off, size in entries(anb):
        if t == 9 and size >= 0xA2:
            return struct.unpack_from("<H", anb, off + 0xA0)[0]
    return None


def build(file, want_bones, wanted, mickey):
    sora = (OBJ / file).read_bytes()
    sora_e = entries(sora)
    mick_e = entries(mickey)
    assert len(sora_e) == 839, f"unexpected {file} ({len(sora_e)} entries): game update or another mod?"

    new_blobs = {}
    for motion, (idx, sub) in wanted.items():
        t, _, name, off, size = mick_e[idx]
        assert t == 20, f"Mickey entry {idx} type {t}"
        inner = mickey[off:off + size]
        st, _, sname, soff, ssize = entries(inner)[sub]
        anb = inner[soff:soff + ssize]
        assert st == TYPE_ANB and bones(anb) == want_bones, f"{file} motion {motion}: wrong skeleton"
        new_blobs[motion * 4] = (sname, anb)
        print(f"{file} motion {motion}: Mickey entry {idx} ({name.decode()}) sub {sub} -> slot {motion * 4}, {ssize} bytes")

    count = max(new_blobs) + 1
    table_end = 0x10 + 16 * count
    old_data_start = 0x10 + 16 * len(sora_e)
    shift = table_end - old_data_start
    assert shift % 16 == 0
    data = bytearray(sora[old_data_start:])
    dumm_off = None
    table = []
    for e in sora_e:
        e = list(e)
        e[3] += shift
        if e[2] == b"DUMM" and dumm_off is None:
            dumm_off = e[3]
        table.append(e)
    for slot in range(len(sora_e), count):
        if slot in new_blobs:
            name, anb = new_blobs[slot]
            while len(data) % 16:
                data.append(0)
            table.append([TYPE_ANB, 0, name, table_end + len(data), len(anb)])
            data += anb
        else:
            table.append([TYPE_ANB, 1, b"DUMM", dumm_off, 0])
    header = struct.pack("<4sIII", b"BAR\x01", count, *struct.unpack_from("<II", sora, 8))
    out = bytearray(header)
    for e in table:
        out += struct.pack("<HH4sII", *e)
    assert len(out) == table_end
    out += data
    # Round trip: the old motions are byte-identical, the new ones resolve to Sora motions.
    chk = entries(bytes(out))
    for i, e in enumerate(sora_e):
        assert bytes(out[chk[i][3]:chk[i][3] + chk[i][4]]) == sora[e[3]:e[3] + e[4]], f"entry {i} changed"
    for slot in new_blobs:
        assert bones(bytes(out[chk[slot][3]:chk[slot][3] + chk[slot][4]])) == want_bones
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / file).write_bytes(out)
    print(f"{OUT / file}: {count} entries, {len(out)} bytes (was {len(sora)})")


def main():
    mickey = (OBJ / "P_EX200.mset").read_bytes()
    for file, (want_bones, wanted) in TARGETS.items():
        build(file, want_bones, wanted, mickey)


if __name__ == "__main__":
    main()
