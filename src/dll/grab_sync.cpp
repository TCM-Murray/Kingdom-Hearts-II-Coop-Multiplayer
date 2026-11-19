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
//           the copy is grabbed don't run in the host's game: they are the friend's. Each frame's calls go as one
//           message on the reliable channel's grab stream (TODO 6.3): once each, in the host's order (a lost one holds
//           the ones after it until it comes again); the held place (below) goes alone each frame, unreliable.
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
//           our puppet sets the copy's motion again, which ends the grab motion. So at a let go (since session 37 the frame
//           after it, before that half a second later) our Sora, if still in the grab motion, is set idle as trap_obj_idle does and its motion set to idle with the
//           puppet's call (EndStuckGrab).
// Arena changes (TODO 1.81; your two-PC Data Xigbar run 2026-10-16: the host fell out of the arena): a script that
// swaps the room's parts (trap_bg_hide / trap_bg_show) and in the same frame places one Sora (trap_obj_set_pos, the
// game's exe+0x3B64D0) places "the player" only, which with one brain can be either Sora. Xigbar's arena change
// (attack 1447 on the player, the parts swapped, the player and the companions placed in the new arena) placed the
// friend's copy, and the host's Sora stayed over a part that had just gone. Now the other Sora goes to the same
// place too and is set idle as the script does with the player: the host's own Sora in the host's game when the
// copy was placed, and the friend's Sora (sent, "arena place") either way, since the friend's game swaps its parts too.
// Held place (TODO 1.89; your pair bench 2026-10-17: Data Xigbar's sniper reaction command played on the friend's
// Sora where it stood, not on his bullet): what moves the held copy in the host's game isn't one of the calls above
// (no place or attach was sent). So while an enemy's grab holds the copy, the host sends where the copy is drawn
// (world matrix row 3) and its facing every frame. With the Sora near the bullet the second 598 can be offered (TODO 1.85).
// Held stream (TODO 1.143 / 1.144; your Dark Thorn, chandelier and Sparkle Ray runs: the friend's held Sora jittered).
// Its own game moved it on its own clock while its grabber (world sync) is drawn ~100 ms behind on the playback clock,
// and a nudge toward the host's place every 2 frames fought that. Now one mover, one clock, as world sync does for
// enemies: each frame, after our Sora's own update, its place and facing become the host copy's at the playback time
// (blended between two samples), by the gap between where each is drawn; not while our Sora is attached (its parent
// draws it). And the host's grab events play at their playback time too, so motions, landings and the place line up
// with the grabber. Its velocity is left to the game (its landing, F411, comes from its own fall).
// The player's die and card (TODO 1.90; your bench 2026-10-17: the friend lost Data Luxord's card game, the host saw
// the copy as a die, the friend's Sora only played the stun before it). Luxord's script turns "the player" into the
// die or card with calls that name no one (VERIFIED_OFFSETS row 'Data Luxord's die'): trap_dice_set_spec (6/28) /
// trap_card_set_spec (6/30), trap_player_capture_form (1/242, a form change of the player in exe+0x2AE9A28 to form 12
// with the die's or card's object), trap_player_dice (6/27) / trap_player_card (6/29, a state of the player in
// exe+0x2A105D0), trap_player_capture_form_end (1/243). Aggro.cpp had made the copy "the player" (exe+0x2A105D0) for
// his script, but the form change still took the host's Sora (exe+0x2AE9A28): the die became the host's player for
// 13 s, while the friend's game got none of it. The form change works only on a game's own player, so:
//   Host:   these calls while the copy is the player (a boss's script run for the copy) don't run in our game: they
//           are sent to the friend ("player call").
//   Friend: the same calls run in our game, on our own Sora (both pointers are ours there); the capture only when the
//           game's own check (trap_player_can_capture_form, 1/355) allows it, else the die or card state after it
//           is left out too.
// The push while held (your report 2026-10-25: in Shan-Yu's clash the friend's Sora 'blinks' to each new place). Each
// press of his reaction command makes his script push the held copy with trap_obj_hitback (1/146: a direction and a
// strength, exe+0x3D4960, VERIFIED_OFFSETS L136), which the friend's Sora never got: it played the push motion in place
// and the held place then moved it at once. Now, only while an enemy holds the copy (other hits have hit sync), the push
// is sent too ("push"), and the friend's game runs the same push on its own Sora, which then slides as the copy does.
// The friend's reaction command's cameras (TODO 1.141; your two-PC Dark Thorn 2026-10-30, F389: its close-ups played in
// the host's game). The grabber's own camera wasn't enough: Dark Thorn watches the chandelier with trap_camera_watch
// (1/207) as it grabs, and the object the friend's command was on (the chandelier, 150) takes the copy from Dark Thorn
// and starts a camera on itself, or (F_BB130, 149) starts it before its own grab. So the camera calls (1/157, 1/219,
// 1/164, 1/207, 1/295) and the flashes (0/31, 0/32) of a script reaction sync gives the friend (it holds the copy, or
// the friend used a command on it last, within 5 s: the offers' rule) are the friend's too, as is a camera watch by
// the grabber holding the copy. "camera watch" is sent with the watched actor; the friend's game watches its own of it
// (the copy = its own Sora) with the game's calls of exe+0x430B80. Volpestyle's mod has the same rule for Axel's.
// The landing a held Sora is given (TODO 1.144; your report 2026-10-31: the friend's Snag lifts the Surveillance Robot
// but Sparkle Ray never comes). The robot's script sets the held Sora's fall and land motions (trap_obj_set_fall_motion
// 1/179 = actor +0x118, trap_obj_set_land_motion 1/180 = +0x11C, motion 254), and motion 254's trigger offers Sparkle
// Ray (28). Only the copy got them, so the friend's Sora landed in its own motion 5 and was never offered 28 (F411).
// Both are sent while an enemy holds the copy, and the friend's game sets the same field on its Sora. So are the leap
// to the robot and the lift (method_obj_jump 1/55, trap_obj_sysjump 1/128, F412) once that enemy has hooked the copy:
// without them the friend's Sora flew its own path, landed ~0.5 s after the copy, and a Sparkle Ray pressed while it
// still fell was cut by its landing (bench 2026-10-31 20:09:35).
// An enemy's camera for the copy before any grab (TODO 1.179; your two-PC Cerberus run 2026-11-10: the host's screen
// showed his grab's first shot). Cerberus flashes (0/32) and starts his scene camera on himself (1/157) before he hooks
// the copy, in one script step (F462, F485). The camera starts inside its call, so it can't be ended after it (a
// camera reset didn't). So an enemy's camera on itself or on nothing (1/157, 1/219; not 1/164, see GrabSyncHolds)
// while its script has the copy as "the player" (aggro's swap) is the friend's at the call, with no grab needed (your
// pick). That enemy's flashes are kept until the frame's end: one just before its camera goes with it, one later in
// its shot goes to the friend a frame late, and any other runs here a frame late.
// The friend's game starts a host's shot only once our copy of its owner stands where the host's did at the call
// (kShotNear): world sync puts it there a frame or two after the shot's event plays, and a shot looks framed from
// where its actors stand at its start (F486; your runs 2026-11-10/11). At most kShotWaitFrames; the events after it
// wait too. A grabber that takes itself off the copy (Cerberus at '20', F489) comes off our Sora then too.
// An attach with flag bit 0 keeps its rider only while the parent carries (+0x18C bit 15; the game's exe+0x3B4980 drops
// it at once otherwise, F735). The Living Bone sets that bit with its ride motion, which reaches our bound copy a few
// frames after the host's Rodeo attach, so ours was dropped in its first frame and our Sora missed the bounce (TODO
// 1.254); in the host's game a late press attached the copy mid-move, before the ride motion (F738, TODO 1.255). In both
// games such a rider goes on at once and is kept on until its parent carries (HoldOn): waiting to attach instead, the
// friend's Sora fell out of its ride pose in the air (F745).
// A script's place for the held copy (TODO 1.195, F568, F570; your Cerberus bench 2026-11-14: the friend's Sora blinked
// at his throw and stood under him before his bite). Cerberus, the Thresholder, Data Xemnas, the chandelier and the
// Hydra put the copy somewhere (trap_obj_set_pos, 1/148) just before a new grab motion, whose first pose starts there.
// It wasn't sent: our capture was put back on the last held place and the held stream then blended across the jump.
// Now it is ("script place"; one made in the grab's first frame goes ahead of the grab's first call), the friend's game
// runs the same call on its Sora, holds it there until a held place sampled after it is due and follows only those,
// and a capture after it is put back there. One script step's calls run in the same frame, and a held stream doesn't
// start before the grab's calls that wait behind a scene camera. KH2COOP_GRAB_PLACE=0 turns these off (control runs).
// Not covered: trap_obj_set_pos_trans (1/217: Data Xaldin, the lamp, Final Xemnas's scenes, F503).
// GRAB_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "aggro.hpp"
#include "avatar_link.hpp"
#include "common.hpp"
#include "game.hpp"
#include "grab_sync.hpp"
#include "motion_mirror.hpp"
#include "puppet.hpp"
#include "reaction_sync.hpp"
#include "reliable.hpp"
#include "stand_in.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4232484B;  // "KH2B"
constexpr std::uint16_t kVersion = 13;  // v13: script place (F568); v12: detach b = 1 (the grabber off the copy), camera v[3] = the owner's facing; v11: calls on the reliable grab stream, no event number; v10: event time, held place every frame with facing; v9: jump, lift; v8: fall / land motion; v7: camera watch; v6: push; v5: player call; v4: held place; v3: arena place; v2: camera b = 2, camera reset
constexpr std::uintptr_t kListHead = 0x2A171C8;  // our Sora
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;  // "the player" (aggro.cpp swaps it to the copy for a boss's script)
constexpr std::uintptr_t kTeam = 0x4DC, kObjRow = 0x918;
constexpr std::uintptr_t kHook = 0xD48, kFlags18C = 0x18C, kAttachedTo = 0x6A0;  // +0x18C bit 14: grab motion
constexpr std::uint32_t kCaptured = 1u << 14;
constexpr std::uintptr_t kFallMotion = 0x118, kLandMotion = 0x11C;  // what 1/179 and 1/180 set (F411)
constexpr std::uintptr_t kJumpStart = 0x3CBB00;   // (actor, float[4] speed): method_obj_jump's jump (1/55, exe+0x42CC80)
constexpr std::uintptr_t kMotionStep = 0x3C86A0;  // (actor + 0x158, motion, float, float): its motion, as 1/55 calls it
constexpr std::uintptr_t kSysJumper = 0xC;        // actor: handle of the object whose vtable +0x20 runs trap_obj_sysjump
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
constexpr std::uintptr_t kShotBase = 0xB64F90;      // research: the running shot's base matrix pointer (L282)
constexpr std::uintptr_t kShotEye = 0x740A88, kShotLook = 0x740A98;  // research: its camera's eye and look-at (L282)
constexpr std::uintptr_t kPlace = 0x3B64D0;         // (actor, float[4]): trap_obj_set_pos (1/148, exe+0x42B800)
constexpr std::uintptr_t kHitback = 0x3D4960;       // (actor, float[4], float power): trap_obj_hitback (1/146, exe+0x42B720)
// trap_camera_reset (1/295, no arguments) is run through the game's own trap: exe+0x430B60 = exe+0x165A40(the camera).
// trap_camera_watch (1/207, exe+0x430B80; F390) = these three on a 24-byte target {handle, mode, handle, -1, ...}:
constexpr std::uintptr_t kWatchInit = 0x3BDA50;     // (target)
constexpr std::uintptr_t kWatchSet = 0x3BEE60;      // (target, actor, mode 0)
constexpr std::uintptr_t kWatch = 0x176310;         // (target): the camera's mode 3, watching it
// host: the copy free this long = let go. 30 (0.5 s) before session 37: with the friend's own 0.5 s wait after the let
// go, its Sora stood ~1 s in Data Roxas's steal after the host's script had let go (two-PC run 2026-10-16 20:26:48).
// shortcut: "let go" is guessed as the copy free 10 frames (host only); a chain with a longer free gap ends early
// (logged: 'free for N frames, then held again'). Real state: the script's own let go (not found yet). TODO 8.11.
constexpr std::uint64_t kQuietFrames = 10;
constexpr std::uint64_t kMaxFrames = 60 * 30;       // host: a grab this long after its last call is let go anyway
// Friend: the host's let go (kOpRelease, reliable, after the grab's other calls) ends the held stream; until it comes
// our Sora stays at the newest held place. This silence only ends one whose let go never came (the link down),
// logged. The old 250 ms ended a grab early on a lag spike (TODO 8.11; 6.3).
constexpr std::uint32_t kStreamEndMs = 2000;
constexpr std::uint64_t kMaxWaitFrames = 30;        // friend: an event waits at most this long for its playback time
constexpr float kShotNear = 30.0f;                  // friend: a scene camera waits until its owner is this close
constexpr std::uint64_t kShotWaitFrames = 15;       // ... at most this long after its playback time
constexpr std::uint32_t kCarrying = 1u << 15;       // +0x18C: a parent that keeps a flag-1 rider (F735)
constexpr std::uintptr_t kAttachFlags = 0x6A6;      // the attach's flags (F243); bit 0: dropped unless its parent carries
// A flag-1 rider is kept on at most this long for its parent to carry (HoldOn). Rodeo's ride motion comes after the
// Living Bone's current motion, and 202 runs 2 s or more (L1604).
constexpr std::uint64_t kCarryWaitFrames = 180;

enum Op : std::uint8_t {
    kOpIdle = 1, kOpCapture, kOpPos, kOpDir, kOpAttach, kOpDetach, kOpMoveToSpace, kOpCamera, kOpScreen, kOpRelease,
    kOpCameraReset, kOpArenaPlace, kOpHeldPlace, kOpPlayerCall, kOpHitback, kOpWatch, kOpFallMotion,
    kOpLandMotion, kOpJump, kOpSysJump, kOpCameraCancel, kOpScriptPlace
};
const char* const kOpNames[] = {"?", "idle", "grab motion", "place", "facing", "attach", "detach", "move to space",
                                "scene camera", "screen", "let go", "camera reset", "arena place", "held place",
                                "player call", "push", "camera watch", "fall motion",
                                "land motion", "jump", "lift", "camera cancel", "script place"};

