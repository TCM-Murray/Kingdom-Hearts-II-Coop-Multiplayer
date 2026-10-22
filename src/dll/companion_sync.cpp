// Companions leave and come back the same way on both screens for Drive Forms and summons (TODO 1.98; DECISIONS C1,
// C2; you, 2026-10-21: "as close to the original game as possible"). Limits do the same in limit_sync.cpp.
//
// The game (VERIFIED_OFFSETS lead 'Drive Forms and summons send the companions away like a Limit'): the form change
// exe+0x40CE60 calls exe+0x400B20(form), which sends the party member of the form's place away with
// exe+0x400990(member, fade) (Valor: Goofy's place, Wisdom: Donald's, Limit Form: nobody, Master / Final / Anti:
// both); the summon start exe+0x3DE460 sends both with exe+0x400A30(fade). Each puts the member's party place
// (1 = Donald's, 2 = Goofy's; world allies take those places) on the bring-back list exe+0x2AE9788 (count
// exe+0x2AE9790); the form's end (exe+0x40CC00 / exe+0x40CF20) and the summon's end (exe+0x3DE080) build everyone on
// it again with exe+0x400440. The other game ran none of it.
// Here:
//   - The places our own form or summon sent away (the two leave calls, told apart by their call sites) are "ours"
//     until that form's or summon's end brings them back; each game streams them ("KH2O").
//   - While the other player holds a place, our member in that place leaves with the same game call and fade
//     (checked every frame: a member built again meanwhile leaves too). The game itself then hides a form or summon
//     that needs it (exe+0x4005E0: no live member in the place = not offered), as it does in one game (C2).
//   - The bring-back exe+0x400440 builds only the places nobody holds any more: the other player's form, summon or
//     Limit (limit_sync.cpp, not its partner) still holding one keeps it on the list, and so does our own form or
//     summon unless this is its own end. When the other player lets a place go, we call the bring-back ourselves.
//
// Their state (TODO 1.98 part 2, 1.43; DECISIONS C3): the friend's companions copy the host's (world_sync.cpp), but
// only pose and HP above 0. The host's game alone runs them, so here:
//   - The host streams each companion's HP, MP, MP recharge and KO ("KH2S", status). The friend's copies take the MP
//     as it is; a KO the game's way (its HP change to 0: the dead bit, the get-up timer) and the get-up the game's
//     way too (an HP gain on a KO'd member revives it, exe+0x3C3C30); a plain HP write of 0 does nothing.
//   - The friend's copies don't think (exe+0x1B53B0): the brain picked heals with MP that never changed in that
//     game (you, 2026-10-21: the friend's Donald tried to heal Sora without the MP for it).
//   - An HP or MP gain on a friend's copy (a Cure or an item from the friend) goes to the host's game, which applies
//     it to its own companion (a KO'd one gets up, as in one game); the copy then follows the host's status. The
//     friend's game's own gains on a copy (exe+0x3C0C30's +1s) are dropped: one got a KO'd copy up for a frame.
// COMPANION_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include "companion_sync.hpp"

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "limit_sync.hpp"
#include "puppet.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4F32484B;  // "KH2O"
constexpr std::uint16_t kVersion = 1;
constexpr std::uintptr_t kNow = 0x717008;             // u16 world, room
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;    // u64[2] party members
constexpr std::uintptr_t kForm = 0x9ACDD4;            // u8 current Drive Form (0 = none)
constexpr std::uintptr_t kSummonState = 0x2A24EDC;    // u32, 2 = a summon is out
constexpr std::uintptr_t kList = 0x2AE9788;           // u32[] party places to build again
constexpr std::uintptr_t kListCount = 0x2AE9790;      // i32
constexpr int kListMax = 2;                           // the game's own list (two places; the next field follows)
constexpr std::uintptr_t kPartyTable = 0x2A252F0;    // exe+0x3E2E40(): u16 object id per place at +0x10
constexpr std::uintptr_t kDriveFade = 0x623BC0;       // f32: the fade exe+0x400B20 passes
constexpr std::uintptr_t kObjRow = 0x918, kRemoveFlags = 0x9B8;  // +0x9B8 bit 4: being removed
constexpr std::uintptr_t kCharType = 0x3B5B20;        // (actor) -> character type (2 Donald's kind, 3 Goofy's)
constexpr std::uintptr_t kPlaceOf = 0x3E31C0;         // (type) -> party place (0 Sora, 1, 2, 3 other)
constexpr std::uintptr_t kLive = 0x3BA720;            // (actor) -> in the actor list and not going
constexpr std::uintptr_t kLeave = 0x400990;           // (member, float fade)
constexpr std::uint8_t kLeaveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uintptr_t kLeaveBoth = 0x400A30;       // (float fade): both members (summon, Mickey's Game Over)
constexpr std::uint8_t kLeaveBothBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56};
constexpr std::uintptr_t kBringBack = 0x400440;       // (): build everyone on the list again
constexpr std::uint8_t kBringBackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
// Return addresses of the game's calls (after the CALL).
constexpr std::uint32_t kFromDriveLeave = 0x400B92;   // exe+0x400B20 -> exe+0x400990
constexpr std::uint32_t kFromSummonLeave = 0x3DE5B6;  // exe+0x3DE460 -> exe+0x400A30
constexpr std::uint32_t kFromDriveEnd[] = {0x40CC6B, 0x40CF96};  // exe+0x40CC00 / exe+0x40CF20 -> exe+0x400440
// The summon's end exe+0x3DE080 is a task callback (handed over by exe+0x3DE010) that ends with a tail jump to the
// bring-back, so no return address tells it apart: it is hooked, and its bring-back counts as our summon's end.
constexpr std::uintptr_t kSummonEnd = 0x3DE080;
constexpr std::uint8_t kSummonEndBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9};
constexpr std::uint32_t kPeerTimeoutMs = 1000;        // no word this long: the other player holds nothing
// Status (part 2).
constexpr std::uint32_t kStatusMagic = 0x5332484B;  // "KH2S"
constexpr std::uintptr_t kStats = 0x5C0;            // stat block: {cur, max, min} i32 each; HP stat 0, MP stat 32
constexpr std::uintptr_t kMpCur = 0x180, kMpMax = 0x184, kMpCharge = 0x1C0;  // in the block; charge f32 > 0 = recharging
constexpr int kStatMp = 32;
constexpr std::uint32_t kDownBit = 4;               // actor+0x9B8 bit 2: KO'd
constexpr std::uintptr_t kReviveFlags = 0x6C8;      // bit 14: an HP gain revives (exe+0x3D2EB0)
constexpr std::uintptr_t kRevive = 0x3C3C30;        // (actor, gain) -> scaled gain: clears the KO, motion 54 -> 55
constexpr std::uintptr_t kThink = 0x1B53B0;         // a companion's think (actor+0xE00, run by the brain exe+0x1B15F0)
constexpr std::uint8_t kThinkBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0x81, 0xB8, 0x09, 0x00, 0x00};

