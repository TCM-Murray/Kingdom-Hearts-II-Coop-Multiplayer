#pragma once
// The host's Sora wears the gold crown, the friend's none, so the two players can be told apart
// (TODO 3.13, see crown.cpp). CROWN=0 in kh2coop.ini leaves the game's own crown (from each save's Proofs).

#include <cstdint>

namespace kh2coop {

void CrownInit(bool host);  // from AvatarLinkInit, once the role is known
void CrownFrame();          // from OnFrame while co-op is on

}  // namespace kh2coop
