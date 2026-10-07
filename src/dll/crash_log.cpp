// Crash logger: when the game hits a fatal exception, write where it happened
// to our log so a crash becomes a clue. A vectored handler sees exceptions
// first (before the game's own handlers), so it also logs ones that something
// later recovers from; those are marked "first-chance". Capped per run.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"

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
    if (code == EXCEPTION_STACK_OVERFLOW) LogStackHistogram(c->Rsp);
    return EXCEPTION_CONTINUE_SEARCH;
}

void* g_handler = nullptr;

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
}

}  // namespace kh2coop
