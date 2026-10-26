// The other player's own Keyblade on their copy (TODO 3.3, 1.110; DECISIONS 'Players and the copy', your picks
// 2026-10-25: their Keyblade in this world's look, changed when their menu closes; their form Keyblades too).
//
// The game builds the copy's Keyblade (weapon 0, copy+0xD60) at the copy's spawn from our own save, in our own Sora's
// file group: both point into the same list entries (VERIFIED_OFFSETS F332). The pause menu's Keyblade swap
// (exe+0x3D59C0) frees that group at once (exe+0x39D010, L139), and the copy's next motion step read the freed files
// (crash 2026-10-22, TODO 1.110).
//
// What this file does:
//   - each game sends its Sora's Keyblade items ("KH2J": the record's weapon at exe+0x9ABDA0 and the 10 form
//     records' items at +0xE04 + 0x38 n) when they change with no menu open, and every 2 s;
//   - the copy's weapon 0 is built again the way the swap builds one, from the other player's item:
//     exe+0x3EF8B0(kind, item) with the copy's own body kind (entry +0x4E), so it takes this world's look; its files
//     are asked for in our group kGroup (the Drive area's last group; a file already listed joins that entry, L138)
//     and it's built once they are in;
//   - the copy's Keyblade is ended the game's own immediate way (exe+0x3EFF20, L140) before any of its files is freed
//     (hook on exe+0x39D010) or moved (hook on the layout's place pass exe+0x39D590, L141: places are worked out
//     there, nothing moved yet), and built again in the next spawn step. Without the other player's items it gets
//     the game's own pick (our save), as before.
// KH2COOP_COPY_BLADE=0: off (the game's Keyblade, and the old crash).

#include "copy_blade.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "form_look.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4A32484B;  // "KH2J"
constexpr std::uint16_t kVersion = 1;
constexpr std::uintptr_t kRecord = 0x9ABDA0;        // Sora's character record: u16 +0 the Keyblade item
constexpr std::uintptr_t kFormItems = 0xE04;        // + 0x38 x (form - 1): the form record's Keyblade item
constexpr int kForms = 10;
constexpr std::uintptr_t kOpenMenu = 0x7435D0;      // u8: 0xFF no menu open (pause_sync.cpp)
constexpr std::uintptr_t kNow = 0x717008;           // u8 world, 0xFF = title
constexpr std::uint32_t kResendMs = 2000;
constexpr int kGroup = 5099;                        // our Keyblade files (the Drive area 5000-5099, its last group)
constexpr std::uintptr_t kWeaponOf = 0x3EF8B0;      // (kind, item) -> weapon object
constexpr std::uintptr_t kWeaponObject = 0x3E0CC0;  // (entry, slot) -> the game's pick from our save
constexpr std::uintptr_t kObjectEntry = 0x3DFEB0;   // (objId) -> object entry
constexpr std::uintptr_t kFilesState = 0x3E0160;    // (objId, entry) -> & ~0xC = not loaded
constexpr std::uintptr_t kAskWeapon = 0x3E0A20;     // (kind, slot, objId, group, -1): the swap's file request
constexpr std::uintptr_t kWeaponMotions = 0x3EF910; // (kind, slot, 0) -> the kind's weapon motion file name
constexpr std::uintptr_t kStartLoads = 0x39D460;    // (task or 0)
constexpr std::uintptr_t kAlloc = 0x152430;         // (size) -> actor memory
constexpr std::uintptr_t kNewWeapon = 0x3EF750;     // (mem, weapon entry, owner, kind, slot, group)
constexpr std::uintptr_t kMotionSync = 0x3C8440;    // (controller, owner+0x188)
constexpr std::uintptr_t kSetMotion = 0x3C86A0;     // (controller, motion, start, blend)
constexpr std::uintptr_t kDestroyWeapon = 0x3EFF20; // (weapon): hide, unlink, end, free its motion state and model now
constexpr std::uintptr_t kIsHidden = 0x3BA840;      // (actor) -> hidden
constexpr std::uintptr_t kLive = 0x3BA720;          // (actor) -> in the actor list and not going (limit_sync.cpp)
constexpr std::uintptr_t kGroupFree = 0x39D010;     // (group): frees that group's files now
constexpr std::uint8_t kGroupFreeBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kPlacePass = 0x39D590;     // (): works out every file's place (old one stays at +0x60)
constexpr std::uint8_t kPlacePassBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                            0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56};
constexpr std::uintptr_t kResHead = 0x29F33C8;      // the file list; next = handle +0x70
constexpr std::uintptr_t kHandle = 0x4AD270;
constexpr std::uintptr_t kReaderBusy = 0x2A0D628;
constexpr std::uintptr_t kLoaderBusy2 = 0x1DC340;
constexpr std::uintptr_t kListHead = 0x2A171C8;     // the actor list; next = handle +0xA90
constexpr std::uintptr_t kNextActor = 0xA90;
constexpr std::uintptr_t kWeapon0 = 0xD60, kWeapon1 = 0xD68, kMotionCtx = 0x158, kWeaponFlags = 0x124;
constexpr std::uint32_t kPutAway = 0x400;
constexpr std::uint64_t kLoadTimeoutMs = 15000;

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t main;
    std::uint16_t forms[kForms];
};
#pragma pack(pop)

