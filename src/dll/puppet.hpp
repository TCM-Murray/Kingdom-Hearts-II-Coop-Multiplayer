#pragma once
// Puppet: while the peer's player is in our room, a real copy of Sora in our
// game copies the peer's position, facing and motion (it runs no AI of its own).
// It is held where it stands when the peer leaves the room or stops sending.
//
// On unless KH2COOP_PUPPET=0 when the game starts.

#include <cstddef>
#include <cstdint>

#include "avatar_link.hpp"  // HitShape

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
    std::uint32_t body;  // object id of the peer's player body (0x54 Sora, 0x602 the die; stand_in.cpp)
    std::uint8_t form;   // the peer's form byte (Drive Forms; stays 0 for the die)
    bool captured;       // a boss's capture (die / card) has the peer's player (stand_in.cpp)
    float motionTime = 0.0f;  // the peer's place in its motion (research: a restart of the same motion)
    std::uint8_t motionBlend = 0;  // the blend (frames) the peer's game passed when its Sora went into `motion`
    std::uint8_t hudHit = 0;       // the hit events the peer's own gauge played (party_hud.cpp's hit byte)
    std::int32_t mp = 0, maxMp = 0;  // the peer's Sora MP and max MP (its sheet +0x180 / +0x184)
    // A motion of the peer's own set (its number + 1; 0 = none) that it played between the sample before this one
    // and this one, and that is neither's: it lasted under one of our frames (TODO 1.205).
    std::uint32_t skippedMotion = 0;
    bool euler = false;   // the peer's body turns by three angles, tilt included (game::kFlagTurnEuler: the rails)
    float turn[3] = {};   // ... these (game::kActorRotVec x, y, z)
};

void PuppetInit();                       // from OnInit, after the input hooks
// The blend, in frames, our game passed when `actor` (our player) went into `motion`; 0 when that change wasn't seen.
std::uint8_t PuppetOurMotionBlend(std::uintptr_t actor, std::uint32_t motion);
void PuppetOnPeerPose(const PeerPose&);  // each fresh packet from the peer
void PuppetFrame();                      // from OnFrame: staleness and logging
// The peer reports our player was hit in their game; `info` = the hit's details from the game's hit apply (0 = none),
// for our own protection and survive rules; `shape` = for playing its reaction on our Sora.
void PuppetOnPeerHit(int damage, int react, int info, const HitShape& shape);
// The peer healed our player (Cure in range / item on our copy); info = the item (puppet.cpp ItemInfo), 0 for a Cure.
void PuppetOnPeerHeal(int percent, int source, int info);
void PuppetSetMotion(std::uintptr_t actor, int motion);
void PuppetMotionAgain();  // the copy's motion is set from the peer's pose again next frame (after a look change)
// The other player's body started this motion twice in one frame (its form's script on top of the game's own start,
// TODO 1.205): the copy starts it twice too, when it goes into it or, if it is in it already, now.
void PuppetMotionTwice(int motion);
bool PuppetHasMotion(std::uintptr_t actor, int motion);  // the actor's motion set has it (never set one it lacks: T-pose)
bool PuppetOurLimitRunning();  // our Sora is the user of the game's current Limit
bool PuppetLimitCommand();     // our Sora is carrying out a Limit command right now (start: before the pointer is set)
int PuppetApplyHp(std::uintptr_t actor, int delta, int react);  // the game's HP change, bypassing our hook  // our own motion change (not blocked by the motion hook)
int PuppetApplyStat(std::uintptr_t actor, int delta, int stat, int react);  // any stat (32 = MP), bypassing our hook
int PuppetApplyHit(std::uintptr_t actor, int damage, int react);  // a hit with the god-mode rules (test settings)
// Host: this hit (trap_damage_is_reaction's argument) is our Sora's on an enemy holding the copy, applied this frame:
// its script is told it caused no reaction (TODO 1.257 case B).
bool PuppetQuietHitReaction(std::uintptr_t hit);
// Host: the friend's hit on our object with no stat block (TODO 1.201): the game's own hit handler for that actor,
// with a hit record built from what the friend's game sent, the copy as its attacker. False = not run. The caller
// gives a live actor (world_sync.cpp calls it from that actor's own update).
// With HP (TODO 1.277 step 2) the same call: the handler also takes the HP by the game's rules. `revengeMult`: the
// friend's own multiplier for the revenge this hit adds (classes that keep one). `out`: what the call did.
struct PlayedHit {
    int tells;           // times the handler sent the actor's main script its damage event
    bool applied;        // the game's hit apply ran (hit +0x18 bit 1)
    bool revengeClass;   // the class adds the attack's revenge value (L2052)
    float revengeBefore, revengeAfter;
};
bool PuppetPlayHit(std::uintptr_t actor, int damage, std::uint32_t info, const HitShape& shape, const std::uint8_t* attackData,
                   float revengeMult = 1.0f, PlayedHit* out = nullptr);
