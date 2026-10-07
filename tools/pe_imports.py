"""Read-only: list a PE file's imported DLLs and functions (optionally filtered).

Usage: py tools/pe_imports.py <exe> [substring ...]
"""
import struct
import sys


def rva_to_off(secs, rva):
    for va, vsize, raw, rawsize in secs:
        if va <= rva < va + max(vsize, rawsize):
            return raw + rva - va
    raise ValueError(hex(rva))


def main():
    data = open(sys.argv[1], "rb").read()
    filters = [f.lower() for f in sys.argv[2:]]
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    opt = pe + 24
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    secs = [struct.unpack_from("<4xIIII", data, opt + opt_size + i * 40 + 4)[:4] for i in range(nsec)]
    secs = [(va, vs, raw, rs) for vs, va, rs, raw in secs]
    imp_rva = struct.unpack_from("<I", data, opt + 112 + 8)[0]  # PE32+: data dir 1
    off = rva_to_off(secs, imp_rva)
    while True:
        ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", data, off)
        if name_rva == 0:
            break
        dll = data[rva_to_off(secs, name_rva):].split(b"\0")[0].decode()
        thunk = rva_to_off(secs, ilt or iat)
        i = 0
        while True:
            v = struct.unpack_from("<Q", data, thunk + i * 8)[0]
            if v == 0:
                break
            fn = f"#{v & 0xFFFF}" if v >> 63 else data[rva_to_off(secs, v & 0x7FFFFFFF) + 2:].split(b"\0")[0].decode()
            line = f"{dll}!{fn}  IAT rva 0x{iat + i * 8:X}"
            if not filters or any(f in line.lower() for f in filters):
                print(line)
            i += 1
        off += 20


if __name__ == "__main__":
    main()