int g_on = -1;
// Ours, as last sent.
Packet g_sent{};
bool g_sentAny = false;
std::uint64_t g_sentAt = 0;
std::uint32_t g_seq = 0;
// The other player's.
bool g_peerKnown = false;
std::uint32_t g_peerSeq = 0;
std::uint16_t g_peerMain = 0, g_peerForms[kForms] = {};
// The copy's Keyblade.
std::uintptr_t g_copy = 0;          // the copy the rest is about
std::uint16_t g_builtItem = 0;      // the item we built its weapon 0 from, 0 = the game's (our save)
bool g_ended = false;               // its weapon 0 was ended (files freed or moving): build it again
bool g_endedHidden = false;
std::uint32_t g_endedFlags = 0;     // the ended one's +0x124
struct Wait {
    std::uint32_t obj;              // weapon object whose files we asked for, 0 none
    std::uint64_t since;
};
Wait g_wait[2] = {};                // slot 0: the Keyblade, slot 1: a form's second Keyblade (form_look.cpp)
std::uint16_t g_failedItem = 0;     // an item whose Keyblade couldn't be made (its files didn't come)
bool g_dropAsk = false;             // let go of our files the copy no longer uses

using PFN_GroupFree = void(__fastcall*)(int);
PFN_GroupFree g_realGroupFree = nullptr;
using PFN_PlacePass = void(__fastcall*)();
PFN_PlacePass g_realPlacePass = nullptr;

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

std::uintptr_t Decode(std::uint32_t h) { return Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kHandle)(h); }
std::uintptr_t NextRes(std::uintptr_t r) { return Decode(Read<std::uint32_t>(r + 0x70)); }

bool On() {
    if (g_on < 0) {
        g_on = EnvInt("KH2COOP_COPY_BLADE", 1) == 1;
        Log("copy blade: %s", g_on ? "on (the other player's own Keyblade on their copy)" : "off (COPY_BLADE=0)");
    }
    return g_on != 0;
}

bool LoaderIdle() {
    if (Read<std::uint64_t>(ExeBase() + kReaderBusy)) return false;
    if (Fn<std::uint8_t(__fastcall*)()>(kLoaderBusy2)()) return false;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        std::uint8_t state = Read<std::uint8_t>(r + 1);
        if (Read<int>(r + 0x48) < 10000 && state != 0 && state != 3) return false;
    }
    return true;
}

// The places a weapon reads files through: its set (model, +0x928, motions) and its motion controller's file.
int WeaponPlaces(std::uintptr_t w, std::uintptr_t* out) {
    int n = 0;
    for (std::uintptr_t off = 0x920; off <= 0x930; off += 8)
        if (std::uintptr_t p = Read<std::uintptr_t>(w + off)) out[n++] = p;
    if (std::uintptr_t p = Read<std::uintptr_t>(w + kMotionCtx)) out[n++] = p;
    return n;
}

int g_afterBuild = 0;  // spawn steps after a build still to log

bool Inside(std::uintptr_t p, std::uintptr_t from, std::uint64_t size) { return from && p >= from && p < from + size; }

