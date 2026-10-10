// Spawn step (user pick 2026-10-10, session 22). After a room load (a new room, the same room reloaded for
// a scripted fight, a loaded save) both games put their own Sora on the same entry spot, and the other
// player's Sora copy (solid with PUPPET_COLLIDE=1, placed every frame where the other player stands)
// pushed our Sora away; the push reached the other game through our position, so the two slid apart
// (bench 2026-10-10: entering 02/03 and 02/06 they appeared 16 and 25 units apart and were shoved 35-55
// units each in 0.1-0.2 s; contact starts somewhere between 63 and 77 apart and a shove ends near 100).
//
// Now, for a short window after our Sora is back in the field, the two Soras are kept kClear apart:
// - the other player is in the room: while closer than kClear, our Sora moves straight away from theirs
//   (host to one side of its facing and friend to the other when they're on one spot), half the missing
//   distance per frame; they do the same, and if one is against a wall the other keeps going;
// - the other player isn't here yet: our Sora steps kClear / 2 to its own side, so they don't appear on us.
// When the other player comes into our room near our Sora, ours helps only if theirs couldn't get clear
// by itself within kHelpDelay frames. Only while a partner is connected. A load that ends in a story scene
// starts the window once the scene hands Sora back.
//
// The moves go through the game's own movement, not a teleport: actor+0xA48 is a one-frame displacement
// that the actor's move (exe+0x3B89A0, called by the per-entity update exe+0x3BFD30) passes to
// exe+0x3B9090 together with the pushes from other actors; the sum goes through the terrain collision and
// +0xA48 is cleared at the end of the move (Ghidra 2026-10-10, VERIFIED_OFFSETS 'One-frame
// displacement'). A wall stops it like it stops a push. KH2COOP_SPAWN_STEP=0 turns it off.

#include "spawn_step.hpp"

#include <cmath>
#include <cstdint>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;        // u8 world, u8 room
constexpr std::uintptr_t kInField = 0x9BA8D0;    // u8: 0 while a room loads
constexpr std::uintptr_t kListHead = 0x2A171C8;  // u64 -> the player's actor in the field
constexpr std::uintptr_t kActorPos = 0x670;      // f32 x, y, z (= entity +0x30)
constexpr std::uintptr_t kActorAngle = 0x640 + 0x4C;  // f32 facing (entity +0x4C): forward = (sin, cos)
constexpr std::uintptr_t kActorStepIn = 0xA48;   // f32 x, y, z: one-frame displacement, cleared by the move
constexpr float kClear = 110.0f;                 // closer than this, the two Soras may push each other apart
constexpr float kMaxPerFrame = 20.0f;            // so no single move is large
constexpr int kWindow = 45;                      // frames the two are kept apart after a spawn or an arrival
constexpr int kHelpDelay = 15;                   // frames the newcomer gets to clear the spot by itself
constexpr float kDone = 2.0f;
constexpr float kOnTop = 20.0f;                  // closer than this, "away from the other" has no direction
constexpr float kSameSpot = 10.0f;               // a "load end" that didn't put Sora back: no second side step
constexpr std::uint32_t kPeerFreshMs = 5000;

enum class Why { kSpawn, kArrival };

bool g_on = false;
float g_side = 1.0f;  // host +1, friend -1: across the facing, (cos a, 0, -sin a)
bool g_wasLoading = true;
bool g_spawnPending = false, g_arrivalPending = false;
bool g_peerWasHere = false;
std::uintptr_t g_actor = 0;
Why g_why = Why::kSpawn;
int g_frame = 0;            // frames into the current window
bool g_active = false;
float g_sideDir[2] = {};    // our side across the facing at the window's start (x, z)
bool g_haveTarget = false;  // the side step's goal while the other player isn't here
float g_target[2] = {};
std::uint16_t g_targetRoom = 0;
std::uint32_t g_logs = 0;

std::uint16_t Room() {
    const auto* now = reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
    return static_cast<std::uint16_t>(now[0] | now[1] << 8);
}

// The other player's Sora in our room right now (fresh packet), else false.
bool PeerHere(PeerPose& peer) {
    if (!AvatarLinkPeerNow(peer) || !peer.hasActor) return false;
    if (peer.pos[0] == 0.0f && peer.pos[1] == 0.0f && peer.pos[2] == 0.0f) return false;  // mid-load
    std::uint16_t room = Room();
    return peer.world == (room & 0xFF) && peer.room == (room >> 8);
}

void Add(std::uintptr_t actor, float dx, float dz) {
    float d = std::hypot(dx, dz);
    if (d > kMaxPerFrame) {
        dx *= kMaxPerFrame / d;
        dz *= kMaxPerFrame / d;
    }
    auto* in = reinterpret_cast<float*>(actor + kActorStepIn);
    in[0] += dx;
    in[2] += dz;
}

