// Party HUD widget for the Sora copy (TODO 3.11).
//
// The HUD's party widgets (dk::GAUGE_FRIEND, two of them) show the actors in a
// three-entry list: {player, friend gauge 0, friend gauge 1}. AI party members
// add themselves when they're set up (exe+0x3C27F0, exe+0x3C35C0); the copy is
// a player-class actor and never does, so its widget stays hidden. Only HUD
// code reads the list (Ghidra 2026-10-09), so we add the copy to the empty
// entry ourselves while it's alive and take it out again when it goes.
//
// Its HP bar would read the copy's stat block (actor+0x5C0), which is our own
// Sora's. With COPY_HUD_HP the widget's per-frame update runs with the copy's
// stat pointer swapped to a stand-in block holding the peer's HP and max HP
// (from their packets) and full MP; game code outside the widget never sees it.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "avatar_link.hpp"
#include "common.hpp"
#include "party_hud.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

// Leads (VERIFIED_OFFSETS.md, Ghidra 2026-10-09).
constexpr std::uintptr_t kHudParty = 0xABCFE8;  // u64[3] actors: player, friend gauge 0, friend gauge 1
constexpr std::uintptr_t kHudFlags = 0xABD000;  // u32[3]; bit 0 = entry changed (the widget re-reads it)
// Bit 4 = HP changed: ApplyStatDelta raises it (exe+0x168870 from exe+0x3D2F55); the widget then re-reads
// the HP and switches its face to the low-HP look or back (vtable +0x48, exe+0x182360). The copy's HP
// never changes in our game, so we raise it when the peer's HP does.
constexpr std::uint32_t kFlagHpChanged = 0x10;
constexpr std::uintptr_t kHudSet = 0x168850;    // (i, actor): entry + changed bit
constexpr std::uint8_t kHudSetBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x8E, 0x47, 0x95, 0x00};
constexpr std::uintptr_t kHudClear = 0x168970;  // (i): entry 0 + changed bit
constexpr std::uint8_t kHudClearBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x6E, 0x46, 0x95, 0x00};
// dk::GAUGE_FRIEND vtable +8: per-frame update. It first handles the entry's event bits (refresh,
// damage...), then fills the HP/MP bars from the shown actor (gauge+0x40) -> +0x5C0 stat block.
constexpr std::uintptr_t kGaugeUpdate = 0x182730;
constexpr std::uint8_t kGaugeUpdateBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kGaugeActor = 0x40;
constexpr std::uintptr_t kActorStats = 0x5C0;   // -> stat block: u32 HP, u32 max HP, ...
constexpr std::uint32_t kStatBlock = 0x278;     // party stat slot stride
constexpr std::uint32_t kStatMp = 0x180, kStatMaxMp = 0x184;
constexpr int kSettleFrames = 30;  // the copy must be alive this long before we list it
// Face picture lookup (actor, &image, &layout): the actor's file (+0x928) entries named "face", type 0x18
// image and 0x19 2D layout (SEQD, used in place: offsets from its start). Called by the friend widget
// (exe+0x17E980, call at exe+0x17E9A7), the player gauge and party member setup. Sora's layout draws a
// 70x95 quad for the player gauge (Goofy's: 47x47), so the copy's friend widget gets a scaled copy.
constexpr std::uintptr_t kFaceLookup = 0x3B57B0;
constexpr std::uint8_t kFaceLookupBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::uintptr_t kFaceLookupFromFriendWidget = 0x17E9AC;  // return address
constexpr std::uint32_t kSeqdMagic = 0x44514553;                  // "SEQD"
// SEQD header (u32): +0x10 part count, +0x14 part offset (parts: s32 left, top, right, bottom, u32
// sprite), +0x20 animation count, +0x24 offset (0x90 each), +0x28 animation group count, +0x2C offset
// (0x24 each, last section: 4 groups end exactly at the file entry size 0x4EC for Sora and Goofy).
constexpr std::uint32_t kSeqdPart = 0x14, kSeqdAnimGroup = 0x24;

using PFN_HudSet = void(__fastcall*)(int index, std::uintptr_t actor);
using PFN_HudClear = void(__fastcall*)(int index);
using PFN_GaugeUpdate = void(__fastcall*)(void* gauge);
using PFN_FaceLookup = std::uint8_t(__fastcall*)(std::uintptr_t actor, void** image, void** layout);

