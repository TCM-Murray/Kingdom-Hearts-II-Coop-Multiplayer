#pragma once
// World mirror, one brain (the host's) for every enemy: TODO 1.55 step 3, with world sync. See world_mirror.cpp.

#include <cstdint>

namespace kh2coop {

void WorldMirrorInit(bool host);                          // from WorldSyncInit, once the role is known
bool WorldMirrorWants(int table, int index);              // a script call the host sends (cheap check)
// Friend: a prize call by our bound enemy's own script, skipped (the host's prizes for it come here).
bool WorldMirrorHolds(int table, int index, const std::uint64_t* args);
// Host: a script call in the set above just ran (arguments as the script passed them, result if it returns one).
void WorldMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result);
void WorldMirrorOnPacket(const char* buf, int n);         // a "KH2V" packet from the host (friend side)
void WorldMirrorOnStatePacket(const char* buf, int n);    // a "KH2Y" packet: the host's effect and rotation states
// The field an effect script call sets (38 trap_effect_set_pos: its place; 41 set_dir: its rotation, set_dir's
// result), 4 floats, through the game's own getter; null if none. `effect` must be a live effect.
float* WorldMirrorFxField(int index, std::uintptr_t effect);
void WorldMirrorFrame();                                  // from WorldSyncFrame (game thread)

}  // namespace kh2coop
