"""Read-only: find direct CALL/JMP rel32 instructions in .text that target an RVA.

Usage: py tools/xrefs.py <target rva hex>
"""
import struct
import sys

EXE = r"D:\SteamLibrary\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe"
data = open(EXE, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt_size = struct.unpack_from("<H", data, pe + 20)[0]
_, vsize, va, rawsize, raw = struct.unpack_from("<8sIIII", data, pe + 24 + opt_size)  # .text first
target = int(sys.argv[1], 16)
text = data[raw:raw + rawsize]
for i in range(len(text) - 5):
    if text[i] in (0xE8, 0xE9):
        rel = struct.unpack_from("<i", text, i + 1)[0]
        if va + i + 5 + rel == target:
            print(f"{'call' if text[i] == 0xE8 else 'jmp '} from 0x{va + i:X}")
