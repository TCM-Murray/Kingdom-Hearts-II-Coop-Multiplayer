// A boss's move that both players play at once (TODO 1.20; you, 2026-10-14: Final Xemnas's desperation move shared,
// "both games play it"). With one brain only the host's Xemnas thinks, so only the host's game played the move (bench
// 2026-10-14: capture of Sora and Riku, the laser barrage, 'Reflect', ~88 s from scene 're_b_ex170-60' to the end of
// '-72'; VERIFIED_OFFSETS row 'Final Xemnas's desperation move'). An exception to the one-brain rule, for this move only:
//   Host:   remembers the acts its Xemnas starts (trap_obj_act_start, 1/8: the act is a name, looked up in the actor's
//           act table by exe+0x3CA700). When the move's scene starts, it sends the first act started on him in that
//           frame (the move's own act; the one a frame before is 'mode_battle_boss', his normal fight: bench
//           2026-10-14 12:16) with world sync's number for that Xemnas ("KH2X", 3 times).
//   Friend: its Xemnas bound to that one gets its own brain back (world_sync.cpp: no brain skip, no pose or motion
//           from the host) and starts the same act by name (exe+0x3CAE60, what 1/8 calls), so its own script plays
//           the move on our Sora and our Riku. It goes back to the host's brain when its script lets go of our Sora
//           (trap_obj_hook_stop, 1/214, as at the end of '-72'), after kMaxFrames, or when the room changes.
// KH2COOP_SHARED_MOVE=0 turns it off.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "shared_move.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x5832484B;  // "KH2X"
constexpr std::uint16_t kVersion = 1;
constexpr std::uintptr_t kListHead = 0x2A171C8;  // our Sora
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kObjRow = 0x918, kHook = 0xD48;
constexpr std::uintptr_t kActControl = 0x390;    // actor + 0x390: its act controller
constexpr std::uintptr_t kActStart = 0x3CAE60;   // (actor + 0x390, act name): trap_obj_act_start's work
constexpr std::uint64_t kMaxFrames = 60 * 180;   // the move took ~88 s on the bench
using PFN_ActStart = void(__fastcall*)(std::uintptr_t, const char*);

// The moves both players play: the scene that starts it, the boss's model.
struct Move {
    const char* scene;
    const char* model;
};
const Move kMoves[] = {{"re_b_ex170-60", "B_EX170_LAST"}};  // Final Xemnas's desperation move

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t id;
    std::uint16_t netId, instance;  // the host's Xemnas (world sync's number and room instance)
    char act[32];
};
#pragma pack(pop)

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;

// Host: the last acts started on each boss of a shared move.
struct Act {
    std::uintptr_t actor;
    std::uint64_t frame;
    char name[32];
};
Act g_acts[8];
int g_actNext = 0;
std::uint32_t g_sendId = 0;
std::uint32_t g_actLogs = 0;  // research: act names seen (first kActLogs)
constexpr std::uint32_t kActLogs = 60;

// Friend.
std::uint32_t g_lastId = 0;
Packet g_inbox {};
bool g_pending = false;
std::uintptr_t g_own = 0;          // our Xemnas running its own brain, or 0
std::uint64_t g_ownSince = 0;
std::uint32_t g_ownRoom = 0;
bool g_endPending = false;
char g_ownAct[32] = "";

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + kHandleBases + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

