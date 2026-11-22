#pragma once
// The other Sora's copy on its own stat sheet (TODO 3.16, 3.4; see copy_sheet.cpp).

#include <cstddef>
#include <cstdint>

namespace kh2coop {

// From PuppetInit: the level-up hook (our Sora's level-up rebuilds our sheet, not the copy's).
void CopySheetInit();
// From world_sync's frame (co-op on): gives a new copy its own sheet, rebuilds it when its record changes, keeps its
// HP the other player's.
void CopySheetFrame();
// The copy's sheet as last built (0x278 bytes into `out`), for the pause menu's box of the other Sora (menu_sora.cpp):
// its +0x250 leads to the other player's record, so the level is theirs. False until a sheet was built, with copy
// sheets off, or when that handle no longer leads back to the record.
bool CopySheetSnapshot(std::uint8_t* out, std::size_t size);

}  // namespace kh2coop
