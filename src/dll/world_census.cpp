// World mirror research (TODO 1.55, docs/WORLD_MIRROR_RESEARCH.md): logging only, off by default.
//   KH2COOP_CENSUS=1    actor census: every actor in the active list (head exe+0x2A171C8, next = handle at
//                       actor+0xA90) with its object, model, type, team, spawn serial or what made it, HP,
//                       position, motion and live script threads. The whole list one second after each room
//                       load and every 10 s, a line whenever one appears or goes, and every actor the
//                       factory exe+0x3DF930 makes with the script call or code that asked for it.
//   KH2COOP_TRAP_LOG=1  script system calls ("traps"): the script interpreter exe+0x41B400 calls them through
//                       16-byte entries {fn, u32 flags} in the tables at exe+0x753490 as fn(u64* args), the
//                       result in args[0] (flags: low 16 bits = argument count, bit 30 = returns a value).
//                       We put a thunk in each entry: counts per call every 10 s, the first calls of each per
//                       room with the actor whose script made it and the arguments, and for a call that acts
//                       on an actor, the bytes of that actor it changed.
//   KH2COOP_FX_LOG=1    effect creations (TODO 1.31, the other player's particles): every effect made from an
//                       effect instance, exe+0x2BD510(instance + 0x10, ...) (callers exe+0x2C6B30 by effect id and
//                       four others): whose instance it is (an actor's own +0x80, the effects of the motion it plays
//                       at +0x158+0x148, the running Limit's +0x18, or none of those), its PAX, the code that made
//                       it and the script call running then. The first 200 per room one by one.
//   KH2COOP_HEAL_TRACE=1 the companions' heal choice (TODO 1.52 / 1.58): who each one sees as needing help, the
//                       action and target it picks, the items it uses, a count every 10 s (see HookThink).
//   KH2COOP_UNSENT_LOG=1 (default on, host): the script calls an enemy's or field object's script makes that the world
//                       mirror doesn't send, once per call and object per session (2000 lines at most), with the
//                       arguments and the room. Playing through the game with it on lists the calls the friend's game
//                       may still miss (you, 2026-10-15: "you can get those functions that would need
//                       implementation"). Left out: queries, maths, and the calls of the enemies' own thinking, moves
//                       and attacks (one brain and world sync cover those).
// All only while co-op is on. Names of the calls: trap_names.inc (from OpenKH).

#include <windows.h>
#include <intrin.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "avatar_link.hpp"
#include "common.hpp"
#include "puppet.hpp"
#include "grab_sync.hpp"
#include "mission_sync.hpp"
#include "shared_move.hpp"
#include "reaction_sync.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;            // u8 world, room
constexpr std::uintptr_t kInField = 0x9BA8D0;        // u8: 0 while a room loads
constexpr std::uintptr_t kListHead = 0x2A171C8;      // the active actor list's first actor
constexpr std::uintptr_t kHandleBases = 0x2B0D720;   // u64[64]: bases of the compressed pointers ("handles")
constexpr std::uintptr_t kTrapTables = 0x753490;     // u64[12] -> tables of {fn, u32 flags} (16 bytes each)
constexpr std::uintptr_t kScriptThread = 0x2A250B0;  // the script thread running now (exe+0x3E1C80); +0x60 = its actor
constexpr std::uintptr_t kFactory = 0x3DF930;        // (object, pos4, yaw) -> actor
constexpr std::uint8_t kFactoryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
// Actor fields (world_sync.cpp, VERIFIED_OFFSETS.md).
constexpr std::uintptr_t kNextActor = 0xA90, kObjRow = 0x918, kSpawnRecord = 0x9F0, kTeam = 0x4DC,
                         kMotion = 0x180, kMotionBank = 0x2B8, kStats = 0x5C0, kThreads = 0x5B8,
                         kPos = 0x640 + 0x30, kFlags120 = 0x120, kFlags9B8 = 0x9B8, kFade = 0xA08;
constexpr std::size_t kActorBytes = 0xD50;  // a field actor's allocation (event actors: 0xBB0)
constexpr std::uint32_t kTagInt = 0x544E4940, kTagFloat = 0x544C4640, kTagAddr = 0x52444140;  // "@INT" "@FLT" "@ADR"
constexpr std::uint32_t kReturns = 0x40000000;

struct TrapName {
    std::uint8_t table;
    std::uint16_t index;
    std::uint32_t flags;
    const char* name;
};
constexpr TrapName kNames[] = {
#include "trap_names.inc"
};
constexpr int kTrapCount = static_cast<int>(sizeof(kNames) / sizeof(kNames[0]));

using PFN_Trap = void(__fastcall*)(std::uint64_t* args);
struct TrapState {
    PFN_Trap real;
    std::uint32_t count;           // calls since the last counts line
    std::uint32_t logged, diffs;   // logged one by one / with the actor's changed bytes, in room g_roomGen
    std::uint32_t room;
    std::uint64_t last;            // the last logged call (actor, arguments, result), and how often it came again
    std::uint32_t repeats;
    bool quiet;                    // maths and prints: counted only
    bool makes;                    // returns what it made (an effect, an actor, an attack): logged like an action
    bool unsentSkip;               // left out of the log of calls not sent (see Unsent)
};
TrapState g_trap[kTrapCount];
constexpr std::uint32_t kLogActions = 300, kLogQueries = 12, kDiffs = 6;  // different calls per call and room

bool g_census = false, g_trapLog = false, g_unsentLog = false;
bool g_starLog = false;  // KH2COOP_STAR_LOG=1: why a player reads 'star' (TODO 1.94)
// The log of calls not sent: (call, object) pairs logged this session (open addressing, key + 1; 0 = empty).
constexpr int kUnsentSeen = 8192, kUnsentLines = 2000;
std::uint64_t g_unsentSeen[kUnsentSeen];
int g_unsentLines = 0;
std::uint64_t g_frame = 0;
std::uint32_t g_roomGen = 1;  // +1 per room load: resets the per-room limits
std::uint64_t g_countsTick = 0;

// The calls running now (a call can run scripts that make calls), with their arguments and the game thread that
// runs them: a crash inside one names it (WorldCensusLogCrashCall; TODO 1.113, the friend's crash in a call 2026-10-22).
int g_depth = 0;
int g_curTrap[8];
const std::uint64_t* g_curArgs[8];
DWORD g_trapThread = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

bool InExe(std::uintptr_t a) { return a > ExeBase() && a < ExeBase() + 0x3000000; }

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + kHandleBases + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

