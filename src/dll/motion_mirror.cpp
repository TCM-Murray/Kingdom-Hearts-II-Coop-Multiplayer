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
// TODO 1.57).
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

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "shared_move.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4D32484B;  // "KH2M"
constexpr std::uint16_t kVersion = 3;  // v3: a capture's source by world sync's number; v2: the Limit file's contents
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
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kActorMotionSet = 0x158;  // the actor's motion state: +0 its own file (a BAR), +0x10 the actor
constexpr std::uintptr_t kActorMotion = 0x180, kActorMotionBank = 0x2B8;
constexpr std::uint32_t kKeepMs = 500;      // how long a played motion waits for the sender's pose to show it
constexpr std::uint32_t kLateMs = 1000;     // an event waits at most this long for the playback to reach it

using PFN_FileMotion = void(__fastcall*)(void* owner, std::uintptr_t bar, void* paxSlot, std::uintptr_t target,
                                         int motion, float blend, int* users);
using PFN_Capture = int(__fastcall*)(std::uintptr_t* motionSet, std::uintptr_t target, int motion, float blend);
PFN_FileMotion g_realFileMotion = nullptr;
PFN_Capture g_realCapture = nullptr;
using PFN_MotionAttack = void(__fastcall*)(std::uint32_t* set, int a, int b, std::uintptr_t c, int d);
PFN_MotionAttack g_realMotionAttack = nullptr;
using PFN_Cast = std::uintptr_t(__fastcall*)(std::uintptr_t actor, std::uint32_t spell, std::uint8_t air);
PFN_Cast g_realCast = nullptr;
using PFN_AttackNew = std::uintptr_t(__fastcall*)(std::uintptr_t self, std::uintptr_t owner, std::uintptr_t a3,
                                                  std::uint32_t a4, std::uint32_t a5, std::int32_t a6, std::uint32_t a7);
PFN_AttackNew g_realAttackNew = nullptr;
using PFN_CanHit = std::uint64_t(__fastcall*)(std::uintptr_t attack, std::uintptr_t actor);
PFN_CanHit g_realCanHit = nullptr;

enum : std::uint8_t { kKindFile = 1, kKindCapture = 2, kKindSpell = 3 };
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
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kPerPacket = 16;

bool g_on = false, g_host = false;
bool g_replaying = false;  // our own call for the peer's motion is running: not ours to send
bool g_muted = false;  // another module replays the peer's game on our Sora (MotionMirrorMute): not ours to send
std::uintptr_t g_replayTarget = 0;  // its target: attacks its first frame makes come during the call

// Sender: each event goes out in 3 packets (one per frame).
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
std::uint32_t g_lastId = 0;
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

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + kHandleBases + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

bool InList(std::uintptr_t x) {
    if (!x) return false;
    __try {
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor)))
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

