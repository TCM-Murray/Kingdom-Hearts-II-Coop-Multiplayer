// Walk-in story scenes for either player (TODO 5.16, user request 2026-10-10). Some story scenes start
// when Sora walks into an area, not through a door: at the Mysterious Tower (02/19, event program 1)
// the Pete scene 802 starts near the tower. Each game started it on its own, so the two scenes ran
// apart and the one that ended later joined the fight later.
//
// How the game does it (Ghidra + room file tt25.ard, VERIFIED_OFFSETS 'Walk-in event areas'): such an
// area is a spawn group of type 3 (descriptor byte 0) with a trigger box. SpawnTask's GroupCheck
// exe+0x3FF000 calls GroupTrigger exe+0x3FE320(group, box) while Sora is in the box; for type 3 it
// calls exe+0x3AABE0(settings number = descriptor +0x1C, group id = descriptor +2) and marks the group
// done (group+4 bit 0). That runs the room's area settings entry (table exe+0x2AE6ED0): story flag,
// the cutscene (played in place, no load), then the move it asks for afterwards (here a reload of the
// same room for the fight).
//
// Here (user's picks, session 24): only areas whose settings entry plays a cutscene are shared; doors
// and talk/examine stay host-only, and areas that only move you elsewhere are left to the game.
//   host walks in:   the host's game runs it as usual and tells the friend's game (FIRE).
//   friend walks in: the friend's game holds it and asks the host (ASK); the host's game runs it and
//                    sends FIRE; the friend's game then runs its own the same way.
// Both games fire the area through the game's own GroupTrigger inside GroupCheck (the spawn task),
// so the scene starts the native way; the move after the scene goes through the load barrier.
// Without a connected host the friend's game runs its areas itself.
//
// Settings: SCENE_SYNC=1 (default on, both PCs; compared in the settings check).

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "scene_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;       // u8 world, room, door, pad, u16 map, btl, evt
constexpr std::uintptr_t kInField = 0x9BA8D0;   // u8: 0 while a room loads
constexpr std::uintptr_t kSettings = 0x2AE6ED0;  // area settings: 16 x 0x60 {i32 id, i32 group, .., +0x10 event}
constexpr int kSettingsCount = 16, kSettingsSize = 0x60, kSettingsEvent = 0x10;
// Group object: +4 flags (bit 0 done), +8 descriptor (u8 type, u16 id @2, u32 settings @0x1C), +0x10 first box.
constexpr std::uintptr_t kGroupFlags = 4, kGroupDesc = 8, kGroupBoxes = 0x10;
constexpr std::uint8_t kTypeEvent = 3;

constexpr std::uintptr_t kGroupTrigger = 0x3FE320;  // (group, box) -> bool
constexpr std::uint8_t kGroupTriggerBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr std::uintptr_t kGroupCheck = 0x3FF000;    // (group, const float pos[4]); SpawnTask, each group
constexpr std::uint8_t kGroupCheckBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57};

constexpr std::uint32_t kMagic = 0x5432484B;  // "KH2T"
constexpr std::uint16_t kVersion = 1;
enum : std::uint8_t { kAsk = 1, kFire = 2 };
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;
    std::uint8_t world, room;
    std::uint8_t pad;
    std::uint16_t programs[3];
    std::uint16_t group;
    std::uint16_t seq;
    std::uint32_t settings;
};
#pragma pack(pop)

constexpr int kFireRepeats = 6;            // frames the host repeats a FIRE (UDP)
constexpr std::uint64_t kAskEvery = 15;    // friend: frames between ASKs while it waits in the area
constexpr ULONGLONG kAllowMs = 10000;      // a FIRE (friend) or an accepted ASK (host) waits this long to run
constexpr ULONGLONG kPeerFreshMs = 2000;

