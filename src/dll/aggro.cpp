// Aggro log (KH2COOP_AGGRO_LOG=1): pass-through hooks on the two functions
// that decide whom an enemy attacks (found 2026-10-05 with the write watch):
//   exe+0x411300 RecordAttacker(enemy, attacker): enemy+0xD38 = attacker;
//                if the attacker is valid and on team 1, enemy+0xD40 = attacker.
//   exe+0x3BE2B0 SetTarget(slot, mode, actor): slot = {cptr target, ...};
//                mode 0 = player, 1 = last party attacker (actor+0xD40), ...
// Logs who hits enemies (is it ever the Sora copy?) and a per-mode tally of
// the targets SetTarget picks. Finding (2026-10-05): the copy's mirrored
// attack motions never hit anything (0 RecordAttacker calls with the copy), so
// enemies can't pick it up through mode 1.
//
// KH2COOP_COPY_AGGRO=1 (works without the log): an enemy whose
// target slot (enemy+0xBF8, mode at +0xC04) holds Sora in modes 0-2 (player /
// last attacker / player) gets the Sora copy instead whenever the copy is
// closer: checked when SetTarget runs and every 10 frames over the entity list
// (enemies mostly pick a target once, when they appear).
//
// Downed players (DOWNED=1, default; works without COPY_AGGRO): an enemy whose
// target is a downed Sora (ours, or the copy of a downed peer) switches to the
// player who is still up, at the same two moments. User test 2026-10-06:
// enemies kept piling on the downed Sora while the other player was free.

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "puppet.hpp"
#include "watch.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kRecordAttacker = 0x411300;
constexpr std::uint8_t kRecordAttackerBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kSetTarget = 0x3BE2B0;
constexpr std::uint8_t kSetTargetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kIsActorValid = 0x3BA720;  // (actor) -> bool: in the entity list, not dying
constexpr std::uintptr_t kSoraPtr = 0x2A171C8;
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;  // (object) -> compressed pointer
constexpr std::uintptr_t kActorPos = 0x640 + 0x30;  // entity position x, y, z
constexpr std::uintptr_t kTargetSlot = 0xBF8;        // {cptr target, u32 point, cptr, u32 mode}
constexpr std::uintptr_t kNextActor = 0xA90;         // cptr to the next actor in the entity list

// Enemies near the other player vanish on the host (TODO 1.4): an enemy's AI script checks how far
// "the player" (exe+0x2A105D0, = GetPartyMember(0)) is and, when too far, sinks back and is removed
// (fade +0xA08 down to 0 -> slot +0x38 -> exe+0x3B4700 sets +0x120 bit 28 -> removal check
// exe+0x3DAC30, lead: Volpestyle docs/ENEMY_PARITY.md); its group then makes it again. Session 13 and
// bench 2026-10-08: Shadows the friend's position spawned ~3300 units from the host's Sora lived ~3 s
// each (appear 44, fall, land, sink 45), 16 in 12 s; with the player pointer set to the copy for 12 s,
// 4 stayed the whole time and fought. KH2COOP_AI_NEAREST_PLAYER=1 (default): during the update of an
// enemy far from our Sora (kSwapMinDist) that is closer to the Sora copy, the player pointer is the
// copy. Near our Sora nothing changes. Also while the other player is downed (user, 2026-10-08): the
// enemies near them stay instead of sinking away and coming back (two-PC test: 6 Shadows in a row while
// the friend was down, each "gone" one paid the friend EXP and drops). Who they attack is still
// MaybeRetarget's job (a downed player isn't kept as the target).
// The swap is made around the enemy's actor update (puppet.cpp) and around every run of its AI
// script (exe+0x41B400, called by the VM exe+0x3E1C80 / 0x3E1410 with the script thread in rcx, owner
// actor at thread+0x60), which also happens outside the actor update: with the update alone the
// Shadows still sank after 4-8 s (bench).
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;
constexpr float kSwapMinDist = 1500.0f;
constexpr std::uintptr_t kAiRun = 0x41B400;
constexpr std::uint8_t kAiRunBytes[] = {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54};
constexpr std::uintptr_t kThreadOwner = 0x60;
using PFN_AiRun = int(__fastcall*)(std::uintptr_t, void*, void*, void*);
PFN_AiRun g_realAiRun = nullptr;
using PFN_RecordAttacker = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_SetTarget = void(__fastcall*)(std::uint32_t*, int, std::uintptr_t);
using PFN_IsActorValid = bool(__fastcall*)(std::uintptr_t);
using PFN_EncodeHandle = std::uint32_t(__fastcall*)(std::uintptr_t);
bool g_copyAggro = false;
bool g_downedAggro = false;          // DOWNED=1: never keep a downed player as the target
std::uint32_t g_offDowned = 0;
bool g_aggroLog = false;
std::uint32_t g_retargeted = 0, g_noCopy = 0, g_notCloser = 0;
PFN_RecordAttacker g_realRecordAttacker = nullptr;
PFN_SetTarget g_realSetTarget = nullptr;
bool g_nearestPlayerAi = false;
std::uint32_t g_aiSwaps = 0;  // enemy updates run with the copy as the player

