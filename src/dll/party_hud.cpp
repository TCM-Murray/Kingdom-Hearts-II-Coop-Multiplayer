// Party HUD widget for the Sora copy (TODO 3.11).
//
// The HUD's party widgets (dk::GAUGE_FRIEND, two of them) show the actors in a
// three-entry list: {player, friend gauge 0, friend gauge 1}. AI party members
// add themselves when they're set up (exe+0x3C27F0, exe+0x3C35C0); the copy is
// a player-class actor and never does, so its widget stays hidden. Only HUD
// code reads the list (Ghidra 2026-10-09), so we add the copy to the empty
// entry ourselves while it's alive and take it out again when it goes.
//
// Its HP bar would read the copy's stat block (actor+0x5C0), which is our own
// Sora's. With COPY_HUD_HP the widget's per-frame update runs with the copy's
// stat pointer swapped to a stand-in block holding the peer's HP and max HP
// (from their packets) and full MP; game code outside the widget never sees it.
//
// While the peer is downed the widget shows the knocked-out look (face group 2, like Goofy's KO): the
// widget's state is re-read every frame from actor+0x9B8 bit 2 ("dead", exe+0x18E940), so that bit is
// set on the copy for the length of the widget's update only, with HP 0 in the stand-in block.
//
// The copy is only listed while the game has the HUD up for this room (our own Sora in entry 0) and the
// copy has a stat block: in the Cerberus cutscene room (Olympus 06/07) the game leaves the whole list
// empty and the copy's +0x5C0 is null; listing it there crashed both PCs at exe+0x182481 (the widget's
// refresh exe+0x182400 reads **(actor+0x5C0); two-PC test 2026-10-09).
//
// A third friend gauge (COPY_HUD_THIRD, default on; for the extra copy, which stands beside Donald and Goofy, so
// both of the game's friend gauges are taken): the HUD's own gauge class built once more right after the game
// builds its HUD (exe+0x16C3A0), in our memory, with the game's set-up call at index 2. That index puts it one
// gauge height above friend gauge 1 (the set-up moves a gauge up by height x index). Its 'group' (the id the HUD
// shows and hides parts by, gauge+0x34 + 3 + index) would be 6, the boss HP bar's, so the set-up runs with the
// base +0x34 = 2 instead of 1: group 7, flag word 4, list entry 3. The game's list has 3 entries and its flag
// words follow it, so the list getter and the flag test / clear hand out our own entry and flag word for those
// indexes. The HUD's global hide / pause bits (set per group for 3..6 only) are copied from friend gauge 1 to
// group 7 every frame. Destroyed (no free: our memory) right before the game takes its HUD down (exe+0x16C2B0).
// The copy uses it only while both list entries are taken; when one frees up (a Drive Form sends a companion
// away) the copy moves into that entry and the third gauge hides, so the gauges stay packed.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "party_hud.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

