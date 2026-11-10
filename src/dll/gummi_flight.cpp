// The Gummi flight in co-op (TODO 5.2 stage 1 items 5-6, docs/GUMMI_COOP.md). Each player flies their own copy; this keeps
// the moments around the flight together:
//   - the 'Gummi Battle Tips' window: the first to press waits for the other, so both flights start together;
//   - the pause (pause_sync.cpp opens and closes it in both games): only the one who paused uses its list; a Retry from
//     it restarts both;
//   - the Game Over and results lists: the first there waits until both are; then only the Host's list responds and the
//     Client's game takes the Host's pick;
//   - quitting through the pause counts as shot down: that player waits on the Game Over list;
//   - a clear counts in both saves; rank and rewards stay each player's own.
// With KH2COOP_GUMMI_SHIP_TEST=1 the other player's ship flies in our mission, and the enemies are shared: each game keeps its
// own (they stay in step, F459), each enemy goes for the nearer ship, and kills and score reach the other game. A small gauge
// beside ours shows the other player's HP and lock-on pips, and their ship fires its charged lasers and shows its hits here, for show.
// Without the other game (no link, or it isn't in a flight) nothing is held.
//
// KH2COOP_GOD=1 (tests only, never in the friend package): the ship's HP stays full, so a bench flight reaches its goal.
//
// Kept across frames: nothing of the game's. The ship and the list are read each frame from the game's own pointers
// (gb::PLAYER's constructor sets the ship's and its destructor clears it). The other player's gb::FRIEND isn't kept either:
// the game hands it to the hook each frame, and its slot number says whether it's the one we filled. The small gauge's sprites
// are the game's own gauge code's: it makes them in our gauge's memory and deletes them when ours is deleted, in the same call
// as the real gauge's delete, and we touch them only while that gauge's state says they exist. A volley's enemy pointer lives
// at most two frames in g_volley, taken inside that enemy's own step and checked against its gone flag before use; once in the
// stand-in lock-on's sight it follows the game's rule for its own sights: the game's sight step runs on it every frame and
// drops it the frame the game marks the enemy gone. A hit effect holds their ship's matrix like the game's own effects on it do:
// it is played from the hook that was just handed that ship, and the ship lasts the whole flight.

#include "gummi_flight.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <windows.h>  // TEMP pick stack

#include "avatar_link.hpp"
#include "common.hpp"
#include "gummi_menu.hpp"
#include "pause_sync.hpp"
#include "spawn_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kShip = 0xAF0540;                     // ptr: gb::PLAYER, 0 outside a flight
constexpr std::uintptr_t kShipHp = 0x578, kShipMaxHp = 0x57C;  // f32, i32
constexpr std::uintptr_t kShipState = 0x5D8;                   // i32: 1 starting, 2 flying, 3 goal, 4 shot down
constexpr std::int32_t kStarting = 1, kFlyingState = 2, kShotDown = 4;
constexpr std::uintptr_t kShipFlags = 0x140C;                  // u32: 0x10 the ship is going down, 0x80000 no rebirth
constexpr std::uint32_t kGoingDown = 0x10 | 0x80000;

// The menu byte (PauseSyncOpenMenu) during a flight.
constexpr std::uint8_t kMenuPause = 0x0B, kMenuGameOver = 0x0C, kMenuResults = 0x0D;

constexpr std::uintptr_t kList = 0xBEE478;       // ptr: the list of the menu that's up
constexpr std::uintptr_t kListInput = 0x2F9440;  // (list): the item confirmed, -1 no press, -2 cancel
constexpr std::uint8_t kListInputBytes[] = {0x40, 0x55, 0x53, 0x48, 0x8D, 0x6C, 0x24, 0xB1, 0x48, 0x81, 0xEC, 0x98,
                                            0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xA2, 0xFD, 0x45, 0x00, 0x48, 0x33};
constexpr std::int32_t kNoPress = -1, kCancel = -2, kPauseRetry = 1, kPauseGummiMenu = 7, kPauseWorldMap = 8;
constexpr std::int32_t kEndRetry = 0, kLastListItem = 2;  // the Game Over and results lists: 0 Retry, 1 Gummi Menu, 2 World Map

constexpr std::uintptr_t kStartEvent = 0x20FF40;  // (event): gb::START_EVENT's update, the tips window and the count-in
constexpr std::uint8_t kStartEventBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                             0x83, 0xEC, 0x30, 0x40, 0x32, 0xFF, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0x79};
constexpr std::uintptr_t kEventState = 0x10;  // i32: 3 the tips wait for a press; 2 and 5 a press skips the count-in
constexpr std::uint64_t kConfirm = 0x04;      // the bit START_EVENT reads in the pad's pressed bits

// The other player's ship in our flight (stage 2, a test so far: KH2COOP_GUMMI_SHIP_TEST=1): their chosen ship's data goes
// into an empty Teeny Ship blueprint, and the game builds a gb::FRIEND from it.
constexpr std::uintptr_t kShipChosen = 0x9A8824;  // u8: a ship is chosen
constexpr std::uintptr_t kShipData = 0x9A8880;    // the chosen ship's data
constexpr std::size_t kShipBytes = 0x800;
constexpr int kShipChunks = 2;
constexpr std::size_t kChunkBytes = kShipBytes / kShipChunks;
constexpr std::uintptr_t kShipParts = 10;  // u16 in the ship data: its part count
constexpr std::uintptr_t kTeenyBlueprints[] = {0xAEF4B0, 0xAEF4D8};  // gb's blueprint objects for the two Teeny Ships
constexpr std::uintptr_t kBlueprintData = 8;                         // ptr: the ship data, 0 = empty
constexpr std::uintptr_t kFillBlueprints = 0x1FA590;                 // (heap): fills the blueprints for a flight
constexpr std::uint8_t kFillBlueprintsBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                                 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8};
constexpr std::uintptr_t kLoadBlueprint = 0x2061D0;  // (blueprint, ship data, heap)
constexpr std::uint8_t kLoadBlueprintBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                                0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xF0};
// It flies where the other player flies: these three gb::OBJ vectors are what the game draws a ship from.
constexpr std::uintptr_t kObjVectors[] = {0x08, 0x28, 0x38};  // position on the rail's plane; the two directions that turn it
constexpr int kVectors = 3;
constexpr std::uintptr_t kPlaceAcrossRail = 0xAEA1A0 + 0x80;  // f32 x, then y: our ship's place; the stick only adds to it
constexpr float kStartApart = 50.0f;                          // the Host starts this far left of the rail's centre, the Client right
constexpr std::uintptr_t kFriendSlot = 0x438;       // i32: gb::FRIEND's Teeny Ship slot
constexpr std::uintptr_t kFriendMatrix = 0x22B4B0;  // (its VTABLE object, friend): builds what's drawn from those vectors
constexpr std::uint8_t kFriendMatrixBytes[] = {0x48, 0x8B, 0xCA, 0xE9, 0x38, 0x9F, 0xFB, 0xFF};
constexpr float kPoseLimit = 100000.0f;  // the farthest a value in a pose from the network may be
// A friend's stats add to the player's: the other player's go to a state of their own.
constexpr std::uintptr_t kShipStats = 0x1FB8B0;  // (gumiship, gb::PLAYER_STATE)
constexpr std::uint8_t kShipStatsBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                            0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr std::uintptr_t kGumishipBlueprint = 0x90;   // i32: 0 the player's blueprint, slot + 1 a friend's
constexpr std::uintptr_t kNewPlayerState = 0x225C30;  // (state): gb::PLAYER_STATE's constructor
constexpr std::uint8_t kNewPlayerStateBytes[] = {0x48, 0x8D, 0x05, 0x39, 0x02, 0x39, 0x00, 0x48,
                                                 0xC7, 0x41, 0x3C, 0x64, 0x00, 0x00, 0x00};
constexpr std::size_t kPlayerStateBytes = 0x68;
// Shared enemies. For an enemy's step our ship's place is the other ship's when theirs is nearer; what it does there is only
// for show here, because the other game's copy of that enemy does it to them for real.
constexpr std::uintptr_t kObjectStep = 0x1E7880;  // (gb::OBJ): one object's step for this frame
constexpr std::uint8_t kObjectStepBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x0F, 0xB6,
                                             0x81, 0xFC, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF9};
constexpr std::uintptr_t kMotionSet = 0x217390;  // (motion player, motion, blend, start): every motion an object starts goes through here (F466)
constexpr std::uint8_t kMotionSetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                            0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F};
constexpr std::uintptr_t kShotAim = 0x1F1B20;  // (enemy gun, out start, out direction): every enemy shot and beam is aimed here (L247)
constexpr std::uint8_t kShotAimBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x70, 0x18, 0x4C,
                                          0x89, 0x70, 0x20, 0x55, 0x48, 0x8D, 0x68, 0xD8};
constexpr std::uintptr_t kEnemyGunOwner = 0x38, kEnemyGunKind = 0x92, kEnemyGunFlags = 0x9C;  // its object; u8 what it fires (L253); u32
constexpr std::uint32_t kGunReadsShip = (1u << 6) | (1u << 13);                                // flags: aims at our ship, starts at it
constexpr std::uintptr_t kGunFire = 0x1F0C50;  // (enemy gun, start, direction): makes one shot of the gun's kind (L253)
constexpr std::uint8_t kGunFireBytes[] = {0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x70};
constexpr std::uintptr_t kHomingStep = 0x221450;  // (gb::ENEMY_HOMING) -> still flying: steers toward its target's place (L253)
constexpr std::uint8_t kHomingStepBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x48, 0x8D, 0x68, 0xA1};
constexpr std::uintptr_t kHomingHit = 0x221650;  // (gb::ENEMY_HOMING, ?, its centre) -> hit: hurts our ship, or stops the missile steering near it (L253)
constexpr std::uint8_t kHomingHitBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x50};
constexpr std::uintptr_t kNewestHoming = 0xAF8410, kHomingVtable = 0x5B5CE0;  // ptr: the last one made; gb::ENEMY_HOMING's vtable
constexpr std::uintptr_t kHomingTarget = 0x98, kHomingDone = 0xA4;           // the object it steers to; u8: it stopped steering
constexpr std::uint8_t kHomingKind = 0x25;
constexpr float kHomingNear = 400.0f;  // the game stops a missile steering this near the ship's own sphere (exe+0x221650)
constexpr float kShipBlock = 10.0f;    // a ship's sides in blocks times this (exe+0x623BB4) give its size (exe+0x52E520)
constexpr std::uintptr_t kObjMotionPlayer = 0xB8, kObjMotion = 0xD4;  // the player; i32: the motion it's on
constexpr std::uintptr_t kObjPlace = 0x1E6BA0;  // (gb::OBJ, out vec4): its place in the world
constexpr std::uint8_t kObjPlaceBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                           0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kObjType = 0xAC;                  // i32: 2 and 3 are enemies
constexpr std::int32_t kEnemyTypeA = 2, kEnemyTypeB = 3;
constexpr std::uintptr_t kObjRow = 0x238;                  // ptr: the row it was built from, +0 i32 its kind
constexpr std::uintptr_t kEnemyFlags = 0x470;              // u32: bit 0 going down
constexpr std::uint32_t kEnemyGoingDown = 1;
constexpr std::uint32_t kEnemyNoDown = 0x200;  // exe+0x214350 does nothing for such an enemy
constexpr std::uintptr_t kEnemyContact = 0x213D70;  // (enemy): it touches our ship
constexpr std::uint8_t kEnemyContactBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0x91,
                                               0x70, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x8B};
constexpr std::uintptr_t kEnemyDown = 0x214350;  // (enemy, award): 1 adds its score, 0 only blows it up and removes it
constexpr std::uint8_t kEnemyDownBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
                                            0xEC, 0x40, 0x8B, 0x81, 0x70, 0x04, 0x00, 0x00};
constexpr std::uintptr_t kAddScore = 0x207A50;  // (points)
constexpr std::uint8_t kAddScoreBytes[] = {0x80, 0x3D, 0xDD, 0x36, 0x52, 0x00, 0x00, 0x75,
                                           0x31, 0x03, 0x0D, 0xD1, 0x36, 0x52, 0x00, 0x89};
