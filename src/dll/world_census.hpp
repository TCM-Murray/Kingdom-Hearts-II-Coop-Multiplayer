#pragma once
// World mirror research (TODO 1.55): actor census and script system call log, logging only, off by
// default (KH2COOP_CENSUS=1, KH2COOP_TRAP_LOG=1; see world_census.cpp).

#include <cstdint>

namespace kh2coop {

void WorldCensusInit();   // from OnInit, after the settings are loaded
void WorldCensusFrame();  // from OnFrame while co-op is on (game thread)
// The game's own function for script call table/index (ours sits in the table while the log is on).
void* WorldCensusTrapReal(int table, int index);
// Script call table/index's name ("trap_..."), or "?".
const char* WorldCensusTrapName(int table, int index);
// The actor whose script is running now (inside a script call), or 0.
std::uintptr_t WorldCensusScriptActor();
// KH2COOP_HEAL_TRACE: after a companion's think exe+0x1B53B0 (companion_sync.cpp hooks it); skipped = it didn't run.
void WorldCensusThinkTrace(std::uintptr_t self, bool skipped);
// The actor whose script made `a` (an object made by a script call, e.g. trap_sysobj_appear), or 0. With one brain
// or the census on.
std::uintptr_t WorldCensusMadeBy(std::uintptr_t a);

}  // namespace kh2coop
