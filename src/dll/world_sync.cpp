// World sync (N3): the host's game owns the world; the friend's game shows it.
//
// N3a companions: the host streams its party companions (the actors in the
// friend slots, e.g. Goofy); on the friend's PC each local companion with the
// same object id stops running its own AI and copies the host's: position,
// facing and motion (blended like the Sora copy, same playback delay), HP.
// Damage from the friend's own game to a mirrored companion is blocked: the
// host owns companion HP.
//
// N3b enemy census: the host also streams its enemies (team 2) with a per-room
// id, object id and the spot where each first appeared; the friend tracks its
// own enemies the same way and logs how well they match (object id + spawn
// spot). Bench 22:50: the Borough's first wave matched 4/4 on fixed spawn points.
//
// N3c/d enemy mirror (friend side): each host enemy is bound to a local enemy
// of the same object id (nearest spawn spot). A bound enemy copies the host's
// position, facing, motion and HP; its own motion changes are blocked. Damage
// the friend deals to it is dropped locally and sent to the host as a claim
// ("KH2E"); the host applies it to its enemy with the game's own HP change and
// records the friend's copy as the attacker (so that enemy turns on the copy).
// When the host's enemy reaches 0 HP, the local one dies through the game's own
// death path (animation, EXP, drops). When it disappears without dying (it sank
// away, an event removed it), the local one is removed the way the game removes
// a faded-out enemy: no death, no EXP, no drops (TODO 1.37). Local enemies with
// no host match are parked far below the room and removed the same way once the
// host has none left.
// A bound enemy also copies the host's height scale (actor+0x34): a Shadow squashes to ~0.1 as it sinks and
// grows back as it rises, in its own code; without the copy the friend's Shadows stayed flat (TODO 1.2).
// Hits from local enemies on our own Sora are dropped (the host's enemies hit
// our copy in the host's game; those hits are forwarded, see puppet.cpp).
//
// Packet "KH2W" (host -> friend, every frame): header + 32-byte entries, in parts of at most kMaxPartBytes
// (one frame's parts share a seq). On unless WORLD_SYNC=0 (both PCs).

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "motion_mirror.hpp"
#include "puppet.hpp"
#include "spawn_sync.hpp"
#include "grab_sync.hpp"
#include "copy_blade.hpp"
#include "limit_sync.hpp"
#include "companion_sync.hpp"
#include "mickey_sync.hpp"
#include "mission_sync.hpp"
#include "shared_move.hpp"
#include "reaction_sync.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;            // u8 world, room
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;   // u64[2] companion actors
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kEntityPos = 0x30, kEntitySin = 0x40, kEntityCos = 0x48, kEntityAngle = 0x4C;
constexpr std::uintptr_t kEntityAirborne = 0x104;
constexpr std::uintptr_t kActorMatrix = 0x40;  // world matrix, 4 rows of 4 floats: row 2 facing, row 3 position
constexpr std::uintptr_t kActorMotion = 0x180;
constexpr std::uintptr_t kActorMotionBank = 0x2B8;  // != 0: the motion id is from another file's set (avatar_link.cpp)
constexpr std::uintptr_t kActorStats = 0x5C0;       // -> stat channels; HP = first int
constexpr std::uintptr_t kActorTeam = 0x4DC;        // 1 party, 2 enemies
constexpr std::uintptr_t kActorObjRow = 0x918;      // -> object table row (u32 id, ..., model @+8)
constexpr std::uintptr_t kActorCarried = 0x690, kActorAccel = 0xA48, kActorVelocity = 0xB98;
// Scale vector {x, y, z, 1} (bench dumps 2026-10-13): a Shadow's y runs 0.1 (sunk) .. 0.985 (up); 1 on the rest.
constexpr std::uintptr_t kActorScaleY = 0x34;
// The motion clock (frames into the motion, +1 a frame; what trap_obj_motion_get_time reads, VERIFIED_OFFSETS).
constexpr std::uintptr_t kActorMotionClock = 0x19C;
constexpr float kClockSlack = 10.0f;  // friend: our enemy's clock this many frames off the host's = set to the host's
constexpr std::uintptr_t kActorSpawnRecord = 0x9F0;  // -> 0x40-byte spawn record; u16 serial @+0x1E
// Fade-out removal (bench 2026-10-08, Ghidra exe+0x3BFD30): fade {f32 value, f32 speed per step} at +0xA08,
// stepped by exe+0x3B77C0; once the value is 0 with +0x9B8 bit 4 set, the actor update calls class slot +0x38
// (Shadow: exe+0x3B4700 ends the AI script, sets +0x120 bit 28), then the removal check and disposal.
constexpr std::uintptr_t kActorFade = 0xA08, kActorFadeFlags = 0x9B8;
constexpr std::uint32_t kFadeRemoveBit = 0x10;

constexpr std::uint32_t kMagic = 0x5732484B;  // "KH2W"
constexpr std::uint16_t kVersion = 6;  // v6: sent in parts; v5: enemy motion clock; v4: enemy height scale; v3: enemy spawn serials; v2: room instance + paused flag
// One frame's packet in parts of at most this many bytes. A whole one was up to ~2,100 bytes and the receiver takes
// 1,500: in the Land of Dragons' first morale fight (35 enemies and objects) every one was lost and the friend's game
// showed no enemies (TODO 1.74, two-PC run 2026-10-16). 1,200 also stays under a VPN's MTU (no IP fragments).
constexpr int kMaxPartBytes = 1200;
int g_partBytes = kMaxPartBytes;  // KH2COOP_WORLD_PART_BYTES (bench: smaller parts in any room)
constexpr int kMaxEntries = 64;  // all we track (g_enemies) plus the two companions fit in the parts
constexpr std::uint8_t kLastPart = 0x80;  // Header::part: index in the low bits, this bit on the frame's last part
// The friend drops an entity the host no longer reports once it was missing from this many of the host's frames
// (a lost part only delays its entities, it doesn't remove them).
constexpr std::uint32_t kMissingFrames = 4;
enum : std::uint8_t { kKindCompanion = 0, kKindEnemy = 1 };
// kFlagOtherBank: the motion comes from another file's set (e.g. the partner's part of a Limit); its id
// means something else in the actor's own set.
// kFlagAttached: attached to another actor (actor+0x6A0); pos and angle are where it is drawn (its world matrix).
// kFlagOnHost / kFlagOnFriend: the enemy goes for the host's Sora / the friend's copy (AggroTargetOf; TODO 1.80).
enum : std::uint8_t { kFlagAirborne = 1, kFlagOtherBank = 2, kFlagAttached = 4, kFlagOnHost = 8, kFlagOnFriend = 16 };

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint32_t senderMs;
    std::uint8_t world, room, count, companions;  // companions come first in the entries
    std::uint16_t instance;  // +1 after every completed room load (also Continue's same-room reload)
    std::uint8_t flags;  // kPaused: the host's game updated no entities this frame (menu, Game Over, load)
    std::uint8_t part;   // index | kLastPart
};
enum : std::uint8_t { kPaused = 1 };
struct Entry {
    std::uint32_t objId;
    std::uint16_t netId;
    std::uint8_t kind, flags;
    float pos[3];
    float angle;
    std::uint32_t motion;
    std::int32_t hp;
};
struct EnemySpawn {  // sent after the entries: where each enemy first appeared (same order as enemy entries)
    float pos[3];
    std::uint16_t serial;  // its spawn record's serial (same on both PCs: same room file); 0 = none
    std::uint16_t scaleY;  // the enemy's height scale now (actor+0x34) x 4096; 0 = not known
    // Its motion clock now. Each game ran its own: Armored Xemnas II's dragon swayed about 4 s ahead in the
    // friend's game, so Xemnas's platform on its head was up to ~1,000 units from the host's (you + bench,
    // 2026-10-14 22:18).
    float clock;
};
#pragma pack(pop)
static_assert(sizeof(Entry) == 32, "entry layout");
static_assert(sizeof(EnemySpawn) == 20, "spawn layout");

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;
std::uint16_t g_roomKey = 0xFFFF;  // world << 8 | room the tables belong to
std::uint16_t g_instance = 0;      // our room instance: +1 after every completed load
std::uint32_t g_epoch = 0;         // +1 whenever our room or the host's room instance changes (spawn_sync.cpp)
std::uint8_t g_lastInField = 0;
constexpr std::uintptr_t kInField = 0x9BA8D0;  // u8: 0 while a room loads (VERIFIED_OFFSETS.md)

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uint16_t RoomKey() {
    return static_cast<std::uint16_t>(Read<std::uint8_t>(ExeBase() + kNow) << 8 | Read<std::uint8_t>(ExeBase() + kNow + 1));
}

bool InExe(std::uintptr_t a) { return a > ExeBase() && a < ExeBase() + 0x3000000; }

