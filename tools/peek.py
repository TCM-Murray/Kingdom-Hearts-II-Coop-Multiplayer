"""Read-only live view of a few KH2 values. Never writes game memory.

Usage: py tools/peek.py            (refreshes twice a second; Ctrl+C to stop)
       py tools/peek.py --once

Addresses are offsets from the exe's base address in memory. They come from
Volpestyle/kh2-multiplayer (same build, see tools/check_build.py) and are
leads until this script shows them behaving correctly in the live game.
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time

EXE = "KINGDOM HEARTS II FINAL MIX.exe"

NOW = 0x717008            # world u8, room u8, door u8
SLOT0 = 0x2A23598         # Sora's stat slot; starts with u32 HP, u32 max HP
SLOT_STRIDE = 0x278       # party slots sit BELOW Sora: slot k at SLOT0 - k*stride
PARTY_SLOTS = 3           # how many slots below Sora to show

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPMODULE = 0x8 | 0x10

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", wt.WCHAR * 260)]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD),
                ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wt.DWORD),
                ("hModule", wt.HMODULE), ("szModule", wt.WCHAR * 256),
                ("szExePath", wt.WCHAR * 260)]


def find_pids(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    entry = PROCESSENTRY32W(dwSize=ctypes.sizeof(PROCESSENTRY32W))
    pids = []
    ok = k32.Process32FirstW(snap, ctypes.byref(entry))
    while ok:
        if entry.szExeFile.lower() == name.lower():
            pids.append(entry.th32ProcessID)
        ok = k32.Process32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return pids


def module_base(pid, name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)
    entry = MODULEENTRY32W(dwSize=ctypes.sizeof(MODULEENTRY32W))
    ok = k32.Module32FirstW(snap, ctypes.byref(entry))
    base = None
    while ok:
        if entry.szModule.lower() == name.lower():
            base = entry.modBaseAddr
            break
        ok = k32.Module32NextW(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return base


def read(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    n = ctypes.c_size_t()
    if not k32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(n)):
        raise OSError(ctypes.get_last_error(), f"ReadProcessMemory failed at 0x{addr:X}")
    return buf.raw


def snapshot(handle, base):
    world, room, door = read(handle, base + NOW, 3)
    slots = []
    for k in range(PARTY_SLOTS + 1):
        hp, max_hp = struct.unpack("<II", read(handle, base + SLOT0 - k * SLOT_STRIDE, 8))
        slots.append(f"{'sora' if k == 0 else f'slot-{k}'} {hp}/{max_hp}")
    return f"world 0x{world:02X} room 0x{room:02X} door 0x{door:02X} | " + " | ".join(slots)


def main():
    pids = find_pids(EXE)
    if not pids:
        sys.exit(f"{EXE} is not running.")
    if len(pids) > 1:
        print(f"several KH2 processes {pids}; using the first")
    pid = pids[0]
    base = module_base(pid, EXE)
    handle = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
    if not handle or not base:
        sys.exit(f"could not open pid {pid} (error {ctypes.get_last_error()})")
    print(f"pid {pid}, exe base 0x{base:X}")
    last = None
    try:
        while True:
            line = snapshot(handle, base)
            if line != last:
                print(time.strftime("%H:%M:%S"), line, flush=True)
                last = line
            if "--once" in sys.argv:
                break
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        k32.CloseHandle(handle)


if __name__ == "__main__":
    main()