bool g_on = false, g_hpOn = false;
PFN_HudSet g_hudSet = nullptr;
PFN_HudClear g_hudClear = nullptr;
PFN_GaugeUpdate g_realGaugeUpdate = nullptr;
std::uintptr_t g_listed = 0;  // the copy we put in the list (0 = none)
int g_alive = 0;              // frames the current copy has been alive
std::uintptr_t g_lastSeen[3] = {};
bool g_noRoomLogged = false;
int g_replacedLogs = 0;
alignas(16) std::uint8_t g_stand[kStatBlock];  // stand-in stat block for the copy's widget
std::uint32_t g_hpFrames = 0;
std::int32_t g_peerHp = -1;
PFN_FaceLookup g_realFaceLookup = nullptr;
int g_faceScale = 50;                              // percent of Sora's own face size
alignas(16) std::uint8_t g_smallFace[0x1000];      // the copy's scaled face layout (the widget keeps using it)
std::uint32_t g_faceSwaps = 0;  // the peer HP the copy's widget last showed

volatile std::uintptr_t* Party() { return reinterpret_cast<volatile std::uintptr_t*>(ExeBase() + kHudParty); }

const char* Who(std::uintptr_t a, std::uintptr_t copy) {
    if (!a) return "empty";
    if (a == copy || a == g_listed) return "Sora copy";
    if (a == *reinterpret_cast<const volatile std::uintptr_t*>(ExeBase() + 0x2A171C8)) return "our Sora";
    return "party member";
}

void LogListChanges(std::uintptr_t copy) {
    volatile std::uintptr_t* party = Party();
    bool changed = false;
    for (int i = 0; i < 3; ++i) changed |= party[i] != g_lastSeen[i];
    if (!changed) return;
    const volatile std::uint32_t* flags = reinterpret_cast<const volatile std::uint32_t*>(ExeBase() + kHudFlags);
    std::uintptr_t base = ExeBase();
    auto rva = [base](std::uintptr_t a) { return static_cast<unsigned long long>(a ? a - base : 0); };
    Log("hud: party list = [0] %s exe+0x%llX, [1] %s exe+0x%llX, [2] %s exe+0x%llX (flags 0x%X 0x%X 0x%X)",
        Who(party[0], copy), rva(party[0]), Who(party[1], copy), rva(party[1]), Who(party[2], copy), rva(party[2]),
        flags[0], flags[1], flags[2]);
    for (int i = 0; i < 3; ++i) g_lastSeen[i] = party[i];
}

void __fastcall HookGaugeUpdate(void* gauge) {
    std::uintptr_t actor = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(gauge) + kGaugeActor);
    PeerPose peer;
    if (!g_hpOn || !actor || actor != g_listed || !AvatarLinkPeerNow(peer) || peer.maxHp <= 0) {
        g_realGaugeUpdate(gauge);
        return;
    }
    auto* slot = reinterpret_cast<std::uintptr_t*>(actor + kActorStats);
    std::uintptr_t own = *slot;
    if (!own) {
        g_realGaugeUpdate(gauge);
        return;
    }
    std::memcpy(g_stand, reinterpret_cast<const void*>(own), sizeof(g_stand));
    std::int32_t hp = peer.hp < 0 ? 0 : (peer.hp > peer.maxHp ? peer.maxHp : peer.hp);
    std::memcpy(g_stand + 0, &hp, 4);
    std::memcpy(g_stand + 4, &peer.maxHp, 4);
    std::memcpy(g_stand + kStatMp, g_stand + kStatMaxMp, 4);  // MP: shown full for now (user, 2026-10-09)
    if (g_hpFrames++ == 0) Log("hud: the copy's widget shows the peer's HP (%d/%d)", hp, peer.maxHp);
    *slot = reinterpret_cast<std::uintptr_t>(g_stand);
    __try {
        g_realGaugeUpdate(gauge);
    } __finally {
        *slot = own;
    }
}

// Copies a SEQD layout into g_smallFace with every part's quad scaled by g_faceScale (around the
// layout's origin, which the widget places in its ring). Null if it doesn't look like one.
void* ScaledFace(const std::uint8_t* src) {
    std::uint32_t h[12];
    std::memcpy(h, src, sizeof(h));
    if (h[0] != kSeqdMagic) return nullptr;
    std::uint64_t size = static_cast<std::uint64_t>(h[11]) + static_cast<std::uint64_t>(h[10]) * kSeqdAnimGroup;
    std::uint64_t partsEnd = static_cast<std::uint64_t>(h[5]) + static_cast<std::uint64_t>(h[4]) * kSeqdPart;
    if (size > sizeof(g_smallFace) || partsEnd > size) return nullptr;
    std::memcpy(g_smallFace, src, static_cast<std::size_t>(size));
    for (std::uint32_t i = 0; i < h[4]; ++i) {
        auto* quad = reinterpret_cast<std::int32_t*>(g_smallFace + h[5] + i * kSeqdPart);
        for (int k = 0; k < 4; ++k) quad[k] = quad[k] * g_faceScale / 100;
    }
    return g_smallFace;
}

