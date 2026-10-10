// "LOAD (CO-OP)" on the title screen (TODO 5.7; user picks session 25,
// 2026-10-10): NEW GAME / LOAD / LOAD (CO-OP) / BACK whenever the mod is on.
//
// How the title works (VERIFIED_OFFSETS 'Title menu state', 'Title rows =
// layout groups', exe+0xB0D54D): the rows are ready-made layout groups in
// title.2ld, one per cursor position, and the game already has a 4-row set for
// Theater mode (NEW GAME / LOAD / THEATER / BACK). Its Theater flag
// exe+0xB0D54D is title-only state (cleared at every title start by
// exe+0x2B4F00); each frame exe+0x2B3400 asks exe+0x2B73F0 "is Theater
// unlocked?" (its only caller) and, once it says yes, sets the flag and 4 rows
// and switches the pictures itself. So:
//   1. Our exe+0x2B73F0 says yes: the game shows its own 4-row menu (also if
//      the title is already up when we hook it).
//   2. Our hook on the menu input exe+0x2B35A0 takes a confirm on row 2: it
//      remembers "co-op", and lets the game run the LOAD row's code (cursor 1
//      for that call) so the game's own save list opens; the cursor goes back
//      to row 2 afterwards (the picture never changes).
// The row's label picture (THEATER -> LOAD (CO-OP) in each game language) is
// a texture made by tools/make_title_label.py. If Theater is really unlocked,
// row 2 is still ours: Theater needs the mod off until a 5-row version exists
// (user pick: later, TODO 5.7).
// What each row does (user picks session 25): LOAD (CO-OP) shows and saves the
// co-op save file (coop_save.cpp) and turns co-op on once a save is picked;
// NEW GAME and LOAD use the normal Steam save and turn co-op off (RequestCoop):
// the game then plays as normal KH2. Without the row (TITLE_COOP=0, or a hook
// failed) co-op is always on, as before the row existed.

#include <windows.h>

#include <cstdint>

#include "common.hpp"
#include "coop_save.hpp"
#include "title_menu.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kTheaterCheck = 0x2B73F0;  // u8(): bit 0 of exe+0xB1D778
constexpr std::uint8_t kTheaterCheckBytes[] = {0x0F, 0xB6, 0x05, 0x81, 0x63, 0x86, 0x00, 0x24, 0x01, 0xC3};
constexpr std::uintptr_t kMenuInput = 0x2B35A0;     // bool(title task): one frame of the title menu
constexpr std::uint8_t kMenuInputBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                                            0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1};

constexpr std::uintptr_t kRowsLayout = 0xB1D5D8;  // u64: the rows layout (0 until the menu is up)
constexpr std::uintptr_t kAction = 0xB1D5E0;      // i32: 1 Load, 5 waiting, -1 none
constexpr std::uintptr_t kCursor = 0xB1D5E4;      // i32: row
constexpr std::uintptr_t kRowCount = 0xB1D5F0;    // i32: 3, or 4 with the Theater row
constexpr std::uintptr_t kTheaterFlag = 0xB0D54D; // u8
constexpr std::uintptr_t kTitleVariant = 0x749804; // bit 25: titlejf.2ld, a 2-row title (leave alone)
constexpr std::uintptr_t kMenuBusy = 0x9006B0;    // u8 (exe+0x1515C0): the menu input does nothing
constexpr std::uintptr_t kPadPressed = 0xBF31A0 + 8;  // u64: buttons pressed this frame (exe+0x39B780(0) + 8)
constexpr std::uint64_t kConfirm = 0x4;
constexpr std::uintptr_t kSavePicked = 0xBEDF60;  // u8: the save list picked a save (exe+0x2E78D0)
constexpr std::int32_t kLoadRow = 1, kCoopRow = 2, kRows = 4, kActionWaiting = 5;

using PFN_TheaterCheck = std::uint8_t(__fastcall*)();
using PFN_MenuInput = bool(__fastcall*)(void*);
PFN_TheaterCheck g_realTheaterCheck = nullptr;
PFN_MenuInput g_realMenuInput = nullptr;

bool g_coopPicked = false;   // the last confirm on the title was our row
bool g_coopLoad = false;     // the save being loaded came through our row
bool g_pickSeen = false;
bool g_theaterLogged = false;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

template <typename T>
void Write(std::uintptr_t rva, T v) {
    *reinterpret_cast<volatile T*>(ExeBase() + rva) = v;
}

