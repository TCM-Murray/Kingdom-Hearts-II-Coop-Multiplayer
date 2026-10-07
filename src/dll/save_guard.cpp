// Save guard: while co-op runs, the party table in memory is modified, so an
// in-game save would write that into the player's real save (synced to Steam
// Cloud). This redirects every write, copy, move or delete aimed at the save
// folder into a sandbox folder next to our DLL. The game believes it saved;
// the real files never change. Reads go to the sandbox copy if this session
// wrote one (so the game sees its own saves), else to the real file.
//
// The hooks are inline detours on KernelBase (and kernel32 for the two ANSI
// functions it alone exports), which every caller reaches: the exe, the CRT
// DLLs and kernel32 itself. Hooking only the exe's own imports missed the
// game's saves (2026-10-04: the real save was overwritten). Idea from
// Volpestyle's SaveGuard. Every save-folder access is logged ("seen") so
// coverage can be proven from the game's startup reads before any save test.
// Off unless KH2COOP_SAVE_GUARD=1 at game start.

#include <windows.h>

#include <MinHook.h>

#include <cstring>
#include <cwchar>
#include <cwctype>

#include "common.hpp"

namespace kh2coop {
namespace {

constexpr wchar_t kSaveFolder[] = L"\\kingdom hearts hd 1.5+2.5 remix\\steam\\";

using PFN_CreateFileW = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using PFN_CreateFileA = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using PFN_CreateFile2 = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
using PFN_DeleteFileW = BOOL(WINAPI*)(LPCWSTR);
using PFN_DeleteFileA = BOOL(WINAPI*)(LPCSTR);
using PFN_MoveFileExW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
using PFN_MoveFileExA = BOOL(WINAPI*)(LPCSTR, LPCSTR, DWORD);
using PFN_MoveFileWithProgressW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD);
using PFN_ReplaceFileW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
using PFN_CopyFileW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, BOOL);
using PFN_CopyFileA = BOOL(WINAPI*)(LPCSTR, LPCSTR, BOOL);
using PFN_CopyFileExW = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);
using PFN_CopyFile2 = HRESULT(WINAPI*)(PCWSTR, PCWSTR, COPYFILE2_EXTENDED_PARAMETERS*);

PFN_CreateFileW o_CreateFileW;
PFN_CreateFileA o_CreateFileA;
PFN_CreateFile2 o_CreateFile2;
PFN_DeleteFileW o_DeleteFileW;
PFN_DeleteFileA o_DeleteFileA;
PFN_MoveFileExW o_MoveFileExW;
PFN_MoveFileExA o_MoveFileExA;
PFN_MoveFileWithProgressW o_MoveFileWithProgressW;
PFN_ReplaceFileW o_ReplaceFileW;
PFN_CopyFileW o_CopyFileW;
PFN_CopyFileA o_CopyFileA;
PFN_CopyFileExW o_CopyFileExW;
PFN_CopyFile2 o_CopyFile2;

wchar_t g_sandbox[MAX_PATH] = {};
volatile LONG g_logged = 0;
constexpr LONG kMaxLogged = 400;

bool InSaveFolder(const wchar_t* path) {
    if (!path) return false;
    wchar_t lower[MAX_PATH * 2];
    std::size_t i = 0;
    for (; path[i] && i < MAX_PATH * 2 - 1; ++i)
        lower[i] = path[i] == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(path[i]));
    lower[i] = 0;
    return std::wcsstr(lower, kSaveFolder) != nullptr;
}

void SandboxPath(const wchar_t* path, wchar_t* out) {
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    swprintf_s(out, MAX_PATH, L"%s%s", g_sandbox, name);
}

bool Exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

void Widen(const char* in, wchar_t* out) {
    if (!in || !MultiByteToWideChar(CP_ACP, 0, in, -1, out, MAX_PATH)) out[0] = 0;
}

