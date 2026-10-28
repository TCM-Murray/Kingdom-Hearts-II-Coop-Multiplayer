// The other player's Drive Form on their copy (TODO 1.24; DECISIONS Q5, your picks 2026-10-21: the copy itself takes
// the form's model and motions, all forms).
//
// When: the pose packet carries the object id of the peer's player body (avatar_link.cpp, actor+0x918's row), which
// changes when their game swaps the form body in (~0.67 s after the form change) and back at the Revert. Each frame
// FormLookFrame picks the look from it (a Drive Form's object, forms 1-6, else the copy's own) and the spawn step
// (game thread) puts it on. A boss's capture (die / card) is stand_in.cpp's, not ours.
//
// How (research step 1, test command 18 'formlook <form>'): every form model has Sora's skeleton (VERIFIED_OFFSETS),
// so the copy keeps its actor and only its look changes, the way the game builds an actor's look (base init
// exe+0x3B3200):
//   - the resource set at actor+0x910 is refilled for the form's object with exe+0x3E63D0(set, objId, entry), which
//     lets go of the old files and takes the new ones (+0x920 model file, +0x928, +0x930 motions);
//   - the model (exe+0x3E62E0(set), +0xA88) makes a render instance (its vtable +0x40, with the allocator at
//     exe+0x9BA920) for actor+0x7B0; the texture (exe+0x3E6310(set), +0x7E0) is bound with exe+0x1A4BB0, then
//     exe+0x1A4E30 and the shadow / level-of-detail step exe+0x3CD820;
//   - the motion context (actor+0x158) points at the new motions (+0x930), as exe+0x3DA940 does; then the puppet
//     sets the peer's current motion again.
// Step 2, the files (option B of the review's 5.2.6, your pick, session 46; DECISIONS 'the copy's files'): a form's
// look is always worn from files of ours, asked for the way the Drive does (exe+0x3E0660(objId, group, 4, 7) queues
// them, then exe+0x39D460(0) lays the file heap out and starts the reads) into a negative group of ours, even when
// the game has them (our own Sora in that form, or its Drive's files a Revert left in 5000, F357 / F358). Negative
// groups sit at the heap's end, where no game area reaches them and no drop from 0 or more lets them go (L148), so our
// own Drive, its Revert and the Limit preload (F356) no longer move or free the copy's files. They are only added,
// stay for the whole room and are let go at the room's own drop; a look over the budget is declined (plain Sora for
// that form). The set fill asks each file in 'any group' (exe+0x3E6120, group -1), which is the first listed entry
// of that name (exe+0x39E1E0), and the list runs from group 0 up before the negative groups: during our fill those
// lookups are given our entry (HookGroupFind).
// The loader is only touched (ask, lay out) while no read is in flight, the check the game's own wait makes
// (exe+0x39E340: exe+0x2A0D628 = 0, exe+0x1DC340() = 0, no listed file of a group under 10000 in state 1 or 2), as
// the game does: a layout during a read would clear its in-flight flag; and only once the room's files are all in and
// placed (RoomNotSettled).
// The follow-up of the Drive-area design stays as a net: a file the copy wears that moves anyway is noted
// (exe+0x39E690) and, at the game's load wait (exe+0x39E340) or our next spawn step, the set is pointed at the new
// places and the look goes on again. Without that, the copy's next motion step called a moved-away motion object
// (pure virtual call, abort, 0xC0000409 past our crash logger; Valor + Wisdom bench 2026-10-21).
// The old render instance is kept, not freed (research).

#include "form_look.hpp"

#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "copy_blade.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kFormObject = 0x405190;    // (form) -> u16 object id of that form's body
constexpr std::uintptr_t kObjectEntry = 0x3DFEB0;   // (objId) -> object entry
constexpr std::uintptr_t kFilesState = 0x3E0160;    // (objId, entry) -> bit per file state; & ~0xC = not loaded
constexpr std::uintptr_t kAskFiles = 0x3E0660;      // (objId, group, 4, 7): the Drive's request
constexpr std::uintptr_t kStartLoads = 0x39D460;    // (task or 0): lay the areas out, start the reads
constexpr std::uintptr_t kPlaceFile = 0x39E690;     // (entry): moves a loaded file to its new place / starts a read
constexpr std::uint8_t kPlaceFileBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x41, 0x44, 0x01};
constexpr std::uintptr_t kLoadWait = 0x39E340;      // (task): waits for the reads; exe+0x39D460(task) calls it after
                                                    // its moves, before any actor update reads the moved files
constexpr std::uint8_t kLoadWaitBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kResHead = 0x29F33C8;      // resource list head; next = exe+0x4AD270(+0x70)
constexpr std::uintptr_t kHandle = 0x4AD270;
constexpr std::uintptr_t kReaderBusy = 0x2A0D628;   // u64, non-zero while the file reader works (exe+0x3A0A30)
constexpr std::uintptr_t kLoaderBusy2 = 0x1DC340;   // () -> non-zero: busy (exe+0x39E340's second check)
constexpr std::uintptr_t kDrop = 0x39CCA0;          // (group): every group >= it + 10000 (5000: the Drive's)
constexpr std::uint8_t kDropBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89};
constexpr std::uintptr_t kHeapNegBottom = 0x29F33F0;  // the layout's bottom of the from-the-end (negative group) files
constexpr std::uint64_t kLoadTimeoutMs = 15000;
constexpr int kLastFollowedForm = 6;             // Valor, Wisdom, Limit, Master, Final, Anti
constexpr std::uintptr_t kSetFill = 0x3E63D0;       // (set, objId, entry)
constexpr std::uintptr_t kSetModel = 0x3E62E0;      // (set) -> model
constexpr std::uintptr_t kSetTexture = 0x3E6310;    // (set) -> texture
constexpr std::uintptr_t kModelAlloc = 0x9BA920;    // the allocator a render instance is made with
constexpr std::uintptr_t kBindTexture = 0x1A4BB0;   // (instance, texture)
constexpr std::uintptr_t kInstanceDone = 0x1A4E30;  // (instance)
constexpr std::uintptr_t kShadowStep = 0x3CD820;    // (actor)
constexpr std::uintptr_t kSet = 0x910, kSetObjId = 0x914, kSetMotions = 0x930;
constexpr std::uintptr_t kEffects = 0xB63608;       // effect groups: +0x18 first effect, next group = handle(+0x28)
constexpr std::uintptr_t kStopEffect = 0x2BE8A0;    // (effect instance): hard stop (+0xC |= 0x10001, its particle
                                                    // system +0x30 deleted, its sound ended)
constexpr std::uintptr_t kFadeEffect = 0x2BE8E0;    // (effect instance): its particle system (+0x30) stops making
                                                    // new ones (+0x91 = 1), the rest fade out; no null check
constexpr std::uintptr_t kPlayer = 0x2A105D0;       // the player's actor (our Sora, or its form body)
constexpr std::uintptr_t kWeapons[2] = {0xD60, 0xD68};  // an actor's weapon actors
constexpr std::uintptr_t kListHead = 0x2A171C8;     // the actor list (our Sora first); next = decode(+0xA90)
constexpr std::uintptr_t kNextActor = 0xA90;
constexpr std::uintptr_t kSetMotion = 0x3C86A0;     // (motion controller, motion, start, blend)
constexpr std::uintptr_t kWeaponObject = 0x3E0CC0;  // (entry, slot) -> the object's weapon slot's object, 0 none
constexpr std::uintptr_t kAlloc = 0x152430;         // (size) -> the game's actor memory
constexpr std::uintptr_t kNewWeapon = 0x3EF750;     // (mem, weapon entry, owner, weapon kind, slot, group)
constexpr std::uintptr_t kMotionSync = 0x3C8440;    // (controller, owner+0x188), as exe+0x3D7C70 does
constexpr std::uintptr_t kReleaseFile = 0x39DA10;   // (place): the file's users - 1
constexpr std::uintptr_t kInstance = 0x7B0, kModel = 0xA88, kTexture = 0x7E0, kMotionCtx = 0x158, kFlags9B8 = 0x9B8;

int g_on = -1;
std::uint32_t g_ask = 0;         // object the copy should look like, 0 none asked
int g_debugForm = -1;            // test command: form asked for, -1 none
std::uint32_t g_lastWant = 0;    // the look last picked from the peer's body
std::uint32_t g_waiting = 0;     // object whose files we asked for, 0 none
std::uint64_t g_waitSince = 0;
std::uint32_t g_wearing = 0;     // the object the copy looks like
std::uintptr_t g_copyWas = 0;    // the copy whose own object we know
std::uint32_t g_ownObject = 0;   // its object before any change
std::uint32_t g_formObj[11] = {};  // forms 1-10's objects (read once)
bool g_fillOurs = false;         // our set fill or weapon build for the copy runs: 'any group' lookups take ours
// An ended actor is only deleted some frames later (750 ms when the friend's Valor ran out: host crash 2026-10-25
// 16:05:38, L137), in steps that read our files: its teardown (the weapon's exe+0x3EF520 -> exe+0x3B45C0 -> the
// detach exe+0x3DA250) reads its motion controller (crash exe+0x3C7B89) and marks it (+0x120 bit 0x80000); a later
// delete pass (exe+0x3BF300) frees its model instance through the model's own code (exe+0x3B4E80 -> handler +0x30 ->
// exe+0x3B43C0, +0xA88; crash exe+0x3B43FE, bench 2026-10-25 17:15:49) and then the actor. Our files stay for the
// whole room (option B), so nothing waits for that free any more; it is logged.
constexpr std::uintptr_t kActorFree = 0x3B4E80;  // (actor): the delete pass's resource free, then the destructor
constexpr std::uint8_t kActorFreeBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81, 0xB8, 0x09, 0x00, 0x00};
constexpr int kEndedMax = 4;
std::uintptr_t g_ended[kEndedMax] = {};  // ended second Keyblades of ours not freed yet, 0 free
std::uint64_t g_endedAt[kEndedMax] = {};
using PFN_ActorFree = void(__fastcall*)(std::uintptr_t);
PFN_ActorFree g_realActorFree = nullptr;
using PFN_Drop = void(__fastcall*)(int);
PFN_Drop g_realDrop = nullptr;
using PFN_PlaceFile = void(__fastcall*)(std::uintptr_t);
PFN_PlaceFile g_realPlaceFile = nullptr, g_realLoadWait = nullptr;
bool g_wornMoved = false;        // a file the copy's look points into was moved; the look goes on again next safe
                                 // moment (the game's load wait, or our spawn step)
// The list entries of the files the copy's look points into (+0x920 model, +0x928, +0x930 motions) and where they
// were when the look went on: a layout that moves them (the game's own Drive loading its form, bench 2026-10-21
// 22:10) leaves the copy pointing at the old place.
// [3]: the motion file of the copy's Keyblade (weapon+0x158); [4..6] the second Keyblade's set files, [7] its
// motion file.
constexpr int kWorn = 8;
std::uintptr_t g_wornEntry[kWorn] = {}, g_wornPlace[kWorn] = {};
std::uintptr_t g_weaponOwnMotions = 0;  // the copy's Keyblade's own motion file (W_EX010.mset), put back at the Revert
std::uintptr_t g_secondBlade = 0;       // the second Keyblade we built on the copy (copy+0xD68), 0 none
std::uintptr_t g_secondPending = 0;     // the form entry whose second Keyblade waits for the other player's files
std::uint64_t g_glowCheckAt = 0;  // research: when to list the copy's effects again after a look change, 0 none
bool g_glowDeclined = false;     // that change was a declined look: the check stops the shine too
std::uintptr_t g_ownBody = 0;       // research: our player's actor last seen (a Drive swaps it)
std::uint64_t g_ownWeaponsAt = 0;   // research: when to log our new body's weapons, 0 none

// Every read goes through the safe read (CRASH_RISK_REVIEW 5.2.1, common.hpp ReadAny): a fault lands as 0 instead of
// ending the game, and a pointer outside the areas we know are mapped is checked with Readable first.
template <typename T>
T Read(std::uintptr_t a) {
    T v{};
    ReadAny(a, &v);
    return v;
}

template <typename T>
T Fn(std::uintptr_t rva) {
    return reinterpret_cast<T>(ExeBase() + rva);
}

