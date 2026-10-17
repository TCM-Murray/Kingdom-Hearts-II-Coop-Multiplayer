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
// STAND_IN=0 in kh2coop.ini turns it off.

#include "stand_in.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>

#include "common.hpp"
#include "spawn_sync.hpp"
#include "world_census.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;             // u8 world, u8 room
constexpr std::uintptr_t kFactory = 0x3DF930;         // (object id, float pos[4], yaw) -> actor or 0
constexpr std::uint32_t kSceneActor = 0x40000000;     // factory: build the object as a cutscene actor
constexpr std::uintptr_t kEncodeHandle = 0x4AD240;    // (object) -> handle
constexpr std::uint64_t kTagAddr = 0x52444140ull << 32;  // "@ADR": a script argument naming an actor
constexpr std::uintptr_t kObjRow = 0x918;             // actor -> its object table row (u32 object id at +0)
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kEntityPos = 0x30;           // f32 x, y, z, w
constexpr std::uintptr_t kEntitySin = 0x40;
constexpr std::uintptr_t kEntityCos = 0x48;
constexpr std::uintptr_t kEntityAngle = 0x4C;
constexpr std::uintptr_t kListHead = 0x2A171C8;       // our Sora (the player body, any form), head of the actor list
constexpr std::uintptr_t kNextActor = 0xA90;          // u32 handle of the next actor in the list
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;    // (u32 handle) -> object or 0
constexpr std::uint64_t kCaptureWait = 120;           // frames for the body to change after the call (else refused)

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
std::uint32_t g_failedObj = 0;  // the factory made nothing or faulted for it in this room: not asked again
std::uint32_t g_made = 0, g_shown = 0;
std::uint64_t g_frame = 0;
// Our own capture: from the call until our body changed and came back (or never changed within kCaptureWait).
bool g_captured = false;
bool g_captureChanged = false;
std::uint32_t g_captureFrom = 0;  // our body's object before the call
std::uint64_t g_captureAt = 0;

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
bool InList(std::uintptr_t x) {
    if (!x) return false;
    __try {
        auto decode = reinterpret_cast<PFN_Decode>(ExeBase() + kDecodeHandle);
        std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
        for (int n = 0; a && n < 512; ++n, a = decode(Read<std::uint32_t>(a + kNextActor)))
            if (a == x) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
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
    g_bodyObj = 0;
    g_bodyShown = false;
    g_copyHidden = 0;
    g_ask = 0;
    g_failedObj = 0;
}

// The copy is back to Sora (or gone): hide the stand-in, show the copy.
void Stop(std::uintptr_t copy) {
    if (g_body && g_bodyShown) {
        SetShown(g_body, false);
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
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
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
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
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
        Log("stand-in: %s", g_on ? "on (the other player's die or card shows on their copy)" : "off (STAND_IN=0)");
    }
    ++g_frame;
    WatchOurCapture(roomGone);
    if (!g_on) return;
    if (roomGone) {  // the room's actors are freed by the load: nothing to hide or show
        if (g_body || g_copyHidden) Log("stand-in: room changed, stand-in forgotten");
        Forget();
        return;
    }
    if (g_body && !InList(g_body)) {
        Log("stand-in: our stand-in (object %#x) is no longer in the game's actor list (a Game Over or cutscene "
            "freed it?): forgotten", g_bodyObj);
        g_body = 0;
        g_bodyObj = 0;
        g_bodyShown = false;
    }
    if (g_copyHidden && g_copyHidden != copy) g_copyHidden = 0;  // that copy is gone: nothing to show again
    std::uint32_t want = 0;
    if (copy && pose && pose->hasActor && pose->captured && pose->body) {
        std::uint32_t own = ObjId(copy);
        if (own && pose->body != own) want = pose->body;
    }
    if (!want) {
        if (g_copyHidden || g_bodyShown || g_ask) Stop(copy);
        return;
    }
    if (g_body && g_bodyObj != want) {  // another body: leave the old one hidden
        if (g_bodyShown) SetShown(g_body, false);
        g_body = 0;
        g_bodyShown = false;
    }
    std::uintptr_t entity = copy + kActorEntity;
    __try {
        if (!g_body) {
            if (!g_ask && want != g_failedObj) {
                for (int i = 0; i < 3; ++i) g_askAt[i] = Read<float>(entity + kEntityPos + 4 * i);
                g_askAt[3] = 1.0f;
                g_askYaw = Read<float>(entity + kEntityAngle);
                g_ask = want;
                Log("stand-in: the other player is captured as object %#x: making it on their copy", want);
            }
            return;
        }
        auto* to = reinterpret_cast<float*>(g_body + kActorEntity);
        for (int i = 0; i < 3; ++i) to[kEntityPos / 4 + i] = Read<float>(entity + kEntityPos + 4 * i);
        to[kEntitySin / 4] = Read<float>(entity + kEntitySin);
        to[kEntityCos / 4] = Read<float>(entity + kEntityCos);
        to[kEntityAngle / 4] = Read<float>(entity + kEntityAngle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("stand-in: exception placing the stand-in, forgotten");
        Forget();
        return;
    }
    if (!g_bodyShown) g_bodyShown = SetShown(g_body, true);
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
    std::uintptr_t made = 0;
    __try {
        made = reinterpret_cast<PFN_Factory>(ExeBase() + kFactory)(obj | kSceneActor, g_askAt, g_askYaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        made = 0;
    }
    if (!made) {
        g_failedObj = obj;
        Log("stand-in: the factory made nothing for object %#x (not loaded here?): the copy stays Sora", obj);
        return;
    }
    g_body = made;
    g_bodyObj = obj;
    g_bodyShown = true;  // made visible
    ++g_made;
    Log("stand-in: made object %#x as a cutscene actor at exe+0x%llX (%u made; in the game's actor list: %s)", obj,
        static_cast<unsigned long long>(made - ExeBase()), g_made, InList(made) ? "yes" : "NO, so it stays unused");
}

}  // namespace kh2coop
