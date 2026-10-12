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
// pass. With no host, nothing is blocked. Rooms the host went through since we were last in its room
// pass too (host trail, g_trail). A refused move is kept for 3 s and replayed if the host goes
// to that room in the meantime (our cutscene can end a moment before the host's, see g_refused).
// After the host skips a cutscene, our skip chain's own moves pass for a while (g_skipUntil, TODO 5.3).
// Only one room change at a time: no request is added while another one's load hasn't started (g_asked);
// our game's own second request is dropped too (TODO 6.10), on both PCs. While the game leaves to the title
// screen (link_watch.cpp, TODO 6.11) no room change may start at all (g_hold).
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
constexpr std::uintptr_t kBattle = 0x2A11404;          // u32: != 0 in a fight (pause_sync.cpp)
constexpr std::uintptr_t kSceneState = 0xB65210;       // u32: 3 = a story cutscene is playing
constexpr std::uintptr_t kSoraHp = 0x2A23598;          // Sora's stat slot HP
// Lead (Ghidra session 28): the load task RequestTransition makes; cleared when that task starts the load.
constexpr std::uintptr_t kLoadTask = 0x9BA928;

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
bool g_sceneLog = false;  // SCENE_LOG: log the game's own room changes (LogTransition)
std::uint64_t g_frame = 0;
// A move of our own the door lock refused, kept for a moment: when both games play the same cutscene
// in step, our event can ask for the next room a few ms before the host's packet saying it goes there
// too arrives. Refused and forgotten, our event then waited forever (bench 2026-10-08, Olympus 06/06
// Hades scene -> 06/0F with the load barrier). If the host goes to that room soon, it is replayed.
// "Soon" = 3 s, or for as long as our game still has entities frozen in the room it was refused in: a
// cutscene the friend skipped first waits for its move with everything frozen, and the host skipping
// 3.1 s later left the friend on a black screen for good (two-PC test 2026-10-09, Olympus 06/06).
struct RefusedMove {
    LocationPacket p;
    std::uint32_t fade;
    int mode;
    std::uint8_t flag;
    int extra;
    std::uint64_t frame;
    std::uint8_t fromWorld, fromRoom;  // where we were when it was refused
    bool valid;
} g_refused {};
constexpr std::uint64_t kRefusedKeepFrames = 180;  // 3 s
// The rooms the host went through, oldest first, since we were last in the host's room (TODO 5.13).
// A cutscene chain the host skipped through (Twilight Town 02/23 -> 02/0C -> 02/19) left our game, which
// skipped a moment later, asking for 02/23 while the host was in 02/19: refused, and our event waited on
// a black screen for good (two-PC run 2026-10-09). Our own move into one of these rooms is allowed; the
// entries up to it are dropped as we pass, and the whole list when we reach the host's room.
struct TrailRoom {
    std::uint8_t world, room;
};
constexpr int kTrailMax = 16;
TrailRoom g_trail[kTrailMax] {};
int g_trailCount = 0;
std::uint32_t g_trailAllowed = 0;
// The host skipped a cutscene (skip_sync.cpp): until this frame our own moves pass the door lock. A skip
// races through the chained scenes' rooms (bench 2026-10-11: host 04/08 -> 04/01 -> 04/08 -> Bailey in
// 0.15 s); our game, skipping a moment later, asks for the same rooms after the host has left them, and the
// host trail can't help when the host ends where we are (it is cleared there). Closed once we're in the
// host's room with its programs.
constexpr std::uint64_t kSkipWindowFrames = 900;  // 15 s
std::uint64_t g_skipUntil = 0;
std::uint32_t g_skipAllowed = 0;
bool g_skipLoaded = false;  // a load started since the window opened (the window closes only after one)
// The last room change asked for (by our game or by us) whose load hasn't started yet. A second
// request stacked on it in the same frame crashed the friend's game in the room teardown (exe+0x39DA55,
// a resource released twice; two-PC test 2026-10-08 19:09, Cerberus event: the replayed event move
// from the host's avatar packet, then the load barrier's own request for the same room).
struct AskedMove {
    std::uint8_t world, room;
    std::uint64_t frame;
    bool valid;
} g_asked {};
constexpr std::uint64_t kAskedKeepFrames = 120;  // a request that never starts a load is forgotten after 2 s
std::uint32_t g_dropped = 0;
std::uint64_t g_lastDroppedLog = 0;
// Leaving to the title screen (link_watch.cpp): the soft reset and a room change starting together crash the
// game at the same spot as two requests (TODO 6.11). Set from the 'Title Screen' pick until the field is gone.
bool g_hold = false;
std::uint32_t g_held = 0;
std::uint64_t g_lastHeldLog = 0;
// Debug warp (tools/debug_cmd.py warp, TODO 2.2, tests): a room asked for by a test command, requested once our
// game is safe (in the field, no cutscene or menu), dropped after 15 s. Host side: the friend's game follows.
LocationPacket g_debugWarp {};
bool g_debugWarpPending = false;
std::uint64_t g_debugWarpFrame = 0;
constexpr std::uint64_t kDebugWarpFrames = 900;
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
    if (g_hold) return "leaving to the title screen";
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