// True while an ended second Keyblade of ours waits for its delete (it reads its files until then); 30 s at most.
bool EndedPending() {
    bool pending = false;
    std::uint64_t now = GetTickCount64();
    for (int k = 0; k < kEndedMax; ++k) {
        if (!g_ended[k]) continue;
        if (now - g_endedAt[k] < 30000) {
            pending = true;
            continue;
        }
        Log("form look: the ended second Keyblade %p not seen freed after 30 s: no longer waited for",
            reinterpret_cast<void*>(g_ended[k]));
        g_ended[k] = 0;
    }
    return pending;
}

// The delete pass calls the actor's destructor right after this, in the same loop (logged: when an ended second
// Keyblade of ours is really gone).
void __fastcall HookActorFree(std::uintptr_t actor) {
    g_realActorFree(actor);
    for (int k = 0; k < kEndedMax; ++k) {
        if (!g_ended[k] || g_ended[k] != actor) continue;
        Log("form look: the ended second Keyblade %p freed %llu ms after its end",
            reinterpret_cast<void*>(actor), GetTickCount64() - g_endedAt[k]);
        g_ended[k] = 0;
    }
}

std::uint32_t FormObject(int form) {
    if (!g_formObj[1]) {
        for (int f = 1; f <= 10; ++f) g_formObj[f] = Fn<std::uint16_t(__fastcall*)(int)>(kFormObject)(f);
        Log("form look: form objects 1-10: %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x", g_formObj[1], g_formObj[2],
            g_formObj[3], g_formObj[4], g_formObj[5], g_formObj[6], g_formObj[7], g_formObj[8], g_formObj[9],
            g_formObj[10]);
    }
    return form >= 1 && form <= 10 ? g_formObj[form] : 0;
}

int FormOf(std::uint32_t obj) {
    for (int f = 1; f <= 10; ++f)
        if (obj && FormObject(f) == obj) return f;
    return 0;
}

const char* NameOf(std::uintptr_t entry) {
    return reinterpret_cast<const char*>(entry + 8);
}

std::uintptr_t NextRes(std::uintptr_t r) {
    return Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(r + 0x70));
}

// The copy's effects (owner +0x50 = the copy). The game's body swap (Drive / Revert) moves the old body's effects to
// the new one (exe+0x2C4480) except those whose definition (+0x48) has flag 0x200 at +0x10: they end with the old
// body. The copy keeps its body, so those (the Drive's white shine and sparkles, started while it plays the peer's
// Drive motion) stayed on it for the whole form (bench screenshots 2026-10-21). `stop`: stop those, as the swap
// would; else only list them.
void CopyBodyEffects(std::uintptr_t copy, bool stop) {
    int all = 0, bodyOnly = 0, stopped = 0;
    std::uintptr_t g = Read<std::uintptr_t>(ExeBase() + kEffects);
    for (int ng = 0; g && ng < 4096; ++ng) {
        std::uintptr_t e = Read<std::uintptr_t>(g + 0x18);
        for (int ne = 0; e && ne < 4096; ++ne) {
            std::uintptr_t next = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(e + 0x74));
            if (Read<std::uintptr_t>(e + 0x50) == copy) {
                ++all;
                std::uintptr_t def = Read<std::uintptr_t>(e + 0x48);
                std::uintptr_t inst = Read<std::uintptr_t>(e + 0x40);
                std::uint32_t defFlags = def ? Read<std::uint32_t>(def + 0x10) : 0;
                std::uint32_t instFlags = inst ? Read<std::uint32_t>(inst + 0xC) : 0;
                bool body = (defFlags & 0x200) != 0;
                bool live = inst && !(instFlags & 0x100000);
                if (body) ++bodyOnly;
                if (all <= 16)
                    Log("form look:   copy effect %p def %p flags %#x, instance %p flags %#x%s", reinterpret_cast<void*>(e),
                        reinterpret_cast<void*>(def), defFlags, reinterpret_cast<void*>(inst), instFlags,
                        body ? (stop && live ? " (ends with the old body: fades out)" : " (ends with the old body)") : "");
                if (stop && body && live && Read<std::uintptr_t>(inst + 0x30)) {
                    Fn<void(__fastcall*)(std::uintptr_t)>(kFadeEffect)(inst);
                    ++stopped;
                }
            }
            e = next;
        }
        g = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(g + 0x28));
    }
    Log("form look: the copy's effects: %d, %d of the kind that ends with the old body, %d fading out", all, bodyOnly,
        stopped);
}

// An effect counts while its particle system (instance +0x30) is alive: one already fading (instance +0xC has
// 0x10001, as the Keyblade's put-away leaves its flash) still reads its definition in the file (debugger dump,
// bench 23:19: owner = the old second Keyblade, definition and particle system pointers inside the old place of
// W_EX010_E0.a.us, flags 0x10001).
// Stops every effect an actor owns (+0x50) at once. For the second Keyblade we end or whose files a layout moves:
// its effects (the appear flash) read its files (Valor + Wisdom bench 23:03: 75 ms after the files moved, the
// game's effect update read the old place of W_EX010_E0.a.us for the ended Keyblade, exe+0x4B0242, and the game
// ended).
int StopEffectsOf(std::uintptr_t owner) {
    int stopped = 0;
    std::uintptr_t g = Read<std::uintptr_t>(ExeBase() + kEffects);
    for (int ng = 0; g && ng < 4096; ++ng) {
        std::uintptr_t e = Read<std::uintptr_t>(g + 0x18);
        for (int ne = 0; e && ne < 4096; ++ne) {
            std::uintptr_t next = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(e + 0x74));
            std::uintptr_t inst = Read<std::uintptr_t>(e + 0x40);
            if (Read<std::uintptr_t>(e + 0x50) == owner && inst && Read<std::uintptr_t>(inst + 0x30)) {
                Fn<void(__fastcall*)(std::uintptr_t)>(kStopEffect)(inst);
                ++stopped;
            }
            e = next;
        }
        g = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(g + 0x28));
    }
    return stopped;
}

// Stops at once every effect whose definition (+0x48) lies in [from, from + size): a file about to move. The game's
// move (exe+0x39E690) fixes some pointers into the file by its name (exe+0x12DF90), not these (Valor + Wisdom bench
// 23:09: our second Keyblade's file W_EX010_E0.a.us moved, none of the effects read from it was owned by the
// Keyblade (+0x50), and the effect update read its old place, exe+0x4B0242).
int StopEffectsIn(std::uintptr_t from, std::uintptr_t size, const char* file) {
    int stopped = 0;
    std::uintptr_t g = Read<std::uintptr_t>(ExeBase() + kEffects);
    for (int ng = 0; g && ng < 4096; ++ng) {
        std::uintptr_t e = Read<std::uintptr_t>(g + 0x18);
        for (int ne = 0; e && ne < 4096; ++ne) {
            std::uintptr_t next = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(e + 0x74));
            std::uintptr_t def = Read<std::uintptr_t>(e + 0x48);
            std::uintptr_t inst = Read<std::uintptr_t>(e + 0x40);
            if (def >= from && def < from + size && inst && Read<std::uintptr_t>(inst + 0x30)) {
                Fn<void(__fastcall*)(std::uintptr_t)>(kStopEffect)(inst);
                if (++stopped <= 4)
                    Log("form look:   effect %p (owner %p) reads %.40s, about to move: stopped",
                        reinterpret_cast<void*>(e), reinterpret_cast<void*>(Read<std::uintptr_t>(e + 0x50)), file);
            }
            e = next;
        }
        g = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(g + 0x28));
    }
    return stopped;
}

const char* FileAt(std::uintptr_t place) {
    if (!place) return "-";
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r))
        if (Read<std::uintptr_t>(r + 0x58) == place) return reinterpret_cast<const char*>(r + 4);
    return "?";
}

// Research (the Keyblades per form): an actor's weapon actors, their object and the files their set holds.
void LogWeapons(std::uintptr_t actor, const char* who) {
    Log("form look: %s (object %#x) weapons:", who, Read<std::uint32_t>(actor + kSetObjId));
    for (int k = 0; k < 2; ++k) {
        std::uintptr_t w = Read<std::uintptr_t>(actor + kWeapons[k]);
        if (!w) {
            Log("form look:   weapon %d: none", k);
            continue;
        }
        Log("form look:   weapon %d %p: object %#x, files %.40s | %.40s | %.40s, motions in use %.40s, owner %p, "
            "flags +0x124 %#x", k, reinterpret_cast<void*>(w), Read<std::uint32_t>(w + kSetObjId),
            FileAt(Read<std::uintptr_t>(w + 0x920)), FileAt(Read<std::uintptr_t>(w + 0x928)),
            FileAt(Read<std::uintptr_t>(w + 0x930)), FileAt(Read<std::uintptr_t>(w + kMotionCtx)),
            reinterpret_cast<void*>(Read<std::uintptr_t>(w + 0xBF8)), Read<std::uint32_t>(w + 0x124));
    }
}

// Lays the heap out and starts the reads (no wait). If it moves the copy's files, the move hook notes it and the spawn
// step puts the look on again.
void StartLoads() {
    Fn<void(__fastcall*)(std::uintptr_t)>(kStartLoads)(0);
}

// No read in flight (exe+0x39E340's check): only then may the heap be laid out.
bool LoaderIdle() {
    if (Read<std::uint64_t>(ExeBase() + kReaderBusy)) return false;
    if (Fn<std::uint8_t(__fastcall*)()>(kLoaderBusy2)()) return false;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        std::uint8_t state = Read<std::uint8_t>(r + 1);
        if (Read<int>(r + 0x48) < 10000 && state != 0 && state != 3) return false;
    }
    return true;
}

// The Drive area's files (groups 5000-5099), and the file heap's free room (research log).
void LogDriveArea(const char* when) {
    unsigned n = 0, loaded = 0;
    unsigned long long total = 0;
    std::uintptr_t top = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        int group = Read<int>(r + 0x48);
        std::uintptr_t end = Read<std::uintptr_t>(r + 0x58) + Read<std::uint64_t>(r + 0x50);
        if (group >= 0 && group < 10000 && end > top) top = end;
        if (group >= 5000 && group < 5100) {
            ++n;
            total += Read<std::uint64_t>(r + 0x50);
            loaded += Read<std::uint8_t>(r + 1) == 3;
        }
    }
    std::uintptr_t bottom = Read<std::uintptr_t>(ExeBase() + kHeapNegBottom);
    Log("form look: Drive area %s: %u files, %llu bytes (area 0x300000), %u loaded; file heap top %p, from-the-end "
        "files from %p: %lld bytes free", when, n, total, loaded, reinterpret_cast<void*>(top),
        reinterpret_cast<void*>(bottom), static_cast<long long>(bottom) - static_cast<long long>(top));
}

// Every drop is noted (the census), and ours are let go at the room's own drop (NegAtDrop). Ours sit in negative
// groups, which no drop from 0 or more reaches (L148), so our own Drive's and the Limit preload's drops from 5000 no
// longer touch the copy's files (F356, F358).
void NegAtDrop(int from, std::uintptr_t caller);

