#pragma once
// The other player's die or card on their copy (TODO 1.90 step 3), and their ship on the World Map (TODO 5.25).

#include <cstdint>

#include "puppet.hpp"

namespace kh2coop {

// From PuppetFrame (game thread): the copy we drive (0 none), the peer's last pose (null when stale or elsewhere),
// and whether the room changed or is loading (its actors are gone then).
void StandInFrame(std::uintptr_t copy, const PeerPose* pose, bool roomGone);
// From spawn_sync.cpp's SpawnTask hook: makes the body asked for, with the game's actor factory.
void StandInSpawnStep();
// From puppet.cpp's per-actor update hook, after the actor's own update: places the World Map ship and plays its motion.
void StandInAfterUpdate(std::uintptr_t actor);
// From puppet.cpp's motion hook: true for the game's own motion sets on the World Map ship while we drive it.
bool StandInBlocksMotion(std::uintptr_t actor);
// From grab_sync.cpp: a boss's trap_player_capture_form (1/242) is running on our own player (the die / a card).
void StandInOurCapture();
// For our pose packet: our player is captured right now (from that call until its body is back to the one before).
bool StandInWeAreCaptured();

}  // namespace kh2coop
