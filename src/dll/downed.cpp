// Downed instead of dead (DOWNED=1 on both PCs, default on; user design 2026-10-06).
// The other player's Sora is a member of the host's party, so a player whose
// Sora takes a lethal hit while the other player is in the same room goes
// down like a knocked-out companion instead of getting a Game Over:
//   - HP stays at 1 (the game's own death is only triggered by its HP change
//     reaching 0), input is blocked except Start and the camera (right stick), every hit on our Sora is
//     dropped, and Sora lies on the ground with stars (motion 252).
//   - Back up after DOWN_SECONDS (default 30, Aomi 2026-10-08; was 20), or 6 s after the fight ends,
//     whichever is first: HP = max/4, get-up motion 253 (~4.8 s, still
//     protected), then idle and control back. That is the game's own
//     companion rule (bench 2026-10-06, Goofy/Donald KO'd through the game's HP
//     change): in a fight they stay down; once it ends they get up ~6-7 s later
//     with a quarter of their max HP (Goofy 7/30, Donald 5/21).
//   - 252/253 are the motions of the Mickey rescue (forced-Mickey bench
//     2026-10-06: Sora lies in 252 while Mickey fights, gets up in 253 after
//     Healing Light). The game keeps them in Mickey's motion set only; the KH2
//     Coop mod ships Sora's motion set with them added (tools/make_mset.py).
//     Without them (another motion set: Drive forms, other worlds' variants,
//     or a PC without the new file) the fallback is the death collapse 54 held
//     at its most collapsed frame, and a plain stand-up (motion 0).
//   - The Keyblade (user test 2026-10-06: it showed through the whole get-up in
//     a fight): as in the rescue, it is put away while down and comes back at
//     253's own frame trigger 238 (id 29 "weapon appears with effect", ~4 s in,
//     the last second). Weapons: Sora actor+0xD60/+0xD68; hide = the weapon
//     handler's vtable+0x80 (flags +0x124 |= 0x700; 0x400 clear = visible), show
//     with effect = exe+0x3D6A90(Sora), what trigger 29 calls (exe+0x40DD70).
//     The game shows it on its own in a fight, so it is re-hidden until then.
//     Each re-show in the Keyblade's update (exe+0x3EFA60: while the weapon has a motion for Sora's
//     current one, 253 in W_EX010.mset, and is away) also starts its "appear" flash effect
//     (exe+0x3B6670(weapon, 0, ...) from exe+0x3EFB64), so hiding it every frame started ~60 flashes
//     a second: ~375 KB of the game's 509 KB effect pool (exe+0x2B0EC08, tools/pool_stat.py, bench
//     2026-10-07). With a fight's own effects the pool ran out: host crash 2026-10-06 23:50:28
//     (exe+0x4B00DA, allocator exe+0x4AF240 returned null), 1 s after a get-up in the Cerberus fight.
//     That flash is now skipped for our held weapons (the frame shows them away anyway).
//   - Sora's own motions (bench 2026-10-06): 54 is a ~22-frame collapse that
//     loops; he has no 55 (the companions' get-up). Asking for a motion he
//     doesn't have leaves him in a T-pose (so availability is checked first),
//     and after 54 the game picks no motion on its own until something sets one.
//   - Healing while down (user decision 2026-10-06): HP orbs do nothing; a Cure or a healing item
//     gets the player up at once, with the heal (at least max/4). The other player's Cure in range
//     and items used on the copy arrive as heals from their game (puppet.cpp); in our own game a
//     companion's Cure or item counts too (told apart by the code that applies it).
//   - Both players down = Game Over for both: each side, seeing the other one
//     down (or dead) while it is down itself, lets the game kill its Sora.
// With no other player in the room, death works as in the normal game.
// Same code on both PCs (host and friend).

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;          // u8 world, room
constexpr std::uintptr_t kInField = 0x9BA8D0;      // u8: 0 while a room loads
constexpr std::uintptr_t kPlayerActor = 0x2A171C8; // list head = Sora
constexpr std::uintptr_t kSoraHp = 0x2A23598;      // Sora's stat slot: u32 HP, u32 max HP
constexpr std::uintptr_t kBattle = 0x2A11404;      // u32: 1 while enemies are around
constexpr std::uintptr_t kActorMotion = 0x180;
constexpr std::uintptr_t kActorMotionClock = 0x19C;  // f32 frames since the motion started
constexpr int kMotionKnockedOut = 54, kMotionIdle = 0;
constexpr int kMotionLying = 252, kMotionRescueGetUp = 253;  // the Mickey rescue's (see above)
constexpr int kRescueGetUpMaxFrames = 330;         // 253 ran ~4.8 s in the rescue; stop by 5.5 s at the latest
constexpr float kWeaponBackFrame = 238.0f;         // 253's frame trigger 29 (game frames, = the motion clock)
constexpr std::uintptr_t kWeapons = 0xD60;         // u64[2] weapon actors
constexpr std::uintptr_t kWeaponFlags = 0x124;     // 0x400 set = put away
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;
constexpr std::uint8_t kDecodeHandleBytes[] = {0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3};
constexpr std::uintptr_t kWeaponAppearFx = 0x3D6A90;  // (owner actor): every weapon appears with its effect
constexpr std::uint8_t kWeaponAppearFxBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                                 0xEC, 0x20, 0x48, 0x8D, 0x99, 0x60, 0x0D, 0x00, 0x00};
constexpr std::uintptr_t kEffectStart = 0x3B6670;  // (actor, effect id, ...): starts one of the actor's effects
constexpr std::uint8_t kEffectStartBytes[] = {0x48, 0x83, 0xEC, 0x38, 0x4C, 0x8D, 0x91, 0x80, 0x00, 0x00, 0x00};
constexpr std::uintptr_t kReshowFlashReturn = 0x3EFB69;  // after the Keyblade update's re-show flash call
constexpr float kHoldFrame = 18.0f;                // most collapsed frame of 54 (it restarts after ~22)
constexpr std::uint16_t kStart = 0x0008;
constexpr int kGetUpFrames = 10;                   // a short beat standing before control returns
constexpr int kAfterBattleFrames = 360;            // the game's companions: up ~6 s after the fight

