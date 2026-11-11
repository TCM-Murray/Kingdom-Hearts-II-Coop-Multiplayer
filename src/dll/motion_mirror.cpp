// Limits and reaction commands on the other player's screen (TODO 1.31; world mirror step 4,
// docs/WORLD_MIRROR_RESEARCH.md). KH2COOP_MIRROR_MOTIONS=0 turns it off (both PCs: feature handshake).
//
// A Limit or a reaction command plays a motion from another file than the actor's own motion set
// (VERIFIED_OFFSETS rows 'Limit motions' and 'Reaction-command motions'). The pose packet only says "a motion
// from another file" (kOtherBank), so the copy stood idle through them. Both games hold those files at the same
// address, so the game that plays one says which file and which motion, and the other game plays the same on its
// copy, with the game's own call:
//   exe+0x3C63E0(owner, bar, paxSlot, target, motion, blend, users): a Limit's motion (its only caller is
//     trap_limit_motion_start). Sent: the Limit's BAR (and its PAX, for later: see g_noEffects) as exe offsets, and
//     what the BAR holds (BarPrint). A Limit's file isn't always at the same address in both games (two-PC run
//     2026-10-14: Trinity's sat at another one in each game, every motion skipped): then the receiver looks for a
//     BAR with the same contents in its own game (FindBar).
//   exe+0x3C6CC0(source+0x158, target, motion, blend): the target plays motion `motion` of the source actor's
//     file, e.g. trap_obj_motion_capture(chandelier, Sora, 252) for a reaction command. Sent: who the source is: the
//     actor at the same address, and world sync's host number for it. Actors a script makes during a fight sit at
//     other addresses in each game (the Twin Lords' ice block that freezes an ally: 450 captures not played in the
//     logs to 2026-10-15), so the receiver takes our copy world sync paired with the host's actor first. Not one
//     that runs its own brain for a shared move (shared_move.cpp, Final Xemnas's desperation move): it plays the
//     move on our own Sora, and as the copy's captor its own capture of our Sora waited until the host's move ended
//     (two-PC run 2026-10-22: the friend's move began 89 s late; TODO 1.111). Such a capture isn't played.
// Each player sends its own Sora's; the host also sends its companions' (the friend's companions mirror them), and
// the friend its Limit partner's during its own Limit (the host's companion with that id plays it, world_sync.cpp
// TODO 1.57). The receiver plays one only from a file its game has loaded and in place, and holds its own Limit preload
// while the other player's Limit runs and our actors play its motions, as the game holds it for its own (TODO 1.119).
// The receiver plays it when its playback of the sender's poses reaches that moment, and the copy keeps it while
// the sender's pose says "from another file" (puppet.cpp DriveMotion, world_sync.cpp ApplyMirror).
// Such motions have attack triggers (exe+0x3D25D0 makes the attack). The sender's game already makes those attacks
// and deals their damage; on the friend, damage to a bound enemy goes to the host as a claim, so the copy's attacks
// would count twice. And a Limit's attack takes the running Limit's effects (exe+0x3D8A40), which the receiver's
// game hasn't: 0, and its hit crashed in exe+0x3D1B40 (bench 2026-10-12 21:08). So a copy playing a sent motion
// makes no attacks.
//
// Spells (user's pick 2026-10-12: cast it on the clone): a spell's particles, projectiles and sounds come from its
// script, which the game runs on the caster (VERIFIED_OFFSETS rows 'A spell's particles' and lead 'Casting a
// spell'). The caster's game sends the spell id (exe+0x3C5170(actor, spell, air) on its Sora); the other game casts
// the same spell on its copy with that call, when the playback of the caster's poses reaches it. The copy then plays
// the cast motion, and its cast trigger (puppet.cpp HookSpellTrigger) lets the spell go once. The caster's game deals
// the spell's damage and heals, so in the other game the copy's spell changes nothing: the attacks its script makes
// hit nothing (exe+0x3D2060, 'can this attack hit that actor', says no) and its HP changes are dropped (puppet.cpp
// HookApplyStatDelta asks MotionMirrorCopySpellRunning).

#include "motion_mirror.hpp"

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "form_look.hpp"
#include "limit_sync.hpp"
#include "puppet.hpp"
#include "reliable.hpp"
#include "shared_move.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4D32484B;  // "KH2M"
constexpr std::uint16_t kVersion = 6;  // v6: the Limit script's effects; v5: the Limit's hit effects; v4: companions' hit effects; v3: a capture's source by world sync's number; v2: the Limit file's contents
constexpr std::uintptr_t kFileMotion = 0x3C63E0;
constexpr std::uint8_t kFileMotionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48,
                                             0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56,
                                             0x48, 0x83, 0xEC, 0x30, 0x33, 0xDB, 0x49, 0x8B, 0xF9};
constexpr std::uintptr_t kCapture = 0x3C6CC0;
constexpr std::uint8_t kCaptureBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48,
                                          0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56,
                                          0x48, 0x83, 0xEC, 0x40, 0x33, 0xED, 0x0F, 0x29, 0x74, 0x24};
constexpr std::uintptr_t kMotionAttack = 0x3D25D0;  // (attack set: u32 owner handle at +0, ...)
constexpr std::uint8_t kMotionAttackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18,
                                               0x48, 0x89, 0x74, 0x24, 0x20, 0x89, 0x54, 0x24, 0x10, 0x57};
constexpr std::uintptr_t kCast = 0x3C5170;  // (actor, spell id, u8 in the air) -> its magic list entry or 0
constexpr std::uint8_t kCastBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57};
// A landed hit's effect (L165; its only callers are the attack-hit code exe+0x3D1B40 / 0x3D1960): (the attacker's
// effect instance, a 64-bit mask, place, a, b, c) -> the effect or 0. exe+0x2C6730 picks the instance's first entry
// whose u64 at +8 has all the mask's bits (none: 0).
constexpr std::uintptr_t kHitEffect = 0x3F6410;
constexpr std::uint8_t kHitEffectBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
                                            0x48, 0x83, 0xEC, 0x30};
constexpr std::uintptr_t kEffectMode = 0xB63628;  // u32: the effect system's mode, 1 in the field (F292)
constexpr std::uintptr_t kWeapons = 0xD60;        // u64[2]: an actor's weapon actors
constexpr std::uintptr_t kCastSpell = 0x3A9DD0;  // (actor, spell id): the command's cast (also sets +0xDA0); debug only
constexpr std::uintptr_t kAttackNew = 0x3CF830;  // YS::ATTACK constructor (self, owner, ...) -> self
constexpr std::uint8_t kAttackNewBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                            0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kCanHit = 0x3D2060;  // (attack, actor) -> can it hit that actor
constexpr std::uint8_t kCanHitBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA};
constexpr std::uintptr_t kSpellSlots = 0x2A23A80;  // 6 x 0x50: +8 script, +0x18 effects, +0x48 data (+0x26 spell id)
constexpr std::uintptr_t kMagicList = 0x2A23A70;   // first entry of the spells' threads {thread, actor, f32, next}
constexpr std::uintptr_t kScriptThread = 0x2A250B0;  // the script thread running now (+0x60 its actor), else 0
constexpr std::uintptr_t kCastEntry = 0xDA0, kActorFlags124 = 0x124;
constexpr std::uintptr_t kListHead = 0x2A171C8;     // the actor list; its first actor is our Sora
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;  // u64[2] companion actors
constexpr std::uintptr_t kNow = 0x717008;           // u8 world, room
constexpr std::uintptr_t kNextActor = 0xA90, kObjRow = 0x918;
constexpr std::uintptr_t kActorMotionSet = 0x158;  // the actor's motion state: +0 its own file (a BAR), +0x10 the actor
constexpr std::uintptr_t kActorMotion = 0x180, kActorMotionBank = 0x2B8;
// shortcut: a played motion is kept 500 ms ('young') before the sender's pose shows it; a pose later than that under
// lag drops it. Real state: the sender's motion at the event's playback time. TODO 8.11; 6.3.
constexpr std::uint32_t kKeepMs = 500;      // how long a played motion waits for the sender's pose to show it
// An event waits at most this long for the playback to reach it; the sender gives it up as long after it first went.
constexpr std::uint32_t kLateMs = 1000;

// Returns the new capture id, which trap_limit_motion_start (2/47) hands to the Limit script.
using PFN_FileMotion = int(__fastcall*)(void* owner, std::uintptr_t bar, void* paxSlot, std::uintptr_t target,
                                        int motion, float blend, int* users);
using PFN_Capture = int(__fastcall*)(std::uintptr_t* motionSet, std::uintptr_t target, int motion, float blend);
PFN_FileMotion g_realFileMotion = nullptr;
PFN_Capture g_realCapture = nullptr;
using PFN_MotionAttack = void(__fastcall*)(std::uint32_t* set, int a, int b, std::uintptr_t c, int d);
PFN_MotionAttack g_realMotionAttack = nullptr;
using PFN_Cast = std::uintptr_t(__fastcall*)(std::uintptr_t actor, std::uint32_t spell, std::uint8_t air);
PFN_Cast g_realCast = nullptr;
using PFN_HitEffect = std::uintptr_t(__fastcall*)(std::uintptr_t inst, std::uint64_t mask, const float* place,
                                                  std::uint32_t a, std::uint32_t b, std::uint32_t c);
PFN_HitEffect g_realHitEffect = nullptr;
bool g_hitsOn = false;
std::uint32_t g_hitsSent = 0, g_hitsPlayed = 0, g_hitsDropped = 0;
using PFN_AttackNew = std::uintptr_t(__fastcall*)(std::uintptr_t self, std::uintptr_t owner, std::uintptr_t a3,
                                                  std::uint32_t a4, std::uint32_t a5, std::int32_t a6, std::uint32_t a7);
PFN_AttackNew g_realAttackNew = nullptr;
using PFN_CanHit = std::uint64_t(__fastcall*)(std::uintptr_t attack, std::uintptr_t actor);
PFN_CanHit g_realCanHit = nullptr;
// The game's Limit preload (L155, F356): each frame while no Limit runs in this game, when a usable Limit's files are
// out, it drops the Drive area from 5000, asks the usable Limits' files again and lays the heap out with no load wait,
// so the Limit files move. The game's own Limit holds it (exe+0x3D8830); the other player's Limit didn't (TODO 1.119).
constexpr std::uintptr_t kLimitPreload = 0x3E7650;
constexpr std::uint8_t kLimitPreloadBytes[] = {0x40, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30};
using PFN_LimitPreload = void(__fastcall*)();
PFN_LimitPreload g_realLimitPreload = nullptr;
const char* g_preloadWhy = nullptr;  // why our Limit preload waits now, null: it runs
std::uint64_t g_preloadSince = 0;
std::uint32_t g_preloadHolds = 0;

