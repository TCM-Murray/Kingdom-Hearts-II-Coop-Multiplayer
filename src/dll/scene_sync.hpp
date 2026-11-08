#pragma once
// Walk-in story scenes for either player (see scene_sync.cpp). On unless SCENE_SYNC=0.

#include <cstdint>

namespace kh2coop {

void SceneSyncInit(bool host);                 // from AvatarLinkInit, after WorldSyncInit (spawn sync checks bytes)
void SceneSyncOnPacket(const char* buf, int n);  // a "KH2T" packet from the peer
void SceneSyncFrame();                         // from OnFrame (game thread)
// TODO 5.4: our RC of type 0x7F would run this settings entry; true = held until both players accepted it.
bool SceneSyncHoldEventRc(std::uint32_t settings);
void SceneSyncOurTick(std::uintptr_t actor);   // from the player tick hook: runs an accepted event
std::uintptr_t SceneSyncInfoBarObject();       // the INFO bar object, 0 = none (TODO 5.2 probe)
void SceneSyncSayClientInGummi();              // INFO bar: why the Host can't land (gummi_menu.cpp)

}  // namespace kh2coop
