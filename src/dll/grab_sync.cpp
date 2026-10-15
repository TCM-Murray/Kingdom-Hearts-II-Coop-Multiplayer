// An enemy's grab of the other player's Sora copy shows on that player's own Sora (TODO 1.62; you, 2026-10-14:
// "When Roxas does the attack on the Host' Sora clone, the animation is done on the real Sora, although the
// reaction command is done on the Client's game"; your pick: the grab on the friend's Sora with the close-up camera
// and flash on the friend's screen, the host's camera stays on the host's Sora).
//
// With one brain only the host's enemies run their scripts. A grab is a few script calls on the grabbed Sora
// (VERIFIED_OFFSETS 'Data Roxas's steal with one brain'): trap_obj_idle, trap_obj_motion_capture (the grabber's
// motion on the Sora), trap_obj_reset_pos / set_dir, trap_obj_hook (a script thread for the Sora), trap_obj_attach,
// trap_obj_camera_start (an in-fight scene with its own camera), func_screen_whiteout / whitein. In the host's game
// the grabbed one is the friend's copy, so the scene took the host's camera and the friend's Sora stayed free.
// The scene camera's target is the copy (Data Roxas's steal) or the grabber itself (the Twin Lords' reaction commands
// 505 / 506, session 36: the friend's Sora was grabbed but the camera moved in the host's game only); both count while
// the grabber holds the copy, and so does that grabber's trap_camera_reset that ends its scene camera.
// Not tied to one boss: any enemy or field object script that makes these calls on the copy.
//   Host:   these calls on the copy run as before (the script reads the copy's state back), and are sent to the
//           friend ("KH2B"), with the grabber as world sync's number. The camera calls and the screen flashes while
//           the copy is grabbed don't run in the host's game: they are the friend's.
//   Friend: the same game functions run on our own Sora, with our copy of the grabber (bound to the host's): the
//           grabber's motion (exe+0x3C6CC0 with its motion file, as trap_obj_motion_capture does), place, facing,
//           attach, the scene camera on our Sora, the flash. trap_obj_hook is not sent: its thread is the grabber's
//           script acting for the Sora (in Data Roxas's steal it makes the Sora hit him), and the host's copy already
//           does that, so it would happen twice.
//           Our motion mirror doesn't send these motions back (MotionMirrorMute): the host's copy played our grab motion
//           as a second grab, which ended the grab there after 0.1 s (bench 2026-10-14 01:25:14).
//   Host:   when the copy is free again (no hook, no grab motion, not attached) a 'let go' is sent; the friend's Sora
//           is detached if still attached, and so is our grabber if the attach left it on our Sora (the attach puts
//           its first actor on the second: Data Roxas on Sora; the host's script detaches its own grabber, which isn't
//           a call on the copy, so it isn't sent). The grab motion frees our Sora by itself when it ends (3.3 s for Data
//           Roxas's steal, the reaction command's window: bench 2026-10-14 01:25:53-56.6, as the host's copy).
//           Not after Data Roxas's steal (the friend used 'The End', 672): its grab motion 254 doesn't end by itself
//           (two-PC runs 2026-10-14, with and without a Drive Form: the friend's Sora stayed in it until downed). In
//           the host's game nothing in the game ends it on the copy either: the script lets go (+0xD48 cleared) and
//           our puppet sets the copy's motion again, which ends the grab motion. So half a second after a let go our
//           Sora, if still in the grab motion, is set idle as trap_obj_idle does and its motion set to idle with the
//           puppet's call (EndStuckGrab).
// GRAB_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "grab_sync.hpp"
#include "motion_mirror.hpp"
#include "puppet.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4232484B;  // "KH2B"
constexpr std::uint16_t kVersion = 2;  // v2: camera b = 2, camera reset
constexpr std::uintptr_t kListHead = 0x2A171C8;  // our Sora
constexpr std::uintptr_t kHandleBases = 0x2B0D720;
constexpr std::uintptr_t kTeam = 0x4DC, kObjRow = 0x918;
constexpr std::uintptr_t kHook = 0xD48, kFlags18C = 0x18C, kAttachedTo = 0x6A0;  // +0x18C bit 14: grab motion
constexpr std::uint32_t kCaptured = 1u << 14;
// The game functions the script calls use (VERIFIED_OFFSETS lead 'Grab calls').
constexpr std::uintptr_t kStopAction = 0x3BFB70;    // (actor)          trap_obj_idle, first part
constexpr std::uintptr_t kSetAction = 0x3B4DA0;     // (actor, action)  trap_obj_idle(actor) = action 0
constexpr std::uintptr_t kCapture = 0x3C6CC0;       // (grabber + 0x158, actor, motion, float blend) -> int
constexpr std::uintptr_t kSetPos = 0x3B6100;        // (actor, float[4])
constexpr std::uintptr_t kSetDir = 0x3B6420;        // (actor, float[4])
constexpr std::uintptr_t kAttach = 0x3D9FE0;        // (actor, parent, a, b): actor + 0x6A0 = the parent's handle
constexpr std::uintptr_t kDetach = 0x3DA250;        // (actor)
constexpr std::uintptr_t kMoveToSpace = 0x3B9AC0;   // (actor, float, flag): the script gives a float and a flag (low 32 bits)
constexpr std::uintptr_t kCameraStart = 0x3DB2F0;   // (owner, 4-letter name, target or 0)
// trap_camera_reset (1/295, no arguments) is run through the game's own trap: exe+0x430B60 = exe+0x165A40(the camera).
constexpr std::uint64_t kQuietFrames = 30;          // host: the copy free this long = let go
constexpr std::uint64_t kMaxFrames = 60 * 30;       // host: a grab longer than this is let go anyway