// An object handle (u32: bit 31 set, table index in bits 25-30) -> the object, or 0.
std::uintptr_t DecodeHandle(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + 0x2B0D720 + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

// The actor this one is attached to (the game's attach exe+0x3D9FE0: parent handle at +0x6A0, bone at +0x6A4), or 0.
// An attached actor is drawn from its parent's bone: its own position (entity+0x30) can say anything, often (0, 0, 0)
// (bench 2026-10-14: a Larxene attached to another drawn at (-463, -85, 948) with her position at the origin).
std::uintptr_t AttachedTo(std::uintptr_t a) {
    __try {
        return DecodeHandle(Read<std::uint32_t>(a + 0x6A0));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::uint32_t ObjId(std::uintptr_t actor) {
    std::uintptr_t row = Read<std::uintptr_t>(actor + kActorObjRow);
    return InExe(row) ? Read<std::uint32_t>(row) : 0;
}

// Field objects a player rides, steered with that player's stick (TODO 1.126; your pick 2026-10-25: the rider's game
// runs it). One brain made the friend's ride the host's: the host's tornado took the copy by touch and steered it with
// the host's stick (two PCs 2026-10-25, F337). Each game runs its own: the host still sends them, so the friend's
// game spawns its own from the same spawn record (spawn_sync.cpp: one per serial, no duplicate), but the friend's
// isn't bound to the host's: its brain runs, it goes its own way and takes only the friend's own Sora; in neither game
// can one take the copy (motion_mirror.cpp's hit check). The two screens show them in different places.
// KH2COOP_OWN_CARRIERS=0: back to one brain for them too.
constexpr std::uint32_t kCarriers[] = {0x151};  // F_MU070, Land of Dragons' tornado
bool g_ownCarriers = true;

bool IsCarrier(std::uint32_t objId) {
    if (!g_ownCarriers) return false;
    for (std::uint32_t c : kCarriers)
        if (c == objId) return true;
    return false;
}

const char* Model(std::uint32_t objId) {
    // Object table at exe+0x2A254D0: {u32 version, u32 count}, 0x60-byte rows, model @+8.
    std::uintptr_t table = ExeBase() + 0x2A254D0;
    std::uint32_t count = Read<std::uint32_t>(table + 4);
    for (std::uint32_t i = 0; i < count && i < 8192; ++i) {
        std::uintptr_t row = table + 8 + i * 0x60;
        if (Read<std::uint32_t>(row) == objId) return reinterpret_cast<const char*>(row + 8);
    }
    return "?";
}

// Readable, committed memory without a guard page (see Readable in puppet.cpp:
// a guarded read of a stack guard page breaks that thread's stack growth).
bool Readable4(std::uintptr_t address) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) return false;
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi.State == MEM_COMMIT && (mbi.Protect & kReadable) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
           address + 4 <= reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

std::uint16_t Serial(std::uintptr_t actor) {
    std::uintptr_t rec = Read<std::uintptr_t>(actor + kActorSpawnRecord);
    return rec && Readable4(rec + 0x1C) ? Read<std::uint16_t>(rec + 0x1E) : 0;
}

// A script holds this actor: trap_obj_hook set its hook (+0xD48).
bool HeldByScript(std::uintptr_t actor) {
    __try {
        return Read<std::uintptr_t>(actor + 0xD48) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int Hp(std::uintptr_t actor) {
    std::uintptr_t stats = Read<std::uintptr_t>(actor + kActorStats);
    return stats && Readable4(stats) ? Read<std::int32_t>(stats) : -1;
}

// The enemy HP bar (dk::GAUGE_ENEMY) only moves when HP changes through the game's own stat change
// (exe+0x3D2EB0), which tells it. Mirrored HP is written straight into memory, so tell it ourselves (TODO 1.21:
// the friend's bars stood still). Both do nothing unless the actor is the enemy the bar shows.
constexpr std::uintptr_t kBarHit = 0x3DCC10;  // (actor, channel, hp): drops, the lost part shown in red
constexpr std::uintptr_t kBarSet = 0x3DCBB0;  // (actor, channel, hp): jumps (a heal)
constexpr std::uint8_t kBarBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                      0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05};
constexpr std::uintptr_t kBarTarget = 0x2A24E90;  // -> the bar's target: +4 actor handle, +0x28 gauge
using PFN_BarUpdate = void(__fastcall*)(std::uintptr_t, int, int);
int g_barOk = -1;  // -1 = not checked yet
std::uint32_t g_barUpdates = 0;

void TellHpBar(std::uintptr_t actor, int before, int after) {
    std::uintptr_t exe = ExeBase();
    if (g_barOk < 0) {
        g_barOk = std::memcmp(reinterpret_cast<void*>(exe + kBarHit), kBarBytes, sizeof(kBarBytes)) == 0 &&
                  std::memcmp(reinterpret_cast<void*>(exe + kBarSet), kBarBytes, sizeof(kBarBytes)) == 0;
        if (!g_barOk) Log("world: the enemy HP bar's functions differ on this build; mirrored HP won't move the bar");
    }
    if (!g_barOk || !Read<std::uintptr_t>(exe + kBarTarget)) return;
    __try {
        reinterpret_cast<PFN_BarUpdate>(exe + (after < before ? kBarHit : kBarSet))(actor, 0, after);
        ++g_barUpdates;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_barOk = 0;
        Log("world: telling the enemy HP bar crashed; turned off");
    }
}

// The actor's height scale for the packet (x 4096, at least 1; 0 = unreadable).
std::uint16_t ScaleBits(std::uintptr_t actor) {
    float y = Read<float>(actor + kActorScaleY);
    if (!std::isfinite(y) || y <= 0.0f) return 0;
    return static_cast<std::uint16_t>((std::min)(65535.0f, (std::max)(1.0f, y * 4096.0f + 0.5f)));
}

// ---- Enemy tables (both sides): per room instance, by first sighting ----
struct LocalEnemy {
    std::uintptr_t actor;
    std::uint16_t netId;
    std::uint32_t objId;
    std::uint16_t serial;
    float spawn[3];
    std::uint64_t lastFrame;
    bool matched;          // census
    std::uint64_t firstFrame;
    std::uint16_t bound;   // friend: host netId it copies (0 = none)
    bool parked, killed;
    const char* killPending;  // friend: kill in this enemy's next update (never from the frame hook)
    bool killIsDeath;         // ... through the game's death (EXP, drops), else removed like a faded-out enemy
    bool vanished;            // removed without a death: hits on it are dropped until the game disposes of it
    std::uint64_t vanishedFrame;
    bool shutdownCalled;      // the game's shutdown start ran on it (FinishRemoval)
    std::uint64_t parkedFrame;
    int lastMotion;
    int traceMotion, traceAttempt;  // ENEMY_TRACE: last motion / blocked AI motion logged
    std::uint8_t traceCollision;
    Entry cached;          // host: state read right after the game's update (actor known valid then)
    bool cachedOk;
    std::uint16_t cachedScale;  // host: its height scale then, as sent (EnemySpawn::scaleY)
    float cachedClock;          // host: its motion clock then (EnemySpawn::clock)
    std::uint64_t attachedAlone;  // friend: frame since ours is attached and the host's (in our samples) isn't, or 0
    std::uint8_t hostTarget;      // friend: whom the host's enemy goes for (WorldSyncHostTargetOf)
};
LocalEnemy g_enemies[64];
int g_enemyCount = 0;
std::uint16_t g_nextNetId = 1;
std::uint32_t g_updatesThisFrame = 0;

void ResetRoom(std::uint16_t key) {
    g_roomKey = key;
    ++g_epoch;
    g_enemyCount = 0;
    g_nextNetId = 1;
}

bool FillEntry(Entry& e, std::uintptr_t a, std::uint32_t objId, std::uint16_t netId, std::uint8_t kind);

// Research (ENEMY_TRACE=1, both PCs): each enemy's motion and collision byte changes, and on the
// friend the motions its own AI asked for that the mirror blocked. For the Shadows that came back
// from underground unhittable on the friend's side (two-PC session 5).
bool g_trace = false;
bool g_claimLog = false;  // KH2COOP_CLAIM_LOG=1: LogClaimAttacker

void TraceEnemy(LocalEnemy& le) {
    int motion = Read<std::int32_t>(le.actor + kActorMotion);
    std::uint8_t col = Read<std::uint8_t>(le.actor + 0x18C);
    if (motion == le.traceMotion && col == le.traceCollision) return;
    const float* pos = reinterpret_cast<const float*>(le.actor + kActorEntity + kEntityPos);
    Log("trace: enemy #%u (obj 0x%X, serial %u%s) motion %d -> %d, collision 0x%02X -> 0x%02X, clock %.0f, pos (%.0f, "
        "%.0f, %.0f), hp %d", le.netId, le.objId, le.serial, le.bound ? ", bound" : "", le.traceMotion, motion,
        le.traceCollision, col, Read<float>(le.actor + 0x19C), pos[0], pos[1], pos[2], Hp(le.actor));
    le.traceMotion = motion;
    le.traceCollision = col;
}

// The game reuses a removed actor's memory for the next one it makes, sometimes while our entry for the
// old one is still fresh (a dying enemy updates until it is disposed). Session 18b bench (TODO 1.41): the
// host's Shadow #4 (serial 79) died, group 244's new enemy got its address 1.4 s later and was sent as #4
// with the Shadow's object, serial and spawn spot; the friend spawned a Shadow from that record, copied
// the new enemy's motions onto it (T-pose) and crashed at exe+0x3C795C. So an address only keeps its
// entry while the object, the spawn serial and "alive" stay the same.
bool SameEnemy(const LocalEnemy& le, std::uintptr_t actor, const Entry& now, bool nowOk) {
    if (ObjId(actor) != le.objId || Serial(actor) != le.serial) return false;
    return !(le.cachedOk && nowOk && le.cached.hp <= 0 && now.hp > 0);
}
std::uint32_t g_reusedLogs = 0;

void TrackEnemy(std::uintptr_t actor) {
    for (int i = 0; i < g_enemyCount; ++i) {
        if (g_enemies[i].actor == actor) {
            LocalEnemy& le = g_enemies[i];
            Entry now;
            bool nowOk = FillEntry(now, actor, le.objId, le.netId, kKindEnemy);
            if (!SameEnemy(le, actor, now, nowOk)) {
                if (g_reusedLogs++ < 20)
                    Log("world: a new enemy %s (serial %u) took the memory of #%u %s (serial %u, HP %d); tracked as "
                        "a new one", Model(ObjId(actor)), Serial(actor), le.netId, Model(le.objId), le.serial,
                        le.cachedOk ? le.cached.hp : -1);
                g_enemies[i] = g_enemies[--g_enemyCount];
                break;
            }
            if (g_trace) TraceEnemy(le);
            le.lastFrame = g_frame;
            le.cached = now;
            le.cachedOk = nowOk;
            le.cachedScale = ScaleBits(actor);
            le.cachedClock = Read<float>(actor + kActorMotionClock);
            return;
        }
    }
    if (g_enemyCount == 64) return;
    LocalEnemy& e = g_enemies[g_enemyCount++];
    e = {actor, g_nextNetId++, ObjId(actor), Serial(actor), {}, g_frame, false, g_frame, 0, false, false, nullptr, false,
         false, 0, false, 0, -1, -1, -1, 0xFF, {}, false};
    std::memcpy(e.spawn, reinterpret_cast<const void*>(actor + kActorEntity + kEntityPos), sizeof(e.spawn));
    e.cachedOk = FillEntry(e.cached, actor, e.objId, e.netId, kKindEnemy);
    e.cachedScale = ScaleBits(actor);
    e.cachedClock = Read<float>(actor + kActorMotionClock);
}

void AgeEnemies() {
    if (g_updatesThisFrame == 0) return;  // menu/pause: nothing updated, nothing gone
    for (int i = 0; i < g_enemyCount;) {
        if (g_frame - g_enemies[i].lastFrame > 30) {
            g_enemies[i] = g_enemies[--g_enemyCount];
        } else {
            ++i;
        }
    }
}

// ---- Friend side: what the host sent ----
struct Sample {
    std::uint32_t senderMs;
    Entry e;
    std::uint16_t scaleY;  // enemies: EnemySpawn::scaleY (0 = not known)
    float clock;           // enemies: EnemySpawn::clock
};
struct Remote {
    std::uint8_t kind;
    std::uint16_t netId;
    std::uint32_t objId;
    std::uint16_t serial;  // enemies: spawn record serial (0 = none)
    float spawn[3];   // enemies
    Sample hist[8];   // newest last
    int count;
    std::uint32_t lastSeq;
    std::uint64_t firstFrame;  // our frame when it first came
};
Remote g_remote[64];
int g_remoteCount = 0;
std::uint16_t g_remoteRoom = 0xFFFF;
std::uint16_t g_remoteInstance = 0xFFFF;
bool g_remotePaused = false;
std::uint32_t g_lastPacketMs = 0;
std::uint32_t g_lastPacketSeq = 0;  // newest host packet applied (0 = none yet)
std::uint32_t g_lastPacketSenderMs = 0;  // its host clock time (parity sampler)
std::uint32_t g_latePackets = 0;
std::uint32_t g_badSamples = 0;

// The TWTNW ship fight (0x12/0x16) plays past z 115,000; the old 100,000 limit froze its enemies on the
// friend's screen (two-PC run session 28, TODO 1.48).
bool SaneEntry(const Entry& e) {
    for (float v : e.pos)
        if (!std::isfinite(v) || std::fabs(v) > 1000000.0f) return false;
    return std::isfinite(e.angle) && std::fabs(e.angle) < 100.0f;
}

Remote* FindRemote(std::uint8_t kind, std::uint16_t netId) {
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].kind == kind && g_remote[i].netId == netId) return &g_remote[i];
    return nullptr;
}

// Interpolated pose of a remote entity at the playback time; false if none.
// The host's Limit plays the partner's part from the Limit's own set and puts him at (0, 0, 0) while he
// warps in and out (bench 2026-10-06: Auron at the origin at motions 253 and 257). He's attached to Sora then:
// where he's drawn is in his world matrix (bench 2026-10-13); both senders now send that (UseDrawnPlace), so
// this only holds a partner whose matrix also says (0, 0, 0).
// Not for other attached actors: there (0, 0, 0) can be the real place (bench 2026-10-14: Data Roxas's steal of the
// host's Sora is staged at the origin, both drawn there; holding the friend's Roxas left him 1076 away).
bool Warped(const Entry& e) {
    return (e.flags & kFlagOtherBank) && e.pos[0] == 0.0f && e.pos[1] == 0.0f && e.pos[2] == 0.0f;
}

// A Limit partner attached to its Sora (Warped) is drawn where its world matrix says (bench 2026-10-13: it starts on
// that Sora and moves with the Limit motion): send that place and facing instead, so the other game's companion,
// which isn't attached, shows up there. Still (0, 0, 0) = hidden: the receiver holds it where it was.
void UseDrawnPlace(Entry& e, std::uintptr_t a) {
    if (!Warped(e)) return;
    const float* m = reinterpret_cast<const float*>(a + kActorMatrix);
    std::memcpy(e.pos, m + 12, sizeof(e.pos));
    e.angle = std::atan2(-m[8], -m[10]);  // row 2 = (-sin, 0, -cos) of the facing
}

bool PoseAt(const Remote& r, std::uint32_t target, Entry& out) {
    if (r.count == 0) return false;
    const Sample* older = &r.hist[0];
    out = r.hist[r.count - 1].e;  // default: newest
    for (int i = r.count - 1; i >= 0; --i) {
        if (static_cast<std::int32_t>(target - r.hist[i].senderMs) >= 0) {
            older = &r.hist[i];
            if (i == r.count - 1) {
                out = older->e;
                return true;
            }
            const Sample& newer = r.hist[i + 1];
            if (Warped(older->e) || Warped(newer.e)) {  // no blending toward the origin
                out = Warped(older->e) ? newer.e : older->e;
                return true;
            }
            std::int32_t span = static_cast<std::int32_t>(newer.senderMs - older->senderMs);
            float t = span > 0 ? static_cast<float>(static_cast<std::int32_t>(target - older->senderMs)) / span : 1.0f;
            t = (std::min)(1.0f, (std::max)(0.0f, t));
            out = older->e;
            for (int k = 0; k < 3; ++k) out.pos[k] = older->e.pos[k] + (newer.e.pos[k] - older->e.pos[k]) * t;
            constexpr float kPi = 3.14159265f;
            float d = std::fmod(newer.e.angle - older->e.angle + 3 * kPi, 2 * kPi) - kPi;
            out.angle = older->e.angle + d * t;
            return true;
        }
    }
    out = r.hist[0].e;  // target older than everything we have
    return true;
}

// The sample shown at the playback time, for its height scale and motion clock (no blending: the host sends one
// every frame).
const Sample* SampleAt(const Remote& r, std::uint32_t target) {
    for (int i = r.count - 1; i >= 0; --i)
        if (static_cast<std::int32_t>(target - r.hist[i].senderMs) >= 0) return &r.hist[i];
    return r.count ? &r.hist[0] : nullptr;
}
std::uint16_t ScaleAt(const Remote& r, std::uint32_t target) {
    const Sample* s = SampleAt(r, target);
    return s ? s->scaleY : 0;
}
std::uint32_t g_clockFixes = 0;
constexpr std::uint64_t kAttachedAloneWait = 30;  // frames (see ApplyEnemy)
std::uint32_t g_attachedAloneHeld = 0;           // frames a copy's place was held for that

// ---- Friend side: mirrored companions ----
struct Mirror {
    std::uintptr_t actor;
    std::uint16_t netId;
    int lastMotion;
};
Mirror g_mirrors[2];
std::uint32_t g_mirrorWrites = 0, g_blockedDamage = 0;

Mirror* MirrorFor(std::uintptr_t actor) {
    for (auto& m : g_mirrors)
        if (m.actor && m.actor == actor) return &m;
    return nullptr;
}

// While the friend's own Limit runs, its game's companions are its own: the Limit drives the world's ally
// (Auron) next to the friend's Sora, so no mirroring, AI skip or motion block until it ends (user decision
// 2026-10-06: "local Limit"; the friend's hits still reach the host's enemies as claims). Before this, a
// friend's Bushido fought the mirror and left Auron invisible on the friend's screen (user test 23:14).
bool g_ownLimit = false;
bool g_partnerLimit = false;     // KH2COOP_PARTNER_LIMIT (TODO 1.57, see SendPartner)
// The companions our running Limit plays its partners' parts on, by party slot: one for most Limits, both for
// Trinity (Donald and Goofy; you, 2026-10-14: the host's Donald stayed with the host's Sora).
std::uintptr_t g_ourPartners[2] = {};
std::uint32_t g_partnerSent = 0;

void UpdateMirrors() {
    bool ownLimit = PuppetOurLimitRunning();
    if (ownLimit != g_ownLimit) {
        Log("world: our own Limit %s: companions %s", ownLimit ? "started" : "ended",
            ownLimit ? "run locally until it ends" : "follow the host's again");
        if (!ownLimit && (g_ourPartners[0] || g_ourPartners[1]))
            Log("world: our Limit partners' poses sent to the host so far: %u", g_partnerSent);
        g_ownLimit = ownLimit;
        if (!ownLimit) g_ourPartners[0] = g_ourPartners[1] = 0;  // (a first motion can come before we see the start)
    }
    bool fresh = AvatarLinkNowMs() - g_lastPacketMs < 1000 && g_remoteRoom == RoomKey() && !ownLimit;
    for (int slot = 0; slot < 2; ++slot) {
        Mirror& m = g_mirrors[slot];
        std::uintptr_t actor = Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot);
        std::uintptr_t want = 0;
        std::uint16_t netId = 0;
        if (fresh && InExe(actor)) {
            std::uint32_t id = ObjId(actor);
            for (int i = 0; i < g_remoteCount; ++i) {
                if (g_remote[i].kind == kKindCompanion && g_remote[i].objId == id) {
                    want = actor;
                    netId = g_remote[i].netId;
                    break;
                }
            }
        }
        if (want != m.actor) {
            if (want)
                Log("world: mirroring our companion %s (slot %d) from the host's", Model(ObjId(want)), slot);
            else if (m.actor)
                Log("world: companion in slot %d released (%u writes so far)", slot, g_mirrorWrites);
            m = {want, netId, -1};
        }
    }
}

