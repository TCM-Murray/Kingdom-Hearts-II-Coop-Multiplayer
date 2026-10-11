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
//     game's main task exe+0x154B50 resets once it may).
//   3 play on with Donald and Goofy: the Sora copy leaves the party (puppet.cpp, PuppetSetSolo): the
//     member it replaced comes back. In a quiet field room the room reloads at once so they are there
//     now; in a fight, a cutscene or a menu the copy is taken away now and they come back at the next
//     room change. Saves never carry the copy anyway (coop_save.cpp).
// When the other game is back (it restarted and connected again), co-op resumes by itself: with 3 the
// copy comes back at the next room load (your pick: rejoin automatically).
// The longest silence seen while connected is logged, to check kLostMs against real loads.

#include "link_watch.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "follow.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kLostMs = 3000;
constexpr std::uintptr_t kSoftReset = 0x154DB0;  // (): sets exe+0xABAC5A = 1, the game's soft reset request
constexpr std::uint8_t kSoftResetBytes[] = {0xC6, 0x05, 0xA3, 0x5E, 0x96, 0x00, 0x01, 0xC3};

enum class State { kWaiting, kConnected, kLost, kSolo };
State g_state = State::kWaiting;
bool g_host = false;
std::uint32_t g_longest = 0;  // longest silence while connected, ms
std::uint32_t g_gap = 0;      // the current silence (age last frame)
std::uint32_t g_losses = 0;

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
        return;
    }
    Log("link watch: leaving to the title screen (the game's soft reset)");
    fn();
}

}  // namespace

void LinkWatchInit(bool host) {
    g_host = host;
    Log("link watch: on: %s game counts as gone after %u s without a packet", Other(), kLostMs / 1000);
}

bool LinkLost() { return g_state == State::kLost || g_state == State::kSolo; }

void LinkWatchFrame() {
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
            Log("link watch: %s game is back: co-op again; the Sora copy comes back at the next room load", Other());
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
    Log("link watch: connection lost: nothing from %s game for %u ms; waiting for it (choices for now: "
        "tools/debug_cmd.py <port> link 1 wait / 2 leave to the title screen / 3 play on with Donald and Goofy)",
        Other(), age);
}

void LinkWatchChoose(int choice) {
    if (choice == 2 && LinkLost()) {  // also after playing on with Donald and Goofy
        LeaveToTitle();
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
