// Shared Game Over (GAMEOVER_SYNC=1 on both PCs, default on; friend's idea, user
// design 2026-10-06): only the host chooses. The friend's buttons are blocked
// on its Game Over menu while the host is connected, also when the host isn't
// at Game Over itself (user rule 2026-10-07, after a lone friend Continue hung
// on the door lock while the host's Mickey fought: the friend waits). When the
// host picks an option the mod picks the same one in the friend's game. With
// the host gone (no packets for 3 s) the friend chooses itself.
//
// The Game Over screen (bench 2026-10-06, Ghidra): object pointer exe+0x2AE8050,
// built by exe+0x3FC7A0, stepped by exe+0x3FCEC0; +0 kind, +0x10 state (6 =
// the Continue / Load menu is up), byte +0x14 = Mickey's rescue chosen. State 5
// opens the game's generic menu as type 8 (the menu byte exe+0x7435D0 reads 8)
// and the menu calls the object's callback exe+0x3FCBE0(option) on a choice.
// Continue calls it with 2 (bench 20:54). Load doesn't call it: the load
// screen opens on top, the next menu slot exe+0x7435D4 turns from -1 to 0x0C
// (bench 20:55), and the host reports that as option 1.
// The friend's cursor starts on Continue (screenshot) and its own input is
// blocked from then on, so Continue = ✕, Load = down, ✕; on the load screen
// the friend picks its file itself.
// A mission with the Retry / Quit pause (the cups, the Underdrome; its header's pause byte 2, 0x1E or 0x23) fails
// into Game Over kind 3: the same object and states, list 0x21, no callback. Its rows are kinds 3 (Retry) and 4
// (Quit), which the menu's confirm handler exe+0x31DD70 runs itself through exe+0x3A3DD0(kind): the host reports
// that call as its choice (two PCs 2026-11-15: the host's Retry never reached the friend, whose menu stayed up).
// The friend picks the host's row by its kind in the open menu, from where its cursor is.
// The pause of such a mission has the same Retry / Quit rows (the host's reload alone left the friend in the old
// fight, TODO 1.214): that choice is sent too, marked, and the friend's game calls exe+0x3A3DD0 itself once a
// room change may start, if its room has that jump.
// The Mickey rescue's white screen is left alone (mickey_sync.cpp: only the roll's winner chooses there); after its
// 'It's all over' the Continue / Load menu is shared like this one.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "follow.hpp"
#include "game.hpp"
#include "game_over.hpp"
#include "scene_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kGameOver = 0x2AE8050;  // Game Over object while the screen is up
constexpr std::uintptr_t kGoState = 0x10, kGoMickey = 0x14;
constexpr int kStateMenu = 6, kStateMickeyMenu = 8;
constexpr std::uintptr_t kMenuAbove = 0x7435D4;  // i32: the menu opened over the Game Over one (-1 none)
constexpr std::int32_t kMenuLoad = 0x0C;
// A choice is its row's kind: 2 Continue (the callback's argument), 1 Load, 3 Retry, 4 Quit.
constexpr std::int32_t kOptionContinue = 2, kOptionLoad = 1, kOptionRetry = 3, kOptionQuit = 4;
constexpr std::int32_t kFromPause = 0x100;  // on a sent choice: made in the pause, no Game Over
constexpr std::int32_t kJumpRetry = 0x8B, kJumpQuit = 0x8D;  // the room settings exe+0x3A3DD0 jumps by
constexpr std::uintptr_t kChoice = 0x3FCBE0;     // (option): the Game Over menu's callback
constexpr std::uint8_t kChoiceBytes[] = {0x48, 0x83, 0xEC, 0x58, 0x66, 0xC7, 0x44, 0x24, 0x30, 0xFF, 0xFF};
constexpr std::uint8_t kMissionChoiceBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x50};
constexpr std::uint16_t kCross = 0x4000, kDown = 0x0040;
// shortcut: the menu is guessed ready 60 frames after it shows (local). Real state: the Game Over menu's own state
// (not found yet). TODO 8.11.
constexpr int kMenuReadyFrames = 60;  // the menu fades in; presses before that may be lost
constexpr std::uint32_t kHostGoneMs = 3000;  // the host sends every 10 frames

constexpr std::uint32_t kMagic = 0x4732484B;  // "KH2G"
constexpr std::uint16_t kVersion = 1;
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t atMenu;     // the sender's Game Over menu is up
    std::uint32_t epoch;     // the host's choices so far
    std::int32_t option;     // the newest one (the callback's argument)
};
#pragma pack(pop)