constexpr std::uintptr_t kScore = 0x72B130;         // i32
// A kill's score and heal orbs (L238). The game drops them only for a kill that awards, so the other player's kills need this call.
constexpr std::uintptr_t kDropOrbs = 0x214570;  // (enemy)
constexpr std::uint8_t kDropOrbsBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x81, 0x70, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0xC1};
constexpr std::uintptr_t kEnemyHit = 0x214190;  // (enemy, attack): what every player shot calls; the enemy reacts in its own step
constexpr std::uint8_t kEnemyHitBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kEnemyHp = 0x450;       // i32: -1 once shot down
constexpr std::uintptr_t kEnemyFlash = 0x454;    // f32: frames of the white hit flash left
constexpr std::uintptr_t kObjTint = 0xA8;        // u32: its colour during the flash
constexpr std::uintptr_t kObjAge = 0xB0;         // f32: frames since it was made, advanced by its own step
constexpr float kHitFlash = 8.0f;                // the values exe+0x214190 writes
constexpr std::uint32_t kHitTint = 0x80FFFFFF;
constexpr std::uint32_t kEnemyFlashed = 0x20, kEnemyNoFlash = 0x0A;
constexpr std::uintptr_t kScriptRandom = 0x7535C0;  // u32: what scripts roll (L243); not the flight's number
constexpr std::uintptr_t kScriptRoll = 0x41DBF0;     // trap_random_get: its first instruction names kScriptRandom
constexpr std::uint8_t kScriptRollBytes[] = {0x69, 0x05, 0xC6, 0x59, 0x33, 0x00, 0xCD, 0x0D, 0x01, 0x00};
constexpr std::uintptr_t kEnemyTreasure = 0x21C;  // i32: non-zero, it drops a treasure instead, in the game that made the kill
constexpr std::uintptr_t kOrbScoreReturns[] = {0x20E56C, 0x20E5E9};  // the orb pickup's two calls of exe+0x2339B0, which jumps to kAddScore
constexpr std::uintptr_t kFriendAbsorb = 0x22B770;  // (friend, shot): a friend takes the shot, +100 score
constexpr std::uint8_t kFriendAbsorbBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x07, 0x82, 0x00,
                                               0x00, 0x48, 0x8D, 0x0D, 0xC0, 0xB9, 0x8C, 0x00};
// The other player's small gauge: the game's gb::MENU_GAUGE built a second time in our memory (L233, L234). It reads every
// value through the ship global, so for its step that global points at a stand-in holding the other player's numbers.
constexpr std::uintptr_t kGauge = 0x72C388;      // the game's gauge
constexpr std::uintptr_t kGaugeCtor = 0x235610;  // (gauge)
constexpr std::uint8_t kGaugeCtorBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8D, 0x05, 0xF3, 0x0B, 0x38, 0x00, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kGaugeClear = 0x236590;  // (gauge): zeroes its pointers and state
constexpr std::uint8_t kGaugeClearBytes[] = {0x33, 0xC0, 0x48, 0xC7, 0x81, 0x8C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F, 0x48, 0x89, 0x41};
constexpr std::uintptr_t kGaugeStep = 0x235A10;  // (gauge): each frame
constexpr std::uint8_t kGaugeStepBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x8B, 0x89, 0x88, 0x01, 0x00, 0x00, 0x83};
constexpr std::uintptr_t kGaugeDelete = 0x2358C0;  // (gauge): deletes its sprites
constexpr std::uint8_t kGaugeDeleteBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::size_t kGaugeBytes = 0x1A0;
constexpr std::uintptr_t kGaugeState = 0x188, kGaugeFadeDone = 0x198;  // i32: 0 none, 1 build, 2 running, 3 fading; u8
constexpr std::int32_t kGaugeNone = 0, kGaugeBuild = 1, kGaugeRunning = 2, kGaugeFading = 3;
constexpr std::uintptr_t kGaugeOnceSprites[] = {0x08, 0x10, 0x18};               // the game offsets these once, by kGaugeBaseX
constexpr std::uintptr_t kGaugeFrameSprites[] = {0x20, 0x30, 0x40, 0x148};       // the step places these every frame
constexpr std::uintptr_t kGaugeMasks[] = {0x158, 0x170};                         // gb::CIRCLE_MASK: the HP ring
constexpr std::uintptr_t kGaugeOtherSprites[] = {0x28, 0x38, 0x150};             // scaled, not moved
constexpr std::uintptr_t kGaugePips = 0x48;                                      // sprite pointers, one per pip
constexpr std::int32_t kGaugeBaseX = 0x55;
constexpr std::uintptr_t kSpriteX = 0x1B4, kSpriteY = 0x1B8;                     // i32
constexpr std::uintptr_t kSpriteSeqFlags = 0xF8, kSpriteScale = 0x188;           // u32 (bit 1 turns the scale on); f32 uniform, x, y
constexpr std::uint32_t kSeqScaled = 2;
constexpr std::uintptr_t kMaskX = 0x8, kMaskY = 0xC, kMaskRadius = 0x14;         // i32 each
constexpr std::int32_t kMaskRadiusFull = 100;
constexpr float kOtherGaugeScale = 0.6f;
constexpr std::int32_t kOtherGaugeDx = -90, kOtherGaugeDy = 20;                  // from the game's gauge, bottom right: just left of it
// Every laser part adds its pips to our player's gb::LOCKON, whichever ship it's on (L235): the other player's ship must not.
constexpr std::uintptr_t kBuildParts = 0x1FAB30;  // (gumiship, blueprint): builds a ship's parts
constexpr std::uint8_t kBuildPartsBytes[] = {0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x6C, 0x24, 0x40, 0x48, 0x8B, 0xF9, 0x48};
constexpr std::size_t kShipBytesAll = 0x1410;
constexpr std::uintptr_t kLockon = 0xB90, kLockonState = kLockon + 0x708, kPips = kLockon + 0x71C;  // gb::LOCKON in gb::PLAYER
constexpr std::uintptr_t kSights = kLockon + 8, kSightBytes = 0x38, kSightState = 0x34;
constexpr std::uintptr_t kPipCounts = kLockon + 0x70C;  // one count per laser kind (4), then the total the gauge draws
constexpr int kPipCountFields = 5;
// The other player's charged lasers (L236): their ship's own guns, told to fire at a second gb::LOCKON in our memory.
constexpr std::uintptr_t kPlayerFire = 0x1FE6C0;  // (gb::PLAYER): the lock-on's release; fires the ship's guns, then both Teeny Ships'
constexpr std::uint8_t kPlayerFireBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kGunTake = 0x2326C0;     // (gb::LOCKON_GUN, gb::LOCKON, sound): takes the next locked sight to fire at
constexpr std::uint8_t kGunTakeBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x89, 0x9C, 0x00, 0x00, 0x00, 0x01, 0x48, 0x8B, 0xC2};
constexpr std::uintptr_t kGunPower = 0x68;        // i32: what its laser's hit takes off
constexpr std::uintptr_t kFriendFire = 0x22B7C0;  // (gb::FRIEND, gb::LOCKON): its guns take one sight each
constexpr std::uint8_t kFriendFireBytes[] = {0x48, 0x81, 0xC1, 0x40, 0x04, 0x00, 0x00, 0xE9, 0x44, 0xFE, 0xFC, 0xFF};
constexpr std::uintptr_t kFriendFireMore = 0x22B7D0;  // the same for the group 2 guns, called until group 2 has no sights left
constexpr std::uint8_t kFriendFireMoreBytes[] = {0x48, 0x81, 0xC1, 0x40, 0x04, 0x00, 0x00, 0xE9, 0xC4, 0xFE, 0xFC, 0xFF};
constexpr std::uintptr_t kLockonStep = 0x1F7900;  // (gb::LOCKON): runs each sight's states
constexpr std::uint8_t kLockonStepBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00};
constexpr std::uintptr_t kTeenyShips = 0x688;     // two gb::FRIEND* in gb::PLAYER
constexpr std::uintptr_t kObjDead = 0x1FC;        // u8, bit 0: the game's own "this object is gone"
constexpr std::size_t kLockonBytes = 0x780;
constexpr std::uintptr_t kSightPlace = 8, kSightTarget = 0x18, kSightPart = 0x20, kSightGroup = 0x30;
constexpr std::int32_t kSightFree = 0, kSightFiring = 4, kSightFlying = 5, kGroupMore = 2, kMaxGroup = 3;
constexpr int kVolleyMatchFrames = 2;  // one whole frame of enemy steps, so each sight has met every enemy
// The hit effect the player's step plays on its own ship (L237), played on the other player's ship when they're hit.
constexpr std::uintptr_t kEffects = 0xAEA388;     // the manager exe+0x1F00A0 returns
constexpr std::uintptr_t kPlayEffect = 0x2C6E00;  // (manager, id, description, attached, 0, 0)
constexpr std::uint8_t kPlayEffectBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B};
constexpr std::uintptr_t kObjMatrix = 0x240;
constexpr std::int32_t kEffectHit = 1;
constexpr std::uint32_t kShipHitFrame = 2;  // in kShipFlags: set for the one step that played the hit effect
constexpr std::int32_t kSightLocked = 1, kMaxPips = 32, kMaxShipHp = 100000;
constexpr float kKillRange = 80.0f;       // on each axis: two games' copies of one enemy were at most about 70 apart
constexpr int kMaxKills = 64;             // a laser volley can down a whole screen at once (44 enemies seen on one)
constexpr int kKillFrames = 90;           // how long a kill waits for an enemy the other game hasn't made yet
constexpr int kMaxLostKillLogs = 40;      // a session
constexpr int kOwnKillFrames = 120;       // how long a kill of ours can still turn out to be the other game's too
constexpr int kMaxTracked = 512, kMaxKindIds = 0x1000;  // the most seen at once is about 75; kinds reach 0x593
constexpr int kLiveFrames = 2;            // an enemy whose step ran this long ago still counts as there
constexpr int kTrackStaleFrames = 120;    // then its entry can be given to a new enemy
constexpr int kMaxHits = 24;              // enemies hit in one message
constexpr int kHitEvery = 6;              // frames between hit messages
constexpr std::int32_t kMaxHitDamage = 1000000;  // the most one message from the network may take
// Each game's strong enemy plays its own moves on the same dice (L243); the Host corrects its place, HP and target.
// Below this HP the two copies stay within 20 units of each other anyway, and a fast one that followed would only lag (F466).
constexpr std::int32_t kStrongHp = 3000;
constexpr int kMaxStrong = 8;             // in one message; two bosses at once so far, arms aren't sent (they ride on the body)
constexpr int kStrongEvery = 3;           // frames between the Host's messages: a damaged boss covers hundreds of units in half a second
constexpr int kStrongStale = 30;          // with no message for this long the Client's copy acts for itself again
constexpr float kStrongEase = 0.5f;       // share of the way to the Host's place moved each frame
constexpr float kStrongSnap = 200.0f;     // further off than this it jumps there
constexpr std::int32_t kMaxMotionId = 0xFFFF;
constexpr float kMaxBlend = 120.0f;       // frames; the game asks for 0 or 8
constexpr int kScoreEvery = 6;            // frames between score messages
constexpr std::int32_t kScoreLimit = 1000000;  // the most one message from the network may add
constexpr int kStatsEvery = 300;          // frames between the log's counts

constexpr int kStatusEvery = 10;         // frames
constexpr int kLoadGraceFrames = 1800;   // the tips wait this long for a game that hasn't reached its flight

constexpr std::uint32_t kMagic = 0x6632484B;  // "KH2f"
constexpr std::uint8_t kVersion = 1;

enum Phase : std::uint8_t { kOutside, kTips, kTipsPressed, kFlying, kPaused, kDown, kBusy, kAtList, kPhaseCount };
const char* const kPhaseName[kPhaseCount] = {"outside a flight", "at the tips",       "at the tips, pressed",
                                             "flying",           "paused",            "shot down",
                                             "at the goal",      "at the end list"};
enum Kind : std::uint8_t { kStatus, kPick, kRestart, kCleared, kShipChunk, kShipPose, kKill, kScoreAdd, kShipGauge, kShipVolley, kHit, kStrong };

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic, build;
    std::uint8_t version, kind, arg;  // kStatus: our Phase; kPick: the list item the Host confirmed
};
struct ShipPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind, index, pad;
    std::uint32_t sum;  // of the whole ship: the two halves of one ship carry the same
    std::uint8_t data[kChunkBytes];
};
struct PosePacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    float pose[kVectors * 3];  // kObjVectors' x, y, z in order
};
struct KillPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    std::uint32_t id;        // Tracked::id, 0 = the sender has none
    std::int32_t enemyKind;  // its row's id
    float place[3];
    std::int32_t points;  // what it gave the sender
};
struct HitEntry {
    std::uint32_t id;
    std::int32_t damage;
};
struct HitPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind, count;
    HitEntry hits[kMaxHits];
};
struct StrongState {
    std::uint32_t id;
    std::int32_t motion, hp;
    float blend;
    float at[3];  // kObjVectors[0]
    std::uint8_t aimsAtClient;  // which ship it's after: both games must agree, or a hit lands on both players or neither
};
struct StrongPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind, count;
    StrongState list[kMaxStrong];
};
struct ScorePacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    std::int32_t points;
};
struct GaugePacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    float hp;
    std::int32_t maxHp;
    std::uint8_t lockonState, pips;
    std::uint32_t lit;  // bit per pip
    std::uint8_t hits;  // counts up per hit taken, so a lost packet can't lose one
};
struct VolleySight {
    float place[3];
    std::int32_t enemyKind;
    std::uint8_t part, group;
};
struct VolleyPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind, count;
    VolleySight sights[kMaxPips];
};
#pragma pack(pop)

