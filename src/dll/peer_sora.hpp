#pragma once
// The other player's Sora as a table (TODO 3.16, 3.4, 5.20a; see peer_sora.cpp).

#include <cstdint>

namespace kh2coop {

constexpr int kSoraRecordSize = 0x114;  // a character record in the save (L121)

// From world_sync's frame (co-op on): sends our Sora's record when it changes (no menu open) and every 2 s.
void PeerSoraFrame();
// A "KH2p" packet: the other player's Sora record.
void PeerSoraOnPacket(const char* buf, int n);
// The other Sora's record as it came (kSoraRecordSize bytes), or null while none came. It comes from the other PC:
// check what you take from it.
const std::uint8_t* PeerSoraRecord();
// Its Keyblade item (record +0), 0 while unknown.
std::uint16_t PeerSoraKeyblade();

}  // namespace kh2coop
