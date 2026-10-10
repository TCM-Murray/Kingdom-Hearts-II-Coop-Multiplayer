#pragma once
// Shared helpers for the kh2coop DLL's source files.

#include <cstddef>
#include <cstdint>

namespace kh2coop {

// Appends a timestamped line to this process's kh2coop_<pid>.log.
void Log(const char* fmt, ...);

// Base address of KINGDOM HEARTS II FINAL MIX.exe in memory (0 if not KH2).
std::uintptr_t ExeBase();

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
// Finds kh2coop.ini and kh2coop_player.ini (writes the latter with the
// defaults if missing); call once from OnInit before reading settings.
void LoadSettingsFile();

// MinHook detour on the exe function at `rva`, only if its first bytes equal
// `expected` (guards against a different game build). Logs the outcome.
bool HookFunction(std::uintptr_t rva, const std::uint8_t* expected, std::size_t size, void* detour,
                  void** original, const char* name);

// Points the game exe's import `dll!name` at `hook`; returns the previous
// function, or null if the exe doesn't import it.
void* PatchExeImport(const char* dll, const char* name, void* hook);

// Redirects game writes into the save folder to a sandbox (see save_guard.cpp).
void InstallSaveGuard();

// Button injection into the game's input (raw slot 0, PS2 bit layout, e.g.
// 0x0008 = Start). InputInjectEnable() installs the input hook if no
// controller filter / virtual pad did (pass-through: the game's own input is
// untouched); InputInject ORs `bits` in for the next `frames` input reads.
bool InputInjectEnable();
void InputInject(std::uint16_t bits, int frames);
// Clears these button bits (and centres the left stick when any bit is set; the right stick, the
// camera, stays free) in raw slot 0 until called with 0.
void InputBlock(std::uint16_t bits);

// Logs fatal exceptions (location, registers, rough call chain) to our log.
void InstallCrashLogger();
void KeepCrashLoggerFirst();  // re-adds the handler in front of any added since

}  // namespace kh2coop
