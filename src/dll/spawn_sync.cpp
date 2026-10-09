// Shared spawning (N3e): both games get the same enemy groups, whoever walks
// into them. On by default with WORLD_SYNC=1 (both PCs); SPAWN_SYNC=0 turns it off.
//
// The game spawns a room's enemy groups from its spawn files (ard: b_00...):
// every frame SpawnTask (exe+0x3A4F90) tests each group's trigger boxes
// against ONE position, our own Sora's, and a hit spawns the group's entities.
// Each spawn record has a serial that is the same on both PCs (same room
// file), and a live actor keeps a pointer to its record (actor+0x9F0).
//
// Host: after the game's own SpawnTask, the friend's position (newest packet,
// same room) is tested against the same boxes, and a hit triggers the group
// through the game's own GroupTrigger. Only type-2 groups (enemies); entrance,
// event and map-change groups stay tied to the host's own Sora.
//
// Friend: while the host is in our room, our game never spawns enemy groups
// itself (GroupSpawnEnemies is skipped). Instead, every host enemy that came
// from a spawn record and has no local actor with that serial is spawned from
// our copy of the same record, at the host enemy's current position, with the
// game's own SpawnEntityAt. World sync then binds the two by serial. This also
// covers arriving in a room where the host is already fighting. When the host
// is elsewhere, our game spawns as usual.
//
// Verified 2026-10-06 (VERIFIED_OFFSETS.md): spawn actor exe+0x2A10420 = Sora;
// group table exe+0x2A10010 / count exe+0x2A10418; box test vtable[1](box,
// float pos[4]); HB Shadows' serials 80/79/76/77 match the PC room file.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "spawn_sync.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;         // u8 world, room
constexpr std::uintptr_t kInField = 0x9BA8D0;     // u8: 0 while a room loads
constexpr std::uintptr_t kSpawnActor = 0x2A10420; // actor whose position SpawnTask tests (Sora)
constexpr std::uintptr_t kGroups = 0x2A10010;     // 16-byte entries {char name[4], u32 flags, group*}
constexpr std::uintptr_t kGroupCount = 0x2A10418;
// Group object: +4 flags (1 done, 8 triggered this frame), +8 descriptor, +0x10 first trigger box.
// Descriptor: u8 type @0 (2 = enemies), u16 entity count @4, 0x40-byte entity records from +0x2C.
constexpr std::uintptr_t kGroupFlags = 4, kGroupDesc = 8, kGroupBoxes = 0x10;
constexpr std::uintptr_t kDescEntities = 0x2C, kRecordSize = 0x40, kRecordSerial = 0x1E;
constexpr std::uintptr_t kBoxNext = 0x58;         // u32 handle of the next box
constexpr std::uint8_t kTypeEnemies = 2;

constexpr std::uintptr_t kSpawnTask = 0x3A4F90;
constexpr std::uint8_t kSpawnTaskBytes[] = {0x48, 0x83, 0xEC, 0x38, 0xF3, 0x0F, 0x10, 0x05, 0xE4, 0xEA, 0x27, 0x00};
constexpr std::uintptr_t kGroupSpawnEnemies = 0x3FE6F0;
constexpr std::uint8_t kGroupSpawnEnemiesBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
                                                    0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x55};
// Called, not hooked (bytes checked before first use).
constexpr std::uintptr_t kGroupTrigger = 0x3FE320;   // (group, box) -> bool
constexpr std::uint8_t kGroupTriggerBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr std::uintptr_t kSpawnEntityAt = 0x3FE650;  // (record, group, float pos[4]) -> spawned object or 0
constexpr std::uint8_t kSpawnEntityAtBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                                0x57, 0x48, 0x83, 0xEC, 0x20, 0xF3};
constexpr std::uintptr_t kSpawnBlocked = 0x3ABC80;   // () -> bool (events, etc.)
constexpr std::uint8_t kSpawnBlockedBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x8B, 0x05, 0x76, 0x57, 0x66, 0x02};
constexpr std::uintptr_t kActorMayTrigger = 0x3BA720;  // (actor) -> bool; SpawnTask's own gate
constexpr std::uint8_t kActorMayTriggerBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kFindBySerial = 0x3B41E0;   // (serial) -> actor or 0
constexpr std::uint8_t kFindBySerialBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x85, 0xC9, 0xBB, 0x70};
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;   // (u32 handle) -> object or 0
constexpr std::uint8_t kDecodeHandleBytes[] = {0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3};