// The list entry (data at its current place +0x60, or +0x58 before any layout) that a weapon of the copy reads and
// that `pick` selects; 0 if none.
template <typename Pick>
std::uintptr_t WeaponReads(std::uintptr_t w, Pick pick) {
    std::uintptr_t places[4];
    int n = WeaponPlaces(w, places);
    if (!n) return 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        if (!pick(r)) continue;
        std::uintptr_t at = Read<std::uintptr_t>(r + 0x60);
        if (!at) at = Read<std::uintptr_t>(r + 0x58);
        std::uint64_t size = Read<std::uint64_t>(r + 0x50);
        for (int k = 0; k < n; ++k)
            if (Inside(places[k], at, size)) return r;
    }
    return 0;
}

// Ends the copy's weapon 0 the game's own way (the menu swap's, just before it frees the old files); built again in
// the next spawn step. `moving`: called between a layout's place pass and its moves. The end lets go of its set's
// files by place (exe+0x39DA10 finds the entry whose new place +0x58 is that one, else writes through null: the
// friend's crash at exe+0x39DA55 at its own Valor Drive, bench 2026-10-25 21:27:28), so the set first gets the new
// places; the rest of the end reads the data, still at the old place until the moves. The same for its motion
// controller's file (+0x158, released by place too): with the copy in a form look it is form_look's weapon motions
// (W_EX010_BTLF_R.mset in Valor), which our own Drive into another form moves (bench form_share 2026-10-26 20:04:19,
// the same crash).
void EndBlade(std::uintptr_t copy, const char* why, std::uintptr_t file, bool moving = false) {
    std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapon0);
    if (!w) return;
    constexpr std::uintptr_t kByPlace[] = {0x920, 0x928, 0x930, kMotionCtx};
    for (std::uintptr_t off : kByPlace) {
        if (!moving) break;
        std::uintptr_t p = Read<std::uintptr_t>(w + off);
        for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); p && r; r = NextRes(r))
            if (Read<std::uintptr_t>(r + 0x60) == p && Read<std::uintptr_t>(r + 0x58) != p) {
                *reinterpret_cast<std::uintptr_t*>(w + off) = Read<std::uintptr_t>(r + 0x58);
                break;
            }
    }
    g_endedHidden = Fn<std::uint8_t(__fastcall*)(std::uintptr_t)>(kIsHidden)(w) != 0;
    g_endedFlags = Read<std::uint32_t>(w + kWeaponFlags);
    // For the log (the friend's crash 2026-10-25 21:59:48 came right after a visible Keyblade's end): the effects it
    // owns and those reading its .a.us, which the end doesn't stop (form_look stops them for the second Keyblade).
    std::uintptr_t fxFile = 0, fxAt = 0;
    std::uintptr_t aus = Read<std::uintptr_t>(w + 0x928);
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); aus && r; r = NextRes(r)) {
        std::uintptr_t at = Read<std::uintptr_t>(r + 0x60);
        if (!at) at = Read<std::uintptr_t>(r + 0x58);
        if (Inside(aus, at, Read<std::uint64_t>(r + 0x50))) {
            fxFile = r;
            fxAt = at;
            break;
        }
    }
    int owned = 0, reading = 0;
    FormLookCountEffects(w, fxAt, fxFile ? Read<std::uint64_t>(fxFile + 0x50) : 0, &owned, &reading);
    Log("copy blade:   before its end: flags +0x124 %#x, %d effects of its own, %d reading %.40s (group %d); the copy's "
        "motion %d", g_endedFlags, owned, reading, fxFile ? reinterpret_cast<const char*>(fxFile + 4) : "-",
        fxFile ? Read<int>(fxFile + 0x48) : 0, Read<int>(copy + 0x180));
    Fn<void(__fastcall*)(std::uintptr_t)>(kDestroyWeapon)(w);
    if (Read<std::uintptr_t>(copy + kWeapon0) == w) *reinterpret_cast<std::uintptr_t*>(copy + kWeapon0) = 0;
    g_ended = true;
    Log("copy blade: the copy's Keyblade %p ended (%s: %.40s, group %d)%s", reinterpret_cast<void*>(w), why,
        file ? reinterpret_cast<const char*>(file + 4) : "-", file ? Read<int>(file + 0x48) : 0,
        g_endedHidden ? ", it was hidden" : "");
}

bool Live(std::uintptr_t actor) { return Fn<std::uint8_t(__fastcall*)(std::uintptr_t)>(kLive)(actor) != 0; }

