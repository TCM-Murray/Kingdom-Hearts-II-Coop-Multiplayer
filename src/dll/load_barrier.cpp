// Load barrier (TODO 5.10, user idea 2026-10-07): both games leave the black screen of a room load
// together, so cutscenes, enemies and bosses start at the same moment. Before it, the friend only
// started loading once the host had been in the new room for 0.5 s, so it came in ~1-1.5 s late.
//
// The hold point (research, session 12b): the room load is a task, exe+0x152680, one step per frame.
// It ends with
//     while (!RoomReady()) wait one frame;      // exe+0x156770 returns the byte at exe+0x7177B8
//     schedule exe+0x152CD0;                    // sets in-field (exe+0x9BA8D0) = 1, starts the room
// (calls at exe+0x1528DF / 0x1528FA, returning to 0x1528E4 / 0x1528FF). Answering "not ready" there
// keeps the game in its own wait loop on the black screen; the cutscene then plays normally (Hades
// scene, 3 s test). NOW (exe+0x717008) holds the destination with its final programs from the moment
// the load starts (in-field 0).
//
// Protocol ("KH2L" packets, each sent once over the reliable channel: AvatarLinkSendReliable):
//   host:   in-field drops to 0 with the friend in the field  -> LOADING(seq, destination)
//   friend: starts loading that room at once (follow.cpp FollowLoadNow), or sees it's already
//           loading it                                         -> ACK(seq); can't now -> NACK(seq)
//           in a cutscene (its own event may make the same move, e.g. Hades 06/06 -> 06/0F)
//                                    -> ACK, then NACK if our own load doesn't start within 2 s
//           with its chest item window up (TODO 1.167)
//                                    -> ACK, loads once the window is gone (NACK after LOAD_BARRIER_MS)
//   both:   at the end of the load, hold on the black screen. The friend sends READY(seq).
//   host:   releases on READY, on NACK, with no answer and the friend's game silent kAckMs, or after
//           LOAD_BARRIER_MS
//                                                              -> GO(seq), then fades in
//   friend: releases on GO, or after LOAD_BARRIER_MS + 1 s. The channel doesn't keep order: a LOADING older than
//           the one we have (same host run) is ignored.
// No friend in the field (title, other menus, not connected) = no hold. Same-room reloads (event ->
// boss battle, Continue) go through the same path, since they also drop in-field.
//
// Settings: LOAD_BARRIER=1 (default on, both PCs; compared in the settings check),
//           LOAD_BARRIER_MS=<ms> (longest hold waiting for the other game, default 5000).

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "follow.hpp"
#include "load_barrier.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kRoomReady = 0x156770;
constexpr std::uint8_t kRoomReadyBytes[] = {0x0F, 0xB6, 0x05, 0x41, 0x10, 0x5C, 0x00, 0xC3};
constexpr std::uint32_t kLoadWaitReturns[] = {0x1528E4, 0x1528FF};  // the load task's two calls
constexpr std::uintptr_t kNow = 0x717008;      // u8 world, room, door, pad, u16 map, btl, evt
constexpr std::uintptr_t kInField = 0x9BA8D0;  // u8: 0 while a room loads

constexpr std::uint32_t kMagic = 0x4C32484B;  // "KH2L"
constexpr std::uint16_t kVersion = 1;
enum : std::uint8_t { kLoading = 1, kAck = 2, kNack = 3, kReady = 4, kGo = 5 };
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;
    std::uint8_t pad;
    std::uint16_t seq;
    std::uint8_t world, room, door, pad2;
    std::uint16_t programs[3];
};
#pragma pack(pop)

// Host: no answer and nothing at all from the friend's game this long = it isn't coming (a link-silence timer, as
// the others; it was 1 s from the load's start, which a ~1 s round trip beat: TODO 8.11 / 6.3, network plan step 4).
constexpr std::uint32_t kAckMs = 2000;
// Friend: a LOADING whose GO never came (the host's load ended without a release) is forgotten after this; GO is
// reliable, so this is only a cap (it was 1.5 s of no repeated LOADING).
constexpr ULONGLONG kStaleMs = 15000;
// shortcut: the friend's own cutscene is guessed to start its move within 2 s, else the host goes alone (local).
// Real state: the friend's event asking for the move (not found yet). TODO 8.11.
constexpr ULONGLONG kOwnMoveMs = 2000;  // friend in a cutscene: time for its own event to start the move

using PFN_RoomReady = std::uint8_t(__fastcall*)();
PFN_RoomReady g_realRoomReady = nullptr;
bool g_on = false, g_host = false;
int g_maxHoldMs = 5000;
std::uint64_t g_frame = 0;

