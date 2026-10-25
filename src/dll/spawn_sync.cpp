// Shared spawning (N3e): both games get the same enemy groups, whoever walks
// into them. On by default with WORLD_SYNC (both PCs); SPAWN_SYNC=0 turns it off.
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
// Host objects with no spawn record (made by a script, e.g. Luxord's cards with trap_sysobj_appear, or by game code)
// that nothing of ours matches after half a second are made here with the game's actor factory (exe+0x3DF930, what
// trap_sysobj_appear calls: object id, position, facing), at the host's spot, and world sync binds each to the host
// object it was made for (you, 2026-10-14: cover most if not all types of objects the host's game makes).
// The factory loads no files (F240): an object our room never loaded gets its files first, the way a spawn group
// preloads its records (exe+0x3E0460, VERIFIED_OFFSETS section 1), and is made once they are in (or after 3 s as before).
//
// Verified 2026-10-06 (VERIFIED_OFFSETS.md): spawn actor exe+0x2A10420 = Sora;
// group table exe+0x2A10010 / count exe+0x2A10418; box test vtable[1](box,
// float pos[4]); HB Shadows' serials 80/79/76/77 match the PC room file.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "copy_blade.hpp"
#include "form_look.hpp"
#include "puppet.hpp"
#include "spawn_sync.hpp"
#include "stand_in.hpp"
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
constexpr std::uintptr_t kFactory = 0x3DF930;        // (object id, float pos[4], yaw) -> actor or 0
constexpr std::uint8_t kFactoryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;   // (u32 handle) -> object or 0
constexpr std::uint8_t kDecodeHandleBytes[] = {0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3};
// An object's files (Ghidra 2026-10-24, VERIFIED_OFFSETS section 1): the preload a spawn group runs for each record (object id,
// slot -1 = by object type, kind 0x1E), which only queues the files; the file pass that places the queued files in
// the file heap and starts reading them (a summon runs it with 0 right after its preload); and whether every file of
// the object is loaded. Bench 2026-10-24: queued alone, a Dusk's three files stayed at state 0 for 14 s.
constexpr std::uintptr_t kObjectPreload = 0x3E0460;  // (object id, slot, kind)
constexpr std::uint8_t kObjectPreloadBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kFilePass = 0x39D460;      // (task or 0)
constexpr std::uint8_t kFilePassBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0x5C, 0x24, 0x30};
constexpr std::uintptr_t kObjectLoaded = 0x3E6100;   // (object id, objentry row or 0) -> all its files loaded
constexpr std::uint8_t kObjectLoadedBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x57, 0xA0, 0xFF, 0xFF,
                                               0xA9, 0xF3, 0xFF, 0xFF, 0xFF};
constexpr std::uintptr_t kListHead = 0x2A171C8;      // our Sora
constexpr std::uintptr_t kActorPos = 0x670, kActorAngle = 0x68C;
// The battle state (Ghidra 2026-10-24, lead L132): u32 0 calm, 1 a fight (the frame update sets it from the enemies
// around), 2 forced (rooms flagged 'always battle' set it on load; the frame update leaves 2 alone). Limits are
// greyed while it is 0.
constexpr std::uintptr_t kBattle = 0x2A11404;
constexpr std::uintptr_t kForceBattle = 0x3AB690;     // () -> state 2
constexpr std::uint8_t kForceBattleBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x06, 0x00, 0x00, 0x00, 0xE8};
constexpr std::uintptr_t kSetBattle = 0x3AB6B0;       // (state 0, 1 or 2)
constexpr std::uint8_t kSetBattleBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xD9, 0x85, 0xC9};

