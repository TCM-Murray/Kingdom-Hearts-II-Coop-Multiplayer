#pragma once
// Shared spawning (see spawn_sync.cpp). On with WORLD_SYNC unless SPAWN_SYNC=0.

#include <cstdint>

namespace kh2coop {

void SpawnSyncInit(bool host);  // from WorldSyncInit when world sync is on
std::uint32_t* SpawnSyncRandom();  // the game's random number (the Gummi flight rolls enemy dice with it)
void SpawnSyncFrame();          // from OnFrame (game thread): periodic log
bool SpawnSyncFactoryOk();     // the actor factory's bytes matched at start (stand_in.cpp)
// The living actor of this spawn serial by the game's own search of its actor list (exe+0x3B41E0), or 0 (none now,
// serial 0, or spawn sync is off). Game thread.
std::uintptr_t SpawnSyncActorBySerial(std::uint16_t serial);
// The game's actor factory (game thread; its files must be loaded): the actor, or 0 (nothing made, or its bytes
// didn't match at start).
std::uintptr_t SpawnSyncMake(std::uint32_t objId, const float* pos, float yaw);
void SpawnSyncDebugBody(int objId);  // test command 'body': make this object beside the copy (+0x10000: set back the
                                     // player pointers it takes, +0x20000: as a cutscene actor, +0x40000: beside our Sora), log what it takes
void SpawnSyncDebugSpawn(int objId, int count);  // test command 'spawn': load its files, then make `count` in front of
                                                 // our Sora (TODO 2.5)
void SpawnSyncDebugBattle(bool on);  // test command 'battle': hold the game's forced battle state (Limits with no
                                     // enemies; benches only) or hand it back

}  // namespace kh2coop