// A companion takes the other game's pose (pos, facing, motion; HP when writeHp).
void ApplyPose(std::uintptr_t a, const Entry& e, int& lastMotion, bool writeHp) {
    std::uintptr_t entity = a + kActorEntity;
    // A warp to the origin (see Warped) would hide him under the map on the friend's screen (user test
    // 2026-10-06 23:14): hold him where he was, idle, until he's back.
    bool otherBank = (e.flags & kFlagOtherBank) != 0;
    bool warped = Warped(e);
    auto* pos = reinterpret_cast<float*>(entity + kEntityPos);
    if (!warped) {
        pos[0] = e.pos[0];
        pos[1] = e.pos[1];
        pos[2] = e.pos[2];
    }
    *reinterpret_cast<float*>(entity + kEntityAngle) = e.angle;
    *reinterpret_cast<float*>(entity + kEntitySin) = std::sin(e.angle);
    *reinterpret_cast<float*>(entity + kEntityCos) = std::cos(e.angle);
    std::memset(reinterpret_cast<void*>(a + kActorVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(a + kActorCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(a + kActorAccel), 0, 0x18);
    std::uint32_t motion = otherBank ? 0 : e.motion;  // the Limit's ids aren't in his own set: idle
    if (MotionMirrorKeeps(a, otherBank, static_cast<int>(e.motion))) {
        lastMotion = -1;  // the other game sent which file (motion_mirror.cpp); set ours again after it
    } else if (static_cast<int>(motion) != lastMotion && motion != 0xFFFFFFFF) {
        PuppetSetMotion(a, static_cast<int>(motion));
        lastMotion = static_cast<int>(motion);
    }
    if (!writeHp) return;
    // Not on a KO'd one: companion_sync.cpp KOs it and gets it up the game's way from the host's status, which comes
    // sooner than this pose (played ~110 ms late): its last HP above 0 showed on the KO'd copy (bench 2026-10-21).
    if (Read<std::uint32_t>(a + 0x9B8) & 4) return;
    std::uintptr_t stats = Read<std::uintptr_t>(a + kActorStats);
    if (stats && e.hp > 0 && Read<std::int32_t>(stats) != e.hp) *reinterpret_cast<std::int32_t*>(stats) = e.hp;
}

void ApplyMirror(Mirror& m) {
    Remote* r = FindRemote(kKindCompanion, m.netId);
    Entry e;
    if (!r || !PoseAt(*r, AvatarLinkPlaybackSenderMs(), e)) return;
    ApplyPose(m.actor, e, m.lastMotion, true);
    ++g_mirrorWrites;
}

// ---- The friend's Limit partner in the host's game (TODO 1.57) ----
// During the friend's own Limit its game runs its companions itself (UpdateMirrors) and the Limit plays the
// partner's part on one of them (on both for Trinity); the host's game had that companion go on following the host's
// Sora while the friend's copy played the Limit alone (two-PC run 2026-10-12, Mulan). Now the friend's game sends each
// partner's pose every frame while its Limit runs (a "KH2W" packet with that one companion entry each), and the host's
// companion with the same object id leaves our Sora for it: its AI and its own motions are held, it takes the friend's
// positions and the Limit motions motion_mirror.cpp plays on it, and it takes no damage, until the friend's
// packets stop (its Limit ended) or our own Limit needs it. KH2COOP_PARTNER_LIMIT=0 turns it off.
struct Borrow {                   // host: our companion playing the friend's partner
    std::uintptr_t actor;
    std::uint32_t objId;
    Sample hist[8];               // newest last (the friend's clock)
    int count;
    std::uint32_t lastSeq, lastArrivalMs, writes;
    int lastMotion;
};
Borrow g_borrows[2];  // one per partner (Trinity has two)

Borrow* BorrowFor(std::uintptr_t actor) {
    for (Borrow& b : g_borrows)
        if (actor && b.actor == actor) return &b;
    return nullptr;
}
std::uint32_t g_partnerDamageDropped = 0, g_borrowRefusedLogs = 0;
constexpr std::uint32_t kBorrowTimeoutMs = 400;  // no packet this long: the friend's Limit ended

std::uintptr_t CompanionWithId(std::uint32_t objId) {
    for (int slot = 0; slot < 2; ++slot) {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot);
        if (InExe(a) && ObjId(a) == objId) return a;
    }
    return 0;
}

int CompanionSlot(std::uintptr_t actor) {
    for (int slot = 0; slot < 2; ++slot)
        if (actor && Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot) == actor) return slot;
    return -1;
}

bool OurLimitBusy() { return PuppetOurLimitRunning() || PuppetLimitCommand(); }

// Friend: a partner's pose, every frame while our Limit runs.
void SendPartner(std::uintptr_t partner) {
    int slot = CompanionSlot(partner);
    if (slot < 0) return;
    char buf[sizeof(Header) + sizeof(Entry)];
    auto* h = reinterpret_cast<Header*>(buf);
    auto* e = reinterpret_cast<Entry*>(buf + sizeof(Header));
    if (!FillEntry(*e, partner, ObjId(partner), static_cast<std::uint16_t>(slot + 1), kKindCompanion)) return;
    UseDrawnPlace(*e, partner);
    *h = {kMagic, kVersion, AvatarLinkBuild(), ++g_seq, AvatarLinkNowMs(),
          Read<std::uint8_t>(ExeBase() + kNow), Read<std::uint8_t>(ExeBase() + kNow + 1), 1, 1, g_instance, 0, kLastPart};
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(buf)));
    ++g_partnerSent;
}

void ReleaseBorrow(Borrow& b, const char* why) {
    if (!b.actor) return;
    Log("world: our companion %s is back with our Sora (%s; %u pose writes)", Model(b.objId), why, b.writes);
    b = {};
}

void ReleaseBorrows(const char* why) {
    for (Borrow& b : g_borrows) ReleaseBorrow(b, why);
}

// Host: a partner packet from the friend.
void OnPartnerPacket(const Header& h, const Entry& e) {
    if (!g_partnerLimit || !SaneEntry(e)) return;
    if (static_cast<std::uint16_t>(h.world << 8 | h.room) != RoomKey()) return;
    if (OurLimitBusy()) {  // our own Limit has it
        if (g_borrowRefusedLogs++ < 5) Log("world: the friend's Limit partner %s not shown: our own Limit runs", Model(e.objId));
        return ReleaseBorrows("our own Limit started");
    }
    std::uintptr_t a = CompanionWithId(e.objId);
    if (!a) {
        if (g_borrowRefusedLogs++ < 5)
            Log("world: the friend's Limit partner %s not shown: no companion with that id here", Model(e.objId));
        return;
    }
    Borrow* b = nullptr;  // the borrow for this partner, else a free one
    for (Borrow& x : g_borrows)
        if (x.actor && x.objId == e.objId) b = &x;
    for (Borrow& x : g_borrows)
        if (!b && !x.actor) b = &x;
    if (!b) b = &g_borrows[0];
    if (a != b->actor) {
        ReleaseBorrow(*b, "another partner");
        b->actor = a;
        b->objId = e.objId;
        b->lastMotion = -1;
        Log("world: our companion %s plays the friend's Limit partner (its own AI held until the friend's Limit ends)",
            Model(e.objId));
    }
    b->lastArrivalMs = AvatarLinkNowMs();
    if (b->count && h.seq <= b->lastSeq) return;  // late
    if (b->count == 8) std::memmove(&b->hist[0], &b->hist[1], 7 * sizeof(Sample)), --b->count;
    b->hist[b->count++] = {h.senderMs, e, 0};
    b->lastSeq = h.seq;
}

void UpdateBorrows() {
    for (Borrow& b : g_borrows) {
        if (!b.actor) continue;
        if (AvatarLinkNowMs() - b.lastArrivalMs > kBorrowTimeoutMs) ReleaseBorrow(b, "the friend's Limit ended");
        else if (OurLimitBusy()) ReleaseBorrow(b, "our own Limit started");
        else if (CompanionWithId(b.objId) != b.actor) ReleaseBorrow(b, "it left our party slots");
    }
}

void ApplyBorrow(Borrow& b) {
    Remote r{};
    r.count = b.count;
    std::memcpy(r.hist, b.hist, sizeof(r.hist));
    Entry e;
    if (!PoseAt(r, AvatarLinkPlaybackSenderMs(), e)) return;
    ApplyPose(b.actor, e, b.lastMotion, false);
    ++b.writes;
}

// ---- Friend side: enemy mirror ----
constexpr std::uint32_t kClaimMagic = 0x4532484B;  // "KH2E"
#pragma pack(push, 1)
struct Claim {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t claimId;
    std::uint16_t worldRoom;
    std::uint16_t netId;
    std::int32_t damage, react;
};
#pragma pack(pop)
// Friend: actors spawn_sync.cpp made for a host object with nothing of ours to bind to (WorldSyncMadeFor).
struct MadeFor {
    std::uintptr_t actor;
    std::uint16_t netId;
    std::uint32_t epoch;
};
MadeFor g_madeFor[32] = {};
int g_madeForNext = 0;

// The host object this actor of ours was made for, while that one is still alive in the host's game. Once it is gone
// (bench 2026-10-14: a card killed in the moment ours was made), ours is free for the next host object of its kind;
// kept for the dead one, it stayed parked and, counted as free, stopped every later card from being made.
std::uint16_t MadeForNetId(std::uintptr_t actor) {
    for (const MadeFor& m : g_madeFor) {
        if (m.actor != actor || m.epoch != g_epoch) continue;
        const Remote* r = FindRemote(kKindEnemy, m.netId);
        return r && r->count && r->hist[r->count - 1].e.hp != 0 ? m.netId : 0;
    }
    return 0;
}

std::uint32_t g_claimSendId = 0;
std::uint32_t g_claimsSent = 0, g_ownDamageDropped = 0, g_binds = 0, g_kills = 0, g_parks = 0, g_vanished = 0;
std::uint64_t g_hostEmptySince = 0;

LocalEnemy* LocalFor(std::uintptr_t actor) {
    for (int i = 0; i < g_enemyCount; ++i)
        if (g_enemies[i].actor == actor) return &g_enemies[i];
    return nullptr;
}
std::uint32_t g_targetLogs = 0;

bool HostFresh() { return AvatarLinkNowMs() - g_lastPacketMs < 1000 && g_remoteRoom == RoomKey(); }

// Friend: enemies and field objects that are a copy of the host's from birth (one brain): made by a script of one of
// our bound enemies (the Twin Lords' threads make their fire and ice objects in both games, and ours bind to the
// host's), or made by spawn_sync.cpp for a host object. Their brain is off from the start, so they never run their
// own script. Before (bench 2026-10-15 21:26, you watching): in the one frame before its bind, each fire object of ours
// started its own fire floor, hitbox and thread; nothing ended them (you counted 8 fire floors left on the friend's
// screen), and they kept the game from ever removing the object (its removal test exe+0x3DAC30 wants no script
// threads and no effect records). Once bound it is like any bound enemy; with nothing of the host's bound to it after
// 1 s its brain turns on, as for our other enemies with no match.
struct BornCopy {
    std::uintptr_t actor;
    std::uint32_t objId;
    std::uint64_t frame;
    const char* why;
};
BornCopy g_born[32] = {};
int g_bornNext = 0;
std::uint32_t g_bornCount = 0, g_bornLate = 0;
constexpr std::uint64_t kBornWait = 60;  // frames

void AddBorn(std::uintptr_t actor, const char* why) {
    BornCopy b = {actor, ObjId(actor), g_frame, why};
    if (!b.objId) return;
    ++g_bornCount;
    for (BornCopy& o : g_born)
        if (o.actor == actor) {
            o = b;  // the same memory made again
            return;
        }
    g_born[g_bornNext++ % 32] = b;
}

// Its brain waits: born a copy, an enemy or field object, not bound yet, within the wait.
bool BornWaits(std::uintptr_t actor) {
    for (BornCopy& b : g_born) {
        if (b.actor != actor || !actor) continue;
        std::uint32_t team = Read<std::uint32_t>(actor + kActorTeam);
        if (ObjId(actor) != b.objId || (team != 2 && team != 3)) {
            b = {};
            return false;
        }
        if (g_frame - b.frame <= kBornWait) return true;
        if (++g_bornLate <= 20)
            Log("world: our %s (%s) has nothing of the host's bound to it after 1 s: its brain turns on",
                Model(b.objId), b.why);
        b = {};
        return false;
    }
    return false;
}

// Friend: a bound enemy that runs its own brain for now (shared_move.cpp: a boss's move both play; reaction_sync.cpp:
// our press of a field object's reaction command).
bool OwnBrain(std::uintptr_t actor) { return SharedMoveOwnBrain(actor) || ReactionSyncOwnBrain(actor); }

// Deaths are applied inside the enemy's own update (WorldSyncAfterUpdate), which
// only runs while our game is live. Two-PC test 23:45:12: a kill applied from
// the frame hook while the friend's game was still in the combat pause, then
// the pause closed and the game crashed in its task list (exe+0x14F6B8, freed
// node 0xEFACCAFE).
// death = the host's enemy died: ours dies too (EXP, drops). Otherwise it is only removed (TODO 1.37:
// two-PC test 2026-10-08, enemies that sank away on the host paid the friend EXP and drops each time).
void KillLocal(LocalEnemy& le, const char* why, bool death) {
    if (le.killed || le.killPending) return;
    le.killPending = why;
    le.killIsDeath = death;
}