// Leads (VERIFIED_OFFSETS.md, Ghidra 2026-10-09).
constexpr std::uintptr_t kHudParty = 0xABCFE8;  // u64[3] actors: player, friend gauge 0, friend gauge 1
constexpr std::uintptr_t kHudFlags = 0xABD000;  // u32[3]; bit 0 = entry changed (the widget re-reads it)
// Bit 4 = HP changed: ApplyStatDelta raises it (exe+0x168870 from exe+0x3D2F55); the widget then re-reads
// the HP and switches its face to the low-HP look or back (vtable +0x48, exe+0x182360). The copy's HP
// never changes in our game, so we raise it when the peer's HP does.
constexpr std::uint32_t kFlagHpChanged = 0x10;
constexpr std::uintptr_t kHudSet = 0x168850;    // (i, actor): entry + changed bit
constexpr std::uint8_t kHudSetBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x8E, 0x47, 0x95, 0x00};
constexpr std::uintptr_t kHudClear = 0x168970;  // (i): entry 0 + changed bit
constexpr std::uint8_t kHudClearBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x6E, 0x46, 0x95, 0x00};
// dk::GAUGE_FRIEND vtable +8: per-frame update. It first handles the entry's event bits (refresh,
// damage...), then fills the HP/MP bars from the shown actor (gauge+0x40) -> +0x5C0 stat block.
constexpr std::uintptr_t kGaugeUpdate = 0x182730;
constexpr std::uint8_t kGaugeUpdateBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kGaugeActor = 0x40;
// The player's own gauge (vtable exe+0x5B2100) +8 update (lead 'The player's own gauge, its KO face and the low-HP
// beep', disasm 2026-10-18): KO state from actor+0x9B8 bit 2 first, then the beep: dead -> stopped, else low HP ->
// started. Our downed Sora gets the bit for this update only (your pick: red face, no beep; the game sees it alive).
constexpr std::uintptr_t kPlayerGaugeUpdate = 0x181270;
constexpr std::uint8_t kPlayerGaugeUpdateBytes[] = {0x40, 0x53, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x48, 0x4C, 0x8B, 0xF1};
constexpr std::uintptr_t kActorStats = 0x5C0;   // -> stat block: u32 HP, u32 max HP, ...
constexpr std::uint32_t kStatBlock = 0x278;     // party stat slot stride
constexpr std::uint32_t kStatMp = 0x180, kStatMaxMp = 0x184;
constexpr std::uintptr_t kActorFlags = 0x9B8;   // u32; bit 2 = dead/KO (the widget's KO state)
constexpr std::uint32_t kFlagDead = 0x4;
// Face picture lookup (actor, &image, &layout): the actor's file (+0x928) entries named "face", type 0x18
// image and 0x19 2D layout (SEQD, used in place: offsets from its start). Called by the friend widget
// (exe+0x17E980, call at exe+0x17E9A7), the player gauge and party member setup. Sora's layout draws a
// 70x95 quad for the player gauge (Goofy's: 47x47), so the copy's friend widget gets a scaled copy.
constexpr std::uintptr_t kFaceLookup = 0x3B57B0;
constexpr std::uint8_t kFaceLookupBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::uintptr_t kFaceLookupFromFriendWidget = 0x17E9AC;  // return address
constexpr std::uint32_t kSeqdMagic = 0x44514553;                  // "SEQD"
// SEQD header (u32): +0x10 part count, +0x14 part offset (parts: s32 left, top, right, bottom, u32
// sprite), +0x20 animation count, +0x24 offset (0x90 each), +0x28 animation group count, +0x2C offset
// (0x24 each, last section: 4 groups end exactly at the file entry size 0x4EC for Sora and Goofy).
constexpr std::uint32_t kSeqdPart = 0x14, kSeqdAnimGroup = 0x24;

// Third gauge (Ghidra 2026-10-13, session 30; VERIFIED_OFFSETS 'A third party gauge').
constexpr std::uintptr_t kHudInit = 0x16C3A0;      // builds the HUD's gauges (from exe+0x168B30)
constexpr std::uint8_t kHudInitBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56};
constexpr std::uintptr_t kHudTeardown = 0x16C2B0;  // destroys them (from exe+0x168B10)
constexpr std::uint8_t kHudTeardownBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x0D, 0xA3, 0x0F, 0x95, 0x00};
constexpr std::uintptr_t kListGet = 0x1689A0;      // (i) -> list[i]
constexpr std::uint8_t kListGetBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x3E, 0x46, 0x95, 0x00, 0x48, 0x8B, 0x04, 0xC1, 0xC3};
constexpr std::uintptr_t kFlagTest = 0x1687F0;     // (i, bit) -> flags[i] bit set
constexpr std::uint8_t kFlagTestBytes[] = {0x48, 0x63, 0xC1, 0x44, 0x8B, 0xCA, 0x44, 0x8B, 0xC2, 0x41, 0x83, 0xE1, 0x1F};
constexpr std::uintptr_t kFlagClear = 0x168AE0;    // (i, bit): clear it
constexpr std::uint8_t kFlagClearBytes[] = {0x48, 0x63, 0xC1, 0x4C, 0x8D, 0x0D, 0x16, 0x45, 0x95, 0x00, 0x44, 0x8B, 0xC2};
constexpr std::uintptr_t kFriendCtor = 0x16B5A0;   // dk::GAUGE_FRIEND constructor (0x1710 bytes, zeroed first)
constexpr std::uint8_t kFriendCtorBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kFriendSetup = 0x181D20;  // (gauge, actor, index): parts, face, position
constexpr std::uint8_t kFriendSetupBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
constexpr std::uintptr_t kSetupBase = 0x181D41;    // mov r9d, 1: the set-up's gauge+0x34 (the group base)
constexpr std::uint8_t kSetupBaseBytes[] = {0x41, 0xB9, 0x01, 0x00, 0x00, 0x00};
constexpr std::uintptr_t kDecodeHandle = 0x4AD270; // u32 handle -> object (0 -> 0)
constexpr std::uint8_t kDecodeHandleBytes[] = {0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0xE9};
constexpr std::uintptr_t kHudSystem = 0xABCD20;    // -> HUD system: +8 first 2D element (elements: +8 next handle)
constexpr std::uintptr_t kFriendGauge1 = 0xABD258; // -> friend gauge 1 (list entry 2)
constexpr std::uint32_t kGaugeSize = 0x1710;
constexpr std::uintptr_t kElemNext = 0x8, kElemFlags = 0x10, kElemGroup = 0x14;
constexpr std::uint32_t kElemHudBits = 0x2 | 0x100;  // set per group by the HUD's hide / pause calls (exe+0x166BD0 / 0x167310)
constexpr int kThirdIndex = 2;                          // position: one height above friend gauge 1 (index 1)
constexpr int kThirdBase = 2;                           // gauge+0x34 (the game's friend gauges: 1)
constexpr int kThirdList = kThirdIndex + 1;             // 3: the list entry its refresh reads
constexpr int kThirdFlags = kThirdBase + kThirdIndex;   // 4: its flag word
constexpr int kThirdGroup = kThirdBase + 3 + kThirdIndex;  // 7

