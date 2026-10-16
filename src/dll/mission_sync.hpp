#pragma once
// A story fight's mission values (timer, counters, gauges) are the host's; the friend's game shows them (TODO 1.22;
// see mission_sync.cpp).

#include <cstdint>

namespace kh2coop {

void MissionSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void MissionSyncFrame();                           // from WorldSyncFrame (game thread)
void MissionSyncOnPacket(const char* buf, int n);  // a "KH2N" packet from the host (friend side)
// Friend: the host's game runs the same mission in our room and its packets are fresh: its values count here.
bool MissionSyncHostOwnsHere();
// Friend, from the script call thunks (world_census.cpp): true = one of our scripts' mission value calls (set / add
// count or gauge) that is left out, because the host's values count here.
bool MissionSyncHolds(int table, int index, const std::uint64_t* args);
// A mission call this module or scene_sync.cpp takes care of (values, their getters, the endings): the host's log of
// calls not sent leaves them out.
bool MissionSyncCovers(int table, int index);

}  // namespace kh2coop
