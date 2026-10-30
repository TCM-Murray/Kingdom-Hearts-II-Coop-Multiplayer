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
//   Strikes: a script that lands an attack on the friend's Sora copy (trap_attack_new + trap_attack_strike +
//           trap_attack_free, all in one frame) only moves the copy, which our puppet puts back where the friend is:
//           Armored Xemnas II's push (attacks 269, 1703, 1704) and the dragon's blow (286) never threw the friend's
//           Sora, so the dragon kept striking the copy and the host was kept far out (VERIFIED_OFFSETS). The host
//           sends each such strike (attacker, attack id, combo group, flags); the friend makes the same attack from
//           its copy of the attacker and lands it on its own Sora, so its game throws it the same way (your pick:
//           every strike, 2026-10-14). The damage still comes only from the host (WorldSyncBlocksDamage).
//   Vacuums: between those strikes the script pulls the struck Sora away with a vacuum (trap_vacuum_create at
//           the far end of the road, (0, -8000, -55000) in Armored Xemnas II, its speeds raised every frame,
//           trap_vacuum_destroy), then throws it. The copy can't be pulled (our puppet puts it where the friend is)
//           and the friend's game had no vacuum, so Xemnas never threw the friend's Sora (bench 2026-10-14 22:40). The host sends each vacuum call; the friend makes, changes and
//           ends the same vacuum in its own game (the game's vacuum list exe+0x2AE7DC0, VERIFIED_OFFSETS), which
//           pulls its own Sora.
//   More calls (your pick 2026-10-15, from the list of what bosses call that the friend's game never ran; each
//           checked in Ghidra: none reads the running script): an actor's tilt (trap_obj_set_rot), texture
//           animations, shadow hide/show, scissoring; effects started at a place (trap_obj_effect_start_pos, Final
//           Xemnas) and paused; the room's background parts (trap_bg_hide / show) and lights (trap_light_*) when an
//           enemy's or field object's script changes them. Calls a script repeats every frame with the same values
//           (Roxas's Keyblades hide their shadow every frame) are sent once a second at most.
//   States: what a script sets every frame goes in its own "KH2Y" packet instead, the latest value only (the
//           event queue would drown): where the host's effects are, their scale, rotation and colour
//           (trap_effect_set_pos / scale / rot / dir / color: the friend's effects stayed where they started), and
//           an actor's free rotation (trap_obj_set_xyzrot, Data Roxas every frame). The friend applies each at the
//           host's time its enemies are shown at (AvatarLinkPlaybackSenderMs), so they stay together.
//   Prizes, sheets, missions (you, 2026-10-15: "Implement 1, 2 and 4"): HP orbs, munny and drive orbs a living enemy's or
//           field object's script scatters (trap_obj_scatter_prize / _mu / _tr: the Fire and Ice Lords, Luxord's cards,
//           the Beast's castle flowers) or makes appear at a place (trap_prize_appear / _tr / _num / _xaldin), so the
//           friend gets its own; a dead enemy's drops stay with the friend's own copy (its brain is back, it drops them
//           itself). An enemy's damage sheet (trap_btlobj_set_sheet / dup_sheet) and its element rates
//           (trap_sheet_set_element_rate: Data Roxas takes no damage from element 6 between his moves, the Twin Lords
//           none from fire / ice), so the damage the friend's game works out for its hits (its claims, world_sync.cpp)
//           follows the same rules. A boss's mission setup (show the HUD, set the timer, a counter's start and end, the
//           information messages: Data Demyx's B_EX120_HB_LV99, Luxord's B_EX150_LV99); the values themselves are
//           mission_sync.cpp's.
// The script calls come through the call log's thunks (world_census.cpp), which go in when this is on.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x5632484B;  // "KH2V"
constexpr std::uint16_t kVersion = 4;  // v4: prizes, sheets, element rates, mission setup
constexpr std::uintptr_t kListHead = 0x2A171C8, kNextActor = 0xA90, kObjRow = 0x918, kTeam = 0x4DC;
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;  // (object) -> handle
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // "@ADR"
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
using PFN_Trap = void(__fastcall*)(std::uint64_t* args);

// The calls we mirror. Actor: the first argument is the actor, the rest are numbers. EffectStart: same, returns
// an effect handle. EffectEnd: the first argument is an effect handle. EffectCall: the same, the effect goes on.
// EffectStartPos: actor, effect id, a script vector (the place), two numbers. Room: numbers only (the room's
// background), Light: a light (a fixed slot, exe+0x2AE7B98 for light 0, the same in both games) and numbers; both
// sent only when an enemy's or field object's script calls them. State: set every frame, "KH2Y" packets.
// Prize: actor and a number, sent only while the actor lives. PrizeAt: a script vector (the place) and numbers, the
// friend calls the game function behind it. SheetRate: an actor's sheet (sent as its owner) and numbers. Mission:
// numbers only, a mission setup call an enemy's or field object's script makes.
// (Decompiled: none of them reads the running script, so they can run from our frame code.)
enum Kind : std::uint8_t {
    kNone, kActor, kEffectStart, kEffectEnd, kEffectCall, kEffectStartPos, kRoom, kLight, kState,
    kPrize, kPrizeAt, kSheetRate, kMission
};
Kind KindOf(int table, int index) {
    if (table == 1) {
        switch (index) {
            case 1:              // trap_obj_set_rot (exe+0x3B6560: actor+0x680 from an angle, +0x9B8 bit 3 off)
            case 85: case 86:    // trap_obj_texanm_start / stop (exe+0x3CD8A0 / 0x3CD8B0 on the render object)
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
            // trap_obj_motion_speed (exe+0x42FCE0: actor+0x2A8 = the speed). Armored Xemnas II's dragon B_EX180 sets 0
            // in the intro and 1 five seconds later, after its brain is skipped: its animation (Xemnas's platform)
            // stood still in the friend's game (bench 2026-10-14 22:03).
            case 107:
                return kActor;
            // trap_obj_scatter_prize (exe+0x3B6170(actor, id)), _mu (munny, exe+0x414220 at the actor), _tr
            // (exe+0x42A980 at the actor); _tt (240) does nothing
            case 92: case 226: case 278:
                return kPrize;
            // trap_prize_appear_tr (exe+0x42A980(place, n)), _num (exe+0x3DC3D0(place, id, n)), _xaldin
            // (exe+0x42AD40(place, n))
            case 279: case 342: case 363:
                return kPrizeAt;
            case 311:  // trap_sheet_set_element_rate (exe+0x3C2650(sheet, element, rate))
                return kSheetRate;
            case 87:  // trap_obj_effect_start_bind (exe+0x3B6670)
            case 21:  // trap_obj_effect_start (exe+0x3B6630)
                return kEffectStart;
            case 22:  // trap_obj_effect_start_pos (exe+0x3B66C0(actor, id, place, a, b))
                return kEffectStartPos;
            case 36: case 37:  // trap_bg_hide / bg_show (a part number)
                return kRoom;
            // trap_light_create / set_flag / set_color / fadeout / fadein (Final Xemnas, Larxene)
            case 181: case 182: case 183: case 184: case 251:
                return kLight;
        }
    } else if (table == 0) {
        switch (index) {
            case 51: case 52:  // trap_effect_kill / trap_effect_fadeout
            case 44: case 85:  // trap_effect_loop_end / loop_end_kill: how looping effects end (the chandelier's)
                return kEffectEnd;
            case 66:  // trap_effect_pause (exe+0x2BED00: effect+0xC bit 21)
                return kEffectCall;
            // trap_effect_set_pos / set_scale / set_rot / set_dir / set_color: 4 floats into the effect's part
            case 38: case 39: case 40: case 41: case 67:
                return kState;
        }
    } else if (table == 2) {
        // trap_btlobj_set_sheet (exe+0x411350: frees the old sheet, a new one for the id at actor+0x5C0) and
        // trap_btlobj_dup_sheet (exe+0x4112B0)
        if (index == 9 || index == 95) return kActor;
    } else if (table == 4) {
        switch (index) {
            case 4:                       // information (a message)
            case 7: case 13: case 24:     // restart_timer, pause_timer, cancel_pause_timer
            case 14: case 15:             // activate2d / deactivate2d (the mission HUD)
            case 17: case 18: case 19:    // set_timer_param / set_count_param / set_gauge_param
            case 39: case 45:             // set_watch / disable_watch
            case 44: case 49:             // disable_count / disable_timer
            case 46: case 47: case 50: case 57: case 58:  // warning sound, timer / count / gauge warnings
                return kMission;
        }
    } else if (table == 6) {
        switch (index) {
            case 1:   // trap_prize_appear (exe+0x3DC170(place, id))
                return kPrizeAt;
            case 45:  // trap_obj_hide_shadow (actor+0x124 bit 9)
            case 64:  // trap_obj_show_shadow (exe+0x3CD820)
            case 65:  // trap_obj_set_scissoring (actor+0x8C8 bit 5)
                return kActor;
        }
    } else if (table == 8 && index == 2) {  // trap_obj_set_xyzrot (exe+0x3B65F0: actor+0x680, +0x9B8 bit 3 on)
        return kState;
    }
    return kNone;
}