using PFN_SpawnTask = void(__fastcall*)(void*, void*);
using PFN_GroupSpawnEnemies = void(__fastcall*)(std::uintptr_t);
using PFN_GroupTrigger = bool(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_SpawnEntityAt = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, const float*);
using PFN_Bool = bool(__fastcall*)();
using PFN_ActorBool = bool(__fastcall*)(std::uintptr_t);
using PFN_FindBySerial = std::uintptr_t(__fastcall*)(std::uint32_t);
using PFN_DecodeHandle = std::uintptr_t(__fastcall*)(std::uint32_t);
using PFN_Factory = std::uintptr_t(__fastcall*)(std::uint32_t, const float*, float);
using PFN_BoxTest = bool(__fastcall*)(std::uintptr_t, const float*);
using PFN_ObjectPreload = void(__fastcall*)(std::uint32_t, std::int32_t, std::int32_t);
using PFN_ObjectLoaded = bool(__fastcall*)(std::uint32_t, std::uintptr_t);
using PFN_FilePass = void(__fastcall*)(std::uintptr_t);
using PFN_ForceBattle = void(__fastcall*)();
using PFN_SetBattle = void(__fastcall*)(std::int32_t);

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
struct Made {  // host objects with no spawn record we made one for (by world sync's number)
    std::uint16_t netId;
    std::uint64_t frame;
};
Made g_made[64];
int g_madeCount = 0;
std::uint32_t g_makes = 0, g_makeFails = 0;
bool g_factoryOk = false;
bool g_factoryBytesOk = false;  // checked at init: the census hooks the factory right after, so its bytes change
bool g_loadOk = false;          // the preload and loaded check matched at init
bool g_battleOk = false;        // the battle state calls matched at init
struct Loading {  // objects whose files we asked for in this room (by epoch)
    std::uint32_t objId;
    std::uint64_t frame;
    bool late;  // logged as not loaded in time
};
Loading g_loading[32];
int g_loadingCount = 0;
std::uint32_t g_loads = 0;
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
    g_madeCount = 0;
    g_loadingCount = 0;
}

void ForgetLoading(std::uint32_t objId) {
    for (int i = 0; i < g_loadingCount; ++i)
        if (g_loading[i].objId == objId) g_loading[i] = g_loading[--g_loadingCount];
}

