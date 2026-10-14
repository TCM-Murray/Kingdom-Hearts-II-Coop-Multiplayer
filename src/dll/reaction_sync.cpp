// Reaction commands for one player at a time, with one brain (TODO 1.62; you, 2026-10-13: "a global function so
// all those reaction commands only focus one character at a time", bosses going for the nearest player).
//
// Each game has one command menu (YS::FIELD_COMMAND exe+0x2A10620), its own player's. A boss offers a reaction
// command with one of two script calls, both into the menu of the player pointer exe+0x2A105D0, whichever Sora the
// boss holds (VERIFIED_OFFSETS 'Data Roxas's Keyblade-steal reaction command'):
//   trap_command_override_top (1/237, exe+0x4300A0): the four top commands (Data Roxas's 'The End' among three '?',
//     Data Saix, B_EX370), called every frame while it lasts;
//   trap_command_override (1/165, exe+0x430010 -> exe+0x3B2850(menu, slot, command, source)): one slot, e.g. the
//     triangle reaction command (Shan-Yu's Takedown 270 in slot 1, Data Xigbar 647, B_EX370 714), called once.
// With two brains each game's boss did that to its own player, so both players got it at once. With one brain only
// the host's boss runs, and its "player" while its script runs is the nearer Sora (aggro.cpp: the host's Sora, or
// the friend's copy in the host's game). The same rule for every boss and both calls:
//   Host:   such a call while the boss's player is the friend's copy is held back (the host's menu doesn't get it)
//           and sent to the friend ("KH2C" offer: the boss's number, the call and its numbers). Except: a source that
//           hooked a player (grab_sync.cpp) offers to that player, whoever its script sees as the player now (two-PC
//           run 2026-10-14, Armored Xemnas II: the F_EH060 that carried the host's Sora offered the shield break 646
//           while its "player" was the copy; neither player got it and the fight couldn't be won).
//   Friend: the latest offer goes into our menu every frame (the same call, with our copy of the boss) while the
//           host keeps sending it (1/237 is called every frame, the menu doesn't keep it), and for a moment after.
//   Friend: pressing it carries the command out as usual (exe+0x405440 -> exe+0x3A8B10). Its last step,
//           exe+0x3B5FA0(boss, user, command, target) = script event 0xB to the boss's script, is held back for our
//           brain-off boss and sent to the host instead ("KH2C" use).
//   Host:   exe+0x3B5FA0(boss, the friend's copy, command, target) on its boss, so its script goes on with the copy
//           as the one who used it (once: each use has a number).
//   Friend: a field object's command (model F_...) that the host didn't offer us came from our game's own per-enemy
//           list (VERIFIED_OFFSETS 'Some boss reaction commands come from the game's per-enemy list'): the host
//           refused those ('not offered to the friend lately'; two-PC run 2026-10-14, TWTNW: the jumps between the
//           cylinders, F_EH080 612 and F_EH100 643, did nothing for the friend). Such a command only moves a player
//           (DECISIONS Q8: free for both; your pick 2026-10-14: the friend's game plays it), so our object gets its own
//           brain back (world_sync.cpp: no brain skip, no pose from the host) and its script carries it out on our
//           Sora. It goes back to the host's brain kOwnQuietFrames after its last 're_<model>' scene (or the press),
//           after kOwnMaxFrames, or when it is no longer bound.
// REACTION_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "grab_sync.hpp"
#include "puppet.hpp"
#include "reaction_sync.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4332484B;  // "KH2C" ("KH2R" is avatar_link.cpp's heal packet)
constexpr std::uint16_t kVersion = 2;  // v2: the call (1/237 or 1/165)
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;   // the player (the AI swap may point it at the copy)
constexpr std::uintptr_t kListHead = 0x2A171C8;    // our Sora
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;  // (object) -> handle
constexpr std::uintptr_t kTargetInit = 0x3BDA50;   // (target info): empty
constexpr std::uintptr_t kTargetSet = 0x3BEE60;    // (target info, actor, 0)
constexpr std::uintptr_t kReactionEvent = 0x3B5FA0;  // (target, user, command, target info): script event 0xB
constexpr std::uint8_t kReactionEventBytes[] = {0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x60};
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // "@ADR"
constexpr std::uint64_t kTagInt = 0x544E4940ull << 32;   // "@INT"
constexpr std::uint64_t kOfferFrames = 15;   // friend: an offer stays this long after the host's last one
constexpr std::uint64_t kUseFrames = 60;     // host: a use counts if the offer was this recent
constexpr std::uint64_t kUseFramesOnce = 600;  // the same for a one-time offer (1/165)
constexpr std::uint64_t kBusyFrames = 60;
constexpr std::uint64_t kOwnQuietFrames = 300;  // friend: own brain until 5 s after the object's last scene
constexpr std::uint64_t kOwnMaxFrames = 1800;   // at most 30 s

