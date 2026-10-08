// kh2coop.dll: loaded by OpenKH Panacea from <mod_path>\kh2\dll\.
// Panacea calls OnInit(modFolder) once at startup and OnFrame() once per game
// frame on the game's own thread (see OpenKh.Research.Panacea/Panacea.cpp,
// LoadDLLs / FrameHook).
//
// Slice 1: prove we run inside the game. Read-only: logs world/room and
// Sora's HP once a second to kh2coop_<pid>.log next to this DLL.
// Slice 2a: also logs what XInput reports inside the game process (which pad
// slots are connected and button changes), so we can see how Steam Input
// presents each physical controller before filtering pads per game copy.
// Slice 2b: per-copy controller assignment for running two copies on one PC.
// Both are off unless the copy is launched with these environment variables:
//   KH2COOP_PAD=<n>          this copy is driven by XInput slot n only: after KH2's
//                            input collector runs each frame, we overwrite its raw
//                            slot 0 with that pad (PS2 layout) and clear the rest.
//                            Also filters the game's own XInputGetState calls.
//   KH2COOP_BACKGROUND=1     this copy keeps reading input while unfocused
//   KH2COOP_XINPUT_ONLY=1    Steam Input reports no controllers to the game and
//                            DirectInput lists no game controllers (keyboard and
//                            mouse untouched), so every pad goes through XInput,
//                            where KH2COOP_PAD filters it. (Refusing the whole
//                            "SteamInput006" interface crashes the game: it
//                            never null-checks.) KH2 enumerates DirectInput
//                            devices with DI8DEVCLASS_GAMECTRL at exe+0x133642.
// Done by swapping two entries in the game exe's import table: XInputGetState
// (XINPUT1_4 ordinal 2) and GetForegroundWindow. The game only acts on pad
// input while its window is the foreground window.

#include <winsock2.h>
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>

#include <MinHook.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <share.h>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "aggro.hpp"
#include "follow.hpp"
#include "load_barrier.hpp"
#include "game_over.hpp"
#include "world_sync.hpp"
#include "spawn_sync.hpp"
#include "downed.hpp"
#include "pause_sync.hpp"
#include "watch.hpp"

using namespace kh2coop;

namespace {

// Verified addresses: see VERIFIED_OFFSETS.md.
constexpr std::uintptr_t kNow = 0x717008;          // u8 world, u8 room, u8 door
constexpr std::uintptr_t kSoraSlot = 0x2A23598;    // u32 HP, u32 max HP
constexpr std::uintptr_t kSlotStride = 0x278;      // party slots sit below Sora

constexpr char kExeName[] = "KINGDOM HEARTS II FINAL MIX.exe";

HMODULE g_self = nullptr;
std::uintptr_t g_exeBase = 0;
FILE* g_log = nullptr;
std::uint64_t g_frame = 0;
DWORD g_padConnected = 0;          // bit i = XInput slot i connected
WORD g_padButtons[XUSER_MAX_COUNT] = {};
std::uint8_t g_lastStatus[3 + 6 * 4] = {};
bool g_inputDebug = false;         // KH2COOP_INPUT_DEBUG=1: log pads and raw input slots

// KH2's own input slots (leads from Volpestyle's input RE, being verified):
// *(exe+0x79CF00) = input struct; raw slot i at +0x18 + i*0x44 (u16 buttons,
// u8 right X/Y, u8 left X/Y, 0x80 = centred); +0x14 controller count;
// +0x129C index of the controller the collector swapped into slot 0.
constexpr std::uintptr_t kInputStructPtr = 0x79CF00;
constexpr int kRawSlots = 8;
std::uint8_t g_lastRaw[kRawSlots][6] = {};
std::int32_t g_lastActive = -2, g_lastCount = -2;

// Input collector, exe+0x105810 (lead: Volpestyle INPUT_RE_SESSION "main input
// collector"); takes the input struct. Prologue checked before hooking.
constexpr std::uintptr_t kInputCollector = 0x105810;
constexpr std::uint8_t kInputCollectorBytes[] = {0x4C, 0x8B, 0xDC, 0x56, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00};
using PFN_InputCollector = void(__fastcall*)(void*);
PFN_InputCollector g_realInputCollector = nullptr;

// Slice 2b state.
using PFN_XInputGetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using PFN_GetForegroundWindow = HWND(WINAPI*)();
using PFN_FindOrCreateUserInterface = void*(__cdecl*)(std::int32_t, const char*);
PFN_FindOrCreateUserInterface g_realFindOrCreateUserInterface = nullptr;
PFN_XInputGetState g_realXInputGetState = nullptr;
PFN_GetForegroundWindow g_realGetForegroundWindow = nullptr;
int g_assignedPad = -1;            // -1 = no filtering
HWND g_gameWindow = nullptr;
void** g_xinputIatSlot = nullptr;  // where the game's XInputGetState pointer lives
volatile LONG g_hookCalls = 0;     // XInputGetState calls the game made through our hook
WORD g_hookButtons = 0;            // last buttons the game saw through the filter

}  // namespace

