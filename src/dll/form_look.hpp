#pragma once
// The other player's Drive Form on their copy (TODO 1.24, see form_look.cpp).

#include <cstdint>

namespace kh2coop {

struct PeerPose;

// From PuppetInit: the hook that keeps the copy's form files when our own Drive drops the Drive area.
void FormLookInit();
// From PuppetFrame, after StandInFrame (same arguments): picks the look the copy should have from the peer's body.
void FormLookFrame(std::uintptr_t copy, const PeerPose* pose, bool roomGone);
// From spawn_sync.cpp's SpawnTask hook (game thread): carries out a look change asked for.
void FormLookSpawnStep();
// From copy_blade.cpp after it built the copy's Keyblade again: the Keyblade's own motions are noted and, in a form,
// the form's weapon motions put on it.
void FormLookBladeRebuilt(std::uintptr_t copy);
// Stops every effect `owner` owns (+0x50) at once; the count.
int FormLookStopEffectsOf(std::uintptr_t owner);
// The copy wears a look other than its own body (a Drive Form).
bool FormLookCopyInForm();
// Live effects owned by `owner` (+0x50) and reading [from, from + size) (their definition, +0x48), for the log only.
void FormLookCountEffects(std::uintptr_t owner, std::uintptr_t from, std::uint64_t size, int* owned, int* reading);
// Test command 18 'formlook <form>': the copy takes that Drive Form's model, texture and motions (0 = back to Sora),
// until the peer's body changes.
void FormLookDebug(int form);
// From OnFrame while co-op is on: the file census (research log, the review's 5.2.6).
void FormLookFileCensus();
// Test command 24 'negfiles <form>' (the review's 5.2.6): that form's look asked into a negative group of ours, once
// the room's files are placed and within the budget (KH2COOP_NEG_DRIVE_KB, _MARGIN_KB, _BUDGET_KB).
void FormLookDebugNeg(int form);
// Test command 25 'limitgap' (bench limit_gap's baseline): the Limits' files out of the Drive area on the spawn step,
// so the game's own Limit preload lays the heap out with no load wait on the next frame. Refused in a form, a Limit,
// a read, or while the copy wears a form look.
void FormLookDebugLimitGap();
// Option B of the review's 5.2.6 (the copy's files in negative groups of ours), for copy_blade.cpp's Keyblades, on
// the game thread: `ask(group)` makes the request (entries of ours only); the group, or 0: not now (the room's files
// not all in and placed yet) or *declined (no room in our list). Over the budget a Keyblade is asked anyway (logged).
// `keep`: for the whole room (the copy's Keyblade); else it goes with the look worn now (a form's second Keyblade).
int FormLookCopyFilesAsk(const char* what, bool keep, void (*ask)(int group, const void* arg), const void* arg,
                         bool* declined);
// True for a group of ours (asked this room).
bool FormLookCopyFilesOurs(int group);
// On around a weapon build for the copy: the game's 'any group' file lookups (exe+0x39CC40 with -1) take our entry of
// that name; reset at every spawn step.
void FormLookCopyFilesFill(bool on);
// From copy_blade.cpp's place-pass hook, after the game's layout (the census and the negative files' watch).
void FormLookAfterLayout();

}  // namespace kh2coop