void Note(const char* what, const wchar_t* path, const wchar_t* to) {
    if (InterlockedIncrement(&g_logged) > kMaxLogged) return;
    if (to)
        Log("save guard: %s %ls -> %ls", what, path, to);
    else
        Log("save guard: %s %ls (real file, read only)", what, path);
}

// Path to use for writing `path` (sandbox if it's in the save folder).
// The game saves a slot by opening the existing file and overwriting part of
// it, so the first write seeds the sandbox with a copy of the real file
// (2026-10-04: without this the open failed and the game reported a save
// error). Only opens seed; copy, move and replace targets must not pre-exist.
const wchar_t* ForWrite(const wchar_t* path, wchar_t* buf, const char* what, bool seed = false) {
    if (!InSaveFolder(path)) return path;
    SandboxPath(path, buf);
    Note(what, path, buf);
    if (seed && !Exists(buf) && Exists(path)) {
        if (o_CopyFileW(path, buf, TRUE))
            Log("save guard: seeded sandbox with a copy of the real file");
        else
            Log("save guard: could not seed sandbox (error %lu)", GetLastError());
    }
    return buf;
}

// Logs the outcome of an open that the guard redirected.
HANDLE Opened(HANDLE h, const wchar_t* original, const wchar_t* used, DWORD access, DWORD disposition) {
    if (used != original && InterlockedIncrement(&g_logged) <= kMaxLogged) {
        DWORD err = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        Log("save guard:   open access 0x%lX disposition %lu -> %s (error %lu)", access, disposition,
            h == INVALID_HANDLE_VALUE ? "FAILED" : "ok", err);
        if (err) SetLastError(err);
    }
    return h;
}

// KH2COOP_DISPLAY_MODE=<n>: the game's settings file config1525.dat ("SET-", 508 bytes) keeps the display
// mode as u16 at +0x18: 1 in the user's real file (full screen), 2 in the sandbox copies the game wrote after
// Alt+Enter (windowed; +0x1A/+0x1C = window size). The guard keeps every settings change in the sandbox,
// so each start read full screen again (user 2026-10-06: one of the two test copies stayed full screen).
// With the setting, the first read seeds the sandbox with a copy in that mode; the real file is never written.
int g_displayMode = 0;

bool IsSettingsFile(const wchar_t* path) {
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/') name = p + 1;
    return _wcsicmp(name, L"config1525.dat") == 0;
}

void SeedSettingsWithDisplayMode(const wchar_t* real, const wchar_t* sandbox) {
    HANDLE in = o_CreateFileW(real, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (in == INVALID_HANDLE_VALUE) return;
    unsigned char data[1024];
    DWORD size = 0;
    BOOL read = ReadFile(in, data, sizeof(data), &size, nullptr);
    CloseHandle(in);
    if (!read || size < 0x1E || std::memcmp(data, "SET-", 4) != 0) {
        Log("save guard: settings file not as expected (%lu bytes); display mode left alone", size);
        return;
    }
    unsigned old = data[0x18] | data[0x19] << 8;
    data[0x18] = static_cast<unsigned char>(g_displayMode);
    data[0x19] = 0;
    HANDLE out = o_CreateFileW(sandbox, GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, data, size, &written, nullptr);
    CloseHandle(out);
    Log("save guard: settings read from a sandbox copy with display mode %d (real file: %u)", g_displayMode, old);
}

// Path to use for reading `path` (sandbox copy if this session wrote one).
const wchar_t* ForRead(const wchar_t* path, wchar_t* buf, const char* what) {
    if (!InSaveFolder(path)) return path;
    SandboxPath(path, buf);
    if (g_displayMode > 0 && !Exists(buf) && IsSettingsFile(path)) SeedSettingsWithDisplayMode(path, buf);
    if (Exists(buf)) {
        Note(what, path, buf);
        return buf;
    }
    Note(what, path, nullptr);
    return path;
}

bool Writes(DWORD access, DWORD disposition) {
    return (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) != 0 ||
           disposition == CREATE_ALWAYS || disposition == CREATE_NEW || disposition == TRUNCATE_EXISTING;
}

HANDLE WINAPI H_CreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    wchar_t buf[MAX_PATH];
    const wchar_t* use = Writes(a, d) ? ForWrite(p, buf, "write", true) : ForRead(p, buf, "read");
    return Opened(o_CreateFileW(use, a, s, sa, d, f, t), p, use, a, d);
}

