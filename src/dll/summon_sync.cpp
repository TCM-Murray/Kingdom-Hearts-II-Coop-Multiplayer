// The other player's summon as an actor in our game (TODO 1.61; DECISIONS C3: no entry cutscene here, the summon
// seen, moving and with its own particles). Each game's "KH2O" packet (companion_sync.cpp) names its own summon's
// actors while it is out, with their place and motion: the other game builds the same object, has it follow that
// pose (as Mickey's stand-in does) and has it leave when theirs ends. Test command 27 builds one by hand.
//
// The game (F629, F630, leads L1505-L1519): a summon is a party-class actor (YS::SUMMON, objentry type 0x13) that its
// constructor puts in a party slot (exe+0x2A239B0) and a summon slot (exe+0x2A24EE8). The factory builds one from an
// object id, but its own file load puts a summon's files in group 5000, which the Limit preload drops: the game
// crashed a frame later (bench summon_probe). The party checks (Drive, Summon, Mickey's branch, the Limit rows) read
// the party slots and never the summon state, so a summon left there would count as a companion (L1517).
// Here:
//   - Its files are asked into a group of ours first (form_look.cpp's copy files: out of the game's drops), and the
//     factory runs only once they are loaded, with the game's any-group lookups pointed at ours.
//   - Right after the build the actor is taken out of the party slot and the summon slot: our game sees no member
//     and no summon, as today. Its files are checked: one of the game's = removed at once.
//   - Its brain is off: the party brain (exe+0x1B15F0) does its own first-call set-up for it and nothing else (no
//     think, no scripts: its script would leave at once, our Sora has no summon time).
//   - Removal: the game's own, the 'leave' act of its script (what the Dismiss starts, exe+0x3DE080), with its brain
//     on again: it plays its leave and deletes itself. Still there after 8 s: faded out with its effects stopped (the
//     removal check exe+0x3DAC30 waits for the script's threads and the actor's effects). Its free clears the party
//     slot it was built in without a check (L1518), so the removal only starts while that slot is empty.
// The kept actor is ours only while the game hasn't freed it (the actor free hook), the room is the one it was built
// in (our own room load count, world sync's: the host's room changes don't move it) and it is still that object:
// Live().
// KH2COOP_SUMMON_SYNC=1 turns it on (off by default while it is being built).

#include "summon_sync.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "companion_sync.hpp"
#include "form_look.hpp"
#include "game.hpp"
#include "puppet.hpp"
#include "shared_move.hpp"
#include "spawn_sync.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint8_t kPartyBrainBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x60, 0xF7, 0x81,
                                             0x18, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
constexpr std::uintptr_t kObjRow = 0x918;       // actor -> its object table row (u32 object id, u8 type at +4)
constexpr std::uintptr_t kActorPos = 0x670, kActorAngle = 0x68C, kActorMotion = 0x180;
constexpr std::uintptr_t kActorSin = 0x680, kActorCos = 0x688;  // of the facing, beside it in the entity (+0x640)
constexpr std::uintptr_t kActorVelocity = 0xB98, kActorCarried = 0x690, kActorAccel = 0xA48;
constexpr std::uintptr_t kPartySlot = 0xDD0;    // i32: the party slot its constructor took (L1507)
constexpr std::uintptr_t kBrainFlags = 0xE18;   // u32: bit 29 = the brain's first-call set-up is done (L1515)
constexpr std::uint32_t kBrainSetUp = 0x20000000;
constexpr std::uint8_t kSummonType = 0x13;
constexpr std::uintptr_t kAiHelper = 0xDF8;     // -> the brain's own object of a party-class actor
constexpr std::uint64_t kFilesWaitMs = 10000, kShutdownAfterMs = 8000, kFadedWaitMs = 1500;  // the leave act's own time first

using PFN_Brain = void(__fastcall*)(std::uintptr_t);
PFN_Brain g_realBrain = nullptr;

