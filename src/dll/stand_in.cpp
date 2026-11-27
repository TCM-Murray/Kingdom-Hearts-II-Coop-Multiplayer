// The other player's die or card on their copy (TODO 1.90 step 3; your choice 2026-10-17: a still die now).
//
// When Data Luxord's card game turns a player into the die or a card, that player's own game swaps its player for
// the die's body (trap_player_capture_form; grab_sync.cpp runs the calls there). The other game kept showing the copy
// as Sora. The pose packet now says which body the player wears (its object id) and whether a boss's capture has it:
// each game sets that from the capture call on its own player (1/242, the call behind the die and the cards) until
// its body is back to the one before. Your pick 2026-10-17 (option C): by the game's capture, not by the body's name,
// so a ride or a Drive Form can never trigger it (the form byte stays 0 during the die, your two-PC run 22:41, so it
// can't be used either). While the peer is captured in a body other than the copy's, the copy is hidden
// (trap_obj_hide) and that body stands in on the copy's spot and facing.
// The body is made with the actor factory as a cutscene actor (object | 0x40000000): made as itself it is a player
// and takes our player pointer, camera and HUD, and setting those back froze the game (VERIFIED_OFFSETS rows of
// 2026-10-17). A cutscene actor has no motion set, so the stand-in is a still die that follows the copy (no roll).
// Only captures, not Drive Forms: those bodies' files are loaded only in the game that drove into them, and
// the factory crashes without them. A capture's object belongs to the room's boss, so both games have it loaded.
// One body per room is kept and hidden between uses (no removal route for cutscene actors is known); the room's
// unload frees it.
// The World Map (TODO 5.25, your picks 2026-11-20: both ways, the same model): there is no copy there, so while both
// players fly the map the other player's ship is a stand-in too, made from our own ship's object (so its files are
// loaded) and placed from their pose each frame. It keeps the game's collision (your pick 2026-11-20: collision if
// possible): placed each frame it isn't pushed, so it pushes our ship, in each game. Kept across frames by the same
// rule: in the game's actor list with the object it was made as, and forgotten at the load that leaves the map.
// A cutscene actor has no motion file of its own (F761, F766): it borrows our own ship's and plays the other player's
// motion (F764) with the game's own motion call, its effects from our ship's PAX. Placed after its own update (F765).
// STAND_IN=0 in kh2coop.ini turns it off.

#include "stand_in.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "copy_blade.hpp"
#include "downed.hpp"
#include "form_look.hpp"
#include "game.hpp"
#include "grab_sync.hpp"
#include "solo_copy.hpp"
#include "spawn_sync.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
void PlayFrom(std::uintptr_t actor, std::uintptr_t ours, std::int32_t motion, float blend, bool& fxTried, bool& fx,
              std::uint32_t& logs, const char* what);