// A script's actor argument: a handle to an object whose +4 is the actor's handle.
std::uintptr_t ActorArg(std::uint64_t arg) {
    __try {
        std::uintptr_t x = Decode(static_cast<std::uint32_t>(arg));
        return x ? Decode(Read<std::uint32_t>(x + 4)) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool IsModel(std::uintptr_t a, const char* model) {
    __try {
        std::uintptr_t row = a ? Read<std::uintptr_t>(a + kObjRow) : 0;
        if (row <= ExeBase() || row >= ExeBase() + 0x3000000) return false;
        return std::strncmp(reinterpret_cast<const char*>(row + 8), model, 32) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InMoves(std::uintptr_t a) {
    for (const Move& m : kMoves)
        if (IsModel(a, m.model)) return true;
    return false;
}

std::uint32_t RoomNow() { return Read<std::uint16_t>(ExeBase() + 0x717008) | 0x10000; }

// Host: an act started on a boss of a shared move (1/8 trap_obj_act_start(actor, act name)).
void NoteAct(const std::uint64_t* args) {
    std::uintptr_t actor = ActorArg(args[0]);
    if (!actor || !InMoves(actor)) return;
    Act& a = g_acts[g_actNext++ % 8];
    a.actor = actor;
    a.frame = g_frame;
    a.name[0] = 0;
    __try {
        auto name = reinterpret_cast<const char*>(Decode(static_cast<std::uint32_t>(args[1])));
        if (name) std::snprintf(a.name, sizeof(a.name), "%s", name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        a.name[0] = 0;
    }
    if (g_actLogs < kActLogs) {
        ++g_actLogs;
        Log("shared move: act '%s' started on %s (exe+0x%llX) by %s", a.name, kMoves[0].model,
            static_cast<unsigned long long>(actor - ExeBase()),
            WorldCensusScriptActor() == actor ? "its own script" : "another script");
    }
}

void SendStart(const Move& m) {
    const Act* pick = nullptr;  // the first act started on that boss in this frame (the ring holds them in order)
    for (int k = 0; k < 8; ++k) {
        const Act& a = g_acts[(g_actNext + k) % 8];
        if (a.actor && a.name[0] && a.frame == g_frame && IsModel(a.actor, m.model)) {
            pick = &a;
            break;
        }
    }
    if (!pick) {
        Log("shared move: scene '%s' started, but no act of %s seen in this frame: not sent", m.scene, m.model);
        return;
    }
    std::uint16_t instance = 0;
    std::uint16_t netId = WorldSyncNetIdOf(pick->actor, &instance);
    if (!netId) {
        Log("shared move: scene '%s': %s isn't tracked by world sync: not sent", m.scene, m.model);
        return;
    }
    Packet p {kMagic, kVersion, AvatarLinkBuild(), ++g_sendId, netId, instance, {}};
    std::snprintf(p.act, sizeof(p.act), "%s", pick->name);
    for (int i = 0; i < 3; ++i) AvatarLinkSendRaw(&p, sizeof(p));
    Log("shared move: scene '%s' started: the friend's %s (host#%u) plays it too, from act '%s'", m.scene, m.model,
        netId, p.act);
}

// Friend: our Xemnas bound to the host's starts the act with its own brain.
void Start() {
    g_pending = false;
    std::uintptr_t actor = WorldSyncBoundActor(g_inbox.netId, g_inbox.instance);
    if (!actor) {
        Log("shared move: the host's move (host#%u, act '%s') not played: no enemy of ours bound to it",
            g_inbox.netId, g_inbox.act);
        return;
    }
    std::snprintf(g_ownAct, sizeof(g_ownAct), "%s", g_inbox.act);
    g_own = actor;  // from now on its brain runs (WorldSyncSkipsBrain) and world sync leaves it alone
    g_ownSince = g_frame;
    g_ownRoom = RoomNow();
    g_endPending = false;
    bool ok = true;
    __try {
        reinterpret_cast<PFN_ActStart>(ExeBase() + kActStart)(actor + kActControl, g_ownAct);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    Log("shared move: our %s (bound to host#%u) runs its own brain and starts act '%s'%s", "B_EX170_LAST",
        g_inbox.netId, g_ownAct, ok ? "" : ": the act start faulted, back to the host's brain");
    if (!ok) g_own = 0;
}

void End(const char* why) {
    Log("shared move: our Xemnas back on the host's brain after %llu frames (%s)",
        static_cast<unsigned long long>(g_frame - g_ownSince), why);
    g_own = 0;
    g_endPending = false;
}

}  // namespace

void SharedMoveInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_SHARED_MOVE", 1) == 1;
    if (g_on)
        Log("shared move: on (%s)", host ? "Final Xemnas's desperation move is sent to the friend's game"
                                         : "the host's Final Xemnas desperation move plays in our game too");
}

void SharedMoveOnCall(int table, int index, const std::uint64_t* args) {
    if (!g_on || table != 1 || !CoopActive()) return;
    if (g_host) {
        if (index == 8) NoteAct(args);
        return;
    }
    // Friend: our Xemnas's script lets go of our Sora (1/214 trap_obj_hook_stop): its move is over.
    if (index == 214 && g_own && WorldCensusScriptActor() == g_own &&
        ActorArg(args[0]) == Read<std::uintptr_t>(ExeBase() + kListHead))
        g_endPending = true;
}

void SharedMoveOnScene(const char* name) {
    if (!g_on || !g_host || !name || !CoopActive()) return;
    for (const Move& m : kMoves)
        if (std::strncmp(name, m.scene, 20) == 0) SendStart(m);
}

void SharedMoveOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.id + 1000 < g_lastId) g_lastId = 0;  // the host's game restarted
    if (p.id <= g_lastId) return;              // each comes 3 times
    g_lastId = p.id;
    p.act[sizeof(p.act) - 1] = 0;
    g_inbox = p;
    g_pending = true;
}

void SharedMoveFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_host) return;
    if (g_pending && !g_own) Start();
    if (!g_own) return;
    if (g_endPending) End("its script let go of our Sora");
    else if (RoomNow() != g_ownRoom) End("room changed");
    else if (g_frame - g_ownSince > kMaxFrames) End("time limit");
}

bool SharedMoveOwnBrain(std::uintptr_t actor) { return g_on && !g_host && actor && actor == g_own; }

}  // namespace kh2coop
