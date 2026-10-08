// Write watch (KH2COOP_WATCH=<hex offset>[,<hex offset>...], up to 4, e.g.
// BF8,D38,D40): CPU debug registers DR0..DR3 on 4-byte fields at those offsets
// inside one enemy actor, so every instruction that writes them is logged with
// the value written and the calling code. Used to find the function that sets
// an enemy's target. (enemy+0x8F0, the first candidate, turned out to be the
// "next" link of the spatial grid list, written by exe+0x189810.)
//
// Debug registers belong to a thread. The watch is armed from the enemy
// update hook on the game thread: a short helper thread suspends the game
// thread, sets DR0..DR3/DR7 with SetThreadContext and resumes it. (Setting them
// in the context of a RaiseException we handle ourselves did not take effect.)

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "watch.hpp"

namespace kh2coop {
volatile bool g_swapActive = false;
namespace {

constexpr std::uintptr_t kActorUpdateAge = 60;  // frames without updates before switching enemy
constexpr std::uintptr_t kSoraPtr = 0x2A171C8;
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;
constexpr std::uintptr_t kHandleBases = 0x2B0D720;  // see DecodeHandle in puppet.cpp

std::uintptr_t g_offsets[4] = {};  // fields watched (DR0..DR3)
int g_offsetCount = 0;             // 0 = watch off
std::uintptr_t g_enemy = 0;        // actor being watched
std::uintptr_t g_armBase = 0;      // enemy the helper should arm on (0 = disarm)
std::uint64_t g_frame = 0, g_enemySeen = 0;
DWORD g_gameThread = 0;
void* g_veh = nullptr;
DWORD64 g_readBackDr7 = 0;

struct WriteHit {
    std::uint8_t field;                // index into g_offsets
    std::uint32_t rip;                 // exe RVA of the instruction after the write
    std::uint32_t caller[3];           // first exe return addresses on the stack
    std::uint32_t count;
    std::uint32_t values[5];           // 0, Sora, companion, other, Sora copy (compressed pointers decoded)
    std::uintptr_t lastOther;          // last value of class "other", decoded
};
WriteHit g_hits[32];
volatile LONG g_hitCount = 0;

// Read watch (KH2COOP_READWATCH=<hex exe offset>): DR3 on 8 bytes of a global, read or write; every
// instruction touching it is counted with its callers, split by whether an enemy's copy-as-player swap
// (aggro.cpp) was in progress. Used to find who reads the player pointer exe+0x2A105D0 (TODO 1.4).
// Research only, keep it short: with thousands of traps a frame the game thread ran out of stack
// when Sora entered a Drive Form (crash in clr.dll's stack probe, bench 2026-10-08).
std::uintptr_t g_readWatch = 0;  // absolute address, 0 = off
bool g_readArmed = false;
struct ReadHit {
    std::uint32_t rip, caller[3];
    std::uint32_t head, other;  // outside / inside an enemy swap (aggro.cpp)
};
ReadHit g_reads[96];
volatile LONG g_readCount = 0;

// A player-class actor (object table type 0) that isn't Sora: the Sora copy.
bool IsOtherPlayerClass(std::uintptr_t actor, std::uintptr_t sora) {
    if (actor == sora) return false;
    __try {
        std::uintptr_t exe = ExeBase();
        auto row = *reinterpret_cast<const std::uintptr_t*>(actor + 0x918);
        return row > exe && row < exe + 0x3000000 && *reinterpret_cast<const std::uint8_t*>(row + 4) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return h;
    std::uintptr_t base = reinterpret_cast<const std::uintptr_t*>(ExeBase() + kHandleBases)[(h >> 25) & 0x3F];
    return base | (h & 0x1FFFFFF);
}

// 32-bit field: 0, a compressed pointer into Sora or a companion, the copy, or other.
int ValueClass(std::uint32_t h) {
    if (h == 0) return 0;
    if (!(h & 0x80000000u)) return 3;
    std::uintptr_t exe = ExeBase();
    std::uintptr_t base = reinterpret_cast<const std::uintptr_t*>(exe + kHandleBases)[(h >> 25) & 0x3F];
    std::uintptr_t v = base | (h & 0x1FFFFFF);
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + kSoraPtr);
    if (sora && v >= sora && v < sora + 0x1000) return 1;
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t f = *reinterpret_cast<const std::uintptr_t*>(exe + kFriendSlots + 8 * i);
        if (f && v >= f && v < f + 0x1000) return 2;
    }
    return IsOtherPlayerClass(v, sora) ? 4 : 3;
}

LONG CALLBACK OnWatchException(EXCEPTION_POINTERS* info) {
    CONTEXT* c = info->ContextRecord;
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(c->Dr6 & 0xF))
        return EXCEPTION_CONTINUE_SEARCH;
    int field = 0;
    while (!(c->Dr6 & (1ull << field))) ++field;
    if (field == 3 && g_readWatch) {
        c->Dr6 = 0;
        std::uintptr_t exe = ExeBase();
        auto rva = [exe](std::uintptr_t v) -> std::uint32_t {
            return v > exe && v < exe + 0x579000 ? static_cast<std::uint32_t>(v - exe) : 0;
        };
        std::uint32_t rip = rva(c->Rip), callers[3] = {};
        auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Rsp);
        for (int i = 0, n = 0; i < 64 && n < 3; ++i)
            if (std::uint32_t r = rva(stack[i])) callers[n++] = r;
        // Never read the watched address here: that fires DR3 again inside the handler (the game
        // closed silently, twice). "head" = an enemy update/AI run with the swap is not in progress.
        bool head = !g_swapActive;
        LONG count = g_readCount;
        for (LONG i = 0; i < count; ++i) {
            ReadHit& r = g_reads[i];
            if (r.rip == rip && r.caller[0] == callers[0] && r.caller[1] == callers[1]) {
                ++(head ? r.head : r.other);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        if (count < 96) {
            g_reads[count] = {rip, {callers[0], callers[1], callers[2]}, head ? 1u : 0u, head ? 0u : 1u};
            InterlockedIncrement(&g_readCount);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    std::uintptr_t address = (&c->Dr0)[field];
    c->Dr6 = 0;
    std::uintptr_t exe = ExeBase();
    auto rva = [exe](std::uintptr_t v) -> std::uint32_t {
        return v > exe && v < exe + 0x579000 ? static_cast<std::uint32_t>(v - exe) : 0;
    };
    std::uint32_t rip = rva(c->Rip);
    std::uint32_t callers[3] = {};
    auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Rsp);
    for (int i = 0, n = 0; i < 64 && n < 3; ++i)
        if (std::uint32_t r = rva(stack[i])) callers[n++] = r;
    std::uint32_t raw = *reinterpret_cast<const std::uint32_t*>(address);
    int cls = ValueClass(raw);
    LONG count = g_hitCount;
    for (LONG i = 0; i < count; ++i) {
        WriteHit& h = g_hits[i];
        if (h.field == field && h.rip == rip && h.caller[0] == callers[0]) {
            ++h.count;
            ++h.values[cls];
            if (cls == 3) h.lastOther = Decode(raw);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (count < 32) {
        WriteHit& h = g_hits[count];
        h = {static_cast<std::uint8_t>(field), rip, {callers[0], callers[1], callers[2]}, 1, {}, 0};
        ++h.values[cls];
        if (cls == 3) h.lastOther = Decode(raw);
        InterlockedIncrement(&g_hitCount);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI ArmThread(LPVOID target) {
    HANDLE thread = static_cast<HANDLE>(target);
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) return 1;
    CONTEXT c {};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &c)) {
        DWORD64* dr = &c.Dr0;
        c.Dr7 = 0;
        for (int i = 0; i < 4; ++i) {
            dr[i] = g_armBase && i < g_offsetCount ? g_armBase + g_offsets[i] : 0;
            // Li enable, RWi = 01 (write), LENi = 11 (4 bytes).
            if (dr[i]) c.Dr7 |= (1ull << (2 * i)) | (1ull << (16 + 4 * i)) | (3ull << (18 + 4 * i));
        }
        if (g_readWatch && g_readArmed) {  // DR3: RW = 11 (read or write), LEN = 10 (8 bytes)
            c.Dr3 = g_readWatch;
            c.Dr7 |= (1ull << 6) | (3ull << 28) | (2ull << 30);
        }
        c.Dr6 = 0;
        SetThreadContext(thread, &c);
        CONTEXT check {};
        check.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(thread, &check);
        g_readBackDr7 = check.Dr7;
    }
    ResumeThread(thread);
    return 0;
}

// Must be called on the game thread; waits for the helper.
void Arm(std::uintptr_t enemy) {
    g_armBase = enemy;
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, 0);
    HANDLE helper = CreateThread(nullptr, 0, ArmThread, self, 0, nullptr);
    if (helper) {
        WaitForSingleObject(helper, 1000);
        CloseHandle(helper);
    }
    CloseHandle(self);
}

// What is this "other" value? Logs its location and, if it looks like an
// actor, its object-table model name and team.
void DescribeOther(std::uintptr_t v) {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + kSoraPtr);
    char model[33] = "?";
    std::uint32_t team = 0xFFFFFFFF;
    __try {
        auto row = *reinterpret_cast<const std::uintptr_t*>(v + 0x918);
        if (row > exe && row < exe + 0x3000000) std::memcpy(model, reinterpret_cast<const void*>(row + 8), 32);
        team = *reinterpret_cast<const std::uint32_t*>(v + 0x4DC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    model[32] = 0;
    Log("      last other value 0x%llX (exe%+lld, Sora%+lld): model \"%s\", team %d",
        static_cast<unsigned long long>(v), static_cast<long long>(v - exe), static_cast<long long>(v - sora), model,
        static_cast<int>(team));
}

}  // namespace

void WatchInit() {
    char buf[64];
    if (EnvStr("KH2COOP_READWATCH", buf, sizeof(buf))) {
        if (std::uintptr_t off = std::strtoull(buf, nullptr, 16)) {
            g_readWatch = ExeBase() + off;
            if (!g_veh) g_veh = AddVectoredExceptionHandler(1, OnWatchException);
            Log("read watch: exe+0x%llX (8 bytes, DR3), armed on the first frame",
                static_cast<unsigned long long>(off));
        }
    }
    if (!EnvStr("KH2COOP_WATCH", buf, sizeof(buf))) return;
    char* p = buf;
    while (*p && g_offsetCount < 4) {
        std::uintptr_t off = std::strtoull(p, &p, 16);
        if (off) g_offsets[g_offsetCount++] = off;
        while (*p == ',' || *p == ' ') ++p;
    }
    if (!g_offsetCount) return;
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, OnWatchException);
    Log("write watch: %d enemy field(s) from +0x%llX (first enemy updated, then the next when it goes away)",
        g_offsetCount, static_cast<unsigned long long>(g_offsets[0]));
}

void WatchOnEnemyUpdate(std::uintptr_t actor) {
    if (!g_offsetCount) return;
    if (!g_gameThread) g_gameThread = GetCurrentThreadId();
    if (GetCurrentThreadId() != g_gameThread) return;
    if (actor == g_enemy) {
        g_enemySeen = g_frame;
        return;
    }
    if (g_enemy && g_frame - g_enemySeen < kActorUpdateAge) return;
    g_enemy = actor;
    g_enemySeen = g_frame;
    Arm(actor);
    Log("write watch: armed on enemy exe+0x%llX (thread %lu, DR7 read back 0x%llX)",
        static_cast<unsigned long long>(actor - ExeBase()), GetCurrentThreadId(),
        static_cast<unsigned long long>(g_readBackDr7));
}

void LogReads() {
    LONG count = g_readCount;
    Log("read watch: readers of exe+0x%llX so far (outside / inside a copy-as-player swap):",
        static_cast<unsigned long long>(g_readWatch - ExeBase()));
    bool used[96] = {};
    for (int n = 0; n < 60; ++n) {
        int best = -1;
        for (LONG i = 0; i < count; ++i)
            if (!used[i] && (best < 0 || g_reads[i].head + g_reads[i].other > g_reads[best].head + g_reads[best].other))
                best = static_cast<int>(i);
        if (best < 0) break;
        used[best] = true;
        const ReadHit& r = g_reads[best];
        Log("  after exe+0x%X, from exe+0x%X <- 0x%X <- 0x%X: %u / %u", r.rip, r.caller[0], r.caller[1], r.caller[2],
            r.head, r.other);
    }
}

void WatchFrame() {
    if (g_readWatch) {
        if (!g_gameThread) g_gameThread = GetCurrentThreadId();
        static std::uint64_t frames = 0, inFieldFrames = 0;
        ++frames;
        // Only while in a room for 5 s (armed during the save load, the game closed silently).
        bool inField = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + 0x9BA8D0) != 0;
        inFieldFrames = inField ? inFieldFrames + 1 : 0;
        bool want = inFieldFrames >= 300;
        if (want != g_readArmed && GetCurrentThreadId() == g_gameThread && !g_offsetCount) {
            g_readArmed = want;
            Arm(0);
            Log("read watch: %s (thread %lu, DR7 read back 0x%llX)", want ? "armed" : "disarmed for a load",
                GetCurrentThreadId(), static_cast<unsigned long long>(g_readBackDr7));
        }
        if (frames % 600 == 0 && g_readCount) LogReads();
    }
    if (!g_offsetCount) return;
    ++g_frame;
    // Disarm once the watched enemy is gone, so a reused slot doesn't keep firing
    // (re-armed on the next enemy update). Only possible from the game thread;
    // OnFrame runs there (Panacea's frame hook).
    if (g_enemy && g_frame - g_enemySeen >= kActorUpdateAge && GetCurrentThreadId() == g_gameThread) {
        Arm(0);
        Log("write watch: enemy exe+0x%llX gone, disarmed", static_cast<unsigned long long>(g_enemy - ExeBase()));
        g_enemy = 0;
    }
    if (g_frame % 600 != 0 || g_hitCount == 0) return;
    Log("write watch: writers so far (value written: 0 / Sora / companion / other / Sora copy):");
    for (LONG i = 0; i < g_hitCount; ++i) {
        const WriteHit& h = g_hits[i];
        Log("  enemy+0x%llX after exe+0x%X, called from exe+0x%X <- 0x%X <- 0x%X: x%u (%u / %u / %u / %u / %u)%s",
            static_cast<unsigned long long>(g_offsets[h.field]), h.rip, h.caller[0], h.caller[1], h.caller[2],
            h.count, h.values[0], h.values[1], h.values[2], h.values[3], h.values[4], "");
        if (h.lastOther) DescribeOther(h.lastOther);
    }
}

}  // namespace kh2coop
