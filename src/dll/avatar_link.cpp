#include "avatar_link.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common.hpp"
#include "downed.hpp"
#include "follow.hpp"
#include "game_over.hpp"
#include "load_barrier.hpp"
#include "pause_sync.hpp"
#include "world_sync.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

// Verified addresses: see VERIFIED_OFFSETS.md.
constexpr std::uintptr_t kNow = 0x717008;         // u8 world, u8 room, u8 door, pad, u16 map, btl, evt programs
constexpr std::uintptr_t kSoraSlot = 0x2A23598;   // u32 HP, u32 max HP
constexpr std::uintptr_t kPlayerActor = 0x2A171C8;  // u64: head of active-entity list
constexpr std::uintptr_t kActorEntity = 0x640;    // actor -> entity transform
constexpr std::uintptr_t kEntityPos = 0x30;       // f32 x, y, z, w(=1)
constexpr std::uintptr_t kEntityAngle = 0x4C;     // f32 facing, radians
constexpr std::uintptr_t kActorMotion = 0x180;    // u32 motion id (0 = idle)
constexpr std::uintptr_t kActorMotionTime = 0x19C;  // f32 frames since motion start
// Motion bank of a motion from another file (a Limit's or a reaction command's set, Mickey's rescue);
// 0 while the motion comes from Sora's own set. Bench 2026-10-06: Bushido sets it on Sora for 252..257.
constexpr std::uintptr_t kActorMotionBank = 0x2B8;
constexpr std::uintptr_t kEntityAirborne = 0x104;   // u32 1 while airborne

constexpr std::uint32_t kMagic = 0x4132484B;     // "KH2A"
constexpr std::uint32_t kHitMagic = 0x4832484B;  // "KH2H"
constexpr std::uint32_t kHealMagic = 0x5232484B;  // "KH2R": a heal for the peer's player (HitPacket layout)
constexpr std::uint32_t kWorldMagic = 0x5732484B;  // "KH2W" (world_sync.cpp)
constexpr std::uint32_t kFeaturesMagic = 0x4632484B;  // "KH2F": which co-op features this PC has on

// Feature handshake: both PCs must run with the same features, or the game
// behaves half-synced (two-PC test 23:27: the friend's kh2coop.ini lacked
// WORLD_SYNC/PAUSE_SYNC, so Goofy and enemies weren't mirrored there). Each
// side announces its settings every 2 s; a difference is logged plainly.
struct Feature {
    const char* setting;
    int fallback;
};
constexpr Feature kFeatures[] = {
    {"KH2COOP_WORLD_SYNC", 0}, {"KH2COOP_PAUSE_SYNC", 0}, {"KH2COOP_COPY_AGGRO", 0},
    {"KH2COOP_PUPPET", 0},     {"KH2COOP_FORWARD_HITS", 1}, {"KH2COOP_SAVE_GUARD", 0},
    {"KH2COOP_SPAWN_SYNC", 1}, {"KH2COOP_DOWNED", 1},     {"KH2COOP_GAMEOVER_SYNC", 1},
    {"KH2COOP_AI_NEAREST_PLAYER", 1}, {"KH2COOP_LOAD_BARRIER", 1},
};
#pragma pack(push, 1)
struct FeaturesPacket {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t bits;
};
#pragma pack(pop)
std::uint32_t g_featureBits = 0;
std::uint32_t g_peerFeatureBits = 0xFFFFFFFF;  // unknown until the first announcement
constexpr std::uint16_t kVersion = 3;            // v2: build id + sender clock; v3: room programs

// Both PCs must run the same DLL; packets from another build are ignored.
constexpr std::uint32_t BuildId() {
    const char* s = __DATE__ " " __TIME__;
    std::uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<std::uint8_t>(*s)) * 16777619u;
    return h;
}
constexpr std::uint32_t kBuild = BuildId();

// kOtherBank: the motion id is from another file's set (see kActorMotionBank), so the same id in
// Sora's own set is a different motion (Bushido's 252/253 vs our rescue motions).
enum : std::uint8_t { kHasActor = 1, kAirborne = 2, kDowned = 4, kOtherBank = 8 };