enum Op : std::uint8_t {
    kOpIdle = 1, kOpCapture, kOpPos, kOpDir, kOpAttach, kOpDetach, kOpMoveToSpace, kOpCamera, kOpScreen, kOpRelease,
    kOpCameraReset
};
const char* const kOpNames[] = {"?", "idle", "grab motion", "place", "facing", "attach", "detach", "move to space",
                                "scene camera", "screen", "let go", "camera reset"};

#pragma pack(push, 1)
struct Event {
    std::uint32_t id;
    std::uint8_t op;
    std::uint16_t netId, instance;  // the grabber (world sync's number on the host), 0 if none
    std::int32_t a, b;              // capture: motion, blend (float bits); attach: its two numbers; move to space:
                                    // float bits, flag;
                                    // camera: b = 1 with the copy as target, 2 with the grabber, 0 none;
                                    // screen: a = the call (31 / 32)
    float v[4];                     // place, facing
    char name[4];                   // camera
    std::uint64_t raw;              // screen: the script's argument
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kPerPacket = 16;

using PFN_Actor = void(__fastcall*)(std::uintptr_t);
using PFN_SetAction = void(__fastcall*)(std::uintptr_t, std::int32_t);
using PFN_Capture = std::int32_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::int32_t, float);
using PFN_Vec = void(__fastcall*)(std::uintptr_t, float*);
using PFN_Attach = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::int32_t, std::int32_t);
using PFN_MoveToSpace = void(__fastcall*)(std::uintptr_t, float, bool);
using PFN_Camera = void(__fastcall*)(std::uintptr_t, const char*, std::uintptr_t);
using PFN_Trap = void(__fastcall*)(std::uint64_t* args);

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;

// Host.
struct Queued {
    Event e;
    int sent;
};
Queued g_queue[32];
int g_queueNext = 0;
std::uint32_t g_nextId = 1;
bool g_grab = false;           // the copy is grabbed (since its first grab call)
std::uintptr_t g_grabber = 0;
std::uint64_t g_grabFrame = 0, g_lastOpFrame = 0, g_freeSince = 0;
std::uint32_t g_sent = 0, g_heldBack = 0, g_grabs = 0;
std::uintptr_t g_cameraBy = 0;  // the grabber whose scene camera went to the friend, until its camera reset

