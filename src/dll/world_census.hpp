#pragma once
// World mirror research (TODO 1.55): actor census and script system call log, logging only, off by
// default (KH2COOP_CENSUS=1, KH2COOP_TRAP_LOG=1; see world_census.cpp).

namespace kh2coop {

void WorldCensusInit();   // from OnInit, after the settings are loaded
void WorldCensusFrame();  // from OnFrame while co-op is on (game thread)
// The game's own function for script call table/index (ours sits in the table while the log is on).
void* WorldCensusTrapReal(int table, int index);

}  // namespace kh2coop