std::uintptr_t ScriptActor() {
    __try {
        std::uintptr_t thread = Read<std::uintptr_t>(ExeBase() + kScriptThread);
        return thread ? Read<std::uintptr_t>(thread + 0x60) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int ScriptPc() {
    __try {
        std::uintptr_t thread = Read<std::uintptr_t>(ExeBase() + kScriptThread);
        return thread ? Read<std::int32_t>(thread + 8) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
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

// An "@ADR" argument as an actor: the handle itself, or (the objects scripts get from trap_sysobj_*)
// the handle at +4 of what it points to (trap_obj_hide exe+0x42B640 decodes both).
std::uintptr_t ActorOf(std::uint64_t arg) {
    if (static_cast<std::uint32_t>(arg >> 32) != kTagAddr) return 0;
    std::uintptr_t x = Decode(static_cast<std::uint32_t>(arg));
    if (InList(x)) return x;
    __try {
        std::uintptr_t y = x ? Decode(Read<std::uint32_t>(x + 4)) : 0;
        return InList(y) ? y : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

struct ActorInfo {
    std::uint32_t objId;
    std::uint8_t type;
    char model[24];
    std::uint16_t serial;
};

void Info(std::uintptr_t a, ActorInfo& out) {
    out = {};
    out.model[0] = '?';
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        if (InExe(row)) {
            out.objId = Read<std::uint32_t>(row);
            out.type = Read<std::uint8_t>(row + 4);
            std::memcpy(out.model, reinterpret_cast<const void*>(row + 8), sizeof(out.model) - 1);
            out.model[sizeof(out.model) - 1] = 0;
        }
        std::uintptr_t rec = Read<std::uintptr_t>(a + kSpawnRecord);
        if (rec) out.serial = Read<std::uint16_t>(rec + 0x1E);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// "M_EX050#1A2B30" (model and the low address bits, to tell two of the same apart) or "-".
void Name(std::uintptr_t a, char* out, unsigned size) {
    if (!a) {
        std::snprintf(out, size, "-");
        return;
    }
    ActorInfo i;
    Info(a, i);
    std::snprintf(out, size, "%s#%llX", i.model, static_cast<unsigned long long>(a & 0xFFFFFF));
}

int FormatArg(std::uint64_t v, char* out, int size) {
    if (size <= 0) return 0;
    std::uint32_t tag = static_cast<std::uint32_t>(v >> 32), lo = static_cast<std::uint32_t>(v);
    int n;
    if (tag == kTagInt) {
        n = std::snprintf(out, size, "%d", static_cast<std::int32_t>(lo));
    } else if (tag == kTagFloat) {
        float f;
        std::memcpy(&f, &lo, 4);
        n = std::snprintf(out, size, "%.4g", f);
    } else if (tag == kTagAddr) {
        std::uintptr_t actor = ActorOf(v);
        std::uintptr_t x = Decode(lo);
        if (actor) {
            char name[48];
            Name(actor, name, sizeof(name));
            n = std::snprintf(out, size, "%s", name);
        } else if (!x) {
            n = std::snprintf(out, size, "adr 0x%X", lo);
        } else if (InExe(x)) {
            n = std::snprintf(out, size, "exe+0x%llX", static_cast<unsigned long long>(x - ExeBase()));
        } else {
            n = std::snprintf(out, size, "adr 0x%llX", static_cast<unsigned long long>(x));
        }
    } else {
        n = std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(v));
    }
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

bool SafeCopy(void* to, std::uintptr_t from, std::size_t bytes) {
    __try {
        std::memcpy(to, reinterpret_cast<const void*>(from), bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Runs the call and writes the actor's changed bytes to out: "+0x9B8 83 04 00 00 -> 93 04 00 00, ...".
__declspec(noinline) void CallWithDiff(PFN_Trap real, std::uint64_t* args, std::uintptr_t actor, char* out,
                                       int size) {
    std::uint8_t before[kActorBytes], after[kActorBytes];
    bool ok = SafeCopy(before, actor, kActorBytes);
    real(args);
    if (!ok || !SafeCopy(after, actor, kActorBytes)) {
        std::snprintf(out, size, "actor not readable");
        return;
    }
    int n = 0, ranges = 0, more = 0;
    for (std::size_t i = 0; i < kActorBytes;) {
        if (before[i] == after[i]) {
            ++i;
            continue;
        }
        std::size_t start = i & ~std::size_t(3), end = i + 1;  // a run of changes with gaps under 4 bytes
        for (std::size_t j = end; j < kActorBytes && j < end + 4; ++j)
            if (before[j] != after[j]) end = j + 1;
        while (end < kActorBytes) {
            std::size_t next = end;
            for (std::size_t j = end; j < kActorBytes && j < end + 4; ++j)
                if (before[j] != after[j]) next = j + 1;
            if (next == end) break;
            end = next;
        }
        end = (end + 3) & ~std::size_t(3);
        if (end > kActorBytes) end = kActorBytes;
        if (ranges++ >= 10 || n >= size - 64) {
            ++more;
        } else if (end - start <= 8) {
            n += std::snprintf(out + n, size - n, "%s+0x%zX", n ? ", " : "", start);
            for (std::size_t j = start; j < end; ++j) n += std::snprintf(out + n, size - n, " %02X", before[j]);
            n += std::snprintf(out + n, size - n, " ->");
            for (std::size_t j = start; j < end; ++j) n += std::snprintf(out + n, size - n, " %02X", after[j]);
        } else {
            n += std::snprintf(out + n, size - n, "%s+0x%zX..+0x%zX (%zu bytes)", n ? ", " : "", start, end, end - start);
        }
        i = end;
    }
    if (more && n < size - 16) std::snprintf(out + n, size - n, ", %d more", more);
    if (!ranges) std::snprintf(out, size, "no change");
}

// Host: an enemy's or field object's script call the world mirror doesn't send, the first time per call and object.
void LogUnsent(int k, std::uintptr_t owner, const std::uint64_t* in, int argc) {
    if (!owner) return;
    ActorInfo info;
    std::uint32_t team = 0;
    __try {
        team = Read<std::uint32_t>(owner + kTeam);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (team != 2 && team != 3) return;
    Info(owner, info);
    std::uint64_t key = ((static_cast<std::uint64_t>(k) << 32) | info.objId) + 1;
    std::uint32_t i = static_cast<std::uint32_t>((key * 0x9E3779B97F4A7C15ull) >> 51) % kUnsentSeen;
    for (int n = 0; g_unsentSeen[i] && n < kUnsentSeen; ++n, i = (i + 1) % kUnsentSeen)
        if (g_unsentSeen[i] == key) return;
    if (g_unsentSeen[i]) return;  // full
    g_unsentSeen[i] = key;
    char text[400];
    int n = 0;
    for (int a = 0; a < argc && n < static_cast<int>(sizeof(text)) - 2; ++a) {
        if (a) n += std::snprintf(text + n, sizeof(text) - n, ", ");
        n += FormatArg(in[a], text + n, static_cast<int>(sizeof(text)) - n);
    }
    text[n] = 0;
    const TrapName& name = kNames[k];
    ++g_unsentLines;
    Log("unsent call %u/%u %s(%s) by %s (obj 0x%X, team %u) in 0x%02X/0x%02X: not sent to the friend's game%s",
        name.table, name.index, name.name, text, info.model, info.objId, team, Read<std::uint8_t>(ExeBase() + kNow),
        Read<std::uint8_t>(ExeBase() + kNow + 1),
        g_unsentLines == kUnsentLines ? " (the last line of this log this session)" : "");
}

// Research (TODO 1.94, Data Luxord stalled after trap_obj_is_star(the host's Sora) = 1): when that call says 1 for a
// player, log what the game's protection check reads (VERIFIED_OFFSETS lead 'trap_obj_is_star', and 'How a landed hit
// becomes damage'): actor+0x6C8 bit 1, then the class check exe+0x3A7360(class, actor): exe+0x3D3570(actor) (flags
// +0x120, +0x18C, +0x9B8 & 0x210, ...), +0x126 bit 0, float +0xD70 > 0, exe+0x2A11400 bit 1.
void StarReport(std::uint64_t arg, std::uint64_t result) {
    static int lines = 0;
    static std::uint64_t lastFrame[2] = {};
    if (!g_starLog || lines >= 60 || static_cast<std::uint32_t>(result) != 1) return;
    std::uintptr_t a = ActorOf(arg);
    if (!a || !PuppetIsFieldPlayer(a)) return;
    std::uintptr_t ours = Read<std::uintptr_t>(ExeBase() + 0x2A171C8);
    int who = a == ours ? 0 : 1;
    if (lastFrame[who] && g_frame - lastFrame[who] < 60) return;
    lastFrame[who] = g_frame;
    ++lines;
    __try {
        using PFN_Check = bool(__fastcall*)(std::uintptr_t, std::uintptr_t);
        using PFN_One = bool(__fastcall*)(std::uintptr_t);
        std::uintptr_t cls = Decode(Read<std::uint32_t>(a));
        bool prot = cls && reinterpret_cast<PFN_Check>(ExeBase() + 0x3A7360)(cls, a);
        bool inner = reinterpret_cast<PFN_One>(ExeBase() + 0x3D3570)(a);
        Log("star: %s reads 'star' (frame %llu): +0x6C8 %08X, class check %d (exe+0x3D3570 %d; +0x120 %08X, +0x18C "
            "%08X, +0x9B8 %08X, +0x126 %02X, +0xD70 %.1f, exe+0x2A11400 %08X), motion %d, HP slot %u",
            a == ours ? "our Sora" : a == PuppetCloneActor() ? "the copy" : "another player actor",
            static_cast<unsigned long long>(g_frame), Read<std::uint32_t>(a + 0x6C8), prot, inner,
            Read<std::uint32_t>(a + 0x120), Read<std::uint32_t>(a + 0x18C), Read<std::uint32_t>(a + 0x9B8),
            Read<std::uint8_t>(a + 0x126), Read<float>(a + 0xD70), Read<std::uint32_t>(ExeBase() + 0x2A11400),
            Read<std::int32_t>(a + 0x180), Read<std::uint32_t>(ExeBase() + 0x2A23598));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("star: exception reading the protection check");
    }
}

void Called(int k, std::uint64_t* args) {
    TrapState& t = g_trap[k];
    if (!CoopActive()) return t.real(args);
    ++t.count;
    if (t.room != g_roomGen) {
        t.room = g_roomGen;
        t.logged = t.diffs = t.repeats = 0;
        t.last = 0;
    }
    const TrapName& name = kNames[k];
    bool returns = (name.flags & kReturns) != 0;
    int argc = static_cast<int>(name.flags & 0xFFFF);
    if (argc > 16) argc = 16;
    bool log = g_trapLog && !t.quiet && t.logged < (returns && !t.makes ? kLogQueries : kLogActions);
    bool mirror = WorldMirrorWants(name.table, name.index);
    SharedMoveOnCall(name.table, name.index, args);  // watches only (a boss's move both players play)
    if (GrabSyncHolds(name.table, name.index, args)) return;  // a grab's camera or flash: the friend's (sent)
    if (ReactionSyncHolds(name.table, name.index, args)) return;  // a reaction command for the friend (held back)
    if (MissionSyncHolds(name.table, name.index, args)) return;  // our mission value call: the host's values count
    bool unsent = g_unsentLog && !mirror && !t.unsentSkip && g_unsentLines < kUnsentLines && AvatarLinkIsHost();
    if (g_depth < 8) {
        g_curTrap[g_depth] = k;
        g_curArgs[g_depth] = args;
    }
    if (g_depth == 0) g_trapThread = GetCurrentThreadId();
    ++g_depth;
    if (!log && !mirror && !unsent) {
        std::uint64_t a0 = args[0];
        t.real(args);
        --g_depth;
        if (name.table == 1 && name.index == 225) StarReport(a0, args[0]);
        return;
    }
    std::uint64_t in[16];
    std::memcpy(in, args, argc * sizeof(std::uint64_t));
    std::uintptr_t owner = log || unsent ? ScriptActor() : 0;
    int pc = log ? ScriptPc() : 0;
    std::uintptr_t target = log && !returns && argc ? ActorOf(in[0]) : 0;
    char diff[400] = "";
    if (target && t.diffs < kDiffs) {
        ++t.diffs;
        CallWithDiff(t.real, args, target, diff, sizeof(diff));
    } else {
        t.real(args);
    }
    --g_depth;
    if (name.table == 1 && name.index == 225) StarReport(in[0], args[0]);
    if (mirror) WorldMirrorOnTrap(name.table, name.index, in, argc, returns ? args[0] : 0);
    if (unsent) LogUnsent(k, owner, in, argc);
    if (!log) return;
    std::uint64_t hash = 1469598103934665603ull ^ owner;  // FNV-1a over the actor, the arguments and the result
    for (int i = 0; i <= argc; ++i) {
        hash = (hash ^ (i < argc ? in[i] : returns ? args[0] : 0)) * 1099511628211ull;
    }
    if (hash == t.last && !diff[0]) {
        ++t.repeats;
        return;
    }
    t.last = hash;
    ++t.logged;
    char again[40] = "";
    if (t.repeats) std::snprintf(again, sizeof(again), " (the one before: %u more times)", t.repeats);
    t.repeats = 0;
    char text[480];
    int n = 0;
    for (int i = 0; i < argc && n < static_cast<int>(sizeof(text)) - 2; ++i) {
        if (i) n += std::snprintf(text + n, sizeof(text) - n, ", ");
        n += FormatArg(in[i], text + n, static_cast<int>(sizeof(text)) - n);
    }
    text[n] = 0;
    char result[64] = "";
    if (returns) {
        std::memcpy(result, " = ", 4);
        FormatArg(args[0], result + 3, static_cast<int>(sizeof(result)) - 3);
    }
    char who[48];
    Name(owner, who, sizeof(who));
    Log("trap %u/%u %s(%s)%s by %s pc %d frame %llu%s%s%s%s", name.table, name.index, name.name, text, result, who, pc,
        static_cast<unsigned long long>(g_frame), again, diff[0] ? " | changed: " : "", diff,
        g_depth ? " (inside another call)" : "");
}

template <int K>
void __fastcall Thunk(std::uint64_t* args) {
    Called(K, args);
}
template <int... K>
std::array<PFN_Trap, sizeof...(K)> MakeThunks(std::integer_sequence<int, K...>) {
    return {{&Thunk<K>...}};
}

bool Quiet(const char* name) {
    static const char* const kQuiet[] = {"trap_vector_", "trap_abs", "trap_frametime", "trap_puti", "trap_putf",
                                         "trap_puts", "trap_stput", "trap_sqrt", "trap_sin", "trap_cos",
                                         "trap_atan", "trap_vector"};
    for (const char* q : kQuiet)
        if (std::strncmp(name, q, std::strlen(q)) == 0) return true;
    return false;
}

// Left out of the log of calls not sent: maths and prints (Quiet), queries (a result and not 'Makes'), the calls of an
// enemy's own thinking, moves and attacks (one brain: only the host's enemies think; world sync sends where they are
// and their motions, the host's game deals their damage), and the calls other modules take care of.
bool UnsentSkip(const TrapName& name, bool quiet, bool makes) {
    if (quiet || ((name.flags & kReturns) && !makes)) return true;
    static const char* const kInternal[] = {
        "trap_act_", "trap_obj_act_", "trap_obj_set_act_table", "trap_attack_", "trap_target_", "trap_memory_",
        "method_vector_", "trap_vector", "trap_obj_wish_dir", "trap_obj_set_movement", "trap_obj_search",
        "trap_obj_set_pos", "trap_obj_move_request", "trap_sysobj_motion_", "trap_obj_motion_", "trap_magic_start_thread",
        "trap_teamwork_", "trap_obj_set_dir", "trap_obj_fly", "trap_obj_set_unit_arg", "trap_thread_",
        // objects made, dying, leaving: spawn sync and world sync
        "trap_sysobj_appear", "trap_obj_dead", "trap_obj_leave"};
    for (const char* q : kInternal)
        if (std::strncmp(name.name, q, std::strlen(q)) == 0) return true;
    if (MissionSyncCovers(name.table, name.index)) return true;
    if (name.table == 1 && (name.index == 165 || name.index == 237)) return true;  // reaction_sync.cpp
    if (name.table == 1 && (name.index == 29 || name.index == 112 || name.index == 153 || name.index == 164))
        return true;  // grab_sync.cpp
    if (name.table == 0 && (name.index == 31 || name.index == 32)) return true;  // grab_sync.cpp (white flash)
    return false;
}

// Calls that return what they made: an effect start returned a handle and was capped like a query (12 per room), so
// most of a fight's effects were missing from the log.
bool Makes(const char* name) {
    static const char* const kMakes[] = {"_start", "_appear", "_new", "_alloc", "_dup"};
    for (const char* m : kMakes)
        if (std::strstr(name, m)) return true;
    return false;
}

void InstallTrapLog() {
    static const std::array<PFN_Trap, kTrapCount> thunks = MakeThunks(std::make_integer_sequence<int, kTrapCount>{});
    std::uintptr_t exe = ExeBase();
    int patched = 0, skipped = 0;
    for (int k = 0; k < kTrapCount; ++k) {
        const TrapName& name = kNames[k];
        std::uintptr_t table = Read<std::uintptr_t>(exe + kTrapTables + 8 * name.table);
        if (!InExe(table)) {
            ++skipped;
            continue;
        }
        std::uintptr_t entry = table + 16 * name.index;
        auto fn = Read<std::uintptr_t>(entry);
        if (Read<std::uint32_t>(entry + 8) != name.flags || !fn) {
            if (skipped++ < 5)
                Log("trap log: %u/%u %s: entry differs (flags 0x%X, fn 0x%llX); not logged", name.table, name.index,
                    name.name, Read<std::uint32_t>(entry + 8), static_cast<unsigned long long>(fn));
            continue;
        }
        if (fn <= exe + 0x1000 || fn >= exe + 0x579000) {  // already redirected (scene_sync.cpp, another mod): we go in front
            HMODULE module = nullptr;
            char path[MAX_PATH] = "?";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(fn), &module))
                GetModuleFileNameA(module, path, sizeof(path));
            const char* file = std::strrchr(path, '\\');
            Log("trap log: %u/%u %s already goes to %s+0x%llX (redirected before us); logged in front of it", name.table,
                name.index, name.name, file ? file + 1 : path,
                static_cast<unsigned long long>(module ? fn - reinterpret_cast<std::uintptr_t>(module) : fn));
        }
        g_trap[k].real = reinterpret_cast<PFN_Trap>(fn);
        g_trap[k].quiet = Quiet(name.name);
        g_trap[k].makes = Makes(name.name);
        g_trap[k].unsentSkip = UnsentSkip(name, g_trap[k].quiet, g_trap[k].makes);
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(entry), 8, PAGE_READWRITE, &old)) {
            ++skipped;
            continue;
        }
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(entry),
                              static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(thunks[k])));
        VirtualProtect(reinterpret_cast<void*>(entry), 8, old, &old);
        ++patched;
    }
    Log("trap log: %d of %d script calls logged (%d skipped)", patched, kTrapCount, skipped);
}

// Every 10 s: the calls made since, most first.
void LogCounts() {
    std::uint64_t now = GetTickCount64();
    if (now - g_countsTick < 10000) return;
    g_countsTick = now;
    std::uint64_t total = 0;
    int used = 0;
    for (const TrapState& t : g_trap)
        if (t.count) total += t.count, ++used;
    if (!total) return;
    Log("trap counts, last 10 s: %llu calls, %d different, room 0x%02X/0x%02X", static_cast<unsigned long long>(total),
        used, Read<std::uint8_t>(ExeBase() + kNow), Read<std::uint8_t>(ExeBase() + kNow + 1));
    char line[400];
    int n = 0;
    for (;;) {
        int best = -1;
        for (int k = 0; k < kTrapCount; ++k)
            if (g_trap[k].count && (best < 0 || g_trap[k].count > g_trap[best].count)) best = k;
        if (best < 0) break;
        const char* name = kNames[best].name;
        if (std::strncmp(name, "trap_", 5) == 0) name += 5;
        n += std::snprintf(line + n, sizeof(line) - n, "%s%s %u", n ? ", " : "  ", name, g_trap[best].count);
        g_trap[best].count = 0;
        if (n > 300) {
            Log("%s", line);
            n = 0;
        }
    }
    if (n) Log("%s", line);
}

// ---- Census ----
struct Made {
    std::uintptr_t actor;
    std::uint32_t objId;
    int trap;              // the script call that asked, -1 = game code
    std::uintptr_t owner;  // the actor whose script it was
    char chain[100];
};
Made g_made[64];
int g_madeNext = 0;
// Objects a script made (Data Roxas's keyblades, made by his trap_sysobj_appear) and their maker, kept with one brain
// too (not only with the census): aggro.cpp gives such an object its maker's player (WorldCensusMadeBy).
struct Owner {
    std::uintptr_t actor;
    std::uint32_t objId;
    std::uintptr_t owner;
};
Owner g_owners[128];
int g_ownerNext = 0;

using PFN_Factory = std::uintptr_t(__fastcall*)(std::uint32_t objId, const float* pos, float yaw);
PFN_Factory g_realFactory = nullptr;

std::uintptr_t __fastcall HookFactory(std::uint32_t objId, const float* pos, float yaw) {
    std::uintptr_t a = g_realFactory(objId, pos, yaw);
    int trap = g_depth > 0 && g_depth <= 8 ? g_curTrap[g_depth - 1] : -1;
    if (a && trap >= 0) {
        Owner* slot = nullptr;
        for (Owner& o : g_owners)
            if (o.actor == a) slot = &o;  // the same memory made again: replace
        if (!slot) slot = &g_owners[g_ownerNext++ % 128];
        *slot = {a, objId, ScriptActor()};
        WorldSyncScriptMade(a, slot->owner);  // one brain: a copy from birth when our copy of a host enemy made it
    }
    if (!g_census) return a;
    Made& m = g_made[g_madeNext++ % 64];
    m.actor = a;
    m.objId = objId;
    m.trap = g_depth > 0 && g_depth <= 8 ? g_curTrap[g_depth - 1] : -1;
    m.owner = m.trap >= 0 ? ScriptActor() : 0;
    CallerChain(m.chain, sizeof(m.chain), 0);
    char name[48], owner[48];
    Name(a, name, sizeof(name));
    Name(m.owner, owner, sizeof(owner));
    float p[3] = {};
    if (pos) SafeCopy(p, reinterpret_cast<std::uintptr_t>(pos), sizeof(p));
    Log("census: made %s (object 0x%X) at (%.0f, %.0f, %.0f) %s%s%s%s, from %s, frame %llu", name, objId, p[0], p[1],
        p[2], m.trap >= 0 ? "by script call " : "by game code", m.trap >= 0 ? kNames[m.trap].name : "",
        m.trap >= 0 ? " of " : "", m.trap >= 0 ? owner : "", m.chain, static_cast<unsigned long long>(g_frame));
    return a;
}

const Made* MadeFor(std::uintptr_t a, std::uint32_t objId) {
    for (int i = 0; i < 64; ++i) {
        const Made& m = g_made[(g_madeNext - 1 - i + 64 * 64) % 64];
        if (m.actor == a && (m.objId & 0xFFFF) == (objId & 0xFFFF)) return &m;
    }
    return nullptr;
}

struct Seen {
    std::uintptr_t actor;
    std::uint32_t objId;
    std::uint64_t first, last;  // frames
    char model[16];
};
Seen g_seen[300];
int g_seenCount = 0;
std::uint16_t g_room = 0xFFFF;
bool g_wasField = false;
std::uint64_t g_dumpFrame = 0;

void Describe(std::uintptr_t a, char* out, int size) {
    ActorInfo i;
    Info(a, i);
    int n = std::snprintf(out, size, "%s#%llX obj 0x%X t%u", i.model, static_cast<unsigned long long>(a & 0xFFFFFF),
                          i.objId, i.type);
    __try {
        std::uintptr_t stats = Read<std::uintptr_t>(a + kStats);
        const float* pos = reinterpret_cast<const float*>(a + kPos);
        std::uintptr_t cls = Decode(Read<std::uint32_t>(a));
        n += std::snprintf(out + n, size - n,
                           " team %u hp %d pos (%.0f %.0f %.0f) mot %d%s thr %u f120 0x%X 9b8 0x%X fade %.2f cls %s0x%llX "
                           "self4 %d",
                           Read<std::uint32_t>(a + kTeam), stats ? Read<std::int32_t>(stats) : -1, pos[0], pos[1], pos[2],
                           Read<std::int32_t>(a + kMotion), Read<std::uintptr_t>(a + kMotionBank) ? " (bank)" : "",
                           Read<std::uint32_t>(a + kThreads), Read<std::uint32_t>(a + kFlags120),
                           Read<std::uint32_t>(a + kFlags9B8), Read<float>(a + kFade), InExe(cls) ? "exe+" : "",
                           static_cast<unsigned long long>(InExe(cls) ? cls - ExeBase() : cls),
                           Decode(Read<std::uint32_t>(a + 4)) == a);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out + n, size - n, " (not readable)");
        return;
    }
    if (n >= size - 8) return;
    if (i.serial) {
        std::snprintf(out + n, size - n, " | spawn serial %u", i.serial);
    } else if (const Made* m = MadeFor(a, i.objId)) {
        char owner[48];
        Name(m->owner, owner, sizeof(owner));
        if (m->trap >= 0)
            std::snprintf(out + n, size - n, " | made by %s of %s", kNames[m->trap].name, owner);
        else
            std::snprintf(out + n, size - n, " | made by %s", m->chain);
    } else {
        std::snprintf(out + n, size - n, " | no spawn record");
    }
}

void CensusFrame() {
    std::uintptr_t exe = ExeBase();
    std::uint16_t room = static_cast<std::uint16_t>(Read<std::uint8_t>(exe + kNow) << 8 | Read<std::uint8_t>(exe + kNow + 1));
    bool field = Read<std::uint8_t>(exe + kInField) != 0;
    if (room != g_room || (field && !g_wasField)) {
        g_room = room;
        ++g_roomGen;
        g_seenCount = 0;
        g_dumpFrame = g_frame + 60;
    }
    g_wasField = field;
    if (!g_census || !field || g_frame % 15 != 0) return;
    bool dump = g_frame >= g_dumpFrame;
    bool first = g_seenCount == 0;
    if (dump) {
        g_dumpFrame = g_frame + 600;
        Log("census: room 0x%02X/0x%02X, frame %llu, the actor list:", room >> 8, room & 0xFF,
            static_cast<unsigned long long>(g_frame));
    }
    char text[400];
    int count = 0;
    __try {
        std::uintptr_t a = Read<std::uintptr_t>(exe + kListHead);
        for (int n = 0; a && n < 512; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor))) {
            ++count;
            ActorInfo i;
            Info(a, i);
            int s = 0;
            while (s < g_seenCount && !(g_seen[s].actor == a && g_seen[s].objId == i.objId)) ++s;
            if (s == g_seenCount && g_seenCount < 300) {
                Seen& seen = g_seen[g_seenCount++];
                seen = {a, i.objId, g_frame, g_frame, {}};
                std::memcpy(seen.model, i.model, sizeof(seen.model) - 1);
                if (!first && !dump) {
                    Describe(a, text, sizeof(text));
                    Log("census: + %s", text);
                }
            }
            if (s < g_seenCount) g_seen[s].last = g_frame;
            if (dump) {
                Describe(a, text, sizeof(text));
                Log("census:   %s", text);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("census: the actor list changed while read; next time");
        return;
    }
    for (int s = 0; s < g_seenCount;) {
        if (g_seen[s].last == g_frame) {
            ++s;
            continue;
        }
        Log("census: - %s#%llX obj 0x%X (was there %.1f s)", g_seen[s].model,
            static_cast<unsigned long long>(g_seen[s].actor & 0xFFFFFF), g_seen[s].objId,
            (g_frame - g_seen[s].first) / 60.0);
        g_seen[s] = g_seen[--g_seenCount];
    }
    if (dump) Log("census: %d actors", count);
}

// KH2COOP_FX_LOG: effect creations.
constexpr std::uintptr_t kMakeEffect = 0x2BD510;  // (instance + 0x10, entry, u32, u32, u32, float) -> effect
constexpr std::uint8_t kMakeEffectBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr std::uintptr_t kRunningLimit = 0x2A24CC0;  // the running Limit object (+0x18 = its effect instance)
using PFN_MakeEffect = std::uintptr_t(__fastcall*)(std::uintptr_t, std::int32_t, std::uint32_t, std::uint32_t,
                                                   std::uint32_t, float);
PFN_MakeEffect g_realMakeEffect = nullptr;
bool g_fxLog = false;
std::uint32_t g_fxRoom = 0, g_fxLogged = 0, g_fxCount = 0;

// Whose effect instance it is: "own" = actor + 0x80, "motion" = the effects of the motion the actor plays now
// (actor+0x158+0x148, when that isn't its own), "limit" = the running Limit's.
const char* FxOwner(std::uintptr_t inst, std::uintptr_t& actor) {
    actor = 0;
    __try {
        std::uintptr_t limit = Read<std::uintptr_t>(ExeBase() + kRunningLimit);
        if (limit && inst == limit + 0x18) return "limit";
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor))) {
            if (inst == a + 0x80) {
                actor = a;
                return "own";
            }
            if (inst == Read<std::uintptr_t>(a + 0x158 + 0x148)) {
                actor = a;
                return "motion";
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return "other";
}

std::uintptr_t __fastcall HookMakeEffect(std::uintptr_t mgr, std::int32_t entry, std::uint32_t a, std::uint32_t b,
                                         std::uint32_t c, float d) {
    std::uintptr_t fx = g_realMakeEffect(mgr, entry, a, b, c, d);
    ++g_fxCount;
    if (g_fxRoom != g_roomGen) {
        g_fxRoom = g_roomGen;
        g_fxLogged = 0;
    }
    if (g_fxLogged >= 200) return fx;
    ++g_fxLogged;
    std::uintptr_t exe = ExeBase();
    void* frames[8] = {};
    USHORT n = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    std::uint32_t stack[7] = {};
    stack[0] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - exe);
    for (int i = 1; i < 7 && i < n; ++i) {
        auto f = reinterpret_cast<std::uintptr_t>(frames[i]);
        stack[i] = InExe(f) ? static_cast<std::uint32_t>(f - exe) : 0;
    }
    std::uintptr_t inst = mgr - 0x10, actor = 0, pax = 0;
    const char* owner = FxOwner(inst, actor);
    __try {
        pax = Read<std::uintptr_t>(inst);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    char who[48], script[48];
    Name(actor, who, sizeof(who));
    Name(ScriptActor(), script, sizeof(script));
    int trap = g_depth > 0 && g_depth <= 8 ? g_curTrap[g_depth - 1] : -1;
    std::uintptr_t player = Read<std::uintptr_t>(exe + kListHead);
    Log("fx: #%u entry %d (%u, %u, %u) -> %llX, instance %s %s%s (%s0x%llX), PAX %s0x%llX; call %s by %s; "
        "stack exe+0x%X <- 0x%X <- 0x%X <- 0x%X <- 0x%X <- 0x%X <- 0x%X",
        g_fxCount, entry, a, b, c, static_cast<unsigned long long>(fx & 0xFFFFFF), owner, who,
        actor && actor == player ? " (our Sora)" : "", InExe(inst) ? "exe+" : "",
        static_cast<unsigned long long>(InExe(inst) ? inst - exe : inst), InExe(pax) ? "exe+" : "",
        static_cast<unsigned long long>(InExe(pax) ? pax - exe : pax), trap >= 0 ? kNames[trap].name : "-", script,
        stack[0], stack[1], stack[2], stack[3], stack[4], stack[5], stack[6]);
    return fx;
}

// Same switch: script threads made for a P_ actor (exe+0x3E1970(parent, 0, pc, args, n) -> thread, +0x60 = its
// actor), with the code that made them: which code starts a spell's script on which Sora.
constexpr std::uintptr_t kMakeThread = 0x3E1970;
constexpr std::uint8_t kMakeThreadBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
using PFN_MakeThread = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uint32_t, std::uint32_t, std::uintptr_t,
                                                   std::uint32_t);
PFN_MakeThread g_realMakeThread = nullptr;
std::uint32_t g_threadRoom = 0, g_threadLogged = 0;

std::uintptr_t __fastcall HookMakeThread(std::uintptr_t parent, std::uint32_t a, std::uint32_t pc, std::uintptr_t args,
                                         std::uint32_t n) {
    std::uintptr_t thread = g_realMakeThread(parent, a, pc, args, n);
    if (g_threadRoom != g_roomGen) {
        g_threadRoom = g_roomGen;
        g_threadLogged = 0;
    }
    if (!thread || g_threadLogged >= 100) return thread;
    std::uintptr_t actor = 0;
    __try {
        actor = Read<std::uintptr_t>(thread + 0x60);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    ActorInfo i;
    Info(actor, i);
    if (!actor || i.model[0] != 'P') return thread;
    ++g_threadLogged;
    std::uintptr_t exe = ExeBase();
    void* frames[8] = {};
    USHORT got = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
    std::uint32_t stack[7] = {};
    stack[0] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - exe);
    for (int k = 1; k < 7 && k < got; ++k) {
        auto f = reinterpret_cast<std::uintptr_t>(frames[k]);
        stack[k] = InExe(f) ? static_cast<std::uint32_t>(f - exe) : 0;
    }
    char who[48], script[48];
    Name(actor, who, sizeof(who));
    Name(ScriptActor(), script, sizeof(script));
    int trap = g_depth > 0 && g_depth <= 8 ? g_curTrap[g_depth - 1] : -1;
    std::uintptr_t prog = 0;
    __try {
        prog = Read<std::uintptr_t>(parent);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    Log("fx: thread for %s%s at pc %u (%u args), program %s0x%llX; call %s by %s; stack exe+0x%X <- 0x%X <- 0x%X <- "
        "0x%X <- 0x%X <- 0x%X <- 0x%X",
        who, actor == Read<std::uintptr_t>(exe + kListHead) ? " (our Sora)" : "", pc, n, InExe(prog) ? "exe+" : "",
        static_cast<unsigned long long>(InExe(prog) ? prog - exe : prog), trap >= 0 ? kNames[trap].name : "-", script,
        stack[0], stack[1], stack[2], stack[3], stack[4], stack[5], stack[6]);
    return thread;
}

// KH2COOP_HEAL_TRACE: the companions' heal choice (TODO 1.52 / 1.58, VERIFIED_OFFSETS lead 'Companion heal choice').
// The brain exe+0x1B15F0 (type handler +0x20) runs the think callback at actor+0xE00, exe+0x1B53B0: it sets
// actor+0xF78 = who needs help (the player, itself or the other companion at a quarter HP or less) and picks an
// action (+0xEE8, a record whose handler is at +8) with a target (handle at +0xBF8). After each think, a line when
// one of those changes; each item a companion uses (exe+0x1B7D90(companion, item, target, u32)); every 10 s a count.
constexpr std::uintptr_t kThink = 0x1B53B0;
constexpr std::uint8_t kThinkBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0x81, 0xB8, 0x09, 0x00, 0x00};
constexpr std::uintptr_t kUseItem = 0x1B7D90;
constexpr std::uint8_t kUseItemBytes[] = {0x4D, 0x85, 0xC0, 0x0F, 0x84, 0x89, 0x00, 0x00, 0x00};
constexpr std::uintptr_t kNeedsHelp = 0xF78, kAction = 0xEE8, kTarget = 0xBF8;
using PFN_Think = void(__fastcall*)(std::uintptr_t);
using PFN_UseItem = void(__fastcall*)(std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint32_t);
PFN_Think g_realThink = nullptr;
PFN_UseItem g_realUseItem = nullptr;
bool g_healTrace = false;
struct Mind {
    std::uintptr_t actor, needs, action, target;
    std::uint32_t thinks, mirrored, skipped, picks, help[4];  // help: our Sora, the Sora copy, itself, other
};
Mind g_minds[4];
std::uint32_t g_mindLines = 0, g_mindRoom = 0;  // lines logged in this room (world, room | 0x10000)
std::uint64_t g_mindTick = 0;

const char* WhoFor(std::uintptr_t x, std::uintptr_t self, char* buf, unsigned size) {
    if (!x) return "nobody";
    if (x == self) return "itself";
    if (x == Read<std::uintptr_t>(ExeBase() + kListHead)) return "our Sora";
    if (x == PuppetCloneActor()) return "the Sora copy";
    Name(x, buf, size);
    return buf;
}

void Hp(std::uintptr_t a, int& hp, int& max) {
    hp = max = -1;
    __try {
        std::uintptr_t stats = a ? Read<std::uintptr_t>(a + kStats) : 0;
        if (stats) {
            hp = Read<std::int32_t>(stats);
            max = Read<std::int32_t>(stats + 4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// After a companion's think (companion_sync.cpp owns the hook; skipped = it didn't run: a mirrored companion).
void ThinkTrace(std::uintptr_t self, bool skipped) {
    __try {
        std::uint32_t room = Read<std::uint16_t>(ExeBase() + kNow) | 0x10000;
        if (g_mindRoom != room) {
            g_mindRoom = room;
            g_mindLines = 0;
            std::memset(g_minds, 0, sizeof(g_minds));
        }
        Mind* m = nullptr;
        for (auto& x : g_minds)
            if (x.actor == self || (!m && !x.actor)) m = &x;
        if (!m || (m->actor && m->actor != self)) return;
        m->actor = self;
        bool mirrored = WorldSyncSkipAi(self);
        if (skipped) {
            ++m->skipped;
            return;
        }
        ++m->thinks;
        if (mirrored) ++m->mirrored;
        std::uintptr_t needs = Read<std::uintptr_t>(self + kNeedsHelp), action = Read<std::uintptr_t>(self + kAction);
        std::uintptr_t target = Decode(Read<std::uint32_t>(self + kTarget));
        std::uintptr_t player = Read<std::uintptr_t>(ExeBase() + kListHead);
        if (needs) ++m->help[needs == player ? 0 : needs == PuppetCloneActor() ? 1 : needs == self ? 2 : 3];
        if (action && action != m->action) ++m->picks;
        bool changed = needs != m->needs || action != m->action || target != m->target;
        m->needs = needs;
        m->action = action;
        m->target = target;
        if (!changed || g_mindLines >= 400) return;
        ++g_mindLines;
        char me[48], b1[48], b2[48];
        Name(self, me, sizeof(me));
        int hp, max;
        Hp(needs, hp, max);
        std::uintptr_t handler = action ? Read<std::uintptr_t>(action + 8) : 0;
        Log("heal trace: %s%s: needs help %s (HP %d/%d), action %s0x%llX (flags 0x%X), target %s, motion %d", me,
            mirrored ? " (mirrored: its brain still ran)" : "", WhoFor(needs, self, b1, sizeof(b1)), hp, max,
            InExe(handler) ? "exe+" : "", static_cast<unsigned long long>(InExe(handler) ? handler - ExeBase() : handler),
            action ? Read<std::uint8_t>(action) : 0, WhoFor(target, self, b2, sizeof(b2)), Read<std::int32_t>(self + kMotion));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookThink(std::uintptr_t self) {
    g_realThink(self);
    ThinkTrace(self, false);
}

void __fastcall HookUseItem(std::uintptr_t self, std::uint32_t item, std::uintptr_t target, std::uint32_t flag) {
    __try {
        char me[48], b[48];
        Name(self, me, sizeof(me));
        int hp, max;
        Hp(target, hp, max);
        Log("heal trace: %s uses item %u on %s (HP %d/%d)", me, item, WhoFor(target, self, b, sizeof(b)), hp, max);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_realUseItem(self, item, target, flag);
}

void HealTraceFrame() {
    std::uint64_t now = GetTickCount64();
    if (now - g_mindTick < 10000) return;
    g_mindTick = now;
    for (auto& m : g_minds) {
        if (!m.actor || !(m.thinks || m.skipped)) continue;
        __try {
            char me[48];
            Name(m.actor, me, sizeof(me));
            int hp, max;
            Hp(m.actor, hp, max);
            std::uintptr_t stats = Read<std::uintptr_t>(m.actor + kStats);
            Log("heal trace: %s in 10 s: %u thinks (%u mirrored, %u skipped), needs help: our Sora %u, the Sora copy %u, itself %u, "
                "other %u; %u action picks; HP %d/%d, stat +0x180/+0x184 (MP?) %d/%d",
                me, m.thinks, m.mirrored, m.skipped, m.help[0], m.help[1], m.help[2], m.help[3], m.picks, hp, max,
                stats ? Read<std::int32_t>(stats + 0x180) : -1, stats ? Read<std::int32_t>(stats + 0x184) : -1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        m.thinks = m.mirrored = m.skipped = m.picks = 0;
        std::memset(m.help, 0, sizeof(m.help));
    }
}

}  // namespace

std::uintptr_t WorldCensusScriptActor() { return ScriptActor(); }

void WorldCensusLogCrashCall() {
    if (g_depth <= 0 || GetCurrentThreadId() != g_trapThread) return;
    int depth = g_depth < 8 ? g_depth : 8;
    for (int d = depth - 1; d >= 0; --d) {
        int k = g_curTrap[d];
        if (k < 0 || k >= static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) continue;
        const TrapName& name = kNames[k];
        int argc = static_cast<int>(name.flags & 0xFFFF);
        if (argc > 8) argc = 8;
        char text[400];
        int n = 0;
        __try {
            for (int i = 0; i < argc && n < static_cast<int>(sizeof(text)) - 64; ++i) {
                if (i) n += std::snprintf(text + n, sizeof(text) - n, ", ");
                n += FormatArg(g_curArgs[d][i], text + n, static_cast<int>(sizeof(text)) - n);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::snprintf(text + n, sizeof(text) - n, "(arguments not readable)");
        }
        char owner[48] = "?";
        if (d == depth - 1) Name(ScriptActor(), owner, sizeof(owner));
        Log("  in script call %d/%d %s(%s)%s%s, pc %d", name.table, name.index, name.name, text,
            d == depth - 1 ? " by " : " (an outer call)", d == depth - 1 ? owner : "",
            d == depth - 1 ? ScriptPc() : -1);
    }
}

void WorldCensusThinkTrace(std::uintptr_t self, bool skipped) {
    if (g_healTrace) ThinkTrace(self, skipped);
}

std::uintptr_t WorldCensusMadeBy(std::uintptr_t a) {
    if (!a) return 0;
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        if (!InExe(row)) return 0;
        std::uint32_t objId = Read<std::uint32_t>(row);
        for (const Owner& o : g_owners)
            if (o.actor == a && (o.objId & 0xFFFF) == (objId & 0xFFFF)) return o.owner;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

void* WorldCensusTrapReal(int table, int index) {
    for (int k = 0; k < kTrapCount; ++k)
        if (kNames[k].table == table && kNames[k].index == index) {
            if (g_trap[k].real) return reinterpret_cast<void*>(g_trap[k].real);
            std::uintptr_t t = Read<std::uintptr_t>(ExeBase() + kTrapTables + 8 * table);
            return InExe(t) ? reinterpret_cast<void*>(Read<std::uintptr_t>(t + 16 * index)) : nullptr;
        }
    return nullptr;
}

const char* WorldCensusTrapName(int table, int index) {
    for (const TrapName& n : kNames)
        if (n.table == table && n.index == index) return n.name;
    return "?";
}

void WorldCensusInit() {
    g_census = EnvInt("KH2COOP_CENSUS", 0) == 1;
    g_trapLog = EnvInt("KH2COOP_TRAP_LOG", 0) == 1;
    g_starLog = EnvInt("KH2COOP_STAR_LOG", 0) == 1;
    g_unsentLog = EnvInt("KH2COOP_UNSENT_LOG", 1) == 1;
    g_fxLog = EnvInt("KH2COOP_FX_LOG", 0) == 1;
    g_healTrace = EnvInt("KH2COOP_HEAL_TRACE", 0) == 1;
    if (g_healTrace) {
        Log("heal trace (research, TODO 1.52 / 1.58): the companions' heal choice is logged");
        if (EnvInt("KH2COOP_COMPANION_SYNC", 1) != 1)  // else companion_sync.cpp hooks the think and calls ThinkTrace
            HookFunction(kThink, kThinkBytes, sizeof(kThinkBytes), reinterpret_cast<void*>(HookThink),
                         reinterpret_cast<void**>(&g_realThink), "companion think (heal trace)");
        HookFunction(kUseItem, kUseItemBytes, sizeof(kUseItemBytes), reinterpret_cast<void*>(HookUseItem),
                     reinterpret_cast<void**>(&g_realUseItem), "companion item use (heal trace)");
    }
    // The world mirror (one brain) gets the host's script calls from the thunks, so they always go in.
    Log("world census (research, TODO 1.55): census %s, script call log %s, effect log %s, log of calls not sent %s "
        "(host only); script call thunks for the world mirror", g_census ? "on" : "off", g_trapLog ? "on" : "off",
        g_fxLog ? "on" : "off", g_unsentLog ? "on" : "off");
    if (g_fxLog)
        HookFunction(kMakeEffect, kMakeEffectBytes, sizeof(kMakeEffectBytes), reinterpret_cast<void*>(HookMakeEffect),
                     reinterpret_cast<void**>(&g_realMakeEffect), "effect maker (effect log)");
    if (g_fxLog)
        HookFunction(kMakeThread, kMakeThreadBytes, sizeof(kMakeThreadBytes), reinterpret_cast<void*>(HookMakeThread),
                     reinterpret_cast<void**>(&g_realMakeThread), "script thread maker (effect log)");
    InstallTrapLog();
    HookFunction(kFactory, kFactoryBytes, sizeof(kFactoryBytes), reinterpret_cast<void*>(HookFactory),
                 reinterpret_cast<void**>(&g_realFactory), "actor factory (census, objects' makers)");
}

void WorldCensusFrame() {
    if (g_healTrace) HealTraceFrame();
    if (!g_census && !g_trapLog) return;
    ++g_frame;
    CensusFrame();
    if (g_trapLog) LogCounts();
}

}  // namespace kh2coop
