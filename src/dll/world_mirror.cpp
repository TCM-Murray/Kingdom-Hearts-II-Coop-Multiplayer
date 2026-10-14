// World mirror: one brain, the host's (TODO 1.55 step 3, docs/WORLD_MIRROR_RESEARCH.md), for every enemy type, with
// world sync. On by default since session 34 (you, 2026-10-13: "have the one brain AI enabled by default"); the
// switch (ONE_BRAIN), the type list (ONE_BRAIN_TYPES) and two brains were removed in session 35.
//   Friend: an enemy bound to a host enemy (world_sync.cpp) no longer thinks: its brain exe+0x3B4460 (AI
//           script + action script) is skipped (aggro.cpp HookBrain asks WorldSyncSkipsBrain). Its position,
//           motion and HP already come from the host; host enemies without a stat block are bound too.
//   Host:   the script calls that change how an enemy looks (fades, hide/show, parts, stealth, flags, attack
//           patterns, collision, effects) are sent as they happen, in "KH2V" packets (each event in 3 packets).
//   Friend: runs the same call on its enemy bound to the host's (by the host's enemy number, sent with the call),
//           else on its actor at the same address (actors from a room load have the same address in both games,
//           VERIFIED_OFFSETS; enemies from spawn groups don't), if that actor's brain is skipped. Effects the host started
//           are paired with the friend's by the host's effect handle, so their fade-out, loop end or kill follows.
// The script calls come through the call log's thunks (world_census.cpp), which go in when this is on.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x5632484B;  // "KH2V"
constexpr std::uint16_t kVersion = 2;  // v2: the host's enemy number and room instance
constexpr std::uintptr_t kListHead = 0x2A171C8, kNextActor = 0xA90, kObjRow = 0x918, kTeam = 0x4DC;
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;  // (object) -> handle
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // "@ADR"
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
using PFN_Trap = void(__fastcall*)(std::uint64_t* args);

// The calls we mirror. Actor: the first argument is the actor, the rest are numbers. EffectStart: same, returns
// an effect handle. EffectEnd: the first argument is an effect handle. (Decompiled: none of them reads the
// running script, so they can run from our frame code.)
enum Kind : std::uint8_t { kNone, kActor, kEffectStart, kEffectEnd };
Kind KindOf(int table, int index) {
    if (table == 1) {
        switch (index) {
            case 19: case 20:    // trap_sysobj_fadeout / fadein (actor+0xA08)
            case 133: case 134:  // trap_obj_hide / show
            case 108: case 109:  // trap_obj_show_part / hide_part (exe+0x3CC9B0)
            case 194: case 195:  // trap_obj_set_stealth / reset_stealth (render object actor+0x7B0)
            case 70: case 71:    // trap_obj_set_flag / reset_flag (bits of actor+0x120)
            case 211: case 212:  // trap_obj_pattern_enable / disable (bits of actor+0xD10)
            case 52: case 53: case 54:  // method_obj_enable / disable / reset_collision (actor+0x5A8)
            case 349:            // trap_obj_effect_kill_all
            // trap_obj_shadow_move_start / end (exe+0x42FBF0 / 0x42FC10): bit 6 of actor+0x8C8, the Shadow's
            // puddle form. Not sent before 2026-10-13: the friend's Shadows never came back up (you, bench run 4).
            case 104: case 105:
                return kActor;
            case 87:  // trap_obj_effect_start_bind (exe+0x3B6670)
            case 21:  // trap_obj_effect_start (exe+0x3B6630)
                return kEffectStart;
        }
    } else if (table == 0) {
        switch (index) {
            case 51: case 52:  // trap_effect_kill / trap_effect_fadeout
            case 44: case 85:  // trap_effect_loop_end / loop_end_kill: how looping effects end (the chandelier's)
                return kEffectEnd;
        }
    }
    return kNone;
}

// A fade-out or a loop end leaves the effect running for a while, and a kill can follow on the same handle; a kill
// (0/51, 0/85) is the last call, so the pairing goes.
bool EndsEffect(int table, int index) { return table == 0 && (index == 51 || index == 85); }

