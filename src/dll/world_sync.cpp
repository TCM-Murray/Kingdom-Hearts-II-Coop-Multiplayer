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
// Hits from local enemies on our own Sora are dropped (the host's enemies hit
// our copy in the host's game; those hits are forwarded, see puppet.cpp).
//
// Packet "KH2W" (host -> friend, every frame): header + up to kMaxEntries
// 32-byte entries. Off unless WORLD_SYNC=1 (both PCs).

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "spawn_sync.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;            // u8 world, room
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;   // u64[2] companion actors
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kEntityPos = 0x30, kEntitySin = 0x40, kEntityCos = 0x48, kEntityAngle = 0x4C;
constexpr std::uintptr_t kEntityAirborne = 0x104;
constexpr std::uintptr_t kActorMotion = 0x180;
constexpr std::uintptr_t kActorMotionBank = 0x2B8;  // != 0: the motion id is from another file's set (avatar_link.cpp)
constexpr std::uintptr_t kActorStats = 0x5C0;       // -> stat channels; HP = first int
constexpr std::uintptr_t kActorTeam = 0x4DC;        // 1 party, 2 enemies
constexpr std::uintptr_t kActorObjRow = 0x918;      // -> object table row (u32 id, ..., model @+8)
constexpr std::uintptr_t kActorCarried = 0x690, kActorAccel = 0xA48, kActorVelocity = 0xB98;
constexpr std::uintptr_t kActorSpawnRecord = 0x9F0;  // -> 0x40-byte spawn record; u16 serial @+0x1E
// Fade-out removal (bench 2026-10-08, Ghidra exe+0x3BFD30): fade {f32 value, f32 speed per step} at +0xA08,
// stepped by exe+0x3B77C0; once the value is 0 with +0x9B8 bit 4 set, the actor update calls class slot +0x38
// (Shadow: exe+0x3B4700 ends the AI script, sets +0x120 bit 28), then the removal check and disposal.
constexpr std::uintptr_t kActorFade = 0xA08, kActorFadeFlags = 0x9B8;
constexpr std::uint32_t kFadeRemoveBit = 0x10;

constexpr std::uint32_t kMagic = 0x5732484B;  // "KH2W"
constexpr std::uint16_t kVersion = 3;  // v3: enemy spawn serials; v2: room instance + paused flag
constexpr int kMaxEntries = 40;
enum : std::uint8_t { kKindCompanion = 0, kKindEnemy = 1 };
// kFlagOtherBank: the motion comes from another file's set (e.g. the partner's part of a Limit); its id
// means something else in the actor's own set.
enum : std::uint8_t { kFlagAirborne = 1, kFlagOtherBank = 2 };

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint32_t senderMs;
    std::uint8_t world, room, count, companions;  // companions come first in the entries
    std::uint16_t instance;  // +1 after every completed room load (also Continue's same-room reload)
    std::uint8_t flags, pad2; // kPaused: the host's game updated no entities this frame (menu, Game Over, load)
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
    std::uint16_t pad;
};
#pragma pack(pop)
static_assert(sizeof(Entry) == 32, "entry layout");
static_assert(sizeof(EnemySpawn) == 16, "spawn layout");

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

std::uint32_t ObjId(std::uintptr_t actor) {
    std::uintptr_t row = Read<std::uintptr_t>(actor + kActorObjRow);
    return InExe(row) ? Read<std::uint32_t>(row) : 0;
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

int Hp(std::uintptr_t actor) {
    std::uintptr_t stats = Read<std::uintptr_t>(actor + kActorStats);
    return stats && Readable4(stats) ? Read<std::int32_t>(stats) : -1;
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
    std::uint64_t parkedFrame;
    int lastMotion;
    int traceMotion, traceAttempt;  // ENEMY_TRACE: last motion / blocked AI motion logged
    std::uint8_t traceCollision;
    Entry cached;          // host: state read right after the game's update (actor known valid then)
    bool cachedOk;
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
            return;
        }
    }
    if (g_enemyCount == 64) return;
    LocalEnemy& e = g_enemies[g_enemyCount++];
    e = {actor, g_nextNetId++, ObjId(actor), Serial(actor), {}, g_frame, false, g_frame, 0, false, false, nullptr, false,
         false, 0, -1, -1, -1, 0xFF, {}, false};
    std::memcpy(e.spawn, reinterpret_cast<const void*>(actor + kActorEntity + kEntityPos), sizeof(e.spawn));
    e.cachedOk = FillEntry(e.cached, actor, e.objId, e.netId, kKindEnemy);
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
};
Remote g_remote[64];
int g_remoteCount = 0;
std::uint16_t g_remoteRoom = 0xFFFF;
std::uint16_t g_remoteInstance = 0xFFFF;
bool g_remotePaused = false;
std::uint32_t g_lastPacketMs = 0;
std::uint32_t g_badSamples = 0;

