#include "puppet.hpp"
#include "stand_in.hpp"
#include "form_look.hpp"
#include "watch.hpp"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "companion_heal.hpp"
#include "companion_sync.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "grab_sync.hpp"
#include "limit_sync.hpp"
#include "mickey_sync.hpp"
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
// A landed hit's damage (Ghidra session 37, VERIFIED_OFFSETS Leads "How a landed hit becomes damage"):
// (victim, hit) applies hit+0x28 to stat hit+0x25 once, with the victim's survive rules (Second Chance, Once
// More). The hit record: +0x10 attacker handle, +0x18 flags (bit 1 applied), +0x20 attack handle, +0x25 stat,
// +0x26 reaction, +0x27 hit kind, +0x28 damage.
constexpr std::uintptr_t kApplyHit = 0x3D3BA0;
constexpr std::uint8_t kApplyHitBytes[] = {0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kHandleToPtr = 0x4AD270;      // (handle) -> object, 0 for handle 0
constexpr std::uintptr_t kPlayerProtected = 0x3A7360;  // player class vtable +0xB8: (class, actor) -> protected
constexpr std::uintptr_t kProtectedFlags = 0x3D3570;   // (actor) -> one of its "can't be hit" states
constexpr std::uintptr_t kInReaction = 0x3D4880;       // (actor) -> in a hit reaction (Once More's test)
// The body's reaction to a hit (Ghidra session 37, VERIFIED_OFFSETS Leads "The player's hit reaction"; TODO 1.79).
constexpr std::uintptr_t kPlayerReaction = 0x3A73D0;   // player class vtable +0xC8: (class, actor, hit) -> exe+0x3D2FE0
constexpr std::uintptr_t kStatusRemap = 0x3EE9B0;      // (status, hit, actor): the reaction hit+0x26 by our states
constexpr std::uintptr_t kReactionClass = 0x3CE980;    // (reaction) -> 1, 3 (flinch), 4 (knockback), ...
constexpr std::uintptr_t kHudHurt[3] = {0x1688F0, 0x1688B0, 0x168930};  // (actor): class 3 / 4 / other, a HUD flag
constexpr std::uintptr_t kHandleOf = 0x4AD240;         // (object) -> handle
constexpr std::uintptr_t kHitSpeed = 0x3CEAB0;         // (hit) -> knockback speed, from the attack object
constexpr std::uint8_t kHitSpeedBytes[] = {0x48, 0x8B, 0xC4, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00};
constexpr std::uintptr_t kHitPush = 0x3CEA00;          // (hit, float*, float*): knockback values, same
constexpr std::uint8_t kHitPushBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};
// What a forwarded hit carries besides its damage (HitPacket.info).
constexpr std::uint32_t kHitInfoValid = 1u << 31;      // from the game's hit apply: the fields below are set
constexpr std::uint32_t kHitInfoNoSurvive = 1u << 24;  // the attack's +0x12 bit 5 (exe+0x3CEDE0)
constexpr std::uint32_t kHitInfoAttackerFlag = 1u << 25;  // the attacker's +0x6C8 bit 7 (exe+0x3CEE50)
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
// Verified 2026-10-04: header at exe+0x2A254D0, 1900 rows; id 92 = Donald (P_EX020, type 1 = party member).
constexpr std::uintptr_t kObjTable = 0x2A254D0;
constexpr std::uint32_t kObjRow = 0x60;
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
int g_pendingDamage[kMaxPendingHits], g_pendingReact[kMaxPendingHits], g_pendingInfo[kMaxPendingHits];
HitShape g_pendingShape[kMaxPendingHits];
std::uint64_t g_pendingArrived[kMaxPendingHits];  // GetTickCount64 when it came
int g_pendingCount = 0;
using PFN_ApplyHit = void(__fastcall*)(void* victim, void* hit);
PFN_ApplyHit g_realApplyHit = nullptr;
bool g_inCopyHit = false;  // inside the game's hit apply for the copy, already sent with its raw damage
std::uint32_t g_finishLogs = 0;  // friend: 'finish check' lines (HookApplyHit)
const char* ModelOf(std::uintptr_t actor);
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
// The copy stays out of the party table: solo_copy.cpp spawns it next to the player in every room, so Donald,
// Goofy and the world allies keep their places (the extra copy; the copy in Donald's slot was removed 2026-10-14).
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