#pragma pack(push, 1)
struct Event {
    std::uint32_t id;
    std::uint8_t table;
    std::uint16_t index;
    std::uint8_t argc;     // arguments after the first (at most 4)
    std::uint32_t target;  // the actor as an exe offset, or (EffectEnd) the host's effect handle
    std::uint32_t objId;   // the actor's object (0 for EffectEnd)
    std::uint64_t args[4]; // the other arguments, as the script passed them (tagged)
    std::uint32_t result;  // EffectStart: the host's effect handle
    std::uint16_t netId;   // world sync's number for the actor on the host (0 = not tracked yet)
    std::uint16_t instance;  // the host's room instance that number belongs to
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kPerPacket = 24;  // 11 + 24 * 56 = 1355 bytes

bool g_on = false, g_host = false;

// Host: the events of the last frames, each sent in 3 packets.
struct Queued {
    Event e;
    int sent;
};
Queued g_queue[128];
int g_queueNext = 0;
std::uint32_t g_nextId = 1;
std::uint32_t g_hostEffects[64];  // effect handles we sent a start for (their end is sent too)
int g_hostEffectNext = 0;
std::uint32_t g_sentEvents = 0;

// Friend.
std::uint32_t g_lastId = 0;
// An effect handle has no check: once the effect is freed it still decodes (the freed block is filled with
// 0xEFACCAFE), and fading it out then crashed in exe+0x4B01C0 (bench 2026-10-12 18:33:56). So a pair keeps the
// effect's +0x30 part, cleared by the game when the effect is killed, and the end runs only while it is unchanged.
struct EffectPair {
    std::uint32_t host, ours;
    std::uintptr_t part;
    std::uint64_t started;  // research: GetTickCount64 at the start, for the alive checks below
    std::uint8_t checks;
    char what[40];
};
EffectPair g_effects[64];
int g_effectNext = 0;
std::uint32_t g_applied = 0, g_notOurs = 0, g_noEffect = 0, g_effectGone = 0, g_failed = 0;
std::uint32_t g_byNumber = 0;  // run on our bound enemy at another address than the host's
std::uint32_t g_appliedLogs = 0;
std::uint64_t g_statsTick = 0;

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

bool InList(std::uintptr_t x) {
    if (!x) return false;
    __try {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor)))
            if (a == x) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

