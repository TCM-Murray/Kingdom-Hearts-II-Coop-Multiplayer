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
// Story fight endings (TODO 5.14, user's pick session 24: either player's ending ends it for both). A
// story fight is a mission: each game runs the mission's script (msn/<lang>/<name>.bar, entry type 3)
// against its own Sora, so the Pete fight ended in the friend's game when its Sora fell to 30% and in the
// host's when every enemy was gone, 20 s apart. Whatever the condition (enemies, HP, timer, counter,
// escort...), 282-293 of the 303 mission scripts end the fight with one of three script commands (trap 4
// of the mission command table exe+0x753490, VERIFIED_OFFSETS 'Mission end commands'):
//   #3  mission complete (exe+0x3A33C0 -> exe+0x3A41E0(mission, 7 or 0x9C, arg): banner, trigger 7)
//   #16 mission end kind 0x10 (exe+0x3A3440 -> exe+0x3A41E0(mission, 0x10, arg): likely 'failed')
//   #22 end without a banner (exe+0x3A37A0(arg): trigger 0xF, e.g. Sora at 30% HP, the story goes on)
// Those three slots of the table are pointed at our wrappers (only mission scripts reach them):
//   host's script ends: runs as usual, END to the friend's game, which runs the same command.
//   friend's script ends first: held, the friend's mission scripts pause (task exe+0x3F4860 skipped),
//                   END_ASK to the host; the host's game runs that command inside its mission task and
//                   sends END; the friend's runs it too. No END within 2 s = the friend's own end runs.
// Missions without these commands (some bosses, minigames) are left as they are.
// Mission failed (#11, exe+0x3A3800: the mission ends with +4 |= 0x14, then Game Over or the mission's own failure
// flow) is the host's (TODO 1.22, "owned by the host ... failed in both games"; mission_sync.cpp makes the host's
// timer, counters and gauges count in the friend's game):
//   host's script fails it: runs as usual, END to the friend's game, which fails it the same way (its Game Over then
//                   waits for the host's choice, game_over.cpp).
//   friend's script fails it while the host's game owns the mission here: left out (the host's failure decides).
//                   With no host connected, or no host mission here, it runs as before.
//
// Events a player accepts (TODO 5.4, your pick 2026-11-02: both must accept). An RC of type 0x7F (Cogsworth's Ask
// for Help) and a dialogue choice (Donald's 'Get on.') run a room settings entry through exe+0x3AABE0 in the game
// that pressed only (VERIFIED_OFFSETS row exe+0x3AABE0). When that entry plays a cutscene and the other game is
// connected, the press is held: the player stays free to move, the INFO bar says they wait, and WANT goes to the
// other game, whose INFO bar says so too. Once both players accepted the same entry in the same place, the host's
// game sends GO and each game runs it in its own Sora's next tick (as the RC does natively). A wait ends without the
// event when its player changes room, a fight starts (battle state != 0) or the other game goes quiet; no time limit.
// Pressing again while waiting does nothing. A press during a fight runs as before (in that game only).
//
// Settings: SCENE_SYNC=1 (default on, both PCs; compared in the settings check).

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <intrin.h>

#include "avatar_link.hpp"
#include "common.hpp"
#include "link_menu.hpp"
#include "mission_sync.hpp"
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

// Mission end commands: trap 4 table, 16-byte entries {fn, u32 flags, pad}; fn(u64 args[]) reads args[0].
constexpr std::uintptr_t kMissionCommands = 0x757250;
struct EndSlot {
    int index;
    std::uintptr_t expected;  // the game's own wrapper (`mov ecx, [rcx]; jmp ...`)
    const char* name;
};
constexpr EndSlot kEnds[] = {{3, 0x4334C0, "mission complete"},
                                {16, 0x4334D0, "mission end (kind 0x10)"},
                                {22, 0x4334F0, "end without banner (trigger 0xF)"},
                                {11, 0x4334E0, "mission failed"}};  // `jmp exe+0x3A3800`, no argument
constexpr int kEndCount = 4;
constexpr int kFailedSlot = 3;  // the host's only (see above)
constexpr std::uintptr_t kMissionTask = 0x3F4860;  // () per frame: mission gauges + up to 8 mission scripts
constexpr std::uint8_t kMissionTaskBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8};
constexpr std::uintptr_t kMission = 0x2A0FF68;     // current mission object; +4 bits 0x14 set once it ended
// The friend's held ending waits for the host's END while the link is up (PeerConnected); the host answers END_NO at
// once when this fight isn't running there. This cap only ends a wait the host never answers (logged). The old 2 s
// ran the friend's own ending under lag, a split (TODO 8.11; 6.3).
constexpr ULONGLONG kEndCapMs = 10000;