#pragma pack(push, 1)
struct Event {
    std::uint8_t op;
    std::uint16_t netId, instance;  // the grabber (world sync's number on the host), 0 if none
    std::int32_t a, b;              // capture: motion, blend (float bits); attach: its two numbers; move to space:
                                    // float bits, flag;
                                    // push: a = the strength's bits;
                                    // camera: b = 1 with the copy as target, 2 with the grabber, 0 none;
                                    // detach: b = 1 the grabber off the copy, 0 the copy off its parent;
                                    // screen: a = the call (31 / 32); player call: a = table, b = index;
                                    // camera watch: a = 1 the copy, 2 the host's Sora, 0 the actor netId names
    float v[4];                     // place, facing, push direction; camera: the owner's drawn place, v[3] its facing
    char name[4];                   // camera
    std::uint64_t raw;              // screen: the script's argument
    std::uint32_t args[7];          // player call: its arguments' low 32 bits (all these calls read)
    std::uint32_t ms;               // the host's clock when it happened (avatar link's, as the playback time)
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t count;
};
#pragma pack(pop)
constexpr int kPerPacket = 16;
static_assert(sizeof(Packet) + kPerPacket * sizeof(Event) <= reliable::kMaxPayload, "a full packet must fit one message");

using PFN_Actor = void(__fastcall*)(std::uintptr_t);
using PFN_SetAction = void(__fastcall*)(std::uintptr_t, std::int32_t);
using PFN_Capture = std::int32_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::int32_t, float);
using PFN_JumpStart = void(__fastcall*)(std::uintptr_t, const float*);
using PFN_MotionStep = void(__fastcall*)(std::uintptr_t, std::int32_t, float, float);
using PFN_SysJump = void(__fastcall*)(std::uintptr_t, std::uintptr_t, std::int32_t);
using PFN_Vec = void(__fastcall*)(std::uintptr_t, float*);
using PFN_Attach = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::int32_t, std::int32_t);
using PFN_MoveToSpace = void(__fastcall*)(std::uintptr_t, float, bool);
using PFN_Hitback = void(__fastcall*)(std::uintptr_t, float*, float);
using PFN_Camera = void(__fastcall*)(std::uintptr_t, const char*, std::uintptr_t);
using PFN_Trap = void(__fastcall*)(std::uint64_t* args);
using PFN_WatchSet = void(__fastcall*)(void*, std::uintptr_t, std::int32_t);
using PFN_Ptr = void(__fastcall*)(void*);

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;

// Host.
Event g_queue[32];  // this frame's calls, sent at its end (SendQueued)
int g_queueCount = 0;
bool g_grab = false;           // the copy is grabbed (since its first grab call)
std::uintptr_t g_grabber = 0;
std::uint64_t g_grabFrame = 0, g_lastOpFrame = 0, g_freeSince = 0;
std::uint32_t g_sent = 0, g_heldBack = 0, g_grabs = 0;
std::uintptr_t g_cameraBy = 0;  // the grabber whose scene camera went to the friend, until its camera reset

// EXCEPTION TABLE of this mechanism (the gates: memory 'exceptions-to-general-rules'; DECISIONS, Bosses). The general
// rule gives an enemy's own scene camera to one game, that of the Sora its script aims at: right for a grab, wrong
// for a camera that shows both players something. A row here runs that camera in both games, and its reset too.
// Nothing else may name a boss for this: add a row, with why the rule fails there and its TODO number. At 5 rows,
// stop and rethink the rule.
struct BothCamera {
    const char* model;  // the camera's owner, by its model name
    const char* name;   // the camera's name, as the script passes it
};
constexpr BothCamera kBothCameras[] = {
    {"N_EX760_BTL", "20"},  // Pete's scenery change, Timeless River's last fight: an arena change, no grab (TODO 1.215)
};
std::uintptr_t g_bothBy = 0;  // the owner whose camera of that table runs in both games, until its camera reset
// An enemy's flash while its script has the copy as the player, kept until the frame's end: it goes to the friend with
// that enemy's camera if one follows in the same frame (Cerberus flashes, then starts its camera, F462), or a frame
// late if its camera already went there, else it runs here a frame late (ReleaseFlashes). `by` is only compared.
struct ParkedFlash {
    std::uintptr_t by;
    std::uint64_t raw;
    std::int32_t index;  // 31 / 32
    std::uint32_t ms;    // when it was parked: a whiteout kept over a co-op pause is dropped, not run late
    const char* model;   // its name then (the exe's object table), for the log
};
ParkedFlash g_flashes[4];
int g_flashCount = 0;
std::uint32_t g_ownPlayerCameras = 0;  // enemy self-cameras left in our game: their player was our Sora
std::uintptr_t g_launchBy = 0;         // the field object that last launched the copy (TODO 1.193), and when
std::uint64_t g_launchFrame = 0;
std::uint32_t g_launchLogs = 0;
// A script's place for the copy in a frame before its grab holds it (F568: Cerberus at pc 7401, the Thresholder at
// 3980): it goes just ahead of that frame's next grab call on the copy, or nowhere (ScriptPlace).
Event g_parkedPlace{};
std::uintptr_t g_parkedPlaceBy = 0;  // its script's actor, this frame only, for the log
std::uint32_t g_parkedPlaceMs = 0;   // our clock then: it goes only with a call of the same script step (kStepMs)
bool g_placeParked = false;
std::uint32_t g_scriptPlaces = 0;
bool g_placeSync = true;  // KH2COOP_GRAB_PLACE (both games): the script's place, one step's calls, the start's order

// Friend.
struct Inbound {
    Event e;
    std::uint64_t frame;  // ours, when it came
    std::uint64_t due;    // ours, when a scene camera first waited (ShotReady), 0 before
};
Inbound g_inbox[64];
int g_inboxCount = 0;
struct HeldPlace {  // the host's held copy at a moment of its clock
    std::uint32_t ms;
    float pos[3], angle;
};
HeldPlace g_places[16];  // oldest first
int g_placeCount = 0;
std::uint32_t g_letGoMs = 0;  // the host's clock at its last let go
// The host's clock at its script's last place for the held copy (kOpScriptPlace; 0 = none): the held places sampled
// up to it are of the place before, so FollowHeldStream holds ours there until a later one is due (F568).
std::uint32_t g_cutMs = 0;
std::uint32_t g_callsWaitMs = 0;  // this frame: the host's clock of a grab call held back behind a scene camera
constexpr std::uint32_t kStepMs = 4;  // calls this close on the host's clock are one script step (frames are ~16)
struct StreamStats {
    std::uint32_t writes, attached, waiting, placed;  // placed: frames held at a script's place (g_cutMs)
    std::uint32_t usedMs;    // the newest held place our last write used (the host's clock)
    float sum, max;  // how far our own update had moved our Sora from the host's (the correction)
    float turnSum, turnMax;  // and turned it (degrees)
    float wrote[3];          // the place we wrote last frame (the held trace: what the game's own update did since)
    float wroteAngle;        // and the facing
    bool based;              // wrote holds the place a capture was put back to (the first write's base)
};
// shortcut: a held stream's first kBlendFrames writes slide our Sora to the host copy's place (1/6, 2/6 ... of the
// gap) instead of one step (TODO 1.195, your pick 2026-11-13); for 0.1 s it is drawn up to that gap off the host's
// path. Upgrade if a grab's first moves need it on the host's path at once.
constexpr std::uint32_t kBlendFrames = 6;
int g_heldTrace = 1;  // KH2COOP_HELD_TRACE: frames of each held stream logged one by one (TODO 1.162's jitter)
StreamStats g_stream = {};
std::uint32_t g_run = 0, g_skipped = 0;
std::uint64_t g_checkFrame = 0;  // our Sora's state is checked a moment after a let go (EndStuckGrab)
std::uint64_t g_releaseFrame = 0, g_captureFrame = 0;  // the last let go, the last grab motion run on our Sora
std::uint16_t g_capturedNet = 0, g_capturedInstance = 0;  // the host's enemy behind that grab motion (net id, instance)
std::uint64_t g_jumpFrame = 0;  // the last host jump (1/55) run on our Sora, and its motion
std::int32_t g_jumpMotion = -1;
int g_freeStep = 0;  // what EndStuckGrab did after the last let go: 0 nothing, 1 set idle
struct AttachedGrabber {  // our grabber the host's attach put on our Sora (world sync's number on the host)
    std::uint16_t netId, instance;
};
AttachedGrabber g_attached[4] = {};
std::uint64_t g_captureRefused = 0;  // the frame our game refused the host's capture (its die / card state left out)

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
// Who last put our Sora [0] / the copy [1] in a capture motion (trap_obj_motion_capture): a hook may end while that
// capture still plays (Data Luxord's card penalty, capture 254 after his hook thread; F749). Cleared with the victims.
std::uintptr_t g_capturedBy[2] = {};
std::uint64_t g_heldByCaptureLog = 0;  // tick of the last 'by its capture' line: one a second (aggro asks each frame)
// The script actor whose func_obj_control_off (1/42) turned our control off, until its own control_on (1/43) or a room
// change: Data Luxord's die keeps it off ~0.6 s after our Sora left the player slot (F752); a hit then cut his
// control_on and left it off (F753). Your pick 2026-11-19 'control-off window'.
std::uintptr_t g_controlOffBy = 0;
std::uint64_t g_controlOffTick = 0, g_heldByControlLog = 0;
// The live check on that latch: func_obj_control_off(0) leaves the frozen groups at 1 (F754; an event's control_off
// sets bits 0 and 1, L1554); the hold lasts only while one of them is set, and the latch ends once neither is, 100 ms
// after the call (another script's control_on, a Retry of the same room).
bool ControlStillOff() {
    std::uint32_t frozen = 0;
    if (!ReadAny(ExeBase() + game::kFrozenGroups, &frozen)) return false;
    if (frozen & 3) return true;
    if (g_controlOffBy && GetTickCount64() - g_controlOffTick > 100) {
        Log("grab sync: the script control-off is over (frozen groups %X): no hold by it now", frozen);
        g_controlOffBy = 0;
    }
    return false;
}
std::uint32_t g_victimRoom = 0;

// Research (KH2COOP_GRAB_TRACE=1): you, 2026-10-14: "if the Host isn't looking at Roxas while it does the attack on
// the Host' Sora clone, Roxas' animation glitches. Only if I aim the camera at there it recovers". Every 6 frames of
// a grab the grabber's and the copy's motion, its clock (+0x19C, what trap_obj_motion_get_time reads), the motion
// triggers (+0x2AD, trap_obj_motion_check_trigger) and where each is drawn (world matrix +0x40 row 3) next to its
// place (+0x640 +0x30): does the game stop moving them on while the host's camera looks away?
bool g_trace = false;
// Test (KH2COOP_GRAB_HOLD_CAMERA=0): the grab's scene camera and flashes also run in the host's game.
bool g_holdCamera = true;
// Friend: an enemy the host runs grabs no Sora with its own script here (RefuseOwnGrab); KH2COOP_SCRIPT_GRAB_GUARD=0: off.
bool g_scriptGrabGuard = true;
bool g_scriptGrabEnd = true;  // KH2COOP_SCRIPT_GRAB_END: a refused grab's one-shot script run ends there (TODO 1.197)
std::uint32_t g_scriptGrabsRefused = 0, g_scriptRunsEnded = 0, g_scriptRunsKept = 0;
std::uint32_t g_traceLines = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

