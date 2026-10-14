#pragma once
// A bigger effect pool and a log of each room's peak (TODO 1.59, see fx_pool.cpp). FX_POOL_KB=0 in
// kh2coop.ini leaves the game's own 512 KB pool.

namespace kh2coop {

void FxPoolInit();   // from AvatarLinkInit
void FxPoolFrame();  // from OnFrame while co-op is on (game thread)

}  // namespace kh2coop