using PFN_ListInput = std::int32_t(__fastcall*)(std::uintptr_t);
using PFN_StartEvent = std::uintptr_t(__fastcall*)(std::uintptr_t);
using PFN_FillBlueprints = void(__fastcall*)(std::uintptr_t);
using PFN_LoadBlueprint = void(__fastcall*)(std::uintptr_t, const void*, std::uintptr_t);
using PFN_FriendMatrix = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_ShipStats = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_NewPlayerState = void(__fastcall*)(void*);
using PFN_ObjectStep = void(__fastcall*)(std::uintptr_t);
using PFN_MotionSet = void(__fastcall*)(std::uintptr_t, std::int32_t, float, float);
using PFN_ShotAim = void(__fastcall*)(std::uintptr_t, float*, float*);
using PFN_HomingStep = bool(__fastcall*)(std::uintptr_t);
using PFN_HomingHit = bool(__fastcall*)(std::uintptr_t, std::uintptr_t, const float*);
using PFN_ObjPlace = void(__fastcall*)(std::uintptr_t, float*);
using PFN_EnemyContact = void(__fastcall*)(std::uintptr_t);
using PFN_EnemyDown = void(__fastcall*)(std::uintptr_t, std::uint8_t);
using PFN_EnemyHit = std::int32_t(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_AddScore = void(__fastcall*)(std::int32_t);
using PFN_FriendAbsorb = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_Gauge = void(__fastcall*)(std::uintptr_t);

bool g_on = false;
PFN_Gauge g_realGaugeStep = nullptr, g_realGaugeDelete = nullptr, g_gaugeClear = nullptr;
void(__fastcall* g_realBuildParts)(std::uintptr_t, std::int32_t) = nullptr;
alignas(16) std::uint8_t g_otherGauge[kGaugeBytes] = {};  // stays put: the game keeps pointers into it for its ring's draw
alignas(16) std::uint8_t g_standShip[kShipBytesAll] = {};  // only the fields the gauge reads are ever set
GaugePacket g_peerGauge = {};
bool g_peerGaugeReady = false;
void(__fastcall* g_realPlayerFire)(std::uintptr_t) = nullptr;
void(__fastcall* g_realGunTake)(std::uintptr_t, std::uintptr_t, std::uint8_t) = nullptr;
void(__fastcall* g_friendFire)(std::uintptr_t, std::uintptr_t) = nullptr;
void(__fastcall* g_friendFireMore)(std::uintptr_t, std::uintptr_t) = nullptr;
PFN_Gauge g_lockonStep = nullptr;
alignas(16) std::uint8_t g_standLockon[kLockonBytes] = {};  // stays put: their guns and lasers keep pointers to its sights
struct VolleyAim {
    float place[3];
    std::int32_t enemyKind, part, group;
    std::uintptr_t enemy;  // our copy of their target; only between MatchVolley and FireVolley
    float best;
};
VolleyAim g_volley[kMaxPips] = {};
int g_volleyCount = 0, g_volleyFrames = 0;
struct EffectDescription {  // as the game builds one on the stack
    float offset[4], angle[4], scale[4];
    std::uintptr_t matrix;
    std::uint64_t unused;
    std::uint16_t attached;
};
std::uintptr_t(__fastcall* g_playEffect)(std::uintptr_t, std::int32_t, const EffectDescription*, std::int32_t, std::uintptr_t, std::uintptr_t) = nullptr;
std::uint8_t g_hitsOut = 0;
bool g_showOtherHit = false;
PFN_ListInput g_realListInput = nullptr;
PFN_StartEvent g_realStartEvent = nullptr;
PFN_FillBlueprints g_realFillBlueprints = nullptr;
PFN_LoadBlueprint g_loadBlueprint = nullptr;
std::uint8_t g_shipSent[kShipBytes] = {};       // the ship we last told the other game about
bool g_shipSentValid = false;
std::uint8_t g_peerShipIn[kShipBytes] = {};     // the other's ship as its halves arrive
std::uint32_t g_peerShipInSum = 0;
int g_peerShipInHave = 0;                       // bit per half
std::uint8_t g_peerShip[kShipBytes] = {};       // stays put: the game keeps a pointer to it for the flight
bool g_peerShipReady = false;
PFN_FriendMatrix g_realFriendMatrix = nullptr;
PFN_ShipStats g_realShipStats = nullptr;
PFN_NewPlayerState g_newPlayerState = nullptr;
alignas(16) std::uint8_t g_otherState[kPlayerStateBytes] = {};  // stays put: their gumiship keeps a pointer to it
int g_otherSlot = -1;                                           // this flight's Teeny Ship slot holding the other player's ship
float g_peerPose[kVectors * 3] = {};
bool g_peerPoseReady = false;
PFN_ObjectStep g_realObjectStep = nullptr;
PFN_MotionSet g_realMotionSet = nullptr;
PFN_ShotAim g_realShotAim = nullptr;
PFN_ShotAim g_realGunFire = nullptr;
PFN_HomingStep g_realHomingStep = nullptr;
PFN_HomingHit g_realHomingHit = nullptr;
PFN_ObjPlace g_objPlace = nullptr;
PFN_EnemyContact g_realEnemyContact = nullptr;
PFN_EnemyDown g_realEnemyDown = nullptr;
PFN_EnemyHit g_realEnemyHit = nullptr;
PFN_AddScore g_realAddScore = nullptr;
void(__fastcall* g_dropOrbs)(std::uintptr_t) = nullptr;
std::int32_t g_statOrbPoints = 0;
std::uint32_t* g_scriptRandom = nullptr;  // null unless this build's code names it
PFN_FriendAbsorb g_realFriendAbsorb = nullptr;
bool g_aimingAtOther = false;  // this enemy's step sees the other ship as ours
struct PendingKill {
    std::uint32_t id;
    std::int32_t enemyKind;
    float place[3];
    int frames;
    std::int32_t points;
};
PendingKill g_kills[kMaxKills] = {};
int g_killCount = 0;
std::int32_t g_scoreOut = 0;  // points of ours the other game hasn't heard about
int g_scoreFrames = 0;
PendingKill g_own[kMaxKills] = {};  // kills we just told the other game about, with the points each gave
int g_ownCount = 0;
int g_statEnemies = 0, g_statAimed = 0, g_statSent = 0, g_statDone = 0, g_statBoth = 0, g_statLost = 0, g_statFull = 0, g_statPeak = 0, g_statDiffer = 0, g_statHitsSent = 0, g_statHitsDone = 0, g_statHitsLost = 0, g_statFollowMotion = 0;
HitEntry g_hits[kMaxHits] = {};
int g_hitCount = 0, g_hitFrames = 0;
Phase g_mine = kOutside;
std::uint8_t g_peer = kOutside;  // the other game's Phase
bool g_tipsPressed = false;      // our player pressed at the tips window
int g_tipsFrames = 0;            // frames at the tips window while the other game was outside its flight
int g_pick = -1;                 // the Client: the Host's pick, kept until the list closes
bool g_restart = false;          // the other player's Retry from their pause, for when we're not in the pause
bool g_quit = false;             // our player quit through the pause: the ship goes down once the pause has closed
bool g_clearSent = false, g_peerCleared = false;  // this flight's clear: ours told; theirs waiting for our end list
int g_frame = 0;
const char* g_note = nullptr;

void Note(const char* text) {
    if (text == g_note) return;
    g_note = text;
    Log("gummi flight: %s", text);
}

void Send(Kind kind, std::uint8_t arg) {
    Packet p {kMagic, AvatarLinkBuild(), kVersion, kind, arg};
    if (kind == kStatus) AvatarLinkSendRaw(&p, sizeof(p));
    else AvatarLinkSendReliable(&p, sizeof(p));
}

std::uintptr_t Ship() {
    std::uintptr_t ship = 0;
    return ReadAny(ExeBase() + kShip, &ship) ? ship : 0;
}

std::int32_t ShipState(std::uintptr_t ship) {
    std::int32_t state = 0;
    ReadAny(ship + kShipState, &state);
    return state;
}

Phase Mine() {
    const std::uintptr_t ship = Ship();
    if (!ship) return kOutside;
    const std::uint8_t menu = PauseSyncOpenMenu();
    if (menu == kMenuGameOver || menu == kMenuResults) return kAtList;
    if (menu == kMenuPause) return kPaused;
    const std::int32_t state = ShipState(ship);
    if (state == kStarting) return g_tipsPressed ? kTipsPressed : kTips;
    if (state == kFlyingState) return kFlying;
    return state == kShotDown ? kDown : kBusy;
}

std::int32_t PauseInput(std::uintptr_t list) {
    const int close = PauseSyncTakeClose();
    if (close) return close == 2 ? kPauseRetry : kCancel;
    if (GummiMenuLinked() && PauseSyncFollowing()) return kNoPress;  // the other player's pause: they use the list
    const std::int32_t r = g_realListInput(list);
    if (r >= 0) Log("gummi flight: our pause list: item %d", r);
    if (r == kPauseRetry) {
        Note("our Retry from the pause: both restart");
        PauseSyncRetryOnClose();
        if (GummiMenuLinked()) Send(kRestart, 0);
    } else if ((r == kPauseGummiMenu || r == kPauseWorldMap) && GummiMenuLinked() && g_peer != kOutside) {
        Note("our quit from the pause counts as shot down");
        g_quit = true;
        return kCancel;  // closes the pause as Continue
    }
    return r;
}

// Never answers anything but -1 or a real item: the game indexes its table with whatever isn't -1.
std::int32_t EndListInput(std::uintptr_t list) {
    if (g_restart) return kEndRetry;
    if (!GummiMenuLinked() || g_peer == kOutside) return g_realListInput(list);
    if (g_peer != kAtList) {
        Note("at the end list first: held until the other player is there too");
        return kNoPress;
    }
    if (!AvatarLinkIsHost()) {
        if (g_pick < 0) Note("at the end list, both there: waiting for the Host's pick");
        return g_pick < 0 ? kNoPress : g_pick;
    }
    Note("at the end list, both there: the Host picks");
    const std::int32_t r = g_realListInput(list);
    if (r >= 0 && r <= kLastListItem) {
        Log("gummi flight: the Host's pick is item %d, sent", r);
        Send(kPick, static_cast<std::uint8_t>(r));
    }
    return r;
}

std::int32_t __fastcall HookListInput(std::uintptr_t list) {
    const std::uint8_t menu = PauseSyncOpenMenu();
    std::uintptr_t current = 0;
    if ((menu != kMenuPause && menu != kMenuGameOver && menu != kMenuResults) || !Ship() ||
        !ReadAny(ExeBase() + kList, &current) || current != list)
        return g_realListInput(list);
    return menu == kMenuPause ? PauseInput(list) : EndListInput(list);
}

std::uintptr_t __fastcall HookStartEvent(std::uintptr_t event) {
    std::int32_t state = 0;
    std::uint64_t* const pressed = GummiMenuPressed();
    if (!pressed || !ReadAny(event + kEventState, &state) || (state != 3 && state != 2 && state != 5) ||
        !GummiMenuLinked() || (g_peer == kOutside && ++g_tipsFrames > kLoadGraceFrames))
        return g_realStartEvent(event);
    const std::uint64_t saved = *pressed;
    bool go = false;  // the count-in's presses (states 2 and 5) stay masked: a skip would split the two starts
    if (state == 3) {
        if (saved & kConfirm) g_tipsPressed = true;
        go = g_tipsPressed && (g_peer == kTipsPressed || g_peer == kFlying);
        if (go) Note("both pressed at the tips: the flight starts");
        else if (g_tipsPressed) Note("our press at the tips is held for the other player");
    }
    *pressed = go ? saved | kConfirm : saved & ~kConfirm;
    const std::uintptr_t result = g_realStartEvent(event);
    *pressed = saved;
    return result;
}

std::uint32_t ShipSum(const std::uint8_t* ship) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < kShipBytes; ++i) h = (h ^ ship[i]) * 16777619u;
    return h;
}

// shortcut: sent once per ship, so a game restarted after our pick doesn't get it; send again on a peer restart when stage 2 is real.
void ShipFrame() {
    std::uint8_t chosen = 0;
    if (!ReadAny(ExeBase() + kShipChosen, &chosen) || !chosen || !GummiMenuLinked()) return;
    const auto* const ship = reinterpret_cast<const std::uint8_t*>(ExeBase() + kShipData);
    if (g_shipSentValid && std::memcmp(ship, g_shipSent, kShipBytes) == 0) return;
    std::memcpy(g_shipSent, ship, kShipBytes);
    g_shipSentValid = true;
    ShipPacket p {kMagic, AvatarLinkBuild(), kVersion, kShipChunk, 0, 0, ShipSum(ship), {}};
    for (int i = 0; i < kShipChunks; ++i) {
        p.index = static_cast<std::uint8_t>(i);
        std::memcpy(p.data, ship + i * kChunkBytes, kChunkBytes);
        AvatarLinkSendReliable(&p, sizeof(p));
    }
    Log("gummi flight: our ship sent to the other game (sum %08X, %u parts)", p.sum, *reinterpret_cast<const std::uint16_t*>(ship + kShipParts));
}

void OnShipPacket(const char* buf) {
    ShipPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipChunk || p.index >= kShipChunks)
        return;
    if (p.sum != g_peerShipInSum) {
        g_peerShipInSum = p.sum;
        g_peerShipInHave = 0;
    }
    std::memcpy(g_peerShipIn + p.index * kChunkBytes, p.data, kChunkBytes);
    g_peerShipInHave |= 1 << p.index;
    if (g_peerShipInHave != (1 << kShipChunks) - 1) return;
    const std::uint16_t parts = *reinterpret_cast<const std::uint16_t*>(g_peerShipIn + kShipParts);
    if (ShipSum(g_peerShipIn) != p.sum || parts == 0 || parts >= kShipBytes) {  // the game walks these parts: only a whole ship
        Log("gummi flight: the other player's ship refused (sum %08X, %u parts)", p.sum, parts);
        return;
    }
    std::memcpy(g_peerShip, g_peerShipIn, kShipBytes);
    g_peerShipReady = true;
    Log("gummi flight: the other player's ship arrived (sum %08X, %u parts)", p.sum, parts);
}