using PFN_HudSet = void(__fastcall*)(int index, std::uintptr_t actor);
using PFN_HudClear = void(__fastcall*)(int index);
using PFN_GaugeUpdate = void(__fastcall*)(void* gauge);
using PFN_FaceLookup = std::uint8_t(__fastcall*)(std::uintptr_t actor, void** image, void** layout);
using PFN_Void = void(__fastcall*)();
using PFN_ListGet = std::uintptr_t(__fastcall*)(int index);
using PFN_FlagTest = std::uint8_t(__fastcall*)(int index, int bit);
using PFN_FlagClear = void(__fastcall*)(int index, int bit);
using PFN_FriendCtor = void*(__fastcall*)(void* gauge);
using PFN_FriendSetup = void(__fastcall*)(void* gauge, std::uintptr_t actor, int index);
using PFN_Dtor = void*(__fastcall*)(void* gauge, unsigned flags);
using PFN_Decode = std::uintptr_t(__fastcall*)(std::uint32_t handle);

bool g_on = false, g_hpOn = false;
PFN_HudSet g_hudSet = nullptr;
PFN_HudClear g_hudClear = nullptr;
PFN_GaugeUpdate g_realGaugeUpdate = nullptr;
std::uintptr_t g_listed = 0;  // the copy we put in the list (0 = none)
std::uintptr_t g_lastSeen[3] = {};
bool g_noRoomLogged = false;
int g_replacedLogs = 0;
alignas(16) std::uint8_t g_stand[kStatBlock];  // stand-in stat block for the copy's widget
std::uint32_t g_hpFrames = 0;
std::int32_t g_peerHp = -1;
int g_peerDowned = -1;          // the downed state the copy's widget last showed (-1 = none yet)
PFN_FaceLookup g_realFaceLookup = nullptr;
int g_faceScale = 50;                              // percent of Sora's own face size
alignas(16) std::uint8_t g_smallFace[0x1000];      // the copy's scaled face layout (the widget keeps using it)
std::uint32_t g_faceSwaps = 0;  // the peer HP the copy's widget last showed

PFN_Void g_realHudInit = nullptr, g_realHudTeardown = nullptr;
PFN_ListGet g_realListGet = nullptr;
PFN_FlagTest g_realFlagTest = nullptr;
PFN_FlagClear g_realFlagClear = nullptr;
alignas(16) std::uint8_t g_third[kGaugeSize];  // the third gauge
void* g_thirdVtable[12];                       // its vtable: the friend gauge's, update (+8) ours
bool g_thirdBuilt = false;
std::uintptr_t g_thirdActor = 0;   // whom it should show (PartyHudFrame); 0 = nobody, hidden
std::uintptr_t g_thirdGiven = 0;   // what its refresh was last handed
volatile std::uint32_t g_thirdFlagWord = 0;  // its list flags (bit 0 = refresh, bit 4 = HP changed)
std::uint32_t g_thirdHudBits = ~0u;          // the hide / pause bits last copied to its parts
int g_hudInits = 0, g_hudTeardowns = 0;

