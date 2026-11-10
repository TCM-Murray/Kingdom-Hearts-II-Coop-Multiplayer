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
// Our holder of the other player's Limit's effects while it's filled, else 0 (world_census.cpp's effect counter).
std::uintptr_t MotionMirrorLimitFx();
// Before the game frees file group `group`, or lays the heap out (-1 before its place pass, -2 after it, before the
// moves): our holder of the other player's Limit ends while its file is in place if the change reaches that file.
// From copy_blade.cpp's file hooks; game thread.
void MotionMirrorFileChange(int group);
// The Limit script's effect calls (starts 2/57-59, ends 0/44, 0/51, 0/52, 0/85; KH2COOP_LIMIT_FX_SCRIPT=1): wanted, and
// each one after it ran, from world_census.cpp's script call thunks (game thread).
bool MotionMirrorWantsTrap(int table, int index);
void MotionMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result);
// The copy's spell script (a spell the peer cast, cast again on our copy) is running now: its HP changes are dropped.
bool MotionMirrorCopySpellRunning();
// The copy's cast trigger reached `record` (its +0xDA0): true once for the spell we cast on it, while it runs.
bool MotionMirrorCastTrigger(std::uintptr_t actor, std::uintptr_t record);
bool MotionMirrorCasting(std::uintptr_t actor);  // we're casting the peer's spell on `actor` right now
// While on, our motions aren't sent: grab_sync.cpp runs the host's grab on our Sora, and sending its grab motion back
// put the host's copy into a second grab motion, which ended the grab there (bench 2026-10-14 01:25:14).
void MotionMirrorMute(bool on);
void MotionMirrorDebugCast(int slot);  // test command: our Sora casts the spell in slot 0-5 (no MP, no menu)
void MotionMirrorDebugDeadOwner();     // test command: the dead-owner attack guard (TODO 1.132)

}  // namespace kh2coop