void ApplyPendingKill(LocalEnemy& le) {
    const char* why = le.killPending;
    le.killPending = nullptr;
    if (le.killed) return;
    le.killed = true;
    if (!le.killIsDeath) {
        auto* fade = reinterpret_cast<float*>(le.actor + kActorFade);
        fade[0] = 0.0f;  // faded out
        fade[1] = 0.0f;  // no fade running
        *reinterpret_cast<std::uint32_t*>(le.actor + kActorFadeFlags) |= kFadeRemoveBit;
        le.vanished = true;
        le.vanishedFrame = g_frame;
        ++g_vanished;
        Log("world: ours#%u %s removed without a death (%s): no EXP, no drops", le.netId, Model(le.objId), why);
        return;
    }
    int hp = Hp(le.actor);
    if (hp > 0) PuppetApplyHp(le.actor, -hp, 0);  // the game's own death: animation, EXP, drops
    ++g_kills;
    Log("world: ours#%u %s killed (%s)", le.netId, Model(le.objId), why);
}

// A removed one the game never takes away (bench 2026-10-18, the Land of Dragons swarm): its own script still runs,
// so the game's shutdown start never comes, and it keeps its weight in the factory's budget (VERIFIED_OFFSETS 'The
// actor factory's budget'); 11 of them left the friend's game room for 2 of the host's 13. 1.5 s after the removal,
// if it's still updated and the game hasn't begun taking it away (+0x120 bit 28), we run that start ourselves:
// exe+0x3B4700 stops its scripts and motion, gives the weight back and sets bit 28; the game disposes of it after.
constexpr std::uintptr_t kShutdownStart = 0x3B4700;
constexpr std::uint8_t kShutdownStartBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81, 0x20, 0x01, 0x00, 0x00};
constexpr std::uint32_t kShutdownBit = 0x10000000;  // +0x120: the game is taking it away
constexpr std::uint64_t kShutdownWait = 90;         // frames after our removal
constexpr std::uintptr_t kBudgetUsed = 0x2A0F830;   // float: the factory's used budget
std::uint32_t g_shutdowns = 0;
bool g_shutdownOk = false;

void FinishRemoval(LocalEnemy& le) {
    if (!g_shutdownOk || !le.vanished || le.shutdownCalled || g_frame - le.vanishedFrame < kShutdownWait) return;
    le.shutdownCalled = true;
    if (Read<std::uint32_t>(le.actor + 0x120) & kShutdownBit) return;  // the game is already on it
    float before = Read<float>(ExeBase() + kBudgetUsed);
    std::uint32_t threads = Read<std::uint32_t>(le.actor + 0x5B8);
    __try {
        reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(ExeBase() + kShutdownStart)(le.actor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_shutdownOk = false;
        Log("world: the game's shutdown start on ours#%u %s failed (exception): off for this session", le.netId,
            Model(le.objId));
        return;
    }
    if (++g_shutdowns <= 30)
        Log("world: ours#%u %s still here %llu frames after its removal (script threads %u): the game's shutdown start "
            "(exe+0x3B4700), budget used %.0f -> %.0f", le.netId, Model(le.objId),
            static_cast<unsigned long long>(g_frame - le.vanishedFrame), threads, before,
            Read<float>(ExeBase() + kBudgetUsed));
}

void UnbindAll(const char* why) {
    int n = 0;
    for (int k = 0; k < g_enemyCount; ++k) {
        LocalEnemy& le = g_enemies[k];
        if (le.bound && !le.killed) ++n;
        le.bound = 0;
        le.parked = false;
        le.firstFrame = g_frame;  // a fresh grace period before parking
    }
    if (n) Log("world: %d enemies unbound (%s)", n, why);
}

void BindEnemies() {
    if (!HostFresh() || g_remotePaused) return;  // a paused host (menu, Game Over, load) decides nothing
    int hostEnemies = 0;
    for (int i = 0; i < g_remoteCount; ++i) {
        const Remote& r = g_remote[i];
        if (r.kind != kKindEnemy || r.count == 0) continue;
        ++hostEnemies;
        // HP -1 = no stat block (field objects such as the ballroom's F_BB080): bound too, for one brain
        // (bench 2026-10-12: left unbound, the friend's own copy stayed parked out of sight all fight).
        int hp = r.hist[r.count - 1].e.hp;
        if (hp == 0) {
            static int hpLogs = 0;
            for (int k = 0; k < g_enemyCount && hpLogs < 20; ++k)
                if (MadeForNetId(g_enemies[k].actor) == r.netId && !g_enemies[k].bound) {
                    ++hpLogs;
                    Log("world: host#%u %s has HP %d: ours#%u made for it not bound", r.netId, Model(r.objId), hp,
                        g_enemies[k].netId);
                }
            continue;
        }
        bool taken = false;
        int takenBy = -1;
        for (int k = 0; k < g_enemyCount && !taken; ++k)
            if ((taken = g_enemies[k].bound == r.netId && !g_enemies[k].killed)) takenBy = k;
        if (taken) {
            static int takenLogs = 0;
            for (int k = 0; k < g_enemyCount && takenLogs < 20; ++k)
                if (MadeForNetId(g_enemies[k].actor) == r.netId && !g_enemies[k].bound && takenBy != k) {
                    ++takenLogs;
                    Log("world: host#%u %s already bound to ours#%u (killed %d), so ours#%u made for it stays unbound",
                        r.netId, Model(r.objId), g_enemies[takenBy].netId, g_enemies[takenBy].killed,
                        g_enemies[k].netId);
                }
            continue;
        }
        int best = -1;
        float bestD = 1e30f;
        // One we made for exactly this host object (spawn_sync.cpp) first.
        for (int k = 0; k < g_enemyCount; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (!le.bound && !le.killed && le.objId == r.objId && MadeForNetId(le.actor) == r.netId) best = k, bestD = 0;
        }
        // Same spawn record on both PCs (serial) next: exact, whatever the positions.
        for (int k = 0; k < g_enemyCount && r.serial && best < 0; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (!le.bound && !le.killed && le.objId == r.objId && le.serial == r.serial) best = k, bestD = 0;
        }
        for (int k = 0; k < g_enemyCount && best < 0; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (le.bound || le.killed || le.objId != r.objId) continue;
            if (le.serial && r.serial) continue;  // both from spawn records but different ones: never the same enemy
            if (std::uint16_t made = MadeForNetId(le.actor); made && made != r.netId) continue;  // made for another
            float dx = le.spawn[0] - r.spawn[0], dy = le.spawn[1] - r.spawn[1], dz = le.spawn[2] - r.spawn[2];
            float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) bestD = d, best = k;
        }
        if (best < 0) {
            // Research: a host object we made one for that still has nothing bound (bench 2026-10-14, Luxord's cards).
            static std::uint16_t whyLogged[32];
            static int whyCount = 0;
            bool made = false, logged = false;
            for (int k = 0; k < g_enemyCount; ++k) made |= MadeForNetId(g_enemies[k].actor) == r.netId;
            for (int k = 0; k < whyCount; ++k) logged |= whyLogged[k] == r.netId;
            if (made && !logged && whyCount < 32) {
                whyLogged[whyCount++] = r.netId;
                for (int k = 0; k < g_enemyCount; ++k) {
                    const LocalEnemy& le = g_enemies[k];
                    if (MadeForNetId(le.actor) == r.netId)
                        Log("world: host#%u %s (HP %d) not bound to ours#%u made for it: bound %u, killed %d, object "
                            "0x%X / 0x%X", r.netId, Model(r.objId), hp, le.netId, le.bound, le.killed, le.objId, r.objId);
                }
            }
            continue;
        }
        LocalEnemy& le = g_enemies[best];
        le.bound = r.netId;
        le.parked = false;
        le.lastMotion = -1;
        ++g_binds;
        Log("world: host#%u %s bound to ours#%u (serial %u/%u, spawn spots %.0f apart)", r.netId, Model(r.objId),
            le.netId, r.serial, le.serial, std::sqrt(bestD));
    }
    // Host enemies that died or vanished: the local copy dies too.
    for (int k = 0; k < g_enemyCount; ++k) {
        LocalEnemy& le = g_enemies[k];
        if (!le.bound || le.killed) continue;
        Remote* r = FindRemote(kKindEnemy, le.bound);
        if (!r || r->count == 0)
            KillLocal(le, "the host's enemy is gone", false);
        else if (r->hist[r->count - 1].e.hp == 0)
            KillLocal(le, "the host's enemy died", true);
    }
    // Local enemies the host doesn't have: park them; once the host has none left, kill them.
    g_hostEmptySince = hostEnemies ? 0 : (g_hostEmptySince ? g_hostEmptySince : g_frame);
    for (int k = 0; k < g_enemyCount; ++k) {
        LocalEnemy& le = g_enemies[k];
        if (le.bound || le.killed) continue;
        if (!le.parked) {  // at once: its spawn animation must not show (test 23:4x)
            le.parked = true;
            le.parkedFrame = g_frame;
            ++g_parks;
            Log("world: ours#%u %s has no host match; parked out of sight", le.netId, Model(le.objId));
        }
        // The host may simply not have reached this spot yet (bench 23:55: the friend
        // spawned a group first; killing it after 2 s left nothing to bind to when
        // the host arrived). Keep it hidden 15 s, and only while the host has no enemies.
        if (le.parked && g_frame - le.parkedFrame > 900 && g_hostEmptySince && g_frame - g_hostEmptySince > 300)
            KillLocal(le, "extra; the host never had it", false);
    }
}

void ApplyEnemy(LocalEnemy& le) {
    std::uintptr_t entity = le.actor + kActorEntity;
    auto* pos = reinterpret_cast<float*>(entity + kEntityPos);
    if (le.parked) {
        pos[0] = le.spawn[0];
        pos[1] = le.spawn[1] + 5000.0f;  // y grows downward: far below the room
        pos[2] = le.spawn[2];
        return;
    }
    Remote* r = FindRemote(kKindEnemy, le.bound);
    Entry e;
    std::uint32_t playback = AvatarLinkPlaybackSenderMs();
    if (!r || !PoseAt(*r, playback, e)) return;
    std::uint8_t target = (e.flags & kFlagOnFriend) ? 2 : (e.flags & kFlagOnHost) ? 1 : 0;
    if (target != le.hostTarget && g_targetLogs < 40) {
        ++g_targetLogs;
        Log("world: our copy of the host's enemy #%u now goes for %s (the host's pick)", le.bound,
            target == 2 ? "our Sora" : target == 1 ? "the host's copy" : "neither player");
    }
    le.hostTarget = target;
    if (std::uint16_t scale = ScaleAt(*r, playback))
        *reinterpret_cast<float*>(le.actor + kActorScaleY) = scale / 4096.0f;
    // Attached in both games (e.g. grab sync's grab on our Sora): our attachment draws it from its parent's bone,
    // so writing the host's place and facing only bends it (you, 2026-10-14: Data Roxas turned to the right in the
    // clash). Attached only here: written as usual, so a copy left attached isn't stuck there, but not in its first
    // half second: grab sync runs the host's attach as soon as it comes, while our samples of the host's enemy are
    // about 100 ms behind and still say 'not attached'. The attach set our copy's place to (0, 0, 0), its offset from
    // the parent's bone; the host's place written there puts it far off (you, bench 2026-10-15 21:52:36: Data Roxas
    // not seen during his Keyblade steal on the friend's Sora, his offset left at (-1003, 0, -442)). A Limit partner
    // hidden at the origin by the host (Warped): keep it where it was.
    bool oursAttached = AttachedTo(le.actor) != 0;
    bool bothAttached = (e.flags & kFlagAttached) && oursAttached;
    bool aloneWait = false;
    if (oursAttached && !(e.flags & kFlagAttached)) {
        if (!le.attachedAlone) le.attachedAlone = g_frame;
        aloneWait = g_frame - le.attachedAlone < kAttachedAloneWait;
        if (aloneWait) ++g_attachedAloneHeld;
    } else {
        le.attachedAlone = 0;
    }
    if (!bothAttached && !aloneWait && !Warped(e)) {
        // Position and facing only: the velocity fields cleared on companions were
        // checked on party members; an enemy's layout past the shared part may differ.
        pos[0] = e.pos[0];
        pos[1] = e.pos[1];
        pos[2] = e.pos[2];
        *reinterpret_cast<float*>(entity + kEntityAngle) = e.angle;
        *reinterpret_cast<float*>(entity + kEntitySin) = std::sin(e.angle);
        *reinterpret_cast<float*>(entity + kEntityCos) = std::cos(e.angle);
    }
    if (!le.killed && static_cast<int>(e.motion) != le.lastMotion && e.motion != 0xFFFFFFFF) {
        PuppetSetMotion(le.actor, static_cast<int>(e.motion));
        le.lastMotion = static_cast<int>(e.motion);
    }
    // The host's motion clock, when ours plays the same motion of its own set and has drifted (your pick: every
    // enemy, only when off, so normal animations don't stutter).
    const Sample* s = SampleAt(*r, playback);
    if (!le.killed && s && s->e.motion == e.motion && !(e.flags & kFlagOtherBank) &&
        Read<std::int32_t>(le.actor + kActorMotion) == static_cast<std::int32_t>(e.motion) &&
        !Read<std::uintptr_t>(le.actor + kActorMotionBank)) {
        float ours = Read<float>(le.actor + kActorMotionClock);
        if (std::fabs(ours - s->clock) > kClockSlack) {
            *reinterpret_cast<float*>(le.actor + kActorMotionClock) = s->clock;
            if (g_clockFixes++ < 30)
                Log("world: ours#%u %s motion %u clock %.0f -> the host's %.0f", le.netId, Model(le.objId), e.motion,
                    ours, s->clock);
        }
    }
    std::uintptr_t stats = Read<std::uintptr_t>(le.actor + kActorStats);
    if (!le.killed && stats && Readable4(stats) && e.hp > 0) {
        int before = Read<std::int32_t>(stats);
        if (before != e.hp) {
            *reinterpret_cast<std::int32_t*>(stats) = e.hp;
            TellHpBar(le.actor, before, e.hp);
        }
    }
}

void SendClaim(std::uint16_t netId, int damage, int react) {
    Claim c {kClaimMagic, kVersion, AvatarLinkBuild(), ++g_claimSendId, RoomKey(), netId, damage, react};
    for (int i = 0; i < 3; ++i) AvatarLinkSendRaw(&c, sizeof(c));
    ++g_claimsSent;
}

