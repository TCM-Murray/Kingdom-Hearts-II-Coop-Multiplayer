#pragma once
// The other player's Sora as its own row, "Sora (P1)" / "Sora (P2)", in the item and Cure target list (TODO 3.17,
// docs/DECISIONS.md). On with the Sora copy (KH2COOP_PUPPET).

#include <cstdint>

namespace kh2coop {

void TargetRowInit();                           // from PuppetInit, once the copy is on
void TargetRowAfterBuild(std::uintptr_t menu);  // mickey_sync.cpp's command menu build hook, after the game's build
const std::uint8_t* TargetRowText(int id);      // link_menu.cpp's message hook: our row's text for its id, else null

}  // namespace kh2coop