// Friend.
Event g_inbox[32];
int g_inboxCount = 0;
std::uint32_t g_lastId = 0;
std::uint32_t g_run = 0, g_skipped = 0;
std::uint64_t g_checkFrame = 0;  // our Sora's state is checked a moment after a let go (EndStuckGrab)
std::uint64_t g_releaseFrame = 0, g_captureFrame = 0;  // the last let go, the last grab motion run on our Sora
int g_freeStep = 0;  // what EndStuckGrab did after the last let go: 0 nothing, 1 set idle
struct AttachedGrabber {  // our grabber the host's attach put on our Sora (world sync's number on the host)
    std::uint16_t netId, instance;
};
AttachedGrabber g_attached[4] = {};

std::uint32_t g_logs = 0, g_room = 0;
std::uint64_t g_statsFrame = 0;

// Who each enemy last grabbed with a hook (host; you, 2026-10-14: Data Roxas's extra Keyblades "stay with the player he
// stole from"): the objects it made follow that player (aggro.cpp) instead of its current target. Only grabs where the
// grabbed Sora is hooked (+0xD48) count, so the steal does and Roxas's motion-256 throw (no hook) doesn't. Cleared at
// each room change.
struct Victim {
    std::uintptr_t grabber;
    bool copy;
};
Victim g_victims[8];
int g_victimNext = 0;
std::uint32_t g_victimRoom = 0;

// Research (KH2COOP_GRAB_TRACE=1): you, 2026-10-14: "if the Host isn't looking at Roxas while it does the attack on
// the Host' Sora clone, Roxas' animation glitches. Only if I aim the camera at there it recovers". Every 6 frames of
// a grab the grabber's and the copy's motion, its clock (+0x19C, what trap_obj_motion_get_time reads), the motion
// triggers (+0x2AD, trap_obj_motion_check_trigger) and where each is drawn (world matrix +0x40 row 3) next to its
// place (+0x640 +0x30): does the game stop moving them on while the host's camera looks away?
bool g_trace = false;
// Test (KH2COOP_GRAB_HOLD_CAMERA=0): the grab's scene camera and flashes also run in the host's game.
bool g_holdCamera = true;
std::uint32_t g_traceLines = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uintptr_t Decode(std::uint32_t h) {
    if (!(h & 0x80000000u)) return 0;
    std::uintptr_t base = Read<std::uintptr_t>(ExeBase() + kHandleBases + 8 * ((h >> 25) & 0x3F));
    if (base == 0 || base == ~std::uintptr_t(0)) return 0;
    return base | (h & 0x1FFFFFF);
}