HANDLE WINAPI H_CreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t) {
    wchar_t wide[MAX_PATH], buf[MAX_PATH];
    Widen(p, wide);
    if (!InSaveFolder(wide)) return o_CreateFileA(p, a, s, sa, d, f, t);
    const wchar_t* use = Writes(a, d) ? ForWrite(wide, buf, "write", true) : ForRead(wide, buf, "read");
    return Opened(o_CreateFileW(use, a, s, sa, d, f, t), wide, use, a, d);
}

HANDLE WINAPI H_CreateFile2(LPCWSTR p, DWORD a, DWORD s, DWORD d, LPCREATEFILE2_EXTENDED_PARAMETERS x) {
    wchar_t buf[MAX_PATH];
    const wchar_t* use = Writes(a, d) ? ForWrite(p, buf, "write", true) : ForRead(p, buf, "read");
    return Opened(o_CreateFile2(use, a, s, d, x), p, use, a, d);
}

BOOL WINAPI H_DeleteFileW(LPCWSTR p) {
    if (!InSaveFolder(p)) return o_DeleteFileW(p);
    wchar_t buf[MAX_PATH];
    ForWrite(p, buf, "delete");
    if (Exists(buf)) o_DeleteFileW(buf);
    return TRUE;  // the real file stays
}

BOOL WINAPI H_DeleteFileA(LPCSTR p) {
    wchar_t wide[MAX_PATH];
    Widen(p, wide);
    if (!InSaveFolder(wide)) return o_DeleteFileA(p);
    return H_DeleteFileW(wide);
}

BOOL WINAPI H_MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (!InSaveFolder(from) && !InSaveFolder(to)) return o_MoveFileExW(from, to, flags);
    wchar_t b1[MAX_PATH], b2[MAX_PATH];
    const wchar_t* src = ForRead(from, b1, "move from");
    const wchar_t* dst = ForWrite(to, b2, "move to");
    // Never move a real save file away: copy it instead.
    if (src == from && InSaveFolder(from)) return o_CopyFileW(src, dst, FALSE);
    return o_MoveFileExW(src, dst, flags);
}

BOOL WINAPI H_MoveFileExA(LPCSTR from, LPCSTR to, DWORD flags) {
    wchar_t a[MAX_PATH], b[MAX_PATH];
    Widen(from, a);
    Widen(to, b);
    if (!InSaveFolder(a) && !InSaveFolder(b)) return o_MoveFileExA(from, to, flags);
    return H_MoveFileExW(a, b, flags);
}

BOOL WINAPI H_MoveFileWithProgressW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE r, LPVOID d, DWORD flags) {
    if (!InSaveFolder(from) && !InSaveFolder(to)) return o_MoveFileWithProgressW(from, to, r, d, flags);
    return H_MoveFileExW(from, to, flags);
}

BOOL WINAPI H_ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID e1,
                           LPVOID e2) {
    if (!InSaveFolder(replaced) && !InSaveFolder(replacement) && !InSaveFolder(backup))
        return o_ReplaceFileW(replaced, replacement, backup, flags, e1, e2);
    wchar_t b1[MAX_PATH], b2[MAX_PATH], b3[MAX_PATH];
    const wchar_t* target = ForWrite(replaced, b1, "replace");
    const wchar_t* source = ForRead(replacement, b2, "replace with");
    const wchar_t* keep = backup ? ForWrite(backup, b3, "replace backup") : nullptr;
    if (!Exists(target)) return o_CopyFileW(source, target, FALSE);  // nothing to replace in the sandbox yet
    return o_ReplaceFileW(target, source, keep, flags, e1, e2);
}

