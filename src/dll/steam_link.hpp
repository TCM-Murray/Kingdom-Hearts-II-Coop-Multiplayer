#pragma once
// Steam link (TODO 6.7): the avatar link's packets over Steam instead of UDP,
// for playing with a Steam friend without Radmin VPN or IP addresses.
//
// Uses the game's own Steam session (the game's steam_api64.dll, flat API
// exports only; the game starts Steam itself): Steam networking messages,
// unreliable like our UDP, routed by Steam (Valve's relays when needed).
// The host's game shows as joinable in the Steam friends list while co-op is
// on; the friend uses Join Game (or accepts Invite to Game) from the Steam
// overlay and connects. Any Steam friend may join, the first one only (user
// picks, session 25). Settings (kh2coop.ini):
//   PEER=steam                 wait: host = for a friend to join; friend = for Join Game
//   PEER=steam:<SteamID64>     send to that Steam account right away (typed ID)
//   ROLE=host                  the host (default with PEER=steam: friend)

#include <cstdint>

namespace kh2coop {

void SteamLinkInit(bool host, std::uint64_t peer);  // from AvatarLinkInit; peer 0 = wait
void SteamLinkFrame(bool coopOn);  // every frame: starts once the game's Steam is up, runs Steam's callbacks
bool SteamLinkHasPeer();
void SteamLinkSend(const void* data, int len);
int SteamLinkReceive(char* buf, int size);  // next message from the peer, 0 when none left

}  // namespace kh2coop