bool ShipTest() {
    static const bool test = EnvInt("KH2COOP_GUMMI_SHIP_TEST", 0) == 1;
    return test;
}

void __fastcall HookFillBlueprints(std::uintptr_t heap) {
    g_realFillBlueprints(heap);
    g_otherSlot = -1;
    g_peerPoseReady = g_peerGaugeReady = false;
    g_volleyCount = g_volleyFrames = 0;
    g_showOtherHit = false;
    std::memset(g_standLockon, 0, sizeof(g_standLockon));
    if (!ShipTest() || !g_peerShipReady || !GummiMenuLinked()) return;
    for (int slot = 0; slot < 2; ++slot) {
        const std::uintptr_t blueprint = ExeBase() + kTeenyBlueprints[slot];
        std::uintptr_t data = 1;
        if (!ReadAny(blueprint + kBlueprintData, &data) || data) continue;
        g_loadBlueprint(blueprint, g_peerShip, heap);
        g_otherSlot = slot;
        Log("gummi flight: the other player's ship put in Teeny Ship slot %d", slot);
        return;
    }
    // shortcut: with both Teeny Ship slots taken the other player's ship isn't shown; replace one when stage 2 is real.
    Log("gummi flight: no free Teeny Ship slot for the other player's ship");
}

void __fastcall HookShipStats(std::uintptr_t gumiship, std::uintptr_t state) {
    std::int32_t blueprint = -1;
    if (g_otherSlot >= 0 && ReadAny(gumiship + kGumishipBlueprint, &blueprint) && blueprint == g_otherSlot + 1) {
        g_newPlayerState(g_otherState);
        state = reinterpret_cast<std::uintptr_t>(g_otherState);
    }
    g_realShipStats(gumiship, state);
}

// shortcut: uses the newest pose with no smoothing; add it if the two-PC run looks jittery.
bool ShipOut(std::uint8_t phase) { return phase == kTips || phase == kTipsPressed || phase == kFlying; }

std::uintptr_t StandSight(int i) { return reinterpret_cast<std::uintptr_t>(g_standLockon) + (kSights - kLockon) + i * kSightBytes; }
std::int32_t& SightState(std::uintptr_t sight) { return *reinterpret_cast<std::int32_t*>(sight + kSightState); }

bool GroupLocked(std::int32_t group) {
    for (int i = 0; i < kMaxPips; ++i)
        if (SightState(StandSight(i)) == kSightLocked && *reinterpret_cast<std::int32_t*>(StandSight(i) + kSightGroup) == group) return true;
    return false;
}

// Their volley, from their ship's own guns: the same calls the game makes on the player's release, at our stand-in lock-on.
void FireVolley(std::uintptr_t other) {
    const auto lockon = reinterpret_cast<std::uintptr_t>(g_standLockon);
    *reinterpret_cast<std::int32_t*>(lockon + (kPips - kLockon)) = kMaxPips;
    int free = 0, onEnemy = 0;
    for (int v = 0; v < g_volleyCount; ++v) {
        while (free < kMaxPips && SightState(StandSight(free)) != kSightFree) ++free;  // earlier sights stay until their lasers are done
        if (free == kMaxPips) break;
        const std::uintptr_t sight = StandSight(free);
        const VolleyAim& aim = g_volley[v];
        std::uint8_t gone = 1;
        const bool alive = aim.enemy && ReadAny(aim.enemy + kObjDead, &gone) && !(gone & 1);
        std::memcpy(reinterpret_cast<void*>(sight + kSightPlace), aim.place, sizeof(aim.place));
        *reinterpret_cast<std::uintptr_t*>(sight + kSightTarget) = alive ? aim.enemy : 0;  // none: the laser flies straight
        *reinterpret_cast<std::int32_t*>(sight + kSightPart) = aim.part;
        *reinterpret_cast<std::int32_t*>(sight + kSightGroup) = aim.group;
        SightState(sight) = kSightLocked;
        onEnemy += alive;
    }
    g_friendFire(other, lockon);
    for (int i = 0; i < kMaxPips && GroupLocked(kGroupMore); ++i) g_friendFireMore(other, lockon);
    int fired = 0;
    for (int i = 0; i < kMaxPips; ++i) {
        const std::uintptr_t sight = StandSight(i);
        if (SightState(sight) == kSightFiring) {
            SightState(sight) = kSightFlying;  // the game's step at 4 animates the sight's reticle, and these have none
            ++fired;
        } else if (SightState(sight) == kSightLocked) {  // no gun of theirs took it
            *reinterpret_cast<std::uintptr_t*>(sight + kSightTarget) = 0;
            SightState(sight) = kSightFree;
        }
    }
    Log("gummi flight: the other player's ship fired %d lasers (of %d sights, %d on an enemy here)", fired, g_volleyCount, onEnemy);
    g_volleyCount = 0;
}

void __fastcall HookFriendMatrix(std::uintptr_t table, std::uintptr_t ship) {
    std::int32_t slot = -1;
    const bool other = g_otherSlot >= 0 && ReadAny(ship + kFriendSlot, &slot) && slot == g_otherSlot;
    if (other && g_peerPoseReady && ShipOut(g_peer))
        for (int i = 0; i < kVectors; ++i) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[i]), g_peerPose + i * 3, 3 * sizeof(float));
    g_realFriendMatrix(table, ship);
    if (other && g_volleyCount && g_volleyFrames <= 0) FireVolley(ship);
    if (other && g_showOtherHit && g_playEffect) {
        g_showOtherHit = false;
        alignas(16) const EffectDescription hit {{0, 0, 0, 1}, {0, 0, 0, 0}, {1, 1, 1, 0}, ship + kObjMatrix, 0, 1};
        const std::uintptr_t effect = g_playEffect(ExeBase() + kEffects, kEffectHit, &hit, 1, 0, 0);
        Log("gummi flight: the other player's ship took a hit (effect %s)", effect ? "played" : "refused");
    }
}

void SendPose(std::uintptr_t ship) {
    PosePacket p {kMagic, AvatarLinkBuild(), kVersion, kShipPose, {}};
    for (int i = 0; i < kVectors * 3; ++i) {
        float value = 0.0f;
        if (!ReadAny(ship + kObjVectors[i / 3] + (i % 3) * sizeof(float), &value)) return;
        p.pose[i] = value;
    }
    AvatarLinkSendRaw(&p, sizeof(p));
}

void OnPosePacket(const char* buf) {
    PosePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipPose) return;
    for (const float v : p.pose)
        if (!std::isfinite(v) || std::fabs(v) > kPoseLimit) return;  // the game draws from these
    std::memcpy(g_peerPose, p.pose, sizeof(g_peerPose));
    g_peerPoseReady = true;
}

bool Sharing() { return ShipTest() && GummiMenuLinked() && g_otherSlot >= 0 && g_peerPoseReady && ShipOut(g_peer); }

// A bit per pip that's locked on an enemy.
std::uint32_t LitSights(std::uintptr_t ship) {
    std::int32_t pips = 0, sight = 0;
    std::uint32_t lit = 0;
    if (!ReadAny(ship + kPips, &pips)) return 0;
    for (int i = 0; i < pips && i < kMaxPips; ++i)
        if (ReadAny(ship + kSights + i * kSightBytes + kSightState, &sight) && sight == kSightLocked) lit |= 1u << i;
    return lit;
}

void SendGauge(std::uintptr_t ship) {
    GaugePacket p {kMagic, AvatarLinkBuild(), kVersion, kShipGauge, 0.0f, 0, 0, 0, 0};
    std::int32_t state = 0, pips = 0, maxHp = 0;
    float hp = 0.0f;
    if (!ReadAny(ship + kShipHp, &hp) || !ReadAny(ship + kShipMaxHp, &maxHp) || !ReadAny(ship + kLockonState, &state) ||
        !ReadAny(ship + kPips, &pips) || pips < 0 || pips > kMaxPips)
        return;
    p.hp = hp;
    p.maxHp = maxHp;
    p.lockonState = static_cast<std::uint8_t>(state);
    p.pips = static_cast<std::uint8_t>(pips);
    p.lit = LitSights(ship);
    std::uint32_t flags = 0;
    if (ReadAny(ship + kShipFlags, &flags) && (flags & kShipHitFrame)) ++g_hitsOut;
    p.hits = g_hitsOut;
    AvatarLinkSendRaw(&p, sizeof(p));
}

void OnGaugePacket(const char* buf) {
    GaugePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipGauge) return;
    if (!std::isfinite(p.hp) || p.hp < 0.0f || p.maxHp <= 0 || p.maxHp > kMaxShipHp || p.pips > kMaxPips) return;  // the game draws from these
    if (g_peerGaugeReady && p.hits != g_peerGauge.hits) g_showOtherHit = true;
    g_peerGauge = p;
    g_peerGaugeReady = true;
}

// Our gauge's sprites, once it has been built: smaller, and to the left of the game's.
void PlaceOtherGauge(std::uintptr_t gauge) {
    const auto scale = [](std::uintptr_t sprite) {
        *reinterpret_cast<std::uint32_t*>(sprite + kSpriteSeqFlags) |= kSeqScaled;
        auto* const s = reinterpret_cast<float*>(sprite + kSpriteScale);
        s[0] = kOtherGaugeScale;
        s[1] = s[2] = 1.0f;
    };
    const auto at = [gauge](std::uintptr_t offset) { return *reinterpret_cast<std::uintptr_t*>(gauge + offset); };
    const auto x = [](std::uintptr_t sprite) -> std::int32_t& { return *reinterpret_cast<std::int32_t*>(sprite + kSpriteX); };
    const auto y = [](std::uintptr_t sprite) -> std::int32_t& { return *reinterpret_cast<std::int32_t*>(sprite + kSpriteY); };
    const auto moved = [&](std::uintptr_t sprite) { x(sprite) += kOtherGaugeDx; y(sprite) += kOtherGaugeDy; };
    for (const std::uintptr_t o : kGaugeOnceSprites)
        if (const std::uintptr_t s = at(o)) { x(s) = kGaugeBaseX + kOtherGaugeDx; y(s) = kOtherGaugeDy; scale(s); }
    for (const std::uintptr_t o : kGaugeFrameSprites)
        if (const std::uintptr_t s = at(o)) { moved(s); scale(s); }
    for (const std::uintptr_t o : kGaugeOtherSprites)
        if (const std::uintptr_t s = at(o)) scale(s);
    for (int i = 0; i < kMaxPips; ++i)
        if (const std::uintptr_t s = at(kGaugePips + i * sizeof(std::uintptr_t))) { moved(s); scale(s); }
    for (const std::uintptr_t o : kGaugeMasks) {
        *reinterpret_cast<std::int32_t*>(gauge + o + kMaskX) += kOtherGaugeDx;
        *reinterpret_cast<std::int32_t*>(gauge + o + kMaskY) += kOtherGaugeDy;
        *reinterpret_cast<std::int32_t*>(gauge + o + kMaskRadius) = static_cast<std::int32_t>(kMaskRadiusFull * kOtherGaugeScale);
    }
}

void StepOtherGauge() {
    const auto gauge = reinterpret_cast<std::uintptr_t>(g_otherGauge);
    const auto stand = reinterpret_cast<std::uintptr_t>(g_standShip);
    auto& state = *reinterpret_cast<std::int32_t*>(gauge + kGaugeState);
    auto* const shipGlobal = reinterpret_cast<std::uintptr_t*>(ExeBase() + kShip);
    const std::uintptr_t ship = *shipGlobal;
    std::int32_t real = kGaugeNone;
    if (!ship || !ReadAny(ExeBase() + kGauge + kGaugeState, &real)) return;
    if (state == kGaugeNone) {
        if (real != kGaugeRunning || !g_peerGaugeReady || !Sharing()) return;
        // Once: the game makes one sprite per pip here and then walks that many, so the count can't change after this.
        *reinterpret_cast<std::int32_t*>(stand + kPips) = g_peerGauge.pips;
        *reinterpret_cast<std::uint8_t*>(gauge + kGaugeFadeDone) = 0;
        state = kGaugeBuild;
        Log("gummi flight: the other player's gauge built (max HP %d, %u pips)", g_peerGauge.maxHp, g_peerGauge.pips);
    } else if (state == kGaugeRunning && real == kGaugeFading) {
        state = kGaugeFading;
    }
    *reinterpret_cast<float*>(stand + kShipHp) = g_peerGauge.hp;
    *reinterpret_cast<std::int32_t*>(stand + kShipMaxHp) = g_peerGauge.maxHp;
    *reinterpret_cast<std::int32_t*>(stand + kLockonState) = g_peerGauge.lockonState;
    for (int i = 0; i < kMaxPips; ++i)
        *reinterpret_cast<std::int32_t*>(stand + kSights + i * kSightBytes + kSightState) = (g_peerGauge.lit >> i & 1) ? kSightLocked : 0;
    *shipGlobal = stand;
    g_realGaugeStep(gauge);
    *shipGlobal = ship;
    if (state == kGaugeRunning) PlaceOtherGauge(gauge);
}