volatile std::uintptr_t* Party() { return reinterpret_cast<volatile std::uintptr_t*>(ExeBase() + kHudParty); }

const char* Who(std::uintptr_t a, std::uintptr_t copy) {
    if (!a) return "empty";
    if (a == copy || a == g_listed) return "Sora copy";
    if (a == *reinterpret_cast<const volatile std::uintptr_t*>(ExeBase() + 0x2A171C8)) return "our Sora";
    return "party member";
}

void LogListChanges(std::uintptr_t copy) {
    volatile std::uintptr_t* party = Party();
    bool changed = false;
    for (int i = 0; i < 3; ++i) changed |= party[i] != g_lastSeen[i];
    if (!changed) return;
    const volatile std::uint32_t* flags = reinterpret_cast<const volatile std::uint32_t*>(ExeBase() + kHudFlags);
    std::uintptr_t base = ExeBase();
    auto rva = [base](std::uintptr_t a) { return static_cast<unsigned long long>(a ? a - base : 0); };
    Log("hud: party list = [0] %s exe+0x%llX, [1] %s exe+0x%llX, [2] %s exe+0x%llX (flags 0x%X 0x%X 0x%X)",
        Who(party[0], copy), rva(party[0]), Who(party[1], copy), rva(party[1]), Who(party[2], copy), rva(party[2]),
        flags[0], flags[1], flags[2]);
    for (int i = 0; i < 3; ++i) g_lastSeen[i] = party[i];
}

bool HasStats(std::uintptr_t actor) {
    return *reinterpret_cast<const volatile std::uintptr_t*>(actor + kActorStats) != 0;
}

// The friend gauge update, with the copy's stand-in stat block while it shows the copy. `real` = the game's
// update (MinHook's trampoline once hooked).
void GaugeUpdateFor(void* gauge, PFN_GaugeUpdate real) {
    std::uintptr_t actor = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(gauge) + kGaugeActor);
    if (!actor || actor != g_listed) {
        real(gauge);
        return;
    }
    auto* slot = reinterpret_cast<std::uintptr_t*>(actor + kActorStats);
    std::uintptr_t own = *slot;
    if (!own) return;  // no stat block (cutscene room): the widget would read through null
    PeerPose peer;
    if (!g_hpOn || !AvatarLinkPeerNow(peer) || peer.maxHp <= 0) {
        real(gauge);
        return;
    }
    std::memcpy(g_stand, reinterpret_cast<const void*>(own), sizeof(g_stand));
    std::int32_t hp = peer.hp < 0 ? 0 : (peer.hp > peer.maxHp ? peer.maxHp : peer.hp);
    if (peer.downed) hp = 0;  // KO look: empty ring
    std::memcpy(g_stand + 0, &hp, 4);
    std::memcpy(g_stand + 4, &peer.maxHp, 4);
    std::memcpy(g_stand + kStatMp, g_stand + kStatMaxMp, 4);  // MP: shown full for now (user, 2026-10-09)
    if (g_hpFrames++ == 0) Log("hud: the copy's widget shows the peer's HP (%d/%d)", hp, peer.maxHp);
    auto* flags = reinterpret_cast<volatile std::uint32_t*>(actor + kActorFlags);
    bool wasDead = (*flags & kFlagDead) != 0;
    *slot = reinterpret_cast<std::uintptr_t>(g_stand);
    if (peer.downed) *flags |= kFlagDead;
    __try {
        real(gauge);
    } __finally {
        *slot = own;
        if (peer.downed && !wasDead) *flags &= ~kFlagDead;
    }
}

void __fastcall HookGaugeUpdate(void* gauge) { GaugeUpdateFor(gauge, g_realGaugeUpdate); }

PFN_GaugeUpdate g_realPlayerGaugeUpdate = nullptr;
bool g_downFace = false;