// A script's actor argument as these calls read it: a handle to an object whose +4 is the actor's handle.
std::uintptr_t ActorArg(std::uint64_t arg) {
    __try {
        std::uintptr_t x = DecodeHandle(static_cast<std::uint32_t>(arg));
        return x ? DecodeHandle(Read<std::uint32_t>(x + 4)) : 0;
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
               DecodeHandle(Read<std::uint32_t>(a + kAttachedTo));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Queue(Event e) {
    if (g_queueCount == 32) {
        Log("grab sync: more than 32 calls on the copy in one frame: its %s not sent", kOpNames[e.op]);
        return;
    }
    e.ms = AvatarLinkNowMs();
    g_queue[g_queueCount++] = e;
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

// events: reliable, in order (grab stream); else one held place, unreliable.
void SendEvents(const Event* e, int n, bool reliable) {
    char buf[sizeof(Packet) + kPerPacket * sizeof(Event)];
    Packet p{kMagic, kVersion, AvatarLinkBuild(), static_cast<std::uint8_t>(n)};
    std::memcpy(buf, &p, sizeof(p));
    std::memcpy(buf + sizeof(Packet), e, n * sizeof(Event));
    int len = static_cast<int>(sizeof(Packet) + n * sizeof(Event));
    if (reliable) AvatarLinkSendReliable(buf, len, 0, kStreamGrab);
    else AvatarLinkSendRaw(buf, len);
}

void SendQueued() {
    for (int i = 0; i < g_queueCount; i += kPerPacket) SendEvents(g_queue + i, (std::min)(kPerPacket, g_queueCount - i), true);
    g_queueCount = 0;
}

bool ReadVec(std::uint64_t arg, float* v) {
    __try {
        std::uintptr_t p = DecodeHandle(static_cast<std::uint32_t>(arg));
        if (!p) return false;
        std::memcpy(v, reinterpret_cast<const void*>(p), 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadName(std::uint64_t arg, char* name) {
    __try {
        std::uintptr_t p = DecodeHandle(static_cast<std::uint32_t>(arg));
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

// The script whose func_screen_whiteout ran in our game with no whitein of its here since: our screen is white
// until that whitein runs here too. An address that is only compared, never read (TODO 1.229: Vexen's grab of the
// copy whited the host's screen out a frame before the grab was known, and its whitein went to the friend alone).
std::uintptr_t g_ourWhiteBy = 0;

// A flash (0/31 whiteout, 0/32 whitein) by `by` runs in our game.
void NoteOurFlash(std::uintptr_t by, std::int32_t index) {
    if (index == 31) g_ourWhiteBy = by;
    else if (by == g_ourWhiteBy) g_ourWhiteBy = 0;
}

// A flash about to be kept out of our game is the whitein of a whiteout that ran here: it has to run here too.
bool OurWhitein(std::uintptr_t by, std::int32_t index) { return index == 32 && by && by == g_ourWhiteBy; }

void QueueFlash(std::int32_t index, std::uint64_t raw) {
    Event e{};
    e.op = kOpScreen;
    e.a = index;
    e.raw = raw;
    Queue(e);
}

void RunFlash(const ParkedFlash& f) {
    NoteOurFlash(f.by, f.index);
    if (const auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(0, f.index))) {
        std::uint64_t args[16] = {f.raw};
        fn(args);
    }
}

// The flashes `by` made this frame go to the friend with its camera, and don't run here (but see OurWhitein).
void SendParkedFlashes(std::uintptr_t by) {
    int kept = 0;
    for (int i = 0; i < g_flashCount; ++i) {
        if (g_flashes[i].by != by) {
            g_flashes[kept++] = g_flashes[i];
            continue;
        }
        QueueFlash(g_flashes[i].index, g_flashes[i].raw);
        const bool here = OurWhitein(by, g_flashes[i].index);
        if (here) RunFlash(g_flashes[i]);
        else ++g_heldBack;
        if (Logs())
            Log("grab sync: %s's %s before its camera sent to the friend with it (%s)", Model(by),
                WorldCensusTrapName(0, g_flashes[i].index),
                here ? "run in our game too: its whiteout ran here" : "not run in our game");
    }
    g_flashCount = kept;
}

// The flashes `by` made this frame run in our game now: its camera runs here too (Data Luxord's 1/164, F488), so
// they are ours, before it, even if a grab follows in the frame.
void RunParkedFlashes(std::uintptr_t by) {
    int kept = 0;
    for (int i = 0; i < g_flashCount; ++i) {
        if (g_flashes[i].by == by) RunFlash(g_flashes[i]);
        else g_flashes[kept++] = g_flashes[i];
    }
    g_flashCount = kept;
}

// Each frame's start: the flashes no camera took. One by the enemy whose camera went to the friend (and none of its
// cameras ran here since) is the friend's too, a frame late: a whiteout / whitein later in its shot. The others run
// in our game after all, a frame late. A whiteout kept over a co-op pause is dropped; a whitein never is (one
// dropped after its whiteout ran would leave the screen white).
void ReleaseFlashes() {
    int n = g_flashCount;
    g_flashCount = 0;  // first: a flash that faults is not run again every frame
    std::uint32_t now = AvatarLinkNowMs();
    for (int i = 0; i < n; ++i) {
        const ParkedFlash& f = g_flashes[i];
        if (f.index == 31 && now - f.ms > 1000) continue;
        if (f.by == g_cameraBy) {
            QueueFlash(f.index, f.raw);
            const bool here = OurWhitein(f.by, f.index);
            if (here) RunFlash(f);
            else ++g_heldBack;
            if (Logs())
                Log("grab sync: %s's %s during its camera sent to the friend (%s)", f.model,
                    WorldCensusTrapName(0, f.index),
                    here ? "run in our game too: its whiteout ran here" : "not run in our game");
        } else {
            RunFlash(f);
        }
    }
}

// A script's fall / land motion calls on the copy in the frame of its grab, before the grab was known: Vexen's
// freeze sets them one step before its capture (F677), and unsent the friend's held Sora landed out of its grab
// motion and ran about (TODO 1.230). Kept for the frame and sent with the grab. `by` is only compared.
struct EarlyMotion {
    std::uintptr_t by;
    std::uint64_t frame;
    std::uint8_t op;
    std::int32_t motion;
};
EarlyMotion g_early[4];
int g_earlyCount = 0;

void NoteEarlyMotion(std::uintptr_t by, std::uint8_t op, std::int32_t motion) {
    if (g_earlyCount && g_early[0].frame != g_frame) g_earlyCount = 0;
    if (g_earlyCount < 4) g_early[g_earlyCount++] = {by, g_frame, op, motion};
}

void SendEarlyMotions(std::uintptr_t by) {
    for (int i = 0; i < g_earlyCount; ++i) {
        const EarlyMotion& m = g_early[i];
        if (m.by != by || m.frame != g_frame) continue;
        Event e{};
        std::uint16_t instance = 0;
        e.netId = WorldSyncNetIdOf(by, &instance);
        e.instance = instance;
        e.op = m.op;
        e.a = m.motion;
        Queue(e);
        if (Logs())
            Log("grab sync: %s's %s %d just before its grab sent to the friend with it", Model(by), kOpNames[m.op],
                m.motion);
    }
    g_earlyCount = 0;
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
    SendParkedFlashes(by);  // a flash just before the grab, in its frame, is the grab's
    SendEarlyMotions(by);   // and so are its fall / land motions on the copy
}

// Host: the script of `by` is the friend's now (it holds the copy, or the friend used a command on it last).
bool FriendsScript(std::uintptr_t by) { return by && ReactionSyncPlayerOf(by) == 2; }

// Host: `by` is an enemy, a field object or an NPC (the scripts whose calls on the copy go to the friend).
bool EnemysScript(std::uintptr_t by) {
    std::uint32_t team = 0;
    __try {
        team = by ? Read<std::uint32_t>(by + kTeam) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return team == 2 || team == 3 || WorldSyncIsNpc(by);
}

void ClearVictimsOnRoomChange() {
    std::uint32_t room = Read<std::uint16_t>(ExeBase() + 0x717008) | 0x10000;
    if (room == g_victimRoom) return;
    g_victimRoom = room;
    std::memset(g_victims, 0, sizeof(g_victims));
    g_capturedBy[0] = g_capturedBy[1] = 0;
    g_controlOffBy = 0;
    g_ourWhiteBy = 0;  // a room's load ends its fades
}

// trap_obj_motion_capture(grabber, actor, ...) on our Sora or the copy while that one is hooked, or
// trap_obj_hook(actor, ...) by the grabber's script on one of them (hooking: true, the hook isn't set yet).
void NoteVictim(std::uintptr_t grabber, std::uintptr_t actor, bool hooking = false) {
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead), copy = PuppetCloneActor();
    if (!grabber || !actor || (actor != sora && actor != copy)) return;
    ClearVictimsOnRoomChange();
    if (!hooking) g_capturedBy[actor == copy] = grabber;  // any capturer (a Limit partner too): ends a boss's capture hold
    __try {
        // Enemies and NPCs (TODO 1.163: BC's wardrobe N_BB080_BTL, team 1, held the friend's copy unseen there)
        if ((Read<std::uint32_t>(grabber + kTeam) != 2 && !WorldSyncIsNpc(grabber)) ||
            (!hooking && !Read<std::uintptr_t>(actor + kHook)))
            return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
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

// Friend, the frame after a let go (and again half a second after the step): our Sora still in the grab motion (+0x18C bit 14,
// what trap_obj_motion_is_capture reads) though the host's copy is free, with no new grab since, is freed with the
// game's own calls: idle (trap_obj_idle: exe+0x3BFB70, exe+0x3B4DA0(actor, 0)) and the idle motion (the motion set
// the puppet drives the copy with; it ended the copy's grab motion in the session 34 bench, VERIFIED_OFFSETS 'Data
// Roxas's steal with one brain').
// Friend: our Sora out of a grabber's motion with those calls, its place first put where it's drawn: a grab motion
// draws it away from its place (its anchor), so freeing it alone showed it at the anchor for a frame (Cerberus's
// finisher, on his middle head; F492). Used at the let go (the same frame the held stream stops) and in EndStuckGrab.
std::uint32_t g_releaseTrace = 0;  // research: frames of our Sora logged after a release (TODO 1.179's let-go blink)
// Set by FreeGrabMotion on a Sora still in a grab pose, cleared when its next update ends: until that update draws a
// pose of its own, a motion set's blend would start from the grab pose (TODO 1.195, F563, F564).
bool g_grabPoseLeft = false;
bool g_releaseNoBlend = true;      // KH2COOP_RELEASE_NOBLEND: 0 lets the game blend as before (the control run)
std::uint32_t g_blendsDropped = 0;
constexpr float kBlendMin = 2.0f;  // the game blends only over this many frames (exe+0x623B18, F564)

void FreeGrabMotion(std::uintptr_t sora) {
    std::uintptr_t exe = ExeBase();
    const float* m = reinterpret_cast<const float*>(sora + 0x40);
    float at[3] = {m[12], m[13], m[14]};
    g_grabPoseLeft = (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) != 0;  // not the jump-left case
    reinterpret_cast<PFN_Actor>(exe + kStopAction)(sora);
    reinterpret_cast<PFN_SetAction>(exe + kSetAction)(sora, 0);
    PuppetSetMotion(sora, 0);
    std::memcpy(reinterpret_cast<float*>(sora + 0x640 + 0x30), at, sizeof(at));  // its place (actor+0x670)
    std::memcpy(g_stream.wrote, at, sizeof(at));
    g_releaseTrace = 12;
    Log("grab sync: our Sora freed from the grabber's motion, its place kept where it was drawn (%.0f, %.0f, %.0f)",
        at[0], at[1], at[2]);
}

}  // namespace

// Friend: our Sora's motion sets before its first update out of a grab pose get no blend (F563, F564). The game's
// fall (16 frames) blended from the grab pose, whose bones are measured from the grabber, onto our Sora's own place:
// drawn up to 1460 units off for 0.2 s. The host's copy changes motion with no blend (the puppet's sets).
void GrabSyncMotionSet(std::uintptr_t actor, int motion, float* blendFrames) {
    if (!g_grabPoseLeft || !g_releaseNoBlend || *blendFrames <= kBlendMin) return;
    if (actor != Read<std::uintptr_t>(ExeBase() + kListHead)) return;
    if (g_blendsDropped++ < 40)
        Log("grab sync: our Sora's motion %d after the let go: its %.0f-frame blend dropped (it would start from the "
            "grab pose)", motion, *blendFrames);
    *blendFrames = 0.0f;
}

namespace {

void EndStuckGrab() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kListHead);
    if (!sora) return;
    bool captured = (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) != 0;
    int form = PuppetDriveForm();
    const char* when = g_freeStep == 1 ? "half a second after set idle" : "at the let go";
    Log("grab sync: our Sora %s: grab motion %d, attached %d, hook %d, motion %d, motion file %s, Drive Form %d", when,
        captured ? 1 : 0, DecodeHandle(Read<std::uint32_t>(sora + kAttachedTo)) ? 1 : 0, Read<std::uintptr_t>(sora + kHook) ? 1 : 0,
        Read<std::int32_t>(sora + 0x180), Read<std::uintptr_t>(sora + 0x2B8) ? "a grabber's" : "its own", form);
    // Or still in the motion of a host jump with no grab motion after it (its grabber gone from our game in between).
    bool jumpLeft = g_jumpFrame > g_captureFrame && g_jumpFrame <= g_releaseFrame &&
                    Read<std::int32_t>(sora + 0x180) == g_jumpMotion;
    if ((!captured && !jumpLeft) || g_captureFrame > g_releaseFrame) {  // free, or grabbed again since
        g_freeStep = 0;
        return;
    }
    if (g_freeStep == 0) {
        FreeGrabMotion(sora);
        g_freeStep = 1;
        g_checkFrame = g_frame + 30;
        Log("grab sync: our Sora is still in %s though the host's copy is free: set idle (as trap_obj_idle) and its idle "
            "motion", captured ? "the grab motion" : "the host's jump motion (no grab motion came after it)");
    } else {
        Log("grab sync: our Sora is STILL in the grab motion; nothing more to try");
        g_freeStep = 0;
    }
}

// Host: where the held copy is drawn now and its facing (see "Held place" at the top), every frame, sent once
// unreliable (a lost one is blended over; a doubled or late one AddHeldPlace drops). Not counted as a call, not a grab op.
std::uint32_t g_placesSent = 0;
float DrawnAngle(const float* m) { return std::atan2(-m[8], -m[10]); }  // row 2 = (-sin, 0, -cos) of the facing

// Research (TODO 1.179, L269 / L282): does an in-fight shot ('re_...') follow its actors or play a fixed view? At its
// start its base matrix and where its owner and the player stand, then its camera 1, 10, 30 and 60 frames in.
std::uintptr_t g_shotBase = 0;  // only compared with the game's own pointer, read again each frame
std::uint64_t g_shotFrame = 0;
std::uint32_t g_shotLogs = 0;
char g_shotName[24] = {};

void Pose(char* out, std::size_t size, std::uintptr_t a) {
    if (!a) {
        std::snprintf(out, size, "none");
        return;
    }
    const float* m = reinterpret_cast<const float*>(a + 0x40);
    std::snprintf(out, size, "%s (%.0f, %.0f, %.0f) %.0f deg", Model(a), m[12], m[13], m[14], DrawnAngle(m) * 57.29578f);
}

void Matrix(char* out, std::size_t size, std::uintptr_t p) {
    const float* f = reinterpret_cast<const float*>(p);
    bool identity = true;
    for (int i = 0; i < 16; ++i) identity = identity && std::fabs(f[i] - (i % 5 == 0 ? 1.0f : 0.0f)) < 1e-4f;
    if (identity) std::snprintf(out, size, "identity");
    else
        std::snprintf(out, size, "x (%.2f, %.2f, %.2f) z (%.2f, %.2f, %.2f) at (%.0f, %.0f, %.0f)", f[0], f[1], f[2],
                      f[8], f[9], f[10], f[12], f[13], f[14]);
}

void LogShotFrame(std::uint64_t n) {
    std::uintptr_t exe = ExeBase();
    const float* eye = reinterpret_cast<const float*>(exe + kShotEye);
    const float* look = reinterpret_cast<const float*>(exe + kShotLook);
    std::uintptr_t base = Read<std::uintptr_t>(exe + kShotBase);
    char m[128] = "another shot's or none", player[96];
    if (base && base == g_shotBase) Matrix(m, sizeof(m), base);
    Pose(player, sizeof(player), Read<std::uintptr_t>(exe + kPlayerPtr));
    Log("shot research: '%s' frame %llu: eye (%.0f, %.0f, %.0f), look-at (%.0f, %.0f, %.0f), base %s; the player %s",
        g_shotName, static_cast<unsigned long long>(n), eye[0], eye[1], eye[2], look[0], look[1], look[2], m, player);
}

void SendHeldPlace(std::uintptr_t copy) {
    Event e{};
    __try {
        const float* m = reinterpret_cast<const float*>(copy + 0x40);
        std::memcpy(e.v, m + 12, sizeof(e.v));
        float angle = DrawnAngle(m);
        std::memcpy(&e.a, &angle, sizeof(angle));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    e.op = kOpHeldPlace;
    e.ms = AvatarLinkNowMs();
    SendEvents(&e, 1, false);
    ++g_placesSent;
}

// Friend: a held place from the host, kept in order of its clock.
void AddHeldPlace(const Event& e) {
    if (g_placeCount && static_cast<std::int32_t>(e.ms - g_places[g_placeCount - 1].ms) <= 0) return;  // late
    // sent before the let go that ended its stream: it would hold our Sora there until the cap
    if (g_letGoMs && static_cast<std::int32_t>(e.ms - g_letGoMs) <= 0) return;
    if (g_placeCount == 16) std::memmove(&g_places[0], &g_places[1], 15 * sizeof(HeldPlace)), --g_placeCount;
    HeldPlace& p = g_places[g_placeCount++];
    p.ms = e.ms;
    std::memcpy(p.pos, e.v, sizeof(p.pos));
    std::memcpy(&p.angle, &e.a, sizeof(p.angle));
}

void EndStream(const char* why) {
    if (g_stream.writes || g_stream.attached || g_stream.waiting || g_stream.placed)
        Log("grab sync: held stream ended (%s): %u writes, our own update off by %.1f on average, %.0f at most, turned "
            "%.1f deg on average, %.0f at most; %u frames attached (left to the parent), %u waiting for the first place, "
            "%u held at a script's place", why, g_stream.writes, g_stream.writes ? g_stream.sum / g_stream.writes : 0.0f,
            g_stream.max, g_stream.writes ? g_stream.turnSum / g_stream.writes : 0.0f, g_stream.turnMax,
            g_stream.attached, g_stream.waiting, g_stream.placed);
    g_stream = {};
    g_placeCount = 0;
    g_cutMs = 0;
}

float WrapAngle(float d) {
    constexpr float kPi = 3.14159265f;
    return std::fmod(d + 3 * kPi, 2 * kPi) - kPi;
}

// Friend, after our Sora's own update: its place and facing become the host copy's at the playback time.
void FollowHeldStream(std::uintptr_t sora) {
    std::uint32_t target = AvatarLinkPlaybackSenderMs();
    if (!target) return;
    // The stream starts with the grab's calls, not before them while they wait behind a scene camera: our Sora stood
    // under Cerberus in its own landing motion for 2 frames before his capture came (F568).
    if (g_callsWaitMs && !g_stream.writes && g_captureFrame <= g_releaseFrame &&
        static_cast<std::int32_t>(target - g_callsWaitMs) >= 0)
        target = g_callsWaitMs - 1;
    if (static_cast<std::int32_t>(target - g_places[g_placeCount - 1].ms) > static_cast<std::int32_t>(kStreamEndMs))
        return EndStream("no new place for 2 s and no let go");
    int first = 0;  // after a script's place: the first held place sampled after it
    while (g_cutMs && first < g_placeCount && static_cast<std::int32_t>(g_places[first].ms - g_cutMs) <= 0) ++first;
    if (!g_cutMs && static_cast<std::int32_t>(target - g_places[0].ms) < 0) {
        ++g_stream.waiting;
        // kept where the capture was put back, or (the playback clock stepped back) at our last write, not on the
        // capture's anchor (F560)
        if ((g_stream.based && !g_stream.writes) || (g_placeSync && g_stream.writes))
            std::memcpy(reinterpret_cast<float*>(sora + 0x640 + 0x30), g_stream.wrote, sizeof(g_stream.wrote));
        return;
    }
    if (std::uintptr_t parent = DecodeHandle(Read<std::uint32_t>(sora + kAttachedTo))) {
        ++g_stream.attached;
        // What our attached Sora plays, at each change (your look at the steamboat's hook, 2026-11-15).
        static std::int32_t last = -2;
        static std::uint32_t logs = 0;
        std::int32_t motion = Read<std::int32_t>(sora + 0x180);
        if (motion != last && logs < 300) {
            ++logs;
            Log("grab sync: our Sora on our %s: motion %d -> %d, grab motion %d, hook %d", Model(parent), last, motion,
                (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) ? 1 : 0, Read<std::uintptr_t>(sora + kHook) ? 1 : 0);
        }
        last = motion;
        return;
    }
    bool justCut = false;  // this write is the first after a script place
    if (g_cutMs) {
        // Held at the script's place until a place sampled after it is due, then only those: one blended across the
        // jump drew our Sora 0.29 of the way, 1070 off, with Cerberus's throw's first pose (F568).
        if (first == g_placeCount || static_cast<std::int32_t>(target - g_places[first].ms) < 0) {
            std::memcpy(reinterpret_cast<float*>(sora + 0x640 + 0x30), g_stream.wrote, sizeof(g_stream.wrote));
            ++g_stream.placed;
            return;
        }
        std::memmove(&g_places[0], &g_places[first], (g_placeCount - first) * sizeof(HeldPlace));
        g_placeCount -= first;
        g_cutMs = 0;
        justCut = true;
    }
    // Not the places sampled after a script place still in our inbox (it runs at our next frame start): until its cut,
    // a write between the last one before it and the first one after it would blend across its jump (F568).
    int n = g_placeCount;
    std::uint32_t pending = 0;
    for (int i = 0; g_placeSync && i < g_inboxCount; ++i)
        if (g_inbox[i].e.op == kOpScriptPlace && (!pending || static_cast<std::int32_t>(g_inbox[i].e.ms - pending) < 0))
            pending = g_inbox[i].e.ms;
    while (pending && n > 0 && static_cast<std::int32_t>(g_places[n - 1].ms - pending) > 0) --n;
    if (!n) {  // all sampled after it: held at our last write or the capture's put-back, else left alone
        if (g_stream.based || g_stream.writes)
            std::memcpy(reinterpret_cast<float*>(sora + 0x640 + 0x30), g_stream.wrote, sizeof(g_stream.wrote));
        ++g_stream.waiting;
        return;
    }
    const HeldPlace& newest = g_places[n - 1];
    HeldPlace at = newest;
    g_stream.usedMs = newest.ms;
    for (int i = n - 2; i >= 0; --i) {
        const HeldPlace& older = g_places[i];
        if (static_cast<std::int32_t>(target - older.ms) < 0) continue;
        const HeldPlace& newer = g_places[i + 1];
        g_stream.usedMs = newer.ms;
        float t = (std::min)(1.0f, static_cast<float>(target - older.ms) / static_cast<float>(newer.ms - older.ms));
        for (int k = 0; k < 3; ++k) at.pos[k] = older.pos[k] + (newer.pos[k] - older.pos[k]) * t;
        at.angle = older.angle + WrapAngle(newer.angle - older.angle) * t;
        break;
    }
    // By the gap between where each is drawn: a grab motion can draw a Sora away from its place, on both sides alike.
    const float* m = reinterpret_cast<const float*>(sora + 0x40);
    float d[3] = {at.pos[0] - m[12], at.pos[1] - m[13], at.pos[2] - m[14]};
    float gap = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    auto* p = reinterpret_cast<float*>(sora + 0x640 + 0x30);  // its place (actor+0x670)
    if (static_cast<int>(g_stream.writes) < g_heldTrace)
        Log("held trace %u: target %u ms; host's (%.0f, %.0f, %.0f) %.0f deg; drawn (%.0f, %.0f, %.0f) %.0f deg; "
            "place (%.0f, %.0f, %.0f), our update moved it (%.1f, %.1f, %.1f) since our write; correction %.1f",
            g_stream.writes, target, at.pos[0], at.pos[1], at.pos[2], at.angle * 57.29578f, m[12], m[13], m[14],
            DrawnAngle(m) * 57.29578f, p[0], p[1], p[2], g_stream.writes ? p[0] - g_stream.wrote[0] : 0.0f,
            g_stream.writes ? p[1] - g_stream.wrote[1] : 0.0f, g_stream.writes ? p[2] - g_stream.wrote[2] : 0.0f, gap);
    // Absolute: the drawn place comes from the place this update started from (our last write), and a captured
    // motion puts the place back on its own anchor every update; adding the gap to that anchor flipped our Sora
    // between two places each frame (BC wardrobe, held trace 2026-11-01 17:50). The motion's draw offset is measured
    // against our last write; with no reset in between this is the same as adding the gap.
    auto* angle = reinterpret_cast<float*>(sora + 0x640 + 0x4C);
    bool based = g_stream.writes || g_stream.based;
    const float* from = based ? g_stream.wrote : p;
    float fromAngle = based ? g_stream.wroteAngle : *angle;
    // Only from a put-back place: from the capture's anchor (no place had come yet) the old one-step write is better.
    float share = g_stream.based && g_stream.writes < kBlendFrames
                      ? static_cast<float>(g_stream.writes + 1) / kBlendFrames : 1.0f;
    for (int k = 0; k < 3; ++k) p[k] = from[k] + d[k] * share;
    std::memcpy(g_stream.wrote, p, sizeof(g_stream.wrote));
    // The first write after a script place keeps the facing its step's facing call set: this frame's matrix can still
    // be drawn at the old one (-38 against 53 at Cerberus's throw), and the turn added that 90 deg on top (144 drawn).
    float turn = justCut ? 0.0f : WrapAngle(at.angle - DrawnAngle(m)) * share;
    *angle = fromAngle + turn;
    g_stream.wroteAngle = *angle;
    turn = std::fabs(turn) * 57.29578f;
    g_stream.turnSum += turn;
    if (turn > g_stream.turnMax) g_stream.turnMax = turn;
    *reinterpret_cast<float*>(sora + 0x640 + 0x40) = std::sin(*angle);
    *reinterpret_cast<float*>(sora + 0x640 + 0x48) = std::cos(*angle);
    if (g_stream.writes++ == 0 && Logs()) Log("grab sync: our held Sora follows the host's copy (first gap %.0f)", gap);
    g_stream.sum += gap;
    if (gap > g_stream.max) g_stream.max = gap;
}

// Friend: run one of the host's grab calls on our own Sora.
// Friend: the host's die / card call, run on our own Sora through the game's own script call (see the top).
void ApplyPlayerCall(const Event& e) {
    auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(e.a, e.b));
    const char* name = WorldCensusTrapName(e.a, e.b);
    if (!fn) {
        ++g_skipped;
        Log("grab sync: the host's %s not run: no such call in our game", name);
        return;
    }
    if (e.a == 1 && e.b == 243 && !StandInWeAreCaptured()) {  // ended already (our down ended it, downed.cpp)
        ++g_skipped;
        Log("grab sync: the host's %s not run: our Sora isn't captured any more", name);
        return;
    }
    bool state = e.a == 6 && (e.b == 27 || e.b == 29);  // the die's / card's state on the player
    if (state && g_captureRefused && g_frame - g_captureRefused < 120) {
        ++g_skipped;
        Log("grab sync: the host's %s not run: our game refused the capture before it", name);
        return;
    }
    std::uint64_t args[16] = {};
    for (int i = 0; i < 7; ++i) args[i] = e.args[i];
    __try {
        if (e.a == 1 && e.b == 242) {
            std::uint64_t can[16] = {};
            auto check = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, 355));  // trap_player_can_capture_form
            if (check) check(can);
            if (check && !static_cast<std::uint32_t>(can[0])) {
                g_captureRefused = g_frame;
                ++g_skipped;
                Log("grab sync: the host's %s(%u) not run: our game says our Sora can't be captured now", name,
                    e.args[0]);
                return;
            }
            g_captureRefused = 0;
        }
        if (e.a == 1 && e.b == 242) StandInOurCapture();  // before the call: it reads our body as it was
        fn(args);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_skipped;
        Log("grab sync: the host's %s on our Sora failed (exception)", name);
        return;
    }
    ++g_run;
    Log("grab sync: ran the host's %s(%u, %u, ...) on our Sora (the copy lost to %s in the host's game)", name,
        e.args[0], e.args[1], e.netId ? Model(WorldSyncBoundActor(e.netId, e.instance)) : "?");
}

// TODO 1.250 (F726, L1593): with a capture source on (actor+0x2B8), the game's fall and land handlers
// (exe+0x3CB8B0, exe+0x3CB940) play our Sora's fall / land motion from that source without checking it has one
// (exe+0x3C7190), and one it lacks faults at exe+0x3A648A. A belt jump leaves land 2, which the belt's set has no
// entry for: the host's script leaves the same on the copy, and our replay lands it a playback delay late, on a Sora
// that may be in the air. So after each replayed call a fall / land motion the source lacks becomes -1, the game's
// "none" that both handlers skip; their own set's 4 / 5 and the source's own motions stay. Nothing is kept.
std::uint32_t g_captureMotionsCleared = 0;

bool SetHasMotion(std::uintptr_t set, std::int32_t motion) {
    std::int32_t n = 0;
    std::uint32_t handle = 0;
    return motion >= 0 && ReadAny(set + 4, &n) && motion < n &&
           ReadAny(set + 0x10 + 16 * static_cast<std::uintptr_t>(motion) + 8, &handle) && DecodeHandle(handle);
}

void ClearMissingCaptureMotions(std::uintptr_t sora) {
    std::uintptr_t set = 0;
    std::int32_t motion = -1;
    if (!sora || !ReadAny(sora + game::kActorMotionBank, &set) || !set || !ReadAny(sora + game::kActorMotion, &motion))
        return;
    static bool warned = false;
    if (!SetHasMotion(set, motion)) {  // the motion it plays from there must be in it, else this read of it is wrong
        if (!warned) {
            warned = true;
            Log("grab sync: our Sora's capture source %p has no entry for its own motion %d: its fall / land motions "
                "left as they are", reinterpret_cast<void*>(set), motion);
        }
        return;
    }
    const struct {
        std::uintptr_t at;
        std::int32_t own;  // the value the handler plays from the actor's own set
        const char* name;
    } kinds[] = {{kFallMotion, 4, "fall"}, {kLandMotion, 5, "land"}};
    for (const auto& k : kinds) {
        std::int32_t v = -1;
        if (!ReadAny(sora + k.at, &v) || v < 0 || v == k.own || SetHasMotion(set, v)) continue;
        *reinterpret_cast<volatile std::int32_t*>(sora + k.at) = -1;
        if (g_captureMotionsCleared++ < 20)
            Log("grab sync: our Sora's %s motion %d isn't in its capture source %p (motion %d): set to -1 (TODO 1.250)",
                k.name, v, reinterpret_cast<void*>(set), motion);
    }
}

// A rider put on a parent that doesn't carry yet (see the top): its attach flag bit 0 cleared so the game keeps it on,
// set back once the parent carries or after kCarryWaitFrames. The rider is the host's copy (GrabSyncAfterAttach) or the
// friend's Sora (Apply's attach). Kept across frames: the same objects while the rider, found again each frame, still
// holds that parent handle and the actor there has the same object row; a detach, a let go or a new attach ends it.
struct CarryWait {
    std::uintptr_t rider, row;
    std::uint32_t parent;
    std::uint64_t frame;
} g_carryWait{};

std::uintptr_t Rider() { return g_host ? PuppetCloneActor() : Read<std::uintptr_t>(ExeBase() + kListHead); }
const char* RiderName() { return g_host ? "the copy" : "our Sora"; }

void HoldOn(std::uintptr_t rider, std::uintptr_t parent) {
    g_carryWait = {};  // a new attach writes its own flags
    std::uintptr_t row = 0;
    std::uint32_t h = 0, flags = 0;
    if (!rider || !parent || !ReadAny(rider + kAttachedTo, &h) || DecodeHandle(h) != parent ||
        !ReadAny(parent + kObjRow, &row) || !ReadAny(parent + kFlags18C, &flags) || (flags & kCarrying))
        return;
    *reinterpret_cast<std::uint8_t*>(rider + kAttachFlags) &= ~1;
    g_carryWait = {rider, row, h, g_frame};
    Log("grab sync: %s goes on our %s before it carries (+0x18C bit 15 clear): kept on until it does (F735)", RiderName(),
        Model(parent));
}

void KeepRiderOn() {
    CarryWait w = g_carryWait;
    std::uintptr_t rider = Rider(), row = 0;
    std::uint32_t h = 0, flags = 0;
    std::uintptr_t parent = rider == w.rider && ReadAny(rider + kAttachedTo, &h) && h == w.parent ? DecodeHandle(h) : 0;
    if (!parent || !ReadAny(parent + kObjRow, &row) || row != w.row || !ReadAny(parent + kFlags18C, &flags)) {
        g_carryWait = {};  // taken off or put on something else: its own flags now, left alone
        Log("grab sync: %s left its parent while it waited for it to carry: its attach flags left as they are", RiderName());
        return;
    }
    auto waited = static_cast<unsigned long long>(g_frame - w.frame);
    if (!(flags & kCarrying) && waited < kCarryWaitFrames) return;
    *reinterpret_cast<std::uint8_t*>(rider + kAttachFlags) |= 1;
    g_carryWait = {};
    if (flags & kCarrying)
        Log("grab sync: %s kept on our %s %llu frames until it carried: its attach flag back", RiderName(), Model(parent),
            waited);
    else
        Log("grab sync: our %s doesn't carry after %llu frames: %s's attach flag back, the game drops it", Model(parent),
            waited, RiderName());
}

void Apply(const Event& e) {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t sora = Read<std::uintptr_t>(exe + kListHead);
    if (e.op == kOpRelease) {
        EndStream("let go");
        g_letGoMs = e.ms;
    }
    if (e.op == kOpPlayerCall) {
        ApplyPlayerCall(e);
        return;
    }
    if (e.op == kOpWatch) {
        std::uintptr_t watched = e.a == 1 ? sora : e.a == 2 ? PuppetCloneActor()
                                 : e.netId ? WorldSyncBoundActor(e.netId, e.instance) : 0;
        if (!watched) {
            ++g_skipped;
            if (Logs()) Log("grab sync: the host's camera watch (host#%u, kind %d) not run: no such actor of ours", e.netId, e.a);
            return;
        }
        __try {
            alignas(8) std::uint32_t target[6] = {};  // 24 bytes as the game's own: exe+0x176310 reads +0x10 too
            reinterpret_cast<PFN_Ptr>(exe + kWatchInit)(target);
            reinterpret_cast<PFN_WatchSet>(exe + kWatchSet)(target, watched, 0);
            reinterpret_cast<PFN_Ptr>(exe + kWatch)(target);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ++g_skipped;
            Log("grab sync: the host's camera watch on our %s failed (exception)", Model(watched));
            return;
        }
        ++g_run;
        if (Logs()) Log("grab sync: ran the host's camera watch on our %s", e.a == 1 ? "Sora" : Model(watched));
        return;
    }
    std::uintptr_t by = e.netId ? WorldSyncBoundActor(e.netId, e.instance) : 0;
    // The jump and lift too: the grab motions, attach and lift after them need our grabber, and a jump left alone
    // stood our Sora in its motion 3 until a hit (bench 2026-10-31 20:47:44, the robot killed in our game).
    bool needsBy = e.op == kOpCapture || e.op == kOpAttach || e.op == kOpCamera || e.op == kOpJump || e.op == kOpSysJump;
    const char* op = e.op < sizeof(kOpNames) / sizeof(kOpNames[0]) ? kOpNames[e.op] : "?";
    // A grabber killed with the host's still runs the host's calls on our Sora while it dies (its brain is on for its
    // death): the Aerial Knocker's Rapid Blow drop (capture 254) comes as it fades out, and left our Sora in the strike
    // pose (F740, TODO 1.256).
    bool dying = false;
    if (!by && needsBy && e.netId && (by = WorldSyncDyingActor(e.netId, e.instance)) != 0) {
        dying = true;
        if (Logs()) Log("grab sync: the host's %s runs on our %s, killed with the host's and still dying", op, Model(by));
    }
    if (!sora || (needsBy && (!by || (!dying && !WorldSyncSkipsBrain(by))))) {
        ++g_skipped;
        if (Logs())
            Log("grab sync: the host's %s (host#%u) not run: %s", op, e.netId,
                !sora ? "no Sora of ours" : "no enemy of ours bound to the grabber");
        return;
    }
    MotionMirrorMute(true);
    __try {
        switch (e.op) {
            case kOpIdle:  // not a release: Cerberus calls it right after each grab's first capture too (F492)
                reinterpret_cast<PFN_Actor>(exe + kStopAction)(sora);
                reinterpret_cast<PFN_SetAction>(exe + kSetAction)(sora, 0);
                break;
            case kOpCapture: {
                float blend;  // the script's float, sent as its bits
                std::memcpy(&blend, &e.b, sizeof(blend));
                auto* place = reinterpret_cast<float*>(sora + 0x640 + 0x30);  // actor+0x670
                float before[3];
                std::memcpy(before, place, sizeof(before));
                reinterpret_cast<PFN_Capture>(exe + kCapture)(by + 0x158, sora, e.a, blend);
                g_captureFrame = g_frame;
                g_capturedNet = WorldSyncBoundTo(by, &g_capturedInstance);
                // The capture puts the place on the motion's anchor, and this frame's update would draw it there before
                // our held write (F560: one frame 471 off on Phil One-Two). With a held stream coming, the place goes
                // back where the update would have started from: our last held write, or where it was, or the host's
                // script's place just before it (kOpScriptPlace).
                // Attached (the steamboat's hook holds our Sora on its bone): the place is its parent's own, back as it
                // was; the stream's last write there hung our Sora 900 off the hook (your run, 2026-11-15).
                if (DecodeHandle(Read<std::uint32_t>(sora + kAttachedTo))) {
                    std::memcpy(place, before, sizeof(before));
                } else if (g_placeCount || g_cutMs) {
                    if (!g_stream.writes) {
                        std::memcpy(g_stream.wrote, before, sizeof(before));
                        g_stream.wroteAngle = *reinterpret_cast<const float*>(sora + 0x640 + 0x4C);
                        g_stream.based = true;
                    }
                    std::memcpy(place, g_stream.wrote, sizeof(g_stream.wrote));
                }
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
                // after a script's place, the held stream's next write turns from this facing (its step's own)
                if (g_cutMs) g_stream.wroteAngle = *reinterpret_cast<const float*>(sora + 0x640 + 0x4C);
                break;
            }
            case kOpScriptPlace: {  // the host's script put its held copy there (F568): ours too, as trap_obj_set_pos
                // Came late (a resend): our held writes already used places sampled after it (FollowHeldStream doesn't
                // while it waits in the inbox), so ours stays on the stream; a capture after it is put back on the
                // stream's last write.
                if (g_stream.writes && static_cast<std::int32_t>(g_stream.usedMs - e.ms) > 0) {
                    MotionMirrorMute(false);
                    ++g_skipped;
                    Log("grab sync: the host's script place came after our held writes passed it: left out");
                    return;
                }
                // The game's call, not a bare +0x670 write: with the bare write our update drew our Sora ~480 off at
                // the start and once 1568 at the throw (bench 2026-11-14). It rebuilds the draw matrix at once (L332),
                // so until our Sora's update a memory read sees the old grab pose there; the update redraws it.
                float v[4];
                std::memcpy(v, e.v, sizeof(v));
                reinterpret_cast<PFN_Vec>(exe + kPlace)(sora, v);
                std::memcpy(g_stream.wrote, reinterpret_cast<const float*>(sora + 0x640 + 0x30), sizeof(g_stream.wrote));
                g_stream.wroteAngle = *reinterpret_cast<const float*>(sora + 0x640 + 0x4C);
                g_stream.based = true;  // the held stream's base from here: its writes, a capture's put-back
                g_cutMs = e.ms ? e.ms : 1;
                break;
            }
            case kOpArenaPlace: {  // the host's script placed its Sora in a changed arena: ours goes there too
                float v[4];
                std::memcpy(v, e.v, sizeof(v));
                reinterpret_cast<PFN_Vec>(exe + kPlace)(sora, v);
                reinterpret_cast<PFN_Actor>(exe + kStopAction)(sora);
                reinterpret_cast<PFN_SetAction>(exe + kSetAction)(sora, 0);
                break;
            }
            case kOpAttach: {
                if (e.raw == 1) {  // our Sora on our grabber's bone: its parent moves it from here (HeldFollow leaves it)
                    reinterpret_cast<PFN_Attach>(exe + kAttach)(sora, by, e.a, e.b);
                    if (e.b & 1) HoldOn(sora, by);  // kept on until our grabber carries (see the top)
                    else g_carryWait = {};
                    break;
                }
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
                if (e.op == kOpRelease || e.b == 0) g_carryWait = {};  // our Sora off: its flag never put back
                if (e.op == kOpDetach && e.b == 1) {  // the grabber took itself off the copy: ours off our Sora (F489)
                    if (by && DecodeHandle(Read<std::uint32_t>(by + kAttachedTo)) == sora)
                        reinterpret_cast<PFN_Actor>(exe + kDetach)(by);
                    for (AttachedGrabber& g : g_attached)
                        if (g.netId == e.netId && g.instance == e.instance) g = {};
                    break;
                }
                if (DecodeHandle(Read<std::uint32_t>(sora + kAttachedTo))) reinterpret_cast<PFN_Actor>(exe + kDetach)(sora);
                if (e.op == kOpRelease) {
                    // The host's copy has been free kQuietFrames already: free ours on the next frame (it was 30
                    // frames, which kept the friend's Sora in Data Roxas's steal ~1 s, two-PC run 2026-10-16).
                    g_checkFrame = g_frame + 1;
                    g_releaseFrame = g_frame;
                    g_freeStep = 0;
                    // A grabber the attach left on our Sora: off it, or world sync leaves it there (it doesn't move a
                    // copy attached here while the host's is attached too, and draws this one from our Sora's bone).
                    for (AttachedGrabber& g : g_attached) {
                        std::uintptr_t grabber = g.netId ? WorldSyncBoundActor(g.netId, g.instance) : 0;
                        if (!grabber && g.netId) grabber = WorldSyncDyingActor(g.netId, g.instance);  // F740
                        if (grabber && DecodeHandle(Read<std::uint32_t>(grabber + kAttachedTo)) == sora) {
                            reinterpret_cast<PFN_Actor>(exe + kDetach)(grabber);
                            if (Logs()) Log("grab sync: our %s was still on our Sora at the let go: detached", Model(grabber));
                        }
                        g = {};
                    }
                    // Still in the grab motion (no idle came): freed now, not a frame later, or that frame with no held
                    // place shows our Sora at the motion's anchor (F492).
                    if (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) FreeGrabMotion(sora);
                    g_releaseTrace = 12;
                }
                break;
            case kOpMoveToSpace: {
                float f;
                std::memcpy(&f, &e.a, sizeof(f));
                reinterpret_cast<PFN_MoveToSpace>(exe + kMoveToSpace)(sora, f, e.b != 0);
                break;
            }
            case kOpHitback: {
                float v[4], power;
                std::memcpy(v, e.v, sizeof(v));
                std::memcpy(&power, &e.a, sizeof(power));
                reinterpret_cast<PFN_Hitback>(exe + kHitback)(sora, v, power);
                break;
            }
            case kOpJump: {  // as exe+0x42CC80 does it on its actor
                float v[4], f;
                std::memcpy(v, e.v, sizeof(v));
                for (float& x : v)
                    if (x != x) x = 0.0f;  // the game zeroes a NaN
                std::memcpy(&f, &e.b, sizeof(f));
                reinterpret_cast<PFN_JumpStart>(exe + kJumpStart)(sora, v);
                reinterpret_cast<PFN_MotionStep>(exe + kMotionStep)(sora + 0x158, e.a, f, 0.0f);
                if (e.raw) {
                    *reinterpret_cast<std::int32_t*>(sora + kFallMotion) = e.a + 1;
                    *reinterpret_cast<std::int32_t*>(sora + kLandMotion) = e.a + 2;
                }
                g_jumpFrame = g_frame;
                g_jumpMotion = e.a;
                break;
            }
            case kOpSysJump: {  // as exe+0x42CC30 does it on its actor
                std::uintptr_t jumper = DecodeHandle(Read<std::uint32_t>(sora + kSysJumper));
                if (!jumper) break;
                auto fn = reinterpret_cast<PFN_SysJump>(Read<std::uintptr_t>(Read<std::uintptr_t>(jumper) + 0x20));
                fn(jumper, sora, e.a);
                break;
            }
            case kOpFallMotion:
            case kOpLandMotion:
                *reinterpret_cast<std::int32_t*>(sora + (e.op == kOpFallMotion ? kFallMotion : kLandMotion)) = e.a;
                break;
            case kOpCamera: {
                char name[8] = {};
                std::memcpy(name, e.name, 4);
                // Research (F486): where our owner and Sora are as the shot starts, against the host's owner.
                const float* o = reinterpret_cast<const float*>(by + 0x70);
                const float* s = reinterpret_cast<const float*>(sora + 0x70);
                float dx = o[0] - e.v[0], dy = o[1] - e.v[1], dz = o[2] - e.v[2];
                if (Logs())
                    Log("grab sync: the host's scene camera '%s' starts: our %s %.0f from the host's (ours (%.0f, %.0f, "
                        "%.0f), the host's (%.0f, %.0f, %.0f)), facing ours %.0f, the host's %.0f deg; our Sora at "
                        "(%.0f, %.0f, %.0f)", name, Model(by), std::sqrt(dx * dx + dy * dy + dz * dz), o[0], o[1], o[2],
                        e.v[0], e.v[1], e.v[2], DrawnAngle(reinterpret_cast<const float*>(by + 0x40)) * 57.29578f,
                        e.v[3] * 57.29578f, s[0], s[1], s[2]);
                reinterpret_cast<PFN_Camera>(exe + kCameraStart)(by, name, e.b == 1 ? sora : e.b == 2 ? by : 0);
                break;
            }
            case kOpCameraReset:
            case kOpCameraCancel: {  // trap_camera_cancel(flag) ends the scene the camera runs as (L181)
                auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, e.op == kOpCameraReset ? 295 : 223));
                std::uint64_t args[16] = {static_cast<std::uint64_t>(e.a)};
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
        } else if (e.op == kOpHitback) {
            float power;
            std::memcpy(&power, &e.a, sizeof(power));
            Log("grab sync: ran the host's push on our Sora: direction (%.2f, %.2f, %.2f), strength %g (bits 0x%X)",
                e.v[0], e.v[1], e.v[2], power, static_cast<std::uint32_t>(e.a));
        } else {
            Log("grab sync: ran the host's %s on our Sora%s%s (%d, %d)%s", op, by ? " with our " : "", by ? Model(by) : "",
                e.a, e.b, e.op == kOpAttach && e.raw == 1 ? ": our Sora goes on it" : "");
        }
    }
}

float Gap(const float* a, const float* b) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// Friend: may a host scene camera start now (see the top)? Our owner is found again each frame by world sync's number:
// only the event is kept across frames.
bool ShotReady(Inbound& in) {
    if (!in.due) in.due = g_frame;
    std::uintptr_t by = in.e.netId ? WorldSyncBoundActor(in.e.netId, in.e.instance) : 0;
    if (!by) return true;  // Apply leaves it and says why
    float owner = Gap(reinterpret_cast<const float*>(by + 0x70), in.e.v);
    auto waited = static_cast<unsigned long long>(g_frame - in.due);
    if (owner <= kShotNear) {
        if (waited && Logs())
            Log("grab sync: the host's scene camera '%.4s' waited %llu frames for our %s", in.e.name, waited, Model(by));
        return true;
    }
    if (waited < kShotWaitFrames) return false;
    Log("grab sync: the host's scene camera '%.4s' starts after %llu frames anyway: our %s %.0f from the host's",
        in.e.name, waited, Model(by), owner);
    return true;
}

}  // namespace

void GrabSyncAfterAttach(const std::uint64_t* args) {
    if (!g_on || !g_host) return;
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy || ActorArg(args[0]) != copy) return;
    if (args[3] & 1) HoldOn(copy, ActorArg(args[1]));
    else g_carryWait = {};  // a new attach of the copy writes its own flags
}

void GrabSyncInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_GRAB_SYNC", 1) == 1;
    g_trace = g_on && host && EnvInt("KH2COOP_GRAB_TRACE", 0) == 1;
    g_holdCamera = EnvInt("KH2COOP_GRAB_HOLD_CAMERA", 1) == 1;
    g_heldTrace = EnvInt("KH2COOP_HELD_TRACE", 240);
    g_releaseNoBlend = EnvInt("KH2COOP_RELEASE_NOBLEND", 1) == 1;
    g_scriptGrabGuard = EnvInt("KH2COOP_SCRIPT_GRAB_GUARD", 1) == 1;
    g_scriptGrabEnd = EnvInt("KH2COOP_SCRIPT_GRAB_END", 1) == 1;
    g_placeSync = EnvInt("KH2COOP_GRAB_PLACE", 1) == 1;
    if (g_on && !g_placeSync) Log("grab sync: a script's place for the held copy not sent / not followed (GRAB_PLACE=0)");
    if (g_on)
        Log("grab sync: on (%s)", host ? "an enemy's grab of the friend's copy goes to the friend's game"
                                       : "the host's enemies' grabs of our copy play on our Sora");
    if (!host)
        Log("grab sync: enemies the host runs %s", g_scriptGrabGuard ? "grab no Sora with their own script here"
                                                                     : "can grab with their own script here (SCRIPT_GRAB_GUARD=0)");
}

// Host: an enemy's or field object's script swapped the room's parts (36 / 37) and placed a Sora (148) in the same
// frame: the other Sora goes there too (see "Arena changes" at the top). True when it sent the place.
std::uint64_t g_arenaFrame = 0;
std::uintptr_t g_arenaBy = 0;
std::uint32_t g_arenaPlaced = 0;
bool ArenaCall(int index, const std::uint64_t* args) {
    std::uintptr_t by = WorldCensusScriptActor();
    std::uint32_t team = 0;
    __try {
        team = by ? Read<std::uint32_t>(by + kTeam) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (team != 2 && team != 3 && !WorldSyncIsNpc(by)) return false;
    if (index != 148) {
        g_arenaFrame = g_frame;
        g_arenaBy = by;
        return false;
    }
    if (!g_arenaFrame || by != g_arenaBy || g_frame - g_arenaFrame > 1) return false;
    std::uintptr_t placed = ActorArg(args[0]);
    std::uintptr_t copy = PuppetCloneActor();
    std::uintptr_t ours = Read<std::uintptr_t>(ExeBase() + kListHead);
    if (!placed || !copy || !ours || (placed != copy && placed != ours)) return false;
    Event e{};
    if (!ReadVec(args[1], e.v)) return false;
    for (float& f : e.v)
        if (f != f) f = 0.0f;  // the game's trap zeroes what isn't a number
    ++g_arenaPlaced;
    // The friend's own Sora goes there in both cases: when the copy was placed, its game removed the floor under the
    // friend's Sora too (Data Xigbar two-PC run 2026-10-17 00:17:02, it fell for the rest of the fight).
    std::uint16_t instance = 0;
    e.netId = WorldSyncNetIdOf(by, &instance);
    e.instance = instance;
    e.op = kOpArenaPlace;
    Queue(e);
    if (placed == copy) {  // our own Sora goes there too, and stands as the script makes the player
        __try {
            float v[4];
            std::memcpy(v, e.v, sizeof(v));
            reinterpret_cast<PFN_Vec>(ExeBase() + kPlace)(ours, v);
            reinterpret_cast<PFN_Actor>(ExeBase() + kStopAction)(ours);
            reinterpret_cast<PFN_SetAction>(ExeBase() + kSetAction)(ours, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        Log("grab sync: %s changed the arena and placed the friend's copy at (%.0f, %.0f, %.0f): our Sora placed there "
            "too, sent to the friend for its Sora (%u so far)", Model(by), e.v[0], e.v[1], e.v[2], g_arenaPlaced);
        return true;
    }
    Log("grab sync: %s changed the arena and placed our Sora at (%.0f, %.0f, %.0f): sent to the friend for its Sora "
        "(%u so far)", Model(by), e.v[0], e.v[1], e.v[2], g_arenaPlaced);
    return true;
}

// Host: trap_obj_set_pos (1/148) by an enemy's script on the copy it grabs (F568, F570: Cerberus, the Thresholder, Data
// Xemnas, the chandelier, the Hydra put it there just before a new grab motion, whose first pose starts from there).
// Still held: sent now. Before the grab holds it (its first frame): parked for that frame's next grab call on the copy
// (QueueParkedPlace), else dropped. A place for a copy no grab holds stays ours, as before (the puppet places it).
void ScriptPlace(const std::uint64_t* args) {
    std::uintptr_t copy = PuppetCloneActor(), by = WorldCensusScriptActor();
    if (!g_placeSync || !copy || ActorArg(args[0]) != copy || !EnemysScript(by)) return;
    Event e{};
    if (!ReadVec(args[1], e.v)) return;
    for (float& f : e.v)
        if (f != f) f = 0.0f;  // the game's trap zeroes what isn't a number
    std::uint16_t instance = 0;
    e.netId = WorldSyncNetIdOf(by, &instance);
    e.instance = instance;
    e.op = kOpScriptPlace;
    if (g_grab && StillGrabbed(copy)) {  // not g_grab alone: it stays up kQuietFrames after the copy is free
        g_placeParked = false;  // an older one this frame is out of date
        Queue(e);
        if (Logs())
            Log("grab sync: %s's place for the copy (%.0f, %.0f, %.0f) sent to the friend (%u so far)", Model(by), e.v[0],
                e.v[1], e.v[2], ++g_scriptPlaces);
        return;
    }
    g_parkedPlace = e;  // a second one this frame replaces it: the copy ends at the last
    g_parkedPlaceBy = by;
    g_parkedPlaceMs = AvatarLinkNowMs();
    g_placeParked = true;
}

// Host: a grab call on the copy is about to be queued: a place parked in the same script step goes first, in the
// script's order; one from an earlier step that no grab took stays ours (an RC's script event runs in our frame
// start, before GrabSyncFrame drops it).
void QueueParkedPlace() {
    if (!g_placeParked) return;
    g_placeParked = false;
    if (AvatarLinkNowMs() - g_parkedPlaceMs > kStepMs) return;
    Queue(g_parkedPlace);
    if (Logs())
        Log("grab sync: %s's place for the copy (%.0f, %.0f, %.0f), made as its grab starts, sent to the friend ahead of "
            "the grab (%u so far)", Model(g_parkedPlaceBy), g_parkedPlace.v[0], g_parkedPlace.v[1], g_parkedPlace.v[2],
            ++g_scriptPlaces);
}

// Host: a die / card call while a boss's script runs with the copy as the player: the friend's (see the top).
bool IsPlayerFormCall(int table, int index) {
    return (table == 1 && (index == 242 || index == 243)) || (table == 6 && index >= 27 && index <= 30);
}

std::uint32_t g_playerCalls = 0;
bool HoldPlayerCall(int table, int index, const std::uint64_t* args) {
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy || Read<std::uintptr_t>(ExeBase() + kPlayerPtr) != copy) return false;  // ours: runs as usual
    std::uintptr_t by = WorldCensusScriptActor();
    Event e{};
    e.op = kOpPlayerCall;
    e.a = table;
    e.b = index;
    std::uint16_t instance = 0;
    e.netId = by ? WorldSyncNetIdOf(by, &instance) : 0;
    e.instance = instance;
    int argc = table == 1 ? (index == 242 ? 1 : 0) : index == 28 ? 4 : index == 30 ? 7 : 0;
    for (int i = 0; i < argc; ++i) e.args[i] = static_cast<std::uint32_t>(args[i]);
    Queue(e);
    ++g_heldBack;
    ++g_playerCalls;
    Log("grab sync: %s's %s(%u, ...) with the copy as the player: not run in our game, sent to the friend for its "
        "Sora (%u so far)", Model(by), WorldCensusTrapName(table, index), e.args[0], g_playerCalls);
    return true;
}

// Friend (TODO 1.113, your pick 2026-11-06): an enemy the host runs grabs no Sora with its own script in our game. One
// brain stops only its brain step; its motions' frame-triggered callbacks still run here (L187), and Data Final
// Xemnas's captured and hooked the host's copy in our game alone, then crashed (F369, F445). The host's grabs reach our
// Sora through this file's direct calls, not through scripts. Refused: capture (grabber, actor, ...), hook (actor, pc,
// owner) and attach (actor, parent, ...) when our Sora or the copy is the actor or the parent.
bool RefuseOwnGrab(int index, const std::uint64_t* args) {
    std::uintptr_t by = WorldCensusScriptActor();
    if (!by || !WorldSyncSkipsBrain(by)) return false;
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead), copy = PuppetCloneActor();
    auto isSora = [&](std::uintptr_t a) { return a && (a == sora || a == copy); };
    std::uintptr_t a = ActorArg(args[index == 29 ? 1 : 0]), b = index == 18 ? ActorArg(args[1]) : 0;
    std::uintptr_t who = isSora(a) ? a : isSora(b) ? b : 0;
    if (!who) return false;
    if (g_scriptGrabsRefused++ < 20) {
        // Which script thread made the call (TODO 1.197: a motion trigger's one-shot thread, not the root? L337).
        std::uintptr_t thread = WorldCensusScriptThread(), root = 0, block = 0;
        ReadAny(by + 0x5B0, &root);  // its root script thread (F82)
        if (thread) ReadAny(thread + 0x30, &block);
        Log("grab sync: our %s's own %s of %s not run: the host runs that enemy, its grabs come from the host (%u so far; "
            "script thread %p%s, its stack block %p %s)", Model(by), index == 29 ? "capture" : index == 112 ? "hook" : "attach",
            who == copy ? "the host's copy" : "our Sora", g_scriptGrabsRefused, reinterpret_cast<void*>(thread),
            thread && thread == root ? " = its root" : "", reinterpret_cast<void*>(block),
            block && KnownArea(block, 0x100) ? "in a known area" : "not in a known area");
    }
    // TODO 1.197 (your pick 'Stop it entirely'): nor does the rest of that run. A motion trigger's one-shot thread went
    // on to set flag 8, idle and the beam threads on the Sora it couldn't hook, none of which the host's game ended
    // (F579-F585). Not on attach (no case seen). Its own log lines, outside the 20 above.
    if (index != 18 && g_scriptGrabEnd) {
        const char* why = "";
        bool ended = WorldCensusEndScriptThread(by, &why);
        std::uint32_t n = ended ? ++g_scriptRunsEnded : ++g_scriptRunsKept;
        if (n <= 60)
            Log("grab sync: our %s's script run %s at its refused %s (%u so far)%s%s", Model(by),
                ended ? "ended" : "goes on", index == 29 ? "capture" : "hook", n, ended ? "" : ": ", why);
    }
    return true;
}

namespace {

// A boss's script protects 'its player' for a sequence (flag 8 = bit 8 of actor+0x120: no capture, trap_obj_is_star
// answers 1) and unprotects 'its player' at the end. With two Soras the player it is handed the second time can be
// the other one (aggro.cpp's swap follows the fight), so the first stayed protected and the script's next wait on it
// never ended: Pete stood frozen at his next scenery change (bench pete_curtain, your look, 2026-11-15; F228's
// Luxord reads the same). When a script unprotects one Sora, the other one it protected is unprotected too.
// Kept across frames: a Sora's address, written only while it is still our Sora or the copy in the same room load.
constexpr std::uintptr_t kFlagBits = 0x120;
constexpr std::uint32_t kProtectedBit = 1u << 8;
struct Protected {
    std::uintptr_t by, sora;
    std::uint16_t load;
};
Protected g_protected[4];

void NoteProtect(bool set, std::uintptr_t sora) {
    std::uintptr_t by = WorldCensusScriptActor(), ours = 0, copy = PuppetCloneActor();
    std::uint16_t load = 0;
    WorldSyncNetIdOf(0, &load);
    if (!ReadAny(ExeBase() + kListHead, &ours) || !by || !sora || by == sora || (sora != ours && sora != copy)) return;
    bool listed = false;
    for (Protected& p : g_protected) {
        if (p.by != by) continue;
        if (p.load != load) {
            p = {};
        } else if (set) {
            listed |= p.sora == sora;
        } else {
            std::uintptr_t other = p.sora;
            std::uint32_t bits = 0;
            p = {};
            if (other == sora || (other != ours && other != copy) || !ReadAny(other + kFlagBits, &bits) ||
                !(bits & kProtectedBit))
                continue;
            *reinterpret_cast<std::uint32_t*>(other + kFlagBits) = bits & ~kProtectedBit;
            static std::uint32_t logs = 0;
            if (logs++ < 40)
                Log("grab sync: %s's script unprotects %s; it had protected %s too (flag 8): unprotected as well",
                    Model(by), sora == copy ? "the Sora copy" : "our Sora", other == copy ? "the Sora copy" : "our Sora");
        }
    }
    if (!set || listed) return;
    static unsigned next = 0;
    Protected* slot = &g_protected[next++ % 4];
    for (Protected& p : g_protected)
        if (!p.by) slot = &p;
    *slot = {by, sora, load};
}

}  // namespace

bool GrabSyncHolds(int table, int index, const std::uint64_t* args) {
    if (table == 1 && (index == 70 || index == 71) && static_cast<std::int32_t>(args[1]) == 8 && CoopActive())
        NoteProtect(index == 70, ActorArg(args[0]));
    // Either game: a capture (1/29) whose grabber names no live actor would read 0 + 0x158 at exe+0x3C7C6F (the
    // friend's Guard on a Large Body, TODO 1.142): skipped, so that move does nothing instead of crashing.
    if (table == 1 && index == 29 && CoopActive() && !ActorArg(args[0])) {
        static std::uint32_t skipped = 0;
        if (skipped++ < 20)
            Log("grab sync: %s's capture (motion %d) of %s skipped: its grabber %08X names no live actor (the game "
                "would crash on it; %u so far)", Model(WorldCensusScriptActor()), static_cast<int>(args[2]),
                ActorArg(args[1]) == PuppetCloneActor() ? "the copy" : Model(ActorArg(args[1])),
                static_cast<std::uint32_t>(args[0]), skipped);
        return true;
    }
    if (!g_host && g_scriptGrabGuard && table == 1 && (index == 29 || index == 112 || index == 18) && CoopActive() &&
        RefuseOwnGrab(index, args))
        return true;
    if (!g_on || !g_host || !CoopActive()) return false;
    if (table == 1 && (index == 42 || index == 43)) {  // func_obj_control_off / _on by a script (the control-off window)
        std::uintptr_t by = WorldCensusScriptActor();
        ClearVictimsOnRoomChange();
        if (index == 42 && by && by != g_controlOffBy) {
            g_controlOffBy = by;
            g_controlOffTick = GetTickCount64();
            Log("grab sync: %s's script turns our control off: it holds the player it hooked until its control_on",
                Model(by));
        } else if (index == 43 && by && by == g_controlOffBy) {
            g_controlOffBy = 0;
            Log("grab sync: %s's script turns our control on", Model(by));
        }
    }
    if (IsPlayerFormCall(table, index)) {
        bool held = HoldPlayerCall(table, index, args);
        if (!held && table == 1 && index == 242) StandInOurCapture();  // our own Sora lost: the friend shows it
        if (!held && table == 1 && index == 243 && !StandInWeAreCaptured()) {  // our down ended it (downed.cpp)
            Log("grab sync: %s's capture end on our Sora left out: it isn't captured any more",
                Model(WorldCensusScriptActor()));
            return true;
        }
        return held;
    }
    if (table == 0 && (index == 31 || index == 32)) {  // func_screen_whiteout / whitein
        std::uintptr_t by = WorldCensusScriptActor(), copy = PuppetCloneActor();
        if (!g_grab && !FriendsScript(by)) {
            // An enemy's flash while its script has the copy as the player: kept for its camera (ParkedFlash).
            if (!g_holdCamera || !copy || Read<std::uintptr_t>(ExeBase() + kPlayerPtr) != copy || !EnemysScript(by) ||
                g_flashCount == 4) {
                NoteOurFlash(by, index);
                return false;
            }
            g_flashes[g_flashCount++] = {by, args[0], index, AvatarLinkNowMs(), Model(by)};
            return true;
        }
        QueueFlash(index, args[0]);
        if (!g_holdCamera || OurWhitein(by, index)) {
            if (g_holdCamera && Logs())
                Log("grab sync: %s's whitein sent to the friend and run in our game too: its whiteout ran here", Model(by));
            NoteOurFlash(by, index);
            return false;
        }
        ++g_heldBack;
        return true;
    }
    if (table != 1) return false;
    if (index == 36 || index == 37 || index == 148) {
        if (!ArenaCall(index, args) && index == 148) ScriptPlace(args);  // never both for one call
        return false;
    }
    // trap_camera_reset, or trap_camera_cancel(flag) (Beast 05/03 and Data Luxord end their scene camera with it: the
    // friend's camera stayed, TODO 1.120 / 1.170), by the one whose camera went to the friend, or by a friend's script
    if (index == 295 || index == 223) {
        std::uintptr_t by = WorldCensusScriptActor();
        if (!by || (by != g_cameraBy && !FriendsScript(by))) return false;
        Event e{};
        std::uint16_t instance = 0;
        e.netId = WorldSyncNetIdOf(by, &instance);
        e.instance = instance;
        e.op = index == 295 ? kOpCameraReset : kOpCameraCancel;
        e.a = static_cast<std::int32_t>(args[0]);
        Queue(e);
        if (Logs())
            Log("grab sync: %s's %s sent to the friend%s", Model(by), kOpNames[e.op],
                by == g_bothBy ? " (run in our game too)" : g_holdCamera ? " (not run in our game)" : "");
        bool both = by == g_bothBy;
        g_cameraBy = g_bothBy = 0;
        if (!g_holdCamera || both) return false;
        ++g_heldBack;
        return true;
    }
    if (index == 207) {  // trap_camera_watch(actor) by the grabber holding the copy, or by a friend's script
        std::uintptr_t by = WorldCensusScriptActor(), watched = ActorArg(args[0]), copy = PuppetCloneActor();
        if (!by || !watched || !copy || !((g_grab && by == g_grabber) || FriendsScript(by))) return false;
        Event e{};
        e.op = kOpWatch;
        e.a = watched == copy ? 1 : watched == Read<std::uintptr_t>(ExeBase() + kListHead) ? 2 : 0;
        if (!e.a) {
            std::uint16_t instance = 0;
            e.netId = WorldSyncNetIdOf(watched, &instance);
            e.instance = instance;
            if (!e.netId) return false;  // the friend's game has no such actor: the watch stays ours
        }
        Queue(e);
        g_cameraBy = by;
        if (Logs())
            Log("grab sync: %s's camera watch on %s sent to the friend%s", Model(by), e.a == 1 ? "the copy" : Model(watched),
                g_holdCamera ? " (not run in our game)" : "");
        if (!g_holdCamera) return false;
        ++g_heldBack;
        return true;
    }
    if (index == 29) NoteVictim(ActorArg(args[0]), ActorArg(args[1]));
    if (index == 112) NoteVictim(WorldCensusScriptActor(), ActorArg(args[0]), true);  // trap_obj_hook(actor, pc, owner)
    int target;  // the argument that is the grabbed actor
    switch (index) {
        case 74: case 153: case 79: case 102: case 220: case 146: target = 0; break;  // idle, reset_pos, set_dir,
                                                                                     // detach, move_to_space, hitback
        case 179: case 180: target = 0; break;        // set_fall_motion, set_land_motion (actor, motion)
        case 55: case 128: target = 0; break;         // method_obj_jump (actor, speed, motion, f, flag), sysjump
        case 29: case 18: target = 1; break;          // motion_capture, attach (grabber first)
        case 157: case 219: target = 2; break;        // camera_start (owner, name, target)
        case 164: target = -1; break;                 // camera_start_global (owner, name)
        default: return false;
    }
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) return false;
    std::uintptr_t by = WorldCensusScriptActor();
    if (!EnemysScript(by)) return false;  // only enemies', field objects' and NPCs' scripts
    // A grabber taking itself off the copy (Cerberus at its '20' shot, F489): ours comes off our Sora then too, not at
    // the let go, or world sync drags it along with our thrown Sora (Data Roxas's steal now too).
    std::uintptr_t off = index == 102 ? ActorArg(args[0]) : 0;
    bool offCopy = off && off != copy && DecodeHandle(Read<std::uint32_t>(off + kAttachedTo)) == copy;
    // A scene camera on the grabber itself (or on nothing) counts while that grabber holds the copy.
    bool ownCamera = target == 2 && g_grab && by == g_grabber && ActorArg(args[0]) == by &&
                     (ActorArg(args[2]) == by || !ActorArg(args[2]));
    // The copy put on its grabber's bone, the other way round (the steamboat's hook: trap_obj_attach(copy, F_WI390,
    // 23, 0, 0, 0)): sent too, or the friend's Sora only follows by place, falls under its own gravity and its game
    // swaps the hang motion for the fall one (your look, 2026-11-15).
    bool copyOn = index == 18 && ActorArg(args[0]) == copy;
    bool onCopy = target < 0 ? g_grab : ActorArg(args[target]) == copy || copyOn;
    // A camera by a script the friend's command or grab made (the chandelier's on itself, F389): the friend's too.
    bool camera = index == 157 || index == 219 || index == 164;
    bool friends = camera && !ownCamera && !onCopy && FriendsScript(by);
    // An enemy's camera on itself (or on nothing) while its script has the copy as the player, grab or not: Cerberus
    // starts its grab's camera before it hooks the copy (F485), and your pick 2026-11-10 (TODO 1.179) sends such a
    // camera to the friend even when no grab follows. Not 1/164: Data Luxord's arena change starts one and his script
    // waits for it with trap_event_is_exec (~180 frames in a host log), so it stays here.
    bool forCopy = target == 2 && !g_grab && !onCopy && !friends && ActorArg(args[0]) == by &&
                   (ActorArg(args[2]) == by || !ActorArg(args[2]));
    // A camera of the exception table: both games, whichever Sora its script aims at.
    char cameraName[8] = {};
    bool both = false;
    if (target == 2 && ActorArg(args[0]) == by && ReadName(args[1], cameraName))
        for (const BothCamera& row : kBothCameras)
            both |= !std::strcmp(Model(by), row.model) && !std::strcmp(cameraName, row.name);
    if (forCopy && Read<std::uintptr_t>(ExeBase() + kPlayerPtr) != copy) {
        forCopy = false;
        if (!both && g_ownPlayerCameras++ < 20)
            Log("grab sync: %s's own scene camera left in our game: its player is our Sora (%u so far)", Model(by),
                g_ownPlayerCameras);
    }
    // Its camera runs here now: the flashes before it are ours, and so is its next reset.
    auto stayHere = [&] {
        RunParkedFlashes(by);
        if (by == g_cameraBy) g_cameraBy = 0;
        return false;
    };
    bool bothOnly = false;  // sent for that reason alone: it is nobody's grab
    if (!ownCamera && !onCopy && !friends && !forCopy && !offCopy) {
        if (!both) return camera ? stayHere() : false;
        RunParkedFlashes(by);
        bothOnly = true;
    }
    if ((index == 146 || index == 179 || index == 180) && !g_grab) {  // a push or landing only while held
        if (index != 146 && onCopy)
            NoteEarlyMotion(by, index == 179 ? kOpFallMotion : kOpLandMotion, static_cast<std::int32_t>(args[1]));
        return false;
    }
    // A field object's launch of the copy (TODO 1.193, your pick B: the scaffold F_WI360's hook thread, L317): its
    // flag-1 jump goes to the friend with no grab, so no held places and no let go cut the flight (a flag-1 jump
    // lands by itself, F412; a flag-0 one needs the let go to leave its motion 3, F413).
    bool launch = false;
    if (index == 55 && !g_grab && onCopy && Read<std::uint32_t>(by + kTeam) == 3) {
        std::uintptr_t hook = Read<std::uintptr_t>(copy + kHook);
        std::uint32_t flag = static_cast<std::uint32_t>(args[4]);
        launch = flag && hook;
        if (launch) g_launchBy = by, g_launchFrame = g_frame;  // from the jump seen, not its send (aggro waits on it)
        if (g_launchLogs++ < 20)
            Log("grab sync: %s's jump on the copy (flag %u, the copy %s): %s", Model(by), flag,
                hook ? "hooked" : "not hooked", launch ? "a launch, sent with no grab (TODO 1.193)" : "not sent");
    }
    if ((index == 55 || index == 128) && !g_grab && !launch && GrabSyncLastHooked(by) != 2) return false;  // the hooker's jumps
    Event e{};
    bool hold = false;
    std::uintptr_t grabber = offCopy ? off : copyOn ? ActorArg(args[1]) : target >= 1 || index == 164 ? ActorArg(args[0]) : by;
    std::uint16_t instance = 0;
    e.netId = WorldSyncNetIdOf(grabber, &instance);
    e.instance = instance;
    if ((forCopy || bothOnly) && !e.netId) return stayHere();  // the friend's game has no such enemy to run it on
    if (offCopy && !e.netId) return false;
    switch (index) {
        case 74:
            e.op = kOpIdle;
            break;
        case 29:  // (grabber, actor, motion, blend)
        case 18:  // (grabber, actor, a, b): the grabber goes on the actor
            e.op = index == 29 ? kOpCapture : kOpAttach;
            e.a = static_cast<std::int32_t>(args[2]);
            e.b = static_cast<std::int32_t>(args[3]);
            e.raw = copyOn ? 1 : 0;  // attach: 1 = the copy goes on the grabber (its last two arguments, 0 here, aren't sent)
            break;
        case 153:
        case 79:
            e.op = index == 153 ? kOpPos : kOpDir;
            if (!ReadVec(args[1], e.v)) return false;
            break;
        case 102:
            e.op = kOpDetach;
            e.b = offCopy ? 1 : 0;
            break;
        case 220:  // (actor, float, flag): a = the float's bits; the game reads only the flag's low 32 bits
            e.op = kOpMoveToSpace;
            e.a = static_cast<std::int32_t>(args[1]);
            e.b = static_cast<std::uint32_t>(args[2]) != 0 ? 1 : 0;
            break;
        case 179:
        case 180:  // (actor, motion): the game reads the motion's low 32 bits
            e.op = index == 179 ? kOpFallMotion : kOpLandMotion;
            e.a = static_cast<std::int32_t>(args[1]);
            break;
        case 55:  // (actor, speed, motion, float, flag): a = motion, b = the float's bits, raw = the flag
            e.op = kOpJump;
            if (!ReadVec(args[1], e.v)) return false;
            e.a = static_cast<std::int32_t>(args[2]);
            e.b = static_cast<std::int32_t>(args[3]);
            e.raw = static_cast<std::uint32_t>(args[4]);
            break;
        case 128:  // (actor, kind)
            e.op = kOpSysJump;
            e.a = static_cast<std::int32_t>(args[1]);
            break;
        case 146:  // (actor, vector, strength): a = the strength's low 32 bits, as the game reads them
            e.op = kOpHitback;
            if (!ReadVec(args[1], e.v)) return false;
            e.a = static_cast<std::int32_t>(args[2]);
            break;
        default:  // the cameras; v = where the owner is drawn, for the friend's log (F486)
            e.op = kOpCamera;
            e.b = target != 2 ? 0 : ActorArg(args[2]) == copy ? 1 : ActorArg(args[2]) ? 2 : 0;
            if (!ReadName(args[1], e.name)) return false;
            if (grabber) std::memcpy(e.v, reinterpret_cast<const void*>(grabber + 0x70), 12);
            if (grabber) e.v[3] = DrawnAngle(reinterpret_cast<const float*>(grabber + 0x40));  // research: its facing
            hold = g_holdCamera && !both;
            g_cameraBy = grabber;
            g_bothBy = both ? grabber : 0;
            SendParkedFlashes(by);  // its flash came first (Cerberus)
            break;
    }
    // A friend's script's camera alone isn't a grab, nor is one the enemy starts for the copy as its player.
    if (e.op != kOpDetach && !friends && !forCopy && !launch && !bothOnly) {
        // its place made just before, as the grab takes the copy (a capture or attach, or the copy hooked): first
        if (index == 29 || index == 18 || StillGrabbed(copy)) QueueParkedPlace();
        StartGrab(grabber);
    }
    Queue(e);
    if (hold) ++g_heldBack;
    if (Logs())
        Log("grab sync: %s's %s %s sent to the friend%s", Model(grabber), kOpNames[e.op],
            forCopy ? "with the copy as its player" : offCopy ? "off the copy" : bothOnly ? "for both games"
            : "on the copy", hold ? " (not run in our game)" : both ? " (run in our game too: kBothCameras)" : "");
    return hold;
}

// Host: `actor` launched the copy less than a second ago (TODO 1.193). Kept rule: only the last launcher's address,
// for 60 frames, compared with the object being run; a new object at that address within that second only waits too.
bool GrabSyncCopyLaunchedBy(std::uintptr_t actor) {
    return g_on && g_host && actor && actor == g_launchBy && g_frame - g_launchFrame < 60;
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

int GrabSyncHolding(std::uintptr_t enemy) {
    if (!g_on || !g_host || !enemy) return 0;
    __try {
        ClearVictimsOnRoomChange();
        for (const Victim& v : g_victims) {
            if (v.grabber != enemy) continue;
            std::uintptr_t player = v.copy ? PuppetCloneActor() : Read<std::uintptr_t>(ExeBase() + kListHead);
            if (!player) return 0;
            if (Read<std::uintptr_t>(player + kHook) != 0) return v.copy ? 2 : 1;
            // Its hook ended, a capture it started still plays (F749): it holds that player until the capture ends.
            if (g_capturedBy[v.copy] == enemy && (Read<std::uint32_t>(player + kFlags18C) & kCaptured)) {
                if (GetTickCount64() - g_heldByCaptureLog >= 1000 && (g_heldByCaptureLog = GetTickCount64()))
                    Log("grab sync: %s holds %s by its capture (hook ended, motion %d)", Model(enemy),
                        v.copy ? "the friend's copy" : "our Sora", Read<std::int32_t>(player + game::kActorMotion));
                return v.copy ? 2 : 1;
            }
            // Its script has our control off (func_obj_control_off): still its sequence on our Sora, whatever fills
            // the player slot now (Data Luxord's die, F752, F753).
            // The copy too: its penalty's control-off runs in our game (case B, bench 2026-11-20).
            if (g_controlOffBy == enemy && ControlStillOff()) {
                if (GetTickCount64() - g_heldByControlLog >= 1000 && (g_heldByControlLog = GetTickCount64()))
                    Log("grab sync: %s holds %s: its script has our control off (frozen groups %X)", Model(enemy),
                        v.copy ? "the friend's copy" : "our Sora", Read<std::uint32_t>(ExeBase() + game::kFrozenGroups));
                return v.copy ? 2 : 1;
            }
            return 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

bool GrabSyncHoldsPlayer(std::uintptr_t enemy) { return GrabSyncHolding(enemy) != 0; }

bool GrabSyncCopyHookedBy(std::uintptr_t actor) {
    if (!g_on || !g_host || !actor || !g_grab || g_grabber != actor) return false;  // a launch's hook is no grab
    std::uintptr_t copy = PuppetCloneActor(), thread = 0, owner = 0;
    return copy && ReadAny(copy + kHook, &thread) && thread && ReadAny(thread + 0x60, &owner) && owner == actor;  // +0x60: a thread's owner (F670)
}

bool GrabSyncWeaponThrown(std::uintptr_t weapon, std::uintptr_t holder, float out[8], std::int32_t* motion) {
    __try {
        if (!weapon || !holder || DecodeHandle(Read<std::uint32_t>(weapon + kAttachedTo)) == holder) return false;
        std::memcpy(out, reinterpret_cast<const void*>(weapon + 0x640 + 0x30), 8 * sizeof(float));
        if (motion) *motion = Read<std::int32_t>(weapon + 0x180);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Kept: the weapon we took off and the bone it was on (+0x6A4, F243); only used for that same address while it is
// still off its holder, forgotten when it is back or another weapon is passed.
struct ThrownWeapon {
    std::uintptr_t weapon;
    std::int32_t bone;
};
ThrownWeapon g_thrown = {};

void GrabSyncThrownWeapon(std::uintptr_t weapon, std::uintptr_t holder, const float* place, const float* rot,
                          std::int32_t motion) {
    std::uintptr_t exe = ExeBase();
    __try {
        bool on = DecodeHandle(Read<std::uint32_t>(weapon + kAttachedTo)) == holder;
        if (!place) {
            if (!on && g_thrown.weapon == weapon) {
                reinterpret_cast<PFN_Attach>(exe + kAttach)(weapon, holder, g_thrown.bone, 0);
                Log("grab sync: our copy's thrown %s is back on its bone %d", Model(weapon), g_thrown.bone);
            }
            g_thrown = {};
            return;
        }
        if (on) {
            g_thrown = {weapon, Read<std::int32_t>(weapon + kAttachedTo + 4)};
            reinterpret_cast<PFN_Actor>(exe + kDetach)(weapon);
            Log("grab sync: the other player threw its %s: ours off the copy's bone %d, its motion %d, ours %d",
                Model(weapon), g_thrown.bone, motion, Read<std::int32_t>(weapon + 0x180));
        } else if (g_thrown.weapon != weapon) {
            return;
        }
        std::memcpy(reinterpret_cast<void*>(weapon + 0x640 + 0x30), place, 3 * sizeof(float));
        std::memcpy(reinterpret_cast<void*>(weapon + 0x640 + 0x40), rot, 4 * sizeof(float));
        if (motion >= 0 && Read<std::int32_t>(weapon + 0x180) != motion)
            reinterpret_cast<PFN_MotionStep>(exe + kMotionStep)(weapon + 0x158, motion, 0.0f, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The game's own ride calls, made by our game on its board and the copy: the board's script attaches the board to its
// rider (trap_obj_attach(board, rider, 16386, 0)) and every ride or trick motion N of the rider comes with motion
// N - 0x65 on the board (exe+0x53BC70, exe+0x53BE50; L347). Letting go: detached and back in motion 0, as its script does.
void GrabSyncShowRide(std::uintptr_t board, std::uintptr_t rider, int motion) {
    __try {
        std::uintptr_t exe = ExeBase();
        bool on = DecodeHandle(Read<std::uint32_t>(board + kAttachedTo)) == rider;
        if (motion < 0) {
            if (!on) return;
            reinterpret_cast<PFN_Actor>(exe + kDetach)(board);
            reinterpret_cast<PFN_MotionStep>(exe + kMotionStep)(board + 0x158, 0, 0.0f, 0.0f);
            return;
        }
        if (!on) reinterpret_cast<PFN_Attach>(exe + kAttach)(board, rider, 16386, 0);
        if (motion >= 0x65) reinterpret_cast<PFN_MotionStep>(exe + kMotionStep)(board + 0x158, motion - 0x65, 0.0f, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool GrabSyncOurSoraHeld() {
    return g_on && !g_host && g_captureFrame > g_releaseFrame && g_frame - g_captureFrame < kMaxFrames;
}

bool GrabSyncOurSoraHeldBy(std::uint16_t netId, std::uint16_t instance) {
    return netId && netId == g_capturedNet && instance == g_capturedInstance && GrabSyncOurSoraHeld();
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
        Event e;  // the calls come once each, in order (reliable grab stream)
        std::memcpy(&e, buf + sizeof(Packet) + i * sizeof(Event), sizeof(e));
        if (e.op == kOpHeldPlace) AddHeldPlace(e);
        else if (g_inboxCount < 64) g_inbox[g_inboxCount++] = {e, g_frame, 0};
    }
}

void GrabSyncAfterUpdate(std::uintptr_t actor) {
    if (!g_on || g_host || !actor || actor != Read<std::uintptr_t>(ExeBase() + kListHead)) return;
    if (g_grabPoseLeft) {  // this update drew our Sora's own pose: later blends start from it
        g_grabPoseLeft = false;
        if (Logs()) Log("grab sync: our Sora's first update out of the grab pose: its motions blend again");
    }
    if (g_cutMs && !g_placeCount) {  // a script place and no held stream after it (the link down): not kept forever
        std::uint32_t target = AvatarLinkPlaybackSenderMs();
        if (target && static_cast<std::int32_t>(target - g_cutMs) > static_cast<std::int32_t>(kStreamEndMs))
            EndStream("a script place with no held place for 2 s");
    }
    if (!g_placeCount) return;
    FollowHeldStream(actor);
}

void GrabSyncOnScene(std::uintptr_t script, std::uintptr_t base, std::uintptr_t owner80) {
    if (!g_on || !script || g_shotLogs >= 200) return;
    const char* name = reinterpret_cast<const char*>(script + 10);
    if (std::strncmp(name, "re_", 3) != 0) return;
    ++g_shotLogs;
    std::snprintf(g_shotName, sizeof(g_shotName), "%.20s", name);
    g_shotBase = base;
    g_shotFrame = g_frame;
    char m[128] = "none", owner[96], player[96];
    if (base) Matrix(m, sizeof(m), base);
    Pose(owner, sizeof(owner), owner80 ? owner80 - 0x80 : 0);
    Pose(player, sizeof(player), Read<std::uintptr_t>(ExeBase() + kPlayerPtr));
    Log("shot research: '%s' starts in the %s's game: base %s; its owner %s; the player %s", g_shotName,
        g_host ? "host" : "friend", m, owner, player);
}

void GrabSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    if (g_shotBase) {  // research: the running shot's camera (GrabSyncOnScene)
        std::uint64_t n = g_frame - g_shotFrame;
        if (n == 1 || n == 10 || n == 30 || n == 60) LogShotFrame(n);
        if (n >= 60) g_shotBase = 0;
    }
    if (g_carryWait.rider) KeepRiderOn();  // both games (HoldOn)
    if (g_host) {
        g_placeParked = false;  // a place parked last frame that no grab call took: the copy wasn't grabbed
        ReleaseFlashes();
        std::uintptr_t copy = PuppetCloneActor();
        if (g_grab) {
            Trace(copy);
            bool held = copy && StillGrabbed(copy);
            if (held) SendHeldPlace(copy);
            if (held) {
                // A short free gap inside one grab (a chain like grab -> second grab): with a short quiet wait, a gap
                // as long as it would send the let go too early. Logged to check kQuietFrames against real chains.
                if (g_freeSince && Logs())
                    Log("grab sync: the copy was free for %llu frames, then held again (let go after %llu)",
                        static_cast<unsigned long long>(g_frame - g_freeSince),
                        static_cast<unsigned long long>(kQuietFrames));
                g_freeSince = 0;
            } else if (!g_freeSince) {
                g_freeSince = g_frame;
            }
            if ((g_freeSince && g_frame - g_freeSince >= kQuietFrames && g_frame - g_lastOpFrame >= kQuietFrames) ||
                g_frame - g_lastOpFrame > kMaxFrames || !copy) {  // from its last call: a hang kept up with Hold On ran 35 s
                g_grab = false;
                Event e{};
                e.op = kOpRelease;
                Queue(e);
                AggroBossActedOn(g_grabber, true, "let go of");
                if (Logs())
                    Log("grab sync: the copy is free again after %.1f s: 'let go' sent to the friend (held places sent "
                        "so far %u)", (g_frame - g_grabFrame) / 60.0, g_placesSent);
            }
        }
        SendQueued();
    } else {
        // Each event at its playback time, as the grabber's pose and our Sora's place (see "Held stream" at the top). The
        // arena place and the player calls at once: the friend's game swaps its own parts, and they move no body.
        std::uint32_t target = AvatarLinkPlaybackSenderMs();
        int kept = 0;
        bool waiting = false;  // a scene camera waits for its owner: the events after it wait too (the host's order)
        bool ran = false;      // the event before this one ran in this pass, at lastMs on the host's clock
        std::uint32_t lastMs = 0;
        g_callsWaitMs = 0;
        for (int i = 0; i < g_inboxCount; ++i) {
            Inbound& in = g_inbox[i];
            // One script step's calls run together: a script's place and the capture after it in two frames would draw
            // our Sora for a frame at the new place in its old grab pose (F568).
            bool step = g_placeSync && ran && !kept &&
                        static_cast<std::int32_t>(in.e.ms - lastMs) <= static_cast<std::int32_t>(kStepMs);
            bool now = !waiting && (step || in.e.op == kOpArenaPlace || in.e.op == kOpPlayerCall || !target ||
                                    static_cast<std::int32_t>(target - in.e.ms) >= 0 ||
                                    g_frame - in.frame >= kMaxWaitFrames);
            if (now && in.e.op == kOpCamera && !ShotReady(in)) {
                now = false, waiting = true;
                if (g_placeSync) g_callsWaitMs = in.e.ms ? in.e.ms : 1;  // the held stream's start waits too
            }
            if (now) {
                Apply(in.e);
                std::uintptr_t sora = 0;
                if (ReadAny(ExeBase() + kListHead, &sora)) ClearMissingCaptureMotions(sora);
                ran = in.e.op != kOpRelease, lastMs = in.e.ms;  // a grab after a let go waits for its own time
            } else {
                g_inbox[kept++] = in;
                ran = false;
            }
        }
        g_inboxCount = kept;
        if (g_releaseTrace) {  // research: where our Sora is drawn and placed, frame by frame, after a release
            --g_releaseTrace;
            std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
            if (sora) {
                const float* m = reinterpret_cast<const float*>(sora + 0x40);
                const float* p = reinterpret_cast<const float*>(sora + 0x640 + 0x30);
                Log("release trace %u: our Sora drawn (%.0f, %.0f, %.0f), place (%.0f, %.0f, %.0f), motion %d, grab "
                    "motion %d", 12 - g_releaseTrace, m[12], m[13], m[14], p[0], p[1], p[2],
                    Read<std::int32_t>(sora + 0x180), (Read<std::uint32_t>(sora + kFlags18C) & kCaptured) ? 1 : 0);
            }
        }
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
            "%u not run; %u fall / land motions cleared (not in the capture source)", g_grabs, g_sent, g_heldBack, g_run,
            g_skipped, g_captureMotionsCleared);
    }
}

}  // namespace kh2coop
