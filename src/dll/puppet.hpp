#pragma once
// Puppet: while the peer's player is in our room, a real copy of Sora in our
// game copies the peer's position, facing and motion (it runs no AI of its own).
// It is held where it stands when the peer leaves the room or stops sending.
//
// On unless KH2COOP_PUPPET=0 when the game starts.

#include <cstddef>
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
// Takes our party change (the Sora copy) out of `save`, a copy of the save data the game is about
// to write: each patched world gets back the member the copy replaced. Describes it in `what`;
// false if there was nothing to take out (TODO 5.19).
bool PuppetCleanSave(std::uint8_t* save, char* what, std::size_t size);
void PuppetOnPeerHit(int damage, int react);  // the peer reports our player was hit in their game
void PuppetOnPeerHeal(int percent, int source);  // the peer healed our player (Cure in range / item on our copy)
void PuppetSetMotion(std::uintptr_t actor, int motion);
bool PuppetHasMotion(std::uintptr_t actor, int motion);  // the actor's motion set has it (never set one it lacks: T-pose)
bool PuppetOurLimitRunning();  // our Sora is the user of the game's current Limit
bool PuppetLimitCommand();     // our Sora is carrying out a Limit command right now (start: before the pointer is set)
int PuppetApplyHp(std::uintptr_t actor, int delta, int react);  // the game's HP change, bypassing our hook  // our own motion change (not blocked by the motion hook)
int PuppetApplyHit(std::uintptr_t actor, int damage, int react);  // a hit with the god-mode rules (test settings)
std::uintptr_t PuppetCloneActor();
std::uintptr_t PuppetDrivenActor();  // the copy of the peer's Sora we drive now, if it has Sora's motions; else 0
// Playing on without the other player (link_watch.cpp, TODO 6.1): on = the party change is undone in
// every world (the member the copy replaced is back from the next room load) and not made again;
// off = co-op again, the copy comes back at the next room load.
void PuppetSetSolo(bool on);
bool PuppetSolo();       // playing on without the other player
bool PuppetExtraCopy();  // KH2COOP_EXTRA_COPY (on by default): the copy is spawned beside the party (solo_copy.cpp)
bool PuppetRemoveCopy();  // takes the Sora copy out of this room now (the game's fade-out removal); false if none
bool PuppetIsFieldPlayer(std::uintptr_t actor);  // a player-class actor that isn't a story cutscene's
int PuppetDriveForm();                         // our Sora's Drive Form (save+0x3524), 0 = none
bool PuppetRevertForm(std::uintptr_t actor);    // end it like the menu's Revert
void PuppetDebugTeleport(float x, float y, float z);  // test command: our Sora to that spot (y grows downward)
void PuppetDebugLowHp(int hp);  // test command lowhp: with god mode, hits leave our Sora at this HP (0 = off)
void PuppetDebugDamage(int who, int amount);  // test command: 0 Sora, 1/2 companion slot, 3 Sora copy; applied in its next update         // the Sora copy's actor while it is alive in this room, else 0

}  // namespace kh2coop
