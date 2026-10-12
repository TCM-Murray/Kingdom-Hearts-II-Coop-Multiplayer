// Forced-solo rooms keep the other player's Sora (TODO 5.5, your pick session 27: both fight together;
// only Donald and Goofy leave).
//
// How the game makes the party (VERIFIED_OFFSETS rows 'Party members come from spawn-file placeholders',
// '`Party` command rewrites the world party entry'): a room's spawn groups list placeholder objects,
// 0x236 = the player, 0x237 = active member 1, 0x238 = active member 2; each placeholder becomes whoever
// sits in that slot of the world party entry (save+0x3534). Our copy is a 0x00 (the playable character) in
// one of those slots (puppet.cpp), so it comes out of a member placeholder like Donald would. Forced-solo
// programs (Bailey HB 04/08 program 52 and ~90 other fights) spawn the player with no member placeholder
// at all, so no copy is made in either game: the players fight apart and enemies only see the local Sora.
//
// Here: our hook on the game's "spawn one entry of a group" exe+0x3FE590(record, group) watches the
// player's spawn. If the player's group lists no member placeholder, we spawn the copy's placeholder right
// after the player, in the same spawn pass, through the same function, from a copy of the player's record
// (same spot: the spawn step moves the two apart). Not later: one frame later (first build, bench session
// 27) the game took the new Sora as the player and left ours standing. In 11 groups of the game the members
// sit in another group than the player; if such a member turns out to be the copy's slot, it is skipped, so
// there is never a second copy.

#include "solo_copy.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSpawnEntry = 0x3FE590;  // (record, group) -> actor or 0
constexpr std::uint8_t kSpawnEntryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                             0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uintptr_t kNow = 0x717008;    // u8 world, room, door
constexpr std::uintptr_t kSave = 0x9A98B0;   // save data in memory ("KH2J")
constexpr std::uintptr_t kPartyTable = 0x3534;  // + 4*world: {player, active 1, active 2, bench}
constexpr std::uint8_t kPartyPlayable = 0x00;
constexpr std::uint32_t kObjPlayer = 0x236, kObjMember1 = 0x237, kObjMember2 = 0x238;
constexpr std::uint32_t kRecordSize = 0x40;
// EXPERIMENT (KH2COOP_EXTRA_COPY=1, puppet.cpp): see AddExtraCopy.
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;
constexpr std::uintptr_t kListHead = 0x2A171C8;
// Character -> object table (u16 each, exe+0x3E2E50 reads it): what member placeholder 0x237 resolves to when the
// party slot holds character 0x00 (exe+0x3C32E0), i.e. how today's copy is built. Building from the PLAYER
// placeholder 0x236 instead made the new Sora the head of the actor list (bench session 28, 03:13:48).
constexpr std::uintptr_t kCharObjects = 0x2A25300;

using PFN_SpawnEntry = std::uintptr_t(__fastcall*)(std::uint32_t*, std::uintptr_t);

PFN_SpawnEntry g_real = nullptr;
// Our record for the copy stays valid for the room: the actor may keep a pointer to it.
alignas(16) std::uint8_t g_copyRecord[kRecordSize] = {};
std::uint32_t g_injected = 0;  // the placeholder we added in this room's spawn pass (0 = none)
std::uint32_t g_spawned = 0, g_skipped = 0, g_logs = 0;
alignas(16) std::uint8_t g_extraRecord[kRecordSize] = {};  // EXPERIMENT: the copy's record beside the party
std::uint32_t g_extraSpawned = 0;

bool IsMember(std::uint32_t object) {
    object &= 0xFFFFFFF;
    // 0x319/0x31A/0x3EE also resolve to party members (exe+0x3DFEB0); a group using them has its party.
    return object == kObjMember1 || object == kObjMember2 || object == 0x319 || object == 0x31A || object == 0x3EE;
}

// The member placeholder whose slot holds our copy in the current world's party entry, or 0.
std::uint32_t CopyPlaceholder() {
    std::uintptr_t exe = ExeBase();
    if (std::memcmp(reinterpret_cast<const void*>(exe + kSave), "KH2J", 4) != 0) return 0;
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow);
    if (world >= 32) return 0;
    const auto* entry = reinterpret_cast<const std::uint8_t*>(exe + kSave + kPartyTable + 4 * world);
    if (entry[1] == kPartyPlayable) return kObjMember1;
    if (entry[2] == kPartyPlayable) return kObjMember2;
    return 0;
}

// The group runtime (exe+0x3FF550's argument): +8 -> the group header (u16 entity count at +4),
// +0x30 -> its 0x40-byte entity records.
bool GroupHasMember(std::uintptr_t group) {
    auto header = *reinterpret_cast<const std::uint8_t* const*>(group + 8);
    auto records = *reinterpret_cast<const std::uint8_t* const*>(group + 0x30);
    if (!header || !records) return true;  // unknown layout: leave the room as the game makes it
    int count = *reinterpret_cast<const std::uint16_t*>(header + 4);
    for (int i = 0; i < count && i < 256; ++i)
        if (IsMember(*reinterpret_cast<const std::uint32_t*>(records + kRecordSize * i))) return true;
    return false;
}

