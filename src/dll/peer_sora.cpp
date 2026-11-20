// The other player's Sora as a table in each game (TODO 3.16, 3.4, 5.20a; your idea 2026-11-20: one table of what
// the other Sora carries, gear, level and stats, that the rest of the mod reads).
//
// Each game sends its Sora's character record ("KH2p": the 0x114 bytes at game::kSoraRecord, L121: the Keyblade +0, HP and
// MP +4..+7, level +0xF, armor and accessories +0x10.., abilities +0x54) when it changes with no menu open (the menu
// writes it piece by piece), and every 2 s; the other game keeps the newest one as it came. It comes from the other
// PC, so each reader checks what it takes from it. Readers: the cutscene twin's Keyblade (cutscene_twin.cpp).
// KH2COOP_PEER_SORA=0: off (nothing sent, the table stays empty, the readers keep the game's own pick).

#include "peer_sora.hpp"

#include <windows.h>

#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "game.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x7032484B;  // "KH2p"
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kResendMs = 2000;

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint8_t record[kSoraRecordSize];
};
#pragma pack(pop)

int g_on = -1;
Packet g_sent{};  // ours, as last sent
bool g_sentAny = false;
std::uint64_t g_sentAt = 0;
std::uint32_t g_seq = 0;
std::uint8_t g_peer[kSoraRecordSize] = {};  // the other Sora's, as it came
bool g_peerKnown = false;
std::uint32_t g_peerSeq = 0;
int g_changeLogs = 0;

bool On() {
    if (g_on < 0) g_on = EnvInt("KH2COOP_PEER_SORA", 1) == 1 ? 1 : 0;
    return g_on == 1;
}

std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

}  // namespace

void PeerSoraFrame() {
    if (!On()) return;
    std::uint8_t world = 0xFF, menu = 0;
    if (!ReadAny(ExeBase() + game::kNow, &world) || world == 0xFF) return;  // no save loaded
    if (!ReadAny(ExeBase() + game::kOpenMenu, &menu)) return;
    // The record is in the exe image's data, always mapped.
    const auto* ours = reinterpret_cast<const std::uint8_t*>(ExeBase() + game::kSoraRecord);
    std::uint64_t now = GetTickCount64();
    if (std::memcmp(ours, g_sent.record, kSoraRecordSize) != 0 && menu == 0xFF) {
        std::memcpy(g_sent.record, ours, kSoraRecordSize);
        g_sentAny = true;
    } else if (!g_sentAny || now - g_sentAt < kResendMs) {
        return;
    }
    g_sent.magic = kMagic;
    g_sent.version = kVersion;
    g_sent.build = AvatarLinkBuild();
    g_sent.seq = ++g_seq;
    g_sentAt = now;
    AvatarLinkSendRaw(&g_sent, static_cast<int>(sizeof(g_sent)));
}

void PeerSoraOnPacket(const char* buf, int n) {
    if (!On() || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    // Older than the newest one (the network reordered them), unless far older: the other game restarted, counting
    // from 1 again, and its record replaces the old one at once.
    if (p.seq <= g_peerSeq && g_peerSeq - p.seq < 8) return;
    g_peerSeq = p.seq;
    if (g_peerKnown && std::memcmp(p.record, g_peer, kSoraRecordSize) == 0) return;
    if (g_changeLogs++ < 40)
        Log("peer sora: the other Sora's record %s: level %u, Keyblade %#x, HP %u/%u, MP %u/%u, %u armor, %u accessories",
            g_peerKnown ? "changed" : "came", p.record[0xF], U16(p.record), p.record[4], p.record[5], p.record[6],
            p.record[7], p.record[0x10], p.record[0x11]);
    std::memcpy(g_peer, p.record, kSoraRecordSize);
    g_peerKnown = true;
}

const std::uint8_t* PeerSoraRecord() { return On() && g_peerKnown ? g_peer : nullptr; }

std::uint16_t PeerSoraKeyblade() {
    const std::uint8_t* r = PeerSoraRecord();
    return r ? U16(r) : 0;
}

}  // namespace kh2coop