// ---- Host side: claims from the friend ----
struct PendingClaim {
    std::uint16_t netId;
    std::int32_t damage, react;
};
PendingClaim g_claims[32];
int g_claimCount = 0;
std::uint32_t g_claimSeenMax = 0;
std::uint64_t g_claimSeenWindow = 0;
std::uint32_t g_claimsApplied = 0;
constexpr std::uintptr_t kRecordAttacker = 0x411300;  // (enemy, attacker): see aggro.cpp
using PFN_RecordAttacker = void(__fastcall*)(std::uintptr_t, std::uintptr_t);

// TODO 1.40 (your decision 2026-10-13, option (a)): the friend's damage reaches the host's enemies only through its
// claims (KH2COOP_OWN_HITS_ONLY=1, default). Each hit's attacker comes from the game's RecordAttacker call for that
// hit (hook in aggro.cpp; it runs just before the HP change of the same hit). enemy+0xD38 alone can be stale: our
// ApplyClaims records the copy there after every claim, and that call is left out here.
bool g_ownHitsOnly = false;
bool g_recordingClaim = false;
std::uintptr_t g_recEnemy = 0, g_recAttacker = 0;

// This hit's attacker; *known = false when no RecordAttacker ran for this enemy since its last HP loss.
std::uintptr_t HitAttacker(std::uintptr_t enemy, bool* known) {
    *known = g_recEnemy && g_recEnemy == enemy;
    std::uintptr_t who = *known ? g_recAttacker : 0;
    g_recEnemy = 0;
    return who;
}

bool FirstClaim(std::uint32_t id) {
    if (id > g_claimSeenMax) {
        std::uint32_t shift = id - g_claimSeenMax;
        g_claimSeenWindow = shift >= 64 ? 0 : g_claimSeenWindow << shift;
        g_claimSeenWindow |= 1;
        g_claimSeenMax = id;
        return true;
    }
    std::uint32_t back = g_claimSeenMax - id;
    if (back >= 64 || (g_claimSeenWindow & (1ull << back))) return false;
    g_claimSeenWindow |= 1ull << back;
    return true;
}

// The game's own rule (hit step exe+0x3D3BA0, class check +0xA8, base exe+0x3D2A20): an enemy whose flags
// (+0xD10 | +0xD14) & ~+0xD18 have bit 9 can't die; a hit that would take it to its min HP (stat +8) leaves min + 1.
// Final Xemnas's script sets it (trap_obj_pattern_enable(9)) to stay at 1 HP until his desperation move; the
// friend's claims skip that step and took him 1 -> 0 (two-PC run 2026-10-22, TODO 1.112). The lowest HP a claim
// may leave, or 0 when the game would let it die.
int KeptAliveHp(std::uintptr_t actor) {
    if (!Readable4(actor + 0xD10) || !Readable4(actor + 0xD18)) return 0;
    std::uint32_t flags = (Read<std::uint32_t>(actor + 0xD10) | Read<std::uint32_t>(actor + 0xD14)) &
                          ~Read<std::uint32_t>(actor + 0xD18);
    if (!(flags & (1u << 9))) return 0;
    std::uintptr_t stats = Read<std::uintptr_t>(actor + kActorStats);
    if (!stats || !Readable4(stats + 8)) return 0;
    return Read<std::int32_t>(stats + 8) + 1;
}

void ApplyClaims(LocalEnemy& le) {
    for (int i = 0; i < g_claimCount;) {
        if (g_claims[i].netId != le.netId) {
            ++i;
            continue;
        }
        int before = Hp(le.actor);
        // You, 2026-10-14: while a boss's script holds a player (a grab, Final Xemnas's laser barrage), the friend's
        // hits still count but can't kill it: in one-player play nobody can hit it then, and a kill in the first frame
        // of the barrage cut the move short and crashed our game in his laser call (bench 2026-10-14 11:32:01).
        int damage = g_claims[i].damage;
        bool spared = damage >= before && before > 0 && GrabSyncHoldsPlayer(le.actor);
        if (spared) {
            damage = before - 1;
            Log("world: friend hit our#%u %s for %d while it holds a player: %s", le.netId, Model(le.objId),
                g_claims[i].damage, damage > 0 ? "HP kept at 1" : "no damage (at 1 HP)");
        }
        // The game's rule: Mickey's hits leave a boss at 1 HP at least. The friend's Mickey hits arrive here as its
        // claims and took Shan-Yu's last HP (two-PC run 2026-10-22, TODO 1.109). While the friend's Mickey is out,
        // its claims keep a boss (model B_...) at 1 HP or more.
        const char* model = Model(le.objId);
        if (!spared && damage >= before && before > 0 && model && model[0] == 'B' && model[1] == '_' &&
            MickeyTheirsOut()) {
            damage = before - 1;
            Log("world: friend's Mickey hit our#%u %s for %d: %s (Mickey can't finish a boss)", le.netId, model,
                g_claims[i].damage, damage > 0 ? "HP kept at 1" : "no damage (at 1 HP)");
        }
        int keep = KeptAliveHp(le.actor);
        if (keep > 0 && before > 0 && before - damage < keep) {
            damage = before > keep ? before - keep : 0;
            Log("world: friend hit our#%u %s for %d while the game keeps it alive: %s", le.netId, model,
                g_claims[i].damage, damage > 0 ? "HP kept at its floor" : "no damage (at its floor)");
        }
        if (before > 0 && damage > 0) {
            int after = PuppetApplyHit(le.actor, damage, g_claims[i].react);
            if (std::uintptr_t copy = PuppetCloneActor()) {
                g_recordingClaim = true;
                reinterpret_cast<PFN_RecordAttacker>(ExeBase() + kRecordAttacker)(le.actor, copy);
                g_recordingClaim = false;
            }
            ++g_claimsApplied;
            Log("world: friend hit our#%u %s for %d: HP %d -> %d", le.netId, Model(le.objId), g_claims[i].damage,
                before, after);
        }
        g_claims[i] = g_claims[--g_claimCount];
    }
}

// ---- Host side: build and send ----
// One entity, read on its own: a dying enemy's stat record can already be
// freed (bench 22:47:27: one dead Shadow made every packet throw for 11 s).
bool FillEntry(Entry& e, std::uintptr_t a, std::uint32_t objId, std::uint16_t netId, std::uint8_t kind) {
    __try {
        std::uintptr_t entity = a + kActorEntity;
        e.objId = objId;
        e.netId = netId;
        e.kind = kind;
        e.flags = (Read<std::uint32_t>(entity + kEntityAirborne) ? kFlagAirborne : 0) |
                  (Read<std::uintptr_t>(a + kActorMotionBank) ? kFlagOtherBank : 0);
        if (kind == kKindEnemy) {
            int target = AggroTargetOf(a);
            e.flags |= target == 1 ? kFlagOnHost : target == 2 ? kFlagOnFriend : 0;
        }
        std::memcpy(e.pos, reinterpret_cast<const void*>(entity + kEntityPos), sizeof(e.pos));
        e.angle = Read<float>(entity + kEntityAngle);
        e.motion = Read<std::uint32_t>(a + kActorMotion);
        // Attached (held, on a bone, a grab, a Limit partner): send where it is drawn, as UseDrawnPlace does for
        // the Limit partner, so a copy that isn't attached in the other game shows up there (you, 2026-10-14:
        // "animations in general get their correct position", held things floating away).
        if (AttachedTo(a)) {
            const float* m = reinterpret_cast<const float*>(a + kActorMatrix);
            e.flags |= kFlagAttached;
            std::memcpy(e.pos, m + 12, sizeof(e.pos));
            e.angle = std::atan2(-m[8], -m[10]);  // row 2 = (-sin, 0, -cos) of the facing
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    __try {
        e.hp = Hp(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        e.hp = -1;
    }
    return true;
}

std::uint32_t g_partsSent = 0, g_framesInParts = 0;  // frames that needed more than one part
int g_partsLastFrame = 0;

void SendWorld() {
    Entry companionsNow[2];
    int companions = 0;
    for (int slot = 0; slot < 2; ++slot) {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot);
        if (InExe(a) && FillEntry(companionsNow[companions], a, ObjId(a), static_cast<std::uint16_t>(slot + 1), kKindCompanion)) {
            UseDrawnPlace(companionsNow[companions], a);  // our Limit's partner (your pick 2026-10-13: like the friend's, TODO 1.57)
            ++companions;
        }
    }
    const LocalEnemy* sent[kMaxEntries];
    int enemies = 0;
    for (int i = 0; i < g_enemyCount && enemies < kMaxEntries; ++i)
        if (g_enemies[i].cachedOk) sent[enemies++] = &g_enemies[i];
    // Companions go in the first part; then as many enemies (entry + spawn) as fit in each part.
    constexpr int kPerEnemy = static_cast<int>(sizeof(Entry) + sizeof(EnemySpawn));
    std::uint32_t seq = ++g_seq;
    std::uint32_t nowMs = AvatarLinkNowMs();
    std::uint8_t world = Read<std::uint8_t>(ExeBase() + kNow), room = Read<std::uint8_t>(ExeBase() + kNow + 1);
    std::uint8_t flags = static_cast<std::uint8_t>(g_updatesThisFrame == 0 ? kPaused : 0);
    int next = 0;
    for (int part = 0;; ++part) {
        char buf[kMaxPartBytes];
        int c = part == 0 ? companions : 0;
        int room4 = (g_partBytes - static_cast<int>(sizeof(Header)) - c * static_cast<int>(sizeof(Entry))) / kPerEnemy;
        int e = (std::min)(enemies - next, room4);
        int n = c + e;
        auto* h = reinterpret_cast<Header*>(buf);
        auto* entries = reinterpret_cast<Entry*>(buf + sizeof(Header));
        auto* spawns = reinterpret_cast<EnemySpawn*>(buf + sizeof(Header) + n * sizeof(Entry));
        for (int k = 0; k < c; ++k) entries[k] = companionsNow[k];
        for (int k = 0; k < e; ++k) {
            const LocalEnemy& le = *sent[next + k];
            entries[c + k] = le.cached;  // captured right after the game updated it; never re-read here
            std::memcpy(spawns[k].pos, le.spawn, sizeof(spawns[k].pos));
            spawns[k].serial = le.serial;
            spawns[k].scaleY = le.cachedScale;
            spawns[k].clock = le.cachedClock;
        }
        next += e;
        bool last = next >= enemies;
        *h = {kMagic, kVersion, AvatarLinkBuild(), seq, nowMs, world, room, static_cast<std::uint8_t>(n),
              static_cast<std::uint8_t>(c), g_instance, flags,
              static_cast<std::uint8_t>((part & 0x7F) | (last ? kLastPart : 0))};
        AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Header) + n * sizeof(Entry) + e * sizeof(EnemySpawn)));
        ++g_partsSent;
        if (last) {
            if (part > 0) ++g_framesInParts;
            g_partsLastFrame = part + 1;
            break;
        }
    }
}

// ---- Census log (friend side, every 10 s) ----
void LogCensus() {
    int hostEnemies = 0, matched = 0;
    for (int i = 0; i < g_remoteCount; ++i) hostEnemies += g_remote[i].kind == kKindEnemy;
    for (int i = 0; i < g_enemyCount; ++i) g_enemies[i].matched = false;
    char unmatched[512] = {};
    int len = 0;
    for (int i = 0; i < g_remoteCount; ++i) {
        const Remote& r = g_remote[i];
        if (r.kind != kKindEnemy) continue;
        int best = -1;
        float bestD = 60.0f * 60.0f;
        for (int k = 0; k < g_enemyCount; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (le.matched || le.objId != r.objId) continue;
            float dx = le.spawn[0] - r.spawn[0], dy = le.spawn[1] - r.spawn[1], dz = le.spawn[2] - r.spawn[2];
            float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) bestD = d, best = k;
        }
        if (best >= 0) {
            g_enemies[best].matched = true;
            ++matched;
        } else if (len < 400) {
            len += std::snprintf(unmatched + len, sizeof(unmatched) - len, " host#%u %s@(%.0f,%.0f,%.0f)", r.netId,
                                 Model(r.objId), r.spawn[0], r.spawn[1], r.spawn[2]);
        }
    }
    for (int k = 0; k < g_enemyCount && len < 400; ++k)
        if (!g_enemies[k].matched)
            len += std::snprintf(unmatched + len, sizeof(unmatched) - len, " ours#%u %s@(%.0f,%.0f,%.0f)",
                                 g_enemies[k].netId, Model(g_enemies[k].objId), g_enemies[k].spawn[0],
                                 g_enemies[k].spawn[1], g_enemies[k].spawn[2]);
    if (hostEnemies == 0 && g_enemyCount == 0) return;
    Log("world: enemies host %d, ours %d, matched %d (same object, spawn within 60)%s%s", hostEnemies, g_enemyCount,
        matched, len ? "; unmatched:" : "", unmatched);
}

