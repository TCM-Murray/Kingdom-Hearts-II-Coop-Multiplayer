#include "puppet.hpp"
#include "watch.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "motion_mirror.hpp"
#include "spawn_step.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

// Verified (VERIFIED_OFFSETS.md).
constexpr std::uintptr_t kNow = 0x717008;            // u8 world, u8 room
constexpr std::uintptr_t kInField = 0x9BA8D0;        // u8: 0 while a room loads
constexpr std::uintptr_t kFriend1Actor = 0x2A239B0;  // u64 -> Donald's actor (model P_EX020)
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kEntityPos = 0x30;          // f32 x, y, z
constexpr std::uintptr_t kEntitySin = 0x40;          // f32 sin(facing)
constexpr std::uintptr_t kEntityCos = 0x48;          // f32 cos(facing)
constexpr std::uintptr_t kEntityAngle = 0x4C;        // f32 facing, radians

// Leads from Volpestyle/kh2-multiplayer, verified by this slice's test.
constexpr std::uintptr_t kActorMotCtrl = 0x158;       // embedded motion controller
constexpr std::uintptr_t kActorCollision = 0x18C;     // u8, bit 6 = skip actor/terrain collision
constexpr std::uint8_t kNoCollide = 0x40;
constexpr std::uintptr_t kActorCarried = 0x690;       // 3 f32 displacement added each frame
constexpr std::uintptr_t kActorAccel = 0xA48;         // 0x18 bytes of acceleration terms
constexpr std::uintptr_t kActorVelocity = 0xB98;      // 3 f32
constexpr std::uintptr_t kActorFollowTimer = 0xBA8;   // f32 companion follow timer
constexpr float kFollowTimerHeld = 999.0f;

// Functions (start bytes checked against the exe before hooking).
constexpr std::uintptr_t kPerEntityUpdate = 0x3BFD30;  // (actor)
constexpr std::uint8_t kPerEntityUpdateBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kFriendAi = 0x1B0020;         // friend vtable +0x10: (typeHandler, actor)
constexpr std::uint8_t kFriendAiBytes[] = {0x48, 0x8B, 0xCA, 0xE9, 0x38, 0x36, 0x21, 0x00};
constexpr std::uintptr_t kMotionChainSet = 0x3C88C0;   // (motCtrl, motion, start, blend) -> u8
constexpr std::uint8_t kMotionChainSetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::uintptr_t kSetMotion = 0x3C86A0;        // (motCtrl, motion, start, blend)
// (motCtrl, motion) -> entry index in the actor's motion set(s) (motion * 4 + variant), -1 if it has none.
// A motion the actor lacks must never be set: SetMotion then leaves it in a T-pose (bench 2026-10-06).
constexpr std::uintptr_t kFindMotion = 0x3C7C50;
constexpr std::uint8_t kFindMotionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
// Stat change funnel (lead: Volpestyle "ApplyStatDelta"): (actor, delta, stat, reactFlag) -> new value.
// Disassembly: reads actor+0x5C0 (stat channels, 12 bytes each: current, max, min); stat 0 = HP.
constexpr std::uintptr_t kApplyStatDelta = 0x3D2EB0;
constexpr std::uint8_t kApplyStatDeltaBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56};
constexpr std::uintptr_t kActorStats = 0x5C0;
constexpr std::uintptr_t kActorTeam = 0x4DC;  // u32 lead: 0 untouchable, 1 party, 2 enemies
// Party = {player, friend slot 0, friend slot 1}: exe+0x3D5C50 returns party
// member n (0 = player, else GetFriend(n-1)). GetFriend(i) = slots[i]. With the
// Sora copy in the party the slots hold {Goofy, empty}: the copy is no party
// member, which may be why enemies ignore it. Writing it into the empty slot
// crashed at once (2026-10-04): companion AI (exe+0x1B5E30) uses each friend's
// helper object at actor+0xDF8, which a Sora actor lacks. So instead GetFriend
// answers "the copy" for the empty slot to every caller outside companion AI
// and the party manager.
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;  // u64[2]
constexpr std::uintptr_t kGetFriend = 0x3C3270;     // (index) -> actor
constexpr std::uint8_t kGetFriendBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D};
// Menu party list (count, then 0x20-byte entries; s16 character id at +0xA):
// exe+0x2FC6D0(list, i) returns entry i's id (1 Sora, 2 Donald, 3 Goofy).
// Menu code maps companion ids with exe+0x3E60C0(id) - 2 into per-companion
// ability bitfields; Sora (id 1) as companion gives -1 and the menu crashed
// reading out of bounds (exe+0x36A4B2, 2026-10-04, several times). With the
// Sora copy in the party, companion entries holding Sora read as Donald.
constexpr std::uintptr_t kMenuPartyId = 0x2FC6D0;
constexpr std::uint8_t kMenuPartyIdBytes[] = {0x85, 0xD2, 0x78, 0x11, 0x3B, 0x11, 0x7D, 0x0D};
constexpr int kCharSora = 1, kCharDonald = 2, kCharGoofy = 3;
struct RvaRange {
    std::uint32_t lo, hi;
};
constexpr RvaRange kNoCloneCallers[] = {{0x1B0000, 0x1CE000},   // companion AI
                                        {0x3C2000, 0x3C4000}};  // party manager
// Party luck sums (TODO 1.36): enemy drops (exe+0x3DC170: Lucky Lucky +0x6C, prize factor +0x74)
// and prize amounts (exe+0x3ABA40: Jackpot +0x68) add up each party member's status block. The
// copy shares our Sora's block, so our bonuses counted twice (item drop factor 3.0 instead of 2.0,
// bench 2026-10-07). An empty member is skipped there (exe+0x3BA720 looks it up in the actor list).
constexpr std::uint32_t kNoCloneExactCallers[] = {0x3DC208, 0x3DC2E7, 0x3ABA67};
constexpr std::uint8_t kSetMotionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};

// Object table (00objentry, version 3): header {u32 version, u32 count} then
// 0x60-byte rows {u32 id, u8 type, ..., char model[32] @+8, char motionSet[32] @+0x28}.
// Verified 2026-10-04: header at exe+0x2A254D0, 1900 rows; id 90 = Roxas
// (P_EX110), id 92 = Donald (P_EX020, type 1 = party member).
constexpr std::uintptr_t kObjTable = 0x2A254D0;
constexpr std::uint32_t kObjRow = 0x60, kObjModel = 0x08, kObjMotionSet = 0x28, kObjName = 32;
// Byte 7 = weapon joint: the skeleton bone the weapon is attached to (OpenKH
// objentry layout; Expert595 uses the same offset). Donald 0x02, Roxas 0x0E.
constexpr std::uint32_t kObjWeaponJoint = 0x07;
constexpr std::uint32_t kObjIdSora = 84, kObjIdRoxas = 90, kObjIdDonald = 92;
constexpr std::uint32_t kObjType = 0x04;  // u8: 0 = player class, 1 = party member

// Save data in memory starts with "KH2J". Leads disagree on where (0x9A98B0:
// Volpestyle/Archipelago; 0x9A9830: KH2 Lua library / Expert595), so check both.
constexpr std::uintptr_t kSaveCandidates[] = {0x9A98B0, 0x9A9830};
// World party table: save+0x3534 + 4*world = {player, active 1, active 2,
// bench}; 0x00 = the playable character, 0x01 Donald, 0x02 Goofy, 0x03 the
// world's ally (Beast, Mulan, Auron; 0x80 seen on it in some visits), 0x12
// empty. The user's saves: Beast's Castle 00 01 02 12 -> 00 03 02 01 once
// Beast joined, Olympus 00 83 01 02. An active slot = 0x00 makes the next room
// load spawn a real copy of the playable character in that member's place.
constexpr std::uintptr_t kPartyTable = 0x3534;
constexpr std::uint8_t kPartyPlayable = 0x00, kPartyDonald = 0x01, kPartyGoofy = 0x02, kPartyEmpty = 0x12;
// Motions seen playing correctly on the Roxas puppet (2026-10-04 test):
// 0-6 basic movement, 41/151/153/154/161/164/173/181 ground and air attacks.
// Others (e.g. 56/59 during Sora's spells) T-pose because Roxas's motion set
// lacks them; they fall back to idle (or fall if airborne).
constexpr std::uint32_t kRoxasMotions[] = {0, 1, 2, 3, 4, 5, 6, 41, 151, 153, 154, 161, 164, 173, 181};

// Donald's moveset shares Sora's basic motions (idle, walk, run, jump, fall,
// land...); anything higher (Sora's attacks) may not exist for him.
constexpr std::uint32_t kMaxSharedMotion = 8;
constexpr std::uint32_t kMotionIdle = 0, kMotionFall = 4;
constexpr std::uint64_t kStaleFrames = 120;

using PFN_PerEntityUpdate = void(__fastcall*)(void* actor);
using PFN_FriendAi = void(__fastcall*)(void* typeHandler, void* actor);
using PFN_MotionChainSet = std::uint8_t(__fastcall*)(void* motCtrl, int motion, float start, float blend);
using PFN_SetMotion = void(__fastcall*)(void* motCtrl, int motion, float start, float blend);
using PFN_ApplyStatDelta = int(__fastcall*)(void* actor, int delta, int stat, int react);
PFN_ApplyStatDelta g_realApplyStatDelta = nullptr;
bool g_keepCollision = false;   // KH2COOP_PUPPET_COLLIDE=1: don't set no-collide on the puppet
// Damage forwarding (clone mode, KH2COOP_FORWARD_HITS, default on): the Sora
// copy shares our Sora's HP record, so HP loss on it is blocked here and sent
// to its owner, whose game applies it to their own Sora.
bool g_forwardHits = false;
constexpr int kMaxPendingHits = 16;
int g_pendingDamage[kMaxPendingHits], g_pendingReact[kMaxPendingHits];
int g_pendingCount = 0;
int g_debugWho = -1, g_debugAmount = 0;  // test command (KH2COOP_DEBUG=1): damage for Sora / companion 1 / 2
std::uintptr_t g_debugDoneActors[8];      // who 4: the scene Soras already given the command
int g_debugDone = 0;
std::uint64_t g_debugSetFrame = 0;
// God mode (KH2COOP_GOD=1, automated tests only, never in the friend package; TODO 2.6): hits on the party still
// land (flinch, grabs) but cost at most 1 HP, and the HP is topped up right after.
// KH2COOP_ENEMY_GOD=1 (same rules, your idea 2026-10-13): the same for enemies (team 2), so a bench fight goes on
// as long as the test wants (the parity runs of TODO 1.55). The host applies the friend's hits the same way.
bool g_god = false, g_enemyGod = false;
// Low-HP hold (test command lowhp, automated tests only; TODO 1.58 heal bench): with god mode, a hit on our own Sora
// leaves it at this HP instead of full, so the companions see it low; a hit of 9999 or more (the benches' lethal
// hit) goes through to the downed rule, and so does every hit while it's down. 0 = off.
int g_godSoraHp = 0;
int g_statLogs = 0;
PFN_PerEntityUpdate g_realPerEntityUpdate = nullptr;
PFN_FriendAi g_realFriendAi = nullptr;
PFN_MotionChainSet g_realMotionChainSet = nullptr;
PFN_SetMotion g_setMotion = nullptr;

bool g_enabled = false;
bool g_roxasWanted = false;     // KH2COOP_PUPPET_ROXAS=1 (or =sora via KH2COOP_PUPPET_LOOK)
bool g_roxasPatched = false;    // object table now points Donald at the player look
bool g_lookIsSora = false;      // KH2COOP_PUPPET_LOOK=sora: Sora's model instead of Roxas's (crashes)
bool g_cloneMode = false;       // KH2COOP_PUPPET_LOOK=clone: a real Sora copy via the party table
// KH2COOP_CLONE_TARGET=1: GetFriend returns the Sora copy for the empty slot
// (see kGetFriend). KH2COOP_CLONE_TARGET_SKIP=<rva>,<rva>... lists more caller
// return addresses (hex) that must not get it, e.g. one that crashed.
bool g_cloneTarget = false;
bool g_itemTriggerHooked = false;  // the copy's item motion can't use items (HookItemTrigger): motion 9 allowed
using PFN_GetFriend = std::uintptr_t(__fastcall*)(int index);
PFN_GetFriend g_realGetFriend = nullptr;
std::uint32_t g_skipCallers[16];
int g_skipCount = 0;
struct CallerStat {
    std::uint32_t rva, gave, refused;
};
CallerStat g_callers[64];
int g_callerCount = 0;
PFN_GetFriend g_unusedMenuOriginal = nullptr;
int g_menuSwaps = 0;
// Target-field scan (KH2COOP_TARGET_SCAN=1): where enemies keep their target.
// v1 (exact Sora/companion pointer or handle in the first 0x1000 bytes of the
// enemy) found nothing in 13 reports. v2, every 15 frames: values pointing
// anywhere inside Sora's or the companion's first 0x1000 bytes, handles with
// bit 31 ignored, in the enemy itself (depth 0) and in up to 64 objects it
// points to (depth 1, first 0x400 bytes each).
bool g_targetScan = false;
constexpr std::uint32_t kScanBytes = 0x1000, kScanChildBytes = 0x400;
struct ScanHit {
    std::uint16_t parentOff, off;  // parentOff 0xFFFF = in the enemy itself
    std::uint8_t what;             // 0 Sora ptr, 1 companion ptr, 2 Sora handle, 3 companion handle,
                                   // 4 Sora cptr, 5 companion cptr (compressed pointer, see DecodeHandle)
    std::uint32_t count;
    std::int32_t inner;            // offset inside the target the pointer points to (pointers only)
};
ScanHit g_scan[256];
int g_scanCount = 0;
std::uint32_t g_scannedEnemies = 0;
std::uint64_t g_cloneFrame = 0;     // frame the copy was last updated by the game
std::uint16_t g_cloneRoom = 0xFFFF; // world/room it was updated in
std::uintptr_t g_save = 0;      // save data base, once found
bool g_solo = false;            // playing on without the other player (link_watch.cpp): no party change
// EXPERIMENT (KH2COOP_EXTRA_COPY=1, off by default, not for play): the copy stays out of the party table; solo_copy.cpp
// spawns it next to the player in every room, so Donald, Goofy and the world allies keep their places.
bool g_extraCopy = false;
std::uint32_t g_partyPatchedWorlds = 0;  // bit per world whose party entry we changed
std::uint8_t g_copyReplaced[32] = {};    // per world: party id the copy took the place of
std::uint8_t g_benchBefore[32] = {};     // per world: the bench (entry[3]) when we patched
std::uint32_t g_partyLogged[32] = {};    // per world: last entry logged as "left alone"
std::uintptr_t g_clone = 0;     // player-class actor other than Sora seen last frame
// A copy we faded out (PuppetRemoveCopy) is gone for everyone at once, though the game updates it until the
// fade ends: aggro's player swap onto it in a Borough fight left the player pointer exe+0x2A105D0 at 0 when
// the game dropped it (crash exe+0x3C380C, bench session 27). Cleared at the next room change or load.
std::uintptr_t g_removed = 0;
std::uintptr_t g_cloneSeen = 0; // ... being collected this frame
// Actors that were the player (list head) earlier in this room. A Drive Form swaps the player to
// another actor, and at its end the old form body stays in the room, still updated, until the next
// room change: it is player class and not the head, so it looked like a second copy and replaced
// the real one (two-PC test 2026-10-07 23:18, TODO 1.35). Cleared on every load.
constexpr int kMaxExPlayers = 8;
std::uintptr_t g_exPlayers[kMaxExPlayers] = {};
int g_exPlayerCount = 0;
std::uintptr_t g_lastPlayer = 0;
std::uint32_t g_otherCandidateLogs = 0, g_sceneActorLogs = 0;
std::uint32_t g_updatesThisFrame = 0;  // PerEntityUpdate calls since the last PuppetFrame
// Hold: a Sora copy left alone obeys the local controller like Sora, so when
// no peer pose applies it is frozen idle where it stands.
std::uintptr_t g_holdActor = 0;
PeerPose g_holdPose {};
PeerPose g_pose {};
std::uint64_t g_frame = 0, g_poseFrame = 0;
bool g_active = false;          // puppet driven this frame
std::uintptr_t g_actor = 0;     // actor we are driving
int g_lastMotion = -1;
bool g_settingMotion = false;   // our own SetMotion call is in progress
bool g_collisionSaved = false;
bool g_savedNoCollide = false;
std::uint32_t g_aiSkips = 0, g_transforms = 0;
std::uintptr_t g_heldActor = 0;  // Donald's actor this frame (held idle when not driven)
std::uintptr_t g_idledActor = 0; // actor HoldIdle last set to idle