// The BARs in [from, to) (16-byte steps) with this many entries and these contents: the first full match, else the
// only one whose names match. Counts the name matches in `namesHits`.
std::uintptr_t ScanBars(std::uintptr_t from, std::uintptr_t to, std::uint32_t count, std::uint32_t fileHash,
                        std::uint32_t namesHash, std::uintptr_t& namesOnly, int& namesHits) {
    __try {
        for (std::uintptr_t q = (from + 15) & ~std::uintptr_t(15); q + 16 <= to; q += 16) {
            if ((*reinterpret_cast<const std::uint32_t*>(q) & 0xFFFFFF) != 0x524142 ||  // "BAR"
                *reinterpret_cast<const std::uint32_t*>(q + 4) != count)
                continue;
            BarPrint p;
            if (!PrintBar(q, count, p)) continue;
            if (p.full == fileHash) return q;
            if (p.names == namesHash) {
                namesOnly = q;
                ++namesHits;
            }
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
// load, VERIFIED_OFFSETS 'Limit motions'), or 0. The exe's memory (where those files are, e.g. exe+0x1A42AD0) is
// scanned once per file; the result is kept.
std::uintptr_t FindBar(std::uint32_t count, std::uint32_t fileHash, std::uint32_t namesHash, std::uint32_t theirs) {
    std::uint64_t now = GetTickCount64();
    for (FoundBar& f : g_foundBars) {
        if (f.fileHash != fileHash || f.namesHash != namesHash || !f.tick) continue;
        BarPrint p;
        if (f.bar && BarCount(f.bar) == count && PrintBar(f.bar, count, p) && p.names == namesHash) return f.bar;
        if (!f.bar && now - f.tick < 5000) return 0;
        f = {};  // moved or gone: look again
    }
    std::uintptr_t exe = ExeBase(), end = exe + 0x3000000, found = 0, namesOnly = 0;
    int namesHits = 0;
    for (std::uintptr_t p = exe; p < end && !found;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<const void*>(p), &mi, sizeof(mi))) break;
        std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (regionEnd > end) regionEnd = end;
        if (Readable(mi)) found = ScanBars(p, regionEnd, count, fileHash, namesHash, namesOnly, namesHits);
        p = regionEnd;
    }
    if (!found && namesHits == 1) found = namesOnly;
    g_foundBars[g_foundNext++ % 8] = {fileHash, namesHash, found, now};
    const char* who = g_host ? "friend" : "host";
    if (found) {
        Log("motion mirror: the %s's Limit file (exe+0x%X there, %u entries) is at exe+0x%X in our game (found by its "
            "contents%s)", who, theirs, count, ExeOffset(found), found == namesOnly ? ", names only" : "");
    } else {
        Log("motion mirror: the %s's Limit file (exe+0x%X there, %u entries, contents %08X, names %08X): no file with the "
            "same contents in our game%s", who, theirs, count, fileHash, namesHash,
            namesHits > 1 ? " (several with the same names)" : "");
        LogBarsLike(count);
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
            e = Decode(last);
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
    e.targetObj = ObjId(target);
    Queued& q = g_queue[g_queueNext++ % 32];
    q.e = e;
    q.sent = 0;
    ++g_sent;
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

void __fastcall HookFileMotion(void* owner, std::uintptr_t bar, void* paxSlot, std::uintptr_t target, int motion,
                               float blend, int* users) {
    g_realFileMotion(owner, bar, paxSlot, target, motion, blend, users);
    if (!g_on || g_replaying || g_muted) return;
    __try {
        OnFileMotion(bar, paxSlot, target, motion, blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
                Model(Decode(Read<std::uint32_t>(attack + 0x10))), Model(actor));
        return 0;
    }
    // A ride (TODO 1.126, world_sync.cpp's carriers): our game's tornado never takes the copy; the other player's own
    // game gives it its ride. Here, both games.
    if (g_on && attack && actor && actor == PuppetCloneActor()) {
        std::uintptr_t owner = Decode(Read<std::uint32_t>(attack + 0x10));
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
        if (!q.e.id || q.sent >= 3) continue;
        std::memcpy(buf + sizeof(Packet) + n * sizeof(Event), &q.e, sizeof(Event));
        ++q.sent;
        ++n;
    }
    if (!n) return;
    p.count = static_cast<std::uint8_t>(n);
    std::memcpy(buf, &p, sizeof(p));
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Packet) + n * sizeof(Event)));
}

// ---- Receiver ----

bool PlaysSentMotion(std::uintptr_t actor) {
    for (const Kept& k : g_kept)
        if (k.actor && k.actor == actor && ReadPtr(actor + kActorMotionBank) == k.bank) return true;
    return false;
}

