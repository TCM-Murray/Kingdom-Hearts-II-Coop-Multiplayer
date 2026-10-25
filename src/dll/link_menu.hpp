#pragma once
// "Connection lost" screen (TODO 6.8, see link_menu.cpp): the game's pause menu with "Continue Alone" and
// "Title Screen", open while the other game is gone; and the friend's "different save" screen (story_sync.cpp)
// with "Continue Playing" and "Load". Driven by link_watch.cpp.

#include <cstddef>
#include <cstdint>

namespace kh2coop {

void LinkMenuInit(bool host);      // from LinkWatchInit
// Each frame from LinkWatchFrame: the screen wanted (0 none, 1 'Connection lost', 2 'different save' with its line
// under the title); opens it, closes it when no longer wanted (or another one is).
void LinkMenuFrame(int screen, const char* line);
bool LinkMenuUp();                 // our screen is open (pause sync leaves it alone)
// Once the menu has closed: 2 Title Screen / 3 Continue Alone ('Connection lost'), 5 Continue Playing / 6 Load
// ('different save'); else 0
int LinkMenuTakeChoice();
void LinkMenuEncode(const char* s, std::uint8_t* out, std::size_t size);  // ASCII -> the game's text (sys.bar, us)

}  // namespace kh2coop