bool SaneEntry(const Entry& e) {
    for (float v : e.pos)
        if (!std::isfinite(v) || std::fabs(v) > 100000.0f) return false;
    return std::isfinite(e.angle) && std::fabs(e.angle) < 100.0f;
}

Remote* FindRemote(std::uint8_t kind, std::uint16_t netId) {
    for (int i = 0; i < g_remoteCount; ++i)
        if (g_remote[i].kind == kind && g_remote[i].netId == netId) return &g_remote[i];
    return nullptr;
}

// Interpolated pose of a remote entity at the playback time; false if none.
// The host's Limit plays the partner's part from the Limit's own set and puts him at (0, 0, 0) while he
// warps in and out (bench 2026-10-06: Auron at the origin at motions 253 and 257).
bool Warped(const Entry& e) {
    return (e.flags & kFlagOtherBank) && e.pos[0] == 0.0f && e.pos[1] == 0.0f && e.pos[2] == 0.0f;
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

void UpdateMirrors() {
    bool ownLimit = PuppetOurLimitRunning();
    if (ownLimit != g_ownLimit) {
        Log("world: our own Limit %s: companions %s", ownLimit ? "started" : "ended",
            ownLimit ? "run locally until it ends" : "follow the host's again");
        g_ownLimit = ownLimit;
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

void ApplyMirror(Mirror& m) {
    Remote* r = FindRemote(kKindCompanion, m.netId);
    Entry e;
    if (!r || !PoseAt(*r, AvatarLinkPlaybackSenderMs(), e)) return;
    std::uintptr_t a = m.actor, entity = a + kActorEntity;
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
    if (static_cast<int>(motion) != m.lastMotion && motion != 0xFFFFFFFF) {
        PuppetSetMotion(a, static_cast<int>(motion));
        m.lastMotion = static_cast<int>(motion);
    }
    std::uintptr_t stats = Read<std::uintptr_t>(a + kActorStats);
    if (stats && e.hp > 0 && Read<std::int32_t>(stats) != e.hp) *reinterpret_cast<std::int32_t*>(stats) = e.hp;
    ++g_mirrorWrites;
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
std::uint32_t g_claimSendId = 0;
std::uint32_t g_claimsSent = 0, g_ownDamageDropped = 0, g_binds = 0, g_kills = 0, g_parks = 0, g_vanished = 0;
std::uint64_t g_hostEmptySince = 0;

LocalEnemy* LocalFor(std::uintptr_t actor) {
    for (int i = 0; i < g_enemyCount; ++i)
        if (g_enemies[i].actor == actor) return &g_enemies[i];
    return nullptr;
}

bool HostFresh() { return AvatarLinkNowMs() - g_lastPacketMs < 1000 && g_remoteRoom == RoomKey(); }

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
        ++g_vanished;
        Log("world: ours#%u %s removed without a death (%s): no EXP, no drops", le.netId, Model(le.objId), why);
        return;
    }
    int hp = Hp(le.actor);
    if (hp > 0) PuppetApplyHp(le.actor, -hp, 0);  // the game's own death: animation, EXP, drops
    ++g_kills;
    Log("world: ours#%u %s killed (%s)", le.netId, Model(le.objId), why);
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
        if (r.hist[r.count - 1].e.hp <= 0) continue;
        bool taken = false;
        for (int k = 0; k < g_enemyCount && !taken; ++k)
            taken = g_enemies[k].bound == r.netId && !g_enemies[k].killed;
        if (taken) continue;
        int best = -1;
        float bestD = 1e30f;
        // Same spawn record on both PCs (serial) first: exact, whatever the positions.
        for (int k = 0; k < g_enemyCount && r.serial; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (!le.bound && !le.killed && le.objId == r.objId && le.serial == r.serial) best = k, bestD = 0;
        }
        for (int k = 0; k < g_enemyCount && best < 0; ++k) {
            const LocalEnemy& le = g_enemies[k];
            if (le.bound || le.killed || le.objId != r.objId) continue;
            if (le.serial && r.serial) continue;  // both from spawn records but different ones: never the same enemy
            float dx = le.spawn[0] - r.spawn[0], dy = le.spawn[1] - r.spawn[1], dz = le.spawn[2] - r.spawn[2];
            float d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) bestD = d, best = k;
        }
        if (best < 0) continue;
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
    if (!r || !PoseAt(*r, AvatarLinkPlaybackSenderMs(), e)) return;
    // Position and facing only: the velocity fields cleared on companions were
    // checked on party members; an enemy's layout past the shared part may differ.
    pos[0] = e.pos[0];
    pos[1] = e.pos[1];
    pos[2] = e.pos[2];
    *reinterpret_cast<float*>(entity + kEntityAngle) = e.angle;
    *reinterpret_cast<float*>(entity + kEntitySin) = std::sin(e.angle);
    *reinterpret_cast<float*>(entity + kEntityCos) = std::cos(e.angle);
    if (!le.killed && static_cast<int>(e.motion) != le.lastMotion && e.motion != 0xFFFFFFFF) {
        PuppetSetMotion(le.actor, static_cast<int>(e.motion));
        le.lastMotion = static_cast<int>(e.motion);
    }
    std::uintptr_t stats = Read<std::uintptr_t>(le.actor + kActorStats);
    if (!le.killed && stats && Readable4(stats) && e.hp > 0 && Read<std::int32_t>(stats) != e.hp)
        *reinterpret_cast<std::int32_t*>(stats) = e.hp;
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

void ApplyClaims(LocalEnemy& le) {
    for (int i = 0; i < g_claimCount;) {
        if (g_claims[i].netId != le.netId) {
            ++i;
            continue;
        }
        int before = Hp(le.actor);
        if (before > 0) {
            int after = PuppetApplyHp(le.actor, -g_claims[i].damage, g_claims[i].react);
            if (std::uintptr_t copy = PuppetCloneActor())
                reinterpret_cast<PFN_RecordAttacker>(ExeBase() + kRecordAttacker)(le.actor, copy);
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
        std::memcpy(e.pos, reinterpret_cast<const void*>(entity + kEntityPos), sizeof(e.pos));
        e.angle = Read<float>(entity + kEntityAngle);
        e.motion = Read<std::uint32_t>(a + kActorMotion);
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

void SendWorld() {
    char buf[sizeof(Header) + kMaxEntries * (sizeof(Entry) + sizeof(EnemySpawn))];
    auto* h = reinterpret_cast<Header*>(buf);
    auto* entries = reinterpret_cast<Entry*>(buf + sizeof(Header));
    int n = 0;
    for (int slot = 0; slot < 2; ++slot) {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot);
        if (InExe(a) && FillEntry(entries[n], a, ObjId(a), static_cast<std::uint16_t>(slot + 1), kKindCompanion)) ++n;
    }
    int companions = n;
    const LocalEnemy* sent[kMaxEntries];
    for (int i = 0; i < g_enemyCount && n < kMaxEntries; ++i) {
        const LocalEnemy& le = g_enemies[i];
        if (!le.cachedOk) continue;
        entries[n] = le.cached;  // captured right after the game updated it; never re-read here
        sent[n - companions] = &le;
        ++n;
    }
    auto* spawns = reinterpret_cast<EnemySpawn*>(buf + sizeof(Header) + n * sizeof(Entry));
    for (int k = 0; k < n - companions; ++k) {
        std::memcpy(spawns[k].pos, sent[k]->spawn, sizeof(spawns[k].pos));
        spawns[k].serial = sent[k]->serial;
        spawns[k].pad = 0;
    }
    *h = {kMagic, kVersion, AvatarLinkBuild(), ++g_seq, AvatarLinkNowMs(),
          Read<std::uint8_t>(ExeBase() + kNow), Read<std::uint8_t>(ExeBase() + kNow + 1),
          static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(companions), g_instance,
          static_cast<std::uint8_t>(g_updatesThisFrame == 0 ? kPaused : 0), 0};
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Header) + n * sizeof(Entry) + (n - companions) * sizeof(EnemySpawn)));
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

}  // namespace

void WorldSyncInit(bool host) {
    g_on = EnvInt("KH2COOP_WORLD_SYNC", 0) == 1;
    g_host = host;
    g_trace = g_on && EnvInt("KH2COOP_ENEMY_TRACE", 0) == 1;
    if (g_on) {
        Log("world sync: on (%s)", host ? "host: sending companions and enemies"
                                        : "friend: mirroring companions, enemy census");
        SpawnSyncInit(host);
    }
}

void WorldSyncOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n < static_cast<int>(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, buf, sizeof(h));
    if (h.version != kVersion || h.build != AvatarLinkBuild()) return;  // the link already warns about builds
    int companions = h.companions;
    if (n != static_cast<int>(sizeof(Header) + h.count * sizeof(Entry) + (h.count - companions) * sizeof(EnemySpawn)))
        return;
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
        }
        if (e.kind == kKindEnemy && i >= companions) {
            std::memcpy(r->spawn, spawns[i - companions].pos, sizeof(r->spawn));
            r->serial = spawns[i - companions].serial;
        }
        if (r->count && h.seq <= r->lastSeq && h.seq + 600 > r->lastSeq) continue;  // late: newest only
        if (r->count == 8) std::memmove(&r->hist[0], &r->hist[1], 7 * sizeof(Sample)), --r->count;
        r->hist[r->count++] = {h.senderMs, e};
        r->lastSeq = h.seq;
    }
    // Entities the host no longer reports (dead, despawned): drop after this packet.
    for (int i = 0; i < g_remoteCount;) {
        if (g_remote[i].lastSeq != h.seq) {
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
    if (!g_on) return;
    ++g_updatesThisFrame;
    std::uint16_t key = RoomKey();
    if (key != g_roomKey) ResetRoom(key);
    if (Read<std::uint32_t>(actor + kActorTeam) == 2) TrackEnemy(actor);
    if (g_host) {
        if (g_claimCount)
            if (LocalEnemy* le = LocalFor(actor)) ApplyClaims(*le);
        return;
    }
    if (Mirror* m = MirrorFor(actor); m && WorldSyncSkipAi(actor)) ApplyMirror(*m);  // not during our own Limit
    if (LocalEnemy* le = LocalFor(actor)) {
        if (le->killPending) ApplyPendingKill(*le);
        if (le->bound || le->parked) ApplyEnemy(*le);
    }
}

void WorldSyncOnPeerRestart() {
    if (!g_on) return;
    g_remoteCount = 0;
    g_remoteRoom = 0xFFFF;
    g_remoteInstance = 0xFFFF;
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
        if (e.hp <= 0) continue;
        out[n++] = {r.serial, r.objId, {e.pos[0], e.pos[1], e.pos[2]}};
    }
    return n;
}

