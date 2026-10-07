"""Read-only: disassemble the KH2 exe at an RVA (x64).

Usage: py tools/disasm.py <rva hex> [count=40]
"""
import struct
import sys

import capstone

EXE = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe"
data = open(EXE, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
secs = []
for i in range(nsec):
    _, vsize, va, rawsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size + i * 40)
    secs.append((va, max(vsize, rawsize), raw))
rva = int(sys.argv[1], 16)
count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
va, size, raw = next(s for s in secs if s[0] <= rva < s[0] + s[1])
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for n, ins in enumerate(md.disasm(data[raw + rva - va: raw + rva - va + count * 15], rva)):
    if n >= count:
        break
    print(f"0x{ins.address:X}: {ins.mnemonic} {ins.op_str}")
