// Shared combat pause (PAUSE_SYNC=1 on both PCs; user request 2026-10-05), also in story cutscenes
// (TODO 5.3, your rule session 20: both can pause a cutscene and the pause is shared).
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
// the press is retried if it doesn't take. The Gummi flight's pause (menu 0x0B) is shared the same way; it closes
// through its list (gummi_flight.cpp), so only the one who paused uses it and a Retry reaches both.
//
// All pausing shared (TODO 1.286, your picks 2026-11-25): each side also says whether its player has the Start menu
// open (menu 0x0A, opened by the player's own Start press: save points and shops don't count). While the other
// player is in their Start menu, or in a pause our Start press can't follow (we're out of a fight, a cutscene or a
// flight), our game shows the wait screen (link_menu.cpp, screen 3): PAUSE with no items and who paused; only the
// one who paused ends it. Only a pause or menu our player opened is reported, never the wait screen or a followed
// pause, so the two games can't wait on each other.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "game.hpp"
#include "gummi_flight.hpp"
#include "link_menu.hpp"
#include "pause_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kOpenMenu = 0x7435D0;
constexpr std::uint8_t kMenuNone = 0xFF, kMenuCombatPause = 0x08, kMenuFlightPause = 0x0B, kMenuStart = 0x0A;
constexpr std::uintptr_t kBattle = 0x2A11404;
constexpr std::uintptr_t kSceneState = 0xB65210;  // u32: 3 = a story cutscene is playing (its pause is menu 0x08 too)
constexpr std::uint16_t kStart = 0x0008;  // raw slot button bit (PS2 layout)

constexpr std::uint32_t kMagic = 0x5032484B;  // "KH2P"
constexpr std::uint16_t kVersion = 2;  // 2: the Start menu (TODO 1.286)
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t epoch;
    std::uint8_t paused, battle, retry;  // retry: the flight's pause closed with its Retry
    std::uint8_t startMenu;              // our player has the Start menu open
    std::uint32_t menuSeq;               // counts our Start menu's changes: an older packet's state is ignored
};
#pragma pack(pop)

bool g_on = false;
std::uint64_t g_frame = 0;
bool g_localPaused = false;
std::uint32_t g_epoch = 0;             // our player's own pause changes
std::uint32_t g_remoteEpoch = 0;       // newest peer epoch seen
bool g_remotePaused = false;
bool g_want = false, g_wantActive = false;  // a pending "match the peer" request
int g_tries = 0;
// Our Start press: the frame the next one may come, the window in which its effect is "ours", and the state it
// asks for. Only that change counts as ours, and only once (TODO 1.18).
std::uint64_t g_nextPress = 0, g_expectUntil = 0;
bool g_pressFor = false;
// shortcut: frame counts for a press to work (45) and for the pad to read as let go after one that did (6).
// Real state: the pad's pressed bits and the menu's own open / close steps. TODO 8.11.
constexpr std::uint64_t kPressFrames = 45, kReleaseFrames = 6;
bool g_following = false;              // this pause was opened by the other player
bool g_closeRetry = false;             // our player picked Retry in the flight's pause
bool g_peerRetry = false;              // the other player's last close was a Retry
int g_close = 0;                       // for the flight's pause list: 1 close it, 2 pick Retry
bool g_localMenu = false;              // our player opened the Start menu and a menu is still open
bool g_remoteMenu = false;             // the other player's
std::uint32_t g_menuSeq = 0, g_remoteMenuSeq = 0;
std::uint64_t g_startAt = 0;           // our frame of our player's last Start press (raw, not ours injected)
std::uint64_t g_lastPacketMs = 0;      // GetTickCount64 of the other side's last packet
// shortcut: both are guesses: 30 frames from the player's Start press to the Start menu (the bench's pad: within it), and
// 2 s with no packet before the other side's menu state counts as gone (a 6-7 s link silence, TODO 6.16, closes the
// wait screen and opens it again). Upgrade: the Start watcher's own open (exe+0x1572B0) and link_watch's state.
constexpr std::uint64_t kStartMenuFrames = 30;  // a Start menu opened this soon after the player's Start press is theirs
constexpr std::uint64_t kStaleMs = 2000;        // no packet this long: the other side's menu state says nothing

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

void Send() {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), g_epoch, static_cast<std::uint8_t>(g_localPaused),
              static_cast<std::uint8_t>(Read<std::uint32_t>(kBattle) != 0), static_cast<std::uint8_t>(g_closeRetry),
              static_cast<std::uint8_t>(g_localMenu), g_menuSeq};
    AvatarLinkSendRaw(&p, sizeof(p));
}

