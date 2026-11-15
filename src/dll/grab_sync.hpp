#pragma once
// An enemy's grab of the other player's Sora copy shows on that player's own Sora (TODO 1.62, see grab_sync.cpp).

#include <cstdint>

namespace kh2coop {

void GrabSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void GrabSyncFrame();                           // from OnFrame while co-op is on (game thread)
void GrabSyncOnPacket(const char* buf, int n);  // a "KH2B" packet from the other PC
// Research (TODO 1.179): an in-fight shot ('re_...') starts, from cutscene_twin.cpp's StartCutscene hook: its script,
// its base matrix (the hook's second argument) and its owner + 0x80 (the fourth).
void GrabSyncOnScene(std::uintptr_t script, std::uintptr_t base, std::uintptr_t owner80);
// Friend: from the entity update hook, after the game's update: our held Sora takes the host's copy's place.
void GrabSyncAfterUpdate(std::uintptr_t actor);
// Script call thunk (world_census.cpp), before the game's function: true = not run in our game (sent instead).
bool GrabSyncHolds(int table, int index, const std::uint64_t* args);
// Host: an enemy's grab of the friend's copy is on and the copy is still grabbed (grab motion, attached or hooked):
// puppet.cpp leaves its motion and place to the script.
bool GrabSyncCopyHeld();
// Friend: the host's grab of its copy is playing on our Sora now (a grab motion run since the last let go, within
// kMaxFrames): world_sync.cpp sends no claims then, the host's game counts those hits (TODO 1.84).
bool GrabSyncOurSoraHeld();
// Friend: GrabSyncOurSoraHeld, by the host's enemy netId / instance (whose capture ran last; reaction_sync.cpp lets a
// chain RC's repeats through for it, TODO 1.197). Ids, not an actor pointer: nothing kept to go stale.
bool GrabSyncOurSoraHeldBy(std::uint16_t netId, std::uint16_t instance);
// Host: the player this enemy last grabbed with a hook in this room: 0 none, 1 our Sora, 2 the friend's copy.
int GrabSyncLastHooked(std::uintptr_t grabber);
// Host: this field object launched the friend's copy less than a second ago (TODO 1.193: aggro.cpp's ride rule waits).
bool GrabSyncCopyLaunchedBy(std::uintptr_t actor);
// Host: this enemy's script hooked our Sora or the friend's copy (trap_obj_hook or a grab) and that player is still
// hooked: it holds a player now (world_sync.cpp: the friend's hits can't kill it then).
bool GrabSyncHoldsPlayer(std::uintptr_t enemy);
// Host: the player this enemy holds now (as GrabSyncHoldsPlayer): 0 none, 1 our Sora, 2 the friend's copy.
int GrabSyncHolding(std::uintptr_t enemy);
// Both games: the other player rides a board each game runs for its own player (world_sync.cpp's carriers, TODO
// 1.200): our own board goes on the copy and plays the board's part of the rider's motion; motion < 0 = the ride
// ended, the board is let go. The caller has checked that both are live actors.
void GrabSyncShowRide(std::uintptr_t board, std::uintptr_t rider, int motion);
// Friend, from the motion set hook: our Sora's sets before its first update out of a grab pose lose their blend (1.195).
void GrabSyncMotionSet(std::uintptr_t actor, int motion, float* blendFrames);

}  // namespace kh2coop