void kh2coop::Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log, fmt, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

std::uintptr_t kh2coop::ExeBase() { return g_exeBase; }

namespace {
// Two settings files (user decision 2026-10-08):
//   kh2coop.ini         [kh2coop]  the connection and the co-op features that must be the same on
//                                  both PCs (compared when the games connect); friend updates replace it.
//   kh2coop_player.ini  [player]   each player's own rules (kPlayerKeys); the PCs may differ, updates
//                                  never touch it, and the DLL writes it with the defaults if missing.
// A key is read from one file only: a personal key in kh2coop.ini is ignored (logged).
wchar_t g_iniPath[MAX_PATH] = {};        // empty = no kh2coop.ini
wchar_t g_playerIniPath[MAX_PATH] = {};  // empty = no kh2coop_player.ini (defaults)

constexpr const char* kPlayerKeys[] = {"DOWN_SECONDS", "ACHIEVEMENTS"};

// Must match the defaults in the code (downed.cpp, InstallAchievementBlock).
constexpr char kPlayerIniTemplate[] =
    "; KH2 Coop: YOUR OWN settings. Each player has their own copy of this file next to\r\n"
    "; \"KINGDOM HEARTS II FINAL MIX.exe\", and the two PCs may use different values.\r\n"
    "; Mod updates never replace it. Delete it to get the defaults back (the mod writes a new one).\r\n"
    "; The connection and the co-op features that must match on both PCs are in kh2coop.ini.\r\n"
    "[player]\r\n"
    "; seconds you stay down after a lethal hit before getting up by yourself\r\n"
    "; (you also get up 6 s after the fight ends, whichever comes first)\r\n"
    "DOWN_SECONDS=30\r\n"
    "; Steam achievements while co-op is set up: 0 = blocked, 1 = allowed\r\n"
    "ACHIEVEMENTS=0\r\n";

bool IsPlayerKey(const char* key) {
    for (const char* k : kPlayerKeys)
        if (std::strcmp(k, key) == 0) return true;
    return false;
}

// `file` next to module `m` (the game exe or this DLL).
bool PathNextTo(HMODULE m, const wchar_t* file, wchar_t* out) {
    DWORD len = GetModuleFileNameW(m, out, MAX_PATH);
    wchar_t* slash = len && len < MAX_PATH ? wcsrchr(out, L'\\') : nullptr;
    return slash && wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - out), file) == 0;
}

// The game folder first (it survives OpenKH "Build"; the DLL folder is regenerated by it), then next to the DLL.
bool FindSettingsFile(const wchar_t* file, wchar_t* out) {
    HMODULE modules[] = {GetModuleHandleW(nullptr), g_self};
    wchar_t path[MAX_PATH];
    for (HMODULE m : modules) {
        if (PathNextTo(m, file, path) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(out, MAX_PATH, path);
            return true;
        }
    }
    return false;
}
}  // namespace

void kh2coop::LoadSettingsFile() {
    if (FindSettingsFile(L"kh2coop.ini", g_iniPath))
        Log("settings file: %ls (environment variables override it)", g_iniPath);
    else
        Log("settings file: none (kh2coop.ini in the game folder); using environment variables and defaults");
    if (FindSettingsFile(L"kh2coop_player.ini", g_playerIniPath)) {
        Log("personal settings: %ls", g_playerIniPath);
        return;
    }
    // First start: write the defaults into the game folder, where the player edits the file.
    wchar_t path[MAX_PATH];
    if (!PathNextTo(GetModuleHandleW(nullptr), L"kh2coop_player.ini", path)) return;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0, error = 0;
    bool ok = f != INVALID_HANDLE_VALUE &&
              WriteFile(f, kPlayerIniTemplate, sizeof(kPlayerIniTemplate) - 1, &written, nullptr) &&
              written == sizeof(kPlayerIniTemplate) - 1;
    if (!ok) error = GetLastError();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (ok) {
        wcscpy_s(g_playerIniPath, path);
        Log("personal settings: none yet; wrote %ls with the defaults", path);
    } else {
        Log("personal settings: none, and %ls could not be written (error %lu); using the defaults", path, error);
    }
}

