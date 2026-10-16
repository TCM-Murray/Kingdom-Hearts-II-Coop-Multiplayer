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
// Except an enemy far from the player who is up (DOWNED_STAY=1, default): it stays on the downed one, so
// enemies stay where they are instead of crossing the map (TODO 1.39).

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "grab_sync.hpp"
#include "puppet.hpp"
#include "reaction_sync.hpp"
#include "watch.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

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
// Bosses (model B_...) are left out (KH2COOP_AI_NEAREST_PLAYER_BOSSES=0, default): their scripts run whole phases
// around "the player" (grabs, reaction commands, scenes). Final Xemnas, extra copy, two-copy bench 2026-10-13
// (session 32, `test_extra_copy.py xend`): with the swap, the host's game stuck on a white loading screen after
// scene re_b_ex170-61 in 2 of 2 runs and the friend's Xemnas never asked for its scenes (-11, -40, -50, -60);
// with the swap off (1 run) or bosses left out (2 runs) the host went on to -70 / -71 / -72 every time.
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
// TODO 1.39 (user, 2026-10-08): one player down and the enemy far (kSwapMinDist) from the one still up:
// it stays on the downed player (whose hits are dropped) instead of crossing the map. Bench 2026-10-08:
// emptying its target instead left the Shadows running in place against a wall (their AI asked for a new
// target ~1000 times a second, exe+0x4319D0 -> SetTarget mode 2), so that was dropped.
bool g_downedStay = false;
std::uint32_t g_stayed = 0;
bool g_aggroLog = false;
std::uint32_t g_hostTargetsSet = 0;  // friend: our enemies' targets set to the host's pick (FollowHostTarget)
std::uint32_t g_retargeted = 0, g_noCopy = 0, g_notCloser = 0;
PFN_RecordAttacker g_realRecordAttacker = nullptr;
PFN_SetTarget g_realSetTarget = nullptr;
bool g_nearestPlayerAi = false;
bool g_swapBosses = false;
std::uint32_t g_bossSwapsSkipped = 0;
// One brain (TODO 1.62, you 2026-10-13: bosses go for the nearest player): a boss's "player" (what its script grabs,
// aims at and offers reaction commands to, reaction_sync.cpp) is the nearer Sora, ours or the friend's copy. It
// switches only when the other one is nearer by kBossMargin, at most every kBossHoldMs, never while the boss is in
// a reaction command (it offered one in the last second), and never to a downed player while the other is up.
// A boss that strikes or grabs a player (a script's trap_attack_strike, grab sync's grab and let go) keeps that
// player for kBossActHoldMs after each such call, so a move in steps ends on the player it started on (Armored
// Xemnas II: his push grabs with attack 269, then throws with 1703 + 1704 1.5 s later; the switch to the nearer
// player right after 269 threw the host's Sora instead and left the friend's close, 2026-10-14). An attached boss
// (Xemnas on the dragon) is measured from where it is drawn: its own place is (0, 0, 0) then.
// BOSS_NEAREST_PLAYER=0: bosses keep our Sora as the player, as before.
bool g_bossNearest = false;
std::uint32_t g_xemnasOnUs = 0;
constexpr float kBossMargin = 300.0f;
constexpr std::uint64_t kBossHoldMs = 4000;
constexpr std::uint64_t kBossActHoldMs = 2000;
struct BossPick {
    std::uintptr_t boss;
    bool copy;
    std::uint64_t since;  // GetTickCount64 of the last switch
    std::uint64_t holdUntil;  // a strike or grab on its player: kept until then
};
std::uint32_t g_bossActHolds = 0;
BossPick g_bossPicks[8] = {};
std::uint32_t g_bossSwitches = 0;
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

