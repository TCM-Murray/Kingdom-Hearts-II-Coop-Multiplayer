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

// Story cutscenes (F97, L304, L305).
constexpr std::uintptr_t kSceneActors = 0xB653E0;   // -> 0x60 rows of 40 bytes {s32 object, s32 actor id, u32 handle, ...}
constexpr std::uintptr_t kSceneEffects = 0xB653C8;  // -> 0x80 rows of 0x50 bytes {+0 pax, +4 effect id, +0x4C s16 tied actor row}

// Missions.
constexpr std::uintptr_t kMissionLock = 0x2A10500;  // u32: trap_mission_lock (4/54) sets bit 0; any bit = all can't-be-hit (F504)

}  // namespace kh2coop::game