std::uint64_t g_frame = 0;
int g_attackerLogs = 0;
constexpr int kMaxAttackerLogs = 150;
// tally[mode 0..7][class 0 none, 1 Sora, 2 companion, 3 copy, 4 other]
std::uint32_t g_tally[8][5] = {};
std::uint32_t g_copyAttacks = 0, g_copyAsPartyAttacker = 0;

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = reinterpret_cast<const std::uintptr_t*>(ExeBase() + kHandleBases)[(h >> 25) & 0x3F];
    return base | (h & 0x1FFFFFF);
}

const char* Model(std::uintptr_t actor, char* out) {
    strcpy_s(out, 33, "?");
    __try {
        std::uintptr_t exe = ExeBase();
        auto row = *reinterpret_cast<const std::uintptr_t*>(actor + 0x918);
        if (row > exe && row < exe + 0x3000000) {
            std::memcpy(out, reinterpret_cast<const void*>(row + 8), 32);
            out[32] = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return out;
}

bool IsPlayerClass(std::uintptr_t actor) {
    __try {
        std::uintptr_t exe = ExeBase();
        auto row = *reinterpret_cast<const std::uintptr_t*>(actor + 0x918);
        return row > exe && row < exe + 0x3000000 && *reinterpret_cast<const std::uint8_t*>(row + 4) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int ActorClass(std::uintptr_t a) {
    if (!a) return 0;
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + kSoraPtr);
    if (a == sora) return 1;
    for (int i = 0; i < 2; ++i)
        if (a == *reinterpret_cast<const std::uintptr_t*>(exe + kFriendSlots + 8 * i)) return 2;
    return IsPlayerClass(a) ? 3 : 4;
}

std::uint32_t Team(std::uintptr_t a) {
    __try {
        return *reinterpret_cast<const std::uint32_t*>(a + 0x4DC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFF;
    }
}

void __fastcall HookRecordAttacker(std::uintptr_t enemy, std::uintptr_t attacker) {
    g_realRecordAttacker(enemy, attacker);
    if (!g_aggroLog) return;
    int cls = ActorClass(attacker);
    if (cls == 3) {
        ++g_copyAttacks;
        if (Decode(*reinterpret_cast<const std::uint32_t*>(enemy + 0xD40)) == attacker) ++g_copyAsPartyAttacker;
    }
    if (g_attackerLogs >= kMaxAttackerLogs) return;
    ++g_attackerLogs;
    char model[33];
    bool valid = attacker && reinterpret_cast<PFN_IsActorValid>(ExeBase() + kIsActorValid)(attacker);
    static const char* kClass[] = {"none", "Sora", "companion", "Sora copy", "other"};
    Log("aggro: enemy exe+0x%llX hit by %s 0x%llX (model %s, team %d, valid %d); +0xD40 now %s",
        static_cast<unsigned long long>(enemy - ExeBase()), kClass[cls], static_cast<unsigned long long>(attacker),
        Model(attacker, model), static_cast<int>(Team(attacker)), valid ? 1 : 0,
        kClass[ActorClass(Decode(*reinterpret_cast<const std::uint32_t*>(enemy + 0xD40)))]);
}

float Dist2(std::uintptr_t a, std::uintptr_t b) {
    auto pa = reinterpret_cast<const float*>(a + kActorPos);
    auto pb = reinterpret_cast<const float*>(b + kActorPos);
    float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
    return dx * dx + dy * dy + dz * dz;
}

bool PeerDowned() {
    PeerPose p;
    return AvatarLinkPeerNow(p) && p.downed;
}

// In a player-target slot (modes 0-2): a downed player -> the other player if
// that one is up; with COPY_AGGRO, Sora -> copy when the copy is closer.
void MaybeRetarget(std::uint32_t* slot, int mode, std::uintptr_t enemy) {
    if (mode < 0 || mode > 2) return;
    std::uintptr_t target = Decode(slot[0]);
    if (!target) return;
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kSoraPtr);
    std::uintptr_t copy = PuppetCloneActor();
    if (target != sora && (!copy || target != copy)) return;
    bool soraUp = !(g_downedAggro && DownedIsDown());
    bool copyUp = copy && !(g_downedAggro && PeerDowned());
    std::uintptr_t want = target;
    if (target == sora) {
        if (!soraUp && copyUp) {
            want = copy;
        } else if (g_copyAggro && soraUp) {
            if (!copy) ++g_noCopy;
            else if (!copyUp || Dist2(enemy, copy) >= Dist2(enemy, sora)) ++g_notCloser;
            else want = copy;
        }
    } else if (!copyUp && soraUp) {
        want = sora;
    }
    if (want == target) return;
    slot[0] = reinterpret_cast<PFN_EncodeHandle>(ExeBase() + kEncodeHandle)(want);
    slot[1] = 0;
    ++g_retargeted;
    if ((target == sora && !soraUp) || (target == copy && !copyUp)) ++g_offDowned;
}

void RetargetAll() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t a = *reinterpret_cast<const std::uintptr_t*>(exe + kSoraPtr);
    for (int n = 0; a && n < 512; ++n, a = Decode(*reinterpret_cast<const std::uint32_t*>(a + kNextActor))) {
        if (*reinterpret_cast<const std::uint32_t*>(a + 0x4DC) != 2) continue;
        auto slot = reinterpret_cast<std::uint32_t*>(a + kTargetSlot);
        MaybeRetarget(slot, static_cast<int>(slot[3]), a);
    }
}

void __fastcall HookSetTarget(std::uint32_t* slot, int mode, std::uintptr_t actor) {
    g_realSetTarget(slot, mode, actor);
    if ((g_copyAggro || g_downedAggro) && actor) MaybeRetarget(slot, mode, actor);
    int m = mode >= 0 && mode < 8 ? mode : 7;
    ++g_tally[m][ActorClass(Decode(slot[0]))];
}

}  // namespace

std::uintptr_t AggroPlayerSwapFor(std::uintptr_t actor) {
    if (!g_nearestPlayerAi) return 0;
    __try {
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kPlayerPtr);
        if (!sora || Team(actor) != 2) return 0;
        std::uintptr_t copy = PuppetCloneActor();
        if (!copy) return 0;
        float toSora = Dist2(actor, sora);
        if (toSora < kSwapMinDist * kSwapMinDist || Dist2(actor, copy) >= toSora) return 0;
        ++g_aiSwaps;
        return copy;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

namespace {
int __fastcall HookAiRun(std::uintptr_t thread, void* table, void* a, void* b) {
    std::uintptr_t owner = 0;
    __try {
        owner = thread ? *reinterpret_cast<const std::uintptr_t*>(thread + kThreadOwner) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        owner = 0;
    }
    std::uintptr_t copy = owner ? AggroPlayerSwapFor(owner) : 0;
    if (!copy) return g_realAiRun(thread, table, a, b);
    auto player = reinterpret_cast<std::uintptr_t*>(ExeBase() + kPlayerPtr);
    std::uintptr_t sora = *player;
    *player = copy;
    bool outer = g_swapActive;
    g_swapActive = true;
    int result = g_realAiRun(thread, table, a, b);
    g_swapActive = outer;
    if (*player == copy) *player = sora;
    return result;
}
}  // namespace

void AggroInit() {
    g_aggroLog = EnvInt("KH2COOP_AGGRO_LOG", 0) == 1;
    g_copyAggro = EnvInt("KH2COOP_COPY_AGGRO", 0) == 1;
    g_downedAggro = EnvInt("KH2COOP_DOWNED", 1) == 1;
    g_nearestPlayerAi = EnvInt("KH2COOP_AI_NEAREST_PLAYER", 1) == 1;
    if (g_nearestPlayerAi)
        g_nearestPlayerAi = HookFunction(kAiRun, kAiRunBytes, sizeof(kAiRunBytes), reinterpret_cast<void*>(HookAiRun),
                                         reinterpret_cast<void**>(&g_realAiRun), "enemy AI script run (nearest player)");
    if (g_nearestPlayerAi)
        Log("aggro: enemies far from our Sora see the Sora copy as the player when it is closer (they stay and "
            "fight it instead of vanishing)");
    if (!g_aggroLog && !g_copyAggro && !g_downedAggro) return;
    if (g_aggroLog)
        HookFunction(kRecordAttacker, kRecordAttackerBytes, sizeof(kRecordAttackerBytes),
                 reinterpret_cast<void*>(HookRecordAttacker), reinterpret_cast<void**>(&g_realRecordAttacker),
                 "RecordAttacker (aggro log)");
    HookFunction(kSetTarget, kSetTargetBytes, sizeof(kSetTargetBytes), reinterpret_cast<void*>(HookSetTarget),
                 reinterpret_cast<void**>(&g_realSetTarget), "SetTarget (aggro log)");
    if (g_copyAggro) Log("aggro: experiment on: enemies that would target Sora target the copy when it is closer");
    if (g_downedAggro) Log("aggro: enemies leave a downed player for the one still up");
}

void AggroFrame() {
    if (!g_realSetTarget) return;
    ++g_frame;
    if ((g_copyAggro || g_downedAggro) && g_frame % 10 == 0) {
        __try {
            RetargetAll();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    static std::uint32_t loggedAiSwaps = 0;
    if (g_frame % 600 == 0 && g_aiSwaps != loggedAiSwaps) {
        Log("aggro: enemy updates with the Sora copy as the player so far: %u", g_aiSwaps);
        loggedAiSwaps = g_aiSwaps;
    }
    static std::uint32_t loggedOffDowned = 0;
    if (g_frame % 600 == 0 && g_offDowned != loggedOffDowned) {
        Log("aggro: enemies moved off a downed player so far: %u", g_offDowned);
        loggedOffDowned = g_offDowned;
    }
    if (!g_aggroLog || g_frame % 600 != 0) return;
    bool any = g_copyAttacks != 0;
    for (auto& row : g_tally)
        for (auto n : row) any |= n != 0;
    if (!any) return;
    Log("aggro: last 10 s: copy hits recorded %u (as party attacker %u); Sora -> copy %u (no copy %u, copy not "
        "closer %u). SetTarget picks by mode (none / Sora / companion / Sora copy / other):", g_copyAttacks,
        g_copyAsPartyAttacker, g_retargeted, g_noCopy, g_notCloser);
    g_retargeted = g_noCopy = g_notCloser = 0;
    for (int m = 0; m < 8; ++m) {
        auto& t = g_tally[m];
        if (t[0] + t[1] + t[2] + t[3] + t[4] == 0) continue;
        Log("  mode %d: %u / %u / %u / %u / %u", m, t[0], t[1], t[2], t[3], t[4]);
    }
    std::memset(g_tally, 0, sizeof(g_tally));
    g_copyAttacks = g_copyAsPartyAttacker = 0;
}

}  // namespace kh2coop
