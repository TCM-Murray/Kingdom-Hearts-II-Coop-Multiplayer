// A bigger effect pool (TODO 1.59, user decision 2026-10-13, session 33: 4 MB instead of 512 KB) and
// a log of the most each room uses of it (kept in docs/EFFECT_POOL_PEAKS.md by tools/pool_peaks.py).
//
// The game keeps every effect's particles in one pool of 512 KB (exe+0xB60808; VERIFIED_OFFSETS
// leads "Where the effect pool is made" and "Friend crash in Data Roxas"). When it's full the
// allocator exe+0x4AF240 returns null, and particle spawns write through that without a check:
// crashes at exe+0x4B00DA (host, Cerberus get-up 2026-10-06; Final Xemnas with the extra copy
// 2026-10-12) and exe+0x4D3A84 (friend, Data Roxas 2026-10-13). Two players' effects in one game
// fill it faster than the game was made for.
//   - The pool is made again at room loads: exe+0x2C2480 calls exe+0x4AF380(&pool, buffer, size)
//     with the game's buffer and 0x80000. The hook passes our buffer and size instead (FX_POOL_KB,
//     default 4096). The buffer is allocated once, used for every load after and never freed
//     (effects of the room before could still point into it during a load). The game's own buffer
//     stays as it was (allocated and freed by the game), just unused.
//   - Effect memory is addressed by handles: 0x80000000 | slot << 25 | address & 0x1FFFFFF, the
//     slot's 32 MB base kept in a 64-slot table (exe+0x2B0D720) that the encoder exe+0x4AD390 fills
//     by itself for new addresses and that is never cleared. Our buffer takes one slot (two if it
//     crosses a 32 MB line); the count is logged.
//   - Only exe+0x2C3020 (a debug text) reads the size the game asked for (exe+0xB60288); left as is.
//   - A co-op hook: off while co-op is off, so the game's own pool comes back at the next load.
// Peak log (FX_POOL_LOG, default on; with either pool): each frame in a room (in-field
// exe+0x9BA8D0 != 0), the pool's bytes in use (+0x18, counting each block's 16-byte header) are
// kept as the room's peak; the room is the world, room and map/battle/event programs of NOW
// (exe+0x717008). The line
//     effect pool: room 0xWW/0xRR programs M/B/E: peak N B (K KB) of S KB, <when>
// is logged when the room is left, every 10 s while its peak grows, and at once when the peak first
// goes past 512 KB (more than the game's own pool holds).

#include <windows.h>

#include <cstdint>

#include "common.hpp"
#include "fx_pool.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kPoolInit = 0x4AF380;  // (pool, buffer, size): the pool as one free block
constexpr std::uint8_t kPoolInitBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48,
                                           0x83, 0xEC, 0x20, 0x48, 0x89, 0x51, 0x08};
constexpr std::uintptr_t kEffectPool = 0xB60808;  // +8 start, +0x10 end (start + size - 0x10), +0x18 u32 in use
constexpr std::uintptr_t kPoolStart = 0x8, kPoolEnd = 0x10, kPoolInUse = 0x18;
constexpr std::uintptr_t kBaseTable = 0x2B0D720;  // u64[64]: the handles' 32 MB bases, -1 = free
constexpr int kBaseSlots = 64;
constexpr std::uintptr_t kNow = 0x717008;         // u8 world, u8 room, u8 door, pad, u16 map, btl, evt
constexpr std::uintptr_t kInField = 0x9BA8D0;     // u8: 0 while a room loads
constexpr std::uint32_t kGamePool = 0x80000;      // the game's own size
constexpr int kMaxKb = 16384;
constexpr int kLogEvery = 600;                    // frames (10 s)

using PFN_PoolInit = void(__fastcall*)(void* pool, void* buffer, std::uint32_t size);
PFN_PoolInit g_realPoolInit = nullptr;
std::uint32_t g_size = 0;  // our pool's size; 0 = the game's own
void* g_buffer = nullptr;
bool g_allocFailed = false;
std::uint32_t g_ours = 0, g_left = 0;

bool g_log = true;
bool g_inRoom = false;
std::uint8_t g_world = 0, g_room = 0;
std::uint16_t g_programs[3] = {};
std::uint32_t g_peak = 0, g_peakLogged = 0, g_poolSize = 0;
bool g_pastGame = false;
int g_sinceLog = 0;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

int UsedSlots() {
    int n = 0;
    while (n < kBaseSlots && Read<std::uint64_t>(kBaseTable + 8 * n) != ~0ull) ++n;
    return n;
}

