#pragma once
// The host's story progress counts in the friend's game (TODO 5.1; see story_sync.cpp). STORY_SYNC=0 turns it off.

namespace kh2coop {

void StorySyncInit(bool host);                   // from AvatarLinkInit, once the role is known
void StorySyncFrame();                           // from OnFrame (game thread)
void StorySyncOnPacket(const char* buf, int n);  // a "KH2U" packet: the host's block, or a chest the friend opened
void StorySyncDebugChest(int bit);  // test command 'chest' (friend): open that chest through the game's chest take
void StorySyncDebugOpen(int bit);   // test command 'openchest' (both): 'Open' on our chest object of that chest bit
// Friend: our save has story the host's lacks, and the copy waits for the player's pick on the 'different save'
// screen (link_watch.cpp / link_menu.cpp); its line under the title (ASCII).
bool StorySyncSaveDiffers();
const char* StorySyncScreenLine();
void StorySyncChoose(bool hostStory);  // the pick: true Continue Playing (the host's story), false Load
void StorySyncOnLeave();               // our game leaves to the title screen: a pick not used yet is dropped

}  // namespace kh2coop