using PFN_SpawnTask = void(__fastcall*)(void*, void*);
using PFN_GroupSpawnEnemies = void(__fastcall*)(std::uintptr_t);
using PFN_GroupTrigger = bool(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_SpawnEntityAt = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, const float*);
using PFN_Bool = bool(__fastcall*)();
using PFN_ActorBool = bool(__fastcall*)(std::uintptr_t);
using PFN_FindBySerial = std::uintptr_t(__fastcall*)(std::uint32_t);
using PFN_DecodeHandle = std::uintptr_t(__fastcall*)(std::uint32_t);
using PFN_BoxTest = bool(__fastcall*)(std::uintptr_t, const float*);

PFN_SpawnTask g_realSpawnTask = nullptr;
PFN_GroupSpawnEnemies g_realGroupSpawnEnemies = nullptr;
bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;  // SpawnTask runs

// Per room epoch (world sync's): what we already did, so each thing is logged / done once.
std::uint32_t g_epoch = 0xFFFFFFFF;
std::uintptr_t g_loggedGroups[32];
int g_loggedGroupCount = 0;
struct Spawned {
    std::uint16_t serial;
    std::uint64_t frame;
};
Spawned g_spawned[128];
int g_spawnedCount = 0;
std::uint32_t g_friendTriggers = 0, g_suppressed = 0, g_spawns = 0, g_noRecord = 0;
// Which step the spawn task was in, for the exception log (Olympus 06/0F threw, 2026-10-06).
const char* g_stage = "";
std::uint16_t g_stageSerial = 0;
std::uint32_t g_stageObj = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

template <typename F>
F Fn(std::uintptr_t rva) {
    return reinterpret_cast<F>(ExeBase() + rva);
}

bool BytesMatch(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n, const char* name) {
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0) return true;
    Log("spawn sync: %s bytes differ (different game build?)", name);
    return false;
}

void NewEpoch() {
    std::uint32_t e = WorldSyncEpoch();
    if (e == g_epoch) return;
    g_epoch = e;
    g_loggedGroupCount = 0;
    g_spawnedCount = 0;
}

bool FirstLogFor(std::uintptr_t group) {
    for (int i = 0; i < g_loggedGroupCount; ++i)
        if (g_loggedGroups[i] == group) return false;
    if (g_loggedGroupCount < 32) g_loggedGroups[g_loggedGroupCount++] = group;
    return true;
}

std::uint16_t DescId(std::uintptr_t group) { return Read<std::uint16_t>(Read<std::uintptr_t>(group + kGroupDesc) + 2); }

// The same gates the game's SpawnTask applies before testing any group. Null = ok, else why not.
const char* SpawnContextBlocked() {
    std::uintptr_t exe = ExeBase();
    if (!Read<std::uint8_t>(exe + kInField) || Read<std::uint8_t>(exe + kNow) == 0xFF) return "not in the field";
    std::uintptr_t actor = Read<std::uintptr_t>(exe + kSpawnActor);
    if (!actor) return "no player actor";
    if (!Fn<PFN_ActorBool>(kActorMayTrigger)(actor)) return "our Sora can't trigger (+0x120 bits 0x10080000 or not listed)";
    if (Read<std::uint32_t>(actor + 0x9B8) & 4) return "our Sora is down";
    if (Fn<PFN_Bool>(kSpawnBlocked)()) return "spawning blocked (event?)";
    return nullptr;
}
bool SpawnContextOk() { return SpawnContextBlocked() == nullptr; }
ULONGLONG g_lastBlockedLog = 0;  // TODO 1.44 diagnosis: why the friend's position didn't trigger a group

