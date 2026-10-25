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
// Live effects owned by `owner` (+0x50) and reading [from, from + size) (their definition, +0x48), for the log only.
void FormLookCountEffects(std::uintptr_t owner, std::uintptr_t from, std::uint64_t size, int* owned, int* reading);
// Test command 18 'formlook <form>': the copy takes that Drive Form's model, texture and motions (0 = back to Sora),
// until the peer's body changes.
void FormLookDebug(int form);

}  // namespace kh2coop