// While g_hold: true, and the move is logged (at most once a second) and not done.
bool Held(const LocationPacket* p) {
    if (!g_hold) return false;
    ++g_held;
    if (g_frame - g_lastHeldLog > 60) {
        Log("follow: no room change to world 0x%02X room 0x%02X: leaving to the title screen (%u held so far)",
            p->world, p->room, g_held);
        g_lastHeldLog = g_frame;
    }
    return true;
}

// Every room change that reaches the game goes through here (ours and, with the door lock, the game's own).
void Request(const LocationPacket* p, std::uint32_t fade, int mode, std::uint8_t flag, int extra) {
    if (Held(p)) return;
    g_asked = {p->world, p->room, g_frame, true};
    g_request(p, fade, mode, flag, extra);
}

// A room change was asked for and its load hasn't started yet.
bool MoveOnItsWay() {
    return g_asked.valid && g_frame - g_asked.frame <= kAskedKeepFrames;
}

// Index of world/room in the host trail, or -1.
int TrailIndex(std::uint8_t world, std::uint8_t room) {
    for (int i = 0; i < g_trailCount; ++i)
        if (g_trail[i].world == world && g_trail[i].room == room) return i;
    return -1;
}

// Forget the trail up to and including entry i (we're there now, or on our way).
void TrailDropThrough(int i) {
    std::memmove(g_trail, g_trail + i + 1, sizeof(TrailRoom) * (g_trailCount - i - 1));
    g_trailCount -= i + 1;
}

// The host went to another room while we aren't in it: add it to the trail.
void TrailAdd(std::uint8_t world, std::uint8_t room) {
    if (world == 0xFF || TrailIndex(world, room) >= 0) return;
    if (g_trailCount == kTrailMax) TrailDropThrough(0);
    g_trail[g_trailCount++] = {world, room};
}

// Research log (SCENE_LOG=1, TODO 5.16): every room change the game itself asks for, with who asked (call
// chain), on both PCs. Tells walk-in event triggers apart from doors, talk/examine and cutscene moves.
void LogTransition(const LocationPacket* p, std::uint32_t fade, int mode, std::uint8_t flag, int extra) {
    char chain[200];
    CallerChain(chain, sizeof(chain), 1);
    float x = 0, y = 0, z = 0;
    if (std::uintptr_t sora = Read<std::uintptr_t>(kPlayerActor)) {
        auto pos = reinterpret_cast<const float*>(sora + 0x670);
        x = pos[0], y = pos[1], z = pos[2];
    }
    Log("scene log: the game asks for world 0x%02X room 0x%02X door 0x%02X programs %u/%u/%u (fade %u mode %d flag "
        "%u extra %d); we are in 0x%02X/0x%02X programs %u/%u/%u, frozen 0x%X, menu 0x%02X, Sora (%.0f, %.0f, %.0f); "
        "from %s", p->world, p->room, p->door, p->map, p->btl, p->evt, fade, mode, flag, extra, Read<std::uint8_t>(kNow),
        Read<std::uint8_t>(kNow + 1), Read<std::uint16_t>(kNow + 4), Read<std::uint16_t>(kNow + 6),
        Read<std::uint16_t>(kNow + 8), Read<std::uint32_t>(kFrozenGroups), Read<std::uint8_t>(kOpenMenu), x, y, z,
        chain);
}