// Saves made while the copy took Donald's (or Goofy's) slot in the party table (the old mode) can still
// carry it. Checked every frame, so a loaded save and new worlds are caught before the next room load.
void RepairSavedCopy() {
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
    // A copy in the party entry came from a save made during co-op (TODO 5.19, two-PC test 2026-10-10:
    // after a restart the menu crashed at exe+0x36A4B2: the menu takes a companion entry holding Sora out
    // of bounds). Repair it: the member it replaced comes back. The saved entry shows where the replaced
    // member went: HB 00 01 02 12 patched was saved as 00 00 02 01 (Donald, left out of the active party,
    // on the bench), so the bench goes back to empty.
    if (entry[0] == kPartyPlayable && (entry[1] == kPartyPlayable) != (entry[2] == kPartyPlayable)) {
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
}

// held: a script holds the copy (CopyHeldByScript). Then the other player's Sora is grabbed too (grab_sync.cpp), and
// an attached actor's place reads (0, 0, 0): not a load. Letting go there set the copy idle, which ended the grab
// in the host's script (bench 2026-10-14 01:25:53).
bool ShouldDrive(bool held) {
    if (g_frame - g_poseFrame > kStaleFrames || !g_pose.hasActor) return false;
    if (!held && g_pose.pos[0] == 0.0f && g_pose.pos[1] == 0.0f && g_pose.pos[2] == 0.0f) return false;  // mid-load
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
    // Sora's own motion set. 9 (item use) crashed a Sora clone in Volpestyle's tests; it only plays
    // once its item trigger is blocked on the copy (HookItemTrigger).
    bool known = motion != 9 || g_itemTriggerHooked;
    // e.g. the peer's rescue motions 252/253 when this PC's Sora motion set lacks them
    if (known && motion > 200 && !HasMotion(actor, static_cast<int>(motion))) known = false;
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


bool CopyHeldByScript(std::uintptr_t actor);  // a boss's script holds the copy (below)

void __fastcall HookFriendAi(void* typeHandler, void* actor) {
    auto a = reinterpret_cast<std::uintptr_t>(actor);
    if (WorldSyncSkipAi(a)) return;  // a companion copying the host's (world_sync.cpp)
    if (g_active && a == g_actor) {
        ++g_aiSkips;
        __try {
            if (!CopyHeldByScript(a)) DriveMotion(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception setting motion");
        }
        return;  // the puppet never runs its own AI
    }
    g_realFriendAi(typeHandler, actor);
}

// A boss's script holding the copy (TODO 1.62): trap_obj_hook(copy, n, boss) sets +0xD48 (Data Roxas's Keyblade
// steal: capture, hook, attach, then the reaction command while trap_obj_motion_is_capture(copy) = 1; the game clears
// +0xD48 when the hold ends, ~3.5 s). Driving the copy from the other player's pose in the meantime put it back into
// its own motion 10 frames into the grab, and the script dropped the steal before offering it (bench 2026-10-14
// 00:50:10). So while it's set the copy's motion and place are the script's: no pose, no motion, no motion block.
// Capped, in case something leaves +0xD48 set.
constexpr std::uintptr_t kActorHook = 0xD48;
constexpr std::uint64_t kMaxHeldFrames = 900;  // 15 s
std::uint64_t g_heldSince = 0;  // frame the hold on the copy started (0 = not held)
std::uint32_t g_holds = 0;

bool CopyHeldByScript(std::uintptr_t actor) {
    if (!actor || actor != g_clone) return false;
    std::uintptr_t hook = *reinterpret_cast<const volatile std::uintptr_t*>(actor + kActorHook);
    // Also a grab without trap_obj_hook (Data Roxas's grab with motion 256: capture, move to space, attach; his throw
    // then hits the grabbed Sora): driving the copy freed it from the grab within 0.1 s (bench 2026-10-14 01:43:43).
    if (!hook && !GrabSyncCopyHeld()) {
        if (g_heldSince) Log("puppet: the script let go of the Sora copy after %.1f s: it follows the other player again",
                             (g_frame - g_heldSince) / 60.0);
        g_heldSince = 0;
        return false;
    }
    if (!g_heldSince) {
        g_heldSince = g_frame;
        ++g_holds;
        Log("puppet: a script holds the Sora copy (%s, #%u): its motion and place are the script's",
            hook ? "hook +0xD48 set" : "an enemy's grab", g_holds);
    }
    if (g_frame - g_heldSince > kMaxHeldFrames) {
        if (g_frame - g_heldSince == kMaxHeldFrames + 1)
            Log("puppet: the Sora copy still held after %llu s: driven from the other player's pose again",
                static_cast<unsigned long long>(kMaxHeldFrames / 60));
        return false;
    }
    return true;
}

const char* WhoIs(std::uintptr_t actor);

// Research trace (your two-PC run 2026-10-21: in Land of Dragons 08/08-08/09 the host's ground combos stopped after
// the first swing for ~2.5 min, with or without lock-on; air combos and the friend's combos were fine). Logged for our
// Sora: each motion it asks for through exe+0x3C88C0 that differs from the one it plays, while either is an attack
// (150-199), with what let it through or blocked it; each Attack press with the menu's answer (HookCmdExecute). Both
// say who "the player" (kPlayerPtr, exe+0x2A105D0) is right then: aggro.cpp makes it the copy while an enemy's script runs. KH2COOP_COMBO_TRACE=0
// turns it off.
constexpr int kAttackMotionFirst = 150, kAttackMotionLast = 199;
constexpr std::uint32_t kMaxComboLogs = 5000;
bool g_comboTrace = false;
std::uint32_t g_comboLogs = 0;

bool IsAttackMotion(int motion) { return motion >= kAttackMotionFirst && motion <= kAttackMotionLast; }

const char* PlayerNow() {
    std::uintptr_t player = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kPlayerPtr);
    return player ? WhoIs(player) : "none";
}

void ComboTraceAsk(std::uintptr_t actor, int motion, const char* blockedBy) {
    if (g_comboLogs >= kMaxComboLogs || actor != *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8)) return;
    __try {
        int now = *reinterpret_cast<const std::int32_t*>(actor + 0x180);
        if (motion == now || (!IsAttackMotion(motion) && !IsAttackMotion(now))) return;
        if (++g_comboLogs == kMaxComboLogs) Log("combo: trace full (%u lines): no more combo lines", kMaxComboLogs);
        Log("combo: our Sora asks motion %d (plays %d): %s%s; the player is %s", motion, now,
            blockedBy ? "blocked by " : "let through", blockedBy ? blockedBy : "", PlayerNow());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

std::uint8_t __fastcall HookMotionChainSet(void* motCtrl, int motion, float start, float blend) {
    // The game re-sets every actor's motion each frame, restarting the clip;
    // for the puppet only our own SetMotion call may go through.
    // Also the cast motion of the other player's spell, cast on the copy (motion_mirror.cpp): the spell's script
    // checks it right away and stops if the copy isn't in it (bench 2026-10-12 22:07).
    auto actor = reinterpret_cast<std::uintptr_t>(motCtrl) - kActorMotCtrl;
    const char* blockedBy = nullptr;
    if (!g_settingMotion && !MotionMirrorCasting(actor) && !(g_heldSince && actor == g_clone)) {
        if ((g_active && actor == g_actor) || (!g_active && actor == g_holdActor))
            blockedBy = "the puppet";
        else if (WorldSyncBlocksMotion(actor, motion))
            blockedBy = "world sync";
        else if (DownedBlocksMotion(actor))
            blockedBy = "downed";
        else if (MickeyStandIn(actor))
            blockedBy = "Mickey's stand-in";
    }
    if (g_comboTrace) ComboTraceAsk(actor, motion, blockedBy);
    if (blockedBy) return 1;
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

// A heal landed on the copy in our game (see HookApplyStatDelta). True = a Cure or an item, sent to the other player.
bool ForwardCopyHeal(std::uintptr_t copy, int delta, int react) {
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
    if (source == kHealOther) return false;
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
        CompanionHealSentToCopy();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return true;
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

// A Limit needs a real companion in our party, as in the game (TODO 1.78, docs/DECISIONS.md L7): our GetFriend(1)
// hook puts the Sora copy in an empty party slot, and that made the menu offer Limits with Sora alone (Data Xemnas:
// Limit 0x26C used by the friend; with the copy kept out of the party (KH2COOP_CLONE_TARGET=0, or only from the menu
// build's caller exe+0x3F2A5D) the Limit wasn't there; benches 2026-10-16 23:26-23:28).
bool NoRealCompanion() {
    auto get = g_realGetFriend ? g_realGetFriend : reinterpret_cast<PFN_GetFriend>(ExeBase() + kGetFriend);
    return !get(0) && !get(1);
}

constexpr std::uintptr_t kLimitState = 0x3D88E0;  // (actor, command record, state) -> state; 5 = greyed
constexpr std::uint8_t kLimitStateBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                             0x48, 0x89, 0x74, 0x24, 0x18};
using PFN_LimitState = int(__fastcall*)(std::uintptr_t, const std::uint16_t*, int);
PFN_LimitState g_realLimitState = nullptr;
std::uint32_t g_limitsGreyed = 0;

int __fastcall HookLimitState(std::uintptr_t actor, const std::uint16_t* command, int state) {
    int result = g_realLimitState(actor, command, state);
    __try {
        if (command && command[1] == 0x51 && result < 5 && NoRealCompanion()) {
            if (g_limitsGreyed++ % 600 == 0)
                Log("menu: Limit %#x greyed: no companion in our party (the Sora copy doesn't count; %u so far)",
                    command[0], g_limitsGreyed);
            return 5;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}

std::uint64_t __fastcall HookCmdExecute(std::uintptr_t menu, const std::uint16_t* command, std::uintptr_t info) {
    // Only one Limit at a time (docs/DECISIONS.md L2): while the other player's Limit runs, ours is refused here, the
    // game's own 'refused' (the menu offers it anyway when an ally stays as the other Limit's partner, bench
    // 2026-10-14).
    if (command && (command[1] == 0x51 || command[1] == 0x2C3) && LimitSyncPeerLimit()) {
        Log("menu: Limit %#x refused: the other player's Limit runs (one Limit at a time)", command[0]);
        return 0;
    }
    if (command && command[1] == 0x51 && NoRealCompanion()) {
        Log("menu: Limit %#x refused: no companion in our party (the Sora copy doesn't count)", command[0]);
        return 0;
    }
    std::uint64_t result = g_realCmdExecute(menu, command, info);
    __try {
        std::uintptr_t target = info ? DecodeHandle(*reinterpret_cast<const std::uint32_t*>(info)) : 0;
        bool onCopy = target && target == g_clone;
        static int logged = 0;
        if (g_comboTrace && command && command[1] == 0x1 && g_comboLogs < kMaxComboLogs) {  // Attack (type 1)
            ++g_comboLogs;
            std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
            Log("combo: Attack press on %s -> %s, our Sora plays %d, the player is %s", target ? WhoIs(target) : "no target",
                static_cast<char>(result) ? "ok" : "refused", sora ? *reinterpret_cast<const std::int32_t*>(sora + 0x180) : -1,
                PlayerNow());
        } else if (onCopy || logged++ < 30)
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
        // A companion's HP item picked for the copy (companion_heal.cpp, TODO 1.58): its target character is Sora's.
        bool companionOnCopy = item && !party && CompanionHealItemForCopy(user);
        onCopy = onCopy || companionOnCopy;
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
                    CompanionHealSentToCopy();
                    Log("heal: item %d used on the Sora copy%s: it heals the other player (%d%%), not our Sora", item,
                        companionOnCopy ? " by a companion" : "", pct);
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

// Test setting (KH2COOP_TEST_ONE_HIT=1; your bench setup 2026-10-18 for the Land of Dragons swarm): any damage to an
// enemy (team 2) takes all its HP, our own hits and the friend's claims alike (PuppetApplyHit).
bool g_testOneHit = false;
std::uint32_t g_oneHits = 0;
int OneHitDamage(std::uintptr_t a, int damage) {
    if (!g_testOneHit || !a || damage <= 0) return damage;
    __try {
        auto stats = *reinterpret_cast<const std::int32_t* const*>(a + kActorStats);
        if (!stats || *reinterpret_cast<const std::uint32_t*>(a + kActorTeam) != 2 || stats[0] <= damage) return damage;
        if (g_oneHits++ < 20) Log("puppet: TEST one hit: %s %d damage -> %d (all its HP)", WhoIs(a), damage, stats[0]);
        return stats[0];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return damage;
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
    if (delta > 0 && (stat == 0 || stat == 32) && CompanionSyncHostOwns(reinterpret_cast<std::uintptr_t>(actor))) {
        if (HealSource() != kHealOther) CompanionSyncForwardGain(reinterpret_cast<std::uintptr_t>(actor), delta, stat, flag);
        __try {  // the host's game applies it to its companion
            auto stats = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(actor) + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats + 12 * stat) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
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
    // Our copy of a host's companion: its HP and MP are the host's (companion_sync.cpp). A Cure or item from us goes
    // to the host's game; the game's own gains here are dropped (exe+0x3C0C30's +1s: bench 2026-10-21 13:14 sent them
    // too and got the host's Goofy up at once; 13:20 one got our KO'd copy up for a frame).
    if (delta > 0 && (stat == 0 || stat == 32) && CompanionSyncHostOwns(a)) {
        if (HealSource() != kHealOther) CompanionSyncForwardGain(a, delta, stat, react);
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats + 12 * stat) : 0;
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
    // The copy's HP block is our own Sora's (exe+0x2A23598): a heal sent to the other player doesn't heal us too
    // (TODO 1.58: a companion's Cure on the copy healed the host as well).
    if (stat == 0 && delta > 0 && a != 0 && a == g_clone && g_active && ForwardCopyHeal(a, delta, react)) {
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
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
        if (forward && !g_inCopyHit) AvatarLinkSendHit(-delta, react, 0, nullptr);
        Log("HP change on the Sora copy blocked: %+d (our HP stays %d)%s", delta, hp,
            g_inCopyHit ? ", its raw hit already sent"
            : forward   ? ", forwarded to its owner"
            : g_active  ? ", not forwarded (the host's game owns its hits)"
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
    if (stat == 0 && delta < 0 && MickeyStandIn(a)) {  // our Mickey only stands in for the other player's
        __try {
            auto stats = *reinterpret_cast<const std::uintptr_t*>(a + kActorStats);
            return stats ? *reinterpret_cast<const int*>(stats) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    if (stat == 0 && delta < 0) delta = -OneHitDamage(a, -delta);
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
            Log("HP change: %s (exe+0x%llX, team %u) %+d -> %d (react %d, from exe+0x%llX)", WhoIs(a),
                static_cast<unsigned long long>(a - ExeBase()), *reinterpret_cast<const std::uint32_t*>(a + kActorTeam),
                delta, hp, react,
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return result;
}

std::uintptr_t HandleToPtr(std::uint32_t handle) {
    return reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(ExeBase() + kHandleToPtr)(handle);
}

// Reaction replay (TODO 1.79): a hit the host's game landed on the copy changed only our HP, so our Sora never
// flinched. Now the host also sends the hit's shape (HitShape). The hit waits the playback delay (our copy of the
// host's enemy reaches it then on our screen), then its damage and the game's own reaction, played with a hit record
// built from the shape, come together; unless our own game hit our Sora just before (it played a reaction itself).
// Our own game's reaction to a hit right after a replayed one is the same attack seen late: not played. It is
// stopped by marking that hit's reaction done (hit +0x18 bit 0, which exe+0x3D2FE0 checks first) once its apply ran:
// a local hit reaches exe+0x3D2FE0 without the player class's +0xC8 entry (bench, hooking that never fired).
// The knockback values come from the attack object, which only the host's game has: the two functions that read
// it answer with the host's values while we play the reaction.
using PFN_HitSpeed = float(__fastcall*)(void* hit);
using PFN_HitPush = void(__fastcall*)(void* hit, float* a, float* b);
PFN_HitSpeed g_realHitSpeed = nullptr;
PFN_HitPush g_realHitPush = nullptr;
const HitShape* g_replayShape = nullptr;  // while we play a forwarded hit's reaction
const void* g_replayHit = nullptr;
constexpr std::uint64_t kOwnHitBeforeMs = 300;  // our own hit up to this long before a replay: it played one
constexpr std::uint64_t kOwnHitAfterMs = 300;   // our own game's reaction up to this long after a replay: not played
std::uint64_t g_lastOwnHitMs = 0;  // our own game's last hit on our Sora
std::uint64_t g_lastReplayMs = 0;  // our last replayed reaction
// Once More (TODO 1.77): the game asks "in a hit reaction?" before the hit's own reaction starts. A forwarded hit is
// applied ~100 ms after it arrives, when our own game has usually hit our Sora with the same attack and started that
// reaction, so every hit looked like the middle of a combo (pair bench 2026-10-17: ~35 saves at HP 1 in 18 s). Our
// own game's hits on our Sora note the state just before them; a forwarded hit takes the oldest note of an own hit up
// to kOwnHitBeforeMs before it arrived (the same attack), else the state now (our game didn't see that hit).
struct OwnHitNote {
    std::uint64_t ms;
    bool inReaction;
};
OwnHitNote g_ownHitNotes[8];
int g_ownHitNoteCount = 0;
std::uint32_t g_onceMoreRefused = 0;
using PFN_PlayerReaction = void(__fastcall*)(void* cls, std::uintptr_t actor, void* hit);
std::uint32_t g_ownReactionsBlocked = 0;
struct PendingReaction {
    std::uint64_t arrivedMs;
    int damage;
    std::uint32_t info;
    HitShape shape;
};
constexpr int kMaxReactions = 8;
PendingReaction g_reactions[kMaxReactions];
int g_reactionCount = 0;
std::uint32_t g_reactionsPlayed = 0, g_reactionsOwn = 0, g_reactionsSkipped = 0;

float __fastcall HookHitSpeed(void* hit) {
    if (g_replayShape && hit == g_replayHit) return g_replayShape->speed;
    return g_realHitSpeed(hit);
}

void __fastcall HookHitPush(void* hit, float* a, float* b) {
    if (g_replayShape && hit == g_replayHit) {
        *a = g_replayShape->push[0];
        *b = g_replayShape->push[1];
        return;
    }
    g_realHitPush(hit, a, b);
}

// The shape of a hit that landed in our game (host, on the copy): read with the game's own functions.
void ReadHitShape(std::uintptr_t h, HitShape& out) {
    std::memcpy(out.dir, reinterpret_cast<const void*>(h), sizeof(out.dir));
    out.b24 = *reinterpret_cast<const std::uint8_t*>(h + 0x24);
    if (*reinterpret_cast<const std::uint8_t*>(h + 0x27) == 5) return;  // reads another object's handle; heals only
    auto speed = g_realHitSpeed ? g_realHitSpeed : reinterpret_cast<PFN_HitSpeed>(ExeBase() + kHitSpeed);
    auto push = g_realHitPush ? g_realHitPush : reinterpret_cast<PFN_HitPush>(ExeBase() + kHitPush);
    out.speed = speed(reinterpret_cast<void*>(h));
    push(reinterpret_cast<void*>(h), &out.push[0], &out.push[1]);
}

void QueueReaction(int damage, std::uint32_t info, const HitShape& shape) {
    if (!(info & kHitInfoValid) || g_reactionCount >= kMaxReactions) return;
    g_reactions[g_reactionCount++] = PendingReaction {GetTickCount64(), damage, info, shape};
}

// Plays a forwarded hit's reaction on our Sora: the steps the game's OnHit (exe+0x3D60C0) runs after the damage.
void PlayReaction(std::uintptr_t sora, const PendingReaction& r) {
    alignas(16) std::uint8_t hit[0x40] = {};
    std::memcpy(hit, r.shape.dir, sizeof(r.shape.dir));
    *reinterpret_cast<std::uint32_t*>(hit + 0x14) =
        reinterpret_cast<std::uint32_t(__fastcall*)(std::uintptr_t)>(ExeBase() + kHandleOf)(sora);  // the victim
    hit[0x24] = r.shape.b24;
    hit[0x25] = static_cast<std::uint8_t>(r.info & 0xFF);
    hit[0x26] = static_cast<std::uint8_t>((r.info >> 8) & 0xFF);
    hit[0x27] = static_cast<std::uint8_t>((r.info >> 16) & 0xFF);
    *reinterpret_cast<std::int32_t*>(hit + 0x28) = r.damage;
    std::uint32_t status = (*reinterpret_cast<const std::uint32_t*>(sora + 0xD10) |
                            *reinterpret_cast<const std::uint32_t*>(sora + 0xD14)) &
                           ~*reinterpret_cast<const std::uint32_t*>(sora + 0xD18);
    reinterpret_cast<void(__fastcall*)(std::uint32_t, void*, std::uintptr_t)>(ExeBase() + kStatusRemap)(status, hit, sora);
    int cls = reinterpret_cast<int(__fastcall*)(std::uint8_t)>(ExeBase() + kReactionClass)(hit[0x26]);
    if (cls == 3 || cls == 4 || r.damage > 0)
        reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(ExeBase() + kHudHurt[cls == 3 ? 0 : cls == 4 ? 1 : 2])(sora);
    int before = *reinterpret_cast<const std::int32_t*>(sora + 0x180);
    g_replayShape = &r.shape;
    g_replayHit = hit;
    reinterpret_cast<PFN_PlayerReaction>(ExeBase() + kPlayerReaction)(nullptr, sora, hit);
    g_replayShape = nullptr;
    g_replayHit = nullptr;
    g_lastReplayMs = GetTickCount64();
    ++g_reactionsPlayed;
    Log("hit reaction: the host's hit (-%d, reaction %u, kind %u) played on our Sora: motion %d -> %d", r.damage,
        hit[0x26], hit[0x27], before, *reinterpret_cast<const std::int32_t*>(sora + 0x180));
}

void ReactionFrame() {
    if (!g_reactionCount) return;
    int kept = 0;
    for (int i = 0; i < g_reactionCount; ++i) {
        PendingReaction& r = g_reactions[i];
        if (g_lastOwnHitMs + kOwnHitBeforeMs >= r.arrivedMs) {
            ++g_reactionsOwn;  // our own game hit our Sora around then and played its reaction
            Log("hit reaction: the host's hit (-%d) not played: our own game hit our Sora too (%lld ms from its due time)",
                r.damage, static_cast<long long>(g_lastOwnHitMs) - static_cast<long long>(r.arrivedMs));
            continue;
        }
        __try {
            std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
            std::uintptr_t stats = sora ? *reinterpret_cast<const std::uintptr_t*>(sora + kActorStats) : 0;
            if (!stats || *reinterpret_cast<const int*>(stats) <= 0 || DownedIsDown()) {
                ++g_reactionsSkipped;
                continue;
            }
            PlayReaction(sora, r);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_replayShape = nullptr;
            g_replayHit = nullptr;
            Log("hit reaction: exception playing the host's hit");
        }
    }
    g_reactionCount = kept;
}

// Hit comparison (TODO 1.79, "the friend's game decides the hits its Sora takes"): every hit the game applies to our
// own Sora or to the copy is logged with its attacker, so the host's hits on the copy and the friend's own hits on its
// Sora can be matched (tools/hit_compare.py). On unless KH2COOP_HIT_COMPARE=0.
bool g_hitCompare = true;
std::uint64_t g_motionCheckFrame = 0;  // log our Sora's motion at this frame (a local hit's reaction)
int g_motionAtHit = -1;

void LogHitCompare(std::uintptr_t v, std::uintptr_t h) {
    if (!g_hitCompare) return;
    __try {
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        bool own = v == sora;
        if (!own && !(v && v == g_clone)) return;
        std::uintptr_t attacker = HandleToPtr(*reinterpret_cast<const std::uint32_t*>(h + 0x10));
        std::uintptr_t row = attacker ? *reinterpret_cast<const std::uintptr_t*>(attacker + 0x918) : 0;
        const char* model = row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
        int motion = *reinterpret_cast<const std::int32_t*>(v + 0x180);
        Log("hit compare: on %s: %d raw, stat %u, kind %u, reaction %u, by %.32s (exe+0x%llX), motion %d", own ? "our Sora" : "the copy",
            *reinterpret_cast<const std::int32_t*>(h + 0x28), *reinterpret_cast<const std::uint8_t*>(h + 0x25),
            *reinterpret_cast<const std::uint8_t*>(h + 0x27), *reinterpret_cast<const std::uint8_t*>(h + 0x26), model,
            static_cast<unsigned long long>(attacker ? attacker - ExeBase() : 0), motion);
        if (own) {
            g_motionCheckFrame = g_frame + 8;
            g_motionAtHit = motion;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// TODO 1.75-1.77: the game's hit apply on the copy in our game (host). The copy shares our Sora's stat record, so
// the game's survive rules would trim the damage with OUR HP and abilities (and a hit trimmed to 0 never reached
// HookApplyStatDelta). The raw damage and the hit's details go to the friend instead, whose game runs the same
// rules on its own Sora (OurHitRules). The game's own apply still runs: its HP change on the copy is blocked.
void __fastcall HookApplyHit(void* victim, void* hit) {
    auto v = reinterpret_cast<std::uintptr_t>(victim), h = reinterpret_cast<std::uintptr_t>(hit);
    LogHitCompare(v, h);
    bool own = v && v == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
    if (own) g_lastOwnHitMs = GetTickCount64();
    if (own && !AvatarLinkIsHost()) {
        __try {
            if (g_ownHitNoteCount == 8) {  // full: the oldest goes
                std::memmove(g_ownHitNotes, g_ownHitNotes + 1, 7 * sizeof(OwnHitNote));
                --g_ownHitNoteCount;
            }
            g_ownHitNotes[g_ownHitNoteCount++] = {
                g_lastOwnHitMs, reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(ExeBase() + kInReaction)(v)};
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    bool send = false;
    int raw = 0;
    std::uint32_t info = 0;
    HitShape shape {};
    if (g_forwardHits && g_active && v && v == g_clone && !WorldSyncHostPresent()) {
        __try {
            std::uintptr_t attack = HandleToPtr(*reinterpret_cast<const std::uint32_t*>(h + 0x20));
            // Not yet applied, HP, and not an attack kind 5-6 (the game's heal path).
            if (!(*reinterpret_cast<const std::uint32_t*>(h + 0x18) & 2) && *reinterpret_cast<const std::uint8_t*>(h + 0x25) == 0 &&
                attack && static_cast<std::uint8_t>(*reinterpret_cast<const std::uint8_t*>(attack + 4) - 5) >= 2) {
                raw = *reinterpret_cast<const std::int32_t*>(h + 0x28);
                info = kHitInfoValid | *reinterpret_cast<const std::uint8_t*>(h + 0x25) |
                       (static_cast<std::uint32_t>(*reinterpret_cast<const std::uint8_t*>(h + 0x26)) << 8) |
                       (static_cast<std::uint32_t>(*reinterpret_cast<const std::uint8_t*>(h + 0x27)) << 16);
                if (*reinterpret_cast<const std::uint8_t*>(attack + 0x12) & 0x20) info |= kHitInfoNoSurvive;
                std::uintptr_t attacker = HandleToPtr(*reinterpret_cast<const std::uint32_t*>(h + 0x10));
                if (attacker && (*reinterpret_cast<const std::uint32_t*>(attacker + 0x6C8) & 0x80)) info |= kHitInfoAttackerFlag;
                send = raw > 0;
                if (send) ReadHitShape(h, shape);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            send = false;
        }
    }
    if (send) AvatarLinkSendHit(raw, 0, static_cast<int>(info), &shape);
    // The friend's game: our Sora's hit that would take a host-run enemy to its minimum HP (TODO 1.97b; two-PC run
    // 2026-10-24, VERIFIED_OFFSETS F322: no friend hit counted on Shan-Yu at 1 HP). The game's rule (exe+0x3D3BA0) cuts
    // such a hit to leave the enemy at min + 1 unless the attack can finish (attack +0x12 bit 5) or the enemy's bits
    // allow it; a hit cut to 0 never reaches the HP change our claim is taken from. Logged, to see which it was.
    std::uint16_t boundNet = 0;
    int hpBefore = 0, hpMin = 0, finishRaw = 0;
    std::uint32_t finishBits = 0;
    bool canFinish = false;
    if (!AvatarLinkIsHost() && v && h && g_finishLogs < 300) {
        __try {
            std::uint16_t inst = 0;
            std::uintptr_t attacker = HandleToPtr(*reinterpret_cast<const std::uint32_t*>(h + 0x10));
            std::uintptr_t stats = *reinterpret_cast<const std::uintptr_t*>(v + kActorStats);
            if (attacker && attacker == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8) && stats &&
                *reinterpret_cast<const std::uint8_t*>(h + 0x25) == 0 && (boundNet = WorldSyncBoundTo(v, &inst)) != 0) {
                hpBefore = *reinterpret_cast<const int*>(stats);
                hpMin = *reinterpret_cast<const int*>(stats + 8);
                finishRaw = *reinterpret_cast<const std::int32_t*>(h + 0x28);
                if (hpBefore - finishRaw > hpMin) boundNet = 0;  // not a lethal hit: claimed as usual
                auto u32 = [v](std::uintptr_t off) { return *reinterpret_cast<const std::uint32_t*>(v + off); };
                finishBits = (u32(0xD10) | u32(0xD14)) & ~u32(0xD18);
                std::uintptr_t attack = HandleToPtr(*reinterpret_cast<const std::uint32_t*>(h + 0x20));
                canFinish = attack && (*reinterpret_cast<const std::uint8_t*>(attack + 0x12) & 0x20) != 0;
            } else {
                boundNet = 0;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            boundNet = 0;
        }
    }
    g_inCopyHit = send;
    g_realApplyHit(victim, hit);
    g_inCopyHit = false;
    if (boundNet) {
        int hpAfter = -1;
        __try {
            hpAfter = *reinterpret_cast<const int*>(*reinterpret_cast<const std::uintptr_t*>(v + kActorStats));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        ++g_finishLogs;
        Log("finish check: our Sora's hit on our %s (host#%u) for %d: HP %d -> %d (min %d), the attack can finish: %s, "
            "can't-die bits %08X (bit 9 %s, bit 10 %s)%s", ModelOf(v), boundNet, finishRaw, hpBefore, hpAfter, hpMin,
            canFinish ? "yes" : "no", finishBits, finishBits & 0x200 ? "on" : "off", finishBits & 0x400 ? "on" : "off",
            hpAfter == hpBefore ? ": cut to 0, no claim" : ": claimed");
    }
    // Our own game's hit on our Sora right after we played the host's one: the same attack seen late, no second
    // reaction (see "Reaction replay").
    std::uint64_t since = GetTickCount64() - g_lastReplayMs;
    if (own && !AvatarLinkIsHost() && g_lastReplayMs && since < kOwnHitAfterMs) {
        __try {
            if (!(*reinterpret_cast<const std::uint32_t*>(h + 0x18) & 1)) {
                *reinterpret_cast<std::uint32_t*>(h + 0x18) |= 1;
                ++g_ownReactionsBlocked;
                Log("hit reaction: our own game's reaction %llu ms after a replayed one: not played (the same attack)",
                    static_cast<unsigned long long>(since));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

// The game's rules for a hit on our Sora (exe+0x3D60C0 and exe+0x3D3BA0, VERIFIED_OFFSETS Leads), run on a hit
// the host's game sent us (TODO 1.75-1.77): our Sora's protection (its Limit, invincibility after a hit, ...) drops
// it; Second Chance / Once More / our states keep a lethal one at 1 HP; some states make it 1 damage.
// Returns the damage left (0 = none) and what changed it in `why`. `reactionBefore`: our Sora was in a hit reaction
// just before our own game's hit of the same attack (1 / 0), or -1 = no such hit, ask now.
int OurHitRules(std::uintptr_t sora, int damage, std::uint32_t info, int reactionBefore, char* why, int whyLen) {
    why[0] = 0;
    auto u32 = [sora](std::uintptr_t off) { return *reinterpret_cast<const std::uint32_t*>(sora + off); };
    std::uint32_t status = (u32(0xD10) | u32(0xD14)) & ~u32(0xD18);
    // Protection (exe+0x3D60C0's first check).
    bool flags = reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(ExeBase() + kProtectedFlags)(sora);
    bool b126 = (*reinterpret_cast<const std::uint8_t*>(sora + 0x126) & 1) != 0;
    bool timer = *reinterpret_cast<const float*>(sora + 0xD70) > 0.0f;
    bool b9b8 = (u32(0x9B8) & 4) != 0;
    bool game = reinterpret_cast<bool(__fastcall*)(void*, std::uintptr_t)>(ExeBase() + kPlayerProtected)(nullptr, sora);
    if (b9b8 || game) {
        std::snprintf(why, whyLen,
                      "our Sora is protected (%s%s%s%s%s; +0x120 %08X +0x18C %08X +0x9B8 %08X timer %.2f status %08X)",
                      b9b8 ? "+0x9B8 bit 2 " : "", flags ? "a can't-be-hit state " : "", b126 ? "+0x126 bit 0 " : "",
                      timer ? "invincibility timer " : "", !flags && !b126 && !timer && !b9b8 ? "other (event?) " : "",
                      u32(0x120), u32(0x18C), u32(0x9B8), *reinterpret_cast<const float*>(sora + 0xD70), status);
        return 0;
    }
    bool valid = (info & kHitInfoValid) != 0;
    int stat = valid ? static_cast<int>(info & 0xFF) : 0;
    int react = valid ? static_cast<int>((info >> 8) & 0xFF) : 0;
    int kind = valid ? static_cast<int>((info >> 16) & 0xFF) : -1;
    bool noSurvive = (info & kHitInfoNoSurvive) != 0, attackerFlag = (info & kHitInfoAttackerFlag) != 0;
    auto stats = *reinterpret_cast<const std::uintptr_t*>(sora + kActorStats);
    if (!stats) return damage;
    int cur = *reinterpret_cast<const int*>(stats), minimum = *reinterpret_cast<const int*>(stats + 8);
    // A lethal hit: the player class's survive check (exe+0x3D6670, then exe+0x3D35F0).
    if (cur - damage <= minimum) {
        const char* rule = nullptr;
        bool onceMore = (*reinterpret_cast<const std::uint8_t*>(stats + 0x1E0) & 1) != 0;
        bool inReaction = reactionBefore >= 0
                              ? reactionBefore == 1
                              : reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(ExeBase() + kInReaction)(sora);
        if (onceMore && !inReaction && reactionBefore == 0 &&
            reinterpret_cast<bool(__fastcall*)(std::uintptr_t)>(ExeBase() + kInReaction)(sora)) {
            ++g_onceMoreRefused;
            Log("Once More not used for the peer's hit (-%d, HP %d): our Sora wasn't in a hit reaction before our own "
                "game's hit of the same attack (%u so far)", damage, cur, g_onceMoreRefused);
        }
        if (*reinterpret_cast<const std::int32_t*>(stats + 0x1DC) < 0 && minimum == 0 && cur > 1)
            rule = "Second Chance";
        else if (onceMore && inReaction)
            rule = "Once More";
        else if (status & (1u << 9))
            rule = "state bit 9";
        else if ((status & (1u << 10)) && ((status & (1u << 19)) || stat == 0) && !noSurvive)
            rule = "state bit 10";
        else if (((status & (1u << 11)) || stat != 0) && !noSurvive && !attackerFlag)
            rule = "state bit 11";
        else if ((u32(0x6C8) & 0x80) && (u32(0x18C) & (1u << 14)) && react == 0)
            rule = "+0x6C8 bit 7 in a grab";
        if (rule) {
            int left = cur - minimum - 1;
            if (left < 0) left = 0;
            std::snprintf(why, whyLen, "%s: lethal %d trimmed to %d (HP %d)", rule, damage, left, cur);
            damage = left;
        }
    }
    // State bit 15: damage over 1 becomes 1 for these hit kinds (exe+0x3CEE00), not from a flagged attacker.
    bool kindOk = kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 6 || kind == 7 || kind == 0xF || kind == 0x10;
    if (damage > 1 && (status & (1u << 15)) && kindOk && !attackerFlag) {
        std::snprintf(why, whyLen, "state bit 15: %d damage made 1", damage);
        damage = 1;
    }
    return damage;
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
    // During a Limit (ours or the other player's) the copy is no party member: a Limit that sent the allies away
    // took the copy for its own (bench 2026-10-14: the host's copy in a T-pose on the friend's screen during the
    // friend's Limit 0x26C). The game gets "empty", as in a Limit without our copy (docs/DECISIONS.md L1).
    if (LimitSyncAnyLimit()) {
        static std::uint64_t loggedFrame = 0;
        if (g_frame - loggedFrame > 600) {
            loggedFrame = g_frame;
            Log("party: GetFriend(1) from exe+0x%X during a Limit: left empty (the copy is no party member then)",
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
        }
        return friendActor;
    }
    auto rva = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase());
    bool give = CloneAllowedFor(rva);
    if (CallerStat* c = Caller(rva)) {
        if ((give ? c->gave : c->refused) == 0)
            Log("party: GetFriend(1) from exe+0x%X: %s", rva, give ? "gave it the Sora copy" : "left empty (excluded)");
        ++(give ? c->gave : c->refused);
    }
    return give ? g_clone : 0;
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
std::uint32_t g_playerCommandLogs = 0;
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
    if (copy) return;
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

// Research log (TODO 1.90, Data Luxord turns a player into a card or a die): every form change exe+0x40CE60(actor,
// form) with its caller. It compares the form with the u8 at save+0x3524 and stores it there at the end (disassembly,
// session 38), the value trap_player_get_form reads. PuppetFrame also logs when our Sora's body, its model or that
// byte changes, in case the card or die comes another way.
constexpr std::uintptr_t kFormChange = 0x40CE60;
constexpr std::uint8_t kFormChangeBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20};
using PFN_FormChange = void(__fastcall*)(std::uintptr_t actor, int form);
PFN_FormChange g_realFormChange = nullptr;

const char* ModelOf(std::uintptr_t actor) {
    __try {
        std::uintptr_t row = actor ? *reinterpret_cast<const std::uintptr_t*>(actor + 0x918) : 0;
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

void __fastcall HookFormChange(std::uintptr_t actor, int form) {
    Log("forms: form change of %s (%.32s, exe+0x%llX) to %d (form now %d), from exe+0x%llX", WhoIs(actor),
        ModelOf(actor), static_cast<unsigned long long>(actor ? actor - ExeBase() : 0), form, PuppetDriveForm(),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()));
    g_realFormChange(actor, form);
}

// The friend's game: a script thread hooked onto the host's Sora copy (TODO 1.99; your two-PC run 2026-10-21). When
// the host did Shan-Yu's clash, the friend's game had the copy held for under a frame (+0xD48 set). The game's
// held-player step exe+0x3D7280 (from the player's per-frame exe+0x404A40) ran that thread with it (exe+0x3E1C80),
// and it started the clash scene 're_b_mu100-00' with its camera in the friend's game (friend log stack 0x3E1CBE <-
// 0x3D72B1 <- 0x404A51). The host's game runs the clash; the friend's game only shows its motions on the copy
// (motion_mirror.cpp). So in the friend's game, a thread on the copy whose owner (thread +0x60) is an enemy whose
// brain the host runs (one brain, WorldSyncSkipsBrain) is ended without running, with the game's own release
// exe+0x3D6F80, as when such a thread finishes (the step then returns 1). Any other thread on the copy is logged and
// runs as before. KH2COOP_COPY_HOOK_GUARD=0 turns the ending off (the log stays).
// The same for a thread on our own Sora (TODO 1.97; two-PC run 2026-10-24, VERIFIED_OFFSETS F320): when Shan-Yu
// grabbed the host's copy of the friend, the friend's own Shan-Yu had a thread on the friend's own Sora that started the
// clash in the friend's game 17 ms before the host's grab arrived; that capture went back to the host, whose held copy
// played it as a second grab, and the host's script never offered the push (270) and let go after 30 s. The host's
// grab reaches our Sora through grab_sync.cpp, which sets no thread. KH2COOP_OWN_HOOK_GUARD=0 turns this one off.
constexpr std::uintptr_t kHeldStep = 0x3D7280;
constexpr std::uint8_t kHeldStepBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
                                           0xD9, 0x48, 0x8B, 0x89, 0x48, 0x0D, 0x00, 0x00};
constexpr std::uintptr_t kReleaseHook = 0x3D6F80;  // (actor): ends the thread at +0xD48 and clears it
constexpr std::uintptr_t kThreadOwner = 0x60;
using PFN_HeldStep = std::uint8_t(__fastcall*)(std::uintptr_t actor);
PFN_HeldStep g_realHeldStep = nullptr;
bool g_copyHookGuard = true, g_ownHookGuard = true;
std::uintptr_t g_copyHookSeen = 0, g_ownHookSeen = 0;  // the last thread on the copy / our Sora we logged
std::uint32_t g_copyHooksEnded = 0, g_copyHookLogs = 0;

std::uint8_t __fastcall HookHeldStep(std::uintptr_t actor) {
    __try {
        bool copy = actor && actor == g_clone;
        bool ours = actor && !copy && actor == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        std::uintptr_t thread =
            copy || ours ? *reinterpret_cast<const volatile std::uintptr_t*>(actor + kActorHook) : 0;
        if (thread && !AvatarLinkIsHost()) {
            std::uintptr_t owner = *reinterpret_cast<const std::uintptr_t*>(thread + kThreadOwner);
            bool hostRuns = owner && WorldSyncSkipsBrain(owner);
            bool end = hostRuns && (copy ? g_copyHookGuard : g_ownHookGuard);
            std::uintptr_t& seen = copy ? g_copyHookSeen : g_ownHookSeen;
            if (thread != seen && g_copyHookLogs < 100) {
                ++g_copyHookLogs;
                Log("puppet: a script thread of %.32s (exe+0x%llX) holds %s in our game: %s",
                    owner ? ModelOf(owner) : "nobody", static_cast<unsigned long long>(owner ? owner - ExeBase() : 0),
                    copy ? "the host's Sora copy" : "our own Sora",
                    end        ? "ended without running (the host's game runs that enemy and its scene)"
                    : hostRuns ? (copy ? "runs (KH2COOP_COPY_HOOK_GUARD=0)" : "runs (KH2COOP_OWN_HOOK_GUARD=0)")
                               : "runs (not an enemy the host's game runs)");
            }
            seen = thread;
            if (end) {
                ++g_copyHooksEnded;
                reinterpret_cast<PFN_Void1>(ExeBase() + kReleaseHook)(reinterpret_cast<void*>(actor));
                return 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return g_realHeldStep(actor);
}

std::uintptr_t g_bodyWatch = 0;
const char* g_bodyWatchModel = nullptr;
int g_bodyWatchForm = -1;
std::uint32_t g_bodyWatchLogs = 0;

void WatchBody() {
    if (g_bodyWatchLogs >= 300) return;
    __try {
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
        const char* model = sora ? ModelOf(sora) : nullptr;
        int form = PuppetDriveForm();
        if (sora == g_bodyWatch && model == g_bodyWatchModel && form == g_bodyWatchForm) return;
        ++g_bodyWatchLogs;
        Log("forms: our Sora is now exe+0x%llX (%.32s), form %d (was exe+0x%llX, %.32s, form %d)",
            static_cast<unsigned long long>(sora ? sora - ExeBase() : 0), model ? model : "-", form,
            static_cast<unsigned long long>(g_bodyWatch ? g_bodyWatch - ExeBase() : 0),
            g_bodyWatchModel ? g_bodyWatchModel : "-", g_bodyWatchForm);
        g_bodyWatch = sora;
        g_bodyWatchModel = model;
        g_bodyWatchForm = form;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
// Mickey's rescue ends, exe+0x400440 rebuilds the party members from the party table, where one entry was
// our playable copy (the old copy in Donald's slot): host crash 2026-10-07 21:04:32, 4 s after the Mickey revive.
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
    if (g_enabled) {
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
    __try {
        MickeyAfterUpdate(a);
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
                std::uint64_t now = GetTickCount64(), wait = static_cast<std::uint64_t>(AvatarLinkInterpMs());
                int kept = 0;
                for (int i = 0; i < g_pendingCount; ++i) {
                    // Not yet: our copy of the host's enemy reaches this hit on our screen after the playback delay.
                    if (now - g_pendingArrived[i] < wait) {
                        g_pendingDamage[kept] = g_pendingDamage[i];
                        g_pendingReact[kept] = g_pendingReact[i];
                        g_pendingInfo[kept] = g_pendingInfo[i];
                        g_pendingShape[kept] = g_pendingShape[i];
                        g_pendingArrived[kept] = g_pendingArrived[i];
                        ++kept;
                        continue;
                    }
                    // Sora's stat slot HP (exe+0x2A23598). A hit that arrives after he went
                    // down (two-PC test 21:41:49) would hit a Sora in his Game Over.
                    if (*reinterpret_cast<const volatile std::uint32_t*>(ExeBase() + 0x2A23598) == 0) {
                        Log("peer's hit ignored (-%d): our Sora is already down", g_pendingDamage[i]);
                        continue;
                    }
                    // Our own rules first, also in god mode (the bench sees a protected hit dropped).
                    // The note of our own game's hit of the same attack, if any (Once More, see OwnHitNote).
                    int reactionBefore = -1;
                    int keptNotes = 0;
                    for (int n = 0; n < g_ownHitNoteCount; ++n) {
                        const OwnHitNote& note = g_ownHitNotes[n];
                        if (note.ms + kOwnHitBeforeMs < g_pendingArrived[i]) continue;  // too old: another attack
                        if (reactionBefore < 0 && note.ms <= now) {
                            reactionBefore = note.inReaction ? 1 : 0;
                            continue;
                        }
                        g_ownHitNotes[keptNotes++] = note;
                    }
                    g_ownHitNoteCount = keptNotes;
                    char why[320];
                    int damage = OurHitRules(a, g_pendingDamage[i], static_cast<std::uint32_t>(g_pendingInfo[i]),
                                             reactionBefore, why, sizeof(why));
                    if (why[0]) Log("the peer's hit (-%d) on our Sora: %s", g_pendingDamage[i], why);
                    if (damage <= 0) continue;
                    g_pendingDamage[i] = damage;
                    QueueReaction(damage, static_cast<std::uint32_t>(g_pendingInfo[i]), g_pendingShape[i]);
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
                g_pendingCount = kept;
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
            if (!CopyHeldByScript(a)) {
                DriveMotion(a);  // clones run no friend AI, so set motion here
                ApplyTransform(a, g_pose);  // after the actor's own update, so ours is final
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception writing transform");
        }
    } else if (!g_active && a == g_clone && a != 0 && a != g_removed) {
        __try {
            HoldClone(a);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("puppet: exception holding the Sora copy");
        }
    }
}

// The Sora stat sheet's Drive Form link (TODO 1.102; host crash in your two-PC run 2026-10-21, exe+0x3E84B3). The game
// keeps one stat sheet per character (exe+0x3C04E0), so the Sora copy shares our Sora's (the same +0x5C0). Each Sora
// writes its own form (+0xDE0) into it: the constructor exe+0x3A7A40 (exe+0x3C0620) and the refreshes exe+0x3A9220
// (actor) and exe+0x3A7890 (vtable: self, actor), both exe+0x3C26C0(sheet, form): +0x254 = the handle of that form's
// save record, 0 for no form. The copy is never in a form, so it cleared our Valor's link, and Valor's level-up
// (exe+0x3E8380, on a hit) wrote through the null record. Now the copy's refreshes do only their recalculation
// (exe+0x3D6850), and each frame our Sora's link is set back the game's way if it doesn't match our form (anything
// else that clears it, e.g. the copy built after a Drive). KH2COOP_SORA_SHEET_FIX=0 turns both off.
constexpr std::uintptr_t kSheetRefresh = 0x3A9220;    // (actor)
constexpr std::uint8_t kSheetRefreshBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kSheetRefreshVt = 0x3A7890;  // (self, actor)
constexpr std::uint8_t kSheetRefreshVtBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
                                                 0x8A, 0xC0, 0x05, 0x00, 0x00};
constexpr std::uintptr_t kSheetRecalc = 0x3D6850;     // (actor): the refresh's recalculation of the sheet
constexpr std::uintptr_t kSheetSetForm = 0x3C26C0;    // (sheet, form): +0x254 = handle of the form's record
constexpr std::uintptr_t kSheetPool = 0x2A17290, kSheetPoolEnd = 0x2A23810;  // 0x278 bytes each
constexpr std::uintptr_t kSheetCharId = 0x260, kSheetFormLink = 0x254, kActorForm = 0xDE0;
constexpr std::uintptr_t kFormRecords = 0x9ABDA0 + 0xE04;  // form 1's save record; 0x38 bytes each, forms 1-10
using PFN_SheetRefreshVt = void(__fastcall*)(std::uintptr_t self, std::uintptr_t actor);
PFN_Void1 g_realSheetRefresh = nullptr;
PFN_SheetRefreshVt g_realSheetRefreshVt = nullptr;
bool g_sheetFix = true;
std::uint32_t g_copySheetSkips = 0, g_sheetRepairs = 0;

void __fastcall HookSheetRefresh(void* actor) {
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(actor);
    if (g_sheetFix && a && a == g_clone) {
        ++g_copySheetSkips;
        reinterpret_cast<PFN_Void1>(ExeBase() + kSheetRecalc)(actor);
        return;
    }
    g_realSheetRefresh(actor);
}

void __fastcall HookSheetRefreshVt(std::uintptr_t self, std::uintptr_t actor) {
    if (g_sheetFix && actor && actor == g_clone) {
        ++g_copySheetSkips;
        reinterpret_cast<PFN_Void1>(ExeBase() + kSheetRecalc)(reinterpret_cast<void*>(actor));
        return;
    }
    g_realSheetRefreshVt(self, actor);
}

}  // namespace

void PuppetInit() {
    if (EnvInt("KH2COOP_PUPPET", 1) != 1) return;
    FormLookInit();
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
    if (ok && g_realApplyStatDelta)
        HookFunction(kApplyHit, kApplyHitBytes, sizeof(kApplyHitBytes), reinterpret_cast<void*>(HookApplyHit),
                     reinterpret_cast<void**>(&g_realApplyHit), "hit apply (raw hits on the copy for its owner)");
    if (ok && g_realApplyStatDelta) {
        HookFunction(kHitSpeed, kHitSpeedBytes, sizeof(kHitSpeedBytes), reinterpret_cast<void*>(HookHitSpeed),
                     reinterpret_cast<void**>(&g_realHitSpeed), "hit knockback speed (the host's for a replayed reaction)");
        HookFunction(kHitPush, kHitPushBytes, sizeof(kHitPushBytes), reinterpret_cast<void*>(HookHitPush),
                     reinterpret_cast<void**>(&g_realHitPush), "hit knockback values (same)");
    }
    g_keepCollision = EnvInt("KH2COOP_PUPPET_COLLIDE", 1) == 1;
    g_forwardHits = g_realApplyStatDelta && EnvInt("KH2COOP_FORWARD_HITS", 1) == 1;
    g_god = g_realApplyStatDelta && EnvInt("KH2COOP_GOD", 0) == 1;
    g_hitCompare = EnvInt("KH2COOP_HIT_COMPARE", 1) == 1;
    g_comboTrace = EnvInt("KH2COOP_COMBO_TRACE", 1) == 1;
    if (g_comboTrace) Log("combo trace: on: our Sora's attack motions and Attack presses are logged (up to %u lines)",
                          kMaxComboLogs);
    g_testBadItem = EnvInt("KH2COOP_TEST_BAD_ITEM", 0) == 1;
    g_testCopyTeam = EnvInt("KH2COOP_TEST_COPY_TEAM", -1);
    g_testAnti = EnvInt("KH2COOP_TEST_ANTI", 0) == 1;
    if (g_testAnti) Log("puppet: TEST setting: every Drive into Valor/Wisdom/Limit/Master rolls Anti at 100%%");
    HookFunction(kFormRoll, kFormRollBytes, sizeof(kFormRollBytes), reinterpret_cast<void*>(HookFormRoll),
                 reinterpret_cast<void**>(&g_realFormRoll), "Drive form roll (Anti Form log)");
    HookFunction(kFormChange, kFormChangeBytes, sizeof(kFormChangeBytes), reinterpret_cast<void*>(HookFormChange),
                 reinterpret_cast<void**>(&g_realFormChange), "form change (log)");
    g_copyHookGuard = EnvInt("KH2COOP_COPY_HOOK_GUARD", 1) == 1;
    g_ownHookGuard = EnvInt("KH2COOP_OWN_HOOK_GUARD", 1) == 1;
    HookFunction(kHeldStep, kHeldStepBytes, sizeof(kHeldStepBytes), reinterpret_cast<void*>(HookHeldStep),
                 reinterpret_cast<void**>(&g_realHeldStep),
                 "held player step (friend: no host enemy's scene on the copy or our Sora)");
    g_sheetFix = EnvInt("KH2COOP_SORA_SHEET_FIX", 1) == 1;
    if (g_sheetFix) {
        HookFunction(kSheetRefresh, kSheetRefreshBytes, sizeof(kSheetRefreshBytes),
                     reinterpret_cast<void*>(HookSheetRefresh), reinterpret_cast<void**>(&g_realSheetRefresh),
                     "Sora sheet refresh (the copy keeps out of our form link)");
        HookFunction(kSheetRefreshVt, kSheetRefreshVtBytes, sizeof(kSheetRefreshVtBytes),
                     reinterpret_cast<void*>(HookSheetRefreshVt), reinterpret_cast<void**>(&g_realSheetRefreshVt),
                     "Sora sheet refresh 2 (the copy keeps out of our form link)");
    }
    Log("puppet: Sora sheet fix %s", g_sheetFix ? "on: our Drive Form's link in the shared Sora stat sheet is kept"
                                                : "OFF (KH2COOP_SORA_SHEET_FIX=0)");
    if (g_testCopyTeam >= 0) Log("puppet: TEST setting: the Sora copy's team held at %d", g_testCopyTeam);
    if (EnvInt("KH2COOP_TRACE_STAT2", 0) == 1)
        HookFunction(kStatDelta2, kStatDelta2Bytes, sizeof(kStatDelta2Bytes), reinterpret_cast<void*>(HookStatDelta2),
                     reinterpret_cast<void**>(&g_realStatDelta2), "stat change twin (trace)");
    g_itemGuard = EnvInt("KH2COOP_ITEM_GUARD", 1) == 1;
    if (g_testBadItem || !g_itemGuard)
        Log("puppet: TEST settings: bad item %d, item guard %d", g_testBadItem ? 1 : 0, g_itemGuard ? 1 : 0);
    if (g_god) Log("puppet: GOD MODE on (KH2COOP_GOD=1, test setting): hits on the party cost no HP");
    g_enemyGod = g_realApplyStatDelta && EnvInt("KH2COOP_ENEMY_GOD", 0) == 1;
    if (g_enemyGod) Log("puppet: ENEMY GOD MODE on (KH2COOP_ENEMY_GOD=1, test setting): hits on enemies cost no HP");
    g_testOneHit = g_realApplyStatDelta && EnvInt("KH2COOP_TEST_ONE_HIT", 0) == 1;
    if (g_testOneHit) Log("puppet: TEST setting: any damage to an enemy takes all its HP (KH2COOP_TEST_ONE_HIT=1)");
    if (ok && std::memcmp(reinterpret_cast<void*>(ExeBase() + kSetMotion), kSetMotionBytes, sizeof(kSetMotionBytes)) == 0) {
        g_setMotion = reinterpret_cast<PFN_SetMotion>(ExeBase() + kSetMotion);
        g_enabled = true;
    }
    if (g_enabled && EnvInt("KH2COOP_CLONE_TARGET", 1) == 1) {
        char skip[256] = {};
        EnvStr("KH2COOP_CLONE_TARGET_SKIP", skip, sizeof(skip));
        char* next = nullptr;
        for (char* t = strtok_s(skip, ", ", &next); t && g_skipCount < 16; t = strtok_s(nullptr, ", ", &next))
            g_skipCallers[g_skipCount++] = static_cast<std::uint32_t>(std::strtoul(t, nullptr, 16));
        g_cloneTarget = HookFunction(kGetFriend, kGetFriendBytes, sizeof(kGetFriendBytes),
                                     reinterpret_cast<void*>(HookGetFriend),
                                     reinterpret_cast<void**>(&g_realGetFriend), "GetFriend (clone target)");
    }
    Log("puppet: %s", g_enabled ? "ready; mode: real Sora copy beside the party (extra copy)"
                                : "DISABLED (hook setup failed)");
    if (g_enabled && g_realApplyStatDelta)
        HookFunction(kCmdExecute, kCmdExecuteBytes, sizeof(kCmdExecuteBytes), reinterpret_cast<void*>(HookCmdExecute),
                     reinterpret_cast<void**>(&g_realCmdExecute), "command execute (target = the copy?)");
    if (g_enabled && g_realApplyStatDelta)
        HookFunction(kLimitState, kLimitStateBytes, sizeof(kLimitStateBytes), reinterpret_cast<void*>(HookLimitState),
                     reinterpret_cast<void**>(&g_realLimitState), "Limit menu state (greyed with no companion)");
    if (g_enabled && g_realApplyStatDelta)
        HookFunction(kItemApply, kItemApplyBytes, sizeof(kItemApplyBytes), reinterpret_cast<void*>(HookItemApply),
                     reinterpret_cast<void**>(&g_realItemApply), "item use (items on the copy heal its owner)");
    if (g_enabled)
        HookFunction(kCmdMenuUpdate, kCmdMenuUpdateBytes, sizeof(kCmdMenuUpdateBytes),
                     reinterpret_cast<void*>(HookCmdMenuUpdate), reinterpret_cast<void**>(&g_realCmdMenuUpdate),
                     "command menu update (Sora's only)");
    if (g_enabled)
        HookFunction(kPlayerCommand, kPlayerCommandBytes, sizeof(kPlayerCommandBytes),
                     reinterpret_cast<void*>(HookPlayerCommand), reinterpret_cast<void**>(&g_realPlayerCommand),
                     "player command (Sora's only, e.g. Limits)");
    if (g_enabled)
        HookFunction(kRunCommand, kRunCommandBytes, sizeof(kRunCommandBytes), reinterpret_cast<void*>(HookRunCommand),
                     reinterpret_cast<void**>(&g_realRunCommand), "command record run (Sora's only, e.g. spells)");
    if (g_enabled)
        HookFunction(kPlayerTick, kPlayerTickBytes, sizeof(kPlayerTickBytes), reinterpret_cast<void*>(HookPlayerTick),
                     reinterpret_cast<void**>(&g_realPlayerTick), "player summon check (not on the copy)");
    if (g_enabled)
        g_itemTriggerHooked = HookFunction(kItemTrigger, kItemTriggerBytes, sizeof(kItemTriggerBytes),
                                           reinterpret_cast<void*>(HookItemTrigger),
                                           reinterpret_cast<void**>(&g_realItemTrigger),
                                           "item motion trigger (not on the copy)");
    if (g_enabled)
        HookFunction(kSpellTrigger, kSpellTriggerBytes, sizeof(kSpellTriggerBytes), reinterpret_cast<void*>(HookSpellTrigger),
                     reinterpret_cast<void**>(&g_realSpellTrigger), "spell cast trigger (not on the copy)");
    if (g_enabled)
        HookFunction(kBuildMember, kBuildMemberBytes, sizeof(kBuildMemberBytes), reinterpret_cast<void*>(HookBuildMember),
                     reinterpret_cast<void**>(&g_realBuildMember), "party member build (not the copy's entry)");
    if (g_enabled)
        HookFunction(kRemovePlayerA, kRemovePlayerBytes, sizeof(kRemovePlayerBytes),
                     reinterpret_cast<void*>(HookRemovePlayerA), reinterpret_cast<void**>(&g_realRemovePlayerA),
                     "player removal (not the player's cleanup for a removed copy)");
    if (g_enabled)
        HookFunction(kRemovePlayerB, kRemovePlayerBytes, sizeof(kRemovePlayerBytes),
                     reinterpret_cast<void*>(HookRemovePlayerB), reinterpret_cast<void**>(&g_realRemovePlayerB),
                     "player removal method (same)");
    g_targetScan = EnvInt("KH2COOP_TARGET_SCAN", 0) == 1;
    if (g_cloneTarget) Log("puppet: clone target experiment on (%d extra callers skipped)", g_skipCount);
}

void PuppetOnPeerHit(int damage, int react, int info, const HitShape& shape) {
    if (!g_forwardHits || damage <= 0) return;
    if (g_pendingCount < kMaxPendingHits) {
        g_pendingDamage[g_pendingCount] = damage;
        g_pendingReact[g_pendingCount] = react;
        g_pendingInfo[g_pendingCount] = info;
        g_pendingShape[g_pendingCount] = shape;
        g_pendingArrived[g_pendingCount] = GetTickCount64();
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

int PuppetApplyStat(std::uintptr_t actor, int delta, int stat, int react) {
    if (!g_realApplyStatDelta) return -1;
    return g_realApplyStatDelta(reinterpret_cast<void*>(actor), delta, stat, react);
}

int PuppetApplyHit(std::uintptr_t actor, int damage, int react) {
    damage = OneHitDamage(actor, damage);
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

void PuppetMotionAgain() {
    g_lastMotion = -1;
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
bool PuppetEnabled() { return g_enabled; }

void PuppetSetSolo(bool on) {
    if (on == g_solo) return;
    g_solo = on;
    Log(on ? "puppet: playing on without the other player: no Sora copy from the next room load"
           : "puppet: co-op again: the Sora copy comes back (from the next room load)");
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
    return g_enabled && CloneAlive() ? g_clone : 0;
}

std::uintptr_t PuppetDrivenActor() { return g_active ? g_actor : 0; }

bool PuppetIsFieldPlayer(std::uintptr_t actor) {
    __try {
        return IsPlayerClass(actor) && !IsSceneActor(actor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Our Sora in a Drive Form: its stat sheet's form link must be that form's record (see HookSheetRefresh).
void KeepSoraSheetForm() {
    if (!g_sheetFix) return;
    __try {
        std::uintptr_t exe = ExeBase();
        std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(exe + 0x2A171C8);
        if (!sora || sora == g_clone || !PuppetIsFieldPlayer(sora)) return;
        std::uintptr_t sheet = *reinterpret_cast<const std::uintptr_t*>(sora + kActorStats);
        if (sheet < exe + kSheetPool || sheet >= exe + kSheetPoolEnd) return;
        std::int32_t id = *reinterpret_cast<const std::int32_t*>(sheet + kSheetCharId);
        std::int32_t form = *reinterpret_cast<const std::int32_t*>(sora + kActorForm);
        if ((id != 1 && id != 0xE) || form < 1 || form > 10) return;
        std::uintptr_t want = exe + kFormRecords + static_cast<std::uintptr_t>(form - 1) * 0x38;
        std::uint32_t h = *reinterpret_cast<const std::uint32_t*>(sheet + kSheetFormLink);
        std::uintptr_t have = GameHandleLive(h) ? HandleToPtr(h) : 0;
        if (have == want) return;
        reinterpret_cast<void(__fastcall*)(std::uintptr_t, int)>(exe + kSheetSetForm)(sheet, form);
        if (++g_sheetRepairs <= 50)
            Log("puppet: our Sora's stat sheet linked to %s, not form %d's record: set back (the game's own call; %u so "
                "far, the copy's refreshes kept out %u)",
                have ? "another form's record" : "no form", form, g_sheetRepairs, g_copySheetSkips);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    KeepSoraSheetForm();
    FlushCureReach();  // last frame's distance-rule Cure, if no heal on the copy replaced it
    ReactionFrame();
    WatchBody();
    if (g_motionCheckFrame && g_frame >= g_motionCheckFrame) {
        g_motionCheckFrame = 0;
        __try {
            std::uintptr_t sora = *reinterpret_cast<const std::uintptr_t*>(ExeBase() + 0x2A171C8);
            if (sora)
                Log("hit compare: our Sora's motion 8 frames after its own hit: %d (was %d)",
                    *reinterpret_cast<const std::int32_t*>(sora + 0x180), g_motionAtHit);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
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
        RepairSavedCopy();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    bool drive = false;
    std::uintptr_t actor = 0;
    __try {
        actor = g_clone != g_removed ? g_clone : 0;
        drive = ShouldDrive(g_active && g_heldSince && actor == g_actor && g_frame - g_heldSince <= kMaxHeldFrames);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        drive = false;
        actor = 0;
    }
    g_updatesThisFrame = 0;
    drive = drive && actor != 0;
    if (drive) g_holdActor = 0;  // re-hold from the current spot when this drive ends
    if (drive && (!g_active || actor != g_actor)) {
        Release();  // new session or different actor (new room): start fresh
        g_actor = actor;
        Log("puppet: driving the Sora copy (actor exe+0x%llX)%s",
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
    StandInFrame(drive ? actor : 0, drive ? &g_pose : nullptr, roomChanged || loading);  // the peer's die / card
    FormLookFrame(drive ? actor : 0, drive ? &g_pose : nullptr, roomChanged || loading);  // the peer's Drive Form
    if (g_frame % 300 == 0) KeepCrashLoggerFirst();
    if (g_targetScan && g_frame % 1200 == 0) LogScan();
    if (g_cloneTarget && g_frame % 1200 == 0 && g_callerCount) {
        Log("party: GetFriend(1) callers so far:");
        LogCallerStats();
    }
}

}  // namespace kh2coop