void __fastcall HookDrop(int from) {
    NegAtDrop(from, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    g_realDrop(from);
}

bool Apply(std::uint32_t obj, bool again);

int LogException(EXCEPTION_POINTERS* e, const char* where) {
    std::uintptr_t at = reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    Log("form look: exception 0x%lX at exe+0x%llX %s", e->ExceptionRecord->ExceptionCode,
        static_cast<unsigned long long>(at - ExeBase()), where);
    return EXCEPTION_EXECUTE_HANDLER;
}

// If the files the copy wears moved, the look goes on again from their new place (before the copy's next update
// reads the old one). The game's own layouts run in its tasks (the Drive's, exe+0x40D000), on the game thread, and
// its call exe+0x39D460(task) moves every file (exe+0x39E690, hooked: notes it) and then waits frames for the reads
// (exe+0x39E340, hooked: the look goes on again there, all moves done; mid-moves it threw). Ours (no wait) are
// checked from the spawn step.
// The set (copy+0x910) holds the worn files by place (+0x10/+0x18/+0x20); refilling it lets each go by that place
// (exe+0x39DA10: finds the entry whose +0x58 matches, else writes through null: bench 22:20, exe+0x39DA55), so the
// set first gets the files' new places.
// The motion controllers (the body's and its Keyblades', linked) point into the motion files: +0 the file, +0x18 /
// +0x20 the motions playing (debugger 22:10). Each pointer into a moved file is moved with it before anything runs
// a controller (Valor + Wisdom bench 22:52: the body's motion change restarted the Keyblade from its moved-away file
// and the friend's game ended). All moves are taken together, so a pointer is moved once even when one file's new
// place lies in another's old one.
void ShiftControllers(std::uintptr_t copy, const std::uintptr_t* from, const std::uintptr_t* size,
                      const std::uintptr_t* to, int n) {
    std::uintptr_t ctls[3] = {copy + kMotionCtx, 0, 0};
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapons[i]);
        if (w) ctls[1 + i] = w + kMotionCtx;
    }
    int shifted = 0;
    for (std::uintptr_t c : ctls) {
        if (!c) continue;
        for (std::uintptr_t off = 0; off < 0x60; off += 8) {
            auto* q = reinterpret_cast<std::uintptr_t*>(c + off);
            for (int m = 0; m < n; ++m) {
                if (*q < from[m] || *q >= from[m] + size[m]) continue;
                *q = *q - from[m] + to[m];
                ++shifted;
                break;
            }
        }
    }
    if (shifted) Log("form look:   %d motion pointers moved with their files", shifted);
}

// A guard since option B: no layout has moved a file the copy wears (0 in 22 logs, 2026-10-27/28).
void FollowMovedFiles(const char* who) {
    if (!g_copyWas || g_wearing == g_ownObject || PuppetCloneActor() != g_copyWas) return;
    std::uintptr_t from[kWorn], size[kWorn], to[kWorn];
    int n = 0;
    for (int k = 0; k < kWorn; ++k) {
        if (!g_wornEntry[k]) continue;
        std::uintptr_t now = Read<std::uintptr_t>(g_wornEntry[k] + 0x58);
        if (now == g_wornPlace[k]) continue;
        Log("form look: %s moved the copy's %.40s %p -> %p", who, reinterpret_cast<const char*>(g_wornEntry[k] + 4),
            reinterpret_cast<void*>(g_wornPlace[k]), reinterpret_cast<void*>(now));
        from[n] = g_wornPlace[k];
        size[n] = Read<std::uint32_t>(g_wornEntry[k] + 0x50);
        to[n++] = now;
    }
    if (!n) return;
    // The sets (the copy's and its Keyblades') hold their files by place: the new places.
    const std::uintptr_t holders[3] = {g_copyWas, Read<std::uintptr_t>(g_copyWas + kWeapons[0]),
                                       Read<std::uintptr_t>(g_copyWas + kWeapons[1])};
    for (std::uintptr_t a : holders) {
        if (!a) continue;
        for (std::uintptr_t off = 0x920; off <= 0x930; off += 8)
            for (int m = 0; m < n; ++m)
                if (Read<std::uintptr_t>(a + off) == from[m]) {
                    *reinterpret_cast<std::uintptr_t*>(a + off) = to[m];
                    break;
                }
    }
    ShiftControllers(g_copyWas, from, size, to, n);
    Log("form look: the look goes on again from the files' new places");
    Apply(g_wearing, true);
}

void __fastcall HookPlaceFile(std::uintptr_t entry) {
    // A file the copy wears about to move (the layout put its new place at +0x58; the data is still at +0x60 until
    // this call copies it, exe+0x39E690): the effects that read from it stop first, while it's still there.
    std::uintptr_t oldPlace = entry ? Read<std::uintptr_t>(entry + 0x60) : 0;
    if (oldPlace && Read<std::uintptr_t>(entry + 0x58) != oldPlace && !(Read<std::uint32_t>(entry + 0x44) & 3)) {
        for (int k = 0; k < kWorn; ++k) {
            if (entry != g_wornEntry[k]) continue;
            __try {
                int n = StopEffectsIn(oldPlace, Read<std::uintptr_t>(entry + 0x50),
                                      reinterpret_cast<const char*>(entry + 4));
                if (n) Log("form look: %.40s is about to move: %d effects reading it stopped",
                           reinterpret_cast<const char*>(entry + 4), n);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            break;
        }
    }
    g_realPlaceFile(entry);
    for (int k = 0; entry && k < kWorn; ++k)
        if (entry == g_wornEntry[k]) g_wornMoved = true;
}

void __fastcall HookLoadWait(std::uintptr_t task) {
    if (g_wornMoved) {
        g_wornMoved = false;
        __try {
            FollowMovedFiles("the game's file layout");
        } __except (LogException(GetExceptionInformation(), "following the moved files")) {
        }
    }
    g_realLoadWait(task);
}

// Option B's request (defined with the negative groups below): asks files into our next negative group with
// `ask(group)`; the group, or 0 (not now, or *declined: over the budget when `budgeted`, or no room in our list).
int NegAsk(const char* what, bool budgeted, bool keep, void (*ask)(int, const void*), const void* arg, bool* declined);
bool IsOurGroup(int group);
constexpr int kNegSpan = 3;  // groups per request (first, +1, +2: F351 found them all in the first)
std::uint32_t g_freeLook = 0;  // a look the copy no longer wears, whose files go once nothing reads them (one at a time)
std::uint64_t g_freeLookSince = 0;

// The looks asked this room: their group (first of kNegSpan), 0 = declined. Forgotten at the room's own drop, and
// when their files are let go (one look at a time).
constexpr int kLooksMax = 16;
struct Look {
    std::uint32_t obj;
    int group;
};
Look g_looks[kLooksMax] = {};
int g_lookCount = 0;

const Look* LookOf(std::uint32_t obj) {
    for (int k = 0; k < g_lookCount; ++k)
        if (g_looks[k].obj == obj) return &g_looks[k];
    return nullptr;
}

// 'loaded', 'asked' (on their way, or the room not settled yet), 'declined' (over the budget: plain Sora for that
// form) or 'failed'. The copy's own look is the room's own files. A form's look is ours: asked once per room into
// a negative group of ours and worn only from there, even when the game has the form's files (our own Sora in that
// form, or a Revert's leftovers in 5000, F358).
const char* EnsureFiles(std::uint32_t obj, std::uintptr_t entry) {
    if (obj == g_ownObject) return "loaded";
    if (g_freeLook && g_freeLook != obj && !LookOf(obj)) return "asked";  // the last look's room first
    if (const Look* l = LookOf(obj)) {
        if (!l->group) return "declined";
        bool all = true;
        for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && all; r = NextRes(r)) {
            int group = Read<int>(r + 0x48);
            if (group >= l->group && group < l->group + kNegSpan && Read<std::uint8_t>(r + 1) != 3) all = false;
        }
        if (all) {
            if (g_waiting == obj) {
                Log("form look: object %#x (%.32s): our files loaded %llu ms after asking", obj, NameOf(entry),
                    GetTickCount64() - g_waitSince);
                g_waiting = 0;
            }
            return "loaded";
        }
        if (g_waiting == obj && GetTickCount64() - g_waitSince >= kLoadTimeoutMs) {
            Log("form look: object %#x: our files (group %d) still not loaded after %llu ms: given up for now", obj,
                l->group, kLoadTimeoutMs);
            g_waiting = 0;
            return "failed";
        }
        return "asked";
    }
    char what[64];
    std::snprintf(what, sizeof(what), "object %#x's look (%.32s)", obj, NameOf(entry));
    bool declined = false;
    int group = NegAsk(what, true, false, [](int g, const void* o) {
        Fn<void(__fastcall*)(std::uint64_t, int, int, int)>(kAskFiles)(*static_cast<const std::uint32_t*>(o), g, 4, 7);
    }, &obj, &declined);
    if (!group && !declined) return "asked";
    if (g_lookCount < kLooksMax) g_looks[g_lookCount++] = {obj, group};
    if (!group) return "declined";
    g_waiting = obj;
    g_waitSince = GetTickCount64();
    return "asked";
}

void KnowCopy(std::uintptr_t copy) {
    if (copy == g_copyWas) return;
    g_copyWas = copy;
    g_ownObject = Read<std::uint32_t>(copy + kSetObjId);
    g_wearing = g_ownObject;
    std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapons[0]);
    g_weaponOwnMotions = w ? Read<std::uintptr_t>(w + kMotionCtx) : 0;
    g_secondBlade = 0;
}

// Notes which list entries the copy's look points into, and where they are now.
void RecordWorn(std::uintptr_t copy) {
    std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapons[0]);
    std::uintptr_t w1 = Read<std::uintptr_t>(copy + kWeapons[1]);
    const std::uintptr_t places[kWorn] = {Read<std::uintptr_t>(copy + 0x920), Read<std::uintptr_t>(copy + 0x928),
                                          Read<std::uintptr_t>(copy + 0x930),
                                          w ? Read<std::uintptr_t>(w + kMotionCtx) : 0,
                                          w1 ? Read<std::uintptr_t>(w1 + 0x920) : 0,
                                          w1 ? Read<std::uintptr_t>(w1 + 0x928) : 0,
                                          w1 ? Read<std::uintptr_t>(w1 + 0x930) : 0,
                                          w1 ? Read<std::uintptr_t>(w1 + kMotionCtx) : 0};
    for (int k = 0; k < kWorn; ++k) {
        g_wornEntry[k] = g_wornPlace[k] = 0;
        std::uintptr_t p = places[k];
        if (!p) continue;
        for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
            if (Read<std::uintptr_t>(r + 0x58) != p) continue;
            g_wornEntry[k] = r;
            g_wornPlace[k] = p;
            break;
        }
    }
}

// A loaded file by name (state 3): one of ours first (option B), then one in use (group under 10000), then a let-go
// one; its place, 0 if none.
std::uintptr_t LoadedFile(const char* name) {
    std::uintptr_t inUse = 0, cached = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        if (Read<std::uint8_t>(r + 1) != 3 || _stricmp(reinterpret_cast<const char*>(r + 4), name) != 0) continue;
        int group = Read<int>(r + 0x48);
        if (IsOurGroup(group)) return Read<std::uintptr_t>(r + 0x58);
        if (group < 10000 && !inUse) inUse = Read<std::uintptr_t>(r + 0x58);
        if (group >= 10000 && !cached) cached = Read<std::uintptr_t>(r + 0x58);
    }
    return inUse ? inUse : cached;
}

// The copy's Keyblade (weapon 0) takes the form's weapon motions, as a form body's own Keyblade has (bench 22:44:
// Valor W_EX010_BTLF_R.mset, Wisdom W_EX010_MAGF.mset, Limit W_EX010_KH1F.mset; loaded with the form's files): they
// make it spin (Wisdom, Master) or float (Final). The file is named after the body's motions (obj/P_EX100<form>.mset
// -> obj/W_EX010<form>_R.mset, else obj/W_EX010<form>.mset); back on its own (W_EX010.mset) for the copy's own look.
// Then its motion controller starts again (exe+0x3C86A0, as the Drive does for the body) so it doesn't keep playing a
// motion object in the old file. Valor / Master / Final's second Keyblade isn't built (a form body's weapon 1).
void SetWeaponMotions(std::uintptr_t copy, std::uint32_t obj) {
    std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapons[0]);
    if (!w) return;
    std::uintptr_t want = 0;
    char name[64] = "own";
    if (obj == g_ownObject) {
        want = g_weaponOwnMotions;
    } else {
        const char* body = FileAt(Read<std::uintptr_t>(copy + kSetMotions));
        constexpr char kPrefix[] = "obj/P_EX100";
        const char* dot = std::strrchr(body, '.');
        if (_strnicmp(body, kPrefix, sizeof(kPrefix) - 1) != 0 || !dot) {
            Log("form look: the copy's Keyblade: body motions %.40s, no form weapon motions to look for", body);
            return;
        }
        int formLen = static_cast<int>(dot - body) - static_cast<int>(sizeof(kPrefix) - 1);
        std::snprintf(name, sizeof(name), "obj/W_EX010%.*s_R.mset", formLen, body + sizeof(kPrefix) - 1);
        want = LoadedFile(name);
        if (!want) {
            std::snprintf(name, sizeof(name), "obj/W_EX010%.*s.mset", formLen, body + sizeof(kPrefix) - 1);
            want = LoadedFile(name);
        }
    }
    std::uintptr_t now = Read<std::uintptr_t>(w + kMotionCtx);
    if (!want) {
        Log("form look: the copy's Keyblade: %s isn't loaded, keeps %.40s", name, FileAt(now));
        return;
    }
    if (want != now) {
        *reinterpret_cast<std::uintptr_t*>(w + kMotionCtx) = want;
        Fn<void(__fastcall*)(std::uintptr_t, int, float, float)>(kSetMotion)(w + kMotionCtx, 0, 0.0f, 0.0f);
    }
    Log("form look: the copy's Keyblade: motions %.40s (were %.40s)", FileAt(want), FileAt(now));
}