void __fastcall HookRequestTransition(const LocationPacket* p, std::uint32_t fade, int mode, std::uint8_t flag,
                                      int extra) {
    if (g_sceneLog) LogTransition(p, fade, mode, flag, extra);
    if (Held(p)) return;
    std::uint8_t world = Read<std::uint8_t>(kNow), room = Read<std::uint8_t>(kNow + 1);
    // A second request before the first one's load starts crashes the game (g_asked). Two-PC run session 28
    // (TWTNW, end of the ship fight, friend 00:06:07): our follow asked for the host's new room, then in the same
    // frame our game's own mission-end move asked for it too, and the door lock lets moves to the host's room
    // through. The first one goes on; a dropped move to another room is kept like a refused one (g_refused).
    // A request once the load has started (in-field 0) is the game's own business: its skip chains do that.
    if (MoveOnItsWay() && Read<std::uint8_t>(kInField) != 0) {
        ++g_dropped;
        bool sameTarget = p->world == g_asked.world && p->room == g_asked.room;
        if (g_lockDoors && !sameTarget) g_refused = {*p, fade, mode, flag, extra, g_frame, world, room, true};
        if (g_frame - g_lastDroppedLog > 60) {
            Log("follow: dropped our game's own move to world 0x%02X room 0x%02X (programs %u/%u/%u): a move to "
                "0x%02X/0x%02X was asked for %llu frames ago and its load hasn't started (load task %s; %u dropped "
                "so far)", p->world, p->room, p->map, p->btl, p->evt, g_asked.world, g_asked.room,
                static_cast<unsigned long long>(g_frame - g_asked.frame),
                Read<std::uintptr_t>(kLoadTask) ? "waiting" : "none", g_dropped);
            g_lastDroppedLog = g_frame;
        }
        return;
    }
    bool sameRoom = p->world == world && p->room == room;
    bool hostRoom = p->world == g_hostWorld && p->room == g_hostRoom;
    if (!g_lockDoors || !HostPresent() || sameRoom || hostRoom) {
        Request(p, fade, mode, flag, extra);
        return;
    }
    if (g_frame < g_skipUntil) {
        ++g_skipAllowed;
        Log("follow: door lock: our own move to world 0x%02X room 0x%02X allowed: the host skipped this cutscene "
            "chain (%u allowed so far)", p->world, p->room, g_skipAllowed);
        Request(p, fade, mode, flag, extra);
        return;
    }
    if (int i = TrailIndex(p->world, p->room); i >= 0) {
        TrailDropThrough(i);
        ++g_trailAllowed;
        Log("follow: door lock: our own move to world 0x%02X room 0x%02X allowed: the host went through it on its "
            "way to 0x%02X/0x%02X (%u allowed so far)", p->world, p->room, g_hostWorld, g_hostRoom, g_trailAllowed);
        Request(p, fade, mode, flag, extra);
        return;
    }
    ++g_blocked;
    g_refused = {*p, fade, mode, flag, extra, g_frame, world, room, true};
    if (g_frame - g_lastBlockedLog > 60) {
        Log("follow: door lock: refused our own move to world 0x%02X room 0x%02X door 0x%02X (programs %u/%u/%u, "
            "fade %u mode %d flag %u extra %d; host is in 0x%02X/0x%02X; %u refused so far)", p->world, p->room,
            p->door, p->map, p->btl, p->evt, fade, mode, flag, extra, g_hostWorld, g_hostRoom, g_blocked);
        g_lastBlockedLog = g_frame;
    }
}

// The host now goes where our refused move wanted to go: do our own move after all (true if done).
bool ReplayRefusedMove(std::uint8_t world, std::uint8_t room) {
    if (!g_refused.valid) return false;
    bool stillHere = Read<std::uint8_t>(kNow) == g_refused.fromWorld && Read<std::uint8_t>(kNow + 1) == g_refused.fromRoom;
    bool waiting = stillHere && Read<std::uint32_t>(kFrozenGroups) != 0;  // our event still waits for its move
    if (!stillHere || (g_frame - g_refused.frame > kRefusedKeepFrames && !waiting)) {
        g_refused.valid = false;
        return false;
    }
    if (g_refused.p.world != world || g_refused.p.room != room) return false;
    g_refused.valid = false;
    if (MoveOnItsWay()) {
        Log("follow: the host goes to world 0x%02X room 0x%02X too; our refused move isn't needed (a room change "
            "is already on its way)", world, room);
        return g_asked.world == world && g_asked.room == room;
    }
    Log("follow: the host goes to world 0x%02X room 0x%02X too: doing our own refused move now (%llu frames late)",
        world, room, static_cast<unsigned long long>(g_frame - g_refused.frame));
    Request(&g_refused.p, g_refused.fade, g_refused.mode, g_refused.flag, g_refused.extra);
    return true;
}