// Accepted events (5.4).
constexpr std::uintptr_t kRunSettings = 0x3AABE0;  // (settings, arg) -> u64
constexpr std::uint8_t kRunSettingsBytes[] = {0x40, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x28};
constexpr std::uintptr_t kChoiceReturn = 0x3BCFBB;  // the choice window's call (exe+0x3BCFB6) returns here
constexpr std::uintptr_t kBattle = 0x2A11404;       // u32 battle state, != 0 in a fight
constexpr std::uintptr_t kListHead = 0x2A171C8;     // our Sora
constexpr std::uintptr_t kInfoBar = 0x17B710;       // (text): the top-left INFO bar (story_sync.cpp)
constexpr std::uint8_t kInfoBarBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uint64_t kWantEvery = 30;   // frames between WANTs while we wait
// The bar stays until both accepted (you, 2026-11-02): it slides out when its frames left (f32 bar+0x4A0, 175 when
// shown) reach 0, so we top them up. Showing the text again instead restarts its fade and typing (bench).
constexpr std::uintptr_t kInfoBarObj = 0xABE828;  // *: the INFO bar object
constexpr std::uintptr_t kInfoLeft = 0x4A0;
constexpr float kInfoFull = 175.0f;
constexpr ULONGLONG kWantStaleMs = 3000;   // the other player's wait is forgotten this long after its last WANT

constexpr std::uint32_t kMagic = 0x5432484B;  // "KH2T"
constexpr std::uint16_t kVersion = 4;  // v4: accepted events (WANT / GO / CANCEL)
// Packet fields for the end kinds: group = end command slot (0..2), settings = its argument.
// For WANT / GO: settings = the settings entry, group = its argument (as u16).
enum : std::uint8_t { kAsk = 1, kFire = 2, kEndAsk = 3, kEnd = 4, kWant = 5, kGo = 6, kCancel = 7, kEndNo = 8 };
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
std::uint16_t g_seq = 0;

// Friend: the area we hold.
std::uint16_t g_heldGroup = 0xFFFF;
std::uint64_t g_lastAsk = 0;
std::uint32_t g_held = 0, g_runs = 0, g_asks = 0;

// ---- mission ends ----
using PFN_Command = void(__fastcall*)(std::uint64_t*);
using PFN_Task = void(__fastcall*)();
PFN_Command g_realEnd[kEndCount] = {};
PFN_Task g_realMissionTask = nullptr;
bool g_endsOn = false;
// The ending to run inside our next mission task (host: the friend's ask; friend: the host's END).
struct EndOrder {
    Place where;
    int slot;
    std::uint32_t arg;
    bool valid;
} g_endOrder {};
// Friend: our own ending, held while we wait for the host.
struct HeldEnd {
    Place where;
    int slot;
    std::uint32_t arg;
    ULONGLONG since, lastAsk;
    bool valid;
    bool refused;  // the host answered END_NO: not in this fight there
} g_heldEnd {};
// The place whose mission already ended here (later end commands of that mission are left alone on the
// friend: the host's END decided it), and the host's END being repeated.
Place g_endedAt {};
bool g_endedValid = false;
struct SentEnd {
    Place where;
    int slot;
    std::uint32_t arg;
    std::uint16_t seq;
    bool valid;
} g_sentEnd {};
std::uint16_t g_lastEndSeq = 0;
bool g_sawEnd = false;
bool g_runningOrder = false;  // our own call of a held/ordered ending: the wrapper lets it through

