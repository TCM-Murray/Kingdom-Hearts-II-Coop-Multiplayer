#pragma once
// World mirror experiment, one brain (the host's): TODO 1.55 step 3, off unless KH2COOP_ONE_BRAIN=1 on both PCs.
// See world_mirror.cpp.

#include <cstdint>

namespace kh2coop {

bool WorldMirrorOn();                                     // KH2COOP_ONE_BRAIN=1 (known from OnInit)
// One brain for this enemy type: on, and its model name (e.g. "M_EX020") is in KH2COOP_ONE_BRAIN_TYPES (or that
// list is empty: every type). The rollout goes type by type (TODO 1.55).
bool WorldMirrorTypeListed(const char* model);
void WorldMirrorInit(bool host);                          // from WorldSyncInit, once the role is known
bool WorldMirrorWants(int table, int index);              // a script call the host sends (cheap check)
// Host: a script call in the set above just ran (arguments as the script passed them, result if it returns one).
void WorldMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result);
void WorldMirrorOnPacket(const char* buf, int n);         // a "KH2V" packet from the host (friend side)
void WorldMirrorFrame();                                  // from WorldSyncFrame (game thread)

}  // namespace kh2coop