void __fastcall HookBuildParts(std::uintptr_t gumiship, std::int32_t blueprint) {
    // The game adds every Teeny Ship's lasers to the player's lock-on; the other player's ship keeps its own count.
    const std::uintptr_t ship = Ship();
    std::int32_t counts[kPipCountFields] = {};
    bool keep = ship && g_otherSlot >= 0 && blueprint == g_otherSlot + 1;
    for (int i = 0; keep && i < kPipCountFields; ++i) keep = ReadAny(ship + kPipCounts + i * sizeof(std::int32_t), &counts[i]);
    g_realBuildParts(gumiship, blueprint);
    if (keep) std::memcpy(reinterpret_cast<void*>(ship + kPipCounts), counts, sizeof(counts));
}

void __fastcall HookGaugeStep(std::uintptr_t gauge) {
    g_realGaugeStep(gauge);
    if (gauge != ExeBase() + kGauge) return;
    StepOtherGauge();
    // The game's own sight step, as for its own lock-on: it drops a sight's enemy the frame the game marks that enemy gone.
    if (g_lockonStep) g_lockonStep(reinterpret_cast<std::uintptr_t>(g_standLockon));
}

void __fastcall HookGaugeDelete(std::uintptr_t gauge) {
    g_realGaugeDelete(gauge);
    if (gauge != ExeBase() + kGauge) return;
    const auto other = reinterpret_cast<std::uintptr_t>(g_otherGauge);
    g_realGaugeDelete(other);
    g_gaugeClear(other);
}

bool ReadPlace(std::uintptr_t obj, float* out) {
    for (int i = 0; i < 3; ++i)
        if (!ReadAny(obj + kObjVectors[0] + i * sizeof(float), &out[i])) return false;
    return true;
}

float Dist2(const float* a, const float* b) {
    return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
}

// True when the other ship is nearer: the ship's place is then theirs and `mine` holds ours, to put back after the step.
bool OtherNearer(std::uintptr_t enemy, std::uintptr_t ship, float* mine) {
    alignas(16) float at[4] = {0, 0, 0, 1}, us[4] = {0, 0, 0, 1}, them[4] = {0, 0, 0, 1};
    if (!ReadPlace(ship, mine)) return false;
    void* const place = reinterpret_cast<void*>(ship + kObjVectors[0]);
    g_objPlace(enemy, at);
    g_objPlace(ship, us);
    std::memcpy(place, g_peerPose, 3 * sizeof(float));
    g_objPlace(ship, them);
    if (Dist2(at, them) < Dist2(at, us)) return true;
    std::memcpy(place, mine, 3 * sizeof(float));
    return false;
}

bool Near(const float* a, const float* b) {
    return std::fabs(a[0] - b[0]) <= kKillRange && std::fabs(a[1] - b[1]) <= kKillRange && std::fabs(a[2] - b[2]) <= kKillRange;
}

bool EnemyKind(std::uintptr_t obj, std::int32_t* kind) {
    std::uintptr_t row = 0;
    return ReadAny(obj + kObjRow, &row) && row && ReadAny(row, kind);
}

// ---- one enemy, the same in both games ----

// Each game makes a mission's enemies in the same order, so its kind and its place in that kind's order name the same enemy in both.
// Kept across frames: an entry counts only while its object's step ran in the last kLiveFrames frames and the object isn't going
// down; the object's own age going backwards, or another kind at that address, means a new enemy took the address over.
struct Tracked {
    std::uintptr_t obj;
    std::uint32_t id;  // kind << 16 | place in that kind's order
    int frame;         // g_frame of its last step
    float age;
    bool strong;       // from its HP when first seen
    bool aimedAtOther; // in its last step
    float blend;       // Host: how its last motion was blended in
    std::uint32_t scriptDice;  // its script's number between its steps (0: not started): by roll order, so frame pacing can't part the games
};
Tracked g_tracked[kMaxTracked] = {};
int g_trackedCount = 0;
std::uint16_t g_kindCount[kMaxKindIds] = {};

Tracked* Track(std::uintptr_t obj, std::int32_t kind) {
    float age = 0;
    if (kind < 0 || kind >= kMaxKindIds || !ReadAny(obj + kObjAge, &age)) return nullptr;
    const std::uint32_t top = static_cast<std::uint32_t>(kind) << 16;
    Tracked* slot = nullptr;
    for (int i = 0; i < g_trackedCount; ++i) {
        Tracked& t = g_tracked[i];
        if (t.obj == obj) {
            if (age >= t.age && (t.id & 0xFFFF0000u) == top) {
                t.age = age;
                t.frame = g_frame;
                return &t;
            }
            slot = &t;
            break;
        }
        if (!slot && g_frame - t.frame > kTrackStaleFrames) slot = &t;
    }
    if (!slot) {
        if (g_trackedCount == kMaxTracked) return nullptr;
        slot = &g_tracked[g_trackedCount++];
    }
    std::int32_t hp = 0;
    ReadAny(obj + kEnemyHp, &hp);
    *slot = {obj, top | g_kindCount[kind]++, g_frame, age, hp >= kStrongHp, false, 0.0f};
    return slot;
}

std::uint32_t IdOf(std::uintptr_t obj) {
    for (int i = 0; i < g_trackedCount; ++i)
        if (g_tracked[i].obj == obj) return g_tracked[i].id;
    return 0;
}

// The enemy that id names here, if it's still there.
std::uintptr_t LiveEnemy(std::uint32_t id) {
    for (int i = 0; i < g_trackedCount; ++i) {
        const Tracked& t = g_tracked[i];
        std::uint32_t flags = kEnemyGoingDown;
        if (t.id == id && g_frame - t.frame <= kLiveFrames && ReadAny(t.obj + kEnemyFlags, &flags) && !(flags & kEnemyGoingDown)) return t.obj;
    }
    return 0;
}

// The same enemy at the same age rolls the same numbers in both games, even when one game is a few frames behind (exe+0x783CA0 only multiplies: it must stay odd).
std::uint32_t Dice(std::uint32_t id, float age) {
    return ((id * 0x9E3779B1u) ^ (static_cast<std::uint32_t>(age) * 0x85EBCA6Bu)) | 1u;
}

// TEMP attack diag: new objects by class
struct NewObj { std::uintptr_t vtable; std::int32_t type; int count; };
NewObj g_newObjs[48] = {};
int g_newObjCount = 0;
void CountNew(std::uintptr_t obj, std::int32_t type) {
    float age = 1.0f;
    std::uint32_t handle = 0;
    std::uintptr_t vt = 0;
    if (!GummiMenuLinked() || !ReadAny(obj + kObjAge, &age) || age != 0.0f || !ReadAny(obj, &handle)) return;
    const std::uintptr_t cls = reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(ExeBase() + 0x4AD270)(handle);
    if (!cls || !ReadAny(cls, &vt)) return;
    for (int i = 0; i < g_newObjCount; ++i)
        if (g_newObjs[i].vtable == vt && g_newObjs[i].type == type) {
            ++g_newObjs[i].count;
            return;
        }
    if (g_newObjCount < 48) g_newObjs[g_newObjCount++] = {vt, type, 1};
}

// What the Host last said about a strong enemy, by Tracked::id: no pointer into the game is kept here.
struct Follow {
    std::uint32_t id;
    std::int32_t motion, hp;
    float blend;
    float target[3];
    int frame;
    float at[3];  // where the Client's copy was put last
    bool placed;
    bool aimsAtClient;
};
Follow g_follow[kMaxStrong] = {};
int g_followCount = 0;

Follow* FreshFollow(std::uint32_t id) {
    for (int i = 0; i < g_followCount; ++i)
        if (g_follow[i].id == id) return g_frame - g_follow[i].frame <= kStrongStale ? &g_follow[i] : nullptr;
    return nullptr;
}

void __fastcall HookMotionSet(std::uintptr_t player, std::int32_t motion, float blend, float start) {
    for (int i = 0; i < g_trackedCount; ++i) {
        Tracked& t = g_tracked[i];
        if (!t.strong || t.obj + kObjMotionPlayer != player || g_frame - t.frame > kLiveFrames) continue;  // the address is only compared here
        static int stackLogs = 0;  // TEMP pick stack
        if (stackLogs < 80 && ++stackLogs) {
            void* frames[16];
            const USHORT got = RtlCaptureStackBackTrace(0, 16, frames, nullptr);
            char line[400] = {};
            int used = 0;
            for (USHORT k = 0; k < got && used < 360; ++k) {
                const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(frames[k]) - ExeBase();
                if (rva < 0x3000000) used += snprintf(line + used, sizeof(line) - used, " %llX", static_cast<unsigned long long>(rva));
            }
            Log("gummi pick %08X motion %d age %.0f stack%s", t.id, motion, t.age, line);
        }
        if (AvatarLinkIsHost()) t.blend = blend;
        break;
    }
    g_realMotionSet(player, motion, blend, start);
}

// On the Client, right after a strong enemy's own step.
void FollowHost(std::uintptr_t obj, std::uint32_t id) {
    Follow* const f = FreshFollow(id);
    if (!f) return;
    std::int32_t motion = -1, hp = 0;
    if (ReadAny(obj + kObjMotion, &motion) && motion != f->motion) ++g_statFollowMotion;  // a count only: setting it here would restart the clock a queued motion waits on (L249)
    if (!f->placed || Dist2(f->at, f->target) > kStrongSnap * kStrongSnap) {
        std::memcpy(f->at, f->target, sizeof(f->at));
    } else {
        for (int i = 0; i < 3; ++i) f->at[i] += (f->target[i] - f->at[i]) * kStrongEase;
    }
    f->placed = true;
    std::memcpy(reinterpret_cast<void*>(obj + kObjVectors[0]), f->at, sizeof(f->at));
    if (ReadAny(obj + kEnemyHp, &hp) && hp > 0 && hp != f->hp) *reinterpret_cast<std::int32_t*>(obj + kEnemyHp) = f->hp;  // not once it's shot down: the kill comes as its own message
}

void SendStrong() {
    StrongPacket p {kMagic, AvatarLinkBuild(), kVersion, kStrong, 0, {}};
    for (int i = 0; i < g_trackedCount && p.count < kMaxStrong; ++i) {
        const Tracked& t = g_tracked[i];
        std::int32_t motion = -1, hp = 0;
        float at[3];
        if (!t.strong || g_frame - t.frame > kLiveFrames || !ReadAny(t.obj + kObjMotion, &motion) || motion < 0 || !ReadAny(t.obj + kEnemyHp, &hp) || !ReadPlace(t.obj, at)) continue;
        StrongState& out = p.list[p.count++];
        out.id = t.id;
        out.motion = motion;
        out.hp = hp;
        out.blend = t.blend;
        out.aimsAtClient = t.aimedAtOther ? 1 : 0;
        std::memcpy(out.at, at, sizeof(at));
    }
    if (p.count) AvatarLinkSendRaw(&p, sizeof(p));
}

void OnStrongPacket(const char* buf) {
    StrongPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kStrong || p.count > kMaxStrong || AvatarLinkIsHost()) return;
    for (int i = 0; i < p.count; ++i) {
        const StrongState s = p.list[i];
        bool sane = s.motion >= 0 && s.motion <= kMaxMotionId && s.hp > 0 && s.hp <= kMaxHitDamage && std::isfinite(s.blend) && s.blend >= 0.0f && s.blend <= kMaxBlend && s.aimsAtClient <= 1;
        for (const float v : s.at) sane = sane && std::isfinite(v) && std::fabs(v) <= kPoseLimit;
        if (!sane) continue;
        Follow* entry = nullptr;
        Follow* old = nullptr;
        for (int j = 0; j < g_followCount && !entry; ++j) {
            if (g_follow[j].id == s.id) entry = &g_follow[j];
            else if (g_frame - g_follow[j].frame > kStrongStale) old = &g_follow[j];
        }
        if (!entry) {
            entry = old ? old : g_followCount < kMaxStrong ? &g_follow[g_followCount++] : nullptr;
            if (!entry) continue;
            entry->placed = false;
        }
        entry->id = s.id;
        entry->motion = s.motion;
        entry->hp = s.hp;
        entry->blend = s.blend;
        entry->aimsAtClient = s.aimsAtClient != 0;
        std::memcpy(entry->target, s.at, sizeof(entry->target));
        entry->frame = g_frame;
    }
}

bool SameEnemy(const PendingKill& k, std::uint32_t id, std::int32_t kind, const float* at) {
    return k.id && id ? k.id == id : k.enemyKind == kind && Near(k.place, at);
}

void DownIfKilled(std::uintptr_t enemy, std::uint32_t id) {
    std::int32_t kind = 0;
    std::uint32_t flags = kEnemyGoingDown;
    float at[3];
    if (!EnemyKind(enemy, &kind) || !ReadAny(enemy + kEnemyFlags, &flags) || (flags & kEnemyGoingDown) || !ReadPlace(enemy, at)) return;
    for (int i = 0; i < g_killCount; ++i) {
        if (!SameEnemy(g_kills[i], id, kind, at)) continue;
        g_kills[i] = g_kills[--g_killCount];
        ++g_statDone;
        // shortcut: a treasure enemy whose treasure is already gone gives orbs only to the killer; read exe+0x208240 if a score gap shows.
        std::int32_t treasure = 0;
        if (g_dropOrbs && !(flags & kEnemyNoDown) && ReadAny(enemy + kEnemyTreasure, &treasure) && !treasure) g_dropOrbs(enemy);
        g_realEnemyDown(enemy, 0);
        return;
    }
}

