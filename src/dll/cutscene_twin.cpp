// The other player's Sora in story cutscenes (TODO 5.12, user decision 2026-10-09: done in memory,
// only while a partner is connected; a twin that does exactly what Sora does, a step to his side;
// CUTSCENE_TWIN=0 in kh2coop_player.ini turns it off).
//
// A story cutscene is a script in the room file (ARD entry type 0x16, VERIFIED_OFFSETS "Cutscene
// scripts"): a list of commands {u16 size, u16 type, data}, ended by size 0, that makes the scene's own
// actors (SetActor) and drives them (positions, animations, face textures...). The field party and our
// Sora copy aren't part of it. The game starts a cutscene with exe+0x2CC4B0(script, ...), which keeps
// the script pointer for its setup task (exe+0x2CC180, SetActor etc.) and its command walker
// (exe+0x2CB5D0). Our hook hands it a copy of the script with a twin added: one more SetActor for every
// Sora actor (event Sora, close-up Sora) and Sora Keyblade actor, and right after every command that
// names one of them, the same command for its twin. Positions get a side step across the actor's facing.
// Spline moves (SeqSpline) aren't copied: their path can't be moved aside, and the same path would put
// both Soras in one spot.
//
// The scene's actors are created by their first animation command (exe+0x2D2160, SeqPlayAnimation) with
// the actor factory; if that fails for a twin (no memory) or the twin's SetActor didn't make it into the
// scene's actor table, the twin's remaining commands are switched off, so no handler meets a missing actor.
//
// Ghidra 2026-10-09 (session 20). Bench 2026-10-10 (sessions 21-22, TT 02/08 scenes 706/707, two copies): the twin
// is created a side step from Sora and walks and fights beside him. It first stayed where it was created: our
// puppet (puppet.cpp) took it for the Sora copy and held it idle; cutscene actors are now never the copy.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "avatar_link.hpp"
#include "common.hpp"
#include "cutscene_twin.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kStartCutscene = 0x2CC4B0;  // (script, ...) -> u64
constexpr std::uint8_t kStartCutsceneBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                                0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kPlayAnimation = 0x2D2160;  // (command) SeqPlayAnimation; creates the actor on first use
constexpr std::uint8_t kPlayAnimationBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48,
                                                0x8B, 0xF1, 0x0F, 0xB7, 0x49, 0x04};
constexpr std::uintptr_t kFindSceneActor = 0x2CB300;  // (actor id) -> int[10] {object, id, handle, ...} or 0
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;    // (u32 handle) -> object or 0
constexpr std::uintptr_t kObjTable = 0x2A254D0;       // {u32 version, u32 count}, 0x60-byte rows, name @+8

using PFN_StartCutscene = std::uint64_t(__fastcall*)(std::uintptr_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                                     std::uint64_t, std::uint32_t, std::uint32_t);
// Command handlers get (command, task): the walker exe+0x2CB5D0 passes its own task in rdx (`mov rdx, rsi`), and a
// handler that waits for its start frame (exe+0x2C87F0) switches fibers through it (exe+0x150060 -> task+0x70).
using PFN_Command = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_FindSceneActor = int*(__fastcall*)(int);
using PFN_DecodeHandle = std::uintptr_t(__fastcall*)(std::uint32_t);
PFN_StartCutscene g_realStart = nullptr;
PFN_Command g_realPlayAnimation = nullptr;
bool g_sceneLog = false;  // SCENE_LOG: log each cutscene start and its caller

constexpr std::uint16_t kSetActor = 0x01, kActorPosition = 0x02, kSpline = 0x1F, kPosMove = 0x47;
constexpr std::uint16_t kNoOp = 0x03;      // SetMap: neither of the game's two tasks acts on it
// Scene actor table: 0x60 entries (exe+0x2CB300); animation slots: 0x40 (exe+0x2D2160 takes one per animated
// actor and writes through a null slot pointer when none is free). Twins only when every actor fits both.
constexpr int kMaxSceneActors = 0x40;
constexpr std::size_t kMaxScript = 1 << 20;
constexpr float kSideStep = 90.0f;         // how far beside Sora the twin stands
constexpr std::uint32_t kPeerFreshMs = 5000;
constexpr int kMaxTwins = 8;

