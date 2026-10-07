"""Read-only: find instructions in .text whose RIP-relative operand lands in [lo, hi).

Usage: py tools/riprefs.py <lo rva hex> <hi rva hex>
Raw-byte scan for candidate disp32 values, then confirmed with capstone.
"""
import struct
import sys

import capstone

EXE = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe"
data = open(EXE, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
_, vsize, va, rawsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size)  # .text first
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
text = data[raw:raw + rawsize]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
seen = set()
for i in range(len(text) - 4):
    disp = struct.unpack_from("<i", text, i)[0]
    base = va + i + 4
    if not (lo - 4 <= base + disp < hi):  # instruction may end up to 4 bytes after disp (imm)
        continue
    for start in range(i - 1, max(i - 8, -1), -1):
        ins = next(md.disasm(text[start:start + 16], va + start), None)
        if not ins or ins.address in seen:
            continue
        end = ins.address + ins.size
        if "rip" in ins.op_str and start + ins.size > i and lo <= end + disp < hi and end - (va + i) <= 8:
            seen.add(ins.address)
            print(f"0x{ins.address:X}: {ins.mnemonic} {ins.op_str}   ; -> 0x{end + disp:X}")
            break
