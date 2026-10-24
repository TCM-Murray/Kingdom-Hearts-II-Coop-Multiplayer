#pragma once
// Shared spawning (see spawn_sync.cpp). On with WORLD_SYNC unless SPAWN_SYNC=0.

namespace kh2coop {

void SpawnSyncInit(bool host);  // from WorldSyncInit when world sync is on
void SpawnSyncFrame();          // from OnFrame (game thread): periodic log
bool SpawnSyncFactoryOk();     // the actor factory's bytes matched at start (stand_in.cpp)
void SpawnSyncDebugBody(int objId);  // test command 'body': make this object beside the copy (+0x10000: set back the
                                     // player pointers it takes, +0x20000: as a cutscene actor, +0x40000: beside our Sora), log what it takes
void SpawnSyncDebugSpawn(int objId, int count);  // test command 'spawn': load its files, then make `count` in front of
                                                 // our Sora (TODO 2.5)

}  // namespace kh2coop
