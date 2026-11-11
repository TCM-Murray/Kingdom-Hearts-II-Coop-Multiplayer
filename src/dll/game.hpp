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

}  // namespace kh2coop::game