// Final Xemnas: the story one (B_EX170_LAST) and Data Xemnas's second part (B_EX170_LAST_LV99), the same fight and
// the same Riku grab (your two-PC Data Xemnas run 2026-10-16: the rule matched only the story name, Data Xemnas
// grabbed the friend's copy and the host crashed at exe+0x1A8E67 as on the session 35 bench).
bool IsFinalXemnas(const char* model) {
    return std::strncmp(model, "B_EX170_LAST", 12) == 0 && (model[12] == 0 || model[12] == '_');
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
    WorldSyncAttackerRecorded(enemy, attacker);
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

// Down or still in its get-up: control isn't back yet (user, 2026-10-08: waiting enemies wake up then).
bool PeerNotBackYet() {
    PeerPose p;
    return AvatarLinkPeerNow(p) && (p.downed || p.gettingUp);
}

// TODO 1.42 (user, 2026-10-08): a companion (Goofy, Donald) who is up counts like the player still up, so it
// wakes waiting enemies near it and they turn on it; a knocked-out one (HP 0, collapse 54, get-up 55) doesn't.
// Mirrored companions on the friend's PC copy the host's position, HP and motion, so both games agree.
// The nearest such companion within kSwapMinDist of the enemy, or 0.
constexpr std::uintptr_t kCompanionStats = 0x5C0, kCompanionMotion = 0x180;
std::uintptr_t NearUpCompanion(std::uintptr_t enemy) {
    std::uintptr_t exe = ExeBase(), best = 0;
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + kSoraPtr), copy = PuppetCloneActor();
    float bestD = kSwapMinDist * kSwapMinDist;
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t c = *reinterpret_cast<const std::uintptr_t*>(exe + kFriendSlots + 8 * i);
        if (!c || c == sora || c == copy || Team(c) != 1) continue;
        if (!reinterpret_cast<PFN_IsActorValid>(exe + kIsActorValid)(c)) continue;
        auto stats = *reinterpret_cast<const std::uintptr_t*>(c + kCompanionStats);
        if (!stats || *reinterpret_cast<const std::int32_t*>(stats) <= 0) continue;
        std::uint32_t motion = *reinterpret_cast<const std::uint32_t*>(c + kCompanionMotion);
        if (motion == 54 || motion == 55) continue;
        float d = Dist2(enemy, c);
        if (d < bestD) {
            bestD = d;
            best = c;
        }
    }
    return best;
}

// In a player-target slot (modes 0-2): a downed player -> the other player if
// that one is up; with COPY_AGGRO, Sora -> copy when the copy is closer.
// Friend (one brain): the host's enemy goes for a player; our copy of it goes for the same one here (TODO 1.80: Data
// Xigbar's shots, which copy his target slot when they start, flew at the host's copy because COPY_AGGRO had put it
// there when it was closer). host 1 = the host's Sora (our copy of it), 2 = the friend (our Sora). True = set.
bool FollowHostTarget(std::uint32_t* slot, std::uintptr_t enemy) {
    if (AvatarLinkIsHost()) return false;
    int host = WorldSyncHostTargetOf(enemy);
    if (!host) return false;
    std::uintptr_t want = host == 2 ? *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kSoraPtr) : PuppetCloneActor();
    if (!want) return false;
    if (Decode(slot[0]) != want) {
        slot[0] = reinterpret_cast<PFN_EncodeHandle>(ExeBase() + kEncodeHandle)(want);
        slot[1] = 0;
        ++g_hostTargetsSet;
    }
    return true;
}

