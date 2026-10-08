#pragma once
// Aggro log: who hits enemies and whom SetTarget picks (KH2COOP_AGGRO_LOG=1).

#include <cstdint>

namespace kh2coop {

void AggroInit();   // from OnInit
void AggroFrame();  // from OnFrame
// Before an actor's update: the Sora copy if that enemy should see it as "the player" during this
// update (far from our Sora, closer to the copy; KH2COOP_AI_NEAREST_PLAYER), else 0.
std::uintptr_t AggroPlayerSwapFor(std::uintptr_t actor);

}  // namespace kh2coop