// Tells an actor's scripts of a hit whose damage we applied ourselves (the friend's claim on a boss part, TODO
// 1.277), as the game's hit handler does after its own damage. `stat` = the HP channel that was hit.
bool PuppetTellScriptHit(std::uintptr_t actor, int damage, int stat);
void PuppetClearMovement(std::uintptr_t actor);  // its velocity, carried and acceleration terms zeroed
std::uintptr_t PuppetCloneActor();
// Host, before the game's 1/111 trap_obj_set_movement(actor, vec): with the held copy and its own movement vector, the
// friend's stick's goes there first (a ride steered by its rider, TODO 1.264).
void PuppetBeforeSetMovement(std::uintptr_t actor, std::uintptr_t vec);
// Host, after the game's 1/113 trap_player_get_movement(actor): with the held copy, its answer becomes the friend's
// stick (a ride that reads its rider's stick, TODO 1.266).
void PuppetAfterGetMovement(std::uintptr_t actor);
// Host, after the game's 0/80 (held) or 0/81 (pressed) for `bit` of pad 0: when `thread` (the script running now) is
// the one hooked on the copy, *answer becomes the friend's button (a ride's jump out, TODO 1.126).
void PuppetAfterPadRead(std::uintptr_t thread, bool pressed, int bit, std::uint64_t* answer);
// The copy while it is still in this room but not updated for a few frames (a menu or a pause), else 0: not gone,
// only not to be touched now (PuppetCloneActor is 0 meanwhile).
std::uintptr_t PuppetCopyPaused();
// Why the copy isn't in a quiet moment for a Keyblade rebuild (the crash review's 5.2.3), or null: held by a script,
// grabbed, down, captured, airborne, in another bank's motion (a Limit, a reaction command) or attacking (150-199).
const char* PuppetCopyBusy();
std::uintptr_t PuppetDrivenActor();  // the copy of the peer's Sora we drive now, if it has Sora's motions; else 0
// Playing on without the other player (link_watch.cpp, TODO 6.1): on = no Sora copy is spawned from the
// next room load; off = co-op again, the copy comes back at the next room load.
void PuppetSetSolo(bool on);
bool PuppetSolo();       // playing on without the other player
bool PuppetEnabled();    // KH2COOP_PUPPET (on by default): the copy is spawned beside the party (solo_copy.cpp)
bool PuppetRemoveCopy();  // takes the Sora copy out of this room now (the game's fade-out removal); false if none
bool PuppetIsFieldPlayer(std::uintptr_t actor);  // a player-class actor that isn't a story cutscene's
int PuppetDriveForm();                         // our Sora's Drive Form (save+0x3524), 0 = none
bool PuppetRevertForm(std::uintptr_t actor);    // end it like the menu's Revert
// Our Sora carries out this command record now, through the same step as a picked one (companion_sync.cpp: a move the
// host agreed to). The record is the command's whole table row, not only its id and type.
void PuppetCarryOut(std::uintptr_t actor, const std::uint16_t* command);
void PuppetDebugTeleport(float x, float y, float z);  // test command: our Sora to that spot (y grows downward)
// Test command endedloop (TODO 5.2d): a Potion's sparkle made on our Sora as an item makes it (F553); its effect
// instance (+0xC flags, +0x30 its part), or 0 when none was made.
std::uintptr_t PuppetDebugSparkle();
void PuppetDebugLowHp(int hp);  // test command lowhp: with god mode, hits leave our Sora at this HP (0 = off)
void PuppetDebugDamage(int who, int amount);  // test command: 0 Sora, 1/2 companion slot, 3 Sora copy, 5 our stand-in Mickey; applied in its next update         // the Sora copy's actor while it is alive in this room, else 0

}  // namespace kh2coop