// The copy we know, while it's in the actor list and not going (a room's unload frees file groups too, after its
// actors). Not PuppetCloneActor(): that one wants the copy updated in the last 2 frames, and the pause menu stops the
// actor updates, so in the menu's Keyblade swap it said 'no copy' and the copy's Keyblade kept the freed files
// (bench 2026-10-25 21:19:34: the 1.110 crash at exe+0x3C859E).
std::uintptr_t Copy() {
    if (g_on != 1 || !CoopActive() || !g_copy) return 0;
    return Live(g_copy) ? g_copy : 0;
}

void __fastcall HookGroupFree(int group) {
    __try {
        if (std::uintptr_t copy = Copy()) {
            std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapon0);
            std::uintptr_t file = w ? WeaponReads(w, [group](std::uintptr_t r) {
                return Read<int>(r + 0x48) == group;
            }) : 0;
            if (file) EndBlade(copy, "the game frees its files' group", file);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception before a group free (%d)", group);
    }
    g_realGroupFree(group);
}

// Before: a file the Keyblade reads that the pass may free (let go: group 10000 and up, or marked +0x44 bit 1).
// After: one that moves (new place +0x58, data still at +0x60 until exe+0x39E690 copies it).
void __fastcall HookPlacePass() {
    std::uintptr_t copy = 0;
    __try {
        copy = Copy();
        std::uintptr_t w = copy ? Read<std::uintptr_t>(copy + kWeapon0) : 0;
        std::uintptr_t file = w ? WeaponReads(w, [](std::uintptr_t r) {
            return Read<int>(r + 0x48) >= 10000 || (Read<std::uint32_t>(r + 0x44) & 2) != 0;
        }) : 0;
        if (file) EndBlade(copy, "a file layout may free its file", file);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception before a file layout");
    }
    g_realPlacePass();
    __try {
        std::uintptr_t w = copy ? Read<std::uintptr_t>(copy + kWeapon0) : 0;
        std::uintptr_t file = w ? WeaponReads(w, [](std::uintptr_t r) {
            std::uintptr_t now = Read<std::uintptr_t>(r + 0x60);
            return now && Read<std::uintptr_t>(r + 0x58) != now && Read<int>(r + 0x48) < 10000;
        }) : 0;
        if (file) EndBlade(copy, "a file layout moves its file", file, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception after a file layout");
    }
}

// A loaded file by name, ours (kGroup) first, else any in use; its entry or 0.
std::uintptr_t LoadedEntry(const char* name) {
    std::uintptr_t any = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        int group = Read<int>(r + 0x48);
        if (Read<std::uint8_t>(r + 1) != 3 || group >= 10000 || _stricmp(reinterpret_cast<const char*>(r + 4), name))
            continue;
        if (group == kGroup) return r;
        if (!any) any = r;
    }
    return any;
}

// One of this actor's weapons, if it has any: weapon actors (0xC10 bytes) and others are shorter than +0xD68, so a
// slot only counts when it holds a readable pointer to a weapon whose owner (+0xBF8) is this actor. Checked, never
// tried: on a short actor the slot holds anything (0x90 seen), and that read's fault, guarded or not, ended the
// game twice right after a Keyblade build (friend 2026-10-25 21:59:48, host 23:12:11: kh2coop.dll+0x4A729, address
// 0xC88, then clr.dll's stack overflow).
std::uintptr_t WeaponOf(std::uintptr_t a, std::uintptr_t slot) {
    if (!Readable(a + slot, 8)) return 0;
    std::uintptr_t w = Read<std::uintptr_t>(a + slot);
    if (w < 0x10000 || (w & 7) || !Readable(w + 0xBF8, 8)) return 0;
    return Read<std::uintptr_t>(w + 0xBF8) == a ? w : 0;
}

bool Reads(std::uintptr_t x, std::uintptr_t at, std::uint64_t size) {
    std::uintptr_t places[4];
    int n = WeaponPlaces(x, places);
    for (int k = 0; k < n; ++k)
        if (Inside(places[k], at, size)) return true;
    return false;
}