void __fastcall HookPlayerGaugeUpdate(void* gauge) {
    std::uintptr_t actor = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(gauge) + kGaugeActor);
    bool lying = actor && DownedIsLying();
    if (lying != g_downFace) {
        g_downFace = lying;
        // The face is picked again on the HP-changed event (as for the copy's widget): raise it for entry 0.
        reinterpret_cast<volatile std::uint32_t*>(ExeBase() + kHudFlags)[0] |= kFlagHpChanged;
        Log("hud: our gauge %s", lying ? "shows the KO face while we are down (no low-HP beep)" : "is back to normal");
    }
    if (!lying) {
        g_realPlayerGaugeUpdate(gauge);
        return;
    }
    auto* flags = reinterpret_cast<volatile std::uint32_t*>(actor + kActorFlags);
    bool wasDead = (*flags & kFlagDead) != 0;
    *flags |= kFlagDead;
    __try {
        g_realPlayerGaugeUpdate(gauge);
    } __finally {
        if (!wasDead) *flags &= ~kFlagDead;
    }
}

// ---- Third gauge ----

std::uintptr_t ThirdWanted() {
    return CoopActive() && g_thirdActor && HasStats(g_thirdActor) ? g_thirdActor : 0;
}

std::uintptr_t __fastcall HookListGet(int index) {
    if (index == kThirdList && g_thirdBuilt) return g_thirdGiven = ThirdWanted();
    return g_realListGet(index);
}

std::uint8_t __fastcall HookFlagTest(int index, int bit) {
    if (index == kThirdFlags && g_thirdBuilt && bit >= 0 && bit < 32) return (g_thirdFlagWord >> bit) & 1;
    return g_realFlagTest(index, bit);
}

void __fastcall HookFlagClear(int index, int bit) {
    if (index == kThirdFlags && g_thirdBuilt && bit >= 0 && bit < 32) {
        g_thirdFlagWord &= ~(1u << bit);
        return;
    }
    g_realFlagClear(index, bit);
}

// The HUD hides / pauses its gauges per group (3..6); group 7 follows friend gauge 1.
void CopyHudBits() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t g1 = *reinterpret_cast<const std::uintptr_t*>(exe + kFriendGauge1);
    std::uintptr_t hud = *reinterpret_cast<const std::uintptr_t*>(exe + kHudSystem);
    if (!g1 || !hud) return;
    std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(g1 + kElemFlags) & kElemHudBits;
    if (bits == g_thirdHudBits) return;
    auto decode = reinterpret_cast<PFN_Decode>(exe + kDecodeHandle);
    int parts = 0;
    std::uintptr_t e = *reinterpret_cast<const std::uintptr_t*>(hud + 8);
    for (int n = 0; e && n < 4096; ++n) {
        if (*reinterpret_cast<const std::int32_t*>(e + kElemGroup) == kThirdGroup) {
            auto* f = reinterpret_cast<std::uint32_t*>(e + kElemFlags);
            *f = (*f & ~kElemHudBits) | bits;
            ++parts;
        }
        e = decode(*reinterpret_cast<const std::uint32_t*>(e + kElemNext));
    }
    static int changes = 0;
    if (g_thirdHudBits == ~0u || parts == 0 || changes++ < 40)
        Log("hud: third gauge: %d parts in group %d, HUD bits 0x%X (from friend gauge 1)", parts, kThirdGroup, bits);
    g_thirdHudBits = bits;
}