struct Destination {
    std::uint8_t world, room, door;
    std::uint16_t programs[3];
};

Destination ReadNow() {
    const auto* p = reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
    Destination d {p[0], p[1], p[2], {}};
    for (int i = 0; i < 3; ++i) d.programs[i] = *reinterpret_cast<const volatile std::uint16_t*>(p + 4 + 2 * i);
    return d;
}

bool SameDestination(const Destination& a, const Destination& b) {
    return a.world == b.world && a.room == b.room && std::memcmp(a.programs, b.programs, sizeof(a.programs)) == 0;
}

void Send(std::uint8_t kind, std::uint16_t seq, const Destination& d) {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), kind, 0, seq, d.world, d.room, d.door, 0, {}};
    std::memcpy(p.programs, d.programs, sizeof(p.programs));
    AvatarLinkSendReliable(&p, sizeof(p));
}

// ---- host ----
bool g_wasLoading = false;
std::uint8_t g_lastWorld = 0xFF;  // the world of our last frame in the field
std::uint16_t g_seq = 0;
Destination g_dest {};
bool g_active = false;            // this load waits for the friend
bool g_acked = false, g_nacked = false, g_friendReady = false;
ULONGLONG g_loadStart = 0, g_readySince = 0;
std::uint32_t g_holds = 0;

// ---- friend ----
std::uint16_t g_fSeq = 0;
std::uint32_t g_fRun = 0;        // the host's run g_fSeq is from (AvatarLinkPeerRun)
bool g_fHaveSeq = false;
Destination g_fDest {};
std::uint8_t g_fAnswer = 0;       // kAck / kNack sent for g_fSeq
bool g_fHolding = false, g_fGo = false;
bool g_fOwnMove = false;          // answered ACK during a cutscene: our own event should load it
bool g_fWindow = false;           // answered ACK with our item window up: we load it once the window is gone
ULONGLONG g_fHeard = 0, g_fHoldSince = 0, g_fAnswered = 0;

bool FriendInField() {
    PeerPose p;
    return AvatarLinkPeerNow(p) && p.hasActor && p.world != 0xFF;
}

void HostRelease(const char* why) {
    ULONGLONG now = GetTickCount64();
    Log("load barrier: released 0x%02X/0x%02X (load #%u): %s; held %llu ms, load to release %llu ms", g_dest.world,
        g_dest.room, g_seq, why, static_cast<unsigned long long>(g_readySince ? now - g_readySince : 0),
        static_cast<unsigned long long>(now - g_loadStart));
    g_active = false;
    g_readySince = 0;
    Send(kGo, g_seq, g_dest);
    ++g_holds;
}

// Host: true while the finished room must wait.
bool HostHolds() {
    if (!g_active) return false;
    ULONGLONG now = GetTickCount64();
    if (!g_readySince) {
        g_readySince = now;
        Log("load barrier: our room 0x%02X/0x%02X is ready after %llu ms; the friend %s", g_dest.world, g_dest.room,
            static_cast<unsigned long long>(now - g_loadStart),
            g_friendReady ? "is ready too"
            : g_nacked    ? "can't follow"
            : g_acked     ? "is still loading: waiting"
                          : "hasn't answered yet: waiting");
    }
    if (g_friendReady) {
        HostRelease("the friend is ready");
    } else if (g_nacked) {
        HostRelease("the friend can't follow right now");
    } else if (!g_acked && AvatarLinkPeerAgeMs() >= kAckMs) {
        HostRelease("no answer, the friend's game silent");
    } else if (now - g_readySince >= static_cast<ULONGLONG>(g_maxHoldMs)) {
        HostRelease("time limit, going in alone");
    }
    return g_active;
}

// Friend: true while our finished room must wait for the host's GO.
bool FriendHolds() {
    if (!g_fHaveSeq || g_fAnswer != kAck || g_fGo) return false;
    Destination now = ReadNow();
    if (now.world != g_fDest.world || now.room != g_fDest.room) return false;  // some other load
    ULONGLONG t = GetTickCount64();
    if (!g_fHolding) {
        g_fHolding = true;
        g_fHoldSince = t;
        Send(kReady, g_fSeq, g_fDest);
        Log("load barrier: our room 0x%02X/0x%02X is ready; waiting for the host (load #%u)", now.world, now.room,
            g_fSeq);
    }
    if (t - g_fHoldSince >= static_cast<ULONGLONG>(g_maxHoldMs) + 1000) {
        Log("load barrier: no GO from the host after %llu ms: going in", static_cast<unsigned long long>(t - g_fHoldSince));
        g_fGo = true;
        g_fHolding = false;
        return false;
    }
    return true;
}

