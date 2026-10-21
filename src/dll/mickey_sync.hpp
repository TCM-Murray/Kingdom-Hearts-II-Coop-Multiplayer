#pragma once
// Mickey's rescue in co-op (TODO 1.3, DECISIONS Q6; see mickey_sync.cpp). On unless MICKEY_SYNC=0.

#include <cstdint>

namespace kh2coop {

void MickeySyncInit(bool host);                   // from AvatarLinkInit
void MickeySyncOnPacket(const char* buf, int n);  // a "KH2Z" packet from the peer
void MickeySyncFrame();                           // from OnFrame (game thread)
// While our Mickey is out: our own Sora (lying), for the pose we send instead of Mickey; else 0.
std::uintptr_t MickeyRescueSora();

}  // namespace kh2coop
