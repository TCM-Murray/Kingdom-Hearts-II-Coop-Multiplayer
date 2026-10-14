// The other player's Sora (the extra copy): a real Sora spawned beside the party in every room.
//
// How the game makes the party (VERIFIED_OFFSETS rows 'Party members come from spawn-file placeholders',
// '`Party` command rewrites the world party entry'): a room's spawn groups list placeholder objects,
// 0x236 = the player, 0x237 = active member 1, 0x238 = active member 2; each placeholder becomes whoever
// sits in that slot of the world party entry (save+0x3534). The copy is in none of those slots, so Donald,
// Goofy and the world allies keep their places, and forced-solo rooms (Bailey HB 04/08 program 52 and ~90
// other fights spawn the player with no member placeholder) still get the copy (TODO 5.5: both fight together).
//
// Here: our hook on the game's "spawn one entry of a group" exe+0x3FE590(record, group) watches the
// player's spawn and spawns the copy in the same spawn pass, through the same function, from a copy of the
// player's record (same spot: the spawn step moves the two apart). Not later: one frame later (bench session
// 27) the game took the new Sora as the player and left ours standing.

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
constexpr std::uint32_t kObjPlayer = 0x236;
constexpr std::uint32_t kRecordSize = 0x40;
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;
constexpr std::uintptr_t kListHead = 0x2A171C8;
// Character -> object table (u16 each, exe+0x3E2E50 reads it): what member placeholder 0x237 resolves to when the
// party slot holds character 0x00 (exe+0x3C32E0), i.e. how the copy is built. Building from the PLAYER
// placeholder 0x236 instead made the new Sora the head of the actor list (bench session 28, 03:13:48).
constexpr std::uintptr_t kCharObjects = 0x2A25300;

using PFN_SpawnEntry = std::uintptr_t(__fastcall*)(std::uint32_t*, std::uintptr_t);

PFN_SpawnEntry g_real = nullptr;
// Our record for the copy stays valid for the room: the actor may keep a pointer to it.
alignas(16) std::uint8_t g_extraRecord[kRecordSize] = {};
std::uint32_t g_extraSpawned = 0;

// The Sora copy is not in the party table, so the room spawns the party as the game has it; the copy is spawned
// in the same pass just BEFORE our Sora, from a copy of the PLAYER record
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        object = 0;
    }
    if (object != kObjPlayer || !PuppetEnabled() || PuppetSolo()) return g_real(record, group);
    std::uintptr_t extra = 0;
    __try {
        extra = AddExtraCopy(record, group);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("extra copy: adding the Sora copy failed (exception)");
    }
    std::uintptr_t actor = g_real(record, group);
    __try {
        CheckOurSoraLast(actor, extra);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return actor;
}

}  // namespace

void SoloCopyInit() {
    bool on = HookFunction(kSpawnEntry, kSpawnEntryBytes, sizeof(kSpawnEntryBytes),
                           reinterpret_cast<void*>(HookSpawnEntry), reinterpret_cast<void**>(&g_real), "spawn entry");
    Log("extra copy: %s", on ? "on (every room that spawns our Sora also spawns the other player's Sora)"
                             : "OFF: the hook failed (no Sora copy)");
}

}  // namespace kh2coop