enum : std::uint8_t { kKindFile = 1, kKindCapture = 2, kKindSpell = 3, kKindHit = 4, kKindLimitFx = 5 };
// Who an actor is, in terms both games share: the sender's Sora (the receiver's copy of it), one of the host's
// companions (the friend's mirrored one with that object id), or the actor at the same exe offset (actors from a
// room load have the same address in both games, VERIFIED_OFFSETS).
enum : std::uint8_t { kRoleNone, kRoleSora, kRoleCompanion, kRoleSameAddress };

#pragma pack(push, 1)
struct Event {
    std::uint32_t id;
    std::uint32_t senderMs;  // the sender's clock when it played it
    std::uint16_t room;      // the sender's world and room (kNow)
    std::uint8_t kind, targetRole, sourceRole, pad;
    std::uint32_t targetObj;          // the target's object id
    std::uint32_t source, sourceObj;  // kKindCapture: the source (exe offset for kRoleSameAddress) and its object id;
                                      // kKindSpell: source = in the air (the cast's flag)
    std::int32_t motion;              // kKindSpell: the spell id
    float blend;
    std::uint32_t file, fileCount;  // the motion file (exe offset; for a capture only logged) and its entry count
    std::uint32_t pax;              // kKindFile: its effects (PAX) as an exe offset (not played yet)
    std::uint32_t fileHash, namesHash;  // kKindFile: what the file holds (BarPrint), to find it in the other game
    std::uint16_t sourceNet, sourceInstance;  // kKindCapture: world sync's host number for the source and the host's
                                              // room instance (0 = not an enemy world sync pairs)
    float place[4];                           // kKindHit: where the hit's effect goes (source = 0 the companion,
    std::uint32_t hitA, hitB, hitC;           // 1-2 its weapon) and its three values,
    std::uint64_t hitMask;                    // and which effect: the instance's first entry with all these bits
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kPerPacket = 12;
static_assert(sizeof(Packet) + kPerPacket * sizeof(Event) <= reliable::kMaxPayload, "a packet is one reliable message");

bool g_on = false, g_host = false;
bool g_replaying = false;  // our own call for the peer's motion is running: not ours to send
bool g_muted = false;  // another module replays the peer's game on our Sora (MotionMirrorMute): not ours to send
std::uintptr_t g_replayTarget = 0;  // its target: attacks its first frame makes come during the call

// Sender: each event goes once, on the reliable channel's motion stream, so the other game plays them in our order
// (a motion chain on one copy); the frame's new events share one message.
struct Queued {
    Event e;
    int sent;
};
Queued g_queue[32];
int g_queueNext = 0;
std::uint32_t g_nextId = 1;
std::uint32_t g_sent = 0, g_notSentLogs = 0;
// Captures whose source was our paired copy; frames one waited; captures not played: their source in a shared move.
std::uint32_t g_sourcePaired = 0, g_sourceWaits = 0, g_sharedSkips = 0;
// Sender: a capture whose source world sync hasn't numbered yet. A script that makes an actor often has it capture
// in its first update (the Twin Lords' ice block: made at frame 5607, freezes Aladdin at 5608), and world sync numbers
// it right after that update (WorldSyncAfterUpdate): so it waits a few frames for the number, then goes with its time.
constexpr int kNumberFrames = 5;
struct Unnumbered {
    Event e;
    std::uintptr_t target, source;
    std::uint8_t role;
    std::uint32_t at;  // when it was played (our clock)
    int frames;
};
Unnumbered g_unnumbered[8];
std::uint32_t g_numberedLate = 0, g_neverNumbered = 0;

// Receiver.
struct Pending {
    Event e;
    std::uint64_t arrived;
};
Pending g_pending[16];
int g_pendingCount = 0;
struct Kept {  // a copy playing a motion the peer sent, until the peer's pose leaves it
    std::uintptr_t actor, bank;
    int motion;
    std::uint64_t tick;
    bool spell;  // a spell's cast motion (bank 0): kept while the peer's motion is the same. Setting it again
                 // restarts it, which ends the effects the spell started on it, and the spell's script then used
                 // a freed effect (friend crash exe+0x2C2830, rcx 0xEFACCAFE..., bench 2026-10-12 22:10:59)
    bool limit;  // a Limit's motion: our Limit preload waits while the actor plays it (HookLimitPreload)
};
Kept g_kept[4];
std::uint32_t g_played = 0, g_skipped = 0, g_playLogs = 0, g_skipLogs = 0, g_attacksDropped = 0;
std::uint64_t g_statsTick = 0;
std::uint32_t g_statsSeen = 0;
// The Limit's effects: the game passes the Limit object's PAX instance (limit +0x18: PAX, its entries, then the
// effects it made; filled by exe+0x2C6320, which also links it into a game list by handle), and the motion's
// triggers start effects from it (exe+0x40DD70 -> exe+0x2C6B30). Our copy has no Limit object: pointing the game at
// the PAX alone crashed there (bench 2026-10-12 21:04, exe+0x2C6B60 read a null entry list). An empty instance makes
// the triggers start nothing (exe+0x40DD70 checks its PAX for 0): the copy moves without the Limit's sparkles.
std::uint64_t g_noEffects[8] = {};
// The other player's Limit's effects in our game (TODO 1.31, EFFECT_SYNC_PLAN 7, F470): a whole holder of our own,
// filled from our copy of the Limit's PAX as the game fills its Limit's (exe+0x3D96D0, L258). The Limit's landed hits
// are made from it at the sender's place (kHitLimit), and with KH2COOP_LIMIT_FX_TRIGGERS=1 the replayed motion's
// triggers too (L257: they send key 0x10 to the actor's own script, not read yet, so off by default). Static memory:
// motion states keep the pointer raw (L257), so it is never freed; it costs one handle-table slot (L259).
// Kept rule (LimitFxLive): its +0 is still the PAX we filled it with, the game still lists it, that PAX's file is the
// same live entry, and the room is the same. It is ended (its effects killed, unlinked) while the file is in place:
// before a group free or layout that could free or move the file (copy_blade.cpp calls MotionMirrorFileChange), when
// our Limit preload may run again, and on a room change.
constexpr std::uint32_t kHitLimit = 3;            // a kKindHit's source: the Limit's own holder
alignas(16) std::uint8_t g_limitFx[0x30] = {};
std::uintptr_t g_limitFxPax = 0, g_limitFxEntry = 0;  // what it was filled with, and the file entry holding that
std::uint16_t g_limitFxRoom = 0;
bool g_limitFxHits = false, g_limitFxTriggers = false, g_limitFxScript = false;
std::uint32_t g_limitHitsSent = 0, g_limitHitsPlayed = 0, g_limitHitsSkipped = 0, g_limitFxFills = 0;
// The Limit script's own effects (L260, KH2COOP_LIMIT_FX_SCRIPT=1): its three start calls (2/57 plain, 2/58 at a
// place, 2/59 bound) all make them from the running Limit's holder through exe+0x3D9B60(limit, id, a, owner), then
// the script places them (trap_effect_set_pos, mostly in the same frame) and ends them (kill, fadeout, loop end, loop
// end + kill). The doer's game numbers each start, sends it at the end of its frame with the effect's place and its
// owner, then each end call on it; the other game makes it from its holder with the same call (our holder - 0x18 as
// the 'Limit': the call only adds 0x18) and runs the end calls on it by number, as world mirror does (E2 a).
// Kept rules: doer: the script's handle, while the effect keeps the part it had at its start (another part = the
// handle went to a new effect). Receiver: the effect's record is still on our holder with the same effect, and the
// effect keeps its part (or, for a kill, ran out by itself: the kill finishes it, world_mirror.cpp EndedNotFinished).
// shortcut: later moves (set_pos / set_dir after the start's frame, 16 in one Limit's log) aren't sent; send them as
// world mirror's states (latest value only) once a Limit is seen needing them.
constexpr std::uintptr_t kLimitEffectStart = 0x3D9B60;  // (limit, id, a, owner): exe+0x2C6B30(limit + 0x18, id, a, 1, 30000, owner)
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // a script value's tag for an address ("@ADR")
constexpr std::uintptr_t kFxFlags = 0xC, kFxPart = 0x30;   // effect: +0xC bit 16 = killed, +0x30 its part (+0: place)
constexpr std::uint32_t kFxKilled = 0x10000;
struct OurFx {  // doer: an effect our Limit's script started
    std::uint32_t handle, n, id, a;
    std::uintptr_t part, owner;
    std::uint8_t start, role;  // the start call (57 / 58 / 59), the owner's role
    bool sent;
};
OurFx g_ourFx[32] = {};
std::uint32_t g_ourFxNext = 0, g_ourFxCount = 0;  // the next slot; the last number given
struct TheirFx {  // receiver: an effect we made for the other player's Limit
    std::uint32_t n;
    std::uintptr_t fx, record, part;
};
TheirFx g_theirFx[32] = {};
int g_theirFxNext = 0;
std::uint32_t g_scriptFxSent = 0, g_scriptFxEnds = 0, g_scriptFxMade = 0, g_scriptFxSkipped = 0, g_scriptFxEndsRun = 0,
              g_scriptFxEndsGone = 0;
// Limit files found by their contents at another address than the sender's (FindBar), and recent misses.
struct FoundBar {
    std::uint32_t fileHash, namesHash;
    std::uintptr_t bar;     // 0 = not found
    std::uint64_t tick;     // when it was looked for (a miss is looked for again after 5 s)
};
FoundBar g_foundBars[8] = {};
int g_foundNext = 0;
// Spells cast on our copy: its newest one (the cast trigger lets it go once), and the attacks its script made.
std::uintptr_t g_castActor = 0, g_castEntry = 0;
bool g_castTriggered = false;
bool g_casting = false;  // inside our cast call: its cast motion may go through the puppet's motion block
std::uintptr_t g_quietAttacks[32] = {};
int g_quietNext = 0;
std::uint32_t g_spellsSent = 0, g_spellsCast = 0, g_quietMade = 0, g_quietHits = 0, g_castTriggers = 0;
std::uint32_t g_limitlessHits = 0;  // hits stopped: a Limit's attack with no Limit running here (TODO 1.118)
std::uint32_t g_carrierHits = 0;    // hits stopped: a ride (world_sync.cpp's carriers) taking the copy (TODO 1.126)
int g_debugCast = -1;  // test command: our Sora casts the spell of this slot (0-5) next frame

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uint16_t Room() { return Read<std::uint16_t>(ExeBase() + kNow); }

std::uint32_t ExeOffset(std::uintptr_t p) {
    std::uintptr_t exe = ExeBase();
    return p > exe && p < exe + 0x3000000 ? static_cast<std::uint32_t>(p - exe) : 0;
}

bool InList(std::uintptr_t x) {
    if (!x) return false;
    __try {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = DecodeHandle(Read<std::uint32_t>(a + kNextActor)))
            if (a == x) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

std::uint32_t ObjId(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        return ExeOffset(row) ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = a ? Read<std::uintptr_t>(a + kObjRow) : 0;
        return ExeOffset(row) ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// The entry count of the BAR at p (the motion files and the Limit file are BARs); 0 if p isn't one.
std::uint32_t BarCount(std::uintptr_t p) {
    if (!ExeOffset(p)) return 0;
    __try {
        return std::memcmp(reinterpret_cast<const void*>(p), "BAR", 3) == 0 ? Read<std::uint32_t>(p + 4) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// What a BAR holds, the same in both games: its entry count and each 16-byte entry (from +0x10: u16 type, u16, char
// name[4], u32 where its data is, u32 size) without the data's place, which the game rewrites in a file in use.
// `names` leaves out the sizes too (a fallback, in case a size also changes in use).
struct BarPrint {
    std::uint32_t full, names;
};

bool PrintBar(std::uintptr_t bar, std::uint32_t count, BarPrint& out) {
    if (!count || count > 4096) return false;
    __try {
        std::uint32_t full = 2166136261u ^ count, names = full;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto* entry = reinterpret_cast<const std::uint8_t*>(bar + 0x10 + 0x10 * i);
            for (int k = 0; k < 16; ++k) {
                if (k >= 8 && k < 12) continue;
                full = (full ^ entry[k]) * 16777619u;
                if (k < 8) names = (names ^ entry[k]) * 16777619u;
            }
        }
        out = {full, names};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Readable(const MEMORY_BASIC_INFORMATION& mi) {
    constexpr DWORD kRead = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mi.State == MEM_COMMIT && (mi.Protect & kRead) && !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

// The BARs in [from, to) (16-byte steps) with this many entries and these contents, in a file the game has loaded and
// in place: the first full match, else the only one whose names match. Counts the name matches in `namesHits`, and in
// `stale` the matches no loaded file holds (a copy left behind by a move or a let-go file).
std::uintptr_t ScanBars(std::uintptr_t from, std::uintptr_t to, std::uint32_t count, std::uint32_t fileHash,
                        std::uint32_t namesHash, std::uintptr_t& namesOnly, int& namesHits, int& stale) {
    __try {
        for (std::uintptr_t q = (from + 15) & ~std::uintptr_t(15); q + 16 <= to; q += 16) {
            if ((*reinterpret_cast<const std::uint32_t*>(q) & 0xFFFFFF) != 0x524142 ||  // "BAR"
                *reinterpret_cast<const std::uint32_t*>(q + 4) != count)
                continue;
            BarPrint p;
            if (!PrintBar(q, count, p) || (p.full != fileHash && p.names != namesHash)) continue;
            if (!FormLookLiveFileAt(q)) {
                ++stale;
                continue;
            }
            if (p.full == fileHash) return q;
            namesOnly = q;
            ++namesHits;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

// One BAR, for the log: where, how many entries, its prints and its first entries' names.
void LogBar(const char* what, std::uintptr_t bar, std::uint32_t count) {
    BarPrint p{};
    char names[48] = {};
    __try {
        PrintBar(bar, count, p);
        for (std::uint32_t i = 0, n = 0; i < count && i < 4; ++i)
            n += std::snprintf(names + n, sizeof(names) - n, "%s%.4s", i ? " " : "",
                               reinterpret_cast<const char*>(bar + 0x10 + 0x10 * i + 4));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("motion mirror: %s exe+0x%X: %u entries, contents %08X, names %08X, first entries %s", what, ExeOffset(bar), count,
        p.full, p.names, names);
}

// Research, after a miss: the BARs in the exe's memory with about as many entries (motion files have 200-400).
void LogBarsLike(std::uint32_t count) {
    std::uintptr_t exe = ExeBase(), end = exe + 0x3000000;
    int logged = 0;
    for (std::uintptr_t p = exe; p < end && logged < 16;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<const void*>(p), &mi, sizeof(mi))) break;
        std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (regionEnd > end) regionEnd = end;
        if (Readable(mi)) {
            __try {
                for (std::uintptr_t q = (p + 15) & ~std::uintptr_t(15); q + 16 <= regionEnd && logged < 16; q += 16) {
                    if ((*reinterpret_cast<const std::uint32_t*>(q) & 0xFFFFFF) != 0x524142) continue;
                    std::uint32_t c = *reinterpret_cast<const std::uint32_t*>(q + 4);
                    if (c + 20 < count || c > count + 20) continue;
                    LogBar("  a BAR like it:", q, c);
                    ++logged;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        p = regionEnd;
    }
    if (!logged) Log("motion mirror:   no BAR with %u +/- 20 entries in the exe's memory", count);
}

// Receiver: a BAR in our game with the sender's Limit file's contents (it holds the party's Limit files from the room
// load, VERIFIED_OFFSETS 'Limit motions'), in a file the game has loaded and in place, or 0. The exe's memory (where
// those files are, e.g. exe+0x1A42AD0) is scanned once per file; the result is kept while that file stays live.
std::uintptr_t FindBar(std::uint32_t count, std::uint32_t fileHash, std::uint32_t namesHash, std::uint32_t theirs) {
    std::uint64_t now = GetTickCount64();
    for (FoundBar& f : g_foundBars) {
        if (f.fileHash != fileHash || f.namesHash != namesHash || !f.tick) continue;
        BarPrint p;
        if (f.bar && FormLookLiveFileAt(f.bar) && BarCount(f.bar) == count && PrintBar(f.bar, count, p) &&
            p.names == namesHash)
            return f.bar;
        if (!f.bar && now - f.tick < 5000) return 0;
        f = {};  // moved or gone: look again
    }
    std::uintptr_t exe = ExeBase(), end = exe + 0x3000000, found = 0, namesOnly = 0;
    int namesHits = 0, stale = 0;
    for (std::uintptr_t p = exe; p < end && !found;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<const void*>(p), &mi, sizeof(mi))) break;
        std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (regionEnd > end) regionEnd = end;
        if (Readable(mi)) found = ScanBars(p, regionEnd, count, fileHash, namesHash, namesOnly, namesHits, stale);
        p = regionEnd;
    }
    if (!found && namesHits == 1) found = namesOnly;
    // A miss with a left-behind copy isn't kept: the live file may be read in or placed a moment later.
    if (found || !stale) g_foundBars[g_foundNext++ % 8] = {fileHash, namesHash, found, now};
    const char* who = g_host ? "friend" : "host";
    if (found) {
        Log("motion mirror: the %s's Limit file (exe+0x%X there, %u entries) is at exe+0x%X in our game (found by its "
            "contents%s)", who, theirs, count, ExeOffset(found), found == namesOnly ? ", names only" : "");
    } else {
        Log("motion mirror: the %s's Limit file (exe+0x%X there, %u entries, contents %08X, names %08X): no loaded file "
            "with the same contents in our game%s; %d left-behind copies no loaded file holds (moved or let go)", who,
            theirs, count, fileHash, namesHash, namesHits > 1 ? " (several with the same names)" : "", stale);
        if (!stale) LogBarsLike(count);
    }
    return found;
}

std::uintptr_t ReadPtr(std::uintptr_t p) {
    __try {
        return Read<std::uintptr_t>(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool IsPax(std::uintptr_t p) {
    if (!ExeOffset(p)) return false;
    __try {
        return std::memcmp(reinterpret_cast<const void*>(p), "PAX_", 4) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uintptr_t ScriptActor() {
    __try {
        std::uintptr_t thread = Read<std::uintptr_t>(ExeBase() + kScriptThread);
        return thread ? Read<std::uintptr_t>(thread + 0x60) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The copy's spell script is running now (the copy runs no other script: puppet.cpp HookRunCommand).
bool CopySpellRunning() {
    std::uintptr_t copy = PuppetCloneActor();
    return copy && ScriptActor() == copy;
}

// The spell slot (0-5) that holds `spell` in our game, or -1.
int SpellSlot(std::uint32_t spell) {
    __try {
        for (int i = 0; i < 6; ++i) {
            std::uintptr_t data = Read<std::uintptr_t>(ExeBase() + kSpellSlots + 0x50 * i + 0x48);
            if (data && Read<std::uint16_t>(data + 0x26) == spell) return i;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return -1;
}

void LogSpellSlots() {
    char text[160] = {};
    int n = 0;
    __try {
        for (int i = 0; i < 6; ++i) {
            std::uintptr_t data = Read<std::uintptr_t>(ExeBase() + kSpellSlots + 0x50 * i + 0x48);
            n += std::snprintf(text + n, sizeof(text) - n, " %d:0x%X", i, data ? Read<std::uint16_t>(data + 0x26) : 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("motion mirror: our spell slots (slot:spell id, 0 = empty):%s", n ? text : " unreadable");
}

// `entry` is still on the spells' thread list (it's freed when its script ends). Links are handles (+0xC).
bool OnMagicList(std::uintptr_t entry, bool log = false) {
    int n = 0;
    std::uint32_t last = 0;
    bool found = false;
    __try {
        std::uintptr_t e = Read<std::uintptr_t>(ExeBase() + kMagicList);
        for (; e && n < 128; ++n) {
            if (e == entry) {
                found = true;
                break;
            }
            last = Read<std::uint32_t>(e + 0xC);
            e = DecodeHandle(last);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (log)
        Log("motion mirror: spells' thread list: %d entries before %s (last link 0x%08X), head exe+0x%X, tail exe+0x%X",
            n, found ? "ours" : "its end", last, ExeOffset(ReadPtr(ExeBase() + kMagicList)),
            ExeOffset(ReadPtr(ExeBase() + kMagicList + 8)));
    return found;
}

// ---- Sender ----

std::uint8_t RoleOf(std::uintptr_t a, std::uint32_t& where) {
    where = 0;
    if (!a) return kRoleNone;
    if (a == Read<std::uintptr_t>(ExeBase() + kListHead)) return kRoleSora;
    if (a == PuppetCloneActor() || a == PuppetDrivenActor()) return kRoleNone;  // our copy of the peer
    if (g_host || PuppetOurLimitRunning() || PuppetLimitCommand())  // the friend: its own Limit's partner
        for (int slot = 0; slot < 2; ++slot)
            if (a == Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot)) return kRoleCompanion;
    if (!InList(a)) return kRoleNone;
    where = static_cast<std::uint32_t>(a - ExeBase());
    return kRoleSameAddress;
}

void NotSent(const char* what, std::uintptr_t target, int motion, const char* why) {
    if (g_notSentLogs++ < 20) Log("motion mirror: %s motion %d on %s not sent: %s", what, motion, Model(target), why);
}

void Queue(Event& e, std::uintptr_t target, std::uint8_t role, std::uint32_t at = 0) {
    e.id = g_nextId++;
    e.senderMs = at ? at : AvatarLinkNowMs();
    e.room = Room();
    e.targetRole = role;
    e.targetObj = target ? ObjId(target) : 0;  // a Limit script effect with no owner
    Queued& q = g_queue[g_queueNext++ % 32];
    q.e = e;
    q.sent = 0;
    ++g_sent;
}

// An effect's part while it isn't killed, else 0.
std::uintptr_t FxPart(std::uintptr_t fx) {
    std::uint32_t flags = 0;
    std::uintptr_t part = 0;
    return fx && ReadAny(fx + kFxFlags, &flags) && !(flags & kFxKilled) && ReadAny(fx + kFxPart, &part) ? part : 0;
}

bool EndsScriptFx(std::uint32_t index) { return index == 51 || index == 85; }  // kill, loop end + kill

// Doer: a start our Limit's script made, with where the effect is now (its start call or the set_pos after it).
void SendScriptFxStart(OurFx& f) {
    f.sent = true;
    std::uintptr_t fx = DecodeHandle(f.handle), part = FxPart(fx);
    std::uint64_t place[2] = {};
    if (!part || part != f.part || !ReadAny(part, &place[0]) || !ReadAny(part + 8, &place[1]))
        return;  // gone already: nothing to show
    Event e{};
    std::memcpy(e.place, place, sizeof(e.place));
    e.kind = kKindLimitFx;
    e.source = f.start;
    e.hitA = f.n, e.hitB = f.id, e.hitC = f.a;
    Queue(e, f.owner, f.role);
    ++g_scriptFxSent;
}

// Doer, from world_census.cpp's script call thunks (after the call): our Limit script's effect starts and the end
// calls on those effects.
void OnLimitTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (table == 2) {
        std::uintptr_t ours = LimitSyncOurLimitEffects();
        std::uint32_t h = static_cast<std::uint32_t>(result);
        if (!ours || argc < 3 || DecodeHandle(static_cast<std::uint32_t>(args[0])) + 0x18 != ours) return;
        std::uintptr_t part = FxPart(DecodeHandle(h)), owner = 0;
        if (!part) return;
        std::uint8_t role = kRoleNone;
        if (index == 59) {  // bound to an object whose +4 is its actor's handle
            std::uintptr_t obj = DecodeHandle(static_cast<std::uint32_t>(args[2]));
            std::uint32_t ah = 0;
            owner = obj && ReadAny(obj + 4, &ah) ? DecodeHandle(ah) : 0;
            std::uint32_t where;
            role = RoleOf(owner, where);
            if (role != kRoleSora && role != kRoleCompanion) return;  // an owner the other game can't name
        }
        OurFx* f = nullptr;  // the slot with this handle (its old effect is gone), else the next
        for (OurFx& o : g_ourFx)
            if (o.handle == h) f = &o;
        if (!f) f = &g_ourFx[g_ourFxNext++ % 32];
        *f = {h, ++g_ourFxCount, static_cast<std::uint32_t>(args[1]),
              static_cast<std::uint32_t>(args[index == 57 ? 2 : 3]), part, owner, static_cast<std::uint8_t>(index), role,
              false};
        return;
    }
    std::uint32_t h = static_cast<std::uint32_t>(args[0]);
    OurFx* f = nullptr;
    for (OurFx& o : g_ourFx)
        if (h && o.handle == h) f = &o;
    if (!f) return;
    std::uintptr_t part = FxPart(DecodeHandle(h));
    if (part && part != f->part) {  // the handle went to a new effect
        *f = {};
        return;
    }
    if (!f->sent) {
        if (EndsScriptFx(index)) {  // started and ended in one frame: nothing to show
            *f = {};
            return;
        }
        SendScriptFxStart(*f);
    }
    Event e{};
    e.kind = kKindLimitFx;
    e.source = static_cast<std::uint32_t>(index);
    e.hitA = f->n;
    e.hitB = argc > 1 ? static_cast<std::uint32_t>(args[1]) : 0;  // fadeout's two numbers
    e.hitC = argc > 2 ? static_cast<std::uint32_t>(args[2]) : 0;
    Queue(e, 0, kRoleNone);
    ++g_scriptFxEnds;
    if (EndsScriptFx(index)) *f = {};
}

// Doer, each frame: the starts of this frame go (their set_pos ran by now); our Limit over: forget them.
void SendScriptFx() {
    bool running = LimitSyncOurLimitEffects() != 0;
    for (OurFx& f : g_ourFx) {
        if (!f.handle) continue;
        if (!running) f = {};
        else if (!f.sent) SendScriptFxStart(f);
    }
}

void OnFileMotion(std::uintptr_t bar, void* paxSlot, std::uintptr_t target, int motion, float blend) {
    std::uint32_t where;
    std::uint8_t role = RoleOf(target, where);
    if (role != kRoleSora && role != kRoleCompanion) return;  // others' motions aren't sent (yet)
    std::uintptr_t pax = paxSlot ? ReadPtr(reinterpret_cast<std::uintptr_t>(paxSlot)) : 0;
    Event e{};
    e.kind = kKindFile;
    e.motion = motion;
    e.blend = blend;
    e.file = ExeOffset(bar);
    e.fileCount = BarCount(bar);
    e.pax = IsPax(pax) ? ExeOffset(pax) : 0;
    if (!e.fileCount) return NotSent("Limit", target, motion, "its file isn't a BAR in the game's memory");
    BarPrint print;
    if (!PrintBar(bar, e.fileCount, print)) return NotSent("Limit", target, motion, "its file's entries can't be read");
    e.fileHash = print.full;
    e.namesHash = print.names;
    static std::uintptr_t logged[8];  // each of our Limit files once, for comparing with the other game's log
    bool seen = false;
    for (std::uintptr_t b : logged) seen = seen || b == bar;
    static int next = 0;
    if (!seen) {
        logged[next++ % 8] = bar;
        LogBar("our Limit file", bar, e.fileCount);
    }
    // No PAX found is no reason to drop it: the receiver plays it with its own empty effect instance (g_noEffects).
    // Bench 2026-10-14: the friend's Limit 0x26C in Agrabah had none, nothing was sent and its copy stood idle.
    if (!g_host && role == kRoleCompanion) WorldSyncOurLimitPartner(target);
    Queue(e, target, role);
}

void OnCapture(std::uintptr_t* motionSet, std::uintptr_t target, int motion, float blend) {
    std::uint32_t where;
    std::uint8_t role = RoleOf(target, where);
    if (role != kRoleSora && role != kRoleCompanion) return;
    std::uintptr_t source = motionSet[2];  // +0x10: the actor
    Event e{};
    e.kind = kKindCapture;
    e.motion = motion;
    e.blend = blend;
    e.sourceRole = RoleOf(source, e.source);
    e.sourceObj = ObjId(source);
    // The host's number for it: the host's own, or the one our copy is paired with.
    e.sourceNet = g_host ? WorldSyncNetIdOf(source, &e.sourceInstance) : WorldSyncBoundTo(source, &e.sourceInstance);
    e.file = ExeOffset(motionSet[0]);
    e.fileCount = BarCount(motionSet[0]);
    if (!e.fileCount) return NotSent("captured", target, motion, "its source's file isn't a BAR");
    if (!e.sourceNet) {
        for (Unnumbered& u : g_unnumbered)
            if (!u.source) {
                u = {e, target, source, role, AvatarLinkNowMs(), 0};
                return;
            }
    }
    if (e.sourceRole == kRoleNone && !e.sourceNet)
        return NotSent("captured", target, motion, "its source isn't an actor the peer has");
    Queue(e, target, role);
}

// Each frame: the captures waiting for their source's number go once it has one, or after kNumberFrames as they are.
void SendNumbered() {
    for (Unnumbered& u : g_unnumbered) {
        if (!u.source) continue;
        u.e.sourceNet = g_host ? WorldSyncNetIdOf(u.source, &u.e.sourceInstance)
                               : WorldSyncBoundTo(u.source, &u.e.sourceInstance);
        if (!u.e.sourceNet && ++u.frames < kNumberFrames) continue;
        if (u.e.sourceNet)
            ++g_numberedLate;
        else
            ++g_neverNumbered;
        if (u.e.sourceRole == kRoleNone && !u.e.sourceNet)
            NotSent("captured", u.target, u.e.motion, "its source isn't an actor the peer has");
        else
            Queue(u.e, u.target, u.role, u.at);
        u.source = 0;
    }
}

int __fastcall HookFileMotion(void* owner, std::uintptr_t bar, void* paxSlot, std::uintptr_t target, int motion,
                              float blend, int* users) {
    int id = g_realFileMotion(owner, bar, paxSlot, target, motion, blend, users);
    if (!g_on || g_replaying || g_muted) return id;
    __try {
        OnFileMotion(bar, paxSlot, target, motion, blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return id;
}

int __fastcall HookCapture(std::uintptr_t* motionSet, std::uintptr_t target, int motion, float blend) {
    int id = g_realCapture(motionSet, target, motion, blend);
    if (!g_on || g_replaying || g_muted) return id;
    __try {
        OnCapture(motionSet, target, motion, blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return id;
}

std::uintptr_t __fastcall HookCast(std::uintptr_t actor, std::uint32_t spell, std::uint8_t air) {
    std::uintptr_t entry = g_realCast(actor, spell, air);
    if (!g_on || g_replaying || g_muted || !entry) return entry;
    __try {
        if (actor == Read<std::uintptr_t>(ExeBase() + kListHead)) {
            Event e{};
            e.kind = kKindSpell;
            e.motion = static_cast<std::int32_t>(spell & 0xFFFF);
            e.source = air;
            Queue(e, actor, kRoleSora);
            ++g_spellsSent;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return entry;
}

// Whose effect instance a hit's effect comes from: one of our party members (slot 0-1) itself (part 0) or one of its
// two weapons (1, 2: Goofy's shield). 0 = none of theirs.
std::uintptr_t CompanionOf(std::uintptr_t inst, std::uint32_t& part) {
    for (int slot = 0; slot < 2; ++slot) {
        std::uintptr_t c = Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot);
        if (!c) continue;
        part = 0;
        if (inst == c + 0x80) return c;
        for (std::uint32_t w = 0; w < 2; ++w) {
            std::uintptr_t weapon = Read<std::uintptr_t>(c + kWeapons + 8 * w);
            part = w + 1;
            if (weapon && inst == weapon + 0x80) return c;
        }
    }
    return 0;
}

// The companions' hits land only in the host's game (the friend's don't think), so their hit effects are the host's
// (DECISIONS E2b, TODO 1.128): the host sends each; the friend draws none of its mirrored companions' own (some of
// Goofy's motion hits land there too: they would double) and plays the host's (PlayHit).
std::uintptr_t __fastcall HookHitEffect(std::uintptr_t inst, std::uint64_t mask, const float* place, std::uint32_t a,
                                        std::uint32_t b, std::uint32_t c) {
    if (!g_on || g_replaying) return g_realHitEffect(inst, mask, place, a, b, c);
    std::uint32_t part = 0;
    std::uintptr_t companion = 0;
    __try {
        companion = CompanionOf(inst, part);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (!g_host && companion && WorldSyncSkipAi(companion)) {
        ++g_hitsDropped;
        return 0;
    }
    std::uintptr_t fx = g_realHitEffect(inst, mask, place, a, b, c);
    // Our Limit's landed hits are made from its own holder, which only our game has (F470): sent the same way, made
    // from the other game's holder of that Limit (LimitFxFor).
    bool limit = g_limitFxHits && !companion && inst && inst == LimitSyncOurLimitEffects();
    if (((g_host && companion) || limit) && fx && place) {
        Event e{};
        e.kind = kKindHit;
        e.source = limit ? kHitLimit : part;
        e.hitMask = mask;
        std::memcpy(e.place, place, sizeof(e.place));
        e.hitA = a, e.hitB = b, e.hitC = c;
        if (limit) {
            Queue(e, Read<std::uintptr_t>(ExeBase() + kListHead), kRoleSora);
            ++g_limitHitsSent;
        } else {
            Queue(e, companion, kRoleCompanion);
            ++g_hitsSent;
        }
    }
    return fx;
}

// Attacks made by the copy's spell script hit nothing in our game (the caster's game deals that damage).
std::uintptr_t __fastcall HookAttackNew(std::uintptr_t self, std::uintptr_t owner, std::uintptr_t a3, std::uint32_t a4,
                                        std::uint32_t a5, std::int32_t a6, std::uint32_t a7) {
    std::uintptr_t r = g_realAttackNew(self, owner, a3, a4, a5, a6, a7);
    if (!g_on) return r;
    for (std::uintptr_t& q : g_quietAttacks)
        if (q == self) q = 0;  // the memory of an earlier one
    if (self && CopySpellRunning()) {
        g_quietAttacks[g_quietNext++ % 32] = self;
        ++g_quietMade;
    }
    return r;
}

// A Limit's attack (its data's +0x12 bit 1) takes the running Limit's effects as its +0x28 handle (exe+0x3CF830 from
// exe+0x3D8A40); made where no Limit runs, that handle is 0 and the game's hit faults on it (exe+0x3D1C99, F156): the
// copy playing the peer's Limit Form Limits from its own bank (TODO 1.118). The hit pass asks us first, so such an
// attack hits nothing; the Limit's own game deals its damage.
bool LimitlessLimitAttack(std::uintptr_t attack) {
    __try {
        std::uintptr_t data = Read<std::uintptr_t>(attack + 0x30);
        return data && (Read<std::uint8_t>(data + 0x12) & 2) && Read<std::uint32_t>(attack + 0x28) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint64_t __fastcall HookCanHit(std::uintptr_t attack, std::uintptr_t actor) {
    if (g_on && attack && LimitlessLimitAttack(attack)) {
        if (g_limitlessHits++ < 10)
            Log("motion mirror: %s's Limit attack would hit %s with no Limit running here: no hit",
                Model(DecodeHandle(Read<std::uint32_t>(attack + 0x10))), Model(actor));
        return 0;
    }
    // A ride (TODO 1.126, world_sync.cpp's carriers): our game's tornado never takes the copy; the other player's own
    // game gives it its ride. Here, both games.
    if (g_on && attack && actor && actor == PuppetCloneActor()) {
        std::uintptr_t owner = DecodeHandle(Read<std::uint32_t>(attack + 0x10));
        if (owner && WorldSyncCarrier(owner)) {
            if (g_realCanHit(attack, actor) && g_carrierHits++ < 10)
                Log("motion mirror: our %s would take the copy: no hit (the other player's own game runs its ride)",
                    Model(owner));
            return 0;
        }
    }
    if (g_on && attack)
        for (std::uintptr_t q : g_quietAttacks)
            if (q == attack) {
                if (g_realCanHit(attack, actor) && g_quietHits++ < 10)  // a check only: to count what it stops
                    Log("motion mirror: the copy's spell attack would hit %s: no hit (the caster's game deals it)",
                        Model(actor));
                return 0;
            }
    return g_realCanHit(attack, actor);
}

void SendQueued() {
    char buf[sizeof(Packet) + kPerPacket * sizeof(Event)];
    Packet p{kMagic, kVersion, AvatarLinkBuild(), 0};
    int n = 0;
    for (int i = 0; i < 32 && n < kPerPacket; ++i) {  // oldest first
        Queued& q = g_queue[(g_queueNext + i) % 32];
        if (!q.e.id || q.sent) continue;
        std::memcpy(buf + sizeof(Packet) + n * sizeof(Event), &q.e, sizeof(Event));
        q.sent = 1;
        ++n;
    }
    if (!n) return;
    p.count = static_cast<std::uint8_t>(n);
    std::memcpy(buf, &p, sizeof(p));
    AvatarLinkSendReliable(buf, static_cast<int>(sizeof(Packet) + n * sizeof(Event)), kLateMs, kStreamMotion);
}

// ---- Receiver ----

bool PlaysSentMotion(std::uintptr_t actor) {
    for (const Kept& k : g_kept)
        if (k.actor && k.actor == actor && ReadPtr(actor + kActorMotionBank) == k.bank) return true;
    return false;
}

// The game's attack maker writes decode(owner)+0xCF0 unchecked (TODO 1.132, L114): an owner that no longer decodes
// (its handle's base slot empty, as the game's decode exe+0x4AD3F0 reads it) crashed the friend's game at
// exe+0x3D26A5 while a Drive or a Game Over sent a companion away. Such an attack can't be made: skip it.
std::uint32_t g_deadOwnerSkips = 0;

bool OwnerGone(std::uint32_t h) { return !h || !HandleBase(h); }

void __fastcall HookMotionAttack(std::uint32_t* set, int a, int b, std::uintptr_t c, int d) {
    std::uint32_t h = set ? *set : 0;
    if (OwnerGone(h)) {
        if (g_deadOwnerSkips++ < 10) {
            std::uintptr_t actor = reinterpret_cast<std::uintptr_t>(set) - 0xC10;  // the set is actor+0xC10 (L114)
            Log("motion mirror: an attack whose owner is gone (handle %08X) not made: actor %s%s, from exe+0x%llX "
                "(%u so far)",
                h, InList(actor) ? Model(actor) : "?", actor == PuppetDrivenActor() ? " (the Sora copy)" : "",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - ExeBase()),
                g_deadOwnerSkips);
        }
        return;
    }
    std::uintptr_t owner = g_on ? DecodeHandle(h) : 0;
    if (owner && (owner == g_replayTarget || PlaysSentMotion(owner))) {
        if (g_attacksDropped++ < 10)
            Log("motion mirror: no attack for our %s's sent motion (the sender's game makes it)", Model(owner));
        return;
    }
    g_realMotionAttack(set, a, b, c, d);
}

std::uintptr_t Resolve(std::uint8_t role, std::uint32_t where, std::uint32_t obj) {
    switch (role) {
        case kRoleSora:
            return PuppetDrivenActor();
        case kRoleCompanion:
            return WorldSyncMirroredCompanion(obj);
        case kRoleSameAddress: {
            std::uintptr_t a = ExeBase() + where;
            return InList(a) && ObjId(a) == obj ? a : 0;
        }
    }
    return 0;
}

const char* Whose(std::uint8_t role) {
    return role == kRoleSora ? "copy of their Sora"
           : role == kRoleCompanion ? (g_host ? "companion (the friend's Limit partner)" : "mirrored companion")
                                    : "actor";
}

void Skip(const Event& e, const char* why) {
    ++g_skipped;
    if (g_skipLogs++ < 30)
        Log("motion mirror: the %s's %s %d not played: %s", g_host ? "friend" : "host",
            e.kind == kKindFile ? "Limit motion" : e.kind == kKindCapture ? "captured motion" : "spell", e.motion, why);
}

// The game's own call, as the peer's game made it (bar set: a Limit's motion with its effects from `slot`, else a
// capture from source).
bool Call(const Event& e, std::uintptr_t target, std::uintptr_t bar, std::uintptr_t source, void* slot) {
    bool ok = true;
    g_replaying = true;
    g_replayTarget = target;
    __try {
        if (bar)
            g_realFileMotion(nullptr, bar, slot, target, e.motion, e.blend, nullptr);
        else
            g_realCapture(reinterpret_cast<std::uintptr_t*>(source + kActorMotionSet), target, e.motion, e.blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_replaying = false;
    g_replayTarget = 0;
    return ok;
}

void Keep(std::uintptr_t actor, std::uintptr_t bank, int motion, bool spell, bool limit) {
    Kept* k = nullptr;  // this actor's entry, else a free one, else the oldest
    for (Kept& c : g_kept)
        if (c.actor == actor) k = &c;
    for (Kept& c : g_kept)
        if (!k && !c.actor) k = &c;
    if (!k) {
        k = &g_kept[0];
        for (Kept& c : g_kept)
            if (c.tick < k->tick) k = &c;
    }
    *k = {actor, bank, motion, GetTickCount64(), spell, limit};
}

std::uintptr_t LimitFxHolder() { return reinterpret_cast<std::uintptr_t>(g_limitFx); }

bool LimitFxLive() {
    std::uintptr_t h = LimitFxHolder(), pax = 0;
    return g_limitFxPax && ReadAny(h, &pax) && pax == g_limitFxPax && Room() == g_limitFxRoom &&
           FormLookHolderListed(h) && FormLookLiveFileAt(pax) == g_limitFxEntry;
}

// Ends our holder while its file is still in place: its effects killed (the kill reads the file, L259), unlinked,
// zeroed. One the game let go of already (a map teardown) is only zeroed: its records went with the map.
void EndLimitFx(const char* why) {
    if (!g_limitFxPax) return;
    std::uintptr_t h = LimitFxHolder(), at = 0, next = 0;
    std::uint64_t size = 0;
    bool listed = FormLookHolderListed(h);
    // Its data still where the PAX is: +0x60 (+0x58 before any layout; a pending move's data stays at +0x60).
    bool inPlace = ReadAny(g_limitFxEntry + 0x60, &at) && ReadAny(g_limitFxEntry + 0x58, &next) &&
                   ReadAny(g_limitFxEntry + 0x50, &size) && (at ? at : next) <= g_limitFxPax &&
                   g_limitFxPax < (at ? at : next) + size;
    FormLookHolderEnd(h);
    for (TheirFx& t : g_theirFx) t = {};  // their effects went with it
    Log("motion mirror: the other player's Limit effects ended (%s)%s", why,
        !listed ? ": the game had let go of them already"
        : inPlace ? "" : ": BUG: its file wasn't in place any more (a change we didn't see)");
    g_limitFxPax = g_limitFxEntry = 0;
}

// Our holder for the other player's Limit whose live motion file is `bar`, filled once per Limit from our own copy of
// its PAX: the Limit file's type-0x12 entry, as the game finds it (exe+0x3D96D0, L258). 0 if there is none.
std::uintptr_t LimitFxFor(const Event& e, std::uintptr_t bar) {
    std::uintptr_t entry = FormLookLiveFileAt(bar), file = 0, pax = 0;
    if (!entry || !ReadAny(entry + 0x58, &file)) return 0;
    std::uint32_t n = BarCount(file);
    for (std::uint32_t i = 0; i < n && !pax; ++i) {
        std::uint16_t type = 0;
        std::uint32_t handle = 0;
        if (ReadAny(file + 0x10 + 0x10 * i, &type) && type == 0x12 && ReadAny(file + 0x18 + 0x10 * i, &handle))
            pax = DecodeHandle(handle);
    }
    static int logged = 0;
    const char* name = reinterpret_cast<const char*>(entry + 4);
    if (!IsPax(pax) || FormLookLiveFileAt(pax) != entry) {
        if (logged++ < 5)
            Log("motion mirror: the other player's Limit file %.40s has no PAX of its own (exe+0x%X): its effects aren't "
                "made here", name, ExeOffset(pax));
        return 0;
    }
    if (e.pax && pax != bar + e.pax - e.file && logged++ < 5)  // the sender's PAX, at the same place in the file
        Log("motion mirror: the other player's Limit PAX: ours at exe+0x%X, theirs exe+0x%X (file exe+0x%X, ours exe+0x%X)",
            ExeOffset(pax), e.pax, e.file, ExeOffset(bar));
    std::uintptr_t h = LimitFxHolder();
    if (g_limitFxPax == pax && LimitFxLive()) return h;
    EndLimitFx("another Limit's file");
    // The fill links it by handle: our DLL's 32 MB area takes one of the handle table's 64 slots for good (L259).
    std::uintptr_t area = h & ~std::uintptr_t(0x1FFFFFF);
    int freeSlots = 0;
    bool ours = false;
    for (std::uint32_t i = 0; i < 64; ++i) {
        std::uintptr_t base = HandleBase(0x80000000u | i << 25);
        freeSlots += !base;
        ours = ours || base == area;
    }
    if (!ours && freeSlots < 4) {
        if (logged++ < 5) Log("motion mirror: only %d handle slots free: the other player's Limit effects aren't made", freeSlots);
        return 0;
    }
    if (!FormLookHolderFill(h, pax)) {
        if (logged++ < 5) Log("motion mirror: BUG: our Limit effect holder is still listed: not filled again");
        return 0;
    }
    g_limitFxPax = pax;
    g_limitFxEntry = entry;
    g_limitFxRoom = Room();
    ++g_limitFxFills;
    Log("motion mirror: the other player's Limit effects: our holder filled from %.40s's PAX at exe+0x%X (handle slots "
        "free %d%s)", name, ExeOffset(pax), freeSlots, ours ? "" : ", our area takes one now");
    return h;
}

// Why our Limit preload waits, or null. The other player's Limit runs here: its partner and our copy play motions from
// its file, and a party member leaving for it changes which Limits are usable, so the preload moved that file under
// the replay (TODO 1.119: purecall on a destroyed motion object in exe+0x3C8A40, bench 2026-11-10 2 of 2). Or an actor
// of ours still plays a motion from a replayed Limit's file after it ended. Kept rule: the actor is in the actor list
// and still plays from the bank we gave it (a new actor at that address in another motion releases it).
const char* PreloadHold() {
    if (LimitSyncPeerLimit()) return "the other player's Limit runs";
    for (const Kept& k : g_kept)
        if (k.limit && InList(k.actor) && ReadPtr(k.actor + kActorMotionBank) == k.bank)
            return "an actor of ours still plays a motion from the other player's Limit file";
    return nullptr;
}

void __fastcall HookLimitPreload() {
    const char* why = PreloadHold();
    if (why != g_preloadWhy) {
        std::uint64_t now = GetTickCount64();
        if (!g_preloadWhy) {
            g_preloadSince = now;
            ++g_preloadHolds;
        }
        if (why)
            Log("motion mirror: our Limit preload waits: %s", why);
        else
            Log("motion mirror: our Limit preload runs again (held %.1f s)", (now - g_preloadSince) / 1000.0);
        g_preloadWhy = why;
    }
    if (why) return;
    EndLimitFx("our Limit preload may move its file now");
    g_realLimitPreload();
}

// The peer's spell on our copy: the game's own cast, as the command does it (exe+0x3A9DD0 also keeps the entry at
// +0xDA0 for the cast trigger and sets +0x124 bit 18).
void PlaySpell(const Event& e) {
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return Skip(e, "no Sora copy in this room");
    if (SpellSlot(static_cast<std::uint32_t>(e.motion)) < 0) {
        LogSpellSlots();
        return Skip(e, "that spell isn't loaded in our game");
    }
    std::uintptr_t entry = 0;
    bool ok = true;
    g_replaying = true;
    g_replayTarget = copy;
    g_casting = true;
    __try {
        entry = g_realCast(copy, static_cast<std::uint32_t>(e.motion), static_cast<std::uint8_t>(e.source ? 1 : 0));
        if (entry) {
            *reinterpret_cast<std::uintptr_t*>(copy + kCastEntry) = entry;
            *reinterpret_cast<std::uint32_t*>(copy + kActorFlags124) |= 0x40000;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_casting = false;
    g_replaying = false;
    g_replayTarget = 0;
    if (!ok) return Skip(e, "exception in the game's cast");
    if (!entry) return Skip(e, "the game didn't start it");
    g_castActor = copy;
    g_castEntry = entry;
    g_castTriggered = false;
    ++g_spellsCast;
    Keep(copy, 0, Read<std::int32_t>(copy + kActorMotion), true, false);
    if (g_playLogs++ < 40)
        Log("motion mirror: cast the %s's spell 0x%X on our copy of their Sora%s (motion now %d)", g_host ? "friend" : "host",
            e.motion, e.source ? " (in the air)" : "", Read<std::int32_t>(copy + kActorMotion));
}

bool IsSource(std::uintptr_t a, std::uint32_t obj) {
    return a && InList(a) && ObjId(a) == obj && ReadPtr(a + kActorMotionSet + 0x10) == a;
}

// A capture's source in our game: our copy world sync paired with the host's actor, else the actor at the same address.
std::uintptr_t CaptureSource(const Event& e, bool& paired) {
    paired = false;
    __try {
        if (e.sourceNet) {
            std::uintptr_t a = g_host ? WorldSyncHostActor(e.sourceNet, e.sourceInstance)
                                      : WorldSyncBoundActor(e.sourceNet, e.sourceInstance);
            if (IsSource(a, e.sourceObj)) {
                paired = true;
                return a;
            }
        }
        std::uintptr_t a = Resolve(e.sourceRole, e.source, e.sourceObj);
        return IsSource(a, e.sourceObj) ? a : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// A capture by an actor world sync pairs, which our game hasn't paired yet (a script made it a frame before the
// capture, and our copy of it is bound when the host's next packets come): it waits, up to kLateMs after it came.
bool SourceNotPairedYet(const Event& e) {
    bool paired;
    return e.kind == kKindCapture && e.sourceNet && e.room == Room() && !CaptureSource(e, paired);
}

// The other player's Limit's landed hit: made from our holder of that Limit at the sender's place (owner 0, as the
// game's own hits, L256), only in the effects' field mode (L256: mode 2 reads the owner).
void PlayLimitHit(const Event& e) {
    const char* why = !LimitFxLive() ? "no effects of the other player's Limit here (yet)"
                      : Read<std::uint32_t>(ExeBase() + kEffectMode) != 1 ? "the effects aren't in their field mode"
                                                                           : nullptr;
    std::uintptr_t fx = 0;
    if (!why) {
        g_replaying = true;
        fx = g_realHitEffect(LimitFxHolder(), e.hitMask, e.place, e.hitA, e.hitB, e.hitC);
        g_replaying = false;
        if (!fx) why = "the game made no effect";
    }
    if (why) {
        if (g_limitHitsSkipped++ < 5) Log("motion mirror: the other player's Limit hit effect not made: %s", why);
        return;
    }
    if (g_limitHitsPlayed++ < 3)
        Log("motion mirror: the other player's Limit hit effect (mask 0x%llX) made at (%.0f, %.0f, %.0f)",
            static_cast<unsigned long long>(e.hitMask), e.place[0], e.place[1], e.place[2]);
}

// Receiver: `record` is still on our holder's record list (newest at +0x18, each +0x74 the handle of the one before
// it; L260) and still holds the effect `fx`.
bool HolderKeeps(std::uintptr_t record, std::uintptr_t fx) {
    std::uintptr_t r = 0, f = 0;
    if (!record || !ReadAny(LimitFxHolder() + 0x18, &r)) return false;
    for (int n = 0; r && n < 512; ++n) {
        if (r == record) return ReadAny(r + 0x40, &f) && f == fx;
        std::uint32_t next = 0;
        if (!ReadAny(r + 0x74, &next)) return false;
        r = DecodeHandle(next);
    }
    return false;
}

// The other player's Limit script's effect: its start made from our holder of that Limit with the game's own call and
// put where the doer's script put it, or one of its end calls run on ours by its number.
void PlayLimitFx(const Event& e) {
    std::uint32_t index = e.source;
    if (index == 57 || index == 58 || index == 59) {
        std::uintptr_t owner = e.targetRole == kRoleNone ? 0 : Resolve(e.targetRole, 0, e.targetObj);
        const char* why = !LimitFxLive() ? "no effects of the other player's Limit here (yet)"
                          : Read<std::uint32_t>(ExeBase() + kEffectMode) != 1 ? "the effects aren't in their field mode"
                          : e.targetRole != kRoleNone && !owner ? "its owner isn't in our game"
                                                                : nullptr;
        std::uintptr_t fx = 0, record = 0, part = 0;
        if (!why) {
            using PFN_Start = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uint32_t, std::uint32_t, std::uintptr_t);
            g_replaying = true;
            fx = reinterpret_cast<PFN_Start>(ExeBase() + kLimitEffectStart)(LimitFxHolder() - 0x18, e.hitB, e.hitC, owner);
            g_replaying = false;
            part = FxPart(fx);
            if (!part || !ReadAny(LimitFxHolder() + 0x18, &record) || !HolderKeeps(record, fx))
                why = "the game made no effect";
        }
        if (why) {
            if (g_scriptFxSkipped++ < 5)
                Log("motion mirror: the other player's Limit script effect %u (start 2/%u) not made: %s", e.hitB, index, why);
            return;
        }
        std::memcpy(reinterpret_cast<void*>(part), e.place, sizeof(e.place));  // where the doer's script put it
        for (TheirFx& o : g_theirFx)
            if (o.n == e.hitA || o.fx == fx) o = {};
        g_theirFx[g_theirFxNext++ % 32] = {e.hitA, fx, record, part};
        if (g_scriptFxMade++ < 3)
            Log("motion mirror: the other player's Limit script effect %u (start 2/%u) made at (%.0f, %.0f, %.0f)%s%s",
                e.hitB, index, e.place[0], e.place[1], e.place[2], owner ? " on our " : "", owner ? Model(owner) : "");
        return;
    }
    TheirFx* t = nullptr;
    for (TheirFx& o : g_theirFx)
        if (e.hitA && o.n == e.hitA) t = &o;
    if (!t) {
        ++g_scriptFxEndsGone;
        return;
    }
    TheirFx f = *t;
    if (EndsScriptFx(index)) *t = {};
    std::uint32_t flags = 0;
    std::uintptr_t part = 0;
    bool listed = LimitFxLive() && HolderKeeps(f.record, f.fx) && ReadAny(f.fx + kFxFlags, &flags) &&
                  ReadAny(f.fx + kFxPart, &part);
    bool same = listed && !(flags & kFxKilled) && part == f.part;
    // Ours ran out by itself before the doer's kill: the kill finishes it (world_mirror.cpp EndedNotFinished).
    bool unfinished = listed && EndsScriptFx(index) && (flags & kFxKilled) && !(flags & 1) && !part;
    std::uint32_t h = same || unfinished ? HandleOf(f.fx) : 0;
    auto fn = reinterpret_cast<void(__fastcall*)(std::uint64_t*)>(WorldCensusTrapReal(0, static_cast<int>(index)));
    if (!h || !fn) {
        ++g_scriptFxEndsGone;
        return;
    }
    std::uint64_t args[16] = {kTagAddr | h, e.hitB, e.hitC};
    g_replaying = true;
    fn(args);
    g_replaying = false;
    ++g_scriptFxEndsRun;
}

// Friend: the host's companion's hit effect, from our mirrored companion (or its weapon) at the host's place.
void PlayHit(const Event& e) {
    if (e.source == kHitLimit) return PlayLimitHit(e);
    std::uintptr_t companion = Resolve(kRoleCompanion, 0, e.targetObj), owner = companion;
    if (!companion) return Skip(e, "no mirrored companion");
    if (e.source) owner = Read<std::uintptr_t>(companion + kWeapons + 8 * (e.source - 1));
    if (!owner) return Skip(e, "our companion has no such weapon");
    if (Read<std::uint32_t>(ExeBase() + kEffectMode) != 1) return Skip(e, "the effects aren't in their field mode");
    std::uintptr_t fx = 0;
    g_replaying = true;
    __try {
        fx = g_realHitEffect(owner + 0x80, e.hitMask, e.place, e.hitA, e.hitB, e.hitC);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_replaying = false;
    if (!fx) return Skip(e, "the game made no effect");
    if (g_hitsPlayed++ < 5)
        Log("motion mirror: the host's %s's hit effect (mask 0x%llX) played on ours at (%.0f, %.0f, %.0f)", Model(owner),
            static_cast<unsigned long long>(e.hitMask), e.place[0], e.place[1], e.place[2]);
}

void Play(const Event& e) {
    if (e.room != Room()) return Skip(e, "another room");
    if (e.kind == kKindSpell) return PlaySpell(e);
    if (e.kind == kKindHit) return PlayHit(e);
    if (e.kind == kKindLimitFx) return PlayLimitFx(e);
    std::uintptr_t target = Resolve(e.targetRole, 0, e.targetObj);
    if (!target)
        return Skip(e, e.targetRole == kRoleSora ? "no copy of their Sora with Sora's motions"
                       : g_host ? "none of our companions plays the friend's Limit partner now" : "no mirrored companion");
    std::uintptr_t exe = ExeBase(), bar = 0, source = 0;
    void* slot = g_noEffects;
    bool paired = false;
    if (e.kind == kKindFile) {
        bar = exe + e.file;
        BarPrint p;
        // A file the game moved or let go of leaves its bytes behind, but the motion objects its handles name are
        // destroyed: only a file the game lists as loaded and in place counts (TODO 1.119).
        bool same = FormLookLiveFileAt(bar) && BarCount(bar) == e.fileCount && PrintBar(bar, e.fileCount, p) &&
                    p.names == e.namesHash;
        if (!same) bar = FindBar(e.fileCount, e.fileHash, e.namesHash, e.file);
        if (!bar) return Skip(e, "no loaded file with the same contents in our game");
        std::uintptr_t fx = g_limitFxHits || g_limitFxTriggers || g_limitFxScript ? LimitFxFor(e, bar) : 0;
        if (fx && g_limitFxTriggers) slot = reinterpret_cast<void*>(fx);
    } else {
        source = CaptureSource(e, paired);
        if (!source) return Skip(e, "we don't have its source actor");
        if (SharedMoveOwnBrain(source)) {
            ++g_sharedSkips;
            return Skip(e, "its source plays a shared move on our own Sora");
        }
        if (BarCount(ReadPtr(source + kActorMotionSet)) != e.fileCount) return Skip(e, "our source actor's motion file differs");
    }
    if (!Call(e, target, bar, source, slot)) return Skip(e, "exception in the game's call");
    std::uintptr_t bank = ReadPtr(target + kActorMotionBank);
    if (Read<std::int32_t>(target + kActorMotion) != e.motion || !bank) return Skip(e, "the game didn't take it");
    ++g_played;
    if (paired) ++g_sourcePaired;
    Keep(target, bank, e.motion, false, e.kind == kKindFile);
    if (g_playLogs++ < 40)
        Log("motion mirror: played the %s's %s motion %d (file exe+0x%X, ours exe+0x%X) on our %s%s%s%s",
            g_host ? "friend" : "host", e.kind == kKindFile ? "Limit" : "captured", e.motion, e.file,
            bar ? ExeOffset(bar) : e.file, Whose(e.targetRole),
            source ? " from our " : "", source ? Model(source) : "", paired ? " (paired with the host's)" : "");
}

}  // namespace

void MotionMirrorInit(bool host) {
    g_host = host;
    if (EnvInt("KH2COOP_MIRROR_MOTIONS", 1) != 1) {
        Log("motion mirror: off (MIRROR_MOTIONS=0): the other player's copy stands idle during Limits and reaction commands");
        return;
    }
    bool a = HookFunction(kFileMotion, kFileMotionBytes, sizeof(kFileMotionBytes), reinterpret_cast<void*>(HookFileMotion),
                          reinterpret_cast<void**>(&g_realFileMotion), "Limit motion");
    bool b = HookFunction(kCapture, kCaptureBytes, sizeof(kCaptureBytes), reinterpret_cast<void*>(HookCapture),
                          reinterpret_cast<void**>(&g_realCapture), "motion capture");
    bool c = HookFunction(kMotionAttack, kMotionAttackBytes, sizeof(kMotionAttackBytes),
                          reinterpret_cast<void*>(HookMotionAttack), reinterpret_cast<void**>(&g_realMotionAttack),
                          "motion attack");
    bool d = HookFunction(kCast, kCastBytes, sizeof(kCastBytes), reinterpret_cast<void*>(HookCast),
                          reinterpret_cast<void**>(&g_realCast), "spell cast");
    bool f = HookFunction(kAttackNew, kAttackNewBytes, sizeof(kAttackNewBytes), reinterpret_cast<void*>(HookAttackNew),
                          reinterpret_cast<void**>(&g_realAttackNew), "attack made (the copy's spells')");
    bool g = HookFunction(kCanHit, kCanHitBytes, sizeof(kCanHitBytes), reinterpret_cast<void*>(HookCanHit),
                          reinterpret_cast<void**>(&g_realCanHit), "attack can hit (not the copy's spells')");
    g_on = a && b && c && d && f && g;
    g_hitsOn = g_on && HookFunction(kHitEffect, kHitEffectBytes, sizeof(kHitEffectBytes),
                                    reinterpret_cast<void*>(HookHitEffect), reinterpret_cast<void**>(&g_realHitEffect),
                                    "hit effect (the companions' hits: the host's)");
    Log("motion mirror: %s", g_on ? "on: Limit and reaction-command motions and spells show on the other player's copy"
                                  : "OFF: a hook failed");
    if (g_on && !HookFunction(kLimitPreload, kLimitPreloadBytes, sizeof(kLimitPreloadBytes),
                              reinterpret_cast<void*>(HookLimitPreload), reinterpret_cast<void**>(&g_realLimitPreload),
                              "Limit preload (waits while a replayed Limit motion may use its file)"))
        Log("motion mirror: our Limit preload isn't held: a Limit file it moves is skipped, not played (TODO 1.119)");
    // Our holder needs copy_blade.cpp's file hooks to end before its file is freed or moved (MotionMirrorFileChange).
    bool fileHooks = EnvInt("KH2COOP_COPY_BLADE", 1) == 1;
    g_limitFxHits = g_hitsOn && fileHooks && EnvInt("KH2COOP_LIMIT_FX_HITS", 1) == 1;
    g_limitFxTriggers = g_on && fileHooks && EnvInt("KH2COOP_LIMIT_FX_TRIGGERS", 0) == 1;
    g_limitFxScript = g_on && fileHooks && EnvInt("KH2COOP_LIMIT_FX_SCRIPT", 0) == 1;
    Log("motion mirror: the other player's Limit effects: hits %s, motion triggers %s, script effects %s%s",
        g_limitFxHits ? "on" : "off", g_limitFxTriggers ? "on" : "off", g_limitFxScript ? "on" : "off",
        fileHooks ? "" : " (COPY_BLADE=0: no file hooks to end them before a move)");
}

void MotionMirrorOnPacket(const char* buf, int n) {
    if (!g_on || n < static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (n != static_cast<int>(sizeof(Packet) + p.count * sizeof(Event))) return;
    for (int i = 0; i < p.count; ++i) {
        Event e;
        std::memcpy(&e, buf + sizeof(Packet) + i * sizeof(Event), sizeof(e));
        if (g_pendingCount == 16) {  // full: an effect goes first (the newest, if only motions wait), never a motion
            auto effect = [](const Event& x) { return x.kind == kKindHit || x.kind == kKindLimitFx; };
            int drop = -1;
            for (int k = 0; k < 16 && drop < 0; ++k)
                if (effect(g_pending[k].e)) drop = k;
            if (drop < 0 && effect(e)) {
                Skip(e, "too many waiting");
                continue;
            }
            if (drop < 0) drop = 0;
            Skip(g_pending[drop].e, "too many waiting");
            std::memmove(&g_pending[drop], &g_pending[drop + 1], (15 - drop) * sizeof(Pending));
            --g_pendingCount;
        }
        g_pending[g_pendingCount++] = {e, GetTickCount64()};
    }
}

void MotionMirrorFrame() {
    if (!g_on) return;
    if (g_debugCast >= 0) {
        int slot = g_debugCast;
        g_debugCast = -1;
        LogSpellSlots();
        __try {
            std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
            std::uintptr_t data = Read<std::uintptr_t>(ExeBase() + kSpellSlots + 0x50 * slot + 0x48);
            std::uint32_t spell = data ? Read<std::uint16_t>(data + 0x26) : 0;
            Log("motion mirror: debug cast: our Sora casts slot %d (spell 0x%X)", slot, spell);
            if (sora && spell)
                reinterpret_cast<void(__fastcall*)(std::uintptr_t, std::uint32_t)>(ExeBase() + kCastSpell)(sora, spell);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("motion mirror: debug cast: exception");
        }
    }
    if (g_limitFxPax && (Room() != g_limitFxRoom || !PreloadHold()))
        EndLimitFx(Room() != g_limitFxRoom ? "another room" : "the other player's Limit is over");
    SendNumbered();
    if (g_limitFxScript) SendScriptFx();
    SendQueued();
    std::uint32_t playback = AvatarLinkPlaybackSenderMs();
    std::uint64_t now = GetTickCount64();
    int kept = 0;
    for (int i = 0; i < g_pendingCount; ++i) {
        Pending& p = g_pending[i];
        bool late = now - p.arrived > kLateMs;
        bool due = !playback || static_cast<std::int32_t>(playback - p.e.senderMs) >= 0 || late;
        if (due && !late && SourceNotPairedYet(p.e)) {
            ++g_sourceWaits;
            due = false;
        }
        if (due)
            Play(p.e);
        else
            g_pending[kept++] = p;
    }
    g_pendingCount = kept;
    if (now - g_statsTick < 30000) return;
    g_statsTick = now;
    std::uint32_t seen = g_sent + g_played + g_skipped + g_attacksDropped + g_spellsCast + g_quietHits + g_limitlessHits +
                         g_hitsDropped;
    if (seen == g_statsSeen) return;
    g_statsSeen = seen;
    Log("motion mirror: sent %u (spells %u; captures that waited for their source's number %u, sent without one %u), "
        "played %u (captures from our copy paired with the host's source %u, frames one waited for the pairing %u), "
        "spells cast on the copy %u (let go %u), not played %u (their source in a shared move %u), copies' attacks dropped %u, copy spell attacks %u "
        "(hits stopped %u), Limit attacks with no Limit here stopped %u; companions' hit effects %s: sent %u, played %u, "
        "our own not drawn %u; our Limit preload held %u times; Limit hit effects %s: sent %u, made %u, not made %u, "
        "holders filled %u (triggers %s); Limit script effects %s: starts sent %u, end calls sent %u, made %u, not made "
        "%u, end calls run %u, not run %u", g_sent, g_spellsSent, g_numberedLate,
        g_neverNumbered, g_played, g_sourcePaired, g_sourceWaits, g_spellsCast, g_castTriggers, g_skipped, g_sharedSkips,
        g_attacksDropped, g_quietMade, g_quietHits, g_limitlessHits, g_hitsOn ? "on" : "OFF", g_hitsSent, g_hitsPlayed,
        g_hitsDropped, g_preloadHolds, g_limitFxHits ? "on" : "off", g_limitHitsSent, g_limitHitsPlayed,
        g_limitHitsSkipped, g_limitFxFills, g_limitFxTriggers ? "on" : "off", g_limitFxScript ? "on" : "off",
        g_scriptFxSent, g_scriptFxEnds, g_scriptFxMade, g_scriptFxSkipped, g_scriptFxEndsRun, g_scriptFxEndsGone);
}

bool MotionMirrorWantsTrap(int table, int index) {
    return g_limitFxScript && ((table == 2 && index >= 57 && index <= 59) ||
                               (table == 0 && (index == 44 || index == 51 || index == 52 || index == 85)));
}

void MotionMirrorOnTrap(int table, int index, const std::uint64_t* args, int argc, std::uint64_t result) {
    if (g_limitFxScript && !g_replaying) OnLimitTrap(table, index, args, argc, result);
}

bool MotionMirrorKeeps(std::uintptr_t actor, bool peerOtherBank, int peerMotion) {
    if (!g_on || !actor) return false;
    for (Kept& k : g_kept) {
        if (k.actor != actor) continue;
        bool playing = false;
        __try {
            playing = Read<std::int32_t>(actor + kActorMotion) == k.motion && Read<std::uintptr_t>(actor + kActorMotionBank) == k.bank;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        bool young = GetTickCount64() - k.tick < kKeepMs;
        if (playing && (k.spell ? peerMotion == k.motion || young : peerOtherBank || young)) return true;
        k = {};
        return false;
    }
    return false;
}

bool MotionMirrorCopySpellRunning() { return g_on && CopySpellRunning(); }

std::uintptr_t MotionMirrorLimitFx() { return g_limitFxPax ? LimitFxHolder() : 0; }

void MotionMirrorFileChange(int group) {
    if (!g_limitFxPax) return;
    int g = 0;
    std::uint32_t flags = 0;
    bool known = ReadAny(g_limitFxEntry + 0x48, &g) && ReadAny(g_limitFxEntry + 0x44, &flags);
    // A group free takes that group's files; a layout frees let-go or marked files (+0x44 bit 1) and moves the rest.
    bool reaches = !known || (group >= 0 ? g == group
                                         : FormLookLiveFileAt(g_limitFxPax) != g_limitFxEntry || (flags & 2) != 0);
    if (reaches)
        EndLimitFx(group >= 0 ? "the game frees its file's group" : group == -1 ? "a file layout may free its file"
                                                                                 : "a file layout moves its file");
}

bool MotionMirrorCasting(std::uintptr_t actor) { return g_casting && actor == g_replayTarget; }

void MotionMirrorMute(bool on) { g_muted = on; }

bool MotionMirrorCastTrigger(std::uintptr_t actor, std::uintptr_t record) {
    if (!g_on || !actor) return false;
    static int logged = 0;
    bool listed = record == g_castEntry && OnMagicList(record, logged < 20);
    if (actor != g_castActor || record != g_castEntry || g_castTriggered || !listed) {
        if (logged++ < 20)
            Log("motion mirror: the copy's cast trigger: not ours to let go (record %s, %s, %s)",
                record == g_castEntry ? "ours" : "another", g_castTriggered ? "already let go" : "not let go yet",
                listed ? "its script runs" : "its script ended");
        return false;
    }
    g_castTriggered = true;
    ++g_castTriggers;
    return true;
}

void MotionMirrorDebugCast(int slot) {
    if (slot >= 0 && slot < 6) g_debugCast = slot;
}

// Test command (tools/debug_cmd.py deadowner, TODO 1.132): the attack guard on handles whose answer we know, then one
// dead handle through the hook itself (it returns before the game's attack maker: nothing is made).
void MotionMirrorDebugDeadOwner() {
    std::uint32_t dead = 0;
    for (std::uint32_t i = 0; i < 64 && !dead; ++i)
        if (!HandleBase(0x80000000u | i << 25)) dead = 0x80000000u | i << 25 | 0x10;
    std::uintptr_t first = Read<std::uintptr_t>(ExeBase() + kListHead);
    std::uint32_t live = first ? Read<std::uint32_t>(first + kNextActor) : 0;  // the second actor in the list
    bool zeroOk = OwnerGone(0), liveOk = live && DecodeHandle(live) && !OwnerGone(live), deadOk = dead && OwnerGone(dead);
    std::uint32_t before = g_deadOwnerSkips, set[1] = {dead};
    if (deadOk) HookMotionAttack(set, 0, 0, 0, 0);
    bool hookOk = g_deadOwnerSkips == before + 1;
    Log("dead owner test: handle 0 %s, live %08X %s, dead %08X %s, the hook %s: %s", zeroOk ? "skipped" : "MADE", live,
        liveOk ? "made" : "WRONG", dead, deadOk ? "skipped" : "WRONG", hookOk ? "skipped it" : "DID NOT skip it",
        zeroOk && liveOk && deadOk && hookOk ? "PASS" : "FAIL");
}

}  // namespace kh2coop