// Some actor (or one of its weapons) other than the copy's weapon 0 reads this file.
bool UsedByOthers(std::uintptr_t r, std::uintptr_t mine) {
    std::uintptr_t at = Read<std::uintptr_t>(r + 0x58);
    std::uint64_t size = Read<std::uint64_t>(r + 0x50);
    std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
    for (int n = 0; a && n < 1024; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor))) {
        if (a != mine && Reads(a, at, size)) return true;
        constexpr std::uintptr_t kSlots[] = {kWeapon0, kWeapon1};
        for (std::uintptr_t off : kSlots) {
            std::uintptr_t w = WeaponOf(a, off);
            if (w && w != mine && Reads(w, at, size)) return true;
        }
    }
    return false;
}

// Our group's files the copy's Keyblade doesn't read are let go (group + 10000; a later layout frees them).
void DropUnused(std::uintptr_t copy, bool verbose = false) {
    g_dropAsk = false;
    if (!LoaderIdle() || g_wait[0].obj || g_wait[1].obj) {  // not while files we asked for are coming or unused yet
        if (verbose) Log("copy blade:   unused files: later (loader busy or files still coming)");
        g_dropAsk = true;
        return;
    }
    std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapon0);
    unsigned n = 0, kept = 0;
    for (std::uintptr_t r = Read<std::uintptr_t>(ExeBase() + kResHead); r; r = NextRes(r)) {
        if (Read<int>(r + 0x48) != kGroup || Read<std::uint8_t>(r + 1) != 3) continue;
        bool mine = w && WeaponReads(w, [r](std::uintptr_t x) { return x == r; });
        if (mine || UsedByOthers(r, w)) {
            ++kept;
            continue;
        }
        *reinterpret_cast<int*>(r + 0x48) = kGroup + 10000;
        ++n;
    }
    if (n || verbose) Log("copy blade: let go of %u of our Keyblade files (%u kept)", n, kept);
}

// Builds the copy's weapon 0 as the menu swap does (exe+0x3D59C0): the weapon actor with the group its files are in,
// its controller linked to the copy's, at the copy's motion, shown unless the old one was hidden.
std::uintptr_t Build(std::uintptr_t copy, std::uintptr_t weaponEntry, int kind, int group, bool hidden,
                     std::uint32_t flags) {
    std::uintptr_t mem = Fn<std::uintptr_t(__fastcall*)(std::uint64_t)>(kAlloc)(0xC10);
    if (!mem) return 0;
    std::uintptr_t w = Fn<std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, int, int, int)>(
        kNewWeapon)(mem, weaponEntry, copy, kind, 0, group);
    if (!w) return 0;
    *reinterpret_cast<std::uintptr_t*>(copy + kWeapon0) = w;
    Fn<void(__fastcall*)(std::uintptr_t, std::uint32_t)>(kMotionSync)(w + kMotionCtx, Read<std::uint32_t>(copy + 0x188));
    Fn<void(__fastcall*)(std::uintptr_t, int, float, float)>(kSetMotion)(w + kMotionCtx, Read<int>(copy + 0x180), 0.0f,
                                                                         Read<float>(copy + 0x19C));
    if (!hidden) {
        std::uintptr_t handler = Decode(Read<std::uint32_t>(w));
        auto show = reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uintptr_t)>(
            Read<std::uintptr_t>(Read<std::uintptr_t>(handler) + 0x88));
        show(handler, w);
    }
    auto* f = reinterpret_cast<std::uint32_t*>(w + kWeaponFlags);
    *f = (*f & ~kPutAway) | (flags & kPutAway);  // put away (out of battle) as the old one was
    return w;
}

// Whether a weapon object's files are in: 1 = yes (*group = the group its model is listed in, which the weapon's set
// takes its files from), 0 = on their way (asked for now, in our group, or before), -1 = given up. Also the kind's
// weapon motion file for that slot (the weapon finds it by name among the loaded ones).
int Files(int kind, int slot, std::uint32_t obj, std::uintptr_t weaponEntry, int* group) {
    Wait& wait = g_wait[slot];
    char name[64];
    std::snprintf(name, sizeof(name), "obj/%.32s.mdlx", reinterpret_cast<const char*>(weaponEntry + 8));
    std::uintptr_t model = LoadedEntry(name);
    const char* motions = Fn<const char*(__fastcall*)(int, int, int)>(kWeaponMotions)(kind, slot, 0);
    bool loaded = model && !(Fn<std::uint32_t(__fastcall*)(std::uint64_t, std::uintptr_t)>(kFilesState)(obj, weaponEntry) &
                             ~0xCu) && (!motions || LoadedEntry(motions));
    if (loaded) {
        if (wait.obj == obj) {
            Log("copy blade: %s in %llu ms after asking", name, GetTickCount64() - wait.since);
            wait.obj = 0;
        }
        *group = Read<int>(model + 0x48);
        return 1;
    }
    if (wait.obj == obj) {
        if (GetTickCount64() - wait.since < kLoadTimeoutMs) return 0;
        Log("copy blade: %s's files (object %#x) still not in after %llu ms: given up", name, obj, kLoadTimeoutMs);
        wait.obj = 0;
        return -1;
    }
    if (!LoaderIdle()) return 0;
    Fn<void(__fastcall*)(int, int, std::uint32_t, int, int)>(kAskWeapon)(kind, slot, obj, kGroup, -1);
    Fn<void(__fastcall*)(std::uintptr_t)>(kStartLoads)(0);
    wait.obj = obj;
    wait.since = GetTickCount64();
    Log("copy blade: asked for %s (object %#x, kind %#x, slot %d) and %.40s in group %d", name, obj, kind, slot,
        motions ? motions : "-", kGroup);
    return 0;
}

