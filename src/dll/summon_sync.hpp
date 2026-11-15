#pragma once
// The other player's summon as an actor in our game (TODO 1.61, see summon_sync.cpp).

#include <cstdint>

namespace kh2coop {

void SummonSyncInit();       // from PuppetInit: the brain hook, when KH2COOP_SUMMON_SYNC=1
void SummonSyncSpawnStep();  // from spawn_sync.cpp's SpawnTask hook (game thread): builds and removes
void SummonSyncOnActorFree(std::uintptr_t actor);  // from form_look.cpp's actor free hook: the game frees this actor
void SummonSyncAfterUpdate(std::uintptr_t actor);  // from puppet.cpp's per-entity hook, after the actor's update
// The other player's summon stands here as an actor of ours: companion_sync.cpp keeps their companions' places held
// until it is freed (capped 12 s into its removal).
bool SummonSyncOut();
// The script call thunk (world_census.cpp): true = not run (our leaving summon's call on another actor).
bool SummonSyncHolds(int table, int index, const std::uint64_t* args, int argc);
// The motion hook (puppet.cpp): true = only our own motion changes go through (the summon following theirs).
bool SummonSyncBlocksMotion(std::uintptr_t actor);
void SummonSyncDebug(int objId);  // test command 27 'summon <object id>': build it by the copy; 0 removes it

}  // namespace kh2coop
