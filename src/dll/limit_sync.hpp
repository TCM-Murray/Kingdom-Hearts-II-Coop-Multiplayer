#pragma once
// A Limit in either game sends the allies away in both games, except its partner (docs/DECISIONS.md L1; see
// limit_sync.cpp).

#include <cstdint>

namespace kh2coop {

void LimitSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void LimitSyncFrame();                           // from WorldSyncFrame (game thread)
void LimitSyncOnPacket(const char* buf, int n);  // a "KH2I" packet from the other PC
// A Limit runs in our game or the other player's (puppet.cpp: the Sora copy is no party member then).
bool LimitSyncAnyLimit();
// The other player's Limit runs in this room: our Limit commands are refused meanwhile (DECISIONS L2).
bool LimitSyncPeerLimit();
// The other player's Limit keeps our member with this object id away (every member but its partners).
bool LimitSyncPeerHolds(std::uint32_t objId);

}  // namespace kh2coop
