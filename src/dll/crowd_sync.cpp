// The exe's crowd system the same in both games (TODO 1.149; your pick 2026-10-30: full crowd sync).
//
// Step 1, the same layout: the crowd (the 1000 Heartless' idle members, F401) is set up area by area by
// exe+0x427570(controller, place), which spreads each area's members with the game's random number exe+0x783CA0.
// Each game drew its own, so the two crowds differed from the first area (F404). Here each area's setup draws from a
// seed made from its place (the script gives both games the same), and the game's own random number is put back
// after, so nothing else in the game changes. Both games then hold the same members in the same list order (F404).
//
// Step 2, the host's crowd drives the friend's (one brain for the crowd): after each setup both games number their
// members in list order. A member is drawn only from its place and the bytes +0x14..+0x18 (exe+0x412450, L171), so
// the host sends those for each member that changed, "gone" for each member its game removed, and a few unchanged
// ones each frame so a lost packet heals; the friend's member runner writes them instead of running the member, so
// the friend's crowd makes no fighters, rings and knock-aways of its own: it shows the host's. The host's kill count
// goes along.
//
// Step 4, the friend's knock-aways (TODO 1.147; your pick: the knocker's game alone gets the prize): the friend's game
// runs the game's own knock test exe+0x427220 on its members (its own Sora's swing and facing; it plays the knock, the
// EXP and prize, the journal tally there) and sends the member and its push; the host's game knocks that member if it's
// still idle with the parts of exe+0x427C10 that aren't the prize: the kill count, the mission counter
// (exe+0x3FB2B0(1, 0)) and the flight state.

#include "crowd_sync.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "avatar_link.hpp"
#include "common.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kAreaSetup = 0x427570;  // (controller, place) -> controller
constexpr std::uint8_t kAreaSetupBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x41, 0x54, 0x41, 0x55};
constexpr std::uintptr_t kRandom = 0x783CA0;     // u32, x 0x10DCD per draw
constexpr std::uintptr_t kAreas = 0x2AF8410;     // 0x20 each: place at +0, members at +0x10
constexpr std::uintptr_t kAreaCount = 0x2AF8AA0;
constexpr std::uintptr_t kKills = 0x2AF8AB0;     // the crowd's kill count (1000 ends it)
constexpr std::uintptr_t kMemberList = 0xBF8;    // controller +0xBF8: the list the members are made in
constexpr std::uintptr_t kListHead = 0x10, kMemberNext = 0x24;  // first member; next member's handle
constexpr std::uintptr_t kLook = 0x14, kAlive = 0x19;  // member: drawn bytes +0x14..+0x18; runner's kept flag
// +0x17 is the animation frame, copied by the runner from the member's animation object each frame: 98% of all changes
// (bench 2026-10-30: 863655 in 30 s against 7182 of the place), so it isn't sent; the friend sets it as the runner does.
constexpr int kFrameByte = 0x17 - kLook;
constexpr std::uintptr_t kAnimHandle = 0x20, kKindHandle = 0x1C;  // member: animation object; the list's kind (+8)
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;  // (u32 handle) -> object or 0
constexpr std::uintptr_t kKnockTest = 0x427220;   // (member) -> 1: the player's swing knocked it (all its effects)
constexpr std::uintptr_t kMissionCount = 0x3FB2B0;  // (amount, counter): the mission's counter
constexpr std::uintptr_t kIdleState = 0x426C30, kFlightState = 0x426A80;  // member +0x50
constexpr std::uintptr_t kState = 0x50, kPush = 0x38;  // member: state; push (x, y, z, w), +0x44 float, +0x48 byte
using PFN_KnockTest = char(__fastcall*)(std::uintptr_t);
using PFN_MissionCount = void(__fastcall*)(int, int);
using PFN_AreaSetup = std::uintptr_t(__fastcall*)(std::uintptr_t, const float*);
using PFN_DecodeHandle = std::uintptr_t(__fastcall*)(std::uint32_t);

constexpr int kMaxMembers = 2048;
constexpr std::uint32_t kMagic = 0x7232484B;  // "KH2r"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kGone = 0x8000;       // entry index bit: the member is gone
constexpr int kEntriesPerPacket = 80;         // 24 + 80 * 14 = 1144 bytes
constexpr int kPacketsPerFrame = 3;           // changed members past that wait for the next frame
constexpr int kRefreshPerFrame = 8;           // unchanged members sent again each frame (all 645 in ~1.3 s)
constexpr std::uint64_t kFreshMs = 1000;

