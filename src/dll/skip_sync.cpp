// Cutscene skip follows the host (TODO 5.3, your rule session 20 + pick session 27): only the host can skip a
// story cutscene; when the host skips, the friend's game skips at the same moment.
//
// How the game skips (VERIFIED_OFFSETS row 'Skip Scene', bench session 27): the pause menu's "Skip Scene"
// (exe+0x2DB142, only when the scene frame exe+0xB64F98 is past 9 and no skip is pending) jumps to
// exe+0x2CFCD0, which sets the skip request byte exe+0xB64F9C to 1. The scene's frame update exe+0x2CF980
// sees it and ends the scene through exe+0x2CBD80, the same end a scene reaches when it runs out; the
// scenes chained after it are skipped too (the host went 04/08 -> 04/01 -> 04/08 -> Bailey on one press).
// (exe+0x2CB4F0, the older skip with a fade, isn't used by the PC pause menu.)
//
// Host: our hook on exe+0x2CFCD0 lets the request through and sends "KH2K" with the scene's name to the
// friend. Friend: its own skip request is refused while the host is connected (the scene goes on); on
// "KH2K" it requests the skip of its own scene with the same call once its pause menu is closed (the shared
// pause closes it), and tells room following to let its skip chain's moves through (follow.cpp).
// Which scene: the one the friend is playing when the message arrives, else (still loading) the next
// scene of that name to start within 5 s.

#include "skip_sync.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "follow.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSkip = 0x2CFCD0;  // (): the pause menu's Skip Scene (sets kSkipRequest)
constexpr std::uint8_t kSkipBytes[] = {0xC6, 0x05, 0xC5, 0x52, 0x89, 0x00, 0x01, 0xE9};
constexpr std::uintptr_t kSkipRequest = 0xB64F9C;  // u8: 1 = end the scene on its next frame
constexpr std::uintptr_t kSceneFrame = 0xB64F98;   // s32: the scene's frame
constexpr std::uintptr_t kSceneState = 0xB65210;  // u32: 3 = playing, 4 = ending (skipped or done)
constexpr std::uintptr_t kSceneName = 0xB65578;   // char[]: the scene's name (SetProject), e.g. hb_event_107
constexpr std::uintptr_t kOpenMenu = 0x7435D0;    // u8: 0xFF = no menu
constexpr int kScenePlaying = 3;
constexpr std::uint32_t kMagic = 0x4B32484B;  // "KH2K"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint64_t kPendingFrames = 300;  // 5 s

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t id;  // counts the host's skips; repeats are ignored
    char scene[32];
};
#pragma pack(pop)

using PFN_Skip = void(__fastcall*)();
PFN_Skip g_real = nullptr;
bool g_on = false, g_host = false;
bool g_ourCall = false;  // the friend's skip we started for the host's
std::uint64_t g_frame = 0;
std::uint32_t g_sentId = 0, g_seenId = 0, g_refusedLogs = 0;
// Friend: a host skip to apply. anyScene = our scene was already playing when it arrived (skip it,
// whatever its name); else only a scene named `scene` that starts before `until`.
struct Pending {
    bool valid, anyScene;
    char scene[32];
    std::uint64_t until;
} g_pending {};

template <typename T>
T Read(std::uintptr_t offset) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + offset);
}

void SceneName(char* out, std::size_t size) {
    strncpy_s(out, size, reinterpret_cast<const char*>(ExeBase() + kSceneName), _TRUNCATE);
}

bool HostConnected() { return AvatarLinkPeerAgeMs() < 1000; }

void Send(const char* scene) {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), ++g_sentId, {}};
    strncpy_s(p.scene, scene, _TRUNCATE);
    for (int i = 0; i < 3; ++i) AvatarLinkSendRaw(&p, sizeof(p));  // sent three times, like other one-off events
}

// The menu asks only while the scene is playing past its first frames, with no skip pending.
bool Skippable() {
    return Read<std::int32_t>(kSceneState) == kScenePlaying && Read<std::int32_t>(kSceneFrame) > 9 &&
           Read<std::uint8_t>(kSkipRequest) == 0;
}

void __fastcall HookSkip() {
    char scene[32];
    SceneName(scene, sizeof(scene));
    if (!g_host && !g_ourCall && HostConnected()) {
        if (g_refusedLogs++ < 50) Log("skip sync: our Skip Scene refused in '%s': only the host skips cutscenes", scene);
        return;
    }
    bool playing = Read<std::int32_t>(kSceneState) == kScenePlaying;
    g_real();
    if (!g_host || !playing) return;
    Send(scene);
    Log("skip sync: we skipped '%s'; telling the friend's game to skip too", scene);
}

void Apply() {
    char scene[32];
    SceneName(scene, sizeof(scene));
    g_pending.valid = false;
    FollowHostSkipped();  // the window runs from our own skip
    g_ourCall = true;
    g_real();
    g_ourCall = false;
    bool took = Read<std::uint8_t>(kSkipRequest) != 0;
    Log("skip sync: skipped '%s' like the host%s", scene, took ? "" : " (the game didn't take it)");
}

}  // namespace

void SkipSyncInit(bool host) {
    g_host = host;
    if (EnvInt("KH2COOP_SKIP_SYNC", 1) != 1) {
        Log("skip sync: off (SKIP_SYNC=0): each player skips cutscenes on their own");
        return;
    }
    g_on = HookFunction(kSkip, kSkipBytes, sizeof(kSkipBytes), reinterpret_cast<void*>(HookSkip),
                        reinterpret_cast<void**>(&g_real), "Skip Scene");
    Log("skip sync: %s", !g_on ? "OFF: the hook failed"
                         : host ? "on: when we skip a cutscene, the friend's game skips it too"
                                : "on: only the host skips cutscenes; ours follow the host's");
}

void SkipSyncOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild() || p.id == g_seenId) return;
    g_seenId = p.id;
    p.scene[sizeof(p.scene) - 1] = 0;
    char ours[32];
    SceneName(ours, sizeof(ours));
    bool playing = Read<std::int32_t>(kSceneState) == kScenePlaying;
    g_pending = {true, playing, {}, g_frame + kPendingFrames};
    strncpy_s(g_pending.scene, p.scene, _TRUNCATE);
    Log("skip sync: the host skipped '%s'; %s", p.scene,
        playing ? "skipping ours as soon as our pause menu is closed" : "we aren't in a cutscene yet: skipping it when it starts");
    if (playing && std::strcmp(ours, p.scene) != 0) Log("skip sync: (ours is '%s', not the same scene)", ours);
    FollowHostSkipped();  // also replays a move refused a moment ago (our scene ended before the host skipped)
}

void SkipSyncFrame() {
    if (!g_on || g_host) return;
    ++g_frame;
    if (!g_pending.valid) return;
    if (g_frame > g_pending.until) {
        Log("skip sync: the host's skip of '%s' wasn't used: no such cutscene here within 5 s", g_pending.scene);
        g_pending.valid = false;
        return;
    }
    if (!Skippable() || Read<std::uint8_t>(kOpenMenu) != 0xFF) return;
    char scene[32];
    SceneName(scene, sizeof(scene));
    if (!g_pending.anyScene && std::strcmp(scene, g_pending.scene) != 0) return;
    __try {
        Apply();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_ourCall = false;
        Log("skip sync: skipping failed (exception)");
    }
}

}  // namespace kh2coop
