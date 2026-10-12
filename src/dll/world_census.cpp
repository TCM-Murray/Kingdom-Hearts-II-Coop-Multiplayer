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
// Both only while co-op is on. Names of the calls: trap_names.inc (from OpenKH).

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "common.hpp"
#include "world_census.hpp"
#include "world_mirror.hpp"

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
};
TrapState g_trap[kTrapCount];
constexpr std::uint32_t kLogActions = 300, kLogQueries = 12, kDiffs = 6;  // different calls per call and room

bool g_census = false, g_trapLog = false;
std::uint64_t g_frame = 0;
std::uint32_t g_roomGen = 1;  // +1 per room load: resets the per-room limits
std::uint64_t g_countsTick = 0;

// The calls running now (a call can run scripts that make calls).
int g_depth = 0;
int g_curTrap[8];

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
    bool log = g_trapLog && !t.quiet && t.logged < (returns ? kLogQueries : kLogActions);
    bool mirror = WorldMirrorWants(name.table, name.index);
    if (g_depth < 8) g_curTrap[g_depth] = k;
    ++g_depth;
    if (!log && !mirror) {
        t.real(args);
        --g_depth;
        return;
    }
    std::uint64_t in[16];
    std::memcpy(in, args, argc * sizeof(std::uint64_t));
    std::uintptr_t owner = log ? ScriptActor() : 0;
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
    if (mirror) WorldMirrorOnTrap(name.table, name.index, in, argc, returns ? args[0] : 0);
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

using PFN_Factory = std::uintptr_t(__fastcall*)(std::uint32_t objId, const float* pos, float yaw);
PFN_Factory g_realFactory = nullptr;

std::uintptr_t __fastcall HookFactory(std::uint32_t objId, const float* pos, float yaw) {
    std::uintptr_t a = g_realFactory(objId, pos, yaw);
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

}  // namespace

void* WorldCensusTrapReal(int table, int index) {
    for (int k = 0; k < kTrapCount; ++k)
        if (kNames[k].table == table && kNames[k].index == index) {
            if (g_trap[k].real) return reinterpret_cast<void*>(g_trap[k].real);
            std::uintptr_t t = Read<std::uintptr_t>(ExeBase() + kTrapTables + 8 * table);
            return InExe(t) ? reinterpret_cast<void*>(Read<std::uintptr_t>(t + 16 * index)) : nullptr;
        }
    return nullptr;
}

void WorldCensusInit() {
    g_census = EnvInt("KH2COOP_CENSUS", 0) == 1;
    g_trapLog = EnvInt("KH2COOP_TRAP_LOG", 0) == 1;
    bool mirror = WorldMirrorOn();  // the one-brain experiment gets the host's script calls from the thunks
    if (!g_census && !g_trapLog && !mirror) return;
    Log("world census (research, TODO 1.55): census %s, script call log %s%s", g_census ? "on" : "off",
        g_trapLog ? "on" : "off", mirror ? ", script call thunks for the world mirror" : "");
    if (g_trapLog || mirror) InstallTrapLog();
    if (g_census)
        HookFunction(kFactory, kFactoryBytes, sizeof(kFactoryBytes), reinterpret_cast<void*>(HookFactory),
                     reinterpret_cast<void**>(&g_realFactory), "actor factory (census)");
}

void WorldCensusFrame() {
    if (!g_census && !g_trapLog) return;
    ++g_frame;
    CensusFrame();
    if (g_trapLog) LogCounts();
}

}  // namespace kh2coop
