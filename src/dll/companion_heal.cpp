// The host's companions heal the other player's Sora copy (TODO 1.58; you, 2026-10-14: "the need for the Host's
// companions to cast heal and use healing items on the Sora's clone").
//
// In a fight each companion's think (exe+0x1B53B0) puts who needs help in actor+0xF78: the player (entry 0 of the
// companion table exe+0xAD6A18), else itself, else the other companion, the first at HP <= max / 4
// (exe+0x3C1D30). Right after, the emergency pick exe+0x1B3A30 (only while +0xF78 is set) runs the companion's
// heal actions for that one: Cure (exe+0x1CC1A0) and an HP item (exe+0x1CC970 -> exe+0x1B7D90(companion, item,
// target, 1)), aimed at +0xF78 (VERIFIED_OFFSETS row 'Companion heal choice'). The extra copy is never in the
// table, and its HP block is our own Sora's (exe+0x2A23598), so it never needed help.
//   Host: before that pick, when our Sora doesn't need help and the other player is at a quarter HP or less (their
//         HP from the link) or down, the copy goes into +0xF78: our Sora first, as the game does, then the other
//         player, then the companion itself and the other companion.
//   A Cure that lands on the copy is sent to the other player (puppet.cpp ForwardCopyHeal) and no longer also heals
//   our Sora through the shared HP block. An HP item a companion picked for the copy is sent the same way when it
//   lands (puppet.cpp HookItemApply asks CompanionHealItemForCopy), instead of healing our Sora.
//   After a heal went to the other player the copy isn't put forward again for 1.5 s: their HP comes back with
//   their next packets, and without the wait a second companion would heal them again for the same wound.
// Only the host's game.

#include <windows.h>

#include <cstdint>

#include "avatar_link.hpp"
#include "common.hpp"
#include "companion_heal.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kPick = 0x1B3A30;  // (companion): the emergency pick, while +0xF78 is set
constexpr std::uint8_t kPickBytes[] = {0x4C, 0x8B, 0xDC, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00};
constexpr std::uintptr_t kTable = 0xAD6A18;  // u64[3]: the player, the two companions
constexpr std::uintptr_t kNeedsHelp = 0xF78, kAction = 0xEE8, kTarget = 0xBF8, kObjRow = 0x918;
constexpr std::uintptr_t kCure = 0x1CC1A0, kHpItem = 0x1CC970;  // the heal actions' handlers (action record +8)
constexpr std::uintptr_t kNow = 0x717008;
constexpr std::uint64_t kHoldMs = 1500;  // after a heal went to the other player
constexpr std::uint64_t kItemMs = 4000;  // a picked item lands within this

using PFN_Pick = std::uint64_t(__fastcall*)(std::uintptr_t);
PFN_Pick g_realPick = nullptr;

bool g_on = false;
std::uint64_t g_sentMs = 0;
struct ItemPick {
    std::uintptr_t companion;
    std::uint64_t ms;
};
ItemPick g_items[2] = {};
struct Seen {
    std::uintptr_t companion;
    bool forCopy;          // its +0xF78 was the copy last time
    std::uintptr_t action;  // the action it last picked for the copy
};
Seen g_seen[2] = {};
std::uint32_t g_room = 0, g_lines = 0;
std::uint32_t g_turns = 0, g_cures = 0, g_itemPicks = 0, g_itemsLanded = 0, g_sent = 0;
std::uint64_t g_statsMs = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

const char* Model(std::uintptr_t a) {
    std::uintptr_t row = a ? Read<std::uintptr_t>(a + kObjRow) : 0;
    return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
}

Seen* SeenFor(std::uintptr_t companion) {
    std::uint32_t room = Read<std::uint16_t>(ExeBase() + kNow) | 0x10000;
    if (room != g_room) {
        g_room = room;
        g_lines = 0;
        for (auto& s : g_seen) s = {};
    }
    for (auto& s : g_seen)
        if (s.companion == companion) return &s;
    for (auto& s : g_seen)
        if (!s.companion) return &(s = {companion, false, 0});
    return nullptr;
}

