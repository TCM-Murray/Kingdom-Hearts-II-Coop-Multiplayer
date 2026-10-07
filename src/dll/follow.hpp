#pragma once
// Room following: the friend's game loads the host's room (see follow.cpp).

#include <cstdint>

namespace kh2coop {

void FollowInit(bool host);  // from AvatarLinkInit, once the role is known
// programs = the host's loaded map/btl/evt programs; each newest packet
void FollowOnHostLocation(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                          bool hasActor);
void FollowFrame();          // from OnFrame (game thread)

}  // namespace kh2coop
