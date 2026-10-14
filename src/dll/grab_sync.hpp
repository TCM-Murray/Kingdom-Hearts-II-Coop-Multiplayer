#pragma once
// An enemy's grab of the other player's Sora copy shows on that player's own Sora (TODO 1.62, see grab_sync.cpp).

#include <cstdint>

namespace kh2coop {

void GrabSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void GrabSyncFrame();                           // from OnFrame while co-op is on (game thread)
void GrabSyncOnPacket(const char* buf, int n);  // a "KH2B" packet from the other PC
// Script call thunk (world_census.cpp), before the game's function: true = not run in our game (sent instead).
bool GrabSyncHolds(int table, int index, const std::uint64_t* args);
// Host: an enemy's grab of the friend's copy is on and the copy is still grabbed (grab motion, attached or hooked):
// puppet.cpp leaves its motion and place to the script.
bool GrabSyncCopyHeld();

}  // namespace kh2coop
