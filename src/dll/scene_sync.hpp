#pragma once
// Walk-in story scenes for either player (see scene_sync.cpp). On unless SCENE_SYNC=0.

namespace kh2coop {

void SceneSyncInit(bool host);                 // from AvatarLinkInit, after WorldSyncInit (spawn sync checks bytes)
void SceneSyncOnPacket(const char* buf, int n);  // a "KH2T" packet from the peer
void SceneSyncFrame();                         // from OnFrame (game thread)

}  // namespace kh2coop
