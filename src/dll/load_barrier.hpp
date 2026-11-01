#pragma once
// Load barrier (TODO 5.10): both games leave a room load's black screen together (see load_barrier.cpp).

namespace kh2coop {

void LoadBarrierInit(bool host);                 // from AvatarLinkInit, once the role is known
void LoadBarrierFrame();                         // from OnFrame (game thread)
void LoadBarrierOnPacket(const char* buf, int n);  // "KH2L" packets from the other game
// The load both games share for the room we're loading or in: the host's load number with the world and room
// (the friend's is the host's load it follows), 0 if this load isn't one the host announced (spawn_sync.cpp's picks).
unsigned LoadBarrierLoadKey();

}  // namespace kh2coop
