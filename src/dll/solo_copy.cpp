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
//
// Rooms that spawn the player's body directly, not through 0x236 (TODO 5.36; your picks 2026-11-26, DECISIONS): any
// PLAYER-type object with Sora's status (objentry NeoStatus 1): Agrabah's carpet rides P_EX100_AL_CARPET 0x4FF and
// AL14_PLAYER 0x5E9, the Light Cycle 0x5F5, the ship fight's sidecar 0x840 (L2131). With KH2COOP_DIRECT_COPY=1 the
// copy is made from that body's own record and object, before ours, as for 0x236 (off by default: F211, a second
// sidecar rider crashed; for now the carpet's two only, see DirectBody). Each such body's ride, the battle-NPC or minigame object in its spawn group, is that
// player's own (TODO 1.302): world sync leaves it out (SoloCopyOwnRide).

#include "solo_copy.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "game.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSpawnEntry = 0x3FE590;  // (record, group) -> actor or 0
constexpr std::uint8_t kSpawnEntryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                             0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uint32_t kObjPlayer = 0x236;
constexpr std::uint32_t kRecordSize = 0x40;
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;
// Character -> object table (u16 each, exe+0x3E2E50 reads it): what member placeholder 0x237 resolves to when the
// party slot holds character 0x00 (exe+0x3C32E0), i.e. how the copy is built. Building from the PLAYER
// placeholder 0x236 instead made the new Sora the head of the actor list (bench session 28, 03:13:48).
constexpr std::uintptr_t kCharObjects = 0x2A25300;

using PFN_SpawnEntry = std::uintptr_t(__fastcall*)(std::uint32_t*, std::uintptr_t);

PFN_SpawnEntry g_real = nullptr;
// Our record for the copy stays valid for the room: the actor may keep a pointer to it.
alignas(16) std::uint8_t g_extraRecord[kRecordSize] = {};
std::uint32_t g_extraSpawned = 0;
bool g_directCopy = false;  // KH2COOP_DIRECT_COPY: a copy in rooms that spawn the player's body directly

// A spawn record (OpenKH Ard/SpawnPoint.cs Entity, 0x40 bytes): +0 object id (top 4 bits flags), +0x1E u16 serial.
constexpr std::uintptr_t kRecordSerial = 0x1E;
// An object's row (OpenKH Objentry.cs): +4 u8 type (0 PLAYER, 9 BTLNPC, 16 MINIGAME), +0x4C u16 NeoStatus.
constexpr std::uint8_t kTypePlayer = 0, kTypeBattleNpc = 9, kTypeMinigame = 16;
constexpr std::uint16_t kSoraStatus = 1;

bool ObjectKind(std::uint32_t objId, std::uint8_t* type, std::uint16_t* status) {
    auto entry = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(ExeBase() + game::kObjectEntry);
    std::uintptr_t row = entry(objId);
    return row && ReadAny(row + 4, type) && ReadAny(row + 0x4C, status);
}

bool DirectBody(std::uint32_t objId) {  // a Sora body the room spawns itself
    // shortcut: only the bodies benched with the switch, the carpet rides; the Light Cycle 0x5F5 and the sidecar 0x840
    // keep their stand-in (your pick 2026-11-27, TODO 1.306) until each is fixed and benched, then this check goes.
    if (objId != 0x4FF && objId != 0x5E9) return false;
    std::uint8_t type = 0xFF;
    std::uint16_t status = 0;
    return objId != kObjPlayer && ObjectKind(objId, &type, &status) && type == kTypePlayer && status == kSoraStatus;
}

bool RideObject(std::uint32_t objId) {
    std::uint8_t type = 0xFF;
    std::uint16_t status = 0;
    return ObjectKind(objId, &type, &status) && (type == kTypeBattleNpc || type == kTypeMinigame);
}

std::uint16_t RoomNow() {
    std::uint16_t room = 0xFFFF;
    ReadAny(ExeBase() + game::kNow, &room);  // world, room
    return room;
}

// The rides of this room's direct bodies, by object, serial and room: no actor kept (a room change makes them stale).
struct Ride {
    std::uint32_t objId;
    std::uint16_t serial, room;
};
Ride g_rides[16] = {};
int g_rideCount = 0;
// The pass's last records: a ride may come before its rider in the group (07/0E's e_50, b_61, b_70).
struct SeenRecord {
    std::uintptr_t group;  // compared only, never read
    std::uint32_t objId;
    std::uint16_t serial, room;
};
SeenRecord g_seen[32] = {};
unsigned g_seenNext = 0;
std::uintptr_t g_riderGroup = 0;
std::uint16_t g_riderRoom = 0xFFFF;

void AddRide(std::uint32_t objId, std::uint16_t serial, std::uint16_t room) {
    if (!serial) return;  // world sync matches by serial
    if (g_rideCount && g_rides[0].room != room) g_rideCount = 0;
    for (int i = 0; i < g_rideCount; ++i)
        if (g_rides[i].objId == objId && g_rides[i].serial == serial) return;
    if (g_rideCount >= 16) return;
    g_rides[g_rideCount++] = {objId, serial, room};
    Log("extra copy: 0x%02X/0x%02X: object 0x%X (serial %u) is its rider's own ride: world sync leaves it out",
        room & 0xFF, room >> 8, objId, serial);
}