// ---- Host: the friend's position triggers our enemy groups too ----
void HostFriendTriggers() {
    PeerPose p;
    if (!AvatarLinkPeerNow(p) || !p.hasActor) return;
    std::uintptr_t exe = ExeBase();
    if (p.world != Read<std::uint8_t>(exe + kNow) || p.room != Read<std::uint8_t>(exe + kNow + 1)) return;
    const char* blocked = SpawnContextBlocked();
    NewEpoch();
    const float pos[4] = {p.pos[0], p.pos[1], p.pos[2], 1.0f};
    std::uint32_t count = Read<std::uint32_t>(exe + kGroupCount);
    for (std::uint32_t i = 0; i < count && i < 64; ++i) {
        std::uintptr_t entry = exe + kGroups + 16 * i;
        if (Read<std::uint32_t>(entry + 4) & 1) continue;  // the game skips these too
        std::uintptr_t group = Read<std::uintptr_t>(entry + 8);
        if (!group) continue;
        std::uint32_t flags = Read<std::uint32_t>(group + kGroupFlags);
        if (flags & (1 | 8)) continue;  // done, or our own Sora already triggered it this frame
        std::uintptr_t desc = Read<std::uintptr_t>(group + kGroupDesc);
        if (!desc || Read<std::uint8_t>(desc) != kTypeEnemies) continue;
        for (std::uintptr_t box = Read<std::uintptr_t>(group + kGroupBoxes); box;
             box = Fn<PFN_DecodeHandle>(kDecodeHandle)(Read<std::uint32_t>(box + kBoxNext))) {
            auto test = reinterpret_cast<PFN_BoxTest>(Read<std::uintptr_t>(Read<std::uintptr_t>(box) + 8));
            if (!test(box, pos)) continue;
            if (blocked) {
                if (GetTickCount64() - g_lastBlockedLog > 2000) {
                    g_lastBlockedLog = GetTickCount64();
                    std::uintptr_t sora = Read<std::uintptr_t>(exe + kSpawnActor);
                    Log("spawn: the friend is in group %.4s id %u's box but our game can't spawn now: %s (our Sora "
                        "+0x120 = 0x%X, group cooldown %.1f)", reinterpret_cast<const char*>(entry), DescId(group), blocked,
                        sora ? Read<std::uint32_t>(sora + 0x120) : 0, Read<float>(group + 0x20));
                }
                break;
            }
            *reinterpret_cast<std::uint32_t*>(group + kGroupFlags) |= 8;
            if (FirstLogFor(group)) {
                ++g_friendTriggers;
                Log("spawn: the friend at (%.0f, %.0f, %.0f) is in group %.4s id %u's box; triggering it", p.pos[0],
                    p.pos[1], p.pos[2], reinterpret_cast<const char*>(entry), DescId(group));
            }
            if (Fn<PFN_GroupTrigger>(kGroupTrigger)(group, box)) break;
        }
    }
}

// ---- Friend: spawn what the host has ----
// The record with this serial (and object id) in our type-2 groups.
bool FindRecord(std::uint16_t serial, std::uint32_t objId, std::uintptr_t& group, std::uintptr_t& record) {
    std::uintptr_t exe = ExeBase();
    std::uint32_t count = Read<std::uint32_t>(exe + kGroupCount);
    for (std::uint32_t i = 0; i < count && i < 64; ++i) {
        // Flag 1 = unused entry; its pointer can be anything (Olympus 06/0F: entry 7 pointed into a string).
        if (Read<std::uint32_t>(exe + kGroups + 16 * i + 4) & 1) continue;
        std::uintptr_t g = Read<std::uintptr_t>(exe + kGroups + 16 * i + 8);
        std::uintptr_t desc = g ? Read<std::uintptr_t>(g + kGroupDesc) : 0;
        if (!desc || Read<std::uint8_t>(desc) != kTypeEnemies) continue;
        std::uint16_t n = Read<std::uint16_t>(desc + 4);
        for (std::uint16_t k = 0; k < n && k < 64; ++k) {
            std::uintptr_t rec = desc + kDescEntities + k * kRecordSize;
            if (Read<std::uint16_t>(rec + kRecordSerial) == serial && Read<std::uint32_t>(rec) == objId) {
                group = g;
                record = rec;
                return true;
            }
        }
    }
    return false;
}

Spawned* FindSpawned(std::uint16_t serial) {
    for (int i = 0; i < g_spawnedCount; ++i)
        if (g_spawned[i].serial == serial) return &g_spawned[i];
    return nullptr;
}

void FriendSpawnMissing() {
    g_stage = "context";
    if (!WorldSyncHostPresent()) return;
    NewEpoch();
    if (!SpawnContextOk()) return;
    HostEnemy host[64];
    g_stage = "host list";
    int n = WorldSyncHostEnemies(host, 64);
    int done = 0;
    for (int i = 0; i < n && done < 4; ++i) {  // a few per frame: no hitch when joining a big fight
        const HostEnemy& h = host[i];
        Spawned* s = FindSpawned(h.serial);
        // Once per enemy; again only if ours vanished and the host still has it 5 s later.
        if (s && g_frame - s->frame < 300) continue;
        g_stageSerial = h.serial;
        g_stageObj = h.objId;
        g_stage = "find by serial";
        if (Fn<PFN_FindBySerial>(kFindBySerial)(h.serial)) continue;
        if (!s) {
            if (g_spawnedCount == 128) continue;
            s = &g_spawned[g_spawnedCount++];
            s->serial = h.serial;
        }
        s->frame = g_frame;
        std::uintptr_t group = 0, record = 0;
        g_stage = "find record";
        if (!FindRecord(h.serial, h.objId, group, record)) {
            if (g_noRecord++ < 20)
                Log("spawn: the host has an enemy with serial %u (object %#x) that isn't in our enemy groups; left "
                    "to world sync", h.serial, h.objId);
            continue;
        }
        const float pos[4] = {h.pos[0], h.pos[1], h.pos[2], 0.0f};
        g_stage = "spawn at (game)";
        std::uintptr_t spawned = Fn<PFN_SpawnEntityAt>(kSpawnEntityAt)(record, group, pos);
        ++done;
        ++g_spawns;
        Log("spawn: host enemy serial %u (group id %u) %s at (%.0f, %.0f, %.0f)", h.serial, DescId(group),
            spawned ? "spawned here" : "FAILED to spawn here", h.pos[0], h.pos[1], h.pos[2]);
    }
}