struct Ours {
    std::uintptr_t actor;  // 0 none
    std::uint32_t obj, epoch;
    int slot;                  // the party slot its free clears
    std::uint64_t builtAt, fadeAt;  // ticks; fadeAt 0 = not being removed
    bool shutdown, looked, leaving;  // leaving: its own 'leave' act runs (brain on)
    std::uint64_t stateLogAt;
    int stateLogs;
    bool byPeer;       // built for the other player's summon (not the test command): follows it, leaves with it
    bool stitch;       // the character is Stitch (objentry +0x4C = 0xC8): his screen state is theirs's
    int lastMotion;    // the last motion we set from their pose, -1 none
    std::uint32_t poses, motionsMissing, held;  // held: its leave's script calls on other actors, not run
};
Ours g_ours{};
bool g_on = false;
std::uint32_t g_ask = 0;       // object to build, 0 none
bool g_askByPeer = false;      // g_ask is the other player's summon (else the test command)
bool g_askRemove = false;
std::uint32_t g_refusedObj = 0, g_refusedEpoch = 0;  // their summon we can't show in this room (declined, failed)
const char* g_lastNo = nullptr;                      // the last 'not yet' reason logged
int g_group = 0;               // the group of ours its files were asked into, for g_groupObj in room g_groupEpoch
std::uint32_t g_groupObj = 0, g_groupEpoch = 0;
std::uint64_t g_askedAt = 0;
std::uint32_t g_brainSkips = 0;

std::uintptr_t Slot(std::uintptr_t array, int i) {
    std::uintptr_t a = 0;
    return ReadAny(ExeBase() + array + 8 * i, &a) ? a : 0;
}

const char* ModelOf(std::uintptr_t actor) {
    std::uintptr_t row = 0;
    return actor && ReadAny(actor + kObjRow, &row) && row ? reinterpret_cast<const char*>(row + 8) : "-";
}

void LogSlots(const char* when) {
    std::uint32_t state = 0;
    ReadAny(ExeBase() + game::kSummonState, &state);
    Log("summon sync: %s: party slots %.16s / %.16s, summon slots %.16s / %.16s, summon state %u", when,
        ModelOf(Slot(game::kFriendSlots, 0)), ModelOf(Slot(game::kFriendSlots, 1)), ModelOf(Slot(game::kSummonSlots, 0)),
        ModelOf(Slot(game::kSummonSlots, 1)), state);
}

// Research: where the game is with taking it away (+0x120 bit 28 = its shutdown started, +0x5B8 live script threads).
void LogState(std::uintptr_t actor, const char* when) {
    std::uint32_t f120 = 0, f9b8 = 0, threads = 0;
    float fade = -1.0f, speed = -1.0f;
    ReadAny(actor + 0x120, &f120);
    ReadAny(actor + 0x9B8, &f9b8);
    ReadAny(actor + 0x5B8, &threads);
    ReadAny(actor + 0xA08, &fade);
    ReadAny(actor + 0xA0C, &speed);
    Log("summon sync: %s: +0x120 %#x, +0x9B8 %#x, fade %.2f (speed %.2f), script threads %u", when, f120, f9b8, fade, speed,
        threads);
}

// Our own room load: world sync's count of this game's completed loads. Not its epoch, which also moves when the
// host's room changes or the other game restarts, while our actor still stands (review 2026-11-15).
std::uint32_t OwnLoad() {
    std::uint16_t load = 0;
    WorldSyncNetIdOf(0, &load);
    return load;
}

// The liveness rule (the header): 0 once the room changed or the actor is another object.
std::uintptr_t Live() {
    if (!g_ours.actor) return 0;
    std::uintptr_t row = 0;
    std::uint32_t id = 0;
    if (g_ours.epoch == OwnLoad() && ReadAny(g_ours.actor + kObjRow, &row) && row && ReadAny(row, &id) &&
        id == g_ours.obj)
        return g_ours.actor;
    Log("summon sync: our summon (object %#x) forgotten after %u poses: %s", g_ours.obj, g_ours.poses,
        g_ours.epoch != OwnLoad() ? "our room changed (the game frees it with the room)" : "it is another object now");
    g_ours = {};
    return 0;
}

