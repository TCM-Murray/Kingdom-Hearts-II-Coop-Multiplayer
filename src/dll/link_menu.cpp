// "Connection lost" screen (TODO 6.8, your layout session 27): while the other game is gone (link_watch.cpp),
// the game's own pause menu is open with our list: the PAUSE title, the line "Connection lost. Reconnecting
// to Host..." (on the friend) / "...to Client..." (on the host), and two items, "Continue Alone" and "Title
// Screen". Waiting has no item: the screen stays until the other game is back, then it closes by itself.
//
// How (VERIFIED_OFFSETS lead 'Pause menu (menu 8)', Ghidra session 27):
// - exe+0x2E4C20(list, def) opens the pause menu (menu 8) with a 0x40-byte item list, the way the Start
//   button does in a fight (list 6, no items) or a cutscene (list 4, Continue / Skip Scene). Ours is a copy
//   of list 6 from the game's table (the 'slct' entry of 14mission.bar) with our two items.
// - Item kinds the menu doesn't know go to def+0x28 (kind, task): our two items close the menu like
//   "Continue" does and hand the choice to link_watch.cpp, which acts once the menu is shut.
// - Item texts come from the message lookup exe+0x3AD9A0(id): our hook answers our two ids with our text,
//   only while our menu is up. The text is in the game's own format (sys.bar: A=0x2E, a=0x9A, ...).
// - The line under the title is the menu's text sprite for list+0x18 (a mission's goal line). That field
//   holds a 32-bit game pointer, which can't reach our memory, so the list gets a non-zero dummy (the
//   sprite is only made when it's set) and our hook on the item fill exe+0x31F1F0 swaps the sprite's text
//   pointer (+0x1F8) for ours right after the game set it, before anything draws.
// - The same menu is the friend's "different save" screen (story_sync.cpp, your layout 2026-10-25): the line
//   says the save differs from the host's, the items are "Continue Playing" and "Load" (the title screen,
//   where LOAD (CO-OP) picks another). The texts are set each time a screen opens; when the wanted screen
//   changes while one is up, it closes first (the Start press below) and the other opens after.
// - Start or Back closes any pause; ours then opens again a moment later (blocking the close inside the
//   menu leaves its input task in a 'done' state, exe+0xBEE658).
// - When the other game is back we close it the way a player does, with a Start press (as pause sync
//   does): calling the close from outside the menu (exe+0x2EB790 + exe+0xBEE658 = 1) crashed the game 70 ms
//   later in its sprite list (exe+0x166E4C, bench session 27).

#include "link_menu.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "common.hpp"
#include "story_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kOpenPause = 0x2E4C20;  // (list, def): open the pause menu with this item list
constexpr std::uint8_t kOpenPauseBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kCloseMenu = 0x2EB790;  // (): pops the game's pause (exe+0xAD803C stack): the world runs again
constexpr std::uint8_t kCloseMenuBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8};
constexpr std::uintptr_t kMessage = 0x3AD9A0;  // (id) -> the message's text
constexpr std::uint8_t kMessageBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                          0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kFillItems = 0x31F1F0;  // (): builds the pause menu's text sprites from its list
constexpr std::uint8_t kFillItemsBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C,
                                            0x24, 0x20, 0x56, 0x41, 0x54, 0x41, 0x56};

constexpr std::uintptr_t kListTable = 0x2A11498;  // ptr: the pause item lists ({?, u32 count, 0x40-byte lists})
constexpr std::uintptr_t kMenuList = 0xBEF540;    // ptr: the list the open pause menu was built from
constexpr std::uintptr_t kMenuSprites = 0xBEF518; // ptr: the pause menu's 3 text sprites (0x220 bytes each)
constexpr std::size_t kLineText = 0x220 + 0x1F8;  // the second sprite's (list+0x18 line) text pointer
constexpr std::uintptr_t kOpenMenu = 0x7435D0;    // u8: 0xFF = no menu
constexpr std::uintptr_t kNow = 0x717008;         // u8 world (0xFF = title)
constexpr std::uintptr_t kSoraHp = 0x2A23598;     // Sora's stat slot HP (0 = Game Over, which uses menu 8 too)
// The Start watcher exe+0x1572B0 opens a pause only when all of these allow it.
constexpr std::uintptr_t kPauseBlock = 0xABB878;  // u32 pause blockers, 0 = none
constexpr std::uintptr_t kInField = 0x9BA8D0;     // u8
constexpr std::uintptr_t kBusy1 = 0x9006B0;       // u8 (exe+0x1515C0)
constexpr std::uintptr_t kBusy2 = 0xABB4B8;       // u32 (exe+0x156DC0()+0x108)
constexpr std::uintptr_t kBusy3 = 0xAC0F48;       // u64 (exe+0x19BA60)
constexpr std::uintptr_t kMenuLock = 0x9A8738;    // u8: exe+0x2E4950 opens nothing while set

