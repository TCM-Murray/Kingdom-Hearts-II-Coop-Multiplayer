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
//   Friend: an enemy's command from our game's own per-enemy list (the Blizzard Lord's 506 against his ice breath,
//           Data Xaldin's 649, Data Luxord's 458; each game offers those to its own Sora) goes to the host marked as
//           such (your pick 2026-10-15: 'the friend's offer counts'; before, the host refused it, 'not offered to the
//           friend lately', and the friend's press did nothing).
//   Host:   runs such a use like an offered one. Both players can get these commands: the first press wins, so a
//           press of the same command on the same boss within kUseFrames of the other player's is dropped.
//   Host:   an attack can carry its own command (trap_attack_set_rc, 2/51; VERIFIED_OFFSETS lead 'Attacks can carry a
//           reaction command'): the game offers it to its player from its list of attacks (exe+0x407B40), with the
//           attack as the target info's second handle, which the boss's script then reads (trap_attack_rc_receiver,
//           2/52). Data Xigbar's sniper round gets its third command (598, a chain after 491) that way, and the
//           friend's game has no such attack (TODO 1.85). So every frame the host runs the same checks for the copy
//           (OfferAttackCommands) and offers what passes to the friend as a slot 1 offer (1/165's form), and a
//           friend's press runs with our matching attack in the second handle (TODO 1.83: without it his script read
//           nothing and the game faulted at exe+0x432810).
// REACTION_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "grab_sync.hpp"
#include "puppet.hpp"
#include "reaction_sync.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4332484B;  // "KH2C" ("KH2R" is avatar_link.cpp's heal packet)
constexpr std::uint16_t kVersion = 3;  // v3: a use from the friend's own list (cmds[1]); v2: the call (1/237 or 1/165)
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;   // the player (the AI swap may point it at the copy)
constexpr std::uintptr_t kListHead = 0x2A171C8;    // our Sora
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;  // (object) -> handle
constexpr std::uintptr_t kTargetInit = 0x3BDA50;   // (target info): empty
constexpr std::uintptr_t kTargetSet = 0x3BEE60;    // (target info, actor, 0)
constexpr std::uintptr_t kReactionEvent = 0x3B5FA0;  // (target, user, command, target info): script event 0xB
// The attack list and the checks exe+0x407B40 makes for each attack with a command (see the top of this file).
constexpr std::uintptr_t kAttackList = 0x2A24B40;  // the first attack; the next one's handle at +0xC4 (exe+0x3CFE90)
constexpr std::uintptr_t kCmdEntry = 0x3B68B0;     // (command) -> its table entry: +0xC flags, +0x22 chain command
constexpr std::uintptr_t kCmdUsable = 0x3B6A10;    // (entry, player): the player can use it now
constexpr std::uintptr_t kCollision = 0x3B5650;    // (actor, 1, -1): its first body shape of kind 1
constexpr std::uintptr_t kShapeOf = 0x3C9C00;      // (shape, actor, body shape): the actor's reach shape
constexpr std::uintptr_t kShapesMeet = 0x3C94C0;   // (shape, shape): they overlap
constexpr std::uintptr_t kRcMode = 0x2A11404;      // u32: non-zero = only commands with entry flag 0x10 (exe+0x3ABB20)
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
    std::uint16_t cmds[4];   // offer: as above; use: [0] = the command used, [1] = 1 from the friend's own list
};
#pragma pack(pop)

using PFN_Trap = void(__fastcall*)(std::uint64_t* args);
using PFN_ReactionEvent = void(__fastcall*)(std::uintptr_t target, std::uintptr_t user, std::uint32_t command,
                                            void* info);