// The copy goes into +0xF78 when it needs help more than whoever the think chose (see the top).
void BeforePick(std::uintptr_t companion) {
    std::uintptr_t copy = PuppetCloneActor();
    Seen* seen = SeenFor(companion);
    if (!seen) return;
    bool forCopy = false;
    seen->action = seen->forCopy ? seen->action : 0;
    PeerPose peer;
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kTable);
    std::uintptr_t needs = Read<std::uintptr_t>(companion + kNeedsHelp);
    if (copy && sora && needs != sora && Read<std::uintptr_t>(exe + kTable + 8) != copy &&
        Read<std::uintptr_t>(exe + kTable + 16) != copy && AvatarLinkPeerNow(peer) && peer.maxHp > 0 &&
        !peer.gettingUp && (peer.downed || peer.hp <= peer.maxHp / 4) && GetTickCount64() - g_sentMs >= kHoldMs) {
        *reinterpret_cast<std::uintptr_t*>(companion + kNeedsHelp) = copy;
        forCopy = true;
        if (!seen->forCopy) {
            ++g_turns;
            if (g_lines < 60) {
                ++g_lines;
                Log("companion heal: %s looks after the other player's Sora copy (their HP %d/%d%s; it had %s)",
                    Model(companion), peer.hp, peer.maxHp, peer.downed ? ", down" : "",
                    !needs ? "nobody to help" : needs == companion ? "itself to help" : "the other companion to help");
            }
        }
    }
    seen->forCopy = forCopy;
}

// What the pick chose for the copy: an HP item is remembered until it lands. The pick runs again while the copy
// still needs help (each action kind has one record, so the same pointer comes back): counted and logged when the
// choice changes.
void AfterPick(std::uintptr_t companion, bool picked) {
    Seen* seen = SeenFor(companion);
    std::uintptr_t copy = PuppetCloneActor();
    if (!seen || !seen->forCopy || !copy) return;
    std::uintptr_t action = picked ? Read<std::uintptr_t>(companion + kAction) : 0;
    std::uintptr_t handler = action ? Read<std::uintptr_t>(action + 8) - ExeBase() : 0;
    std::uintptr_t target = DecodeHandle(Read<std::uint32_t>(companion + kTarget));
    if (handler == kHpItem && target == copy) {
        for (auto& i : g_items)
            if (i.companion == companion || !i.companion || GetTickCount64() - i.ms > kItemMs) {
                i = {companion, GetTickCount64()};
                break;
            }
    }
    bool changed = action != seen->action;
    seen->action = action;
    if (!action || !changed) return;
    if (handler == kHpItem && target == copy) ++g_itemPicks;
    if (handler == kCure) ++g_cures;
    if (g_lines < 60) {
        ++g_lines;
        Log("companion heal: %s picks %s for the Sora copy (target %s)", Model(companion),
            handler == kCure ? "Cure" : handler == kHpItem ? "an HP item" : "another action",
            target == copy ? "the copy" : target ? Model(target) : "none");
    }
}

std::uint64_t __fastcall HookPick(std::uintptr_t companion) {
    if (g_on && companion && CoopActive()) {
        __try {
            BeforePick(companion);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    std::uint64_t picked = g_realPick(companion);
    if (g_on && companion && CoopActive()) {
        __try {
            AfterPick(companion, (picked & 0xFF) != 0);
            std::uint64_t now = GetTickCount64();
            if (now - g_statsMs >= 30000 && (g_turns || g_sent)) {
                g_statsMs = now;
                Log("companion heal: so far %u times the copy needed help, %u Cures and %u HP items picked for it, "
                    "%u items landed on it, %u heals sent to the other player", g_turns, g_cures, g_itemPicks,
                    g_itemsLanded, g_sent);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return picked;
}

}  // namespace

void CompanionHealInit(bool host) {
    if (!host || EnvInt("KH2COOP_COMPANION_HEAL_COPY", 1) != 1) return;
    if (!HookFunction(kPick, kPickBytes, sizeof(kPickBytes), reinterpret_cast<void*>(HookPick),
                      reinterpret_cast<void**>(&g_realPick), "companions' heal pick (the Sora copy too)")) {
        Log("companion heal: off (the function's bytes differ on this build)");
        return;
    }
    g_on = true;
    Log("companion heal: on (our companions heal the other player's Sora copy when they're at a quarter HP or down)");
}

bool CompanionHealItemForCopy(std::uintptr_t user) {
    if (!g_on || !user) return false;
    for (auto& i : g_items) {
        if (i.companion == user && GetTickCount64() - i.ms <= kItemMs) {
            i = {};
            ++g_itemsLanded;
            return true;
        }
    }
    return false;
}

void CompanionHealSentToCopy() {
    g_sentMs = GetTickCount64();
    ++g_sent;
}

}  // namespace kh2coop
