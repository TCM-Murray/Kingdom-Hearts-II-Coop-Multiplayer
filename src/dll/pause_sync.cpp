// Shared combat pause (PAUSE_SYNC=1 on both PCs; user request 2026-10-05).
// KH2 has two pauses: out of combat Start opens the full menu (equipment,
// items...), which stays local; in combat it only shows "PAUSE". That combat
// pause is shared: when one player opens or closes it, the mod presses Start
// in the other player's game so it does the same.
//
// Verified live on the bench (single copy, Borough fight):
//   exe+0x7435D0 menu byte: 0xFF none, 0x0A full menu, 0x08 combat pause
//   exe+0x2A11404 battle flag: 1 while Heartless are around, 0 otherwise
//
// Each side sends {paused, epoch} ("KH2P"); epoch counts only changes the
// player made (not ones we caused by injecting Start). When the peer's epoch
// moves on and its state differs from ours, we press Start once: to pause only
// if we're in a fight with no menu open, to unpause only from the combat pause;
// the press is retried if it doesn't take.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "pause_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kOpenMenu = 0x7435D0;
constexpr std::uint8_t kMenuNone = 0xFF, kMenuCombatPause = 0x08;
constexpr std::uintptr_t kBattle = 0x2A11404;
constexpr std::uintptr_t kInField = 0x9BA8D0;
constexpr std::uintptr_t kNow = 0x717008;      // u8 world (0xFF = title)
constexpr std::uintptr_t kSoraHp = 0x2A23598;  // Sora's stat slot HP
constexpr std::uint16_t kStart = 0x0008;  // raw slot button bit (PS2 layout)

constexpr std::uint32_t kMagic = 0x5032484B;  // "KH2P"
constexpr std::uint16_t kVersion = 1;
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t epoch;
    std::uint8_t paused, battle;
};
#pragma pack(pop)

bool g_on = false;
std::uint64_t g_frame = 0;
bool g_localPaused = false;
std::uint32_t g_epoch = 0;             // our player's own pause changes
std::uint32_t g_remoteEpoch = 0;       // newest peer epoch seen
bool g_remotePaused = false, g_remoteBattle = false;
bool g_want = false, g_wantActive = false;  // a pending "match the peer" request
int g_tries = 0;
std::uint64_t g_lastPress = 0, g_expectUntil = 0;  // our press and the window in which its effect is "ours"
std::uint32_t g_synced = 0, g_skipped = 0;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

void Send() {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), g_epoch, static_cast<std::uint8_t>(g_localPaused),
              static_cast<std::uint8_t>(Read<std::uint32_t>(kBattle) != 0)};
    AvatarLinkSendRaw(&p, sizeof(p));
}

void Press() {
    InputInject(kStart, 3);  // held 3 input reads, then released: one press
    g_lastPress = g_frame;
    g_expectUntil = g_frame + 45;
    ++g_tries;
}

}  // namespace

void PauseSyncInit() {
    if (EnvInt("KH2COOP_PAUSE_SYNC", 0) != 1) return;
    g_on = InputInjectEnable();
    Log("pause sync: %s", g_on ? "on (the combat pause is shared; the full menu stays local)"
                               : "OFF: the input hook could not be installed");
}

void PauseSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    g_remoteBattle = p.battle != 0;
    if (p.epoch <= g_remoteEpoch) return;
    g_remoteEpoch = p.epoch;
    g_remotePaused = p.paused != 0;
    Log("pause sync: the other player %s the combat pause", g_remotePaused ? "opened" : "closed");
    g_want = g_remotePaused;
    g_wantActive = true;
    g_tries = 0;
}

void PauseSyncOnPeerRestart() {
    g_remoteEpoch = 0;  // its epochs start again from 0
    g_wantActive = false;
}

void PauseSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    std::uint8_t menu = Read<std::uint8_t>(kOpenMenu);
    // The Game Over screen sets the same menu value as the combat pause (bench 17:38:59 and
    // test 17:50:08, 2026-10-06: a Game Over paused the other game). Sora at 0 HP = Game Over.
    bool gameOver = Read<std::uint8_t>(kNow) != 0xFF && Read<std::uint32_t>(kSoraHp) == 0;
    bool paused = menu == kMenuCombatPause && !gameOver;
    if (paused != g_localPaused) {
        g_localPaused = paused;
        bool ours = g_frame <= g_expectUntil;  // caused by our injected press
        if (ours) {
            ++g_synced;
            Log("pause sync: %s our combat pause to match the other player", paused ? "opened" : "closed");
            // A newer request that came in while our press was taking effect stays (two-PC test 2026-10-09:
            // pause, unpause, pause within a second left one game unpaused: the second "opened" was dropped here).
            if (g_want == paused) g_wantActive = false;
        } else {
            ++g_epoch;
            g_wantActive = false;  // the player's own choice wins over a pending request
            Log("pause sync: our player %s the combat pause; telling the other game", paused ? "opened" : "closed");
        }
        for (int i = 0; i < 3; ++i) Send();  // state changes go out three times
    } else if (g_frame % 10 == 0) {
        Send();  // regular refresh, so a lost packet is repaired
    }

    if (!g_wantActive || g_want == g_localPaused) {
        g_wantActive = g_wantActive && g_want != g_localPaused;
        return;
    }
    if (g_frame - g_lastPress < 45) return;  // give the last press time to work
    if (g_tries >= 3) {
        Log("pause sync: gave up matching the other player's pause after 3 presses (menu 0x%02X)", menu);
        g_wantActive = false;
        return;
    }
    bool canPause = g_want && menu == kMenuNone && Read<std::uint32_t>(kBattle) != 0 && Read<std::uint8_t>(kInField);
    bool canUnpause = !g_want && menu == kMenuCombatPause && !gameOver;
    if (canPause || canUnpause) {
        Press();
    } else if (g_tries == 0) {
        ++g_skipped;
        Log("pause sync: not matching the other player's %s (we're %s)", g_want ? "pause" : "unpause",
            menu == kMenuNone ? "not in a fight" : "in another menu");
        g_wantActive = false;
    }
}

}  // namespace kh2coop