bool IsPlayerClass(std::uintptr_t actor) {
    std::uintptr_t exe = ExeBase();
    auto row = *reinterpret_cast<const std::uintptr_t*>(actor + 0x918);  // -> object table row
    if (row <= exe || row >= exe + 0x3000000) return false;
    return *reinterpret_cast<const std::uint8_t*>(row + kObjType) == 0;
}

std::uintptr_t DecodeHandle(std::uint32_t h);

// Story cutscene actors: the scene's first SeqPlayAnimation (exe+0x2D2160) asks the actor factory
// exe+0x3DF930 for object | 0x40000000, which builds an event actor (exe+0x419230) whose class handle
// at actor+0 decodes to exe+0x752658 (the field Sora's: exe+0x750300; probe 2026-10-10). The scene's
// Sora (P_EX120, ACTOR_SORA...) and our cutscene twin are player-class objects, but never the copy:
// taken as the copy, the twin was held idle where it was made and never moved (bench 2026-10-10,
// TODO 5.12), and other scenes' actors were driven with the peer's pose.
constexpr std::uintptr_t kSceneActorClass = 0x752658;
bool IsSceneActor(std::uintptr_t actor) {
    return DecodeHandle(*reinterpret_cast<const std::uint32_t*>(actor)) == ExeBase() + kSceneActorClass;
}

// Puts a copy of the playable character into the current world's active
// party in place of Donald (or Goofy when Donald isn't active); the world's
// ally stays (user decision 2026-10-06). Checked every frame, so party
// changes from the menu and new worlds are caught before the next room load.
void PatchPartyForClone() {
    if (g_solo) return;
    std::uintptr_t exe = ExeBase();
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow);
    if (world >= 32) return;
    if (!g_save) {
        for (auto rva : kSaveCandidates)
            if (std::memcmp(reinterpret_cast<const void*>(exe + rva), "KH2J", 4) == 0) g_save = exe + rva;
        if (!g_save) return;
        Log("puppet: save data found at exe+0x%llX", static_cast<unsigned long long>(g_save - exe));
    }
    auto* entry = reinterpret_cast<std::uint8_t*>(g_save + kPartyTable + 4 * world);
    // A copy we didn't put there this game came from a save made during co-op (TODO 5.19,
    // two-PC test 2026-10-10: reloads in the same game worked, after a restart the menu crashed
    // at exe+0x36A4B2 because the menu fix only knows worlds patched this game). Repair it: the
    // member it replaced comes back, then it is patched below as usual. The saved entry shows
    // where the replaced member went: HB 00 01 02 12 patched was saved as 00 00 02 01 (Donald,
    // left out of the active party, on the bench), so the bench goes back to empty.
    if (!(g_partyPatchedWorlds & (1u << world)) && entry[0] == kPartyPlayable &&
        (entry[1] == kPartyPlayable) != (entry[2] == kPartyPlayable)) {
        int pos = entry[1] == kPartyPlayable ? 1 : 2;
        std::uint8_t other = entry[3 - pos], bench = entry[3];
        std::uint8_t back = other == kPartyGoofy                              ? kPartyDonald
                            : other == kPartyDonald                           ? kPartyGoofy
                            : (bench == kPartyDonald || bench == kPartyGoofy) ? bench
                                                                              : kPartyDonald;
        Log("puppet: world 0x%02X party entry %02X %02X %02X %02X has a Sora copy from a save made during co-op; "
            "repaired: active %d is %s again%s", world, entry[0], entry[1], entry[2], entry[3], pos,
            back == kPartyDonald ? "Donald" : "Goofy", bench == back ? ", off the bench" : "");
        entry[pos] = back;
        if (bench == back) entry[3] = kPartyEmpty;
    }
    if (g_extraCopy) return;  // EXPERIMENT: the copy is spawned beside the party (solo_copy.cpp)
    if (entry[0] != kPartyPlayable || entry[1] == kPartyPlayable || entry[2] == kPartyPlayable) return;
    int pos = entry[1] == kPartyDonald ? 1 : entry[2] == kPartyDonald ? 2
            : entry[1] == kPartyGoofy  ? 1 : entry[2] == kPartyGoofy  ? 2 : 0;
    if (!pos) {
        std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(entry);
        if (g_partyLogged[world] != value)
            Log("puppet: world 0x%02X party entry is %02X %02X %02X %02X, no active Donald or Goofy; leaving it",
                world, entry[0], entry[1], entry[2], entry[3]);
        g_partyLogged[world] = value;
        return;
    }
    Log("puppet: world 0x%02X party entry %02X %02X %02X %02X -> active %d (%s) = playable copy (from the next "
        "room load). DO NOT SAVE while this runs", world, entry[0], entry[1], entry[2], entry[3], pos,
        entry[pos] == kPartyDonald ? "Donald" : "Goofy");
    g_copyReplaced[world] = entry[pos];
    g_benchBefore[world] = entry[3];
    entry[pos] = kPartyPlayable;
    g_partyPatchedWorlds |= 1u << world;
}

std::uintptr_t Friend1Actor() {
    std::uintptr_t exe = ExeBase();
    auto actor = *reinterpret_cast<const volatile std::uintptr_t*>(exe + kFriend1Actor);
    // Party actors live inside the exe's data section.
    return (actor > exe && actor < exe + 0x3000000) ? actor : 0;
}

bool ShouldDrive() {
    if (g_frame - g_poseFrame > kStaleFrames || !g_pose.hasActor) return false;
    if (g_pose.pos[0] == 0.0f && g_pose.pos[1] == 0.0f && g_pose.pos[2] == 0.0f) return false;  // mid-load
    std::uintptr_t exe = ExeBase();
    std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow);
    std::uint8_t room = *reinterpret_cast<const volatile std::uint8_t*>(exe + kNow + 1);
    return world != 0xFF && world == g_pose.world && room == g_pose.room;
}

void Release() {
    if (g_actor && g_collisionSaved) {
        __try {
            auto* flags = reinterpret_cast<std::uint8_t*>(g_actor + kActorCollision);
            *flags = static_cast<std::uint8_t>((*flags & ~kNoCollide) | (g_savedNoCollide ? kNoCollide : 0));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_actor = 0;
    g_lastMotion = -1;
    g_collisionSaved = false;
}

// Points Donald's object table row at Roxas's model and motion set. Returns
// true once done (or if the table isn't loaded yet / doesn't look right: false).
bool PatchDonaldAsRoxas() {
    std::uintptr_t table = ExeBase() + kObjTable;
    auto version = *reinterpret_cast<const std::uint32_t*>(table);
    auto count = *reinterpret_cast<const std::uint32_t*>(table + 4);
    if (version != 3 || count < 100 || count > 8192) return false;
    char* roxas = nullptr;  // the look to copy: Roxas (P_EX110) or Sora (P_EX100)
    char* donald = nullptr;
    const std::uint32_t lookId = g_lookIsSora ? kObjIdSora : kObjIdRoxas;
    const char* lookModel = g_lookIsSora ? "P_EX100" : "P_EX110";
    for (std::uint32_t i = 0; i < count; ++i) {
        auto row = reinterpret_cast<char*>(table + 8 + i * kObjRow);
        auto id = *reinterpret_cast<const std::uint32_t*>(row);
        if (id == lookId) roxas = row;
        if (id == kObjIdDonald) donald = row;
    }
    if (!roxas || !donald || std::strcmp(roxas + kObjModel, lookModel) != 0) return false;
    if (std::strcmp(donald + kObjModel, "P_EX020") != 0) {
        Log("puppet: Donald's object row has model \"%.32s\", not P_EX020; leaving it alone", donald + kObjModel);
        g_roxasWanted = false;
        return false;
    }
    std::memcpy(donald + kObjModel, roxas + kObjModel, kObjName);
    std::memcpy(donald + kObjMotionSet, roxas + kObjMotionSet, kObjName);
    donald[kObjWeaponJoint] = roxas[kObjWeaponJoint];  // else the keyblade sits on joint 2 = his head
    // Donald's staves are the W_EX020 family (one row per staff, plus world
    // variants _NM/_TR/_WI); show each as the Kingdom Key W_EX010 with the
    // same world variant. Only Donald uses staves.
    int weapons = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        char* model = reinterpret_cast<char*>(table + 8 + i * kObjRow) + kObjModel;
        if (std::strncmp(model, "W_EX020", 7) != 0) continue;
        const char* variant = std::strrchr(model, '_');
        bool world = variant && variant > model + 6 &&
                     (!std::strcmp(variant, "_NM") || !std::strcmp(variant, "_TR") || !std::strcmp(variant, "_WI"));
        char keyblade[kObjName] = {};
        std::snprintf(keyblade, sizeof(keyblade), "W_EX010%s", world ? variant : "");
        std::memcpy(model, keyblade, kObjName);
        ++weapons;
    }
    Log("puppet: %d staff rows now use Kingdom Key models", weapons);
    Log("puppet: object table patched: Donald (id 92) now uses %.32s / %.32s, weapon joint %u, from the next "
        "room load", donald + kObjModel, donald + kObjMotionSet, static_cast<unsigned>(donald[kObjWeaponJoint]));
    return true;
}

bool HasMotion(std::uintptr_t actor, int motion) {
    static int ok = -1;  // build check, once
    if (ok < 0)
        ok = std::memcmp(reinterpret_cast<const void*>(ExeBase() + kFindMotion), kFindMotionBytes,
                         sizeof(kFindMotionBytes)) == 0;
    if (!ok || motion < 0) return false;
    using PFN_FindMotion = int(__fastcall*)(std::uintptr_t, int);
    return reinterpret_cast<PFN_FindMotion>(ExeBase() + kFindMotion)(actor + kActorMotCtrl, motion) >= 0;
}

void DriveMotion(std::uintptr_t actor) {
    // A Limit's or reaction command's motion the peer's game sent (motion_mirror.cpp) plays on while their pose
    // shows one, and a spell's cast motion while their Sora is in it; after it, the pose's motion is set again.
    if (MotionMirrorKeeps(actor, g_pose.otherBank, static_cast<int>(g_pose.motion))) {
        g_lastMotion = -1;
        return;
    }
    std::uint32_t motion = g_pose.motion;
    bool known = false;
    if (g_cloneMode || (g_roxasPatched && g_lookIsSora)) {
        // Sora's own motion set. 9 (item use) crashed a Sora clone in Volpestyle's tests; it only plays
        // once its item trigger is blocked on the copy (HookItemTrigger).
        known = motion != 9 || g_itemTriggerHooked;
    } else if (g_roxasPatched) {
        for (auto m : kRoxasMotions) known = known || m == motion;
    } else {
        known = motion <= kMaxSharedMotion;
    }
    // e.g. the peer's rescue motions 252/253 when this PC's Sora motion set lacks them
    if (known && g_cloneMode && motion > 200 && !HasMotion(actor, static_cast<int>(motion))) known = false;
    // A Limit's or reaction command's motion comes from that file's set; the same id in Sora's set is
    // another motion (Bushido 252/253 = our lying / getting up), so show idle (or falling) instead, when the
    // peer's game didn't send which file it is (above).
    if (g_pose.otherBank) known = false;
    if (!known) motion = g_pose.airborne ? kMotionFall : kMotionIdle;
    if (static_cast<int>(motion) == g_lastMotion) return;
    g_settingMotion = true;
    g_setMotion(reinterpret_cast<void*>(actor + kActorMotCtrl), static_cast<int>(motion), 0.0f, 0.0f);
    g_settingMotion = false;
    Log("puppet: motion %d -> %u (peer %u)", g_lastMotion, motion, g_pose.motion);
    g_lastMotion = static_cast<int>(motion);
}

void ApplyTransform(std::uintptr_t actor, const PeerPose& pose) {
    std::uintptr_t entity = actor + kActorEntity;
    auto* pos = reinterpret_cast<float*>(entity + kEntityPos);
    pos[0] = pose.pos[0];
    pos[1] = pose.pos[1];
    pos[2] = pose.pos[2];
    *reinterpret_cast<float*>(entity + kEntityAngle) = pose.angle;
    *reinterpret_cast<float*>(entity + kEntitySin) = std::sin(pose.angle);
    *reinterpret_cast<float*>(entity + kEntityCos) = std::cos(pose.angle);
    // Leftover movement terms would drag him back toward Sora before our next write.
    std::memset(reinterpret_cast<void*>(actor + kActorVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorAccel), 0, 0x18);
    if (!g_cloneMode) *reinterpret_cast<float*>(actor + kActorFollowTimer) = kFollowTimerHeld;
    if (g_keepCollision) return;
    auto* flags = reinterpret_cast<std::uint8_t*>(actor + kActorCollision);
    if (!g_collisionSaved) {
        g_savedNoCollide = (*flags & kNoCollide) != 0;
        g_collisionSaved = true;
    }
    *flags |= kNoCollide;
}

// Removing a player-class actor (exe+0x3A8F30(actor), and the class method exe+0x3A7120(?, actor)) does the
// player's cleanup whenever the player pointer exe+0x2A105D0 is set, whoever is removed: clears that pointer,
// exe+0x718CB0 and exe+0x2A10420, and tears down +0xDC0, which every player-class actor points at the same
// global (exe+0x2A10620, set by the constructor exe+0x3A7A40). Fading out the copy in a Borough fight thus
// left our Sora without a player and crashed (exe+0x3C380C reads the pointer, bench session 27). For a copy
// we removed, the pointer is hidden during the call, so only the actor's own removal (exe+0x3D6770) runs.
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;
constexpr std::uintptr_t kRemovePlayerA = 0x3A8F30;  // (actor)
constexpr std::uintptr_t kRemovePlayerB = 0x3A7120;  // (?, actor)
constexpr std::uint8_t kRemovePlayerBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x3D};
using PFN_RemovePlayerA = void(__fastcall*)(std::uintptr_t);
using PFN_RemovePlayerB = void(__fastcall*)(void*, std::uintptr_t);
PFN_RemovePlayerA g_realRemovePlayerA = nullptr;
PFN_RemovePlayerB g_realRemovePlayerB = nullptr;
std::uint32_t g_guardedRemovals = 0;

