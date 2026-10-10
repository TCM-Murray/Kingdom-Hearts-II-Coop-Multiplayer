#pragma once
// The other player's Sora in story cutscenes (TODO 5.12, see cutscene_twin.cpp).
// CUTSCENE_TWIN in kh2coop_player.ini (personal, default 1) turns it off with 0.

namespace kh2coop {

void CutsceneTwinInit();   // from OnInit, after AvatarLinkInit
void CutsceneTwinFrame();  // from OnFrame (research trace only, KH2COOP_TWIN_TRACE=1)

}  // namespace kh2coop