// Where a command keeps the actor id it drives (offset from the command start, header included);
// OpenKH Event.cs layouts ("PutId" fields are the actor id too). -1: names no actor.
int ActorField(std::uint16_t type) {
    switch (type) {
    case 0x02: return 0x24;  // SeqActorPosition
    case 0x0B: return 0x08;  // SeqKage (shadow)
    case 0x0D: return 0x08;  // SeqPart
    case 0x0E: return 0x0C;  // SeqAlpha
    case 0x19: return 0x06;  // SeqTextureAnim (eyes, mouth)
    case 0x1A: return 0x06;  // SeqActorLeave
    case 0x1C: return 0x08;  // SeqIk
    case 0x1F: return 0x04;  // SeqSpline
    case 0x28: return 0x06;  // Scale
    case 0x29: return 0x06;  // Turn
    case 0x2C: return 0x0E;  // SeqPlayAnimation
    case 0x36: return 0x06;  // Lookat
    case 0x37: return 0x06;  // ShadowAlpha
    case 0x3A: return 0x06;  // SeqMirror
    case 0x47: return 0x04;  // SeqPosMove
    default: return -1;
    }
}

bool g_on = false;
// Our script copies: the current one and the one before (a pointer into the previous script may still be
// used for a moment after the next scene starts); a copy is freed two starts later.
std::vector<std::uint8_t> g_scripts[2];
int g_current = 0;
struct Twin {
    std::int16_t of, id;
};
Twin g_twins[kMaxTwins];
int g_twinCount = 0;
bool g_twinsOff = false;  // switched off for the rest of this scene
std::uint32_t g_scenes = 0;

const char* ObjName(int objId) {
    std::uintptr_t table = ExeBase() + kObjTable;
    std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(table + 4);
    for (std::uint32_t i = 0; i < count && i < 8192; ++i) {
        std::uintptr_t row = table + 8 + i * 0x60;
        if (*reinterpret_cast<const std::uint32_t*>(row) == static_cast<std::uint32_t>(objId))
            return reinterpret_cast<const char*>(row + 8);
    }
    return "";
}

// Sora's cutscene models: ACTOR_SORA / ACTOR_SORA_H in most scenes; Twilight Town's scenes (02/08 events 706,
// 707 and the other 70x/80x) use P_EX120 with the close-up H_EX790 instead (seen in game 2026-10-10), a few use
// P_EX100 / H_EX500 by name. Not Sora, left out on purpose: P_EX110 / H_EX510 (Roxas), P_EX130, the
// P_EX120_NPC_* colour variants, H_EX740/750/760 (Hayner, Pence, Olette), H_EX590.
bool IsSora(const char* name) {
    return std::strncmp(name, "ACTOR_SORA", 10) == 0 || std::strcmp(name, "P_EX120") == 0 ||
           std::strcmp(name, "H_EX790") == 0 || std::strcmp(name, "P_EX100") == 0 || std::strcmp(name, "H_EX500") == 0;
}
bool IsKeyblade(const char* name) {
    return std::strncmp(name, "SORA_WEAPON", 11) == 0 || std::strncmp(name, "W_EX010", 7) == 0;
}