// What a state call sets, and the game's getter for that field of an effect (effect -> its 4 floats, through the
// effect's part +0x30; Ghidra, 2026-10-15).
enum StateKind : std::uint8_t { kFxPos = 1, kFxScale, kFxRot, kFxColor, kActorRot };
StateKind StateOf(int table, int index) {
    if (table == 8) return kActorRot;
    switch (index) {
        case 38: return kFxPos;
        case 39: return kFxScale;
        case 67: return kFxColor;
        default: return kFxRot;  // 40 set_rot, and 41 set_dir, which the game turns into a rotation first
    }
}
constexpr std::uintptr_t kFxGetter[] = {0, 0x2BE970, 0x2BEA80, 0x2BE1B0, 0x2BE1E0};
// The kinds whose event names an actor (the friend runs them on its copy of it).
bool IsActorKind(Kind kind) {
    return kind == kActor || kind == kEffectStart || kind == kEffectStartPos || kind == kRoom || kind == kLight ||
           kind == kPrize || kind == kPrizeAt || kind == kSheetRate || kind == kMission;
}
// The game functions behind the prize-at-a-place calls (Ghidra, 2026-10-15): each takes the place (4 floats) and the
// call's numbers.
constexpr std::uintptr_t kPrizeAppear = 0x3DC170;        // 6/1   (place, prize id)
constexpr std::uintptr_t kPrizeAppearTr = 0x42A980;      // 1/279 (place, n)
constexpr std::uintptr_t kPrizeAppearNum = 0x3DC3D0;     // 1/342 (place, prize id, n)
constexpr std::uintptr_t kPrizeAppearXaldin = 0x42AD40;  // 1/363 (place, n)
// An actor's damage sheet (actor+0x5C0; HP = its first int): +0x268 the owner's handle, element rates from +0x1A8.
constexpr std::uintptr_t kActorSheet = 0x5C0, kSheetOwner = 0x268;
constexpr std::uintptr_t kSetElementRate = 0x3C2650;  // (sheet, element, rate in %)
// The prize manager (exe+0x3DC3D0 makes each orb with exe+0x413F00(manager, ...)): its pool at +0xBF8, whose +0x30
// counts the free places (an orb takes one). Read before and after a replayed prize call, to log how many it made.
constexpr std::uintptr_t kPrizeManager = 0x2A24E40, kPrizePool = 0xBF8, kPrizeFree = 0x30;
int PrizeFree() {
    __try {
        std::uintptr_t m = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kPrizeManager);
        std::uintptr_t pool = m ? *reinterpret_cast<const std::uintptr_t*>(m + kPrizePool) : 0;
        return pool ? *reinterpret_cast<const std::int32_t*>(pool + kPrizeFree) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
constexpr std::uintptr_t kActorSetXyzRot = 0x3B65F0, kActorRotVec = 0x680;
constexpr std::uintptr_t kEffectStartAt = 0x3B66C0;
using PFN_FxField = float*(__fastcall*)(std::uintptr_t effect);

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
    std::uintptr_t actor;  // not sent: the actor while world sync has no number for it yet (netId 0)
    std::uint32_t frame;   // our frame it was queued in
};
Queued g_queue[128];
int g_queueNext = 0;
std::uint32_t g_mirrorFrame = 0;
std::uint32_t g_nextId = 1;
std::uint32_t g_hostEffects[64];  // effect handles we sent a start for (their end is sent too)
// Their part (+0x30) at the start: an effect that ends by itself leaves its handle here, and the game gives the
// handle to a new effect (Goofy's set_dir calls went out as Final Xemnas's effect's, bench 2026-10-15 00:53).
std::uintptr_t g_hostEffectParts[64];
int g_hostEffectNext = 0;
std::uint32_t g_sentEvents = 0;
// Vacuums (trap_vacuum_create, exe+0x433200): a 0x34-byte record in the list head exe+0x2AE7DC0 / tail
// exe+0x2AE7DC8, linked by the handle at +0x30: +0x00 place (4 floats), +0x10 types it ignores (bits), +0x18/+0x1C/
// +0x20 speeds, +0x24 turn speed, +0x28 near range (100), +0x2C distance rate (0.01). exe+0x3F9130 adds up their
// pull on an actor as it moves. Made with exe+0x152430(0x34), unlinked with exe+0x3F8FB0, freed with exe+0x152570.
constexpr std::uintptr_t kVacuumHead = 0x2AE7DC0, kVacuumTail = 0x2AE7DC8;
constexpr std::uintptr_t kAlloc = 0x152430, kFree = 0x152570, kVacuumUnlink = 0x3F8FB0;
bool IsVacuumCall(int table, int index) { return table == 2 && index >= 77 && index <= 84; }
std::uint32_t g_hostVacuums[16];  // host: the vacuums whose making we sent
int g_hostVacuumNext = 0;
std::uint32_t g_vacuumsSent = 0;
struct VacuumPair {
    std::uint32_t host;
    std::uintptr_t ours;
};
VacuumPair g_vacuums[16];  // friend
int g_vacuumNext = 0;
std::uint32_t g_vacuumsMade = 0, g_vacuumCalls = 0;

