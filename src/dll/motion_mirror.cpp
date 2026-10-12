// Limits and reaction commands on the other player's screen (TODO 1.31; world mirror step 4,
// docs/WORLD_MIRROR_RESEARCH.md). KH2COOP_MIRROR_MOTIONS=0 turns it off (both PCs: feature handshake).
//
// A Limit or a reaction command plays a motion from another file than the actor's own motion set
// (VERIFIED_OFFSETS rows 'Limit motions' and 'Reaction-command motions'). The pose packet only says "a motion
// from another file" (kOtherBank), so the copy stood idle through them. Both games hold those files at the same
// address, so the game that plays one says which file and which motion, and the other game plays the same on its
// copy, with the game's own call:
//   exe+0x3C63E0(owner, bar, paxSlot, target, motion, blend, users): a Limit's motion (its only caller is
//     trap_limit_motion_start). Sent: the Limit's BAR (and its PAX, for later: see g_noEffects) as exe offsets.
//   exe+0x3C6CC0(source+0x158, target, motion, blend): the target plays motion `motion` of the source actor's
//     file, e.g. trap_obj_motion_capture(chandelier, Sora, 252) for a reaction command. Sent: who the source is.
// Each player sends its own Sora's; the host also sends its companions' (the friend's companions mirror them).
// The receiver plays it when its playback of the sender's poses reaches that moment, and the copy keeps it while
// the sender's pose says "from another file" (puppet.cpp DriveMotion, world_sync.cpp ApplyMirror).
// Such motions have attack triggers (exe+0x3D25D0 makes the attack). The sender's game already makes those attacks
// and deals their damage; on the friend, damage to a bound enemy goes to the host as a claim, so the copy's attacks
// would count twice. And a Limit's attack takes the running Limit's effects (exe+0x3D8A40), which the receiver's
// game hasn't: 0, and its hit crashed in exe+0x3D1B40 (bench 2026-10-12 21:08). So a copy playing a sent motion
// makes no attacks.

#include "motion_mirror.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4D32484B;  // "KH2M"
constexpr std::uint16_t kVersion = 1;
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

enum : std::uint8_t { kKindFile = 1, kKindCapture = 2 };
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
    std::uint32_t source, sourceObj;  // kKindCapture: the source (exe offset for kRoleSameAddress) and its object id
    std::int32_t motion;
    float blend;
    std::uint32_t file, fileCount;  // the motion file (exe offset; for a capture only logged) and its entry count
    std::uint32_t pax;              // kKindFile: its effects (PAX) as an exe offset (not played yet)
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


// ---- Sender ----

std::uint8_t RoleOf(std::uintptr_t a, std::uint32_t& where) {
    where = 0;
    if (!a) return kRoleNone;
    if (a == Read<std::uintptr_t>(ExeBase() + kListHead)) return kRoleSora;
    if (a == PuppetCloneActor() || a == PuppetDrivenActor()) return kRoleNone;  // our copy of the peer
    if (g_host)
        for (int slot = 0; slot < 2; ++slot)
            if (a == Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * slot)) return kRoleCompanion;
    if (!InList(a)) return kRoleNone;
    where = static_cast<std::uint32_t>(a - ExeBase());
    return kRoleSameAddress;
}

void NotSent(const char* what, std::uintptr_t target, int motion, const char* why) {
    if (g_notSentLogs++ < 20) Log("motion mirror: %s motion %d on %s not sent: %s", what, motion, Model(target), why);
}

void Queue(Event& e, std::uintptr_t target, std::uint8_t role) {
    e.id = g_nextId++;
    e.senderMs = AvatarLinkNowMs();
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
    if (!e.pax) return NotSent("Limit", target, motion, "no effects (PAX) in the game's memory");
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
    e.file = ExeOffset(motionSet[0]);
    e.fileCount = BarCount(motionSet[0]);
    if (e.sourceRole == kRoleNone) return NotSent("captured", target, motion, "its source isn't an actor the peer has");
    if (!e.fileCount) return NotSent("captured", target, motion, "its source's file isn't a BAR");
    Queue(e, target, role);
}

