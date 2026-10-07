// Shared Game Over (GAMEOVER_SYNC=1 on both PCs, default on; friend's idea, user
// design 2026-10-06): when both players are at the Game Over screen, only the
// host chooses. The friend's buttons are blocked there, and when the host
// picks an option the mod picks the same one in the friend's game.
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
// The Mickey rescue screen is left alone (each player chooses).

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "game_over.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kGameOver = 0x2AE8050;  // Game Over object while the screen is up
constexpr std::uintptr_t kGoState = 0x10, kGoMickey = 0x14;
constexpr int kStateMenu = 6;
constexpr std::uintptr_t kMenuAbove = 0x7435D4;  // i32: the menu opened over the Game Over one (-1 none)
constexpr std::int32_t kMenuLoad = 0x0C;
constexpr std::int32_t kOptionContinue = 2, kOptionLoad = 1;  // 2 = the callback's; 1 = ours
constexpr std::uintptr_t kChoice = 0x3FCBE0;     // (option): the Game Over menu's callback
constexpr std::uint8_t kChoiceBytes[] = {0x48, 0x83, 0xEC, 0x58, 0x66, 0xC7, 0x44, 0x24, 0x30, 0xFF, 0xFF};
constexpr std::uint16_t kCross = 0x4000, kDown = 0x0040;
constexpr int kMenuReadyFrames = 60;  // the menu fades in; presses before that may be lost

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
PFN_Choice g_realChoice = nullptr;
bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
// Host
std::uint32_t g_epoch = 0;
std::int32_t g_option = 0;
bool g_loadOpen = false;             // our load screen is open over the Game Over menu
// Friend
bool g_peerAtMenu = false;
std::uint32_t g_peerMs = 0;          // when the last packet arrived (our clock)
std::uint32_t g_seenEpoch = 0;
bool g_pending = false;
std::int32_t g_pendingOption = 0;
std::uint64_t g_pendingSince = 0, g_menuSince = 0;
bool g_blocking = false;
int g_step = 0;                      // 0 idle, 1 down sent (Load), 2 done
std::uint64_t g_stepFrame = 0;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

bool LocalAtMenu() {
    std::uintptr_t go = Read<std::uintptr_t>(kGameOver);
    if (!go) return false;
    return *reinterpret_cast<const volatile std::int32_t*>(go + kGoState) == kStateMenu &&
           *reinterpret_cast<const volatile std::uint8_t*>(go + kGoMickey) == 0;
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
    Log("game over sync: %s", !g_on ? "OFF: the input hook could not be installed"
                              : host ? "on (when both players are at Game Over, our choice is the friend's too)"
                                     : "on (when both players are at Game Over, the host chooses for us)");
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
        Log("game over: the host chose option %d", p.option);
        g_pending = true;
        g_pendingOption = p.option;
        g_pendingSince = g_frame;
        g_step = 0;
    }
}

void GameOverSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    bool atMenu = false;
    __try {
        atMenu = LocalAtMenu();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
    if (g_pending && g_frame - g_pendingSince > 20 * 60) {
        Log("game over: the host's choice wasn't used (we weren't at the Game Over menu)");
        g_pending = false;
    }
    if (!atMenu) {
        Unblock("the Game Over menu closed");
        g_step = 0;
        return;
    }
    // Our menu is up: the host chooses if it's at Game Over too (or just chose).
    if (!g_blocking && g_step != 2 && (peerAtMenu || g_pending)) {
        g_blocking = true;
        InputBlock(0xFFFF);
        Log("game over: both players are at Game Over; the host chooses");
    }
    if (g_blocking && !peerAtMenu && !g_pending && g_step == 0) {
        Unblock("the host left its Game Over screen without a choice");
        return;
    }
    if (!g_pending || g_frame - g_menuSince < kMenuReadyFrames || g_frame - g_stepFrame < 20) return;
    bool load = g_pendingOption != kOptionContinue;
    if (load && g_step == 0) {
        InputInject(kDown, 3);
        g_step = 1;
        g_stepFrame = g_frame;
        return;
    }
    InputInject(kCross, 3);
    g_stepFrame = g_frame;
    g_pending = false;
    g_step = 2;
    Log("game over: chose %s like the host", load ? "Load" : "Continue");
    if (load) Unblock("the load screen needs our player");
}

}  // namespace kh2coop