void Start(std::uintptr_t actor, Why why) {
    const auto* pos = reinterpret_cast<const float*>(actor + kActorPos);
    float a = *reinterpret_cast<const float*>(actor + kActorAngle);
    g_sideDir[0] = std::cos(a) * g_side;
    g_sideDir[1] = -std::sin(a) * g_side;
    g_actor = actor;
    g_why = why;
    g_frame = 0;
    g_active = true;
    PeerPose peer {};
    bool here = PeerHere(peer);
    float d = here ? std::hypot(pos[0] - peer.pos[0], pos[2] - peer.pos[2]) : 0.0f;
    if (why == Why::kSpawn && !here) {
        std::uint16_t room = Room();
        if (g_haveTarget && room == g_targetRoom && std::fabs(pos[0] - g_target[0]) < kSameSpot &&
            std::fabs(pos[2] - g_target[1]) < kSameSpot) {
            if (g_logs++ < 100) Log("spawn step: Sora is still where the last step put him: no second step");
            g_active = false;
            return;
        }
        g_target[0] = pos[0] + g_sideDir[0] * kClear / 2;
        g_target[1] = pos[2] + g_sideDir[1] * kClear / 2;
        g_targetRoom = room;
        g_haveTarget = true;
    }
    if ((why == Why::kSpawn || (here && d < kClear)) && g_logs++ < 100)
        Log("spawn step: %s at (%.0f, %.0f, %.0f) facing %.0f deg: %s", why == Why::kSpawn ? "our Sora appeared" :
            "the other Sora came in", pos[0], pos[1], pos[2], a * 57.2957795f, !here ? "other Sora not here, step to our side"
            : d < kClear ? "keeping the two apart" : "already apart");
}

void Step(std::uintptr_t actor) {
    if (++g_frame > kWindow) {
        g_active = false;
        return;
    }
    const auto* pos = reinterpret_cast<const float*>(actor + kActorPos);
    PeerPose peer {};
    if (PeerHere(peer)) {
        float vx = pos[0] - peer.pos[0], vz = pos[2] - peer.pos[2], d = std::hypot(vx, vz);
        if (d >= kClear) return;
        if (g_why == Why::kArrival && g_frame <= kHelpDelay) return;  // theirs moves first
        float share = (kClear - d) * 0.5f + 1.0f;  // they cover the other half
        if (d >= kOnTop)
            Add(actor, vx / d * share, vz / d * share);
        else
            Add(actor, g_sideDir[0] * share, g_sideDir[1] * share);
        return;
    }
    if (g_why == Why::kSpawn && g_haveTarget) {
        float dx = g_target[0] - pos[0], dz = g_target[1] - pos[2];
        if (std::hypot(dx, dz) >= kDone) Add(actor, dx, dz);
    }
}

}  // namespace

void SpawnStepInit(bool host) {
    g_side = host ? 1.0f : -1.0f;
    g_on = EnvInt("KH2COOP_SPAWN_STEP", 1) == 1;
    Log("spawn step: %s", g_on ? (host ? "on (host side)" : "on (friend side)") : "off (KH2COOP_SPAWN_STEP=0)");
}

void SpawnStepFrame() {
    if (!g_on) return;
    bool loading = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0;
    if (loading) {
        g_active = false;  // the actor is going away
        g_arrivalPending = false;
    }
    if (g_wasLoading && !loading) g_spawnPending = true;
    g_wasLoading = loading;
    PeerPose peer {};
    bool here = !loading && PeerHere(peer);
    // They came into our room (not us into theirs: that's our own spawn).
    if (here && !g_peerWasHere && !g_spawnPending) g_arrivalPending = true;
    g_peerWasHere = here;
}

void SpawnStepBeforeUpdate(std::uintptr_t actor) {
    if (!g_on) return;
    if (g_active && actor == g_actor) {
        Step(actor);
        return;
    }
    if ((!g_spawnPending && !g_arrivalPending) || g_wasLoading) return;
    if (actor != *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kListHead)) return;
    // In a story scene the list head is some scene actor or item (VERIFIED_OFFSETS 'List head during a
    // story scene'): wait for our own Sora.
    if (!PuppetIsFieldPlayer(actor)) return;
    Why why = g_spawnPending ? Why::kSpawn : Why::kArrival;
    g_spawnPending = g_arrivalPending = false;
    if (AvatarLinkPeerAgeMs() >= kPeerFreshMs) return;  // playing alone: nobody to make room for
    Start(actor, why);
    if (g_active) Step(actor);  // this frame's share
}

}  // namespace kh2coop
