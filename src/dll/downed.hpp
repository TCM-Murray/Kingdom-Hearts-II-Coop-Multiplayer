#pragma once
// Downed instead of dead while the other player is in the room (see downed.cpp).

#include <cstdint>

namespace kh2coop {

void DownedInit();                         // from AvatarLinkInit
void DownedFrame();                        // from OnFrame (game thread)
bool DownedIsDown();                       // our Sora is down or getting up
bool DownedIsLying();                      // our Sora is down and not yet getting up (sent to the peer)
// HP loss on any actor: true = handled here (our Sora: dropped while down, or a lethal hit left at 1 HP).
bool DownedInterceptDamage(std::uintptr_t actor, int delta, int react, int* hpOut);
// HP gain on our Sora while down: a Cure or an item (`revives`) gets it up now with the heal (at
// least max/4); anything else (HP orbs...) is dropped. True = handled here (HP left as it is).
bool DownedInterceptHeal(std::uintptr_t actor, int amount, bool revives, const char* what);
bool DownedBlocksMotion(std::uintptr_t actor);  // motion hook: only our own motion calls on a downed Sora
void DownedAfterUpdate(std::uintptr_t actor);   // from the entity update hook, after the game's update

}  // namespace kh2coop
