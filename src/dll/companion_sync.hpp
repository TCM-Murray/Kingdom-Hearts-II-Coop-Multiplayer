#pragma once
// A Drive Form or summon in either game sends the same companions away in both, and they come back when nobody holds
// them any more (DECISIONS C1, C2; see companion_sync.cpp). Limits: limit_sync.cpp.

#include <cstdint>

namespace kh2coop {

void CompanionSyncInit(bool host);                   // from WorldSyncInit, once the role is known
void CompanionSyncFrame();                           // from WorldSyncFrame (game thread)
void CompanionSyncOnPacket(const char* buf, int n);  // a "KH2O" or "KH2S" packet from the other PC
// Friend, the HP hooks: true = our copy of a host's companion, whose HP and MP are the host's (drop gains here).
bool CompanionSyncHostOwns(std::uintptr_t actor);
// Friend: a Cure or item from us (HP stat 0, MP stat 32) on such a copy goes to the host's game.
void CompanionSyncForwardGain(std::uintptr_t actor, int delta, int stat, int react);
// The sender's own summon actor in its "KH2O" packet: [0] the summon, [1] its second actor (Tinker Bell); obj 0 = none.
#pragma pack(push, 1)
struct SummonPose {
    std::uint32_t obj;       // object id
    float pos[3], angle;
    std::uint32_t motion;
    std::uint8_t otherBank;  // the motion is from another file's set
};
// Stitch is drawn on the screen, not in the room: his brain keeps where in its helper object (actor+0xDF8), +0x650 to
// +0x700 (place, target, heading, rotation: floats) and its state at +0x790 (F635). Sent while the sender's summon is
// Stitch (character 0xC8), written into ours, whose brain is off.
constexpr int kStitchScreenBytes = 0xB0;
struct StitchScreen {
    std::uint8_t on;
    std::uint8_t bytes[kStitchScreenBytes];
    std::uint32_t state;
};
#pragma pack(pop)
// The other player's summon state while they are in our room and their packets come: 1 its entry plays in their
// game, 2 the summon is out, 3 it leaves; 0 none.
int CompanionSyncPeerSummonState();
// The other player's Stitch's screen state, while their summon is Stitch and their packet is fresh; else null.
const StitchScreen* CompanionSyncPeerStitch();
// The other player's summon actor k while its summon is out (its game's state 2) and it is in our room; *poseFresh:
// the pose is new enough to follow (a lag spike keeps the summon, not the pose). Game thread.
bool CompanionSyncPeerSummon(int k, SummonPose* out, bool* poseFresh);
// A place waits on the game's bring-back list: a leave put it there and no bring-back built it yet.
bool CompanionSyncListed();
// One move at a time for the same companion (TODO 1.198, 1.275; see companion_sync.cpp): from the step where our Sora
// carries out a picked command (puppet.cpp). True = not now: the friend's game asked the host and carries it out on
// its yes; the host's own is dropped while a move just agreed to for the friend needs the same companion.
bool CompanionSyncHoldMove(std::uintptr_t actor, const std::uint16_t* command);

}  // namespace kh2coop