// States ("KH2Y"): one entry per effect field or actor rotation, the latest value; the host sends each for 3
// frames after it changes (a lost packet is made up by the next one).
constexpr std::uint32_t kStateMagic = 0x5932484B;  // "KH2Y"
constexpr std::uint16_t kStateVersion = 1;
#pragma pack(push, 1)
struct StateEntry {
    std::uint8_t kind;      // StateKind
    std::uint32_t key;      // the host's effect handle, or the actor as an exe offset
    std::uint16_t netId;    // actor rotation: world sync's number for the actor, and its room instance
    std::uint16_t instance;
    std::uint32_t objId;
    float v[4];
};
struct StatePacket {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t senderMs;  // the host's link clock (AvatarLinkNowMs)
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kStatePerPacket = 44;  // 15 + 44 * 29 = 1291 bytes
struct HostState {
    StateEntry e;
    std::uint8_t sendsLeft;
};
HostState g_hostStates[128];
int g_stateNext = 0, g_stateSendFrom = 0;
std::uint32_t g_statesSent = 0;
// Host: calls a script repeats with the same values (a shadow hidden every frame): sent again after 1 s at most,
// so an actor bound late still gets it.
struct Toggle {
    std::uintptr_t actor;
    std::uint8_t family;
    std::uint32_t argKey;
    std::uint16_t index;
    std::uint64_t value;
    std::uint64_t at;
};
Toggle g_toggles[64];
int g_toggleNext = 0;
std::uint32_t g_repeatsSkipped = 0;
// Friend: state entries waiting for the host's time our enemies are shown at.
struct Pending {
    std::uint32_t senderMs;
    std::uint64_t arrived;
    StateEntry e;
};
Pending g_pending[512];
int g_pendingHead = 0, g_pendingTail = 0;
std::uint32_t g_statesApplied = 0, g_statesNoEffect = 0, g_statesNoActor = 0;

// Host: the attacks scripts made (trap_attack_new), until freed, so a strike on the copy can name its attack.
struct HostAttack {
    std::uint64_t attack;  // the script's value for it (what trap_attack_strike / free get)
    std::uintptr_t owner;
    std::uint64_t id, group, flags;  // trap_attack_new's arguments as the script passed them
};
HostAttack g_attacks[32];
int g_attackNext = 0;
std::uint32_t g_strikesSent = 0;

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
std::uint32_t g_applied = 0, g_notOurs = 0, g_noEffect = 0, g_effectGone = 0, g_effectFinished = 0, g_failed = 0;
// The effect system's mode (exe+0xB63628; Ghidra, 2026-10-16): 1 in the field. exe+0x2C4440(mode) switches it (to 3 from
// exe+0x29B590, 0x29B600, 0x1512B0) and exe+0x2C4420 puts the old one back. A bound effect started in mode 3 takes its
// owner for another kind of object (exe+0x2C5F70 -> exe+0x2FF110 reads owner+0x30 as a model), but the game has linked
// the effect's record into the actor's list before that: the two-PC run 2026-10-15 23:30:41 started the host's
// trap_obj_effect_start_bind on our F_MU060 that way (the call failed, its record stayed half made), so the game
// couldn't remove F_MU060 and crashed on that record when the room was left 30 s later. Our effect starts wait for
// mode 1 (up to 1 s, as for a bind) and are dropped if it doesn't come.
constexpr std::uintptr_t kEffectMode = 0xB63628;
std::uint32_t g_effectModeHeld = 0, g_effectModeDropped = 0;
std::uint32_t g_byNumber = 0;  // run on our bound enemy at another address than the host's
std::uint32_t g_appliedLogs = 0;
std::uint32_t g_strikes = 0, g_strikesFailed = 0;
struct GroupPair {
    std::uint64_t host, ours;
};
GroupPair g_groups[8];  // the host's combo groups and ours (a group's attacks hit a target once)
int g_groupNext = 0;
std::uint64_t g_statsTick = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

bool IsEffectStart(const Event& e) {
    Kind kind = KindOf(e.table, e.index);
    return kind == kEffectStart || kind == kEffectStartPos;
}
bool EffectModeOff() { return Read<std::uint32_t>(ExeBase() + kEffectMode) != 1; }
bool WaitsForMode(const Event& e) { return IsEffectStart(e) && EffectModeOff(); }

bool InList(std::uintptr_t x) {
    if (!x) return false;
    __try {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = DecodeHandle(Read<std::uint32_t>(a + kNextActor)))
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
        std::uintptr_t fx = DecodeHandle(h);
        if (!fx || (Read<std::uint32_t>(fx + kEffectFlags) & kEffectKilled)) return 0;
        return Read<std::uintptr_t>(fx + kEffectPart);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Ours ran out of particles before the host's kill came: the effect update (exe+0x2BD3BD) marks it killed (0x10000)
// and drops its part, but only a kill or loop end sets bit 0, and the effect sweep (exe+0x2BD730) destroys a killed
// effect only once bit 0 is set. Till then its record stays on the actor, and the game never removes an actor with
// records (exe+0x3DAC30). Twin Lords bench 2026-10-15 21:41: every fire and ice object the friend's game kept after
// the host's was gone held one such record, its effect flags 0x10000 with no part.
bool EndedNotFinished(std::uint32_t h) {
    __try {
        std::uintptr_t fx = DecodeHandle(h);
        if (!fx) return false;
        std::uint32_t flags = Read<std::uint32_t>(fx + kEffectFlags);
        return flags != kFreedFill && (flags & kEffectKilled) && !(flags & 1) && !Read<std::uintptr_t>(fx + kEffectPart);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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
    std::uintptr_t x = DecodeHandle(static_cast<std::uint32_t>(arg));
    if (InList(x)) return x;
    __try {
        std::uintptr_t y = x ? DecodeHandle(Read<std::uint32_t>(x + 4)) : 0;
        return InList(y) ? y : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// A sheet handle's owner (the actor whose sheet it is), if in the list.
std::uintptr_t SheetOwner(std::uint64_t arg) {
    std::uintptr_t sheet = DecodeHandle(static_cast<std::uint32_t>(arg));
    if (!sheet) return 0;
    __try {
        std::uintptr_t owner = DecodeHandle(Read<std::uint32_t>(sheet + kSheetOwner));
        return InList(owner) && Read<std::uintptr_t>(owner + kActorSheet) == sheet ? owner : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// No sheet (a field object) or HP left.
bool Alive(std::uintptr_t actor) {
    __try {
        std::uintptr_t sheet = Read<std::uintptr_t>(actor + kActorSheet);
        return !sheet || Read<std::int32_t>(sheet) > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Queue(const Event& e, std::uintptr_t actor = 0) {
    Queued& q = g_queue[g_queueNext++ % 128];
    q.e = e;
    q.sent = 0;
    q.actor = e.netId ? 0 : actor;
    q.frame = g_mirrorFrame;
}

void SendQueued() {
    char buf[sizeof(Packet) + kPerPacket * sizeof(Event)];
    Packet p{kMagic, kVersion, AvatarLinkBuild(), 0};
    int n = 0;
    for (int i = 0; i < 128 && n < kPerPacket; ++i) {  // oldest first
        Queued& q = g_queue[(g_queueNext + i) % 128];
        if (!q.e.id || q.sent >= 3) continue;
        if (q.actor && !q.sent) {
            // A new enemy's first calls can come before world sync has a number for it, which the friend needs to
            // find its copy: wait up to 6 frames for one. The calls after it wait too (the friend runs them in order).
            q.e.netId = WorldSyncNetIdOf(q.actor, &q.e.instance);
            if (q.e.netId) q.actor = 0;
            else if (g_mirrorFrame - q.frame < 6) break;
        }
        std::memcpy(buf + sizeof(Packet) + n * sizeof(Event), &q.e, sizeof(Event));
        ++q.sent;
        ++n;
    }
    if (!n) return;
    p.count = static_cast<std::uint8_t>(n);
    std::memcpy(buf, &p, sizeof(p));
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Packet) + n * sizeof(Event)));
}

// Friend: the host's script struck the copy (args: attack id, combo group, flags, the strike's third argument):
// the same attack from our copy of the attacker, on our own Sora.
void ApplyStrike(const Event& e) {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t bound = WorldSyncBoundActor(e.netId, e.instance);
    std::uintptr_t actor = bound ? bound : exe + e.target;
    bool listed = bound || InList(actor);
    if (!listed || ObjId(actor) != e.objId || !WorldSyncSkipsBrain(actor)) {
        if (g_notOurs++ < 20)
            Log("world mirror: the host's strike on our copy by host#%u (obj 0x%X) not run: no enemy of ours bound to it",
                e.netId, e.objId);
        return;
    }
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kListHead);
    auto make = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(2, 10));    // trap_attack_new
    auto strike = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(2, 17));  // trap_attack_strike
    auto release = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(2, 13));  // trap_attack_free
    auto newGroup = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(2, 67));  // trap_attack_new_combo_group
    if (!sora || !make || !strike || !release) {
        ++g_strikesFailed;
        return;
    }
    auto encode = reinterpret_cast<PFN_Encode>(exe + kEncodeHandle);
    std::uint64_t group = e.args[1];
    __try {
        if (static_cast<std::int32_t>(group) != -1 && newGroup) {
            int i = 0;
            while (i < 8 && g_groups[i].host != group) ++i;
            if (i < 8) {
                group = g_groups[i].ours;
            } else {
                std::uint64_t a[16] = {};
                newGroup(a);
                g_groups[g_groupNext++ % 8] = {e.args[1], a[0]};
                group = a[0];
            }
        }
        std::uint64_t a[16] = {kTagAddr | encode(actor), e.args[0], group, e.args[2]};
        make(a);
        std::uint64_t attack = a[0];
        if (!attack) {
            ++g_strikesFailed;
            return;
        }
        std::uint64_t s[16] = {attack, kTagAddr | encode(sora), e.args[3], e.args[3] & 0xFFFFFFFF00000000ull};
        strike(s);
        std::uint64_t f[16] = {attack};
        release(f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_strikesFailed++ < 5) Log("world mirror: the host's strike by %s failed here (exception)", Model(actor));
        return;
    }
    if (g_strikes++ < 60)
        Log("world mirror: the host's %s struck our copy with attack %d: the same strike on our Sora", Model(actor),
            static_cast<std::int32_t>(e.args[0]));
}

bool VacuumListed(std::uintptr_t v) {
    __try {
        std::uintptr_t p = Read<std::uintptr_t>(ExeBase() + kVacuumHead);
        for (int n = 0; p && n < 64; ++n, p = DecodeHandle(Read<std::uint32_t>(p + 0x30)))
            if (p == v) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// Friend: one of the host's vacuum calls (target = the host's vacuum handle; args as OnVacuumCall sends them).
void ApplyVacuum(const Event& e) {
    std::uintptr_t exe = ExeBase();
    auto f = [&](int i) {
        float v;
        std::uint32_t bits = static_cast<std::uint32_t>(e.args[i]);
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    };
    __try {
        if (e.index == 77) {  // make it as trap_vacuum_create's exe+0x3F8F20 does, at the list's end
            std::uintptr_t v = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint64_t)>(exe + kAlloc)(0x34);
            if (!v) return;
            auto* w = reinterpret_cast<float*>(v);
            w[0] = f(0), w[1] = f(1), w[2] = f(2), w[3] = 1.0f;
            *reinterpret_cast<std::uint64_t*>(v + 0x10) = 0;
            w[6] = w[7] = w[8] = f(3);
            w[9] = 0.0f;
            w[10] = 100.0f;
            w[11] = 0.01f;
            auto encode = reinterpret_cast<PFN_Encode>(exe + kEncodeHandle);
            *reinterpret_cast<std::uint32_t*>(v + 0x30) = encode(0);
            auto* head = reinterpret_cast<std::uintptr_t*>(exe + kVacuumHead);
            auto* tail = reinterpret_cast<std::uintptr_t*>(exe + kVacuumTail);
            if (!*tail) *head = v;
            else *reinterpret_cast<std::uint32_t*>(*tail + 0x30) = encode(v);
            *tail = v;
            g_vacuums[g_vacuumNext++ % 16] = {e.target, v};
            if (g_vacuumsMade++ < 30)
                Log("world mirror: made the host's vacuum here at (%.0f, %.0f, %.0f), speed %.0f", f(0), f(1), f(2), f(3));
            return;
        }
        int i = 0;
        while (i < 16 && g_vacuums[i].host != e.target) ++i;
        if (i == 16 || !e.target) return;
        std::uintptr_t v = g_vacuums[i].ours;
        if (e.index == 78) g_vacuums[i] = {};
        if (!VacuumListed(v)) return;  // the game already cleared its vacuums (room change)
        ++g_vacuumCalls;
        auto* w = reinterpret_cast<float*>(v);
        switch (e.index) {
            case 78:  // trap_vacuum_destroy
                reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(exe + kVacuumUnlink)(v);
                reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(exe + kFree)(v);
                break;
            case 79: *reinterpret_cast<std::uint32_t*>(v + 0x10) |= 1u << (e.args[0] & 31); break;  // ignore type
            case 80: w[0] = f(0), w[1] = f(1), w[2] = f(2); break;                                   // place
            case 81: w[6] = f(0), w[7] = f(1), w[8] = f(2); break;                                   // speeds
            case 82: w[9] = f(0); break;
            case 83: w[10] = f(0); break;
            case 84: w[11] = f(0); break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_failed++ < 5) Log("world mirror: the host's vacuum call 2/%u failed here (exception)", e.index);
    }
}

void Run(const Event& e) {
    if (e.table == 2 && e.index == 17) {
        ApplyStrike(e);
        return;
    }
    if (IsVacuumCall(e.table, e.index)) {
        ApplyVacuum(e);
        return;
    }
    Kind kind = KindOf(e.table, e.index);
    if (WaitsForMode(e)) {  // given up on after its wait: not run (see kEffectMode)
        if (g_effectModeDropped++ < 10)
            Log("world mirror: the host's effect start %u/%u not run: our effects are still in mode %u", e.table, e.index,
                Read<std::uint32_t>(ExeBase() + kEffectMode));
        return;
    }
    std::uint64_t args[16] = {};
    std::uintptr_t actor = 0;
    if (kind == kEffectEnd || kind == kEffectCall) {
        int i = 0;
        while (i < 64 && g_effects[i].host != e.target) ++i;
        if (i == 64 || !e.target) {
            ++g_noEffect;
            return;
        }
        EffectPair pair = g_effects[i];
        if (EndsEffect(e.table, e.index)) g_effects[i] = {};
        if (!pair.part || EffectPart(pair.ours) != pair.part) {  // ours already ended (or the handle is reused)
            // Ended by itself, not finished: the host's kill finishes it, as it did the host's (see EndedNotFinished).
            if (!pair.part || !EndsEffect(e.table, e.index) || !EndedNotFinished(pair.ours)) {
                ++g_effectGone;
                return;
            }
            ++g_effectFinished;
        }
        args[0] = kTagAddr | pair.ours;
    } else if (IsActorKind(kind)) {
        // Enemies from spawn groups are made in a different order in each game, so a different address (bench
        // 2026-10-13: 299 of 320 Shadow calls missed and the friend's Shadows kept their 'underground' flags).
        // Room, Light, PrizeAt, Mission: the actor whose script made the call (ours runs no script, so the call is the
        // host's). SheetRate: the sheet's owner.
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
    auto encode = reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle);
    if (kind == kRoom || kind == kMission) {
        for (int i = 0; i < e.argc && i < 4; ++i) args[i] = e.args[i];
    } else if (kind == kLight) {
        if (e.index == 181) {
            args[0] = e.args[0];
        } else {
            std::uint32_t light = encode(ExeBase() + e.result);
            if (!light) {
                ++g_failed;
                return;
            }
            args[0] = kTagAddr | light;
            for (int i = 0; i < e.argc && i < 4; ++i) args[1 + i] = e.args[i];
        }
    } else if (kind != kEffectStartPos && kind != kPrizeAt && kind != kSheetRate) {
        for (int i = 0; i < e.argc && i < 4; ++i) args[1 + i] = e.args[i];
    }
    bool direct = kind == kEffectStartPos || kind == kPrizeAt || kind == kSheetRate;  // a game function, not the call
    auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(e.table, e.index));
    if (!fn && !direct) {
        ++g_failed;
        return;
    }
    int freeBefore = kind == kPrize || kind == kPrizeAt ? PrizeFree() : -1;
    __try {
        if (kind == kPrizeAt) {
            // args: the place's x | y << 32, z | w << 32, then the call's numbers (OnTrap). NaN goes to 0, as the call does.
            float place[4];
            std::uint32_t bits[4] = {static_cast<std::uint32_t>(e.args[0]), static_cast<std::uint32_t>(e.args[0] >> 32),
                                     static_cast<std::uint32_t>(e.args[1]), static_cast<std::uint32_t>(e.args[1] >> 32)};
            std::memcpy(place, bits, sizeof(bits));
            for (float& f : place)
                if (f != f) f = 0.0f;
            std::uint32_t a = static_cast<std::uint32_t>(e.args[2]), b = static_cast<std::uint32_t>(e.args[3]);
            std::uintptr_t exe = ExeBase();
            using PFN_PlaceN = void(__fastcall*)(float*, std::uint32_t);
            if (e.table == 6) reinterpret_cast<PFN_PlaceN>(exe + kPrizeAppear)(place, a);
            else if (e.index == 279) reinterpret_cast<PFN_PlaceN>(exe + kPrizeAppearTr)(place, a);
            else if (e.index == 363) reinterpret_cast<PFN_PlaceN>(exe + kPrizeAppearXaldin)(place, a);
            else reinterpret_cast<void(__fastcall*)(float*, std::uint32_t, std::uint32_t)>(exe + kPrizeAppearNum)(place, a, b);
        } else if (kind == kSheetRate) {
            std::uintptr_t sheet = Read<std::uintptr_t>(actor + kActorSheet);
            if (!sheet) {
                ++g_failed;
                return;
            }
            reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::int32_t, std::int32_t)>(ExeBase() + kSetElementRate)(
                sheet, static_cast<std::int32_t>(e.args[0]), static_cast<std::int32_t>(e.args[1]));
        } else if (kind == kEffectStartPos) {
            // args: id | a << 32, x | y << 32, z | b << 32 (OnTrap). The game's start with a place, as the call does.
            float place[4];
            std::uint32_t bits[3] = {static_cast<std::uint32_t>(e.args[1]), static_cast<std::uint32_t>(e.args[1] >> 32),
                                     static_cast<std::uint32_t>(e.args[2])};
            std::memcpy(place, bits, sizeof(bits));
            place[3] = 1.0f;
            auto start = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t, std::uint32_t, float*, std::uint32_t,
                                                                      std::uint32_t)>(ExeBase() + kEffectStartAt);
            std::uintptr_t fx = start(actor, static_cast<std::uint32_t>(e.args[0]), place,
                                      static_cast<std::uint32_t>(e.args[0] >> 32), static_cast<std::uint32_t>(e.args[2] >> 32));
            args[0] = fx ? encode(fx) : 0;
        } else {
            fn(args);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_failed++ < 5) Log("world mirror: call %u/%u on %s failed (exception)", e.table, e.index, Model(actor));
        return;
    }
    ++g_applied;
    if (kind == kPrize || kind == kPrizeAt) {
        static int prizeLogs = 0;
        int freeAfter = PrizeFree();
        if (prizeLogs++ < 60)
            Log("world mirror: the host's prize call %u/%u on our %s made %d prizes here (free places %d -> %d)", e.table,
                e.index, Model(actor), freeBefore - freeAfter, freeBefore, freeAfter);
    }
    if (kind == kEffectEnd) {
        static int endLogs = 0;
        if (endLogs++ < 40) {
            std::uint32_t flags = 0;
            __try {
                std::uintptr_t fx = DecodeHandle(static_cast<std::uint32_t>(args[0]));
                if (fx) flags = Read<std::uint32_t>(fx + kEffectFlags);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            Log("world mirror: host's %u/%u ran on our effect 0x%08X, its flags now 0x%X", e.table, e.index,
                static_cast<std::uint32_t>(args[0]), flags);
        }
    }
    if ((kind == kEffectStart || kind == kEffectStartPos) && e.result) {
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

// Friend: a call for a host enemy our copy of which is not bound yet. The host makes Blizzard Lord's ice B_AL120 and
// starts its effect 19 ms later; ours was bound 30 ms after that call came, so the effect never started here and
// all its moves (5652 in a 90 s run) found no effect of ours (bench 2026-10-15 00:27). Such calls wait up to 1 s, and
// the ends and pauses of an effect whose start waits, wait behind it.
struct Held {
    Event e;
    std::uint64_t arrived;
};
Held g_held[64];
int g_heldCount = 0;
std::uint32_t g_heldEvents = 0, g_heldRan = 0;

bool WaitsForBind(const Event& e) {
    return IsActorKind(KindOf(e.table, e.index)) && e.netId && !WorldSyncBoundActor(e.netId, e.instance);
}
bool AfterHeldStart(const Event& e, int count) {
    Kind kind = KindOf(e.table, e.index);
    if ((kind != kEffectEnd && kind != kEffectCall) || !e.target) return false;
    for (int i = 0; i < count; ++i) {
        Kind k = KindOf(g_held[i].e.table, g_held[i].e.index);
        if ((k == kEffectStart || k == kEffectStartPos) && g_held[i].e.result == e.target) return true;
    }
    return false;
}

void Apply(const Event& e) {
    bool vacuum = IsVacuumCall(e.table, e.index);
    if (!(e.table == 2 && e.index == 17) && !vacuum && g_heldCount < 64 &&
        (WaitsForBind(e) || WaitsForMode(e) || AfterHeldStart(e, g_heldCount))) {
        if (WaitsForMode(e) && g_effectModeHeld++ < 10)
            Log("world mirror: the host's effect start %u/%u waits: our effects are in mode %u, not the field's",
                e.table, e.index, Read<std::uint32_t>(ExeBase() + kEffectMode));
        g_held[g_heldCount++] = {e, GetTickCount64()};
        ++g_heldEvents;
        return;
    }
    Run(e);
}

void RunHeld() {
    std::uint64_t now = GetTickCount64();
    int kept = 0;
    for (int i = 0; i < g_heldCount; ++i) {
        Held h = g_held[i];
        bool wait = WaitsForBind(h.e) || WaitsForMode(h.e) || AfterHeldStart(h.e, kept);
        if (wait && now - h.arrived < 1000) {
            g_held[kept++] = h;
            continue;
        }
        if (!wait) ++g_heldRan;
        Run(h.e);  // bound now, or given up on (counted as not ours)
    }
    g_heldCount = kept;
}

// Friend: one state entry, at the host's time our enemies are shown at.
void ApplyState(const StateEntry& s) {
    std::uintptr_t exe = ExeBase();
    __try {
        if (s.kind == kActorRot) {
            std::uintptr_t bound = WorldSyncBoundActor(s.netId, s.instance);
            std::uintptr_t actor = bound ? bound : exe + s.key;
            if ((!bound && !InList(actor)) || ObjId(actor) != s.objId || !WorldSyncSkipsBrain(actor)) {
                ++g_statesNoActor;
                return;
            }
            float v[4] = {s.v[0], s.v[1], s.v[2], s.v[3]};
            reinterpret_cast<void(__fastcall*)(std::uintptr_t, float*)>(exe + kActorSetXyzRot)(actor, v);
        } else if (s.kind >= kFxPos && s.kind <= kFxColor) {
            int i = 0;
            while (i < 64 && g_effects[i].host != s.key) ++i;
            if (i == 64 || !s.key || !g_effects[i].part || EffectPart(g_effects[i].ours) != g_effects[i].part) {
                ++g_statesNoEffect;
                return;
            }
            float* field = reinterpret_cast<PFN_FxField>(exe + kFxGetter[s.kind])(DecodeHandle(g_effects[i].ours));
            if (!field) return;
            std::memcpy(field, s.v, sizeof(s.v));
        } else {
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_failed++ < 5) Log("world mirror: the host's state %u failed here (exception)", s.kind);
        return;
    }
    ++g_statesApplied;
}

void ApplyPendingStates() {
    std::uint32_t target = AvatarLinkPlaybackSenderMs();
    std::uint64_t now = GetTickCount64();
    while (g_pendingHead != g_pendingTail) {
        const Pending& p = g_pending[g_pendingHead];
        // Not yet: the host's time we show is behind it (half a second at most, should the clocks be off).
        if (static_cast<std::int32_t>(target - p.senderMs) < 0 && now - p.arrived < 500) break;
        ApplyState(p.e);
        g_pendingHead = (g_pendingHead + 1) % 512;
    }
}

// Host: a state call just ran: keep its new value for the next "KH2Y" packets.
void SetState(const StateEntry& s) {
    HostState* slot = nullptr;
    for (HostState& h : g_hostStates)
        if (h.e.kind == s.kind && h.e.key == s.key) slot = &h;
    if (slot && std::memcmp(slot->e.v, s.v, sizeof(s.v)) == 0) return;  // unchanged: its sends go on as they were
    if (!slot) {
        for (int n = 0; n < 128 && !slot; ++n) {  // a free one, else the oldest
            HostState& h = g_hostStates[(g_stateNext + n) % 128];
            if (!h.sendsLeft) slot = &h;
        }
        if (!slot) slot = &g_hostStates[g_stateNext % 128];
        g_stateNext = static_cast<int>((slot - g_hostStates) + 1) % 128;
    }
    slot->e = s;
    slot->sendsLeft = 3;
}

void OnStateCall(int table, int index, const std::uint64_t* args, int argc) {
    if (argc < 2) return;
    StateEntry s{};
    s.kind = StateOf(table, index);
    __try {
        if (s.kind == kActorRot) {
            std::uintptr_t actor = ActorArg(args[0]);
            if (!actor) return;
            std::uint32_t team = Read<std::uint32_t>(actor + kTeam);
            if (team != 2 && team != 3) return;
            s.key = static_cast<std::uint32_t>(actor - ExeBase());
            s.objId = ObjId(actor);
            std::uint16_t instance = 0;
            s.netId = WorldSyncNetIdOf(actor, &instance);
            s.instance = instance;
            std::memcpy(s.v, reinterpret_cast<const void*>(actor + kActorRotVec), sizeof(s.v));
        } else {
            std::uint32_t h = static_cast<std::uint32_t>(args[0]);
            int i = 0;
            while (i < 64 && g_hostEffects[i] != h) ++i;
            if (i == 64 || !h || EffectPart(h) != g_hostEffectParts[i]) return;  // not an effect we told the friend about
            s.key = h;
            float* field = reinterpret_cast<PFN_FxField>(ExeBase() + kFxGetter[s.kind])(DecodeHandle(h));
            if (!field) return;
            std::memcpy(s.v, field, sizeof(s.v));  // what the call left there (set_dir: the rotation)
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    SetState(s);
}

void SendStates() {
    char buf[sizeof(StatePacket) + kStatePerPacket * sizeof(StateEntry)];
    for (int packet = 0; packet < 2; ++packet) {  // up to 88 a frame, the rest next frame
        int n = 0;
        for (int k = 0; k < 128 && n < kStatePerPacket; ++k) {
            HostState& h = g_hostStates[(g_stateSendFrom + k) % 128];
            if (!h.sendsLeft) continue;
            std::memcpy(buf + sizeof(StatePacket) + n * sizeof(StateEntry), &h.e, sizeof(StateEntry));
            --h.sendsLeft;
            ++n;
        }
        if (!n) return;
        g_stateSendFrom = (g_stateSendFrom + 37) % 128;  // no slot waits behind the others for long
        StatePacket p{kStateMagic, kStateVersion, AvatarLinkBuild(), AvatarLinkNowMs(), static_cast<std::uint8_t>(n)};
        std::memcpy(buf, &p, sizeof(p));
        AvatarLinkSendRaw(buf, static_cast<int>(sizeof(StatePacket) + n * sizeof(StateEntry)));
        g_statesSent += n;
    }
}

// Host: true if this call only repeats the last one of its kind on that actor, sent less than 1 s ago.
bool RepeatedCall(std::uintptr_t actor, int table, int index, const std::uint64_t* args, int argc) {
    std::uint8_t family;
    std::uint32_t argKey = 0;
    std::uint64_t value = argc >= 2 ? args[1] : 0;
    if (table == 6 && (index == 45 || index == 64)) family = 1, value = 0;          // shadow hide / show
    else if (table == 1 && (index == 85 || index == 86)) family = 2, argKey = static_cast<std::uint32_t>(value), value = 0;
    else if (table == 6 && index == 65) family = 3;                                 // scissoring on / off
    else if (table == 1 && index == 1) family = 4;                                  // tilt angle
    // hide / show: Data Roxas's two Keyblades call obj_hide every frame, 10,800 of the 12,894 calls sent in a 90 s
    // run (bench 2026-10-15 00:28)
    else if (table == 1 && (index == 133 || index == 134)) family = 5, value = 0;
    // collision enable / disable / reset, per collision number: Final Xemnas's script disables one about every
    // frame (most of the 11,403 calls sent in a 90 s run of ex400_148, bench 2026-10-15 00:36)
    else if (table == 1 && index >= 52 && index <= 54) family = 6, argKey = static_cast<std::uint32_t>(value), value = 0;
    else return false;
    std::uint64_t now = GetTickCount64();
    for (Toggle& t : g_toggles) {
        if (t.actor != actor || t.family != family || t.argKey != argKey) continue;
        if (t.index == index && t.value == value && now - t.at < 1000) return true;
        t.index = static_cast<std::uint16_t>(index), t.value = value, t.at = now;
        return false;
    }
    g_toggles[g_toggleNext++ % 64] = {actor, family, argKey, static_cast<std::uint16_t>(index), value, now};
    return false;
}

}  // namespace

void WorldMirrorOnStatePacket(const char* buf, int n) {
    if (!g_on || g_host || n < static_cast<int>(sizeof(StatePacket))) return;
    StatePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kStateVersion || p.build != AvatarLinkBuild()) return;
    if (n != static_cast<int>(sizeof(StatePacket) + p.count * sizeof(StateEntry))) return;
    std::uint64_t now = GetTickCount64();
    for (int i = 0; i < p.count; ++i) {
        Pending& q = g_pending[g_pendingTail];
        q.senderMs = p.senderMs;
        q.arrived = now;
        std::memcpy(&q.e, buf + sizeof(StatePacket) + i * sizeof(StateEntry), sizeof(StateEntry));
        g_pendingTail = (g_pendingTail + 1) % 512;
        if (g_pendingTail == g_pendingHead) g_pendingHead = (g_pendingHead + 1) % 512;  // full: the oldest goes
    }
}

void WorldMirrorInit(bool host) {
    g_on = true;
    g_host = host;
    Log("world mirror (one brain): %s", host ? "host: sending enemies' visible script calls"
                                             : "friend: bound enemies don't think; running the host's calls");
}

// trap_attack_new / trap_attack_free / trap_attack_strike, for the strikes on the copy.
bool IsAttackCall(int table, int index) { return table == 2 && (index == 10 || index == 13 || index == 17); }

bool WorldMirrorWants(int table, int index) {
    return g_on && g_host && (KindOf(table, index) != kNone || IsAttackCall(table, index) || IsVacuumCall(table, index));
}

namespace {
// Host: a vacuum call. Sent: create (place x, y, z, speed as float bits), set_pos (x, y, z), set_speed (3 speeds),
// the one-value setters (the value), destroy; target = the vacuum's handle.
void OnVacuumCall(int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (argc < 1) return;
    Event e{};
    e.table = 2;
    e.index = static_cast<std::uint16_t>(index);
    auto vec = [&](std::uint64_t handle) {  // a script vector: 4 floats behind a handle
        std::uintptr_t p = DecodeHandle(static_cast<std::uint32_t>(handle));
        if (!p) return false;
        __try {
            for (int i = 0; i < 3; ++i) e.args[i] = Read<std::uint32_t>(p + 4 * i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        return true;
    };
    if (index == 77) {
        e.target = static_cast<std::uint32_t>(result);
        if (!e.target || argc < 2 || !vec(args[0])) return;
        e.args[3] = static_cast<std::uint32_t>(args[1]);
        for (std::uint32_t& h : g_hostVacuums)
            if (h == e.target) h = 0;
        g_hostVacuums[g_hostVacuumNext++ % 16] = e.target;
    } else {
        e.target = static_cast<std::uint32_t>(args[0]);
        int i = 0;
        while (i < 16 && g_hostVacuums[i] != e.target) ++i;
        if (i == 16 || !e.target) return;  // not one we told the friend about
        if (index == 78) g_hostVacuums[i] = 0;
        if (index == 80 && (argc < 2 || !vec(args[1]))) return;
        if (index == 81)
            for (int k = 0; k < 3 && k + 1 < argc; ++k) e.args[k] = static_cast<std::uint32_t>(args[k + 1]);
        if (index == 79 || index >= 82) e.args[0] = argc >= 2 ? static_cast<std::uint32_t>(args[1]) : 0;
    }
    e.argc = 4;
    e.id = g_nextId++;
    Queue(e);
    ++g_sentEvents;
    if (index == 77 && g_vacuumsSent++ < 30) Log("world mirror: a vacuum made here: sent to the friend");
}

void OnAttackCall(int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (index == 10) {  // (actor, attack id, combo group, flags) -> the attack
        std::uintptr_t owner = argc >= 4 ? ActorArg(args[0]) : 0;
        if (!owner || !result) return;
        std::uint32_t team = Read<std::uint32_t>(owner + kTeam);
        if (team != 2 && team != 3) return;  // enemies and field objects only
        g_attacks[g_attackNext++ % 32] = {result, owner, args[1], args[2], args[3]};
        return;
    }
    int i = 0;
    while (i < 32 && (!g_attacks[i].attack || g_attacks[i].attack != args[0])) ++i;
    if (i == 32) return;
    if (index == 13) {  // trap_attack_free(attack)
        g_attacks[i] = {};
        return;
    }
    // trap_attack_strike(attack, target, ?, ?)
    std::uintptr_t clone = PuppetCloneActor();
    std::uintptr_t target = argc >= 3 ? ActorArg(args[1]) : 0;
    const HostAttack& a = g_attacks[i];
    if (target && target == Read<std::uintptr_t>(ExeBase() + kListHead)) AggroBossActedOn(a.owner, false, "struck");
    if (!clone || target != clone) return;
    AggroBossActedOn(a.owner, true, "struck");
    Event e{};
    e.table = 2;
    e.index = 17;
    e.target = static_cast<std::uint32_t>(a.owner - ExeBase());
    e.objId = ObjId(a.owner);
    e.netId = WorldSyncNetIdOf(a.owner, &e.instance);
    e.argc = 4;
    e.args[0] = a.id;
    e.args[1] = a.group;
    e.args[2] = a.flags;
    e.args[3] = args[2];
    e.id = g_nextId++;
    Queue(e);
    ++g_sentEvents;
    if (g_strikesSent++ < 60)
        Log("world mirror: %s struck the friend's Sora copy with attack %d: sent to the friend", Model(a.owner),
            static_cast<std::int32_t>(a.id));
    if (argc >= 4 && static_cast<std::int32_t>(args[3]) != 0) {
        static int odd = 0;
        if (odd++ < 5) Log("world mirror: that strike's fourth argument was 0x%llX (not sent)",
                           static_cast<unsigned long long>(args[3]));
    }
}
}  // namespace

void WorldMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (!g_on || !g_host || (argc < 1 && KindOf(table, index) != kMission)) return;
    if (IsAttackCall(table, index)) {
        OnAttackCall(index, args, argc, result);
        return;
    }
    if (IsVacuumCall(table, index)) {
        OnVacuumCall(index, args, argc, result);
        return;
    }
    Kind kind = KindOf(table, index);
    if (kind == kState) {
        OnStateCall(table, index, args, argc);
        return;
    }
    Event e{};
    e.table = static_cast<std::uint8_t>(table);
    e.index = static_cast<std::uint16_t>(index);
    std::uintptr_t sender = 0;
    if (kind == kEffectEnd || kind == kEffectCall) {
        std::uint32_t h = static_cast<std::uint32_t>(args[0]);
        int i = 0;
        while (i < 64 && g_hostEffects[i] != h) ++i;
        if (i == 64 || !h) return;  // not an effect we told the friend about
        // An end runs after the kill cleared the part; another part = the handle went to a new effect.
        std::uintptr_t part = EffectPart(h);
        if (part != g_hostEffectParts[i] && (kind == kEffectCall || part)) return;
        if (EndsEffect(table, index)) g_hostEffects[i] = 0;
        e.target = h;
    } else if (IsActorKind(kind)) {
        // Room, Light, PrizeAt, Mission: the actor whose script is running (a room's own script and a mission's script
        // run in both games: not sent). SheetRate: the sheet's owner.
        std::uintptr_t actor = 0;
        if (kind == kRoom || kind == kLight || kind == kPrizeAt || kind == kMission) actor = WorldCensusScriptActor();
        else if (kind == kSheetRate) actor = SheetOwner(args[0]);
        else actor = ActorArg(args[0]);
        if (!actor) return;
        std::uint32_t team = Read<std::uint32_t>(actor + kTeam);
        if (team != 2 && team != 3) return;  // enemies and field objects only
        // A dead enemy's prizes: the friend's copy of it drops its own (its brain runs again once it is killed there).
        if ((kind == kPrize || kind == kPrizeAt) && !Alive(actor)) return;
        if (kind == kActor && RepeatedCall(actor, table, index, args, argc)) {
            ++g_repeatsSkipped;
            return;
        }
        e.target = static_cast<std::uint32_t>(actor - ExeBase());
        e.objId = ObjId(actor);
        e.netId = WorldSyncNetIdOf(actor, &e.instance);
        sender = actor;
        if (kind == kRoom || kind == kMission) {
            e.argc = static_cast<std::uint8_t>(argc > 4 ? 4 : argc);
            for (int i = 0; i < e.argc; ++i) e.args[i] = args[i];
        } else if (kind == kPrizeAt) {
            // (place, numbers): the place's 4 floats go with it (a script vector is a handle in our game).
            std::uintptr_t v = DecodeHandle(static_cast<std::uint32_t>(args[0]));
            if (!v || argc < 2) return;
            std::uint32_t f[4];
            __try {
                for (int i = 0; i < 4; ++i) f[i] = Read<std::uint32_t>(v + 4 * i);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return;
            }
            e.argc = 4;
            e.args[0] = f[0] | (static_cast<std::uint64_t>(f[1]) << 32);
            e.args[1] = f[2] | (static_cast<std::uint64_t>(f[3]) << 32);
            e.args[2] = args[1];
            e.args[3] = argc >= 3 ? args[2] : 0;
        } else if (kind == kSheetRate) {
            if (argc < 3) return;
            e.argc = 2;
            e.args[0] = args[1];
            e.args[1] = args[2];
        } else if (kind == kLight) {
            // The light: a fixed slot in the exe (light 0 = exe+0x2AE7B98), the same place in the friend's game.
            std::uintptr_t light = DecodeHandle(static_cast<std::uint32_t>(index == 181 ? result : args[0]));
            if (light <= ExeBase() || light >= ExeBase() + 0x3000000) return;
            e.result = static_cast<std::uint32_t>(light - ExeBase());
            if (index == 181) {
                e.argc = 1;
                e.args[0] = args[0];
            } else {
                e.argc = static_cast<std::uint8_t>(argc - 1 > 4 ? 4 : argc - 1);
                for (int i = 0; i < e.argc; ++i) e.args[i] = args[1 + i];
            }
        } else if (kind == kEffectStartPos) {
            // (actor, id, place, a, b): the place's floats go with it (a script vector is a handle in our game).
            if (argc < 5) return;
            std::uintptr_t v = DecodeHandle(static_cast<std::uint32_t>(args[2]));
            if (!v) return;
            std::uint32_t x, y, z;
            __try {
                x = Read<std::uint32_t>(v), y = Read<std::uint32_t>(v + 4), z = Read<std::uint32_t>(v + 8);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return;
            }
            e.argc = 3;
            e.args[0] = static_cast<std::uint32_t>(args[1]) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(args[3])) << 32);
            e.args[1] = x | (static_cast<std::uint64_t>(y) << 32);
            e.args[2] = z | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(args[4])) << 32);
        }
        if (kind == kEffectStart || kind == kEffectStartPos) {
            e.result = static_cast<std::uint32_t>(result);
            if (e.result) {
                for (std::uint32_t& h : g_hostEffects)
                    if (h == e.result) h = 0;  // a reused handle: its old effect is gone
                g_hostEffectParts[g_hostEffectNext % 64] = EffectPart(e.result);
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
    if (kind != kRoom && kind != kLight && kind != kEffectStartPos && kind != kPrizeAt && kind != kSheetRate &&
        kind != kMission) {
        e.argc = static_cast<std::uint8_t>(argc - 1 > 4 ? 4 : argc - 1);
        for (int i = 0; i < e.argc; ++i) e.args[i] = args[1 + i];
    }
    if (kind == kPrize || kind == kPrizeAt || kind == kSheetRate || kind == kMission || table == 2) {
        static int logs = 0;
        if (logs++ < 200)
            Log("world mirror: %s's call %u/%u sent to the friend's game (args %llX %llX)", Model(sender), table, index,
                static_cast<unsigned long long>(e.args[0]), static_cast<unsigned long long>(e.args[1]));
    }
    e.id = g_nextId++;
    Queue(e, sender);
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
    ++g_mirrorFrame;
    if (g_host) {
        SendQueued();
        SendStates();
    } else {
        RunHeld();
        ApplyPendingStates();
    }
    std::uint64_t now = GetTickCount64();
    // Research: is a started effect still there 1 s and 5 s later (alive = not killed, active = has its part)?
    for (EffectPair& p : g_effects) {
        if (!p.ours || p.checks >= 2 || now - p.started < (p.checks ? 5000u : 1000u)) continue;
        ++p.checks;
        std::uint32_t flags = 0;
        std::uintptr_t part = 0;
        __try {
            std::uintptr_t fx = DecodeHandle(p.ours);
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
        Log("world mirror: script calls sent so far %u (strikes on the copy %u, repeats left out %u), states sent %u",
            g_sentEvents, g_strikesSent, g_repeatsSkipped, g_statesSent);
    else
        Log("world mirror: host calls run %u (%u on our enemy bound to the host's at another address), not for a "
            "bound enemy of ours %u, effect not known %u, our effect already gone %u (ended by itself, finished by the "
            "host's kill %u), failed %u; strikes on our Sora "
            "%u (failed %u); vacuums made %u, vacuum calls run %u; states set %u (no effect of ours %u, no actor %u); "
            "held for our copy's binding %u (bound in time %u)",
            g_applied, g_byNumber, g_notOurs, g_noEffect, g_effectGone, g_effectFinished, g_failed, g_strikes,
            g_strikesFailed,
            g_vacuumsMade, g_vacuumCalls, g_statesApplied, g_statesNoEffect, g_statesNoActor, g_heldEvents, g_heldRan);
}

}  // namespace kh2coop