// A script's actor argument as these calls read it: a handle to an object whose +4 is the actor's handle.
std::uintptr_t ActorArg(std::uint64_t arg) {
    __try {
        std::uintptr_t x = Decode(static_cast<std::uint32_t>(arg));
        return x ? Decode(Read<std::uint32_t>(x + 4)) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = a ? Read<std::uintptr_t>(a + kObjRow) : 0;
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

bool Logs() {
    std::uint32_t room = Read<std::uint16_t>(ExeBase() + 0x717008) | 0x10000;
    if (room != g_room) {
        g_room = room;
        g_logs = 0;
    }
    return g_logs++ < 80;
}

bool StillGrabbed(std::uintptr_t a) {
    __try {
        return Read<std::uintptr_t>(a + kHook) || (Read<std::uint32_t>(a + kFlags18C) & kCaptured) ||
               Decode(Read<std::uint32_t>(a + kAttachedTo));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Queue(Event e) {
    e.id = g_nextId++;
    g_queue[g_queueNext++ % 32] = {e, 0};
    ++g_sent;
    g_lastOpFrame = g_frame;
}

void TraceOne(char* out, std::size_t size, std::uintptr_t a) {
    const float* m = reinterpret_cast<const float*>(a + 0x40);
    const float* p = reinterpret_cast<const float*>(a + 0x640 + 0x30);
    std::snprintf(out, size, "motion %d clock %.1f triggers 0x%02X drawn (%.0f, %.0f, %.0f) place (%.0f, %.0f, %.0f)",
                  Read<std::int32_t>(a + 0x180), Read<float>(a + 0x19C), Read<std::uint8_t>(a + 0x2AD), m[12], m[13],
                  m[14], p[0], p[1], p[2]);
}

void Trace(std::uintptr_t copy) {
    if (!g_trace || (g_frame - g_grabFrame) % 6 || g_traceLines >= 600) return;
    char by[160] = "-", us[160] = "-";
    __try {
        if (g_grabber) TraceOne(by, sizeof(by), g_grabber);
        if (copy) TraceOne(us, sizeof(us), copy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    ++g_traceLines;
    Log("grab trace: frame %llu of grab #%u | %s: %s | copy: %s", static_cast<unsigned long long>(g_frame - g_grabFrame),
        g_grabs, Model(g_grabber), by, us);
}

void SendQueued() {
    char buf[sizeof(Packet) + kPerPacket * sizeof(Event)];
    int n = 0;
    for (int i = 0; i < 32 && n < kPerPacket; ++i) {  // oldest first, each 3 times
        Queued& q = g_queue[(g_queueNext + i) % 32];
        if (!q.e.id || q.sent >= 3) continue;
        std::memcpy(buf + sizeof(Packet) + n * sizeof(Event), &q.e, sizeof(Event));
        ++q.sent;
        ++n;
    }
    if (!n) return;
    Packet p{kMagic, kVersion, AvatarLinkBuild(), static_cast<std::uint8_t>(n)};
    std::memcpy(buf, &p, sizeof(p));
    AvatarLinkSendRaw(buf, static_cast<int>(sizeof(Packet) + n * sizeof(Event)));
}

bool ReadVec(std::uint64_t arg, float* v) {
    __try {
        std::uintptr_t p = Decode(static_cast<std::uint32_t>(arg));
        if (!p) return false;
        std::memcpy(v, reinterpret_cast<const void*>(p), 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadName(std::uint64_t arg, char* name) {
    __try {
        std::uintptr_t p = Decode(static_cast<std::uint32_t>(arg));
        if (!p) return false;
        for (int i = 0; i < 4; ++i) {
            name[i] = Read<char>(p + i);
            if (!name[i]) break;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void StartGrab(std::uintptr_t by) {
    if (g_grab) return;
    g_grab = true;
    g_grabber = by;
    g_grabFrame = g_frame;
    g_freeSince = 0;
    ++g_grabs;
    if (Logs())
        Log("grab sync: %s grabs the friend's Sora copy (#%u): the grab goes to the friend's game, its camera and "
            "flashes stay out of ours", Model(by), g_grabs);
    AggroBossActedOn(by, true, "grabbed");
}

void ClearVictimsOnRoomChange() {
    std::uint32_t room = Read<std::uint16_t>(ExeBase() + 0x717008) | 0x10000;
    if (room == g_victimRoom) return;
    g_victimRoom = room;
    std::memset(g_victims, 0, sizeof(g_victims));
}

// trap_obj_motion_capture(grabber, actor, ...) on our Sora or the copy while that one is hooked, or
// trap_obj_hook(actor, ...) by the grabber's script on one of them (hooking: true, the hook isn't set yet).
void NoteVictim(std::uintptr_t grabber, std::uintptr_t actor, bool hooking = false) {
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead), copy = PuppetCloneActor();
    if (!grabber || !actor || (actor != sora && actor != copy)) return;
    __try {
        if (Read<std::uint32_t>(grabber + kTeam) != 2 || (!hooking && !Read<std::uintptr_t>(actor + kHook))) return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    ClearVictimsOnRoomChange();
    bool onCopy = actor == copy;
    Victim* v = nullptr;
    for (Victim& x : g_victims)
        if (x.grabber == grabber) v = &x;
    if (v && v->copy == onCopy) return;
    if (!v) v = &g_victims[g_victimNext++ % 8];
    *v = {grabber, onCopy};
    Log("grab sync: %s hooked %s: the objects it made follow that player now", Model(grabber),
        onCopy ? "the friend's copy" : "our Sora");
}

// Friend, half a second after a let go (and again after the step): our Sora still in the grab motion (+0x18C bit 14,
// what trap_obj_motion_is_capture reads) though the host's copy is free, with no new grab since, is freed with the
// game's own calls: idle (trap_obj_idle: exe+0x3BFB70, exe+0x3B4DA0(actor, 0)) and the idle motion (the motion set
// the puppet drives the copy with; it ended the copy's grab motion in the session 34 bench, VERIFIED_OFFSETS 'Data
// Roxas's steal with one brain').
void EndStuckGrab() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kListHead);
    if (!sora) return;
    bool captured = (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) != 0;
    int form = PuppetDriveForm();
    const char* when = g_freeStep == 1 ? "half a second after set idle" : "half a second after the let go";
    Log("grab sync: our Sora %s: grab motion %d, attached %d, hook %d, motion %d, motion file %s, Drive Form %d", when,
        captured ? 1 : 0, Decode(Read<std::uint32_t>(sora + kAttachedTo)) ? 1 : 0, Read<std::uintptr_t>(sora + kHook) ? 1 : 0,
        Read<std::int32_t>(sora + 0x180), Read<std::uintptr_t>(sora + 0x2B8) ? "a grabber's" : "its own", form);
    if (!captured || g_captureFrame > g_releaseFrame) {  // free, or grabbed again since
        g_freeStep = 0;
        return;
    }
    if (g_freeStep == 0) {
        reinterpret_cast<PFN_Actor>(exe + kStopAction)(sora);
        reinterpret_cast<PFN_SetAction>(exe + kSetAction)(sora, 0);
        PuppetSetMotion(sora, 0);
        g_freeStep = 1;
        g_checkFrame = g_frame + 30;
        Log("grab sync: our Sora is still in the grab motion though the host's copy is free: set idle (as trap_obj_idle) "
            "and its idle motion");
    } else {
        Log("grab sync: our Sora is STILL in the grab motion; nothing more to try");
        g_freeStep = 0;
    }
}

// Friend: run one of the host's grab calls on our own Sora.
void Apply(const Event& e) {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kListHead);
    std::uintptr_t by = e.netId ? WorldSyncBoundActor(e.netId, e.instance) : 0;
    bool needsBy = e.op == kOpCapture || e.op == kOpAttach || e.op == kOpCamera;
    const char* op = e.op < sizeof(kOpNames) / sizeof(kOpNames[0]) ? kOpNames[e.op] : "?";
    if (!sora || (needsBy && (!by || !WorldSyncSkipsBrain(by)))) {
        ++g_skipped;
        if (Logs())
            Log("grab sync: the host's %s (host#%u) not run: %s", op, e.netId,
                !sora ? "no Sora of ours" : "no enemy of ours bound to the grabber");
        return;
    }
    MotionMirrorMute(true);
    __try {
        switch (e.op) {
            case kOpIdle:
                reinterpret_cast<PFN_Actor>(exe + kStopAction)(sora);
                reinterpret_cast<PFN_SetAction>(exe + kSetAction)(sora, 0);
                break;
            case kOpCapture: {
                float blend;  // the script's float, sent as its bits
                std::memcpy(&blend, &e.b, sizeof(blend));
                reinterpret_cast<PFN_Capture>(exe + kCapture)(by + 0x158, sora, e.a, blend);
                g_captureFrame = g_frame;
                break;
            }
            case kOpPos: {
                float v[4];
                std::memcpy(v, e.v, sizeof(v));
                reinterpret_cast<PFN_Vec>(exe + kSetPos)(sora, v);
                break;
            }
            case kOpDir: {
                float v[4];
                std::memcpy(v, e.v, sizeof(v));
                reinterpret_cast<PFN_Vec>(exe + kSetDir)(sora, v);
                break;
            }
            case kOpAttach: {
                reinterpret_cast<PFN_Attach>(exe + kAttach)(by, sora, e.a, e.b);
                AttachedGrabber* slot = nullptr;  // its own entry, else a free one, else the first
                for (AttachedGrabber& g : g_attached)
                    if (!slot && g.netId == e.netId && g.instance == e.instance) slot = &g;
                for (AttachedGrabber& g : g_attached)
                    if (!slot && !g.netId) slot = &g;
                *(slot ? slot : &g_attached[0]) = {e.netId, e.instance};
                break;
            }
            case kOpDetach:
            case kOpRelease:
                if (Decode(Read<std::uint32_t>(sora + kAttachedTo))) reinterpret_cast<PFN_Actor>(exe + kDetach)(sora);
                if (e.op == kOpRelease) {
                    g_checkFrame = g_frame + 30;
                    g_releaseFrame = g_frame;
                    g_freeStep = 0;
                    // A grabber the attach left on our Sora: off it, or world sync leaves it there (it doesn't move a
                    // copy attached here while the host's is attached too, and draws this one from our Sora's bone).
                    for (AttachedGrabber& g : g_attached) {
                        std::uintptr_t grabber = g.netId ? WorldSyncBoundActor(g.netId, g.instance) : 0;
                        if (grabber && Decode(Read<std::uint32_t>(grabber + kAttachedTo)) == sora) {
                            reinterpret_cast<PFN_Actor>(exe + kDetach)(grabber);
                            if (Logs()) Log("grab sync: our %s was still on our Sora at the let go: detached", Model(grabber));
                        }
                        g = {};
                    }
                }
                break;
            case kOpMoveToSpace: {
                float f;
                std::memcpy(&f, &e.a, sizeof(f));
                reinterpret_cast<PFN_MoveToSpace>(exe + kMoveToSpace)(sora, f, e.b != 0);
                break;
            }
            case kOpCamera: {
                char name[8] = {};
                std::memcpy(name, e.name, 4);
                reinterpret_cast<PFN_Camera>(exe + kCameraStart)(by, name, e.b == 1 ? sora : e.b == 2 ? by : 0);
                break;
            }
            case kOpCameraReset: {
                auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, 295));
                std::uint64_t args[16] = {};
                if (fn) fn(args);
                break;
            }
            case kOpScreen: {
                auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(0, e.a));
                std::uint64_t args[16] = {e.raw};
                if (fn) fn(args);
                break;
            }
            default:
                MotionMirrorMute(false);
                return;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MotionMirrorMute(false);
        ++g_skipped;
        Log("grab sync: the host's %s on our Sora failed (exception)", op);
        return;
    }
    MotionMirrorMute(false);
    ++g_run;
    if (Logs()) {
        if (e.op == kOpMoveToSpace) {
            float f;
            std::memcpy(&f, &e.a, sizeof(f));
            Log("grab sync: ran the host's %s on our Sora (%.0f, %d)", op, f, e.b);
        } else {
            Log("grab sync: ran the host's %s on our Sora%s%s (%d, %d)", op, by ? " with our " : "", by ? Model(by) : "",
                e.a, e.b);
        }
    }
}

}  // namespace

void GrabSyncInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_GRAB_SYNC", 1) == 1;
    g_trace = g_on && host && EnvInt("KH2COOP_GRAB_TRACE", 0) == 1;
    g_holdCamera = EnvInt("KH2COOP_GRAB_HOLD_CAMERA", 1) == 1;
    if (g_on)
        Log("grab sync: on (%s)", host ? "an enemy's grab of the friend's copy goes to the friend's game"
                                       : "the host's enemies' grabs of our copy play on our Sora");
}

bool GrabSyncHolds(int table, int index, const std::uint64_t* args) {
    if (!g_on || !g_host || !CoopActive()) return false;
    if (table == 0 && (index == 31 || index == 32)) {  // func_screen_whiteout / whitein
        if (!g_grab) return false;
        Event e{};
        e.op = kOpScreen;
        e.a = index;
        e.raw = args[0];
        Queue(e);
        if (!g_holdCamera) return false;
        ++g_heldBack;
        return true;
    }
    if (table != 1) return false;
    if (index == 295) {  // trap_camera_reset by the grabber whose scene camera went to the friend
        if (!g_cameraBy || WorldCensusScriptActor() != g_cameraBy) return false;
        Event e{};
        std::uint16_t instance = 0;
        e.netId = WorldSyncNetIdOf(g_cameraBy, &instance);
        e.instance = instance;
        e.op = kOpCameraReset;
        Queue(e);
        if (Logs())
            Log("grab sync: %s's camera reset sent to the friend%s", Model(g_cameraBy), g_holdCamera ? " (not run in our game)" : "");
        g_cameraBy = 0;
        if (!g_holdCamera) return false;
        ++g_heldBack;
        return true;
    }
    if (index == 29) NoteVictim(ActorArg(args[0]), ActorArg(args[1]));
    if (index == 112) NoteVictim(WorldCensusScriptActor(), ActorArg(args[0]), true);  // trap_obj_hook(actor, pc, owner)
    int target;  // the argument that is the grabbed actor
    switch (index) {
        case 74: case 153: case 79: case 102: case 220: target = 0; break;  // idle, reset_pos, set_dir, detach,
                                                                           // move_to_space
        case 29: case 18: target = 1; break;          // motion_capture, attach (grabber first)
        case 157: case 219: target = 2; break;        // camera_start (owner, name, target)
        case 164: target = -1; break;                 // camera_start_global (owner, name)
        default: return false;
    }
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return false;
    std::uintptr_t by = WorldCensusScriptActor();
    std::uint32_t team = 0;
    __try {
        team = by ? Read<std::uint32_t>(by + kTeam) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (team != 2 && team != 3) return false;  // only enemies' and field objects' scripts
    // A scene camera on the grabber itself (or on nothing) counts while that grabber holds the copy.
    bool ownCamera = target == 2 && g_grab && by == g_grabber && ActorArg(args[0]) == by &&
                     (ActorArg(args[2]) == by || !ActorArg(args[2]));
    if (!ownCamera && (target < 0 ? !g_grab : ActorArg(args[target]) != copy)) return false;
    Event e{};
    bool hold = false;
    std::uintptr_t grabber = target >= 1 || index == 164 ? ActorArg(args[0]) : by;
    std::uint16_t instance = 0;
    e.netId = WorldSyncNetIdOf(grabber, &instance);
    e.instance = instance;
    switch (index) {
        case 74:
            e.op = kOpIdle;
            break;
        case 29:  // (grabber, actor, motion, blend)
        case 18:  // (grabber, actor, a, b): the grabber goes on the actor
            e.op = index == 29 ? kOpCapture : kOpAttach;
            e.a = static_cast<std::int32_t>(args[2]);
            e.b = static_cast<std::int32_t>(args[3]);
            break;
        case 153:
        case 79:
            e.op = index == 153 ? kOpPos : kOpDir;
            if (!ReadVec(args[1], e.v)) return false;
            break;
        case 102:
            e.op = kOpDetach;
            break;
        case 220:  // (actor, float, flag): a = the float's bits; the game reads only the flag's low 32 bits
            e.op = kOpMoveToSpace;
            e.a = static_cast<std::int32_t>(args[1]);
            e.b = static_cast<std::uint32_t>(args[2]) != 0 ? 1 : 0;
            break;
        default:  // the cameras
            e.op = kOpCamera;
            e.b = target != 2 ? 0 : ActorArg(args[2]) == copy ? 1 : ActorArg(args[2]) ? 2 : 0;
            if (!ReadName(args[1], e.name)) return false;
            hold = g_holdCamera;
            g_cameraBy = grabber;
            break;
    }
    if (e.op != kOpDetach) StartGrab(grabber);
    Queue(e);
    if (hold) ++g_heldBack;
    if (Logs())
        Log("grab sync: %s's %s on the copy sent to the friend%s", Model(grabber), kOpNames[e.op],
            hold ? " (not run in our game)" : "");
    return hold;
}

int GrabSyncLastHooked(std::uintptr_t grabber) {
    if (!g_on || !g_host || !grabber) return 0;
    __try {
        ClearVictimsOnRoomChange();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    for (const Victim& v : g_victims)
        if (v.grabber == grabber) return v.copy ? 2 : 1;
    return 0;
}

bool GrabSyncHoldsPlayer(std::uintptr_t enemy) {
    if (!g_on || !g_host || !enemy) return false;
    __try {
        ClearVictimsOnRoomChange();
        for (const Victim& v : g_victims) {
            if (v.grabber != enemy) continue;
            std::uintptr_t player = v.copy ? PuppetCloneActor() : Read<std::uintptr_t>(ExeBase() + kListHead);
            return player && Read<std::uintptr_t>(player + kHook) != 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

bool GrabSyncCopyHeld() {
    if (!g_on || !g_host || !g_grab) return false;
    std::uintptr_t copy = PuppetCloneActor();
    return copy && StillGrabbed(copy);
}

void GrabSyncOnPacket(const char* buf, int n) {
    if (!g_on || g_host || n < static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (n != static_cast<int>(sizeof(Packet) + p.count * sizeof(Event))) return;
    for (int i = 0; i < p.count; ++i) {
        Event e;
        std::memcpy(&e, buf + sizeof(Packet) + i * sizeof(Event), sizeof(e));
        if (e.id + 1000 < g_lastId) g_lastId = 0;  // the host's game restarted
        if (e.id <= g_lastId) continue;           // already queued (each event comes 3 times)
        g_lastId = e.id;
        if (g_inboxCount < 32) g_inbox[g_inboxCount++] = e;
    }
}

void GrabSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_host) {
        std::uintptr_t copy = PuppetCloneActor();
        if (g_grab) {
            Trace(copy);
            bool held = copy && StillGrabbed(copy);
            if (held) g_freeSince = 0;
            else if (!g_freeSince) g_freeSince = g_frame;
            if ((g_freeSince && g_frame - g_freeSince >= kQuietFrames && g_frame - g_lastOpFrame >= kQuietFrames) ||
                g_frame - g_grabFrame > kMaxFrames || !copy) {
                g_grab = false;
                Event e{};
                e.op = kOpRelease;
                Queue(e);
                AggroBossActedOn(g_grabber, true, "let go of");
                if (Logs())
                    Log("grab sync: the copy is free again after %.1f s: 'let go' sent to the friend",
                        (g_frame - g_grabFrame) / 60.0);
            }
        }
        SendQueued();
    } else {
        for (int i = 0; i < g_inboxCount; ++i) Apply(g_inbox[i]);
        g_inboxCount = 0;
        if (g_checkFrame && g_frame >= g_checkFrame) {  // is our Sora free again?
            g_checkFrame = 0;
            __try {
                EndStuckGrab();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("grab sync: exception checking our Sora after the let go");
            }
        }
    }
    if (g_frame - g_statsFrame >= 1800 && (g_sent || g_run || g_skipped)) {
        g_statsFrame = g_frame;
        Log("grab sync: so far %u grabs of the copy, %u calls sent (%u not run in our game); %u run on our Sora, "
            "%u not run", g_grabs, g_sent, g_heldBack, g_run, g_skipped);
    }
}

}  // namespace kh2coop