using PFN_TargetInit = void(__fastcall*)(void* info);
using PFN_TargetSet = void(__fastcall*)(void* info, std::uintptr_t actor, std::uint32_t zero);
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
using PFN_CmdEntry = std::uintptr_t(__fastcall*)(int command);
using PFN_CmdUsable = std::uint8_t(__fastcall*)(std::uintptr_t entry, std::uintptr_t player);
using PFN_Collision = std::uintptr_t(__fastcall*)(std::uintptr_t actor, int kind, int which);
using PFN_ShapeOf = void(__fastcall*)(void* shape, std::uintptr_t actor, std::uintptr_t body);
using PFN_ShapesMeet = std::uint8_t(__fastcall*)(const void* a, const void* b);
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
    bool ownList;  // the friend's game offered it from its own per-enemy list, not the host
    std::uint16_t part;  // the boss's part in the friend's target info (+1; 0 = not sent)
};
Use g_use{};                 // host: the friend's latest use, run in our next frame
struct Used {  // host: the last use of a command on a boss by each player, for 'the first press wins'
    std::uintptr_t boss;
    std::uint32_t cmd;
    std::uint64_t frame;
};
Used g_hostUsed{}, g_friendUsed{};
std::uint32_t g_ownListRun = 0, g_firstPressDrops = 0;
std::uint32_t g_lastUseSeq = 0;
alignas(16) std::uint32_t g_info[8];  // host: the target info a use passes (its handle is read by the script later)
struct AttackOffer {  // host: the attack command the copy can take now (OfferAttackCommands)
    std::uint16_t netId, instance, cmd;
    std::uint32_t attack;  // its handle
    std::uint64_t frame;   // when it last passed
};
AttackOffer g_atk{};
std::uint64_t g_atkSent = 0;
std::uint32_t g_atkOffers = 0, g_atkLogs = 0, g_atkSecond = 0, g_atkFaults = 0;
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

