#pragma once
// The host's companions heal the other player's Sora copy (TODO 1.58, see companion_heal.cpp).
// COMPANION_HEAL_COPY=0 in kh2coop.ini turns it off.

#include <cstdint>

namespace kh2coop {

void CompanionHealInit(bool host);  // from AvatarLinkInit, once the role is known
// puppet.cpp HookItemApply: `user` (a companion) picked this HP item for the copy (once per pick).
bool CompanionHealItemForCopy(std::uintptr_t user);
// puppet.cpp: a Cure or an item on the copy was just sent to the other player (they need a moment to show it).
void CompanionHealSentToCopy();

}  // namespace kh2coop
