#pragma once
// World sync (N3): host streams companions and enemies; the friend mirrors
// companions and checks how its enemies match the host's (see world_sync.cpp).

#include <cstdint>

#include "avatar_link.hpp"  // HitShape

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
bool WorldSyncShared(std::uintptr_t actor);         // one brain's actors: enemies, field objects, NPCs (TODO 1.163)
bool WorldSyncIsNpc(std::uintptr_t actor);          // an NPC by object type (2, 9, 15; F425)
std::uintptr_t WorldSyncNearestBoundNpc(std::uintptr_t to, float maxDist);  // friend: our bound NPC nearest 'to', or 0
bool WorldSyncCarrier(std::uintptr_t actor);        // a field object each game runs for its own player (rides)
bool WorldSyncSkipsBrain(std::uintptr_t actor);     // one brain (world_mirror.cpp): friend's bound enemy
// Friend: whom the host's enemy our `actor` copies goes for (aggro.cpp AggroTargetOf there): 1 = the host's Sora,
// 2 = the friend, 0 = neither / not bound / not known / ours runs its own brain (the game's own rules then).
int WorldSyncHostTargetOf(std::uintptr_t actor);
// One-brain replay (world_mirror.cpp). Host: our number for this tracked enemy (0 = not tracked) and our room
// instance. Friend: our live enemy bound to that host enemy of that host room instance (0 = none).
std::uint16_t WorldSyncNetIdOf(std::uintptr_t actor, std::uint16_t* instance);
// Host: lists this actor now, whatever its team (it is one brain's from then on, as any listed one): its number, or
// 0 (the list is full, or world sync is off). For an object whose script is about to act on the friend's copy.
std::uint16_t WorldSyncTrackNow(std::uintptr_t actor);
std::uintptr_t WorldSyncBoundActor(std::uint16_t hostNetId, std::uint16_t hostInstance);
// Friend: our copy of that host enemy, killed with the host's (its death, not a removal) and not yet freed by the game
// (its entry goes at the free, WorldSyncOnActorFree), so the same object: 0 if none. Grab sync (F740).
std::uintptr_t WorldSyncDyingActor(std::uint16_t hostNetId, std::uint16_t hostInstance);
// Friend: our copy of that host enemy was there in this room instance and has been killed or removed with the host's.
bool WorldSyncGoneHere(std::uint16_t hostNetId, std::uint16_t hostInstance);
// Friend: the host's number (and room instance) our live enemy is bound to (0 = none). Host: our live enemy with
// that number in our current room instance (0 = none). Reaction sync (reaction_sync.cpp).
std::uint16_t WorldSyncBoundTo(std::uintptr_t actor, std::uint16_t* hostInstance);
// Friend: the host's number our enemy is or was bound to, killed or removed too, until the game frees it (0 = none).
std::uint16_t WorldSyncBoundEver(std::uintptr_t actor);
// An actor's spawn record serial, the same in both games for a room's records; 0 = none (or the read failed).
std::uint16_t WorldSyncSpawnSerial(std::uintptr_t actor);
std::uintptr_t WorldSyncHostActor(std::uint16_t netId, std::uint16_t instance);
bool WorldSyncBlocksMotion(std::uintptr_t actor, int motion);   // motion hook: true = only our motion changes go through
bool WorldSyncBlocksDamage(std::uintptr_t actor, int damage, int react);  // stat hook: true = drop local HP loss (host owns it)
// TODO 1.201: a hit on an object with no stat block changes no HP, so no claim above carries it.
// Friend: our Sora's hit landed on such an object of ours bound to the host's: sent to the host (true = sent).
// `attackData` = the hit's 0x30-byte attack record (hit +0x20), which the object's script may ask about.
bool WorldSyncClaimHit(std::uintptr_t actor, int damage, std::uint32_t info, const HitShape& shape, const std::uint8_t* attackData);
// Host: an object with no stat block that the friend's game has too: the copy's replayed swing lands nothing on it
// (motion_mirror.cpp's hit check), the friend's hit claims do (as its HP hits count only from its claims, 1.40).
bool WorldSyncHitsByClaim(std::uintptr_t actor);
void WorldSyncAttackerRecorded(std::uintptr_t enemy, std::uintptr_t attacker);  // RecordAttacker hook: this hit's attacker
void WorldSyncDebugBossHp(int hp);  // KH2COOP_DEBUG test (tools/debug_cmd.py bosshp): every boss's HP set to hp
// Why this actor's script labels can't be looked up now (F403 taken away, F407 freed), or null when they can.
const char* WorldSyncScriptGone(std::uintptr_t actor);

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
// The removal route for an actor of ours (game thread, a live actor): faded out with the remove bit; the game then
// disposes of it once its script threads and effects have ended.
void WorldSyncFadeOut(std::uintptr_t actor);
// The host's enemies and field objects (no HP) that came from a spawn record, but not dead ones.
int WorldSyncHostEnemies(HostEnemy* out, int max);
// The host's objects with no spawn record (a script or game code made them) that none of ours matches after a moment:
// our game has nothing to bind to them (one brain: only the host's boss script makes Luxord's cards).
int WorldSyncHostUnmatched(HostEnemy* out, int max, bool (*noRecord)(std::uint16_t serial));
// Friend: we just made this actor for the host's object netId (spawn_sync.cpp): bind it to that one, no other.
void WorldSyncMadeFor(std::uintptr_t actor, std::uint16_t netId);
void WorldSyncOnActorFree(std::uintptr_t actor);  // form_look.cpp's actor free hook: the game frees this actor
// Friend: a script of maker's just made this actor (world_census.cpp's factory hook). Made by our copy of a host enemy,
// its brain stays off from the start (the host's twin of it thinks).
void WorldSyncScriptMade(std::uintptr_t actor, std::uintptr_t maker);

// For the diagnostic panel (diag_hud.cpp). Host: enemies it sends and the parts of its last frame; friend: the host's
// enemies it knows, ours, how many of ours copy one of the host's, and how old the host's newest world packet is.
struct WorldDiag {
    bool on, host;
    int hostEnemies, ourEnemies, bound;
    int partsLastFrame;
    std::uint32_t hostPacketAgeMs;  // friend (0xFFFFFFFF = none yet)
};
void WorldSyncDiag(WorldDiag& out);

}  // namespace kh2coop

