#pragma once
// The other Sora's copy on its own stat sheet (TODO 3.16, 3.4; see copy_sheet.cpp).

namespace kh2coop {

// From PuppetInit: the level-up hook (our Sora's level-up rebuilds our sheet, not the copy's).
void CopySheetInit();
// From world_sync's frame (co-op on): gives a new copy its own sheet, rebuilds it when its record changes, keeps its
// HP the other player's.
void CopySheetFrame();

}  // namespace kh2coop
