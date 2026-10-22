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
// Friend: the host's grab of its copy is playing on our Sora now (a grab motion run since the last let go, within
// kMaxFrames): world_sync.cpp sends no claims then, the host's game counts those hits (TODO 1.84).
bool GrabSyncOurSoraHeld();
// Host: the player this enemy last grabbed with a hook in this room: 0 none, 1 our Sora, 2 the friend's copy.
int GrabSyncLastHooked(std::uintptr_t grabber);
// Host: this enemy's script hooked our Sora or the friend's copy (trap_obj_hook or a grab) and that player is still
// hooked: it holds a player now (world_sync.cpp: the friend's hits can't kill it then).
bool GrabSyncHoldsPlayer(std::uintptr_t enemy);
// Host: the player this enemy holds now (as GrabSyncHoldsPlayer): 0 none, 1 our Sora, 2 the friend's copy.
int GrabSyncHolding(std::uintptr_t enemy);

}  // namespace kh2coop
