#pragma once
// Limits and reaction commands on the other player's screen (TODO 1.31, see motion_mirror.cpp).
// KH2COOP_MIRROR_MOTIONS=0 on both PCs turns it off (the copy stands idle during them, as before).

#include <cstdint>

namespace kh2coop {

void MotionMirrorInit(bool host);                // from AvatarLinkInit, once the role is known
void MotionMirrorOnPacket(const char* buf, int n);  // a "KH2M" packet from the peer
void MotionMirrorFrame();                        // from OnFrame (game thread)
// The copy `actor` plays a motion from another file the peer's game sent, or the cast motion of the peer's spell we
// cast on it: true = leave its motion alone this frame. peerOtherBank / peerMotion: the peer's pose for it ("a
// motion from another file" / its motion id).
bool MotionMirrorKeeps(std::uintptr_t actor, bool peerOtherBank, int peerMotion);
// The copy's spell script (a spell the peer cast, cast again on our copy) is running now: its HP changes are dropped.
bool MotionMirrorCopySpellRunning();
// The copy's cast trigger reached `record` (its +0xDA0): true once for the spell we cast on it, while it runs.
bool MotionMirrorCastTrigger(std::uintptr_t actor, std::uintptr_t record);
bool MotionMirrorCasting(std::uintptr_t actor);  // we're casting the peer's spell on `actor` right now
void MotionMirrorDebugCast(int slot);  // test command: our Sora casts the spell in slot 0-5 (no MP, no menu)

}  // namespace kh2coop
