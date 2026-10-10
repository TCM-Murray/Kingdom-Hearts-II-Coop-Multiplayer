// Steam link: see steam_link.hpp. Research: VERIFIED_OFFSETS lead 'Steam networking'
// (the game's steam_api64.dll exports; Volpestyle's way of reaching the game's
// Steam: flat exports only, wait for the game's own SteamAPI_Init, connection
// events through a config-value function pointer). Struct layouts from Valve's
// headers (steamnetworkingtypes.h, steam_api_common.h, isteamfriends.h).

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "steam_link.hpp"

namespace kh2coop {
namespace {

#pragma pack(push, 1)
struct Identity {  // SteamNetworkingIdentity (packed, 136 bytes)
    std::int32_t type;
    std::int32_t size;
    std::uint8_t data[128];
};
#pragma pack(pop)
static_assert(sizeof(Identity) == 136);

struct Message {  // SteamNetworkingMessage_t, the fields we read (the rest belongs to Steam)
    void* data;
    std::int32_t size;
    std::uint32_t connection;
    Identity peer;
};

struct SessionRequest {  // SteamNetworkingMessagesSessionRequest_t
    Identity remote;
};

// CCallbackBase (steam_api_common.h), declared the same way so MSVC gives it the same vtable.
class CallbackBase {
public:
    virtual void Run(void* param) = 0;
    virtual void Run(void* param, bool ioFailure, std::uint64_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;

protected:
    std::uint8_t m_nCallbackFlags = 0;
    int m_iCallback = 0;
};

constexpr int kJoinRequested = 337;  // GameRichPresenceJoinRequested_t {u64 friend; char connect[256];}
constexpr int kJoinRequestedSize = 264;
constexpr int kConfigSessionRequest = 204;  // k_ESteamNetworkingConfig_Callback_MessagesSessionRequest
constexpr int kConfigSessionFailed = 205;   // k_ESteamNetworkingConfig_Callback_MessagesSessionFailed
constexpr int kSendFlags = 1 | 32;          // unreliable, no Nagle delay, restart a broken session
constexpr int kRelationshipFriend = 3;      // k_EFriendRelationshipFriend
constexpr std::uint32_t kPeerSilentMs = 10000;  // a joined friend this quiet counts as gone

struct Api {
    std::int32_t (*getHSteamUser)();
    void* (*user)();
    void* (*friends)();
    void* (*sockets)();
    void* (*messages)();
    void* (*utils)();
    std::uint64_t (*getSteamID)(void*);
    const char* (*personaName)(void*, std::uint64_t);
    int (*relationship)(void*, std::uint64_t);
    bool (*setRichPresence)(void*, const char*, const char*);
    void (*runCallbacks)(void*);
    void (*initRelay)(void*);
    bool (*setGlobalPtr)(void*, int, void*);
    int (*send)(void*, const Identity*, const void*, std::uint32_t, int, int);
    int (*receive)(void*, int, Message**, int);
    bool (*accept)(void*, const Identity*);
    void (*release)(Message*);
    void (*setSteamID64)(Identity*, std::uint64_t);
    std::uint64_t (*getSteamID64)(const Identity*);
    void (*registerCallback)(CallbackBase*, int);
};

Api g_api {};
bool g_on = false;      // Steam mode chosen in the settings
bool g_resolved = false;  // every steam_api64.dll export we use was found
bool g_ready = false;   // the game's Steam is up and our part is set up
bool g_host = false;
bool g_joinable = false;  // our "connect" rich presence is set (host, co-op on)
std::uint32_t g_tries = 0;
void* g_user = nullptr;
void* g_friends = nullptr;
void* g_sockets = nullptr;
void* g_messages = nullptr;
void* g_utils = nullptr;
std::uint64_t g_self = 0;
std::uint64_t g_peer = 0;
std::uint64_t g_lastRefused = 0;
Identity g_peerIdentity {};

const char* Name(std::uint64_t id) {
    const char* name = g_friends ? g_api.personaName(g_friends, id) : nullptr;
    return name && *name ? name : "?";
}

void SetPeer(std::uint64_t id) {
    g_peer = id;
    std::memset(&g_peerIdentity, 0, sizeof(g_peerIdentity));
    g_api.setSteamID64(&g_peerIdentity, id);
}

// A Steam user started a session with us (their first message). Called from
// RunCallbacks, on the game thread.
void OnSessionRequest(SessionRequest* request) {
    std::uint64_t id = g_api.getSteamID64(&request->remote);
    bool known = id != 0 && id == g_peer;
    bool free = g_peer == 0 || AvatarLinkPeerAgeMs() > kPeerSilentMs;
    bool isFriend = g_api.relationship(g_friends, id) == kRelationshipFriend;
    if (!known && !(g_host && free && isFriend)) {
        if (id != g_lastRefused)
            Log("steam link: %s (%llu) tried to connect; refused (%s)", Name(id), static_cast<unsigned long long>(id),
                !g_host ? "this PC isn't the host" : !isFriend ? "not on your Steam friends list"
                                                               : "another friend is already connected");
        g_lastRefused = id;
        return;
    }
    g_api.accept(g_messages, &request->remote);
    if (!known) {
        SetPeer(id);
        Log("steam link: %s (%llu) joined", Name(id), static_cast<unsigned long long>(id));
    }
}

void OnSessionFailed(void* failed) {  // SteamNetworkingMessagesSessionFailed_t: starts with the remote's identity
    std::uint64_t id = g_api.getSteamID64(static_cast<const Identity*>(failed));
    Log("steam link: the Steam connection to %s (%llu) failed or closed", Name(id), static_cast<unsigned long long>(id));
}

// Join Game in the Steam friends list, or an accepted Invite to Game.
class JoinRequested : public CallbackBase {
public:
    JoinRequested() { m_iCallback = kJoinRequested; }
    void Run(void* param) override {
        std::uint64_t host;
        std::memcpy(&host, param, sizeof(host));
        if (g_host) {
            Log("steam link: Join Game on %s ignored: this PC is the host", Name(host));
            return;
        }
        SetPeer(host);
        Log("steam link: Join Game: connecting to %s (%llu)%s", Name(host), static_cast<unsigned long long>(host),
            CoopActive() ? "" : "; load your save through LOAD (CO-OP) to start");
    }
    void Run(void* param, bool, std::uint64_t) override { Run(param); }
    int GetCallbackSizeBytes() override { return kJoinRequestedSize; }
};
JoinRequested g_joinRequested;

template <typename T>
bool Get(HMODULE dll, T& out, const char* name) {
    out = reinterpret_cast<T>(GetProcAddress(dll, name));
    if (!out) Log("steam link: steam_api64.dll has no %s; Steam connection OFF", name);
    return out != nullptr;
}

bool Resolve() {
    HMODULE dll = GetModuleHandleW(L"steam_api64.dll");
    Api& a = g_api;
    return Get(dll, a.getHSteamUser, "SteamAPI_GetHSteamUser") && Get(dll, a.user, "SteamAPI_SteamUser_v023") &&
           Get(dll, a.friends, "SteamAPI_SteamFriends_v017") &&
           Get(dll, a.sockets, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012") &&
           Get(dll, a.messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002") &&
           Get(dll, a.utils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004") &&
           Get(dll, a.getSteamID, "SteamAPI_ISteamUser_GetSteamID") &&
           Get(dll, a.personaName, "SteamAPI_ISteamFriends_GetFriendPersonaName") &&
           Get(dll, a.relationship, "SteamAPI_ISteamFriends_GetFriendRelationship") &&
           Get(dll, a.setRichPresence, "SteamAPI_ISteamFriends_SetRichPresence") &&
           Get(dll, a.runCallbacks, "SteamAPI_ISteamNetworkingSockets_RunCallbacks") &&
           Get(dll, a.initRelay, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess") &&
           Get(dll, a.setGlobalPtr, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValuePtr") &&
           Get(dll, a.send, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser") &&
           Get(dll, a.receive, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel") &&
           Get(dll, a.accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser") &&
           Get(dll, a.release, "SteamAPI_SteamNetworkingMessage_t_Release") &&
           Get(dll, a.setSteamID64, "SteamAPI_SteamNetworkingIdentity_SetSteamID64") &&
           Get(dll, a.getSteamID64, "SteamAPI_SteamNetworkingIdentity_GetSteamID64") &&
           Get(dll, a.registerCallback, "SteamAPI_RegisterCallback");
}

// Sets our part up once the game has started Steam (it calls SteamAPI_Init itself; we never do).
void TryStart() {
    if (!g_resolved) {
        if (!GetModuleHandleW(L"steam_api64.dll")) {
            if (g_tries == 1) Log("steam link: steam_api64.dll not loaded yet; waiting");
            return;
        }
        if (!Resolve()) {
            g_on = false;  // logged by Get
            return;
        }
        g_resolved = true;
    }
    if (g_api.getHSteamUser() == 0) return;  // the game's Steam isn't up yet
    g_user = g_api.user();
    g_friends = g_api.friends();
    g_sockets = g_api.sockets();
    g_messages = g_api.messages();
    g_utils = g_api.utils();
    if (!g_user || !g_friends || !g_sockets || !g_messages || !g_utils) return;
    g_self = g_api.getSteamID(g_user);
    g_api.initRelay(g_utils);
    if (!g_api.setGlobalPtr(g_utils, kConfigSessionRequest, reinterpret_cast<void*>(&OnSessionRequest)) ||
        !g_api.setGlobalPtr(g_utils, kConfigSessionFailed, reinterpret_cast<void*>(&OnSessionFailed))) {
        Log("steam link: Steam refused our connection callbacks; Steam connection OFF");
        g_on = false;
        return;
    }
    g_api.registerCallback(&g_joinRequested, kJoinRequested);
    g_ready = true;
    if (g_peer) SetPeer(g_peer);
    Log("steam link: ready as %s (Steam ID %llu); %s", Name(g_self), static_cast<unsigned long long>(g_self),
        g_peer   ? "sending to the Steam ID in PEER"
        : g_host ? "your friend can use Join Game in the Steam friends list once co-op is on"
                 : "waiting for Join Game (Steam overlay: right-click the host > Join Game)");
}

}  // namespace

void SteamLinkInit(bool host, std::uint64_t peer) {
    g_on = true;
    g_host = host;
    g_peer = peer;
}

void SteamLinkFrame(bool coopOn) {
    if (!g_on) return;
    if (!g_ready) {
        if (g_tries++ % 60 == 0) TryStart();  // about once a second
        if (!g_ready && g_tries == 60 * 60) Log("steam link: Steam still not ready after a minute; is Steam running?");
        return;
    }
    g_api.runCallbacks(g_sockets);
    bool joinable = g_host && coopOn;
    if (joinable != g_joinable) {
        g_joinable = joinable;
        g_api.setRichPresence(g_friends, "status", joinable ? "KH2 Co-op: hosting" : "");
        g_api.setRichPresence(g_friends, "connect", joinable ? "+kh2coop" : "");
        Log("steam link: %s", joinable ? "joinable: your Steam friends see Join Game on you"
                                       : "not joinable (co-op is off)");
    }
}

bool SteamLinkHasPeer() { return g_ready && g_peer != 0; }

void SteamLinkSend(const void* data, int len) {
    if (!SteamLinkHasPeer() || len <= 0) return;
    g_api.send(g_messages, &g_peerIdentity, data, static_cast<std::uint32_t>(len), kSendFlags, 0);
}

int SteamLinkReceive(char* buf, int size) {
    if (!g_ready) return 0;
    for (;;) {
        Message* message = nullptr;
        if (g_api.receive(g_messages, 0, &message, 1) <= 0 || !message) return 0;
        std::uint64_t from = g_api.getSteamID64(&message->peer);
        int n = 0;
        if (from == g_peer && message->size > 0 && message->size <= size) {
            std::memcpy(buf, message->data, message->size);
            n = message->size;
        }
        g_api.release(message);
        if (n) return n;  // anything else (another account, too big) is dropped
    }
}

}  // namespace kh2coop
