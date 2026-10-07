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
bool WorldSyncBlocksMotion(std::uintptr_t actor, int motion);   // motion hook: true = only our motion changes go through
bool WorldSyncBlocksDamage(std::uintptr_t actor, int damage, int react);  // stat hook: true = drop local HP loss (host owns it)

// For spawn_sync.cpp (friend side).
struct HostEnemy {
    std::uint16_t serial;
    std::uint32_t objId;
    float pos[3];  // where it is now in the host's game
};
bool WorldSyncHostPresent();                   // friend: the host's world data is fresh and for our room
std::uint32_t WorldSyncEpoch();                // changes whenever our room or the host's room instance changes
int WorldSyncHostEnemies(HostEnemy* out, int max);  // the host's live enemies that came from a spawn record

}  // namespace kh2coop