bool kh2coop::EnvStr(const char* name, char* out, unsigned size) {
    if (!size) return false;
    out[0] = 0;
    DWORD n = GetEnvironmentVariableA(name, out, size);
    if (n > 0 && n < size) return true;
    out[0] = 0;
    const char* key = std::strncmp(name, "KH2COOP_", 8) == 0 ? name + 8 : name;
    wchar_t wkey[64], wout[256];
    MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, 64);
    bool personal = IsPlayerKey(key);
    if (personal && g_iniPath[0] && GetPrivateProfileStringW(L"kh2coop", wkey, L"", wout, 256, g_iniPath) > 0)
        Log("settings: %s in kh2coop.ini is ignored; it's a personal setting, set it in kh2coop_player.ini", key);
    const wchar_t* file = personal ? g_playerIniPath : g_iniPath;
    if (!file[0]) return false;
    DWORD got = GetPrivateProfileStringW(personal ? L"player" : L"kh2coop", wkey, L"", wout, 256, file);
    if (got == 0) return false;
    WideCharToMultiByte(CP_UTF8, 0, wout, -1, out, static_cast<int>(size), nullptr, nullptr);
    return out[0] != 0;
}

int kh2coop::EnvInt(const char* name, int fallback) {
    char buf[64];
    return EnvStr(name, buf, sizeof(buf)) ? std::atoi(buf) : fallback;
}

bool kh2coop::HookFunction(std::uintptr_t rva, const std::uint8_t* expected, std::size_t size, void* detour,
                           void** original, const char* name) {
    auto target = reinterpret_cast<void*>(g_exeBase + rva);
    if (std::memcmp(target, expected, size) != 0) {
        Log("hook %s: bytes at exe+0x%llX differ (different game build?); NOT hooking", name,
            static_cast<unsigned long long>(rva));
        return false;
    }
    static bool initialized = [] {
        MH_STATUS status = MH_Initialize();  // the save guard may have initialized it first
        return status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED;
    }();
    bool ok = initialized && MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
    Log("hook %s at exe+0x%llX: %s", name, static_cast<unsigned long long>(rva), ok ? "ok" : "FAILED");
    return ok;
}

namespace {

void OpenLogNextToDll() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    // One log per game process, so several game copies don't overwrite each other.
    swprintf_s(slash + 1, MAX_PATH - (slash + 1 - path), L"kh2coop_%lu.log", GetCurrentProcessId());
    // Share mode lets tools read the log while the game runs.
    g_log = _wfsopen(path, L"w", _SH_DENYNO);
}

template <typename T>
T ReadExe(std::uintptr_t offset) {
    return *reinterpret_cast<const volatile T*>(g_exeBase + offset);
}

DWORD WINAPI HookXInputGetState(DWORD index, XINPUT_STATE* state);
bool InstallInputOverride();
bool OpenVirtualPad(int port);

// Finds `dll`'s import in the exe (by name, or by ordinal when name is null)
// and points it at `hook`. Returns the previous pointer, or null if absent.
void* PatchImport(const char* dll, const char* name, WORD ordinal, void* hook) {
    auto base = reinterpret_cast<std::uint8_t*>(g_exeBase);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto iat = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++iat) {
            bool match = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
                ? (!name && IMAGE_ORDINAL64(names->u1.Ordinal) == ordinal)
                : (name && std::strcmp(reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name, name) == 0);
            if (!match) continue;
            DWORD old;
            VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            void* previous = reinterpret_cast<void*>(iat->u1.Function);
            iat->u1.Function = reinterpret_cast<ULONGLONG>(hook);
            if (hook == reinterpret_cast<void*>(HookXInputGetState)) g_xinputIatSlot = reinterpret_cast<void**>(&iat->u1.Function);
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            return previous;
        }
    }
    return nullptr;
}

}  // namespace

void* kh2coop::PatchExeImport(const char* dll, const char* name, void* hook) {
    return PatchImport(dll, name, 0, hook);
}