// A reaction command's target info for the log (TODO 1.83 / 1.85): {actor handle, part, handle, -1, ?}, each handle
// with the model it names now.
void InfoText(const void* info, char* buf, int len) {
    buf[0] = 0;
    if (!info) return;
    __try {
        const std::uint32_t* v = reinterpret_cast<const std::uint32_t*>(info);
        std::uintptr_t a = Decode(v[0]), b = Decode(v[2]);
        std::snprintf(buf, len, "actor %08X (%s), part %u, second %08X (%s), then %08X %08X", v[0], a ? Model(a) : "-",
                      v[1], v[2], b ? Model(b) : "-", v[3], v[4]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(buf, len, "unreadable");
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
            // The target info (exe+0x3BEE60: {handle, part, handle, -1}) names the boss's part that offered it. A
            // command the per-enemy list took from an object the boss carries (part command 0x83 -> boss+0x9F0, e.g.
            // Data Xigbar's 492 against his bouncing shot) needs that part in the host's run too (TODO 1.83: with
            // part 0 his script's handle named nothing and the host's game faulted at exe+0x432810).
            std::uint32_t part = 0;
            __try {
                if (info) part = reinterpret_cast<const std::uint32_t*>(info)[1];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            std::uint16_t cmds[4] = {static_cast<std::uint16_t>(command), static_cast<std::uint16_t>(hostOffered ? 0 : 1),
                                     static_cast<std::uint16_t>(part < 0xFFFF ? part + 1 : 0), 0};
            char text[200];
            InfoText(info, text, sizeof(text));
            Log("reaction sync: our target info for command %u: %s", command, text);
            Send(kUse, netId, instance, 0, cmds, 3);
            ++g_usesSent;
            Log("reaction sync: we used command %u of our %s (bound to host#%u)%s: sent to the host, whose boss "
                "goes on with it", command, Model(target), netId, hostOffered ? "" : ", from our game's own list");
            return;
        }
    }
    if (g_on && g_host && target && user && user == Read<std::uintptr_t>(ExeBase() + kListHead)) {
        // Our player's press: dropped if the friend used the same command on this boss just before (first press wins).
        if (g_friendUsed.boss == target && g_friendUsed.cmd == command && g_frame - g_friendUsed.frame <= kUseFrames) {
            ++g_firstPressDrops;
            Log("reaction sync: our command %u on %s dropped: the friend used it first", command, Model(target));
            return;
        }
        g_hostUsed = {target, command, g_frame};
        char text[200];
        InfoText(info, text, sizeof(text));
        Log("reaction sync: our own command %u on %s: target info %s", command, Model(target), text);
    }
    g_realReactionEvent(target, user, command, info);
}

// The next attack in the game's list after `a` (0: the first), as exe+0x3CFE90 walks it: by the handle at +0xC4, only
// those with +8 bit 8.
std::uintptr_t NextAttack(std::uintptr_t a) {
    for (int n = 0; n < 1024; ++n) {
        a = a ? Decode(Read<std::uint32_t>(a + 0xC4)) : Read<std::uintptr_t>(ExeBase() + kAttackList);
        if (!a || ((Read<std::uint32_t>(a + 8) >> 8) & 1)) return a;
    }
    return 0;
}

// Why the game would not offer this attack's command to `player` (exe+0x407B40's checks, with `me` the player's reach
// shape): 0 = it would.
enum AttackCheck { kAtkOffered = 0, kAtkNoEntry, kAtkMode, kAtkChain, kAtkUnusable, kAtkReach };
const char* const kAtkCheckNames[] = {"offered", "no table entry", "not in this mode", "chain window closed",
                                      "not usable now", "out of reach"};
int CheckAttack(std::uintptr_t attack, std::uint32_t cmd, std::uintptr_t player, const void* me) {
    std::uintptr_t entry = reinterpret_cast<PFN_CmdEntry>(ExeBase() + kCmdEntry)(static_cast<int>(cmd));
    if (!entry) return kAtkNoEntry;
    std::uint32_t flags = Read<std::uint32_t>(entry + 0xC);
    if (Read<std::uint32_t>(ExeBase() + kRcMode) && !(flags & 0x10)) return kAtkMode;
    if ((flags & 0x40) && (!((Read<std::uint32_t>(player + 0x18C) >> 21) & 1) ||
                           Read<std::uint32_t>(player + 0x638) != Read<std::uint16_t>(entry + 0x22)))
        return kAtkChain;  // a chain: the player's window must be open for the command it follows
    if (!reinterpret_cast<PFN_CmdUsable>(ExeBase() + kCmdUsable)(entry, player)) return kAtkUnusable;
    // The attack's shape as exe+0x3C9B60 makes it: {kind 0, radius, height, -, no body, place}, the place being
    // the attack's +0xE4 plus its offset +0x128 (trap_attack_set_reaction_offset).
    alignas(16) std::uint32_t shape[12] = {};
    shape[1] = Read<std::uint32_t>(attack + 0x120);
    shape[2] = Read<std::uint32_t>(attack + 0x124);
    float* at = reinterpret_cast<float*>(&shape[6]);
    for (int i = 0; i < 4; ++i) at[i] = Read<float>(attack + 0xE4 + 4 * i) + Read<float>(attack + 0x128 + 4 * i);
    return reinterpret_cast<PFN_ShapesMeet>(ExeBase() + kShapesMeet)(shape, me) ? kAtkOffered : kAtkReach;
}

// Host, every frame: the first attack of a boss world sync tracks whose command the game would offer the copy now goes
// to the friend as a slot 1 offer, every 6 frames while it lasts (see the top of this file). The first attack with a
// command is logged whenever its check result changes, to see why one isn't offered.
std::uint32_t g_atkWatch = 0;
int g_atkWatchCheck = -1;
void OfferAttackCommands() {
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return;
    __try {
        std::uintptr_t body = reinterpret_cast<PFN_Collision>(ExeBase() + kCollision)(copy, 1, -1);
        if (!body) return;
        alignas(16) std::uint32_t me[16] = {};
        reinterpret_cast<PFN_ShapeOf>(ExeBase() + kShapeOf)(me, copy, body);
        auto encode = reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle);
        bool watched = false;
        for (std::uintptr_t a = NextAttack(0); a; a = NextAttack(a)) {
            std::uint32_t cmd = Read<std::uint32_t>(a + 0x11C);
            if (!cmd || cmd > 0xFFFF) continue;
            std::uintptr_t owner = Decode(Read<std::uint32_t>(a + 0x118));
            std::uint16_t instance = 0;
            std::uint16_t netId = owner ? WorldSyncNetIdOf(owner, &instance) : 0;
            if (!netId) continue;
            int check = CheckAttack(a, cmd, copy, me);
            std::uint32_t handle = encode(a);
            if (!watched) {
                watched = true;
                if ((handle != g_atkWatch || check != g_atkWatchCheck) && g_atkLogs < 80) {
                    ++g_atkLogs;
                    Log("reaction sync: %s's attack %08X carries command %u: for the friend's copy %s (copy chain %u, "
                        "window %u)", Model(owner), handle, cmd, kAtkCheckNames[check],
                        Read<std::uint32_t>(copy + 0x638), (Read<std::uint32_t>(copy + 0x18C) >> 21) & 1);
                }
                g_atkWatch = handle;
                g_atkWatchCheck = check;
            }
            if (check != kAtkOffered) continue;
            bool changed = g_atk.netId != netId || g_atk.cmd != cmd || g_atk.attack != handle ||
                           g_frame - g_atk.frame > kOfferFrames;
            g_atk = {netId, instance, static_cast<std::uint16_t>(cmd), handle, g_frame};
            if (changed || g_frame - g_atkSent >= 6) {
                std::uint16_t cmds[4] = {1, static_cast<std::uint16_t>(cmd), 1, 0};  // slot 1, command, flag 1
                Send(kOffer, netId, instance, 165, cmds, 2);
                g_atkSent = g_frame;
                ++g_atkOffers;
            }
            if (changed && g_atkLogs < 80) {
                ++g_atkLogs;
                Log("reaction sync: %s's attack %08X offers command %u to the friend's copy: sent to the friend (as a "
                    "slot 1 offer)", Model(owner), handle, cmd);
            }
            return;  // one at a time: slot 1 takes one command
        }
        if (!watched) g_atkWatch = 0, g_atkWatchCheck = -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_atkFaults++ == 0) Log("reaction sync: checking the attacks' commands for the copy failed (exception)");
    }
}

