#pragma once
// Load barrier (TODO 5.10, research stage): hold a finished room load on the black screen (see load_barrier.cpp).

namespace kh2coop {

void LoadBarrierInit(bool host);  // from AvatarLinkInit, once the role is known

}  // namespace kh2coop
