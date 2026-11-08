#pragma once
// The Gummi menus in co-op (TODO 5.2 stage 1, see gummi_menu.cpp).

#include <cstdint>

namespace kh2coop {

void GummiMenuInit();   // from AvatarLinkInit, after LinkWatchInit (the message lookup hook)
void GummiMenuFrame();  // from OnFrame (game thread)
void GummiMenuOnPacket(const char* buf, int n);  // "KH2g"
// Our text for message id 0x7F42 (from link_menu.cpp's message lookup hook), nullptr = none set.
const std::uint8_t* GummiMenuText();
// The other player is in the Gummi menu of this route right now (a Proceed there needs no second pick).
bool GummiMenuPartnerThere(std::int32_t route);

}  // namespace kh2coop
