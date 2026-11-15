#pragma once
// Party HUD widget for the Sora copy (see party_hud.cpp). COPY_HUD=1 (default)
// lists the copy in the HUD; COPY_HUD_HP=1 (default) makes its bar show the
// peer's HP from their packets instead of our own Sora's.

#include <cstdint>

namespace kh2coop {

void PartyHudInit();   // from OnInit, after PuppetInit
void PartyHudFrame();  // from OnFrame (game thread), after PuppetFrame
// Hit events (TODO 3.18). A hit byte = a count << 2 | the event's flag bit (1-3): it changes with every event.
std::uint8_t PartyHudOwnHit();                         // our Sora's gauge, for the pose packet
std::uint8_t PartyHudMemberHit(std::uintptr_t actor);  // host: a companion's gauge, for its status (0 = not shown)
void PartyHudHostHit(std::uintptr_t actor, std::uint8_t hit);  // friend: the host's hit byte for our copy of it

}  // namespace kh2coop