void __fastcall HookBrain(std::uintptr_t actor) {
    // Brain off until its removal: then it runs its own 'leave' act, as at the game's Dismiss (exe+0x3DE080), and
    // deletes itself. Faded out with its brain off, 2 script threads stayed and the game never freed it (bench).
    if (actor && actor == g_ours.actor && !g_ours.leaving) {
        auto* flags = reinterpret_cast<std::uint32_t*>(actor + kBrainFlags);
        if (!(*flags & kBrainSetUp)) {  // as the game's own first call: the type handler's +0x150, then the bit
            std::uintptr_t handler = DecodeHandle(*reinterpret_cast<const std::uint32_t*>(actor));
            if (handler)
                reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
                    *reinterpret_cast<const std::uintptr_t*>(*reinterpret_cast<const std::uintptr_t*>(handler) + 0x150))(
                    handler, actor);
            *flags |= kBrainSetUp;
        }
        ++g_brainSkips;
        return;
    }
    g_realBrain(actor);
}

// One of the three files in the actor's set: logged, and whether it is one of ours.
bool FileOurs(std::uintptr_t actor, std::uintptr_t off) {
    std::uintptr_t p = 0, entry = 0;
    int group = 0;
    if (!ReadAny(actor + off, &p) || !p) {
        Log("summon sync:   set +0x%llX: none", static_cast<unsigned long long>(off));
        return true;
    }
    entry = FormLookLiveFileAt(p);
    if (entry) ReadAny(entry + 0x48, &group);
    bool ours = entry && FormLookCopyFilesOurs(group);
    Log("summon sync:   set +0x%llX: %.40s, group %d%s", static_cast<unsigned long long>(off), FormLookFileHolding(p),
        group, ours ? " (ours)" : entry ? ": THE GAME'S" : ": not a settled file");
    return ours;
}

void StartRemoval(const char* why) {
    std::uintptr_t actor = Live();
    if (!actor || g_ours.fadeAt) return;
    std::uintptr_t held = Slot(game::kFriendSlots, g_ours.slot);
    if (held && held != actor) {
        static std::uint64_t logged = 0;
        if (GetTickCount64() - logged > 2000) {
            logged = GetTickCount64();
            Log("summon sync: our summon's removal (%s) waits: its free would clear party slot %d, which holds %.16s",
                why, g_ours.slot, ModelOf(held));
        }
        return;
    }
    g_askRemove = false;
    g_ours.fadeAt = GetTickCount64();
    g_ours.leaving = true;
    const char* reply = SharedMoveStartAct(actor, "leave");
    LogState(actor, "at its leave");
    Log("summon sync: our summon %.16s (object %#x) leaves the game's way (%s): %s; %u brain calls skipped",
        ModelOf(actor), g_ours.obj, why, reply, g_brainSkips);
}