namespace {

// The game sees only the assigned physical pad, as slot 0.
DWORD WINAPI HookXInputGetState(DWORD index, XINPUT_STATE* state) {
    InterlockedIncrement(&g_hookCalls);
    if (index != 0) return ERROR_DEVICE_NOT_CONNECTED;
    return g_realXInputGetState(static_cast<DWORD>(g_assignedPad), state);
}

BOOL CALLBACK FindGameWindow(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    g_gameWindow = hwnd;
    return FALSE;
}

// The game believes its window is in front, so it keeps reading pad input.
HWND WINAPI HookGetForegroundWindow() {
    if (!g_gameWindow || !IsWindow(g_gameWindow)) {
        g_gameWindow = nullptr;
        EnumWindows(FindGameWindow, 0);
    }
    return g_gameWindow ? g_gameWindow : g_realGetForegroundWindow();
}

// ISteamInput006 vtable slots, read from the game's own steam_api64.dll flat
// wrappers (SteamAPI_ISteamInput_* = mov rax,[rcx]; jmp [rax+off]).
constexpr int kSteamInputGetConnectedControllers = 0x30 / 8;
constexpr int kSteamInputGetControllerForGamepadIndex = 0x130 / 8;
constexpr int kSteamInputVtableCopy = 64;  // > highest slot used by the 48 methods
void* g_steamInputVtable[kSteamInputVtableCopy];

int __fastcall NoConnectedControllers(void*, std::uint64_t*) { return 0; }
std::uint64_t __fastcall NoControllerForGamepadIndex(void*, int) { return 0; }

// Steam achievements (ACHIEVEMENTS, a personal setting; user decision 2026-10-08): blocked by
// default while co-op is set up in this game, ACHIEVEMENTS=1 allows them. The game asks for
// ISteamUserStats by this version string (the only one in the exe). Its vtable slots come from the
// game's own steam_api64.dll flat wrappers, like the Steam Input ones (MSVC puts each overload's
// float version first). Unlocks, progress pop-ups and stat changes are answered "done" without
// reaching Steam, so achievements tied to a stat can't unlock either; reads and StoreStats pass.
constexpr char kUserStatsVersion[] = "STEAMUSERSTATS_INTERFACE_VERSION012";
constexpr int kStatsSetStatFloat = 0x18 / 8;
constexpr int kStatsSetStatInt32 = 0x20 / 8;
constexpr int kStatsUpdateAvgRateStat = 0x28 / 8;
constexpr int kStatsSetAchievement = 0x38 / 8;
constexpr int kStatsIndicateAchievementProgress = 0x68 / 8;
constexpr int kStatsVtableCopy = 64;  // > highest slot of the 45 methods (0x160 / 8)
void* g_statsVtable[kStatsVtableCopy];
bool g_hideSteamInput = false;      // KH2COOP_XINPUT_ONLY=1
bool g_blockAchievements = false;   // see InstallAchievementBlock
bool g_statsSeen = false;           // the game asked for ISteamUserStats through our hook
volatile LONG g_blockedUnlocks = 0, g_blockedStats = 0;

bool __fastcall NoSetAchievement(void*, const char* name) {
    InterlockedIncrement(&g_blockedUnlocks);
    Log("achievements: blocked the unlock of \"%s\"", name ? name : "(null)");
    return true;
}
bool __fastcall NoIndicateAchievementProgress(void*, const char* name, std::uint32_t now, std::uint32_t max) {
    InterlockedIncrement(&g_blockedUnlocks);
    Log("achievements: blocked the progress pop-up of \"%s\" (%u/%u)", name ? name : "(null)", now, max);
    return true;
}
bool BlockedStat(const char* name) {
    if (InterlockedIncrement(&g_blockedStats) <= 20) Log("achievements: blocked a change of stat \"%s\"", name ? name : "(null)");
    return true;
}
bool __fastcall NoSetStatFloat(void*, const char* name, float) { return BlockedStat(name); }
bool __fastcall NoSetStatInt32(void*, const char* name, std::int32_t) { return BlockedStat(name); }
bool __fastcall NoUpdateAvgRateStat(void*, const char* name, float, double) { return BlockedStat(name); }

void BlockUserStats(void* iface, const char* version) {
    g_statsSeen = true;
    if (std::strcmp(version, kUserStatsVersion) != 0) {
        Log("%s: unknown version, achievements NOT blocked", version);
        return;
    }
    void** vtable = *static_cast<void***>(iface);
    if (vtable == g_statsVtable) return;
    std::memcpy(g_statsVtable, vtable, sizeof(g_statsVtable));
    g_statsVtable[kStatsSetStatFloat] = reinterpret_cast<void*>(NoSetStatFloat);
    g_statsVtable[kStatsSetStatInt32] = reinterpret_cast<void*>(NoSetStatInt32);
    g_statsVtable[kStatsUpdateAvgRateStat] = reinterpret_cast<void*>(NoUpdateAvgRateStat);
    g_statsVtable[kStatsSetAchievement] = reinterpret_cast<void*>(NoSetAchievement);
    g_statsVtable[kStatsIndicateAchievementProgress] = reinterpret_cast<void*>(NoIndicateAchievementProgress);
    *static_cast<void***>(iface) = g_statsVtable;
    Log("%s: achievements blocked (ACHIEVEMENTS=1 in kh2coop_player.ini allows them)", version);
}

// steam_api hands out interfaces by version string. For Steam Input, keep the
// real object but give it a copied vtable whose controller queries say "none";
// for the user stats, one whose unlocks do nothing (BlockUserStats).
void* __cdecl HookFindOrCreateUserInterface(std::int32_t user, const char* version) {
    void* iface = g_realFindOrCreateUserInterface(user, version);
    if (iface && version && g_blockAchievements && std::strncmp(version, "STEAMUSERSTATS_INTERFACE_VERSION", 32) == 0)
        BlockUserStats(iface, version);
    if (iface && version && g_hideSteamInput && std::strncmp(version, "SteamInput", 10) == 0) {
        void** vtable = *static_cast<void***>(iface);
        if (vtable != g_steamInputVtable) {
            std::memcpy(g_steamInputVtable, vtable, sizeof(g_steamInputVtable));
            g_steamInputVtable[kSteamInputGetConnectedControllers] = reinterpret_cast<void*>(NoConnectedControllers);
            g_steamInputVtable[kSteamInputGetControllerForGamepadIndex] = reinterpret_cast<void*>(NoControllerForGamepadIndex);
            *static_cast<void***>(iface) = g_steamInputVtable;
            Log("%s: controller queries now report none", version);
        }
    }
    return iface;
}

// DirectInput: IDirectInput8 vtable (dinput.h): 3 IUnknown, CreateDevice 3,
// EnumDevices 4, ... ConfigureDevices 10.
using PFN_DirectInput8Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using PFN_EnumDevices = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
constexpr int kDiEnumDevices = 4;
constexpr int kDiVtableSize = 11;
PFN_DirectInput8Create g_realDirectInput8Create = nullptr;
PFN_EnumDevices g_realEnumDevices = nullptr;
void* g_diVtable[kDiVtableSize];

struct EnumFilter {
    LPDIENUMDEVICESCALLBACKW callback;
    LPVOID context;
};

// Same callback shape for the A and W interfaces; tell them apart by dwSize.
BOOL CALLBACK FilterDevice(LPCDIDEVICEINSTANCEW device, LPVOID ref) {
    auto filter = static_cast<EnumFilter*>(ref);
    bool wide = device->dwSize == sizeof(DIDEVICEINSTANCEW);
    char name[MAX_PATH];
    if (wide) {
        WideCharToMultiByte(CP_UTF8, 0, device->tszProductName, -1, name, sizeof(name), nullptr, nullptr);
    } else {
        strcpy_s(name, reinterpret_cast<LPCDIDEVICEINSTANCEA>(device)->tszProductName);
    }
    BYTE type = GET_DIDEVICE_TYPE(device->dwDevType);
    bool pad = type != DI8DEVTYPE_KEYBOARD && type != DI8DEVTYPE_MOUSE;
    Log("DirectInput device \"%s\" type 0x%02X: %s", name, type, pad ? "hidden" : "kept");
    return pad ? DIENUM_CONTINUE : filter->callback(device, filter->context);
}

HRESULT STDMETHODCALLTYPE HookEnumDevices(void* self, DWORD devType, LPDIENUMDEVICESCALLBACKW callback,
                                          LPVOID context, DWORD flags) {
    EnumFilter filter {callback, context};
    return g_realEnumDevices(self, devType, FilterDevice, &filter, flags);
}

HRESULT WINAPI HookDirectInput8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    HRESULT hr = g_realDirectInput8Create(inst, version, iid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        void** vtable = *static_cast<void***>(*out);
        if (vtable != g_diVtable) {
            std::memcpy(g_diVtable, vtable, sizeof(g_diVtable));
            g_realEnumDevices = reinterpret_cast<PFN_EnumDevices>(vtable[kDiEnumDevices]);
            g_diVtable[kDiEnumDevices] = reinterpret_cast<void*>(HookEnumDevices);
        }
        *static_cast<void***>(*out) = g_diVtable;
        Log("DirectInput8Create: game controllers will be hidden");
    }
    return hr;
}