void AddCopy(const std::uint32_t* playerRecord, std::uintptr_t group) {
    std::uintptr_t exe = ExeBase();
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow);
    std::uint8_t room = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow + 1);
    std::uint16_t btl = *reinterpret_cast<const volatile std::uint16_t*>(exe + kNow + 6);
    std::uint32_t placeholder = CopyPlaceholder();
    if (!placeholder) {
        if (g_logs++ < 50)
            Log("solo copy: 0x%02X/0x%02X (battle program %u) spawns Sora alone, but the party entry has no Sora copy; "
                "nothing to add", world, room, btl);
        return;
    }
    std::memcpy(g_copyRecord, playerRecord, kRecordSize);
    auto* rec = reinterpret_cast<std::uint32_t*>(g_copyRecord);
    rec[0] = (rec[0] & 0xF0000000u) | placeholder;
    const auto* pos = reinterpret_cast<const float*>(g_copyRecord + 4);
    std::uintptr_t actor = g_real(rec, group);
    g_injected = placeholder;
    ++g_spawned;
    Log("solo copy: 0x%02X/0x%02X (battle program %u) spawns Sora with no party member: added the Sora copy "
        "(member %d placeholder) at (%.0f, %.0f, %.0f) right after Sora: %s", world, room, btl,
        placeholder == kObjMember1 ? 1 : 2, pos[0], pos[1], pos[2], actor ? "made" : "the game made nothing");
}

// EXPERIMENT (KH2COOP_EXTRA_COPY=1): the Sora copy is not in the party table, so the room spawns the party as
// the game has it; the copy is spawned in the same pass just BEFORE our Sora, from a copy of the PLAYER record
// with character 0's object (what today's copy is built from). Order matters (Ghidra + bench session 28): every
// Sora the constructor exe+0x3A7A40 builds takes the player pointer, the camera target exe+0x718CB0,
// exe+0x2A10420 and the player controller exe+0x2A10620, and goes in front of the other Soras in the actor list
// (exe+0x3BFBC0 sorts by actor+0xB91), so the Sora built last is the player. Built after ours, the copy took the
// list head and the HUD's player entry (bench 03:13:48 and 03:16:56).
std::uintptr_t AddExtraCopy(const std::uint32_t* playerRecord, std::uintptr_t group) {
    std::uintptr_t exe = ExeBase();
    std::memcpy(g_extraRecord, playerRecord, kRecordSize);
    auto* rec = reinterpret_cast<std::uint32_t*>(g_extraRecord);
    std::uint32_t object = *reinterpret_cast<const std::uint16_t*>(exe + kCharObjects);
    rec[0] = (rec[0] & 0xF0000000u) | object;
    std::uintptr_t actor = g_real(rec, group);
    ++g_extraSpawned;
    const auto* pos = reinterpret_cast<const float*>(g_extraRecord + 4);
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow);
    std::uint8_t room = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow + 1);
    Log("extra copy: 0x%02X/0x%02X: Sora copy (object 0x%X) spawned beside the party at (%.0f, %.0f, %.0f), before "
        "our Sora: %s (actor exe+0x%llX, #%u)", world, room, object, pos[0], pos[1], pos[2],
        actor ? "made" : "the game made nothing", static_cast<unsigned long long>(actor ? actor - exe : 0), g_extraSpawned);
    return actor;
}

void CheckOurSoraLast(std::uintptr_t sora, std::uintptr_t copy) {
    std::uintptr_t exe = ExeBase();
    auto read = [exe](std::uintptr_t rva) { return *reinterpret_cast<const std::uintptr_t*>(exe + rva); };
    auto name = [sora, copy](std::uintptr_t a) { return a == sora ? "our Sora" : a == copy ? "THE COPY" : "other"; };
    Log("extra copy: after our Sora: player pointer = %s, list head = %s, camera target = %s",
        name(read(kPlayerPtr)), name(read(kListHead)), name(read(0x718CB0)));
}

std::uintptr_t __fastcall HookSpawnEntry(std::uint32_t* record, std::uintptr_t group) {
    std::uint32_t object = 0;
    __try {
        object = record[0] & 0xFFFFFFF;
        // Members in another group of the same pass: the copy's own slot was already added.
        if (g_injected && object == g_injected) {
            if (g_skipped++ < 50) Log("solo copy: the room's own member %d placeholder (the Sora copy) skipped: already added",
                                      object == kObjMember1 ? 1 : 2);
            return 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        object = 0;
    }
    std::uintptr_t extra = 0;
    bool extraTried = false;
    if (object == kObjPlayer && PuppetExtraCopy() && !PuppetSolo()) {
        __try {
            extraTried = true;
            extra = AddExtraCopy(record, group);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("extra copy: adding the Sora copy failed (exception)");
        }
    }
    std::uintptr_t actor = g_real(record, group);
    if (object != kObjPlayer) return actor;
    g_injected = 0;  // a new room's spawn pass
    __try {
        if (PuppetExtraCopy()) {
            if (extraTried) CheckOurSoraLast(actor, extra);
        } else if (!GroupHasMember(group)) {
            AddCopy(record, group);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("solo copy: adding the Sora copy failed (exception)");
    }
    return actor;
}

}  // namespace

void SoloCopyInit() {
    if (EnvInt("KH2COOP_SOLO_COPY", 1) != 1) {
        Log("solo copy: off (SOLO_COPY=0): forced-solo rooms leave the other player out");
        return;
    }
    bool on = HookFunction(kSpawnEntry, kSpawnEntryBytes, sizeof(kSpawnEntryBytes),
                        reinterpret_cast<void*>(HookSpawnEntry), reinterpret_cast<void**>(&g_real), "spawn entry");
    Log("solo copy: %s", on ? "on (rooms that spawn Sora alone also get the other player's Sora)"
                              : "OFF: the hook failed");
}

}  // namespace kh2coop