std::uint8_t __fastcall HookRoomReady() {
    std::uint8_t ready = g_realRoomReady();
    if (!ready) return ready;
    auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase());
    if (rva != kLoadWaitReturns[0] && rva != kLoadWaitReturns[1]) return ready;
    bool hold = g_host ? HostHolds() : FriendHolds();
    return hold ? 0 : ready;
}

void HostFrame() {
    bool loading = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0;
    Destination now = ReadNow();
    if (loading && now.world != 0xFF && (!g_wasLoading || !SameDestination(now, g_dest))) {
        // A new load: announce it if the friend is in a room to follow from.
        g_dest = now;
        ++g_seq;
        // World Map to World Map is Gummi mode starting (a key door's Proceed): not a room to enter together, and this
        // "load" only ends when the player leaves the Gummi menu (TODO 5.2, run kh2coop_34432).
        constexpr std::uint8_t kWorldMap = 0x0F;
        bool gummi = !g_wasLoading && g_lastWorld == kWorldMap && now.world == kWorldMap;
        g_active = FriendInField() && !gummi;
        g_acked = g_nacked = g_friendReady = false;
        g_loadStart = GetTickCount64();
        g_readySince = 0;
        Log("load barrier: loading world 0x%02X room 0x%02X (programs %u/%u/%u, load #%u)%s", now.world, now.room,
            now.programs[0], now.programs[1], now.programs[2], g_seq,
            g_active ? "; telling the friend" : gummi ? "; Gummi mode starts: no waiting" : "; the friend isn't in a room: no waiting");
        if (g_active) Send(kLoading, g_seq, g_dest);
    }
    g_wasLoading = loading;
    if (!loading) g_lastWorld = now.world;
}

void FriendFrame() {
    if (g_fWindow) {
        const char* why = nullptr;
        ULONGLONG waited = GetTickCount64() - g_fAnswered;
        int r = FollowLoadNow(g_fDest.world, g_fDest.room, g_fDest.door, g_fDest.programs, &why);
        if (r == 4 && waited < static_cast<ULONGLONG>(g_maxHoldMs)) return;
        g_fWindow = false;
        if (r == 1) {
            Log("load barrier: our item window is gone after %llu ms: loading it now (load #%u)",
                static_cast<unsigned long long>(waited), g_fSeq);
        } else if (r == 2) {
            Send(kReady, g_fSeq, g_fDest);
            g_fGo = true;
            Log("load barrier: our item window is gone; we're already there (load #%u)", g_fSeq);
        } else {
            g_fAnswer = kNack;
            g_fGo = true;
            Send(kNack, g_fSeq, g_fDest);
            Log("load barrier: can't follow after our item window (%llu ms): %s; the host goes in alone",
                static_cast<unsigned long long>(waited), why ? why : "?");
        }
    }
    if (g_fOwnMove) {
        Destination now = ReadNow();
        bool loading = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0;
        if (loading && now.world == g_fDest.world && now.room == g_fDest.room) {
            g_fOwnMove = false;
            Log("load barrier: our cutscene moved us there too (load #%u)", g_fSeq);
        } else if (GetTickCount64() - g_fAnswered > kOwnMoveMs) {
            g_fOwnMove = false;
            g_fAnswer = kNack;
            g_fGo = true;
            Send(kNack, g_fSeq, g_fDest);
            Log("load barrier: our cutscene didn't move us within %llu ms: the host goes in alone",
                static_cast<unsigned long long>(kOwnMoveMs));
        }
    }
}

