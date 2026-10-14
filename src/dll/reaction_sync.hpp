#pragma once
// Reaction commands for one player at a time, with one brain (TODO 1.62, see reaction_sync.cpp).

#include <cstdint>

namespace kh2coop {

void ReactionSyncInit(bool host);                 // from WorldSyncInit, once the role is known
void ReactionSyncFrame();                         // from OnFrame while co-op is on (game thread)
void ReactionSyncOnPacket(const char* buf, int n);  // a "KH2C" packet from the other PC
// Script call thunk (world_census.cpp), before the game's function: true = held back (offered to the friend).
bool ReactionSyncHolds(int table, int index, const std::uint64_t* args);
bool ReactionSyncBusy(std::uintptr_t boss);       // host: this boss offered a reaction command in the last second
// Friend: this field object, bound to the host's, runs its own brain now (our press of its reaction command).
bool ReactionSyncOwnBrain(std::uintptr_t actor);
void ReactionSyncOnScene(const char* name);       // a cutscene starts (cutscene_twin.cpp), name as in the scene log

}  // namespace kh2coop
