#pragma once
// The other player's Sora is spawned beside the party in every room (the extra copy, see solo_copy.cpp).

#include <cstdint>

namespace kh2coop {

void SoloCopyInit();  // from AvatarLinkInit; the work happens in the room's spawn pass
// This room's object of that serial is a directly spawned rider's own ride (TODO 1.302): world sync leaves it out.
bool SoloCopyOwnRide(std::uint32_t objId, std::uint16_t serial);  // game thread
bool SoloCopyRideObject(std::uint32_t objId);  // this room has a ride of that object (stand_in.cpp: the copy's twin)

}  // namespace kh2coop
