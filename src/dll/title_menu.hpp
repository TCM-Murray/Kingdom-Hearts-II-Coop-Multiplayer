#pragma once
// "LOAD (CO-OP)" on the title screen (TODO 5.7, see title_menu.cpp).
// TITLE_COOP=0 turns it off (the normal 3-row title, co-op always on).

namespace kh2coop {

void TitleMenuInit();  // from OnInit (works without a peer: the row shows whenever the mod is on)

// True when the save being loaded from the title was picked through LOAD (CO-OP),
// false for a normal LOAD (latched when the save is picked).
bool TitleCoopLoad();

}  // namespace kh2coop
