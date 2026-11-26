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
// - The cutscene pause in co-op (TODO 1.227, your picks 2026-11-17): a list the game opens with a Skip Scene row
//   (kinds 5 and 0x13, L67) is opened from our copy of it: the Client's without that row, since only the Host skips
//   (skip_sync.cpp); a line under the title tells the player who did not pause who did, and the Client that the Host
//   decides. With the other game gone, or skip sync
//   off, the game's own list opens.
// - Our screens take no pick for their first 1.5 s (TODO 6.15, your pick 2026-11-23: an attack press picked Continue
//   Alone 0.3 s after the screen opened, twice in one run): the input hook hides Cross and Circle from the game until
//   the time is up and both are released, since the menu acts on a new press and a held one would count as new.
// - When the other game is back we close it the way a player does, with a Start press (as pause sync
//   does): calling the close from outside the menu (exe+0x2EB790 + exe+0xBEE658 = 1) crashed the game 70 ms
//   later in its sprite list (exe+0x166E4C, bench session 27).

#include "link_menu.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "game.hpp"
#include "game_over.hpp"
#include "gummi_menu.hpp"
#include "pause_sync.hpp"
#include "skip_sync.hpp"
#include "story_sync.hpp"
#include "target_row.hpp"

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
// The Start watcher exe+0x1572B0 opens a pause only when all of these allow it.
constexpr std::uintptr_t kPauseBlock = 0xABB878;  // u32 pause blockers, 0 = none
constexpr std::uintptr_t kBusy1 = 0x9006B0;       // u8 (exe+0x1515C0)
constexpr std::uintptr_t kBusy2 = 0xABB4B8;       // u32 (exe+0x156DC0()+0x108)
constexpr std::uintptr_t kBusy3 = 0xAC0F48;       // u64 (exe+0x19BA60)
constexpr std::uintptr_t kMenuLock = 0x9A8738;    // u8: exe+0x2E4950 opens nothing while set