// A minigame's how-to screen (menu 0x0E; the Light Cycle's opens before its ride, F850), shared like the pause (TODO
// 1.274): each player closed their own with Cross and the two rides started as far apart as the two presses. Once
// both games have had one open at the same time, the other player's close presses Cross in ours until ours is
// closed too.
constexpr std::uint16_t kCross = 0x4000;
constexpr std::uint64_t kTutorialPressEvery = 20;  // frames between presses: a window of several pages takes one each
constexpr int kTutorialPressMax = 20;
bool g_tutorialBoth = false;
bool g_howToUp = false;  // ours is open and our player hasn't pressed Cross on it (PauseSyncHowToUp)
bool g_wasHowTo = false;
std::uint64_t g_tutorialPressAt = 0;
int g_tutorialPresses = 0;

void TutorialFrame() {
    std::uint8_t menu = 0xFF;
    const bool open = ReadAny(ExeBase() + game::kOpenMenu, &menu) && menu == game::kMenuHowTo;
    // Down from our player's own press on (the screen takes a third of a second to close after it), up again only
    // with the next screen.
    if (!open) g_howToUp = false;
    else if (!g_wasHowTo) g_howToUp = true;
    else if (InputRawButtons() & kCross) g_howToUp = false;
    g_wasHowTo = open;
    if (!open) {  // ours is closed: nothing to follow
        if (g_tutorialPresses)
            Log("pause sync: our how-to screen closed after %d press(es) of ours", g_tutorialPresses);
        g_tutorialBoth = false;
        g_tutorialPresses = 0;
        return;
    }
    if (AvatarLinkPeerTutorial()) {
        g_tutorialBoth = true;
        return;
    }
    // Theirs is closed, as a fresh pose says (a stale one says nothing: their game may be loading or gone).
    if (!g_tutorialBoth || AvatarLinkPeerAgeMs() > 500 || g_frame < g_tutorialPressAt ||
        g_tutorialPresses >= kTutorialPressMax)
        return;
    if (!g_tutorialPresses) Log("pause sync: the other player closed their how-to screen: Cross pressed in our game");
    ++g_tutorialPresses;
    g_tutorialPressAt = g_frame + kTutorialPressEvery;
    InputInject(kCross, 3);
}

void Press() {
    InputInject(kStart, 3);  // held 3 input reads, then released: one press
    g_nextPress = g_expectUntil = g_frame + kPressFrames;
    g_pressFor = g_want;
    ++g_tries;
}

}  // namespace

void PauseSyncInit() {
    if (EnvInt("KH2COOP_PAUSE_SYNC", 1) != 1) return;
    g_on = InputInjectEnable();
    Log("pause sync: %s", g_on ? "on (the combat and cutscene pause and the Start menu are shared)"
                               : "OFF: the input hook could not be installed");
}

void PauseSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    g_lastPacketMs = GetTickCount64();
    if (p.menuSeq >= g_remoteMenuSeq && (p.startMenu != 0) != g_remoteMenu) {
        g_remoteMenu = p.startMenu != 0;
        Log("pause sync: the other player %s the Start menu", g_remoteMenu ? "opened" : "closed");
    }
    if (p.menuSeq > g_remoteMenuSeq) g_remoteMenuSeq = p.menuSeq;
    if (p.epoch <= g_remoteEpoch) return;
    g_remoteEpoch = p.epoch;
    g_remotePaused = p.paused != 0;
    g_peerRetry = p.retry != 0;
    Log("pause sync: the other player %s the combat pause", g_remotePaused ? "opened" : "closed");
    g_want = g_remotePaused;
    g_wantActive = true;
    g_tries = 0;
}

void PauseSyncOnPeerRestart() {
    g_remoteEpoch = 0;  // its epochs start again from 0
    g_wantActive = false;
    g_remotePaused = g_remoteMenu = false;
    g_remoteMenuSeq = 0;
}

const char* PauseSyncWaitLine() {
    if (!g_on || GetTickCount64() - g_lastPacketMs > kStaleMs) return nullptr;
    // Their pause, which our Start press didn't (or couldn't) follow: no request pending and no pause of ours open.
    const bool unfollowed = g_remotePaused && !g_wantActive && !g_localPaused;
    if (!g_remoteMenu && !unfollowed) return nullptr;
    return AvatarLinkIsHost() ? "The Client paused." : "The Host paused.";
}

std::uint8_t PauseSyncOpenMenu() { return Read<std::uint8_t>(kOpenMenu); }

bool PauseSyncFollowing() { return g_following; }

bool PauseSyncHowToUp() { return g_on && g_howToUp; }

bool PauseSyncMatching() { return g_on && g_pressFor && g_frame <= g_expectUntil; }

int PauseSyncTakeClose() {
    const int close = g_close;
    g_close = 0;
    return close;
}

void PauseSyncRetryOnClose() { g_closeRetry = true; }

void PauseSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    TutorialFrame();
    std::uint8_t menu = Read<std::uint8_t>(kOpenMenu);
    if (InputRawButtons() & kStart) g_startAt = g_frame;
    // Ours from the player's Start press on (the full menu, 0x0A) until no menu is open (its screens may change the
    // byte); a menu that opens any other way (a save point, a shop) isn't the Start menu.
    const bool startMenu = g_localMenu ? menu != kMenuNone
                                       : menu == kMenuStart && g_startAt && g_frame - g_startAt <= kStartMenuFrames;
    if (startMenu != g_localMenu) {
        g_localMenu = startMenu;
        ++g_menuSeq;
        Log("pause sync: our player %s the Start menu; telling the other game", startMenu ? "opened" : "closed");
        for (int i = 0; i < 3; ++i) Send();
    }
    // The Game Over screen sets the same menu value as the combat pause (bench 17:38:59 and
    // test 17:50:08, 2026-10-06: a Game Over paused the other game). Sora at 0 HP = Game Over.
    bool gameOver = Read<std::uint8_t>(game::kNow) != 0xFF && Read<std::uint32_t>(game::kSoraSlot) == 0;
    // The 'Connection lost' screen is a pause menu too, but ours, not the player's (link_menu.cpp).
    // The World Map's own menu (Continue, Jiminy's Journal, Save) reads 0x08 too and stays local: the other player's
    // close pressed Start on it (TODO 1.226).
    bool mapMenu = menu == kMenuCombatPause && Read<std::uint8_t>(game::kNow) == game::kWorldMap;
    bool paused = (menu == kMenuCombatPause && !gameOver && !LinkMenuUp() && !mapMenu) || menu == kMenuFlightPause;
    if (paused != g_localPaused) {
        g_localPaused = paused;
        // Caused by our injected press: inside its window and the change it asked for. A press counts once: the
        // player's own pause just after a matched close is theirs, and is told (before, the other game never
        // heard of it and one player stayed paused; TODO 1.18).
        bool ours = g_frame <= g_expectUntil && paused == g_pressFor;
        if (ours) {
            g_expectUntil = 0;
            g_nextPress = g_frame + kReleaseFrames;  // it worked: no need to wait out the rest
        }
        g_following = paused && ours;
        g_close = 0;
        if (paused) g_closeRetry = false;
        if (ours) {
            Log("pause sync: %s our combat pause to match the other player", paused ? "opened" : "closed");
            // A newer request that came in while our press was taking effect stays (two-PC test 2026-10-09:
            // pause, unpause, pause within a second left one game unpaused: the second "opened" was dropped here).
            if (g_want == paused) g_wantActive = false;
        } else {
            ++g_epoch;
            g_wantActive = false;  // the player's own choice wins over a pending request
            // Our close ends the shared pause: the other game follows it as its own press's change, which never raises
            // its epoch, so its 'paused' would stay with us and keep a wait screen up (TODO 1.286, review).
            if (!paused) g_remotePaused = false;
            Log("pause sync: our player %s the combat pause; telling the other game", paused ? "opened" : "closed");
        }
        for (int i = 0; i < 3; ++i) Send();  // state changes go out three times
    } else if (g_frame % 10 == 0) {
        Send();  // regular refresh, so a lost packet is repaired
    }

    if (!g_wantActive) return;
    if (g_want == g_localPaused) {
        // Met, unless a press of ours for the other state is still on its way: the other player resumed and paused
        // again before our close took, we then closed, and nothing opened ours again (TODO 1.18).
        if (g_frame > g_expectUntil || g_pressFor == g_want) g_wantActive = false;
        return;
    }
    if (g_frame < g_nextPress) return;  // give the last press time to work
    if (g_tries >= 3) {
        Log("pause sync: gave up matching the other player's pause after 3 presses (menu 0x%02X)", menu);
        g_wantActive = false;
        return;
    }
    bool scene = Read<std::int32_t>(kSceneState) == 3;
    bool canPause = g_want && menu == kMenuNone &&
                    ((Read<std::uint32_t>(kBattle) != 0 && Read<std::uint8_t>(game::kInField)) || scene || GummiFlightFlying());
    // Our wait screen (a pause menu too) is link_menu's to close, not a pause of ours to press shut.
    bool canUnpause = !g_want && !LinkMenuUp() &&
                      (menu == kMenuFlightPause || (menu == kMenuCombatPause && !gameOver && !mapMenu));
    if (canUnpause && menu == kMenuFlightPause) {
        g_close = g_peerRetry ? 2 : 1;  // through its list: Start is the other player's to press there
        g_nextPress = g_expectUntil = g_frame + kPressFrames;
        g_pressFor = false;
        ++g_tries;
    } else if (canPause || canUnpause) {
        Press();
    } else {  // no press possible now, also after one that didn't take: the wait screen takes over (TODO 1.286, review)
        if (g_tries == 0)
            Log("pause sync: not matching the other player's %s (we're %s)%s", g_want ? "pause" : "unpause",
                menu == kMenuNone ? "not in a fight or a cutscene" : "in another menu",
                g_want ? ": the wait screen instead" : "");
        else
            Log("pause sync: matching the other player's %s stopped after %d press(es): no press possible now",
                g_want ? "pause" : "unpause", g_tries);
        g_wantActive = false;
    }
}

}  // namespace kh2coop
