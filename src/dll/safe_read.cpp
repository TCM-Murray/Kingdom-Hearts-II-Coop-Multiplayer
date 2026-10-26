// The safe read (docs/CRASH_RISK_REVIEW.md section 4, way 1, and its four conditions). A fault inside our DLL can
// end the game even inside __try, because the .NET runtime's vectored handler sees it before any __except of ours
// (F335). So the read itself runs in a few bytes of code emitted at init into a page of ours, and a vectored handler
// of its own, kept first in the process's list, moves a fault there to a landing that returns false and answers
// "continue execution": the dispatch ends with it, and no later handler hears of the fault.
//
// The handler does one range compare and nothing more on every other exception; the crash logger stays a separate
// handler behind it. It is put back at the head every frame from OnFrame, the new copy added before the old one is
// removed, so the list is never without it (user pick 2026-10-26). A guard-page fault in the routine (another
// thread's stack, which the area check of the callers should have kept out) gets its guard put back and is logged as
// a bug (user pick 2026-10-26): the thread whose guard it was could otherwise not grow its stack past that page.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "fx_pool.hpp"

namespace kh2coop {
namespace {

// The routines, Microsoft x64 calling convention: rcx = the address, rdx = where the value goes; eax = 1, or 0 from
// the landing. Each reads exactly its size, so a read never touches a page its caller did not ask about.
constexpr std::uint8_t kCode[] = {
    // +0x00 one byte: movzx eax, byte [rcx]; mov [rdx], al; mov eax, 1; ret
    0x0F, 0xB6, 0x01, 0x88, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    // +0x10 two bytes: movzx eax, word [rcx]; mov [rdx], ax; mov eax, 1; ret
    0x0F, 0xB7, 0x01, 0x66, 0x89, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC,
    // +0x20 four bytes: mov eax, [rcx]; mov [rdx], eax; mov eax, 1; ret
    0x8B, 0x01, 0x89, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    // +0x30 eight bytes: mov rax, [rcx]; mov [rdx], rax; mov eax, 1; ret
    0x48, 0x8B, 0x01, 0x48, 0x89, 0x02, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC,
    // +0x40 the landing: xor eax, eax; ret
    0x31, 0xC0, 0xC3,
};
constexpr std::uintptr_t kLanding = 0x40;

using ReadFn = int (*)(const void*, void*);
std::uintptr_t g_code = 0;  // the page; 0 = not made (SafeReadBytes then reads under __try, as before)
ReadFn g_read[4] = {};
void* g_rescue = nullptr;

// What the handler caught, for SafeReadFrame's lines (the handler itself never logs).
volatile LONG g_caught = 0, g_fellBack = 0;
volatile std::uintptr_t g_lastAddress = 0;
struct GuardHit {
    std::uintptr_t address;
    DWORD thread;
    bool restored;
};
GuardHit g_guardHits[8];
volatile LONG g_guardCount = 0;
LONG g_guardLogged = 0;

void RestoreGuard(std::uintptr_t address) {
    // The system cleared the guard before any handler ran; it allows putting it back.
    MEMORY_BASIC_INFORMATION mbi;
    DWORD old = 0;
    auto page = reinterpret_cast<void*>(address & ~std::uintptr_t(0xFFF));
    bool restored = VirtualQuery(page, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                    VirtualProtect(page, 1, mbi.Protect | PAGE_GUARD, &old);
    LONG n = InterlockedIncrement(&g_guardCount) - 1;
    if (n < static_cast<LONG>(sizeof(g_guardHits) / sizeof(g_guardHits[0])))
        g_guardHits[n] = {address, GetCurrentThreadId(), restored};
}

LONG CALLBACK OnRescue(EXCEPTION_POINTERS* info) {
    CONTEXT* c = info->ContextRecord;
    if (c->Rip - g_code >= kLanding) return EXCEPTION_CONTINUE_SEARCH;  // not in the four reads
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    DWORD code = r->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_GUARD_PAGE_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR)
        return EXCEPTION_CONTINUE_SEARCH;
    std::uintptr_t address = r->NumberParameters >= 2 ? r->ExceptionInformation[1] : 0;
    if (code == STATUS_GUARD_PAGE_VIOLATION) RestoreGuard(address);
    g_lastAddress = address;
    InterlockedIncrement(&g_caught);
    c->Rip = g_code + kLanding;
    return EXCEPTION_CONTINUE_EXECUTION;
}

std::uintptr_t g_exeEnd = 0;
LONG g_caughtLogged = 0, g_fellBackLogged = 0, g_lines = 0;
std::uint64_t g_lastLine = 0;
bool g_testing = false;  // the saferead test's own guard hits are expected, not bugs

}  // namespace

bool SafeReadBytes(const void* at, void* out, int size) {
    int i = size == 1 ? 0 : size == 2 ? 1 : size == 4 ? 2 : 3;
    __try {
        if (g_read[i]) return g_read[i](at, out) != 0;
        std::memcpy(out, at, static_cast<std::size_t>(size));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Reached only when our handler was not first (or the page was not made): the protection of today.
        InterlockedIncrement(&g_fellBack);
        return false;
    }
}

bool KnownArea(std::uintptr_t address, std::uint32_t bytes) {
    // The exe image: every section mapped for the whole run, the 30 MB file heap (exe+0xBF33C0..exe+0x29F33C0)
    // inside .data (VERIFIED_OFFSETS, exe image layout).
    std::uintptr_t exe = ExeBase();
    if (exe && address >= exe && address + bytes <= g_exeEnd) return true;
    return FxPoolOwns(address, bytes);
}

namespace {
volatile LONG g_outside = 0;
volatile std::uintptr_t g_lastOutside = 0;
LONG g_outsideLogged = 0;
}  // namespace

bool ReadAnyBytes(std::uintptr_t at, void* out, int size) {
    if (!KnownArea(at, static_cast<std::uint32_t>(size))) {
        InterlockedIncrement(&g_outside);
        g_lastOutside = at;
        if (!Readable(at, static_cast<std::uint32_t>(size))) return false;
    }
    return SafeReadBytes(reinterpret_cast<const void*>(at), out, size);
}

bool SafeReadOwns(std::uintptr_t rip) { return g_code && rip - g_code < sizeof(kCode); }

void SafeReadInit() {
    if (std::uintptr_t exe = ExeBase()) {
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew);
        g_exeEnd = exe + nt->OptionalHeader.SizeOfImage;
    }
    void* page = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old = 0;
    if (!page) {
        Log("safe read: no page for the read routine (error %lu); reads stay under __try only", GetLastError());
        return;
    }
    std::memcpy(page, kCode, sizeof(kCode));
    if (!VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old)) {
        Log("safe read: the routine's page cannot be made executable (error %lu); reads stay under __try only",
            GetLastError());
        VirtualFree(page, 0, MEM_RELEASE);
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), page, sizeof(kCode));
    auto base = reinterpret_cast<std::uintptr_t>(page);
    for (int i = 0; i < 4; ++i) g_read[i] = reinterpret_cast<ReadFn>(base + 0x10 * i);
    g_rescue = AddVectoredExceptionHandler(1, OnRescue);
    g_code = base;  // last: the handler and SafeReadOwns test it
    Log("safe read: routine at 0x%llX, its handler first in line (exe image 0x%llX..0x%llX)",
        static_cast<unsigned long long>(base), static_cast<unsigned long long>(ExeBase()),
        static_cast<unsigned long long>(g_exeEnd));
}