constexpr std::uint16_t kStart = 0x0008;  // raw slot button bit (PS2 layout)
constexpr std::uint16_t kPickButtons = 0x4000 | 0x2000;  // Cross and Circle: confirm and cancel, either way round
constexpr std::uint64_t kPickWaitMs = 1500;  // our screens take no pick this long after they open (TODO 6.15)
constexpr std::uint16_t kCombatPauseList = 6;
constexpr std::uint16_t kKindFirst = 0x40, kKindSecond = 0x41;    // unknown to the game: go to our callback
constexpr std::uint16_t kMsgFirst = 0x7F40, kMsgSecond = 0x7F41;  // not in sys.bar (its ids end at 0x562D)
constexpr std::uint16_t kMsgGummi = 0x7F42;  // the Gummi menu help bar's (gummi_menu.cpp); 0x7F43-0x7F44 target_row.cpp's
constexpr std::uint16_t kKindBack = 0x42, kMsgBack = 0x7F45;  // 'Back to Menu' (TODO 1.286, screen 4)
// The calls the shared Start menu makes (TODO 1.286, F936-F938, L2114, L2126), each checked before its first use.
constexpr std::uint8_t kMenuOpenKindBytes[] = {0x40, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uint8_t kMenuSetTaskBytes[] = {0x80, 0x3D, 0x39, 0xEF, 0x7A, 0x00, 0x00, 0x74, 0x0F};
constexpr std::uint8_t kForcePauseListBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::int32_t kPauseTask = 0x32, kStartMenuTask = 8;  // the open task of a pause and of the Start menu (F936)
constexpr std::uint32_t kFieldSuspended = 2;  // exe+0x716884 while a menu is up (F939)

using PFN_OpenPause = void(__fastcall*)(void* list, void* def);
using PFN_Void = void(__fastcall*)();
using PFN_Message = const std::uint8_t*(__fastcall*)(int id);

PFN_Message g_realMessage = nullptr;
PFN_OpenPause g_realOpen = nullptr;
PFN_Void g_realFill = nullptr;
bool g_on = false, g_host = false, g_press = false;  // g_press: the Start press works (input hook in)

alignas(8) std::uint8_t g_list[0x40] {};
alignas(8) std::uint8_t g_def[0x30] {};
std::uint8_t g_textFirst[32], g_textSecond[32], g_textLine[96];
constexpr std::uint16_t kKindSkip = 5, kKindSkipAlt = 0x13;  // a pause row that skips the scene (L67)
alignas(8) std::uint8_t g_sceneList[0x40] {};  // our copy of a game list (HookOpenPause)
std::uint8_t g_sceneLine[128];
bool g_sceneOurLine = false;  // its line under the title is ours (the game's list had none)
bool g_sceneVote = false;     // that list is a Retry / Quit Game Over's: its line goes below Sora, smaller
constexpr std::int32_t kVoteLineDown = 29, kVoteLineSize = 12;  // its y (the game sets -189; 1 unit = 1.7 px of 720); the size
int g_screen = 0;  // the screen open or opening: 1 'Connection lost', 2 'different save', 3 'wait', 4 'Back to Menu'
alignas(8) std::uint8_t g_waitList[0x40] {};  // the wait screen's: our list with no items (TODO 1.286)
// Screen 4 (TODO 1.286's redesign): PAUSE with the wait line and 'Back to Menu', for the player who left the shared
// Start menu first. Queued under their closing Start menu (LinkMenuQueueBack), or opened as the other screens are.
alignas(8) std::uint8_t g_backList[0x40] {};
std::uint8_t g_textBack[32], g_backLine[96];
bool g_canStart = false, g_canSetTask = false, g_canForceList = false;  // the three calls' code is as checked
bool g_queued = false;        // screen 4 was queued under a Start menu and hasn't opened yet
bool g_backPicked = false;    // 'Back to Menu' picked: taken by pause sync (LinkMenuTakeBack)
bool g_backClosing = false;   // ... and screen 4 is closing by itself: no Start press of ours into it (or the next menu)
bool g_picksFreed = false;    // screen 4: Cross and Circle work again (1.5 s, released); Start stays hidden
int g_lostFrames = 0;         // frames with no menu running while our queued screen 4 hasn't opened
std::uint32_t g_soundBefore = 0;  // the sound's pause depth just before our queue
bool g_dropArmed = false;     // 'menu drop' (bench): the next queue is lost on purpose, to try the recovery
std::uint64_t g_lockedAt = 0; // frame we set kMenuLock for that
bool g_trace = false;
std::uint32_t g_traceLines = 0;
struct MenuBytes {
    std::uint8_t menu, busy, done;
    std::int32_t task, queued;
    std::uint32_t field, scene;
    bool operator==(const MenuBytes& o) const {
        return menu == o.menu && busy == o.busy && done == o.done && task == o.task && queued == o.queued &&
               field == o.field && scene == o.scene;
    }
};
MenuBytes g_traceLast{};

bool g_up = false;            // we opened it and it hasn't closed yet (any menu up since then is ours)
bool g_seen = false;          // the menu byte showed it open
int g_closePresses = 0;       // Start presses sent to close it because the other game is back
std::uint64_t g_frame = 0, g_openedAt = 0, g_closedAt = 0, g_pressedAt = 0;
int g_choice = 0;             // picked on our menu, not yet taken by link_watch
std::uint64_t g_pickBlockedAt = 0;  // GetTickCount64 when our screen opened with its picks blocked; 0 = not blocked
std::uint32_t g_opens = 0;
// shortcut: the four frame counts below guess the pause menu's steps (shown, torn down, a press taken, updates back);
// a slow frame can miss one. Real state: the menu byte per step and the copy's update running. TODO 8.11.
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

const char* ScreenName(int screen) {
    return screen == 4 ? "Back to Menu" : screen == 3 ? "wait" : screen == 2 ? "different save" : "Connection lost";
}

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
        else if (c == '(') b = 0x5A;  // OpenKH InternationalSystemDecode
        else if (c == ')') b = 0x5B;
        else if (c >= '0' && c <= '9') b = static_cast<std::uint8_t>(0x90 + (c - '0'));
        out[n++] = b;
    }
    out[n] = 0;
}