// One import patch serves Steam Input hiding and the achievement block (patching it twice would
// hand back our own hook as the "real" function).
bool HookSteamInterfaces() {
    if (!g_realFindOrCreateUserInterface)
        g_realFindOrCreateUserInterface = reinterpret_cast<PFN_FindOrCreateUserInterface>(
            PatchImport("steam_api64.dll", "SteamInternal_FindOrCreateUserInterface", 0,
                        reinterpret_cast<void*>(HookFindOrCreateUserInterface)));
    return g_realFindOrCreateUserInterface != nullptr;
}

// Co-op is set up = this game has a link port and a partner address (as AvatarLinkInit needs).
// Without them (kh2coop.ini renamed to play normal KH2) achievements stay as normal.
void InstallAchievementBlock() {
    char peer[64];
    if (EnvInt("KH2COOP_LISTEN", 0) <= 0 || !EnvStr("KH2COOP_PEER", peer, sizeof(peer))) {
        Log("achievements: as normal (co-op isn't set up in this game)");
        return;
    }
    if (EnvInt("KH2COOP_ACHIEVEMENTS", 0) == 1) {
        Log("achievements: allowed (ACHIEVEMENTS=1)");
        return;
    }
    g_blockAchievements = HookSteamInterfaces();
    Log("achievements: %s", g_blockAchievements ? "will be blocked (ACHIEVEMENTS=0)" : "NOT blocked: Steam import not found");
}

void AchievementsFrame() {
    if (!g_blockAchievements || g_frame != 3600) return;  // ~1 min in
    if (!g_statsSeen) Log("achievements: WARNING the game hasn't asked Steam for its achievements through our hook yet");
}