// Their sight's enemy in our game: the nearest of its kind (each game has its own copies, close but not at the same place).
void MatchVolley(std::uintptr_t enemy) {
    std::int32_t kind = 0;
    float at[3];
    if (!EnemyKind(enemy, &kind) || !ReadPlace(enemy, at)) return;
    for (int i = 0; i < g_volleyCount; ++i) {
        VolleyAim& aim = g_volley[i];
        const float d = Dist2(aim.place, at);
        if (aim.enemyKind == kind && (!aim.enemy || d < aim.best)) {
            aim.enemy = enemy;
            aim.best = d;
        }
    }
}

void SendVolley(std::uintptr_t player, std::uint32_t fired) {
    VolleyPacket p {kMagic, AvatarLinkBuild(), kVersion, kShipVolley, 0, {}};
    for (int i = 0; i < kMaxPips; ++i) {
        if (!(fired & (1u << i))) continue;
        const std::uintptr_t sight = player + kSights + i * kSightBytes;
        std::uintptr_t enemy = 0;
        std::int32_t kind = 0, part = 0, group = 0;
        float place[3];
        if (!ReadAny(sight + kSightTarget, &enemy) || !enemy || !EnemyKind(enemy, &kind) || !ReadAny(sight + kSightPart, &part) ||
            !ReadAny(sight + kSightGroup, &group) || part < 0 || part > 255 || group < 0 || group > kMaxGroup)
            continue;
        bool read = true;
        for (int j = 0; j < 3; ++j) read = read && ReadAny(sight + kSightPlace + j * sizeof(float), &place[j]);
        if (!read) continue;
        VolleySight& out = p.sights[p.count++];
        std::memcpy(out.place, place, sizeof(place));
        out.enemyKind = kind;
        out.part = static_cast<std::uint8_t>(part);
        out.group = static_cast<std::uint8_t>(group);
    }
    if (p.count) AvatarLinkSendReliable(&p, sizeof(p));
}

void OnVolleyPacket(const char* buf) {
    VolleyPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipVolley) return;
    if (!g_lockonStep || !Sharing() || p.count > kMaxPips) return;
    for (int i = 0; i < p.count; ++i) {
        VolleySight s;
        std::memcpy(&s, &p.sights[i], sizeof(s));
        for (const float v : s.place)
            if (!std::isfinite(v) || std::fabs(v) > kPoseLimit) return;  // the game flies lasers from these
        if (s.group > kMaxGroup) return;
        VolleyAim& aim = g_volley[i];
        std::memcpy(aim.place, s.place, sizeof(aim.place));
        aim.enemyKind = s.enemyKind;
        aim.part = s.part;
        aim.group = s.group;
        aim.enemy = 0;
        aim.best = 0.0f;
    }
    g_volleyCount = p.count;
    g_volleyFrames = kVolleyMatchFrames;
}

void __fastcall HookPlayerFire(std::uintptr_t player) {
    // The game fires both Teeny Ships on the player's release; the other player's ship fires on that player's release instead.
    std::uintptr_t* hidden = nullptr;
    for (int i = 0; i < 2 && g_otherSlot >= 0 && !hidden; ++i) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(player + kTeenyShips + i * sizeof(std::uintptr_t));
        std::int32_t slot = -1;
        if (*entry && ReadAny(*entry + kFriendSlot, &slot) && slot == g_otherSlot) hidden = entry;
    }
    const std::uintptr_t teeny = hidden ? *hidden : 0;
    const std::uint32_t lit = Sharing() ? LitSights(player) : 0;
    if (hidden) *hidden = 0;
    g_realPlayerFire(player);
    if (hidden) *hidden = teeny;
    const std::uint32_t fired = lit & ~LitSights(player);
    if (!fired) return;
    SendVolley(player, fired);
    Log("gummi flight: we fired on sights %08X; the other ship held back: %s", fired, hidden ? "yes" : "no");
}

void __fastcall HookGunTake(std::uintptr_t gun, std::uintptr_t lockon, std::uint8_t sound) {
    // For show: the laser is real and hits, but this game's enemies only lose HP to its own player's shots.
    if (lockon == reinterpret_cast<std::uintptr_t>(g_standLockon)) *reinterpret_cast<std::int32_t*>(gun + kGunPower) = 0;
    g_realGunTake(gun, lockon, sound);
}

void __fastcall HookObjectStep(std::uintptr_t obj) {
    std::int32_t type = 0;
    if (!ReadAny(obj + kObjType, &type) || (type != kEnemyTypeA && type != kEnemyTypeB)) {
        CountNew(obj, type);  // TEMP attack diag
        g_realObjectStep(obj);
        return;
    }
    const std::uintptr_t ship = Ship();
    float mine[3];
    std::int32_t kind = 0;
    Tracked* const tracked = GummiMenuLinked() && EnemyKind(obj, &kind) ? Track(obj, kind) : nullptr;
    const std::uint32_t id = tracked ? tracked->id : 0;
    const Follow* const host = tracked && tracked->strong && !AvatarLinkIsHost() ? FreshFollow(id) : nullptr;
    if (host && ship && Sharing()) {
        g_aimingAtOther = !host->aimsAtClient && ReadPlace(ship, mine);
        if (g_aimingAtOther) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), g_peerPose, 3 * sizeof(float));
    } else {
        g_aimingAtOther = ship && Sharing() && OtherNearer(obj, ship, mine);
    }
    if (tracked) tracked->aimedAtOther = g_aimingAtOther;
    ++g_statEnemies;
    if (g_aimingAtOther) ++g_statAimed;
    std::uint32_t* const random = SpawnSyncRandom();
    const std::uint32_t otherRolls = *random;
    if (tracked) *random = Dice(id, tracked->age);
    const bool scriptDice = tracked && g_scriptRandom;
    const std::uint32_t otherScriptRolls = scriptDice ? *g_scriptRandom : 0;
    const bool firstScriptStep = scriptDice && !tracked->scriptDice;  // TEMP script dice
    if (scriptDice) *g_scriptRandom = tracked->scriptDice ? tracked->scriptDice : Dice(id, 0.0f);
    const std::uint32_t scriptBefore = scriptDice ? *g_scriptRandom : 0;  // TEMP script dice
    g_realObjectStep(obj);
    if (tracked) *random = otherRolls;
    if (scriptDice) {
        tracked->scriptDice = *g_scriptRandom;
        *g_scriptRandom = otherScriptRolls;
        static int rollLogs = 0;  // TEMP script dice
        if (tracked->strong && (firstScriptStep || tracked->scriptDice != scriptBefore) && rollLogs < 600 && ++rollLogs)
            Log("gummi roll %08X age %.0f from %08X to %08X%s", id, tracked->age, scriptBefore, tracked->scriptDice, firstScriptStep ? " first" : "");
    }
    if (g_aimingAtOther) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), mine, sizeof(mine));
    g_aimingAtOther = false;
    if (tracked && tracked->strong && !AvatarLinkIsHost()) FollowHost(obj, id);
    if (g_killCount) DownIfKilled(obj, id);
    if (g_volleyCount && g_volleyFrames > 0) MatchVolley(obj);
}

const Tracked* LiveTracked(std::uintptr_t obj) {
    for (int i = 0; i < g_trackedCount; ++i)
        if (g_tracked[i].obj == obj && g_frame - g_tracked[i].frame <= kLiveFrames) return &g_tracked[i];
    return nullptr;
}

// The other player's ship as this game holds it: the gb::FRIEND in their Teeny Ship slot, kept at their place by HookFriendMatrix.
std::uintptr_t OtherShipCopy(std::uintptr_t ship) {
    for (int i = 0; i < 2 && g_otherSlot >= 0; ++i) {
        std::uintptr_t entry = 0;
        std::int32_t slot = -1;
        if (ReadAny(ship + kTeenyShips + i * sizeof(std::uintptr_t), &entry) && entry && ReadAny(entry + kFriendSlot, &slot) && slot == g_otherSlot) return entry;
    }
    return 0;
}

// Guns tick outside their enemy's step too, so a shot or beam takes our ship's place as its target here as well: the place is
// the other ship's when the gun's owner picked it in its last step. The owner is only compared with enemies that stepped in the
// last kLiveFrames frames; the gun is the game's own argument for this call.
void __fastcall HookShotAim(std::uintptr_t gun, float* start, float* dir) {
    const std::uintptr_t ship = Ship();
    std::uintptr_t owner = 0;
    std::uint32_t flags = 0;
    float mine[3];
    if (!ship || !Sharing() || !ReadAny(gun + kEnemyGunFlags, &flags) || !(flags & kGunReadsShip) || !ReadAny(gun + kEnemyGunOwner, &owner) ||
        !ReadPlace(ship, mine)) {
        g_realShotAim(gun, start, dir);
        return;
    }
    const Tracked* const t = LiveTracked(owner);
    const bool other = !g_aimingAtOther && t && t->aimedAtOther;  // inside a step that already lent it, the place is theirs
    if (other) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), g_peerPose, 3 * sizeof(float));
    g_realShotAim(gun, start, dir);
    if (other) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), mine, sizeof(mine));
    static int shotLogs = 0;  // TEMP shot aim: what the game fired
    if (shotLogs < 4000 && ++shotLogs) {
        std::uint8_t kind = 0;
        ReadAny(gun + kEnemyGunKind, &kind);
        Log("gummi shot %08X age %.0f kind %02X flags %X other %d step %d apart %.0f start %.0f %.0f %.0f dir %.3f %.3f %.3f", t ? t->id : 0u, t ? t->age : -1.0f, kind,
            flags, other ? 1 : 0, g_aimingAtOther ? 1 : 0, g_aimingAtOther ? -1.0f : std::sqrt(Dist2(mine, g_peerPose)), start[0], start[1], start[2], dir[0], dir[1], dir[2]);
    }
}

// A homing missile steers toward our own ship (exe+0x1FD150). One whose owner picked the other ship gets their ship's copy as its
// target instead: a mark only, which HookHomingStep turns back into our ship lent their place before the game reads it.
void __fastcall HookGunFire(std::uintptr_t gun, float* start, float* dir) {
    g_realGunFire(gun, start, dir);
    const std::uintptr_t ship = Ship();
    std::uint8_t kind = 0;
    std::uintptr_t owner = 0, shot = 0, vtable = 0, target = 0;
    if (!ship || !ReadAny(gun + kEnemyGunKind, &kind) || kind != kHomingKind || !ReadAny(gun + kEnemyGunOwner, &owner)) return;
    const Tracked* const t = LiveTracked(owner);
    const std::uintptr_t copy = t && t->aimedAtOther && Sharing() ? OtherShipCopy(ship) : 0;
    const bool marked = copy && ReadAny(ExeBase() + kNewestHoming, &shot) && shot && ReadAny(shot, &vtable) && vtable == ExeBase() + kHomingVtable &&
                        ReadAny(shot + kHomingTarget, &target) && target == ship;
    if (marked) *reinterpret_cast<std::uintptr_t*>(shot + kHomingTarget) = copy;
    static int launchLogs = 0;  // TEMP homing
    if (launchLogs < 400 && ++launchLogs) Log("gummi homing %08X age %.0f other %d marked %d", t ? t->id : 0u, t ? t->age : -1.0f, t && t->aimedAtOther ? 1 : 0, marked ? 1 : 0);
}

// The radius the game gives a ship for a missile's stop (exe+0x200560): half its widest side, from the ship data of the blueprint it
// was built from (exe+0x52E520). The other player's ship is the one in Teeny Ship slot g_otherSlot; 0 when there's none.
float OtherShipRadius() {
    std::uintptr_t data = 0;
    std::uint64_t head = 0;  // +2 u16 the three sides in blocks (4 bits each), +4 +5 +6 s8 what each side gives back
    if (g_otherSlot < 0 || !ReadAny(ExeBase() + kTeenyBlueprints[g_otherSlot] + kBlueprintData, &data) || !data || !ReadAny(data, &head)) return 0.0f;
    float side[3];
    for (int i = 0; i < 3; ++i)
        side[i] = static_cast<float>(static_cast<int>((head >> (16 + 4 * i)) & 0xF) - std::abs(static_cast<int>(static_cast<std::int8_t>(head >> (32 + 8 * i))))) * kShipBlock;
    // the game's pick, kept as it is: x when it's strictly the widest, else y when it beats x and z, else z
    const float widest = side[0] > side[1] && side[0] > side[2] ? side[0] : (side[0] < side[1] && side[2] < side[1] ? side[1] : side[2]);
    return widest * 0.5f;
}