namespace {

void Encode(const char* s, std::uint8_t* out, std::size_t size) { LinkMenuEncode(s, out, size); }

// The screen's texts, set just before it opens.
void SetTexts(int screen, const char* line) {
    if (screen == 4) {
        Encode("Back to Menu", g_textBack, sizeof(g_textBack));
        Encode(line ? line : "", g_backLine, sizeof(g_backLine));
        return;
    }
    if (screen == 3) {
        Encode(line ? line : "", g_textLine, sizeof(g_textLine));
        return;
    }
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
    if (kind == kKindBack) {
        // From our pause's confirm handler (exe+0x31DD70): the pause is open and busy. The Start menu goes in the
        // queue as task 0 (F938; task 8 would close at once, L2125; the opener itself drops it under a pause), with
        // the arg the opener writes for it, then the pause closes as Continue does and the menu end opens it.
        // Never over a task already queued (L2122: it would be lost).
        std::uint8_t busy = 0;
        std::int32_t queued = 0;
        if (g_canSetTask && ReadAny(ExeBase() + kBusy1, &busy) && busy &&
            ReadAny(ExeBase() + game::kMenuQueued, &queued) && queued == -1) {
            reinterpret_cast<void(__fastcall*)(int)>(ExeBase() + game::kMenuSetTask)(0);
            *reinterpret_cast<volatile std::int32_t*>(ExeBase() + game::kStartMenuArg) = 1;
            g_backPicked = g_backClosing = g_up && g_screen == 4;  // the lab's list ('menu wait') tells nobody
            Log("link menu: 'Back to Menu' picked: the Start menu queued (task 0); the pause closes");
        } else {  // not busy: exe+0x151770 would replace the open task instead of queueing
            Log("link menu: 'Back to Menu' picked, but nothing could be queued (busy %u, queued %d): the pause closes",
                busy, queued);
        }
        reinterpret_cast<PFN_Void>(ExeBase() + kCloseMenu)();
        return;
    }
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
    if ((id & 0x7FFF) == kMsgBack) return g_textBack;
    if ((id & 0x7FFF) == kMsgGummi)
        if (const std::uint8_t* t = GummiMenuText()) return t;
    if (const std::uint8_t* t = TargetRowText(id & 0x7FFF)) return t;  // the other Sora's target row (TODO 3.17)
    return g_realMessage(id);
}

void __fastcall HookFill() {
    g_realFill();
    const auto list = Read<std::uintptr_t>(kMenuList);
    const std::uint8_t* line = list == reinterpret_cast<std::uintptr_t>(g_list) ||
                                       list == reinterpret_cast<std::uintptr_t>(g_waitList) ? g_textLine
                               : list == reinterpret_cast<std::uintptr_t>(g_sceneList) && g_sceneOurLine ? g_sceneLine
                               : list == reinterpret_cast<std::uintptr_t>(g_backList) ? g_backLine
                               : nullptr;
    auto sprites = Read<std::uintptr_t>(kMenuSprites);
    if (!line || !sprites) return;
    *reinterpret_cast<const std::uint8_t**>(sprites + kLineText) = line;
}

// Every pause-type menu opens through here (the Start watcher, Game Over, ours). A game list is opened from our copy
// in two cases: it has a Skip Scene row while skips are the Host's (TODO 1.227), or it is a mission's Retry / Quit
// Game Over where both players pick (TODO 1.228), whose line game_over.cpp writes through LinkMenuLine.
void __fastcall HookOpenPause(void* list, void* def) {
    std::uint64_t copy[8];
    // Our one copy isn't overwritten under a menu still open from it.
    bool read = list && list != g_list && list != g_waitList && list != g_backList &&
                !(Read<std::uintptr_t>(kMenuList) == reinterpret_cast<std::uintptr_t>(g_sceneList) &&
                  Read<std::uint8_t>(kOpenMenu) != 0xFF);
    for (int i = 0; read && i < 8; ++i) read = ReadAny(reinterpret_cast<std::uintptr_t>(list) + 8 * i, &copy[i]);
    if (read) {
        std::uint8_t ours[sizeof(g_sceneList)];
        std::memcpy(ours, copy, sizeof(ours));
        auto items = reinterpret_cast<std::uint16_t*>(ours + 4);  // {kind, message} x count
        auto text = reinterpret_cast<std::uint32_t*>(ours + 0x18);
        const int count = ours[2] < 4 ? ours[2] : 4;
        const bool hostSkips = SkipSyncHostOnly();
        int kept = 0;
        bool skip = false, retry = false, quit = false;
        for (int i = 0; i < count; ++i) {
            const bool row = hostSkips && (items[2 * i] == kKindSkip || items[2 * i] == kKindSkipAlt);
            skip = skip || row;
            retry = retry || items[2 * i] == 3;  // a mission's Retry and Quit rows (game.hpp, L1522)
            quit = quit || items[2 * i] == 4;
            if (row && !g_host) continue;  // the Client can't skip: no row for it
            items[2 * kept] = items[2 * i];
            items[2 * kept + 1] = items[2 * i + 1];
            ++kept;
        }
        if (kept == 0) skip = false;  // a list of skip rows only: the game's own stays, never an empty menu
        const bool theirs = PauseSyncMatching();  // a press of ours = the other player paused
        const char* line = nullptr;
        // Who paused is told only to the player who didn't (your pick 2026-11-17); the Client also that the Host decides.
        if (skip)
            line = g_host ? (theirs ? "The Client paused." : nullptr)
                          : (theirs ? "The Host paused.\nThe Host is deciding." : "The Host is deciding.");
        else if (retry && quit && GameOverSyncBothPick())
            line = "";  // game_over.cpp writes it as the picks come in
        // A fight pause the other player opened: it says who (TODO 1.286, your pick 2026-11-25).
        const bool followed = !line && theirs && *reinterpret_cast<std::uint16_t*>(ours) == kCombatPauseList;
        if (followed) line = g_host ? "The Client paused." : "The Host paused.";
        if (skip || line) {
            g_sceneVote = !skip && !followed;
            if (skip) {
                ours[2] = static_cast<std::uint8_t>(kept);
                if (ours[3] >= kept) ours[3] = 0;  // the cursor
            }
            g_sceneOurLine = line && *text == 0;  // a line of the game's own stays
            if (g_sceneOurLine) {
                *text = 1;  // dummy, as BuildList: makes the line sprite; HookFill sets its text
                Encode(line, g_sceneLine, sizeof(g_sceneLine));
            }
            std::memcpy(g_sceneList, ours, sizeof(g_sceneList));
            if (followed)
                Log("link menu: a fight pause the other player opened: %s", g_sceneOurLine ? "who paused shown" : "the list has its own line");
            else if (skip)
                Log("link menu: a pause with Skip Scene (list %u, %d rows): %d shown, %s paused, %s",
                    *reinterpret_cast<std::uint16_t*>(ours), count, kept, g_host != theirs ? "the Host" : "the Client",
                    g_sceneOurLine ? "our line shown" : "no line of ours");
            else
                Log("link menu: a Retry / Quit Game Over (list %u, %d rows): %s", *reinterpret_cast<std::uint16_t*>(ours),
                    count, g_sceneOurLine ? "the picks show in our line" : "the list has its own line, the picks aren't shown");
            list = g_sceneList;
        }
    }
    g_realOpen(list, def);
}

bool MayOpen() {
    return Read<std::uint8_t>(game::kNow) != 0xFF && Read<std::uint8_t>(kOpenMenu) == 0xFF &&
           Read<std::uint32_t>(game::kSoraSlot) != 0 && Read<std::uint32_t>(kPauseBlock) == 0 &&
           Read<std::uint8_t>(game::kInField) != 0 && Read<std::uint8_t>(kBusy1) == 0 &&
           Read<std::uint32_t>(kBusy2) == 0 && Read<std::uint64_t>(kBusy3) == 0 &&
           Read<std::uint8_t>(kMenuLock) == 0;
}

bool ReadMenuBytes(MenuBytes& b) {
    const std::uintptr_t e = ExeBase();
    return ReadAny(e + kOpenMenu, &b.menu) && ReadAny(e + kBusy1, &b.busy) && ReadAny(e + game::kMenuDone, &b.done) &&
           ReadAny(e + game::kMenuTask, &b.task) && ReadAny(e + game::kMenuQueued, &b.queued) &&
           ReadAny(e + game::kFieldScene, &b.field) && ReadAny(e + game::kMenuScene, &b.scene);
}

void FormatMenuBytes(const MenuBytes& b, char* out, std::size_t size) {
    std::snprintf(out, size, "menu %02X busy %u task %d queued %d field %u menu scene %u done %u", b.menu, b.busy,
                  b.task, b.queued, b.field, b.scene, b.done);
}

void TraceMenu() {  // the menu lab's trace: a line on each frame the bytes differ from the last
    MenuBytes b{};
    if (!ReadMenuBytes(b) || b == g_traceLast) return;
    g_traceLast = b;
    if (g_traceLines++ >= 3000) return;
    char text[128];
    FormatMenuBytes(b, text, sizeof(text));
    Log("menu trace: frame %llu: %s", static_cast<unsigned long long>(g_frame), text);
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

// Screen 4's list: ours with one item, 'Back to Menu', the cursor on it.
void BuildBackList() {
    if (!g_list[2]) BuildList();
    std::memcpy(g_backList, g_list, sizeof(g_backList));
    g_backList[2] = 1;
    g_backList[3] = 0;
    auto items = reinterpret_cast<std::uint16_t*>(g_backList + 4);
    items[0] = kKindBack;
    items[1] = kMsgBack;
}

// Screen 4 is ours while its list is the open menu (byte 0x08); while it is still coming, a pause task is queued or
// launching. The byte alone also reads the Start menu it was queued under, the one 'Back to Menu' opens, and 0xFF in
// the hand-over frame between them (F937, F938): that 0xFF is no close.
bool BackListOpen() {
    return Read<std::uintptr_t>(kMenuList) == reinterpret_cast<std::uintptr_t>(g_backList) &&
           Read<std::uint8_t>(kOpenMenu) == 0x08;
}

bool BackComing() {
    return Read<std::int32_t>(game::kMenuQueued) == kPauseTask || Read<std::int32_t>(game::kMenuTask) == kPauseTask;
}

// A queued screen 4 that the menu end never opened (L2121, L2122, F945): no menu runs, yet our request set the Start
// watcher's mask bit 0 (the menu end left it set: F945) and pushed the sound's pause. Undo them, and take back a
// pause task left in the queue so a later menu end doesn't open it out of nowhere. Then screen 4 opens the direct way.
// The sound: one pop only if the depth is above what it was before our request (in the one bench run, F945, it wasn't,
// and the depth was back at 0 at rest).
void RecoverLostQueue() {
    const std::uintptr_t e = ExeBase();
    std::uint32_t mask = 0, sound = 0;
    std::int32_t queued = 0;
    ReadAny(e + kPauseBlock, &mask);
    ReadAny(e + game::kSoundPause, &sound);
    ReadAny(e + game::kMenuQueued, &queued);
    Log("link menu: our PAUSE's queue was lost (mask 0x%X, sound depth %u, %u before the queue, queued %d): undoing it",
        mask, sound, g_soundBefore, queued);
    if (queued == kPauseTask) *reinterpret_cast<volatile std::int32_t*>(e + game::kMenuQueued) = -1;
    if (mask & 1) *reinterpret_cast<volatile std::uint32_t*>(e + kPauseBlock) = mask & ~1u;
    if (sound > g_soundBefore) reinterpret_cast<PFN_Void>(e + kCloseMenu)();  // one pop: the depth our request pushed
    ReadAny(e + kPauseBlock, &mask);
    ReadAny(e + game::kSoundPause, &sound);
    Log("link menu: after the undo: mask 0x%X, sound depth %u", mask, sound);
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
    auto same = [](std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n) {
        return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0;
    };
    g_canStart = g_on && same(game::kMenuOpenKind, kMenuOpenKindBytes, sizeof(kMenuOpenKindBytes));
    g_canSetTask = g_on && same(game::kMenuSetTask, kMenuSetTaskBytes, sizeof(kMenuSetTaskBytes));
    g_canForceList = g_on && same(game::kForcePauseList, kForcePauseListBytes, sizeof(kForcePauseListBytes));
    if (g_on && !(g_canStart && g_canSetTask && g_canForceList))
        Log("link menu: the shared Start menu's game code differs (opener %d, queue %d, pause-list check %d): "
            "the other player's Start menu isn't followed", g_canStart, g_canSetTask, g_canForceList);
    // Its own hook: without it only the cutscene pause's co-op look is lost (TODO 1.227).
    if (g_on && !HookFunction(kOpenPause, kOpenPauseBytes, sizeof(kOpenPauseBytes), reinterpret_cast<void*>(HookOpenPause),
                              reinterpret_cast<void**>(&g_realOpen), "pause menu open"))
        Log("link menu: no hook on the pause's open: the cutscene pause stays the game's own");
    Log("link menu: %s%s", g_on ? "on: the 'Connection lost' screen opens while the other game is gone"
                                : "OFF: the pause menu code differs (other game build?); choices only with debug_cmd link",
        g_on && !g_press ? " (no input hook: when the other game is back the player closes it with Start)" : "");
}

bool LinkMenuOn() { return g_on; }

// Your ask 2026-11-17: the picks below Sora's feet, a bit smaller. The line sprite's own offset (ints: +0x1C8 x,
// +0x1CC y, added to its layout's place at each draw) moves its frame and its text together; +0x20C is the text's
// size, 15 by default (F687). Each frame, from inside the menu's own update: written once after the fill, the
// game put the offset back to 0.
void LinkMenuVoteLineTick() {
    auto sprites = Read<std::uintptr_t>(kMenuSprites);
    if (!g_sceneVote || !g_sceneOurLine || !sprites || Read<std::uint8_t>(kOpenMenu) == 0xFF ||
        Read<std::uintptr_t>(kMenuList) != reinterpret_cast<std::uintptr_t>(g_sceneList))
        return;
    *reinterpret_cast<std::int32_t*>(sprites + 0x220 + 0x1CC) = kVoteLineDown;
    *reinterpret_cast<std::int32_t*>(sprites + 0x220 + 0x20C) = kVoteLineSize;
}

void LinkMenuLine(const char* text) { Encode(text, g_sceneLine, sizeof(g_sceneLine)); }

bool LinkMenuUp() { return g_up; }  // until the menu is shut, also after a pick

// As OnItem above does, and only from inside a pause row's handler (from outside it crashed the game, see the top).
void LinkMenuPauseContinue() {
    if (g_on) reinterpret_cast<PFN_Void>(ExeBase() + kCloseMenu)();
}

void LinkMenuFrame(int screen, const char* line) {
    if (!g_on) return;
    ++g_frame;
    if (g_trace) TraceMenu();
    bool wanted = screen != 0 && (!g_up || screen == g_screen);  // another screen wanted: this one closes first
    if (g_lockedAt && (Read<std::uint8_t>(kBusy1) == 0 || g_frame - g_lockedAt > kOpenGrace)) {
        *reinterpret_cast<volatile std::uint8_t*>(ExeBase() + kMenuLock) = 0;  // the 'menu drop' lock, ours to take off
        g_lockedAt = 0;
        Log("link menu: menu lab: kMenuLock taken off again");
    }
    if (g_up) {
        bool open, gone;
        if (g_screen == 4) {
            open = BackListOpen();
            // Queued and never opened: the Start menu closed and no menu runs for 2 frames (the hand-over keeps busy set).
            // shortcut: 2 frames with nothing busy is a guess at 'the menu end has run'; upgrade: a hook on the menu end
            // exe+0x150CA0 telling what it opened.
            g_lostFrames = g_queued && !g_seen && !open && Read<std::uint8_t>(kBusy1) == 0 &&
                           Read<std::uint8_t>(kOpenMenu) == 0xFF && Read<std::int32_t>(game::kMenuTask) != kPauseTask
                               ? g_lostFrames + 1 : 0;
            if (g_lostFrames >= 2) RecoverLostQueue();
            gone = g_lostFrames >= 2 ||
                   (!open && !BackComing() && (g_seen || (!g_queued && g_frame - g_openedAt > kOpenGrace)));
        } else {
            open = Read<std::uint8_t>(kOpenMenu) != 0xFF;
            gone = !open && (g_seen || g_frame - g_openedAt > kOpenGrace);
        }
        if (open) {
            if (!g_seen && g_screen == 4 && g_press) {  // its buttons hidden once it is up, not under the Start menu
                g_pickBlockedAt = GetTickCount64();
                g_picksFreed = false;
                InputBlockMenu(static_cast<std::uint16_t>(kPickButtons | kStart));
            }
            g_seen = true;
        } else if (gone) {
            if (wanted && !g_choice && !g_backPicked)
                Log(g_seen ? "link menu: closed without a choice (Start or Back); it opens again"
                           : "link menu: the screen didn't open; trying again");
            const bool lost = g_lostFrames >= 2;
            g_up = g_seen = g_queued = g_backClosing = false;
            g_backPicked = false;  // pause sync took it frames ago (the close takes several); a stale one would block us
            g_lostFrames = 0;
            g_closePresses = 0;
            g_closedAt = lost ? g_frame - kReopenGap : g_frame;  // a lost queue had no pause to tear down
        }
    }
    if (g_pickBlockedAt) {
        const std::uint64_t waited = GetTickCount64() - g_pickBlockedAt;
        // The wait screen keeps Start and the picks hidden while it's up (only the one who paused ends it), and until
        // they're let go after: one still held would count as a new press (TODO 1.286). Screen 4 frees its pick after
        // 1.5 s, once released, and keeps Start hidden while it's up.
        const std::uint16_t raw = InputRawButtons();
        if (g_screen == 4 && g_up && !g_picksFreed && waited >= kPickWaitMs && !(raw & kPickButtons)) {
            InputBlockMenu(kStart);
            g_picksFreed = true;
        }
        if (g_screen >= 3 ? !g_up && !(raw & (kPickButtons | kStart))
                          : !g_up || (waited >= kPickWaitMs && !(raw & kPickButtons))) {
            InputBlockMenu(0);
            g_pickBlockedAt = 0;
            if (g_opens <= 20)
                Log("link menu: picks taken again after %llu ms (%s)", static_cast<unsigned long long>(waited),
                    g_up ? "the wait is over and the buttons are released" : "the screen closed");
        }
    }
    if (!wanted) {
        // g_up stays set until the menu is shut, so pause sync doesn't take the closing for a player's pause.
        if (!g_up || !g_seen || !g_press || g_closePresses >= 3 || g_choice || g_backClosing) return;
        if (g_closePresses > 0 && g_frame - g_pressedAt < kPressGap) return;
        if (g_closePresses == 0)
            Log("link menu: closing the '%s' screen: %s (Start press)", ScreenName(g_screen),
                screen ? "another screen is wanted" : g_screen == 3 ? "the other player is done"
                : g_screen == 2 ? "no longer needed" : "the other game is back");
        InputInject(kStart, 3);  // held 3 input reads, then released: one press
        g_pressedAt = g_frame;
        if (++g_closePresses == 3) {
            if (g_screen >= 3) InputBlockMenu(kPickButtons);  // Start is the player's way out, as on the other screens
            Log("link menu: still open after 3 Start presses; the player can close it with Start");
        }
        return;
    }
    if (g_up || g_choice || g_backPicked || g_frame - g_closedAt < kReopenGap || !MayOpen()) return;
    if (!g_list[2]) BuildList();
    g_screen = screen;
    SetTexts(screen, line);
    g_up = true;
    g_seen = g_queued = false;
    g_openedAt = g_frame;
    ++g_opens;
    void* list = g_list;
    if (screen == 3) {  // the same list with no items
        std::memcpy(g_waitList, g_list, sizeof(g_waitList));
        g_waitList[2] = g_waitList[3] = 0;
        list = g_waitList;
    }
    if (screen == 4) {  // not queued under the Start menu: a quick tap, or the queue missed or lost (TODO 1.286)
        BuildBackList();
        Log("link menu: our PAUSE opened directly (with 'Back to Menu')");
        reinterpret_cast<PFN_OpenPause>(ExeBase() + kOpenPause)(g_backList, g_def);  // its buttons: once it's up
        return;
    }
    if (g_press) {  // no input hook: nothing to block with
        g_pickBlockedAt = GetTickCount64();
        InputBlockMenu(screen == 3 ? static_cast<std::uint16_t>(kPickButtons | kStart) : kPickButtons);
    }
    if (g_opens <= 20)
        Log("link menu: opening the '%s' screen%s", ScreenName(screen),
            !g_press ? "" : screen == 3 ? " (Start and picks hidden while it's up)"
                                        : " (no pick for 1.5 s, then once Cross and Circle are released)");
    reinterpret_cast<PFN_OpenPause>(ExeBase() + kOpenPause)(list, g_def);
}

int LinkMenuTakeChoice() {
    if (!g_choice || g_up || Read<std::uint8_t>(kOpenMenu) != 0xFF || g_frame - g_closedAt < kSettleFrames) return 0;
    int c = g_choice;
    g_choice = 0;
    return c;
}

// TODO 1.286 (your pick 'Queue in hand-over'): screen 4 requested under our closing Start menu, so the menu end opens
// it the next frame with no world frame between (F937). From PauseSyncFrame in the frame the close shows (menu scene
// 3): a want sent through LinkWatchFrame would come a frame late. The guards are what loses a queued pause (L2121,
// L2122): its request already set the watcher's mask bit and pushed the sound, so it is made only where it can open.
bool LinkMenuQueueBack(const char* line) {
    if (!g_on || g_up || g_choice || g_backPicked) return false;
    const std::uintptr_t e = ExeBase();
    std::int32_t task = -1, queued = 0;
    std::uint32_t field = 0, remap = 4;
    std::uint8_t busy = 0, lock = 1;
    if (!ReadAny(e + game::kMenuTask, &task) || task != kStartMenuTask || !ReadAny(e + kBusy1, &busy) || !busy ||
        !ReadAny(e + game::kFieldScene, &field) || field != kFieldSuspended || !ReadAny(e + kMenuLock, &lock) || lock ||
        !ReadAny(e + game::kMenuRemap, &remap) || (remap & 4) || !ReadAny(e + game::kMenuQueued, &queued) ||
        queued != -1) {
        Log("link menu: our PAUSE not queued: task %d, busy %u, field %u, lock %u, remap 0x%X, queued %d (it opens "
            "directly after the close)", task, busy, field, lock, remap, queued);
        return false;
    }
    BuildBackList();
    g_screen = 4;
    SetTexts(4, line);
    g_up = g_queued = true;
    g_seen = g_picksFreed = false;
    g_lostFrames = 0;
    g_openedAt = g_frame;
    ++g_opens;
    ReadAny(e + game::kSoundPause, &g_soundBefore);
    reinterpret_cast<PFN_OpenPause>(e + kOpenPause)(g_backList, g_def);
    ReadAny(e + game::kMenuQueued, &queued);
    if (queued != kPauseTask) {  // dropped: the next frames find no pause coming and undo it (RecoverLostQueue)
        Log("link menu: our PAUSE's request didn't queue (queued %d)", queued);
        return true;
    }
    Log("link menu: our PAUSE queued under the closing Start menu");
    if (g_dropArmed) {  // 'menu drop': the menu end then opens nothing (L2116), as a lost queue would
        g_dropArmed = false;
        g_lockedAt = g_frame;
        *reinterpret_cast<volatile std::uint8_t*>(e + kMenuLock) = 1;
        Log("link menu: menu lab: kMenuLock set over the hand-over: this queue gets lost");
    }
    return true;
}

bool LinkMenuTakeBack() {
    const bool picked = g_backPicked;
    g_backPicked = false;
    return picked;
}

bool LinkMenuBackUp() { return g_up && g_screen == 4; }

bool LinkMenuBackOpen() { return g_up && g_screen == 4 && g_seen && BackListOpen(); }

// Could the Start watcher (exe+0x1572B0, L2114) open the Start menu now, but for the Start press, and the opener
// (exe+0x2E4950) take it? nullptr = yes; else why not, with *hold set when the game's Start would open a pause list
// here instead (a mission's, an RC's, a Limit's: L2126), the case where our game shows the wait screen.
const char* LinkMenuStartRefusal(bool* hold) {
    *hold = false;
    if (!g_on || !g_canStart || !g_canForceList) return "the opener's code differs";
    if (Read<std::uint8_t>(game::kNow) == 0xFF || !Read<std::uint8_t>(game::kInField) ||
        Read<std::uint64_t>(game::kLoadTask) || Read<std::uint64_t>(game::kReaderBusy))
        return "a load";
    if (Read<std::uint32_t>(kBusy2)) return "a fade";
    if (Read<std::uint8_t>(kOpenMenu) != 0xFF || Read<std::uint8_t>(kBusy1)) return "a menu of the game's";
    if (Read<std::uint32_t>(kPauseBlock) || Read<std::uint64_t>(kBusy3) || Read<std::uint8_t>(kMenuLock))
        return "the game blocks its Start";
    for (int i = 1; i <= 5; ++i) {
        if (Read<std::uint16_t>(game::kPauseSlots + 0x40 * i)) {
            *hold = true;
            return "a pause list of a mission";
        }
    }
    if ((Read<std::uint32_t>(game::kTitleVariant) >> 25 & 1) ||
        reinterpret_cast<bool(__fastcall*)()>(ExeBase() + game::kForcePauseList)()) {
        *hold = true;
        return "an RC, a Limit or a held object";
    }
    return nullptr;
}

// exe+0x2E4950(0, -1), the Start watcher's own call (F936): only after LinkMenuStartRefusal said yes this frame.
void LinkMenuOpenStart() {
    if (g_on && g_canStart) reinterpret_cast<void(__fastcall*)(int, int)>(ExeBase() + game::kMenuOpenKind)(0, -1);
}

// TODO 1.286's redesign, step 1 (L2114-L2120): the Start menu's opener and queue tried on the bench before the build
// uses them. From DevCommand ('menu ...', tools/kh2cmd.py), in OnFrame before LinkWatchFrame: where our screens open
// from (exe+0x2E4C20 ends in the same opener, exe+0x2E4950(8)).
//   menu state          the menu's bytes, once
//   menu trace on|off   a log line each frame one of them changes
//   menu start          exe+0x2E4950(0, -1): the Start menu, as the Start watcher opens it out of a fight
//   menu wait           our PAUSE with the wait line and 'Back to Menu'; over an open menu it should queue (L2116)
void LinkMenuDebug(const char* args, void (*line)(const char* text, void* ctx), void* ctx) {
    while (*args == ' ') ++args;
    char text[160];
    MenuBytes b{};
    if (ReadMenuBytes(b)) FormatMenuBytes(b, text, sizeof(text));
    else strcpy_s(text, "the menu's bytes can't be read");
    if (!*args || std::strcmp(args, "state") == 0) {
        line(text, ctx);
        return;
    }
    if (std::strncmp(args, "trace", 5) == 0) {
        g_trace = std::strcmp(args + 5, " off") != 0;
        g_traceLast = {};
        g_traceLines = 0;
        Log("link menu: menu trace %s (%s)", g_trace ? "on" : "off", text);
        line(g_trace ? "trace on" : "trace off", ctx);
        return;
    }
    if (std::strcmp(args, "drop") == 0) {  // the absent case of the queue (L2121, L2122): see LinkMenuQueueBack
        g_dropArmed = true;
        Log("link menu: menu lab: the next queue of our PAUSE gets lost on purpose (kMenuLock over the hand-over)");
        line("drop armed", ctx);
        return;
    }
    const bool start = std::strcmp(args, "start") == 0;
    if (!start && std::strcmp(args, "wait") != 0) {
        line("error: menu state | trace on | trace off | start | wait | drop", ctx);
        return;
    }
    if (start && !g_canStart) {
        line("refused: the opener's code differs", ctx);
        return;
    }
    // Must exist: a loaded room in the field with our Sora and no pause blocker (the Start watcher's first test), and
    // our hooks (the item callback, the line). An open menu is allowed: what it does then is what the lab is for.
    const std::uintptr_t e = ExeBase();
    std::uint8_t now = 0xFF, inField = 0;
    std::uint32_t sora = 0, block = 1;
    if (!g_on || !ReadAny(e + game::kNow, &now) || now == 0xFF || !ReadAny(e + game::kInField, &inField) || !inField ||
        !ReadAny(e + game::kSoraSlot, &sora) || !sora || !ReadAny(e + kPauseBlock, &block) || block) {
        line("refused: not in the field with our Sora, a pause blocker set, or the link menu off", ctx);
        return;
    }
    if (start) {
        Log("link menu: menu lab: exe+0x2E4950(0, -1), the Start menu (%s)", text);
        reinterpret_cast<void(__fastcall*)(int, int)>(e + game::kMenuOpenKind)(0, -1);
        line("start asked", ctx);
        return;
    }
    BuildBackList();
    SetTexts(4, g_host ? "Waiting for the Client." : "Waiting for the Host.");
    Log("link menu: menu lab: our PAUSE with 'Back to Menu' (%s)", text);
    reinterpret_cast<PFN_OpenPause>(e + kOpenPause)(g_backList, g_def);
    line("wait asked", ctx);
}

}  // namespace kh2coop