// One brain (world_mirror.cpp), friend, with the census every 10 s: how many of our enemies copy the host's, and
// which ones still think for themselves and why (several enemies at once, you 2026-10-13: "keep substantial logs").
void LogOneBrain() {
    int alive = 0, off = 0, len = 0;
    char own[400] = {};
    for (int k = 0; k < g_enemyCount; ++k) {
        const LocalEnemy& le = g_enemies[k];
        if (le.killed || le.vanished) continue;
        ++alive;
        if (le.bound) {
            ++off;
            continue;
        }
        if (len < 340)
            len += std::snprintf(own + len, sizeof(own) - len, " ours#%u %s (%s)", le.netId, Model(le.objId),
                                 le.parked ? "no host match, parked" : "not bound yet");
    }
    if (alive)
        Log("world mirror: one brain: %d of our %d enemies copy the host's (their brain off)%s%s", off, alive,
            len ? "; thinking on their own:" : "", own);
    // Research (session 36, Twin Lords): removed copies the game hasn't disposed of. Its removal test for them
    // (exe+0x3DAC30, Ghidra) wants no script threads (+0x5B8), no effect records (+0x80 / +0x98) and nothing playing
    // in the slot at +0xBB4 (its +0x14).
    static int lingerLogs = 0;
    for (int k = 0; k < g_enemyCount && lingerLogs < 80; ++k) {
        const LocalEnemy& le = g_enemies[k];
        if (!le.vanished || g_frame - le.lastFrame > 2) continue;
        std::uint32_t threads = 0, f120 = 0, slotPlaying = 0;
        std::uintptr_t root = 0, fx0 = 0, fxNewest = 0, slot = 0;
        __try {
            threads = Read<std::uint32_t>(le.actor + 0x5B8);
            root = Read<std::uintptr_t>(le.actor + 0x5B0);
            fx0 = Read<std::uintptr_t>(le.actor + 0x80);
            fxNewest = Read<std::uintptr_t>(le.actor + 0x98);
            f120 = Read<std::uint32_t>(le.actor + 0x120);
            slot = DecodeHandle(Read<std::uint32_t>(le.actor + 0xBB4));
            if (slot) slotPlaying = Read<std::uint32_t>(slot + 0x14);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        // The records (newest first; record +0x40 the effect, +0x48 its entry with the effect id at +2, +0x58 age,
        // +0x70 flags, +0x74 the next one's handle): effect flags +0xC (0x10000 killed, 0xEFACCAFE freed) and part +0x30.
        char recs[200] = {};
        int len = 0;
        __try {
            std::uintptr_t r = fxNewest;
            for (int i = 0; r && i < 4 && len < 160; ++i) {
                std::uintptr_t fx = Read<std::uintptr_t>(r + 0x40), entry = Read<std::uintptr_t>(r + 0x48);
                len += std::snprintf(recs + len, sizeof(recs) - len, " [id %d age %.0f flags 0x%X fx 0x%X part %s]",
                                     entry ? Read<std::uint16_t>(entry + 2) : -1, Read<float>(r + 0x58),
                                     Read<std::uint32_t>(r + 0x70), fx ? Read<std::uint32_t>(fx + 0xC) : 0,
                                     fx && Read<std::uintptr_t>(fx + 0x30) ? "yes" : "no");
                r = DecodeHandle(Read<std::uint32_t>(r + 0x74));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        ++lingerLogs;
        Log("world: ours#%u %s removed but still here: script threads %u (root %s), effect records %s/%s, slot %s "
            "(playing 0x%X), +0x120 0x%X;%s", le.netId, Model(le.objId), threads, root ? "yes" : "no", fx0 ? "yes" : "no",
            fxNewest ? "yes" : "no", slot ? "yes" : "no", slotPlaying, f120, recs);
    }
}

// ---- Parity sampler (KH2COOP_PARITY=1 on both PCs; tools/parity_compare.py compares the two logs) ----
// Every 250 ms (KH2COOP_PARITY_MS) of the host's clock, one line with the state each tracked enemy's own update
// left: on the host at that time, on the friend when its playback shows that time (AvatarLinkPlaybackSenderMs),
// so a host line and a friend line with the same t describe the same moment of the host's game. On the friend,
// position, motion and HP of a bound enemy are written by the mirror, so they only show it was applied; collision,
// flags, fade, attack patterns and script threads are what each game's own scripts (with one brain: the host's
// replayed calls) left. Entry fields: id = the host's enemy number (friend: the host enemy it copies, 0 = none),
// obj model, ser spawn serial, hp, pos, mot motion (bank = from another file), col +0x18C, cl the 8 bytes at
// +0x5A8 (collision switch at +0x5AC), f120 +0x120, f9b8 +0x9B8, fade +0xA08, pat +0xD10, thr +0x5B8 script
// threads, f8c8 +0x8C8 (bit 6: a Shadow's puddle form, trap_obj_shadow_move_start / end), sy the height scale
// +0x34 (a mirrored field since 2026-10-13); friend only: ours = its own number, st = b bound, p parked, k killed, v vanished, s brain skipped.
int g_parityMs = 0;  // 0 = off
int g_parityDump = 0;  // KH2COOP_PARITY_DUMP=N (research): every N-th sample also logs each enemy's whole actor
std::uint32_t g_parityTick = 0;
bool g_parityArmed = false;
std::uint32_t g_parityT = 0;  // friend: the host time its playback showed for this sample
char g_parityBuf[12288];
int g_parityLen = 0, g_parityCount = 0, g_parityDropped = 0;

void ParityAdd(const LocalEnemy& le) {
    if (g_parityLen > static_cast<int>(sizeof(g_parityBuf)) - 400) {
        ++g_parityDropped;
        return;
    }
    if (!g_host && g_parityCount == 0) g_parityT = AvatarLinkPlaybackSenderMs();  // the pose just applied
    std::uintptr_t a = le.actor;
    char st[8] = "-";
    if (!g_host) {
        int n = 0;
        if (le.bound) st[n++] = 'b';
        if (le.parked) st[n++] = 'p';
        if (le.killed) st[n++] = 'k';
        if (le.vanished) st[n++] = 'v';
        if (WorldSyncSkipsBrain(a)) st[n++] = 's';
        st[n ? n : 1] = 0;
    }
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kActorObjRow);
        const char* model = InExe(row) ? reinterpret_cast<const char*>(row + 8) : "?";
        const float* pos = reinterpret_cast<const float*>(a + kActorEntity + kEntityPos);
        char* out = g_parityBuf + g_parityLen;
        int space = static_cast<int>(sizeof(g_parityBuf)) - g_parityLen;
        int w = std::snprintf(out, space,
                              " ; id=%u obj=%.15s ser=%u hp=%d pos=%.0f,%.0f,%.0f mot=%d bank=%d col=%02X cl=%016llX "
                              "f120=%X f9b8=%X fade=%.2f pat=%X thr=%u f8c8=%X sy=%.3f",
                              g_host ? le.netId : le.bound, model, le.serial, Hp(a), pos[0], pos[1], pos[2],
                              Read<std::int32_t>(a + kActorMotion), Read<std::uintptr_t>(a + kActorMotionBank) ? 1 : 0,
                              Read<std::uint8_t>(a + 0x18C), static_cast<unsigned long long>(Read<std::uint64_t>(a + 0x5A8)),
                              Read<std::uint32_t>(a + 0x120), Read<std::uint32_t>(a + kActorFadeFlags),
                              Read<float>(a + kActorFade), Read<std::uint32_t>(a + 0xD10), Read<std::uint32_t>(a + 0x5B8),
                              Read<std::uint32_t>(a + 0x8C8), Read<float>(a + kActorScaleY));
        if (w > 0 && w < space) {  // where it is drawn (world matrix row 3) and what it is attached to (+0x6A0)
            const float* m = reinterpret_cast<const float*>(a + kActorMatrix);
            std::uintptr_t parent = DecodeHandle(Read<std::uint32_t>(a + 0x6A0));
            char att[40] = "-";
            if (parent) {
                std::uintptr_t prow = Read<std::uintptr_t>(parent + kActorObjRow);
                std::snprintf(att, sizeof(att), "%.15s/%u/%X", InExe(prow) ? reinterpret_cast<const char*>(prow + 8) : "?",
                              Read<std::uint16_t>(a + 0x6A4), Read<std::uint16_t>(a + 0x6A6));
            }
            int w2 = std::snprintf(out + w, space - w, " drw=%.0f,%.0f,%.0f att=%s", m[12], m[13], m[14], att);
            if (w2 > 0 && w + w2 < space) w += w2;
        }
        if (w > 0 && w < space && !g_host) {
            int w2 = std::snprintf(out + w, space - w, " ours=%u st=%s", le.netId, st);
            if (w2 > 0 && w + w2 < space) w += w2;
        }
        if (w > 0 && w < space) {
            g_parityLen += w;
            ++g_parityCount;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_parityBuf[g_parityLen] = 0;
        ++g_parityDropped;
    }
    if (g_parityDump && g_parityTick % g_parityDump == 0) {  // which field makes a friend's Shadow look flat (TODO 1.2)
        static char hex[0xD50 * 2 + 1];
        __try {
            const auto* b = reinterpret_cast<const std::uint8_t*>(a);
            for (int i = 0; i < 0xD50; ++i) std::snprintf(hex + 2 * i, 3, "%02X", b[i]);
            Log("parity-dump: %c t=%u id=%u at=%llX %s", g_host ? 'H' : 'F', g_host ? AvatarLinkNowMs() : g_parityT,
                g_host ? le.netId : le.bound, static_cast<unsigned long long>(a - ExeBase()), hex);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

// After an actor's update (and on the friend, after the mirror wrote it), while a sample is open.
void ParitySample(std::uintptr_t actor) {
    if (!g_parityArmed) return;
    if (const LocalEnemy* le = LocalFor(actor)) ParityAdd(*le);
}

// The sample opened last frame: every actor has updated since. One line, then open the next one when the clock
// reaches the next step.
void ParityFrame() {
    if (g_parityArmed) {
        g_parityArmed = false;
        float sp[3] = {};
        int sm = -1;
        std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + 0x2A171C8);
        if (sora) {
            std::memcpy(sp, reinterpret_cast<const void*>(sora + kActorEntity + kEntityPos), sizeof(sp));
            sm = Read<std::int32_t>(sora + kActorMotion);
        }
        if (g_host)  // t = the time this frame's packet (seq) carries these states with
            Log("parity: H t=%u room=%04X inst=%u seq=%u upd=%u sora=%.0f,%.0f,%.0f,%d n=%d drop=%d%s",
                AvatarLinkNowMs(), RoomKey(), g_instance, g_seq + 1, g_updatesThisFrame, sp[0], sp[1], sp[2], sm,
                g_parityCount, g_parityDropped, g_parityBuf);
        else
            Log("parity: F t=%u room=%04X hroom=%04X hinst=%u seq=%u lead=%d upd=%u sora=%.0f,%.0f,%.0f,%d n=%d drop=%d%s",
                g_parityT, RoomKey(), g_remoteRoom, g_remoteInstance, g_lastPacketSeq,
                static_cast<int>(g_lastPacketSenderMs - g_parityT), g_updatesThisFrame, sp[0], sp[1], sp[2], sm,
                g_parityCount, g_parityDropped, g_parityBuf);
    }
    std::uint32_t t = g_host ? AvatarLinkNowMs() : AvatarLinkPlaybackSenderMs();
    if (!t || t / g_parityMs == g_parityTick) return;
    g_parityTick = t / g_parityMs;
    g_parityArmed = true;
    g_parityT = t;
    g_parityLen = 0;
    g_parityCount = 0;
    g_parityDropped = 0;
    g_parityBuf[0] = 0;
}

// Debug (tools/debug_cmd.py bosshp, automated tests): every boss (model name B_...) gets this HP as it next
// updates, for 1 s; each boss once.
int g_debugBossHp = -1;
std::uint64_t g_debugBossUntil = 0;
std::uintptr_t g_debugBossDone[8];
int g_debugBossDoneCount = 0;

void DebugBossHp(std::uintptr_t actor) {
    if (g_frame > g_debugBossUntil) {
        Log("world: debug boss HP %d: %d boss(es) set", g_debugBossHp, g_debugBossDoneCount);
        g_debugBossHp = -1;
        return;
    }
    if (Read<std::uint32_t>(actor + kActorTeam) != 2 || g_debugBossDoneCount == 8) return;
    for (int i = 0; i < g_debugBossDoneCount; ++i)
        if (g_debugBossDone[i] == actor) return;
    const char* model = Model(ObjId(actor));
    if (model[0] != 'B' || model[1] != '_') return;
    std::uintptr_t stats = Read<std::uintptr_t>(actor + kActorStats);
    if (!stats || !Readable4(stats + 4)) return;
    auto channel = reinterpret_cast<std::int32_t*>(stats);  // current, max
    int before = channel[0];
    channel[0] = g_debugBossHp < channel[1] ? g_debugBossHp : channel[1];
    g_debugBossDone[g_debugBossDoneCount++] = actor;
    Log("world: debug boss HP: %s (exe+0x%llX) %d -> %d of %d", model,
        static_cast<unsigned long long>(actor - ExeBase()), before, channel[0], channel[1]);
}

}  // namespace

void WorldSyncDebugBossHp(int hp) {
    g_debugBossHp = hp;
    g_debugBossUntil = g_frame + 60;
    g_debugBossDoneCount = 0;
    Log("world: debug boss HP %d asked for", hp);
}

void WorldSyncInit(bool host) {
    g_on = EnvInt("KH2COOP_WORLD_SYNC", 1) == 1;
    g_host = host;
    g_trace = g_on && EnvInt("KH2COOP_ENEMY_TRACE", 0) == 1;
    g_partBytes = (std::max)(200, (std::min)(kMaxPartBytes, EnvInt("KH2COOP_WORLD_PART_BYTES", kMaxPartBytes)));
    g_claimLog = g_on && EnvInt("KH2COOP_CLAIM_LOG", 0) == 1;
    g_ownCarriers = g_on && EnvInt("KH2COOP_OWN_CARRIERS", 1) == 1;
    g_ownHitsOnly = g_on && EnvInt("KH2COOP_OWN_HITS_ONLY", 1) == 1;
    g_shutdownOk = g_on && !host && EnvInt("KH2COOP_FINISH_REMOVAL", 1) == 1 &&
                   std::memcmp(reinterpret_cast<const void*>(ExeBase() + kShutdownStart), kShutdownStartBytes,
                               sizeof(kShutdownStartBytes)) == 0;
    if (g_on && !host)
        Log("world sync: removed enemies still here after %llu frames get the game's shutdown start: %s",
            static_cast<unsigned long long>(kShutdownWait),
            g_shutdownOk ? "on" : EnvInt("KH2COOP_FINISH_REMOVAL", 1) == 1 ? "off (bytes differ)" : "off (FINISH_REMOVAL=0)");
    if (g_ownHitsOnly)
        Log("world sync: the friend's damage counts only through its claims (%s)",
            host ? "hits by the friend's Sora copy take no HP here" : "only hits by our own Sora, summon and Limit are sent");
    g_partnerLimit = g_on && EnvInt("KH2COOP_PARTNER_LIMIT", 1) == 1;
    if (g_on)
        Log("world sync: the friend's Limit partner %s", !g_partnerLimit ? "not shown in the host's game (PARTNER_LIMIT=0)"
                                                         : host ? "played by our companion with its id while the friend's Limit runs"
                                                                : "sent to the host while our Limit runs");
    if (g_on && EnvInt("KH2COOP_PARITY", 0) == 1) {
        g_parityMs = (std::max)(50, EnvInt("KH2COOP_PARITY_MS", 250));
        g_parityDump = (std::max)(0, EnvInt("KH2COOP_PARITY_DUMP", 0));
        Log("world sync: parity sampler on, every %d ms of the host's clock (tools/parity_compare.py)", g_parityMs);
    }
    if (g_on) {
        Log("world sync: on (%s)", host ? "host: sending companions and enemies"
                                        : "friend: mirroring companions, enemy census");
        SpawnSyncInit(host);
        WorldMirrorInit(host);
        ReactionSyncInit(host);
        GrabSyncInit(host);
        SharedMoveInit(host);
        LimitSyncInit(host);
        CompanionSyncInit(host);
        MissionSyncInit(host);
    }
}

void WorldSyncOnPacket(const char* buf, int n) {
    if (!g_on || n < static_cast<int>(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, buf, sizeof(h));
    if (h.version != kVersion || h.build != AvatarLinkBuild()) return;  // the link already warns about builds
    if (g_host) {  // from the friend: only its Limit partner's pose (TODO 1.57)
        if (h.count == 1 && h.companions == 1 && n == static_cast<int>(sizeof(Header) + sizeof(Entry))) {
            Entry e;
            std::memcpy(&e, buf + sizeof(Header), sizeof(e));
            OnPartnerPacket(h, e);
        }
        return;
    }
    int companions = h.companions;
    if (n != static_cast<int>(sizeof(Header) + h.count * sizeof(Entry) + (h.count - companions) * sizeof(EnemySpawn)))
        return;
    // Packets can arrive out of order (Wi-Fi; the network simulator's jitter). A late one is older than what we
    // already have: drop it whole. Applied, it would flip the room back and drop every entity whose newest sample
    // is newer than it (the loop at the end), and the bosses bound to those would be removed for good (bench
    // 2026-10-12 11:24, NET_SIM=150,30,2: the friend's two Final Xemnas removed 0.1 s after binding, 'the host's
    // enemy is gone'; the friend fought nothing). A big jump back = the host restarted.
    // The parts of one frame share its seq: only an older seq is late.
    if (g_lastPacketSeq && h.seq < g_lastPacketSeq && h.seq + 600 > g_lastPacketSeq) {
        if (g_latePackets++ < 5) Log("world: a late host packet dropped (seq %u, newest %u)", h.seq, g_lastPacketSeq);
        return;
    }
    g_lastPacketSeq = h.seq;
    g_lastPacketSenderMs = h.senderMs;
    std::uint16_t room = static_cast<std::uint16_t>(h.world << 8 | h.room);
    if (room != g_remoteRoom || h.instance != g_remoteInstance) {
        if (g_remoteInstance != 0xFFFF) UnbindAll("the host's room was reloaded or changed");
        ++g_epoch;
        g_remoteRoom = room;
        g_remoteInstance = h.instance;
        g_remoteCount = 0;  // new host room instance: forget the old entities
    }
    g_remotePaused = (h.flags & kPaused) != 0;
    g_lastPacketMs = AvatarLinkNowMs();
    if (g_remotePaused) return;  // nothing updated on the host: keep showing the last state
    const auto* entries = reinterpret_cast<const Entry*>(buf + sizeof(Header));
    const auto* spawns = reinterpret_cast<const EnemySpawn*>(buf + sizeof(Header) + h.count * sizeof(Entry));
    for (int i = 0; i < h.count; ++i) {
        Entry e;
        std::memcpy(&e, &entries[i], sizeof(e));
        if (!SaneEntry(e)) {
            if (g_badSamples++ < 10) Log("world: ignored an impossible position from the host (netId %u)", e.netId);
            continue;
        }
        Remote* r = FindRemote(e.kind, e.netId);
        if (r && r->objId != e.objId) r = nullptr, g_remoteCount = 0;  // ids reused by a new room instance
        if (!r) {
            if (g_remoteCount == 64) continue;
            r = &g_remote[g_remoteCount++];
            *r = {};
            r->kind = e.kind;
            r->netId = e.netId;
            r->objId = e.objId;
            r->firstFrame = g_frame;
        }
        if (e.kind == kKindEnemy && i >= companions) {
            std::memcpy(r->spawn, spawns[i - companions].pos, sizeof(r->spawn));
            r->serial = spawns[i - companions].serial;
        }
        if (r->count && h.seq <= r->lastSeq && h.seq + 600 > r->lastSeq) continue;  // late: newest only
        if (r->count == 8) std::memmove(&r->hist[0], &r->hist[1], 7 * sizeof(Sample)), --r->count;
        std::uint16_t scale = e.kind == kKindEnemy && i >= companions ? spawns[i - companions].scaleY : 0;
        float clock = e.kind == kKindEnemy && i >= companions ? spawns[i - companions].clock : 0.0f;
        r->hist[r->count++] = {h.senderMs, e, scale, clock};
        r->lastSeq = h.seq;
    }
    // Entities the host no longer reports (dead, despawned): dropped once missing from kMissingFrames of the host's
    // frames, checked on each frame's last part (a lost part only delays its entities).
    if (!(h.part & kLastPart)) return;
    for (int i = 0; i < g_remoteCount;) {
        if (h.seq - g_remote[i].lastSeq >= kMissingFrames) {
            g_remote[i] = g_remote[--g_remoteCount];
        } else {
            ++i;
        }
    }
}

void WorldSyncOnClaim(const char* buf, int n) {
    if (!g_on || !g_host || n != static_cast<int>(sizeof(Claim))) return;
    Claim c;
    std::memcpy(&c, buf, sizeof(c));
    if (c.version != kVersion || c.build != AvatarLinkBuild() || !FirstClaim(c.claimId)) return;
    if (c.worldRoom != RoomKey() || c.damage <= 0 || g_claimCount == 32) return;
    g_claims[g_claimCount++] = {c.netId, c.damage, c.react};
}

void WorldSyncAfterUpdate(std::uintptr_t actor) {
    if (g_debugBossHp >= 0) DebugBossHp(actor);
    if (!g_on) return;
    ++g_updatesThisFrame;
    std::uint16_t key = RoomKey();
    if (key != g_roomKey) ResetRoom(key);
    // One brain: field objects (team 3) are the host's too, e.g. the ballroom's orb thrower F_BB090.
    std::uint32_t team = Read<std::uint32_t>(actor + kActorTeam);
    if ((team == 2 || team == 3) && (g_host || !IsCarrier(ObjId(actor)))) TrackEnemy(actor);
    if (g_host) {
        if (g_claimCount)
            if (LocalEnemy* le = LocalFor(actor)) ApplyClaims(*le);
        if (Borrow* b = BorrowFor(actor); b && WorldSyncSkipAi(actor)) ApplyBorrow(*b);  // the friend's Limit partner
        ParitySample(actor);
        return;
    }
    if (Mirror* m = MirrorFor(actor); m && WorldSyncSkipAi(actor)) ApplyMirror(*m);  // not during our own Limit
    if (LocalEnemy* le = LocalFor(actor)) {
        if (le->killPending) ApplyPendingKill(*le);
        FinishRemoval(*le);
        if ((le->bound || le->parked) && !OwnBrain(le->actor)) ApplyEnemy(*le);
    }
    ParitySample(actor);
}

void WorldSyncOnPeerRestart() {
    if (!g_on) return;
    g_remoteCount = 0;
    g_remoteRoom = 0xFFFF;
    g_remoteInstance = 0xFFFF;
    g_lastPacketSeq = 0;
    ++g_epoch;
    g_claimSeenMax = 0;
    g_claimSeenWindow = 0;
    g_claimCount = 0;
    if (!g_host) UnbindAll("the other game restarted");
}

bool WorldSyncHostPresent() { return g_on && !g_host && HostFresh(); }

std::uint32_t WorldSyncEpoch() { return g_epoch; }

int WorldSyncHostEnemies(HostEnemy* out, int max) {
    if (!WorldSyncHostPresent() || g_remotePaused) return 0;
    int n = 0;
    for (int i = 0; i < g_remoteCount && n < max; ++i) {
        const Remote& r = g_remote[i];
        if (r.kind != kKindEnemy || r.count == 0 || !r.serial) continue;
        const Entry& e = r.hist[r.count - 1].e;
        // Dead ones only: field objects have no HP (-1). Armored Xemnas II's shield-break buildings (F_EH060, serials
        // 22-26) were skipped and never made in the friend's game (two-PC run 2026-10-14; your pick: every field
        // object).
        if (e.hp == 0) continue;
        out[n++] = {r.serial, r.objId, {e.pos[0], e.pos[1], e.pos[2]}};
    }
    return n;
}

int WorldSyncHostUnmatched(HostEnemy* out, int max) {
    if (!WorldSyncHostPresent() || g_remotePaused) return 0;
    int n = 0;
    for (int i = 0; i < g_remoteCount && n < max; ++i) {
        const Remote& r = g_remote[i];
        if (r.kind != kKindEnemy || r.count == 0 || r.serial) continue;  // spawn records: spawn_sync's own path
        if (r.objId & 0xF0000000u) continue;  // event actors and other special ids
        const Entry& e = r.hist[r.count - 1].e;
        if (e.hp == 0) continue;  // what BindEnemies would bind
        if (g_frame - r.firstFrame < 30) continue;  // a moment for one of ours to show up and be bound
        bool taken = false;
        for (int k = 0; k < g_enemyCount && !taken; ++k) {
            const LocalEnemy& le = g_enemies[k];
            std::uint16_t made = MadeForNetId(le.actor);
            taken = (le.bound == r.netId && !le.killed) ||
                    (!le.bound && !le.killed && le.objId == r.objId && (!made || made == r.netId));
        }
        if (taken) continue;  // bound, or one of ours is free to bind to it
        out[n++] = {0, r.objId, {e.pos[0], e.pos[1], e.pos[2]}, e.angle, r.netId};
    }
    return n;
}

void WorldSyncMadeFor(std::uintptr_t actor, std::uint16_t netId) {
    if (!actor) return;
    g_madeFor[g_madeForNext++ % 32] = {actor, netId, g_epoch};
    if (g_on && !g_host && !IsCarrier(ObjId(actor))) AddBorn(actor, "made for a host object");
}

void WorldSyncScriptMade(std::uintptr_t actor, std::uintptr_t maker) {
    if (!g_on || g_host || !actor || !maker || !HostFresh()) return;
    __try {
        if (WorldSyncSkipsBrain(maker)) AddBorn(actor, "made by our copy of a host enemy");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool WorldSyncSkipsBrain(std::uintptr_t actor) {
    if (!g_on || g_host) return false;
    LocalEnemy* le = LocalFor(actor);
    // A removed one (no death) keeps its brain off until the game disposes of it. Two-PC run 2026-10-14 (chase before
    // Xemnas): the friend's F_EH110 got its brain back the frame it was removed; its script, held since the bind,
    // started 're_f_eh110-00' a second time and the game crashed at exe+0x3E1271 reading freed memory (0xEFACCAFE).
    if (le && le->vanished) return true;
    if (le && (le->bound || le->killed)) return le->bound && !le->killed && !OwnBrain(actor);
    return BornWaits(actor);  // a copy from birth, before its bind
}

int WorldSyncHostTargetOf(std::uintptr_t actor) {
    if (!g_on || g_host || !HostFresh()) return 0;
    LocalEnemy* le = LocalFor(actor);
    return le && le->bound && !le->killed && !le->parked ? le->hostTarget : 0;
}

std::uint16_t WorldSyncNetIdOf(std::uintptr_t actor, std::uint16_t* instance) {
    if (instance) *instance = g_instance;
    if (!g_on || !g_host) return 0;
    const LocalEnemy* le = LocalFor(actor);
    return le ? le->netId : 0;
}

std::uint16_t WorldSyncBoundTo(std::uintptr_t actor, std::uint16_t* hostInstance) {
    if (hostInstance) *hostInstance = g_remoteInstance;
    if (!g_on || g_host) return 0;
    const LocalEnemy* le = LocalFor(actor);
    return le && !le->killed ? le->bound : 0;
}

std::uintptr_t WorldSyncHostActor(std::uint16_t netId, std::uint16_t instance) {
    if (!g_on || !g_host || !netId || instance != g_instance) return 0;
    for (int k = 0; k < g_enemyCount; ++k)
        if (g_enemies[k].netId == netId && !g_enemies[k].killed) return g_enemies[k].actor;
    return 0;
}

std::uintptr_t WorldSyncBoundActor(std::uint16_t hostNetId, std::uint16_t hostInstance) {
    if (!g_on || g_host || !hostNetId || hostInstance != g_remoteInstance) return 0;
    for (int k = 0; k < g_enemyCount; ++k)
        if (g_enemies[k].bound == hostNetId && !g_enemies[k].killed) return g_enemies[k].actor;
    return 0;
}

bool WorldSyncCarrier(std::uintptr_t actor) {
    __try {
        return actor && IsCarrier(ObjId(actor));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WorldSyncSkipAi(std::uintptr_t actor) {
    if (!g_on || !actor) return false;
    // Host: our companion playing the friend's Limit partner, unless our own Limit needs it now.
    if (g_host) return BorrowFor(actor) && !OurLimitBusy();
    // our own Limit (also the frame it starts, before UpdateMirrors releases the companions)
    return MirrorFor(actor) != nullptr && !OurLimitBusy() && !HeldByScript(actor);
}

void WorldSyncOurLimitPartner(std::uintptr_t actor) {
    int slot = CompanionSlot(actor);
    if (!g_on || g_host || !g_partnerLimit || slot < 0 || actor == g_ourPartners[slot] || !OurLimitBusy()) return;
    g_ourPartners[slot] = actor;
    Log("world: our Limit plays its partner's part on %s: its pose goes to the host until the Limit ends",
        Model(ObjId(actor)));
}

std::uintptr_t WorldSyncMirroredCompanion(std::uint32_t objId) {
    if (!g_on) return 0;
    if (g_host) {
        for (const Borrow& b : g_borrows)
            if (b.actor && b.objId == objId && WorldSyncSkipAi(b.actor)) return b.actor;
        return 0;
    }
    for (const Mirror& m : g_mirrors)
        if (m.actor && WorldSyncSkipAi(m.actor) && ObjId(m.actor) == objId) return m.actor;
    return 0;
}

bool WorldSyncBlocksMotion(std::uintptr_t actor, int motion) {
    if (WorldSyncSkipAi(actor)) return true;
    if (!g_on || g_host) return false;
    LocalEnemy* le = LocalFor(actor);
    bool block = le && (le->bound || le->parked) && !le->killed && !OwnBrain(actor);
    if (block && g_trace && motion != Read<std::int32_t>(actor + kActorMotion) && motion != le->traceAttempt) {
        Log("trace: enemy #%u: its own AI asked for motion %d (blocked; it plays %d)", le->netId, motion,
            Read<std::int32_t>(actor + kActorMotion));
        le->traceAttempt = motion;
    }
    return block;
}

// Who dealt a hit (TODO 1.40 / 1.1). Friend: the side of each hit on a bound enemy; the first kHitDropped kinds are
// ours and become claims. Hits by the host's Sora copy, by companions mirrored from the host (outside our own Limit),
// by enemies, by turrets (F_HB020, team 3) and by other allies happen in the host's game too, so they are dropped.
enum HitSide { kHitSora, kHitSummon, kHitLimit, kHitUnknown, kHitCopy, kHitCompanion, kHitEnemy, kHitTurret, kHitOther,
               kHitSides };
constexpr int kHitDropped = kHitCopy;
const char* const kHitSideName[kHitSides] = {"our Sora", "our summon", "our Limit", "unknown", "the Sora copy",
                                             "a companion", "an enemy", "a turret", "another ally"};
std::uint32_t g_hitSides[kHitSides] = {}, g_hitSidesLogged = 0;
std::uint32_t g_copyHitsDropped = 0, g_copyHitsDroppedLogged = 0, g_copyDamageDropped = 0;
// TODO 1.84: hits by the copy while an enemy's script holds it (a grab, or a reaction command the friend pressed:
// Data Xigbar's sniper shots, made by his script, credited to the copy) only happen in our game, so they count here;
// the friend's game sends no claims meanwhile (GrabSyncOurSoraHeld). And the friend's claims it held back.
std::uint32_t g_heldCopyHits = 0, g_heldCopyDamage = 0, g_heldClaimsDropped = 0;
constexpr std::uintptr_t kSummonState = 0x2A24EDC;  // u32, 2 = a summon is out (puppet.cpp)

int HitSideOf(std::uintptr_t who, bool known) {
    if (!known || !who) return kHitUnknown;
    if (who == Read<std::uintptr_t>(ExeBase() + 0x2A171C8)) return kHitSora;  // any form: the form is the player
    if (who == PuppetCloneActor()) return kHitCopy;
    bool ourLimit = PuppetOurLimitRunning() || PuppetLimitCommand();
    if (MirrorFor(who)) return ourLimit ? kHitLimit : kHitCompanion;  // our Limit's partner runs locally
    std::uint32_t team = Read<std::uint32_t>(who + 0x4DC);
    if (team == 2) return kHitEnemy;
    if (team == 3) return kHitTurret;
    if (team == 1 && Read<std::uint32_t>(ExeBase() + kSummonState) == 2) return kHitSummon;
    if (team == 1 && ourLimit) return kHitLimit;
    return kHitOther;
}

// Research log (KH2COOP_CLAIM_LOG=1, TODO 1.1 / 1.40 / 4.4): who dealt each hit on an enemy. "stale" = no
// RecordAttacker for this hit, the enemy's last attacker (enemy+0xD38) is shown instead.
std::uint32_t g_claimLogs = 0;

std::uintptr_t LastAttacker(std::uintptr_t enemy) {
    std::uint32_t h = Read<std::uint32_t>(enemy + 0xD38);
    return h & 0x80000000u ? Read<std::uintptr_t>(ExeBase() + 0x2B0D720 + 8 * ((h >> 25) & 0x3F)) | (h & 0x1FFFFFF) : 0;
}

const char* AttackerKind(std::uintptr_t who) {
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + 0x2A171C8);
    return !who ? "none" : who == sora ? "our Sora" : who == PuppetCloneActor() ? "the Sora copy"
           : MirrorFor(who) ? "a companion" : "other";
}

// Host side: the HP our enemies lose in our own game (who hit them here).
void LogHostHit(std::uintptr_t enemy, int damage, std::uintptr_t who, bool known, bool dropped) {
    if (g_claimLogs >= 300 || Read<std::uint32_t>(enemy + 0x4DC) != 2) return;
    ++g_claimLogs;
    if (!known) who = LastAttacker(enemy);
    Log("hit: %s exe+0x%llX -%d, attacker %s exe+0x%llX %s team %d%s%s", Model(ObjId(enemy)),
        static_cast<unsigned long long>(enemy - ExeBase()), damage, AttackerKind(who),
        static_cast<unsigned long long>(who ? who - ExeBase() : 0), who ? Model(ObjId(who)) : "-",
        who ? static_cast<int>(Read<std::uint32_t>(who + 0x4DC)) : -1, known ? "" : " (stale)",
        dropped ? " (dropped: the friend's claims carry it)" : "");
}

void LogClaimAttacker(const LocalEnemy& le, int damage, std::uintptr_t who, bool known, int side, bool sent) {
    if (g_claimLogs >= 200) return;
    ++g_claimLogs;
    if (!known) who = LastAttacker(le.actor);
    Log("claim: our#%u %s -%d, attacker %s exe+0x%llX %s team %d%s: %s (%s)", le.netId, Model(le.objId), damage,
        AttackerKind(who), static_cast<unsigned long long>(who ? who - ExeBase() : 0), who ? Model(ObjId(who)) : "-",
        who ? static_cast<int>(Read<std::uint32_t>(who + 0x4DC)) : -1, known ? "" : " (stale)",
        sent ? "sent" : "dropped", kHitSideName[side]);
}

void LogHitSides() {
    if (g_host) {
        if (g_copyHitsDropped + g_heldCopyHits == g_copyHitsDroppedLogged) return;
        g_copyHitsDroppedLogged = g_copyHitsDropped + g_heldCopyHits;
        Log("world: hits by the friend's Sora copy dropped here so far: %u (%u damage; its claims carry them); "
            "counted while a script held it: %u (%u damage)", g_copyHitsDropped, g_copyDamageDropped, g_heldCopyHits,
            g_heldCopyDamage);
        return;
    }
    std::uint32_t total = 0;
    for (std::uint32_t n : g_hitSides) total += n;
    if (total == g_hitSidesLogged) return;
    g_hitSidesLogged = total;
    char buf[400];
    int len = 0;
    for (int i = 0; i < kHitSides && len < static_cast<int>(sizeof(buf)) - 40; ++i)
        if (g_hitSides[i])
            len += std::snprintf(buf + len, sizeof(buf) - len, "%s%s %u", len ? ", " : "", kHitSideName[i],
                                 g_hitSides[i]);
    Log("world: hits on bound enemies so far by attacker (claimed: our Sora, summon, Limit, unknown%s): %s",
        g_ownHitsOnly ? "; the rest dropped" : "; all claimed, KH2COOP_OWN_HITS_ONLY=0", buf);
}

void WorldSyncAttackerRecorded(std::uintptr_t enemy, std::uintptr_t attacker) {
    if (g_recordingClaim) return;
    g_recEnemy = enemy;
    g_recAttacker = attacker;
}

bool WorldSyncBlocksDamage(std::uintptr_t actor, int damage, int react) {
    bool known = false;
    std::uintptr_t who = HitAttacker(actor, &known);
    if (!g_on) return false;
    if (g_host) {
        if (WorldSyncSkipAi(actor)) {  // our companion playing the friend's Limit partner: it's in a Limit there
            ++g_partnerDamageDropped;
            return true;
        }
        // The friend's Sora copy replays the friend's attacks here; the friend's own game claims the hits it saw
        // (also its Limit partner's, which our companion replays).
        bool drop = g_ownHitsOnly && known && who && (who == PuppetCloneActor() || BorrowFor(who));
        if (drop && who == PuppetCloneActor() && GrabSyncCopyHeld()) {  // a script holds the copy: only here (1.84)
            drop = false;
            ++g_heldCopyHits;
            g_heldCopyDamage += damage;
            Log("world: the friend's Sora copy hit %s for %d while a script held it: counted here (%u so far)",
                Model(ObjId(actor)), damage, g_heldCopyHits);
        }
        if (drop) {
            ++g_copyHitsDropped;
            g_copyDamageDropped += damage;
        }
        if (g_claimLog) LogHostHit(actor, damage, who, known, drop);
        return drop;
    }
    if (WorldSyncSkipAi(actor)) {
        ++g_blockedDamage;
        return true;
    }
    if (LocalEnemy* le = LocalFor(actor)) {
        if (le->vanished) return true;  // being removed: a hit now must not kill it (EXP, drops)
        if (le->killed) return false;
        // A boss running its own brain for a shared move (Final Xemnas's desperation move) plays the move on our
        // Sora in our game: the hits of our copy of the move count here, as in one-player play, and aren't claimed
        // (the host's game plays its own). His script lets go of our Sora only after the move's last hit drops his HP
        // (98 -> 1); with every hit blocked our Sora stayed held (two-PC run 2026-10-22, TODO 1.114). After the
        // move the host's HP is applied again. A hit that would kill him leaves 1 HP: his death is the host's.
        if (le->bound && OwnBrain(actor)) {
            int hp = Hp(actor);
            if (hp > 0 && damage >= hp) {
                PuppetApplyHp(actor, 1 - hp, react);
                Log("world: our#%u %s runs its own brain: our hit for %d left it at 1 HP (its death is the host's)",
                    le->netId, Model(le->objId), damage);
                return true;
            }
            Log("world: our#%u %s runs its own brain: our hit for %d counts here (HP %d -> %d), not claimed",
                le->netId, Model(le->objId), damage, hp, hp - damage);
            return false;
        }
        if (le->bound) {
            int side = HitSideOf(who, known);
            bool send = !g_ownHitsOnly || side < kHitDropped;
            if (send && side == kHitSora && GrabSyncOurSoraHeld()) {  // the host's game counts those (1.84)
                send = false;
                ++g_heldClaimsDropped;
                Log("world: our Sora hit our#%u %s for %d while the host's script held it: not claimed, the host's "
                    "game counts it (%u so far)", le->netId, Model(le->objId), damage, g_heldClaimsDropped);
            }
            ++g_hitSides[side];
            if (g_claimLog) LogClaimAttacker(*le, damage, who, known, side, send);
            if (send) SendClaim(le->bound, damage, react);
        }
        return le->bound || le->parked;
    }
    // Our own Sora: while the host's world is live, only its enemies hurt us
    // (their hits on our copy arrive through the forwarded-hit path, which
    // calls the game's HP change directly and doesn't come through here).
    if (actor == Read<std::uintptr_t>(ExeBase() + 0x2A171C8) && HostFresh()) {
        ++g_ownDamageDropped;
        return true;
    }
    return false;
}

void WorldSyncFrame() {
    ++g_frame;  // also with world sync off: the debug boss HP window counts frames
    if (!g_on) return;
    __try {
        if (g_parityMs) ParityFrame();
        std::uint8_t inField = Read<std::uint8_t>(ExeBase() + kInField);
        if (inField && !g_lastInField) {  // a load just finished: new room instance, old actors are gone
            ++g_instance;
            ResetRoom(RoomKey());
            if (!g_host) UnbindAll("our room was loaded");
        }
        g_lastInField = inField;
        AgeEnemies();
        if (g_host) {
            if (Read<std::uint8_t>(ExeBase() + kNow) != 0xFF) SendWorld();
            UpdateBorrows();
        } else {
            UpdateMirrors();
            if (g_ownLimit && g_partnerLimit)
                for (std::uintptr_t p : g_ourPartners)
                    if (p) SendPartner(p);
            BindEnemies();
            if (g_frame % 600 == 0) {
                LogCensus();
                LogOneBrain();
                Log("world: companion writes %u, companion damage blocked %u | enemies bound %u, killed %u, removed %u, "
                    "parked %u, claims sent %u, local hits on our Sora dropped %u, late host packets dropped %u, "
                    "HP bar calls %u, motion clocks set %u, copies from birth %u (brain on after the wait %u), "
                    "frames a copy attached before the host's was held in place %u",
                    g_mirrorWrites, g_blockedDamage, g_binds, g_kills, g_vanished, g_parks, g_claimsSent,
                    g_ownDamageDropped, g_latePackets, g_barUpdates, g_clockFixes, g_bornCount, g_bornLate,
                    g_attachedAloneHeld);
            }
        }
        if (g_host && g_frame % 600 == 0 && g_claimsApplied) Log("world: friend's hits applied so far: %u", g_claimsApplied);
        if (g_host && g_frame % 600 == 0 && g_framesInParts)
            Log("world: frames sent in more than one part so far: %u (%u parts in all)", g_framesInParts, g_partsSent);
        if (g_host && g_frame % 600 == 0 && g_partnerDamageDropped)
            Log("world: hits on our companion while it played the friend's Limit partner, dropped: %u", g_partnerDamageDropped);
        if (g_frame % 1800 == 0) LogHitSides();
        WorldMirrorFrame();
        ReactionSyncFrame();
        GrabSyncFrame();
        SharedMoveFrame();
        LimitSyncFrame();
        CopyBladeFrame();
        CompanionSyncFrame();
        MissionSyncFrame();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 20) Log("world sync: exception in frame update");
    }
    g_updatesThisFrame = 0;
}

void WorldSyncDiag(WorldDiag& out) {
    int hostEnemies = 0, bound = 0;
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].kind == kKindEnemy) ++hostEnemies;
    for (int i = 0; i < g_enemyCount; ++i)
        if (g_enemies[i].bound) ++bound;
    out = WorldDiag {g_on, g_host, g_host ? g_enemyCount : hostEnemies, g_enemyCount, bound, g_partsLastFrame,
                     g_lastPacketMs ? AvatarLinkNowMs() - g_lastPacketMs : 0xFFFFFFFFu};
}

}  // namespace kh2coop
