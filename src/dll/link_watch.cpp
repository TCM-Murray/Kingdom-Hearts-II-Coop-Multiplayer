// Connection watcher (TODO 6.1, your design session 27; the on-screen part is TODO 6.8).
//
// The link is already a heartbeat: each game sends its avatar packet every frame (~60/s, through menus,
// cutscenes and loads) and AvatarLinkPeerAgeMs() says how long ago the last one arrived. Here that age
// becomes a state: once the other game has been connected, kLostMs of silence = lost (its game closed or
// crashed, or the network is gone); packets arriving again = back.
//
// While lost, room following leaves the friend's game alone (follow.cpp: only a host that is sending is
// followed; before, the friend was pulled back into the host's last room, TODO 6.1a). The player then
// chooses (your picks, session 27), for now with tools/debug_cmd.py link <1|2|3>, later on a pause-like
// screen (6.8):
//   1 wait: nothing changes; co-op goes on when the other game is back.
//   2 leave: back to the title screen, the game's own soft reset (exe+0x154DB0 sets exe+0xABAC5A; the
//     game's main task exe+0x154B50 resets once it may). Not while a room change is starting or loading
//     (TODO 6.11): from the pick on no new one may start (follow.cpp), and the reset waits for the last one.
//   3 play on with Donald and Goofy: the Sora copy leaves the party (puppet.cpp, PuppetSetSolo): the
//     member it replaced comes back. The room reloads at once so they are there now, also in a fight or a
//     boss fight, which starts over (your pick, two-PC test session 27); only in a cutscene the copy is
//     taken away and they come back at the next room change. Saves never carry the copy anyway (coop_save.cpp).
// The choices are on the 'Connection lost' screen (link_menu.cpp, 6.8: Continue Alone = 3, Title Screen = 2,
// waiting = the screen staying open); the debug command still works.
// When the other game is back (it restarted and connected again), co-op resumes by itself (your pick:
// rejoin automatically). After 3, our room reloads as soon as the other player is back in a room, so the
// copy is there at once (two-PC test session 27: Donald stayed until the next room change). The friend only
// does it when the host is in its room; otherwise following the host loads the host's room anyway.
// The longest silence seen while connected is logged, to check kLostMs against real loads.

#include "link_watch.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "follow.hpp"
#include "link_menu.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kLostMs = 3000;
constexpr std::uintptr_t kSoftReset = 0x154DB0;  // (): sets exe+0xABAC5A = 1, the game's soft reset request
constexpr std::uint8_t kSoftResetBytes[] = {0xC6, 0x05, 0xA3, 0x5E, 0x96, 0x00, 0x01, 0xC3};
// u8: 0 out of the field. The world byte stays on the last room at the title after a soft reset (bench session 27).
constexpr std::uintptr_t kInField = 0x9BA8D0;
constexpr std::uint64_t kLeaveFrames = 600;  // 10 s for the soft reset to leave the field
constexpr std::uintptr_t kNow = 0x717008;  // u8 world, u8 room
constexpr std::uint64_t kRejoinFrames = 3600;  // 60 s for the other player to get back into a room

enum class State { kWaiting, kConnected, kLost, kSolo };
State g_state = State::kWaiting;
bool g_host = false;
std::uint32_t g_longest = 0;  // longest silence while connected, ms
std::uint32_t g_gap = 0;      // the current silence (age last frame)
std::uint32_t g_losses = 0;
std::uint64_t g_frame = 0;
std::uint64_t g_rejoinSince = 0;  // frame the other game came back after 3 (0 = no reload pending)
std::uint64_t g_leaving = 0;  // frame the soft reset was asked for (0 = not leaving): no screen until it's left the field
std::uint64_t g_leaveAsked = 0;  // frame 'Title Screen' was picked (0 = no); the soft reset waits for room changes
bool g_leaveWaited = false;      // it had to wait (logged)

const char* Other() { return g_host ? "the friend's" : "the host's"; }

void PlayOnWithCompanions() {
    PuppetSetSolo(true);
    const char* why = nullptr;
    if (FollowReloadRoom(&why)) {
        Log("link watch: playing on with Donald and Goofy: this room reloads so they are here now");
        return;
    }
    bool removed = PuppetRemoveCopy();
    Log("link watch: playing on with Donald and Goofy: no reload now (%s); %s; they come back at the next room "
        "change", why ? why : "?", removed ? "the Sora copy taken away" : "no Sora copy in this room");
}

void LeaveToTitle() {
    auto fn = reinterpret_cast<void(__fastcall*)()>(ExeBase() + kSoftReset);
    if (std::memcmp(reinterpret_cast<const void*>(fn), kSoftResetBytes, sizeof(kSoftResetBytes)) != 0) {
        Log("link watch: leaving to the title screen FAILED: the soft reset code differs (other game build?)");
        FollowHoldRoomChanges(false);
        return;
    }
    Log("link watch: leaving to the title screen (the game's soft reset)");
    g_leaving = g_frame;
    fn();
}

}  // namespace

void LinkWatchInit(bool host) {
    g_host = host;
    LinkMenuInit(host);
    Log("link watch: on: %s game counts as gone after %u s without a packet", Other(), kLostMs / 1000);
}

bool LinkLost() { return g_state == State::kLost || g_state == State::kSolo; }

