#pragma once
// Game addresses, declared once (docs/DEBT_REVIEW.md step 2.1), each with its VERIFIED_OFFSETS name. Others move
// here as their files get touched.

#include <cstdint>

namespace kh2coop::game {

// An effect holder (an actor's +0x80, a Limit's +0x18, one of ours): 0x30 bytes, +0 its PAX (L97, L259, L283).
constexpr std::uintptr_t kHolderZero = 0x2C4050;   // (holder): zeroes it, all but its list link +0x28
constexpr std::uintptr_t kHolderFill = 0x2C6320;   // (holder, pax, 0): fills it, links it at the list's tail unchecked
constexpr std::uintptr_t kHolderKill = 0x2C6490;   // (holder): kills each of its effects, frees their records
constexpr std::uintptr_t kHolderEnd = 0x2C40C0;    // (holder): if listed: kills, unlinks, +0 = 0
constexpr std::uintptr_t kEffectStart = 0x3B6670;  // (actor, effect id, u8, u32, attach): one of actor+0x80's effects
constexpr std::uintptr_t kEffectStartPlain = 0x3B6630;  // (actor, effect id, a, b): one of actor+0x80's, no owner (1/21, F496)
constexpr std::uintptr_t kFxPlace = 0x3B50F0;  // (actor, attach code, out matrix) -> float: where an effect goes (L298)
constexpr std::uintptr_t kWeaponFxPlace = 0x3D7600;  // (actor, slot, out): kFxPlace on actor+0xD60 + 8 x slot, unchecked (L298)
constexpr std::uintptr_t kStopEffect = 0x2BE8A0;  // (effect instance): hard stop (+0xC |= 0x10001, its particle system
                                                  // +0x30 deleted, its sound ended; L13)
constexpr std::uintptr_t kEffectMode = 0xB63628;  // u32: the effect system's mode, 1 in the field (F292, L37)
constexpr std::uintptr_t kHolderEffectStart = 0x2C6B30;  // (holder, effect id, a, b, c, owner) -> effect; *holder unchecked (L325)
constexpr std::uintptr_t kFrameStep = 0x717480;  // f32: the step the game's timers count down by each frame (exe+0x3D68B0)
constexpr std::uintptr_t kSystemFx = 0x2AE7508;  // the holder of 00effect.bar's effects: the item sparkles (L324, L325)

// The party and summons.
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;  // u64[2]: the party members' actors (a summon takes one, F629)
constexpr std::uintptr_t kSummonSlots = 0x2A24EE8;  // u64[2]: the summon's actors (F629, L1506)
constexpr std::uintptr_t kSummonState = 0x2A24EDC;  // u32: 1 through the entry, 2 a summon is out (F629, L1508)
constexpr std::uintptr_t kPartyBrain = 0x1B15F0;    // (actor): a party-class actor's think and scripts (L1515)

// An actor's motion: +0x180 the motion it plays, +0x2B8 the other file's motion set it plays from (a capture source,
// 0 = its own). A motion set: +4 s32 its entry count, entry n at +0x10 + 16 n with its handle at +8 (F726, L1593).
constexpr std::uintptr_t kActorMotion = 0x180, kActorMotionBank = 0x2B8;
// (actor + 0x158, motion id, float blend frames, float start frame): plays the actor's own motion; looks it up only when
// the controller's file (+0x158 +0) is set (F564; older files keep their own copy: kSetMotion).
constexpr std::uintptr_t kSetMotion = 0x3C86A0;
// An actor's collision data (a pointer): +0 s32 its row count, +4 its default group mask, rows from +0x40 of 0x14
// bytes: +0 u8 group, +2 u16 command, +4 u8 kind (5 an RC, 14 an RC whose command is actor+0xCF0) (F727, L1581).
constexpr std::uintptr_t kActorCollision = 0xAB8;
// An actor's script collision-group bits, 1 << group: on (1/52), off (1/53); 1/54 clears both (F725, L1575).
constexpr std::uintptr_t kActorGroupsOn = 0x5A8, kActorGroupsOff = 0x5AC;
// The per-enemy RC list's checks (exe+0x407DE0, L1605), both read only: (actor) true = left out of the list;
// (actor, command, group) true = that command of that collision group is on (Rodeo only while the Living Bone idles, F748).
constexpr std::uintptr_t kRcListSkips = 0x3BA4C0, kRcCommandOn = 0x3BA600;

// The object table.
constexpr std::uintptr_t kObjectEntry = 0x3DFEB0;  // (object id) -> its 0x60-byte row (+0x54 u8 its Capacity weight), 0 = none

// Stats.
constexpr std::uintptr_t kItemRecord = 0x3E39A0;  // (item) -> its row in 03system's item table, 0 = no such item (F71)
constexpr std::uintptr_t kAddMp = 0x3C0CD0;  // (stat block, mp, flag): an item's MP, with the MP Charge rule (L326)

// Story cutscenes (F97, L304, L305).
constexpr std::uintptr_t kSceneActors = 0xB653E0;   // -> 0x60 rows of 40 bytes {s32 object, s32 actor id, u32 handle, ...}
constexpr std::uintptr_t kSceneEffects = 0xB653C8;  // -> 0x80 rows of 0x50 bytes {+0 pax, +4 effect id, +0x4C s16 tied actor row}

// The player's lock-on object: every player-class actor's +0xDC8 points at it (the constructor exe+0x3A7A40), the
// friend's copy's too; +4 its target info {u32 actor handle, u32 part, u32 handle, s32 -1} (F596).
constexpr std::uintptr_t kPlayerLockOn = 0x2A11208;

// Frozen entity groups (u32): 0 = none, 3 during a cutscene (event); an Examine task sets bit 0, a script's
// control_off bits 0 and 1 (L1554).
constexpr std::uintptr_t kFrozenGroups = 0x2A171E8;
// u64: the actor list's head, our Sora in the field (older files keep their own copy: kListHead, kPlayerActor, kSoraPtr).
constexpr std::uintptr_t kListHead = 0x2A171C8;
// u8: 0 while a room loads and in Gummi mode, 1 in the field and on the World Map (older files keep their own copy).
constexpr std::uintptr_t kInField = 0x9BA8D0;

// Screen fades. An object's +0x108 is its state (4 = fully out), +0x100 white (L1537).
constexpr std::uintptr_t kScreenFade = 0xABB3B0;  // the fade room changes and scenes use (what exe+0x156DC0() returns)
constexpr std::uintptr_t kFadeIn = 0x156C90;      // (fade, frames, white)
constexpr std::uintptr_t kFadeOut = 0x156CE0;     // (fade, frames, white)

// The chest item window (`dk::TREASURE_INFO`, a pointer): +0x20 state (1 idle), +0x30 / +0x34 its 8-box queue's write
// / read index, the boxes from +0x40 (F711).
constexpr std::uintptr_t kTreasureInfo = 0xABC3C0;
// Its one icon slot: a box shows only once the slot holds its own icon, and each icon load frees the one in it (F709,
// F710).
constexpr std::uintptr_t kIconState = 0x2A11880;  // u32: 0 ready, 1 loading, 2 a box holds it
constexpr std::uintptr_t kIconLoaded = 0x74ACF0;  // s32: the icon in the slot, -1 none
constexpr std::uintptr_t kEventTreasureBox = 0xABC3B8;  // the event treasure window's box while it lives, else 0 (L1557)

// Menus.
constexpr std::uint8_t kWorldMap = 0x0F;        // the World Map's world id (F138); Gummi mode keeps it (F399's row)
constexpr std::uintptr_t kOpenMenu = 0x7435D0;  // u8: 0xFF no menu open, 0x0A the full menu, 0x08 the combat pause
// The open pause-type menu (menu 8: the combat pause, Game Over). Its confirm handler exe+0x31DD70 runs the row's
// kind: 3 / 4 a mission's Retry / Quit, any it doesn't know the menu's own callback (L1522).
constexpr std::uintptr_t kMenuCursor = 0xBEF528;    // -> s16 +0 row, +0x12 scroll, +0x16 row count
constexpr std::uintptr_t kMenuRowKinds = 0xBEF538;  // -> 9 x s16: each row's kind, -1 = no row
constexpr std::uintptr_t kMissionMenuChoice = 0x3A3DD0;  // (kind 3 Retry / 4 Quit): ends the mission, asks for its room

// The scripts' target pool: 144 slots of 0x14 bytes, right after it the free list (144 x s32 slot numbers), then the
// list's count. trap_target_dup (1/131, exe+0x3BDA80) pops a slot and trap_target_free (1/132, exe+0x3BDBA0)
// pushes one; neither checks anything (F649).
constexpr std::uintptr_t kTargetPool = 0x2A163F0;
constexpr std::uintptr_t kTargetFreeList = 0x2A16F30;
constexpr std::uintptr_t kTargetFreeCount = 0x2A17170;  // s32
constexpr int kTargetSlots = 144, kTargetSize = 0x14;

// Missions.
constexpr std::uintptr_t kMissionLock = 0x2A10500;  // u32: trap_mission_lock (4/54) sets bit 0; any bit = all can't-be-hit (F504)

}  // namespace kh2coop::game