std::uintptr_t HidePlayerFor(std::uintptr_t actor) {
    auto* player = reinterpret_cast<std::uintptr_t*>(ExeBase() + kPlayerPtr);
    std::uintptr_t saved = *player;
    if (!actor || actor != g_removed || !saved || saved == actor) return 0;
    *player = 0;
    return saved;
}

void RestorePlayer(std::uintptr_t saved, std::uintptr_t actor) {
    if (!saved) return;
    auto* player = reinterpret_cast<std::uintptr_t*>(ExeBase() + kPlayerPtr);
    if (*player == 0) *player = saved;
    if (++g_guardedRemovals <= 10)
        Log("puppet: the Sora copy (actor exe+0x%llX) removed without the player's cleanup",
            static_cast<unsigned long long>(actor - ExeBase()));
}

void __fastcall HookRemovePlayerA(std::uintptr_t actor) {
    std::uintptr_t saved = HidePlayerFor(actor);
    g_realRemovePlayerA(actor);
    RestorePlayer(saved, actor);
}

void __fastcall HookRemovePlayerB(void* a, std::uintptr_t actor) {
    std::uintptr_t saved = HidePlayerFor(actor);
    g_realRemovePlayerB(a, actor);
    RestorePlayer(saved, actor);
}

// Freezes a Sora copy idle at the spot where the hold began.
void HoldClone(std::uintptr_t actor) {
    if (g_holdActor != actor) {
        std::uintptr_t entity = actor + kActorEntity;
        auto* pos = reinterpret_cast<const float*>(entity + kEntityPos);
        g_holdPose = PeerPose {0, 0, true, false, {pos[0], pos[1], pos[2]},
                               *reinterpret_cast<const float*>(entity + kEntityAngle), kMotionIdle};
        g_settingMotion = true;
        g_setMotion(reinterpret_cast<void*>(actor + kActorMotCtrl), static_cast<int>(kMotionIdle), 0.0f, 0.0f);
        g_settingMotion = false;
        g_holdActor = actor;
        Log("puppet: Sora copy held idle at (%.0f, %.0f, %.0f)", pos[0], pos[1], pos[2]);
    }
    ApplyTransform(actor, g_holdPose);
}

// With Roxas's motion set, Donald's own AI must never run: idle instead.
void HoldIdle(std::uintptr_t actor) {
    if (g_idledActor == actor) return;
    g_settingMotion = true;
    g_setMotion(reinterpret_cast<void*>(actor + kActorMotCtrl), static_cast<int>(kMotionIdle), 0.0f, 0.0f);
    g_settingMotion = false;
    g_idledActor = actor;
}

void __fastcall HookFriendAi(void* typeHandler, void* actor) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    if (WorldSyncSkipAi(a)) return;  // a companion copying the host's (world_sync.cpp)
    if (!g_active && g_roxasPatched && a == g_heldActor) {
        __try {
            HoldIdle(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return;
    }
    if (g_active && a == g_actor) {
        ++g_aiSkips;
        __try {
            DriveMotion(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception setting motion");
        }
        return;  // the puppet never runs its own AI
    }
    g_realFriendAi(typeHandler, actor);
}

std::uint8_t __fastcall HookMotionChainSet(void* motCtrl, int motion, float start, float blend) {
    // The game re-sets every actor's motion each frame, restarting the clip;
    // for the puppet only our own SetMotion call may go through.
    // Also the cast motion of the other player's spell, cast on the copy (motion_mirror.cpp): the spell's script
    // checks it right away and stops if the copy isn't in it (bench 2026-10-12 22:07).
    auto actor = reinterpret_cast<std::uintptr_t>(motCtrl) - kActorMotCtrl;
    if (!g_settingMotion && !MotionMirrorCasting(actor) &&
        ((g_active && actor == g_actor) || (g_roxasPatched && actor == g_heldActor) ||
         (!g_active && g_cloneMode && actor == g_holdActor) || WorldSyncBlocksMotion(actor, motion) ||
         DownedBlocksMotion(actor)))
        return 1;
    return g_realMotionChainSet(motCtrl, motion, start, blend);
}

const char* WhoIs(std::uintptr_t actor) {
    std::uintptr_t exe = ExeBase();
    if (actor == *reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8)) return "Sora";
    if (actor && actor == g_clone) return "Sora copy";
    if (actor == *reinterpret_cast<const std::uintptr_t*>(exe + kFriend1Actor)) return "companion 1";
    if (actor == *reinterpret_cast<const std::uintptr_t*>(exe + kFriend1Actor + 8)) return "companion 2";
    return "other";
}

// Healing (downed rule, user decision 2026-10-06: HP orbs do nothing while down, a Cure or an
// item gets the player up). The game applies heals through ApplyStatDelta, and the code that
// called it tells them apart (bench, return addresses on the stack): a Cure, like any heal
// "attack", comes through the hit handler exe+0x3D60C0 (returns to exe+0x3D613C), an item through
// exe+0x3C4B80 -> 0x3C0C30 (returns to exe+0x3C4C5E). HP orbs come through neither.
constexpr int kHealOther = 0, kHealCure = 1, kHealItem = 2;
constexpr std::uint32_t kCureReturn = 0x3D613C, kItemReturn = 0x3C4C5E;
// The copy isn't one of the actors a Cure heals, and an item used on it lands on our own Sora (it
// has Sora's character id). So a Cure that heals our Sora with the copy this close, and an item
// used on the copy, are sent to the other player instead. The reach is an estimate (logged).
constexpr float kCureReach = 500.0f;
std::uint64_t g_copyTargetFrame = 0;  // frame a menu command was executed on the copy (0 = none pending)
constexpr std::uint64_t kCopyTargetFrames = 600;  // its item lands ~1-2 s after the choice
// Item use (exe+0x3F6D00, called when the item's effect lands): +4 item id (0 = done), +8 target
// actor, +0x14 target character. Item table: *(exe+0x2A25370), u32 count @+4, 0x18-byte rows from
// +8, u16 id @0, u16 power (% of max HP) @+4 (exe+0x3E39A0, 0x3C4B80). Ids 1 Potion, 2 Hi-Potion,
// 4 Elixir, 5 Mega-Potion, 7 Megalixir heal HP (5/6/7/0x83 the whole party); 3/6 are MP only.
constexpr std::uintptr_t kItemApply = 0x3F6D00;
constexpr std::uint8_t kItemApplyBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x79, 0x04, 0x00, 0x48};
constexpr std::uintptr_t kItemTable = 0x2A25370;
using PFN_ItemApply = void(__fastcall*)(std::uint8_t*);
PFN_ItemApply g_realItemApply = nullptr;
constexpr int kMaxPendingHeals = 8;
int g_pendingHealPct[kMaxPendingHeals], g_pendingHealSource[kMaxPendingHeals];
int g_pendingHeals = 0;

bool CloneAlive();
std::uintptr_t DecodeHandle(std::uint32_t h);

int HealSource() {
    void* frames[12] = {};
    USHORT n = CaptureStackBackTrace(0, 12, frames, nullptr);
    std::uintptr_t exe = ExeBase();
    for (USHORT i = 0; i < n; ++i) {
        auto f = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (f == exe + kCureReturn) return kHealCure;
        if (f == exe + kItemReturn) return kHealItem;
    }
    return kHealOther;
}

const float* ActorPos(std::uintptr_t actor) { return reinterpret_cast<const float*>(actor + kActorEntity + kEntityPos); }

// One Cure can reach the other player twice: by distance (ForwardCure) and by the game's own party heal
// landing on the copy (ForwardCopyHeal); friend log 2026-10-06 22:19:35: "Cure, #1" 100% and "#2" 79% for
// one cast. The distance rule waits one frame and is dropped when the Cure healed the copy itself (the
// game's own amount wins).
int g_cureReachPct = 0;
std::uint64_t g_cureReachFrame = 0, g_copyCureFrame = ~0ull;

void FlushCureReach() {
    if (!g_cureReachPct || g_frame == g_cureReachFrame) return;
    AvatarLinkSendHeal(g_cureReachPct, kHealCure);
    g_cureReachPct = 0;
}

