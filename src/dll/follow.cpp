// Room following (N2): the friend's game goes wherever the host is.
// The host does nothing new (its avatar packets already carry world/room/door).
// On the friend's PC, when the host has been settled in another room for a
// moment and our own game is in a safe state, we ask the game to load the
// host's room through the same entrance, with its own room-change function:
//   exe+0x152990 RequestTransition(const LocationPacket*, u32 fade, int mode, u8, int)
// (Ghidra + the world-map exit exe+0x154E20, which calls it with programs
// 0xFFFF and (fade | 1, 0, 0, 0); lead: Volpestyle pointer_map_v1 "cold warp").
// Door lock (user decision): on the friend's PC, the game may only load the
// room the host is in. RequestTransition is hooked; any other target (a door,
// a script exit) is refused while the host is connected and in the field.
// Same-room reloads (Continue after a Game Over) and our own follow requests
// pass. With no host, nothing is blocked.
// Room versions: a room can be loaded with other programs (map/btl/evt) than
// the save's, e.g. Olympus 06/07 after the Cerberus event loads the boss
// battle (same room, door 0). We load the host's exact programs, also when
// only the programs differ (same room), so events and bosses match.
//
// Settings: ROLE=host|friend (default: host when PEER=auto, else friend),
//           FOLLOW=1 (friend side; default on).

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "follow.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;              // u8 world, room, door, pad, u16 map, btl, evt
constexpr std::uintptr_t kRequestTransition = 0x152990;
constexpr std::uint8_t kRequestTransitionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                                    0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};
// Safe-state gates (leads from Volpestyle's pointer map, logged here so they
// can be checked against what we see in game):
constexpr std::uintptr_t kInField = 0x9BA8D0;          // u8: 0 while a room loads
constexpr std::uintptr_t kOpenMenu = 0x7435D0;         // u8: 0xFF = no menu
constexpr std::uintptr_t kFrozenGroups = 0x2A171E8;    // u32: 0 = no entity group frozen (cutscenes...)
constexpr std::uintptr_t kPlayerActor = 0x2A171C8;     // list head = Sora
constexpr std::uintptr_t kSoraHp = 0x2A23598;          // Sora's stat slot HP

#pragma pack(push, 1)
struct LocationPacket {
    std::uint8_t world, room, door, pad;
    std::uint16_t map, btl, evt;  // 0xFFFF = the save's program for that room
    std::uint8_t spare[6];        // the game copies 10 bytes; keep 16 like a stack local
};
#pragma pack(pop)

using PFN_RequestTransition = void(__fastcall*)(const LocationPacket*, std::uint32_t, int, std::uint8_t, int);
PFN_RequestTransition g_request = nullptr;  // the original (trampoline once hooked)
bool g_lockDoors = false;
std::uint32_t g_blocked = 0;
std::uint64_t g_lastBlockedLog = 0;
std::uint64_t g_hostSeenFrame = 0;  // frame of the newest host packet

bool g_follow = false;
std::uint64_t g_frame = 0;
// Host location as last reported, and since when it has been unchanged.
std::uint8_t g_hostWorld = 0xFF, g_hostRoom = 0xFF, g_hostDoor = 0;
std::uint16_t g_hostPrograms[3] = {0xFFFF, 0xFFFF, 0xFFFF};
bool g_hostHasActor = false;
std::uint64_t g_hostSince = 0;
// Our own pending request.
bool g_pending = false;
bool g_pendingLoading = false;  // the requested load has started (in-field went 0)
std::uint8_t g_pendingWorld = 0, g_pendingRoom = 0;
std::uint16_t g_pendingPrograms[3] = {};
std::uint64_t g_pendingFrame = 0;
// A target we loaded but whose programs our game turned into others: don't
// reload it again until the host's location or programs change (no loops).
bool g_givenUp = false;
std::uint32_t g_requests = 0, g_arrivals = 0, g_timeouts = 0;
std::uint64_t g_lastBlockLog = 0;

template <typename T>
T Read(std::uintptr_t offset) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + offset);
}

// Why we can't warp right now, or nullptr when it's safe.
const char* NotSafeReason() {
    if (Read<std::uint8_t>(kInField) == 0) return "room loading";
    if (Read<std::uint8_t>(kOpenMenu) != 0xFF) return "menu open";
    if (Read<std::uint32_t>(kFrozenGroups) != 0) return "entities frozen (cutscene?)";
    if (Read<std::uintptr_t>(kPlayerActor) == 0) return "no Sora";
    if (Read<std::uint32_t>(kSoraHp) == 0) return "Sora is down";
    if (Read<std::uint8_t>(kNow) == 0xFF) return "title screen";
    return nullptr;
}

bool HostPresent() {
    return g_hostHasActor && g_hostWorld != 0xFF && g_frame - g_hostSeenFrame < 120;
}

void __fastcall HookRequestTransition(const LocationPacket* p, std::uint32_t fade, int mode, std::uint8_t flag,
                                      int extra) {
    std::uint8_t world = Read<std::uint8_t>(kNow), room = Read<std::uint8_t>(kNow + 1);
    bool sameRoom = p->world == world && p->room == room;
    bool hostRoom = p->world == g_hostWorld && p->room == g_hostRoom;
    if (!g_lockDoors || !HostPresent() || sameRoom || hostRoom) {
        g_request(p, fade, mode, flag, extra);
        return;
    }
    ++g_blocked;
    if (g_frame - g_lastBlockedLog > 60) {
        Log("follow: door lock: refused our own move to world 0x%02X room 0x%02X door 0x%02X (host is in 0x%02X/0x%02X; "
            "%u refused so far)", p->world, p->room, p->door, g_hostWorld, g_hostRoom, g_blocked);
        g_lastBlockedLog = g_frame;
    }
}