BOOL WINAPI H_CopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists) {
    wchar_t b1[MAX_PATH], b2[MAX_PATH];
    return o_CopyFileW(ForRead(from, b1, "copy from"), ForWrite(to, b2, "copy to"), failIfExists);
}

BOOL WINAPI H_CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists) {
    wchar_t a[MAX_PATH], b[MAX_PATH];
    Widen(from, a);
    Widen(to, b);
    if (!InSaveFolder(a) && !InSaveFolder(b)) return o_CopyFileA(from, to, failIfExists);
    return H_CopyFileW(a, b, failIfExists);
}

BOOL WINAPI H_CopyFileExW(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE r, LPVOID d, LPBOOL cancel, DWORD flags) {
    wchar_t b1[MAX_PATH], b2[MAX_PATH];
    return o_CopyFileExW(ForRead(from, b1, "copy from"), ForWrite(to, b2, "copy to"), r, d, cancel, flags);
}

HRESULT WINAPI H_CopyFile2(PCWSTR from, PCWSTR to, COPYFILE2_EXTENDED_PARAMETERS* x) {
    wchar_t b1[MAX_PATH], b2[MAX_PATH];
    return o_CopyFile2(ForRead(from, b1, "copy from"), ForWrite(to, b2, "copy to"), x);
}

template <typename T>
bool Hook(const wchar_t* dll, const char* name, T detour, T* original) {
    void* target = nullptr;
    if (MH_CreateHookApiEx(dll, name, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), &target) !=
            MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("save guard: could not hook %ls!%s", dll, name);
        return false;
    }
    return true;
}

}  // namespace

void InstallSaveGuard() {
    // Sandbox = <folder of this DLL>\save_sandbox_<pid>\: a fresh one per game start.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&InstallSaveGuard), &self);
    GetModuleFileNameW(self, g_sandbox, MAX_PATH);
    wchar_t* slash = std::wcsrchr(g_sandbox, L'\\');
    if (!slash) return;
    swprintf_s(slash + 1, MAX_PATH - (slash + 1 - g_sandbox), L"save_sandbox_%lu\\", GetCurrentProcessId());
    CreateDirectoryW(g_sandbox, nullptr);
    g_displayMode = EnvInt("KH2COOP_DISPLAY_MODE", 0);

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log("save guard: MinHook init failed (%d); NOT active", init);
        return;
    }
    const wchar_t* kb = L"kernelbase";
    bool ok = Hook(kb, "CreateFileW", &H_CreateFileW, &o_CreateFileW) &
              Hook(kb, "CreateFileA", &H_CreateFileA, &o_CreateFileA) &
              Hook(kb, "CreateFile2", &H_CreateFile2, &o_CreateFile2) &
              Hook(kb, "DeleteFileW", &H_DeleteFileW, &o_DeleteFileW) &
              Hook(kb, "DeleteFileA", &H_DeleteFileA, &o_DeleteFileA) &
              Hook(kb, "MoveFileExW", &H_MoveFileExW, &o_MoveFileExW) &
              Hook(kb, "MoveFileWithProgressW", &H_MoveFileWithProgressW, &o_MoveFileWithProgressW) &
              Hook(kb, "ReplaceFileW", &H_ReplaceFileW, &o_ReplaceFileW) &
              Hook(kb, "CopyFileW", &H_CopyFileW, &o_CopyFileW) &
              Hook(kb, "CopyFileExW", &H_CopyFileExW, &o_CopyFileExW) &
              Hook(kb, "CopyFile2", &H_CopyFile2, &o_CopyFile2) &
              Hook(L"kernel32", "CopyFileA", &H_CopyFileA, &o_CopyFileA) &
              Hook(L"kernel32", "MoveFileExA", &H_MoveFileExA, &o_MoveFileExA);
    Log("save guard: %s; game saves go to %ls", ok ? "on (13 file functions hooked)" : "INCOMPLETE, see above",
        g_sandbox);
}

}  // namespace kh2coop