// Host: our attack for the friend's press of `cmd` on `boss`: the one we offered, else the nearest of the boss's
// attacks carrying that command to the copy; its handle, or 0.
std::uint32_t AttackFor(std::uintptr_t boss, std::uint32_t cmd, std::uintptr_t copy) {
    __try {
        std::uintptr_t a = Decode(g_atk.attack);
        if (a && g_atk.cmd == cmd && Read<std::uint32_t>(a + 0x11C) == cmd &&
            Decode(Read<std::uint32_t>(a + 0x118)) == boss)
            return g_atk.attack;
        const float* me = reinterpret_cast<const float*>(copy + 0x40) + 12;  // where the copy is drawn
        std::uintptr_t best = 0;
        float bestGap = 0.0f;
        for (a = NextAttack(0); a; a = NextAttack(a)) {
            if (Read<std::uint32_t>(a + 0x11C) != cmd || Decode(Read<std::uint32_t>(a + 0x118)) != boss) continue;
            float gap = 0.0f;
            for (int i = 0; i < 3; ++i) {
                float d = Read<float>(a + 0xE4 + 4 * i) - me[i];
                gap += d * d;
            }
            if (!best || gap < bestGap) best = a, bestGap = gap;
        }
        return best ? reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle)(best) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void RunUse() {
    g_use.pending = false;
    std::uintptr_t boss = WorldSyncHostActor(g_use.netId, g_use.instance);
    std::uintptr_t copy = PuppetCloneActor();
    // 1/237 comes every frame while it lasts; 1/165 once, and its slot stays a while in the friend's menu.
    // A command from the friend's own list was offered by its game, not ours (see the top of this file).
    // An attack's command we offered (OfferAttackCommands) counts like the boss's own 1/165 offers (the friend's game
    // marks a press as ours that long after our offer).
    bool atk = g_atk.netId == g_use.netId && g_atk.cmd == g_use.cmd && g_frame - g_atk.frame <= kUseFramesOnce;
    bool offered = g_use.ownList || atk || (g_offer.netId == g_use.netId &&
                                     g_frame - g_offer.frame <= (g_offer.call == 165 ? kUseFramesOnce : kUseFrames));
    bool listed = g_use.ownList || atk || Offers(g_offer, g_use.cmd);
    bool ourFirst = boss && g_hostUsed.boss == boss && g_hostUsed.cmd == g_use.cmd && g_frame - g_hostUsed.frame <= kUseFrames;
    if (!boss || !copy || !offered || !listed || ourFirst) {
        if (ourFirst) ++g_firstPressDrops;
        ++g_usesRefused;
        Log("reaction sync: the friend's command %u on host#%u not run: %s", g_use.cmd, g_use.netId,
            !boss ? "no such boss now" : !copy ? "no Sora copy" : !offered ? "not offered to the friend lately"
            : !listed ? "not one of the commands offered" : "our player used it first");
        return;
    }
    std::uint32_t attack = AttackFor(boss, g_use.cmd, copy);
    __try {
        reinterpret_cast<PFN_TargetInit>(ExeBase() + kTargetInit)(g_info);
        reinterpret_cast<PFN_TargetSet>(ExeBase() + kTargetSet)(g_info, boss, g_use.part ? g_use.part - 1 : 0);
        if (attack) {  // part 0 and the attack, as the game's own offer of it (exe+0x3BEE00)
            g_info[1] = 0;
            g_info[2] = attack;
            ++g_atkSecond;
        }
        g_realReactionEvent(boss, copy, g_use.cmd, g_info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("reaction sync: running the friend's command %u on %s failed (exception)", g_use.cmd, Model(boss));
        return;
    }
    ++g_usesRun;
    if (g_use.ownList) ++g_ownListRun;
    g_friendUsed = {boss, g_use.cmd, g_frame};
    if (!g_use.ownList) g_offer.frame = 0;  // taken: the friend's prompt ends with the next offers the boss makes
    char with[48] = "";
    if (attack) std::snprintf(with, sizeof(with), " and our attack %08X", attack);
    Log("reaction sync: the friend used command %u of our %s%s, part %d%s: its script goes on with the Sora copy as the "
        "user", g_use.cmd, Model(boss), g_use.ownList ? " (from its game's own list)" : atk ? " (an attack's, we offered)"
        : "", attack ? 0 : g_use.part ? g_use.part - 1 : 0, with);
}

}  // namespace

void ReactionSyncInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_REACTION_SYNC", 1) == 1;
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
        g_use = {p.netId, p.instance, p.cmds[0], true, p.cmds[1] == 1, p.cmds[2]};
    }
}

