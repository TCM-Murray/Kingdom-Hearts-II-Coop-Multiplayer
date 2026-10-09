#pragma once
// Party HUD widget for the Sora copy (see party_hud.cpp). COPY_HUD=1 (default)
// lists the copy in the HUD; COPY_HUD_HP=1 (default) makes its bar show the
// peer's HP from their packets instead of our own Sora's.

namespace kh2coop {

void PartyHudInit();   // from OnInit, after PuppetInit
void PartyHudFrame();  // from OnFrame (game thread), after PuppetFrame

}  // namespace kh2coop
