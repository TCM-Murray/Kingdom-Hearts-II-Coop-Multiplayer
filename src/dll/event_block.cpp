// The cutscene system's work block served from a buffer of ours (the review's 5.2.6 step 4, the advisor's call and
// rules, 2026-10-26, session 45). An event played mid-room (a summon's entry, a boss scene) takes its work block from
// the file heap's top: exe+0x39D300 lowers the heap's end, wipes the files above it at once (L150) and our look's
// files in negative groups with them (F346, F353). Served from our buffer, the heap's end never moves for an event.
//   - The block: taken by the cutscene's setup through exe+0x2CCAB0 (the call returns to exe+0x2CDADC), sized by the
//     event script, at most 4 MB (L153), kept at exe+0xB64F88. An event at that cap also takes a 2 MB pool
//     (exe+0x2CF090 / 0x2CF220, the calls returning to exe+0x2CF0E9 / 0x2CF28A, kept at exe+0xB655F0). Both come
//     back through exe+0x39D400 at the event task's end (exe+0x2C85B0 -> 0x2CE470: the pool first, then the
//     block), with the stored pointer (L154). One script slot: no second event takes a block while one is out.
//   - Rule 1, the take: from one of those call sites, co-op on, our buffer free and big enough, the order kept (the
//     block before the pool, the pool only on our block): cleared as the game clears its block, marked out, logged.
//     Anything else from those sites is the game's path with a BUG: line (today's behaviour: our files wiped).
//   - Rule 2, the give-back: our pointer never reaches the game's exe+0x39D400 (it would pop another block and raise
//     the end). Its sweep is run on our buffer instead, as exe+0x39D400 runs it: exe+0x12EF30(block, block + size,
//     1) ends every resource object whose data lies in it (a block starting "BAR": exe+0x3A6A10(block, 1)). Then the
//     resource list is walked again: an object still in our buffer is a BUG: line and that buffer isn't used again.
//   - Rule 3, the balance: each take and give-back of ours logs the block stack's index (exe+0x749750) and the
//     heap's end, which must stay as they were.
//   - The buffers: one allocation (block KH2COOP_EVENT_BLOCK_KB, default 4096, then the 2 MB pool), committed once
//     and never freed (as the effect pool's), on a 32 MB line near the exe (one handle base slot; a 32-bit store
//     would still reach it). KH2COOP_EVENT_BLOCK_GUARD=1 (tests): no access between events, so a late reader faults
//     with a stack instead of reading stale data. Not in KnownArea (not always readable).
//   - The takes and give-backs of 1 MB or more not ours are logged with their callers (KH2COOP_HEAP_TOP_LOG).

#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "event_block.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kTopTake = 0x39D300;  // (size) -> block: lowers the heap's end by size (L150)
constexpr std::uint8_t kTopTakeBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kTopGive = 0x39D400;  // (block): the sweep, then raises the end by the stack top's size
constexpr std::uint8_t kTopGiveBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kHeapEnd = 0x749740;   // u64: the file heap's end
constexpr std::uintptr_t kTopCount = 0x749750;  // s32: the block stack's index (counts down per take)
constexpr std::uintptr_t kTopSizes = 0x29FB400; // u64[]: the sizes taken, by index
constexpr std::uintptr_t kSweep = 0x12EF30;     // (low, high, 1): ends the resource objects whose data lies there
constexpr std::uintptr_t kSweepBar = 0x3A6A10;  // (block, 1): the same for a block starting "BAR"
constexpr std::uintptr_t kResList = 0x8AEF30;   // u64 -> list: +8 first; an object: +8 next, +0x640 u32 size, +0x658 data
constexpr std::uintptr_t kBlockSite = 0x2CDADC;                       // exe+0x2CCAB0's take returns here
constexpr std::uintptr_t kPoolSite1 = 0x2CF0E9, kPoolSite2 = 0x2CF28A;  // exe+0x2CF090's and 0x2CF220's
constexpr std::uint64_t kPoolSize = 0x200000;
constexpr std::uintptr_t kLine = 0x2000000;  // a handle base slot's 32 MB

using PFN_TopTake = std::uintptr_t(__fastcall*)(std::uint64_t);
using PFN_TopGive = void(__fastcall*)(std::uintptr_t);
PFN_TopTake g_realTake = nullptr;
PFN_TopGive g_realGive = nullptr;

struct Buffer {
    const char* name;
    std::uintptr_t base = 0;
    std::uint64_t cap = 0;
    std::uint64_t handed = 0;  // the size handed out while out
    bool out = false;
    bool spoiled = false;      // an object was left in it: never used again
    std::int32_t index = 0;    // at the take: the stack's index and the heap's end (rule 3)
    std::uintptr_t end = 0;
    unsigned takes = 0;
};
Buffer g_block{"work block"}, g_pool{"2 MB pool"};
bool g_on = false, g_guard = false, g_topLog = true;