#pragma pack(push, 1)
struct Look {  // a member as drawn
    std::int16_t x, y, z;
    std::uint8_t bytes[5];  // +0x14..+0x18
    bool operator==(const Look& o) const { return std::memcmp(this, &o, sizeof(Look)) == 0; }
};
struct Entry {
    std::uint16_t index;  // | kGone
    Look look;
    std::uint8_t pad;
};
struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t count;  // entries
    std::uint32_t build;
    std::uint32_t layout;  // the places hash of both games' crowds (step 1): applied only on the same
    std::int32_t kills;
    std::uint32_t seq;
};
struct Knock {  // friend -> host, "KH2k"
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t index;
    std::uint32_t build;
    std::uint32_t layout;
    float push[4];
};
#pragma pack(pop)
static_assert(sizeof(Entry) == 14 && sizeof(Header) == 24 && sizeof(Knock) == 32, "packet layout");
constexpr std::uint32_t kKnockMagic = 0x6B32484B;  // "KH2k"

PFN_AreaSetup g_realAreaSetup = nullptr;
bool g_seed = false, g_sync = false;

// Both games: the members in list order after the last setup, and the hash of their places then.
std::uintptr_t g_member[kMaxMembers];
int g_memberCount = 0;
std::uint32_t g_layout = 0;
std::unordered_map<std::uintptr_t, std::uint16_t> g_index;
bool g_gone[kMaxMembers];

// Host: what the friend was last sent per member; the refresh cursor.
Look g_sent[kMaxMembers];
bool g_sentValid[kMaxMembers];
int g_refresh = 0;
std::uint32_t g_seq = 0;
std::uint32_t g_packets = 0, g_entries = 0, g_bytes = 0, g_changedWaiting = 0;
std::uint32_t g_changedBy[6];  // diagnostic: changes seen by field (place, +0x14..+0x18)
Knock g_knockIn[64];  // the friend's knock-aways, applied at the next controller run
int g_knockInCount = 0;
std::uint32_t g_knocksApplied = 0, g_knocksLate = 0;

// Friend: the host's latest per member.
Look g_hostLook[kMaxMembers];
bool g_hostHas[kMaxMembers], g_hostGone[kMaxMembers];
std::uint32_t g_hostLayout = 0, g_lastSeq = 0;
std::uint64_t g_hostAt = 0;
std::int32_t g_hostKills = -1;
std::uint32_t g_applied = 0, g_removed = 0, g_skipped = 0, g_wrongLayout = 0, g_received = 0;
bool g_knocked[kMaxMembers];  // knocked here: not tested again
Knock g_knockOut[32];         // sent at the next frame and the one after (a lost packet; the host takes it once)
int g_knockOutCount = 0, g_knockOutAgain = 0;
std::uint32_t g_knocksSent = 0;

// FNV-1a over the bytes.
std::uint32_t Hash(std::uint32_t h, const void* data, std::size_t size) {
    auto p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

std::int16_t Q(float v) { return static_cast<std::int16_t>(std::lround((std::max)(-32767.0f, (std::min)(32767.0f, v)))); }

Look LookOf(std::uintptr_t m) {
    auto p = reinterpret_cast<const float*>(m);
    Look l {Q(p[0]), Q(p[1]), Q(p[2]), {}};
    std::memcpy(l.bytes, reinterpret_cast<const void*>(m + kLook), sizeof(l.bytes));
    l.bytes[kFrameByte] = 0;
    return l;
}

// All members in list order: numbered for step 2, their count and a hash of their places (the same = same layout).
void NumberMembers(std::uintptr_t controller, int& count, std::uint32_t& hash) {
    count = 0;
    hash = 2166136261u;
    auto decode = reinterpret_cast<PFN_DecodeHandle>(ExeBase() + kDecodeHandle);
    std::uintptr_t list = *reinterpret_cast<const std::uintptr_t*>(controller + kMemberList);
    g_index.clear();
    if (list) {
        for (std::uintptr_t m = *reinterpret_cast<const std::uintptr_t*>(list + kListHead); m && count < kMaxMembers;
             m = decode(*reinterpret_cast<const std::uint32_t*>(m + kMemberNext))) {
            hash = Hash(hash, reinterpret_cast<const void*>(m), 12);
            g_member[count] = m;
            g_index[m] = static_cast<std::uint16_t>(count);
            ++count;
        }
    }
    g_memberCount = count;
    g_layout = hash;
    std::memset(g_gone, 0, sizeof(g_gone));
    std::memset(g_sentValid, 0, sizeof(g_sentValid));
    std::memset(g_hostHas, 0, sizeof(g_hostHas));
    std::memset(g_hostGone, 0, sizeof(g_hostGone));
    std::memset(g_knocked, 0, sizeof(g_knocked));
    g_knockInCount = g_knockOutCount = g_knockOutAgain = 0;
    g_refresh = 0;
}

std::uintptr_t __fastcall HookAreaSetup(std::uintptr_t controller, const float* place) {
    auto random = reinterpret_cast<std::uint32_t*>(ExeBase() + kRandom);
    std::uint32_t saved = *random, seed = 0;
    if (g_seed) {
        seed = Hash(2166136261u, place, 12) | 1;  // odd: a multiply-only generator stays odd
        *random = seed;
    }
    std::uintptr_t result = g_realAreaSetup(controller, place);
    if (g_seed) *random = saved;
    __try {
        std::uintptr_t exe = ExeBase();
        int area = *reinterpret_cast<const std::int32_t*>(exe + kAreaCount) - 1;
        int members = area >= 0 ? *reinterpret_cast<const std::int32_t*>(exe + kAreas + 0x20 * area + 0x10) : -1;
        int all = 0;
        std::uint32_t hash = 0;
        if (result) NumberMembers(result, all, hash);
        Log("crowd: area %d at (%.0f, %.0f, %.0f) set up, %d members (seed %08X); all members %d, places hash %08X",
            area, place[0], place[1], place[2], members, seed, all, hash);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_memberCount = 0;
        Log("crowd: area set up (its members couldn't be read; no crowd sync until the next setup)");
    }
    return result;
}

int IndexOf(std::uintptr_t member) {
    auto it = g_index.find(member);
    return it == g_index.end() ? -1 : it->second;
}

}  // namespace