using PFN_Leave = void(__fastcall*)(std::uintptr_t, float);
using PFN_LeaveBoth = void(__fastcall*)(float);
using PFN_Void = void(__fastcall*)();
using PFN_Task = void(__fastcall*)(std::uintptr_t);
using PFN_IntOfActor = int(__fastcall*)(std::uintptr_t);
using PFN_IntOfInt = int(__fastcall*)(int);
using PFN_Live = std::uint8_t(__fastcall*)(std::uintptr_t);
using PFN_Revive = int(__fastcall*)(std::uintptr_t, int);

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;   // the sender's world and room
    std::uint8_t places;  // bit 0: Donald's place, bit 1: Goofy's, held away by the sender's own form or summon
    std::uint8_t form;    // for the log
    std::uint8_t summon;  // for the log
    std::uint8_t pad[3];
};
struct Member {
    std::uint32_t objId;
    std::int32_t hp, maxHp, mp, maxMp;
    float charge;  // MP recharge
    std::uint8_t down;
    std::uint8_t pad[3];
};
enum : std::uint8_t { kStatus = 0, kGain = 1 };
struct Status {  // host -> friend
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;
    std::uint8_t kind;  // kStatus
    std::uint8_t count;
    Member m[2];
};
struct Gain {  // friend -> host: an HP or MP gain on our copy of the host's companion
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;
    std::uint8_t kind;  // kGain
    std::uint8_t pad;
    std::uint32_t objId;
    std::int32_t delta, stat, react;
};
#pragma pack(pop)