void __fastcall HookSpawnTask(void* a, void* b) {
    g_realSpawnTask(a, b);
    if (!g_on) return;
    ++g_frame;
    g_stage = g_host ? "host triggers" : "";
    g_stageSerial = 0;
    g_stageObj = 0;
    __try {
        if (g_host)
            HostFriendTriggers();
        else
            FriendSpawnMissing();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 20)
            Log("spawn sync: exception in the spawn task (step: %s, enemy serial %u object %#x, world %02X room %02X)",
                g_stage, g_stageSerial, g_stageObj, Read<std::uint8_t>(ExeBase() + kNow),
                Read<std::uint8_t>(ExeBase() + kNow + 1));
    }
}

void __fastcall HookGroupSpawnEnemies(std::uintptr_t group) {
    if (g_on && !g_host && WorldSyncHostPresent()) {
        ++g_suppressed;
        NewEpoch();
        if (FirstLogFor(group)) Log("spawn: our group id %u was triggered; the host's game spawns it instead", DescId(group));
        return;
    }
    g_realGroupSpawnEnemies(group);
}

}  // namespace

void SpawnSyncInit(bool host) {
    if (EnvInt("KH2COOP_SPAWN_SYNC", 1) != 1) {
        Log("spawn sync: off (SPAWN_SYNC=0)");
        return;
    }
    g_host = host;
    bool ok = BytesMatch(kGroupTrigger, kGroupTriggerBytes, sizeof(kGroupTriggerBytes), "GroupTrigger") &&
              BytesMatch(kSpawnEntityAt, kSpawnEntityAtBytes, sizeof(kSpawnEntityAtBytes), "SpawnEntityAt") &&
              BytesMatch(kSpawnBlocked, kSpawnBlockedBytes, sizeof(kSpawnBlockedBytes), "SpawnBlocked") &&
              BytesMatch(kActorMayTrigger, kActorMayTriggerBytes, sizeof(kActorMayTriggerBytes), "ActorMayTrigger") &&
              BytesMatch(kFindBySerial, kFindBySerialBytes, sizeof(kFindBySerialBytes), "FindActorBySerial") &&
              BytesMatch(kDecodeHandle, kDecodeHandleBytes, sizeof(kDecodeHandleBytes), "DecodeHandle") &&
              HookFunction(kSpawnTask, kSpawnTaskBytes, sizeof(kSpawnTaskBytes), reinterpret_cast<void*>(HookSpawnTask),
                           reinterpret_cast<void**>(&g_realSpawnTask), "SpawnTask");
    if (ok && !host)
        ok = HookFunction(kGroupSpawnEnemies, kGroupSpawnEnemiesBytes, sizeof(kGroupSpawnEnemiesBytes),
                          reinterpret_cast<void*>(HookGroupSpawnEnemies), reinterpret_cast<void**>(&g_realGroupSpawnEnemies),
                          "GroupSpawnEnemies");
    g_on = ok;
    Log("spawn sync: %s", !ok ? "OFF (hooks failed)"
                         : host ? "on (host: the friend's position triggers our enemy groups too)"
                                : "on (friend: while the host is here, enemies come from the host's game)");
}

void SpawnSyncFrame() {
    if (!g_on || g_frame == 0 || g_frame % 600 != 0) return;
    static std::uint64_t lastLogged = 0;
    if (lastLogged == g_frame) return;
    lastLogged = g_frame;
    if (g_host)
        Log("spawn: groups triggered by the friend so far: %u", g_friendTriggers);
    else
        Log("spawn: host enemies spawned here %u, own group spawns skipped %u, missing records %u", g_spawns,
            g_suppressed, g_noRecord);
}

}  // namespace kh2coop