constexpr std::uint16_t kStart = 0x0008;  // raw slot button bit (PS2 layout)
constexpr std::uint16_t kCombatPauseList = 6;
constexpr std::uint16_t kKindFirst = 0x40, kKindSecond = 0x41;    // unknown to the game: go to our callback
constexpr std::uint16_t kMsgFirst = 0x7F40, kMsgSecond = 0x7F41;  // not in sys.bar (its ids end at 0x562D)

using PFN_OpenPause = void(__fastcall*)(void* list, void* def);
using PFN_Void = void(__fastcall*)();
using PFN_Message = const std::uint8_t*(__fastcall*)(int id);

PFN_Message g_realMessage = nullptr;
PFN_Void g_realFill = nullptr;
bool g_on = false, g_host = false, g_press = false;  // g_press: the Start press works (input hook in)

alignas(8) std::uint8_t g_list[0x40] {};
alignas(8) std::uint8_t g_def[0x30] {};
std::uint8_t g_textFirst[32], g_textSecond[32], g_textLine[96];
int g_screen = 0;             // the screen open or opening: 1 'Connection lost', 2 'different save'

bool g_up = false;            // we opened it and it hasn't closed yet (any menu up since then is ours)
bool g_seen = false;          // the menu byte showed it open
int g_closePresses = 0;       // Start presses sent to close it because the other game is back
std::uint64_t g_frame = 0, g_openedAt = 0, g_closedAt = 0, g_pressedAt = 0;
int g_choice = 0;             // picked on our menu, not yet taken by link_watch
std::uint32_t g_opens = 0;
constexpr std::uint64_t kOpenGrace = 30;  // frames for the menu to show up before we try again
constexpr std::uint64_t kReopenGap = 15;  // frames after a pause closed before ours opens (its teardown ends)
constexpr std::uint64_t kPressGap = 45;   // frames for a Start press to work before the next one
// Frames after the menu shut before a pick is handed over: a pause stops entity updates, and the Sora copy
// counts as gone until they run again (bench session 27: 'Continue Alone' in a fight found no copy to remove).
constexpr std::uint64_t kSettleFrames = 10;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

const char* ScreenName(int screen) { return screen == 2 ? "different save" : "Connection lost"; }

}  // namespace

// ASCII -> the game's text (sys.bar, us): only what our strings use.
void LinkMenuEncode(const char* s, std::uint8_t* out, std::size_t size) {
    std::size_t n = 0;
    for (; *s && n + 1 < size; ++s) {
        char c = *s;
        std::uint8_t b = 0x01;
        if (c >= 'A' && c <= 'Z') b = static_cast<std::uint8_t>(0x2E + (c - 'A'));
        else if (c >= 'a' && c <= 'z') b = static_cast<std::uint8_t>(0x9A + (c - 'a'));
        else if (c == '\n') b = 0x02;
        else if (c == '.') b = 0x4F;
        else if (c == '!') b = 0x48;
        else if (c == '?') b = 0x49;
        else if (c == ':') b = 0x52;
        else if (c == '\'') b = 0xEC;
        out[n++] = b;
    }
    out[n] = 0;
}