namespace {

void UpdateState() {
    std::uint32_t age = AvatarLinkPeerAgeMs();
    if (age == 0xFFFFFFFFu) return;  // nothing ever arrived
    if (age < kLostMs) {
        if (g_state == State::kConnected) {
            if (age < g_gap && g_gap >= 500 && g_gap > g_longest) {  // a silence just ended
                g_longest = g_gap;
                Log("link watch: longest silence so far %u ms", g_longest);
            }
            g_gap = age;
            return;
        }
        if (g_state == State::kSolo) {
            PuppetSetSolo(false);
            g_rejoinSince = g_frame;
            Log("link watch: %s game is back: co-op again; our room reloads for the Sora copy once that player is "
                "back in a room", Other());
        } else if (g_state == State::kLost) {
            Log("link watch: %s game is back: co-op goes on", Other());
        } else {
            Log("link watch: %s game connected", Other());
        }
        g_state = State::kConnected;
        g_gap = age;
        return;
    }
    if (g_state != State::kConnected) return;
    g_state = State::kLost;
    ++g_losses;
    Log("link watch: connection lost: nothing from %s game for %u ms; waiting for it (choices on the "
        "'Connection lost' screen, or tools/debug_cmd.py <port> link 2 title screen / 3 play on with Donald and Goofy)",
        Other(), age);
}

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

// After 'Continue Alone' and the other game's return: reload our room so the copy is in it now.
void RejoinReload() {
    if (!g_rejoinSince) return;
    if (g_state != State::kConnected) {
        g_rejoinSince = 0;  // gone again
        return;
    }
    if (Read<std::uint8_t>(kInField) == 0) {
        Log("link watch: a room load came first (following the host?): the Sora copy comes with it");
        g_rejoinSince = 0;
        return;
    }
    if (g_frame - g_rejoinSince > kRejoinFrames) {
        Log("link watch: no room reload for the Sora copy: %s player wasn't back in a room within 60 s; it comes at "
            "the next room change", g_host ? "the friend's" : "the host's");
        g_rejoinSince = 0;
        return;
    }
    PeerPose peer {};
    if (!AvatarLinkPeerNow(peer) || !peer.hasActor || peer.world == 0xFF) return;
    if (!g_host && (peer.world != Read<std::uint8_t>(kNow) || peer.room != Read<std::uint8_t>(kNow + 1)))
        return;  // following the host takes us to its room, which loads the copy
    const char* why = nullptr;
    if (!FollowReloadRoom(&why)) return;  // a menu or cutscene: try again next frame
    Log("link watch: %s player is back in a room: ours reloads so the Sora copy is here now",
        g_host ? "the friend's" : "the host's");
    g_rejoinSince = 0;
}

// 'Title Screen' picked: the soft reset goes in once no room change is starting or loading. Both together
// crash the game in the room teardown (exe+0x39DA55; two-PC run session 28, friend 23:49:11: its cutscene asked
// for the next room 0.22 s after the pick, our reset went in 0.15 s later). The game's own reset check
// (exe+0x39DCC0) doesn't see a room change that was asked for but hasn't started.
void LeaveWhenSafe() {
    if (!g_leaveAsked) return;
    bool loading = Read<std::uint8_t>(kInField) == 0;
    bool starting = FollowRoomChangeStarting();
    if (!loading && !starting) {
        if (g_leaveWaited)
            Log("link watch: the room change is done (%llu frames)",
                static_cast<unsigned long long>(g_frame - g_leaveAsked));
        g_leaveAsked = 0;
        g_leaveWaited = false;
        LeaveToTitle();
        return;
    }
    if (!g_leaveWaited)
        Log("link watch: title screen: waiting for the room change that is %s first",
            loading ? "loading" : "starting");
    g_leaveWaited = true;
    if (g_frame - g_leaveAsked > kLeaveFrames) {  // never seen; a load doesn't take 10 s
        Log("link watch: title screen NOT done: still %s after 10 s; pick it again", loading ? "loading" : "starting");
        g_leaveAsked = 0;
        g_leaveWaited = false;
        FollowHoldRoomChanges(false);
    }
}

}  // namespace

void LinkWatchFrame() {
    ++g_frame;
    UpdateState();
    if (g_leaving && (*reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0 ||
                      g_frame - g_leaving > kLeaveFrames)) {
        if (g_frame - g_leaving > kLeaveFrames) Log("link watch: still in the field 10 s after the soft reset");
        g_leaving = 0;  // left the field (or the reset didn't happen): the screen may show again in the next game
        FollowHoldRoomChanges(false);
    }
    if (int choice = LinkMenuTakeChoice()) LinkWatchChoose(choice);  // a few frames after the menu shut
    LeaveWhenSafe();
    RejoinReload();
    LinkMenuFrame(g_state == State::kLost && !g_leaving && !g_leaveAsked);
}

void LinkWatchChoose(int choice) {
    if (choice == 4) {  // debug command only (tools/debug_cmd.py link 4): the title screen now, also while connected
        if (g_leaving || g_leaveAsked) return;
        Log("link watch: debug: leaving to the title screen while %s", LinkLost() ? "the other game is gone" : "connected");
        g_leaveAsked = g_frame;
        FollowHoldRoomChanges(true);
        return;
    }
    if (choice == 2 && LinkLost()) {  // also after playing on with Donald and Goofy
        if (g_leaving || g_leaveAsked) return;  // already on its way
        g_leaveAsked = g_frame;
        FollowHoldRoomChanges(true);
        return;
    }
    if (g_state != State::kLost) {
        Log("link watch: choice %d ignored: only while %s game is gone%s", choice, Other(),
            g_state == State::kSolo ? " (already playing on with Donald and Goofy)" : "");
        return;
    }
    if (choice == 1) {
        Log("link watch: waiting for %s game", Other());
    } else if (choice == 3) {
        g_state = State::kSolo;
        PlayOnWithCompanions();
    } else {
        Log("link watch: unknown choice %d (1 wait, 2 leave, 3 play on with Donald and Goofy)", choice);
    }
}

}  // namespace kh2coop