enum class State { Up, Down, GettingUp };
bool g_on = false;
State g_state = State::Up;
std::uintptr_t g_actor = 0;        // our Sora while down / getting up
std::uint64_t g_frame = 0, g_since = 0, g_calmSince = 0;
int g_downFrames = 1200;
bool g_reviveWanted = false, g_dieWanted = false;
int g_reviveHp = 0;                // HP the next revive gives (0 = max/4)
bool g_rescueMotions = false;      // this down uses 252/253 (our Sora's motion set has both)
bool g_standDone = false;          // getting up finished (idle set); control returns
bool g_weaponCalls = false;        // the weapon functions' bytes match this build
bool g_weaponBack = false;         // the Keyblade came back during this get-up
bool g_weaponHold = false;         // keep our Sora's weapons put away (set in Sora's update, applied in theirs)
float g_lastClock = 0.0f;
std::uint32_t g_flashesSkipped = 0;
using PFN_EffectStart = std::uint64_t(__fastcall*)(std::uintptr_t, std::uint64_t, std::uint64_t, std::uint32_t,
                                                   std::uintptr_t);
PFN_EffectStart g_realEffectStart = nullptr;
std::uint32_t g_downs = 0, g_revives = 0, g_bothDown = 0, g_hitsDropped = 0, g_healsDropped = 0, g_healRevives = 0;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

std::uintptr_t Sora() { return Read<std::uintptr_t>(kPlayerActor); }

// The other player is in our room with a live Sora that isn't down or dead.
bool PartnerUp() {
    PeerPose p;
    if (!AvatarLinkPeerNow(p) || !p.hasActor) return false;
    if (p.world != Read<std::uint8_t>(kNow) || p.room != Read<std::uint8_t>(kNow + 1)) return false;
    return !p.downed && p.hp > 0;
}

