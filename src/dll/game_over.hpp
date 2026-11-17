#pragma once
// Shared Game Over (see game_over.cpp). On unless GAMEOVER_SYNC=0.

#include <cstdint>

namespace kh2coop {

void GameOverSyncInit(bool host);                // from AvatarLinkInit
void GameOverSyncOnPacket(const char* buf, int n);  // a "KH2G" packet from the peer
void GameOverSyncFrame();                        // from OnFrame (game thread)
void GameOverSyncOnPeerRestart();                // the other game restarted: its choices count from 0 again
// Our load screen is (or was within the last second) open over the Game Over menu: a room change asked for
// now is the save our player picked there (follow.cpp lets it through the door lock).
bool GameOverLoadScreenOpen();
// A mission's Retry / Quit Game Over needs both players' pick (TODO 1.228).
bool GameOverSyncBothPick();  // such a Game Over may be opening now: its menu gets our line (link_menu.cpp)
// From the pause-type menu's list input (hooked in gummi_flight.cpp): `row` is what the game read; the answer is
// the row to run, or -1 to hold our player's pick until the other player's is the same.
int GameOverSyncListInput(std::uintptr_t list, int row);

}  // namespace kh2coop
