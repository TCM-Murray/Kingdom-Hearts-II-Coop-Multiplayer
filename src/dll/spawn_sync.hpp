#pragma once
// Shared spawning (see spawn_sync.cpp). On with WORLD_SYNC=1 unless SPAWN_SYNC=0.

namespace kh2coop {

void SpawnSyncInit(bool host);  // from WorldSyncInit when world sync is on
void SpawnSyncFrame();          // from OnFrame (game thread): periodic log

}  // namespace kh2coop
