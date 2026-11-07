#pragma once
// The Gummi menu's help bar (TODO 5.2, route 2: our text in the Gummi menus' own help bar; see gummi_menu.cpp).

#include <cstdint>

namespace kh2coop {

void GummiMenuInit();   // from AvatarLinkInit, after LinkWatchInit (the message lookup hook)
void GummiMenuFrame();  // from OnFrame (game thread)
// Our text for message id 0x7F42 (from link_menu.cpp's message lookup hook), nullptr = none set.
const std::uint8_t* GummiMenuText();

}  // namespace kh2coop
