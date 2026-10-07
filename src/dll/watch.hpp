#pragma once
// Write watch: logs every instruction that writes a chosen field of an enemy
// actor, using a CPU debug register. Off unless KH2COOP_WATCH=<hex offset>.

#include <cstdint>

namespace kh2coop {

void WatchInit();                                // from OnInit
void WatchOnEnemyUpdate(std::uintptr_t actor);   // from the entity update hook, team-2 actors, game thread
void WatchFrame();                               // from OnFrame (game thread)

}  // namespace kh2coop