void InstallInputHooks() {
    int vpadPort = EnvInt("KH2COOP_VPAD", 0);
    if (vpadPort > 0) {
        bool ok = OpenVirtualPad(vpadPort);
        Log("virtual pad on 127.0.0.1:%d: %s", vpadPort, ok ? "listening" : "FAILED");
        if (ok) InstallInputOverride();
        return;  // real pads ignored in this copy
    }
    int pad = EnvInt("KH2COOP_PAD", -1);
    if (pad >= 0 && pad < static_cast<int>(XUSER_MAX_COUNT)) {
        g_realXInputGetState = reinterpret_cast<PFN_XInputGetState>(
            PatchImport("XINPUT1_4.dll", nullptr, 2, reinterpret_cast<void*>(HookXInputGetState)));
        if (g_realXInputGetState) g_assignedPad = pad;
        Log("pad filter: XInput slot %d -> game slot 0 (%s)", pad, g_realXInputGetState ? "hooked" : "import NOT FOUND");
        if (g_assignedPad >= 0) InstallInputOverride();
    }
    if (EnvInt("KH2COOP_XINPUT_ONLY", 0) == 1) {
        g_hideSteamInput = HookSteamInterfaces();
        Log("hide Steam Input: %s", g_hideSteamInput ? "hooked" : "import NOT FOUND");
        g_realDirectInput8Create = reinterpret_cast<PFN_DirectInput8Create>(
            PatchImport("DINPUT8.dll", "DirectInput8Create", 0, reinterpret_cast<void*>(HookDirectInput8Create)));
        Log("hide DirectInput pads: %s", g_realDirectInput8Create ? "hooked" : "import NOT FOUND");
    }
    if (EnvInt("KH2COOP_BACKGROUND", 0) == 1) {
        g_realGetForegroundWindow = reinterpret_cast<PFN_GetForegroundWindow>(
            PatchImport("USER32.dll", "GetForegroundWindow", 0, reinterpret_cast<void*>(HookGetForegroundWindow)));
        Log("background input: %s", g_realGetForegroundWindow ? "hooked" : "import NOT FOUND");
    }
}

// XInput -> KH2 raw buttons (PS2 DualShock bit order). Verified from paired logs
// 2026-10-04: A, B, X, D-pad down, Start, Back, LB, R3. The rest follow the same
// PS2 order and are pending a live check.
std::uint16_t ToKh2Buttons(const XINPUT_GAMEPAD& g) {
    struct { WORD xinput; std::uint16_t kh2; } map[] = {
        {XINPUT_GAMEPAD_BACK, 0x0001},           {XINPUT_GAMEPAD_LEFT_THUMB, 0x0002},
        {XINPUT_GAMEPAD_RIGHT_THUMB, 0x0004},    {XINPUT_GAMEPAD_START, 0x0008},
        {XINPUT_GAMEPAD_DPAD_UP, 0x0010},        {XINPUT_GAMEPAD_DPAD_RIGHT, 0x0020},
        {XINPUT_GAMEPAD_DPAD_DOWN, 0x0040},      {XINPUT_GAMEPAD_DPAD_LEFT, 0x0080},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, 0x0400},  {XINPUT_GAMEPAD_RIGHT_SHOULDER, 0x0800},
        {XINPUT_GAMEPAD_Y, 0x1000},              {XINPUT_GAMEPAD_B, 0x2000},
        {XINPUT_GAMEPAD_A, 0x4000},              {XINPUT_GAMEPAD_X, 0x8000},
    };
    std::uint16_t out = 0;
    for (const auto& m : map)
        if (g.wButtons & m.xinput) out |= m.kh2;
    if (g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) out |= 0x0100;   // L2
    if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) out |= 0x0200;  // R2
    return out;
}

// XInput axis (-32768..32767, +Y up) -> PS2 byte (0..255, 0x80 centre, 0 = up/left).
std::uint8_t ToKh2Axis(SHORT v, SHORT deadzone, bool invert) {
    if (v > -deadzone && v < deadzone) return 0x80;
    int b = (static_cast<int>(v) + 32768) >> 8;
    return static_cast<std::uint8_t>(invert ? 255 - b : b);
}

// Virtual pad (KH2COOP_VPAD=<udp port>): a test script on this PC sends
// "KH2V" + u16 KH2 buttons + u8 right X/Y, left X/Y (PS2 layout, 0x80 centre)
// to 127.0.0.1:<port>; raw slot 0 then holds that state instead of a real pad.
// Neutral if nothing arrived for 30 frames, so a dead script can't hold a button.
SOCKET g_vpadSocket = INVALID_SOCKET;
std::uint8_t g_vpadState[6] = {0, 0, 0x80, 0x80, 0x80, 0x80};
std::uint32_t g_vpadAge = 1000;  // collector calls since the last packet

void PollVirtualPad() {
    char buf[64];
    int n;
    ++g_vpadAge;
    while ((n = recv(g_vpadSocket, buf, sizeof(buf), 0)) > 0) {
        if (n < 10 || std::memcmp(buf, "KH2V", 4) != 0) continue;
        std::memcpy(g_vpadState, buf + 4, 6);
        g_vpadAge = 0;
    }
}

