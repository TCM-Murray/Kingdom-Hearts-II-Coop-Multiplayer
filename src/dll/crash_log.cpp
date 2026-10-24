// Crash logger: when the game hits a fatal exception, write where it happened
// to our log so a crash becomes a clue. A vectored handler sees exceptions
// first (before the game's own handlers), so it also logs ones that something
// later recovers from; those are marked "first-chance". Capped per run.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "world_census.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kTextStart = 0x1000, kTextEnd = 0x579000;  // exe .text (from the PE headers)
LONG g_logged = 0;
constexpr LONG kMaxLogged = 20;

// Stack overflow: the loop that ran away shows up as the same few return
// addresses repeated thousands of times, so count exe return addresses over
// the whole stack (static storage: the overflowed stack has little room left).
struct Hit {
    std::uint32_t rva, count;
};
Hit g_hits[64];

void Describe(std::uintptr_t address, char* out, std::size_t size) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module)) {
        GetModuleFileNameA(module, name, sizeof(name));
        const char* base = std::strrchr(name, '\\');
        std::snprintf(out, size, "%s+0x%llX", base ? base + 1 : name,
                      static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
    } else {
        std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(address));
    }
}

void LogStackHistogram(std::uintptr_t rsp) {
    std::uintptr_t exe = ExeBase();
    auto top = reinterpret_cast<std::uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    int distinct = 0;
    std::uint64_t scanned = 0, matches = 0;
    std::memset(g_hits, 0, sizeof(g_hits));
    for (std::uintptr_t a = rsp & ~std::uintptr_t(7); a + 8 <= top; a += 8, ++scanned) {
        std::uintptr_t v = 0;
        __try {
            v = *reinterpret_cast<const std::uintptr_t*>(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (v <= exe + kTextStart || v >= exe + kTextEnd) continue;
        ++matches;
        auto rva = static_cast<std::uint32_t>(v - exe);
        int i = 0;
        while (i < distinct && g_hits[i].rva != rva) ++i;
        if (i == distinct) {
            if (distinct == 64) continue;
            g_hits[distinct++] = {rva, 0};
        }
        ++g_hits[i].count;
    }
    Log("  stack scan: %llu slots, %llu exe addresses, %d distinct; most repeated:",
        static_cast<unsigned long long>(scanned), static_cast<unsigned long long>(matches), distinct);
    for (int n = 0; n < 12; ++n) {
        int best = -1;
        for (int i = 0; i < distinct; ++i)
            if (g_hits[i].count && (best < 0 || g_hits[i].count > g_hits[best].count)) best = i;
        if (best < 0) break;
        Log("    exe+0x%X x%u", g_hits[best].rva, g_hits[best].count);
        g_hits[best].count = 0;
    }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT* c = info->ContextRecord;
    char where[MAX_PATH + 32];
    Describe(c->Rip, where, sizeof(where));
    // Faults inside our own DLL come from guarded reads (__try) and are handled there.
    if (std::strstr(where, "kh2coop.dll")) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&g_logged) > kMaxLogged) return EXCEPTION_CONTINUE_SEARCH;
    Log("CRASH (first-chance) code 0x%08lX at %s, thread %lu", code, where, GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        Log("  %s address 0x%llX", info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
            static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
    const auto* code8 = reinterpret_cast<const unsigned char*>(c->Rip);
    __try {
        Log("  bytes at fault: %02X %02X %02X %02X %02X %02X %02X %02X", code8[0], code8[1], code8[2], code8[3],
            code8[4], code8[5], code8[6], code8[7]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("  rax %llX rbx %llX rcx %llX rdx %llX rsi %llX rdi %llX r8 %llX r9 %llX", c->Rax, c->Rbx, c->Rcx, c->Rdx,
        c->Rsi, c->Rdi, c->R8, c->R9);

    // Return addresses into the game's code found on the stack: a rough call chain.
    std::uintptr_t exe = ExeBase();
    auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Rsp);
    char chain[512] = {};
    int len = 0, found = 0;
    for (int i = 0; i < 96 && found < 12 && len < static_cast<int>(sizeof(chain)) - 64; ++i) {
        std::uintptr_t v = 0;
        __try {
            v = stack[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (v > exe + kTextStart && v < exe + kTextEnd) {
            len += std::snprintf(chain + len, sizeof(chain) - len, " 0x%llX", static_cast<unsigned long long>(v - exe));
            ++found;
        } else if (v) {
            char mod[MAX_PATH + 32];
            Describe(v, mod, sizeof(mod));
            if (std::strstr(mod, "kh2coop.dll")) {
                len += std::snprintf(chain + len, sizeof(chain) - len, " [%s]", mod);
                ++found;
            }
        }
    }
    Log("  stack (exe RVAs):%s", chain);
    WorldCensusLogCrashCall();
    if (code == EXCEPTION_STACK_OVERFLOW) LogStackHistogram(c->Rsp);
    return EXCEPTION_CONTINUE_SEARCH;
}

void* g_handler = nullptr;

// abort() (TODO 1.119; the host's game quit that way in Final Xemnas 2026-10-24, Windows: c0000409 kind 7 in
// ucrtbase) ends the process with a fast fail, which no exception handler sees. The game takes abort() from the shared
// CRT (ucrtbase), and so do its pure virtual calls (_purecall) and uncaught C++ exceptions (terminate); the exe calls
// it itself after a failed Direct3D call (exe+0x13EF00, exe+0x13F210). Our DLL links its own CRT, so the handlers go
// into ucrtbase's: SIGABRT runs before the fast fail, and the purecall handler before its abort(). They log the call
// chain (return addresses into any module) and let the game end as before.
LONG g_abortLogged = 0;

void LogCallChain(const char* what) {
    std::uintptr_t here = reinterpret_cast<std::uintptr_t>(&what);
    auto top = reinterpret_cast<std::uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    char chain[900] = {};
    int len = 0, found = 0;
    for (std::uintptr_t a = here & ~std::uintptr_t(7); a + 8 <= top && found < 24 &&
                                                       len < static_cast<int>(sizeof(chain)) - 80; a += 8) {
        std::uintptr_t v = 0;
        __try {
            v = *reinterpret_cast<const std::uintptr_t*>(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        HMODULE module = nullptr;
        if (v < 0x10000 || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                               reinterpret_cast<LPCSTR>(v), &module))
            continue;
        // A return address: the byte before it ends a call (E8 rel32, or FF /2 through a register or memory).
        const auto* p = reinterpret_cast<const unsigned char*>(v);
        bool call = false;
        __try {
            call = p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
                   (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) ||
                   (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (!call) continue;
        char mod[MAX_PATH + 32];
        Describe(v, mod, sizeof(mod));
        const char* name = std::strstr(mod, "KINGDOM HEARTS II FINAL MIX.exe") ? std::strchr(mod, '+') - 3 : mod;
        len += std::snprintf(chain + len, sizeof(chain) - len, " %s", name);
        ++found;
    }
    Log("  %s call chain (return addresses, newest first):%s", what, chain);
}

void __cdecl OnAbortSignal(int) {
    if (InterlockedIncrement(&g_abortLogged) > 3) return;
    Log("ABORT: the game called abort() (thread %lu); it ends now (Windows reports it as c0000409 kind 7)",
        GetCurrentThreadId());
    LogCallChain("abort");
    WorldCensusLogCrashCall();
}

void __cdecl OnPurecall() {
    if (InterlockedIncrement(&g_abortLogged) > 3) return;
    Log("ABORT: a pure virtual call (an object's method called after the object was destroyed?), thread %lu",
        GetCurrentThreadId());
    LogCallChain("purecall");
    WorldCensusLogCrashCall();
}

void InstallAbortLogger() {
    HMODULE crt = GetModuleHandleA("ucrtbase.dll");
    if (!crt) {
        Log("abort logger: ucrtbase.dll not loaded: off");
        return;
    }
    using PFN_Signal = void(__cdecl* (__cdecl*)(int, void(__cdecl*)(int)))(int);
    using PFN_SetPurecall = void(__cdecl* (__cdecl*)(void(__cdecl*)()))();
    auto setSignal = reinterpret_cast<PFN_Signal>(GetProcAddress(crt, "signal"));
    constexpr int kSigAbrt = 22;  // SIGABRT
    bool a = setSignal && setSignal(kSigAbrt, OnAbortSignal) != reinterpret_cast<void(__cdecl*)(int)>(-1);  // SIG_ERR
    // The exe takes _purecall from vcruntime140, which (like ucrtbase) exports its own handler setter: set both.
    int purecall = 0;
    HMODULE modules[] = {GetModuleHandleA("vcruntime140.dll"), crt};
    for (HMODULE m : modules) {
        auto set = m ? reinterpret_cast<PFN_SetPurecall>(GetProcAddress(m, "_set_purecall_handler")) : nullptr;
        if (set) set(OnPurecall), ++purecall;
    }
    Log("abort logger: %s (abort() %s, pure virtual calls %s in %d of vcruntime140 / ucrtbase)",
        a || purecall ? "on" : "OFF", a ? "logged" : "not", purecall ? "logged" : "not", purecall);
}

}  // namespace

// Other code in the process (e.g. the .NET runtime) can add its own handler in
// front of ours later; re-adding ours keeps it first in line. Called now and then.
void KeepCrashLoggerFirst() {
    if (g_handler) RemoveVectoredExceptionHandler(g_handler);
    g_handler = AddVectoredExceptionHandler(1, OnException);
}

void InstallCrashLogger() {
    g_handler = AddVectoredExceptionHandler(1, OnException);
    // Room for the handler to log on this thread after a stack overflow.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    Log("crash logger installed (thread %lu)", GetCurrentThreadId());
    InstallAbortLogger();
}

}  // namespace kh2coop