using PFN_Choice = void(__fastcall*)(int);
PFN_Choice g_realChoice = nullptr, g_realMissionChoice = nullptr;
bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
// Host
std::uint32_t g_epoch = 0;
std::int32_t g_option = 0;
bool g_loadOpen = false;             // our load screen is open over the Game Over menu
bool g_atMenu = false;               // our Game Over menu was up at the last frame
// Both roles
std::uint64_t g_loadSeenMs = 0;      // when our load screen was last seen open over the Game Over menu
// Friend
bool g_peerAtMenu = false;
std::uint32_t g_peerMs = 0;          // when the last packet arrived (our clock)
std::uint32_t g_seenEpoch = 0;
bool g_pending = false;
std::int32_t g_pendingOption = 0;
std::uint64_t g_pendingSince = 0, g_menuSince = 0;
bool g_blocking = false;
int g_step = 0;                      // 0 idle, 1 moving the cursor, 2 done
std::uint64_t g_stepFrame = 0;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

// The Continue / Load menu: state 6 on the normal branch; on Mickey's branch after 'It's all over' the white
// screen's callback opens the same menu and sets state 8 (exe+0x3FCD0D), where the object stays while it is up.
bool LocalAtMenu() {
    std::uintptr_t go = Read<std::uintptr_t>(kGameOver);
    if (!go) return false;
    std::int32_t state = *reinterpret_cast<const volatile std::int32_t*>(go + kGoState);
    bool mickey = *reinterpret_cast<const volatile std::uint8_t*>(go + kGoMickey) != 0;
    return mickey ? state == kStateMickeyMenu : state == kStateMenu;
}

void Send(bool atMenu) {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), static_cast<std::uint8_t>(atMenu), g_epoch, g_option};
    AvatarLinkSendRaw(&p, sizeof(p));
}

void HostChose(std::int32_t option) {
    ++g_epoch;
    g_option = option;
    for (int i = 0; i < 3; ++i) Send(false);
}

void __fastcall HookChoice(int option) {
    Log("game over: our player chose option %d", option);
    if (g_on && g_host) HostChose(option);
    g_realChoice(option);
}

// The pause's Retry / Quit run this too.
void __fastcall HookMissionChoice(int kind) {
    Log("game over: our player chose the mission's row kind %d%s", kind, g_atMenu ? "" : " (in the pause)");
    if (g_on && g_host) HostChose(g_atMenu ? kind : kind | kFromPause);
    g_realMissionChoice(kind);
}

const char* OptionName(std::int32_t option) {
    return option == kOptionContinue ? "Continue" : option == kOptionLoad ? "Load"
           : option == kOptionRetry ? "Retry" : option == kOptionQuit ? "Quit" : "its row";
}

// How many Down presses take the open menu's cursor to the row of this kind; -1 = it has no such row.
int DownsTo(std::int32_t kind) {
    std::uintptr_t kinds = 0, cursor = 0;
    std::int16_t row = 0, scroll = 0, count = 0;
    if (!ReadAny(ExeBase() + game::kMenuRowKinds, &kinds) || !ReadAny(ExeBase() + game::kMenuCursor, &cursor) ||
        !kinds || !cursor || !ReadAny(cursor, &row) || !ReadAny(cursor + 0x12, &scroll) ||
        !ReadAny(cursor + 0x16, &count) || count <= 0 || count > 9)
        return -1;
    for (int i = 0; i < count; ++i) {
        std::int16_t k = -1;
        if (ReadAny(kinds + 2 * i, &k) && k == kind) return ((i - row - scroll) % count + count) % count;
    }
    return -1;
}

void Unblock(const char* why) {
    if (!g_blocking) return;
    g_blocking = false;
    InputBlock(0);
    Log("game over: our buttons work again (%s)", why);
}

}  // namespace

void GameOverSyncInit(bool host) {
    if (EnvInt("KH2COOP_GAMEOVER_SYNC", 1) != 1) return;
    g_host = host;
    if (!HookFunction(kChoice, kChoiceBytes, sizeof(kChoiceBytes), reinterpret_cast<void*>(HookChoice),
                      reinterpret_cast<void**>(&g_realChoice), "Game Over choice"))
        return;
    g_on = host || InputInjectEnable();
    if (g_on)  // the friend's too: it calls the function for the host's pause choice
        HookFunction(game::kMissionMenuChoice, kMissionChoiceBytes, sizeof(kMissionChoiceBytes),
                     reinterpret_cast<void*>(HookMissionChoice), reinterpret_cast<void**>(&g_realMissionChoice),
                     "Game Over choice (a mission's Retry / Quit)");
    Log("game over sync: %s", !g_on ? "OFF: the input hook could not be installed"
                              : host ? "on (when both players are at Game Over, our choice is the friend's too)"
                                     : "on (the host chooses at Game Over; we wait while it is connected)");
}

void GameOverSyncOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    g_peerAtMenu = p.atMenu != 0;
    g_peerMs = AvatarLinkNowMs();
    if (p.epoch > g_seenEpoch) {
        bool fresh = g_seenEpoch != 0 || p.epoch == 1;  // we just started: don't replay an old choice
        g_seenEpoch = p.epoch;
        if (!fresh) return;
        Log("game over: the host chose option %d%s", p.option & ~kFromPause,
            p.option & kFromPause ? " in its pause" : "");
        g_pending = true;
        g_pendingOption = p.option;
        g_pendingSince = g_frame;
        g_step = 0;
    }
}

bool GameOverLoadScreenOpen() {
    bool open = false;
    __try {
        open = LocalAtMenu() && Read<std::int32_t>(kMenuAbove) == kMenuLoad;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return open || (g_loadSeenMs && GetTickCount64() - g_loadSeenMs < 1000);
}

void GameOverSyncFrame() {
    __try {
        if (LocalAtMenu() && Read<std::int32_t>(kMenuAbove) == kMenuLoad) g_loadSeenMs = GetTickCount64();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (!g_on) return;
    ++g_frame;
    bool atMenu = false;
    __try {
        atMenu = LocalAtMenu();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_atMenu = atMenu;
    if (g_host) {
        bool loadOpen = atMenu && Read<std::int32_t>(kMenuAbove) == kMenuLoad;
        if (loadOpen && !g_loadOpen) {
            Log("game over: our player chose Load");
            HostChose(kOptionLoad);
        }
        g_loadOpen = loadOpen;
        if (g_frame % 10 == 0) Send(atMenu && !loadOpen);
        return;
    }
    g_menuSince = atMenu ? (g_menuSince ? g_menuSince : g_frame) : 0;
    bool peerAtMenu = g_peerAtMenu && AvatarLinkNowMs() - g_peerMs < 1000;
    bool hostHere = g_peerMs && AvatarLinkNowMs() - g_peerMs < kHostGoneMs;
    if (g_pending && g_frame - g_pendingSince > 20 * 60) {
        Log("game over: the host's choice wasn't used (we weren't at the Game Over menu)");
        g_pending = false;
    }
    if (g_pending && (g_pendingOption & kFromPause)) {
        if (atMenu) {
            g_pendingOption &= ~kFromPause;  // our Game Over menu is up: its row, as for any choice
        } else if (!FollowNotSafeNow()) {  // else next frame (our own pause may still be closing)
            std::int32_t kind = g_pendingOption & ~kFromPause;
            g_pending = false;
            bool jump = g_realMissionChoice && SceneSyncHasJump(kind == kOptionRetry ? kJumpRetry : kJumpQuit);
            Log("game over: the host chose %s in its pause: %s", OptionName(kind),
                jump ? "ours does the same" : "not run here (our room has no such jump)");
            if (jump) g_realMissionChoice(kind);
        }
    }
    if (!atMenu) {
        Unblock("the Game Over menu closed");
        g_step = 0;
        return;
    }
    // Our menu is up: the host chooses, also while it's still playing (e.g. as Mickey).
    if (!g_blocking && g_step != 2 && (hostHere || g_pending)) {
        g_blocking = true;
        InputBlock(0xFFFF);
        Log(peerAtMenu || g_pending ? "game over: both players are at Game Over; the host chooses"
                                    : "game over: the host is still playing (Mickey?); we wait for its choice");
    }
    if (g_blocking && !hostHere && !g_pending && g_step == 0) {
        Unblock("the host is gone: our player chooses");
        return;
    }
    if (!g_pending || g_frame - g_menuSince < kMenuReadyFrames || g_frame - g_stepFrame < 20) return;
    int downs = DownsTo(g_pendingOption);
    if (downs < 0) {
        Log("game over: our menu has no row for the host's choice (%s, kind %d)", OptionName(g_pendingOption),
            g_pendingOption);
        g_pending = false;
        Unblock("our player chooses");
        g_step = 2;  // not blocked again at this menu
        return;
    }
    g_stepFrame = g_frame;
    if (downs > 0) {  // one press at a time; the cursor is read again before the next
        InputInject(kDown, 3);
        g_step = 1;
        return;
    }
    InputInject(kCross, 3);
    g_pending = false;
    g_step = 2;
    Log("game over: chose %s like the host", OptionName(g_pendingOption));
    if (g_pendingOption == kOptionLoad) Unblock("the load screen needs our player");
}

}  // namespace kh2coop
