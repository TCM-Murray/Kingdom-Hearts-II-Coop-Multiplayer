#pragma once
// Room following: the friend's game loads the host's room (see follow.cpp).

#include <cstdint>

namespace kh2coop {

void FollowInit(bool host);  // from AvatarLinkInit, once the role is known
// programs = the host's loaded map/btl/evt programs; each newest packet
void FollowOnHostLocation(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                          bool hasActor);
void FollowFrame();          // from OnFrame (game thread)
// Load barrier, friend side: start loading the host's new room at once (see follow.cpp).
// Skip sync: the host skipped a cutscene; our skip chain's own moves may pass the door lock for a while.
void FollowHostSkipped();
// Reloads our room (same door and programs) if it can now (in the field, no menu or cutscene; a fight starts
// over); false with the reason otherwise.
bool FollowReloadRoom(const char** why);
// KH2COOP_DEBUG test (tools/debug_cmd.py double): two requests for our room in one frame.
void FollowDebugDoubleRequest();
int FollowLoadNow(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                  const char** why);

}  // namespace kh2coop