bool OurProgramsDiffer() {
    for (int i = 0; i < 3; ++i)
        if (Read<std::uint16_t>(kNow + 4 + 2 * i) != g_hostPrograms[i]) return true;
    return false;
}

// The room we're in, same door and programs (a reload of it).
LocationPacket OurRoom() {
    LocationPacket p {};
    p.world = Read<std::uint8_t>(kNow);
    p.room = Read<std::uint8_t>(kNow + 1);
    p.door = Read<std::uint8_t>(kNow + 2);
    p.map = Read<std::uint16_t>(kNow + 4);
    p.btl = Read<std::uint16_t>(kNow + 6);
    p.evt = Read<std::uint16_t>(kNow + 8);
    return p;
}

// The pending debug warp, once it's safe (see g_debugWarp).
void DebugWarpFrame() {
    if (!g_debugWarpPending) return;
    const char* why = NotSafeReason();
    if (!why && MoveOnItsWay()) why = "another room change is on its way";
    if (why) {
        if (g_frame - g_debugWarpFrame > kDebugWarpFrames) {
            Log("follow: debug warp NOT done: still not safe after 15 s (%s)", why);
            g_debugWarpPending = false;
        }
        return;
    }
    g_debugWarpPending = false;
    const LocationPacket& p = g_debugWarp;
    auto request = g_request ? g_request : reinterpret_cast<PFN_RequestTransition>(ExeBase() + kRequestTransition);
    g_asked = {p.world, p.room, g_frame, true};
    request(&p, 1, 0, 0, 0);
    Log("follow: debug warp: asked for world 0x%02X room 0x%02X door 0x%02X programs %u/%u/%u (%llu frames after the "
        "command)", p.world, p.room, p.door, p.map, p.btl, p.evt,
        static_cast<unsigned long long>(g_frame - g_debugWarpFrame));
}

}  // namespace

void FollowHostSkipped() {
    if (!g_follow) return;
    bool opening = g_frame >= g_skipUntil;
    g_skipUntil = g_frame + kSkipWindowFrames;
    if (opening) {
        g_skipLoaded = false;
        Log("follow: the host skipped a cutscene: our own moves pass the door lock until we're in its room");
    }
    // A move refused just before (our scene ended first) goes now.
    if (g_refused.valid && !MoveOnItsWay()) {
        g_refused.valid = false;
        Log("follow: doing our refused move to world 0x%02X room 0x%02X now (the host skipped)", g_refused.p.world,
            g_refused.p.room);
        Request(&g_refused.p, g_refused.fade, g_refused.mode, g_refused.flag, g_refused.extra);
    }
}

void FollowInit(bool host) {
    g_sceneLog = EnvInt("KH2COOP_SCENE_LOG", 1) == 1;
    auto target = reinterpret_cast<const void*>(ExeBase() + kRequestTransition);
    bool bytesOk = std::memcmp(target, kRequestTransitionBytes, sizeof(kRequestTransitionBytes)) == 0;
    if (host || EnvInt("KH2COOP_FOLLOW", 1) != 1) {
        Log("follow: %s", host ? "host: the friend's game follows us" : "off");
        // Research log only: the hook passes every request through (no door lock here).
        if (g_sceneLog && bytesOk &&
            !HookFunction(kRequestTransition, kRequestTransitionBytes, sizeof(kRequestTransitionBytes),
                          reinterpret_cast<void*>(HookRequestTransition), reinterpret_cast<void**>(&g_request),
                          "RequestTransition (scene log)"))
            g_sceneLog = false;
        return;
    }
    if (!bytesOk) {
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
        if (world != g_hostWorld || room != g_hostRoom) TrailAdd(world, room);
        g_hostWorld = world;
        g_hostRoom = room;
        g_hostDoor = door;
        std::memcpy(g_hostPrograms, programs, sizeof(g_hostPrograms));
        g_hostHasActor = hasActor;
        g_hostSince = g_frame;
        g_givenUp = false;
        ReplayRefusedMove(world, room);
    }
}

