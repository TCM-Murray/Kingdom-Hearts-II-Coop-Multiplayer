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
// A room change was asked for (by us or, where RequestTransition is hooked, by the game) and its load hasn't
// started yet. A second request, or the soft reset, now would crash the game.
bool FollowRoomChangeStarting();
// Leaving to the title screen (link_watch.cpp): true = no room change may start (ours or the game's own).
void FollowHoldRoomChanges(bool hold);
// KH2COOP_DEBUG test (tools/debug_cmd.py double): two requests for our room in one frame.
void FollowDebugDoubleRequest();
// KH2COOP_DEBUG test (tools/debug_cmd.py warp, TODO 2.2): load this room with these programs (0xFFFF = the save's)
// once our game is safe. Meant for the host; the friend's game follows.
void FollowDebugWarp(std::uint8_t world, std::uint8_t room, std::uint8_t door, std::uint16_t map, std::uint16_t btl,
                     std::uint16_t evt);
int FollowLoadNow(std::uint8_t world, std::uint8_t room, std::uint8_t door, const std::uint16_t programs[3],
                  const char** why);

}  // namespace kh2coop
