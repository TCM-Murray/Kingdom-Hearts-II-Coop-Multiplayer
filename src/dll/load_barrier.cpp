// Load barrier (TODO 5.10, user idea 2026-10-07): both games leave the black screen of a room load
// together, so cutscenes, enemies and bosses start at the same moment. Research stage: only the hold.
//
// The room load is a task, exe+0x152680 (Ghidra), that runs one step per frame. It ends with
//     while (!RoomReady()) wait one frame;      // exe+0x156770 returns the byte at exe+0x7177B8
//     schedule exe+0x152CD0;                    // sets in-field (exe+0x9BA8D0) = 1, starts the room
// (calls at exe+0x1528DF / 0x1528FA, returning to 0x1528E4 / 0x1528FF). Answering "not ready" there keeps
// the game in its own wait loop: the rest of the game keeps running, the room doesn't start yet.
// Watched live (Olympus 06/0A -> 06/06, Hades scene): the event's freeze flag came 8 ms after in-field = 1,
// so holding the load end also holds the cutscene. NOW (exe+0x717008) already holds the destination with
// its final programs the moment the load starts (in-field 0), also when the request said "the save's".
//
// Settings: KH2COOP_LOAD_HOLD_MS=<ms> (experiment, default 0 = off): hold every room load that long
// after the room is ready.

#include <windows.h>

#include <intrin.h>

#include <cstdint>

#include "common.hpp"
#include "load_barrier.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kRoomReady = 0x156770;
constexpr std::uint8_t kRoomReadyBytes[] = {0x0F, 0xB6, 0x05, 0x41, 0x10, 0x5C, 0x00, 0xC3};
constexpr std::uint32_t kLoadWaitReturns[] = {0x1528E4, 0x1528FF};  // the load task's two calls
constexpr std::uintptr_t kNow = 0x717008;

using PFN_RoomReady = std::uint8_t(__fastcall*)();
PFN_RoomReady g_realRoomReady = nullptr;
int g_holdMs = 0;
ULONGLONG g_readySince = 0;  // first "ready" answer of the current load (0 = not ready yet)
std::uint32_t g_holds = 0;

std::uint8_t __fastcall HookRoomReady() {
    std::uint8_t ready = g_realRoomReady();
    auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase());
    if (rva != kLoadWaitReturns[0] && rva != kLoadWaitReturns[1]) return ready;
    if (!ready) {
        g_readySince = 0;
        return ready;
    }
    ULONGLONG now = GetTickCount64();
    if (!g_readySince) {
        g_readySince = now;
        const auto* p = reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
        Log("load barrier: room 0x%02X/0x%02X ready; holding it %d ms (experiment)", p[0], p[1], g_holdMs);
    }
    if (now - g_readySince < static_cast<ULONGLONG>(g_holdMs)) return 0;
    Log("load barrier: released after %llu ms (hold #%u)", static_cast<unsigned long long>(now - g_readySince),
        ++g_holds);
    g_readySince = 0;
    return ready;
}

}  // namespace

void LoadBarrierInit(bool host) {
    (void)host;
    g_holdMs = EnvInt("KH2COOP_LOAD_HOLD_MS", 0);
    if (g_holdMs <= 0) return;
    if (HookFunction(kRoomReady, kRoomReadyBytes, sizeof(kRoomReadyBytes), reinterpret_cast<void*>(HookRoomReady),
                     reinterpret_cast<void**>(&g_realRoomReady), "room ready (load barrier experiment)"))
        Log("load barrier: experiment on: every room load is held %d ms on the black screen", g_holdMs);
}

}  // namespace kh2coop
