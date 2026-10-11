#pragma once
// Forced-solo rooms keep the other player's Sora (TODO 5.5, see solo_copy.cpp). SOLO_COPY=0 in
// kh2coop.ini leaves those rooms as the game makes them (Sora alone, the other player not shown).

namespace kh2coop {

void SoloCopyInit();  // from AvatarLinkInit; the work happens in the room's spawn pass

}  // namespace kh2coop