// ---- accepted events (5.4) ----
using PFN_RunSettings = std::uint64_t(__fastcall*)(std::int32_t settings, std::int32_t arg);
using PFN_InfoBar = void(__fastcall*)(const std::uint8_t*);
PFN_RunSettings g_realRunSettings = nullptr;
PFN_InfoBar g_infoBar = nullptr;
bool g_eventsOn = false;
struct Wait {
    Place where;
    std::uint32_t settings;
    std::int16_t arg;
    ULONGLONG since;  // ours: when we pressed; theirs: their last WANT
    bool valid;
};
Wait g_mine {}, g_theirs {};
std::uint64_t g_lastWant = 0, g_lastInfo = 0;
struct Run {
    std::uint32_t settings;
    std::int16_t arg;
    bool valid;
} g_run {};
bool g_runningEvent = false;  // our own call of an accepted event: the hook lets it through
std::uint16_t g_goSeq = 0, g_lastGoSeq = 0;
bool g_sawGo = false;
Wait g_goSent {};
std::uint32_t g_eventHolds = 0, g_eventRuns = 0, g_eventCancels = 0;

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
    // One-off events go once over the reliable channel; ASK, END_ASK and WANT are requests repeated until answered.
    if (kind == kFire || kind == kEnd || kind == kGo || kind == kCancel)
        AvatarLinkSendReliable(&p, sizeof(p));
    else
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
            bool told = PeerConnected();
            if (told) Send(kFire, here, id, settings, g_fired.seq);
            Log("scene sync: walk-in area %u (settings %u, scene '%s') runs %s; %s", id, settings,
                EventName(event, name), allowed ? "for the friend" : "(our Sora walked in)",
                told ? "telling the friend's game to run it too" : "no friend connected");
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

bool MissionRunning() {
    std::uintptr_t mission = Read<std::uintptr_t>(ExeBase() + kMission);
    return mission && (Read<std::uint32_t>(mission + 4) & 0x14) == 0;
}

void RunEnd(int slot, std::uint32_t arg) {
    std::uint64_t args[8] = {arg};
    g_runningOrder = true;
    g_realEnd[slot](args);
    g_runningOrder = false;
}

// A mission script's end command (slot 0..2 of kEnds).
void EndCommand(int slot, std::uint64_t* args) {
    if (g_runningOrder || !g_endsOn || !CoopActive()) {  // the table patch stays in place: co-op off = pass through
        g_realEnd[slot](args);
        return;
    }
    std::uint32_t arg = static_cast<std::uint32_t>(args[0]);
    Place here = Here();
    if (g_host) {
        g_realEnd[slot](args);
        if (!g_sentEnd.valid || !SamePlace(g_sentEnd.where, here)) {  // this mission's first ending
            g_sentEnd = {here, slot, arg, ++g_seq, true};
            bool told = PeerConnected();
            if (told) Send(kEnd, here, static_cast<std::uint16_t>(slot), arg, g_sentEnd.seq);
            Log("scene sync: our mission script ended the fight: %s (argument %u) in 0x%02X/0x%02X programs %u/%u/%u; "
                "%s", kEnds[slot].name, arg, here.world, here.room, here.programs[0], here.programs[1],
                here.programs[2], told ? "telling the friend's game to end it the same way" : "no friend connected");
        }
        return;
    }
    if (!PeerConnected()) {
        Log("scene sync: our mission script ended the fight: %s (argument %u); no host connected", kEnds[slot].name, arg);
        g_realEnd[slot](args);
        return;
    }
    if (g_endedValid && SamePlace(g_endedAt, here)) {
        Log("scene sync: our mission script asked for %s (argument %u) after the host's ending: left out",
            kEnds[slot].name, arg);
        return;
    }
    if (slot == kFailedSlot) {
        static std::uint64_t lastLog = 0;
        if (MissionSyncHostOwnsHere()) {
            if (lastLog == 0 || g_frame - lastLog > 120) {
                lastLog = g_frame;
                Log("scene sync: our mission script failed the mission in 0x%02X/0x%02X programs %u/%u/%u while the "
                    "host's game owns it: left out (the host's failure decides)", here.world, here.room, here.programs[0],
                    here.programs[1], here.programs[2]);
            }
            return;
        }
        Log("scene sync: our mission script failed the mission; no host mission here: it runs");
        g_realEnd[slot](args);
        return;
    }
    if (!g_heldEnd.valid) {
        g_heldEnd = {here, slot, arg, GetTickCount64(), 0, true, false};
        Log("scene sync: our mission script ended the fight: %s (argument %u) in 0x%02X/0x%02X programs %u/%u/%u: held, "
            "mission scripts paused; asking the host's game to end it for both", kEnds[slot].name, arg, here.world,
            here.room, here.programs[0], here.programs[1], here.programs[2]);
    }
}

void __fastcall EndCommand0(std::uint64_t* args) { EndCommand(0, args); }
void __fastcall EndCommand1(std::uint64_t* args) { EndCommand(1, args); }
void __fastcall EndCommand2(std::uint64_t* args) { EndCommand(2, args); }
void __fastcall EndCommand3(std::uint64_t* args) { EndCommand(3, args); }
constexpr PFN_Command kEndWrappers[kEndCount] = {EndCommand0, EndCommand1, EndCommand2, EndCommand3};

