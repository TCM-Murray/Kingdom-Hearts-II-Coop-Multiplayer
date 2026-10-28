#pragma once
// The other player's own Keyblade on their copy (TODO 3.3, 1.110; see copy_blade.cpp).

#include <cstdint>

namespace kh2coop {

// From PuppetInit: the hooks that end the copy's Keyblade before its files are freed or moved.
void CopyBladeInit();
// From world_sync's frame (co-op on): sends our Keyblade items when they change (no menu open) and now and then.
void CopyBladeFrame();
// A "KH2J" packet: the other player's Keyblade items.
void CopyBladeOnPacket(const char* buf, int n);
// From spawn_sync.cpp's SpawnTask hook, before FormLookSpawnStep (game thread): builds the copy's Keyblade again when it was ended or the item changed.
void CopyBladeSpawnStep();
// The other player's Keyblade for a form record (1-10, the form body entry's byte +0x57); 0 = not known.
std::uint16_t CopyBladePeerFormItem(int form);
// The weapon object for an item with a body's weapon kind (exe+0x3EF8B0), 0 none.
std::uint32_t CopyBladeWeaponOf(int kind, std::uint16_t item);
// Whether a weapon object's files (and the kind's weapon motion file for that slot) are loaded in a group of ours:
// 1 = yes, *group = the group its model is listed in; 0 = on their way (asked for now or before, or the room not
// settled yet); -1 = given up / declined / off.
// Slot 1 is form_look's second Keyblade.
int CopyBladeFiles(int kind, int slot, std::uint32_t obj, int* group);

}  // namespace kh2coop
