// A Limit sends the allies away in both games (docs/DECISIONS.md L1-L3; you, 2026-10-14: "when a Limit happens, the
// allies disappear with their HUD except Sora's copy", in both games, the Limit's partner stays for the Limit).
//
// In the game of the player who uses it, the Limit start exe+0x3D8B40 sends each party member that isn't the
// Limit's partner away with exe+0x400990(member, 0.0f): the game's fade-out removal (+0x9B8 |= 0x10, gone the next
// frame, the HUD follows) plus its id on a "bring back" list (exe+0x2AE9788, count exe+0x2AE9790); the Limit end
// exe+0x3D8F40 builds everyone on that list again with exe+0x400440() (VERIFIED_OFFSETS lead 'How a Limit sends the
// allies away'). The other game ran none of it: its allies stayed, and while the host's Limit ran the friend's game
// let its mirrored ally go back to its own AI (bench 2026-10-14).
// Here each game tells the other while its Limit runs ("KH2I": running, the partners' object ids); the other game
// sends its own allies away with the same game call, except the one with a partner's object id (it plays the
// partner: the friend's mirrored companion, or the host's ally borrowed for the friend's partner, world_sync.cpp
// TODO 1.57), and brings them back with the game's own call when that Limit ends.
// With the allies gone the game itself should offer no Limit to the other player (you, 2026-10-14), so only one
// Limit runs at a time; two pressed in the same moment may both start (accepted).
// LIMIT_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include "limit_sync.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4932484B;  // "KH2I"
constexpr std::uint16_t kVersion = 1;
constexpr std::uintptr_t kCurrentLimit = 0x2A24CC0;  // the running Limit's object, 0 = none
constexpr std::uintptr_t kLimitFlags = 0x54;        // its flags: bit 2 set by the Limit end exe+0x3D8F40
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;   // u64[2] party members
constexpr std::uintptr_t kSummonState = 0x2A24EDC;   // u32, 2 = a summon is out
constexpr std::uintptr_t kNow = 0x717008;            // u16 world, room
constexpr std::uintptr_t kObjRow = 0x918, kRemoveFlags = 0x9B8;  // +0x9B8 bit 4: being removed
constexpr std::uintptr_t kLeave = 0x400990;          // (member, float fade): leave for the Limit, on the bring-back list
constexpr std::uint8_t kLeaveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uintptr_t kBringBack = 0x400440;      // (): build everyone on the bring-back list again
constexpr std::uint8_t kBringBackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
constexpr std::uintptr_t kLive = 0x3BA720;           // (actor) -> in the actor list and not going
constexpr std::uint32_t kPeerTimeoutMs = 1000;       // no word this long: the other player's Limit is over
using PFN_Leave = void(__fastcall*)(std::uintptr_t, float);
using PFN_BringBack = void(__fastcall*)();
using PFN_Live = std::uint8_t(__fastcall*)(std::uintptr_t);

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint8_t running, pad;
    std::uint16_t room;         // the sender's world and room
    std::uint32_t partners[2];  // the Limit's partners (object ids), 0 = none
};
#pragma pack(pop)

bool g_on = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;
bool g_ourWas = false;
int g_endSends = 0;  // "not running" a few more times after our Limit ends

Packet g_peer{};  // the other player's latest word
std::uint32_t g_peerMs = 0;
bool g_peerWas = false;
int g_sentAway = 0;  // our allies we sent away for the other player's Limit
std::uint16_t g_room = 0;
bool g_stayWas[2] = {true, true};  // for the log: when an ally leaves or comes back

template <typename T>
T Read(std::uintptr_t a) {
    return *reinterpret_cast<const volatile T*>(a);
}

std::uint16_t Room() { return Read<std::uint16_t>(ExeBase() + kNow); }

std::uint32_t ObjId(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

std::uintptr_t Slot(int i) { return Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * i); }

