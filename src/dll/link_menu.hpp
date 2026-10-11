#pragma once
// "Connection lost" screen (TODO 6.8, see link_menu.cpp): the game's pause menu with "Continue Alone" and
// "Title Screen", open while the other game is gone. Driven by link_watch.cpp.

namespace kh2coop {

void LinkMenuInit(bool host);      // from LinkWatchInit
void LinkMenuFrame(bool wanted);   // each frame from LinkWatchFrame: open the screen while wanted, close it when not
bool LinkMenuUp();                 // our screen is open (pause sync leaves it alone)
int LinkMenuTakeChoice();          // 2 Title Screen / 3 Continue Alone, once the menu has closed; else 0

}  // namespace kh2coop