void MaybeRetarget(std::uint32_t* slot, int mode, std::uintptr_t enemy) {
    if (FollowHostTarget(slot, enemy)) return;
    if (mode < 0 || mode > 2) return;
    std::uintptr_t target = Decode(slot[0]);
    if (!target) return;
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kSoraPtr);
    std::uintptr_t copy = PuppetCloneActor();
    if (target != sora && (!copy || target != copy)) return;
    bool soraUp = !(g_downedAggro && DownedIsDown());
    bool copyUp = copy && !(g_downedAggro && PeerDowned());
    std::uintptr_t want = target;
    if (g_downedStay && copy && soraUp != copyUp && Dist2(enemy, soraUp ? sora : copy) >= kSwapMinDist * kSwapMinDist) {
        std::uintptr_t companion = NearUpCompanion(enemy);
        want = companion ? companion : soraUp ? copy : sora;
    } else if (target == sora) {
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
    if (g_downedStay && ((want == sora && !soraUp) || (want == copy && !copyUp))) ++g_stayed;
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

namespace {
// Where an actor is: its place, or for one attached to another (+0x6A0) where it is drawn (world matrix row 3).
const float* Where(std::uintptr_t a) {
    if (*reinterpret_cast<const std::uint32_t*>(a + 0x6A0)) return reinterpret_cast<const float*>(a + 0x40 + 48);
    return reinterpret_cast<const float*>(a + kActorPos);
}
float DrawnDist(std::uintptr_t a, std::uintptr_t b) {
    const float *pa = Where(a), *pb = Where(b);
    float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

BossPick* FindPick(std::uintptr_t boss, BossPick** oldest) {
    BossPick* p = nullptr;
    *oldest = &g_bossPicks[0];
    for (BossPick& b : g_bossPicks) {
        if (b.boss == boss) p = &b;
        if (b.since < (*oldest)->since) *oldest = &b;
    }
    return p;
}

// The boss's player now: true = the copy (see g_bossNearest).
bool BossOnCopy(std::uintptr_t boss, std::uintptr_t sora, std::uintptr_t copy, const char* model) {
    std::uint64_t now = GetTickCount64();
    BossPick* oldest;
    BossPick* p = FindPick(boss, &oldest);
    if (p && now < p->holdUntil) return p->copy;
    bool soraUp = !(g_downedAggro && DownedIsDown()), copyUp = !(g_downedAggro && PeerDowned());
    float ds = DrawnDist(boss, sora), dc = DrawnDist(boss, copy);
    bool want = !soraUp && copyUp ? true : !copyUp && soraUp ? false : dc < ds;
    if (!p) {
        p = oldest;
        *p = {boss, want, now, 0};
        Log("aggro: boss %s goes for the nearer player: %s (ours %.0f away, the copy %.0f)", model,
            want ? "the friend's copy" : "our Sora", ds, dc);
        return want;
    }
    if (want == p->copy || ReactionSyncBusy(boss)) return p->copy;
    bool forced = (p->copy && !copyUp && soraUp) || (!p->copy && !soraUp && copyUp);
    bool nearer = p->copy ? ds + kBossMargin < dc : dc + kBossMargin < ds;
    if (!forced && (!nearer || now - p->since < kBossHoldMs)) return p->copy;
    p->copy = want;
    p->since = now;
    if (g_bossSwitches++ < 60)
        Log("aggro: boss %s now goes for %s (ours %.0f away, the copy %.0f%s)", model,
            want ? "the friend's copy" : "our Sora", ds, dc, forced ? ", the other player is down" : "");
    return want;
}
}  // namespace

void AggroBossActedOn(std::uintptr_t boss, bool copy, const char* what) {
    if (!g_bossNearest || !boss) return;
    char model[33];
    if (Model(boss, model)[0] != 'B' || model[1] != '_') return;  // bosses only (BossOnCopy)
    std::uint64_t now = GetTickCount64();
    BossPick* oldest;
    BossPick* p = FindPick(boss, &oldest);
    bool changed = !p || p->copy != copy;
    if (!p) {
        p = oldest;
        *p = {boss, copy, now, 0};
    } else if (p->copy != copy) {
        p->copy = copy;
        p->since = now;
    }
    p->holdUntil = now + kBossActHoldMs;
    if (changed || g_bossActHolds < 20) {
        ++g_bossActHolds;
        if (g_bossActHolds <= 60)
            Log("aggro: boss %s %s %s: keeps it as its player for %llu s%s", model, what,
                copy ? "the friend's copy" : "our Sora", static_cast<unsigned long long>(kBossActHoldMs / 1000),
                changed ? " (switched to it)" : "");
    }
}

int AggroTargetOf(std::uintptr_t actor) {
    __try {
        std::uintptr_t copy = PuppetCloneActor();
        if (!actor || !copy) return 0;
        char model[33];
        if (g_bossNearest && Model(actor, model)[0] == 'B' && model[1] == '_') {
            if (IsFinalXemnas(model)) return 1;  // always our Sora (AggroPlayerSwapFor)
            BossPick* oldest;
            if (BossPick* p = FindPick(actor, &oldest)) return p->copy ? 2 : 1;
        }
        std::uintptr_t t = Decode(*reinterpret_cast<const std::uint32_t*>(actor + kTargetSlot));
        return t == copy ? 2 : t == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kSoraPtr) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

namespace {
// Log when an object a boss made changes whom it follows (bench check of the Keyblade rule, at most 60 lines).
struct MadeFollow {
    std::uintptr_t actor;
    bool copy;
};
MadeFollow g_madeFollows[16];
int g_madeFollowNext = 0;
std::uint32_t g_madeFollowLogs = 0;

void NoteMadeFollows(std::uintptr_t actor, std::uintptr_t maker, bool copy, bool hooked) {
    MadeFollow* f = nullptr;
    for (MadeFollow& x : g_madeFollows)
        if (x.actor == actor) f = &x;
    if (f && f->copy == copy) return;
    if (!f) f = &g_madeFollows[g_madeFollowNext++ % 16];
    *f = {actor, copy};
    char model[33], by[33];
    if (g_madeFollowLogs++ < 60)
        Log("aggro: %s (made by %s) follows %s (%s)", Model(actor, model), Model(maker, by),
            copy ? "the friend's copy" : "our Sora", hooked ? "the player its maker hooked" : "its maker's target");
}
}  // namespace

std::uintptr_t AggroPlayerSwapFor(std::uintptr_t actor) {
    if (!g_nearestPlayerAi) return 0;
    __try {
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kPlayerPtr);
        if (!sora) return 0;
        // An object an enemy's script made (team 3) sees its maker's player: Data Roxas's keyblades, made by his
        // trap_sysobj_appear, go to "the player" after the steal, and went to our Sora when the friend stole them
        // (you, 2026-10-14: "the extra keyblades seems to be given to Host Sora, not Host Sora's clone"). Once the maker
        // has grabbed a player with a hook (the steal), they stay with that player (your pick, 2026-10-14), not with
        // the maker's current target.
        if (Team(actor) == 3) {
            std::uintptr_t maker = WorldCensusMadeBy(actor);
            if (!maker || maker == actor || Team(maker) != 2) return 0;
            int hooked = GrabSyncLastHooked(maker);
            std::uintptr_t to = hooked ? (hooked == 2 ? PuppetCloneActor() : 0) : AggroPlayerSwapFor(maker);
            NoteMadeFollows(actor, maker, to != 0, hooked != 0);
            return to;
        }
        if (Team(actor) != 2) return 0;
        std::uintptr_t copy = PuppetCloneActor();
        if (!copy) return 0;
        if (!AvatarLinkIsHost()) {  // one brain: our copy of the host's enemy has the host's player (FollowHostTarget)
            if (int host = WorldSyncHostTargetOf(actor)) return host == 1 ? copy : 0;
        }
        if (g_bossNearest && AvatarLinkIsHost()) {  // the friend's bosses don't think (one brain)
            char model[33];
            if (Model(actor, model)[0] == 'B' && model[1] == '_') {
                // Final Xemnas always goes for our Sora (you, 2026-10-14, option 3): his grab hands "the player" over
                // to Riku, so the host is the one grabbed and plays Riku while the friend fights on as Sora. His grab
                // of the friend's copy crashed our game (session 35 bench). An exception to the nearer-player rule.
                if (IsFinalXemnas(model)) {
                    if (g_xemnasOnUs++ == 0) Log("aggro: boss %s always goes for our Sora (his grab gives Riku to the host)", model);
                    return 0;
                }
                if (!BossOnCopy(actor, sora, copy, model)) return 0;
                ++g_aiSwaps;
                return copy;
            }
        }
        float toSora = Dist2(actor, sora);
        if (toSora < kSwapMinDist * kSwapMinDist || Dist2(actor, copy) >= toSora) return 0;
        if (!g_swapBosses) {
            char model[33];
            if (Model(actor, model)[0] == 'B' && model[1] == '_') {
                if (g_bossSwapsSkipped++ == 0)
                    Log("aggro: boss %s is far from our Sora: it keeps our Sora as the player (bosses are left out)",
                        model);
                return 0;
            }
        }
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

// Wait while a player is down (TODO 1.39 test, KH2COOP_DOWNED_WAIT=1): an enemy near the downed player
// (kSwapMinDist) and far from the one still up (and from any companion who is up, 1.42) skips its brain, so it doesn't move, attack or leave, and
// stands in its idle motion, until the downed player has control again (after the get-up); its target
// stays the downed player (MaybeRetarget). The brain exe+0x3B4460
// (actor) runs the actor's AI script (+0x5B0, VM exe+0x3E1C80) and its action script (+0x390,
// exe+0x3CA550); 32 classes' think slot (+0x20, `mov rcx,rdx; jmp`) jump to it (exe scan 2026-10-08;
// lead: Volpestyle ENEMY_PARITY.md, they skip it for mirrored enemies). Dying enemies are never held.
constexpr std::uintptr_t kBrain = 0x3B4460;
constexpr std::uint8_t kBrainBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kActorMotion = 0x180, kActorStats = 0x5C0;
constexpr int kEnemyIdleMotion = 0;
using PFN_Brain = void(__fastcall*)(std::uintptr_t);
PFN_Brain g_realBrain = nullptr;
bool g_downedWait = false;
std::uint32_t g_brainSkips = 0, g_idleSets = 0, g_oneBrainSkips = 0;

bool WaitsForDowned(std::uintptr_t actor) {
    if (Team(actor) != 2) return false;
    auto stats = *reinterpret_cast<const std::uintptr_t*>(actor + kActorStats);
    if (!stats || *reinterpret_cast<const std::int32_t*>(stats) <= 0) return false;
    if (!reinterpret_cast<PFN_IsActorValid>(ExeBase() + kIsActorValid)(actor)) return false;
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return false;
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kSoraPtr);
    bool soraDown = DownedIsDown(), copyDown = PeerNotBackYet();  // both include the get-up
    if (soraDown == copyDown) return false;
    std::uintptr_t up = soraDown ? copy : sora, down = soraDown ? sora : copy;
    constexpr float kFar2 = kSwapMinDist * kSwapMinDist;
    return Dist2(actor, up) >= kFar2 && Dist2(actor, down) < kFar2 && !NearUpCompanion(actor);
}

void __fastcall HookBrain(std::uintptr_t actor) {
    bool wait = false;
    __try {
        if (actor && WorldSyncSkipsBrain(actor)) {  // one brain: the host's enemy thinks for it
            ++g_oneBrainSkips;
            return;
        }
        wait = g_downedWait && actor && WaitsForDowned(actor);
        if (wait && *reinterpret_cast<const std::uint32_t*>(actor + kActorMotion) != kEnemyIdleMotion &&
            PuppetHasMotion(actor, kEnemyIdleMotion)) {
            PuppetSetMotion(actor, kEnemyIdleMotion);
            ++g_idleSets;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wait = false;
    }
    if (wait) {
        ++g_brainSkips;
        return;
    }
    g_realBrain(actor);
}
}  // namespace

void AggroInit() {
    g_aggroLog = EnvInt("KH2COOP_AGGRO_LOG", 0) == 1;
    g_copyAggro = EnvInt("KH2COOP_COPY_AGGRO", 1) == 1;
    g_downedAggro = EnvInt("KH2COOP_DOWNED", 1) == 1;
    g_nearestPlayerAi = EnvInt("KH2COOP_AI_NEAREST_PLAYER", 1) == 1;
    g_swapBosses = EnvInt("KH2COOP_AI_NEAREST_PLAYER_BOSSES", 0) == 1;
    g_bossNearest = EnvInt("KH2COOP_BOSS_NEAREST_PLAYER", 1) == 1;
    if (g_nearestPlayerAi)
        g_nearestPlayerAi = HookFunction(kAiRun, kAiRunBytes, sizeof(kAiRunBytes), reinterpret_cast<void*>(HookAiRun),
                                         reinterpret_cast<void**>(&g_realAiRun), "enemy AI script run (nearest player)");
    if (g_nearestPlayerAi)
        Log("aggro: enemies far from our Sora see the Sora copy as the player when it is closer (they stay and "
            "fight it instead of vanishing)%s", g_bossNearest ? "; bosses (one brain) go for the nearer player" : "");
    if (!(g_downedAggro && EnvInt("KH2COOP_DOWNED_STAY", 1) == 1 && EnvInt("KH2COOP_DOWNED_WAIT", 1) == 1))
        HookFunction(kBrain, kBrainBytes, sizeof(kBrainBytes), reinterpret_cast<void*>(HookBrain),
                     reinterpret_cast<void**>(&g_realBrain), "enemy brain (one brain)");
    // Also for world sync (TODO 1.40): each hit's attacker decides whether the hit counts in co-op.
    if (g_aggroLog || EnvInt("KH2COOP_OWN_HITS_ONLY", 1) == 1)
        HookFunction(kRecordAttacker, kRecordAttackerBytes, sizeof(kRecordAttackerBytes),
                     reinterpret_cast<void*>(HookRecordAttacker), reinterpret_cast<void**>(&g_realRecordAttacker),
                     "RecordAttacker (each hit's attacker)");
    if (!g_aggroLog && !g_copyAggro && !g_downedAggro) return;
    HookFunction(kSetTarget, kSetTargetBytes, sizeof(kSetTargetBytes), reinterpret_cast<void*>(HookSetTarget),
                 reinterpret_cast<void**>(&g_realSetTarget), "SetTarget (aggro log)");
    if (g_copyAggro) Log("aggro: experiment on: enemies that would target Sora target the copy when it is closer");
    if (g_downedAggro) {
        g_downedStay = EnvInt("KH2COOP_DOWNED_STAY", 1) == 1;
        Log("aggro: enemies leave a downed player for the one still up%s",
            g_downedStay ? " (those far from that one stay on the downed player)" : "");
        g_downedWait = g_downedStay && EnvInt("KH2COOP_DOWNED_WAIT", 1) == 1 &&
                       HookFunction(kBrain, kBrainBytes, sizeof(kBrainBytes), reinterpret_cast<void*>(HookBrain),
                                    reinterpret_cast<void**>(&g_realBrain), "enemy brain (wait while a player is down)");
        if (g_downedWait) Log("aggro: those enemies wait in place (idle, no attacks) until the downed player is up");
    }
}

void AggroFrame() {
    if (!g_realSetTarget) return;
    ++g_frame;
    if ((g_copyAggro || g_downedAggro) && (g_frame % 10 == 0 || !AvatarLinkIsHost())) {
        __try {
            RetargetAll();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    static std::uint32_t loggedHostTargets = 0;
    if (g_frame % 600 == 0 && g_hostTargetsSet != loggedHostTargets) {
        Log("aggro: our enemies' targets set to the host's pick so far: %u", g_hostTargetsSet);
        loggedHostTargets = g_hostTargetsSet;
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
    static std::uint32_t loggedStayed = 0;
    if (g_frame % 600 == 0 && g_stayed != loggedStayed) {
        Log("aggro: enemies kept in place while a player is down (far from the one still up) so far: %u", g_stayed);
        loggedStayed = g_stayed;
    }
    static std::uint32_t loggedOneBrain = 0;
    if (g_frame % 600 == 0 && g_oneBrainSkips != loggedOneBrain) {
        Log("aggro: brain steps of bound enemies skipped so far (one brain): %u", g_oneBrainSkips);
        loggedOneBrain = g_oneBrainSkips;
    }
    static std::uint32_t loggedSkips = 0;
    if (g_frame % 600 == 0 && g_brainSkips != loggedSkips) {
        Log("aggro: enemy brain steps skipped while waiting for a downed player so far: %u (idle motion set %u)",
            g_brainSkips, g_idleSets);
        loggedSkips = g_brainSkips;
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