bool WorldSyncSkipAi(std::uintptr_t actor) {
    // our own Limit (also the frame it starts, before UpdateMirrors releases the companions)
    return g_on && !g_host && MirrorFor(actor) != nullptr && !PuppetLimitCommand() && !PuppetOurLimitRunning();
}

bool WorldSyncBlocksMotion(std::uintptr_t actor, int motion) {
    if (WorldSyncSkipAi(actor)) return true;
    if (!g_on || g_host) return false;
    LocalEnemy* le = LocalFor(actor);
    bool block = le && (le->bound || le->parked) && !le->killed;
    if (block && g_trace && motion != Read<std::int32_t>(actor + kActorMotion) && motion != le->traceAttempt) {
        Log("trace: enemy #%u: its own AI asked for motion %d (blocked; it plays %d)", le->netId, motion,
            Read<std::int32_t>(actor + kActorMotion));
        le->traceAttempt = motion;
    }
    return block;
}

bool WorldSyncBlocksDamage(std::uintptr_t actor, int damage, int react) {
    if (!g_on || g_host) return false;
    if (WorldSyncSkipAi(actor)) {
        ++g_blockedDamage;
        return true;
    }
    if (LocalEnemy* le = LocalFor(actor)) {
        if (le->vanished) return true;  // being removed: a hit now must not kill it (EXP, drops)
        if (le->killed) return false;
        if (le->bound) SendClaim(le->bound, damage, react);
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
    if (!g_on) return;
    ++g_frame;
    __try {
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
        } else {
            UpdateMirrors();
            BindEnemies();
            if (g_frame % 600 == 0) {
                LogCensus();
                Log("world: companion writes %u, companion damage blocked %u | enemies bound %u, killed %u, removed %u, "
                    "parked %u, claims sent %u, local hits on our Sora dropped %u",
                    g_mirrorWrites, g_blockedDamage, g_binds, g_kills, g_vanished, g_parks, g_claimsSent,
                    g_ownDamageDropped);
            }
        }
        if (g_host && g_frame % 600 == 0 && g_claimsApplied) Log("world: friend's hits applied so far: %u", g_claimsApplied);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 20) Log("world sync: exception in frame update");
    }
    g_updatesThisFrame = 0;
}

}  // namespace kh2coop