void SafeReadFrame() {
    if (!g_code) return;
    // Back to the head: the new copy first, then the old one out, so the list is never without it.
    void* fresh = AddVectoredExceptionHandler(1, OnRescue);
    if (fresh) {
        if (g_rescue) RemoveVectoredExceptionHandler(g_rescue);
        g_rescue = fresh;
    }
    LONG guards = g_guardCount;
    constexpr LONG kHits = sizeof(g_guardHits) / sizeof(g_guardHits[0]);
    for (; g_guardLogged < guards; ++g_guardLogged) {
        if (g_guardLogged >= kHits) {
            Log("%ssafe read: %ld more guard-page hits, not logged one by one", g_testing ? "" : "BUG: ",
                guards - g_guardLogged);
            g_guardLogged = guards;
            break;
        }
        const GuardHit& h = g_guardHits[g_guardLogged];
        if (!h.thread) break;  // counted but not written yet: next frame
        Log("%ssafe read: a read hit a guard page at 0x%llX (thread %lu)%s; the guard was %s", g_testing ? "" : "BUG: ",
            static_cast<unsigned long long>(h.address), h.thread,
            g_testing ? " (expected: the saferead test)"
                      : ": a pointer outside the known areas reached it without Readable (probably another "
                        "thread's stack)",
            h.restored ? "put back" : "NOT put back: that thread's stack can no longer grow past it");
    }
    if (g_testing) return;
    std::uint64_t now = GetTickCount64();
    if (now - g_lastLine < 1000 || g_lines >= 100) return;
    LONG caught = g_caught, fellBack = g_fellBack, outside = g_outside;
    if (caught == g_caughtLogged && fellBack == g_fellBackLogged && outside == g_outsideLogged) return;
    Log("safe read: since the last line %ld faults landed as 'false' (the last reading 0x%llX)%s; %ld reads outside the "
        "known areas went through Readable (the last 0x%llX)", caught - g_caughtLogged,
        static_cast<unsigned long long>(g_lastAddress),
        fellBack != g_fellBackLogged ? ", some reached the __try fallback (our handler was not first)" : "",
        outside - g_outsideLogged, static_cast<unsigned long long>(g_lastOutside));
    g_outsideLogged = outside;
    if (++g_lines == 100) Log("safe read: no more of these lines this run");
    g_caughtLogged = caught;
    g_fellBackLogged = fellBack;
    g_lastLine = now;
}

