#pragma once
// Mickey's rescue in co-op (TODO 1.3, DECISIONS Q6; see mickey_sync.cpp). On unless MICKEY_SYNC=0.

#include <cstdint>

namespace kh2coop {

void MickeySyncInit(bool host);                   // from AvatarLinkInit
void MickeySyncOnPacket(const char* buf, int n);  // a "KH2Z" packet from the peer
void MickeySyncFrame();                           // from OnFrame (game thread)
// While our Mickey is out: our own Sora (lying), for the pose we send instead of Mickey; else 0.
std::uintptr_t MickeyRescueSora();
// Our Sora getting up after our Mickey (the player again, Mickey still the actor list's head): for the pose we send.
std::uintptr_t MickeyHandBackSora();
// The loser's own Mickey standing in for the winner's (step c): moved after each of his updates (puppet.cpp's entity
// update hook); the game's own motion changes and hits on him are dropped there.
void MickeyAfterUpdate(std::uintptr_t actor);
// Our stand-in Mickey, or our Sora kept lying after his exit until the down takes over: the game's motion changes
// and hits are dropped (puppet.cpp).
bool MickeyStandIn(std::uintptr_t actor);

}  // namespace kh2coop