namespace {

void Encode(const char* s, std::uint8_t* out, std::size_t size) { LinkMenuEncode(s, out, size); }

// The screen's texts, set just before it opens.
void SetTexts(int screen, const char* line) {
    if (screen == 2) {
        Encode("Continue Playing", g_textFirst, sizeof(g_textFirst));
        Encode("Load", g_textSecond, sizeof(g_textSecond));
        Encode(line ? line : "Your save differs from the Host's.", g_textLine, sizeof(g_textLine));
        return;
    }
    Encode("Continue Alone", g_textFirst, sizeof(g_textFirst));
    Encode("Title Screen", g_textSecond, sizeof(g_textSecond));
    Encode(g_host ? "Connection lost.\nReconnecting to Client..." : "Connection lost.\nReconnecting to Host...",
           g_textLine, sizeof(g_textLine));
}

void __fastcall OnItem(int kind, void* /*task*/) {
    if (kind != kKindFirst && kind != kKindSecond) return;
    bool first = kind == kKindFirst;
    if (g_screen == 2) {
        g_choice = first ? 5 : 6;
        // At once: a room load may start as soon as the menu is shut, before link_watch takes the choice
        // (bench 2026-10-25: following the host loaded the next room first, and the pick waited for the one after).
        StorySyncChoose(first);
    } else {
        g_choice = first ? 3 : 2;
    }
    Log("link menu: %s picked", g_screen == 2 ? (first ? "Continue Playing" : "Load")
                                              : (first ? "Continue Alone" : "Title Screen"));
    // What the pause's own Continue does (exe+0x31DD70 kind 6); the menu then marks itself done (exe+0x2F6500).
    reinterpret_cast<PFN_Void>(ExeBase() + kCloseMenu)();
}

const std::uint8_t* __fastcall HookMessage(int id) {
    if (g_up) {
        int bare = id & 0x7FFF;
        if (bare == kMsgFirst) return g_textFirst;
        if (bare == kMsgSecond) return g_textSecond;
    }
    return g_realMessage(id);
}

void __fastcall HookFill() {
    g_realFill();
    if (Read<std::uintptr_t>(kMenuList) != reinterpret_cast<std::uintptr_t>(g_list)) return;
    auto sprites = Read<std::uintptr_t>(kMenuSprites);
    if (!sprites) return;
    *reinterpret_cast<const std::uint8_t**>(sprites + kLineText) = g_textLine;
}

bool MayOpen() {
    return Read<std::uint8_t>(kNow) != 0xFF && Read<std::uint8_t>(kOpenMenu) == 0xFF &&
           Read<std::uint32_t>(kSoraHp) != 0 && Read<std::uint32_t>(kPauseBlock) == 0 &&
           Read<std::uint8_t>(kInField) != 0 && Read<std::uint8_t>(kBusy1) == 0 &&
           Read<std::uint32_t>(kBusy2) == 0 && Read<std::uint64_t>(kBusy3) == 0 &&
           Read<std::uint8_t>(kMenuLock) == 0;
}

// Our list: a copy of the combat pause's list (same title and look) with our two items.
void BuildList() {
    // The table's fields, from list 6 (dump of 14mission.bar, session 27), in case the table isn't there.
    g_list[0] = kCombatPauseList;
    *reinterpret_cast<std::int16_t*>(g_list + 0x14) = 0;   // title: PAUSE
    *reinterpret_cast<std::int16_t*>(g_list + 0x16) = 15;
    g_list[0x24] = 1;
    g_list[0x26] = 2;
    auto table = Read<std::uintptr_t>(kListTable);
    bool copied = false;
    if (table) {
        int count = *reinterpret_cast<const std::int32_t*>(table + 4);
        for (int i = 0; i < count && i < 256; ++i) {
            auto entry = reinterpret_cast<const std::uint8_t*>(table + 8 + 0x40 * i);
            if (*reinterpret_cast<const std::uint16_t*>(entry) != kCombatPauseList) continue;
            std::memcpy(g_list, entry, sizeof(g_list));
            copied = true;
            break;
        }
    }
    g_list[2] = 2;  // items
    g_list[3] = 0;  // cursor on the first
    auto items = reinterpret_cast<std::uint16_t*>(g_list + 4);
    items[0] = kKindFirst;
    items[1] = kMsgFirst;
    items[2] = kKindSecond;
    items[3] = kMsgSecond;
    *reinterpret_cast<std::uint32_t*>(g_list + 0x18) = 1;  // dummy: makes the line sprite; HookFill sets its text
    g_list[0x25] &= static_cast<std::uint8_t>(~1u);
    if (!copied) Log("link menu: the game's combat pause list wasn't found; using the values read from the file");
}

}  // namespace