// The other player can no longer save us: down, dead, or at its Game Over in our room.
bool PartnerOut() {
    PeerPose p;
    if (!AvatarLinkPeerNow(p)) return false;  // no news: keep waiting (the timer still runs)
    if (p.world != Read<std::uint8_t>(kNow) || p.room != Read<std::uint8_t>(kNow + 1)) return false;
    return p.downed || p.hp <= 0;
}

// Weapons of our Sora: visible -> put away (the handler's own method), or report whether any is out.
bool WeaponsOut(std::uintptr_t actor, bool putAway) {
    if (!g_weaponCalls) return false;
    using PFN_Decode = std::uintptr_t(__fastcall*)(std::uint32_t);
    using PFN_Hide = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
    bool out = false;
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t w = *reinterpret_cast<const volatile std::uintptr_t*>(actor + kWeapons + 8 * i);
        if (!w || (*reinterpret_cast<const volatile std::uint32_t*>(w + kWeaponFlags) & 0x400)) continue;
        out = true;
        if (!putAway) continue;
        std::uintptr_t handler =
            reinterpret_cast<PFN_Decode>(ExeBase() + kDecodeHandle)(*reinterpret_cast<const std::uint32_t*>(w));
        if (!handler) continue;
        auto vtable = *reinterpret_cast<const std::uintptr_t*>(handler);
        reinterpret_cast<PFN_Hide>(*reinterpret_cast<const std::uintptr_t*>(vtable + 0x80))(handler, w);
    }
    return out;
}

std::uint64_t __fastcall HookEffectStart(std::uintptr_t actor, std::uint64_t id, std::uint64_t a3, std::uint32_t a4,
                                        std::uintptr_t a5) {
    if (g_weaponHold && g_actor &&
        reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == ExeBase() + kReshowFlashReturn &&
        (actor == *reinterpret_cast<const volatile std::uintptr_t*>(g_actor + kWeapons) ||
         actor == *reinterpret_cast<const volatile std::uintptr_t*>(g_actor + kWeapons + 8))) {
        ++g_flashesSkipped;
        return 0;  // the caller ignores the result
    }
    return g_realEffectStart(actor, id, a3, a4, a5);
}

void GoUp(const char* why) {
    g_state = State::Up;
    g_weaponHold = false;
    g_actor = 0;
    InputBlock(0);
    Log("downed: %s", why);
}

}  // namespace

void DownedInit() {
    g_on = EnvInt("KH2COOP_DOWNED", 1) == 1;
    if (!g_on) return;
    g_downFrames = EnvInt("KH2COOP_DOWN_SECONDS", 30) * 60;
    g_weaponCalls =
        std::memcmp(reinterpret_cast<const void*>(ExeBase() + kDecodeHandle), kDecodeHandleBytes,
                    sizeof(kDecodeHandleBytes)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(ExeBase() + kWeaponAppearFx), kWeaponAppearFxBytes,
                    sizeof(kWeaponAppearFxBytes)) == 0;
    if (g_weaponCalls &&
        !HookFunction(kEffectStart, kEffectStartBytes, sizeof(kEffectStartBytes), reinterpret_cast<void*>(HookEffectStart),
                      reinterpret_cast<void**>(&g_realEffectStart), "effect start (Keyblade flash while downed)"))
        g_weaponCalls = false;  // hiding it every frame without the hook fills the effect pool
    if (!g_weaponCalls) Log("downed: weapon function bytes differ (other game build?); the Keyblade stays as the game has it");
    if (!InputInjectEnable()) Log("downed: input hook missing; a downed player could still move");
    Log("downed: on (a lethal hit with the other player in the room = down for up to %d s; both down = Game Over)",
        g_downFrames / 60);
}

bool DownedIsDown() { return g_on && g_state != State::Up; }
// Getting up counts as up for the other game: we already have HP back and can't be hurt. Sending "down"
// for it made the other game's lethal hit a real death (bench 2026-10-06 23:27:57: friend hit for 18 at HP 2
// while the host was in its get-up motion -> the friend alone at Game Over).
bool DownedIsLying() { return g_on && g_state == State::Down; }

