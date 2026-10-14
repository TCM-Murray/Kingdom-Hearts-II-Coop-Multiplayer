#pragma once
// World sync (N3): host streams companions and enemies; the friend mirrors
// companions and checks how its enemies match the host's (see world_sync.cpp).

#include <cstdint>

namespace kh2coop {

void WorldSyncInit(bool host);                      // from AvatarLinkInit, once the role is known
void WorldSyncOnPacket(const char* buf, int n);     // a "KH2W" packet from the peer
void WorldSyncOnClaim(const char* buf, int n);      // a "KH2E" hit claim from the friend (host side)
void WorldSyncFrame();
void WorldSyncOnPeerRestart();                     // the other game restarted: drop its old state                              // from OnFrame (game thread)
void WorldSyncAfterUpdate(std::uintptr_t actor);    // from the entity update hook, after the game's update
bool WorldSyncSkipAi(std::uintptr_t actor);         // friend AI hook: true = we drive this companion
std::uintptr_t WorldSyncMirroredCompanion(std::uint32_t objId);  // friend: our companion copying the host's one;
                                                                 // host: ours playing the friend's Limit partner; or 0
void WorldSyncOurLimitPartner(std::uintptr_t actor);  // friend: our Limit played its partner's part on this companion
bool WorldSyncSkipsBrain(std::uintptr_t actor);     // one brain (world_mirror.cpp): friend's bound enemy
// One-brain replay (world_mirror.cpp). Host: our number for this tracked enemy (0 = not tracked) and our room
// instance. Friend: our live enemy bound to that host enemy of that host room instance (0 = none).
std::uint16_t WorldSyncNetIdOf(std::uintptr_t actor, std::uint16_t* instance);
std::uintptr_t WorldSyncBoundActor(std::uint16_t hostNetId, std::uint16_t hostInstance);
// Friend: the host's number (and room instance) our live enemy is bound to (0 = none). Host: our live enemy with
// that number in our current room instance (0 = none). Reaction sync (reaction_sync.cpp).
std::uint16_t WorldSyncBoundTo(std::uintptr_t actor, std::uint16_t* hostInstance);
std::uintptr_t WorldSyncHostActor(std::uint16_t netId, std::uint16_t instance);
bool WorldSyncBlocksMotion(std::uintptr_t actor, int motion);   // motion hook: true = only our motion changes go through
bool WorldSyncBlocksDamage(std::uintptr_t actor, int damage, int react);  // stat hook: true = drop local HP loss (host owns it)
void WorldSyncAttackerRecorded(std::uintptr_t enemy, std::uintptr_t attacker);  // RecordAttacker hook: this hit's attacker
void WorldSyncDebugBossHp(int hp);  // KH2COOP_DEBUG test (tools/debug_cmd.py bosshp): every boss's HP set to hp

// For spawn_sync.cpp (friend side).
struct HostEnemy {
    std::uint16_t serial;
    std::uint32_t objId;
    float pos[3];  // where it is now in the host's game
    float angle;            // its facing there (WorldSyncHostUnmatched)
    std::uint16_t netId;    // world sync's number for it (WorldSyncHostUnmatched)
};
bool WorldSyncHostPresent();                   // friend: the host's world data is fresh and for our room
std::uint32_t WorldSyncEpoch();                // changes whenever our room or the host's room instance changes
int WorldSyncHostEnemies(HostEnemy* out, int max);  // the host's live enemies that came from a spawn record
// The host's objects with no spawn record (a script or game code made them) that none of ours matches after a moment:
// our game has nothing to bind to them (one brain: only the host's boss script makes Luxord's cards).
int WorldSyncHostUnmatched(HostEnemy* out, int max);
// Friend: we just made this actor for the host's object netId (spawn_sync.cpp): bind it to that one, no other.
void WorldSyncMadeFor(std::uintptr_t actor, std::uint16_t netId);

}  // namespace kh2coop