bool g_on = false;
bool g_host = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;
PFN_Leave g_realLeave = nullptr;
PFN_LeaveBoth g_realLeaveBoth = nullptr;
PFN_Void g_realBringBack = nullptr;
PFN_Task g_realSummonEnd = nullptr;
bool g_summonEnding = false;    // inside our summon's end

std::uint8_t g_ours = 0;        // places our own form or summon sent away (bits as in Packet::places)
const char* g_oursWhy = "";
std::uint8_t g_ourSent = 0;     // places we sent away for the other player
bool g_ourCall = false;         // inside our own leave / bring-back call
Packet g_peer{};
std::uint32_t g_peerMs = 0;
std::uint8_t g_peerWas = 0;     // the other player's held places as last applied
const char* g_peerWhy = "Drive Form";  // what the other player holds them for (the log)
std::uint16_t g_room = 0;
std::uint32_t g_kept = 0;       // list entries the bring-back kept (log)
// Status (part 2)
using PFN_Think = void(__fastcall*)(std::uintptr_t);
PFN_Think g_realThink = nullptr;
bool g_statusOn = false;
std::uint32_t g_statusSeq = 0, g_gainSeq = 0, g_peerGainSeq = 0;
std::uint8_t g_downSent = 0;    // host: the KO bits last sent (bit per slot)
Status g_status{};              // friend: the host's latest
std::uint32_t g_statusMs = 0;
std::uint32_t g_skippedThinks = 0, g_kos = 0, g_revives = 0, g_gainsSent = 0, g_gainsApplied = 0, g_mpWrites = 0;
std::uint32_t g_gainsDropped = 0;  // friend: gains on our copies the host's status owns (ours, the game's own)

template <typename T>
T Read(std::uintptr_t a) {
    return *reinterpret_cast<const volatile T*>(a);
}

