#pragma once
// Connection watcher (TODO 6.1 / 6.8, see link_watch.cpp): notices when the other player's game stops
// sending (closed, crashed, network gone) and when it is back.

namespace kh2coop {

void LinkWatchInit(bool host);  // from AvatarLinkInit, once the role is known
void LinkWatchFrame();          // from OnFrame, after AvatarLinkFrame
bool LinkLost();                // the other game was connected and has gone quiet (also while playing on with Donald/Goofy)
// The player's choice while the other game is gone: 1 wait, 2 leave to the title screen, 3 play on with
// Donald and Goofy. From the 'Connection lost' screen (link_menu.cpp, 6.8) or the debug command
// (tools/debug_cmd.py link).
void LinkWatchChoose(int choice);

}  // namespace kh2coop