enum Kind : std::uint8_t { kOffer = 1, kUse = 2 };
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;
    std::uint32_t seq;
    std::uint16_t netId;     // the host's number for the boss (world_sync.cpp)
    std::uint16_t instance;  // the host's room instance
    std::uint16_t call;      // offer: 237 (cmds = the four top commands) or 165 (cmds = slot, command, flag)
    std::uint16_t cmds[4];   // offer: as above; use: [0] = the command used
};
#pragma pack(pop)

using PFN_Trap = void(__fastcall*)(std::uint64_t* args);
using PFN_ReactionEvent = void(__fastcall*)(std::uintptr_t target, std::uintptr_t user, std::uint32_t command,
                                            void* info);
using PFN_TargetInit = void(__fastcall*)(void* info);
using PFN_TargetSet = void(__fastcall*)(void* info, std::uintptr_t actor, std::uint32_t zero);
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
PFN_ReactionEvent g_realReactionEvent = nullptr;

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;

struct Offer {
    std::uint16_t netId, instance, call;
    std::uint16_t cmds[4];
    std::uint64_t frame;  // host: when the boss last offered it; friend: when it last came
};
Offer g_offer{};               // host: the latest offer held back for the friend; friend: the latest one received
std::uint64_t g_offerSent = 0;  // host: frame of the last offer packet
std::uintptr_t g_busy[4] = {};
std::uint64_t g_busyFrame[4] = {};
struct Use {
    std::uint16_t netId, instance, cmd;
    bool pending;
};
Use g_use{};                 // host: the friend's latest use, run in our next frame
std::uint32_t g_lastUseSeq = 0;
alignas(16) std::uint32_t g_info[8];  // host: the target info a use passes (its handle is read by the script later)
std::uint32_t g_held = 0, g_offersSent = 0, g_offersApplied = 0, g_usesSent = 0, g_usesRun = 0, g_usesRefused = 0;
std::uint32_t g_logs = 0, g_hookedLogs = 0;
// Friend: the field object running its own brain for our reaction command, or 0.
std::uintptr_t g_own = 0;
std::uint64_t g_ownSince = 0, g_ownLast = 0;
char g_ownScene[24] = {};  // "re_" + its model in lower case: the scenes its command plays
std::uint64_t g_statsFrame = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + kHandleBases + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