// Reloads the room we're in (same door and programs) when that's safe: in the field, no menu, no
// cutscene, no fight. False (why says why) otherwise. Works on both PCs (link_watch.cpp: Donald and
// Goofy back at once after the other game is gone).
bool FollowReloadRoom(const char** why) {
    // Fights and boss fights reload too (your pick, two-PC test session 27: the fight starts over with the
    // party complete rather than going on one companion short).
    *why = NotSafeReason();
    if (!*why && Read<std::int32_t>(kSceneState) == 3) *why = "in a cutscene";
    if (!*why && MoveOnItsWay()) *why = "another room change is on its way";  // never two requests (g_asked)
    if (*why) return false;
    LocationPacket p = OurRoom();
    auto request = g_request ? g_request : reinterpret_cast<PFN_RequestTransition>(ExeBase() + kRequestTransition);
    g_asked = {p.world, p.room, g_frame, true};
    request(&p, 1, 0, 0, 0);
    Log("follow: reloading our room 0x%02X/0x%02X (door 0x%02X, programs %u/%u/%u)", p.world, p.room, p.door, p.map,
        p.btl, p.evt);
    return true;
}

// Debug command (tools/debug_cmd.py double, TODO 6.10): asks for our room twice in one frame through the game's
// own entry point, the way two of the game's scripts would. Without the guard in HookRequestTransition the game
// crashes (exe+0x39DA55); with it the second one is dropped.
void FollowDebugDoubleRequest() {
    if (!g_request) {
        Log("follow: debug double request: RequestTransition isn't hooked in this game (SCENE_LOG=0?); not done");
        return;
    }
    const char* why = NotSafeReason();
    if (!why && MoveOnItsWay()) why = "a room change is on its way";
    if (why) {
        Log("follow: debug double request: not now (%s)", why);
        return;
    }
    LocationPacket p = OurRoom();
    auto entry = reinterpret_cast<PFN_RequestTransition>(ExeBase() + kRequestTransition);
    Log("follow: debug double request: asking for our room 0x%02X/0x%02X twice in one frame", p.world, p.room);
    entry(&p, 1, 0, 0, 0);
    entry(&p, 1, 0, 0, 0);
}

void FollowDebugWarp(std::uint8_t world, std::uint8_t room, std::uint8_t door, std::uint16_t map, std::uint16_t btl,
                     std::uint16_t evt) {
    g_debugWarp = {};
    g_debugWarp.world = world;
    g_debugWarp.room = room;
    g_debugWarp.door = door;
    g_debugWarp.map = map;
    g_debugWarp.btl = btl;
    g_debugWarp.evt = evt;
    g_debugWarpPending = true;
    g_debugWarpFrame = g_frame;
    Log("follow: debug warp to world 0x%02X room 0x%02X door 0x%02X programs %u/%u/%u: as soon as it's safe%s", world,
        room, door, map, btl, evt, g_follow ? " (friend side: following the host takes us back to its room)" : "");
}

bool FollowRoomChangeStarting() { return MoveOnItsWay(); }

void FollowHoldRoomChanges(bool hold) {
    if (hold == g_hold) return;
    g_hold = hold;
    if (hold)
        Log("follow: no room change may start until we've left for the title screen");
    else
        Log("follow: room changes allowed again (%u held)", g_held);
    g_held = 0;
}

