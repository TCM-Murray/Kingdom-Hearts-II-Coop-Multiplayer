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

// Stats.
constexpr std::uintptr_t kAddMp = 0x3C0CD0;  // (stat block, mp, flag): an item's MP, with the MP Charge rule (L326)

// Story cutscenes (F97, L304, L305).
constexpr std::uintptr_t kSceneActors = 0xB653E0;   // -> 0x60 rows of 40 bytes {s32 object, s32 actor id, u32 handle, ...}
constexpr std::uintptr_t kSceneEffects = 0xB653C8;  // -> 0x80 rows of 0x50 bytes {+0 pax, +4 effect id, +0x4C s16 tied actor row}

// The player's lock-on object: every player-class actor's +0xDC8 points at it (the constructor exe+0x3A7A40), the
// friend's copy's too; +4 its target info {u32 actor handle, u32 part, u32 handle, s32 -1} (F596).
constexpr std::uintptr_t kPlayerLockOn = 0x2A11208;

// Menus.
constexpr std::uintptr_t kOpenMenu = 0x7435D0;  // u8: 0xFF no menu open, 0x0A the full menu, 0x08 the combat pause
// The open pause-type menu (menu 8: the combat pause, Game Over). Its confirm handler exe+0x31DD70 runs the row's
// kind: 3 / 4 a mission's Retry / Quit, any it doesn't know the menu's own callback (L1522).
constexpr std::uintptr_t kMenuCursor = 0xBEF528;    // -> s16 +0 row, +0x12 scroll, +0x16 row count
constexpr std::uintptr_t kMenuRowKinds = 0xBEF538;  // -> 9 x s16: each row's kind, -1 = no row
constexpr std::uintptr_t kMissionMenuChoice = 0x3A3DD0;  // (kind 3 Retry / 4 Quit): ends the mission, asks for its room

// Missions.
constexpr std::uintptr_t kMissionLock = 0x2A10500;  // u32: trap_mission_lock (4/54) sets bit 0; any bit = all can't-be-hit (F504)

}  // namespace kh2coop::game