bool TwoRowTitle() { return (Read<std::uint32_t>(kTitleVariant) >> 25 & 1) != 0; }

std::uint8_t __fastcall HookTheaterCheck() {
    std::uint8_t real = g_realTheaterCheck();
    if (TwoRowTitle()) return real;
    if ((real & 1) && !g_theaterLogged) {
        g_theaterLogged = true;
        Log("title: Theater is unlocked in this game; its row is LOAD (CO-OP) while the mod is on "
            "(a 5-row title is TODO 5.7)");
    }
    return 1;
}

bool __fastcall HookMenuInput(void* task) {
    bool picked = Read<std::uint8_t>(kSavePicked) != 0;
    if (picked && !g_pickSeen) {
        g_coopLoad = g_coopPicked;
        g_coopPicked = false;
        Log("title: save picked through %s", g_coopLoad ? "LOAD (CO-OP)" : "LOAD");
        RequestCoop(g_coopLoad, g_coopLoad ? "a save was loaded through LOAD (CO-OP)" : "a save was loaded through LOAD");
    }
    g_pickSeen = picked;

    bool confirm = (Read<std::uint64_t>(kPadPressed) & kConfirm) != 0;
    if (!confirm || picked || Read<std::uintptr_t>(kRowsLayout) == 0 || Read<std::int32_t>(kRowCount) != kRows ||
        Read<std::uint8_t>(kTheaterFlag) == 0 || Read<std::uint8_t>(kMenuBusy) != 0 ||
        Read<std::int32_t>(kAction) == kActionWaiting || TwoRowTitle())
        return g_realMenuInput(task);

    std::int32_t row = Read<std::int32_t>(kCursor);
    if (row != kCoopRow) {
        if (g_coopPicked) Log("title: row %d confirmed, LOAD (CO-OP) dropped", row);
        g_coopPicked = false;
        if (row <= kLoadRow) {  // NEW GAME, LOAD: the Steam save, the game as normal KH2
            CoopSaveSelect(false);
            RequestCoop(false, row == kLoadRow ? "LOAD picked on the title" : "NEW GAME picked on the title");
        }
        return g_realMenuInput(task);
    }
    if (!CoopSaveSelect(true)) {  // the row does nothing (the reason is in the log)
        Log("title: LOAD (CO-OP) confirmed, but the co-op save file can't be used; nothing happens");
        auto pad = reinterpret_cast<volatile std::uint64_t*>(ExeBase() + kPadPressed);
        std::uint64_t pressed = *pad;
        *pad = pressed & ~kConfirm;  // the game must not run its THEATER row's code
        bool result = g_realMenuInput(task);
        *pad = pressed;
        return result;
    }
    g_coopPicked = true;
    Log("title: LOAD (CO-OP) confirmed: opening the save list (co-op save file)");
    Write<std::int32_t>(kCursor, kLoadRow);  // the LOAD row's code runs (it may wait for the menu system)
    bool result = g_realMenuInput(task);
    if (Read<std::int32_t>(kCursor) == kLoadRow) Write<std::int32_t>(kCursor, kCoopRow);
    return result;
}

}  // namespace

void TitleMenuInit() {
    if (EnvInt("KH2COOP_TITLE_COOP", 1) != 1) {
        Log("title: LOAD (CO-OP) off (TITLE_COOP=0)");
        RequestCoop(true, "no LOAD (CO-OP) row (TITLE_COOP=0): co-op always on");
        return;
    }
    bool ok = HookFunctionAlways(kMenuInput, kMenuInputBytes, sizeof(kMenuInputBytes), reinterpret_cast<void*>(HookMenuInput),
                           reinterpret_cast<void**>(&g_realMenuInput), "title menu input") &&
              HookFunctionAlways(kTheaterCheck, kTheaterCheckBytes, sizeof(kTheaterCheckBytes),
                           reinterpret_cast<void*>(HookTheaterCheck), reinterpret_cast<void**>(&g_realTheaterCheck),
                           "title Theater check");
    Log("title: LOAD (CO-OP) row %s", ok ? "on (4-row title)" : "OFF: a hook could not be installed");
    if (!ok) RequestCoop(true, "no LOAD (CO-OP) row (a title hook failed): co-op always on");
}

bool TitleCoopLoad() { return g_coopLoad; }

}  // namespace kh2coop