// Are this object's files in? If not, asks for them once (per room) and waits; after `wait` frames, gives up waiting.
enum class Files { kReady, kWaiting, kLate };
Files EnsureLoaded(std::uint32_t objId, std::uint64_t wait) {
    if (!g_loadOk || Fn<PFN_ObjectLoaded>(kObjectLoaded)(objId, 0)) return Files::kReady;
    Loading* l = nullptr;
    for (int i = 0; i < g_loadingCount; ++i)
        if (g_loading[i].objId == objId) l = &g_loading[i];
    if (!l) {
        if (g_loadingCount == 32) return Files::kLate;
        l = &g_loading[g_loadingCount++];
        *l = {objId, g_frame, false};
        Fn<PFN_ObjectPreload>(kObjectPreload)(objId, -1, 0x1E);
        Fn<PFN_FilePass>(kFilePass)(0);
        if (++g_loads <= 60) Log("spawn: object %#x's files aren't loaded here: loading them", objId);
        return Files::kWaiting;
    }
    if (g_frame - l->frame < wait) return Files::kWaiting;
    if (!l->late) {
        l->late = true;
        Log("spawn: object %#x's files still aren't all loaded after %llu frames", objId,
            static_cast<unsigned long long>(g_frame - l->frame));
    }
    return Files::kLate;
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

// Host objects with no spawn record and nothing of ours to bind to: made here (a few per frame).
void MakeUnmatched(int done) {
    HostEnemy host[64];
    g_stage = "host unmatched";
    int n = WorldSyncHostUnmatched(host, 64);
    for (int i = 0; i < n && done < 4; ++i) {
        const HostEnemy& h = host[i];
        Made* m = nullptr;
        for (int k = 0; k < g_madeCount; ++k)
            if (g_made[k].netId == h.netId) m = &g_made[k];
        // Once per host object; again only if ours is gone and the host's is still unmatched 5 s later.
        if (m && g_frame - m->frame < 300) continue;
        // Files first: the factory would fail (or fault, F240) on an object our room never loaded (F233). Waiting
        // doesn't count as a try; past 3 s it's made anyway, as before.
        g_stageObj = h.objId;
        g_stage = "object files";
        if (EnsureLoaded(h.objId, 180) == Files::kWaiting) continue;
        if (!m) {
            if (g_madeCount == 64) {
                // Full (bench / two-PC run 2026-10-18, the Land of Dragons swarm: 64 tries in a minute, then none
                // for the rest of the room): the oldest entry past its retry wait makes room.
                Made* oldest = &g_made[0];
                for (int k = 1; k < g_madeCount; ++k)
                    if (g_made[k].frame < oldest->frame) oldest = &g_made[k];
                if (g_frame - oldest->frame < 300) continue;
                m = oldest;
            } else {
                m = &g_made[g_madeCount++];
            }
            m->netId = h.netId;
        }
        m->frame = g_frame;
        g_stageSerial = 0;
        g_stageObj = h.objId;
        g_stage = "make (game factory)";
        const float pos[4] = {h.pos[0], h.pos[1], h.pos[2], 1.0f};
        std::uintptr_t actor = Fn<PFN_Factory>(kFactory)(h.objId, pos, h.angle);
        ++done;
        if (actor) {
            ++g_makes;
            WorldSyncMadeFor(actor, h.netId);
        } else {
            ++g_makeFails;
        }
        if (g_makes + g_makeFails <= 60)
            Log("spawn: the host's object #%u (object %#x, no spawn record) has no match here: %s at (%.0f, %.0f, "
                "%.0f)", h.netId, h.objId, actor ? "made" : "FAILED to make it", h.pos[0], h.pos[1], h.pos[2]);
    }
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
    if (g_factoryOk) MakeUnmatched(done);
}

// Research (TODO 1.90 step 3 / 1.24, test command 'body', KH2COOP_DEBUG=1): can the actor factory make the body the
// other player wears now (a Drive Form's P_EX100_BTLF 0x55, the die M_EX950_DICE 0x602) beside the copy, and what does
// it take from our player? Every Sora the game builds takes the player pointer, camera target, spawn actor and player
// controller (solo_copy.cpp). Logged: those pointers before / after, the new actor, and both again 2 s later. With
// `restore`, the pointers that changed are set back at once.
struct Global {
    const char* name;
    std::uintptr_t rva;
};
constexpr Global kGlobals[] = {{"player", 0x2A105D0},       {"Sora list head", 0x2A171C8}, {"camera target", 0x718CB0},
                               {"spawn actor", 0x2A10420},  {"player controller", 0x2A10620},
                               {"form player", 0x2AE9A28}};
constexpr int kGlobalCount = static_cast<int>(sizeof(kGlobals) / sizeof(kGlobals[0]));
std::uint32_t g_bodyAsk = 0;    // object id + 1 (0 = none); bit 16 of the id: restore
std::uintptr_t g_bodyMade = 0;
std::uint64_t g_bodyCheck = 0;  // the frame of the second look
std::uintptr_t g_bodyBefore[kGlobalCount];

const char* ModelOf(std::uintptr_t a) {
    std::uintptr_t row = a ? Read<std::uintptr_t>(a + 0x918) : 0;
    return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
}

const char* NameOf(std::uintptr_t p, std::uintptr_t made, char* buf, std::size_t size) {
    if (!p) return "0";
    if (p == made) return "THE NEW BODY";
    if (p == Read<std::uintptr_t>(ExeBase() + 0x2A171C8) && p == g_bodyBefore[1]) return "our Sora";
    if (p == PuppetCloneActor()) return "the copy";
    std::snprintf(buf, size, "exe+0x%llX (%.24s)", static_cast<unsigned long long>(p - ExeBase()), ModelOf(p));
    return buf;
}

std::uint64_t g_bodyMotion = 0;  // the frame to try a motion on a body made as a cutscene actor

void DebugBody() {
    if (g_bodyMotion && g_frame >= g_bodyMotion) {
        g_bodyMotion = 0;
        std::uintptr_t m = g_bodyMade;
        using PFN_FindMotion = int(__fastcall*)(std::uintptr_t, int);
        using PFN_SetMotion = void(__fastcall*)(std::uintptr_t, int, float, float);
        auto find = reinterpret_cast<PFN_FindMotion>(ExeBase() + 0x3C7C50);
        Log("body test: motions on %.24s (motion controller +0x158): 0 -> %d, 1 -> %d, 4 -> %d; playing 0", ModelOf(m),
            find(m + 0x158, 0), find(m + 0x158, 1), find(m + 0x158, 4));
        reinterpret_cast<PFN_SetMotion>(ExeBase() + 0x3C86A0)(m + 0x158, 0, 0.0f, 0.0f);
        Log("body test: motion now %d", Read<std::int32_t>(m + 0x180));
    }
    if (g_bodyCheck && g_frame >= g_bodyCheck) {
        g_bodyCheck = 0;
        char a[64], b[64];
        std::uintptr_t m = g_bodyMade;
        Log("body test: 2 s later: the new body %.24s at (%.0f, %.0f, %.0f), motion %d, team %u, +0x120 0x%08X",
            ModelOf(m), Read<float>(m + 0x670), Read<float>(m + 0x674), Read<float>(m + 0x678), Read<std::int32_t>(m + 0x180),
            Read<std::uint32_t>(m + 0x4DC), Read<std::uint32_t>(m + 0x120));
        for (int i = 0; i < kGlobalCount; ++i)
            Log("body test:   %s = %s (before the body: %s)", kGlobals[i].name,
                NameOf(Read<std::uintptr_t>(ExeBase() + kGlobals[i].rva), m, a, sizeof(a)), NameOf(g_bodyBefore[i], m, b, sizeof(b)));
    }
    if (!g_bodyAsk) return;
    std::uint32_t id = (g_bodyAsk - 1) & 0xFFFF;
    bool restore = ((g_bodyAsk - 1) & 0x10000) != 0;
    bool scene = ((g_bodyAsk - 1) & 0x20000) != 0;  // as a cutscene actor (object | 0x40000000, no player class)
    bool nearOurs = ((g_bodyAsk - 1) & 0x40000) != 0;  // beside our Sora even when there is a copy
    g_bodyAsk = 0;
    std::uintptr_t by = nearOurs ? 0 : PuppetCloneActor();
    if (!by) by = Read<std::uintptr_t>(ExeBase() + 0x2A171C8);
    if (!by) return;
    for (int i = 0; i < kGlobalCount; ++i) g_bodyBefore[i] = Read<std::uintptr_t>(ExeBase() + kGlobals[i].rva);
    const float pos[4] = {Read<float>(by + 0x670) + 150.0f, Read<float>(by + 0x674), Read<float>(by + 0x678), 1.0f};
    Log("body test: making object %#x%s beside %s at (%.0f, %.0f, %.0f)%s", id, scene ? " as a cutscene actor" : "",
        by == PuppetCloneActor() ? "the copy" : "our Sora", pos[0], pos[1], pos[2], restore ? ", pointers it takes set back" : "");
    std::uintptr_t made = Fn<PFN_Factory>(kFactory)(scene ? id | 0x40000000u : id, pos, 0.0f);
    g_bodyMade = made;
    if (!made) {
        Log("body test: the factory made nothing");
        return;
    }
    Log("body test: made %.24s at exe+0x%llX (class exe+0x%llX, team %u, size class: next +0xA90 %p)", ModelOf(made),
        static_cast<unsigned long long>(made - ExeBase()),
        static_cast<unsigned long long>(Read<std::uintptr_t>(made) - ExeBase()), Read<std::uint32_t>(made + 0x4DC),
        reinterpret_cast<void*>(Read<std::uintptr_t>(made + 0xA90)));
    char a[64], b[64];
    for (int i = 0; i < kGlobalCount; ++i) {
        std::uintptr_t now = Read<std::uintptr_t>(ExeBase() + kGlobals[i].rva);
        if (now == g_bodyBefore[i]) continue;
        Log("body test:   %s changed: %s -> %s%s", kGlobals[i].name, NameOf(g_bodyBefore[i], made, a, sizeof(a)),
            NameOf(now, made, b, sizeof(b)), restore ? " (set back)" : "");
        if (restore) *reinterpret_cast<std::uintptr_t*>(ExeBase() + kGlobals[i].rva) = g_bodyBefore[i];
    }
    g_bodyCheck = g_frame + 120;
    if (scene) g_bodyMotion = g_frame + 60;
}

// Test command 'spawn' (TODO 2.5, KH2COOP_DEBUG=1): make `count` of an object in front of our Sora, loading its files
// first like a spawn group does. On the host, the friend's game makes its copies through MakeUnmatched.
std::uint32_t g_spawnAsk = 0;  // object id + 1 (0 = none)
int g_spawnCount = 0;
std::uint64_t g_spawnAskFrame = 0;
bool g_spawnAskNew = false;
bool g_spawnRetryFiles = false;  // a new command asks for the files again, even after an earlier timeout here

void DebugSpawn() {
    std::uint32_t id = g_spawnAsk - 1;
    if (g_spawnAskNew) {
        g_spawnAskNew = false;
        g_spawnAskFrame = g_frame;
    }
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
    if (!Read<std::uint8_t>(ExeBase() + kInField) || !sora) {
        if (g_frame - g_spawnAskFrame > 600) {
            Log("spawn test: object %#x: not in the field with our Sora for 10 s; nothing made", id);
            g_spawnAsk = 0;
        }
        return;
    }
    NewEpoch();
    if (g_spawnRetryFiles) {
        g_spawnRetryFiles = false;
        ForgetLoading(id);
    }
    Files f = EnsureLoaded(id, 600);
    if (f == Files::kWaiting) return;
    g_spawnAsk = 0;
    if (f == Files::kLate) {
        Log("spawn test: object %#x: its files didn't load in 10 s; nothing made", id);
        return;
    }
    Log("spawn test: object %#x's files are in (%llu frames after the command); making %d in front of our Sora", id,
        static_cast<unsigned long long>(g_frame - g_spawnAskFrame), g_spawnCount);
    const float* p = reinterpret_cast<const float*>(sora + kActorPos);
    float a = Read<float>(sora + kActorAngle);
    float fwd[2] = {std::sin(a), std::cos(a)}, side[2] = {std::cos(a), -std::sin(a)};
    for (int i = 0; i < g_spawnCount; ++i) {
        float s = (i - (g_spawnCount - 1) * 0.5f) * 150.0f;
        const float pos[4] = {p[0] + fwd[0] * 300.0f + side[0] * s, p[1], p[2] + fwd[1] * 300.0f + side[1] * s, 1.0f};
        std::uintptr_t made = Fn<PFN_Factory>(kFactory)(id, pos, a + 3.14159265f);  // facing our Sora
        Log("spawn test: object %#x %d/%d: %s at (%.0f, %.0f, %.0f)%s%.24s", id, i + 1, g_spawnCount,
            made ? "made" : "FAILED (the factory made nothing: budget full?)", pos[0], pos[1], pos[2], made ? ", " : "",
            made ? ModelOf(made) : "");
    }
}

// Test command 'battle' (Track 2, KH2COOP_DEBUG=1, benches only): hold the game's forced battle state so Limits can
// be used with no enemies; off hands the state back to the game's own update. The room load resets it (lead L132):
// logged once when that happens.
int g_battleAsk = -1;       // 1 on, 0 off, -1 nothing asked
bool g_battleHeld = false;

void DebugBattle() {
    if (g_battleAsk >= 0) {
        if (!Read<std::uint8_t>(ExeBase() + kInField)) return;  // after the room load, or the load resets it
        std::uint32_t before = Read<std::uint32_t>(ExeBase() + kBattle);
        if (g_battleAsk == 1) {
            if (before != 2) Fn<PFN_ForceBattle>(kForceBattle)();
        } else if (before == 2) {
            Fn<PFN_SetBattle>(kSetBattle)(0);
        }
        g_battleHeld = g_battleAsk == 1;
        Log("battle test: %s (state %u -> %u)", g_battleHeld ? "forced on" : "off", before,
            Read<std::uint32_t>(ExeBase() + kBattle));
        g_battleAsk = -1;
        return;
    }
    if (g_battleHeld && Read<std::uint32_t>(ExeBase() + kBattle) != 2) {
        g_battleHeld = false;
        Log("battle test: the game dropped the forced state (now %u, room %02X/%02X)",
            Read<std::uint32_t>(ExeBase() + kBattle), Read<std::uint8_t>(ExeBase() + kNow),
            Read<std::uint8_t>(ExeBase() + kNow + 1));
    }
}

void __fastcall HookSpawnTask(void* a, void* b) {
    g_realSpawnTask(a, b);
    if (!g_on) return;
    StandInSpawnStep();
    CopyBladeSpawnStep();
    FormLookSpawnStep();
    if (g_battleAsk >= 0 || g_battleHeld) {
        __try {
            DebugBattle();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_battleAsk = -1;
            g_battleHeld = false;
            Log("battle test: exception");
        }
    }
    if (g_spawnAsk) {
        __try {
            DebugSpawn();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_spawnAsk = 0;
            Log("spawn test: exception");
        }
    }
    if (g_bodyAsk || g_bodyCheck || g_bodyMotion) {
        __try {
            DebugBody();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_bodyCheck = 0;
            g_bodyMotion = 0;
            Log("body test: exception");
        }
    }
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
    g_factoryBytesOk = BytesMatch(kFactory, kFactoryBytes, sizeof(kFactoryBytes), "ActorFactory");
    g_loadOk = BytesMatch(kObjectPreload, kObjectPreloadBytes, sizeof(kObjectPreloadBytes), "ObjectPreload") &&
               BytesMatch(kObjectLoaded, kObjectLoadedBytes, sizeof(kObjectLoadedBytes), "ObjectLoaded") &&
               BytesMatch(kFilePass, kFilePassBytes, sizeof(kFilePassBytes), "FilePass");
    g_battleOk = BytesMatch(kForceBattle, kForceBattleBytes, sizeof(kForceBattleBytes), "ForceBattle") &&
                 BytesMatch(kSetBattle, kSetBattleBytes, sizeof(kSetBattleBytes), "SetBattle");
    g_factoryOk = ok && !host && EnvInt("KH2COOP_MAKE_UNMATCHED", 1) == 1 && g_factoryBytesOk;
    if (g_factoryOk) Log("spawn sync: host objects with no spawn record and no match here are made here too");
    Log("spawn sync: %s", !ok ? "OFF (hooks failed)"
                         : host ? "on (host: the friend's position triggers our enemy groups too)"
                                : "on (friend: while the host is here, enemies come from the host's game)");
}

bool SpawnSyncFactoryOk() { return g_on && g_factoryBytesOk; }

void SpawnSyncDebugBody(int objId) {
    if (!g_on || !g_factoryBytesOk) {
        Log("debug command: body %#x: spawn sync is off or the factory didn't match at start", objId);
        return;
    }
    g_bodyAsk = static_cast<std::uint32_t>(objId) + 1;
    Log("debug command: body %#x", objId);
}

void SpawnSyncDebugSpawn(int objId, int count) {
    if (!g_on || !g_factoryBytesOk || !g_loadOk) {
        Log("debug command: spawn %#x: spawn sync is off, or the factory or file loading didn't match at start", objId);
        return;
    }
    g_spawnCount = count < 1 ? 1 : count > 8 ? 8 : count;
    g_spawnAskNew = true;
    g_spawnRetryFiles = true;
    g_spawnAsk = static_cast<std::uint32_t>(objId) + 1;
    Log("debug command: spawn %#x x%d", objId, g_spawnCount);
}

void SpawnSyncDebugBattle(bool on) {
    if (!g_on || !g_battleOk) {
        Log("debug command: battle %s: spawn sync is off or the battle state calls didn't match at start", on ? "on" : "off");
        return;
    }
    g_battleAsk = on ? 1 : 0;
    Log("debug command: battle %s", on ? "on" : "off");
}

void SpawnSyncFrame() {
    if (!g_on || g_frame == 0 || g_frame % 600 != 0) return;
    static std::uint64_t lastLogged = 0;
    if (lastLogged == g_frame) return;
    lastLogged = g_frame;
    if (g_host)
        Log("spawn: groups triggered by the friend so far: %u", g_friendTriggers);
    else
        Log("spawn: host enemies spawned here %u, own group spawns skipped %u, missing records %u, host objects "
            "made here %u (failed %u)", g_spawns, g_suppressed, g_noRecord, g_makes, g_makeFails);
}

}  // namespace kh2coop