std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
std::int16_t S16(const std::uint8_t* p) {
    std::int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
void PutS16(std::uint8_t* p, std::int16_t v) { std::memcpy(p, &v, 2); }
float F32(const std::uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

int TwinOf(int id) {
    for (int i = 0; i < g_twinCount; ++i)
        if (g_twins[i].of == id) return g_twins[i].id;
    return -1;
}
bool IsTwinId(int id) {
    for (int i = 0; i < g_twinCount; ++i)
        if (g_twins[i].id == id) return true;
    return false;
}

// Moves the position (x, y, z floats at `at`) a side step across the facing `degrees` (rotation about y).
void SideStep(std::uint8_t* cmd, int at, float degrees) {
    float p[3];
    std::memcpy(p, cmd + at, sizeof(p));
    float r = degrees * 3.14159265f / 180.0f;
    p[0] += std::cos(r) * kSideStep;
    p[2] -= std::sin(r) * kSideStep;
    std::memcpy(cmd + at, p, sizeof(p));
}

// Builds the script with the twin. False (why = the reason) if the scene has no Sora or isn't safe to change.
bool BuildTwinScript(const std::uint8_t* src, std::vector<std::uint8_t>& out, const char*& why) {
    // Pass 1: the length, the scene's actors, the Sora and Keyblade actors.
    std::size_t len = 0;
    int maxId = -1, actors = 0;
    bool sora = false;
    g_twinCount = 0;
    for (;;) {
        if (len + 4 > kMaxScript) return why = "no end found", false;
        std::uint16_t size = U16(src + len);
        if (size == 0) break;
        if (size < 4 || (size & 1)) return why = "odd command size", false;
        if (U16(src + len + 2) == kSetActor && size >= 8) {
            int obj = S16(src + len + 4), id = S16(src + len + 6);
            ++actors;
            if (id > maxId) maxId = id;
            const char* name = ObjName(obj);
            if (IsSora(name) || IsKeyblade(name)) {
                if (g_twinCount == kMaxTwins) return why = "too many Sora actors", false;
                sora = sora || IsSora(name);
                g_twins[g_twinCount++] = {static_cast<std::int16_t>(id), 0};
            }
        }
        len += size;
    }
    if (!sora) return why = "no Sora in this scene", false;
    if (actors + g_twinCount > kMaxSceneActors) return why = "the scene's actor table would be full", false;
    for (int i = 0; i < g_twinCount; ++i) g_twins[i].id = static_cast<std::int16_t>(maxId + 1 + i);

    // Pass 2: every command, and after each one that names a Sora/Keyblade actor, its twin's copy.
    out.clear();
    out.reserve(len * 2 + 2);
    std::uint32_t added = 0, splines = 0;
    for (std::size_t at = 0; at < len;) {
        const std::uint8_t* cmd = src + at;
        std::uint16_t size = U16(cmd), type = U16(cmd + 2);
        out.insert(out.end(), cmd, cmd + size);
        int field = type == kSetActor ? 6 : ActorField(type);
        int twin = field >= 0 && field + 2 <= size ? TwinOf(S16(cmd + field)) : -1;
        if (twin >= 0 && type == kSpline) {
            ++splines;
        } else if (twin >= 0) {
            std::size_t c = out.size();
            out.insert(out.end(), cmd, cmd + size);
            std::uint8_t* copy = out.data() + c;
            PutS16(copy + field, static_cast<std::int16_t>(twin));
            if (type == kActorPosition && size >= 0x28) SideStep(copy, 0x08, F32(cmd + 0x18));
            if (type == kPosMove && size >= 0x2C) {
                SideStep(copy, 0x08, F32(cmd + 0x24));  // start
                SideStep(copy, 0x14, F32(cmd + 0x24));  // end
            }
            ++added;
        }
        at += size;
    }
    out.push_back(0);
    out.push_back(0);
    Log("cutscene twin: scene '%.20s': %d Sora/Keyblade actors twinned (ids %d..%d), %u commands added, %u spline "
        "moves not copied", reinterpret_cast<const char*>(src + 10), g_twinCount, g_twins[0].id,
        g_twins[g_twinCount - 1].id, added, splines);
    return true;
}

// Switches off the twins' commands from `from` on (the walker skips type kNoOp).
void TwinsOff(std::uintptr_t from, const char* why) {
    if (g_twinsOff) return;
    g_twinsOff = true;
    std::vector<std::uint8_t>& s = g_scripts[g_current];
    auto base = reinterpret_cast<std::uintptr_t>(s.data());
    std::size_t at = from >= base && from < base + s.size() ? from - base : s.size();
    std::uint32_t n = 0;
    while (at + 4 <= s.size()) {
        std::uint8_t* cmd = s.data() + at;
        std::uint16_t size = U16(cmd);
        if (size == 0) break;
        int field = ActorField(U16(cmd + 2));
        if (field >= 0 && field + 2 <= size && IsTwinId(S16(cmd + field))) {
            PutS16(cmd + 2, static_cast<std::int16_t>(kNoOp));
            ++n;
        }
        at += size;
    }
    Log("cutscene twin: switched off for the rest of this scene (%s); %u commands skipped", why, n);
}

// ---- Research trace (KH2COOP_TWIN_TRACE=1, off by default; sessions 21-22, TODO 5.12: it found why the twin
// didn't move, VERIFIED_OFFSETS 'Cutscene twin froze') ----
// Logs every SeqPlayAnimation / SeqActorPosition on a Sora actor or its twin (position, motion pointer and clock
// before and after the game's handler), and puts hardware write watches (DR0..DR3, armed when the scene's first
// twin is created) on the twin's and Sora's motion pointer (actor+0x170 = motion controller +0x18) and position x
// (actor+0x670), counting each writing instruction with its callers.
constexpr std::uintptr_t kActorPositionCmd = 0x2D0BD0;  // SeqActorPosition (command, task)
constexpr std::uint8_t kActorPositionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x48,
                                                0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x50};
constexpr std::uintptr_t kSceneFrame = 0xB64F98;  // u32? current scene frame (exe+0x2C87F0 waits for it)
bool g_trace = false;
PFN_Command g_realActorPosition = nullptr;
std::uint32_t g_traceFrame = 0, g_traceArmFrame = 0;
std::uintptr_t g_traceAddr[4] = {};
const char* const kTraceName[4] = {"twin motion ptr", "twin pos x", "Sora motion ptr", "Sora pos x"};
const std::uint8_t kTraceLen[4] = {8, 4, 8, 4};
volatile bool g_traceArmed = false;
struct TraceHit {
    std::uint8_t slot;
    std::uint32_t rip, caller[3], count, first, last;
    std::uint64_t value;
};
TraceHit g_traceHits[64];
volatile LONG g_traceHitCount = 0;
std::uint32_t g_traceShown[64] = {};

std::uintptr_t SceneActor(int id) {
    int* row = reinterpret_cast<PFN_FindSceneActor>(ExeBase() + kFindSceneActor)(id);
    return row ? reinterpret_cast<PFN_DecodeHandle>(ExeBase() + kDecodeHandle)(static_cast<std::uint32_t>(row[2])) : 0;
}

LONG CALLBACK OnTraceException(EXCEPTION_POINTERS* info) {
    CONTEXT* c = info->ContextRecord;
    if (!g_traceArmed || info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(c->Dr6 & 0xF))
        return EXCEPTION_CONTINUE_SEARCH;
    int slot = 0;
    while (!(c->Dr6 & (1ull << slot))) ++slot;
    c->Dr6 = 0;
    std::uintptr_t exe = ExeBase();
    auto rva = [exe](std::uintptr_t v) -> std::uint32_t {
        return v > exe && v < exe + 0x579000 ? static_cast<std::uint32_t>(v - exe) : 0;
    };
    std::uint32_t rip = rva(c->Rip), callers[3] = {};
    auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Rsp);
    for (int i = 0, n = 0; i < 64 && n < 3; ++i)
        if (std::uint32_t r = rva(stack[i])) callers[n++] = r;
    std::uint64_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(g_traceAddr[slot]), kTraceLen[slot]);
    LONG count = g_traceHitCount;
    for (LONG i = 0; i < count; ++i) {
        TraceHit& h = g_traceHits[i];
        if (h.slot == slot && h.rip == rip && h.caller[0] == callers[0] && h.caller[1] == callers[1]) {
            ++h.count;
            h.last = g_traceFrame;
            h.value = value;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (count < 64) {
        g_traceHits[count] = {static_cast<std::uint8_t>(slot), rip, {callers[0], callers[1], callers[2]}, 1,
                              g_traceFrame, g_traceFrame, value};
        InterlockedIncrement(&g_traceHitCount);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI TraceArmThread(LPVOID target) {
    HANDLE thread = static_cast<HANDLE>(target);
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) return 1;
    CONTEXT c {};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &c)) {
        DWORD64* dr = &c.Dr0;
        c.Dr7 = 0;
        for (int i = 0; i < 4; ++i) {
            dr[i] = g_traceArmed ? g_traceAddr[i] : 0;
            // Li enable, RWi = 01 (write), LENi = 10 (8 bytes) or 11 (4 bytes).
            if (dr[i])
                c.Dr7 |= (1ull << (2 * i)) | (1ull << (16 + 4 * i)) | ((kTraceLen[i] == 8 ? 2ull : 3ull) << (18 + 4 * i));
        }
        c.Dr6 = 0;
        SetThreadContext(thread, &c);
    }
    ResumeThread(thread);
    return 0;
}

