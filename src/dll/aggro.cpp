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

void AggroInit() {
    g_aggroLog = EnvInt("KH2COOP_AGGRO_LOG", 0) == 1;
    g_copyAggro = EnvInt("KH2COOP_COPY_AGGRO", 0) == 1;
    g_downedAggro = EnvInt("KH2COOP_DOWNED", 1) == 1;
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
