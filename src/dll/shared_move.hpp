#pragma once
// A boss's move that both players play at once (TODO 1.20, see shared_move.cpp): Final Xemnas's desperation move.

#include <cstdint>

namespace kh2coop {

void SharedMoveInit(bool host);                   // from WorldSyncInit, once the role is known
void SharedMoveFrame();                           // from WorldSyncFrame (game thread)
void SharedMoveOnPacket(const char* buf, int n);  // a "KH2X" packet from the other PC
// Script call thunk (world_census.cpp), before the game's function; only watches.
void SharedMoveOnCall(int table, int index, const std::uint64_t* args);
void SharedMoveOnScene(const char* name);         // a cutscene starts (cutscene_twin.cpp), name as in the scene log
// Friend: this enemy, bound to the host's, runs its own brain now (its part of the shared move).
bool SharedMoveOwnBrain(std::uintptr_t actor);

}  // namespace kh2coop