// Kept across steps: the mark counts only while it is the copy listed in our ship's Teeny Ship entries now; otherwise the missile
// goes back to our ship. The game never reads the copy through it.
void TrackHoming(std::uintptr_t shot, bool marked) {  // TEMP homing: its place, life left (+0x78) and distance to each ship after each step
    static int trackLogs = 0;
    float at[3], mine[3], life = 0;
    std::uint8_t done = 0;
    const std::uintptr_t ship = Ship();
    if (trackLogs < 6000 && ship && ReadPlace(shot, at) && ReadPlace(ship, mine) && ReadAny(shot + 0x78, &life) && ReadAny(shot + kHomingDone, &done) && ++trackLogs)
        Log("gummi track %llX %s life %.2f done %d at %.0f %.0f %.0f ours %.0f theirs %.0f apart %.0f", static_cast<unsigned long long>(shot), marked ? "other" : "own", life, done,
            at[0], at[1], at[2], std::sqrt(Dist2(at, mine)), std::sqrt(Dist2(at, g_peerPose)), std::sqrt(Dist2(mine, g_peerPose)));
}

bool __fastcall HookHomingStep(std::uintptr_t shot) {
    auto* const target = reinterpret_cast<std::uintptr_t*>(shot + kHomingTarget);
    const std::uintptr_t ship = Ship();
    const std::uintptr_t mark = *target;
    float mine[3];
    if (!ship || !mark || mark == ship) {
        const bool flying = g_realHomingStep(shot);
        if (mark) TrackHoming(shot, false);
        return flying;
    }
    *target = ship;
    if (mark != OtherShipCopy(ship) || !Sharing() || g_aimingAtOther || !ReadPlace(ship, mine)) return g_realHomingStep(shot);  // back on our ship
    std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), g_peerPose, 3 * sizeof(float));
    const bool flying = g_realHomingStep(shot);
    std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), mine, sizeof(mine));
    *target = mark;
    TrackHoming(shot, true);
    return flying;
}

// The game's test for a missile chasing our ship: it hurts the ship it touches, and stops steering within 400 of the ship's sphere.
// One marked for the other ship can still hurt ours (your pick, 2026-11-10), but our ship doesn't stop it: the game's 400 test is
// done again on their ship, its radius and its place as the game reads it (our ship lent their place). The mark counts only while
// it is the copy listed now, as in HookHomingStep.
bool __fastcall HookHomingHit(std::uintptr_t shot, std::uintptr_t a2, const float* centre) {
    const std::uintptr_t ship = Ship();
    std::uintptr_t mark = 0;
    std::uint8_t done = 1;
    if (!ship || !centre || !Sharing() || g_aimingAtOther || !ReadAny(shot + kHomingTarget, &mark) || !mark || mark == ship || mark != OtherShipCopy(ship) ||
        !ReadAny(shot + kHomingDone, &done))
        return g_realHomingHit(shot, a2, centre);
    if (g_realHomingHit(shot, a2, centre)) return true;  // it hit our ship
    float mine[3], theirs[4] = {};
    if (done || !ReadPlace(ship, mine)) return false;
    *reinterpret_cast<std::uint8_t*>(shot + kHomingDone) = 0;  // our ship doesn't stop it
    std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), g_peerPose, 3 * sizeof(float));
    g_objPlace(ship, theirs);
    std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), mine, sizeof(mine));
    const float reach = kHomingNear + OtherShipRadius();
    if (Dist2(centre, theirs) < reach * reach) {
        *reinterpret_cast<std::uint8_t*>(shot + kHomingDone) = 1;
        static int otherLogs = 0;  // TEMP homing
        if (otherLogs < 400 && ++otherLogs) Log("gummi homing stop other radius %.0f", reach - kHomingNear);
    }
    return false;
}

// These are told apart by size.
static_assert(sizeof(StrongPacket) != sizeof(Packet) && sizeof(StrongPacket) != sizeof(ShipPacket) && sizeof(StrongPacket) != sizeof(PosePacket) && sizeof(StrongPacket) != sizeof(KillPacket) &&
              sizeof(StrongPacket) != sizeof(HitPacket) && sizeof(StrongPacket) != sizeof(ScorePacket) && sizeof(StrongPacket) != sizeof(GaugePacket) && sizeof(StrongPacket) != sizeof(VolleyPacket));

void SendHits() {
    if (!g_hitCount) return;
    HitPacket p {kMagic, AvatarLinkBuild(), kVersion, kHit, static_cast<std::uint8_t>(g_hitCount), {}};
    std::memcpy(p.hits, g_hits, g_hitCount * sizeof(HitEntry));
    AvatarLinkSendReliable(&p, sizeof(p));
    g_hitCount = g_hitFrames = 0;
}

// A hit that doesn't shoot it down also lands on the other game's copy; the last hit goes as the kill message instead.
std::int32_t __fastcall HookEnemyHit(std::uintptr_t enemy, std::uintptr_t attack) {
    std::int32_t before = 0, after = 0;
    const bool had = ReadAny(enemy + kEnemyHp, &before);
    const std::int32_t result = g_realEnemyHit(enemy, attack);
    if (!had || before <= 0 || !ReadAny(enemy + kEnemyHp, &after) || after <= 0 || after >= before || !GummiMenuLinked() || Mine() != kFlying) return result;
    const std::uint32_t id = IdOf(enemy);
    if (!id) return result;
    ++g_statHitsSent;
    for (int i = 0; i < g_hitCount; ++i)
        if (g_hits[i].id == id) {
            g_hits[i].damage += before - after;
            return result;
        }
    if (g_hitCount == kMaxHits) SendHits();
    g_hits[g_hitCount++] = {id, before - after};
    return result;
}

void OnHitPacket(const char* buf) {
    HitPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kHit || p.count > kMaxHits) return;
    for (int i = 0; i < p.count; ++i) {
        const HitEntry hit = p.hits[i];
        const std::uintptr_t enemy = hit.damage > 0 && hit.damage <= kMaxHitDamage ? LiveEnemy(hit.id) : 0;
        std::int32_t hp = 0;
        std::uint32_t flags = 0;
        if (!enemy || !ReadAny(enemy + kEnemyHp, &hp) || hp <= 0 || !ReadAny(enemy + kEnemyFlags, &flags)) {
            ++g_statHitsLost;
            continue;
        }
        *reinterpret_cast<std::int32_t*>(enemy + kEnemyHp) = hp > hit.damage ? hp - hit.damage : 1;  // never the last point: that comes as a kill
        ++g_statHitsDone;
        if (flags & kEnemyNoFlash) continue;
        *reinterpret_cast<float*>(enemy + kEnemyFlash) = kHitFlash;
        *reinterpret_cast<std::uint32_t*>(enemy + kObjTint) = kHitTint;
        *reinterpret_cast<std::uint32_t*>(enemy + kEnemyFlags) = flags | kEnemyFlashed;
    }
}

void __fastcall HookEnemyContact(std::uintptr_t enemy) {
    if (!g_aimingAtOther) g_realEnemyContact(enemy);  // touching the other ship's copy does nothing here
}

// Both games put it down and each also got the other's points: each takes off the lower of the two, so both end on the same number.
void TakeOffLower(std::int32_t a, std::int32_t b) {
    const std::int32_t lower = a < b ? a : b;
    if (a != b) ++g_statDiffer;
    if (lower > 0 && g_realAddScore) g_realAddScore(-lower);
    ++g_statBoth;
}

void __fastcall HookEnemyDown(std::uintptr_t enemy, std::uint8_t award) {
    KillPacket p {kMagic, AvatarLinkBuild(), kVersion, kKill, 0, 0, {}, 0};
    std::int32_t type = 0, before = 0, after = 0;
    const bool tell = award && ShipTest() && GummiMenuLinked() && Mine() == kFlying && ReadAny(enemy + kObjType, &type) &&
                      (type == kEnemyTypeA || type == kEnemyTypeB) && EnemyKind(enemy, &p.enemyKind) && ReadPlace(enemy, p.place) &&
                      ReadAny(ExeBase() + kScore, &before);
    p.id = tell ? IdOf(enemy) : 0;
    g_realEnemyDown(enemy, award);
    if (!tell || !ReadAny(ExeBase() + kScore, &after)) return;
    p.points = after - before;
    AvatarLinkSendReliable(&p, sizeof(p));
    ++g_statSent;
    for (int i = 0; i < g_killCount; ++i) {
        if (!SameEnemy(g_kills[i], p.id, p.enemyKind, p.place)) continue;
        // Their report got here first and we scored it anyway: the same correction OnKillPacket makes on their side.
        TakeOffLower(p.points, g_kills[i].points);
        g_kills[i] = g_kills[--g_killCount];
        return;
    }
    if (g_ownCount >= kMaxKills) {
        ++g_statFull;
        return;
    }
    g_own[g_ownCount++] = {p.id, p.enemyKind, {p.place[0], p.place[1], p.place[2]}, kOwnKillFrames, p.points};
    if (g_ownCount > g_statPeak) g_statPeak = g_ownCount;
}

void __fastcall HookAddScore(std::int32_t points) {
    std::int32_t before = 0, after = 0;
    const bool had = ReadAny(ExeBase() + kScore, &before);
    const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase();
    g_realAddScore(points);
    if (!had || !ReadAny(ExeBase() + kScore, &after) || after <= before || !ShipTest() || !GummiMenuLinked()) return;
    if (from == kOrbScoreReturns[0] || from == kOrbScoreReturns[1]) g_statOrbPoints += after - before;  // an orb's score stays with whoever took it
    else g_scoreOut += after - before;
}

void __fastcall HookFriendAbsorb(std::uintptr_t ship, std::uintptr_t shot) {
    std::int32_t slot = -1;
    if (g_otherSlot >= 0 && ReadAny(ship + kFriendSlot, &slot) && slot == g_otherSlot) return;  // only their copy: no score here
    g_realFriendAbsorb(ship, shot);
}

void OnKillPacket(const char* buf) {
    KillPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kKill || p.points < 0 || p.points > kScoreLimit) return;
    for (const float v : p.place)
        if (!std::isfinite(v)) return;
    for (int i = 0; i < g_ownCount; ++i) {
        if (!SameEnemy(g_own[i], p.id, p.enemyKind, p.place)) continue;
        TakeOffLower(g_own[i].points, p.points);
        g_own[i] = g_own[--g_ownCount];
        return;
    }
    if (g_killCount >= kMaxKills) {
        ++g_statFull;
        return;
    }
    PendingKill& k = g_kills[g_killCount++];
    k.id = p.id;
    k.enemyKind = p.enemyKind;
    std::memcpy(k.place, p.place, sizeof(k.place));
    k.frames = kKillFrames;
    k.points = p.points;
}

void OnScorePacket(const char* buf) {
    ScorePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kScoreAdd) return;
    if (p.points > 0 && p.points <= kScoreLimit && g_realAddScore) g_realAddScore(p.points);
}

void SharedFrame(Phase now) {
    for (int i = g_ownCount - 1; i >= 0; --i)
        if (--g_own[i].frames <= 0) g_own[i] = g_own[--g_ownCount];
    for (int i = g_killCount - 1; i >= 0; --i)
        if (--g_kills[i].frames <= 0) {
            static int logged = 0;
            if (logged < kMaxLostKillLogs && ++logged) {  // one run lost 39 kills and nothing has shown why: -1 = this game never had it, 90 or more = gone before the message came
                int since = -1;
                for (int t = 0; t < g_trackedCount; ++t)
                    if (g_tracked[t].id == g_kills[i].id) since = g_frame - g_tracked[t].frame;
                Log("gummi kill lost %08X kind %X at %.0f %.0f: last step %d frames ago", g_kills[i].id, g_kills[i].enemyKind, g_kills[i].place[0], g_kills[i].place[1], since);
            }
            g_kills[i] = g_kills[--g_killCount];
            ++g_statLost;
        }
    if (now == kOutside || now == kTips) {
        g_killCount = g_ownCount = g_scoreOut = g_scoreFrames = g_hitCount = g_hitFrames = 0;
        if (g_trackedCount) std::memset(g_kindCount, 0, sizeof(g_kindCount));
        g_trackedCount = g_followCount = 0;
        return;
    }
    if (g_hitCount && ++g_hitFrames >= kHitEvery) SendHits();
    if (AvatarLinkIsHost() && Sharing() && g_frame % kStrongEvery == 0) SendStrong();
    if (g_scoreOut && ++g_scoreFrames >= kScoreEvery) {
        ScorePacket p {kMagic, AvatarLinkBuild(), kVersion, kScoreAdd, g_scoreOut};
        AvatarLinkSendReliable(&p, sizeof(p));
        g_scoreOut = g_scoreFrames = 0;
    }
    static const bool attackDiag = EnvInt("KH2COOP_GUMMI_ENEMY_LOG", 0) == 1;  // TEMP attack diag
    if (attackDiag && g_frame % 30 == 0) {
        for (int i = 0; i < g_newObjCount; ++i) Log("gummi new type %d exe+%llX: %d", g_newObjs[i].type, static_cast<unsigned long long>(g_newObjs[i].vtable - ExeBase()), g_newObjs[i].count);
        g_newObjCount = 0;
    }
    if (g_frame % kStatsEvery == 0 && g_statEnemies) {
        Log("gummi flight: enemy steps aimed at the other ship %d of %d; kills sent %d, done here %d, in both %d, lost %d; list full %d, most in it %d, points differed %d; orb points kept here %d; hits sent %d, done here %d, not found %d; enemies known %d; strong: steps on another motion than the Host's %d", g_statAimed, g_statEnemies, g_statSent, g_statDone, g_statBoth, g_statLost, g_statFull, g_statPeak, g_statDiffer, g_statOrbPoints, g_statHitsSent, g_statHitsDone, g_statHitsLost, g_trackedCount, g_statFollowMotion);
        g_statAimed = g_statEnemies = 0;
    }
}