// True when done for now, false to try again next spawn step.
bool Rebuild(std::uintptr_t copy, std::uint16_t want) {
    std::uintptr_t entry = Decode(Read<std::uint32_t>(copy + 8));
    if (!entry) return true;
    int kind = Read<std::uint16_t>(entry + 0x4E);
    std::uint16_t item = want != g_failedItem ? want : 0;
    std::uint32_t obj = item ? Fn<std::uint32_t(__fastcall*)(int, int)>(kWeaponOf)(kind, item)
                             : static_cast<std::uint32_t>(
                                   Fn<std::uint64_t(__fastcall*)(std::uintptr_t, int)>(kWeaponObject)(entry, 0));
    if (!obj && item) {
        Log("copy blade: item %#x has no Keyblade for kind %#x: the game's own pick instead", item, kind);
        g_failedItem = item;
        return false;
    }
    if (!obj) return true;
    std::uintptr_t weaponEntry = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kObjectEntry)(obj);
    if (!weaponEntry) return true;
    int group = -1;
    int files = Files(kind, 0, obj, weaponEntry, &group);
    if (files < 0 && item) g_failedItem = item;
    if (files <= 0) return false;
    const char* name = reinterpret_cast<const char*>(weaponEntry + 8);
    if (!g_ended) EndBlade(copy, item ? "the other player's Keyblade is in" : "the game's own pick again", 0);
    bool hidden = g_endedHidden;
    std::uint32_t flags = g_endedFlags;
    std::uintptr_t w = Build(copy, weaponEntry, kind, group, hidden, flags);
    g_ended = false;
    g_builtItem = item;
    Log("copy blade: the copy's Keyblade %p built: %.32s (object %#x, item %#x, kind %#x), files in group %d%s",
        reinterpret_cast<void*>(w), name, obj, item, kind, group, hidden ? ", hidden" : "");
    if (w) FormLookBladeRebuilt(copy);
    Log("copy blade:   form look told");
    g_afterBuild = 3;
    __try {
        DropUnused(copy, true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception letting go of our unused Keyblade files");
        g_dropAsk = false;
    }
    return true;
}

void ReadOurs(Packet& p) {
    std::uintptr_t rec = ExeBase() + kRecord;
    p.main = Read<std::uint16_t>(rec);
    for (int f = 0; f < kForms; ++f) p.forms[f] = Read<std::uint16_t>(rec + kFormItems + 0x38 * f);
}

}  // namespace

void CopyBladeInit() {
    if (!On()) return;
    HookFunction(kGroupFree, kGroupFreeBytes, sizeof(kGroupFreeBytes), reinterpret_cast<void*>(HookGroupFree),
                 reinterpret_cast<void**>(&g_realGroupFree), "FileGroupFree");
    HookFunction(kPlacePass, kPlacePassBytes, sizeof(kPlacePassBytes), reinterpret_cast<void*>(HookPlacePass),
                 reinterpret_cast<void**>(&g_realPlacePass), "FilePlacePass");
}