bool OurProgramsDiffer() {
    for (int i = 0; i < 3; ++i)
        if (Read<std::uint16_t>(kNow + 4 + 2 * i) != g_hostPrograms[i]) return true;
    return false;
}

}  // namespace

void FollowInit(bool host) {
    if (host || EnvInt("KH2COOP_FOLLOW", 1) != 1) {
        Log("follow: %s", host ? "host: the friend's game follows us" : "off");
        return;
    }
    auto target = reinterpret_cast<const void*>(ExeBase() + kRequestTransition);
    if (std::memcmp(target, kRequestTransitionBytes, sizeof(kRequestTransitionBytes)) != 0) {
        Log("follow: RequestTransition bytes differ (different game build?); room following OFF");
        return;
    }
    g_lockDoors = EnvInt("KH2COOP_LOCK_DOORS", 1) == 1;
    if (!g_lockDoors || !HookFunction(kRequestTransition, kRequestTransitionBytes, sizeof(kRequestTransitionBytes),
                                      reinterpret_cast<void*>(HookRequestTransition),
                                      reinterpret_cast<void**>(&g_request), "RequestTransition (door lock)")) {
        g_lockDoors = false;
        g_request = reinterpret_cast<PFN_RequestTransition>(ExeBase() + kRequestTransition);
    }
    g_follow = true;
    Log("follow: on: this game loads the host's room when the host changes rooms; door lock %s",
        g_lockDoors ? "on (only the host's room can be loaded)" : "off");
}

void FollowOnHostLocation(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                          bool hasActor) {
    if (!g_follow) return;
    g_hostSeenFrame = g_frame;
    if (world != g_hostWorld || room != g_hostRoom || hasActor != g_hostHasActor ||
        std::memcmp(programs, g_hostPrograms, sizeof(g_hostPrograms)) != 0) {
        g_hostWorld = world;
        g_hostRoom = room;
        g_hostDoor = door;
        std::memcpy(g_hostPrograms, programs, sizeof(g_hostPrograms));
        g_hostHasActor = hasActor;
        g_hostSince = g_frame;
        g_givenUp = false;
    }
}

void FollowFrame() {
    if (!g_follow) return;
    ++g_frame;
    std::uint8_t world = Read<std::uint8_t>(kNow), room = Read<std::uint8_t>(kNow + 1);

    if (g_pending) {
        // A same-room reload starts in the room we're already in: count the
        // arrival only once the load has begun (in-field 0) and finished.
        if (Read<std::uint8_t>(kInField) == 0) g_pendingLoading = true;
        if (g_pendingLoading && world == g_pendingWorld && room == g_pendingRoom && Read<std::uint8_t>(kInField) != 0) {
            ++g_arrivals;
            std::uint16_t now[3];
            for (int i = 0; i < 3; ++i) now[i] = Read<std::uint16_t>(kNow + 4 + 2 * i);
            Log("follow: arrived in world 0x%02X room 0x%02X, programs map %u btl %u evt %u (%llu frames after the "
                "request)", world, room, now[0], now[1], now[2],
                static_cast<unsigned long long>(g_frame - g_pendingFrame));
            if (std::memcmp(now, g_pendingPrograms, sizeof(now)) != 0) {
                Log("follow: our game loaded other programs than the host's (map %u btl %u evt %u); not retrying "
                    "until the host moves", g_pendingPrograms[0], g_pendingPrograms[1], g_pendingPrograms[2]);
                g_givenUp = true;
            }
            g_pending = false;
        } else if (g_frame - g_pendingFrame > 900) {  // 15 s
            ++g_timeouts;
            Log("follow: request for world 0x%02X room 0x%02X timed out (now 0x%02X/0x%02X); will retry",
                g_pendingWorld, g_pendingRoom, world, room);
            g_pending = false;
        }
        return;
    }

    // Host settled (has an actor, same room for 0.5 s) in a room, or a version
    // of our room (other programs), that isn't ours?
    if (!g_hostHasActor || g_hostWorld == 0xFF || g_frame - g_hostSince < 30) return;
    bool sameRoom = g_hostWorld == world && g_hostRoom == room;
    if (sameRoom && (g_givenUp || g_hostPrograms[0] == 0xFFFF || !OurProgramsDiffer())) return;
    if (const char* why = NotSafeReason()) {
        if (g_frame - g_lastBlockLog > 300) {
            Log("follow: host is in 0x%02X/0x%02X (programs %u/%u/%u), waiting: %s", g_hostWorld, g_hostRoom,
                g_hostPrograms[0], g_hostPrograms[1], g_hostPrograms[2], why);
            g_lastBlockLog = g_frame;
        }
        return;
    }
    LocationPacket p {};
    p.world = g_hostWorld;
    p.room = g_hostRoom;
    p.door = g_hostDoor;
    p.map = g_hostPrograms[0];
    p.btl = g_hostPrograms[1];
    p.evt = g_hostPrograms[2];
    Log("follow: host is in world 0x%02X room 0x%02X (door 0x%02X, programs map %u btl %u evt %u); we are in "
        "0x%02X/0x%02X (programs %u/%u/%u) -> loading it", p.world, p.room, p.door, p.map, p.btl, p.evt, world, room,
        Read<std::uint16_t>(kNow + 4), Read<std::uint16_t>(kNow + 6), Read<std::uint16_t>(kNow + 8));
    g_request(&p, 1, 0, 0, 0);
    ++g_requests;
    g_pending = true;
    g_pendingLoading = false;
    g_pendingWorld = p.world;
    g_pendingRoom = p.room;
    std::memcpy(g_pendingPrograms, g_hostPrograms, sizeof(g_pendingPrograms));
    g_pendingFrame = g_frame;
}

}  // namespace kh2coop