using PFN_GroupTrigger = bool(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_GroupCheck = void(__fastcall*)(std::uintptr_t, const float*);
PFN_GroupTrigger g_realTrigger = nullptr;
PFN_GroupCheck g_realCheck = nullptr;
bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;

struct Place {
    std::uint8_t world, room;
    std::uint16_t programs[3];
};

// One area allowed to run in this game (a FIRE from the host, or the friend's ASK on the host).
struct Allowed {
    Place where;
    std::uint16_t group;
    ULONGLONG since;
    bool valid;
} g_allowed {};

// Host: the last area we ran, re-sent while the friend still asks for it.
struct Fired {
    Place where;
    std::uint16_t group;
    std::uint32_t settings;
    std::uint16_t seq;
    bool valid;
} g_fired {};
int g_fireLeft = 0;
std::uint16_t g_seq = 0;

// Friend: the area we hold, and the newest FIRE seen (UDP repeats).
std::uint16_t g_heldGroup = 0xFFFF;
std::uint64_t g_lastAsk = 0;
std::uint16_t g_lastFireSeq = 0;
bool g_sawFire = false;
std::uint32_t g_held = 0, g_runs = 0, g_asks = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

Place Here() {
    std::uintptr_t now = ExeBase() + kNow;
    Place p {Read<std::uint8_t>(now), Read<std::uint8_t>(now + 1), {}};
    for (int i = 0; i < 3; ++i) p.programs[i] = Read<std::uint16_t>(now + 4 + 2 * i);
    return p;
}

bool SamePlace(const Place& a, const Place& b) {
    return a.world == b.world && a.room == b.room && std::memcmp(a.programs, b.programs, sizeof(a.programs)) == 0;
}

bool PeerConnected() { return AvatarLinkPeerAgeMs() < kPeerFreshMs; }

std::uintptr_t Desc(std::uintptr_t group) { return Read<std::uintptr_t>(group + kGroupDesc); }
std::uint16_t GroupId(std::uintptr_t group) { return Read<std::uint16_t>(Desc(group) + 2); }
std::uint32_t GroupSettings(std::uintptr_t group) { return Read<std::uint32_t>(Desc(group) + 0x1C); }

// The event the area's settings entry plays (its 4-char name as a u32), 0 if none: the same entry
// exe+0x3F60D0 picks (exact id + group, else id with either group negative).
std::uint32_t SettingsEvent(std::uint32_t settings, std::uint16_t group) {
    std::uintptr_t table = ExeBase() + kSettings, pick = 0;
    for (int i = 0; i < kSettingsCount; ++i) {
        std::uintptr_t e = table + i * kSettingsSize;
        std::int32_t id = Read<std::int32_t>(e), g = Read<std::int32_t>(e + 4);
        if (id != static_cast<std::int32_t>(settings)) continue;
        if (g == group) {
            pick = e;
            break;
        }
        if (g < 0) pick = e;
    }
    return pick ? Read<std::uint32_t>(pick + kSettingsEvent) : 0;
}

// A walk-in area this module handles: type 3 whose settings play a cutscene.
bool IsSceneArea(std::uintptr_t group, std::uint32_t* event) {
    std::uintptr_t desc = Desc(group);
    if (!desc || Read<std::uint8_t>(desc) != kTypeEvent) return false;
    *event = SettingsEvent(GroupSettings(group), GroupId(group));
    return *event != 0;
}

void Send(std::uint8_t kind, const Place& where, std::uint16_t group, std::uint32_t settings, std::uint16_t seq) {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), kind, where.world, where.room, 0, {}, group, seq, settings};
    std::memcpy(p.programs, where.programs, sizeof(p.programs));
    AvatarLinkSendRaw(&p, sizeof(p));
}

const char* EventName(std::uint32_t event, char (&buf)[5]) {
    std::memcpy(buf, &event, 4);
    buf[4] = 0;
    for (char& c : buf)
        if (c && (c < 0x20 || c > 0x7E)) c = '?';
    return buf;
}

bool __fastcall HookGroupTrigger(std::uintptr_t group, std::uintptr_t box) {
    std::uint32_t event = 0;
    bool scene = false;
    __try {
        scene = g_on && IsSceneArea(group, &event);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        scene = false;
    }
    if (!scene) return g_realTrigger(group, box);
    std::uint16_t id = GroupId(group);
    std::uint32_t settings = GroupSettings(group);
    Place here = Here();
    char name[5];
    bool allowed = g_allowed.valid && g_allowed.group == id && SamePlace(g_allowed.where, here);
    if (g_host) {
        bool result = g_realTrigger(group, box);
        bool done = (Read<std::uint32_t>(group + kGroupFlags) & 1) != 0;
        if (done) {
            g_allowed.valid = false;
            ++g_runs;
            g_fired = {here, id, settings, ++g_seq, true};
            g_fireLeft = PeerConnected() ? kFireRepeats : 0;
            Log("scene sync: walk-in area %u (settings %u, scene '%s') runs %s; %s", id, settings,
                EventName(event, name), allowed ? "for the friend" : "(our Sora walked in)",
                g_fireLeft ? "telling the friend's game to run it too" : "no friend connected");
        }
        return result;
    }
    // Friend.
    if (allowed || !PeerConnected()) {
        if (allowed) g_allowed.valid = false;
        g_heldGroup = 0xFFFF;
        ++g_runs;
        Log("scene sync: walk-in area %u (settings %u, scene '%s') runs %s", id, settings, EventName(event, name),
            allowed ? "now (the host's game runs it too)" : "(no host connected)");
        return g_realTrigger(group, box);
    }
    if (g_heldGroup != id) {
        g_heldGroup = id;
        g_lastAsk = 0;
        ++g_held;
        Log("scene sync: our Sora walked into area %u (settings %u, scene '%s') in 0x%02X/0x%02X programs %u/%u/%u: "
            "held; asking the host's game to run it for both", id, settings, EventName(event, name), here.world,
            here.room, here.programs[0], here.programs[1], here.programs[2]);
    }
    if (g_lastAsk == 0 || g_frame - g_lastAsk >= kAskEvery) {
        g_lastAsk = g_frame;
        ++g_asks;
        Send(kAsk, here, id, settings, 0);
    }
    return false;  // not run: the game tests the box again next frame
}