// ---- tools/debug_cmd.py saferead: condition 1's proof, and the guard-page cases of condition 3 ----
namespace {

struct Helper {
    HANDLE ready, go, done;
    std::uintptr_t guard;  // the helper's own stack guard page
    bool guardFound;
    int grown;             // pages its stack grew past the guard, read one at a time, of kGrow
    bool limitMoved, recursed;
};
constexpr int kGrow = 16;

// 32 frames of 4 KB each (128 KB) touched on the way down: the stack's normal growth past the guard.
int Recurse(int depth) {
    volatile std::uint8_t block[4096];
    block[0] = static_cast<std::uint8_t>(depth);
    block[sizeof(block) - 1] = static_cast<std::uint8_t>(depth);
    return depth ? Recurse(depth - 1) + block[0] : block[sizeof(block) - 1];
}

DWORD WINAPI HelperThread(void* p) {
    auto* h = static_cast<Helper*>(p);
    auto* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    auto limit = reinterpret_cast<std::uintptr_t>(tib->StackLimit);  // the lowest committed page
    h->guard = limit - 0x1000;
    MEMORY_BASIC_INFORMATION mbi;
    h->guardFound = VirtualQuery(reinterpret_cast<void*>(h->guard), &mbi, sizeof(mbi)) && (mbi.Protect & PAGE_GUARD);
    SetEvent(h->ready);
    WaitForSingleObject(h->go, 5000);
    // The owner's own touch of its guard page grows its stack (the system's, not a fault we see); if the guard put
    // back were not one the system honours, the read lands on false and the next page is unreserved memory, also
    // false: the test fails with a log line, not a lost game.
    for (int i = 0; i < kGrow; ++i) {
        std::uint8_t v;
        if (!SafeRead(h->guard - 0x1000 * i, &v)) break;
        ++h->grown;
    }
    h->limitMoved = reinterpret_cast<std::uintptr_t>(tib->StackLimit) <= h->guard - 0x1000 * (kGrow - 1);
    if (h->grown == kGrow) h->recursed = Recurse(32) >= 0;
    SetEvent(h->done);
    return 0;
}

}  // namespace