// Ends the second Keyblade we built: its set's files are let go first (by their place, while it's right; the actor
// is freed later, when the file could have moved or gone), then the weapon class's end (handler +0x38,
// exe+0x3EF4D0: its trail, exe+0x3B4700).
void EndSecondBlade(std::uintptr_t copy) {
    if (!g_secondBlade) return;
    std::uintptr_t w = g_secondBlade;
    g_secondBlade = 0;
    if (Read<std::uintptr_t>(copy + kWeapons[1]) != w) {
        Log("form look: the copy's second Keyblade %p isn't at copy+0xD68 any more: left alone",
            reinterpret_cast<void*>(w));
        return;
    }
    // Off the copy's controller first: the weapon's end doesn't unlink it (exe+0x3C8E90 linked it: owner ctl +0xD8
    // first child, +0xE0 last, child +0xE8 handle of the next), and the copy's next motion change walked into the
    // ended Keyblade's controller (bench 23:13:52, exe+0x3A6425).
    auto next = [](std::uintptr_t c) {
        return Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(c + 0xE8));
    };
    std::uintptr_t owner = copy + kMotionCtx, child = w + kMotionCtx, prev = 0;
    bool unlinked = false;
    for (std::uintptr_t c = Read<std::uintptr_t>(owner + 0xD8); c; prev = c, c = next(c)) {
        if (c != child) continue;
        if (prev)
            *reinterpret_cast<std::uint32_t*>(prev + 0xE8) = Read<std::uint32_t>(child + 0xE8);
        else
            *reinterpret_cast<std::uintptr_t*>(owner + 0xD8) = next(child);
        if (Read<std::uintptr_t>(owner + 0xE0) == child) *reinterpret_cast<std::uintptr_t*>(owner + 0xE0) = prev;
        *reinterpret_cast<std::uint32_t*>(child + 0xE8) = Fn<std::uint32_t(__fastcall*)(std::uintptr_t)>(0x4AD240)(0);
        unlinked = true;
        break;
    }
    if (!unlinked) Log("form look: the second Keyblade's controller wasn't linked to the copy's");
    int effects = StopEffectsOf(w);
    for (std::uintptr_t off = 0x920; off <= 0x930; off += 8) {
        std::uintptr_t place = Read<std::uintptr_t>(w + off);
        if (place) Fn<void(__fastcall*)(std::uintptr_t)>(kReleaseFile)(place);
        *reinterpret_cast<std::uintptr_t*>(w + off) = 0;
    }
    std::uintptr_t handler = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(w));
    auto end = reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
        Read<std::uintptr_t>(Read<std::uintptr_t>(handler) + 0x38));
    end(handler, w);
    int slot = 0;  // a free one, else the oldest
    for (int k = 0; k < kEndedMax; ++k) {
        if (!g_ended[k]) {
            slot = k;
            break;
        }
        if (g_endedAt[k] < g_endedAt[slot]) slot = k;
    }
    if (g_ended[slot])
        Log("form look: %d ended Keyblades already wait for their delete: %p no longer waited for", kEndedMax,
            reinterpret_cast<void*>(g_ended[slot]));
    g_ended[slot] = w;
    g_endedAt[slot] = GetTickCount64();
    // Its teardown (exe+0x3EF520, when the actor is deleted) unlinks it again while its controller has a motion file
    // and walked off the list we took it from (bench 23:20:57, exe+0x3C8F7E): no file, no second unlink.
    *reinterpret_cast<std::uintptr_t*>(w + kMotionCtx) = 0;
    *reinterpret_cast<std::uintptr_t*>(copy + kWeapons[1]) = 0;
    Log("form look: the copy's second Keyblade %p ended (%d of its effects stopped)", reinterpret_cast<void*>(w),
        effects);
}

// Valor, Master and Final have a second Keyblade (a form body's weapon 1, exe+0x3A7A40). The copy gets one built
// the way exe+0x3D7C70 builds it, with the form's weapon kind (the copy's own entry at +8 stays Sora's) and the
// group its model is listed in, found by name (the weapon's set takes its files from exactly that group): its
// model, and its motions by name from the weapon table, its controller linked to the copy's.
// The Keyblade is the one the other player equipped on that form (your pick 2026-10-25; the form record the entry's
// byte +0x57 names, sent by copy_blade.cpp), its files asked for by copy_blade.cpp when they aren't loaded (built
// from the spawn step once they are); our own form record's when that player's isn't known or its files don't come.
void BuildSecondBlade(std::uintptr_t copy, std::uintptr_t entry) {
    g_secondPending = 0;
    if (Read<std::uintptr_t>(copy + kWeapons[1])) {
        Log("form look: the copy already has a second Keyblade (not ours): none built");
        return;
    }
    int kind = Read<std::uint16_t>(entry + 0x4E);
    std::uint16_t peerItem = CopyBladePeerFormItem(Read<std::uint8_t>(entry + 0x57));
    std::uint32_t weaponObj = peerItem ? CopyBladeWeaponOf(kind, peerItem) : 0;
    bool theirs = false;
    int group = -1;
    if (weaponObj) {
        int files = CopyBladeFiles(kind, 1, weaponObj, &group);
        if (files == 0) {
            g_secondPending = entry;
            return;
        }
        if (files < 0) {
            Log("form look: the other player's form Keyblade (item %#x, object %#x): no files, ours instead", peerItem,
                weaponObj);
            weaponObj = 0;
            group = -1;
        } else {
            theirs = true;
        }
    } else if (peerItem) {
        Log("form look: the other player's form Keyblade item %#x has no object for kind %#x: ours instead", peerItem,
            kind);
    }
    if (!weaponObj) {
        weaponObj = static_cast<std::uint32_t>(
            Fn<std::uint64_t(__fastcall*)(std::uintptr_t, int)>(kWeaponObject)(entry, 1));
        if (!weaponObj) return;
    }
    std::uintptr_t weaponEntry = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kObjectEntry)(weaponObj);
    if (!weaponEntry) {
        Log("form look: second Keyblade (object %#x): no entry: not built", weaponObj);
        return;
    }
    if (!theirs) {
        // The group is the one the weapon's model is listed in, found by name (as CopyBladeFiles does), not the
        // body's group plus a rule: a request adds +1, +2 for a group from 0 up and nothing below 0 (exe+0x3E0660,
        // F351), and the set fill takes the file in exactly the group it is given with no check for none (L149).
        // Only one of ours (option B: the look's request brought it); the game's could be let go under it (F358).
        char name[64];
        std::snprintf(name, sizeof(name), "obj/%.32s.mdlx", reinterpret_cast<const char*>(weaponEntry + 8));
        group = -1;
        for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
            int g = Read<int>(r + 0x48);
            if (Read<std::uint8_t>(r + 1) != 3 || g >= 10000 || g == -1 ||
                _stricmp(reinterpret_cast<const char*>(r + 4), name))
                continue;
            if (IsOurGroup(g)) {
                group = g;
                break;
            }
        }
    }
    if (group == -1 || group >= 10000) {  // the none-check exe+0x39CC40 lacks (L149)
        Log("form look: second Keyblade (object %#x, obj/%.32s.mdlx): no loaded entry of ours (group %d): not built",
            weaponObj, reinterpret_cast<const char*>(weaponEntry + 8), group);
        return;
    }
    std::uintptr_t mem = Fn<std::uintptr_t(__fastcall*)(std::uint64_t)>(kAlloc)(0xC10);
    if (!mem) return;
    g_fillOurs = true;  // its motions are found by name in 'any group': ours
    std::uintptr_t w = Fn<std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, int, int, int)>(
        kNewWeapon)(mem, weaponEntry, copy, kind, 1, group);
    g_fillOurs = false;
    if (!w) return;
    *reinterpret_cast<std::uintptr_t*>(copy + kWeapons[1]) = w;
    g_secondBlade = w;
    Fn<void(__fastcall*)(std::uintptr_t, std::uint32_t)>(kMotionSync)(w + kMotionCtx,
                                                                      Read<std::uint32_t>(copy + 0x188));
    Fn<void(__fastcall*)(std::uintptr_t, int, float, float)>(kSetMotion)(w + kMotionCtx, Read<int>(copy + 0x180), 0.0f,
                                                                         Read<float>(copy + 0x19C));
    Log("form look: the copy's second Keyblade %p built: object %#x (%s), group %d, files %.40s | %.40s, motions "
        "%.40s", reinterpret_cast<void*>(w), weaponObj, theirs ? "the other player's" : "ours", group, FileAt(Read<std::uintptr_t>(w + 0x920)),
        FileAt(Read<std::uintptr_t>(w + 0x928)), FileAt(Read<std::uintptr_t>(w + kMotionCtx)));
}

// At a look change (the real game's body swap): the copy's effects that would end with the old body stop
// (KH2COOP_FORM_LOOK_STOP_GLOW=0: only listed).
bool StopGlow() {
    static int stop = -1;
    if (stop < 0) stop = EnvInt("KH2COOP_FORM_LOOK_STOP_GLOW", 1) != 0;
    return stop != 0;
}

void EndBodyEffects(std::uintptr_t copy) {
    __try {
        CopyBodyEffects(copy, StopGlow());
    } __except (LogException(GetExceptionInformation(), "with the copy's effects")) {
    }
}

// True when done (changed, nothing to do or given up), false to try again next frame (files on their way).
// `again`: the same look put on again (its files moved).
bool Apply(std::uint32_t obj, bool again = false) {
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return true;
    KnowCopy(copy);
    if (!again && Read<std::uint32_t>(copy + kSetObjId) == obj) return true;
    std::uintptr_t entry = obj ? Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kObjectEntry)(obj) : 0;
    if (!entry) {
        Log("form look: object %#x has no object entry", obj);
        return true;
    }
    const char* files = EnsureFiles(obj, entry);
    if (files[0] == 'a') return false;
    if (files[0] == 'f') return true;
    if (files[0] == 'd') {  // declined: plain Sora for this form, with its Keyblade
        // The Drive's shine ends as at the game's body swap, now and 1.5 s later; with no look change it stayed on
        // the copy for the whole form (your note, bench form_share 2026-10-27).
        g_glowDeclined = true;
        if (Read<std::uint32_t>(copy + kSetObjId) != g_ownObject) return Apply(g_ownObject);
        EndBodyEffects(copy);
        g_glowCheckAt = GetTickCount64() + 1500;
        return true;
    }
    EndSecondBlade(copy);  // a form body's second Keyblade goes with the body; rebuilt below if the look has one
    g_secondPending = 0;
    std::uintptr_t set = copy + kSet;
    std::uintptr_t oldInstance = Read<std::uintptr_t>(copy + kInstance);
    g_fillOurs = obj != g_ownObject;  // a form's files: ours, not the game's entries listed first
    Fn<void(__fastcall*)(std::uintptr_t, std::uint32_t, std::uintptr_t)>(kSetFill)(set, obj, entry);
    g_fillOurs = false;
    std::uintptr_t model = Fn<std::uintptr_t(__fastcall*)(std::uintptr_t)>(kSetModel)(set);
    if (!model) {
        Log("form look: object %#x: no model in its files", obj);
        return true;
    }
    using PFN_MakeInstance = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t);
    auto make = reinterpret_cast<PFN_MakeInstance>(Read<std::uintptr_t>(Read<std::uintptr_t>(model) + 0x40));
    std::uintptr_t instance = make(model, Read<std::uintptr_t>(ExeBase() + kModelAlloc));
    if (!instance) {
        Log("form look: object %#x: the model made no instance", obj);
        return true;
    }
    *reinterpret_cast<std::uintptr_t*>(copy + kModel) = model;
    *reinterpret_cast<std::uintptr_t*>(copy + kInstance) = instance;
    std::uintptr_t texture = Fn<std::uintptr_t(__fastcall*)(std::uintptr_t)>(kSetTexture)(set);
    if (texture) {
        Fn<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(kBindTexture)(instance, texture);
        *reinterpret_cast<std::uintptr_t*>(copy + kTexture) = texture;
        *reinterpret_cast<std::uint32_t*>(copy + kFlags9B8) |= 2;
    }
    Fn<void(__fastcall*)(std::uintptr_t)>(kInstanceDone)(instance);
    Fn<void(__fastcall*)(std::uintptr_t)>(kShadowStep)(copy);
    std::uintptr_t motions = Read<std::uintptr_t>(copy + kSetMotions);
    if (motions) *reinterpret_cast<std::uintptr_t*>(copy + kMotionCtx) = motions;
    SetWeaponMotions(copy, obj);  // first: the body's motion change also restarts the Keyblade (linked controller)
    if (obj != g_ownObject) BuildSecondBlade(copy, entry);
    PuppetSetMotion(copy, 0);
    PuppetMotionAgain();
    if (!again && g_wearing != g_ownObject && g_wearing != obj) {  // one look at a time
        g_freeLook = g_wearing;
        g_freeLookSince = GetTickCount64();
    }
    g_wearing = obj;
    RecordWorn(copy);
    if (!again) {
        EndBodyEffects(copy);
        g_glowCheckAt = GetTickCount64() + 1500;
    }
    Log("form look: the copy (exe+0x%llX) %s object %#x (form %d, %.32s): instance %p (was %p), texture %p, "
        "motions %p",
        static_cast<unsigned long long>(copy - ExeBase()), again ? "looks again (files moved) like" : "now looks like",
        obj, FormOf(obj), NameOf(entry),
        reinterpret_cast<void*>(instance), reinterpret_cast<void*>(oldInstance), reinterpret_cast<void*>(texture),
        reinterpret_cast<void*>(motions));
    return true;
}