// After the game's own check of a group: run an allowed area that our Sora isn't standing in.
void __fastcall HookGroupCheck(std::uintptr_t group, const float* pos) {
    g_realCheck(group, pos);
    if (!g_allowed.valid) return;
    __try {
        if (GetTickCount64() - g_allowed.since > kAllowMs) {
            Log("scene sync: area %u wasn't run within %llu s; dropped", g_allowed.group,
                static_cast<unsigned long long>(kAllowMs / 1000));
            g_allowed.valid = false;
            return;
        }
        if ((Read<std::uint32_t>(group + kGroupFlags) & 1) != 0 || !Desc(group) || GroupId(group) != g_allowed.group ||
            !SamePlace(g_allowed.where, Here()))
            return;
        std::uint32_t event = 0;
        if (!IsSceneArea(group, &event)) return;
        HookGroupTrigger(group, Read<std::uintptr_t>(group + kGroupBoxes));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_allowed.valid = false;
        Log("scene sync: exception while running an area for the other player; dropped");
    }
}

}  // namespace

void SceneSyncInit(bool host) {
    if (EnvInt("KH2COOP_SCENE_SYNC", 1) != 1) {
        Log("scene sync: off (SCENE_SYNC=0)");
        return;
    }
    g_host = host;
    g_on = HookFunction(kGroupTrigger, kGroupTriggerBytes, sizeof(kGroupTriggerBytes),
                        reinterpret_cast<void*>(HookGroupTrigger), reinterpret_cast<void**>(&g_realTrigger),
                        "GroupTrigger (scene sync)") &&
           HookFunction(kGroupCheck, kGroupCheckBytes, sizeof(kGroupCheckBytes), reinterpret_cast<void*>(HookGroupCheck),
                        reinterpret_cast<void**>(&g_realCheck), "GroupCheck (scene sync)");
    Log("scene sync: %s", g_on ? (host ? "on (host): walk-in story scenes start in both games, whoever walks in"
                                       : "on (friend): our walk-in story scenes wait for the host's game")
                               : "OFF (hooks failed)");
}

void SceneSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_host && g_fireLeft > 0 && g_fired.valid) {
        --g_fireLeft;
        Send(kFire, g_fired.where, g_fired.group, g_fired.settings, g_fired.seq);
    }
    // Friend: forget the held area once our game stops testing it (we walked out, or the room changed).
    if (!g_host && g_heldGroup != 0xFFFF && g_frame - g_lastAsk > 2 * kAskEvery) {
        Log("scene sync: we left area %u before the host's game answered", g_heldGroup);
        g_heldGroup = 0xFFFF;
    }
    if (g_frame % 3600 == 0 && (g_runs || g_held))
        Log("scene sync: areas run %u, held %u, asks sent %u", g_runs, g_held, g_asks);
}

void SceneSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    Place where {p.world, p.room, {}};
    std::memcpy(where.programs, p.programs, sizeof(where.programs));
    Place here = Here();
    if (g_host && p.kind == kAsk) {
        if (g_fired.valid && g_fired.group == p.group && SamePlace(g_fired.where, where)) {
            if (g_fireLeft == 0) g_fireLeft = kFireRepeats;  // ran already: tell the friend again
            return;
        }
        if (g_allowed.valid && g_allowed.group == p.group && SamePlace(g_allowed.where, where)) return;
        if (!SamePlace(where, here) || Read<std::uint8_t>(ExeBase() + kInField) == 0) {
            static std::uint64_t lastLog = 0;
            if (g_frame - lastLog > 120) {
                lastLog = g_frame;
                Log("scene sync: the friend asks for area %u in 0x%02X/0x%02X programs %u/%u/%u, but we're in "
                    "0x%02X/0x%02X programs %u/%u/%u%s: not run", p.group, p.world, p.room, p.programs[0],
                    p.programs[1], p.programs[2], here.world, here.room, here.programs[0], here.programs[1],
                    here.programs[2], Read<std::uint8_t>(ExeBase() + kInField) ? "" : " (loading)");
            }
            return;
        }
        g_allowed = {where, p.group, GetTickCount64(), true};
        Log("scene sync: the friend walked into area %u (settings %u): running it here", p.group, p.settings);
        return;
    }
    if (!g_host && p.kind == kFire) {
        if (g_sawFire && p.seq == g_lastFireSeq) return;
        g_sawFire = true;
        g_lastFireSeq = p.seq;
        if (!SamePlace(where, here)) {
            Log("scene sync: the host's game ran area %u in 0x%02X/0x%02X programs %u/%u/%u; we're in 0x%02X/0x%02X "
                "programs %u/%u/%u: not run here", p.group, p.world, p.room, p.programs[0], p.programs[1],
                p.programs[2], here.world, here.room, here.programs[0], here.programs[1], here.programs[2]);
            return;
        }
        g_allowed = {where, p.group, GetTickCount64(), true};
        Log("scene sync: the host's game ran area %u (settings %u): running it here too", p.group, p.settings);
    }
}

}  // namespace kh2coop