bool DownedInterceptDamage(std::uintptr_t actor, int delta, int react, int* hpOut) {
    if (!g_on || delta >= 0 || actor == 0 || actor != Sora()) return false;
    int hp = static_cast<int>(Read<std::uint32_t>(kSoraHp));
    if (g_state != State::Up) {  // down or getting up: nothing hurts
        ++g_hitsDropped;
        *hpOut = hp;
        return true;
    }
    if (hp + delta > 0 || hp <= 0 || !PartnerUp()) return false;  // not lethal, already dead, or alone
    *hpOut = hp > 1 ? PuppetApplyHp(actor, -(hp - 1), react) : hp;  // the hit lands, but leaves 1 HP
    g_state = State::Down;
    g_actor = actor;
    g_since = g_frame;
    g_calmSince = 0;
    g_reviveWanted = g_dieWanted = g_standDone = false;
    g_reviveHp = 0;
    g_rescueMotions = PuppetHasMotion(actor, kMotionLying) && PuppetHasMotion(actor, kMotionRescueGetUp);
    InputBlock(static_cast<std::uint16_t>(~kStart));  // all but Start (shared combat pause) and the camera stick
    ++g_downs;
    Log("downed: our Sora took a lethal hit (%d at HP %d); down instead of dead, the other player is still up "
        "(%s)", -delta, hp, g_rescueMotions ? "lying, motion 252" : "motion set without 252/253: collapse pose");
    return true;
}

bool DownedInterceptHeal(std::uintptr_t actor, int amount, bool revives, const char* what) {
    if (!g_on || g_state != State::Down || actor == 0 || actor != g_actor || amount <= 0) return false;
    if (!revives) {
        if (g_healsDropped++ < 20) Log("downed: %s (+%d) does nothing while down", what, amount);
        return true;
    }
    if (g_reviveWanted || g_dieWanted) return true;
    int maxHp = static_cast<int>(Read<std::uint32_t>(kSoraHp + 4));
    int healed = 1 + amount < maxHp ? 1 + amount : maxHp;
    g_reviveHp = healed > maxHp / 4 ? healed : (maxHp / 4 > 1 ? maxHp / 4 : 1);
    g_reviveWanted = true;
    ++g_healRevives;
    Log("downed: %s (+%d) reached our Sora: getting up now with HP %d", what, amount, g_reviveHp);
    return true;
}

bool DownedBlocksMotion(std::uintptr_t actor) { return g_on && g_state != State::Up && actor == g_actor; }