template <typename T>
T At(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

template <typename T>
T Fn(std::uintptr_t rva) {
    return reinterpret_cast<T>(ExeBase() + rva);
}

std::uintptr_t ExeEnd() {
    std::uintptr_t exe = ExeBase();
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + reinterpret_cast<const IMAGE_DOS_HEADER*>(exe)->e_lfanew);
    return exe + nt->OptionalHeader.SizeOfImage;
}

// Committed memory starting on a 32 MB line above the exe (inside 2 GB of it), else anywhere.
std::uintptr_t AllocNear(std::uint64_t bytes) {
    std::uintptr_t line = (ExeEnd() + kLine - 1) & ~(kLine - 1);
    for (int i = 0; i < 60; ++i, line += kLine)
        if (void* p = VirtualAlloc(reinterpret_cast<void*>(line), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
            return reinterpret_cast<std::uintptr_t>(p);
    return reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
}

void Protect(Buffer& b, DWORD how) {
    DWORD old = 0;
    if (g_guard && b.base) VirtualProtect(reinterpret_cast<void*>(b.base), b.cap, how, &old);
}

// The resource objects whose data overlaps [low, high) (as exe+0x12EF30 finds them); *seen = the objects walked.
int LiveIn(std::uintptr_t low, std::uintptr_t high, int* seen) {
    int n = 0, hits = 0;
    __try {
        std::uintptr_t list = At<std::uintptr_t>(kResList);
        for (std::uintptr_t o = list ? *reinterpret_cast<std::uintptr_t*>(list + 8) : 0; o && n < 100000;
             o = *reinterpret_cast<std::uintptr_t*>(o + 8), ++n) {
            std::uintptr_t data = *reinterpret_cast<std::uintptr_t*>(o + 0x658);
            std::uintptr_t end = data + *reinterpret_cast<std::uint32_t*>(o + 0x640);
            if (data < high && end > low) {
                if (++hits <= 3)
                    Log("BUG: event block: resource object %p (data %p, %llu bytes) still in our buffer after its "
                        "give-back", reinterpret_cast<void*>(o), reinterpret_cast<void*>(data),
                        static_cast<unsigned long long>(end - data));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("event block: exception walking the resource list (after %d objects)", n);
    }
    *seen = n;
    return hits;
}

void LogTop(const char* what, std::uint64_t size, std::uintptr_t block) {
    char chain[200];
    CallerChain(chain, sizeof(chain), 1);
    Log("heap top: %s %#llx at %p (the end now %p), from %s", what, static_cast<unsigned long long>(size),
        reinterpret_cast<void*>(block), reinterpret_cast<void*>(At<std::uintptr_t>(kHeapEnd)), chain);
}

std::uintptr_t __fastcall HookTake(std::uint64_t size) {
    std::uintptr_t site = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase();
    Buffer* b = site == kBlockSite ? &g_block : site == kPoolSite1 || site == kPoolSite2 ? &g_pool : nullptr;
    if (b && g_on && CoopActive()) {
        std::uint64_t rounded = (size + 0x7F) & ~0x7Full;
        const char* why = !b->base ? "no buffer"
                          : b->spoiled ? "the buffer was spoiled by an object left in it"
                          : b->out ? "the buffer is already out"
                          : rounded > b->cap ? "the size is over the buffer"
                          : b == &g_block && g_pool.out ? "the pool is out (the block must come first)"
                          : b == &g_pool && !g_block.out ? "the work block isn't ours (the pool comes after it)"
                          : b == &g_pool && size != kPoolSize ? "not the pool's 2 MB"
                          : nullptr;
        if (!why) {
            Protect(*b, PAGE_READWRITE);
            std::memset(reinterpret_cast<void*>(b->base), 0, rounded);  // the game hands out a cleared block
            b->out = true;
            b->handed = rounded;
            b->index = At<std::int32_t>(kTopCount);
            b->end = At<std::uintptr_t>(kHeapEnd);
            ++b->takes;
            Log("event block: the event's %s (%#llx) served from ours at %p (take %u, from exe+%#llx); stack index %d, "
                "the heap's end %p", b->name, static_cast<unsigned long long>(rounded), reinterpret_cast<void*>(b->base),
                b->takes, static_cast<unsigned long long>(site), b->index, reinterpret_cast<void*>(b->end));
            return b->base;
        }
        Log("BUG: event block: the event's %s (%#llx, from exe+%#llx) left to the game: %s; it lowers the heap's end "
            "(our files there are wiped)", b->name, static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(site), why);
    }
    std::uintptr_t block = g_realTake(size);
    if (g_topLog && size >= 0x100000) LogTop("took", size, block);
    return block;
}

void __fastcall HookGive(std::uintptr_t block) {
    Buffer* b = block && block == g_block.base ? &g_block : block && block == g_pool.base ? &g_pool : nullptr;
    if (!b) {
        std::uint64_t size = 0;
        __try {
            size = At<std::uint64_t>(kTopSizes + 8ull * At<std::int32_t>(kTopCount));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        bool oursOut = g_block.out || g_pool.out;
        g_realGive(block);
        if ((g_topLog && size >= 0x100000) || oursOut)
            LogTop(oursOut ? "gave back (while a buffer of ours is out)" : "gave back", size, block);
        return;
    }
    // Our pointer never goes to the game's exe+0x39D400, whatever the state (it would pop another block).
    if (!b->out) {
        Log("BUG: event block: our %s given back while not out (from exe+%#llx): ignored", b->name,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
        return;
    }
    if (b == &g_block && g_pool.out)
        Log("BUG: event block: the work block given back while the pool is out (the pool comes back first)");
    std::int32_t index = At<std::int32_t>(kTopCount);
    std::uintptr_t end = At<std::uintptr_t>(kHeapEnd);
    __try {
        if (!std::memcmp(reinterpret_cast<const void*>(b->base), "BAR", 3))
            Fn<void(__fastcall*)(std::uintptr_t, int)>(kSweepBar)(b->base, 1);
        else
            Fn<void(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uint8_t)>(kSweep)(b->base, b->base + b->handed, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("BUG: event block: exception sweeping our %s", b->name);
    }
    int seen = 0;
    int left = LiveIn(b->base, b->base + b->cap, &seen);
    if (left) b->spoiled = true;
    b->out = false;
    Protect(*b, PAGE_NOACCESS);
    bool balanced = index == b->index && end == b->end;
    Log("event block: our %s given back (from exe+%#llx): swept, %d of %d resource objects left in it%s; stack index "
        "%d, the heap's end %p (%s)", b->name,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()), left, seen,
        left ? " (BUG: not used again)" : "", index, reinterpret_cast<void*>(end),
        balanced ? "as at the take" : "BUG: not as at the take");
}

}  // namespace

void EventBlockInit() {
    g_topLog = EnvInt("KH2COOP_HEAP_TOP_LOG", 1) == 1;
    g_guard = EnvInt("KH2COOP_EVENT_BLOCK_GUARD", 0) == 1;
    g_on = EnvInt("KH2COOP_EVENT_BLOCK", 1) == 1;
    if (g_on) {
        int kb = EnvInt("KH2COOP_EVENT_BLOCK_KB", 4096);
        if (kb < 64) kb = 64;
        if (kb > 4096) kb = 4096;
        g_block.cap = (static_cast<std::uint64_t>(kb) * 1024 + 0xFFFF) & ~0xFFFFull;  // page lines for the guard
        g_pool.cap = kPoolSize;
        std::uintptr_t base = AllocNear(g_block.cap + g_pool.cap);
        if (base) {
            g_block.base = base;
            g_pool.base = base + g_block.cap;
            Protect(g_block, PAGE_NOACCESS);
            Protect(g_pool, PAGE_NOACCESS);
        }
        Log("event block: %s: work block %llu KB at %p, pool %llu KB at %p (exe+%#llx away%s)%s",
            base ? "the cutscene's blocks come from ours in co-op" : "can't allocate, the game's own blocks",
            static_cast<unsigned long long>(g_block.cap / 1024), reinterpret_cast<void*>(g_block.base),
            static_cast<unsigned long long>(g_pool.cap / 1024), reinterpret_cast<void*>(g_pool.base),
            static_cast<unsigned long long>(base - ExeBase()),
            base && (base / kLine) == ((base + g_block.cap + g_pool.cap - 1) / kLine) ? ", one 32 MB line" : "",
            g_guard ? "; no access between events (KH2COOP_EVENT_BLOCK_GUARD)" : "");
        if (!base) g_on = false;
    } else {
        Log("event block: off (KH2COOP_EVENT_BLOCK=0): the cutscene's blocks come from the heap");
    }
    // Always on: our pointer's give-back must be caught even if co-op went off meanwhile; the take checks CoopActive.
    if (!HookFunctionAlways(kTopTake, kTopTakeBytes, sizeof(kTopTakeBytes), reinterpret_cast<void*>(HookTake),
                            reinterpret_cast<void**>(&g_realTake), "heap top take (event block)") ||
        !HookFunctionAlways(kTopGive, kTopGiveBytes, sizeof(kTopGiveBytes), reinterpret_cast<void*>(HookGive),
                            reinterpret_cast<void**>(&g_realGive), "heap top give-back (event block)"))
        g_on = false;
}

}  // namespace kh2coop
