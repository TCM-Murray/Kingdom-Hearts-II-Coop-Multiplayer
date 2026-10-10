#pragma once
// Avatar link: each game copy streams its own player's state to a peer and
// receives the peer's, over UDP. Slice 3a only logs what arrives; later slices
// drive a puppet from it.
//
// Off unless both are set when the game starts:
//   KH2COOP_LISTEN=<port>         UDP port this copy receives on
//   KH2COOP_PEER=<ipv4>:<port>    where this copy sends its avatar

#include <cstdint>

namespace kh2coop {

void AvatarLinkInit();   // from OnInit; reads the environment, opens the socket
void AvatarLinkFrame();  // from OnFrame; non-blocking send + drain receive

// Tells the peer its player was hit in our game (our copy of them took
// `damage` HP). Sent 3 times with an id; the receiver ignores duplicates.
void AvatarLinkSendHit(int damage, int react);
// Tells the peer its player was healed by us (our Cure reached their copy, or we used an item on
// it): `percent` of their max HP, `source` 1 = Cure, 2 = item. Sent like a hit.
void AvatarLinkSendHeal(int percent, int source);

// Sends any packet to the peer (through the network simulator if it's on).
void AvatarLinkSendRaw(const void* data, int len);

// Sender clock time (peer's NowMs) that playback shows right now: newest data
// minus the playback delay, mapped to the peer's clock. 0 until data arrived.
std::uint32_t AvatarLinkPlaybackSenderMs();

// Our clock, the same one stamped into outgoing packets.
std::uint32_t AvatarLinkNowMs();

// Milliseconds since the peer's last packet arrived (0xFFFFFFFF if none yet).
std::uint32_t AvatarLinkPeerAgeMs();

// Build id both PCs must share (packets from other builds are ignored).
std::uint32_t AvatarLinkBuild();

// The peer's newest reported state (not the delayed playback pose); false if
// nothing arrived in the last 0.5 s.
struct PeerPose;
bool AvatarLinkPeerNow(PeerPose& out);

}  // namespace kh2coop
