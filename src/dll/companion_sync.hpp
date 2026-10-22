#pragma once
// A Drive Form or summon in either game sends the same companions away in both, and they come back when nobody holds
// them any more (DECISIONS C1, C2; see companion_sync.cpp). Limits: limit_sync.cpp.

#include <cstdint>

namespace kh2coop {

void CompanionSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void CompanionSyncFrame();                           // from WorldSyncFrame (game thread)
void CompanionSyncOnPacket(const char* buf, int n);  // a "KH2O" or "KH2S" packet from the other PC
// Friend, the HP hooks: true = our copy of a host's companion, whose HP and MP are the host's (drop gains here).
bool CompanionSyncHostOwns(std::uintptr_t actor);
// Friend: a Cure or item from us (HP stat 0, MP stat 32) on such a copy goes to the host's game.
void CompanionSyncForwardGain(std::uintptr_t actor, int delta, int stat, int react);

}  // namespace kh2coop
