#pragma once
// Shared helpers for the kh2coop DLL's source files.

#include <cstddef>
#include <cstdint>

namespace kh2coop {

// Appends a timestamped line to this process's kh2coop_<pid>.log.
void Log(const char* fmt, ...);

// Base address of KINGDOM HEARTS II FINAL MIX.exe in memory (0 if not KH2).
std::uintptr_t ExeBase();

// Whether [address, address + bytes) is committed, readable memory with no guard page (VirtualQuery; no read is
// tried). Check an arbitrary pointer with this before reading it: a fault, even inside __try, can kill the game
// (a guard page read silently removes it; the .NET runtime's handler in the game has died on such faults).
bool Readable(std::uintptr_t address, std::uint32_t bytes);

// A read that a fault cannot turn into a lost game (safe_read.cpp, CRASH_RISK_REVIEW section 4 way 1): the read runs
// in a few bytes of code of ours that our own vectored handler, first in the process's list, lands on "false" when
// they fault, so the .NET runtime's handler never sees the fault. Costs a call and a compare, no system call. For
// pointers inside areas we know are mapped (KnownArea): a pointer that could point at a thread's stack goes through
// Readable first, which sees a guard page without touching it (condition 3). Falls back to __try as before.
bool SafeReadBytes(const void* at, void* out, int size);  // size 1, 2, 4 or 8
template <typename T>
inline bool SafeRead(const void* at, T* out) {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "1, 2, 4 or 8 bytes");
    return SafeReadBytes(at, out, sizeof(T));
}
template <typename T>
inline bool SafeRead(std::uintptr_t at, T* out) {
    return SafeRead(reinterpret_cast<const void*>(at), out);
}
// Whether [address, address + bytes) lies in an area we know is mapped while the game runs (way 2: a compare, no
// system call): the file heap, the exe image. A pointer outside them needs Readable before any read.
bool KnownArea(std::uintptr_t address, std::uint32_t bytes);
// A pointer for any read: SafeRead inside a known area, Readable then SafeRead outside (those are counted, and
// SafeReadFrame logs them: a walk that leaves the known areas often costs a system call per read).
bool ReadAnyBytes(std::uintptr_t at, void* out, int size);
template <typename T>
inline bool ReadAny(std::uintptr_t at, T* out) {
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8, "1, 2, 4 or 8 bytes");
    return ReadAnyBytes(at, out, sizeof(T));
}

// The caller's call chain as exe offsets ("0x152A1C <- 0x3A4F90 <- ..."; "?" outside the exe),
// skipping `skip` frames above the function that calls this.
void CallerChain(char* out, unsigned size, int skip);

// Settings: the environment variable `name` (KH2COOP_...) if set, else the
// same key without the "KH2COOP_" prefix in section [kh2coop] of kh2coop.ini
// (game folder first, then next to this DLL), or for a personal key
// (kPlayerKeys in kh2coop.cpp) in section [player] of kh2coop_player.ini,
// else `fallback`.
int EnvInt(const char* name, int fallback);
// Same lookup for text; returns false (and an empty `out`) when unset.
bool EnvStr(const char* name, char* out, unsigned size);
// The partner setting in the link's form: steam, steam:<SteamID64>, <ipv4>:<port> or auto.
// PEER=steam takes STEAM_ID= (the host's SteamID64; empty = wait for Join Game; ignored with
// ROLE=host), PEER=local takes LOCAL_IP= (the host's IP, port 27701 unless given as ip:port).
// The old one-line forms (steam:<id>, ip:port) still work. False when unset or incomplete (logged).
bool PeerSetting(char* out, unsigned size);
// Finds kh2coop.ini and kh2coop_player.ini (writes the latter with the
// defaults if missing); call once from OnInit before reading settings.
void LoadSettingsFile();

// MinHook detour on the exe function at `rva`, only if its first bytes equal
// `expected` (guards against a different game build). Logs the outcome.
// A co-op hook: it runs only while co-op is on (CoopActive), off otherwise.
bool HookFunction(std::uintptr_t rva, const std::uint8_t* expected, std::size_t size, void* detour,
                  void** original, const char* name);
// Same, but always on (the title's LOAD (CO-OP) row, the save path, controller input).
bool HookFunctionAlways(std::uintptr_t rva, const std::uint8_t* expected, std::size_t size, void* detour,
                        void** original, const char* name);

// Co-op on/off (TODO 5.7, user pick session 25): off at game start and after the title's
// NEW GAME / LOAD (the game plays as normal KH2), on after LOAD (CO-OP). While off, the co-op
// hooks are disabled, the per-frame co-op code doesn't run and the link ignores the friend.
bool CoopActive();
// Asks for co-op on/off; takes effect at the next OnFrame (between game frames).
void RequestCoop(bool on, const char* why);

// Redirects game writes into the save folder to a sandbox (see save_guard.cpp):
// always when `forced` (KH2COOP_SAVE_GUARD=1, tests), else only while co-op is on.
void InstallSaveGuard(bool forced);

// Button injection into the game's input (raw slot 0, PS2 bit layout, e.g.
// 0x0008 = Start). InputInjectEnable() installs the input hook if no
// controller filter / virtual pad did (pass-through: the game's own input is
// untouched); InputInject ORs `bits` in for the next `frames` input reads.
bool InputInjectEnable();
void InputInject(std::uint16_t bits, int frames);
// Clears these button bits (and centres the left stick when any bit is set; the right stick, the
// camera, stays free) in raw slot 0 until called with 0.
void InputBlock(std::uint16_t bits);

// The window really in front (the game's GetForegroundWindow is hooked to always answer the game's window).
void* RealForegroundWindow();

// Logs fatal exceptions (location, registers, rough call chain) to our log.
void InstallCrashLogger();
void KeepCrashLoggerFirst();  // re-adds the handler in front of any added since
void CrashLoggerFrame();  // from OnFrame: notes clr.dll once loaded; logs the frame thread and its stack, once

// The safe read's code page and its handler (safe_read.cpp): SafeReadInit from OnInit after InstallCrashLogger,
// SafeReadFrame from OnFrame (puts the handler back at the head, logs what it caught), SafeReadOwns for the crash
// logger (a fault there is the safe read's, not a crash), SafeReadDebugTest for tools/debug_cmd.py saferead.
void SafeReadInit();
void SafeReadFrame();
bool SafeReadOwns(std::uintptr_t rip);
void SafeReadDebugTest();

}  // namespace kh2coop
