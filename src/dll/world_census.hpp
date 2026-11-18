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
std::uintptr_t WorldCensusScriptThread();  // the script thread running now (exe+0x2A250B0), or 0
// Inside a script call: ends the running thread's run at its next instruction (an exit), if it is `owner`'s one-shot
// thread on this game thread's C stack (a motion trigger's, exe+0x3E1840, L337); never a root or a lasting heap
// thread. Else false and `why` says which check refused (TODO 1.197).
bool WorldCensusEndScriptThread(std::uintptr_t owner, const char** why);
// From the crash logger: logs the script calls running on this thread now (name, arguments, whose script), if any.
void WorldCensusLogCrashCall();
// Test command (endedloop, TODO 5.2d): the guard on 0/44 and 0/85 for an effect already gone; logs PASS or FAIL.
void WorldCensusDebugEndedLoop();
// KH2COOP_HEAL_TRACE: after a companion's think exe+0x1B53B0 (companion_sync.cpp hooks it); skipped = it didn't run.
void WorldCensusThinkTrace(std::uintptr_t self, bool skipped);
// The actor whose script made `a` (an object made by a script call, e.g. trap_sysobj_appear), or 0. With one brain
// or the census on.
std::uintptr_t WorldCensusMadeBy(std::uintptr_t a);
// Actor inspector (TODO 2.4, avatar_link.cpp's "KH2v" command): line(text, ctx) for every actor in the active list
// (only = 0) or for that one actor; the census's line plus " || addr 0x.. max <max HP> net N/I who sora|copy|-". Returns the count.
// Game thread.
int WorldCensusInspect(std::uintptr_t only, void (*line)(const char* text, void* ctx), void* ctx);

}  // namespace kh2coop
