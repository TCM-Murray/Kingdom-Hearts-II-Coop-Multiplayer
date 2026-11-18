#pragma once
// Walk-in story scenes for either player (see scene_sync.cpp). On unless SCENE_SYNC=0.

#include <cstdint>

namespace kh2coop {

void SceneSyncInit(bool host);                 // from AvatarLinkInit, after WorldSyncInit (spawn sync checks bytes)
void SceneSyncOnPacket(const char* buf, int n);  // a "KH2T" packet from the peer
void SceneSyncFrame();                         // from OnFrame (game thread)
// TODO 5.4: our RC of type 0x7F would run this settings entry; true = held until both players accepted it.
bool SceneSyncHoldEventRc(std::uint32_t settings);
// TODO 1.236 step 1: +1 / -1 around our own Sora's command (puppet.cpp), for the settings call log.
void SceneSyncOwnCommand(int delta);
void SceneSyncOurTick(std::uintptr_t actor);   // from the player tick hook: runs an accepted event
std::uintptr_t SceneSyncInfoBarObject();       // the INFO bar object, 0 = none (TODO 5.2 probe)
// Whether the room's settings have an entry for this id with no group: what exe+0x3F5EF0 picks without checking
// that it found one (a mission's Retry is 0x8B, its Quit 0x8D).
bool SceneSyncHasJump(std::int32_t id);
void SceneSyncSayClientInGummi();              // INFO bar: why the Host can't land (gummi_menu.cpp)

}  // namespace kh2coop