void* g_traceVeh = nullptr;

// Our handler must run first: a watch hit inside a scene handler is dispatched on the walker fiber's small stack,
// and the .NET runtime's handler (clr.dll, added after ours) probes so much stack that the game died with a stack
// overflow at the first hit (bench 2026-10-10). Ours returns at once.
void TraceHandlerFirst() {
    if (g_traceVeh) RemoveVectoredExceptionHandler(g_traceVeh);
    g_traceVeh = AddVectoredExceptionHandler(1, OnTraceException);
}

// Game thread only (the walker's fiber runs on it; debug registers belong to the thread, not the fiber).
void TraceArm(bool on) {
    if (on) TraceHandlerFirst();
    g_traceArmed = on;
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, 0);
    HANDLE helper = CreateThread(nullptr, 0, TraceArmThread, self, 0, nullptr);
    if (helper) {
        WaitForSingleObject(helper, 1000);
        CloseHandle(helper);
    }
    CloseHandle(self);
}

void TraceLogHits(const char* when) {
    LONG count = g_traceHitCount;
    bool header = false;
    for (LONG i = 0; i < count; ++i) {
        const TraceHit& h = g_traceHits[i];
        if (h.count == g_traceShown[i]) continue;
        if (!header) Log("twin trace: writers (%s, frame %u):", when, g_traceFrame);
        header = true;
        Log("  %s after exe+0x%X, from exe+0x%X <- 0x%X <- 0x%X: x%u (frames %u-%u), last value 0x%llX",
            kTraceName[h.slot], h.rip, h.caller[0], h.caller[1], h.caller[2], h.count, h.first, h.last,
            static_cast<unsigned long long>(h.value));
        g_traceShown[i] = h.count;
    }
}

