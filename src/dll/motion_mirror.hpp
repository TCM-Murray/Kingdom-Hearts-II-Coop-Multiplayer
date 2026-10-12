#pragma once
// Limits and reaction commands on the other player's screen (TODO 1.31, see motion_mirror.cpp).
// KH2COOP_MIRROR_MOTIONS=0 on both PCs turns it off (the copy stands idle during them, as before).

#include <cstdint>

namespace kh2coop {

void MotionMirrorInit(bool host);                // from AvatarLinkInit, once the role is known
void MotionMirrorOnPacket(const char* buf, int n);  // a "KH2M" packet from the peer
void MotionMirrorFrame();                        // from OnFrame (game thread)
// The copy `actor` plays a motion from another file the peer's game sent: true = leave its motion alone this
// frame. peerOtherBank: the peer's pose for it says "a motion from another file".
bool MotionMirrorKeeps(std::uintptr_t actor, bool peerOtherBank);

}  // namespace kh2coop
