#pragma once
// Cutscene skips follow the host (TODO 5.3, see skip_sync.cpp). SKIP_SYNC=0 in kh2coop.ini lets each
// player skip on their own.

namespace kh2coop {

void SkipSyncInit(bool host);                 // from AvatarLinkInit, once the role is known
void SkipSyncOnPacket(const char* buf, int n);  // a "KH2K" packet from the peer
void SkipSyncFrame();                         // from OnFrame (game thread)

}  // namespace kh2coop