// The mission task: runs an ordered ending first; on the friend, pauses the mission while ours is held.
void __fastcall HookMissionTask() {
    __try {
        Place here = Here();
        if (g_endOrder.valid) {
            EndOrder order = g_endOrder;
            g_endOrder.valid = false;
            if (SamePlace(order.where, here)) {
                RunEnd(order.slot, order.arg);
                if (g_host) {
                    g_sentEnd = {here, order.slot, order.arg, ++g_seq, true};
                    Send(kEnd, here, static_cast<std::uint16_t>(order.slot), order.arg, g_sentEnd.seq);
                    Log("scene sync: ended the fight for the friend: %s (argument %u); telling the friend's game",
                        kEnds[order.slot].name, order.arg);
                } else {
                    bool ours = g_heldEnd.valid;
                    g_heldEnd.valid = false;
                    g_endedAt = here;
                    g_endedValid = true;
                    Log("scene sync: ended the fight as the host's game did: %s (argument %u)%s", kEnds[order.slot].name,
                        order.arg, ours ? "; our held ending dropped" : "");
                }
            }
        }
        if (!g_host && g_heldEnd.valid) {
            ULONGLONG now = GetTickCount64();
            if (!SamePlace(g_heldEnd.where, here)) {
                g_heldEnd.valid = false;
            } else if (g_heldEnd.refused || !PeerConnected() || now - g_heldEnd.since > kEndCapMs) {
                HeldEnd held = g_heldEnd;
                g_heldEnd.valid = false;
                g_endedAt = here;
                g_endedValid = true;
                Log("scene sync: %s: our own ending runs (%s, argument %u, after %llu ms)",
                    held.refused ? "the host's game isn't in this fight" : !PeerConnected() ? "no word from the host's game"
                                                                                           : "no answer from the host's game",
                    kEnds[held.slot].name, held.arg, static_cast<unsigned long long>(now - held.since));
                RunEnd(held.slot, held.arg);
            } else {
                if (now - g_heldEnd.lastAsk >= 250) {
                    g_heldEnd.lastAsk = now;
                    Send(kEndAsk, g_heldEnd.where, static_cast<std::uint16_t>(g_heldEnd.slot), g_heldEnd.arg, 0);
                }
                return;  // mission paused while we wait
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_endOrder.valid = g_heldEnd.valid = false;
        g_runningOrder = false;
        Log("scene sync: exception in the mission task; ending orders dropped");
    }
    g_realMissionTask();
}

bool PatchEndCommands() {
    auto* table = reinterpret_cast<std::uintptr_t*>(ExeBase() + kMissionCommands);
    for (const EndSlot& e : kEnds)
        if (table[e.index * 2] != ExeBase() + e.expected) {
            Log("scene sync: mission command #%d isn't the expected one (different game build?); fight endings not "
                "shared", e.index);
            return false;
        }
    for (int i = 0; i < kEndCount; ++i) {
        std::uintptr_t* slot = &table[kEnds[i].index * 2];
        g_realEnd[i] = reinterpret_cast<PFN_Command>(*slot);
        DWORD old;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return false;
        *slot = reinterpret_cast<std::uintptr_t>(kEndWrappers[i]);
        VirtualProtect(slot, sizeof(*slot), old, &old);
    }
    return true;
}

// ---- accepted events (5.4) ----
void Info(int which) {
    static const char* const kText[] = {"Waiting for the other player to accept.",
                                        "The other player waits to start an event.", "The event was called off."};
    static std::uint8_t text[3][64];
    g_lastInfo = g_frame;
    if (!g_infoBar || Read<std::uint8_t>(ExeBase() + kInField) == 0) return;
    LinkMenuEncode(kText[which], text[which], sizeof(text[which]));
    g_infoBar(text[which]);
}

// Keeps the bar up: its frames left topped up; shown again only if it closed (something else used it).
void KeepInfo(int which) {
    __try {
        std::uintptr_t bar = Read<std::uintptr_t>(ExeBase() + kInfoBarObj);
        auto* left = bar ? reinterpret_cast<volatile float*>(bar + kInfoLeft) : nullptr;
        if (left && *left > 0.0f) {
            if (*left < kInfoFull - 60.0f) *left = kInfoFull;
        } else if (g_frame - g_lastInfo >= 30) {
            Info(which);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CloseInfo() {
    __try {
        std::uintptr_t bar = Read<std::uintptr_t>(ExeBase() + kInfoBarObj);
        auto* left = bar ? reinterpret_cast<volatile float*>(bar + kInfoLeft) : nullptr;
        if (left && *left > 0.0f) *left = 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void SendWait(std::uint8_t kind, const Wait& w, std::uint16_t seq) {
    Send(kind, w.where, static_cast<std::uint16_t>(w.arg), w.settings, seq);
}

bool SameEvent(const Wait& a, const Wait& b) {
    return a.settings == b.settings && a.arg == b.arg && SamePlace(a.where, b.where);
}

void CancelMine(const char* why, bool show) {
    if (!g_mine.valid) return;
    g_mine.valid = false;
    ++g_eventCancels;
    if (PeerConnected()) SendWait(kCancel, g_mine, 0);
    // Not on a room change: the bar is rebuilt as the load starts (the friend crashed at exe+0x16AA77, bench 20:39:16).
    if (show) Info(2);
    Log("scene sync: our wait for settings %u ended without the event: %s", g_mine.settings, why);
}

// Our player accepted an event: true = held (the caller doesn't run it).
bool HoldEvent(std::uint32_t settings, std::int16_t arg, const char* how) {
    if (!g_eventsOn || g_runningEvent) return false;
    std::uint32_t event = 0;
    __try {
        event = SettingsEvent(settings, static_cast<std::uint16_t>(arg));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        event = 0;
    }
    char name[5];
    if (!event) return false;  // no cutscene: not ours to share
    if (!PeerConnected() || Read<std::uint32_t>(ExeBase() + kBattle) != 0) {
        Log("scene sync: our %s runs settings %u (scene '%s') here only: %s", how, settings, EventName(event, name),
            PeerConnected() ? "a fight is on" : "no other player connected");
        return false;
    }
    Wait w {Here(), settings, arg, GetTickCount64(), true};
    if (g_mine.valid && SameEvent(g_mine, w)) return true;  // pressed again while waiting: nothing new
    g_mine = w;
    g_lastWant = g_frame;
    ++g_eventHolds;
    SendWait(kWant, g_mine, 0);
    bool theyWait = g_theirs.valid && SameEvent(g_theirs, g_mine);
    if (!theyWait) Info(0);
    Log("scene sync: our %s would start settings %u (scene '%s') in 0x%02X/0x%02X programs %u/%u/%u: held, %s", how,
        settings, EventName(event, name), w.where.world, w.where.room, w.where.programs[0], w.where.programs[1],
        w.where.programs[2], theyWait ? "the other player accepted it already" : "waiting for the other player");
    return true;
}

std::uint64_t __fastcall HookRunSettings(std::int32_t settings, std::int32_t arg) {
    if (!g_runningEvent &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == ExeBase() + kChoiceReturn &&
        HoldEvent(static_cast<std::uint32_t>(settings), static_cast<std::int16_t>(arg), "dialogue choice"))
        return 1;
    return g_realRunSettings(settings, arg);
}

void EventsFrame() {
    bool loading = Read<std::uint8_t>(ExeBase() + kInField) == 0;
    if (g_mine.valid) {
        if (loading || !SamePlace(g_mine.where, Here()))
            CancelMine("we left the room", false);
        else if (Read<std::uint32_t>(ExeBase() + kBattle) != 0)
            CancelMine("a fight started", true);
        else if (!PeerConnected())
            CancelMine("the other game went quiet", true);
        else if (g_frame - g_lastWant >= kWantEvery) {
            g_lastWant = g_frame;
            SendWait(kWant, g_mine, 0);
        }
    }
    if (g_theirs.valid && (loading || GetTickCount64() - g_theirs.since > kWantStaleMs)) {
        g_theirs.valid = false;
        Log("scene sync: the other player's wait for settings %u is over (no word from it)", g_theirs.settings);
    }
    if (g_host && g_mine.valid && g_theirs.valid && SameEvent(g_mine, g_theirs)) {
        g_goSent = g_mine;
        ++g_goSeq;
        SendWait(kGo, g_goSent, g_goSeq);
        g_run = {g_mine.settings, g_mine.arg, true};
        g_mine.valid = g_theirs.valid = false;
        Log("scene sync: both players accepted settings %u: it runs in both games", g_goSent.settings);
    }
    if (loading) g_run.valid = false;
    int info = g_mine.valid ? 0 : g_theirs.valid && SamePlace(g_theirs.where, Here()) ? 1 : -1;
    if (info >= 0 && !loading) KeepInfo(info);
}

void OnEventPacket(std::uint8_t kind, const Place& where, const Packet& p) {
    Wait w {where, p.settings, static_cast<std::int16_t>(p.group), GetTickCount64(), true};
    if (kind == kWant) {
        bool fresh = !g_theirs.valid || !SameEvent(g_theirs, w);
        g_theirs = w;
        if (!fresh) return;
        bool here = SamePlace(where, Here());
        Log("scene sync: the other player accepted settings %u in 0x%02X/0x%02X programs %u/%u/%u%s", p.settings,
            where.world, where.room, where.programs[0], where.programs[1], where.programs[2],
            here ? "" : " (not where we are)");
        if (here && !(g_mine.valid && SameEvent(g_mine, w))) Info(1);
    } else if (kind == kCancel) {
        if (g_theirs.valid && SameEvent(g_theirs, w)) {
            g_theirs.valid = false;
            Log("scene sync: the other player's wait for settings %u ended", p.settings);
        }
    } else if (kind == kGo && !g_host) {
        if (g_sawGo && p.seq == g_lastGoSeq) return;
        g_sawGo = true;
        g_lastGoSeq = p.seq;
        g_mine.valid = g_theirs.valid = false;
        if (!SamePlace(where, Here())) {
            Log("scene sync: the host's game runs settings %u in 0x%02X/0x%02X; we're elsewhere: not run here",
                p.settings, where.world, where.room);
            return;
        }
        g_run = {p.settings, static_cast<std::int16_t>(p.group), true};
        Log("scene sync: the host's game runs settings %u: running it here too", p.settings);
    }
}

}  // namespace

bool SceneSyncHoldEventRc(std::uint32_t settings) { return HoldEvent(settings, 0, "RC"); }

// Our Sora's tick (puppet.cpp HookPlayerTick, before the game's): an accepted event runs here, where the RC runs it.
void SceneSyncOurTick(std::uintptr_t actor) {
    if (!g_run.valid || actor != Read<std::uintptr_t>(ExeBase() + kListHead)) return;
    Run r = g_run;
    g_run.valid = false;
    CloseInfo();
    g_runningEvent = true;
    std::uint64_t result = 0;
    __try {
        result = g_realRunSettings(static_cast<std::int32_t>(r.settings), r.arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("scene sync: exception running settings %u", r.settings);
    }
    g_runningEvent = false;
    ++g_eventRuns;
    Log("scene sync: settings %u run for both players (result %llu)", r.settings,
        static_cast<unsigned long long>(result));
}

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
    if (!g_on) return;
    // Fight endings: the task hook first, so a failed hook leaves the command table untouched.
    g_endsOn = HookFunction(kMissionTask, kMissionTaskBytes, sizeof(kMissionTaskBytes),
                            reinterpret_cast<void*>(HookMissionTask), reinterpret_cast<void**>(&g_realMissionTask),
                            "MissionTask (scene sync)") &&
               PatchEndCommands();
    Log("scene sync: story fight endings %s", g_endsOn ? "shared (whoever's mission ends first ends it in both games)"
                                                       : "NOT shared");
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kInfoBar), kInfoBarBytes, sizeof(kInfoBarBytes)) == 0)
        g_infoBar = reinterpret_cast<PFN_InfoBar>(ExeBase() + kInfoBar);
    g_eventsOn = HookFunction(kRunSettings, kRunSettingsBytes, sizeof(kRunSettingsBytes),
                              reinterpret_cast<void*>(HookRunSettings), reinterpret_cast<void**>(&g_realRunSettings),
                              "RunSettings (scene sync)");
    Log("scene sync: events from an RC or a dialogue choice %s", g_eventsOn ? "wait until both players accepted"
                                                                             : "NOT shared (hook failed)");
}

void SceneSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    // A room load (also a reload of the same room, e.g. Continue into the same fight) starts afresh.
    if (Read<std::uint8_t>(ExeBase() + kInField) == 0) {
        g_fired.valid = g_allowed.valid = false;
        g_sentEnd.valid = g_endedValid = g_endOrder.valid = g_heldEnd.valid = false;
    }
    // Friend: forget the held area once our game stops testing it (we walked out, or the room changed).
    if (!g_host && g_heldGroup != 0xFFFF && g_frame - g_lastAsk > 2 * kAskEvery) {
        Log("scene sync: we left area %u before the host's game answered", g_heldGroup);
        g_heldGroup = 0xFFFF;
    }
    if (g_eventsOn) EventsFrame();
    if (g_frame % 3600 == 0 && (g_runs || g_held || g_eventHolds))
        Log("scene sync: areas run %u, held %u, asks sent %u; accepted events held %u, run %u, called off %u", g_runs,
            g_held, g_asks, g_eventHolds, g_eventRuns, g_eventCancels);
}

void SceneSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    Place where {p.world, p.room, {}};
    std::memcpy(where.programs, p.programs, sizeof(where.programs));
    Place here = Here();
    if (p.kind == kWant || p.kind == kGo || p.kind == kCancel) {
        if (g_eventsOn) OnEventPacket(p.kind, where, p);
        return;
    }
    if (p.kind == kEndNo) {
        if (!g_host && g_heldEnd.valid && SamePlace(g_heldEnd.where, where) && p.group == g_heldEnd.slot)
            g_heldEnd.refused = true;  // HookMissionTask runs our own ending
        return;
    }
    if (p.kind == kEndAsk || p.kind == kEnd) {
        if (p.group >= kEndCount) return;
        if (!g_endsOn) {
            if (g_host && p.kind == kEndAsk) Send(kEndNo, where, p.group, p.settings, 0);
            return;
        }
        if (g_host && p.kind == kEndAsk) {
            if (p.group == kFailedSlot) return;  // the friend's failure isn't asked for (the host's decides)
            if (g_sentEnd.valid && SamePlace(g_sentEnd.where, where)) {
                // ended already: the friend still asks (ours not confirmed yet, or given up): tell it again
                Send(kEnd, g_sentEnd.where, static_cast<std::uint16_t>(g_sentEnd.slot), g_sentEnd.arg, g_sentEnd.seq);
                return;
            }
            if (g_endOrder.valid) return;
            if (!SamePlace(where, here) || !MissionRunning()) {
                Send(kEndNo, where, p.group, p.settings, 0);  // an answer to each ask, like the ask itself
                static std::uint64_t lastLog = 0;
                if (g_frame - lastLog > 120) {
                    lastLog = g_frame;
                    Log("scene sync: the friend's fight ended (%s), but we're in 0x%02X/0x%02X programs %u/%u/%u%s: "
                        "not ended here", kEnds[p.group].name, here.world, here.room, here.programs[0], here.programs[1],
                        here.programs[2], SamePlace(where, here) ? " with no mission running" : "");
                }
                return;
            }
            g_endOrder = {where, p.group, p.settings, true};
            Log("scene sync: the friend's fight ended first (%s, argument %u): ending ours the same way",
                kEnds[p.group].name, p.settings);
            return;
        }
        if (!g_host && p.kind == kEnd) {
            if (g_sawEnd && p.seq == g_lastEndSeq) return;
            g_sawEnd = true;
            g_lastEndSeq = p.seq;
            if (!SamePlace(where, here) || (g_endedValid && SamePlace(g_endedAt, here))) {
                Log("scene sync: the host's fight ended (%s) in 0x%02X/0x%02X programs %u/%u/%u; %s",
                    kEnds[p.group].name, p.world, p.room, p.programs[0], p.programs[1], p.programs[2],
                    SamePlace(where, here) ? "ours already ended" : "we're elsewhere: not ended here");
                return;
            }
            g_endOrder = {where, p.group, p.settings, true};
            Log("scene sync: the host's fight ended (%s, argument %u): ending ours the same way", kEnds[p.group].name,
                p.settings);
        }
        return;
    }
    if (g_host && p.kind == kAsk) {
        if (g_fired.valid && g_fired.group == p.group && SamePlace(g_fired.where, where)) {
            // ran already: the friend still asks (ours not confirmed yet, or given up): tell it again
            Send(kFire, g_fired.where, g_fired.group, g_fired.settings, g_fired.seq);
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
        // No number check: each FIRE arrives once (reliable); a repeat is the host answering our ASK, which must
        // allow the area again (a FIRE that came in our room load's last frame was cleared with the load:
        // bench 2026-11-04, the friend held area 45 and asked 190 times). The area's done flag stops a second run.
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