void Build() {
    std::uint32_t obj = g_ask, epoch = OwnLoad(), state = 0;
    std::uintptr_t copy = PuppetCloneActor();
    ReadAny(ExeBase() + game::kSummonState, &state);
    const char* no = g_ours.actor ? "we have one already"
                     : !copy ? "no Sora copy to put it by"
                     : state || Slot(game::kSummonSlots, 0) || Slot(game::kSummonSlots, 1) ? "our own summon is out"
                     : Slot(game::kFriendSlots, 0) && Slot(game::kFriendSlots, 1) ? "both party slots are taken"
                     : nullptr;
    if (no) {  // their summon is asked for again at the next step
        if (no != g_lastNo) Log("summon sync: object %#x not built yet: %s", obj, no);
        g_lastNo = no;
        g_ask = 0;
        return;
    }
    g_lastNo = nullptr;
    auto refuse = [obj, epoch] {  // not in this room any more
        g_refusedObj = obj;
        g_refusedEpoch = epoch;
        g_ask = 0;
    };
    if (g_groupObj != obj || g_groupEpoch != epoch || !FormLookCopyFilesOurs(g_group)) {
        char what[48];
        std::snprintf(what, sizeof(what), "the summon (object %#x)", obj);
        bool declined = false;
        int group = FormLookAskObjectFiles(what, obj, &declined);
        if (!group) {
            if (declined) {
                Log("summon sync: object %#x not built: its files were declined (no summon in this room)", obj);
                refuse();
            }
            return;  // else: the room isn't settled yet, asked again next step
        }
        g_group = group;
        g_groupObj = obj;
        g_groupEpoch = epoch;
        g_askedAt = GetTickCount64();
        return;
    }
    if (!FormLookCopyFilesLoaded(g_group)) {  // ours: the game's own copy of them (group 5000) may be loaded already
        if (GetTickCount64() - g_askedAt > kFilesWaitMs) {
            Log("summon sync: object %#x not built: its files (group %d) didn't load in %llu ms", obj, g_group,
                kFilesWaitMs);
            refuse();
        }
        return;
    }
    g_ask = 0;
    LogSlots("before the build");
    float at[3] = {}, yaw = 0.0f;
    for (int k = 0; k < 3; ++k) ReadAny(copy + kActorPos + 4 * k, &at[k]);
    ReadAny(copy + kActorAngle, &yaw);
    float pos[4] = {at[0] + std::cos(yaw) * 120.0f, at[1], at[2] - std::sin(yaw) * 120.0f, 1.0f};  // beside the copy
    SummonPose theirs{};
    bool fresh = false;
    if (g_askByPeer && CompanionSyncPeerSummon(0, &theirs, &fresh) && theirs.obj == obj) {  // where theirs stands
        for (int k = 0; k < 3; ++k) pos[k] = theirs.pos[k];
        yaw = theirs.angle;
    }
    FormLookCopyFilesFill(true);  // the build's any-group file lookups take our entries
    std::uintptr_t actor = SpawnSyncMake(obj, pos, yaw);
    FormLookCopyFilesFill(false);
    if (!actor) {
        Log("summon sync: object %#x not built: the factory made nothing (budget full?)", obj);
        refuse();
        return;
    }
    g_ours = {};
    g_ours.actor = actor;
    g_ours.obj = obj;
    g_ours.epoch = epoch;
    g_ours.builtAt = GetTickCount64();
    g_ours.byPeer = g_askByPeer;
    g_ours.lastMotion = -1;
    g_brainSkips = 0;
    LogSlots("after the build");
    ReadAny(actor + kPartySlot, &g_ours.slot);
    if (g_ours.slot < 0 || g_ours.slot > 1) g_ours.slot = 0;
    int cleared = 0;
    constexpr std::uintptr_t kSlotArrays[] = {game::kFriendSlots, game::kSummonSlots};
    for (std::uintptr_t array : kSlotArrays)
        for (int i = 0; i < 2; ++i)
            if (Slot(array, i) == actor) {
                *reinterpret_cast<std::uintptr_t*>(ExeBase() + array + 8 * i) = 0;
                ++cleared;
            }
    std::uintptr_t row = 0;
    std::uint8_t type = 0;
    if (ReadAny(actor + kObjRow, &row) && row) ReadAny(row + 4, &type);
    Log("summon sync: built %.16s (object %#x, type %#x) at (%.0f, %.0f, %.0f), files asked %llu ms ago in group %d; "
        "its party slot %d; taken out of %d of the game's slots",
        ModelOf(actor), obj, type, pos[0], pos[1], pos[2], GetTickCount64() - g_askedAt, g_group, g_ours.slot, cleared);
    bool ours = true;
    constexpr std::uintptr_t kSetFiles[] = {0x920, 0x928, 0x930};  // the actor's set: model, effects, motions
    for (std::uintptr_t off : kSetFiles)
        ours = FileOurs(actor, off) && ours;
    std::uint16_t character = 0;
    if (row) ReadAny(row + 0x4C, &character);
    g_ours.stitch = type == kSummonType && character == 0xC8;
    if (type != kSummonType || !ours) {
        g_refusedObj = obj;
        g_refusedEpoch = epoch;
        StartRemoval(ours ? "not a summon object" : "it reads a file that isn't ours");
    }
}

}  // namespace