// A script's actor argument as exe+0x4300A0 reads it: a handle to an object whose +4 is the actor's handle.
std::uintptr_t ActorArg(std::uint64_t arg) {
    __try {
        std::uintptr_t x = Decode(static_cast<std::uint32_t>(arg));
        return x ? Decode(Read<std::uint32_t>(x + 4)) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = a ? Read<std::uintptr_t>(a + 0x918) : 0;
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// The command an offer gives the player (1/165: its second number).
bool Offers(const Offer& o, std::uint16_t cmd) {
    if (o.call == 165) return cmd && o.cmds[1] == cmd;
    for (std::uint16_t c : o.cmds)
        if (c && c == cmd) return true;
    return false;
}

void Send(Kind kind, std::uint16_t netId, std::uint16_t instance, std::uint16_t call, const std::uint16_t* cmds,
          int copies) {
    Packet p{kMagic, kVersion, AvatarLinkBuild(), kind, ++g_seq, netId, instance, call, {}};
    std::memcpy(p.cmds, cmds, sizeof(p.cmds));
    for (int i = 0; i < copies; ++i) AvatarLinkSendRaw(&p, sizeof(p));
}

void NoteBusy(std::uintptr_t boss) {
    int slot = 0;
    for (int i = 0; i < 4; ++i) {
        if (g_busy[i] == boss) {
            slot = i;
            break;
        }
        if (g_busyFrame[i] < g_busyFrame[slot]) slot = i;
    }
    g_busy[slot] = boss;
    g_busyFrame[slot] = g_frame;
}

// Friend: our brain-off boss would tell its (stopped) script that we used its reaction command: the host's does it.
void __fastcall HookReactionEvent(std::uintptr_t target, std::uintptr_t user, std::uint32_t command, void* info) {
    if (g_on && !g_host && target && user && user == Read<std::uintptr_t>(ExeBase() + kListHead) &&
        WorldSyncSkipsBrain(target)) {
        std::uint16_t instance = 0;
        std::uint16_t netId = WorldSyncBoundTo(target, &instance);
        const char* model = Model(target);
        bool hostOffered = g_offer.netId == netId && g_frame - g_offer.frame <= kUseFramesOnce && Offers(g_offer,
            static_cast<std::uint16_t>(command));
        if (netId && !hostOffered && model[0] == 'F' && model[1] == '_') {
            g_own = target;
            g_ownSince = g_ownLast = g_frame;
            strcpy_s(g_ownScene, "re_");
            for (int i = 0; model[i] && i < 20; ++i)
                g_ownScene[3 + i] = static_cast<char>(std::tolower(static_cast<unsigned char>(model[i]))),
                g_ownScene[4 + i] = 0;
            Log("reaction sync: we used command %u of our %s (bound to host#%u), not offered by the host: our %s runs "
                "its own brain for it (one player at a time doesn't apply to moves)", command, model, netId, model);
            g_realReactionEvent(target, user, command, info);
            return;
        }
        if (netId) {
            std::uint16_t cmds[4] = {static_cast<std::uint16_t>(command), 0, 0, 0};
            Send(kUse, netId, instance, 0, cmds, 3);
            ++g_usesSent;
            Log("reaction sync: we used command %u of our %s (bound to host#%u): sent to the host, whose boss "
                "goes on with it", command, Model(target), netId);
            return;
        }
    }
    g_realReactionEvent(target, user, command, info);
}

void RunUse() {
    g_use.pending = false;
    std::uintptr_t boss = WorldSyncHostActor(g_use.netId, g_use.instance);
    std::uintptr_t copy = PuppetCloneActor();
    // 1/237 comes every frame while it lasts; 1/165 once, and its slot stays a while in the friend's menu.
    bool offered = g_offer.netId == g_use.netId &&
                   g_frame - g_offer.frame <= (g_offer.call == 165 ? kUseFramesOnce : kUseFrames);
    bool listed = Offers(g_offer, g_use.cmd);
    if (!boss || !copy || !offered || !listed) {
        ++g_usesRefused;
        Log("reaction sync: the friend's command %u on host#%u not run: %s", g_use.cmd, g_use.netId,
            !boss ? "no such boss now" : !copy ? "no Sora copy" : !offered ? "not offered to the friend lately"
                                                                           : "not one of the commands offered");
        return;
    }
    __try {
        reinterpret_cast<PFN_TargetInit>(ExeBase() + kTargetInit)(g_info);
        reinterpret_cast<PFN_TargetSet>(ExeBase() + kTargetSet)(g_info, boss, 0);
        g_realReactionEvent(boss, copy, g_use.cmd, g_info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("reaction sync: running the friend's command %u on %s failed (exception)", g_use.cmd, Model(boss));
        return;
    }
    ++g_usesRun;
    g_offer.frame = 0;  // taken: the friend's prompt ends with the next offers the boss makes (or none)
    Log("reaction sync: the friend used command %u of our %s: its script goes on with the Sora copy as the user",
        g_use.cmd, Model(boss));
}

}  // namespace

void ReactionSyncInit(bool host) {
    g_host = host;
    g_on = WorldMirrorOn() && EnvInt("KH2COOP_REACTION_SYNC", 1) == 1;
    if (!g_on) return;
    if (!HookFunction(kReactionEvent, kReactionEventBytes, sizeof(kReactionEventBytes),
                      reinterpret_cast<void*>(HookReactionEvent), reinterpret_cast<void**>(&g_realReactionEvent),
                      "reaction command used -> its source's script (one player at a time)")) {
        g_on = false;
        Log("reaction sync: off (the function's bytes differ on this build)");
        return;
    }
    Log("reaction sync: on (%s)", host ? "a boss's reaction command for the friend's copy goes to the friend"
                                       : "the host's offers go into our menu, our presses go to the host");
}

bool ReactionSyncHolds(int table, int index, const std::uint64_t* args) {
    if (!g_on || table != 1 || (index != 237 && index != 165) || !CoopActive()) return false;
    std::uintptr_t boss = ActorArg(args[index == 237 ? 4 : 2]);
    if (!g_host || !boss) return false;
    NoteBusy(boss);
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return false;
    auto* player = reinterpret_cast<std::uintptr_t*>(ExeBase() + kPlayerPtr);
    int hooked = GrabSyncLastHooked(boss);  // 1 our Sora, 2 the friend's copy, 0 none
    if (*player != copy && hooked != 2) return false;  // ours: as the game does it
    if (hooked == 1) {  // it holds our Sora: ours, in our menu with our Sora as the player
        auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, index));
        if (!fn) return false;
        std::uintptr_t saved = *player;
        *player = Read<std::uintptr_t>(ExeBase() + kListHead);
        __try {
            fn(const_cast<std::uint64_t*>(args));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        *player = saved;
        if (g_hookedLogs++ < 20)
            Log("reaction sync: %s offers command %u while its script sees the friend's copy, but it hooked our Sora: "
                "put into our menu", Model(boss), static_cast<unsigned>(index == 237 ? args[0] : args[1]));
        return true;
    }
    std::uint16_t instance = 0;
    std::uint16_t netId = WorldSyncNetIdOf(boss, &instance);
    if (!netId) return false;  // not tracked by world sync: the host's menu, as before
    std::uint16_t cmds[4] = {};
    if (index == 237)
        for (int i = 0; i < 4; ++i) cmds[i] = static_cast<std::uint16_t>(args[i]);
    else  // slot, command, flag
        cmds[0] = static_cast<std::uint16_t>(args[0]), cmds[1] = static_cast<std::uint16_t>(args[1]),
        cmds[2] = static_cast<std::uint16_t>(args[3]);
    std::uint16_t call = static_cast<std::uint16_t>(index);
    bool changed =
        g_offer.netId != netId || g_offer.call != call || std::memcmp(g_offer.cmds, cmds, sizeof(cmds)) != 0;
    g_offer.netId = netId;
    g_offer.instance = instance;
    g_offer.call = call;
    std::memcpy(g_offer.cmds, cmds, sizeof(cmds));
    g_offer.frame = g_frame;
    ++g_held;
    if (changed || g_frame - g_offerSent >= 6) {
        Send(kOffer, netId, instance, call, cmds, 2);
        g_offerSent = g_frame;
        ++g_offersSent;
    }
    if (changed && g_logs < 40) {
        ++g_logs;
        if (call == 237)
            Log("reaction sync: %s (host#%u) offers commands %u/%u/%u/%u to the friend's copy: sent to the friend, not "
                "in our menu", Model(boss), netId, cmds[0], cmds[1], cmds[2], cmds[3]);
        else
            Log("reaction sync: %s (host#%u) offers command %u in slot %u to the friend's copy: sent to the friend, "
                "not in our menu", Model(boss), netId, cmds[1], cmds[0]);
    }
    return true;
}

bool ReactionSyncOwnBrain(std::uintptr_t actor) { return g_on && !g_host && actor && actor == g_own; }

void ReactionSyncOnScene(const char* name) {
    if (!g_on || g_host || !g_own || !name) return;
    if (std::strncmp(name, g_ownScene, std::strlen(g_ownScene)) == 0) g_ownLast = g_frame;
}

bool ReactionSyncBusy(std::uintptr_t boss) {
    for (int i = 0; i < 4; ++i)
        if (g_busy[i] == boss && g_frame - g_busyFrame[i] <= kBusyFrames) return true;
    return false;
}

void ReactionSyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.kind == kOffer && !g_host) {
        if (p.call != 237 && p.call != 165) return;
        bool changed = g_offer.netId != p.netId || g_offer.call != p.call ||
                       std::memcmp(g_offer.cmds, p.cmds, sizeof(p.cmds)) != 0;
        g_offer.netId = p.netId;
        g_offer.instance = p.instance;
        g_offer.call = p.call;
        std::memcpy(g_offer.cmds, p.cmds, sizeof(p.cmds));
        g_offer.frame = g_frame;
        if (changed && g_logs < 40) {
            ++g_logs;
            if (p.call == 237)
                Log("reaction sync: the host's boss host#%u offers us commands %u/%u/%u/%u", p.netId, p.cmds[0],
                    p.cmds[1], p.cmds[2], p.cmds[3]);
            else
                Log("reaction sync: the host's boss host#%u offers us command %u in slot %u", p.netId, p.cmds[1],
                    p.cmds[0]);
        }
    } else if (p.kind == kUse && g_host && p.seq != g_lastUseSeq) {
        if (g_use.pending && g_use.netId == p.netId && g_use.cmd == p.cmds[0]) return;  // the same use, again
        g_lastUseSeq = p.seq;
        g_use = {p.netId, p.instance, p.cmds[0], true};
    }
}

void ReactionSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_own) {
        const char* why = !WorldSyncBoundTo(g_own, nullptr)          ? "no longer bound"
                          : g_frame - g_ownLast > kOwnQuietFrames   ? "its scenes are over"
                          : g_frame - g_ownSince > kOwnMaxFrames    ? "time limit"
                                                                    : nullptr;
        if (why) {
            Log("reaction sync: our %s goes back to the host's brain (%s, after %llu frames)", Model(g_own), why,
                static_cast<unsigned long long>(g_frame - g_ownSince));
            g_own = 0;
        }
    }
    if (g_host) {
        if (g_use.pending) RunUse();
    } else if (g_offer.netId && g_frame - g_offer.frame <= kOfferFrames) {
        std::uintptr_t boss = WorldSyncBoundActor(g_offer.netId, g_offer.instance);
        auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, g_offer.call));
        if (boss && fn && WorldSyncSkipsBrain(boss)) {
            std::uint64_t args[16] = {};
            std::uint64_t handle = kTagAddr | reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle)(boss);
            if (g_offer.call == 237) {
                for (int i = 0; i < 4; ++i) args[i] = kTagInt | g_offer.cmds[i];
                args[4] = handle;
            } else {  // (slot, command, source, flag)
                args[0] = kTagInt | g_offer.cmds[0];
                args[1] = kTagInt | g_offer.cmds[1];
                args[2] = handle;
                args[3] = kTagInt | g_offer.cmds[2];
            }
            __try {
                fn(args);
                ++g_offersApplied;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_offer.netId = 0;
                Log("reaction sync: putting the host's offer into our menu failed (exception); dropped");
            }
        }
    }
    if (g_frame - g_statsFrame >= 600 && (g_held || g_offersApplied || g_usesSent || g_usesRun || g_usesRefused)) {
        g_statsFrame = g_frame;
        Log("reaction sync: so far %u offers held back for the friend (%u packets), %u frames of offers put into our "
            "menu, uses sent %u, run %u, refused %u", g_held, g_offersSent, g_offersApplied, g_usesSent, g_usesRun,
            g_usesRefused);
    }
}

}  // namespace kh2coop