std::uint8_t __fastcall HookFaceLookup(std::uintptr_t actor, void** image, void** layout) {
    std::uint8_t found = g_realFaceLookup(actor, image, layout);
    auto from = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase();
    if (found && actor && actor == g_listed && from == kFaceLookupFromFriendWidget && *layout) {
        __try {
            if (void* small = ScaledFace(static_cast<const std::uint8_t*>(*layout))) {
                *layout = small;
                if (g_faceSwaps++ < 5) Log("hud: the copy's widget face drawn at %d%% size", g_faceScale);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return found;
}

}  // namespace

void PartyHudInit() {
    if (EnvInt("KH2COOP_COPY_HUD", 1) != 1) return;
    std::uintptr_t exe = ExeBase();
    if (std::memcmp(reinterpret_cast<void*>(exe + kHudSet), kHudSetBytes, sizeof(kHudSetBytes)) != 0 ||
        std::memcmp(reinterpret_cast<void*>(exe + kHudClear), kHudClearBytes, sizeof(kHudClearBytes)) != 0) {
        Log("hud: party list functions don't match this build; the copy gets no widget");
        return;
    }
    g_hudSet = reinterpret_cast<PFN_HudSet>(exe + kHudSet);
    g_hudClear = reinterpret_cast<PFN_HudClear>(exe + kHudClear);
    g_on = true;
    if (EnvInt("KH2COOP_COPY_HUD_HP", 1) == 1)
        g_hpOn = HookFunction(kGaugeUpdate, kGaugeUpdateBytes, sizeof(kGaugeUpdateBytes),
                              reinterpret_cast<void*>(HookGaugeUpdate), reinterpret_cast<void**>(&g_realGaugeUpdate),
                              "party widget update (copy shows the peer's HP)");
    g_faceScale = EnvInt("KH2COOP_COPY_HUD_FACE_SCALE", 50);
    if (g_faceScale > 0 && g_faceScale < 100)
        HookFunction(kFaceLookup, kFaceLookupBytes, sizeof(kFaceLookupBytes), reinterpret_cast<void*>(HookFaceLookup),
                     reinterpret_cast<void**>(&g_realFaceLookup), "face picture lookup (smaller face for the copy)");
    Log("hud: the Sora copy gets a party widget%s", g_hpOn ? " with the peer's HP" : " (our own HP: COPY_HUD_HP off)");
}

void PartyHudFrame() {
    if (!g_on) return;
    __try {
        std::uintptr_t copy = PuppetCloneActor();  // 0 while loading, after a room change, or with no copy
        volatile std::uintptr_t* party = Party();
        if (g_listed && copy != g_listed) {
            for (int i = 1; i < 3; ++i) {
                if (party[i] == g_listed) {
                    g_hudClear(i);
                    Log("hud: Sora copy taken out of the party list (entry %d)", i);
                }
            }
            g_listed = 0;
            g_hpFrames = 0;
            g_peerHp = -1;
        }
        if (!copy) {
            g_alive = 0;
            LogListChanges(copy);
            return;
        }
        if (party[1] == copy || party[2] == copy) {
            g_listed = copy;
            PeerPose peer;
            if (g_hpOn && AvatarLinkPeerNow(peer) && peer.hp != g_peerHp) {
                g_peerHp = peer.hp;
                reinterpret_cast<volatile std::uint32_t*>(ExeBase() + kHudFlags)[party[1] == copy ? 1 : 2] |= kFlagHpChanged;
            }
            LogListChanges(copy);
            return;
        }
        if (g_listed == copy && g_replacedLogs++ < 5)
            Log("hud: the game replaced the Sora copy in the party list; adding it again");
        g_listed = 0;
        if (++g_alive >= kSettleFrames) {
            int entry = party[2] == 0 ? 2 : (party[1] == 0 ? 1 : -1);
            if (entry < 0) {
                if (!g_noRoomLogged) Log("hud: no empty party list entry for the Sora copy");
                g_noRoomLogged = true;
            } else {
                g_hudSet(entry, copy);
                g_listed = copy;
                g_noRoomLogged = false;
                Log("hud: Sora copy (actor exe+0x%llX) added to the party list, entry %d",
                    static_cast<unsigned long long>(copy - ExeBase()), entry);
            }
        }
        LogListChanges(copy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace kh2coop