bool OpenVirtualPad(int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_vpadSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_vpadSocket == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(g_vpadSocket, FIONBIO, &nonBlocking);
    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(port));
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_vpadSocket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        closesocket(g_vpadSocket);
        g_vpadSocket = INVALID_SOCKET;
        return false;
    }
    return true;
}

// Button injection (pause sync): bits OR-ed into raw slot 0 for a few frames.
// With no controller filter or virtual pad the hook is "pass-through": the
// game's own input is left alone and only injected bits are added.
bool g_passThrough = false;
std::uint16_t g_injectBits = 0;
int g_injectFrames = 0;

std::uint16_t g_blockBits = 0;  // downed: buttons cleared (left stick centred) in raw slot 0

void ApplyInjection(void* input) {
    if (g_blockBits) {
        // Raw slot: u16 buttons, u8 right X/Y (camera), u8 left X/Y (movement). The camera stays
        // free so a downed player can look around (user request 2026-10-06).
        auto* slot = static_cast<std::uint8_t*>(input) + 0x18;
        *reinterpret_cast<std::uint16_t*>(slot) &= static_cast<std::uint16_t>(~g_blockBits);
        std::memset(slot + 4, 0x80, 2);
    }
    if (g_injectFrames <= 0) return;
    --g_injectFrames;
    auto* buttons = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(input) + 0x18);
    *buttons |= g_injectBits;
}

void __fastcall HookInputCollector(void* input) {
    g_realInputCollector(input);
    if (g_passThrough) {
        ApplyInjection(input);
        return;
    }
    XINPUT_STATE state {};
    bool vpad = g_vpadSocket != INVALID_SOCKET;
    if (vpad) PollVirtualPad();
    bool connected = !vpad && XInputGetState(static_cast<DWORD>(g_assignedPad), &state) == ERROR_SUCCESS;
    auto base = static_cast<std::uint8_t*>(input) + 0x18;
    for (int i = 0; i < kRawSlots; ++i) {
        std::uint8_t* slot = base + i * 0x44;
        std::uint16_t buttons = 0;
        std::uint8_t sticks[4] = {0x80, 0x80, 0x80, 0x80};
        if (i == 0 && vpad && g_vpadAge < 30) {
            std::memcpy(&buttons, g_vpadState, 2);
            std::memcpy(sticks, g_vpadState + 2, 4);
        } else if (i == 0 && connected) {
            const auto& g = state.Gamepad;
            buttons = ToKh2Buttons(g);
            sticks[0] = ToKh2Axis(g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, false);
            sticks[1] = ToKh2Axis(g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE, true);
            sticks[2] = ToKh2Axis(g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, false);
            sticks[3] = ToKh2Axis(g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, true);
        }
        std::memcpy(slot, &buttons, 2);
        std::memcpy(slot + 2, sticks, 4);
    }
    ApplyInjection(input);
}

bool g_collectorHooked = false;

bool InstallInputOverride() {
    if (g_collectorHooked) return true;
    g_collectorHooked = HookFunction(kInputCollector, kInputCollectorBytes, sizeof(kInputCollectorBytes),
                                     reinterpret_cast<void*>(HookInputCollector),
                                     reinterpret_cast<void**>(&g_realInputCollector), "InputCollector");
    return g_collectorHooked;
}

void LogGameInputSlots() {
    auto input = ReadExe<std::uintptr_t>(kInputStructPtr);
    if (!input) return;
    auto count = *reinterpret_cast<const volatile std::int32_t*>(input + 0x14);
    auto active = *reinterpret_cast<const volatile std::int32_t*>(input + 0x129C);
    if (count != g_lastCount || active != g_lastActive) {
        Log("game input: controller count %d, active raw index %d", count, active);
        g_lastCount = count;
        g_lastActive = active;
    }
    for (int i = 0; i < kRawSlots; ++i) {
        std::uint8_t raw[6];
        std::memcpy(raw, reinterpret_cast<const void*>(input + 0x18 + i * 0x44), sizeof(raw));
        if (std::memcmp(raw, g_lastRaw[i], sizeof(raw)) == 0) continue;
        std::memcpy(g_lastRaw[i], raw, sizeof(raw));
        Log("game raw slot %d: buttons 0x%04X right %02X,%02X left %02X,%02X", i,
            raw[0] | (raw[1] << 8), raw[2], raw[3], raw[4], raw[5]);
    }
}