std::uint16_t Room() { return Read<std::uint16_t>(ExeBase() + kNow); }
std::uintptr_t Slot(int i) { return Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * i); }

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// A party member that is here and not already leaving.
bool Staying(std::uintptr_t a) {
    __try {
        return a && reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a) && !(Read<std::uint32_t>(a + kRemoveFlags) & 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The party place of a member (1 Donald's, 2 Goofy's), 0 for anything else.
int PlaceOf(std::uintptr_t a) {
    __try {
        int type = reinterpret_cast<PFN_IntOfActor>(ExeBase() + kCharType)(a);
        int place = reinterpret_cast<PFN_IntOfInt>(ExeBase() + kPlaceOf)(type);
        return place == 1 || place == 2 ? place : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::uint8_t Bit(int place) { return place == 1 || place == 2 ? static_cast<std::uint8_t>(1 << (place - 1)) : 0; }

const char* PlaceName(int place) { return place == 1 ? "Donald's place" : place == 2 ? "Goofy's place" : "another place"; }

std::uint8_t PeerPlaces() {
    bool fresh = g_peer.room == Room() && AvatarLinkNowMs() - g_peerMs < kPeerTimeoutMs;
    return fresh ? g_peer.places : 0;
}

std::uint32_t Rva(void* ret) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ret) - ExeBase()); }

// The list holds two entries before its count: an append while both are there (a member sent away whose place is
// already listed) writes over the count. After each leave: the places from before plus the new one, once each.
struct ListSnap {
    std::uint32_t e[kListMax];
    int n;
};
ListSnap TakeList() {
    ListSnap s{};
    s.n = Read<std::int32_t>(ExeBase() + kListCount);
    if (s.n < 0 || s.n > kListMax) s.n = 0;
    for (int i = 0; i < s.n; ++i) s.e[i] = Read<std::uint32_t>(ExeBase() + kList + 4 * i);
    return s;
}
std::uint32_t g_listRepairs = 0;
void RepairList(const ListSnap& before, const int* added, int adds) {
    ListSnap fixed = before;
    bool dropped = false;
    for (int k = 0; k < adds; ++k) {
        if (added[k] < 0) continue;
        bool have = false;
        for (int i = 0; i < fixed.n; ++i) have = have || fixed.e[i] == static_cast<std::uint32_t>(added[k]);
        if (have) continue;
        if (fixed.n < kListMax)
            fixed.e[fixed.n++] = static_cast<std::uint32_t>(added[k]);
        else
            dropped = true;
    }
    auto* list = reinterpret_cast<std::uint32_t*>(ExeBase() + kList);
    auto* count = reinterpret_cast<std::int32_t*>(ExeBase() + kListCount);
    bool same = *count == fixed.n;
    for (int i = 0; same && i < fixed.n; ++i) same = list[i] == fixed.e[i];
    if (same) return;
    for (int i = 0; i < fixed.n; ++i) list[i] = fixed.e[i];
    *count = fixed.n;
    if (g_listRepairs++ < 10)
        Log("companion sync: the bring-back list got a place twice%s: kept once (%d entries)", dropped ? " or a third" : "",
            fixed.n);
}

// The place the game puts on the list for a member (exe+0x3E31C0 of its type), -1 if unknown.
int RawPlace(std::uintptr_t a) {
    __try {
        return reinterpret_cast<PFN_IntOfInt>(ExeBase() + kPlaceOf)(reinterpret_cast<PFN_IntOfActor>(ExeBase() + kCharType)(a));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void __fastcall HookLeave(std::uintptr_t member, float fade) {
    std::uint32_t from = Rva(_ReturnAddress());
    if (!g_ourCall && from == kFromDriveLeave) {
        int place = PlaceOf(member);
        g_ours |= Bit(place);
        g_oursWhy = "our Drive Form";
        Log("companion sync: our Drive Form sends our %s (%s) away: the other game sends its own away too", Model(member),
            PlaceName(place));
    }
    ListSnap before = TakeList();
    int added = RawPlace(member);
    g_realLeave(member, fade);
    RepairList(before, &added, 1);
}

void __fastcall HookLeaveBoth(float fade) {
    std::uint32_t from = Rva(_ReturnAddress());
    if (!g_ourCall && from == kFromSummonLeave) {
        for (int i = 0; i < 2; ++i)
            if (Staying(Slot(i))) g_ours |= Bit(PlaceOf(Slot(i)));
        g_oursWhy = "our summon";
        Log("companion sync: our summon sends our party away (places 0x%X): the other game sends its own away too",
            g_ours);
    }
    ListSnap before = TakeList();
    int added[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        __try {
            if (Slot(i) && reinterpret_cast<PFN_Live>(ExeBase() + kLive)(Slot(i))) added[i] = RawPlace(Slot(i));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_realLeaveBoth(fade);
    RepairList(before, added, 2);
}

// The object id the party table builds for a place (what exe+0x400440 passes to the member build exe+0x3C2FC0).
std::uint32_t PartyObjId(int place) {
    __try {
        return place >= 0 && place < 4 ? Read<std::uint16_t>(ExeBase() + kPartyTable + 0x10 + 2 * place) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Who still holds a place on the list: the other player (form, summon, or its Limit unless that member is the
// Limit's partner), or our own form or summon unless this call is its end.
bool StillHeld(int place, bool ourEnd) {
    if (PeerPlaces() & Bit(place)) return true;
    if (LimitSyncPeerHolds(PartyObjId(place))) return true;
    return !ourEnd && (g_ours & Bit(place));
}

void __fastcall HookBringBack() {
    std::uint32_t from = Rva(_ReturnAddress());
    bool ourEnd = !g_ourCall && (from == kFromDriveEnd[0] || from == kFromDriveEnd[1] || g_summonEnding);
    auto* list = reinterpret_cast<std::uint32_t*>(ExeBase() + kList);
    auto* count = reinterpret_cast<std::int32_t*>(ExeBase() + kListCount);
    std::uint32_t keep[kListMax] = {};
    int kept = 0, n = *count;
    if (n < 0 || n > kListMax) {  // not the shape we know: leave it to the game
        g_realBringBack();
        return;
    }
    std::uint32_t build[kListMax] = {};
    int builds = 0;
    for (int i = 0; i < n; ++i) {
        int place = static_cast<int>(list[i]);
        if (StillHeld(place, ourEnd))
            keep[kept++] = list[i];
        else
            build[builds++] = list[i];
    }
    if (ourEnd) {
        Log("companion sync: %s ended: places 0x%X come back%s", g_oursWhy, g_ours,
            kept ? ", except those the other player still holds" : "");
        g_ours = 0;
    }
    if (kept) {
        if (g_kept++ < 20)
            for (int i = 0; i < kept; ++i)
                Log("companion sync: the bring-back keeps %s away: %s", PlaceName(static_cast<int>(keep[i])),
                    PeerPlaces() & Bit(static_cast<int>(keep[i])) ? "the other player's form or summon holds it"
                    : (g_ours & Bit(static_cast<int>(keep[i]))) ? "our own form or summon holds it"
                                                                : "the other player's Limit holds it");
        for (int i = 0; i < builds; ++i) list[i] = build[i];
        *count = builds;
    }
    g_realBringBack();
    for (int i = 0; i < kept; ++i) list[i] = keep[i];
    *count = kept;
}

void __fastcall HookSummonEnd(std::uintptr_t task) {
    g_summonEnding = true;
    g_realSummonEnd(task);
    g_summonEnding = false;
}

// ---- Status (part 2) ----
std::uint32_t ObjIdOf(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool Down(std::uintptr_t a) { return (Read<std::uint32_t>(a + kRemoveFlags) & kDownBit) != 0; }

// A member in the actor list (also KO'd): its stat block, else 0.
std::uintptr_t StatsOf(std::uintptr_t a) {
    __try {
        if (!a || !reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a)) return 0;
        return Read<std::uintptr_t>(a + kStats);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void __fastcall HookThink(std::uintptr_t self) {
    bool skip = g_statusOn && WorldSyncSkipAi(self);  // its pose and actions are the other game's
    if (skip)
        ++g_skippedThinks;
    else
        g_realThink(self);
    WorldCensusThinkTrace(self, skip);
}

// Host: our companions' status, every 6 frames and at once when a KO starts or ends.
void SendStatus() {
    Status p{kStatusMagic, kVersion, AvatarLinkBuild(), 0, Room(), kStatus, 0, {}};
    std::uint8_t down = 0;
    __try {
        for (int i = 0; i < 2; ++i) {
            std::uintptr_t a = Slot(i), st = StatsOf(a);
            if (!st) continue;
            Member& m = p.m[p.count++];
            m.objId = ObjIdOf(a);
            m.hp = Read<std::int32_t>(st);
            m.maxHp = Read<std::int32_t>(st + 4);
            m.mp = Read<std::int32_t>(st + kMpCur);
            m.maxMp = Read<std::int32_t>(st + kMpMax);
            m.charge = Read<float>(st + kMpCharge);
            m.down = Down(a) ? 1 : 0;
            if (m.down) down |= static_cast<std::uint8_t>(1 << i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (g_frame % 6 != 0 && down == g_downSent) return;
    if (down != g_downSent)
        Log("companion sync: our companions' KO now 0x%X (bit per party slot): sent to the friend at once", down);
    g_downSent = down;
    p.seq = ++g_statusSeq;
    AvatarLinkSendRaw(&p, static_cast<int>(sizeof(p)));
}

// Friend: our copy of a host's companion takes its status.
void ApplyStatus(std::uintptr_t a, const Member& m) {
    std::uintptr_t st = StatsOf(a);
    if (!st) return;
    const char* model = Model(a);
    bool down = Down(a);
    if (m.down && !down) {  // the game's HP change to 0: the KO (dead bit, timer); the host's motion stream shows it
        int hp = Read<std::int32_t>(st);
        int after = PuppetApplyHp(a, -(hp > 0 ? hp : 1), 0);
        ++g_kos;
        Log("companion sync: the host's %s is KO'd: ours too (HP %d -> %d, KO %s)", model, hp, after,
            Down(a) ? "yes" : "NO (the game didn't take it)");
    } else if (!m.down && down && m.hp > 0) {  // the game's revive: an HP gain on a KO'd member
        bool byGain = (Read<std::uint32_t>(a + kReviveFlags) & 0x4000) != 0;
        if (byGain) PuppetApplyHp(a, m.hp, 0);
        bool direct = Down(a);
        if (direct) reinterpret_cast<PFN_Revive>(ExeBase() + kRevive)(a, m.hp);
        *reinterpret_cast<std::int32_t*>(st) = m.hp;
        ++g_revives;
        Log("companion sync: the host's %s got up: ours too, HP %d (%s)%s", model, m.hp,
            direct ? "the game's revive call" : "an HP gain, the game's way", Down(a) ? ", STILL KO'd" : "");
    }
    auto* mp = reinterpret_cast<std::int32_t*>(st + kMpCur);
    if (mp[0] != m.mp || mp[1] != m.maxMp || Read<float>(st + kMpCharge) != m.charge) {
        mp[0] = m.mp;
        mp[1] = m.maxMp;
        *reinterpret_cast<float*>(st + kMpCharge) = m.charge;
        ++g_mpWrites;
    }
}

void ApplyHostStatus() {
    if (AvatarLinkNowMs() - g_statusMs > kPeerTimeoutMs || g_status.room != Room()) return;
    for (int i = 0; i < g_status.count && i < 2; ++i) {
        std::uintptr_t a = WorldSyncMirroredCompanion(g_status.m[i].objId);
        if (!a) continue;
        __try {
            ApplyStatus(a, g_status.m[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int logged = 0;
            if (logged++ < 10) Log("companion sync: applying the host's status to our companion failed (exception)");
        }
    }
}

// Host: the friend's gain, on our companion, the game's way (a KO'd one gets up).
void ApplyGain(const Gain& g) {
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!StatsOf(a) || ObjIdOf(a) != g.objId) continue;
        bool down = Down(a);
        int after = PuppetApplyStat(a, g.delta, g.stat, g.react);
        ++g_gainsApplied;
        Log("companion sync: the friend's %s +%d on our %s -> %d%s", g.stat == 0 ? "HP" : "MP", g.delta, Model(a), after,
            down ? (Down(a) ? " (KO'd: it stays down)" : " (it gets up)") : "");
        return;
    }
    Log("companion sync: the friend's gain (+%d, stat %d) for a companion we don't have (object 0x%X): dropped", g.delta,
        g.stat, g.objId);
}

void Send() {
    Packet p{kMagic, kVersion, AvatarLinkBuild(), ++g_seq, Room(), g_ours, Read<std::uint8_t>(ExeBase() + kForm),
             static_cast<std::uint8_t>(Read<std::uint32_t>(ExeBase() + kSummonState) == 2 ? 1 : 0), {}};
    AvatarLinkSendRaw(&p, static_cast<int>(sizeof(p)));
}

// The other player holds these places: our members in them leave (the game's own call, the form's fade).
void SendAway(std::uint8_t places, bool start) {
    if (PuppetOurLimitRunning()) {  // our Limit sent the others away; the one staying is its partner
        if (start) Log("companion sync: the other player's form or summon started while our Limit runs: our partner stays");
        return;
    }
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!Staying(a)) continue;
        int place = PlaceOf(a);
        if (!(places & Bit(place))) continue;
        __try {
            g_ourCall = true;
            g_realLeave(a, Read<float>(ExeBase() + kDriveFade));
            g_ourCall = false;
            g_ourSent |= Bit(place);
            Log("companion sync: our %s (%s) leaves: the other player's %s holds it%s", Model(a), PlaceName(place),
                g_peerWhy, start ? "" : " (it came back meanwhile)");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_ourCall = false;
            Log("companion sync: sending our %s away failed (exception)", Model(a));
        }
    }
}

// The other player let places go: build ours again (the bring-back keeps what anyone still holds).
void BringBack(std::uint8_t freed) {
    ListSnap now = TakeList();
    bool listed = false;
    for (int i = 0; i < now.n; ++i) listed = listed || (freed & Bit(static_cast<int>(now.e[i])));
    g_ourSent &= static_cast<std::uint8_t>(~freed);
    if (!listed) return;
    __try {
        g_ourCall = true;
        HookBringBack();
        g_ourCall = false;
        Log("companion sync: the other player's %s let places 0x%X go: ours brought back (the game's own call)", g_peerWhy,
            freed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_ourCall = false;
        Log("companion sync: bringing our members back failed (exception)");
    }
}

bool CodeMatches(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n, const char* name) {
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0) return true;
    Log("companion sync: %s at exe+0x%llX isn't the expected code: off", name, static_cast<unsigned long long>(rva));
    return false;
}

}  // namespace

void CompanionSyncInit(bool host) {
    g_host = host;
    if (EnvInt("KH2COOP_COMPANION_SYNC", 1) != 1) {
        Log("companion sync: off (COMPANION_SYNC=0)");
        return;
    }
    g_on = HookFunction(kLeave, kLeaveBytes, sizeof(kLeaveBytes), reinterpret_cast<void*>(HookLeave),
                        reinterpret_cast<void**>(&g_realLeave), "party member leave (Drive Form, Limit)") &&
           HookFunction(kLeaveBoth, kLeaveBothBytes, sizeof(kLeaveBothBytes), reinterpret_cast<void*>(HookLeaveBoth),
                        reinterpret_cast<void**>(&g_realLeaveBoth), "both party members leave (summon)") &&
           HookFunction(kBringBack, kBringBackBytes, sizeof(kBringBackBytes), reinterpret_cast<void*>(HookBringBack),
                        reinterpret_cast<void**>(&g_realBringBack), "party bring-back") &&
           HookFunction(kSummonEnd, kSummonEndBytes, sizeof(kSummonEndBytes), reinterpret_cast<void*>(HookSummonEnd),
                        reinterpret_cast<void**>(&g_realSummonEnd), "summon end");
    Log("companion sync: %s", g_on ? "on: a Drive Form or summon in either game sends the same companions away in both"
                                   : "OFF: a hook could not be installed");
    if (!g_on) return;
    g_statusOn = HookFunction(kThink, kThinkBytes, sizeof(kThinkBytes), reinterpret_cast<void*>(HookThink),
                              reinterpret_cast<void**>(&g_realThink), "companion think");
    Log("companion sync: status %s", !g_statusOn ? "OFF: the think hook could not be installed"
                                     : host       ? "on: our companions' HP, MP and KO go to the friend"
                                                  : "on: our companions take the host's HP, MP and KO; they don't think");
}

bool CompanionSyncHostOwns(std::uintptr_t actor) {
    if (!g_statusOn || g_host || !WorldSyncSkipAi(actor) || AvatarLinkNowMs() - g_statusMs > kPeerTimeoutMs) return false;
    ++g_gainsDropped;
    return true;
}

void CompanionSyncForwardGain(std::uintptr_t actor, int delta, int stat, int react) {
    if (!g_statusOn || g_host || delta <= 0 || (stat != 0 && stat != kStatMp) || !WorldSyncSkipAi(actor)) return;
    std::uint32_t objId = ObjIdOf(actor);
    if (!objId) return;
    Gain g{kStatusMagic, kVersion, AvatarLinkBuild(), ++g_gainSeq, Room(), kGain, 0, objId, delta, stat, react};
    AvatarLinkSendRaw(&g, static_cast<int>(sizeof(g)));
    if (g_gainsSent++ < 40)
        Log("companion sync: %s +%d on our %s: sent to the host's game (its companion takes it)", stat == 0 ? "HP" : "MP",
            delta, Model(actor));
}

void CompanionSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        std::uint16_t room = Room();
        if (room != g_room) {  // a room load builds the party again by itself
            if (g_ourSent || g_peerWas) Log("companion sync: room changed: places we sent away forgotten (0x%X)", g_ourSent);
            g_room = room;
            g_ourSent = 0;
            g_peerWas = 0;
        }
        if (g_frame % 6 == 0 || g_ours) Send();
        if (g_statusOn && g_host) SendStatus();
        if (g_statusOn && !g_host) ApplyHostStatus();
        if (g_statusOn && g_frame % 600 == 0 && (g_skippedThinks || g_kos || g_revives || g_gainsSent || g_gainsApplied))
            Log("companion sync: status so far: %u thinks skipped, %u KOs and %u get-ups copied, %u MP writes, gains "
                "%u dropped here, %u sent / %u applied", g_skippedThinks, g_kos, g_revives, g_mpWrites, g_gainsDropped,
                g_gainsSent, g_gainsApplied);
        std::uint8_t peer = PeerPlaces();
        if (peer & ~g_peerWas) g_peerWhy = g_peer.summon ? "summon" : "Drive Form";
        if (peer) SendAway(peer, (peer & ~g_peerWas) != 0);
        std::uint8_t freed = static_cast<std::uint8_t>(g_peerWas & ~peer);
        if (freed) BringBack(freed);
        if (peer != g_peerWas)
            Log("companion sync: the other player holds places 0x%X (form %u, summon %u)", peer, g_peer.form,
                g_peer.summon);
        g_peerWas = peer;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 10) Log("companion sync: exception in frame update");
    }
}

void CompanionSyncOnPacket(const char* buf, int n) {
    std::uint32_t magic = 0;
    if (n >= 4) std::memcpy(&magic, buf, 4);
    if (magic == kStatusMagic) {
        if (!g_statusOn) return;
        if (!g_host && n == static_cast<int>(sizeof(Status))) {
            Status p;
            std::memcpy(&p, buf, sizeof(p));
            if (p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kStatus) return;
            if (p.seq + 1000 < g_status.seq) g_status.seq = 0;  // the other game restarted
            if (p.seq <= g_status.seq) return;
            g_status = p;
            g_statusMs = AvatarLinkNowMs();
        } else if (g_host && n == static_cast<int>(sizeof(Gain))) {
            Gain g;
            std::memcpy(&g, buf, sizeof(g));
            if (g.version != kVersion || g.build != AvatarLinkBuild() || g.kind != kGain) return;
            if (g.seq + 1000 < g_peerGainSeq) g_peerGainSeq = 0;
            if (g.seq <= g_peerGainSeq) return;
            g_peerGainSeq = g.seq;
            if (g.room == Room()) ApplyGain(g);
        }
        return;
    }
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.seq + 1000 < g_peer.seq) g_peer.seq = 0;  // the other game restarted
    if (p.seq <= g_peer.seq) return;
    g_peer = p;
    g_peerMs = AvatarLinkNowMs();
}

}  // namespace kh2coop
