// The co-op save file (TODO 5.7; user picks session 25, 2026-10-10): LOAD
// (CO-OP) lists and saves to "<game exe folder>\KH2 Coop saves\KHIIFM_WW.png",
// NEW GAME and LOAD to the player's normal Steam save, on both PCs. The first
// LOAD (CO-OP) starts the co-op file as a copy of the whole Steam save.
//
// How the game keeps its save (VERIFIED_OFFSETS 'Save container'): the KH2
// save file is read once at start-up into a "container" (exe+0xFEF80, made by
// exe+0x101000), pointer exe+0x79CB10. The save list and every save read or
// write go through that pointer each time, and a save writes the whole
// container back to the file whose path is stored in it (exe+0x100810). So we
// open a second container on our file and swap the pointer when the title's
// LOAD (CO-OP) is confirmed, and swap it back for NEW GAME / LOAD.
// exe+0xFEF80 builds the file path with exe+0x103E40 (always under
// Documents\My Games\...\<Steam ID>); our hook on it gives our folder instead
// while we open ours. The file stays encrypted with the player's own Steam
// key, so a copy only opens for the same Steam account.
// KH2COOP_COOP_SAVE_DIR=<folder> uses another folder (our tests, two copies on one PC).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "coop_save.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSavePath = 0x103E40;  // void(name, dir out, file out)
constexpr std::uint8_t kSavePathBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57,
                                           0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00};
constexpr std::uintptr_t kSaveWrite = 0x100810;  // i32(container): writes the container to its file
constexpr std::uint8_t kSaveWriteBytes[] = {0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56,
                                            0x48, 0x81, 0xEC, 0xB0, 0x05, 0x00, 0x00};
constexpr std::uintptr_t kOpen = 0xFEF80;  // container*(memory, slot size, slot count, name; null = the Steam save)
constexpr std::uint8_t kOpenBytes[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr std::uintptr_t kNew = 0x439064;  // the game's operator new(size)
constexpr std::uint8_t kNewBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kContainer = 0x79CB10;                         // container*: the KH2 save in use
constexpr std::uintptr_t kSlotSize = 0x79CC2C, kSlotCount = 0x79CD34;  // i32, set by exe+0x101000

// Container layout (exe+0xFEF80): +0x00 16-byte header (the Steam key check), +0x10 data*,
// +0x3C status (0 ok, -1 file missing/empty, -2 error), +0x40 folder, +0x144 "\<file name>".
constexpr std::size_t kContainerSize = 0x178, kHeaderSize = 16, kDataOff = 0x10, kStatusOff = 0x3C,
                      kDirOff = 0x40, kDirSize = 0x104, kFileOff = 0x144, kFileSize = 0x20;

using PFN_SavePath = void(__fastcall*)(const char*, char*, char*);
using PFN_SaveWrite = std::int32_t(__fastcall*)(void*);
using PFN_Open = void*(__fastcall*)(void*, std::int32_t, std::int32_t, const char*);
using PFN_New = void*(__fastcall*)(std::size_t);
PFN_SavePath g_realSavePath = nullptr;
PFN_SaveWrite g_realSaveWrite = nullptr;

bool g_on = false;
char g_dir[MAX_PATH] = {};  // our folder (ANSI, as the game's paths), no trailing backslash
bool g_opening = false;     // our exe+0xFEF80 call is running
void* g_real = nullptr;     // the game's own container (the Steam save)
void* g_coop = nullptr;     // ours

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

const char* Str(void* container, std::size_t off) { return static_cast<const char*>(container) + off; }

bool SameBytes(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t size) {
    return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, size) == 0;
}

void __fastcall HookSavePath(const char* name, char* dir, char* file) {
    if (!g_opening || !file) {
        g_realSavePath(name, dir, file);
        return;
    }
    strcpy_s(dir, kDirSize, g_dir);
    const char* base = std::strrchr(name, '\\');
    std::snprintf(file, kFileSize, "\\%s", base ? base + 1 : name);
}

std::int32_t __fastcall HookSaveWrite(void* container) {
    std::int32_t result = g_realSaveWrite(container);
    const char* which = container == g_coop                                  ? "the CO-OP save file"
                        : container == (g_real ? g_real : Read<void*>(kContainer)) ? "the Steam save"
                                                                             : "another save file";
    Log("save: the game wrote %s (%s), result %d, co-op %s", which, container == g_coop ? g_dir : Str(container, kFileOff),
        result, CoopActive() ? "on" : "off");
    return result;
}