void LinkMenuInit(bool host) {
    g_host = host;
    SetTexts(1, nullptr);
    *reinterpret_cast<void**>(g_def + 0x28) = reinterpret_cast<void*>(OnItem);
    g_press = InputInjectEnable();
    bool open = std::memcmp(reinterpret_cast<const void*>(ExeBase() + kOpenPause), kOpenPauseBytes,
                            sizeof(kOpenPauseBytes)) == 0;
    g_on = open &&
           std::memcmp(reinterpret_cast<const void*>(ExeBase() + kCloseMenu), kCloseMenuBytes,
                       sizeof(kCloseMenuBytes)) == 0 &&
           HookFunction(kMessage, kMessageBytes, sizeof(kMessageBytes), reinterpret_cast<void*>(HookMessage),
                        reinterpret_cast<void**>(&g_realMessage), "message lookup") &&
           HookFunction(kFillItems, kFillItemsBytes, sizeof(kFillItemsBytes), reinterpret_cast<void*>(HookFill),
                        reinterpret_cast<void**>(&g_realFill), "pause item fill");
    Log("link menu: %s%s", g_on ? "on: the 'Connection lost' screen opens while the other game is gone"
                                : "OFF: the pause menu code differs (other game build?); choices only with debug_cmd link",
        g_on && !g_press ? " (no input hook: when the other game is back the player closes it with Start)" : "");
}

bool LinkMenuUp() { return g_up; }  // until the menu is shut, also after a pick

void LinkMenuFrame(int screen, const char* line) {
    if (!g_on) return;
    ++g_frame;
    bool wanted = screen != 0 && (!g_up || screen == g_screen);  // another screen wanted: this one closes first
    if (g_up) {
        bool open = Read<std::uint8_t>(kOpenMenu) != 0xFF;
        if (open) {
            g_seen = true;
        } else if (g_seen || g_frame - g_openedAt > kOpenGrace) {
            if (wanted && !g_choice)
                Log(g_seen ? "link menu: closed without a choice (Start or Back); it opens again"
                           : "link menu: the screen didn't open; trying again");
            g_up = g_seen = false;
            g_closePresses = 0;
            g_closedAt = g_frame;
        }
    }
    if (!wanted) {
        // g_up stays set until the menu is shut, so pause sync doesn't take the closing for a player's pause.
        if (!g_up || !g_seen || !g_press || g_closePresses >= 3 || g_choice) return;
        if (g_closePresses > 0 && g_frame - g_pressedAt < kPressGap) return;
        if (g_closePresses == 0)
            Log("link menu: closing the '%s' screen: %s (Start press)", ScreenName(g_screen),
                screen ? "another screen is wanted" : g_screen == 2 ? "no longer needed" : "the other game is back");
        InputInject(kStart, 3);  // held 3 input reads, then released: one press
        g_pressedAt = g_frame;
        if (++g_closePresses == 3) Log("link menu: still open after 3 Start presses; the player can close it with Start");
        return;
    }
    if (g_up || g_choice || g_frame - g_closedAt < kReopenGap || !MayOpen()) return;
    if (!g_list[2]) BuildList();
    g_screen = screen;
    SetTexts(screen, line);
    g_up = true;
    g_seen = false;
    g_openedAt = g_frame;
    ++g_opens;
    if (g_opens <= 20) Log("link menu: opening the '%s' screen", ScreenName(screen));
    reinterpret_cast<PFN_OpenPause>(ExeBase() + kOpenPause)(g_list, g_def);
}

int LinkMenuTakeChoice() {
    if (!g_choice || g_up || Read<std::uint8_t>(kOpenMenu) != 0xFF || g_frame - g_closedAt < kSettleFrames) return 0;
    int c = g_choice;
    g_choice = 0;
    return c;
}

}  // namespace kh2coop