// Its update (vtable +8, every frame from the HUD's task for it): never with an actor it shouldn't show (a
// stale one may have lost its stat block); a change of actor goes through the gauge's own refresh event.
void __fastcall ThirdGaugeUpdate(void* gauge) {
    __try {
        CopyHudBits();
        std::uintptr_t want = ThirdWanted();
        auto* shown = reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(gauge) + kGaugeActor);
        if (*shown != want || g_thirdGiven != want) {
            *shown = want;
            g_thirdFlagWord |= 1;
        }
        GaugeUpdateFor(gauge, g_realGaugeUpdate ? g_realGaugeUpdate
                                                : reinterpret_cast<PFN_GaugeUpdate>(ExeBase() + kGaugeUpdate));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool SetSetupBase(std::uint32_t value) {
    auto* imm = reinterpret_cast<std::uint32_t*>(ExeBase() + kSetupBase + 2);
    DWORD old = 0;
    if (!VirtualProtect(imm, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *reinterpret_cast<volatile std::uint32_t*>(imm) = value;
    VirtualProtect(imm, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), imm, 4);
    return true;
}

void BuildThird() {
    std::uintptr_t exe = ExeBase();
    __try {
        std::memset(g_third, 0, sizeof(g_third));
        reinterpret_cast<PFN_FriendCtor>(exe + kFriendCtor)(g_third);
        std::memcpy(g_thirdVtable, *reinterpret_cast<void* const*>(g_third), sizeof(g_thirdVtable));
        g_thirdVtable[1] = reinterpret_cast<void*>(ThirdGaugeUpdate);
        *reinterpret_cast<void**>(g_third) = g_thirdVtable;
        if (!SetSetupBase(kThirdBase)) {
            Log("hud: third gauge: the set-up call couldn't be adjusted; none this time");
            return;
        }
        reinterpret_cast<PFN_FriendSetup>(exe + kFriendSetup)(g_third, 0, kThirdIndex);
        SetSetupBase(1);
        g_thirdBuilt = true;
        g_thirdHudBits = ~0u;
        Log("hud: third gauge built (HUD build %d): index %d, group %d, offset (%d, %d)", g_hudInits, kThirdIndex,
            *reinterpret_cast<const std::int32_t*>(g_third + 0x14),
            *reinterpret_cast<const std::int32_t*>(g_third + 0x370 + 0x1C0),
            *reinterpret_cast<const std::int32_t*>(g_third + 0x370 + 0x1C4));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        SetSetupBase(1);
        g_thirdBuilt = false;
        Log("hud: third gauge: building it faulted; none this time");
    }
}

void __fastcall HookHudInit() {
    g_realHudInit();
    ++g_hudInits;
    g_thirdBuilt = false;
    g_thirdActor = g_thirdGiven = 0;
    g_thirdFlagWord = 0;
    if (g_hudInits <= 20)
        Log("hud: the game built its HUD (%d)%s", g_hudInits, CoopActive() ? "" : ", co-op off: no third gauge");
    if (CoopActive()) BuildThird();
}

void __fastcall HookHudTeardown() {
    ++g_hudTeardowns;
    if (g_thirdBuilt) {
        g_thirdBuilt = false;
        __try {
            reinterpret_cast<PFN_Dtor>(g_thirdVtable[3])(g_third, 0);  // no free bit: our memory
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_thirdActor = g_thirdGiven = 0;
    if (g_hudTeardowns <= 20) Log("hud: the game takes its HUD down (%d)", g_hudTeardowns);
    g_realHudTeardown();
}

void ThirdInit() {
    if (EnvInt("KH2COOP_COPY_HUD_THIRD", 1) != 1) return;
    std::uintptr_t exe = ExeBase();
    struct Check {
        std::uintptr_t rva;
        const std::uint8_t* bytes;
        std::size_t n;
    };
    const Check checks[] = {
        {kFriendCtor, kFriendCtorBytes, sizeof(kFriendCtorBytes)},
        {kFriendSetup, kFriendSetupBytes, sizeof(kFriendSetupBytes)},
        {kSetupBase, kSetupBaseBytes, sizeof(kSetupBaseBytes)},
        {kDecodeHandle, kDecodeHandleBytes, sizeof(kDecodeHandleBytes)},
    };
    for (const auto& c : checks) {
        if (std::memcmp(reinterpret_cast<void*>(exe + c.rva), c.bytes, c.n) != 0) {
            Log("hud: third gauge: exe+0x%llX doesn't match this build; no third gauge",
                static_cast<unsigned long long>(c.rva));
            return;
        }
    }
    // Always on (not only during co-op): a gauge built during co-op must be answered and taken down after.
    bool ok = HookFunctionAlways(kListGet, kListGetBytes, sizeof(kListGetBytes), reinterpret_cast<void*>(HookListGet),
                                 reinterpret_cast<void**>(&g_realListGet), "HUD party list getter (third gauge)") &&
              HookFunctionAlways(kFlagTest, kFlagTestBytes, sizeof(kFlagTestBytes), reinterpret_cast<void*>(HookFlagTest),
                                 reinterpret_cast<void**>(&g_realFlagTest), "HUD party flag test (third gauge)") &&
              HookFunctionAlways(kFlagClear, kFlagClearBytes, sizeof(kFlagClearBytes),
                                 reinterpret_cast<void*>(HookFlagClear), reinterpret_cast<void**>(&g_realFlagClear),
                                 "HUD party flag clear (third gauge)") &&
              HookFunctionAlways(kHudTeardown, kHudTeardownBytes, sizeof(kHudTeardownBytes),
                                 reinterpret_cast<void*>(HookHudTeardown), reinterpret_cast<void**>(&g_realHudTeardown),
                                 "HUD teardown (third gauge)") &&
              HookFunctionAlways(kHudInit, kHudInitBytes, sizeof(kHudInitBytes), reinterpret_cast<void*>(HookHudInit),
                                 reinterpret_cast<void**>(&g_realHudInit), "HUD build (third gauge)");
    Log("hud: third party gauge %s", ok ? "on (shows the Sora copy when both friend gauges are taken)"
                                        : "off: a hook failed");
}

// Copies a SEQD layout into g_smallFace with every part's quad scaled by g_faceScale (around the
// layout's origin, which the widget places in its ring). Null if it doesn't look like one.
void* ScaledFace(const std::uint8_t* src) {
    std::uint32_t h[12];
    std::memcpy(h, src, sizeof(h));
    if (h[0] != kSeqdMagic) return nullptr;
    std::uint64_t size = static_cast<std::uint64_t>(h[11]) + static_cast<std::uint64_t>(h[10]) * kSeqdAnimGroup;
    std::uint64_t partsEnd = static_cast<std::uint64_t>(h[5]) + static_cast<std::uint64_t>(h[4]) * kSeqdPart;
    if (size > sizeof(g_smallFace) || partsEnd > size) return nullptr;
    std::memcpy(g_smallFace, src, static_cast<std::size_t>(size));
    for (std::uint32_t i = 0; i < h[4]; ++i) {
        auto* quad = reinterpret_cast<std::int32_t*>(g_smallFace + h[5] + i * kSeqdPart);
        for (int k = 0; k < 4; ++k) quad[k] = quad[k] * g_faceScale / 100;
    }
    return g_smallFace;
}

std::uint8_t __fastcall HookFaceLookup(std::uintptr_t actor, void** image, void** layout) {
    std::uint8_t found = g_realFaceLookup(actor, image, layout);
    auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase();
    if (found && actor && actor == g_listed && from == kFaceLookupFromFriendWidget && *layout) {
        __try {
            if (void* small = ScaledFace(static_cast<const std::uint8_t*>(*layout))) {
                *layout = small;
                if (g_faceSwaps++ < 5) Log("hud: the copy's widget face drawn at %d%% size", g_faceScale);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return found;
}

}  // namespace

void PartyHudInit() {
    if (EnvInt("KH2COOP_DOWNED_FACE", 1) == 1)
        HookFunction(kPlayerGaugeUpdate, kPlayerGaugeUpdateBytes, sizeof(kPlayerGaugeUpdateBytes),
                     reinterpret_cast<void*>(HookPlayerGaugeUpdate), reinterpret_cast<void**>(&g_realPlayerGaugeUpdate),
                     "our gauge update (KO face, no low-HP beep while down)");
    if (EnvInt("KH2COOP_COPY_HUD", 1) != 1) return;
    std::uintptr_t exe = ExeBase();
    if (std::memcmp(reinterpret_cast<void*>(exe + kHudSet), kHudSetBytes, sizeof(kHudSetBytes)) != 0 ||
        std::memcmp(reinterpret_cast<void*>(exe + kHudClear), kHudClearBytes, sizeof(kHudClearBytes)) != 0) {
        Log("hud: party list functions don't match this build; the copy gets no widget");
        return;
    }
    g_hudSet = reinterpret_cast<PFN_HudSet>(exe + kHudSet);
    g_hudClear = reinterpret_cast<PFN_HudClear>(exe + kHudClear);
    g_on = true;
    if (EnvInt("KH2COOP_COPY_HUD_HP", 1) == 1)
        g_hpOn = HookFunction(kGaugeUpdate, kGaugeUpdateBytes, sizeof(kGaugeUpdateBytes),
                              reinterpret_cast<void*>(HookGaugeUpdate), reinterpret_cast<void**>(&g_realGaugeUpdate),
                              "party widget update (copy shows the peer's HP)");
    g_faceScale = EnvInt("KH2COOP_COPY_HUD_FACE_SCALE", 50);
    if (g_faceScale > 0 && g_faceScale < 100)
        HookFunction(kFaceLookup, kFaceLookupBytes, sizeof(kFaceLookupBytes), reinterpret_cast<void*>(HookFaceLookup),
                     reinterpret_cast<void**>(&g_realFaceLookup), "face picture lookup (smaller face for the copy)");
    Log("hud: the Sora copy gets a party widget%s", g_hpOn ? " with the peer's HP" : " (our own HP: COPY_HUD_HP off)");
    ThirdInit();
}

void PartyHudFrame() {
    if (!g_on) return;
    __try {
        std::uintptr_t copy = PuppetCloneActor();  // 0 while loading, after a room change, or with no copy
        volatile std::uintptr_t* party = Party();
        if (g_listed && copy != g_listed) {
            for (int i = 1; i < 3; ++i) {
                if (party[i] == g_listed) {
                    g_hudClear(i);
                    Log("hud: Sora copy taken out of the party list (entry %d)", i);
                }
            }
            g_listed = 0;
            g_thirdActor = 0;
            g_hpFrames = 0;
            g_peerHp = -1;
            g_peerDowned = -1;
        }
        // The HUD is up for this room once the game has listed our own Sora; the copy needs a stat block.
        bool hudUp = party[0] != 0, ready = copy && hudUp && HasStats(copy);
        int at = !copy ? -1 : party[1] == copy ? 1 : party[2] == copy ? 2 : -1;
        if (at > 0 && !ready) {
            g_hudClear(at);
            Log("hud: Sora copy taken out of the party list (entry %d): %s", at,
                !hudUp ? "the HUD isn't up in this room" : "the copy has no stat block");
            g_listed = 0;
            at = -1;
        }
        if (!ready) {
            if (g_thirdActor) {
                Log("hud: Sora copy taken out of the third gauge: %s", !copy ? "no copy"
                    : !hudUp ? "the HUD isn't up in this room" : "the copy has no stat block");
                g_thirdActor = 0;
                g_listed = 0;
            }
            LogListChanges(copy);
            return;
        }
        if (at > 0) {
            g_listed = copy;
            g_thirdActor = 0;  // in one of the game's gauges now
            PeerPose peer;
            if (g_hpOn && AvatarLinkPeerNow(peer) && (peer.hp != g_peerHp || static_cast<int>(peer.downed) != g_peerDowned)) {
                g_peerHp = peer.hp;
                g_peerDowned = peer.downed;
                reinterpret_cast<volatile std::uint32_t*>(ExeBase() + kHudFlags)[at] |= kFlagHpChanged;
            }
            LogListChanges(copy);
            return;
        }
        bool inThird = g_thirdBuilt && g_thirdActor == copy;
        if (g_listed == copy && !inThird && g_replacedLogs++ < 5)
            Log("hud: the game replaced the Sora copy in the party list; adding it again");
        g_listed = 0;
        {
            int entry = party[2] == 0 ? 2 : (party[1] == 0 ? 1 : -1);
            if (entry > 0) {
                g_thirdActor = 0;
                g_hudSet(entry, copy);
                g_listed = copy;
                g_noRoomLogged = false;
                Log("hud: Sora copy (actor exe+0x%llX) %s the party list, entry %d",
                    static_cast<unsigned long long>(copy - ExeBase()), inThird ? "moved from the third gauge into" : "added to",
                    entry);
            } else if (g_thirdBuilt) {
                // Both entries taken (the extra copy beside Donald and Goofy): the third gauge, above them.
                if (!inThird)
                    Log("hud: both friend gauges taken: the Sora copy (actor exe+0x%llX) shown in the third gauge",
                        static_cast<unsigned long long>(copy - ExeBase()));
                g_thirdActor = copy;
                g_listed = copy;
                g_noRoomLogged = false;
                PeerPose peer;
                if (g_hpOn && AvatarLinkPeerNow(peer) &&
                    (peer.hp != g_peerHp || static_cast<int>(peer.downed) != g_peerDowned)) {
                    g_peerHp = peer.hp;
                    g_peerDowned = peer.downed;
                    g_thirdFlagWord |= kFlagHpChanged;
                }
            } else {
                if (!g_noRoomLogged) Log("hud: no empty party list entry for the Sora copy");
                g_noRoomLogged = true;
            }
        }
        LogListChanges(copy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace kh2coop