// Every record of the pass: remember it, and mark the rides of a direct body's group.
void NoteRecord(const std::uint32_t* record, std::uintptr_t group, std::uint32_t objId, bool body) {
    std::uint16_t serial = 0, room = RoomNow();
    ReadAny(reinterpret_cast<std::uintptr_t>(record) + kRecordSerial, &serial);
    g_seen[g_seenNext++ % 32] = {group, objId, serial, room};
    if (body) {
        g_riderGroup = group, g_riderRoom = room;
        for (const SeenRecord& s : g_seen)
            if (s.group == group && s.room == room && s.objId != objId && RideObject(s.objId))
                AddRide(s.objId, s.serial, room);
    } else if (group == g_riderGroup && room == g_riderRoom && RideObject(objId)) {
        AddRide(objId, serial, room);
    }
}

// The Sora copy is not in the party table, so the room spawns the party as the game has it; the copy is spawned
// in the same pass just BEFORE our Sora, from a copy of the PLAYER record
// with character 0's object (what today's copy is built from). Order matters (Ghidra + bench session 28): every
// Sora the constructor exe+0x3A7A40 builds takes the player pointer, the camera target exe+0x718CB0,
// exe+0x2A10420 and the player controller exe+0x2A10620, and goes in front of the other Soras in the actor list
// (exe+0x3BFBC0 sorts by actor+0xB91), so the Sora built last is the player. Built after ours, the copy took the
// list head and the HUD's player entry (bench 03:13:48 and 03:16:56).
// sameObject: a directly spawned body's copy keeps its record's object (the carpet rider's body and motions).
std::uintptr_t AddExtraCopy(const std::uint32_t* playerRecord, std::uintptr_t group, bool sameObject) {
    std::uintptr_t exe = ExeBase();
    std::memcpy(g_extraRecord, playerRecord, kRecordSize);
    auto* rec = reinterpret_cast<std::uint32_t*>(g_extraRecord);
    std::uint32_t object = sameObject ? rec[0] & 0xFFFFFFF : *reinterpret_cast<const std::uint16_t*>(exe + kCharObjects);
    rec[0] = (rec[0] & 0xF0000000u) | object;
    std::uintptr_t actor = g_real(rec, group);
    ++g_extraSpawned;
    const auto* pos = reinterpret_cast<const float*>(g_extraRecord + 4);
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + game::kNow);
    std::uint8_t room = *reinterpret_cast<const volatile std::uint8_t*>(exe + game::kNow + 1);
    Log("extra copy: 0x%02X/0x%02X: Sora copy (object 0x%X%s) spawned beside the party at (%.0f, %.0f, %.0f), before "
        "our Sora: %s (actor exe+0x%llX, #%u)", world, room, object, sameObject ? ", the room's own body" : "", pos[0],
        pos[1], pos[2], actor ? "made" : "the game made nothing",
        static_cast<unsigned long long>(actor ? actor - exe : 0), g_extraSpawned);
    return actor;
}

void CheckOurSoraLast(std::uintptr_t sora, std::uintptr_t copy) {
    std::uintptr_t exe = ExeBase();
    auto read = [exe](std::uintptr_t rva) { return *reinterpret_cast<const std::uintptr_t*>(exe + rva); };
    auto name = [sora, copy](std::uintptr_t a) { return a == sora ? "our Sora" : a == copy ? "THE COPY" : "other"; };
    Log("extra copy: after our Sora: player pointer = %s, list head = %s, camera target = %s",
        name(read(kPlayerPtr)), name(read(game::kListHead)), name(read(0x718CB0)));
}

std::uintptr_t __fastcall HookSpawnEntry(std::uint32_t* record, std::uintptr_t group) {
    std::uint32_t object = 0;
    __try {
        object = record[0] & 0xFFFFFFF;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        object = 0;
    }
    bool body = false;
    if (g_directCopy) {  // the bodies and their own rides only with the switch (the default world sync untouched)
        __try {
            body = object && object != kObjPlayer && DirectBody(object);
            NoteRecord(record, group, object, body);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            body = false;
        }
    }
    if ((object != kObjPlayer && !(body && g_directCopy)) || !PuppetEnabled() || PuppetSolo())
        return g_real(record, group);
    std::uintptr_t extra = 0;
    __try {
        extra = AddExtraCopy(record, group, body);
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
    g_directCopy = EnvInt("KH2COOP_DIRECT_COPY", 0) == 1;
    bool on = HookFunction(kSpawnEntry, kSpawnEntryBytes, sizeof(kSpawnEntryBytes),
                           reinterpret_cast<void*>(HookSpawnEntry), reinterpret_cast<void**>(&g_real), "spawn entry");
    Log("extra copy: %s; rooms that spawn the player's body directly: %s", on
        ? "on (every room that spawns our Sora also spawns the other player's Sora)" : "OFF: the hook failed (no Sora copy)",
        g_directCopy ? "a copy of that body (KH2COOP_DIRECT_COPY=1)" : "no copy (KH2COOP_DIRECT_COPY=0)");
}

bool SoloCopyRideObject(std::uint32_t objId) {
    if (!objId || !g_rideCount) return false;
    std::uint16_t room = RoomNow();
    for (int i = 0; i < g_rideCount; ++i)
        if (g_rides[i].objId == objId && g_rides[i].room == room) return true;
    return false;
}

bool SoloCopyOwnRide(std::uint32_t objId, std::uint16_t serial) {
    if (!serial || !g_rideCount) return false;
    std::uint16_t room = RoomNow();
    for (int i = 0; i < g_rideCount; ++i)
        if (g_rides[i].objId == objId && g_rides[i].serial == serial && g_rides[i].room == room) return true;
    return false;
}

}  // namespace kh2coop