// ---- host ----
void CrowdSyncHostMember(std::uintptr_t member, char result) {
    if (!g_sync || result || !AvatarLinkIsHost()) return;
    int i = IndexOf(member);  // the map is only read while members run (the list run may use worker threads)
    if (i >= 0) g_gone[i] = true;  // the runner's caller unlinks and frees it now: never read again
}

void CrowdSyncHostBeforeRun() {
    if (!g_sync || !g_knockInCount) return;
    std::uintptr_t exe = ExeBase();
    for (int k = 0; k < g_knockInCount; ++k) {
        const Knock& c = g_knockIn[k];
        int i = c.index;
        if (c.layout != g_layout || i >= g_memberCount || g_gone[i]) continue;
        std::uintptr_t m = g_member[i];
        __try {
            if (*reinterpret_cast<const std::uintptr_t*>(m + kState) != exe + kIdleState) {  // already going, or ours
                ++g_knocksLate;
                continue;
            }
            std::memcpy(reinterpret_cast<void*>(m + kPush), c.push, sizeof(c.push));
            *reinterpret_cast<float*>(m + kPush + 4) = -30.0f;  // as exe+0x427220 / 0x427C10 write them
            *reinterpret_cast<float*>(m + 0x44) = 14.0f;
            *reinterpret_cast<std::uint8_t*>(m + 0x48) = 0x14;
            *reinterpret_cast<std::uintptr_t*>(m + kState) = exe + kFlightState;
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(exe + kKills));
            reinterpret_cast<PFN_MissionCount>(exe + kMissionCount)(1, 0);
            ++g_knocksApplied;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_knockInCount = 0;
}

void CrowdSyncOnKnock(const char* buf, int n) {
    if (!g_sync || !AvatarLinkIsHost() || n != static_cast<int>(sizeof(Knock))) return;
    Knock c;
    std::memcpy(&c, buf, sizeof(c));
    if (c.version != kVersion || c.build != AvatarLinkBuild() || g_knockInCount == 64) return;
    g_knockIn[g_knockInCount++] = c;
}

void CrowdSyncHostAfterRun() {
    if (!g_sync || !g_memberCount) return;
    char buf[sizeof(Header) + kEntriesPerPacket * sizeof(Entry)];
    auto* h = reinterpret_cast<Header*>(buf);
    auto* e = reinterpret_cast<Entry*>(buf + sizeof(Header));
    int n = 0, packets = 0;
    std::int32_t kills = *reinterpret_cast<const std::int32_t*>(ExeBase() + kKills);
    auto flush = [&]() {
        if (!n) return;
        *h = {kMagic, kVersion, static_cast<std::uint16_t>(n), AvatarLinkBuild(), g_layout, kills, ++g_seq};
        int len = static_cast<int>(sizeof(Header) + n * sizeof(Entry));
        AvatarLinkSendRaw(buf, len);
        ++g_packets;
        g_entries += n;
        g_bytes += len;
        ++packets;
        n = 0;
    };
    auto add = [&](int i, bool gone, const Look& look) {
        e[n] = {static_cast<std::uint16_t>(i | (gone ? kGone : 0)), look, 0};
        if (++n == kEntriesPerPacket) flush();
    };
    // Changed members first (gone ones are sent once here, then by the refresh).
    int waiting = 0;
    for (int i = 0; i < g_memberCount; ++i) {
        if (g_gone[i]) {
            if (g_sentValid[i]) {
                if (packets < kPacketsPerFrame) {
                    add(i, true, {});
                    g_sentValid[i] = false;
                } else {
                    ++waiting;
                }
            }
            continue;
        }
        Look look;
        __try {
            look = LookOf(g_member[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (g_sentValid[i] && g_sent[i] == look) continue;
        if (g_sentValid[i]) {
            const Look& o = g_sent[i];
            g_changedBy[0] += o.x != look.x || o.y != look.y || o.z != look.z;
            for (int b = 0; b < 5; ++b) g_changedBy[1 + b] += o.bytes[b] != look.bytes[b];
        }
        if (packets >= kPacketsPerFrame) {
            ++waiting;
            continue;
        }
        add(i, false, look);
        g_sent[i] = look;
        g_sentValid[i] = true;
    }
    // A few more as they stand, round the list, so a lost packet heals within ~0.5 s.
    for (int k = 0; k < kRefreshPerFrame && packets < kPacketsPerFrame && g_memberCount; ++k) {
        int i = g_refresh++ % g_memberCount;
        if (g_gone[i]) {
            add(i, true, {});
        } else if (g_sentValid[i]) {
            add(i, false, g_sent[i]);
        }
    }
    flush();
    g_changedWaiting += waiting;
}

// ---- friend ----
void CrowdSyncOnPacket(const char* buf, int n) {
    if (!g_sync || AvatarLinkIsHost() || n < static_cast<int>(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, buf, sizeof(h));
    if (h.version != kVersion || h.build != AvatarLinkBuild()) return;
    if (n != static_cast<int>(sizeof(Header) + h.count * sizeof(Entry))) return;
    ++g_received;
    g_hostAt = GetTickCount64();
    g_hostLayout = h.layout;
    if (h.layout != g_layout) {
        ++g_wrongLayout;
        return;
    }
    if (h.seq > g_lastSeq || g_lastSeq - h.seq > 1000000) {  // a late packet's kills are older
        g_lastSeq = h.seq;
        g_hostKills = h.kills;
    }
    for (int k = 0; k < h.count; ++k) {
        Entry e;
        std::memcpy(&e, buf + sizeof(Header) + k * sizeof(Entry), sizeof(e));
        int i = e.index & ~kGone;
        if (i >= g_memberCount) continue;
        if (e.index & kGone) {
            g_hostGone[i] = true;
        } else if (!g_hostGone[i]) {
            g_hostLook[i] = e.look;
            g_hostHas[i] = true;
        }
    }
}

bool CrowdSyncFriendMember(std::uintptr_t member, char& result) {
    if (!g_sync || AvatarLinkIsHost() || !g_memberCount) return false;
    // shortcut: run on the game's thread while packets land on ours; a torn member shows for one frame at most.
    int i = IndexOf(member);
    result = 1;
    if (i < 0) {  // not one of the numbered members: kept as it is, never run (it would make fighters of its own)
        ++g_skipped;
        *reinterpret_cast<std::uint8_t*>(member + kAlive) = 1;
        return true;
    }
    if (g_hostGone[i]) {  // the game unlinks and frees it; members are made only at a setup, which renumbers
        result = 0;
        *reinterpret_cast<std::uint8_t*>(member + kAlive) = 0;
        ++g_removed;
        return true;
    }
    if (g_hostHas[i] && g_hostLayout == g_layout) {
        const Look& l = g_hostLook[i];
        auto p = reinterpret_cast<float*>(member);
        p[0] = l.x;
        p[1] = l.y;
        p[2] = l.z;
        auto look = reinterpret_cast<std::uint8_t*>(member + kLook);
        for (int b = 0; b < 5; ++b)
            if (b != kFrameByte) look[b] = l.bytes[b];
        ++g_applied;
    }
    // The friend's own swing (step 4): the game's knock test on the host's member here.
    if (!g_knocked[i] && g_hostHas[i] && g_knockOutCount < 32 &&
        reinterpret_cast<PFN_KnockTest>(ExeBase() + kKnockTest)(member)) {
        g_knocked[i] = true;
        Knock& c = g_knockOut[g_knockOutCount++];
        c = {kKnockMagic, kVersion, static_cast<std::uint16_t>(i), AvatarLinkBuild(), g_layout, {}};
        std::memcpy(c.push, reinterpret_cast<const void*>(member + kPush), sizeof(c.push));
        ++g_knocksSent;
    }
    // The animation frame, as the runner exe+0x411EA0 sets it after a member's state.
    auto decode = reinterpret_cast<PFN_DecodeHandle>(ExeBase() + kDecodeHandle);
    if (std::uintptr_t anim = decode(*reinterpret_cast<const std::uint32_t*>(member + kAnimHandle))) {
        std::uintptr_t kind = decode(*reinterpret_cast<const std::uint32_t*>(member + kKindHandle));
        int k = kind ? *reinterpret_cast<const std::int32_t*>(kind + 8) : 0;
        *reinterpret_cast<std::uint8_t*>(member + 0x17) =
            k == 0 ? *reinterpret_cast<const std::uint8_t*>(anim + 0xF) : k == 1 ? *reinterpret_cast<const std::uint8_t*>(anim + 0xD) : 0;
    }
    *reinterpret_cast<std::uint8_t*>(member + kAlive) = 1;
    return true;
}

void CrowdSyncFrame() {
    if (!g_sync) return;
    static std::uint64_t frame = 0;
    if (!AvatarLinkIsHost() && (g_knockOutCount || g_knockOutAgain)) {
        // shortcut: filled on the game's thread, sent on ours; same frame order in practice (like the members' data).
        static Knock again[32];
        for (int k = 0; k < g_knockOutAgain; ++k) AvatarLinkSendRaw(&again[k], sizeof(Knock));
        for (int k = 0; k < g_knockOutCount; ++k) AvatarLinkSendRaw(&g_knockOut[k], sizeof(Knock));
        std::memcpy(again, g_knockOut, g_knockOutCount * sizeof(Knock));
        g_knockOutAgain = g_knockOutCount;
        g_knockOutCount = 0;
    }
    if (!AvatarLinkIsHost() && g_hostKills >= 0 && GetTickCount64() - g_hostAt < kFreshMs)
        *reinterpret_cast<std::int32_t*>(ExeBase() + kKills) = g_hostKills;
    if (++frame % 600 != 0 || !g_memberCount) return;
    if (AvatarLinkIsHost()) {
        int gone = 0;
        for (int i = 0; i < g_memberCount; ++i) gone += g_gone[i];
        Log("crowd sync: members %d (gone %d), layout %08X; sent so far %u packets, %u entries, %u bytes; changed "
            "members held to a later frame %u; changes by field: place %u, +0x14 %u, +0x15 %u, +0x16 %u, +0x17 %u, "
            "+0x18 %u; the friend's knock-aways applied %u (already going %u)",
            g_memberCount, gone, g_layout, g_packets, g_entries, g_bytes, g_changedWaiting, g_changedBy[0],
            g_changedBy[1], g_changedBy[2], g_changedBy[3], g_changedBy[4], g_changedBy[5], g_knocksApplied,
            g_knocksLate);
    } else {
        Log("crowd sync: members %d, layout %08X (host's %08X); packets %u (other layout %u); member updates "
            "applied %u, removed %u, unnumbered kept %u; kills %d; our knock-aways sent %u",
            g_memberCount, g_layout, g_hostLayout, g_received, g_wrongLayout, g_applied, g_removed, g_skipped,
            g_hostKills, g_knocksSent);
    }
}

void CrowdSyncInit() {
    g_seed = EnvInt("KH2COOP_CROWD_SEED", 1) == 1;
    g_sync = g_seed && EnvInt("KH2COOP_CROWD_SYNC", 1) == 1;
    bool hooked = HookFunction(kAreaSetup, kAreaSetupBytes, sizeof(kAreaSetupBytes),
                               reinterpret_cast<void*>(HookAreaSetup), reinterpret_cast<void**>(&g_realAreaSetup),
                               "crowd area setup");
    if (!hooked) g_sync = false;
    Log("crowd: %s; %s", !hooked   ? "area setup hook failed: each game lays out its own crowd"
                         : g_seed ? "each crowd area is laid out from a seed made from its place, the same in both games"
                                  : "each game lays out its own crowd (CROWD_SEED=0); setups are logged",
        g_sync ? "the host's crowd drives the friend's (CROWD_SYNC=1; needs AI_NEAREST_PLAYER=1 for its hooks)"
               : "each game's crowd runs on its own");
}

}  // namespace kh2coop
