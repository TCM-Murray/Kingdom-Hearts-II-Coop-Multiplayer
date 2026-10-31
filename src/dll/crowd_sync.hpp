#pragma once
// The exe's crowd system (the 1000 Heartless) the same in both games (TODO 1.149, see crowd_sync.cpp).
// CROWD_SEED=0 leaves each game's own layout; CROWD_SYNC=0 leaves each game's crowd its own.

#include <cstdint>

namespace kh2coop {

void CrowdSyncInit();   // from OnInit
void CrowdSyncFrame();  // from OnFrame while co-op is on (the counts' log)
// From aggro.cpp's crowd hooks (the member runner exe+0x411EA0 and the controller run exe+0x41F830).
// Friend: true = the member was handled from the host's data (result: 1 kept, 0 gone); false = run it.
bool CrowdSyncFriendMember(std::uintptr_t member, char& result);
void CrowdSyncHostMember(std::uintptr_t member, char result);  // host: after the game ran it
void CrowdSyncHostBeforeRun();                                  // host: the friend's knock-aways (step 4)
void CrowdSyncHostAfterRun();                                   // host: after the controller ran its members
void CrowdSyncOnPacket(const char* buf, int n);                 // "KH2r", from avatar_link.cpp
void CrowdSyncOnKnock(const char* buf, int n);                  // "KH2k", from avatar_link.cpp

}  // namespace kh2coop