// Research trace (TODO 1.85, Data Xigbar's second 598 never offered in the friend's game): our Sora's reaction chain
// (+0x638 = the command a chained one needs, +0x18C bit 21 its window, bit 17 an offer from a motion trigger,
// exe+0x40D810 types 0x14 / 0x1A) and the command in its menu's slot 1 override record (menu = Sora+0xDD0, slot 1 at
// +0x9D4, command at +2; exe+0x3B2850, what 1/165 and a trigger 0x14 fill; the per-frame offers of exe+0x4081C0 are
// kept elsewhere, so it reads 0 for them), logged when they change, with the motion (and each motion change during a
// grab, +0x18C bit 14). KH2COOP_RC_TRACE=0 turns it off.
int g_rcTrace = -1;
std::uint32_t g_rcTraceLines = 0, g_rcTraceFaults = 0;
struct RcState {
    std::uint32_t chain, flags, motion;
    std::uint16_t slot;
} g_rcLast{};

void RcTrace() {
    if (g_rcTrace < 0) {
        g_rcTrace = EnvInt("KH2COOP_RC_TRACE", 1);
        Log("rc trace: %s (our Sora's reaction chain and slot 1, TODO 1.85)", g_rcTrace ? "on" : "off");
    }
    if (!g_rcTrace || g_rcTraceLines >= 600) return;
    __try {
        std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
        if (!sora) return;
        std::uintptr_t menu = Read<std::uintptr_t>(sora + 0xDD0);
        std::uint32_t flags = Read<std::uint32_t>(sora + 0x18C);
        RcState now{Read<std::uint32_t>(sora + 0x638), flags & ((1u << 21) | (1u << 17) | (1u << 14)),
                    Read<std::uint32_t>(sora + 0x180), menu ? Read<std::uint16_t>(menu + 0x9D4 + 2) : std::uint16_t{0}};
        bool grab = (flags >> 14) & 1;
        if (now.chain == g_rcLast.chain && now.flags == g_rcLast.flags && now.slot == g_rcLast.slot &&
            (!grab || now.motion == g_rcLast.motion)) {
            g_rcLast.motion = now.motion;
            return;
        }
        g_rcLast = now;
        ++g_rcTraceLines;
        Log("rc trace: our Sora motion %u%s, chain +0x638 %u, window (bit 21) %d, trigger offer (bit 17) %d, slot 1 "
            "override command %u", now.motion, grab ? " (in a grab)" : "", now.chain, (flags >> 21) & 1, (flags >> 17) & 1,
            now.slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) {  // e.g. during a load: this frame only (it turned the trace off for good)
        if (g_rcTraceFaults++ == 0) Log("rc trace: a read failed (skipped; later ones not logged)");
    }
}

void ReactionSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    RcTrace();
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
        OfferAttackCommands();
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
    if (g_frame - g_statsFrame >= 600 &&
        (g_held || g_offersApplied || g_usesSent || g_usesRun || g_usesRefused || g_atkOffers)) {
        g_statsFrame = g_frame;
        Log("reaction sync: so far %u offers held back for the friend (%u packets), %u frames of offers put into our "
            "menu, uses sent %u, run %u (from the friend's own list %u, with our attack %u), refused %u, dropped for "
            "the other's first press %u; attack command offers sent %u", g_held, g_offersSent, g_offersApplied,
            g_usesSent, g_usesRun, g_ownListRun, g_atkSecond, g_usesRefused, g_firstPressDrops, g_atkOffers);
    }
}

}  // namespace kh2coop