void TraceState(char* out, std::size_t size, std::uintptr_t actor) {
    if (!actor) {
        std::snprintf(out, size, "no actor");
        return;
    }
    __try {
        const float* pos = reinterpret_cast<const float*>(actor + 0x670);
        std::snprintf(out, size, "pos (%.0f, %.0f, %.0f) motion 0x%llX clock %.1f", pos[0], pos[1], pos[2],
                      static_cast<unsigned long long>(*reinterpret_cast<const std::uint64_t*>(actor + 0x170)),
                      *reinterpret_cast<const float*>(actor + 0x19C));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, size, "unreadable");
    }
}

bool IsTraced(int id) { return g_trace && g_twinCount && (TwinOf(id) >= 0 || IsTwinId(id)); }

// Runs the handler and logs the actor before (when the walker reaches the command, maybe before its frame)
// and after (when the handler is done, at or after its frame).
void TracedCall(PFN_Command real, const char* what, std::uintptr_t cmd, std::uintptr_t task, int id, int frame) {
    char before[128], after[128];
    std::uint32_t f0 = *reinterpret_cast<const std::uint32_t*>(ExeBase() + kSceneFrame);
    std::uintptr_t a0 = SceneActor(id);
    TraceState(before, sizeof(before), a0);
    real(cmd, task);
    std::uint32_t f1 = *reinterpret_cast<const std::uint32_t*>(ExeBase() + kSceneFrame);
    std::uintptr_t a1 = SceneActor(id);
    TraceState(after, sizeof(after), a1);
    Log("twin trace: %s id %d frame %d (scene frame %u -> %u): actor 0x%llX %s -> actor 0x%llX %s", what, id, frame,
        f0, f1, static_cast<unsigned long long>(a0), before, static_cast<unsigned long long>(a1), after);
}

