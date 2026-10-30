// Party members' Auto Limit off (TODO 1.139; user picks 2026-10-29: the party's Auto Limit, unequipped at each save
// load, in memory). AUTO_LIMIT=0 in kh2coop_player.ini; only while co-op is on (NEW GAME / LOAD stay normal KH2).
//
// How the game does it (VERIFIED_OFFSETS rows `save+0x24F0 + 0x114*(id-1)` +0x54 and `*(actor+0x5C0)+0x1D0`): each
// character record holds 80 u16 abilities, top bit = equipped, and a room load builds each party member's stat sheet
// from its record. The party's Auto Limit is ability 0x01A1 (Sora's own is 0x0238 and is left alone). So from the
// moment a save list picks a save (exe+0xBEDF60) until the first room is up, the equipped bit of 0x01A1 is cleared
// in all 13 records each frame: the save's copy into memory lands somewhere in that window, the sheets are built at
// its end. Equipped again in the Abilities menu, it stays until the next save load.

#include "auto_limit.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "common.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSavePicked = 0xBEDF60;  // u8: the save list picked a save (title_menu.cpp)
constexpr std::uintptr_t kInField = 0x9BA8D0;     // u8: 0 while a room loads
constexpr std::uintptr_t kRecords = 0x9ABDA0;     // character records (save+0x24F0), Sora first
constexpr std::uintptr_t kRecordSize = 0x114;
constexpr int kRecordCount = 13;  // they fill save+0x24F0..0x32F4, where the Drive Form records start (F191)
constexpr std::uintptr_t kAbilities = 0x54;
constexpr int kAbilityCount = 80;
constexpr std::uint16_t kAutoLimit = 0x01A1, kEquipped = 0x8000;

bool g_on = false;
bool g_pickSeen = false;
bool g_window = false;   // a save was picked; the first room isn't up yet
bool g_loading = false;  // the room load began since the pick
std::uint32_t g_cleared = 0;  // records unequipped in this window, bit per record

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

}  // namespace

void AutoLimitInit() {
    g_on = EnvInt("KH2COOP_AUTO_LIMIT", 1) == 0;
    Log("auto limit: %s", g_on ? "the party's Auto Limit is unequipped at each save load while co-op is on "
                                 "(AUTO_LIMIT=0 in kh2coop_player.ini)"
                               : "the game's own (AUTO_LIMIT=0 in kh2coop_player.ini unequips it at each save load)");
}

void AutoLimitFrame() {
    if (!g_on) return;
    std::uintptr_t exe = ExeBase();
    bool picked = Read<std::uint8_t>(exe + kSavePicked) != 0;
    if (picked && !g_pickSeen) {
        g_window = true;
        g_loading = false;
        g_cleared = 0;
    }
    g_pickSeen = picked;
    if (!g_window) return;
    bool inField = Read<std::uint8_t>(exe + kInField) != 0;
    if (!inField) g_loading = true;
    if (g_loading && inField) {  // the first room is up: its sheets are built
        g_window = false;
        if (!CoopActive()) return;
        char ids[64] = "";
        int at = 0;
        for (int r = 0; r < kRecordCount; ++r)
            if (g_cleared & (1u << r)) at += sprintf_s(ids + at, sizeof(ids) - at, "%s%d", at ? ", " : "", r + 1);
        Log("auto limit: save loaded; Auto Limit unequipped on character ids: %s", at ? ids : "none (not equipped)");
        return;
    }
    if (!CoopActive()) return;
    for (int r = 0; r < kRecordCount; ++r) {
        auto* a = reinterpret_cast<std::uint16_t*>(exe + kRecords + r * kRecordSize + kAbilities);
        for (int k = 0; k < kAbilityCount; ++k)
            if (a[k] == (kAutoLimit | kEquipped)) {
                a[k] = kAutoLimit;
                g_cleared |= 1u << r;
            }
    }
}

}  // namespace kh2coop