void LogPads() {
    DWORD connected = 0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state {};
        if (XInputGetState(i, &state) != ERROR_SUCCESS) {
            g_padButtons[i] = 0;
            continue;
        }
        connected |= 1u << i;
        if (state.Gamepad.wButtons != g_padButtons[i]) {
            Log("pad %lu buttons 0x%04X", i, state.Gamepad.wButtons);
            g_padButtons[i] = state.Gamepad.wButtons;
        }
    }
    if (connected != g_padConnected) {
        Log("pads connected: %s%s%s%s(mask 0x%lX)", connected & 1 ? "0 " : "", connected & 2 ? "1 " : "",
            connected & 4 ? "2 " : "", connected & 8 ? "3 " : "", connected);
        g_padConnected = connected;
    }
}

}  // namespace

bool kh2coop::InputInjectEnable() {
    if (!g_collectorHooked) {
        g_passThrough = true;  // no filter or virtual pad here: leave the game's own input alone
        InstallInputOverride();
    }
    return g_collectorHooked;
}

void kh2coop::InputBlock(std::uint16_t bits) { g_blockBits = bits; }

void kh2coop::InputInject(std::uint16_t bits, int frames) {
    g_injectBits = bits;
    g_injectFrames = frames;
}

extern "C" __declspec(dllexport) void OnInit(const wchar_t* modFolder) {
    OpenLogNextToDll();
    HMODULE exe = GetModuleHandleA(kExeName);
    g_exeBase = reinterpret_cast<std::uintptr_t>(exe);
    Log("kh2coop slice 3d loaded (pid %lu). mod folder: %ls", GetCurrentProcessId(), modFolder ? modFolder : L"(null)");
    if (!exe) {
        // Panacea also serves KH1/BBS/ReCoM; stay inert anywhere but KH2.
        Log("not running inside %s; staying inactive", kExeName);
        return;
    }
    Log("exe base 0x%llX", static_cast<unsigned long long>(g_exeBase));
    LoadSettingsFile();
    InstallCrashLogger();
    g_inputDebug = EnvInt("KH2COOP_INPUT_DEBUG", 0) == 1;
    InstallInputHooks();
    InstallAchievementBlock();
    if (EnvInt("KH2COOP_SAVE_GUARD", 0) == 1) InstallSaveGuard();
    WatchInit();
    AggroInit();
    PuppetInit();
    AvatarLinkInit();
}

extern "C" __declspec(dllexport) void OnFrame() {
    if (!g_exeBase) return;
    AvatarLinkFrame();
    PuppetFrame();
    WatchFrame();
    AggroFrame();
    FollowFrame();
    LoadBarrierFrame();
    WorldSyncFrame();
    SpawnSyncFrame();
    DownedFrame();
    PauseSyncFrame();
    GameOverSyncFrame();
    AchievementsFrame();
    if (g_inputDebug) {
        LogPads();
        LogGameInputSlots();
    }
    if (g_inputDebug && g_assignedPad >= 0) {
        XINPUT_STATE filtered {};
        WORD buttons = g_realXInputGetState(static_cast<DWORD>(g_assignedPad), &filtered) == ERROR_SUCCESS
            ? filtered.Gamepad.wButtons : 0;
        if (buttons != g_hookButtons) {
            Log("game slot 0 (= XInput slot %d) buttons 0x%04X", g_assignedPad, buttons);
            g_hookButtons = buttons;
        }
        if (g_frame % 300 == 0) {
            bool intact = g_xinputIatSlot && *g_xinputIatSlot == reinterpret_cast<void*>(HookXInputGetState);
            Log("filter check: game called XInputGetState %ld times so far; import swap %s",
                g_hookCalls, intact ? "still ours" : "OVERWRITTEN");
        }
    }
    if (g_frame++ % 30 != 0) return;  // check twice a second at 60 fps

    std::uint8_t world = ReadExe<std::uint8_t>(kNow);
    std::uint8_t room = ReadExe<std::uint8_t>(kNow + 1);
    std::uint8_t door = ReadExe<std::uint8_t>(kNow + 2);
    std::uint32_t hp[3], maxHp[3];
    for (int k = 0; k < 3; ++k) {
        hp[k] = ReadExe<std::uint32_t>(kSoraSlot - k * kSlotStride);
        maxHp[k] = ReadExe<std::uint32_t>(kSoraSlot - k * kSlotStride + 4);
    }
    std::uint8_t status[sizeof(g_lastStatus)];
    status[0] = world; status[1] = room; status[2] = door;
    std::memcpy(status + 3, hp, sizeof(hp));
    std::memcpy(status + 3 + sizeof(hp), maxHp, sizeof(maxHp));
    if (std::memcmp(status, g_lastStatus, sizeof(status)) == 0) return;
    std::memcpy(g_lastStatus, status, sizeof(status));
    Log("frame %llu world 0x%02X room 0x%02X door 0x%02X | sora %u/%u donald %u/%u goofy %u/%u",
        static_cast<unsigned long long>(g_frame - 1), world, room, door,
        hp[0], maxHp[0], hp[1], maxHp[1], hp[2], maxHp[2]);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        std::fclose(g_log);
        g_log = nullptr;
    }
    return TRUE;
}
