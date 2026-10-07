#pragma once
// Shared Game Over (see game_over.cpp). On unless GAMEOVER_SYNC=0.

namespace kh2coop {

void GameOverSyncInit(bool host);                // from AvatarLinkInit
void GameOverSyncOnPacket(const char* buf, int n);  // a "KH2G" packet from the peer
void GameOverSyncFrame();                        // from OnFrame (game thread)

}  // namespace kh2coop