// ---- The review's 5.2.6, option B (your pick; DECISIONS 'the copy's files'): the copy's files (its Keyblade, a form's
// second Keyblade, a form's look) in negative groups of ours. Negative groups are placed from the heap's end in list
// order and a new file goes after the files of its group (L147), so each request takes the next kNegSpan groups up
// from -100 (a negative group's weapons stay in it, F351; the other two are kept free) and nothing of ours moves when
// one is added (bench neg_group). -1 is avoided: it means 'any group' to the lookups. Ours are only added, stay for the
// whole room and are let go at the room's own drop (L148: a negative group becomes 30000). The cutscene's work block
// no longer moves the heap's end (event_block.cpp). Test command 24 'negfiles <form>' asks a look the same way.
constexpr int kNegFirst = -100, kNegMax = 96, kNegLetGo = 30000;
constexpr std::uintptr_t kFindOrAdd = 0x39E000;     // (name, group, size): a file's entry, listed or new (L138)
constexpr std::uint8_t kFindOrAddBytes[] = {0x40, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00};
constexpr std::uintptr_t kNowRoom = 0x717008;       // u8 world, u8 room
constexpr std::uintptr_t kRoomReadyByte = 0x7177B8; // u8, non-zero once the room is ready
constexpr std::uintptr_t kHeapEnd = 0x749740;       // the file heap's end: negative groups are placed from it down
constexpr std::uintptr_t kUnlinkFile = 0x39E2A0;    // (entry): out of the file list (head +0x29F33C8, tail +0x29F33D0)
constexpr std::uintptr_t kFileAllocator = 0x29F33E8; // the entries' allocator object: its vtable +0x10 (obj, entry) frees one
using PFN_FindOrAdd = std::uintptr_t(__fastcall*)(const char*, int, std::uint64_t);
PFN_FindOrAdd g_realFindOrAdd = nullptr;
int g_negForm = -1;            // test command: the form asked for, -1 none
int g_negNext = kNegFirst;     // the next request's first group
int g_negAsking = 0;           // while our request runs: its first group (the find-or-add hook logs each file)
int g_negJoins = 0;
std::uintptr_t g_negEntry[kNegMax] = {}, g_negPlace[kNegMax] = {};
int g_negCount = 0;
std::uintptr_t g_negEnd = 0, g_negBottom = 0;
bool g_negRebase = false;      // our request added files: the next layout gives the bottom we keep
bool g_negNoJoin = true;       // KH2COOP_NEG_NOJOIN: our entries and the game's never shared (HookFindOrAdd)
bool g_negWaitLogged = false;  // a request waits for the room to settle (logged once)
// Our requests this room, in order (their groups go down the heap): the first group, where its entries start in
// g_negEntry, and whether it stays for the whole room (the copy's Keyblade) or goes with its look (a look, a form's
// second Keyblade asked while it was worn).
struct NegReq {
    int group, firstEntry;
    bool keep;
};
constexpr int kReqMax = 32;
NegReq g_reqs[kReqMax] = {};
int g_reqCount = 0;
bool g_negFullLogged = false;  // our list is full (logged once a room)
// Per room (census): the layouts seen, the smallest gap at a layout between the placed files and the negative files'
// bottom, the largest Drive area (groups 5000-5099).
unsigned g_layouts = 0;
long long g_gapMin = -1;
unsigned long long g_driveMax = 0;

// The top of the placed files of groups 0-9999 (0 none; without the Drive area 5000-5099 if noDrive); also the
// negative files' bottom and the Drive area's bytes.
std::uintptr_t PlacedTop(std::uintptr_t* bottom, unsigned long long* drive, bool noDrive = false) {
    std::uintptr_t top = 0;
    unsigned long long d = 0;
    int n = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && n < 4096; r = NextRes(r), ++n) {
        int group = Read<int>(r + 0x48);
        std::uint64_t size = Read<std::uint64_t>(r + 0x50);
        if (group >= 5000 && group < 5100) d += size;
        if (group < 0 || group >= 10000 || (noDrive && group >= 5000 && group < 5100)) continue;
        std::uintptr_t place = Read<std::uintptr_t>(r + 0x58);
        if (place && place + size > top) top = place + size;
    }
    if (bottom) *bottom = Read<std::uintptr_t>(ExeBase() + kHeapNegBottom);
    if (drive) *drive = d;
    return top;
}

// The gate B uses too: why our files may not be asked for yet (the room's files not all in and placed), or null.
const char* RoomNotSettled() {
    if (!Read<std::uint8_t>(ExeBase() + kRoomReadyByte)) return "the room isn't ready";
    if (!LoaderIdle()) return "a read is in flight";
    int n = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && n < 4096; r = NextRes(r)) {
        if (Read<int>(r + 0x48) >= 10000) continue;
        ++n;
        if (Read<std::uint8_t>(r + 1) != 3) return "a file isn't loaded";
        std::uintptr_t place = Read<std::uintptr_t>(r + 0x58), old = Read<std::uintptr_t>(r + 0x60);
        if (!place || (old && old != place)) return "a file isn't in its place";
    }
    return n ? nullptr : "no files listed";
}

// The first listed entry of that name (exactly in `group` if exact), else 0.
std::uintptr_t ListedEntry(const char* name, int group, bool exact) {
    int n = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && n < 4096; r = NextRes(r), ++n) {
        if (std::strncmp(reinterpret_cast<const char*>(r + 4), name, 0x40)) continue;
        if (!exact || Read<int>(r + 0x48) == group) return r;
    }
    return 0;
}

bool IsOurGroup(int group) {
    return group >= kNegFirst && group < g_negNext;
}

// The no-join rule (your design, 2026-10-26): the set fill looks a file up in exactly its own group (L149), so a name
// shared between our groups and the game's needs an entry in each. A new entry of that name in `group` through the
// game's own second-entry path: asked in 5002, exe+0x39E000 makes one even when the name is listed (L138); an entry
// already in 5002 of that name would be returned as it is, so it is hidden for the call. The new entry then takes
// `group` (+0x48, and +0x68 as the new-entry path sets it); the next layout puts it in its place in the list.
std::uintptr_t NewEntry(const char* name, int group, std::uint64_t size) {
    std::uintptr_t clash = ListedEntry(name, 5002, true);
    if (clash) *reinterpret_cast<int*>(clash + 0x48) = 5003;
    std::uintptr_t r = g_realFindOrAdd(name, 5002, size);
    if (clash) *reinterpret_cast<int*>(clash + 0x48) = 5002;
    if (r) {
        *reinterpret_cast<int*>(r + 0x48) = group;
        *reinterpret_cast<int*>(r + 0x68) = group;
    }
    return r;
}