void SafeReadDebugTest() {
    Log("saferead test: start (routine %s, handler %s)", g_code ? "made" : "NOT made", g_rescue ? "in" : "NOT in");
    g_testing = true;
    LONG caught0 = g_caught, fellBack0 = g_fellBack, guards0 = g_guardCount;

    // A: 100 faulting reads of every size, from the null page, a reserved page, a no-access page, a kernel address.
    void* reserved = VirtualAlloc(nullptr, 0x10000, MEM_RESERVE, PAGE_NOACCESS);
    void* noAccess = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    const std::uintptr_t bad[] = {0x10, reinterpret_cast<std::uintptr_t>(reserved) + 0x100,
                                  reinterpret_cast<std::uintptr_t>(noAccess) + 0x10, 0xFFFF800000001000ull};
    int falses = 0, goods = 0;
    for (int i = 0; i < 100; ++i) {
        std::uintptr_t at = bad[i % 4];
        std::uint64_t v = 0;
        bool ok = (i / 4) % 4 == 0   ? SafeRead(at, reinterpret_cast<std::uint8_t*>(&v))
                  : (i / 4) % 4 == 1 ? SafeRead(at, reinterpret_cast<std::uint16_t*>(&v))
                  : (i / 4) % 4 == 2 ? SafeRead(at, reinterpret_cast<std::uint32_t*>(&v))
                                     : SafeRead(at, &v);
        falses += !ok;
    }
    const std::uint64_t known[2] = {0x1122334455667788ull, 0x99AABBCCDDEEFF00ull};
    for (int i = 0; i < 100; ++i) {
        auto at = reinterpret_cast<std::uintptr_t>(known) + (i % 8);
        std::uint8_t b = 0;
        std::uint16_t w = 0;
        std::uint32_t d = 0;
        std::uint64_t q = 0;
        bool ok = SafeRead(at, &b) && SafeRead(at, &w) && SafeRead(at, &d) && SafeRead(at, &q);
        std::uint64_t want;
        std::memcpy(&want, reinterpret_cast<const void*>(at), 8);
        goods += ok && b == (want & 0xFF) && w == (want & 0xFFFF) && d == (want & 0xFFFFFFFF) && q == want;
    }
    LONG caughtA = g_caught - caught0, fellBackA = g_fellBack - fellBack0;
    bool passA = falses == 100 && goods == 100 && caughtA == 100 && fellBackA == 0;
    Log("saferead test A: %d of 100 bad reads false, the handler caught %ld, %ld reached __try; %d of 100 good reads "
        "right: %s", falses, caughtA, fellBackA, goods, passA ? "pass" : "FAIL");

    // B: a guard page of ours, twice: false, and the guard back each time.
    void* guardPage = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE | PAGE_GUARD);
    int guardOk = 0;
    for (int i = 0; i < 2 && guardPage; ++i) {
        std::uint32_t v = 0;
        bool ok = SafeRead(reinterpret_cast<std::uintptr_t>(guardPage) + 0x20, &v);
        MEMORY_BASIC_INFORMATION mbi;
        bool back = VirtualQuery(guardPage, &mbi, sizeof(mbi)) && (mbi.Protect & PAGE_GUARD);
        guardOk += !ok && back;
    }
    bool passB = guardOk == 2;
    Log("saferead test B: our own guard page, %d of 2 reads false with the guard put back: %s", guardOk,
        passB ? "pass" : "FAIL");

    // C: another thread's stack guard page, read from here (the case of condition 3); then that thread grows its
    // stack past it.
    Helper h = {};
    h.ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    h.go = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    h.done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    HANDLE thread = CreateThread(nullptr, 256 * 1024, HelperThread, &h, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    bool readFalse = false, guardBack = false, finished = false;
    if (thread && WaitForSingleObject(h.ready, 2000) == WAIT_OBJECT_0 && h.guardFound) {
        std::uint8_t v = 0;
        readFalse = !SafeRead(h.guard + 0x800, &v);
        MEMORY_BASIC_INFORMATION mbi;
        guardBack = VirtualQuery(reinterpret_cast<void*>(h.guard), &mbi, sizeof(mbi)) && (mbi.Protect & PAGE_GUARD);
        SetEvent(h.go);
        finished = WaitForSingleObject(h.done, 5000) == WAIT_OBJECT_0;
    } else {
        SetEvent(h.go);
    }
    bool passC = h.guardFound && readFalse && guardBack && finished && h.grown == kGrow && h.limitMoved && h.recursed;
    Log("saferead test C: another thread's stack guard page %s at 0x%llX; read from here %s, guard %s; that thread's "
        "stack grew past it %d of %d pages (its limit %s), 128 KB deeper %s: %s",
        h.guardFound ? "found" : "NOT found", static_cast<unsigned long long>(h.guard), readFalse ? "false" : "NOT false",
        guardBack ? "put back" : "NOT put back", h.grown, kGrow, h.limitMoved ? "moved down" : "did NOT move",
        h.recursed ? "fine" : "not tried", passC ? "pass" : "FAIL");
    if (thread) {
        WaitForSingleObject(thread, 2000);
        CloseHandle(thread);
    }
    CloseHandle(h.ready);
    CloseHandle(h.go);
    CloseHandle(h.done);
    if (reserved) VirtualFree(reserved, 0, MEM_RELEASE);
    if (noAccess) VirtualFree(noAccess, 0, MEM_RELEASE);
    if (guardPage) VirtualFree(guardPage, 0, MEM_RELEASE);

    SafeReadFrame();  // the guard lines now, marked as the test's
    g_testing = false;
    g_caughtLogged = g_caught;  // the test's faults are not the game's
    g_fellBackLogged = g_fellBack;
    Log("saferead test: %s (guard hits %ld)", passA && passB && passC ? "PASS" : "FAIL", g_guardCount - guards0);
}

}  // namespace kh2coop