void GodFrame() {
    static const bool god = EnvInt("KH2COOP_GOD", 0) == 1;
    if (!god) return;
    const std::uintptr_t ship = Ship();
    std::int32_t max = 0;
    float hp = 0.0f;
    if (!ship || !ReadAny(ship + kShipMaxHp, &max) || max <= 0 || !ReadAny(ship + kShipHp, &hp) ||
        hp >= static_cast<float>(max))
        return;
    // shortcut: topped up once a frame, so one hit above full HP would still count; hook the damage if a bench sees it.
    *reinterpret_cast<float*>(ship + kShipHp) = static_cast<float>(max);
    static int logged = 0;
    if (logged < 5 && ++logged) Log("gummi flight: GOD MODE (KH2COOP_GOD=1): a hit took the ship to %.0f of %d, topped up", hp, max);
}

}  // namespace

void GummiFlightInit() {
    g_on = HookFunction(kListInput, kListInputBytes, sizeof(kListInputBytes), reinterpret_cast<void*>(HookListInput),
                        reinterpret_cast<void**>(&g_realListInput), "Gummi flight menu list input") &&
           HookFunction(kStartEvent, kStartEventBytes, sizeof(kStartEventBytes), reinterpret_cast<void*>(HookStartEvent),
                        reinterpret_cast<void**>(&g_realStartEvent), "Gummi flight start event");
    if (g_on && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kLoadBlueprint), kLoadBlueprintBytes, sizeof(kLoadBlueprintBytes)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(ExeBase() + kNewPlayerState), kNewPlayerStateBytes, sizeof(kNewPlayerStateBytes)) == 0) {
        g_loadBlueprint = reinterpret_cast<PFN_LoadBlueprint>(ExeBase() + kLoadBlueprint);
        g_newPlayerState = reinterpret_cast<PFN_NewPlayerState>(ExeBase() + kNewPlayerState);
        // The blueprints last: without the other two hooks no second ship is built at all.
        (void)(HookFunction(kShipStats, kShipStatsBytes, sizeof(kShipStatsBytes), reinterpret_cast<void*>(HookShipStats),
                            reinterpret_cast<void**>(&g_realShipStats), "Gummi flight ship stats") &&
               HookFunction(kFriendMatrix, kFriendMatrixBytes, sizeof(kFriendMatrixBytes), reinterpret_cast<void*>(HookFriendMatrix),
                            reinterpret_cast<void**>(&g_realFriendMatrix), "Gummi flight friend matrix") &&
               HookFunction(kFillBlueprints, kFillBlueprintsBytes, sizeof(kFillBlueprintsBytes), reinterpret_cast<void*>(HookFillBlueprints),
                            reinterpret_cast<void**>(&g_realFillBlueprints), "Gummi flight blueprints"));
    }
    // A test so far, like the second ship; the object step last, since aiming needs the contact guard.
    if (g_on && ShipTest() && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kObjPlace), kObjPlaceBytes, sizeof(kObjPlaceBytes)) == 0) {
        g_objPlace = reinterpret_cast<PFN_ObjPlace>(ExeBase() + kObjPlace);
        (void)(HookFunction(kEnemyDown, kEnemyDownBytes, sizeof(kEnemyDownBytes), reinterpret_cast<void*>(HookEnemyDown),
                            reinterpret_cast<void**>(&g_realEnemyDown), "Gummi flight enemy down") &&
               HookFunction(kAddScore, kAddScoreBytes, sizeof(kAddScoreBytes), reinterpret_cast<void*>(HookAddScore),
                            reinterpret_cast<void**>(&g_realAddScore), "Gummi flight score") &&
               HookFunction(kFriendAbsorb, kFriendAbsorbBytes, sizeof(kFriendAbsorbBytes), reinterpret_cast<void*>(HookFriendAbsorb),
                            reinterpret_cast<void**>(&g_realFriendAbsorb), "Gummi flight friend absorb") &&
               HookFunction(kEnemyContact, kEnemyContactBytes, sizeof(kEnemyContactBytes), reinterpret_cast<void*>(HookEnemyContact),
                            reinterpret_cast<void**>(&g_realEnemyContact), "Gummi flight enemy contact") &&
               HookFunction(kEnemyHit, kEnemyHitBytes, sizeof(kEnemyHitBytes), reinterpret_cast<void*>(HookEnemyHit),
                            reinterpret_cast<void**>(&g_realEnemyHit), "Gummi flight enemy hit") &&
               HookFunction(kMotionSet, kMotionSetBytes, sizeof(kMotionSetBytes), reinterpret_cast<void*>(HookMotionSet),
                            reinterpret_cast<void**>(&g_realMotionSet), "Gummi flight motion set") &&
               HookFunction(kObjectStep, kObjectStepBytes, sizeof(kObjectStepBytes), reinterpret_cast<void*>(HookObjectStep),
                            reinterpret_cast<void**>(&g_realObjectStep), "Gummi flight object step") &&
               HookFunction(kShotAim, kShotAimBytes, sizeof(kShotAimBytes), reinterpret_cast<void*>(HookShotAim),
                            reinterpret_cast<void**>(&g_realShotAim), "Gummi flight shot aim") &&
               HookFunction(kGunFire, kGunFireBytes, sizeof(kGunFireBytes), reinterpret_cast<void*>(HookGunFire),
                            reinterpret_cast<void**>(&g_realGunFire), "Gummi flight gun fire") &&
               HookFunction(kHomingStep, kHomingStepBytes, sizeof(kHomingStepBytes), reinterpret_cast<void*>(HookHomingStep),
                            reinterpret_cast<void**>(&g_realHomingStep), "Gummi flight homing step") &&
               HookFunction(kHomingHit, kHomingHitBytes, sizeof(kHomingHitBytes), reinterpret_cast<void*>(HookHomingHit),
                            reinterpret_cast<void**>(&g_realHomingHit), "Gummi flight homing hit"));
    }
    if (g_on && ShipTest() && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kGaugeCtor), kGaugeCtorBytes, sizeof(kGaugeCtorBytes)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(ExeBase() + kGaugeClear), kGaugeClearBytes, sizeof(kGaugeClearBytes)) == 0) {
        reinterpret_cast<PFN_Gauge>(ExeBase() + kGaugeCtor)(reinterpret_cast<std::uintptr_t>(g_otherGauge));
        g_gaugeClear = reinterpret_cast<PFN_Gauge>(ExeBase() + kGaugeClear);
        g_gaugeClear(reinterpret_cast<std::uintptr_t>(g_otherGauge));
        HookFunction(kBuildParts, kBuildPartsBytes, sizeof(kBuildPartsBytes), reinterpret_cast<void*>(HookBuildParts),
                     reinterpret_cast<void**>(&g_realBuildParts), "Gummi flight build parts");
        // The delete first: a gauge that could be made but never deleted would outlive the flight's sprites.
        (void)(HookFunction(kGaugeDelete, kGaugeDeleteBytes, sizeof(kGaugeDeleteBytes), reinterpret_cast<void*>(HookGaugeDelete),
                            reinterpret_cast<void**>(&g_realGaugeDelete), "Gummi flight gauge delete") &&
               HookFunction(kGaugeStep, kGaugeStepBytes, sizeof(kGaugeStepBytes), reinterpret_cast<void*>(HookGaugeStep),
                            reinterpret_cast<void**>(&g_realGaugeStep), "Gummi flight gauge step"));
        // The release last: until every other piece is in, the other ship keeps firing the old way.
        if (g_realGaugeStep && g_realGaugeDelete && g_realObjectStep &&
            std::memcmp(reinterpret_cast<const void*>(ExeBase() + kFriendFire), kFriendFireBytes, sizeof(kFriendFireBytes)) == 0 &&
            std::memcmp(reinterpret_cast<const void*>(ExeBase() + kFriendFireMore), kFriendFireMoreBytes, sizeof(kFriendFireMoreBytes)) == 0 &&
            std::memcmp(reinterpret_cast<const void*>(ExeBase() + kLockonStep), kLockonStepBytes, sizeof(kLockonStepBytes)) == 0 &&
            HookFunction(kGunTake, kGunTakeBytes, sizeof(kGunTakeBytes), reinterpret_cast<void*>(HookGunTake),
                         reinterpret_cast<void**>(&g_realGunTake), "Gummi flight gun take") &&
            HookFunction(kPlayerFire, kPlayerFireBytes, sizeof(kPlayerFireBytes), reinterpret_cast<void*>(HookPlayerFire),
                         reinterpret_cast<void**>(&g_realPlayerFire), "Gummi flight player fire")) {
            g_friendFire = reinterpret_cast<decltype(g_friendFire)>(ExeBase() + kFriendFire);
            g_friendFireMore = reinterpret_cast<decltype(g_friendFireMore)>(ExeBase() + kFriendFireMore);
            g_lockonStep = reinterpret_cast<PFN_Gauge>(ExeBase() + kLockonStep);
        }
        if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kPlayEffect), kPlayEffectBytes, sizeof(kPlayEffectBytes)) == 0)
            g_playEffect = reinterpret_cast<decltype(g_playEffect)>(ExeBase() + kPlayEffect);
        if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kDropOrbs), kDropOrbsBytes, sizeof(kDropOrbsBytes)) == 0)
            g_dropOrbs = reinterpret_cast<decltype(g_dropOrbs)>(ExeBase() + kDropOrbs);
        if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kScriptRoll), kScriptRollBytes, sizeof(kScriptRollBytes)) == 0)
            g_scriptRandom = reinterpret_cast<std::uint32_t*>(ExeBase() + kScriptRandom);
    }
    Log("gummi flight: %s", g_on ? "on: both players' flights start, pause and end together" : "off (a hook failed)");
}

bool GummiFlightFlying() {
    const std::uintptr_t ship = Ship();
    return ship && ShipState(ship) == kFlyingState;
}

void GummiFlightFrame() {
    GodFrame();
    if (!g_on) return;
    ShipFrame();
    const Phase now = Mine();
    SharedFrame(now);
    if (g_volleyFrames > 0) --g_volleyFrames;
    if (now == kTips && g_mine != kTips && g_mine != kTipsPressed && ShipTest() && GummiMenuLinked())
        *reinterpret_cast<float*>(ExeBase() + kPlaceAcrossRail) = AvatarLinkIsHost() ? -kStartApart : kStartApart;
    if (ShipOut(now) && ShipTest() && GummiMenuLinked()) {
        SendPose(Ship());
        SendGauge(Ship());
    }
    if (now != g_mine) {
        Log("gummi flight: we are %s (the other player: %s)", kPhaseName[now], kPhaseName[g_peer]);
        g_note = nullptr;
    }
    if (now != kTips && now != kTipsPressed) {
        g_tipsPressed = false;
        g_tipsFrames = 0;
    }
    if (now != kAtList) g_pick = -1;
    if (now == kAtList && PauseSyncOpenMenu() == kMenuResults && !g_clearSent && GummiMenuLinked()) {
        g_clearSent = true;
        Send(kCleared, 0);
    }
    if (g_peerCleared && now == kAtList) {  // not before: our own tally, if any, still tells its player what it opened
        g_peerCleared = false;
        GummiMenuCountClear();
    }
    if (g_quit && now == kFlying) {
        g_quit = false;
        *reinterpret_cast<std::uint32_t*>(Ship() + kShipFlags) |= kGoingDown;
    }
    if (now == kTips || now == kOutside) g_restart = g_quit = g_clearSent = g_peerCleared = false;
    if (!GummiMenuLinked()) g_peer = kOutside;
    else if (now != g_mine || ++g_frame % kStatusEvery == 0) Send(kStatus, now);
    g_mine = now;
}

void GummiFlightOnPacket(const char* buf, int n) {
    if (g_on && n == static_cast<int>(sizeof(ShipPacket))) {
        OnShipPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(PosePacket))) {
        OnPosePacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(KillPacket))) {
        OnKillPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(ScorePacket))) {
        OnScorePacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(GaugePacket))) {
        OnGaugePacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(VolleyPacket))) {
        OnVolleyPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(HitPacket))) {
        OnHitPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(StrongPacket))) {
        OnStrongPacket(buf);
        return;
    }
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.kind == kStatus) {
        if (p.arg >= kPhaseCount) return;
        if (p.arg != g_peer) Log("gummi flight: the other player is %s", kPhaseName[p.arg]);
        g_peer = p.arg;
    } else if (p.kind == kPick && !AvatarLinkIsHost() && p.arg <= kLastListItem && Mine() == kAtList) {
        Log("gummi flight: the Host picked item %d", p.arg);
        g_pick = p.arg;
    } else if (p.kind == kRestart && Mine() != kOutside) {
        Log("gummi flight: the other player's Retry restarts us");
        g_restart = true;
    } else if (p.kind == kCleared && Mine() != kOutside) {
        Log("gummi flight: the other player cleared the mission");
        g_peerCleared = true;
    }
}

}  // namespace kh2coop