void __fastcall HookFileMotion(void* owner, std::uintptr_t bar, void* paxSlot, std::uintptr_t target, int motion,
                               float blend, int* users) {
    g_realFileMotion(owner, bar, paxSlot, target, motion, blend, users);
    if (!g_on || g_replaying) return;
    __try {
        OnFileMotion(bar, paxSlot, target, motion, blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

int __fastcall HookCapture(std::uintptr_t* motionSet, std::uintptr_t target, int motion, float blend) {
    int id = g_realCapture(motionSet, target, motion, blend);
    if (!g_on || g_replaying) return id;
    __try {
        OnCapture(motionSet, target, motion, blend);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return id;
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
            return g_host ? 0 : WorldSyncMirroredCompanion(obj);
        case kRoleSameAddress: {
            std::uintptr_t a = ExeBase() + where;
            return InList(a) && ObjId(a) == obj ? a : 0;
        }
    }
    return 0;
}

const char* Whose(std::uint8_t role) {
    return role == kRoleSora ? "copy of their Sora" : role == kRoleCompanion ? "mirrored companion" : "actor";
}

void Skip(const Event& e, const char* why) {
    ++g_skipped;
    if (g_skipLogs++ < 30)
        Log("motion mirror: the %s's %s motion %d not played: %s", g_host ? "friend" : "host",
            e.kind == kKindFile ? "Limit" : "captured", e.motion, why);
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

void Play(const Event& e) {
    if (e.room != Room()) return Skip(e, "another room");
    std::uintptr_t target = Resolve(e.targetRole, 0, e.targetObj);
    if (!target)
        return Skip(e, e.targetRole == kRoleSora ? "no copy of their Sora with Sora's motions" : "no mirrored companion");
    std::uintptr_t exe = ExeBase(), bar = 0, source = 0;
    if (e.kind == kKindFile) {
        bar = exe + e.file;
        if (BarCount(bar) != e.fileCount) return Skip(e, "that file isn't at the same place in our game");
    } else {
        source = Resolve(e.sourceRole, e.source, e.sourceObj);
        if (!source || ReadPtr(source + kActorMotionSet + 0x10) != source) return Skip(e, "we don't have its source actor");
        if (BarCount(ReadPtr(source + kActorMotionSet)) != e.fileCount) return Skip(e, "our source actor's motion file differs");
    }
    if (!Call(e, target, bar, source)) return Skip(e, "exception in the game's call");
    std::uintptr_t bank = ReadPtr(target + kActorMotionBank);
    if (Read<std::int32_t>(target + kActorMotion) != e.motion || !bank) return Skip(e, "the game didn't take it");
    ++g_played;
    Kept* k = nullptr;  // this actor's entry, else a free one, else the oldest
    for (Kept& c : g_kept)
        if (c.actor == target) k = &c;
    for (Kept& c : g_kept)
        if (!k && !c.actor) k = &c;
    if (!k) {
        k = &g_kept[0];
        for (Kept& c : g_kept)
            if (c.tick < k->tick) k = &c;
    }
    *k = {target, bank, e.motion, GetTickCount64()};
    if (g_playLogs++ < 40)
        Log("motion mirror: played the %s's %s motion %d (file exe+0x%X) on our %s%s%s", g_host ? "friend" : "host",
            e.kind == kKindFile ? "Limit" : "captured", e.motion, e.file, Whose(e.targetRole),
            source ? " from our " : "", source ? Model(source) : "");
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
    g_on = a && b && c;
    Log("motion mirror: %s", g_on ? "on: Limit and reaction-command motions show on the other player's copy"
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
    SendQueued();
    std::uint32_t playback = AvatarLinkPlaybackSenderMs();
    std::uint64_t now = GetTickCount64();
    int kept = 0;
    for (int i = 0; i < g_pendingCount; ++i) {
        Pending& p = g_pending[i];
        bool due = !playback || static_cast<std::int32_t>(playback - p.e.senderMs) >= 0 || now - p.arrived > kLateMs;
        if (due)
            Play(p.e);
        else
            g_pending[kept++] = p;
    }
    g_pendingCount = kept;
    if (now - g_statsTick < 30000) return;
    g_statsTick = now;
    if (g_sent + g_played + g_skipped + g_attacksDropped == g_statsSeen) return;
    g_statsSeen = g_sent + g_played + g_skipped + g_attacksDropped;
    Log("motion mirror: sent %u, played %u, not played %u, copies' attacks dropped %u", g_sent, g_played, g_skipped,
        g_attacksDropped);
}

bool MotionMirrorKeeps(std::uintptr_t actor, bool peerOtherBank) {
    if (!g_on || !actor) return false;
    for (Kept& k : g_kept) {
        if (k.actor != actor) continue;
        bool playing = false;
        __try {
            playing = Read<std::int32_t>(actor + kActorMotion) == k.motion && Read<std::uintptr_t>(actor + kActorMotionBank) == k.bank;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (playing && (peerOtherBank || GetTickCount64() - k.tick < kKeepMs)) return true;
        k = {};
        return false;
    }
    return false;
}

}  // namespace kh2coop
