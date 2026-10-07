#pragma once
// Shared combat pause (see pause_sync.cpp). Off unless PAUSE_SYNC=1.

namespace kh2coop {

void PauseSyncInit();                          // from AvatarLinkInit
void PauseSyncOnPacket(const char* buf, int n);  // a "KH2P" packet from the peer
void PauseSyncFrame();
void PauseSyncOnPeerRestart();                 // the other game restarted                         // from OnFrame (game thread)

}  // namespace kh2coop
