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
bool LinkMenuOn();                 // the message lookup hook is in (gummi_menu.cpp answers its id through it)
void LinkMenuLine(const char* text);  // the line under the title of a game list opened from our copy (game_over.cpp)
void LinkMenuVoteLineTick();  // each frame from inside the pause-type menu's update: the vote line's place and size
void LinkMenuPauseContinue();      // inside a pause row's handler only: the pause's own Continue step (game_over.cpp)
// Once the menu has closed: 2 Title Screen / 3 Continue Alone ('Connection lost'), 5 Continue Playing / 6 Load
// ('different save'); else 0
int LinkMenuTakeChoice();
void LinkMenuEncode(const char* s, std::uint8_t* out, std::size_t size);  // ASCII -> the game's text (sys.bar, us)
// The menu lab (TODO 1.286, bench only): 'menu state | trace on | trace off | start | wait | drop' from DevCommand.
void LinkMenuDebug(const char* args, void (*line)(const char* text, void* ctx), void* ctx);

// The shared Start menu (TODO 1.286, pause_sync.cpp). Screen 4 is PAUSE with 'line' and 'Back to Menu'.
bool LinkMenuQueueBack(const char* line);  // screen 4 queued under our closing Start menu; false = not tried
bool LinkMenuTakeBack();                   // once: 'Back to Menu' was picked on screen 4
bool LinkMenuBackUp();                     // screen 4 is open or coming (queued, launching)
bool LinkMenuBackOpen();                   // screen 4's list is the open menu
const char* LinkMenuStartRefusal(bool* hold);  // nullptr = the Start menu may open now (see link_menu.cpp)
void LinkMenuOpenStart();                  // exe+0x2E4950(0, -1), after LinkMenuStartRefusal said yes

}  // namespace kh2coop