void __fastcall HookActorPosition(std::uintptr_t cmd, std::uintptr_t task) {
    int id = *reinterpret_cast<const std::int16_t*>(cmd + 0x24);
    if (!IsTraced(id)) {
        g_realActorPosition(cmd, task);
        return;
    }
    TracedCall(g_realActorPosition, "position", cmd, task, id, *reinterpret_cast<const std::int16_t*>(cmd + 0x26));
}

std::uint64_t __fastcall HookStart(std::uintptr_t script, std::uint64_t a2, std::uint64_t a3, std::uint64_t a4,
                                   std::uint64_t a5, std::uint32_t a6, std::uint32_t a7) {
    if (g_traceArmed) {
        TraceArm(false);
        TraceLogHits("next scene, watch off");
    }
    if (g_sceneLog && script) {  // research log (TODO 5.16): which scene starts, from where
        char chain[200];
        CallerChain(chain, sizeof(chain), 0);
        auto now = reinterpret_cast<const std::uint8_t*>(ExeBase() + 0x717008);
        Log("scene log: cutscene '%.20s' starts in 0x%02X/0x%02X programs %u/%u/%u; from %s",
            reinterpret_cast<const char*>(script + 10), now[0], now[1], *reinterpret_cast<const std::uint16_t*>(now + 4),
            *reinterpret_cast<const std::uint16_t*>(now + 6), *reinterpret_cast<const std::uint16_t*>(now + 8), chain);
    }
    std::uintptr_t use = script;
    __try {
        std::uint32_t age = AvatarLinkPeerAgeMs();
        if (script && age < kPeerFreshMs) {
            int next = g_current ^ 1;
            const char* why = nullptr;
            if (BuildTwinScript(reinterpret_cast<const std::uint8_t*>(script), g_scripts[next], why)) {
                g_current = next;
                g_twinsOff = false;
                use = reinterpret_cast<std::uintptr_t>(g_scripts[next].data());
                ++g_scenes;
            } else {
                g_twinCount = 0;
                Log("cutscene twin: scene '%.20s' left as is (%s)", reinterpret_cast<const char*>(script + 10), why);
            }
        } else {
            g_twinCount = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_twinCount = 0;
        use = script;
        Log("cutscene twin: exception while reading the scene's script; left as is");
    }
    return g_realStart(use, a2, a3, a4, a5, a6, a7);
}

// SeqPlayAnimation for a twin: its first one creates the twin. Check the twin is in the scene's actor
// table first, and catch a failed creation (the game would read the missing actor).
void __fastcall HookPlayAnimation(std::uintptr_t cmd, std::uintptr_t task) {
    int id = -1;
    __try {
        id = g_twinCount ? *reinterpret_cast<const std::int16_t*>(cmd + 0x0E) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        id = -1;
    }
    if (id < 0 || !IsTwinId(id)) {
        if (id >= 0 && IsTraced(id))
            TracedCall(g_realPlayAnimation, "play", cmd, task, id, *reinterpret_cast<const std::int16_t*>(cmd + 4));
        else
            g_realPlayAnimation(cmd, task);
        return;
    }
    if (g_twinsOff) return;
    if (!reinterpret_cast<PFN_FindSceneActor>(ExeBase() + kFindSceneActor)(id)) {
        TwinsOff(cmd, "the twin isn't in the scene's actor table");
        return;
    }
    bool existed = SceneActor(id) != 0;
    __try {
        if (g_trace)
            TracedCall(g_realPlayAnimation, "play", cmd, task, id, *reinterpret_cast<const std::int16_t*>(cmd + 4));
        else
            g_realPlayAnimation(cmd, task);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        TwinsOff(cmd, "creating the twin failed");
        return;
    }
    int* entry = reinterpret_cast<PFN_FindSceneActor>(ExeBase() + kFindSceneActor)(id);
    if (!entry || !reinterpret_cast<PFN_DecodeHandle>(ExeBase() + kDecodeHandle)(static_cast<std::uint32_t>(entry[2]))) {
        TwinsOff(cmd, "the twin wasn't created");
        return;
    }
    if (g_trace && !existed && !g_traceArmed) {
        for (int i = 0; i < g_twinCount; ++i) {
            if (g_twins[i].id != id) continue;
            std::uintptr_t twin = SceneActor(id), sora = SceneActor(g_twins[i].of);
            if (!twin || !sora) break;
            g_traceAddr[0] = twin + 0x170;
            g_traceAddr[1] = twin + 0x670;
            g_traceAddr[2] = sora + 0x170;
            g_traceAddr[3] = sora + 0x670;
            g_traceArmFrame = g_traceFrame;
            TraceArm(true);
            Log("twin trace: write watch armed on twin id %d (actor 0x%llX) and its original id %d (actor 0x%llX)", id,
                static_cast<unsigned long long>(twin), g_twins[i].of, static_cast<unsigned long long>(sora));
            break;
        }
    }
}

}  // namespace