// Opens our container the first time (copying the Steam save to start it).
bool OpenCoop() {
    if (g_coop) return true;
    void* real = Read<void*>(kContainer);
    if (!real) {
        Log("co-op save: the game has no save file open (no Steam save yet?); LOAD (CO-OP) can't be used");
        return false;
    }
    char realPath[2 * MAX_PATH], coopPath[2 * MAX_PATH], name[64];
    std::snprintf(realPath, sizeof(realPath), "%s%s", Str(real, kDirOff), Str(real, kFileOff));
    std::snprintf(coopPath, sizeof(coopPath), "%s%s", g_dir, Str(real, kFileOff));
    if (GetFileAttributesA(coopPath) == INVALID_FILE_ATTRIBUTES) {
        char part[MAX_PATH];  // the folder and any missing parents
        strcpy_s(part, g_dir);
        for (char* p = part + 3; *p; ++p)
            if (*p == '\\' || *p == '/') {
                char c = *p;
                *p = 0;
                CreateDirectoryA(part, nullptr);
                *p = c;
            }
        CreateDirectoryA(part, nullptr);
        if (!CopyFileA(realPath, coopPath, TRUE)) {
            Log("co-op save: could not start %s as a copy of the Steam save (error %lu)", coopPath, GetLastError());
            return false;
        }
        Log("co-op save: first LOAD (CO-OP): started %s as a copy of your Steam save", coopPath);
    }
    void* memory = reinterpret_cast<PFN_New>(ExeBase() + kNew)(kContainerSize);
    if (!memory) return false;
    std::snprintf(name, sizeof(name), "KH2 Coop saves%s", Str(real, kFileOff));
    g_opening = true;
    void* coop = reinterpret_cast<PFN_Open>(ExeBase() + kOpen)(memory, Read<std::int32_t>(kSlotSize),
                                                               Read<std::int32_t>(kSlotCount), name);
    g_opening = false;
    // A copy for another Steam account, or a damaged file, leaves the data empty or the header wrong.
    auto data = *reinterpret_cast<void* const*>(static_cast<char*>(coop) + kDataOff);
    std::int32_t status = *reinterpret_cast<const std::int32_t*>(static_cast<char*>(coop) + kStatusOff);
    if (status != 0 || std::strcmp(Str(coop, kDirOff), g_dir) != 0 || !data ||
        std::memcmp(coop, real, kHeaderSize) != 0 || std::memcmp(data, coop, kHeaderSize) != 0) {
        Log("co-op save: could not read %s (status %d, folder \"%s\", %s); LOAD (CO-OP) can't be used. If it was "
            "copied from another PC or Steam account, delete it: the next LOAD (CO-OP) starts a new copy",
            coopPath, status, Str(coop, kDirOff),
            std::memcmp(coop, real, kHeaderSize) != 0 ? "not this Steam account's save" : "no save data");
        return false;  // the container stays allocated: freeing it needs more of the game's code
    }
    g_real = real;
    g_coop = coop;
    Log("co-op save: opened %s (slot size %d, %d slots)", coopPath, Read<std::int32_t>(kSlotSize),
        Read<std::int32_t>(kSlotCount));
    return true;
}

}  // namespace

void CoopSaveInit() {
    char custom[MAX_PATH];
    if (EnvStr("KH2COOP_COOP_SAVE_DIR", custom, sizeof(custom))) {
        wchar_t wide[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, custom, -1, wide, MAX_PATH);
        WideCharToMultiByte(CP_ACP, 0, wide, -1, g_dir, sizeof(g_dir), nullptr, nullptr);
    } else {
        DWORD len = GetModuleFileNameA(nullptr, g_dir, sizeof(g_dir));
        char* slash = len && len < sizeof(g_dir) ? std::strrchr(g_dir, '\\') : nullptr;
        if (slash) strcpy_s(slash, sizeof(g_dir) - (slash - g_dir), "\\KH2 Coop saves");
        else g_dir[0] = 0;
    }
    std::size_t len = std::strlen(g_dir);
    while (len && (g_dir[len - 1] == '\\' || g_dir[len - 1] == '/')) g_dir[--len] = 0;
    if (!len || len + kFileSize >= kDirSize) {
        Log("co-op save: OFF: the co-op save folder \"%s\" is empty or too long", g_dir);
        return;
    }
    if (!SameBytes(kOpen, kOpenBytes, sizeof(kOpenBytes)) || !SameBytes(kNew, kNewBytes, sizeof(kNewBytes))) {
        Log("co-op save: OFF: the game's save code differs (different game build?)");
        return;
    }
    g_on = HookFunctionAlways(kSavePath, kSavePathBytes, sizeof(kSavePathBytes), reinterpret_cast<void*>(HookSavePath),
                              reinterpret_cast<void**>(&g_realSavePath), "save path (co-op save)") &&
           HookFunctionAlways(kSaveWrite, kSaveWriteBytes, sizeof(kSaveWriteBytes),
                              reinterpret_cast<void*>(HookSaveWrite), reinterpret_cast<void**>(&g_realSaveWrite),
                              "save write (co-op save)");
    Log("co-op save: %s%s", g_on ? "LOAD (CO-OP) uses the co-op save file in " : "OFF (hooks failed)",
        g_on ? g_dir : "");
}

bool CoopSaveSelect(bool coop) {
    void* now = Read<void*>(kContainer);
    if (!coop) {
        if (g_coop && now == g_coop) {
            *reinterpret_cast<void* volatile*>(ExeBase() + kContainer) = g_real;
            Log("co-op save: back to your Steam save");
        }
        return true;
    }
    if (!g_on || !OpenCoop()) return false;
    if (now != g_coop) {
        if (now != g_real) {
            Log("co-op save: the game's own save container changed since we opened ours; keeping the new one as the "
                "Steam save");
            g_real = now;
        }
        *reinterpret_cast<void* volatile*>(ExeBase() + kContainer) = g_coop;
        Log("co-op save: the save list and saves now use the co-op save file");
    }
    return true;
}

}  // namespace kh2coop