void SummonSyncSpawnStep() {
    if (!g_on) return;
    __try {
        std::uintptr_t actor = Live();
        std::uint64_t now = GetTickCount64();
        if (actor && !g_ours.looked && now - g_ours.builtAt > 1500) {  // research: what it does with its brain off
            g_ours.looked = true;
            int motion = -1;
            float y = 0.0f;
            ReadAny(actor + kActorMotion, &motion);
            ReadAny(actor + kActorPos + 4, &y);
            Log("summon sync: 1.5 s after the build: motion %d, y %.0f, %u brain calls skipped", motion, y, g_brainSkips);
            LogSlots("1.5 s after the build");
        }
        if (actor && g_askRemove) StartRemoval("asked");
        // The other player's summon: built once it is out there, gone when it ends there (or their game goes quiet).
        SummonPose theirs{};
        bool fresh = false;
        bool out = CompanionSyncPeerSummon(0, &theirs, &fresh);
        if (out && !g_ours.actor && !g_ask && !(g_refusedObj == theirs.obj && g_refusedEpoch == OwnLoad())) {
            g_ask = theirs.obj;
            g_askByPeer = true;
        }
        if (g_ask && g_askByPeer && (!out || theirs.obj != g_ask)) g_ask = 0;  // gone while its files loaded
        if (actor && g_ours.byPeer && !g_ours.fadeAt && (!out || theirs.obj != g_ours.obj))
            StartRemoval(out ? "theirs is another summon now" : "the other player's summon ended");
        // Its leave didn't free it: faded out by its script and still here 1.5 s in (Peter Pan: an effect whose
        // fade-out wasn't run keeps the removal check waiting), or 8 s in whatever its fade.
        float fade = 1.0f;
        if (actor) ReadAny(actor + 0xA08, &fade);
        if (actor && g_ours.fadeAt && !g_ours.shutdown &&
            now - g_ours.fadeAt > (fade <= 0.01f ? kFadedWaitMs : kShutdownAfterMs)) {
            g_ours.shutdown = true;
            LogState(actor, "before the fallback");
            Log("summon sync: our summon is still here %llu ms after its leave began: faded out, %d of its effects "
                "stopped (the removal check waits for them)", now - g_ours.fadeAt, FormLookStopEffectsOf(actor));
            WorldSyncFadeOut(actor);
            g_ours.stateLogAt = now;
        }
        if (actor && g_ours.shutdown && g_ours.stateLogs < 6 && now - g_ours.stateLogAt > 1000) {
            g_ours.stateLogAt = now;
            ++g_ours.stateLogs;
            LogState(actor, "after the fallback");
        }
        if (g_ask) Build();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("summon sync: exception in its step: the request dropped");
        FormLookCopyFilesFill(false);
        g_ask = 0;
    }
}

// While our summon runs its own leave, its script runs for the first time here, with what its 'appear' would have
// set still unset. Not run (benches 2026-11-15, Peter Pan: crashes at exe+0x3B6638 and exe+0x2BE6A6):
//   - a call passing a variable the script never set (the game's fill pattern for those);
//   - an actor call (trap_obj_..., trap_sysobj_...) whose first argument isn't an actor of this game's list.
// Only calls with arguments: a call without any leaves old data in the first slot. Other calls run as they are (a
// sheet's address is tagged like an actor's; holding those crashed at exe+0x536F90).
bool SummonSyncHolds(int table, int index, const std::uint64_t* args, int argc) {
    if (argc < 1 || !g_ours.actor || !g_ours.leaving || WorldCensusScriptActor() != g_ours.actor) return false;
    constexpr std::uint64_t kUnset = 0xEFACCAFEEFACCAFEull;  // a script variable never written
    constexpr std::uint64_t kTagAddress = 0x52444140;        // "@ADR" in the high half: the argument is an address
    const char* name = WorldCensusTrapName(table, index);
    bool unset = false;
    for (int i = 0; i < argc; ++i) unset = unset || args[i] == kUnset;
    bool actorCall = std::strncmp(name, "trap_obj_", 9) == 0 || std::strncmp(name, "trap_sysobj_", 12) == 0;
    if (!unset && !(actorCall && (args[0] >> 32) == kTagAddress && !WorldMirrorActorArg(args[0]))) return false;
    if (g_ours.held++ < 30)
        Log("summon sync: our leaving summon's %s (first argument %#llx) not run: %s", name,
            static_cast<unsigned long long>(args[0]), unset ? "a variable its script never set" : "no actor of this game");
    return true;
}

// The motion hook (puppet.cpp): the game sets every actor's motion again each frame; ours takes only the motions of
// their summon's pose, until its own leave.
bool SummonSyncBlocksMotion(std::uintptr_t actor) {
    return actor && actor == g_ours.actor && g_ours.byPeer && !g_ours.leaving;
}

