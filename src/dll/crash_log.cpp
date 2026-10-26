// Crash logger: when the game hits a fatal exception, write where it happened
// to our log so a crash becomes a clue. A vectored handler sees exceptions
// first (before the game's own handlers), so it also logs ones that something
// later recovers from; those are marked "first-chance". Capped per run.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

#include <MinHook.h>

#include "common.hpp"
#include "world_census.hpp"

extern "C" IMAGE_DOS_HEADER __ImageBase;

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

void LogCallChain(const char* what);
LONG g_chainLogged = 0;

// Our DLL's and clr.dll's (the .NET runtime's) address ranges, for the cheap "whose code faulted" test the handler
// makes on every exception. clr's is taken once it is seen loaded: at install, then by KeepCrashLoggerFirst.
std::uintptr_t g_selfBase, g_selfEnd, g_clrBase, g_clrEnd;

void ModuleRange(HMODULE module, std::uintptr_t* base, std::uintptr_t* end) {
    auto b = reinterpret_cast<std::uintptr_t>(module);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(b + reinterpret_cast<const IMAGE_DOS_HEADER*>(b)->e_lfanew);
    *end = b + nt->OptionalHeader.SizeOfImage;  // the end first: the handler tests the base
    *base = b;
}

bool InSelf(std::uintptr_t a) { return a >= g_selfBase && a < g_selfEnd; }
bool InClr(std::uintptr_t a) { return g_clrBase && a >= g_clrBase && a < g_clrEnd; }

void NoteClrLoaded() {
    if (g_clrBase) return;
    HMODULE clr = GetModuleHandleA("clr.dll");
    if (!clr) return;
    ModuleRange(clr, &g_clrBase, &g_clrEnd);
    Log("crash logger: clr.dll (the .NET runtime) is loaded at 0x%llX (thread %lu)",
        static_cast<unsigned long long>(g_clrBase), GetCurrentThreadId());
}

// Bytes of stack the faulting thread has left: down to the bottom of its reserve, where a stack overflow is raised
// (CRASH_RISK_REVIEW section 4, measurement a: a handler dying of depth shows as a small number here).
unsigned long long StackLeft(std::uintptr_t rsp) {
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    return rsp > low ? rsp - low : 0;
}

// Measurement b: whether a handler faults while it looks at an earlier fault of ours or clr's on the same thread.
// Such an exception is raised further down the same stack while the earlier one's dispatch still runs, and the
// earlier fault's record, which the dispatcher keeps on the stack, is still intact; once that dispatch is over the
// thread's next exception is not below it, or the record is gone. Depth 0 = not nested.
struct Outstanding {
    const EXCEPTION_RECORD* record;
    DWORD code;
    void* address;
    std::uint64_t at;
    int depth;
    char where[48];
};
thread_local Outstanding t_outstanding;
LONG g_nestedLogged = 0, g_clrLogged = 0, g_lowStackLogged = 0;

int NestedDepth(const EXCEPTION_POINTERS* info) {
    Outstanding& o = t_outstanding;
    if (!o.record) return 0;
    if (info->ContextRecord->Rsp < reinterpret_cast<std::uintptr_t>(o.record) && GetTickCount64() - o.at < 1000 &&
        o.record->ExceptionCode == o.code && o.record->ExceptionAddress == o.address)
        return o.depth + 1;
    o.record = nullptr;
    return 0;
}

void NoteOutstanding(const EXCEPTION_POINTERS* info, int depth, const char* where) {
    Outstanding& o = t_outstanding;
    o.record = info->ExceptionRecord;
    o.code = info->ExceptionRecord->ExceptionCode;
    o.address = info->ExceptionRecord->ExceptionAddress;
    o.at = GetTickCount64();
    o.depth = depth;
    std::snprintf(o.where, sizeof(o.where), "%s", where);
}

// Faults inside our DLL: each fault address logged once (guarded reads land on the same few all the time), and the
// last one kept, with the return addresses into our DLL above it, for the log when the game dies elsewhere next.
std::uint32_t g_ownSeen[16];
LONG g_ownSeenCount = 0;
struct OwnFault {
    std::uint64_t at;
    std::uint32_t rva, callers[6];
    unsigned long long address, stackLeft;
    DWORD thread, code;
} g_lastOwn;

