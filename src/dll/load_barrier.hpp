#pragma once
// Load barrier (TODO 5.10): both games leave a room load's black screen together (see load_barrier.cpp).

namespace kh2coop {

void LoadBarrierInit(bool host);                 // from AvatarLinkInit, once the role is known
void LoadBarrierFrame();                         // from OnFrame (game thread)
void LoadBarrierOnPacket(const char* buf, int n);  // "KH2L" packets from the other game

}  // namespace kh2coop