#pragma pack(push, 1)
struct AvatarPacket {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint32_t senderMs;  // sender's clock (NowMs: high-resolution counter, truncated)
    std::uint8_t world, room, door, flags;
    float pos[3];
    float angle;
    std::uint32_t motion;
    float motionTime;
    std::int32_t hp, maxHp;
    std::uint16_t programs[3];  // the room's loaded map/btl/evt programs (event and boss versions of a room differ)
};
struct HitPacket {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t hitId;
    std::int32_t damage;
    std::int32_t react;
};
#pragma pack(pop)

constexpr int kHitRepeats = 3;

SOCKET g_socket = INVALID_SOCKET;
sockaddr_in g_peer {};
bool g_peerKnown = false;      // false while a host waits to learn the friend's address
bool g_learnPeer = false;      // KH2COOP_PEER=auto: take the address of whoever sends us valid packets
std::uint32_t g_hitSendId = 0;
std::uint32_t g_hitSeenMax = 0;      // highest hit id received
std::uint64_t g_hitSeenWindow = 0;   // bit i = id (g_hitSeenMax - i) already applied
std::uint32_t g_sendSeq = 0;
std::uint64_t g_frames = 0;
bool g_warnedBuild = false;
bool g_debug = false;  // KH2COOP_DEBUG=1: accept test commands from this PC (tools/debug_cmd.py)

// Milliseconds from the high-resolution counter. GetTickCount64 ticks only
// every ~15.6 ms (one game frame), which made the copy repeat a frame about
// every 0.4 s (measured with tools/netbench.py).
std::uint32_t NowMs() {
    static const double kMsPerTick = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<double>(c.QuadPart) * kMsPerTick));
}

// ---- Network simulator (KH2COOP_NET_SIM=latency_ms,jitter_ms,loss_percent) ----
// Delays, reorders and drops our outgoing packets so the one-PC test bench
// behaves like Wi-Fi. Off when unset.
struct SimPacket {
    std::uint32_t due;
    int len;
    char data[1400];
};
SimPacket g_simQueue[512];
int g_simCount = 0;
int g_simLatency = 0, g_simJitter = 0, g_simLoss = 0;
bool g_sim = false;
std::uint32_t g_simDropped = 0;

void SendNow(const void* data, int len) {
    sendto(g_socket, static_cast<const char*>(data), len, 0, reinterpret_cast<const sockaddr*>(&g_peer), sizeof(g_peer));
}

void Send(const void* data, int len) {
    if (!g_peerKnown) return;
    if (!g_sim) {
        SendNow(data, len);
        return;
    }
    if (std::rand() % 100 < g_simLoss) {
        ++g_simDropped;
        return;
    }
    if (g_simCount == 512 || len > static_cast<int>(sizeof(SimPacket::data))) return;
    int jitter = g_simJitter ? std::rand() % (2 * g_simJitter + 1) - g_simJitter : 0;
    SimPacket& p = g_simQueue[g_simCount++];
    p.due = NowMs() + static_cast<std::uint32_t>((std::max)(0, g_simLatency + jitter));
    p.len = len;
    std::memcpy(p.data, data, len);
}

void FlushSim() {
    std::uint32_t now = NowMs();
    for (int i = 0; i < g_simCount;) {
        if (static_cast<std::int32_t>(now - g_simQueue[i].due) >= 0) {
            SendNow(g_simQueue[i].data, g_simQueue[i].len);
            g_simQueue[i] = g_simQueue[--g_simCount];  // order doesn't matter: jitter reorders anyway
        } else {
            ++i;
        }
    }
}

// ---- Receiving: a short history of the peer's poses, played back slightly late ----
// Showing the peer ~100 ms in the past lets us blend between two received
// poses instead of snapping to each one as it arrives (smooth on Wi-Fi, where
// packets come in bunches). KH2COOP_INTERP_MS=0 = snap to the newest (old behaviour).
struct Sample {
    std::uint32_t senderMs;
    AvatarPacket p;
};
constexpr int kHistory = 64;
Sample g_history[kHistory];  // sorted by senderMs, oldest first
int g_historyCount = 0;
std::int32_t g_offsets[128];  // localMs - senderMs per packet (clock offset + this packet's delay)
int g_offsetCount = 0, g_offsetNext = 0;
std::uint32_t g_lastArrivalMs = 0;
int g_interpMs = 100;