// Load barrier (load_barrier.cpp): the host has just started loading this room. Load it now instead
// of waiting for the host to settle there. 1 = our load of it has started or is already running,
// 2 = we're already there with the same programs (nothing to load), 3 = a cutscene is running (its
// own move may take us there), 0 = can't now (why says why).
int FollowLoadNow(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                  const char** why) {
    *why = nullptr;
    if (!g_follow) {
        *why = "room following is off";
        return 0;
    }
    // The host's destination counts as the host's room from now on (door lock, settle timer).
    if (world != g_hostWorld || room != g_hostRoom) TrailAdd(world, room);
    g_hostSeenFrame = g_frame;
    g_hostWorld = world;
    g_hostRoom = room;
    g_hostDoor = door;
    std::memcpy(g_hostPrograms, programs, sizeof(g_hostPrograms));
    g_hostSince = g_frame;
    g_givenUp = false;
    if (ReplayRefusedMove(world, room)) return 1;  // our own event's move to the same room, a moment ago
    std::uint8_t ourWorld = Read<std::uint8_t>(kNow), ourRoom = Read<std::uint8_t>(kNow + 1);
    bool sameRoom = world == ourWorld && room == ourRoom;
    if (Read<std::uint8_t>(kInField) == 0) {
        // Already loading: fine if it's the same room (an event's own move, Continue), else not now.
        if (sameRoom) return 1;
        *why = "our game is loading another room";
        return 0;
    }
    if (MoveOnItsWay()) {  // never stack a second request on one whose load hasn't started (see g_asked)
        if (g_asked.world == world && g_asked.room == room) return 1;
        *why = "another room change is on its way";
        return 0;
    }
    if (g_pending) {
        *why = "a follow request is still running";
        return 0;
    }
    if (sameRoom && (programs[0] == 0xFFFF || !OurProgramsDiffer())) return 2;
    if ((*why = NotSafeReason()) != nullptr) return Read<std::uint32_t>(kFrozenGroups) != 0 ? 3 : 0;
    LocationPacket p {};
    p.world = world;
    p.room = room;
    p.door = door;
    p.map = programs[0];
    p.btl = programs[1];
    p.evt = programs[2];
    Log("follow: the host started loading world 0x%02X room 0x%02X (door 0x%02X, programs %u/%u/%u): loading it now",
        world, room, door, p.map, p.btl, p.evt);
    Request(&p, 1, 0, 0, 0);
    ++g_requests;
    g_pending = true;
    g_pendingLoading = false;
    g_pendingWorld = world;
    g_pendingRoom = room;
    std::memcpy(g_pendingPrograms, programs, sizeof(g_pendingPrograms));
    g_pendingFrame = g_frame;
    return 1;
}

void FollowFrame() {
    // On both PCs: the one-request-at-a-time guard (g_asked) works on the host too.
    ++g_frame;
    if (Read<std::uint8_t>(kInField) == 0) {
        g_asked.valid = false;  // the asked-for load has started
        g_skipLoaded = true;
    }
    DebugWarpFrame();
    if (!g_follow) return;
    std::uint8_t world = Read<std::uint8_t>(kNow), room = Read<std::uint8_t>(kNow + 1);
    // Only after a load: when the skip arrives we're often in the host's room already, in the scene
    // whose chain is about to move us.
    if (g_frame < g_skipUntil && g_skipLoaded && Read<std::uint8_t>(kInField) != 0 && world == g_hostWorld &&
        room == g_hostRoom && !OurProgramsDiffer()) {
        g_skipUntil = 0;
        Log("follow: in the host's room after its skip: door lock back on");
    }
    if (g_trailCount > 0 && Read<std::uint8_t>(kInField) != 0) {  // host trail: drop the rooms we've reached
        if (world == g_hostWorld && room == g_hostRoom)
            g_trailCount = 0;
        else if (int i = TrailIndex(world, room); i >= 0)
            TrailDropThrough(i);
    }

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
    // of our room (other programs), that isn't ours? Only a host that is still sending: its last
    // room is no reason to move once its game is gone (TODO 6.1a: the friend was loaded back into
    // the Bailey fight 80 s after the host closed its game).
    if (!HostPresent() || g_frame - g_hostSince < 30) return;
    bool sameRoom = g_hostWorld == world && g_hostRoom == room;
    if (sameRoom && (g_givenUp || g_hostPrograms[0] == 0xFFFF || !OurProgramsDiffer())) return;
    if (MoveOnItsWay()) return;  // our game's own room change: let its load start first
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
    Request(&p, 1, 0, 0, 0);
    ++g_requests;
    g_pending = true;
    g_pendingLoading = false;
    g_pendingWorld = p.world;
    g_pendingRoom = p.room;
    std::memcpy(g_pendingPrograms, g_hostPrograms, sizeof(g_pendingPrograms));
    g_pendingFrame = g_frame;
}

}  // namespace kh2coop
