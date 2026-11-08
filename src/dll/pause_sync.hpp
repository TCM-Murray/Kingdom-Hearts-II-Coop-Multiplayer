#pragma once
// Shared combat pause (see pause_sync.cpp). Off unless PAUSE_SYNC=1.

#include <cstdint>

namespace kh2coop {

void PauseSyncInit();                          // from AvatarLinkInit
void PauseSyncOnPacket(const char* buf, int n);  // a "KH2P" packet from the peer
void PauseSyncFrame();
void PauseSyncOnPeerRestart();                 // the other game restarted                         // from OnFrame (game thread)

// The Gummi flight's pause (gummi_flight.cpp).
std::uint8_t PauseSyncOpenMenu();              // the menu byte, 0xFF = none
bool PauseSyncFollowing();                     // this pause was opened by the other player
int PauseSyncTakeClose();                      // once: 0 nothing, 1 the other player closed the pause, 2 they picked Retry
void PauseSyncRetryOnClose();                  // our player picked Retry: the close we send says so

}  // namespace kh2coop