bool SummonSyncOut() { return g_ours.actor && !(g_ours.fadeAt && GetTickCount64() - g_ours.fadeAt > 12000); }

// After the actor's own update (puppet.cpp's per-entity hook): their summon's place, facing and motion on ours, as
// Mickey's stand-in takes the winner's.
void SummonSyncAfterUpdate(std::uintptr_t actor) {
    if (!actor || actor != g_ours.actor || !g_ours.byPeer || g_ours.leaving) return;
    SummonPose p{};
    bool fresh = false;
    if (!CompanionSyncPeerSummon(0, &p, &fresh) || !fresh || p.obj != g_ours.obj) return;
    auto* pos = reinterpret_cast<float*>(actor + kActorPos);
    float dx = p.pos[0] - pos[0], dy = p.pos[1] - pos[1], dz = p.pos[2] - pos[2];
    float t = dx * dx + dy * dy + dz * dz > 300.0f * 300.0f ? 1.0f : 0.5f;  // far: at once, else smoothed
    pos[0] += dx * t;
    pos[1] += dy * t;
    pos[2] += dz * t;
    *reinterpret_cast<float*>(actor + kActorAngle) = p.angle;
    *reinterpret_cast<float*>(actor + kActorSin) = std::sin(p.angle);
    *reinterpret_cast<float*>(actor + kActorCos) = std::cos(p.angle);
    std::memset(reinterpret_cast<void*>(actor + kActorVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorAccel), 0, 0x18);
    // Stitch is drawn on the screen from his brain's own state (F635): theirs's, ours's brain being off.
    if (const StitchScreen* s = g_ours.stitch ? CompanionSyncPeerStitch() : nullptr) {
        std::uintptr_t helper = 0;
        if (ReadAny(actor + kAiHelper, &helper) && helper && Readable(helper + 0x650, 0x144)) {
            std::memcpy(reinterpret_cast<void*>(helper + 0x650), s->bytes, sizeof(s->bytes));
            *reinterpret_cast<std::uint32_t*>(helper + 0x790) = s->state;
        }
    }
    ++g_ours.poses;
    if (static_cast<int>(p.motion) == g_ours.lastMotion) return;
    g_ours.lastMotion = static_cast<int>(p.motion);
    if (p.otherBank || !PuppetHasMotion(actor, static_cast<int>(p.motion))) {
        if (g_ours.motionsMissing++ < 20)
            Log("summon sync: their summon's motion %u isn't in our summon's set%s: not played", p.motion,
                p.otherBank ? " (from another file)" : "");
        return;
    }
    PuppetSetMotion(actor, static_cast<int>(p.motion));
}

void SummonSyncOnActorFree(std::uintptr_t actor) {
    if (!actor || actor != g_ours.actor) return;
    Log("summon sync: our summon (object %#x) freed%s after %u poses, %u motions not in its set", g_ours.obj,
        g_ours.fadeAt ? "" : " (not by us)", g_ours.poses, g_ours.motionsMissing);
    if (g_ours.fadeAt) Log("summon sync:   %llu ms after its leave began", GetTickCount64() - g_ours.fadeAt);
    g_ours = {};
    LogSlots("at its free");
}

void SummonSyncDebug(int objId) {
    if (!g_on) {
        Log("debug command: summon %#x refused: KH2COOP_SUMMON_SYNC is off", objId);
        return;
    }
    Log("debug command: summon %#x (%s)", objId, objId ? "build it by the copy" : "remove ours");
    if (objId) {
        g_ask = static_cast<std::uint32_t>(objId);
        g_askByPeer = false;
    } else
        g_askRemove = true;
}

void SummonSyncInit() {
    if (EnvInt("KH2COOP_SUMMON_SYNC", 0) != 1) return;
    // hook: the party brain, the only place a summon's think and scripts can be left out for one actor (L1515)
    g_on = HookFunction(game::kPartyBrain, kPartyBrainBytes, sizeof(kPartyBrainBytes), reinterpret_cast<void*>(HookBrain),
                        reinterpret_cast<void**>(&g_realBrain), "party brain (off for the other player's summon)");
    Log("summon sync: %s (the other player's summon as an actor here, following theirs)", g_on ? "on" : "OFF");
}

}  // namespace kh2coop
