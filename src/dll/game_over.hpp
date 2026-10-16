#pragma once
// Shared Game Over (see game_over.cpp). On unless GAMEOVER_SYNC=0.

namespace kh2coop {

void GameOverSyncInit(bool host);                // from AvatarLinkInit
void GameOverSyncOnPacket(const char* buf, int n);  // a "KH2G" packet from the peer
void GameOverSyncFrame();                        // from OnFrame (game thread)
// Our load screen is (or was within the last second) open over the Game Over menu: a room change asked for
// now is the save our player picked there (follow.cpp lets it through the door lock).
bool GameOverLoadScreenOpen();

}  // namespace kh2coop