// A Cure healed our Sora: the other player's copy in reach gets the same heal in their game.
void ForwardCure(std::uintptr_t sora, int delta) {
    __try {
        if (!g_active || !CloneAlive()) return;
        const float* a = ActorPos(sora);
        const float* b = ActorPos(g_clone);
        float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        int maxHp = *reinterpret_cast<const int*>(*reinterpret_cast<const std::uintptr_t*>(sora + kActorStats) + 4);
        if (dist > kCureReach) {
            Log("heal: our Cure (+%d) did not reach the other player (%.0f away, reach %.0f)", delta, dist, kCureReach);
            return;
        }
        int pct = maxHp > 0 ? (delta * 100 + maxHp - 1) / maxHp : 100;
        if (g_copyCureFrame == g_frame) {
            Log("heal: our Cure (+%d) reaches the other player (%.0f away); already sent by its heal on the copy", delta, dist);
            return;
        }
        Log("heal: our Cure (+%d) reaches the other player (%.0f away)", delta, dist);
        g_cureReachPct = pct < 100 ? pct : 100;
        g_cureReachFrame = g_frame;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// A heal landed on the copy in our game (see HookApplyStatDelta).
void ForwardCopyHeal(std::uintptr_t copy, int delta, int react) {
    int source = HealSource();
    static int logged = 0;
    if (logged++ < 20) {
        void* frames[10] = {};
        USHORT n = CaptureStackBackTrace(0, 10, frames, nullptr);
        char text[200];
        int len = 0;
        std::uintptr_t exe = ExeBase();
        for (USHORT i = 0; i < n && len < 170; ++i) {
            auto f = reinterpret_cast<std::uintptr_t>(frames[i]);
            if (f > exe && f < exe + 0x3000000)
                len += sprintf_s(text + len, sizeof(text) - len, " %llX", static_cast<unsigned long long>(f - exe));
        }
        text[len] = 0;
        Log("heal: the Sora copy +%d (react %d, %s), exe callers:%s", delta, react,
            source == kHealCure ? "Cure" : source == kHealItem ? "item" : "orb or other", text);
    }
    if (source == kHealOther) return;
    __try {
        int maxHp = *reinterpret_cast<const int*>(*reinterpret_cast<const std::uintptr_t*>(copy + kActorStats) + 4);
        int pct = maxHp > 0 ? (delta * 100 + maxHp - 1) / maxHp : 100;
        Log("heal: a %s on the Sora copy heals the other player%s", source == kHealCure ? "Cure" : "item",
            source == kHealCure && g_cureReachPct ? " (instead of the distance rule)" : "");
        if (source == kHealCure) {
            g_cureReachPct = 0;
            g_copyCureFrame = g_frame;
        }
        AvatarLinkSendHeal(pct < 100 ? pct : 100, source);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// HP heal of an item in % of max HP (0 = not an HP item), with our Sora's item bonus like 0x3F6D00.
int ItemHealPercent(int item) {
    if (item == 0x83) return 100;
    if (item != 1 && item != 2 && item != 4 && item != 5 && item != 7) return 0;
    std::uintptr_t exe = ExeBase();
    auto table = *reinterpret_cast<const std::uintptr_t*>(exe + kItemTable);
    if (!table) return 0;
    int count = *reinterpret_cast<const int*>(table + 4);
    for (int i = 0; i < count && i < 2048; ++i) {
        std::uintptr_t row = table + 8 + static_cast<std::uintptr_t>(i) * 0x18;
        if (*reinterpret_cast<const std::uint16_t*>(row) != item) continue;
        int power = *reinterpret_cast<const std::uint16_t*>(row + 4);
        int rate = 100;
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8);
        if (sora)
            rate += *reinterpret_cast<const std::uint16_t*>(
                *reinterpret_cast<const std::uintptr_t*>(sora + kActorStats) + 0x1D0 + 0x54);
        int pct = (power * rate + 99) / 100;
        return pct < 100 ? pct : 100;
    }
    return 0;
}

// The command menu's execute step exe+0x3B22D0(menu, command record, target) still knows the
// target (a 0x10-byte target info from exe+0x3BEE60: u32 actor handle @0); the item use it leads
// to only keeps the target's character id (Sora's for the copy). So a command executed on the copy
// is remembered until its item lands.
constexpr std::uintptr_t kCmdExecute = 0x3B22D0;
constexpr std::uint8_t kCmdExecuteBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                             0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA};
using PFN_CmdExecute = std::uint64_t(__fastcall*)(std::uintptr_t, const std::uint16_t*, std::uintptr_t);
PFN_CmdExecute g_realCmdExecute = nullptr;

std::uint64_t __fastcall HookCmdExecute(std::uintptr_t menu, const std::uint16_t* command, std::uintptr_t info) {
    std::uint64_t result = g_realCmdExecute(menu, command, info);
    __try {
        std::uintptr_t target = info ? DecodeHandle(*reinterpret_cast<const std::uint32_t*>(info)) : 0;
        bool onCopy = target && target == g_clone;
        static int logged = 0;
        if (onCopy || logged++ < 30)
            Log("menu: command %#x (type %#x) executed on %s -> %s", command[0], command[1],
                target ? WhoIs(target) : "no target", static_cast<char>(result) ? "ok" : "refused");
        g_copyTargetFrame = onCopy && static_cast<char>(result) ? g_frame : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}

void __fastcall HookItemApply(std::uint8_t* use) {
    __try {
        // +8 is the user, +0x14 the target's character (0 = whole party).
        int item = *reinterpret_cast<const int*>(use + 4);
        std::uintptr_t user = *reinterpret_cast<const std::uintptr_t*>(use + 8);
        bool party = item == 5 || item == 6 || item == 7 || item == 0x83;
        bool onCopy = g_copyTargetFrame && g_frame - g_copyTargetFrame < kCopyTargetFrames &&
                      user == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        if (item)
            Log("heal: item %d lands (user %s, target character %d, chosen on the copy %s, copy %s, peer %s)", item,
                user > ExeBase() && user < ExeBase() + 0x3000000 ? WhoIs(user) : "none",
                *reinterpret_cast<const int*>(use + 0x14), onCopy ? "yes" : "no", CloneAlive() ? "alive" : "absent",
                g_active ? "here" : "not here");
        if (item) g_copyTargetFrame = 0;
        if (item && g_active && CloneAlive() && (party || onCopy)) {
            int pct = ItemHealPercent(item);
            if (pct > 0) {
                AvatarLinkSendHeal(pct, kHealItem);
                if (!party) {
                    Log("heal: item %d used on the Sora copy: it heals the other player (%d%%), not our Sora", item, pct);
                    *reinterpret_cast<int*>(use + 4) = 0;  // what the game does once the item is applied
                    return;
                }
                Log("heal: party item %d also heals the other player (%d%%)", item, pct);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_realItemApply(use);
}

// HP change funnel: copy damage forwarding, world sync, the downed rule, heals. Calls through.
// God mode: a hit on a party member (team 1; with enemy god also an enemy, team 2) lands with 1 HP (so the
// reaction plays) and the HP is put back to full. Also used for the other player's forwarded hits and the
// friend's hits on the host's enemies, which don't come through the hook.
bool GodHit(void* actor, int react, int* hp, int damage) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    if ((!g_god && !g_enemyGod) || a == 0) return false;
    __try {
        auto stats = *reinterpret_cast<std::int32_t* const*>(a + kActorStats);
        std::uint32_t team = *reinterpret_cast<const std::uint32_t*>(a + kActorTeam);
        if (!stats || !((team == 1 && g_god) || (team == 2 && g_enemyGod))) return false;
        bool held = g_godSoraHp > 0 && a == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        if (held && (damage >= 9999 || DownedIsDown())) return false;
        g_realApplyStatDelta(actor, stats[0] > 1 ? -1 : 0, 0, react);
        stats[0] = held && g_godSoraHp < stats[1] ? g_godSoraHp : stats[1];  // channel 0: current, max
        *hp = stats[0];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Research trace (KH2COOP_TRACE_STAT2=1, TODO 4.4): the stat change has a twin, exe+0x3D2C50(self, actor, delta,
// channel, flag), slot +0xE8 of YS::BTLOBJ's function table (exe+0x5C6250), with the same body. A hit through it
// would skip HookApplyStatDelta (world sync's claim, god mode). Logs its first calls; changes nothing.
constexpr std::uintptr_t kStatDelta2 = 0x3D2C50;
constexpr std::uint8_t kStatDelta2Bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56,
                                             0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA};
using PFN_StatDelta2 = int(__fastcall*)(void*, void*, int, int, char);
PFN_StatDelta2 g_realStatDelta2 = nullptr;
std::uint32_t g_statDelta2Calls = 0;

int __fastcall HookStatDelta2(void* self, void* actor, int delta, int stat, char flag) {
    if (g_statDelta2Calls++ < 60)
        Log("trace: stat change twin on %s (exe+0x%llX): %+d on channel %d, from exe+0x%llX", WhoIs(reinterpret_cast<std::uintptr_t>(actor)),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(actor) - ExeBase()), delta, stat,
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
    return g_realStatDelta2(self, actor, delta, stat, flag);
}

int __fastcall HookApplyStatDelta(void* actor, int delta, int stat, int react) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    // The other player's spell cast on our copy (motion_mirror.cpp) is only for show: its heals and damage happen
    // in the caster's game (a Cure there reaches us through ForwardCure).
    if (stat == 0 && delta != 0 && MotionMirrorCopySpellRunning()) {
        static int logged = 0;
        if (logged++ < 10) Log("HP change from the Sora copy's spell on %s: %+d dropped (the caster's game makes it)", WhoIs(a), delta);
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    if (stat == 0 && delta > 0 && a != 0 && a == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8)) {
        int source = HealSource();
        static const char* const kWhat[] = {"an HP gain (orb...)", "a Cure", "an item"};
        if (DownedInterceptHeal(a, delta, source != kHealOther, kWhat[source])) {
            __try {
                return *reinterpret_cast<const int*>(*reinterpret_cast<const std::uintptr_t*>(a + kActorStats));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
        }
        static int logged = 0;
        if (logged++ < 40) Log("heal: our Sora +%d from %s", delta, kWhat[source]);
        if (source == kHealCure) ForwardCure(a, delta);
    }
    // Cure cast with the copy as its target heals the copy itself (user test 2026-10-06 22:02:40:
    // GetFriend(1) from exe+0x42FA20 handed it the copy, "Sora copy +44"; the effect shows on its
    // Keyblade). A Cure or item heal on the copy is meant for the other player: sent to them.
    // HP orbs the copy picks up stay here (each player collects their own).
    if (stat == 0 && delta > 0 && a != 0 && a == g_clone && g_active) ForwardCopyHeal(a, delta, react);
    if (g_forwardHits && stat == 0 && delta < 0 && a != 0 && a == g_clone) {
        int hp = -1;
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            hp = stats ? *reinterpret_cast<const int*>(stats) : -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        // Friend side with the host's world live: our enemies are mirrors of the host's, and the
        // host's own game already applies their hits to the host (bench 2026-10-06 18:27: forwarding
        // these too hit the host twice for every attack).
        bool forward = g_active && !WorldSyncHostPresent();
        if (forward) AvatarLinkSendHit(-delta, react);
        Log("HP change on the Sora copy blocked: %+d (our HP stays %d)%s", delta, hp,
            forward ? ", forwarded to its owner" : g_active ? ", not forwarded (the host's game owns its hits)"
                                                            : ", no peer to forward to");
        return hp;  // as if the change applied with no effect
    }
    if (stat == 0 && delta < 0 && WorldSyncBlocksDamage(a, -delta, react)) {
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    if (stat == 0 && delta < 0) {
        int hp = 0;
        if (GodHit(actor, react, &hp, -delta)) return hp;
    }
    if (stat == 0 && delta < 0) {
        int hp = 0;
        if (DownedInterceptDamage(a, delta, react, &hp)) return hp;
    }
    int result = g_realApplyStatDelta(actor, delta, stat, react);
    if (stat == 0 && delta != 0 && g_statLogs < 300) {
        ++g_statLogs;
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            int hp = stats ? *reinterpret_cast<const int*>(stats) : -1;
            Log("HP change: %s (exe+0x%llX, team %u) %+d -> %d (react %d)", WhoIs(a),
                static_cast<unsigned long long>(a - ExeBase()), *reinterpret_cast<const std::uint32_t*>(a + kActorTeam),
                delta, hp, react);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return result;
}

CallerStat* Caller(std::uint32_t rva) {
    for (int i = 0; i < g_callerCount; ++i)
        if (g_callers[i].rva == rva) return &g_callers[i];
    if (g_callerCount == 64) return nullptr;
    g_callers[g_callerCount] = {rva, 0, 0};
    return &g_callers[g_callerCount++];
}

bool CloneAllowedFor(std::uint32_t rva) {
    for (auto r : kNoCloneCallers)
        if (rva >= r.lo && rva < r.hi) return false;
    for (auto c : kNoCloneExactCallers)
        if (rva == c) return false;
    for (int i = 0; i < g_skipCount; ++i)
        if (g_skipCallers[i] == rva) return false;
    return true;
}

// The copy is only handed out while it is alive: updated by the game in the
// last two frames, in this room, with a live handle at actor+8 (the game's
// character-id lookup exe+0x3B5B20 resolves it). During a room change the old
// copy's handle is 0 and that lookup crashed (2026-10-04, exe+0x3B5B2C).
std::uintptr_t DecodeHandle(std::uint32_t h);

// actor+8 is a handle (compressed pointer) to the actor's character record
// (GetCharId exe+0x3B5B20 reads its id). During a room change the copy's old
// memory is reused while we may still hold it: 15:14 crash in GetCharId
// (exe+0x3B5B2C) with actor+8 = 0x01DC649D, not a valid handle (no bit 31).
bool CloneAlive() {
    if (!g_clone || g_clone == g_removed || g_frame - g_cloneFrame > 2) return false;
    if (*reinterpret_cast<const volatile std::uint16_t*>(ExeBase() + kNow) != g_cloneRoom) return false;
    if (*reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0) return false;
    return DecodeHandle(*reinterpret_cast<const volatile std::uint32_t*>(g_clone + 8)) != 0 && IsPlayerClass(g_clone);
}

std::uintptr_t __fastcall HookGetFriend(int index) {
    std::uintptr_t friendActor = g_realGetFriend(index);
    if (friendActor || index != 1 || !CloneAlive()) return friendActor;
    auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase());
    bool give = CloneAllowedFor(rva);
    if (CallerStat* c = Caller(rva)) {
        if ((give ? c->gave : c->refused) == 0)
            Log("party: GetFriend(1) from exe+0x%X: %s", rva, give ? "gave it the Sora copy" : "left empty (excluded)");
        ++(give ? c->gave : c->refused);
    }
    return give ? g_clone : 0;
}

// Replaces exe+0x2FC6D0 (no trampoline: it's 8 lines, re-implemented here).
int __fastcall HookMenuPartyId(const std::int32_t* list, int index) {
    if (index < 0 || index >= list[0]) return 0;
    int id = *reinterpret_cast<const std::int16_t*>(reinterpret_cast<const std::uint8_t*>(list) + index * 0x20 + 0xA);
    if (index >= 1 && id == kCharSora && g_partyPatchedWorlds) {
        std::uint8_t world = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
        bool goofy = world < 32 && g_copyReplaced[world] == kPartyGoofy;
        if (g_menuSwaps++ < 20)
            Log("menu: party entry %d is Sora (the copy); reported as %s (caller exe+0x%llX)", index,
                goofy ? "Goofy" : "Donald",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
        return goofy ? kCharGoofy : kCharDonald;
    }
    return id;
}

void AddScanHit(std::uint16_t parentOff, std::uint16_t off, std::uint8_t what, std::int32_t inner) {
    for (int i = 0; i < g_scanCount; ++i) {
        ScanHit& h = g_scan[i];
        if (h.parentOff == parentOff && h.off == off && h.what == what && h.inner == inner) {
            ++h.count;
            return;
        }
    }
    if (g_scanCount < 256) g_scan[g_scanCount++] = {parentOff, off, what, 1, inner};
}

// Game "handles" are compressed pointers (exe+0x4AD390 encodes, 0x4AD3F0
// decodes): bit 31 set, bits 25..30 index a table of 32 MB-aligned bases at
// exe+0x2B0D720, bits 0..24 are the offset. Any object, or any address inside
// one, can be stored this way.
constexpr std::uintptr_t kHandleBases = 0x2B0D720;  // u64[64]

std::uintptr_t DecodeHandle(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = reinterpret_cast<const std::uintptr_t*>(ExeBase() + kHandleBases)[(h >> 25) & 0x3F];
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

void ScanBlock(std::uintptr_t base, std::uint32_t bytes, std::uint16_t parentOff, std::uintptr_t sora,
               std::uintptr_t friend0, std::uint32_t soraHandle, std::uint32_t friendHandle, std::uintptr_t* children,
               int* childCount) {
    for (std::uint32_t off = 0; off < bytes; off += 4) {
        std::uint32_t v32 = *reinterpret_cast<const std::uint32_t*>(base + off) & 0x7FFFFFFF;
        if (soraHandle && v32 == soraHandle) AddScanHit(parentOff, static_cast<std::uint16_t>(off), 2, 0);
        if (friendHandle && v32 == friendHandle) AddScanHit(parentOff, static_cast<std::uint16_t>(off), 3, 0);
        if (std::uintptr_t p = DecodeHandle(*reinterpret_cast<const std::uint32_t*>(base + off))) {
            if (p >= sora && p < sora + kScanBytes)
                AddScanHit(parentOff, static_cast<std::uint16_t>(off), 4, static_cast<std::int32_t>(p - sora));
            else if (friend0 && p >= friend0 && p < friend0 + kScanBytes)
                AddScanHit(parentOff, static_cast<std::uint16_t>(off), 5, static_cast<std::int32_t>(p - friend0));
        }
        if (off % 8) continue;
        std::uintptr_t v = *reinterpret_cast<const std::uintptr_t*>(base + off);
        if (v >= sora && v < sora + kScanBytes)
            AddScanHit(parentOff, static_cast<std::uint16_t>(off), 0, static_cast<std::int32_t>(v - sora));
        else if (friend0 && v >= friend0 && v < friend0 + kScanBytes)
            AddScanHit(parentOff, static_cast<std::uint16_t>(off), 1, static_cast<std::int32_t>(v - friend0));
        else if (children && *childCount < 64 && v > 0x10000 && v < 0x7FFFFFFF0000 && (v & 7) == 0 &&
                 (v < base || v >= base + bytes))
            children[(*childCount)++] = v | (static_cast<std::uintptr_t>(off) << 48);  // remember the offset
    }
}

// True if [address, address+bytes) is committed, readable memory without a
// guard page. Reading a guard page with __try/__except would silently remove
// the guard a thread's stack needs to grow, and that thread later dies of a
// "stack overflow" (seen 2026-10-05: crash in clr.dll ~7 s after scan v2 met
// its first enemies). So arbitrary pointers are checked, never just tried.
bool Readable(std::uintptr_t address, std::uint32_t bytes) {
    std::uintptr_t end = address + bytes;
    while (address < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) return false;
        constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                    PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (mbi.State != MEM_COMMIT || !(mbi.Protect & kReadable) || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        address = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}
std::uint32_t g_scanSkipped = 0;  // child pointers not read (unreadable or guard page)

void ScanEnemy(std::uintptr_t enemy) {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8);
    std::uintptr_t friend0 = *reinterpret_cast<const std::uintptr_t*>(exe + kFriendSlots);
    if (!sora) return;
    std::uint32_t soraHandle = *reinterpret_cast<const std::uint32_t*>(sora + 8) & 0x7FFFFFFF;
    std::uint32_t friendHandle = friend0 ? *reinterpret_cast<const std::uint32_t*>(friend0 + 8) & 0x7FFFFFFF : 0;
    if (!Readable(enemy, kScanBytes)) return;
    ++g_scannedEnemies;
    std::uintptr_t children[64];
    int childCount = 0;
    ScanBlock(enemy, kScanBytes, 0xFFFF, sora, friend0, soraHandle, friendHandle, children, &childCount);
    for (int i = 0; i < childCount; ++i) {
        auto off = static_cast<std::uint16_t>(children[i] >> 48);
        std::uintptr_t child = children[i] & 0xFFFFFFFFFFFF;
        if (child >= sora && child < sora + kScanBytes) continue;
        if (!Readable(child, kScanChildBytes)) {
            ++g_scanSkipped;
            continue;
        }
        __try {
            ScanBlock(child, kScanChildBytes, off, sora, friend0, soraHandle, friendHandle, nullptr, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void LogScan() {
    Log("target scan: %u enemy scans (%u child pointers skipped as unreadable); places holding Sora / companion "
        "(count = scans where seen):", g_scannedEnemies, g_scanSkipped);
    g_scanSkipped = 0;
    static const char* kWhat[] = {"Sora ptr", "companion ptr", "Sora handle", "companion handle", "Sora cptr",
                                  "companion cptr"};
    for (int n = 0; n < 30; ++n) {
        int best = -1;
        for (int i = 0; i < g_scanCount; ++i)
            if (g_scan[i].count && (best < 0 || g_scan[i].count > g_scan[best].count)) best = i;
        if (best < 0) break;
        const ScanHit& h = g_scan[best];
        if (h.parentOff == 0xFFFF)
            Log("  enemy+0x%03X: %s (+0x%X) x%u", h.off, kWhat[h.what], h.inner, h.count);
        else
            Log("  *(enemy+0x%03X)+0x%03X: %s (+0x%X) x%u", h.parentOff, h.off, kWhat[h.what], h.inner, h.count);
        g_scan[best].count = 0;
    }
    g_scanCount = 0;
    g_scannedEnemies = 0;
}

void LogCallerStats() {
    for (int i = 0; i < g_callerCount; ++i)
        Log("  caller exe+0x%X: copy given %u, refused %u", g_callers[i].rva, g_callers[i].gave, g_callers[i].refused);
}

void LogPartyFlags() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t who[] = {*reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8), g_clone,
                            *reinterpret_cast<const std::uintptr_t*>(exe + kFriend1Actor),
                            *reinterpret_cast<const std::uintptr_t*>(exe + kFriend1Actor + 8)};
    const char* names[] = {"Sora", "Sora copy", "companion 1", "companion 2"};
    for (int i = 0; i < 4; ++i) {
        if (!who[i]) continue;
        Log("  %s exe+0x%llX: team %u, collision flags 0x%02X", names[i],
            static_cast<unsigned long long>(who[i] - exe), *reinterpret_cast<const std::uint32_t*>(who[i] + kActorTeam),
            *reinterpret_cast<const std::uint8_t*>(who[i] + kActorCollision));
    }
}

// The Sora copy is a full player actor, and every player actor's update runs the shared field
// command menu (exe+0x2A10620, each player's +0xDD0) through exe+0x3FD690 -> 0x3B2340 (input,
// cursor, timers). With the copy that ran twice per frame (logged: once in Sora's update, once in
// the copy's, same callers), so one press of down moved two places (Magic and Fusion looked
// skipped) and cross also picked the submenu's first entry (bench 2026-10-06; the game-spawned
// copy did it with our code off, and the menu's own states said all four commands were enabled).
// Only Sora's update runs it now.
constexpr std::uintptr_t kCmdMenuUpdate = 0x3FD690;
constexpr std::uint8_t kCmdMenuUpdateBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0xB9, 0x38, 0x0C, 0x00};
using PFN_Void1 = void(__fastcall*)(void*);
PFN_Void1 g_realCmdMenuUpdate = nullptr;
std::uintptr_t g_updatingActor = 0;  // actor inside PerEntityUpdate right now
std::uint32_t g_menuUpdatesSkipped = 0;

bool CopyIsUpdating() {
    __try {
        std::uintptr_t a = g_updatingActor;
        return a && a != *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8) && IsPlayerClass(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookCmdMenuUpdate(void* menu) {
    if (CopyIsUpdating()) {
        if (g_menuUpdatesSkipped++ == 0) Log("puppet: the Sora copy's update no longer runs the command menu");
        return;
    }
    g_realCmdMenuUpdate(menu);
}

// The same sharing reaches exe+0x405440 (actor, command record {u16 id, u16 type}): a player
// actor carries out a picked command (type 0x51/0x2C3 = start a Limit with the limit id at
// *(actor+0xDD0)+0xA through exe+0x3D8B40; also 4, 5, 0x48, 0x49, 0x13C, else exe+0x3A8B10).
// It ran for Sora AND the copy: one Bushido press started the Limit twice, the copy's start
// replaced Sora's in the game's current-Limit pointer (exe+0x2A24CC0, user at +0xC), so Auron
// and the copy played the Limit at the copy's spot while Sora stood in motion 252 for good
// (two-PC test 2026-10-06 22:21, bench 22:41 and 22:48: "GetFriend(1) from exe+0x3D8BFC" twice
// per press). The copy only shows the other player: it never carries out our commands.
constexpr std::uintptr_t kPlayerCommand = 0x405440;
constexpr std::uint8_t kPlayerCommandBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x30};
using PFN_PlayerCommand = void(__fastcall*)(void* actor, const std::uint16_t* command);
PFN_PlayerCommand g_realPlayerCommand = nullptr;
std::uint32_t g_playerCommandLogs = 0, g_copyCommandsSkipped = 0;
bool g_limitCommand = false;  // our Sora is carrying out a Limit command right now (world_sync.cpp)

bool IsTheCopy(std::uintptr_t a) {
    __try {
        return a && a != *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8) && IsPlayerClass(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookPlayerCommand(void* actor, const std::uint16_t* command) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    bool copy = IsTheCopy(a);
    if (g_playerCommandLogs < 30) {
        ++g_playerCommandLogs;
        Log("command: %s carries out command 0x%X (type 0x%X)%s", copy ? "the Sora copy" : WhoIs(a), command[0],
            command[1], copy ? ": skipped (the copy never acts on our input)" : "");
    }
    if (copy) {
        ++g_copyCommandsSkipped;
        return;
    }
    bool limit = command[1] == 0x51 || command[1] == 0x2C3;  // the Limit's start and follow-ups
    if (limit) g_limitCommand = true;
    g_realPlayerCommand(actor, command);
    g_limitCommand = false;
}

// The other consumer of the picked command: exe+0x3A89A0 (each player actor's movement update) runs the shared
// command record on that actor with exe+0x3D6FC0(actor, record) -> pointer to the command's table row or 0. Type 2
// (a spell) casts it there (exe+0x3A9DD0 -> exe+0x3C5170(actor, spell, ..), which starts the spell's script on
// the actor), type 3 (an item) puts the actor in the item motion, the rest go on to exe+0x405440. It ran for the
// copy too: each spell the host cast started on the copy as well (its first effect at the copy's Keyblade tip,
// user 2026-10-12), and the copy's spell script then waited for a cast motion it never played (8 stuck threads
// in a minute; bench session 29, 'trap_pax_start_bind(..., P_EX100 copy)' at the same frame as Sora's). On the
// copy it does nothing: 0, as when the game turns a command down.
constexpr std::uintptr_t kRunCommand = 0x3D6FC0;
constexpr std::uint8_t kRunCommandBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30};
using PFN_RunCommand = std::uintptr_t(__fastcall*)(void* actor, const std::uint16_t* record);
PFN_RunCommand g_realRunCommand = nullptr;
std::uint32_t g_copyRunsSkipped = 0;

std::uintptr_t __fastcall HookRunCommand(void* actor, const std::uint16_t* record) {
    if (IsTheCopy(reinterpret_cast<std::uintptr_t>(actor))) {
        if (record && record[0] && g_copyRunsSkipped++ < 10)
            Log("command: the Sora copy would run command 0x%X: skipped (the copy never acts on our input)", record[0]);
        return 0;
    }
    return g_realRunCommand(actor, record);
}

// exe+0x404A40(?, actor) is a player-class per-frame method: exe+0x3A89A0(actor), then the summon/form checks:
// with a summon out (exe+0x3DE250() = state at exe+0x2A24EDC == 2) and the actor's stat +0x1B4 at 0, it asks for
// command 0x13C and starts the summon's leave (exe+0x3DE010 -> exe+0x3DE080 -> RestoreParty exe+0x400440).
// The extra copy ran it too and dismissed every summon at once: Donald and Goofy were rebuilt while the summon
// came in and the host crashed at exe+0x1B424F (their formation index +0xE0C unset; bench 2026-10-12 07:24 and
// 07:28, Chicken Little; the same summon without the copy worked). On the copy the summon part is skipped.
constexpr std::uintptr_t kPlayerTick = 0x404A40;
constexpr std::uint8_t kPlayerTickBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xCA, 0x48, 0x8B, 0xDA};
constexpr std::uintptr_t kPlayerTickBase = 0x3A89A0;  // its first call (actor)
constexpr std::uintptr_t kSummonState = 0x2A24EDC;    // u32, 2 = a summon is out
using PFN_PlayerTick = void(__fastcall*)(void* self, void* actor);
PFN_PlayerTick g_realPlayerTick = nullptr;
std::uint32_t g_copySummonChecksSkipped = 0;

void __fastcall HookPlayerTick(void* self, void* actor) {
    if (IsTheCopy(reinterpret_cast<std::uintptr_t>(actor)) &&
        *reinterpret_cast<const volatile std::uint32_t*>(ExeBase() + kSummonState) == 2) {
        if (g_copySummonChecksSkipped++ == 0)
            Log("command: a summon is out: the Sora copy doesn't run the summon checks (it would dismiss it)");
        reinterpret_cast<PFN_Void1>(ExeBase() + kPlayerTickBase)(actor);
        return;
    }
    g_realPlayerTick(self, actor);
}

// TODO 1.23 Anti Form (Ghidra, session 32): the Drive command (exe+0x405440, command type 4) picks the form through
// exe+0x421D60(player, form) before switching (exe+0x40CE60; current form = save+0x3524, 6 = Anti). For Valor, Wisdom,
// Limit or Master it adds 1 Anti point (the Anti Form record's EXP field, save+0x32F4 + 5 * 0x38 + 4), takes the
// chance from the points (table exe+0x2AE5708, key 0x61) times the mission's Anti multiplier (msn byte 0x0E,
// exe+0x3A3950; 1 without a mission), or 100 when the player's stat +0x1E4 has bit 15 set, rolls 0-99, and goes Anti
// only in battle (exe+0x3ABB20 = u32 exe+0x2A11404 != 0) with a party that allows it (exe+0x400740(6) == 0); then
// -4 points (Final Form: -10). Logged for every Drive; KH2COOP_TEST_ANTI=1 (bench) sets that bit during the roll,
// so Anti comes whenever the game's other conditions allow it.
constexpr std::uintptr_t kFormRoll = 0x421D60;
constexpr std::uint8_t kFormRollBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                           0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kAntiPoints = 0x9ACCC0;      // i32: save (exe+0x9A98B0) + 0x3410
constexpr std::uintptr_t kAntiInBattle = 0x2A11404;   // u32, != 0 = Anti allowed
constexpr std::uintptr_t kFormPartyCheck = 0x400740;  // int(form): 0 = the party allows it
constexpr std::uintptr_t kStatAntiFlags = 0x1E4;      // u32 in the stat block, bit 15 = Anti chance 100
using PFN_FormRoll = int(__fastcall*)(std::uintptr_t player, int form);
PFN_FormRoll g_realFormRoll = nullptr;
bool g_testAnti = false;

int __fastcall HookFormRoll(std::uintptr_t player, int form) {
    int before = -1, party = -1;
    std::uint32_t battle = 0;
    std::uint32_t* flags = nullptr;
    bool forced = false;
    __try {
        before = *reinterpret_cast<const int*>(ExeBase() + kAntiPoints);
        battle = *reinterpret_cast<const std::uint32_t*>(ExeBase() + kAntiInBattle);
        party = reinterpret_cast<int(__fastcall*)(int)>(ExeBase() + kFormPartyCheck)(6);
        auto stats = *reinterpret_cast<const std::uintptr_t*>(player + kActorStats);
        if (stats) flags = reinterpret_cast<std::uint32_t*>(stats + kStatAntiFlags);
        if (g_testAnti && flags && form >= 1 && form <= 4 && !(*flags & 0x8000)) {
            *flags |= 0x8000;
            forced = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    int result = g_realFormRoll(player, form);
    int after = -1;
    __try {
        if (forced) *flags &= ~0x8000u;
        after = *reinterpret_cast<const int*>(ExeBase() + kAntiPoints);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("forms: Drive into form %d -> %d%s (Anti points %d -> %d, in battle %u, party check %d%s)", form, result,
        result == 6 ? " = ANTI FORM" : "", before, after, battle, party,
        forced ? ", TEST: chance 100" : "");
    return result;
}

// Sora's item motion (9) uses the item at one of its frame triggers: exe+0x40DD70 -> exe+0x3F6790(actor)
// reads the actor's pending item (+0xDA8 item id, +0xDAA, +0xDAC target) and builds the item-use record
// (only caller: exe+0x40DFEA). The copy only shows the other player's item use (their game applies it), so
// on the copy this does nothing, and motion 9 can play on it (it was left out because "9 crashed a Sora
// clone in Volpestyle's tests"; user 2026-10-06: the friend's item use didn't show on the copy).
constexpr std::uintptr_t kItemTrigger = 0x3F6790;
constexpr std::uint8_t kItemTriggerBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x0F, 0xB7, 0x81, 0xAA, 0x0D};
PFN_Void1 g_realItemTrigger = nullptr;
std::uint32_t g_copyItemTriggers = 0;

// Same for a companion the friend's game mirrors from the host (world_sync.cpp): it plays the host's motions
// with its own AI off, so its pending item is stale. User test 2026-10-06 23:15:32: the host's Auron used a
// potion, the friend's Auron played the item motion, its record named an item with no table row (exe+0x3E39A0
// -> null) and the friend's game crashed at exe+0x3F6C94. The host's game applies (and forwards) those heals.
//
// Anyone else's pending item goes through only when it has a row in the item table: the record waits 30 frames,
// then exe+0x3F6B30 reads the row (exe+0x3E39A0) unchecked (crash at exe+0x3F6C2C for one target, exe+0x3F6C94
// for the whole party; bench 2026-10-13 with KH2COOP_TEST_BAD_ITEM=1 KH2COOP_ITEM_GUARD=0). The check before it
// (exe+0x3D7E50) only takes the item from the user's item slots when actor+0xDAA != 0 and actor+0x6C8 bit 22 is
// clear. TODO 1.46 (bench 2026-10-10, session 21, the same crash): a companion world sync had just handed back to
// its own AI (0.5 s before = the record's wait), likely still in a mirrored item motion with the stale item. Same lookup as exe+0x3E39A0 (item table: see kItemTable).
bool ItemHasRow(std::uint16_t id) {
    __try {
        auto table = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kItemTable);
        if (!table) return false;
        int count = *reinterpret_cast<const int*>(table + 4);
        for (int i = 0; i < count && i < 4096; ++i)
            if (*reinterpret_cast<const std::uint16_t*>(table + 8 + static_cast<std::uintptr_t>(i) * 0x18) == id)
                return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

std::uint32_t g_badItemTriggers = 0;
// Test settings (bench only): KH2COOP_TEST_BAD_ITEM=1 gives our Sora's next item use an item with no row, once;
// KH2COOP_ITEM_GUARD=0 lets it through (to see the crash the guard prevents).
bool g_testBadItem = false, g_itemGuard = true;
// Research setting (KH2COOP_TEST_COPY_TEAM=N, TODO 1.30 effect pool): the copy's team held at N every frame
// (0 = untouchable lead), to see whether enemy hits on it are what fills the effect pool in Final Xemnas.
int g_testCopyTeam = -1;

void __fastcall HookItemTrigger(void* actor) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    bool copy = IsTheCopy(a);
    if (copy || WorldSyncSkipAi(a)) {
        if (g_copyItemTriggers++ < 10)
            Log("puppet: %s item motion reached its item trigger: skipped", copy ? "the Sora copy's" : "a mirrored companion's");
        return;
    }
    if (g_testBadItem && a == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8)) {
        g_testBadItem = false;
        Log("puppet: TEST: our Sora's item use gets item 0x7FFF (no row), +0xDAA 0 (no item-slot check)");
        *reinterpret_cast<std::uint16_t*>(a + 0xDA8) = 0x7FFF;
        *reinterpret_cast<std::uint16_t*>(a + 0xDAA) = 0;  // exe+0x3D7E50 lets it through without the slot check
    }
    std::uint16_t item = *reinterpret_cast<const std::uint16_t*>(a + 0xDA8);
    if (g_itemGuard && !ItemHasRow(item)) {
        if (g_badItemTriggers++ < 10)
            Log("puppet: actor exe+0x%llX (%s) reached its item trigger with item %u, which has no item table row: "
                "skipped (it would crash at exe+0x3F6C2C / 0x3F6C94)",
                static_cast<unsigned long long>(a - ExeBase()), WhoIs(a), item);
        return;
    }
    g_realItemTrigger(actor);
}

// A spell's cast motion (Blizzard 59...) has a frame trigger (type 0x11 in exe+0x40DD70) where the spell goes
// out: exe+0x40E1AF reads the actor's cast record *(actor+0xDA0) and, when set, calls exe+0x3C6150(record),
// which looks up the record's object handles (+0, +4) and uses the second one unchecked. The copy's record is
// left over (from the spells it used to start with our commands, HookRunCommand) and can name an object that's gone:
// friend crash 2026-10-07 20:53:02 at exe+0x3C6161 (lookup -1) when the host's Blizzard played on the copy.
// The real spell runs in its owner's game, so on the copy (and a companion mirroring the host's) this step
// does nothing. Anyone else's record goes through only when the handle the game uses unchecked is live.
constexpr std::uintptr_t kSpellTrigger = 0x3C6150;
constexpr std::uint8_t kSpellTriggerBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x8B, 0x49, 0x04};
PFN_Void1 g_realSpellTrigger = nullptr;
std::uint32_t g_copySpellTriggers = 0, g_badSpellRecords = 0;

// The game's handle lookup (exe+0x4AD270 -> 0x4AD3F0) ignores bit 31: 0 -> null, else base table slot.
bool GameHandleLive(std::uint32_t h) {
    if (h == 0) return false;
    std::uintptr_t base = reinterpret_cast<const std::uintptr_t*>(ExeBase() + kHandleBases)[(h & 0x7FFFFFFF) >> 25];
    return base != 0 && base != ~std::uintptr_t(0);
}

void __fastcall HookSpellTrigger(void* record) {
    std::uintptr_t a = g_updatingActor;
    bool copy = IsTheCopy(a);
    // The other player's spell, cast on the copy by motion_mirror.cpp: it goes out once, while its script runs.
    if (copy && MotionMirrorCastTrigger(a, reinterpret_cast<std::uintptr_t>(record))) {
        g_realSpellTrigger(record);
        return;
    }
    if (copy || WorldSyncSkipAi(a)) {
        if (g_copySpellTriggers++ < 10)
            Log("puppet: %s spell reached its cast trigger: skipped", copy ? "the Sora copy's" : "a mirrored companion's");
        return;
    }
    __try {
        std::uint32_t second = reinterpret_cast<const std::uint32_t*>(record)[1];
        if (!GameHandleLive(second)) {
            if (g_badSpellRecords++ < 10)
                Log("puppet: %s's spell record names a missing object (handle 0x%08X): cast trigger skipped",
                    WhoIs(a), second);
            return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    g_realSpellTrigger(record);
}

// Object table type (kObjType) of object `id`, or -1 if it has no row.
int ObjectType(std::uint32_t id) {
    std::uintptr_t table = ExeBase() + kObjTable;
    auto count = *reinterpret_cast<const std::uint32_t*>(table + 4);
    if (*reinterpret_cast<const std::uint32_t*>(table) != 3 || count > 8192) return -1;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uintptr_t row = table + 8 + i * kObjRow;
        if (*reinterpret_cast<const std::uint32_t*>(row) == id) return *reinterpret_cast<const std::uint8_t*>(row + kObjType);
    }
    return -1;
}

// exe+0x3C2FC0(object id, ...) builds an AI party member (its ctor exe+0x1B06E0 picks the AI by the member's
// kind; a player-class object has none, and the ctor then reads a null AI object at exe+0x1CCCF6). When
// Mickey's rescue ends, exe+0x400440 rebuilds the party members from the party table, where one entry is
// our playable copy (PatchPartyForClone): host crash 2026-10-07 21:04:32, 4 s after the Mickey revive.
// exe+0x4008B0 rebuilds one member the same way. Both handle "no actor", so from those a player-class
// object isn't built: the copy, which stays in the room through the rescue, carries on as it is.
constexpr std::uintptr_t kBuildMember = 0x3C2FC0;
constexpr std::uint8_t kBuildMemberBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x48, 0x8D, 0x68, 0xA9};
constexpr std::uint32_t kRebuildReturns[] = {0x400551, 0x40093F};  // after the calls in 0x400440, 0x4008B0
using PFN_BuildMember = std::uintptr_t(__fastcall*)(int objectId, void* where, float a, float b, float c);
PFN_BuildMember g_realBuildMember = nullptr;
std::uint32_t g_buildLogs = 0;

std::uintptr_t __fastcall HookBuildMember(int objectId, void* where, float a, float b, float c) {
    auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase());
    bool rebuild = rva == kRebuildReturns[0] || rva == kRebuildReturns[1];
    int type = -1;
    __try {
        type = ObjectType(static_cast<std::uint32_t>(objectId) & 0xFFFF);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    bool skip = rebuild && type == 0;
    if (skip || g_buildLogs < 20) {
        ++g_buildLogs;
        Log("party: build member object %d (type %d) from exe+0x%X%s", objectId & 0xFFFF, type, rva,
            skip ? ": a player-class member (our copy's party entry): not built" : "");
    }
    if (skip) return 0;
    return g_realBuildMember(objectId, where, a, b, c);
}

// Called with the current player (list head) while in a room: remembers the previous one.
void NotePlayer(std::uintptr_t sora) {
    if (sora == g_lastPlayer) return;
    if (g_lastPlayer) {
        bool known = false;
        for (int i = 0; i < g_exPlayerCount; ++i) known = known || g_exPlayers[i] == g_lastPlayer;
        if (!known && g_exPlayerCount < kMaxExPlayers) g_exPlayers[g_exPlayerCount++] = g_lastPlayer;
        static int logged = 0;
        if (logged++ < 50)
            Log("puppet: the player switched actors (exe+0x%llX -> exe+0x%llX, Drive Form or Mickey?): the old one "
                "can't be the Sora copy",
                static_cast<unsigned long long>(g_lastPlayer - ExeBase()),
                static_cast<unsigned long long>(sora - ExeBase()));
    }
    g_lastPlayer = sora;
}

bool WasPlayer(std::uintptr_t a) {
    for (int i = 0; i < g_exPlayerCount; ++i)
        if (g_exPlayers[i] == a) return true;
    return false;
}

void __fastcall HookPerEntityUpdate(void* actor) {
    std::uintptr_t outer = g_updatingActor;
    g_updatingActor = reinterpret_cast<std::uintptr_t>(actor);
    // An enemy far from our Sora and closer to the copy sees the copy as the player (aggro.cpp, TODO 1.4).
    auto player = reinterpret_cast<std::uintptr_t*>(ExeBase() + 0x2A105D0);
    std::uintptr_t swapTo = AggroPlayerSwapFor(reinterpret_cast<std::uintptr_t>(actor)), swapped = 0;
    bool outerSwap = g_swapActive;
    if (swapTo) {
        swapped = *player;
        *player = swapTo;
        g_swapActive = true;
    }
    __try {
        SpawnStepBeforeUpdate(reinterpret_cast<std::uintptr_t>(actor));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_realPerEntityUpdate(actor);
    g_swapActive = outerSwap;
    if (swapTo && *player == swapTo) *player = swapped;
    g_updatingActor = outer;
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    ++g_updatesThisFrame;
    if (g_cloneMode) {
        __try {
            std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
            bool inField = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) != 0;
            if (inField && sora) NotePlayer(sora);
            bool candidate = a != sora && inField && IsPlayerClass(a);
            if (candidate && IsSceneActor(a)) {
                static std::uintptr_t logged[16];  // each one logged once
                bool known = false;
                for (std::uint32_t i = 0; i < g_sceneActorLogs; ++i) known = known || logged[i] == a;
                if (!known && g_sceneActorLogs < 16) {
                    logged[g_sceneActorLogs++] = a;
                    Log("puppet: actor exe+0x%llX is a cutscene actor: not the copy",
                        static_cast<unsigned long long>(a - ExeBase()));
                }
                candidate = false;
            }
            if (candidate && WasPlayer(a)) {
                static std::uintptr_t lastSkipped = 0;
                if (a != lastSkipped)
                    Log("puppet: actor exe+0x%llX was the player earlier in this room (old form body?): not the copy",
                        static_cast<unsigned long long>(a - ExeBase()));
                lastSkipped = a;
                candidate = false;
            }
            if (candidate) {
                // The copy found first stays the copy while the game still updates it; any other
                // candidate only takes over once it's gone.
                if (a != g_clone && g_clone && g_otherCandidateLogs < 10) {
                    ++g_otherCandidateLogs;
                    Log("puppet: another player-class actor exe+0x%llX ignored: the Sora copy stays exe+0x%llX",
                        static_cast<unsigned long long>(a - ExeBase()),
                        static_cast<unsigned long long>(g_clone - ExeBase()));
                }
                if (a == g_clone || !g_cloneSeen) g_cloneSeen = a;
                g_cloneFrame = g_frame;
                g_cloneRoom = *reinterpret_cast<const volatile std::uint16_t*>(ExeBase() + kNow);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (g_targetScan) {
        __try {
            if (g_frame % 15 == 0 && *reinterpret_cast<const std::uint32_t*>(a + kActorTeam) == 2) ScanEnemy(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    __try {
        if (*reinterpret_cast<const std::uint32_t*>(a + kActorTeam) == 2) WatchOnEnemyUpdate(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        WorldSyncAfterUpdate(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        DownedAfterUpdate(a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (g_debugWho >= 0) {
        __try {
            std::uintptr_t exe = ExeBase();
            std::uintptr_t target = g_debugWho == 0   ? *reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8)
                                    : g_debugWho == 3 ? g_clone
                                    : g_debugWho == 4 ? 0
                                                      : *reinterpret_cast<const std::uintptr_t*>(exe + kFriend1Actor + 8 * (g_debugWho - 1));
            // who 4 (crown research): every Sora actor of the running cutscene (actor table *(exe+0xB653E0),
            // 0x60 rows of 40 bytes {object, id, handle}), each once, for 30 frames
            if (g_debugWho == 4 && a && IsSceneActor(a)) {
                auto rows = *reinterpret_cast<const std::uint8_t* const*>(exe + 0xB653E0);
                for (int i = 0; rows && i < 0x60 && !target; ++i) {
                    int object = *reinterpret_cast<const std::int32_t*>(rows + 40 * i);
                    bool sora = object == 571 || object == 572 || object == 1729 || object == 1757 || object == 84 ||
                                object == 264;
                    if (sora && DecodeHandle(*reinterpret_cast<const std::uint32_t*>(rows + 40 * i + 8)) == a) target = a;
                }
                for (int i = 0; i < g_debugDone && target; ++i)
                    if (g_debugDoneActors[i] == a) target = 0;
                if (target && g_debugDone < 8) g_debugDoneActors[g_debugDone++] = a;
            }
            if (a && a == target) {
                if (g_debugAmount & 0x40000000) {
                    // crown research (TODO 3.13): value 0xFFFF = the game's own crown pick exe+0x3A9E20(actor)
                    // (Proofs owned -> A4/9A/9B/9C), else the setter it ends in, exe+0x3CD8A0(actor, value)
                    int value = g_debugAmount & 0xFFFF;
                    auto parts = *reinterpret_cast<const std::uintptr_t*>(a + 0x7B0);
                    int listed = 0;
                    for (auto p = parts ? *reinterpret_cast<const std::uintptr_t*>(parts + 0x68) : 0; p && listed < 64;
                         p = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(exe + 0x1C13B0)(p))
                        Log("debug: crown: %s part list #%d = %p (vtable exe+0x%llX, +0x288 %p)", WhoIs(a), listed++,
                            reinterpret_cast<void*>(p), static_cast<unsigned long long>(*reinterpret_cast<const std::uintptr_t*>(p) - exe),
                            *reinterpret_cast<void* const*>(p + 0x288));
                    if (value == 0xFFFF)
                        reinterpret_cast<void (*)(std::uintptr_t)>(exe + 0x3A9E20)(a);
                    else
                        reinterpret_cast<void (*)(std::uintptr_t, int)>(exe + 0x3CD8A0)(a, value);
                    Log("debug: crown: %s set %s 0x%X (actor+0x7B0 = %p, %d part(s))", WhoIs(a),
                        value == 0xFFFF ? "by the game's pick, not" : "to", value, reinterpret_cast<void*>(parts), listed);
                } else if (g_debugAmount < 0) {  // motion command: amount = -(motion + 1)
                    PuppetSetMotion(a, -g_debugAmount - 1);
                    Log("debug: %s motion set to %d -> now %d", WhoIs(a), -g_debugAmount - 1,
                        *reinterpret_cast<const std::int32_t*>(a + 0x180));
                } else {
                    // Through our own hook, exactly like an enemy's hit (downed rule, copy forwarding...).
                    int hp = HookApplyStatDelta(actor, -g_debugAmount, 0, 0);
                    Log("debug: %s takes %d damage like an enemy hit -> HP %d", WhoIs(a), g_debugAmount, hp);
                }
                if (g_debugWho != 4) g_debugWho = -1;
            }
            if (g_debugWho == 4 && g_frame - g_debugSetFrame > 30) g_debugWho = -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_debugWho = -1;
        }
    }
    if (g_pendingCount > 0) {
        __try {
            if (a == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8)) {
                for (int i = 0; i < g_pendingCount; ++i) {
                    // Sora's stat slot HP (exe+0x2A23598). A hit that arrives after he went
                    // down (two-PC test 21:41:49) would hit a Sora in his Game Over.
                    if (*reinterpret_cast<const volatile std::uint32_t*>(ExeBase() + 0x2A23598) == 0) {
                        Log("peer's hit ignored (-%d): our Sora is already down", g_pendingDamage[i]);
                        continue;
                    }
                    int hp = 0;
                    if (GodHit(actor, g_pendingReact[i], &hp, g_pendingDamage[i])) {
                        Log("the peer's hit (-%d) on our Sora: god mode, HP %d", g_pendingDamage[i], hp);
                        continue;
                    }
                    if (DownedInterceptDamage(a, -g_pendingDamage[i], g_pendingReact[i], &hp)) {
                        Log("the peer's hit (-%d) on our Sora went through the downed rule -> HP %d", g_pendingDamage[i], hp);
                        continue;
                    }
                    hp = g_realApplyStatDelta(actor, -g_pendingDamage[i], 0, g_pendingReact[i]);
                    Log("applied the peer's hit to our Sora: -%d -> HP %d", g_pendingDamage[i], hp);
                }
                g_pendingCount = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception applying a forwarded hit");
            g_pendingCount = 0;
        }
    }
    if (g_pendingHeals > 0) {
        __try {
            if (a == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8)) {
                for (int i = 0; i < g_pendingHeals; ++i) {
                    int hp = *reinterpret_cast<const volatile int*>(ExeBase() + 0x2A23598);
                    int maxHp = *reinterpret_cast<const volatile int*>(ExeBase() + 0x2A23598 + 4);
                    const char* what =
                        g_pendingHealSource[i] == kHealCure ? "the other player's Cure" : "the other player's item";
                    if (hp <= 0) {
                        Log("%s ignored: our Sora is already dead", what);
                        continue;
                    }
                    int amount = (maxHp * g_pendingHealPct[i] + 99) / 100;
                    if (amount < 1) amount = 1;
                    if (DownedInterceptHeal(a, amount, true, what)) continue;
                    int after = g_realApplyStatDelta(actor, amount, 0, 0);
                    Log("%s healed our Sora: +%d -> HP %d", what, amount, after);
                }
                g_pendingHeals = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception applying a heal from the other player");
            g_pendingHeals = 0;
        }
    }
    if (g_active && a == g_actor) {
        ++g_transforms;
        __try {
            if (g_cloneMode) DriveMotion(a);  // clones run no friend AI, so set motion here
            ApplyTransform(a, g_pose);  // after the actor's own update, so ours is final
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception writing transform");
        }
    } else if (g_cloneMode && !g_active && a == g_clone && a != 0 && a != g_removed) {
        __try {
            HoldClone(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception holding the Sora copy");
        }
    }
}

}  // namespace

void PuppetInit() {
    if (EnvInt("KH2COOP_PUPPET", 0) != 1) return;
    bool ok = HookFunction(kPerEntityUpdate, kPerEntityUpdateBytes, sizeof(kPerEntityUpdateBytes),
                           reinterpret_cast<void*>(HookPerEntityUpdate),
                           reinterpret_cast<void**>(&g_realPerEntityUpdate), "PerEntityUpdate") &&
              HookFunction(kFriendAi, kFriendAiBytes, sizeof(kFriendAiBytes), reinterpret_cast<void*>(HookFriendAi),
                           reinterpret_cast<void**>(&g_realFriendAi), "FriendAI") &&
              HookFunction(kMotionChainSet, kMotionChainSetBytes, sizeof(kMotionChainSetBytes),
                           reinterpret_cast<void*>(HookMotionChainSet),
                           reinterpret_cast<void**>(&g_realMotionChainSet), "MotionChainSet");
    if (ok)
        HookFunction(kApplyStatDelta, kApplyStatDeltaBytes, sizeof(kApplyStatDeltaBytes),
                     reinterpret_cast<void*>(HookApplyStatDelta), reinterpret_cast<void**>(&g_realApplyStatDelta),
                     "ApplyStatDelta (HP log)");
    g_keepCollision = EnvInt("KH2COOP_PUPPET_COLLIDE", 0) == 1;
    g_forwardHits = g_realApplyStatDelta && EnvInt("KH2COOP_FORWARD_HITS", 1) == 1;
    g_god = g_realApplyStatDelta && EnvInt("KH2COOP_GOD", 0) == 1;
    g_testBadItem = EnvInt("KH2COOP_TEST_BAD_ITEM", 0) == 1;
    g_testCopyTeam = EnvInt("KH2COOP_TEST_COPY_TEAM", -1);
    g_testAnti = EnvInt("KH2COOP_TEST_ANTI", 0) == 1;
    if (g_testAnti) Log("puppet: TEST setting: every Drive into Valor/Wisdom/Limit/Master rolls Anti at 100%%");
    HookFunction(kFormRoll, kFormRollBytes, sizeof(kFormRollBytes), reinterpret_cast<void*>(HookFormRoll),
                 reinterpret_cast<void**>(&g_realFormRoll), "Drive form roll (Anti Form log)");
    if (g_testCopyTeam >= 0) Log("puppet: TEST setting: the Sora copy's team held at %d", g_testCopyTeam);
    if (EnvInt("KH2COOP_TRACE_STAT2", 0) == 1)
        HookFunction(kStatDelta2, kStatDelta2Bytes, sizeof(kStatDelta2Bytes), reinterpret_cast<void*>(HookStatDelta2),
                     reinterpret_cast<void**>(&g_realStatDelta2), "stat change twin (trace)");
    g_itemGuard = EnvInt("KH2COOP_ITEM_GUARD", 1) == 1;
    if (g_testBadItem || !g_itemGuard)
        Log("puppet: TEST settings: bad item %d, item guard %d", g_testBadItem ? 1 : 0, g_itemGuard ? 1 : 0);
    g_extraCopy = EnvInt("KH2COOP_EXTRA_COPY", 0) == 1;
    if (g_extraCopy) Log("puppet: EXPERIMENT EXTRA_COPY on: the Sora copy is spawned beside the party, not in it");
    if (g_god) Log("puppet: GOD MODE on (KH2COOP_GOD=1, test setting): hits on the party cost no HP");
    g_enemyGod = g_realApplyStatDelta && EnvInt("KH2COOP_ENEMY_GOD", 0) == 1;
    if (g_enemyGod) Log("puppet: ENEMY GOD MODE on (KH2COOP_ENEMY_GOD=1, test setting): hits on enemies cost no HP");
    if (ok && std::memcmp(reinterpret_cast<void*>(ExeBase() + kSetMotion), kSetMotionBytes, sizeof(kSetMotionBytes)) == 0) {
        g_setMotion = reinterpret_cast<PFN_SetMotion>(ExeBase() + kSetMotion);
        g_enabled = true;
    }
    char look[16] = {};
    EnvStr("KH2COOP_PUPPET_LOOK", look, sizeof(look));
    g_lookIsSora = _stricmp(look, "sora") == 0;
    g_cloneMode = g_enabled && _stricmp(look, "clone") == 0;
    if (g_cloneMode && EnvInt("KH2COOP_CLONE_TARGET", 0) == 1) {
        char skip[256] = {};
        EnvStr("KH2COOP_CLONE_TARGET_SKIP", skip, sizeof(skip));
        char* next = nullptr;
        for (char* t = strtok_s(skip, ", ", &next); t && g_skipCount < 16; t = strtok_s(nullptr, ", ", &next))
            g_skipCallers[g_skipCount++] = static_cast<std::uint32_t>(std::strtoul(t, nullptr, 16));
        g_cloneTarget = HookFunction(kGetFriend, kGetFriendBytes, sizeof(kGetFriendBytes),
                                     reinterpret_cast<void*>(HookGetFriend),
                                     reinterpret_cast<void**>(&g_realGetFriend), "GetFriend (clone target)");
    }
    g_roxasWanted = g_enabled && !g_cloneMode && (EnvInt("KH2COOP_PUPPET_ROXAS", 0) == 1 || g_lookIsSora);
    Log("puppet: %s%s", g_enabled ? "ready" : "DISABLED (hook setup failed)",
        g_cloneMode ? (g_extraCopy ? "; mode: real Sora copy beside the party (extra copy)"
                                   : "; mode: real Sora copy in Donald's slot (party table)")
                    : g_roxasWanted ? (g_lookIsSora ? "; Donald dressed as Sora" : "; Donald dressed as Roxas")
                                    : "; Donald as himself");
    if (g_cloneMode && g_realApplyStatDelta)
        HookFunction(kCmdExecute, kCmdExecuteBytes, sizeof(kCmdExecuteBytes), reinterpret_cast<void*>(HookCmdExecute),
                     reinterpret_cast<void**>(&g_realCmdExecute), "command execute (target = the copy?)");
    if (g_cloneMode && g_realApplyStatDelta)
        HookFunction(kItemApply, kItemApplyBytes, sizeof(kItemApplyBytes), reinterpret_cast<void*>(HookItemApply),
                     reinterpret_cast<void**>(&g_realItemApply), "item use (items on the copy heal its owner)");
    if (g_cloneMode)
        HookFunction(kCmdMenuUpdate, kCmdMenuUpdateBytes, sizeof(kCmdMenuUpdateBytes),
                     reinterpret_cast<void*>(HookCmdMenuUpdate), reinterpret_cast<void**>(&g_realCmdMenuUpdate),
                     "command menu update (Sora's only)");
    if (g_cloneMode)
        HookFunction(kPlayerCommand, kPlayerCommandBytes, sizeof(kPlayerCommandBytes),
                     reinterpret_cast<void*>(HookPlayerCommand), reinterpret_cast<void**>(&g_realPlayerCommand),
                     "player command (Sora's only, e.g. Limits)");
    if (g_cloneMode)
        HookFunction(kRunCommand, kRunCommandBytes, sizeof(kRunCommandBytes), reinterpret_cast<void*>(HookRunCommand),
                     reinterpret_cast<void**>(&g_realRunCommand), "command record run (Sora's only, e.g. spells)");
    if (g_cloneMode)
        HookFunction(kPlayerTick, kPlayerTickBytes, sizeof(kPlayerTickBytes), reinterpret_cast<void*>(HookPlayerTick),
                     reinterpret_cast<void**>(&g_realPlayerTick), "player summon check (not on the copy)");
    if (g_cloneMode)
        g_itemTriggerHooked = HookFunction(kItemTrigger, kItemTriggerBytes, sizeof(kItemTriggerBytes),
                                           reinterpret_cast<void*>(HookItemTrigger),
                                           reinterpret_cast<void**>(&g_realItemTrigger),
                                           "item motion trigger (not on the copy)");
    if (g_cloneMode)
        HookFunction(kSpellTrigger, kSpellTriggerBytes, sizeof(kSpellTriggerBytes), reinterpret_cast<void*>(HookSpellTrigger),
                     reinterpret_cast<void**>(&g_realSpellTrigger), "spell cast trigger (not on the copy)");
    if (g_cloneMode)
        HookFunction(kBuildMember, kBuildMemberBytes, sizeof(kBuildMemberBytes), reinterpret_cast<void*>(HookBuildMember),
                     reinterpret_cast<void**>(&g_realBuildMember), "party member build (not the copy's entry)");
    if (g_cloneMode)
        HookFunction(kMenuPartyId, kMenuPartyIdBytes, sizeof(kMenuPartyIdBytes), reinterpret_cast<void*>(HookMenuPartyId),
                     reinterpret_cast<void**>(&g_unusedMenuOriginal), "menu party list (copy reads as Donald)");
    if (g_cloneMode)
        HookFunction(kRemovePlayerA, kRemovePlayerBytes, sizeof(kRemovePlayerBytes),
                     reinterpret_cast<void*>(HookRemovePlayerA), reinterpret_cast<void**>(&g_realRemovePlayerA),
                     "player removal (not the player's cleanup for a removed copy)");
    if (g_cloneMode)
        HookFunction(kRemovePlayerB, kRemovePlayerBytes, sizeof(kRemovePlayerBytes),
                     reinterpret_cast<void*>(HookRemovePlayerB), reinterpret_cast<void**>(&g_realRemovePlayerB),
                     "player removal method (same)");
    g_targetScan = EnvInt("KH2COOP_TARGET_SCAN", 0) == 1;
    if (g_cloneTarget) Log("puppet: clone target experiment on (%d extra callers skipped)", g_skipCount);
}

void PuppetOnPeerHit(int damage, int react) {
    if (!g_forwardHits || damage <= 0) return;
    if (g_pendingCount < kMaxPendingHits) {
        g_pendingDamage[g_pendingCount] = damage;
        g_pendingReact[g_pendingCount] = react;
        ++g_pendingCount;
    }
}

void PuppetOnPeerHeal(int percent, int source) {
    if (!g_realApplyStatDelta || percent <= 0) return;
    if (g_pendingHeals < kMaxPendingHeals) {
        g_pendingHealPct[g_pendingHeals] = percent;
        g_pendingHealSource[g_pendingHeals] = source;
        ++g_pendingHeals;
    }
}

void PuppetOnPeerPose(const PeerPose& pose) {
    g_pose = pose;
    g_poseFrame = g_frame;
}

void PuppetDebugDamage(int who, int amount) {
    if (!g_realApplyStatDelta || who < 0 || who > 4 || amount == 0) return;
    if (who == 4 && !(amount & 0x40000000)) return;  // scene Soras: crown research only
    g_debugWho = who;
    g_debugAmount = amount;
    g_debugDone = 0;
    g_debugSetFrame = g_frame;
}

void PuppetDebugLowHp(int hp) {
    g_godSoraHp = hp > 0 ? hp : 0;
    if (g_godSoraHp)
        Log("debug: low-HP hold on: god mode leaves our Sora at HP %d after a hit%s", g_godSoraHp,
            g_god ? "" : " (no effect: KH2COOP_GOD is off)");
    else
        Log("debug: low-HP hold off");
}

void PuppetDebugTeleport(float x, float y, float z) {
    __try {
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        if (!sora) return;
        auto* pos = reinterpret_cast<float*>(sora + kActorEntity + kEntityPos);
        Log("debug command: teleport our Sora from (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)", pos[0], pos[1], pos[2], x,
            y, z);
        pos[0] = x;
        pos[1] = y;
        pos[2] = z;
        std::memset(reinterpret_cast<void*>(sora + kActorVelocity), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(sora + kActorCarried), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(sora + kActorAccel), 0, 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int PuppetApplyHp(std::uintptr_t actor, int delta, int react) {
    if (!g_realApplyStatDelta) return -1;
    return g_realApplyStatDelta(reinterpret_cast<void*>(actor), delta, 0, react);
}

int PuppetApplyHit(std::uintptr_t actor, int damage, int react) {
    int hp = 0;
    if (GodHit(reinterpret_cast<void*>(actor), react, &hp, damage)) return hp;
    return PuppetApplyHp(actor, -damage, react);
}

bool PuppetHasMotion(std::uintptr_t actor, int motion) { return HasMotion(actor, motion); }

// The game's current Limit: pointer at exe+0x2A24CC0, set by the Limit start (exe+0x3D8B40, store at
// exe+0x3D8BA7) and cleared to 0 when it ends (e.g. exe+0x3D8A9B); object +0xC = handle of its user.
bool PuppetLimitCommand() { return g_limitCommand; }

bool PuppetOurLimitRunning() {
    __try {
        auto lim = *reinterpret_cast<const volatile std::uintptr_t*>(ExeBase() + 0x2A24CC0);
        if (!lim) return false;
        std::uintptr_t user = DecodeHandle(*reinterpret_cast<const volatile std::uint32_t*>(lim + 0xC));
        return user && user == *reinterpret_cast<const volatile std::uintptr_t*>(ExeBase() + 0x2A171C8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PuppetSetMotion(std::uintptr_t actor, int motion) {
    if (!g_setMotion) return;
    g_settingMotion = true;
    g_setMotion(reinterpret_cast<void*>(actor + kActorMotCtrl), motion, 0.0f, 0.0f);
    g_settingMotion = false;
}

// Our Sora's current Drive Form: save+0x3524 (0 = none, 1 = Valor; bench 2026-10-08, the save at
// exe+0x9A98B0 read 0 -> 1 in Valor -> 0 after Revert). 0 while the save isn't found yet.
int PuppetDriveForm() {
    __try {
        return g_save ? *reinterpret_cast<const volatile std::uint8_t*>(g_save + 0x3524) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Ends our Sora's Drive Form like the command menu's Revert: command type 5 through exe+0x405440
// (menu Revert logged as "command 0x5 (type 0x5)"; that branch only reads the type and calls
// exe+0x40CE60(actor, 0), the form change, then sets +0x120 |= 0x100). False without the hook.
bool PuppetRevertForm(std::uintptr_t actor) {
    if (!g_realPlayerCommand || !actor) return false;
    const std::uint16_t revert[2] = {0x5, 0x5};
    g_realPlayerCommand(reinterpret_cast<void*>(actor), revert);
    return true;
}

bool PuppetSolo() { return g_solo; }
bool PuppetExtraCopy() { return g_extraCopy; }

void PuppetSetSolo(bool on) {
    if (on == g_solo) return;
    g_solo = on;
    if (!on) {
        Log("puppet: co-op again: the Sora copy goes back into the party (from the next room load)");
        return;
    }
    char what[256];
    bool changed = g_save && PuppetCleanSave(reinterpret_cast<std::uint8_t*>(g_save), what, sizeof(what));
    g_partyPatchedWorlds = 0;
    Log("puppet: playing on without the other player: %s%s", changed ? "party back as the game had it: " : "",
        changed ? what : "no party change to undo");
}

bool PuppetRemoveCopy() {
    std::uintptr_t actor = PuppetCloneActor();
    if (!actor) return false;
    Release();
    g_removed = actor;
    g_holdActor = 0;
    __try {
        // The game's fade-out removal (VERIFIED_OFFSETS 'Removing an enemy without a death').
        auto* fade = reinterpret_cast<float*>(actor + 0xA08);
        fade[0] = 0.0f;  // faded out
        fade[1] = 0.0f;  // no fade running
        *reinterpret_cast<std::uint32_t*>(actor + 0x9B8) |= 0x10;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    Log("puppet: the Sora copy (actor exe+0x%llX) removed (fade-out route)",
        static_cast<unsigned long long>(actor - ExeBase()));
    return true;
}

std::uintptr_t PuppetCloneActor() {
    return g_cloneMode && CloneAlive() ? g_clone : 0;
}

// Only a copy with Sora's model and motions can play Sora's Limit and reaction-command motions.
std::uintptr_t PuppetDrivenActor() { return g_active && (g_cloneMode || (g_roxasPatched && g_lookIsSora)) ? g_actor : 0; }

bool PuppetIsFieldPlayer(std::uintptr_t actor) {
    __try {
        return IsPlayerClass(actor) && !IsSceneActor(actor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void PuppetFrame() {
    if (!g_enabled) return;
    ++g_frame;
    if (g_testCopyTeam >= 0 && g_clone) {
        __try {
            *reinterpret_cast<std::uint32_t*>(g_clone + kActorTeam) = static_cast<std::uint32_t>(g_testCopyTeam);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    FlushCureReach();  // last frame's distance-rule Cure, if no heal on the copy replaced it
    if (g_roxasWanted && !g_roxasPatched) {
        __try {
            g_roxasPatched = PatchDonaldAsRoxas();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (g_cloneMode) {
        // A room change makes the old room's copy invalid at once, while the load may run no entity
        // updates for a while: forget it as soon as the room bytes change. Friend log 2026-10-06: after
        // each follow load the friend's game wrote the copy's pose into the old copy's memory for ~1 s
        // ("Sora copy exe+0x24FC1B0: team 4021078782, collision flags 0xFE") until the new one appeared.
        // A reload of the same room (the host's event -> boss battle, our follow of the host's room
        // programs) keeps the room bytes but rebuilds the actors too: also forget it while loading.
        bool roomChanged = *reinterpret_cast<const volatile std::uint16_t*>(ExeBase() + kNow) != g_cloneRoom;
        bool loading = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kInField) == 0;
        if (loading) {
            g_cloneSeen = 0;  // seen in this frame's updates, before the load began
            g_exPlayerCount = 0;  // actor memory is reused by the next room
            g_lastPlayer = 0;
        }
        if (g_clone && (roomChanged || loading)) {
            Log("puppet: Sora copy gone (actor exe+0x%llX, %s)", static_cast<unsigned long long>(g_clone - ExeBase()),
                roomChanged ? "room changed" : "room reloading");
            if (g_actor == g_clone) g_collisionSaved = false;  // nothing left to restore: no write on release
            // The next room may build our own Sora in the copy's memory: a hold left on it blocked every
            // motion of our Sora (T-pose, no control; bench session 27, reload after playing on alone).
            g_holdActor = 0;
            g_clone = 0;
        }
        if (roomChanged || loading) g_removed = 0;
        // Menus and pauses stop entity updates: no updates is not "gone".
        if (g_updatesThisFrame > 0) {
            if (g_cloneSeen != g_clone)
                Log("puppet: Sora copy %s (actor exe+0x%llX)", g_cloneSeen ? "found" : "gone",
                    static_cast<unsigned long long>(g_cloneSeen ? g_cloneSeen - ExeBase() : 0));
            if (g_cloneSeen != g_clone) g_holdActor = 0;  // a hold belongs to the copy it was made on
            g_clone = g_cloneSeen;
            g_cloneSeen = 0;
        }
        __try {
            PatchPartyForClone();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    bool drive = false;
    std::uintptr_t actor = 0;
    __try {
        actor = g_cloneMode ? (g_clone != g_removed ? g_clone : 0) : Friend1Actor();
        drive = ShouldDrive();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        drive = false;
        actor = 0;
    }
    g_heldActor = actor;
    g_updatesThisFrame = 0;
    drive = drive && actor != 0;
    if (drive) g_holdActor = 0;  // re-hold from the current spot when this drive ends
    if (drive && (!g_active || actor != g_actor)) {
        Release();  // new session or different actor (new room): start fresh
        g_actor = actor;
        g_idledActor = 0;  // re-idle him when this drive ends
        Log("puppet: driving %s (actor exe+0x%llX)%s", g_cloneMode ? "the Sora copy" : "Donald",
            static_cast<unsigned long long>(actor - ExeBase()), g_keepCollision ? ", collision kept" : "");
        __try {
            LogPartyFlags();  // before our first write: the game's own values
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    } else if (!drive && g_active) {
        Log("puppet: released (AI skips %u, transform writes %u)", g_aiSkips, g_transforms);
        if (g_cloneTarget) LogCallerStats();
        Release();
    }
    g_active = drive;
    if (g_frame % 300 == 0) KeepCrashLoggerFirst();
    if (g_targetScan && g_frame % 1200 == 0) LogScan();
    if (g_cloneTarget && g_frame % 1200 == 0 && g_callerCount) {
        Log("party: GetFriend(1) callers so far:");
        LogCallerStats();
    }
}

bool PuppetCleanSave(std::uint8_t* save, char* what, std::size_t size) {
    what[0] = 0;
    std::size_t len = 0;
    bool changed = false;
    for (int world = 0; world < 32; ++world) {
        if (!(g_partyPatchedWorlds & (1u << world))) continue;
        std::uint8_t* entry = save + kPartyTable + 4 * world;
        int pos = entry[1] == kPartyPlayable ? 1 : entry[2] == kPartyPlayable ? 2 : 0;
        if (entry[0] != kPartyPlayable || !pos) continue;
        std::uint8_t before[4];
        std::memcpy(before, entry, sizeof(before));
        std::uint8_t back = g_copyReplaced[world];
        entry[pos] = back;
        // The replaced member can also show up in another slot: on the bench (HB, saved 00 00 02 01), or
        // moved by a story Party command (HB Bailey fight 00 00 01 02; bench session 27: putting Donald
        // back gave 00 01 01 02, Donald twice). Drop that second Donald and move the rest up.
        for (int j = 1; j <= 3; ++j) {
            if (j == pos || entry[j] != back) continue;
            for (int k = j; k < 3; ++k) entry[k] = entry[k + 1];
            entry[3] = g_benchBefore[world] != back ? g_benchBefore[world] : kPartyEmpty;
            break;
        }
        if (len < size)
            len += std::snprintf(what + len, size - len, "%sworld 0x%02X %02X %02X %02X %02X -> %02X %02X %02X %02X",
                                 changed ? ", " : "", world, before[0], before[1], before[2], before[3], entry[0],
                                 entry[1], entry[2], entry[3]);
        changed = true;
    }
    return changed;
}

}  // namespace kh2coop
