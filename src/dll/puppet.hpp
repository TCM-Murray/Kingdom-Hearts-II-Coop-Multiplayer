#pragma once
// Puppet: while the peer's player is in our room, our first party member
// (Donald, friend slot 1) stops running his own AI and copies the peer's
// position, facing and motion instead. Released back to his AI when the peer
// leaves the room or stops sending.
//
// Off unless KH2COOP_PUPPET=1 when the game starts.
// KH2COOP_PUPPET_ROXAS=1 also makes the puppet look and move like Roxas: the
// game's object table entry for Donald (id 92) is pointed at Roxas's model and
// motion set (P_EX110) once the table is loaded, so every Donald spawned from
// the next room load on is built from Roxas's data. His own AI then never runs
// (it would ask Roxas's motion set for Donald's moves); with no peer he idles.

#include <cstdint>

namespace kh2coop {

struct PeerPose {
    std::uint8_t world, room;
    bool hasActor;
    bool airborne;
    float pos[3];
    float angle;
    std::uint32_t motion;
    bool downed;       // the peer's Sora is down (downed.cpp)
    std::int32_t hp;   // the peer's Sora HP
    bool otherBank;    // motion is from another file's set (Limit, reaction command): not Sora's own id
    bool gettingUp;    // the peer's Sora is in its get-up after a down ("downed" is false by then)
    std::int32_t maxHp;  // the peer's Sora max HP
};

void PuppetInit();                       // from OnInit, after the input hooks
void PuppetOnPeerPose(const PeerPose&);  // each fresh packet from the peer
void PuppetFrame();                      // from OnFrame: staleness and logging
void PuppetOnPeerHit(int damage, int react);  // the peer reports our player was hit in their game
void PuppetOnPeerHeal(int percent, int source);  // the peer healed our player (Cure in range / item on our copy)
void PuppetSetMotion(std::uintptr_t actor, int motion);
bool PuppetHasMotion(std::uintptr_t actor, int motion);  // the actor's motion set has it (never set one it lacks: T-pose)
bool PuppetOurLimitRunning();  // our Sora is the user of the game's current Limit
bool PuppetLimitCommand();     // our Sora is carrying out a Limit command right now (start: before the pointer is set)
int PuppetApplyHp(std::uintptr_t actor, int delta, int react);  // the game's HP change, bypassing our hook  // our own motion change (not blocked by the motion hook)
std::uintptr_t PuppetCloneActor();
int PuppetDriveForm();                          // our Sora's Drive Form (save+0x3524), 0 = none
bool PuppetRevertForm(std::uintptr_t actor);    // end it like the menu's Revert
void PuppetDebugDamage(int who, int amount);  // test command: 0 Sora, 1/2 companion slot, 3 Sora copy; applied in its next update         // the Sora copy's actor while it is alive in this room, else 0

}  // namespace kh2coop
