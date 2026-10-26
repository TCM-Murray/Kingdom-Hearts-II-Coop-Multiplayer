#pragma once
// The cutscene system's work block and its 2 MB pool served from buffers of ours, so an event played mid-room
// (a summon's entry, a boss scene) never lowers the file heap's end (the review's 5.2.6 step 4; see event_block.cpp).

#include <cstdint>

namespace kh2coop {

void EventBlockInit();  // from AvatarLinkInit

}  // namespace kh2coop