void DownedAfterUpdate(std::uintptr_t actor) {
    if (!g_on || g_state == State::Up || !g_actor) return;
    if (actor != g_actor) {
        // A weapon's own update shows it again while it has the owner's motion (253 in W_EX010.mset):
        // putting it away after that update is what the frame shows (bench 2026-10-06 19:28).
        if (g_weaponHold && g_weaponCalls &&
            (actor == *reinterpret_cast<const volatile std::uintptr_t*>(g_actor + kWeapons) ||
             actor == *reinterpret_cast<const volatile std::uintptr_t*>(g_actor + kWeapons + 8)))
            WeaponsOut(g_actor, true);
        return;
    }
    if (g_dieWanted) {
        g_dieWanted = false;
        GoUp("both players are down: Game Over");
        ++g_bothDown;
        int hp = static_cast<int>(Read<std::uint32_t>(kSoraHp));
        if (hp > 0) PuppetApplyHp(actor, -hp, 0);  // the game's own death
        return;
    }
    if (g_reviveWanted) {
        g_reviveWanted = false;
        int hp = static_cast<int>(Read<std::uint32_t>(kSoraHp));
        int maxHp = static_cast<int>(Read<std::uint32_t>(kSoraHp + 4));
        int target = g_reviveHp > 0 ? g_reviveHp : (maxHp / 4 > 1 ? maxHp / 4 : 1);
        g_reviveHp = 0;
        if (target > hp) PuppetApplyHp(actor, target - hp, 0);
        g_state = State::GettingUp;
        g_since = g_frame;
        g_lastClock = 0.0f;
        g_weaponBack = false;
        ++g_revives;
        if (g_rescueMotions) {
            PuppetSetMotion(actor, kMotionRescueGetUp);
        } else {
            PuppetSetMotion(actor, kMotionIdle);
            g_standDone = true;
        }
        Log("downed: getting up with HP %d/%d", target > hp ? target : hp, maxHp);
        return;
    }
    int motion = *reinterpret_cast<const volatile std::int32_t*>(actor + kActorMotion);
    if (g_state == State::GettingUp) {
        if (g_standDone) return;
        float clock = *reinterpret_cast<const volatile float*>(actor + kActorMotionClock);
        g_weaponHold = g_rescueMotions && motion == kMotionRescueGetUp && !g_weaponBack && clock < kWeaponBackFrame;
        if (g_rescueMotions && motion == kMotionRescueGetUp && !g_weaponBack) {
            if (clock < kWeaponBackFrame) {
                WeaponsOut(actor, true);  // still put away until 253's own trigger
            } else if (WeaponsOut(actor, false)) {
                g_weaponBack = true;      // the trigger brought it back
            } else if (clock >= kWeaponBackFrame + 6.0f) {
                using PFN_AppearFx = void(__fastcall*)(std::uintptr_t);
                reinterpret_cast<PFN_AppearFx>(ExeBase() + kWeaponAppearFx)(actor);  // the trigger's own call
                g_weaponBack = true;
                Log("downed: Keyblade brought back at frame %.0f of the get-up", clock);
            }
        }
        // 253 finished: it moved on, wrapped back to its start, or ran too long.
        if (motion != kMotionRescueGetUp || clock + 1.0f < g_lastClock || g_frame - g_since > kRescueGetUpMaxFrames) {
            PuppetSetMotion(actor, kMotionIdle);  // the game sets nothing itself after these motions
            g_standDone = true;
        }
        g_lastClock = clock;
        return;
    }
    if (g_rescueMotions) {
        if (motion != kMotionLying) PuppetSetMotion(actor, kMotionLying);  // ours; the game's own changes stay blocked
        g_weaponHold = true;
        WeaponsOut(actor, true);  // put away while down (the game draws it in a fight)
        return;
    }
    if (motion != kMotionKnockedOut) {
        PuppetSetMotion(actor, kMotionKnockedOut);  // our own call; the game's own changes stay blocked
        return;
    }
    auto* clock = reinterpret_cast<float*>(actor + kActorMotionClock);
    if (*clock >= kHoldFrame) *clock = kHoldFrame - 1.0f;  // the next update shows ~kHoldFrame again
}

void DownedFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_frame % 600 == 0 && g_downs)
        Log("downed: downs %u, revives %u (by a heal %u), both down %u, hits dropped while down %u, heals dropped %u, "
            "Keyblade flashes skipped %u", g_downs, g_revives, g_healRevives, g_bothDown, g_hitsDropped, g_healsDropped,
            g_flashesSkipped);
    if (g_state == State::Up) return;
    __try {
        // Room change, Continue or title: our Sora is a new actor; nothing to hold.
        if (Sora() != g_actor || !Read<std::uint8_t>(kInField) || Read<std::uint8_t>(kNow) == 0xFF) {
            GoUp("our Sora changed (room load); up again");
            return;
        }
        if (g_state == State::GettingUp) {
            if (g_standDone && g_frame - g_since >= kGetUpFrames) GoUp("control is back");
            return;
        }
        if (PartnerOut()) {
            g_dieWanted = true;
            return;
        }
        bool battle = Read<std::uint32_t>(kBattle) != 0;
        g_calmSince = battle ? 0 : (g_calmSince ? g_calmSince : g_frame);
        if (g_frame - g_since >= static_cast<std::uint64_t>(g_downFrames) ||
            (g_calmSince && g_frame - g_calmSince >= kAfterBattleFrames))
            g_reviveWanted = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        GoUp("exception; up again");
    }
}

}  // namespace kh2coop
