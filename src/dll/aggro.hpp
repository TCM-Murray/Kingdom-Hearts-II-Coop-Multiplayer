#pragma once
// Aggro log: who hits enemies and whom SetTarget picks (KH2COOP_AGGRO_LOG=1).

#include <cstdint>

namespace kh2coop {

void AggroInit();   // from OnInit
void AggroFrame();  // from OnFrame
// Before an actor's update: the Sora copy if that enemy should see it as "the player" during this
// update (far from our Sora, closer to the copy; KH2COOP_AI_NEAREST_PLAYER), else 0.
std::uintptr_t AggroPlayerSwapFor(std::uintptr_t actor);
// Host: a boss struck or grabbed a player (copy = the friend's Sora copy, else our Sora): it keeps that player as
// its player 2 s more (world_mirror.cpp strikes, grab_sync.cpp grabs and let go).
void AggroBossActedOn(std::uintptr_t boss, bool copy, const char* what);
// Host: whom our enemy goes for now (world_sync.cpp sends it): 1 = our Sora, 2 = the friend's copy, 0 = neither / not
// known. A boss: its current pick (the nearer player); others: their target slot.
int AggroTargetOf(std::uintptr_t actor);

}  // namespace kh2coop
