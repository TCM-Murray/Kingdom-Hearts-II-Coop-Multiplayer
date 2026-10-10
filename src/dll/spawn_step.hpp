#pragma once
// Spawn step: after a room load our Sora steps a little aside, the host's to one side and the
// friend's to the other, so the two Soras don't appear on one spot and slide apart (see spawn_step.cpp).

#include <cstdint>

namespace kh2coop {

void SpawnStepInit(bool host);                     // from AvatarLinkInit, once the role is known
void SpawnStepFrame();                             // from OnFrame: notices the end of a room load
void SpawnStepBeforeUpdate(std::uintptr_t actor);  // from the per-entity update hook, before the game's update

}  // namespace kh2coop