std::uint32_t PoolSize() {
    auto start = Read<std::uintptr_t>(kEffectPool + kPoolStart), end = Read<std::uintptr_t>(kEffectPool + kPoolEnd);
    return start && end > start ? static_cast<std::uint32_t>(end - start + 0x10) : 0;
}

void __fastcall HookPoolInit(void* pool, void* buffer, std::uint32_t size) {
    if (pool == reinterpret_cast<void*>(ExeBase() + kEffectPool) && size < g_size && !g_allocFailed) {
        if (!g_buffer) g_buffer = VirtualAlloc(nullptr, g_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_buffer) {
            g_realPoolInit(pool, g_buffer, g_size);
            if (++g_ours <= 3 || g_ours % 100 == 0)
                Log("effect pool: made with our %u KB in place of the game's %u KB (load %u; handle bases in use %d of %d)",
                    g_size / 1024, size / 1024, g_ours, UsedSlots(), kBaseSlots);
            return;
        }
        g_allocFailed = true;
        Log("effect pool: can't allocate %u KB (error %lu); the game keeps its own %u KB", g_size / 1024, GetLastError(),
            size / 1024);
    } else if (++g_left <= 3) {
        Log("effect pool: setup of exe+0x%llX with %u KB left as the game has it",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pool) - ExeBase()), size / 1024);
    }
    g_realPoolInit(pool, buffer, size);
}

void LogPeak(const char* when) {
    Log("effect pool: room 0x%02X/0x%02X programs %u/%u/%u: peak %u B (%u KB) of %u KB, %s", g_world, g_room,
        g_programs[0], g_programs[1], g_programs[2], g_peak, (g_peak + 1023) / 1024, g_poolSize / 1024, when);
    g_peakLogged = g_peak;
    g_sinceLog = 0;
}

}  // namespace

void FxPoolInit() {
    g_log = EnvInt("KH2COOP_FX_POOL_LOG", 1) == 1;
    int kb = EnvInt("KH2COOP_FX_POOL_KB", 4096);
    if (kb > kMaxKb) kb = kMaxKb;
    if (kb * 1024 > static_cast<int>(kGamePool)) {
        g_size = static_cast<std::uint32_t>(kb) * 1024;
        if (!HookFunction(kPoolInit, kPoolInitBytes, sizeof(kPoolInitBytes), reinterpret_cast<void*>(HookPoolInit),
                          reinterpret_cast<void**>(&g_realPoolInit), "effect pool setup (bigger pool)"))
            g_size = 0;
    }
    if (g_size)
        Log("effect pool: %u KB in place of the game's 512 KB from the next room load in co-op%s", g_size / 1024,
            g_log ? "; each room's peak logged" : "");
    else
        Log("effect pool: the game's own 512 KB%s", g_log ? "; each room's peak logged" : "");
}

void FxPoolFrame() {
    if (!g_log) return;
    auto now = reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
    auto programs = reinterpret_cast<const volatile std::uint16_t*>(now + 4);
    if (!Read<std::uint8_t>(kInField) || now[0] == 0xFF) {  // a load or the title
        if (g_inRoom && g_peak) LogPeak("room left");
        g_inRoom = false;
        return;
    }
    if (g_inRoom && (now[0] != g_world || now[1] != g_room || programs[0] != g_programs[0] ||
                     programs[1] != g_programs[1] || programs[2] != g_programs[2])) {
        if (g_peak) LogPeak("programs changed");
        g_inRoom = false;
    }
    if (!g_inRoom) {
        g_inRoom = true;
        g_world = now[0];
        g_room = now[1];
        for (int i = 0; i < 3; ++i) g_programs[i] = programs[i];
        g_peak = g_peakLogged = g_poolSize = 0;
        g_pastGame = false;
        g_sinceLog = 0;
    }
    std::uint32_t size = PoolSize();
    if (size > g_poolSize) g_poolSize = size;
    std::uint32_t used = Read<std::uint32_t>(kEffectPool + kPoolInUse);
    if (used > g_peak) g_peak = used;
    if (!g_pastGame && g_peak > kGamePool) {
        g_pastGame = true;
        LogPeak("past 512 KB, more than the game's own pool holds");
    } else if (++g_sinceLog >= kLogEvery && g_peak > g_peakLogged) {
        LogPeak("so far");
    }
}

bool FxPoolOwns(std::uintptr_t address, std::uint32_t bytes) {
    auto start = reinterpret_cast<std::uintptr_t>(g_buffer);  // allocated once, never freed
    return start && address >= start && address + bytes <= start + g_size;
}

}  // namespace kh2coop