std::uintptr_t __fastcall HookFindOrAdd(const char* name, int group, std::uint64_t size) {
    if (!g_negAsking && (!g_negCount || !g_negNoJoin)) return g_realFindOrAdd(name, group, size);
    std::uintptr_t exact = 0, first = 0;
    __try {
        exact = ListedEntry(name, group, true);
        first = exact ? 0 : ListedEntry(name, group, false);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return g_realFindOrAdd(name, group, size);
    }
    int firstGroup = first ? Read<int>(first + 0x48) : 0x7FFFFFFF;
    if (!g_negAsking) {  // the game's request: never one of our entries (its set looks in its own group)
        if (!first || !IsOurGroup(firstGroup)) {
            std::uintptr_t r = g_realFindOrAdd(name, group, size);
            bool shared = false;
            __try {
                for (int k = 0; k < g_negCount && !shared; ++k)
                    shared = !std::strncmp(reinterpret_cast<const char*>(g_negEntry[k] + 4), name, 0x40);
                if (shared)
                    Log("neg files: the game's %.40s asked in group %d: entry %p, group %d (passed through; the "
                        "first listed one in group %d)", name, group, reinterpret_cast<void*>(r),
                        r ? Read<int>(r + 0x48) : 0, first ? firstGroup : 0);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            return r;
        }
        std::uintptr_t r = NewEntry(name, group, size);
        Log("neg files: the game's %.40s asked in group %d: a new entry %p (no-join: the listed one is ours, group %d)",
            name, group, reinterpret_cast<void*>(r), firstGroup);
        return r;
    }
    // Our request: always an entry in the group asked for (a listed one there is ours already).
    bool fresh = g_negNoJoin && first && firstGroup < 10000;
    std::uintptr_t r = fresh ? NewEntry(name, group, size) : g_realFindOrAdd(name, group, size);
    __try {
        int now = r ? Read<int>(r + 0x48) : 0;
        const char* how = exact ? "already in that group" : !first ? "new" : firstGroup >= 10000 ? "taken back from let go"
                          : fresh ? "new beside the listed one (no-join)" : now == group ? "new beside another group's"
                          : "JOINED the listed entry";
        if (r && now != group) ++g_negJoins;
        char was[48] = "";
        if (first) std::snprintf(was, sizeof(was), ", the listed one in group %d", firstGroup);
        Log("neg files:   %.40s asked in group %d: entry %p, group %d (%s%s)", name, group,
            reinterpret_cast<void*>(r), now, how, was);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

// exe+0x39CC40 (name, group): a file's place by name in that group, -1 any (the first listed, exe+0x39E1E0). While our
// set fill or weapon build for the copy runs (g_fillOurs), 'any group' is our entry of that name when we have one:
// the list runs from group 0 up before the negative groups, so the game's own entry would come first. A miss (no
// null check, L149) is logged with every listed entry of that name before the game's own read of address 4.
constexpr std::uintptr_t kGroupFind = 0x39CC40;
constexpr std::uint8_t kGroupFindBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0x95, 0x15, 0x00, 0x00};
using PFN_GroupFind = std::uintptr_t(__fastcall*)(const char*, int);
PFN_GroupFind g_realGroupFind = nullptr;

//
// Outside our fill the same rule the other way (the no-join rule for name lookups): the list isn't kept in group order
// between layouts (a new entry is listed last), so the game's own weapon build (exe+0x3EF750 -> exe+0x39CC40(name, -1))
// took its Keyblade's motions from our W_EX010_KH1F.mset in -97, and letting go of that look crashed the host
// (exe+0x3C859E, bench form_share 2026-10-27, one look at a time): when ours is the first of that name, the game's own
// live entry (group 0-9999) is given when there is one (logged).
std::uintptr_t __fastcall HookGroupFind(const char* name, int group) {
    if (group == -1 && (g_fillOurs || g_negCount)) {
        __try {
            int n = 0, ours = 0;
            for (std::uintptr_t e = Read<std::uintptr_t>(ExeBase() + kResHead); e && n < 4096; e = NextRes(e), ++n) {
                if (std::strncmp(reinterpret_cast<const char*>(e + 4), name, 0x40)) continue;
                int g = Read<int>(e + 0x48);
                if (g_fillOurs) {
                    if (!IsOurGroup(g)) continue;
                    group = g;
                    break;
                }
                if (IsOurGroup(g)) {
                    if (!ours) ours = g;
                    continue;
                }
                if (g < 0 || g >= 10000) continue;
                if (!ours) break;  // the game's own comes first anyway
                group = g;
                Log("neg files: %.40s looked up in any group from exe+%#llx: the game's own in group %d, not ours (%d, "
                    "listed first)", name,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()), g,
                    ours);
                break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    __try {
        std::uintptr_t r = ListedEntry(name, group, group != -1);
        if (!r || Read<int>(r + 0x48) >= 10000) {
            Log("neg files: MISS: %.40s looked up in group %d from exe+%#llx; listed entries of that name:", name, group,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
            int n = 0;
            for (std::uintptr_t e = Read<std::uintptr_t>(ExeBase() + kResHead); e && n < 4096; e = NextRes(e), ++n) {
                if (std::strncmp(reinterpret_cast<const char*>(e + 4), name, 0x40)) continue;
                Log("neg files:   %p group %d (old %d), state %u, flags %#x, place %p", reinterpret_cast<void*>(e),
                    Read<int>(e + 0x48), Read<int>(e + 0x68), Read<std::uint8_t>(e + 1), Read<std::uint32_t>(e + 0x44),
                    reinterpret_cast<void*>(Read<std::uintptr_t>(e + 0x58)));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_realGroupFind(name, group);
}

// The no-join rule for an actor's group (L152, F350): exe+0x39DB20 gives the group of the first listed entry of a
// name, and ours are listed before the game's own new ones, so the game's Drive actor took our group and looked its
// Keyblade up in ours + 2. While ours exist, the first entry not ours is the answer when there is one.
constexpr std::uintptr_t kNameGroup = 0x39DB20;
constexpr std::uint8_t kNameGroupBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xFF};
using PFN_NameGroup = int(__fastcall*)(const char*);
PFN_NameGroup g_realNameGroup = nullptr;

int __fastcall HookNameGroup(const char* name) {
    if (!g_negCount || !g_negNoJoin) return g_realNameGroup(name);
    __try {
        int ours = 0x7FFFFFFF, n = 0;
        for (std::uintptr_t e = Read<std::uintptr_t>(ExeBase() + kResHead); e && n < 4096; e = NextRes(e), ++n) {
            if (std::strncmp(reinterpret_cast<const char*>(e + 4), name, 0x40)) continue;
            int group = Read<int>(e + 0x48);
            if (!IsOurGroup(group)) {
                if (ours != 0x7FFFFFFF)
                    Log("neg files: %.40s's group: %d, not ours (%d, listed first) (no-join, from exe+%#llx)", name,
                        group, ours,
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) -
                                                        ExeBase()));
                return group;
            }
            if (ours == 0x7FFFFFFF) ours = group;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_realNameGroup(name);
}

// Lets go of a file of ours. One never loaded (state 0) is taken out of the list and freed at once, as the game frees
// a let-go entry (exe+0x39CCA0, exe+0x39D590: unlink, then the allocator's +0x10): let go instead, the place step gives
// it +0x58 = +0x60 = 0 and keeps it listed, and the Drive's drop then frees it at once as a 30000 entry through the
// cleanup at its place (friend crash, bench form_share 2026-10-27 14:43:56, exe+0x3A6A18 reading address 3 after a
// declined look, F360). A loaded one becomes 30000 (L148), freed by a layout or the Drive's drop. One being read
// (state 1) also becomes 30000 (logged: nothing safer while the read runs). 0 freed, 1 let go, 2 let go mid-read.
int LetGoOurs(std::uintptr_t r) {
    std::uint8_t state = Read<std::uint8_t>(r + 1);
    if (state == 0) {
        Fn<void(__fastcall*)(std::uintptr_t)>(kUnlinkFile)(r);
        std::uintptr_t alloc = Read<std::uintptr_t>(ExeBase() + kFileAllocator);
        reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
            Read<std::uintptr_t>(Read<std::uintptr_t>(alloc) + 0x10))(alloc, r);
        return 0;
    }
    *reinterpret_cast<int*>(r + 0x48) = kNegLetGo;
    return state == 1 ? 2 : 1;
}

// Asks files into our next negative groups, on the game thread: `ask(group)` makes the request while the find-or-add
// hook gives it entries of ours only (never the game's, the no-join rule). Only once the room's files are all in and
// placed (RoomNotSettled): before that 0, try again. The budget (your rules): the free room between the placed files
// (without the Drive area) and our bottom, minus our own Drive's area at its largest form (KH2COOP_NEG_DRIVE_KB) and a
// margin (KH2COOP_NEG_MARGIN_KB); KH2COOP_NEG_BUDGET_KB sets it. A budgeted request (a look) over it is declined: its
// new files let go at once, *declined, 0, with a loud line; a Keyblade is asked anyway (logged). Else the group, the
// reads started by a layout of ours (it moves nothing of ours: the new files go below the old ones).
int NegAsk(const char* what, bool budgeted, bool keep, void (*ask)(int, const void*), const void* arg, bool* declined) {
    *declined = false;
    std::uint16_t room = Read<std::uint16_t>(ExeBase() + kNowRoom);
    if (const char* why = RoomNotSettled()) {
        if (!g_negWaitLogged) Log("copy files: %s waits (room %02x/%02x: %s)", what, room & 0xFF, room >> 8, why);
        g_negWaitLogged = true;
        return 0;
    }
    g_negWaitLogged = false;
    if (g_negCount + 16 > kNegMax || g_negNext + kNegSpan - 1 >= -1 || g_reqCount >= kReqMax) {
        if (!g_negFullLogged)
            Log("copy files: %s refused in room %02x/%02x: our list is full (%d files, next group %d)", what,
                room & 0xFF, room >> 8, g_negCount, g_negNext);
        g_negFullLogged = true;
        *declined = true;
        return 0;
    }
    // The free room without our own Drive's files (in a form they are in): the reservation stands for them.
    std::uintptr_t bottom = 0;
    std::uintptr_t top = PlacedTop(&bottom, nullptr, true);
    long long freeKb = top ? (static_cast<long long>(bottom) - static_cast<long long>(top)) / 1024 : 0;
    int drive = EnvInt("KH2COOP_NEG_DRIVE_KB", 3072), margin = EnvInt("KH2COOP_NEG_MARGIN_KB", 1024);
    int forced = EnvInt("KH2COOP_NEG_BUDGET_KB", -1);
    long long budget = forced >= 0 ? forced : freeKb - drive - margin;
    int first = g_negNext;
    g_negJoins = 0;
    g_negAsking = first;
    Log("copy files: %s asked in group %d, room %02x/%02x:", what, first, room & 0xFF, room >> 8);
    ask(first, arg);
    g_negAsking = 0;
    std::uintptr_t mine[16];
    int n = 0;
    unsigned long long bytes = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && n < 16; r = NextRes(r)) {
        int group = Read<int>(r + 0x48);
        if (group < first || group >= first + kNegSpan) continue;
        mine[n++] = r;
        bytes += Read<std::uint64_t>(r + 0x50);
    }
    if (!n) {
        Log("copy files: %s: no file of ours listed after the request (%d joined another group)", what, g_negJoins);
        *declined = true;
        return 0;
    }
    if (static_cast<long long>(bytes / 1024) > budget) {
        if (budgeted) {
            int freed = 0;
            for (int k = 0; k < n; ++k) freed += LetGoOurs(mine[k]) == 0;
            Log("copy files: DECLINED: %s in room %02x/%02x: %llu KB, over the budget %lld KB (free %lld KB, our Drive "
                "%d KB, margin %d KB%s): the copy shows plain Sora; its %d files freed (%d never loaded), %d joined",
                what, room & 0xFF, room >> 8, bytes / 1024, budget, freeKb, drive, margin,
                forced >= 0 ? ", the budget set by KH2COOP_NEG_BUDGET_KB" : "", n, freed, g_negJoins);
            *declined = true;
            return 0;
        }
        Log("copy files: %s: %llu KB, over the budget %lld KB (free %lld KB): asked anyway (the copy needs it)", what,
            bytes / 1024, budget, freeKb);
    }
    g_reqs[g_reqCount++] = {first, g_negCount, keep};
    for (int k = 0; k < n; ++k) {
        g_negEntry[g_negCount] = mine[k];
        g_negPlace[g_negCount++] = 0;
    }
    g_negNext = first + kNegSpan;
    g_negRebase = true;
    StartLoads();
    Log("copy files: %s: %d files of ours in group %d, %llu KB, %d joined another group; budget %lld KB (free %lld KB, "
        "our Drive %d KB, margin %d KB); the reads started", what, n, first, bytes / 1024, g_negJoins, budget, freeKb,
        drive, margin);
    return first;
}

// Test command 24 'negfiles <form>': a form's look asked the same way, apart from the copy (bench neg_group).
void NegRequest(int form) {
    std::uint32_t obj = FormObject(form);
    if (!obj) {
        Log("neg files: form %d refused: no such form", form);
        return;
    }
    char what[32];
    std::snprintf(what, sizeof(what), "form %d (object %#x)", form, obj);
    bool declined = false;
    int group = NegAsk(what, true, true, [](int g, const void* o) {
        Fn<void(__fastcall*)(std::uint64_t, int, int, int)>(kAskFiles)(*static_cast<const std::uint32_t*>(o), g, 4, 7);
    }, &obj, &declined);
    if (!group && !declined) Log("neg files: form %d refused: the room isn't settled", form);
}

// An actor other than the copy, or a weapon of any actor (the copy's too), reads this file: its set (+0x920, +0x928,
// +0x930) or its motion controller (+0x158..+0x1B8) points into it (*who = that actor or weapon). Back from the Drive
// area days (friend crash 2026-10-26 19:55:18: the friend's own Limit Form Keyblade ran its motions from the
// W_EX010_KH1F.mset of the copy's look, and the look's files went under it).
bool UsedByOthers(std::uintptr_t r, std::uintptr_t* who) {
    std::uintptr_t from = Read<std::uintptr_t>(r + 0x60), to = from + Read<std::uint64_t>(r + 0x50);
    if (!from) return false;
    auto reads = [from, to](std::uintptr_t x) {
        constexpr std::uintptr_t kFiles[] = {0x920, 0x928, 0x930};
        for (std::uintptr_t f : kFiles) {
            std::uintptr_t p = Read<std::uintptr_t>(x + f);
            if (p >= from && p < to) return true;
        }
        for (std::uintptr_t off = 0; off < 0x60; off += 8) {
            std::uintptr_t p = Read<std::uintptr_t>(x + kMotionCtx + off);
            if (p >= from && p < to) return true;
        }
        return false;
    };
    std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
    for (int n = 0; a && n < 512; ++n, a = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(
                                           Read<std::uint32_t>(a + kNextActor))) {
        if (a != g_copyWas && reads(a)) {
            *who = a;
            return true;
        }
        for (std::uintptr_t slot : kWeapons) {
            // A slot counts only when it points at a weapon owned by this actor (F336: junk on short actors).
            std::uintptr_t w = Read<std::uintptr_t>(a + slot);
            if (w && Read<std::uintptr_t>(w + 0xBF8) == a && reads(w)) {
                *who = w;
                return true;
            }
        }
    }
    return false;
}

// One look at a time (your pick, session 46, after bench form_share declined 3 of 5 forms in room 04/06): a look the
// copy no longer wears is let go, with the requests after it (a form's second Keyblade), so the next look reuses its
// room. Only when they are the last of ours (the lowest at the heap's end): nothing of ours below them moves. A
// Keyblade of the copy's asked after the look stays, so the look stays too (logged; the budget then applies). Waits
// for an ended second Keyblade's delete (its teardown reads its files, L137) and for the loader; the effects still
// reading the files are stopped first. A layout of ours then frees them (L148, LetGoOurs). A file of theirs another
// actor or weapon reads (UsedByOthers) holds the free back; after 1 s the look stays for the room (logged), so the
// next look isn't held up.
void TryFreeLook() {
    std::uint32_t obj = g_freeLook;
    const Look* l = LookOf(obj);
    if (obj == g_wearing || !l || !l->group) {
        g_freeLook = 0;
        return;
    }
    if (EndedPending() || g_secondPending || !LoaderIdle()) return;
    int i = 0;
    while (i < g_reqCount && g_reqs[i].group != l->group) ++i;
    if (i == g_reqCount) {
        g_freeLook = 0;
        return;
    }
    for (int k = i; k < g_reqCount; ++k) {
        if (!g_reqs[k].keep) continue;
        Log("copy files: object %#x's look (group %d) stays: a file of ours that stays (group %d) came after it", obj,
            l->group, g_reqs[k].group);
        g_freeLook = 0;
        return;
    }
    for (int k = g_reqs[i].firstEntry; k < g_negCount; ++k) {
        std::uintptr_t r = g_negEntry[k], who = 0;
        if (Read<int>(r + 0x48) >= 0 || !UsedByOthers(r, &who)) continue;
        if (GetTickCount64() - g_freeLookSince < 1000) return;
        Log("copy files: object %#x's look (group %d) stays for the room: %.40s is read by %p (an actor or a weapon), "
            "still after 1 s", obj, l->group, reinterpret_cast<const char*>(r + 4), reinterpret_cast<void*>(who));
        g_freeLook = 0;
        return;
    }
    int from = g_reqs[i].firstEntry, n = 0, freed = 0, stopped = 0;
    unsigned long long bytes = 0;
    for (int k = from; k < g_negCount; ++k) {
        std::uintptr_t r = g_negEntry[k];
        if (Read<int>(r + 0x48) >= 0) continue;  // not ours any more
        std::uintptr_t at = Read<std::uintptr_t>(r + 0x60);
        std::uint64_t size = Read<std::uint64_t>(r + 0x50);
        if (at) stopped += StopEffectsIn(at, size, reinterpret_cast<const char*>(r + 4));
        bytes += size;
        freed += LetGoOurs(r) == 0;
        ++n;
    }
    int group = g_reqs[i].group;
    g_negCount = from;
    g_negNext = group;
    g_reqCount = i;
    int kept = 0;
    for (int k = 0; k < g_lookCount; ++k)
        if (!g_looks[k].group || g_looks[k].group < group) g_looks[kept++] = g_looks[k];
    g_lookCount = kept;
    g_negRebase = true;
    g_freeLook = 0;
    StartLoads();
    Log("copy files: object %#x's look let go (one look at a time): %d files from group %d on, %llu KB (%d never "
        "loaded: freed), %d effects reading them stopped", obj, n, group, bytes / 1024, freed, stopped);
}

void NegForget() {
    g_negCount = 0;
    g_negNext = kNegFirst;
    g_negRebase = false;
    g_negEnd = g_negBottom = 0;
    g_negFullLogged = false;
    g_lookCount = 0;
    g_reqCount = 0;
    g_freeLook = 0;
}

// Every drop logged (research: which one is the room's own); ours let go at a drop of the room's own groups (from 0
// to 1000: a room's base files are group 1000 and up), before the game's drop and its layout.
void NegAtDrop(int from, std::uintptr_t caller) {
    __try {
        std::uint16_t room = Read<std::uint16_t>(ExeBase() + kNowRoom);
        Log("file census: drop from group %d (room %02x/%02x, ready %u, caller exe+%#llx)", from, room & 0xFF,
            room >> 8, Read<std::uint8_t>(ExeBase() + kRoomReadyByte),
            static_cast<unsigned long long>(caller - ExeBase()));
        if (from < 0 || from > 1000) return;
        if (!g_negCount) {  // only declined looks this room
            if (g_lookCount) NegForget();
            return;
        }
        int n = 0, freed = 0, states[4] = {};
        for (int k = 0; k < g_negCount; ++k) {
            std::uintptr_t r = g_negEntry[k];
            if (Read<int>(r + 0x48) >= 0) continue;  // not ours any more
            ++states[Read<std::uint8_t>(r + 1) & 3];
            freed += LetGoOurs(r) == 0;
            ++n;
        }
        Log("neg files: the drop from group %d (the room's own): our %d files let go before it (%d never loaded: "
            "freed; states 1/2/3: %d/%d/%d%s)", from, n, freed, states[1], states[2], states[3],
            states[1] ? "; SOME STILL BEING READ (state 1): let go mid-read" : "");
        NegForget();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("neg files: exception at a drop");
    }
}

}  // namespace

void FormLookInit() {
    if (EnvInt("KH2COOP_FORM_LOOK", 1) != 1) return;
    HookFunction(kDrop, kDropBytes, sizeof(kDropBytes), reinterpret_cast<void*>(HookDrop),
                 reinterpret_cast<void**>(&g_realDrop), "FileDrop");
    HookFunction(kPlaceFile, kPlaceFileBytes, sizeof(kPlaceFileBytes), reinterpret_cast<void*>(HookPlaceFile),
                 reinterpret_cast<void**>(&g_realPlaceFile), "FilePlace");
    HookFunction(kLoadWait, kLoadWaitBytes, sizeof(kLoadWaitBytes), reinterpret_cast<void*>(HookLoadWait),
                 reinterpret_cast<void**>(&g_realLoadWait), "FileLoadWait");
    HookFunction(kActorFree, kActorFreeBytes, sizeof(kActorFreeBytes), reinterpret_cast<void*>(HookActorFree),
                 reinterpret_cast<void**>(&g_realActorFree), "ActorFree");
    HookFunction(kFindOrAdd, kFindOrAddBytes, sizeof(kFindOrAddBytes), reinterpret_cast<void*>(HookFindOrAdd),
                 reinterpret_cast<void**>(&g_realFindOrAdd), "FileFindOrAdd");
    HookFunction(kGroupFind, kGroupFindBytes, sizeof(kGroupFindBytes), reinterpret_cast<void*>(HookGroupFind),
                 reinterpret_cast<void**>(&g_realGroupFind), "FileGroupFind");
    HookFunction(kNameGroup, kNameGroupBytes, sizeof(kNameGroupBytes), reinterpret_cast<void*>(HookNameGroup),
                 reinterpret_cast<void**>(&g_realNameGroup), "FileNameGroup");
    g_negNoJoin = EnvInt("KH2COOP_NEG_NOJOIN", 1) != 0;
    Log("neg files: no-join rule %s (KH2COOP_NEG_NOJOIN)", g_negNoJoin ? "on" : "off");
}

int FormLookCopyFilesAsk(const char* what, bool keep, void (*ask)(int group, const void* arg), const void* arg,
                         bool* declined) {
    __try {
        return NegAsk(what, false, keep, ask, arg, declined);
    } __except (LogException(GetExceptionInformation(), "asking for the copy's files")) {
        g_negAsking = 0;
        *declined = true;
        return 0;
    }
}

bool FormLookCopyFilesOurs(int group) { return IsOurGroup(group); }

void FormLookCopyFilesFill(bool on) { g_fillOurs = on; }

void FormLookDebugNeg(int form) {
    g_negForm = form;
    Log("debug command: negfiles %d (a look's files in a negative group of ours)", form);
}

// From copy_blade.cpp's place-pass hook, after the game's layout: the census's per-room gap and Drive area, and
// whether a file of ours, our bottom or the heap's end moved.
void FormLookAfterLayout() {
    __try {
        std::uintptr_t bottom = 0;
        unsigned long long drive = 0;
        std::uintptr_t top = PlacedTop(&bottom, &drive);
        ++g_layouts;
        if (top) {
            long long gap = static_cast<long long>(bottom) - static_cast<long long>(top);
            if (g_gapMin < 0 || gap < g_gapMin) g_gapMin = gap;
        }
        if (drive > g_driveMax) g_driveMax = drive;
        if (!g_negCount) return;
        std::uintptr_t end = Read<std::uintptr_t>(ExeBase() + kHeapEnd);
        for (int k = 0; k < g_negCount; ++k) {
            std::uintptr_t r = g_negEntry[k], place = Read<std::uintptr_t>(r + 0x58);
            int group = Read<int>(r + 0x48);
            if (!g_negPlace[k]) {
                g_negPlace[k] = place;
                continue;
            }
            if (place == g_negPlace[k] && group < 0) continue;
            Log("neg files: MOVED: %.40s (group %d) %p -> %p", reinterpret_cast<const char*>(r + 4), group,
                reinterpret_cast<void*>(g_negPlace[k]), reinterpret_cast<void*>(place));
            g_negPlace[k] = place;
        }
        if (g_negRebase || !g_negEnd) {
            Log("neg files: laid out: %d files of ours, bottom %p, the heap's end %p, %lld KB to the placed files",
                g_negCount, reinterpret_cast<void*>(bottom), reinterpret_cast<void*>(end),
                top ? (static_cast<long long>(bottom) - static_cast<long long>(top)) / 1024 : -1LL);
            g_negRebase = false;
            g_negBottom = bottom;
            g_negEnd = end;
            return;
        }
        if (end != g_negEnd)
            Log("neg files: the heap's end MOVED %p -> %p", reinterpret_cast<void*>(g_negEnd),
                reinterpret_cast<void*>(end));
        if (bottom != g_negBottom)
            Log("neg files: our bottom MOVED %p -> %p", reinterpret_cast<void*>(g_negBottom),
                reinterpret_cast<void*>(bottom));
        g_negEnd = end;
        g_negBottom = bottom;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("neg files: exception after a layout");
    }
}

void FormLookBladeRebuilt(std::uintptr_t copy) {
    __try {
        if (copy != g_copyWas) return;  // KnowCopy notes the new Keyblade's motions when it meets this copy
        std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapons[0]);
        if (!w) return;
        g_weaponOwnMotions = Read<std::uintptr_t>(w + kMotionCtx);
        if (g_wearing != g_ownObject) SetWeaponMotions(copy, g_wearing);
        RecordWorn(copy);
    } __except (LogException(GetExceptionInformation(), "after the copy's Keyblade was built again")) {
    }
}

void FormLookCountEffects(std::uintptr_t owner, std::uintptr_t from, std::uint64_t size, int* owned, int* reading) {
    *owned = *reading = 0;
    __try {
        std::uintptr_t g = Read<std::uintptr_t>(ExeBase() + kEffects);
        for (int ng = 0; g && ng < 4096; ++ng) {
            std::uintptr_t e = Read<std::uintptr_t>(g + 0x18);
            for (int ne = 0; e && ne < 4096; ++ne) {
                std::uintptr_t next =
                    Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(e + 0x74));
                std::uintptr_t inst = Read<std::uintptr_t>(e + 0x40);
                if (inst && Read<std::uintptr_t>(inst + 0x30)) {
                    if (owner && Read<std::uintptr_t>(e + 0x50) == owner) ++*owned;
                    std::uintptr_t def = Read<std::uintptr_t>(e + 0x48);
                    if (from && def >= from && def < from + size) ++*reading;
                }
                e = next;
            }
            g = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(Read<std::uint32_t>(g + 0x28));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *owned = *reading = -1;
    }
}

void FormLookDebug(int form) {
    g_debugForm = form;
    Log("debug command: form look %d on the copy", form);
}

// ---- Test command 25 'limitgap' (bench limit_gap's baseline): the Limits' files out of the Drive area (the drop from
// 5000 and a layout of ours), so that on the next frame the game's own Limit preload (exe+0x3E7650, each frame while
// no Limit runs, L155) asks them back and lays the heap out with no load wait: proof that the preload runs at all
// (it needs a usable Limit). Refused while the copy wears a form look: our own layout would move the look's files
// first (bench 2026-10-27 14:23: the follow-up after copy_blade ended the moved Keyblade crashed at exe+0x3C859E).
// F356 itself needs no command: a Revert leaves its form's files in 5000, so the preload's layout runs right after.
constexpr std::uintptr_t kRunningLimit = 0x2A24CC0;  // the running Limit's object, 0 none (limit_sync.cpp)
constexpr std::uintptr_t kFormByte = 0x9ACDD4;       // u8 our Sora's Drive Form, 0 none
bool g_limitGap = false;

void LimitGap() {
    std::uintptr_t limit = Read<std::uintptr_t>(ExeBase() + kRunningLimit);
    std::uint8_t form = Read<std::uint8_t>(ExeBase() + kFormByte);
    bool idle = LoaderIdle(), look = g_copyWas && g_wearing != g_ownObject;
    if (limit || form || !idle || look) {
        Log("limit gap: refused (a Limit runs %d, our form %u, the loader idle %d, the copy in a form look %d)",
            limit != 0, form, idle, look);
        return;
    }
    Log("limit gap: the copy wears %#x (its own %#x); the Limits' files out of the Drive area", g_wearing,
        g_ownObject);
    LogDriveArea("before the limit gap");
    Fn<void(__fastcall*)(int)>(kDrop)(5000);
    StartLoads();
    LogDriveArea("after the limit gap");
    Log("limit gap: done; the game's Limit preload asks them back on the next frame");
}

void FormLookDebugLimitGap() {
    g_limitGap = true;
    Log("debug command: limitgap (the Limits' files out, the game's preload brings them back)");
}

void FormLookFrame(std::uintptr_t copy, const PeerPose* pose, bool roomGone) {
    if (g_on < 0) {
        g_on = EnvInt("KH2COOP_FORM_LOOK", 1) == 1;
        Log("form look: %s", g_on ? "on (the other player's Drive Form shows on their copy)" : "off (FORM_LOOK=0)");
    }
    if (!g_on) return;
    if (roomGone || !copy) {  // a new room's copy starts as Sora
        if (g_lastWant && g_lastWant != g_ownObject) Log("form look: room changed or copy gone: look forgotten");
        g_lastWant = 0;
        g_ask = 0;
        g_waiting = 0;
        g_copyWas = 0;
        g_secondPending = 0;
        g_freeLook = 0;
        // The worn files' list entries belong to the old room: a new room's file can take the same entry, and the
        // place hook would then stop that file's effects as if the copy wore it (docs/CRASH_RISK_REVIEW.md).
        for (int k = 0; k < kWorn; ++k) g_wornEntry[k] = g_wornPlace[k] = 0;
        g_wornMoved = false;
        return;
    }
    __try {
        KnowCopy(copy);
        std::uint32_t want = g_ownObject;
        if (pose && pose->hasActor && !pose->captured) {
            int form = FormOf(pose->body);
            if (form >= 1 && form <= kLastFollowedForm) want = pose->body;
        }
        if (want == g_lastWant) return;
        Log("form look: the peer's body is object %#x (form %d, form byte %u): the copy should look like %#x",
            pose ? pose->body : 0, pose ? FormOf(pose->body) : 0, pose ? pose->form : 0, want);
        g_lastWant = want;
        g_ask = want;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("form look: exception picking the look");
    }
}

void FormLookSpawnStep() {
    g_fillOurs = false;  // never left on past our own call (an exception inside a fill or a build)
    if (g_negForm >= 0) {
        int form = g_negForm;
        g_negForm = -1;
        __try {
            NegRequest(form);
        } __except (LogException(GetExceptionInformation(), "asking for files in a negative group")) {
            g_negAsking = 0;
        }
    }
    if (g_limitGap) {  // before the follow-up below, which follows our own layout's moves at once
        g_limitGap = false;
        __try {
            LimitGap();
        } __except (LogException(GetExceptionInformation(), "taking the Limits' files out")) {
        }
    }
    if (g_freeLook) {
        __try {
            TryFreeLook();
        } __except (LogException(GetExceptionInformation(), "letting go of a look")) {
            g_freeLook = 0;
        }
    }
    if (g_debugForm >= 0) {
        int form = g_debugForm;
        g_debugForm = -1;
        __try {
            std::uintptr_t copy = PuppetCloneActor();
            if (copy) KnowCopy(copy);
            g_ask = form ? FormObject(form) : g_ownObject;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (g_ask) {
        std::uint32_t obj = g_ask;
        __try {
            if (Apply(obj) && g_ask == obj) g_ask = 0;
        } __except (LogException(GetExceptionInformation(), "putting a look on")) {
            if (g_ask == obj) g_ask = 0;
            g_waiting = 0;
            Log("form look: object %#x: exception", obj);
        }
    }
    if (g_wornMoved) {
        g_wornMoved = false;
        __try {
            FollowMovedFiles("a file layout (seen from the spawn step)");
        } __except (LogException(GetExceptionInformation(), "following the moved files")) {
        }
    }
    __try {  // research: our own body's weapons 1.5 s after a Drive / Revert swapped it
        std::uintptr_t own = Read<std::uintptr_t>(ExeBase() + kPlayer);
        if (own && own != g_ownBody) {
            g_ownBody = own;
            g_ownWeaponsAt = GetTickCount64() + 1500;
        }
        if (g_ownWeaponsAt && GetTickCount64() >= g_ownWeaponsAt) {
            g_ownWeaponsAt = 0;
            LogWeapons(own, "our own body");
        }
    } __except (LogException(GetExceptionInformation(), "with our weapons")) {
        g_ownWeaponsAt = 0;
    }
    if (g_secondPending) {  // the other player's form Keyblade, once its files are in
        __try {
            std::uintptr_t copy = PuppetCloneActor();
            std::uintptr_t entry =
                g_wearing ? Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kObjectEntry)(g_wearing) : 0;
            if (copy && copy == g_copyWas && entry == g_secondPending && !g_secondBlade) {
                BuildSecondBlade(copy, entry);
                if (g_secondBlade) RecordWorn(copy);
            } else {
                g_secondPending = 0;
            }
        } __except (LogException(GetExceptionInformation(), "building the second Keyblade")) {
            g_secondPending = 0;
        }
    }
    // An ended Keyblade's teardown also clears its owner's slot (exe+0x3EF520: owner+0xD60 + 8 x slot): after a
    // rebuild that's our live second Keyblade's place, put back.
    if (g_secondBlade) {
        __try {
            std::uintptr_t copy = PuppetCloneActor();
            if (copy && copy == g_copyWas && !Read<std::uintptr_t>(copy + kWeapons[1]) &&
                Read<std::uintptr_t>(g_secondBlade + 0xBF8) == copy &&
                !(Read<std::uint32_t>(g_secondBlade + 0x120) & 0x10000000)) {
                *reinterpret_cast<std::uintptr_t*>(copy + kWeapons[1]) = g_secondBlade;
                Log("form look: the copy's second Keyblade %p put back at copy+0xD68 (an ended one's teardown cleared "
                    "it)", reinterpret_cast<void*>(g_secondBlade));
            }
        } __except (LogException(GetExceptionInformation(), "checking the second Keyblade")) {
        }
    }
    // 1.5 s after a look change: what the copy carries. In a form, those that end with the old body stop here too:
    // after a room change the new copy plays the peer's motion 53 (a form's appear) after the look went on, and it
    // started them again (two-PC run 2026-10-22: 12 of them 1.5 s later, the shine until the form ended; TODO 1.106).
    if (g_glowCheckAt && GetTickCount64() >= g_glowCheckAt) {
        g_glowCheckAt = 0;
        __try {
            std::uintptr_t copy = PuppetCloneActor();
            if (copy) {
                Log("form look: 1.5 s after the look change%s:", g_glowDeclined ? " (a declined look)" : "");
                CopyBodyEffects(copy, (g_wearing != g_ownObject || g_glowDeclined) && StopGlow());
                LogWeapons(copy, "the copy");
            }
        } __except (LogException(GetExceptionInformation(), "with the copy's effects")) {
        }
        g_glowDeclined = false;
    }
}

// Research (the review's 5.2.6, F344): the groups in the game's file list, logged once the room is ready, once a
// minute, and when the groups in use or the files placed (at most once a second), the negative-group or 5100-5199
// files or the heap's end change. Log only (room between -1: no placed file below the negative ones).
void FormLookFileCensus() {
    constexpr int kMaxEntries = 4096;
    static std::uint16_t s_room = 0xFFFF;
    static bool s_ready = false;
    static std::uint64_t s_at = 0;
    static unsigned s_neg = ~0u, s_area = ~0u, s_placed = ~0u;
    static std::uintptr_t s_end = 0;
    static char s_groups[1024] = {};

    std::uint16_t room = Read<std::uint16_t>(ExeBase() + kNowRoom);
    bool ready = Read<std::uint8_t>(ExeBase() + kRoomReadyByte) != 0;
    const char* why = nullptr;
    if (room != s_room) {
        if (s_room != 0xFFFF && g_layouts)
            Log("file census: room %02x/%02x left: %u layouts, the smallest gap between the placed files and the "
                "negative files' bottom %lld KB, the largest Drive area %llu KB", s_room & 0xFF, s_room >> 8, g_layouts,
                g_gapMin / 1024, g_driveMax / 1024);
        if (g_negCount) {
            Log("neg files: BUG: the room changed with %d files of ours not let go by a drop", g_negCount);
            NegForget();
        }
        g_layouts = 0;
        g_gapMin = -1;
        g_driveMax = 0;
        s_room = room;
        s_ready = false;
    }
    if (!ready) return;
    if (!s_ready) why = "room ready";

    unsigned n = 0, neg = 0, area = 0, letGo = 0, placed = 0;
    unsigned long long negBytes = 0, areaBytes = 0;
    std::uintptr_t top = 0;
    char groups[1024];
    int len = 0, last = 0x7FFFFFFF, lastCount = 0;
    auto flush = [&]() {
        if (lastCount && len < static_cast<int>(sizeof groups) - 24)
            len += std::snprintf(groups + len, sizeof groups - len, " %d:%d", last, lastCount);
    };
    groups[0] = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r && n < kMaxEntries; r = NextRes(r)) {
        ++n;
        int group = Read<int>(r + 0x48);
        std::uint64_t size = Read<std::uint64_t>(r + 0x50);
        if (group >= 10000) {
            ++letGo;
            continue;
        }
        if (group < 0) {
            ++neg;
            negBytes += size;
        } else if (std::uintptr_t place = Read<std::uintptr_t>(r + 0x58)) {
            ++placed;
            if (place + size > top) top = place + size;
        }
        if (group >= 5100 && group < 5200) {
            ++area;
            areaBytes += size;
        }
        if (group != last) {
            flush();
            last = group;
            lastCount = 0;
        }
        ++lastCount;
    }
    flush();

    std::uintptr_t end = Read<std::uintptr_t>(ExeBase() + kHeapEnd);
    std::uint64_t now = GetTickCount64();
    if (!why && (neg != s_neg || area != s_area || end != s_end)) why = "negative / 5100-5199 / heap end changed";
    if (!why && std::strcmp(groups, s_groups) != 0 && now - s_at >= 1000) why = "groups changed";
    if (!why && placed != s_placed && now - s_at >= 1000) why = "files placed changed";
    if (!why && now - s_at >= 60000) why = "a minute";
    if (!why) return;

    s_ready = true;
    s_at = now;
    s_neg = neg;
    s_area = area;
    s_end = end;
    s_placed = placed;
    std::memcpy(s_groups, groups, sizeof groups);
    std::uintptr_t bottom = Read<std::uintptr_t>(ExeBase() + kHeapNegBottom);
    Log("file census (%s): room %02x/%02x: %u files (%u placed, %u let go%s); negative groups %u files %llu bytes; 5100-5199 %u "
        "files %llu bytes; heap end %p, negative files from %p, room between %lld KB; group:files%s", why,
        room & 0xFF, room >> 8, n, placed, letGo, n >= kMaxEntries ? ", list cut" : "", neg, negBytes, area, areaBytes,
        reinterpret_cast<void*>(end), reinterpret_cast<void*>(bottom),
        top ? (static_cast<long long>(bottom) - static_cast<long long>(top)) / 1024 : -1LL, groups);
}

}  // namespace kh2coop