void NoteOwnFault(const EXCEPTION_POINTERS* info, const char* where) {
    auto self = reinterpret_cast<std::uintptr_t>(&__ImageBase);
    const CONTEXT* c = info->ContextRecord;
    g_lastOwn = {};
    g_lastOwn.at = GetTickCount64();
    g_lastOwn.rva = static_cast<std::uint32_t>(c->Rip - self);
    g_lastOwn.address = info->ExceptionRecord->NumberParameters >= 2 ? info->ExceptionRecord->ExceptionInformation[1] : 0;
    g_lastOwn.thread = GetCurrentThreadId();
    g_lastOwn.code = info->ExceptionRecord->ExceptionCode;
    g_lastOwn.stackLeft = StackLeft(c->Rsp);
    auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Rsp);
    int n = 0;
    for (int i = 0; i < 64 && n < 6; ++i) {
        std::uintptr_t v = 0;
        __try {
            v = stack[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (v > self + 0x1000 && v < self + 0x400000) g_lastOwn.callers[n++] = static_cast<std::uint32_t>(v - self);
    }
    bool seen = false;
    for (LONG i = 0; i < g_ownSeenCount && !seen; ++i) seen = g_ownSeen[i] == g_lastOwn.rva;
    // A seen place is logged again only with little stack left (measurement a), a few times per run.
    constexpr unsigned long long kLowStack = 64 * 1024;
    if (seen || g_ownSeenCount >= 16) {
        if (g_lastOwn.stackLeft < kLowStack && InterlockedIncrement(&g_lowStackLogged) <= 10)
            Log("fault in our DLL with little stack left: code 0x%08lX at %s, address 0x%llX, thread %lu, "
                "stack left %llu bytes", g_lastOwn.code, where, g_lastOwn.address, g_lastOwn.thread,
                g_lastOwn.stackLeft);
        return;
    }
    g_ownSeen[g_ownSeenCount++] = g_lastOwn.rva;
    Log("fault in our DLL (first-chance, first at this place; guarded reads land here too) code 0x%08lX at %s, "
        "address 0x%llX, thread %lu, stack left %llu bytes", g_lastOwn.code, where, g_lastOwn.address,
        g_lastOwn.thread, g_lastOwn.stackLeft);
}

void LogLastOwnFault() {
    if (!g_lastOwn.at) return;
    char callers[128] = {};
    int len = 0;
    for (std::uint32_t r : g_lastOwn.callers)
        if (r) len += std::snprintf(callers + len, sizeof(callers) - len, " +0x%X", r);
    Log("  the last fault in our DLL before this: code 0x%08lX at kh2coop.dll+0x%X, address 0x%llX, thread %lu, "
        "%llu ms ago, stack left %llu bytes; callers in our DLL:%s", g_lastOwn.code, g_lastOwn.rva,
        g_lastOwn.address, g_lastOwn.thread, static_cast<unsigned long long>(GetTickCount64() - g_lastOwn.at),
        g_lastOwn.stackLeft, callers);
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    const CONTEXT* c = info->ContextRecord;
    if (SafeReadOwns(c->Rip)) return EXCEPTION_CONTINUE_SEARCH;  // the safe read's handler, just ahead or behind, lands it
    char where[MAX_PATH + 32];
    // Measurement b (CRASH_RISK_REVIEW section 4): every code, not only the five below, for an exception in our DLL
    // or in clr.dll, and for one raised inside the dispatch of such an earlier one on this thread.
    int depth = NestedDepth(info);
    bool own = InSelf(c->Rip), clr = InClr(c->Rip);
    if (own || clr || depth) {
        Describe(c->Rip, where, sizeof(where));
        if (depth && InterlockedIncrement(&g_nestedLogged) <= 30)
            Log("exception 0x%08lX at %s, thread %lu, raised inside the dispatch of the earlier 0x%08lX at %s "
                "(depth %d: a handler faulted while it looked at that one), stack left %llu bytes", code, where,
                GetCurrentThreadId(), t_outstanding.code, t_outstanding.where, depth, StackLeft(c->Rsp));
        else if (clr && InterlockedIncrement(&g_clrLogged) <= 20)
            Log("exception in clr.dll (first-chance) code 0x%08lX at %s, thread %lu, stack left %llu bytes", code,
                where, GetCurrentThreadId(), StackLeft(c->Rsp));
        NoteOutstanding(info, depth, where);
        // Faults inside our own DLL mostly come from guarded reads (__try) and are handled there, but a handler
        // further down the chain (the .NET runtime's) has died on one before our __except ran (the friend's crash
        // 2026-10-25 21:59:48: only clr.dll's stack overflow was logged): the first few are logged, one line each.
        if (own) {
            NoteOwnFault(info, where);
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    if (!(own || clr || depth)) Describe(c->Rip, where, sizeof(where));
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
    // Not in the game's code (e.g. clr.dll dying while it looks at an earlier fault): the whole call chain, every
    // module, so the earlier fault's callers show too.
    if (!std::strstr(where, "KINGDOM HEARTS II FINAL MIX.exe") && InterlockedIncrement(&g_chainLogged) <= 2) {
        LogLastOwnFault();
        LogCallChain("fault");
    }
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
    char chain[2400] = {};
    int len = 0, found = 0;
    for (std::uintptr_t a = here & ~std::uintptr_t(7); a + 8 <= top && found < 60 &&
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

// Condition 1 of CRASH_RISK_REVIEW section 4: who else adds vectored handlers, exception or continue ones (a continue
// handler runs even after a handler of ours answers "continue execution"). Hooked only when clr.dll is not loaded
// yet at install; our own registrations (the logger's re-adds, the watch, the cutscene trace) are not logged.
using PFN_AddVectored = PVOID(NTAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);
PFN_AddVectored g_realAddException = nullptr, g_realAddContinue = nullptr;
LONG g_registrationsLogged = 0;

void LogRegistration(const char* list, ULONG first, void* handler, void* caller) {
    if (InSelf(reinterpret_cast<std::uintptr_t>(caller)) || InterlockedIncrement(&g_registrationsLogged) > 40) return;
    char by[MAX_PATH + 32], at[MAX_PATH + 32];
    Describe(reinterpret_cast<std::uintptr_t>(caller), by, sizeof(by));
    Describe(reinterpret_cast<std::uintptr_t>(handler), at, sizeof(at));
    Log("crash logger: a vectored %s handler was added %s, called from %s, handler at %s (thread %lu)", list,
        first ? "at the head" : "at the end", by, at, GetCurrentThreadId());
}

PVOID NTAPI HookAddException(ULONG first, PVECTORED_EXCEPTION_HANDLER handler) {
    LogRegistration("exception", first, reinterpret_cast<void*>(handler), _ReturnAddress());
    return g_realAddException(first, handler);
}

PVOID NTAPI HookAddContinue(ULONG first, PVECTORED_EXCEPTION_HANDLER handler) {
    LogRegistration("continue", first, reinterpret_cast<void*>(handler), _ReturnAddress());
    return g_realAddContinue(first, handler);
}

void WatchRegistrations() {
    MH_STATUS init = MH_Initialize();
    bool ok = init == MH_OK || init == MH_ERROR_ALREADY_INITIALIZED;
    void* target = nullptr;
    ok = ok &&
         MH_CreateHookApiEx(L"ntdll", "RtlAddVectoredExceptionHandler", reinterpret_cast<void*>(HookAddException),
                            reinterpret_cast<void**>(&g_realAddException), &target) == MH_OK &&
         MH_EnableHook(target) == MH_OK &&
         MH_CreateHookApiEx(L"ntdll", "RtlAddVectoredContinueHandler", reinterpret_cast<void*>(HookAddContinue),
                            reinterpret_cast<void**>(&g_realAddContinue), &target) == MH_OK &&
         MH_EnableHook(target) == MH_OK;
    Log("crash logger: clr.dll is not loaded yet; later vectored handler registrations %s",
        ok ? "are logged" : "can NOT be logged (hook failed)");
}

}  // namespace

// Other code in the process (e.g. the .NET runtime) can add its own handler in
// front of ours later; re-adding ours keeps it first in line. Called now and then.
void KeepCrashLoggerFirst() {
    if (g_handler) RemoveVectoredExceptionHandler(g_handler);
    g_handler = AddVectoredExceptionHandler(1, OnException);
    NoteClrLoaded();
}

void InstallCrashLogger() {
    ModuleRange(reinterpret_cast<HMODULE>(&__ImageBase), &g_selfBase, &g_selfEnd);
    g_handler = AddVectoredExceptionHandler(1, OnException);
    // Room for the handler to log on this thread after a stack overflow.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);
    Log("crash logger installed (thread %lu)", GetCurrentThreadId());
    NoteClrLoaded();
    if (!g_clrBase) WatchRegistrations();
    InstallAbortLogger();
}

// clr.dll's range as soon as it is loaded (about 0.7 s after OnInit, F338), and the thread OnFrame runs on with its
// stack, once (measurement a: SetThreadStackGuarantee is set only on the thread InstallCrashLogger ran on, F339; the
// review's fix for the frame thread waits on what the fault lines say).
void CrashLoggerFrame() {
    NoteClrLoaded();
    static bool logged = false;
    if (logged) return;
    logged = true;
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    ULONG guarantee = 0;
    SetThreadStackGuarantee(&guarantee);  // 0 asks for the current value only
    char here = 0;
    Log("crash logger: frame thread %lu, stack reserve %llu KB, %llu KB left here, stack guarantee %lu bytes",
        GetCurrentThreadId(), static_cast<unsigned long long>((high - low) / 1024),
        static_cast<unsigned long long>((reinterpret_cast<std::uintptr_t>(&here) - low) / 1024), guarantee);
}

}  // namespace kh2coop
