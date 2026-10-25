#pragma once
// Connection watcher (TODO 6.1 / 6.8, see link_watch.cpp): notices when the other player's game stops
// sending (closed, crashed, network gone) and when it is back.

namespace kh2coop {

void LinkWatchInit(bool host);  // from AvatarLinkInit, once the role is known
void LinkWatchFrame();          // from OnFrame, after AvatarLinkFrame
bool LinkLost();                // the other game was connected and has gone quiet (also while playing on with Donald/Goofy)
// The player's choice while the other game is gone: 1 wait, 2 leave to the title screen, 3 play on with
// Donald and Goofy; on the friend's 'different save' screen (story_sync.cpp): 5 continue with the host's story,
// 6 leave to the title screen to load another save. From link_menu.cpp's screens or the debug command
// (tools/debug_cmd.py link).
void LinkWatchChoose(int choice);

}  // namespace kh2coop