struct RemoteView {
    bool seen = false;
    std::uint32_t lastSeq = 0;
    std::uint32_t received = 0;
    std::uint32_t lost = 0;
    std::uint32_t late = 0;   // arrived after a newer one (still used for playback)
    AvatarPacket last {};
} g_remote;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

// Plain data only, so the structured exception guard can wrap it: during room
// loads the actor pointer can briefly point at freed memory.
bool Capture(AvatarPacket& p) {
    __try {
        std::uintptr_t exe = ExeBase();
        p.world = Read<std::uint8_t>(exe + kNow);
        p.room = Read<std::uint8_t>(exe + kNow + 1);
        p.door = Read<std::uint8_t>(exe + kNow + 2);
        for (int i = 0; i < 3; ++i) p.programs[i] = Read<std::uint16_t>(exe + kNow + 4 + 2 * i);
        p.hp = Read<std::int32_t>(exe + kSoraSlot);
        p.maxHp = Read<std::int32_t>(exe + kSoraSlot + 4);
        std::uintptr_t actor = Read<std::uintptr_t>(exe + kPlayerActor);
        if (!actor || p.world == 0xFF) return true;
        std::uintptr_t entity = actor + kActorEntity;
        for (int i = 0; i < 3; ++i) p.pos[i] = Read<float>(entity + kEntityPos + 4 * i);
        p.angle = Read<float>(entity + kEntityAngle);
        p.motion = Read<std::uint32_t>(actor + kActorMotion);
        p.motionTime = Read<float>(actor + kActorMotionTime);
        p.flags = kHasActor | (Read<std::uint32_t>(entity + kEntityAirborne) ? kAirborne : 0) |
                  (DownedIsLying() ? kDowned : 0) |
                  (Read<std::uintptr_t>(actor + kActorMotionBank) ? kOtherBank : 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SendOwnAvatar() {
    AvatarPacket p {};
    p.magic = kMagic;
    p.version = kVersion;
    p.build = kBuild;
    if (!Capture(p)) return;
    p.seq = ++g_sendSeq;
    p.senderMs = NowMs();
    Send(&p, sizeof(p));
}

void LogPeerChanges(const AvatarPacket& p) {
    const AvatarPacket& last = g_remote.last;
    if (p.world != last.world || p.room != last.room)
        Log("peer moved to world 0x%02X room 0x%02X door 0x%02X", p.world, p.room, p.door);
    else if (std::memcmp(p.programs, last.programs, sizeof(p.programs)) != 0)
        Log("peer's room programs changed: map %u btl %u evt %u (door 0x%02X)", p.programs[0], p.programs[1],
            p.programs[2], p.door);
    if (p.motion != last.motion || ((p.flags ^ last.flags) & (kAirborne | kOtherBank)))
        Log("peer motion %u%s%s at (%.0f, %.0f, %.0f)", p.motion, (p.flags & kOtherBank) ? " (other set)" : "",
            (p.flags & kAirborne) ? " airborne" : "", p.pos[0],
            p.pos[1], p.pos[2]);
    if (p.hp != last.hp) Log("peer HP %d/%d", p.hp, p.maxHp);
}

// Late (reordered) packets still go into the time-sorted history, where
// playback can use them; only the "latest state" bookkeeping ignores them.
void AddToHistory(const AvatarPacket& p) {
    int i = g_historyCount;
    while (i > 0 && static_cast<std::int32_t>(g_history[i - 1].senderMs - p.senderMs) > 0) --i;
    if (i > 0 && g_history[i - 1].p.seq == p.seq) return;  // duplicate
    if (g_historyCount == kHistory) {
        if (i == 0) return;  // older than everything we keep
        std::memmove(&g_history[0], &g_history[1], (i - 1) * sizeof(Sample));
        --i;
    } else {
        std::memmove(&g_history[i + 1], &g_history[i], (g_historyCount - i) * sizeof(Sample));
        ++g_historyCount;
    }
    g_history[i] = {p.senderMs, p};
}

void OnRemote(const AvatarPacket& p) {
    RemoteView& r = g_remote;
    std::uint32_t now = NowMs();
    if (r.seen && p.seq + 600 < r.lastSeq) {
        // Its counter went far back: the other game restarted (two-PC test 23:45:
        // every packet after the friend's restart counted as "late" and was never shown).
        Log("avatar link: the other game restarted (packet %u after %u); starting fresh", p.seq, r.lastSeq);
        r = RemoteView {};
        g_historyCount = 0;
        g_offsetCount = g_offsetNext = 0;
        g_hitSeenMax = 0;
        g_hitSeenWindow = 0;
        g_warnedBuild = false;
        WorldSyncOnPeerRestart();
        PauseSyncOnPeerRestart();
    }
    g_offsets[g_offsetNext] = static_cast<std::int32_t>(now - p.senderMs);
    g_offsetNext = (g_offsetNext + 1) % 128;
    if (g_offsetCount < 128) ++g_offsetCount;
    if (!r.seen) {
        Log("avatar link: first packet from peer (seq %u)", p.seq);
    } else if (p.seq <= r.lastSeq) {
        ++r.late;
        AddToHistory(p);
        return;
    } else {
        r.lost += p.seq - r.lastSeq - 1;
        LogPeerChanges(p);
    }
    FollowOnHostLocation(p.world, p.room, p.door, p.programs, (p.flags & kHasActor) != 0);
    r.seen = true;
    r.lastSeq = p.seq;
    ++r.received;
    r.last = p;
    g_lastArrivalMs = now;
    AddToHistory(p);
}

const Sample& History(int age) {  // 0 = newest
    return g_history[g_historyCount - 1 - age];
}

float LerpAngle(float a, float b, float t) {
    constexpr float kPi = 3.14159265f;
    float d = std::fmod(b - a + 3 * kPi, 2 * kPi) - kPi;
    return a + d * t;
}

std::uint32_t PlaybackTarget() {
    if (g_offsetCount == 0) return 0;
    std::int32_t offset = g_offsets[0];
    for (int i = 1; i < g_offsetCount; ++i) offset = (std::min)(offset, g_offsets[i]);
    return NowMs() - static_cast<std::uint32_t>(offset) - static_cast<std::uint32_t>(g_interpMs);
}

// The peer's pose as it was (interp) ms before the newest data, blended
// between the two received poses around that moment.
PeerPose PoseAtPlayback() {
    const AvatarPacket* chosen = &History(0).p;
    float blend[4] = {};
    bool blended = false;
    if (g_interpMs > 0 && g_historyCount >= 2) {
        // Smallest observed (local - sender) ~ clock difference + the fastest delivery.
        std::uint32_t target = PlaybackTarget();
        for (int age = 0; age + 1 < g_historyCount; ++age) {
            const Sample& newer = History(age);
            const Sample& older = History(age + 1);
            if (static_cast<std::int32_t>(target - older.senderMs) < 0) {
                chosen = &older.p;  // keep walking back
                continue;
            }
            chosen = &older.p;
            std::int32_t span = static_cast<std::int32_t>(newer.senderMs - older.senderMs);
            bool sameRoom = older.p.world == newer.p.world && older.p.room == newer.p.room;
            if (span > 0 && sameRoom && (older.p.flags & kHasActor) && (newer.p.flags & kHasActor)) {
                float t = (std::min)(1.0f, static_cast<float>(static_cast<std::int32_t>(target - older.senderMs)) / span);
                for (int i = 0; i < 3; ++i) blend[i] = older.p.pos[i] + (newer.p.pos[i] - older.p.pos[i]) * t;
                blend[3] = LerpAngle(older.p.angle, newer.p.angle, t);
                blended = true;
            }
            break;
        }
    }
    const AvatarPacket& p = *chosen;
    PeerPose pose {p.world, p.room, (p.flags & kHasActor) != 0, (p.flags & kAirborne) != 0,
                   {p.pos[0], p.pos[1], p.pos[2]}, p.angle, p.motion, (p.flags & kDowned) != 0, p.hp,
                   (p.flags & kOtherBank) != 0};
    if (blended) {
        for (int i = 0; i < 3; ++i) pose.pos[i] = blend[i];
        pose.angle = blend[3];
    }
    return pose;
}

// True the first time a hit id is seen (ids from one peer only ever grow;
// a 64-id window covers repeats and reordering).
bool FirstTimeHit(std::uint32_t id) {
    if (id > g_hitSeenMax) {
        std::uint32_t shift = id - g_hitSeenMax;
        g_hitSeenWindow = shift >= 64 ? 0 : g_hitSeenWindow << shift;
        g_hitSeenWindow |= 1;
        g_hitSeenMax = id;
        return true;
    }
    std::uint32_t back = g_hitSeenMax - id;
    if (back >= 64 || (g_hitSeenWindow & (1ull << back))) return false;
    g_hitSeenWindow |= 1ull << back;
    return true;
}

bool SameBuild(std::uint16_t version, std::uint32_t build) {
    if (version == kVersion && build == kBuild) return true;
    if (!g_warnedBuild) {
        Log("avatar link: IGNORING the peer: it runs a different mod build (version %u build %08X, ours %u %08X). "
            "Copy the same kh2coop.dll to both PCs.", version, build, kVersion, kBuild);
        g_warnedBuild = true;
    }
    return false;
}

void LearnPeer(const sockaddr_in& from) {
    if (!g_learnPeer) return;
    if (g_peerKnown && from.sin_addr.s_addr == g_peer.sin_addr.s_addr && from.sin_port == g_peer.sin_port) return;
    g_peer = from;
    g_peerKnown = true;
    char ip[32];
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    Log("avatar link: friend found at %s:%d; sending to it from now on", ip, ntohs(from.sin_port));
}

void DrainReceive() {
    char buf[1500];
    for (;;) {
        sockaddr_in from {};
        int fromLen = sizeof(from);
        int n = recvfrom(g_socket, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n == SOCKET_ERROR) return;  // WSAEWOULDBLOCK: nothing left (or ICMP "port closed" noise)
        std::uint32_t magic;
        if (n < 10) continue;
        std::memcpy(&magic, buf, 4);
        if (magic == kMagic) {
            if (n != sizeof(AvatarPacket)) {
                SameBuild(0, 0);
                continue;
            }
            AvatarPacket p;
            std::memcpy(&p, buf, sizeof(p));
            if (!SameBuild(p.version, p.build)) continue;
            LearnPeer(from);
            OnRemote(p);
        } else if (magic == kWorldMagic) {
            WorldSyncOnPacket(buf, n);
        } else if (magic == 0x4532484B) {  // "KH2E": the friend hit one of our enemies
            WorldSyncOnClaim(buf, n);
        } else if (magic == kFeaturesMagic && n == sizeof(FeaturesPacket)) {
            FeaturesPacket f;
            std::memcpy(&f, buf, sizeof(f));
            if (!SameBuild(f.version, f.build) || f.bits == g_peerFeatureBits) continue;
            g_peerFeatureBits = f.bits;
            if (f.bits == g_featureBits) {
                Log("avatar link: the other PC runs the same co-op features as us");
                continue;
            }
            for (int i = 0; i < static_cast<int>(sizeof(kFeatures) / sizeof(kFeatures[0])); ++i) {
                bool ours = (g_featureBits >> i) & 1, theirs = (f.bits >> i) & 1;
                if (ours != theirs)
                    Log("avatar link: SETTINGS DIFFER: %s is %s here but %s on the other PC. Both PCs need the same "
                        "settings (copy the new kh2coop.ini too).", kFeatures[i].setting + 8, ours ? "ON" : "off",
                        theirs ? "ON" : "off");
            }
        } else if (magic == 0x5032484B) {  // "KH2P": the other player's combat pause
            PauseSyncOnPacket(buf, n);
        } else if (magic == 0x4732484B) {  // "KH2G": the host's Game Over screen and choice
            GameOverSyncOnPacket(buf, n);
        } else if (magic == 0x4C32484B) {  // "KH2L": load barrier (both games leave a load together)
            LoadBarrierOnPacket(buf, n);
        } else if (magic == 0x4432484B && n == 12 && g_debug && ntohl(from.sin_addr.s_addr) == 0x7F000001) {
            // "KH2D" {u32 magic, i32 who, i32 amount}: test command from tools/debug_cmd.py on this PC only
            std::int32_t who, amount;
            std::memcpy(&who, buf + 4, 4);
            std::memcpy(&amount, buf + 8, 4);
            Log("debug command: damage %d to %d", amount, who);
            PuppetDebugDamage(who, amount);
        } else if (magic == kHitMagic && n == sizeof(HitPacket)) {
            HitPacket h;
            std::memcpy(&h, buf, sizeof(h));
            if (!SameBuild(h.version, h.build) || !FirstTimeHit(h.hitId)) continue;
            Log("peer says our player was hit: %d damage (hit #%u)", h.damage, h.hitId);
            PuppetOnPeerHit(h.damage, h.react);
        } else if (magic == kHealMagic && n == sizeof(HitPacket)) {
            HitPacket h;  // damage = heal in % of max HP, react = source (1 Cure, 2 item)
            std::memcpy(&h, buf, sizeof(h));
            if (!SameBuild(h.version, h.build) || !FirstTimeHit(h.hitId)) continue;
            Log("peer healed our player: %d%% of max HP (%s, #%u)", h.damage, h.react == 1 ? "Cure" : "item", h.hitId);
            PuppetOnPeerHeal(h.damage, h.react);
        }
    }
}

}  // namespace

void AvatarLinkInit() {
    int listenPort = EnvInt("KH2COOP_LISTEN", 0);
    char peer[64] = {};
    if (listenPort <= 0 || !EnvStr("KH2COOP_PEER", peer, sizeof(peer))) return;

    bool loopbackPeer = false;
    if (_stricmp(peer, "auto") == 0) {
        g_learnPeer = true;  // host: the friend's address comes from its first packet
    } else {
        char* colon = std::strrchr(peer, ':');
        if (!colon) {
            Log("avatar link: PEER must be ip:port or auto, got \"%s\"", peer);
            return;
        }
        *colon = '\0';
        g_peer.sin_family = AF_INET;
        g_peer.sin_port = htons(static_cast<u_short>(std::atoi(colon + 1)));
        if (inet_pton(AF_INET, peer, &g_peer.sin_addr) != 1) {
            Log("avatar link: bad peer address \"%s\"", peer);
            return;
        }
        g_peerKnown = true;
        loopbackPeer = (ntohl(g_peer.sin_addr.s_addr) >> 24) == 127;
    }

    // Listen on loopback for the one-PC bench (no firewall prompt), on every
    // network interface otherwise. KH2COOP_BIND overrides.
    char bindIp[32] = {};
    if (!EnvStr("KH2COOP_BIND", bindIp, sizeof(bindIp))) strcpy_s(bindIp, loopbackPeer ? "127.0.0.1" : "0.0.0.0");

    char sim[64] = {};
    if (EnvStr("KH2COOP_NET_SIM", sim, sizeof(sim))) {
        g_simLatency = std::atoi(sim);
        const char* c = std::strchr(sim, ',');
        if (c) g_simJitter = std::atoi(c + 1), c = std::strchr(c + 1, ',');
        if (c) g_simLoss = std::atoi(c + 1);
        g_sim = g_simLatency > 0 || g_simJitter > 0 || g_simLoss > 0;
        std::srand(GetCurrentProcessId());
    }
    g_interpMs = EnvInt("KH2COOP_INTERP_MS", 100);
    g_debug = EnvInt("KH2COOP_DEBUG", 0) == 1;
    // Role: the host owns the world (rooms now, enemies later); the friend follows.
    char role[16] = {};
    bool host = EnvStr("KH2COOP_ROLE", role, sizeof(role)) ? _stricmp(role, "host") == 0 : g_learnPeer;
    Log("avatar link: role %s", host ? "HOST" : "FRIEND");
    FollowInit(host);
    LoadBarrierInit(host);
    for (int i = 0; i < static_cast<int>(sizeof(kFeatures) / sizeof(kFeatures[0])); ++i)
        if (EnvInt(kFeatures[i].setting, kFeatures[i].fallback) == 1) g_featureBits |= 1u << i;
    WorldSyncInit(host);
    PauseSyncInit();
    DownedInit();
    GameOverSyncInit(host);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nonBlocking = 1;
    ioctlsocket(g_socket, FIONBIO, &nonBlocking);
    // An ICMP "port unreachable" (peer not started yet) would otherwise make
    // recvfrom fail with WSAECONNRESET until the next send.
    BOOL reportReset = FALSE;
    DWORD ignored = 0;
    constexpr DWORD kSioUdpConnReset = 0x9800000C;  // SIO_UDP_CONNRESET
    WSAIoctl(g_socket, kSioUdpConnReset, &reportReset, sizeof(reportReset), nullptr, 0, &ignored, nullptr, nullptr);
    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<u_short>(listenPort));
    inet_pton(AF_INET, bindIp, &local.sin_addr);
    if (bind(g_socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        Log("avatar link: cannot listen on %s:%d (error %d)", bindIp, listenPort, WSAGetLastError());
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
        return;
    }
    Log("avatar link v%u build %08X: listening on %s:%d, %s; playback delay %d ms%s", kVersion, kBuild, bindIp,
        listenPort, g_learnPeer ? "waiting for the friend's first packet" : "sending to the peer", g_interpMs,
        "");
    if (g_peerKnown) Log("avatar link: peer %s:%d", peer, ntohs(g_peer.sin_port));
    if (g_sim)
        Log("avatar link: NETWORK SIMULATOR on: +%d ms, jitter +-%d ms, %d%% loss on everything we send", g_simLatency,
            g_simJitter, g_simLoss);
}

void AvatarLinkSendHeal(int percent, int source) {
    if (g_socket == INVALID_SOCKET) return;
    HitPacket h {kHealMagic, kVersion, kBuild, ++g_hitSendId, percent, source};
    for (int i = 0; i < kHitRepeats; ++i) Send(&h, sizeof(h));
    Log("heal for the peer's player: %d%% of max HP sent (%s, #%u)", percent, source == 1 ? "Cure" : "item", h.hitId);
}

void AvatarLinkSendHit(int damage, int react) {
    if (g_socket == INVALID_SOCKET) return;
    HitPacket h {kHitMagic, kVersion, kBuild, ++g_hitSendId, damage, react};
    for (int i = 0; i < kHitRepeats; ++i) Send(&h, sizeof(h));
    Log("hit on the peer's copy: %d damage sent (hit #%u)", damage, h.hitId);
}

void AvatarLinkSendRaw(const void* data, int len) {
    if (g_socket != INVALID_SOCKET) Send(data, len);
}

std::uint32_t AvatarLinkPlaybackSenderMs() { return PlaybackTarget(); }
std::uint32_t AvatarLinkNowMs() { return NowMs(); }
std::uint32_t AvatarLinkBuild() { return kBuild; }

bool AvatarLinkPeerNow(PeerPose& out) {
    if (!g_remote.seen || NowMs() - g_lastArrivalMs > 500) return false;
    const AvatarPacket& p = g_remote.last;
    out = PeerPose {p.world, p.room, (p.flags & kHasActor) != 0, (p.flags & kAirborne) != 0,
                    {p.pos[0], p.pos[1], p.pos[2]}, p.angle, p.motion, (p.flags & kDowned) != 0, p.hp,
                   (p.flags & kOtherBank) != 0};
    return true;
}

void AvatarLinkFrame() {
    if (g_socket == INVALID_SOCKET) return;
    SendOwnAvatar();
    if (g_frames % 120 == 0) {
        FeaturesPacket f {kFeaturesMagic, kVersion, kBuild, g_featureBits};
        Send(&f, sizeof(f));
    }
    if (g_sim) FlushSim();
    DrainReceive();
    // Feed the puppet every frame while data is fresh; after 2 s of silence
    // stop, so the puppet's own staleness rule takes over.
    if (g_historyCount > 0 && NowMs() - g_lastArrivalMs < 2000) PuppetOnPeerPose(PoseAtPlayback());
    if (++g_frames % 600 == 0) {  // about every 10 s
        const AvatarPacket& p = g_remote.last;
        std::int32_t minOff = g_offsetCount ? g_offsets[0] : 0, maxOff = minOff;
        for (int i = 1; i < g_offsetCount; ++i) {
            minOff = (std::min)(minOff, g_offsets[i]);
            maxOff = (std::max)(maxOff, g_offsets[i]);
        }
        Log("avatar link: sent %u, received %u in order, gaps %u, late %u, delay spread %d ms%s | peer world 0x%02X room "
            "0x%02X pos (%.0f, %.0f, %.0f) angle %.2f motion %u",
            g_sendSeq, g_remote.received, g_remote.lost, g_remote.late, maxOff - minOff,
            g_peerKnown ? "" : " (friend not found yet)", p.world, p.room, p.pos[0], p.pos[1], p.pos[2], p.angle,
            p.motion);
        if (g_sim && g_simDropped) Log("avatar link: simulator dropped %u of our packets so far", g_simDropped);
    }
}

}  // namespace kh2coop