std::uint32_t ObjId(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Effect instance (exe+0x2BE630/0x2BE8A0): +0xC flags (0x10000 = killed), +0x30 its part, freed and zeroed on a kill.
constexpr std::uintptr_t kEffectFlags = 0xC, kEffectPart = 0x30;
constexpr std::uint32_t kEffectKilled = 0x10000;
constexpr std::uint32_t kFreedFill = 0xEFACCAFE;  // what a freed effect's memory reads

std::uintptr_t EffectPart(std::uint32_t h) {
    __try {
        std::uintptr_t fx = Decode(h);
        if (!fx || (Read<std::uint32_t>(fx + kEffectFlags) & kEffectKilled)) return 0;
        return Read<std::uintptr_t>(fx + kEffectPart);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// The actor a script passed (its own handle, or an object whose +4 is the actor's handle), if in the list.
std::uintptr_t ActorArg(std::uint64_t arg) {
    if ((arg & 0xFFFFFFFF00000000ull) != kTagAddr) return 0;
    std::uintptr_t x = Decode(static_cast<std::uint32_t>(arg));
    if (InList(x)) return x;
    __try {
        std::uintptr_t y = x ? Decode(Read<std::uint32_t>(x + 4)) : 0;
        return InList(y) ? y : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void Queue(const Event& e) {
    Queued& q = g_queue[g_queueNext++ % 128];
    q.e = e;
    q.sent = 0;
}

void SendQueued() {
    char buf[sizeof(Packet) + kPerPacket * sizeof(Event)];
    Packet p{kMagic, kVersion, AvatarLinkBuild(), 0};
    int n = 0;
    for (int i = 0; i < 128 && n < kPerPacket; ++i) {  // oldest first
        Queued& q = g_queue[(g_queueNext + i) % 128];
        if (!q.e.id || q.sent >= 3) continue;
        std::memcpy(buf + sizeof(Packet) + n * sizeof(Event), &q.e, sizeof(Event));
        ++q.sent;
        ++n;
    }
    if (!n) return;
    p.count = static_cast<std::uint8_t>(n);
    std::memcpy(buf, &p, sizeof(p));
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Packet) + n * sizeof(Event)));
}

void Apply(const Event& e) {
    Kind kind = KindOf(e.table, e.index);
    std::uint64_t args[16] = {};
    std::uintptr_t actor = 0;
    if (kind == kEffectEnd) {
        int i = 0;
        while (i < 64 && g_effects[i].host != e.target) ++i;
        if (i == 64 || !e.target) {
            ++g_noEffect;
            return;
        }
        EffectPair pair = g_effects[i];
        if (EndsEffect(e.table, e.index)) g_effects[i] = {};
        if (!pair.part || EffectPart(pair.ours) != pair.part) {  // ours already ended (or the handle is reused)
            ++g_effectGone;
            return;
        }
        args[0] = kTagAddr | pair.ours;
    } else if (kind == kActor || kind == kEffectStart) {
        // Enemies from spawn groups are made in a different order in each game, so a different address (bench
        // 2026-10-13: 299 of 320 Shadow calls missed and the friend's Shadows kept their 'underground' flags).
        std::uintptr_t bound = WorldSyncBoundActor(e.netId, e.instance);
        actor = bound ? bound : ExeBase() + e.target;
        bool listed = bound || InList(actor);
        std::uint32_t obj = listed ? ObjId(actor) : 0;
        if (!listed || obj != e.objId || !WorldSyncSkipsBrain(actor)) {
            if (g_notOurs++ < 20)
                Log("world mirror: host call %u/%u for host#%u at exe+0x%X (obj 0x%X) not run: %s", e.table, e.index,
                    e.netId, e.target, e.objId, !listed ? "no actor of ours bound to it or there"
                    : obj != e.objId ? "another object there" : "ours there is not bound or is dead");
            return;
        }
        if (actor != ExeBase() + e.target) ++g_byNumber;
        args[0] = kTagAddr | reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle)(actor);
    } else {
        return;
    }
    for (int i = 0; i < e.argc && i < 4; ++i) args[1 + i] = e.args[i];
    auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(e.table, e.index));
    if (!fn) {
        ++g_failed;
        return;
    }
    __try {
        fn(args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_failed++ < 5) Log("world mirror: call %u/%u on %s failed (exception)", e.table, e.index, Model(actor));
        return;
    }
    ++g_applied;
    if (kind == kEffectEnd) {
        static int endLogs = 0;
        if (endLogs++ < 40) {
            std::uint32_t flags = 0;
            __try {
                std::uintptr_t fx = Decode(static_cast<std::uint32_t>(args[0]));
                if (fx) flags = Read<std::uint32_t>(fx + kEffectFlags);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            Log("world mirror: host's %u/%u ran on our effect 0x%08X, its flags now 0x%X", e.table, e.index,
                static_cast<std::uint32_t>(args[0]), flags);
        }
    }
    if (kind == kEffectStart && e.result) {
        // A handle is reused once its effect is gone, on either side: drop the old pair that has it.
        std::uint32_t ours = static_cast<std::uint32_t>(args[0]);
        for (EffectPair& p : g_effects)
            if (p.host == e.result || (ours && p.ours == ours)) p = {};
        if (ours) {
            EffectPair& p = g_effects[g_effectNext++ % 64];
            p = {e.result, ours, EffectPart(ours), GetTickCount64(), 0, {}};
            std::snprintf(p.what, sizeof(p.what), "%s effect %d", Model(actor),
                          static_cast<int>(static_cast<std::uint32_t>(e.args[0])));
        }
    }
    if (g_appliedLogs < 60) {
        ++g_appliedLogs;
        Log("world mirror: ran the host's call %u/%u on our %s (args %llX %llX)", e.table, e.index,
            actor ? Model(actor) : "effect", static_cast<unsigned long long>(e.args[0]),
            static_cast<unsigned long long>(e.args[1]));
    }
}

}  // namespace

void WorldMirrorInit(bool host) {
    g_on = true;
    g_host = host;
    Log("world mirror (one brain): %s", host ? "host: sending enemies' visible script calls"
                                             : "friend: bound enemies don't think; running the host's calls");
}

bool WorldMirrorWants(int table, int index) { return g_on && g_host && KindOf(table, index) != kNone; }

void WorldMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (!g_on || !g_host || argc < 1) return;
    Kind kind = KindOf(table, index);
    Event e{};
    e.table = static_cast<std::uint8_t>(table);
    e.index = static_cast<std::uint16_t>(index);
    if (kind == kEffectEnd) {
        std::uint32_t h = static_cast<std::uint32_t>(args[0]);
        int i = 0;
        while (i < 64 && g_hostEffects[i] != h) ++i;
        if (i == 64 || !h) return;  // not an effect we told the friend about
        if (EndsEffect(table, index)) g_hostEffects[i] = 0;
        e.target = h;
    } else if (kind == kActor || kind == kEffectStart) {
        std::uintptr_t actor = ActorArg(args[0]);
        if (!actor) return;
        std::uint32_t team = Read<std::uint32_t>(actor + kTeam);
        if (team != 2 && team != 3) return;  // enemies and field objects only
        e.target = static_cast<std::uint32_t>(actor - ExeBase());
        e.objId = ObjId(actor);
        e.netId = WorldSyncNetIdOf(actor, &e.instance);
        if (kind == kEffectStart) {
            e.result = static_cast<std::uint32_t>(result);
            if (e.result) {
                for (std::uint32_t& h : g_hostEffects)
                    if (h == e.result) h = 0;  // a reused handle: its old effect is gone
                g_hostEffects[g_hostEffectNext++ % 64] = e.result;
                // Research: the same alive checks as the friend's, to compare.
                EffectPair& p = g_effects[g_effectNext++ % 64];
                p = {e.result, e.result, EffectPart(e.result), GetTickCount64(), 0, {}};
                std::snprintf(p.what, sizeof(p.what), "%s effect %d", Model(actor),
                              static_cast<int>(static_cast<std::uint32_t>(args[1])));
            }
        }
    } else {
        return;
    }
    e.argc = static_cast<std::uint8_t>(argc - 1 > 4 ? 4 : argc - 1);
    for (int i = 0; i < e.argc; ++i) e.args[i] = args[1 + i];
    e.id = g_nextId++;
    Queue(e);
    ++g_sentEvents;
}

void WorldMirrorOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n < static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (n != static_cast<int>(sizeof(Packet) + p.count * sizeof(Event))) return;
    for (int i = 0; i < p.count; ++i) {
        Event e;
        std::memcpy(&e, buf + sizeof(Packet) + i * sizeof(Event), sizeof(e));
        if (e.id + 10000 < g_lastId) g_lastId = 0;  // the host's game restarted
        if (e.id <= g_lastId) continue;            // already run (each event comes 3 times)
        g_lastId = e.id;
        Apply(e);
    }
}

void WorldMirrorFrame() {
    if (!g_on) return;
    if (g_host) SendQueued();
    std::uint64_t now = GetTickCount64();
    // Research: is a started effect still there 1 s and 5 s later (alive = not killed, active = has its part)?
    for (EffectPair& p : g_effects) {
        if (!p.ours || p.checks >= 2 || now - p.started < (p.checks ? 5000u : 1000u)) continue;
        ++p.checks;
        std::uint32_t flags = 0;
        std::uintptr_t part = 0;
        __try {
            std::uintptr_t fx = Decode(p.ours);
            if (fx) flags = Read<std::uint32_t>(fx + kEffectFlags), part = Read<std::uintptr_t>(fx + kEffectPart);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        static int logs = 0;
        if (logs++ < 80)
            Log("world mirror: %s %s after %llu s: %s, %s, flags 0x%X", g_host ? "the host's" : "our", p.what,
                static_cast<unsigned long long>((now - p.started) / 1000),
                flags == kFreedFill ? "freed" : (flags & kEffectKilled) ? "killed" : "alive",
                part == p.part ? "same part" : part ? "another part" : "no part", flags);
    }
    if (now - g_statsTick < 10000) return;
    g_statsTick = now;
    if (g_host)
        Log("world mirror: script calls sent so far %u", g_sentEvents);
    else
        Log("world mirror: host calls run %u (%u on our enemy bound to the host's at another address), not for a "
            "bound enemy of ours %u, effect not known %u, our effect already gone %u, failed %u",
            g_applied, g_byNumber, g_notOurs, g_noEffect, g_effectGone, g_failed);
}

}  // namespace kh2coop