void CopyBladeFrame() {
    if (!On()) return;
    __try {
        if (Read<std::uint8_t>(ExeBase() + kNow) == 0xFF) return;  // no save loaded
        Packet p{};
        ReadOurs(p);
        if (!p.main) return;
        bool changed = !g_sentAny || p.main != g_sent.main || std::memcmp(p.forms, g_sent.forms, sizeof(p.forms));
        bool menuOpen = Read<std::uint8_t>(ExeBase() + kOpenMenu) != 0xFF;
        std::uint64_t now = GetTickCount64();
        if (changed && !menuOpen) {
            if (g_sentAny)
                Log("copy blade: our Keyblade items changed (%#x -> %#x, forms too if any): sent", g_sent.main, p.main);
            g_sent = p;
            g_sentAny = true;
        } else if (!g_sentAny || now - g_sentAt < kResendMs) {
            return;
        }
        g_sent.magic = kMagic;
        g_sent.version = kVersion;
        g_sent.build = AvatarLinkBuild();
        g_sent.seq = ++g_seq;
        g_sentAt = now;
        AvatarLinkSendRaw(&g_sent, static_cast<int>(sizeof(g_sent)));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception reading our Keyblade items");
    }
}

void CopyBladeOnPacket(const char* buf, int n) {
    if (g_on != 1 || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.seq + 1000 < g_peerSeq) g_peerSeq = 0;  // the other game restarted
    if (p.seq <= g_peerSeq) return;
    g_peerSeq = p.seq;
    bool changed = !g_peerKnown || p.main != g_peerMain || std::memcmp(p.forms, g_peerForms, sizeof(p.forms));
    if (!changed) return;
    Log("copy blade: the other player's Keyblade %#x (was %#x), form Keyblades %#x %#x %#x %#x %#x %#x", p.main,
        g_peerKnown ? g_peerMain : 0, p.forms[0], p.forms[1], p.forms[2], p.forms[3], p.forms[4], p.forms[5]);
    g_peerMain = p.main;
    std::memcpy(g_peerForms, p.forms, sizeof(g_peerForms));
    g_peerKnown = true;
    g_failedItem = 0;
}

void CopyBladeSpawnStep() {
    if (g_on != 1 || !CoopActive()) return;
    __try {
        std::uintptr_t copy = PuppetCloneActor();
        if (!copy) {  // the menu (no updates) or a load: the copy we know stays known while it's in the actor list
            if (g_copy && !Live(g_copy)) g_copy = 0;
            return;
        }
        if (copy != g_copy) {  // a new copy (room load): the game built its Keyblade from our save
            g_copy = copy;
            g_builtItem = 0;
            g_ended = false;
            g_wait[0].obj = 0;
        }
        if (g_afterBuild > 0) {  // breadcrumbs: the steps right after a build (the friend's crash 2026-10-25 21:59:48)
            --g_afterBuild;
            std::uintptr_t w = Read<std::uintptr_t>(copy + kWeapon0);
            Log("copy blade:   a step after the build: Keyblade %p, flags %#x, the copy's motion %d", reinterpret_cast<void*>(w),
                w ? Read<std::uint32_t>(w + kWeaponFlags) : 0, Read<int>(copy + 0x180));
        }
        std::uint16_t want = g_peerKnown ? g_peerMain : 0;
        bool has = Read<std::uintptr_t>(copy + kWeapon0) != 0;
        if (!g_ended && (!has || want == g_builtItem || (want && want == g_failedItem))) {
            if (g_dropAsk) DropUnused(copy);
            return;
        }
        Rebuild(copy, want);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("copy blade: exception building the copy's Keyblade");
        g_wait[0].obj = 0;
    }
}

int CopyBladeGroup() { return kGroup; }

int CopyBladeFiles(int kind, int slot, std::uint32_t obj, int* group) {
    if (g_on != 1 || slot < 0 || slot > 1 || !obj) return -1;
    std::uintptr_t weaponEntry = Fn<std::uintptr_t(__fastcall*)(std::uint32_t)>(kObjectEntry)(obj);
    return weaponEntry ? Files(kind, slot, obj, weaponEntry, group) : -1;
}

std::uint32_t CopyBladeWeaponOf(int kind, std::uint16_t item) {
    return item ? Fn<std::uint32_t(__fastcall*)(int, int)>(kWeaponOf)(kind, item) : 0;
}

std::uint16_t CopyBladePeerFormItem(int form) {
    return g_on == 1 && g_peerKnown && form >= 1 && form <= kForms ? g_peerForms[form - 1] : 0;
}

}  // namespace kh2coop
