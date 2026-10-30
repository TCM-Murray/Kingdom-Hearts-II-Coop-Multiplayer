#pragma once
// Party members' Auto Limit unequipped at each save load (TODO 1.139, see auto_limit.cpp).
// AUTO_LIMIT=0 in kh2coop_player.ini turns it on; 1 (the default) leaves the game's own.

namespace kh2coop {

void AutoLimitInit();   // from OnInit, after the settings files are read
void AutoLimitFrame();  // from OnFrame, every frame (it checks co-op itself)

}  // namespace kh2coop