void CutsceneTwinInit() {
    if (EnvInt("KH2COOP_CUTSCENE_TWIN", 1) != 1) {
        Log("cutscene twin: off (CUTSCENE_TWIN=0 in kh2coop_player.ini)");
        return;
    }
    // The guard first: without it the script change isn't installed either.
    g_on = HookFunction(kPlayAnimation, kPlayAnimationBytes, sizeof(kPlayAnimationBytes),
                        reinterpret_cast<void*>(HookPlayAnimation), reinterpret_cast<void**>(&g_realPlayAnimation),
                        "CutscenePlayAnimation") &&
           HookFunction(kStartCutscene, kStartCutsceneBytes, sizeof(kStartCutsceneBytes),
                        reinterpret_cast<void*>(HookStart), reinterpret_cast<void**>(&g_realStart), "StartCutscene");
    g_sceneLog = g_on && EnvInt("KH2COOP_SCENE_LOG", 1) == 1;
    Log("cutscene twin: %s", g_on ? "on (the other player's Sora shows in story cutscenes while connected)"
                                  : "OFF (hooks failed)");
    if (g_on && EnvInt("KH2COOP_TWIN_TRACE", 0) == 1) {
        g_trace = HookFunction(kActorPositionCmd, kActorPositionBytes, sizeof(kActorPositionBytes),
                               reinterpret_cast<void*>(HookActorPosition),
                               reinterpret_cast<void**>(&g_realActorPosition), "CutsceneActorPosition");
        Log("twin trace: %s", g_trace ? "on (research logging, KH2COOP_TWIN_TRACE=1)" : "OFF (hook failed)");
    }
}

void CutsceneTwinFrame() {
    if (!g_trace) return;
    ++g_traceFrame;
    if (g_traceArmed && g_traceFrame - g_traceArmFrame >= 60 * 90) {  // a scene's worth; never leave it on
        TraceArm(false);
        TraceLogHits("90 s, watch off");
        return;
    }
    if (g_traceArmed && g_traceFrame % 30 == 0) TraceHandlerFirst();  // the crash logger re-adds itself first
    if (g_traceFrame % 60 == 0) TraceLogHits(g_traceArmed ? "watch on" : "watch off");
}

}  // namespace kh2coop