void __fastcall HookMotionAttack(std::uint32_t* set, int a, int b, std::uintptr_t c, int d) {
    std::uintptr_t owner = g_on && set ? Decode(*set) : 0;
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

// The game's own call, as the peer's game made it (bar set: a Limit's motion, else a capture from source).
bool Call(const Event& e, std::uintptr_t target, std::uintptr_t bar, std::uintptr_t source) {
    bool ok = true;
    g_replaying = true;
    g_replayTarget = target;
    __try {
        if (bar)
            g_realFileMotion(nullptr, bar, g_noEffects, target, e.motion, e.blend, nullptr);
        else
            g_realCapture(reinterpret_cast<std::uintptr_t*>(source + kActorMotionSet), target, e.motion, e.blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_replaying = false;
    g_replayTarget = 0;
    return ok;
}

void Keep(std::uintptr_t actor, std::uintptr_t bank, int motion, bool spell) {
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
    *k = {actor, bank, motion, GetTickCount64(), spell};
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
    Keep(copy, 0, Read<std::int32_t>(copy + kActorMotion), true);
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

void Play(const Event& e) {
    if (e.room != Room()) return Skip(e, "another room");
    if (e.kind == kKindSpell) return PlaySpell(e);
    std::uintptr_t target = Resolve(e.targetRole, 0, e.targetObj);
    if (!target)
        return Skip(e, e.targetRole == kRoleSora ? "no copy of their Sora with Sora's motions"
                       : g_host ? "none of our companions plays the friend's Limit partner now" : "no mirrored companion");
    std::uintptr_t exe = ExeBase(), bar = 0, source = 0;
    bool paired = false;
    if (e.kind == kKindFile) {
        bar = exe + e.file;
        BarPrint p;
        bool same = BarCount(bar) == e.fileCount && PrintBar(bar, e.fileCount, p) && p.names == e.namesHash;
        if (!same) bar = FindBar(e.fileCount, e.fileHash, e.namesHash, e.file);
        if (!bar) return Skip(e, "no file with the same contents in our game");
    } else {
        source = CaptureSource(e, paired);
        if (!source) return Skip(e, "we don't have its source actor");
        if (SharedMoveOwnBrain(source)) {
            ++g_sharedSkips;
            return Skip(e, "its source plays a shared move on our own Sora");
        }
        if (BarCount(ReadPtr(source + kActorMotionSet)) != e.fileCount) return Skip(e, "our source actor's motion file differs");
    }
    if (!Call(e, target, bar, source)) return Skip(e, "exception in the game's call");
    std::uintptr_t bank = ReadPtr(target + kActorMotionBank);
    if (Read<std::int32_t>(target + kActorMotion) != e.motion || !bank) return Skip(e, "the game didn't take it");
    ++g_played;
    if (paired) ++g_sourcePaired;
    Keep(target, bank, e.motion, false);
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
    Log("motion mirror: %s", g_on ? "on: Limit and reaction-command motions and spells show on the other player's copy"
                                  : "OFF: a hook failed");
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
        if (e.id + 10000 < g_lastId) g_lastId = 0;  // the peer's game restarted
        if (e.id <= g_lastId) continue;            // each event comes 3 times
        g_lastId = e.id;
        if (g_pendingCount == 16) {
            Skip(g_pending[0].e, "too many waiting");
            std::memmove(&g_pending[0], &g_pending[1], 15 * sizeof(Pending));
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
    SendNumbered();
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
    std::uint32_t seen = g_sent + g_played + g_skipped + g_attacksDropped + g_spellsCast + g_quietHits + g_limitlessHits;
    if (seen == g_statsSeen) return;
    g_statsSeen = seen;
    Log("motion mirror: sent %u (spells %u; captures that waited for their source's number %u, sent without one %u), "
        "played %u (captures from our copy paired with the host's source %u, frames one waited for the pairing %u), "
        "spells cast on the copy %u (let go %u), not played %u (their source in a shared move %u), copies' attacks dropped %u, copy spell attacks %u "
        "(hits stopped %u), Limit attacks with no Limit here stopped %u", g_sent, g_spellsSent, g_numberedLate,
        g_neverNumbered, g_played, g_sourcePaired, g_sourceWaits, g_spellsCast, g_castTriggers, g_skipped, g_sharedSkips,
        g_attacksDropped, g_quietMade, g_quietHits, g_limitlessHits);
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

}  // namespace kh2coop