void FriendOnLoading(const Packet& p) {
    Destination d {p.world, p.room, p.door, {p.programs[0], p.programs[1], p.programs[2]}};
    ULONGLONG now = GetTickCount64();
    std::uint32_t run = AvatarLinkPeerRun();
    if (g_fHaveSeq && run == g_fRun && static_cast<std::int16_t>(p.seq - g_fSeq) <= 0) {
        Log("load barrier: the host's load #%u came after #%u (out of order): ignored", p.seq, g_fSeq);
        return;
    }
    g_fHaveSeq = true;
    g_fSeq = p.seq;
    g_fRun = run;
    g_fDest = d;
    g_fHeard = now;
    g_fHolding = g_fGo = g_fOwnMove = g_fWindow = false;
    g_fAnswered = now;
    const char* why = nullptr;
    int r = FollowLoadNow(d.world, d.room, d.door, d.programs, &why);
    if (r == 4) {
        g_fAnswer = kAck;
        g_fWindow = true;
        Send(kAck, g_fSeq, g_fDest);
        Log("load barrier: the host is loading world 0x%02X room 0x%02X (load #%u) while our item window is up: "
            "the host waits while it ends (up to %d ms)", d.world, d.room, p.seq, g_maxHoldMs);
        return;
    }
    if (r == 3) {
        g_fAnswer = kAck;
        g_fOwnMove = true;
        Send(kAck, g_fSeq, g_fDest);
        Log("load barrier: the host is loading world 0x%02X room 0x%02X (load #%u) while our cutscene runs: "
            "waiting up to %llu ms for it to move us too", d.world, d.room, p.seq,
            static_cast<unsigned long long>(kOwnMoveMs));
        return;
    }
    if (r == 2) {
        // Already there with the same programs: nothing to load, we're ready as we are.
        g_fAnswer = kAck;
        Send(kAck, g_fSeq, g_fDest);
        Send(kReady, g_fSeq, g_fDest);
        g_fGo = true;  // nothing of ours to hold
        Log("load barrier: the host reloads 0x%02X/0x%02X; we're already there (load #%u)", d.world, d.room, p.seq);
        return;
    }
    g_fAnswer = r == 1 ? kAck : kNack;
    Send(g_fAnswer, g_fSeq, g_fDest);
    Log("load barrier: the host is loading world 0x%02X room 0x%02X (load #%u): %s", d.world, d.room, p.seq,
        r == 1 ? "loading it too; we'll wait for each other" : why ? why : "can't follow");
}

}  // namespace

unsigned LoadBarrierLoadKey() {
    if (!g_on || (!g_host && !g_fHaveSeq)) return 0;
    Destination now = ReadNow();
    const Destination& d = g_host ? g_dest : g_fDest;
    if (now.world != d.world || now.room != d.room) return 0;
    return (g_host ? g_seq : g_fSeq) | (static_cast<unsigned>(now.world) << 24) | (static_cast<unsigned>(now.room) << 16);
}

void LoadBarrierInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_LOAD_BARRIER", 1) == 1 && EnvInt("KH2COOP_WORLD_SYNC", 1) == 1;
    g_maxHoldMs = EnvInt("KH2COOP_LOAD_BARRIER_MS", 5000);
    g_seq = static_cast<std::uint16_t>(GetTickCount64() * 7);  // a restarted host doesn't reuse old numbers
    if (!g_on) return;
    if (!HookFunction(kRoomReady, kRoomReadyBytes, sizeof(kRoomReadyBytes), reinterpret_cast<void*>(HookRoomReady),
                      reinterpret_cast<void**>(&g_realRoomReady), "room ready (load barrier)")) {
        g_on = false;
        return;
    }
    Log("load barrier: on (%s): both games leave a room load together; longest wait %d ms",
        host ? "host" : "friend", g_maxHoldMs);
}

void LoadBarrierFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        if (g_host) {
            HostFrame();
        } else {
            FriendFrame();
            // A LOADING whose GO never came (kStaleMs): don't hold for it.
            if (g_fHaveSeq && !g_fGo && GetTickCount64() - g_fHeard > kStaleMs && !g_fHolding) g_fGo = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void LoadBarrierOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (g_host) {
        if (!g_active || p.seq != g_seq) return;
        if (p.kind == kAck && !g_acked) {
            g_acked = true;
            Log("load barrier: the friend is loading it too (%llu ms after our load started)",
                static_cast<unsigned long long>(GetTickCount64() - g_loadStart));
        } else if (p.kind == kNack && !g_nacked) {
            g_nacked = true;
            Log("load barrier: the friend can't follow this load right now");
        } else if (p.kind == kReady && !g_friendReady) {
            g_friendReady = g_acked = true;
            Log("load barrier: the friend's room is ready (%llu ms after our load started)",
                static_cast<unsigned long long>(GetTickCount64() - g_loadStart));
        }
        return;
    }
    if (p.kind == kLoading) {
        FriendOnLoading(p);
    } else if (p.kind == kGo && g_fHaveSeq && p.seq == g_fSeq && !g_fGo) {
        g_fGo = true;
        Log("load barrier: GO from the host%s", g_fHolding ? "" : " (we weren't holding yet)");
        if (g_fHolding)
            Log("load barrier: released after holding %llu ms",
                static_cast<unsigned long long>(GetTickCount64() - g_fHoldSince));
        g_fHolding = false;
    }
}

}  // namespace kh2coop