// A party member that is here and not already leaving.
bool Staying(std::uintptr_t a) {
    __try {
        return a && reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a) && !(Read<std::uint32_t>(a + kRemoveFlags) & 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CodeMatches(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n, const char* name) {
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0) return true;
    Log("limit sync: %s at exe+0x%llX isn't the expected code: off", name, static_cast<unsigned long long>(rva));
    return false;
}

void Send(bool running) {
    Packet p{kMagic, kVersion, AvatarLinkBuild(), ++g_seq, static_cast<std::uint8_t>(running ? 1 : 0), 0, Room(), {}};
    if (running)  // our members still staying are the Limit's partners (the start sent the others away)
        for (int i = 0; i < 2; ++i)
            if (Staying(Slot(i))) p.partners[i] = ObjId(Slot(i));
    AvatarLinkSendRaw(&p, static_cast<int>(sizeof(p)));
}

// Our Limit's moves run: the game's current Limit is ours and its end (exe+0x3D8F40, which brings the allies back)
// hasn't run yet. The pointer itself stays set about 1 s after that end (bench 2026-10-14), so the other game
// follows the end mark and its allies come back with the user's.
bool OurLimitMoves() {
    if (!PuppetOurLimitRunning()) return false;
    __try {
        std::uintptr_t lim = Read<std::uintptr_t>(ExeBase() + kCurrentLimit);
        return lim && !(Read<std::uint32_t>(lim + kLimitFlags) & 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsPartner(std::uint32_t objId) {
    return objId && (objId == g_peer.partners[0] || objId == g_peer.partners[1]);
}

// While the other player's Limit runs: every ally of ours still here leaves, except the one playing its partner.
// Checked every frame, not only at its start: an ally our own Limit's end builds again in that moment (both Limits
// close together, bench 2026-10-14) leaves too.
void SendAway(bool start) {
    if (OurLimitMoves()) {  // both pressed in the same moment (accepted): our own Limit handles our allies
        if (start)
            Log("limit sync: the other player's Limit started while ours runs (pressed together): our allies left as "
                "they are");
        return;
    }
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!Staying(a)) continue;
        std::uint32_t id = ObjId(a);
        if (IsPartner(id)) {
            if (start) Log("limit sync: our %s stays: it plays the other player's Limit partner", Model(a));
            continue;
        }
        __try {
            reinterpret_cast<PFN_Leave>(ExeBase() + kLeave)(a, 0.0f);
            ++g_sentAway;
            Log("limit sync: our %s leaves for the other player's Limit (the game's own leave)%s", Model(a),
                start ? "" : " (came back while it runs)");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("limit sync: sending our %s away failed (exception)", Model(a));
        }
    }
}

void BringBack(const char* why) {
    if (!g_sentAway) return;
    g_sentAway = 0;
    if (OurLimitMoves()) {
        Log("limit sync: the other player's Limit ended (%s) while ours runs: our Limit's end brings our allies back", why);
        return;
    }
    if (Read<std::uint32_t>(ExeBase() + kSummonState) == 2) {
        Log("limit sync: the other player's Limit ended (%s) while our summon is out: the summon's end brings them back",
            why);
        return;
    }
    __try {
        reinterpret_cast<PFN_BringBack>(ExeBase() + kBringBack)();
        Log("limit sync: the other player's Limit ended (%s): our allies brought back (the game's own call)", why);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("limit sync: bringing our allies back failed (exception)");
    }
}

}  // namespace

void LimitSyncInit(bool /*host*/) {
    g_on = EnvInt("KH2COOP_LIMIT_SYNC", 1) == 1 && CodeMatches(kLeave, kLeaveBytes, sizeof(kLeaveBytes), "Limit leave") &&
           CodeMatches(kBringBack, kBringBackBytes, sizeof(kBringBackBytes), "bring back");
    if (g_on) Log("limit sync: on: a Limit in either game sends the allies away in both, except its partner");
}

void LimitSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        bool ours = OurLimitMoves();
        if (ours && !g_ourWas) {
            Log("limit sync: our Limit started: the other game sends its allies away too");
        }
        if (!ours && g_ourWas)
            Log("limit sync: our Limit's end ran (our allies come back): the other game brings its allies back too");
        if (ours) {
            if (!g_ourWas || g_frame % 6 == 0) Send(true);
            g_endSends = 5;
        } else if (g_endSends > 0 && g_frame % 6 == 0) {
            Send(false);
            --g_endSends;
        }
        g_ourWas = ours;

        std::uint16_t room = Room();
        if (room != g_room) {  // a room load builds the party again by itself
            g_room = room;
            g_sentAway = 0;
            g_peerWas = false;
        }
        for (int i = 0; i < 2; ++i) {
            bool stay = Staying(Slot(i));
            if (stay != g_stayWas[i] && (ours || g_peerWas || PuppetOurLimitRunning()))
                Log("limit sync: party slot %d (%s) %s (our Limit moves %d, the other's %d)", i, Model(Slot(i)),
                    stay ? "is back" : "is gone", ours ? 1 : 0, g_peerWas ? 1 : 0);
            g_stayWas[i] = stay;
        }
        bool peer = g_peer.running && g_peer.room == room && AvatarLinkNowMs() - g_peerMs < kPeerTimeoutMs;
        if (peer) SendAway(!g_peerWas);
        bool ended = !peer && g_peerWas;
        // Cleared before the bring-back: companion_sync.cpp's bring-back filter asks LimitSyncPeerHolds, and with
        // the flag still set it kept the place away (two-PC run 2026-10-22: the friend's Mulan Limit ended, the
        // host's Goofy stayed away and Valor was greyed until the next room; TODO 1.108).
        g_peerWas = peer;
        if (ended) BringBack(g_peer.running ? "no word from the other game" : "its end");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 10) Log("limit sync: exception in frame update");
    }
}

void LimitSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.seq + 1000 < g_peer.seq) g_peer.seq = 0;  // the other game restarted
    if (p.seq <= g_peer.seq) return;
    g_peer = p;
    g_peerMs = AvatarLinkNowMs();
}

bool LimitSyncPeerLimit() { return g_on && g_peerWas; }

bool LimitSyncPeerHolds(std::uint32_t objId) { return g_on && g_peerWas && objId && !IsPartner(objId); }

bool LimitSyncAnyLimit() {
    if (!g_on) return false;
    __try {
        return Read<std::uintptr_t>(ExeBase() + kCurrentLimit) != 0 || g_peerWas;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace kh2coop