namespace {

constexpr std::uintptr_t kFactory = 0x3DF930;         // (object id, float pos[4], yaw) -> actor or 0
constexpr std::uint32_t kSceneActor = 0x40000000;     // factory: build the object as a cutscene actor
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;    // (object) -> handle
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // "@ADR": a script argument naming an actor
constexpr std::uintptr_t kObjRow = 0x918;             // actor -> its object table row (u32 object id at +0)
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kNoCollideAt = 0x18C;  // u8, bit 6 = skip actor / terrain collision (puppet.cpp)
constexpr std::uint8_t kNoCollide = 0x40;
constexpr std::uintptr_t kEntityPos = 0x30;           // f32 x, y, z, w
constexpr std::uintptr_t kEntitySin = 0x40;
constexpr std::uintptr_t kEntityCos = 0x48;
constexpr std::uintptr_t kEntityAngle = 0x4C;
constexpr std::uintptr_t kNextActor = 0xA90;          // u32 handle of the next actor in the list
constexpr std::uint64_t kCaptureWait = 120;           // frames for the body to change after the call (else refused)
constexpr std::uintptr_t kMotionCtl = 0x158;          // the actor's motion controller; +0 its motion file
constexpr std::uintptr_t kFxManager = 0x10;           // holder -> the manager its effects are made from (world_census.cpp)
constexpr std::uintptr_t kFxHolder = 0x80;            // the actor's own effect holder; +0 its PAX (game.hpp)
// Leftover movement terms (puppet.cpp ApplyTransform clears the same on the copy).
constexpr std::uintptr_t kVelocity = 0xB98, kCarried = 0x690, kAccel = 0xA48;

using PFN_Factory = std::uintptr_t(__fastcall*)(std::uint32_t, const float*, float);
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
using PFN_Decode = std::uintptr_t(__fastcall*)(std::uint32_t);
using PFN_Trap = void(__fastcall*)(std::uint64_t* args);

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

int g_on = -1;                 // STAND_IN, read once
std::uintptr_t g_body = 0;     // the stand-in made in this room
std::uint32_t g_bodyObj = 0;
bool g_bodyShown = false;
std::uintptr_t g_copyHidden = 0;  // the copy we hid
std::uint32_t g_ask = 0;       // object to make in the next spawn step (0 none)
float g_askAt[4] = {};
float g_askYaw = 0.0f;
// The stand-in's own part, made with it and attached to it: the Light Cycle's bike (the game makes the bike from
// the room's next spawn record and attaches it to its rider, bone 16386, flags 0: F848). Kept by the stand-in's
// rule: used only while in the game's actor list with the object it was made as; forgotten with the stand-in.
std::uintptr_t g_part = 0;
// Its Keyblade, the same way and by the same rule (the game builds one only as a player's weapon 0, at that
// player's spawn: F854). Made from our own Keyblade's object, whose files are loaded while ours is listed.
std::uintptr_t g_blade = 0;
std::uint32_t g_bladeObj = 0;
// The game hides a rider's Keyblade and shows it only through the Attack and Guard motions (184, 189: our own's
// flags sampled on a bench, F854); nothing does that for one we made, so we do, from the other player's motion.
bool g_bladeShown = false;
bool BladeMotion(std::int32_t motion) { return motion == 184 || motion == 189; }
bool g_peerDown = false;       // the other player's Sora is down (their pose): their cycle takes no enemies
std::uint32_t g_shared = 0;    // answers of StandInSharePlayer in this room
std::uint32_t g_failedObj = 0;  // the factory made nothing or faulted for it in this room: not asked again
std::uint32_t g_made = 0, g_shown = 0;
std::uint64_t g_frame = 0;
// Our own capture: from the call until our body changed and came back (or never changed within kCaptureWait).
bool g_captured = false;
bool g_captureChanged = false;
std::uint32_t g_captureFrom = 0;  // our body's object before the call
std::uint64_t g_captureAt = 0;
// The map ship's place (x, y, z, sin, cos, facing) and the other player's motion, put on it right after its own update
// (StandInAfterUpdate), as puppet.cpp places the copy: written earlier, its update and our ship's push moved it between
// writes (a stutter) and turned it 11 degrees off (F763).
bool g_shipPlace = false;
float g_shipAt[6] = {};
std::int32_t g_shipMotion = -1;  // -1: none to play (the other player's motion is from another file)
float g_shipBlend = 0.0f;
std::uint32_t g_shipCaptures = 0;
// The game sets every actor's motion each frame (puppet.cpp's motion hook); on the stand-in that put idle (0) back
// each frame and restarted ours: while we place it, only our own set goes through.
bool g_shipSetting = false;
bool g_shipFx = false;  // its effect holder (+0x80) filled by us from our ship's PAX, for the thrusters its motion makes
bool g_shipFxTried = false;
// The Light Cycle's bike (g_part). The game plays the rider's motion number on a real bike too, from the bike's own
// motion file, and that motion makes the bike's effects (the crash's sparks, the Charge's glow: F856). Played on
// ours, a cutscene actor, one of that motion's frame events read a field such an actor doesn't keep and the game
// faulted (bench 2026-11-23): so ours plays no motion. Each game tells the other of every effect its own bike
// makes, and the other starts the same one here, from our own bike's PAX in the stand-in bike's effect holder.
// g_ourBike: our own bike, found in the actor list and checked each frame. g_partFxMade: effects started on
// ours since the other player's motion last changed; with no motion of its own to end them, they end then.
bool g_partFx = false, g_partFxTried = false, g_partFxMade = false;
std::uint32_t g_partFxLogs = 0;
std::uintptr_t g_ourBike = 0;

constexpr std::uint32_t kLightCycle = 0x5F5;      // P_EX100_TR_LIGHTCYCLE
constexpr std::uint32_t kLightCycleBike = 0x654;  // F_TR140
constexpr int kBladeBone = 178;                   // the Keyblade's attach on the cycle's rider (F848)

// The copy's ride (TODO 5.36, your picks 2026-11-26): where the copy is made from a directly spawned body
// (solo_copy.cpp: Agrabah's carpet riders, the Light Cycle, the sidecar), the room's ride record makes only our ride. A
// twin of ours (the listed actor of a ride object solo_copy.cpp marked, nearest our Sora) is made as a cutscene actor
// (its files are loaded: ours is listed) and placed after each update at our ride's offset from our Sora, turned to the
// copy's facing: not solid and with no motion, as the Light Cycle stand-in's bike (F855, F857). Kept across frames by
// the stand-in's rule (in the actor list with its object), forgotten at a room change (Forget).
std::uintptr_t g_copyRide = 0, g_ourRide = 0;
std::uintptr_t g_rideCopy = 0;  // the copy the ride is placed under this frame (0: not placed)
std::uint32_t g_copyRideObj = 0, g_copyRideFailed = 0, g_copyRideLogs = 0;
bool g_copyRideShown = false;

std::uint32_t ObjId(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The actor is still in the game's actor list. A Game Over or a cutscene frees actors without a room change: your
// two-PC run 2026-10-17 23:15:21, the Game Over freed the card we made and our hide call on it faulted in the game's
// trap_obj_hide (caught). So nothing of ours touches the stand-in unless it is still listed.
// ... or, with `object`, any listed actor of that object.
bool InList(std::uintptr_t x, std::uint32_t object = 0, std::uintptr_t* found = nullptr, std::uintptr_t except = 0) {
    if (!x && !object) return false;
    __try {
        auto decode = reinterpret_cast<PFN_Decode>(ExeBase() + game::kDecodeHandle);
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + game::kListHead);
        for (int n = 0; a && n < 512; ++n, a = decode(Read<std::uint32_t>(a + kNextActor)))
            if (a == x || (object && a != except && ObjId(a) == object)) {
                if (found) *found = a;
                return true;
            }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

// Our own body's object where the room makes no copy of the other player and both ride the same thing, else 0: our ship
// while we fly the World Map (the list head is the map's ship there, F760), or the Light Cycle (its room spawns the
// cycle's Sora by its own object, not the PLAYER record the copy is made from, F843).
std::uint32_t OurMapShip() {
    std::uint8_t world = 0, inField = 0;
    std::uintptr_t head = 0;
    if (!ReadAny(ExeBase() + game::kNow, &world) || !ReadAny(ExeBase() + game::kInField, &inField) || !inField ||
        !ReadAny(ExeBase() + game::kListHead, &head) || !head)
        return 0;
    std::uint32_t object = ObjId(head);
    return world == game::kWorldMap || object == kLightCycle ? object : 0;
}

// trap_obj_hide (1/133) / trap_obj_show (1/134), the game's own script calls, on an actor.
bool SetShown(std::uintptr_t actor, bool shown) {
    auto fn = reinterpret_cast<PFN_Trap>(WorldCensusTrapReal(1, shown ? 134 : 133));
    if (!fn || !InList(actor)) return false;
    __try {
        std::uint64_t args[16] = {};
        args[0] = kTagAddr | reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle)(actor);
        fn(args);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Forget() {
    g_body = 0;
    g_part = g_blade = 0;
    g_shared = 0;
    g_bodyObj = 0;
    g_bodyShown = false;
    g_copyHidden = 0;
    g_ask = 0;
    g_failedObj = 0;
    g_shipPlace = false;
    g_shipFx = g_shipFxTried = false;  // the load frees the actor, its holder with it
    g_partFx = g_partFxTried = g_partFxMade = false;
    g_ourBike = 0;
    g_copyRide = g_ourRide = g_rideCopy = 0;  // the load frees the copy's ride with the room
    g_copyRideObj = g_copyRideFailed = g_copyRideLogs = 0;
    g_copyRideShown = false;
}

// The stand-in's part with it (trap_obj_hide on the rider isn't known to hide what is attached to it).
void ShowPart(bool shown) {
    if (g_part && InList(g_part) && ObjId(g_part) == kLightCycleBike) SetShown(g_part, shown);
    // The Keyblade only hides with it: showing it is the motion's business (StandInFrame).
    if (!shown && g_bladeShown && g_blade && InList(g_blade) && ObjId(g_blade) == g_bladeObj) {
        SetShown(g_blade, false);
        g_bladeShown = false;
    }
}

// Our own ride: the listed actor of a ride object (solo_copy.cpp's rule) nearest our Sora, not the copy's twin.
std::uintptr_t NearestRide(std::uintptr_t sora) {
    std::uintptr_t best = 0;
    float bestD = 1e30f;
    __try {
        const auto* s = reinterpret_cast<const float*>(sora + kActorEntity + kEntityPos);
        auto decode = reinterpret_cast<PFN_Decode>(ExeBase() + game::kDecodeHandle);
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + game::kListHead);
        for (int n = 0; a && n < 512; ++n, a = decode(Read<std::uint32_t>(a + kNextActor))) {
            if (a == g_copyRide || !SoloCopyRideObject(ObjId(a))) continue;
            const auto* p = reinterpret_cast<const float*>(a + kActorEntity + kEntityPos);
            const float dx = p[0] - s[0], dy = p[1] - s[1], dz = p[2] - s[2], d = dx * dx + dy * dy + dz * dz;
            if (d < bestD) bestD = d, best = a;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        best = 0;
    }
    return best;
}

// Each frame from StandInFrame: check the two rides, make the copy's, hide it while there is no copy to ride.
void CopyRideFrame(std::uintptr_t copy) {
    g_rideCopy = 0;
    if (g_copyRide && (!InList(g_copyRide) || ObjId(g_copyRide) != g_copyRideObj)) g_copyRide = 0, g_copyRideObj = 0;
    if (g_ourRide && (!InList(g_ourRide) || !SoloCopyRideObject(ObjId(g_ourRide)))) g_ourRide = 0;
    std::uintptr_t sora = 0;
    ReadAny(ExeBase() + game::kListHead, &sora);
    if (!g_ourRide && sora) g_ourRide = NearestRide(sora);
    if (!copy || !g_ourRide || !sora) {
        if (g_copyRide && g_copyRideShown) g_copyRideShown = !SetShown(g_copyRide, false);
        return;
    }
    if (!g_copyRide) {
        const std::uint32_t obj = ObjId(g_ourRide);
        if (!obj || obj == g_copyRideFailed || !SpawnSyncFactoryOk()) return;
        float at[4] = {0, 0, 0, 1.0f};
        float yaw = 0;
        std::uintptr_t made = 0;
        __try {
            for (int i = 0; i < 3; ++i) at[i] = Read<float>(copy + kActorEntity + kEntityPos + 4 * i);
            yaw = Read<float>(copy + kActorEntity + kEntityAngle);
            made = reinterpret_cast<PFN_Factory>(ExeBase() + kFactory)(obj | kSceneActor, at, yaw);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            made = 0;
        }
        Log("stand-in: the copy's ride: a twin of ours (object %#x) %s", obj, made ? "made as a cutscene actor, placed "
            "under the copy each frame" : "NOT made (the factory made nothing): the copy rides with none");
        if (!made) {
            g_copyRideFailed = obj;
            return;
        }
        g_copyRide = made;
        g_copyRideObj = obj;
        g_copyRideShown = true;  // made visible
    }
    if (!g_copyRideShown) g_copyRideShown = SetShown(g_copyRide, true);
    g_rideCopy = copy;
    // Its motion: a cutscene actor has none and is drawn at its bind pose's height (the World Map ship, F762, F767).
    // shortcut: our own ride's motion, not the other player's ride's (the packet doesn't carry it); send it when a
    // ride's motion is seen to differ between the two players.
    std::int32_t motion = -1;
    bool noFx = true, fx = false;  // its effects aren't made (no PAX lent): none seen on the carpet
    if (ReadAny(g_ourRide + game::kActorMotion, &motion))
        PlayFrom(g_copyRide, g_ourRide, motion, 0.0f, noFx, fx, g_copyRideLogs, "ride's twin");
}

// 3x3 inverse (the drawn matrices' rotation rows, which may carry a scale): false when it has none.
bool Inverse3(const float m[3][3], float out[3][3]) {
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                      m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (std::fabs(det) < 1e-6f) return false;
    const float k = 1.0f / det;
    out[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * k;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    out[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * k;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    out[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * k;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    return true;
}

float WrapAngle(float a) { return std::remainder(a, 6.2831853f); }

// After an update of the copy or of its ride: the ride where ours is relative to our Sora, carried over to the copy.
// The offset turns with the copy through the drawn matrices (game::kActorDrawn, last frame's: world = local * rotation
// + place), from the copy's place written this frame (its drawn one is a frame behind: 64 units on the rails). Its
// turn is our ride's kind (F952): on the rails three angles, the copy's plus our ride's from our Sora; else a facing.
// All four were checked in the actor list by this frame's StandInFrame.
void PlaceCopyRide() {
    std::uintptr_t sora = 0;
    if (!ReadAny(ExeBase() + game::kListHead, &sora) || !sora) return;
    const auto* S = reinterpret_cast<const float*>(sora + game::kActorDrawn);
    const auto* R = reinterpret_cast<const float*>(g_ourRide + game::kActorDrawn);
    const auto* C = reinterpret_cast<const float*>(g_rideCopy + game::kActorDrawn);
    float s[3][3], si[3][3], rel[3][3], relPlace[3], d[3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) s[r][c] = S[r * 4 + c];
    if (!Inverse3(s, si)) return;
    for (int c = 0; c < 3; ++c) d[c] = R[12 + c] - S[12 + c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) rel[r][c] = R[r * 4] * si[0][c] + R[r * 4 + 1] * si[1][c] + R[r * 4 + 2] * si[2][c];
    for (int c = 0; c < 3; ++c) relPlace[c] = d[0] * si[0][c] + d[1] * si[1][c] + d[2] * si[2][c];
    // Only the place and turn values: the game builds the drawn matrix from them after the updates, as it does the
    // copy's (F951). Written here, mid-update, it was drawn a frame ahead of the copy at times: the carpet shook.
    float to[16] = {};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) to[r * 4 + c] = rel[r][0] * C[c] + rel[r][1] * C[4 + c] + rel[r][2] * C[8 + c];
    const auto* copyPlace = reinterpret_cast<const float*>(g_rideCopy + kActorEntity + kEntityPos);
    for (int c = 0; c < 3; ++c)
        to[12 + c] = relPlace[0] * C[c] + relPlace[1] * C[4 + c] + relPlace[2] * C[8 + c] + copyPlace[c];
    auto* entity = reinterpret_cast<float*>(g_copyRide + kActorEntity);
    for (int i = 0; i < 3; ++i) entity[kEntityPos / 4 + i] = to[12 + i];
    auto mode = [](std::uintptr_t a) { return reinterpret_cast<std::uint32_t*>(a + game::kActorFlags); };
    auto turn = [](std::uintptr_t a) { return reinterpret_cast<float*>(a + game::kActorRotVec); };
    const std::uint32_t euler = game::kFlagTurnEuler;
    if ((*mode(g_ourRide) & euler) && (*mode(sora) & euler) && (*mode(g_rideCopy) & euler)) {
        *mode(g_copyRide) |= euler;
        for (int i = 0; i < 3; ++i)
            turn(g_copyRide)[i] = WrapAngle(turn(g_rideCopy)[i] + turn(g_ourRide)[i] - turn(sora)[i]);
        entity[kEntityAngle / 4] = turn(g_copyRide)[1];
    } else {
        *mode(g_copyRide) &= ~euler;
        const float angle = std::atan2(-to[8], -to[10]);  // row 2 is the back in facing mode (F152)
        entity[kEntityAngle / 4] = angle;
        entity[kEntitySin / 4] = std::sin(angle);
        entity[kEntitySin / 4 + 1] = 0.0f;  // a direction's y
        entity[kEntityCos / 4] = std::cos(angle);
    }
    std::memset(reinterpret_cast<void*>(g_copyRide + kVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(g_copyRide + kCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(g_copyRide + kAccel), 0, 0x18);
    *reinterpret_cast<std::uint8_t*>(g_copyRide + kNoCollideAt) |= kNoCollide;  // as our attached ride: not solid
}

// The copy is back to Sora (or gone): hide the stand-in, show the copy.
void Stop(std::uintptr_t copy) {
    g_shipPlace = false;
    // A hidden ship's motion goes on: end its holder, or its thrusters burn where it stands. Only on the map, where
    // our ship's PAX (the kill reads it) is in place.
    if (g_shipFx && OurMapShip() && InList(g_body) && ObjId(g_body) == g_bodyObj) FormLookHolderEnd(g_body + kFxHolder);
    g_shipFx = g_shipFxTried = false;
    if (g_partFx && g_part && InList(g_part) && ObjId(g_part) == kLightCycleBike) FormLookHolderEnd(g_part + kFxHolder);
    g_partFx = g_partFxTried = g_partFxMade = false;
    if (g_body && g_bodyShown) {
        SetShown(g_body, false);
        ShowPart(false);
        g_bodyShown = false;
    }
    if (g_copyHidden) {
        if (g_copyHidden == copy) SetShown(copy, true);
        Log("stand-in: the copy shows as Sora again (%s)", g_copyHidden == copy ? "the peer's body is Sora's" : "copy gone");
        g_copyHidden = 0;
    }
    g_ask = 0;
}

// Ends our capture once our body is back to the one before it (or it never changed: our game refused it).
void WatchOurCapture(bool roomGone) {
    if (!g_captured) return;
    if (roomGone) {
        g_captured = false;
        Log("stand-in: our capture ended (room changed)");
        return;
    }
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + game::kListHead);
    std::uint32_t now = sora ? ObjId(sora) : 0;
    if (!now) return;  // between bodies
    if (now != g_captureFrom) {
        g_captureChanged = true;
    } else if (g_captureChanged) {
        g_captured = false;
        Log("stand-in: our capture ended: our body is object %#x again", now);
    } else if (g_frame - g_captureAt > kCaptureWait) {
        g_captured = false;
        Log("stand-in: our capture ended: our body never changed (refused?)");
    }
}

}  // namespace

void StandInOurCapture() {
    // Captured again while still the die (Luxord's desperation game, your pair bench 2026-10-17 23:22:50): keep the
    // body from before the first capture, else die -> new die looked like "never changed" and ended it too early.
    if (g_captured) {
        g_captureAt = g_frame;
        Log("stand-in: a capture call on our Sora while already captured: still waiting for object %#x", g_captureFrom);
        return;
    }
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + game::kListHead);
    g_captureFrom = sora ? ObjId(sora) : 0;
    g_captured = g_captureFrom != 0;
    g_captureChanged = false;
    g_captureAt = g_frame;
    Log("stand-in: a capture call on our Sora (object %#x): the other game shows our new body", g_captureFrom);
}

bool StandInWeAreCaptured() { return g_captured; }

void StandInFrame(std::uintptr_t copy, const PeerPose* pose, bool roomGone) {
    if (g_on < 0) {
        g_on = EnvInt("KH2COOP_STAND_IN", 1) == 1;
        Log("stand-in: %s", g_on ? "on (the other player's die or card shows on their copy; their World Map ship and Light Cycle)"
                                 : "off (STAND_IN=0)");
    }
    ++g_frame;
    g_peerDown = pose && (pose->downed || pose->gettingUp);
    WatchOurCapture(roomGone);
    if (!g_on) return;
    // On the map roomGone stays set (it's about the copy, and there is none); the load that leaves it zeroes ourShip.
    std::uint32_t ourShip = copy ? 0 : OurMapShip();
    if (roomGone && !ourShip) {  // the room's actors are freed by the load: nothing to hide or show
        if (g_body || g_copyHidden) Log("stand-in: room changed, stand-in forgotten");
        Forget();
        return;
    }
    if (g_body && (!InList(g_body) || ObjId(g_body) != g_bodyObj)) {
        Log("stand-in: our stand-in (object %#x) is no longer in the game's actor list (a Game Over or cutscene "
            "freed it?): forgotten", g_bodyObj);
        g_body = 0;
        g_part = g_blade = 0;
        g_bodyObj = 0;
        g_bodyShown = false;
    }
    // Its parts by the same rule, each frame: StandInAfterUpdate writes to them.
    if (g_part && (!InList(g_part) || ObjId(g_part) != kLightCycleBike)) g_part = 0;
    if (g_blade && (!InList(g_blade) || ObjId(g_blade) != g_bladeObj)) g_blade = 0;
    if (g_ourBike && (!InList(g_ourBike) || ObjId(g_ourBike) != kLightCycleBike)) g_ourBike = 0;
    if (g_part && !g_ourBike) InList(0, kLightCycleBike, &g_ourBike, g_part);
    if (g_copyHidden && g_copyHidden != copy) g_copyHidden = 0;  // that copy is gone: nothing to show again
    CopyRideFrame(copy);
    std::uint32_t want = 0;
    if (copy && pose && pose->hasActor && pose->captured && pose->body) {
        std::uint32_t own = ObjId(copy);
        if (own && pose->body != own) want = pose->body;
    } else if (ourShip && pose && pose->hasActor && pose->body == ourShip) {
        want = ourShip;  // the other player flies the map too (in Gummi mode their pose has no actor)
    }
    if (!want) {
        if (g_copyHidden || g_bodyShown || g_ask) Stop(copy);
        return;
    }
    if (g_body && g_bodyObj != want) {  // another body: leave the old one hidden
        if (g_bodyShown) SetShown(g_body, false), ShowPart(false);
        g_body = 0;
        g_part = g_blade = 0;
        g_bodyShown = false;
    }
    // x, y, z, sin, cos, facing: the copy's spot, or on the map the other player's ship's.
    float at[6] = {};
    if (!copy) {
        for (int i = 0; i < 3; ++i) at[i] = pose->pos[i];
        at[3] = std::sin(pose->angle);
        at[4] = std::cos(pose->angle);
        at[5] = pose->angle;
    }
    std::uintptr_t entity = copy + kActorEntity;
    __try {
        if (copy) {
            for (int i = 0; i < 3; ++i) at[i] = Read<float>(entity + kEntityPos + 4 * i);
            at[3] = Read<float>(entity + kEntitySin);
            at[4] = Read<float>(entity + kEntityCos);
            at[5] = Read<float>(entity + kEntityAngle);
        }
        if (!g_body) {
            if (!g_ask && want != g_failedObj) {
                for (int i = 0; i < 3; ++i) g_askAt[i] = at[i];
                g_askAt[3] = 1.0f;
                g_askYaw = at[5];
                g_ask = want;
                Log(copy ? "stand-in: the other player is captured as object %#x: making it on their copy"
                         : "stand-in: the other player rides as we do where no copy is made (the World Map's ship, the "
                           "Light Cycle): making theirs (object %#x)", want);
            }
            return;
        }
        if (!copy) {  // the map ship: put on it by StandInAfterUpdate
            for (int i = 0; i < 6; ++i) g_shipAt[i] = at[i];
            const std::int32_t was = g_shipMotion;
            g_shipMotion = pose->otherBank ? -1 : static_cast<std::int32_t>(pose->motion);
            // Their motion changed: what it made on the bike ends with it (a real bike's own motion does that).
            if (g_shipMotion != was && g_partFxMade && g_part) {
                FormLookHolderEnd(g_part + kFxHolder);
                g_partFx = g_partFxTried = g_partFxMade = false;  // filled again at its next update
            }
            g_shipBlend = pose->motionBlend;
            g_shipPlace = true;
            if (const bool want = BladeMotion(g_shipMotion);
                want != g_bladeShown && g_blade && InList(g_blade) && ObjId(g_blade) == g_bladeObj) {
                SetShown(g_blade, want);
                g_bladeShown = want;
            }
        } else {
            auto* to = reinterpret_cast<float*>(g_body + kActorEntity);
            for (int i = 0; i < 3; ++i) to[kEntityPos / 4 + i] = at[i];
            to[kEntitySin / 4] = at[3];
            to[kEntityCos / 4] = at[4];
            to[kEntityAngle / 4] = at[5];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("stand-in: exception placing the stand-in, forgotten");
        Forget();
        return;
    }
    if (!g_bodyShown) {
        g_bodyShown = SetShown(g_body, true);
        ShowPart(true);
    }
    if (g_copyHidden != copy) {
        if (SetShown(copy, false)) g_copyHidden = copy;
        ++g_shown;
        Log("stand-in: the copy hidden%s, object %#x shows in its place (%u times so far)",
            g_copyHidden == copy ? "" : " (hide failed)", g_bodyObj, g_shown);
    }
}

void StandInSpawnStep() {
    if (!g_ask) return;
    std::uint32_t obj = g_ask;
    g_ask = 0;
    if (!SpawnSyncFactoryOk()) {
        g_failedObj = obj;
        Log("stand-in: object %#x not made: the actor factory didn't match at start", obj);
        return;
    }
    // The bike only while our own is listed: then its files are loaded (the factory faults without them).
    const bool bike = obj == kLightCycle && InList(0, kLightCycleBike);
    // The Keyblade from our own's object, only while ours is listed (its files are loaded then).
    std::uint32_t bladeObj = 0;
    if (std::uintptr_t ours = obj == kLightCycle ? CopyBladeOurWeapon() : 0; ours && InList(ours)) bladeObj = ObjId(ours);
    std::uintptr_t made = 0, part = 0, blade = 0;
    __try {
        made = reinterpret_cast<PFN_Factory>(ExeBase() + kFactory)(obj | kSceneActor, g_askAt, g_askYaw);
        if (made && bike) {
            part = reinterpret_cast<PFN_Factory>(ExeBase() + kFactory)(kLightCycleBike | kSceneActor, g_askAt, g_askYaw);
        }
        if (made && bladeObj) {
            blade = reinterpret_cast<PFN_Factory>(ExeBase() + kFactory)(bladeObj | kSceneActor, g_askAt, g_askYaw);
            if (blade) GrabSyncAttach(blade, made, kBladeBone, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        made = part = blade = 0;
    }
    if (!made) {
        g_failedObj = obj;
        Log("stand-in: the factory made nothing for object %#x (not loaded here?): the copy stays Sora", obj);
        return;
    }
    g_body = made;
    g_part = part;
    g_blade = blade;
    g_bladeObj = blade ? bladeObj : 0;
    g_bladeShown = blade != 0;  // made visible; hidden at the next frame unless they attack
    g_bodyObj = obj;
    if (obj == kLightCycle)
        Log("stand-in: the other player's Light Cycle: its Keyblade (our own's object %#x) %s", bladeObj,
            blade ? "made and attached to its hand" : bladeObj ? "NOT made" : "not made: ours isn't in the actor list");
    if (obj == kLightCycle)
        Log("stand-in: the other player's Light Cycle: its bike (object %#x) %s", kLightCycleBike,
            part ? "made (placed with it each frame)" : bike ? "NOT made" : "not made: ours isn't in the actor list");
    g_bodyShown = true;  // made visible
    ++g_made;
    Log("stand-in: made object %#x as a cutscene actor at exe+0x%llX (%u made; in the game's actor list: %s)", obj,
        static_cast<unsigned long long>(made - ExeBase()), g_made, InList(made) ? "yes" : "NO, so it stays unused");
}

// The other player's motion and its effects on an object we made (`actor`), from our own same object (`ours`): a
// cutscene actor's controller has no motion file, so the game looks nothing up for it (F766): it borrows ours. Its
// teardown never frees the controller's file (L1975). The effects its motion makes come from our object's PAX, put
// in its effect holder once per showing.
void PlayFrom(std::uintptr_t actor, std::uintptr_t ours, std::int32_t motion, float blend, bool& fxTried, bool& fx,
              std::uint32_t& logs, const char* what) {
    std::uintptr_t pax = 0, file = 0, lent = 0;
    std::int32_t now = -1;
    if (!fxTried && ReadAny(ours + kFxHolder, &pax) && pax) {
        fxTried = true;
        fx = FormLookHolderFill(actor + kFxHolder, pax);
        Log("stand-in: the other player's %s's effects from our own's PAX %p: %s", what, reinterpret_cast<void*>(pax),
            fx ? "filled" : "NOT filled (its holder is still listed)");
    }
    if (motion < 0 || !ReadAny(ours + kMotionCtl, &file) || !file || !ReadAny(actor + kMotionCtl, &lent) ||
        !ReadAny(actor + game::kActorMotion, &now))
        return;
    if (lent != file) *reinterpret_cast<std::uintptr_t*>(actor + kMotionCtl) = file;
    if (lent == file && now == motion) return;  // already playing it
    using PFN_SetMotion = void(__fastcall*)(std::uintptr_t, std::int32_t, float, float);
    g_shipSetting = true;  // the one motion set StandInBlocksMotion lets through
    reinterpret_cast<PFN_SetMotion>(ExeBase() + game::kSetMotion)(actor + kMotionCtl, motion, blend, 0.0f);
    g_shipSetting = false;
    std::uintptr_t running = 0;
    ReadAny(actor + kMotionCtl + 0x18, &running);  // the motion object: 0 = nothing set up (F766)
    if (++logs <= 20)
        Log("stand-in: the other player's %s plays motion %d (blend %.0f, was %d): motion object %p", what, motion, blend,
            now, reinterpret_cast<void*>(running));
}

void StandInCopyPlaced(std::uintptr_t copy) {
    if (g_rideCopy && g_copyRide && copy == g_rideCopy) PlaceCopyRide();
}

void StandInAfterUpdate(std::uintptr_t actor) {
    if (g_rideCopy && g_copyRide && actor && actor == g_copyRide) PlaceCopyRide();  // over its own rebuild
    // The stand-in's bike: placed where its rider is and facing its way, after its own update, as the rider is. The
    // game's attach (bone 16386, a real rider's) does not hold for a rider we place: through the crash motion the
    // bike stayed at the wall, up to 1,200 units from the thrown rider, and through Charge up to 500 away, while a
    // real rider's bike is never more than 50 from it (bench 2026-11-23, F855). So it is a free object we place.
    // The game just updated it, so it lives.
    if (g_shipPlace && actor && actor == g_part) {
        auto* entity = reinterpret_cast<float*>(actor + kActorEntity);
        for (int i = 0; i < 3; ++i) entity[kEntityPos / 4 + i] = g_shipAt[i];
        entity[kEntitySin / 4] = g_shipAt[3];
        entity[kEntityCos / 4] = g_shipAt[4];
        entity[kEntityAngle / 4] = g_shipAt[5];
        std::memset(reinterpret_cast<void*>(actor + kVelocity), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(actor + kCarried), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(actor + kAccel), 0, 0x18);
        // Not solid, as an attached bike isn't: a free one on the rider's spot pushed our own cycle back when the two
        // rode on the same spot (seen on the bench windows, 2026-11-23).
        *reinterpret_cast<std::uint8_t*>(actor + kNoCollideAt) |= kNoCollide;
        // Its effect holder from our own bike's PAX (ours is in the actor list as of this frame's StandInFrame).
        std::uintptr_t pax = 0;
        if (g_ourBike && !g_partFxTried && ReadAny(g_ourBike + kFxHolder, &pax) && pax) {
            g_partFxTried = true;
            g_partFx = FormLookHolderFill(actor + kFxHolder, pax);
            if (++g_partFxLogs <= 6)
                Log("stand-in: the other player's bike's effects from our own bike's PAX %p: %s", reinterpret_cast<void*>(pax),
                    g_partFx ? "filled" : "NOT filled (its holder is still listed)");
        }
        return;
    }
    // The game just updated this actor, so it lives; g_body only names one we made and found in the list.
    if (!g_shipPlace || !actor || actor != g_body) return;
    auto* entity = reinterpret_cast<float*>(actor + kActorEntity);
    for (int i = 0; i < 3; ++i) entity[kEntityPos / 4 + i] = g_shipAt[i];
    entity[kEntitySin / 4] = g_shipAt[3];
    entity[kEntityCos / 4] = g_shipAt[4];
    entity[kEntityAngle / 4] = g_shipAt[5];
    std::memset(reinterpret_cast<void*>(actor + kVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kAccel), 0, 0x18);
    // The other player's motion, from our own ship's motion file (the same ship), for its hover lift and flight (F762).
    // Our ship is the map's player, so it and its file last as long as the map, as the stand-in does.
    std::uintptr_t ours = 0;
    if (!ReadAny(ExeBase() + game::kListHead, &ours) || !ours || ObjId(ours) != g_bodyObj) return;
    PlayFrom(actor, ours, g_shipMotion, g_shipBlend, g_shipFxTried, g_shipFx, g_shipCaptures,
             g_bodyObj == kLightCycle ? "rider" : "ship");
}

// The Light Cycle's enemies (the Magnum Loaders) ride beside the player their script asks for each frame
// (trap_sysobj_player, 1/3): in the host's game always the host's cycle (F842), so a friend away from the host
// had none. One set is shared (your pick 2026-11-23): in the host's game an enemy with an even spawn serial is
// told the other player's cycle, our stand-in; all of them are while our own Sora is down, and none while theirs
// is. The stand-in was checked against the actor list this frame (StandInFrame), as for its placing.
constexpr std::uint32_t kCycleEnemies[] = {0x68B, 0x6B5, 0x6B6, 0x6B7};  // M_EX600_LC, _LC_ATK, _LC_CHG, _LC_GRD

void StandInSharePlayer(std::uintptr_t asker, std::uint64_t* answer) {
    if (!g_body || g_bodyObj != kLightCycle || !g_bodyShown || g_peerDown || !asker || !AvatarLinkIsHost()) return;
    const std::uint32_t object = ObjId(asker);
    if (std::find(std::begin(kCycleEnemies), std::end(kCycleEnemies), object) == std::end(kCycleEnemies)) return;
    const std::uint16_t serial = WorldSyncSpawnSerial(asker);
    if (!DownedIsDown() && (!serial || (serial & 1))) return;  // odd serials, and enemies with none, stay ours
    *answer = kTagAddr | reinterpret_cast<PFN_Encode>(ExeBase() + kEncodeHandle)(g_body);
    if (++g_shared == 1)
        Log("stand-in: Light Cycle enemies with an even spawn serial ride beside the other player's cycle (the first: "
            "object %#x, serial %u)", object, serial);
}

// A holder is {PAX, records, ...}; the PAX's record count is at +8; a record is 0x50 bytes: u16 +0 the file's entry
// the effect is made from, u16 +2 the id the game's effect start looks up (exe+0x2C6B30, L2040).
int StandInOurBikeEffect(std::uintptr_t mgr, int entry) {
    if (!g_part || !g_bodyShown || !g_ourBike || mgr != g_ourBike + kFxHolder + kFxManager) return -1;
    std::uintptr_t pax = 0, records = 0;
    std::int32_t count = 0;
    if (!ReadAny(g_ourBike + kFxHolder, &pax) || !pax || !ReadAny(g_ourBike + kFxHolder + 8, &records) || !records ||
        !ReadAny(pax + 8, &count))
        return -1;
    for (int i = 0; i < count && i < 256; ++i) {
        std::uint16_t pair[2] = {};
        if (!ReadAny(records + 0x50 * i, &pair)) return -1;
        if (pair[0] == entry) return pair[1];
    }
    return -1;
}

void StandInPartEffect(int id) {
    const int entry = id;
    if (!g_part || !g_bodyShown || !g_partFx || entry < 0 || !InList(g_part) || ObjId(g_part) != kLightCycleBike) return;
    using PFN_EffectStart = std::uint64_t(__fastcall*)(std::uintptr_t, std::uint64_t, std::uint64_t, std::uint32_t,
                                                       std::uintptr_t);
    // As a motion's frame event starts one: exe+0x2C6B30(actor + 0x80, id, 1, 1, 0, actor).
    const bool made = reinterpret_cast<PFN_EffectStart>(ExeBase() + game::kEffectStart)(g_part, entry, 1, 0, 0) != 0;
    g_partFxMade = g_partFxMade || made;
    if (++g_partFxLogs <= 30)
        Log("stand-in: the other player's bike's effect %d: %s", entry, made ? "made" : "NOT made");
}

bool StandInBlocksMotion(std::uintptr_t actor) {
    if (!actor || g_shipSetting) return false;  // our own set goes through
    return (actor == g_body && g_shipPlace) || actor == g_copyRide;  // the game's per-frame idle would undo it (F768)
}

}  // namespace kh2coop
