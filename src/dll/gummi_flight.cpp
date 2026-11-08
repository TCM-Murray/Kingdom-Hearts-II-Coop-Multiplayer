// The Gummi flight in co-op (TODO 5.2 stage 1 items 5-6, docs/GUMMI_COOP.md). Each player flies their own copy; this keeps
// the moments around the flight together:
//   - the 'Gummi Battle Tips' window: the first to press waits for the other, so both flights start together;
//   - the pause (pause_sync.cpp opens and closes it in both games): only the one who paused uses its list; a Retry from
//     it restarts both;
//   - the Game Over and results lists: the first there waits until both are; then only the Host's list responds and the
//     Client's game takes the Host's pick;
//   - quitting through the pause counts as shot down: that player waits on the Game Over list;
//   - a clear counts in both saves; score, rank and rewards stay each player's own.
// Without the other game (no link, or it isn't in a flight) nothing is held.
//
// KH2COOP_GOD=1 (tests only, never in the friend package): the ship's HP stays full, so a bench flight reaches its goal.
//
// Kept across frames: nothing of the game's. The ship and the list are read each frame from the game's own pointers
// (gb::PLAYER's constructor sets the ship's and its destructor clears it).

#include "gummi_flight.hpp"

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "gummi_menu.hpp"
#include "pause_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kShip = 0xAF0540;                     // ptr: gb::PLAYER, 0 outside a flight
constexpr std::uintptr_t kShipHp = 0x578, kShipMaxHp = 0x57C;  // f32, i32
constexpr std::uintptr_t kShipState = 0x5D8;                   // i32: 1 starting, 2 flying, 3 goal, 4 shot down
constexpr std::int32_t kStarting = 1, kFlyingState = 2, kShotDown = 4;
constexpr std::uintptr_t kShipFlags = 0x140C;                  // u32: 0x10 the ship is going down, 0x80000 no rebirth
constexpr std::uint32_t kGoingDown = 0x10 | 0x80000;

// The menu byte (PauseSyncOpenMenu) during a flight.
constexpr std::uint8_t kMenuPause = 0x0B, kMenuGameOver = 0x0C, kMenuResults = 0x0D;

constexpr std::uintptr_t kList = 0xBEE478;       // ptr: the list of the menu that's up
constexpr std::uintptr_t kListInput = 0x2F9440;  // (list): the item confirmed, -1 no press, -2 cancel
constexpr std::uint8_t kListInputBytes[] = {0x40, 0x55, 0x53, 0x48, 0x8D, 0x6C, 0x24, 0xB1, 0x48, 0x81, 0xEC, 0x98,
                                            0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xA2, 0xFD, 0x45, 0x00, 0x48, 0x33};
constexpr std::int32_t kNoPress = -1, kCancel = -2, kPauseRetry = 1, kPauseGummiMenu = 7, kPauseWorldMap = 8;
constexpr std::int32_t kEndRetry = 0, kLastListItem = 2;  // the Game Over and results lists: 0 Retry, 1 Gummi Menu, 2 World Map

constexpr std::uintptr_t kStartEvent = 0x20FF40;  // (event): gb::START_EVENT's update, the tips window and the count-in
constexpr std::uint8_t kStartEventBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                             0x83, 0xEC, 0x30, 0x40, 0x32, 0xFF, 0x48, 0x8B, 0xD9, 0x48, 0x83, 0x79};
constexpr std::uintptr_t kEventState = 0x10;  // i32: 3 the tips wait for a press; 2 and 5 a press skips the count-in
constexpr std::uint64_t kConfirm = 0x04;      // the bit START_EVENT reads in the pad's pressed bits

constexpr int kStatusEvery = 10;         // frames
constexpr int kLoadGraceFrames = 1800;   // the tips wait this long for a game that hasn't reached its flight

constexpr std::uint32_t kMagic = 0x6632484B;  // "KH2f"
constexpr std::uint8_t kVersion = 1;

enum Phase : std::uint8_t { kOutside, kTips, kTipsPressed, kFlying, kPaused, kDown, kBusy, kAtList, kPhaseCount };
const char* const kPhaseName[kPhaseCount] = {"outside a flight", "at the tips",       "at the tips, pressed",
                                             "flying",           "paused",            "shot down",
                                             "at the goal",      "at the end list"};
enum Kind : std::uint8_t { kStatus, kPick, kRestart, kCleared };

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic, build;
    std::uint8_t version, kind, arg;  // kStatus: our Phase; kPick: the list item the Host confirmed
};
#pragma pack(pop)

using PFN_ListInput = std::int32_t(__fastcall*)(std::uintptr_t);
using PFN_StartEvent = std::uintptr_t(__fastcall*)(std::uintptr_t);

bool g_on = false;
PFN_ListInput g_realListInput = nullptr;
PFN_StartEvent g_realStartEvent = nullptr;
Phase g_mine = kOutside;
std::uint8_t g_peer = kOutside;  // the other game's Phase
bool g_tipsPressed = false;      // our player pressed at the tips window
int g_tipsFrames = 0;            // frames at the tips window while the other game was outside its flight
int g_pick = -1;                 // the Client: the Host's pick, kept until the list closes
bool g_restart = false;          // the other player's Retry from their pause, for when we're not in the pause
bool g_quit = false;             // our player quit through the pause: the ship goes down once the pause has closed
bool g_clearSent = false, g_peerCleared = false;  // this flight's clear: ours told; theirs waiting for our end list
int g_frame = 0;
const char* g_note = nullptr;

void Note(const char* text) {
    if (text == g_note) return;
    g_note = text;
    Log("gummi flight: %s", text);
}

void Send(Kind kind, std::uint8_t arg) {
    Packet p {kMagic, AvatarLinkBuild(), kVersion, kind, arg};
    if (kind == kStatus) AvatarLinkSendRaw(&p, sizeof(p));
    else AvatarLinkSendReliable(&p, sizeof(p));
}

std::uintptr_t Ship() {
    std::uintptr_t ship = 0;
    return ReadAny(ExeBase() + kShip, &ship) ? ship : 0;
}

std::int32_t ShipState(std::uintptr_t ship) {
    std::int32_t state = 0;
    ReadAny(ship + kShipState, &state);
    return state;
}

Phase Mine() {
    const std::uintptr_t ship = Ship();
    if (!ship) return kOutside;
    const std::uint8_t menu = PauseSyncOpenMenu();
    if (menu == kMenuGameOver || menu == kMenuResults) return kAtList;
    if (menu == kMenuPause) return kPaused;
    const std::int32_t state = ShipState(ship);
    if (state == kStarting) return g_tipsPressed ? kTipsPressed : kTips;
    if (state == kFlyingState) return kFlying;
    return state == kShotDown ? kDown : kBusy;
}

std::int32_t PauseInput(std::uintptr_t list) {
    const int close = PauseSyncTakeClose();
    if (close) return close == 2 ? kPauseRetry : kCancel;
    if (GummiMenuLinked() && PauseSyncFollowing()) return kNoPress;  // the other player's pause: they use the list
    const std::int32_t r = g_realListInput(list);
    if (r >= 0) Log("gummi flight: our pause list: item %d", r);
    if (r == kPauseRetry) {
        Note("our Retry from the pause: both restart");
        PauseSyncRetryOnClose();
        if (GummiMenuLinked()) Send(kRestart, 0);
    } else if ((r == kPauseGummiMenu || r == kPauseWorldMap) && GummiMenuLinked() && g_peer != kOutside) {
        Note("our quit from the pause counts as shot down");
        g_quit = true;
        return kCancel;  // closes the pause as Continue
    }
    return r;
}

// Never answers anything but -1 or a real item: the game indexes its table with whatever isn't -1.
std::int32_t EndListInput(std::uintptr_t list) {
    if (g_restart) return kEndRetry;
    if (!GummiMenuLinked() || g_peer == kOutside) return g_realListInput(list);
    if (g_peer != kAtList) {
        Note("at the end list first: held until the other player is there too");
        return kNoPress;
    }
    if (!AvatarLinkIsHost()) {
        if (g_pick < 0) Note("at the end list, both there: waiting for the Host's pick");
        return g_pick < 0 ? kNoPress : g_pick;
    }
    Note("at the end list, both there: the Host picks");
    const std::int32_t r = g_realListInput(list);
    if (r >= 0 && r <= kLastListItem) {
        Log("gummi flight: the Host's pick is item %d, sent", r);
        Send(kPick, static_cast<std::uint8_t>(r));
    }
    return r;
}

std::int32_t __fastcall HookListInput(std::uintptr_t list) {
    const std::uint8_t menu = PauseSyncOpenMenu();
    std::uintptr_t current = 0;
    if ((menu != kMenuPause && menu != kMenuGameOver && menu != kMenuResults) || !Ship() ||
        !ReadAny(ExeBase() + kList, &current) || current != list)
        return g_realListInput(list);
    return menu == kMenuPause ? PauseInput(list) : EndListInput(list);
}

std::uintptr_t __fastcall HookStartEvent(std::uintptr_t event) {
    std::int32_t state = 0;
    std::uint64_t* const pressed = GummiMenuPressed();
    if (!pressed || !ReadAny(event + kEventState, &state) || (state != 3 && state != 2 && state != 5) ||
        !GummiMenuLinked() || (g_peer == kOutside && ++g_tipsFrames > kLoadGraceFrames))
        return g_realStartEvent(event);
    const std::uint64_t saved = *pressed;
    bool go = false;  // the count-in's presses (states 2 and 5) stay masked: a skip would split the two starts
    if (state == 3) {
        if (saved & kConfirm) g_tipsPressed = true;
        go = g_tipsPressed && (g_peer == kTipsPressed || g_peer == kFlying);
        if (go) Note("both pressed at the tips: the flight starts");
        else if (g_tipsPressed) Note("our press at the tips is held for the other player");
    }
    *pressed = go ? saved | kConfirm : saved & ~kConfirm;
    const std::uintptr_t result = g_realStartEvent(event);
    *pressed = saved;
    return result;
}

void GodFrame() {
    static const bool god = EnvInt("KH2COOP_GOD", 0) == 1;
    if (!god) return;
    const std::uintptr_t ship = Ship();
    std::int32_t max = 0;
    float hp = 0.0f;
    if (!ship || !ReadAny(ship + kShipMaxHp, &max) || max <= 0 || !ReadAny(ship + kShipHp, &hp) ||
        hp >= static_cast<float>(max))
        return;
    // shortcut: topped up once a frame, so one hit above full HP would still count; hook the damage if a bench sees it.
    *reinterpret_cast<float*>(ship + kShipHp) = static_cast<float>(max);
    static int logged = 0;
    if (logged < 5 && ++logged) Log("gummi flight: GOD MODE (KH2COOP_GOD=1): a hit took the ship to %.0f of %d, topped up", hp, max);
}

}  // namespace

void GummiFlightInit() {
    g_on = HookFunction(kListInput, kListInputBytes, sizeof(kListInputBytes), reinterpret_cast<void*>(HookListInput),
                        reinterpret_cast<void**>(&g_realListInput), "Gummi flight menu list input") &&
           HookFunction(kStartEvent, kStartEventBytes, sizeof(kStartEventBytes), reinterpret_cast<void*>(HookStartEvent),
                        reinterpret_cast<void**>(&g_realStartEvent), "Gummi flight start event");
    Log("gummi flight: %s", g_on ? "on: both players' flights start, pause and end together" : "off (a hook failed)");
}

bool GummiFlightFlying() {
    const std::uintptr_t ship = Ship();
    return ship && ShipState(ship) == kFlyingState;
}

void GummiFlightFrame() {
    GodFrame();
    if (!g_on) return;
    const Phase now = Mine();
    if (now != g_mine) {
        Log("gummi flight: we are %s (the other player: %s)", kPhaseName[now], kPhaseName[g_peer]);
        g_note = nullptr;
    }
    if (now != kTips && now != kTipsPressed) {
        g_tipsPressed = false;
        g_tipsFrames = 0;
    }
    if (now != kAtList) g_pick = -1;
    if (now == kAtList && PauseSyncOpenMenu() == kMenuResults && !g_clearSent && GummiMenuLinked()) {
        g_clearSent = true;
        Send(kCleared, 0);
    }
    if (g_peerCleared && now == kAtList) {  // not before: our own tally, if any, still tells its player what it opened
        g_peerCleared = false;
        GummiMenuCountClear();
    }
    if (g_quit && now == kFlying) {
        g_quit = false;
        *reinterpret_cast<std::uint32_t*>(Ship() + kShipFlags) |= kGoingDown;
    }
    if (now == kTips || now == kOutside) g_restart = g_quit = g_clearSent = g_peerCleared = false;
    if (!GummiMenuLinked()) g_peer = kOutside;
    else if (now != g_mine || ++g_frame % kStatusEvery == 0) Send(kStatus, now);
    g_mine = now;
}

void GummiFlightOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.kind == kStatus) {
        if (p.arg >= kPhaseCount) return;
        if (p.arg != g_peer) Log("gummi flight: the other player is %s", kPhaseName[p.arg]);
        g_peer = p.arg;
    } else if (p.kind == kPick && !AvatarLinkIsHost() && p.arg <= kLastListItem && Mine() == kAtList) {
        Log("gummi flight: the Host picked item %d", p.arg);
        g_pick = p.arg;
    } else if (p.kind == kRestart && Mine() != kOutside) {
        Log("gummi flight: the other player's Retry restarts us");
        g_restart = true;
    } else if (p.kind == kCleared && Mine() != kOutside) {
        Log("gummi flight: the other player cleared the mission");
        g_peerCleared = true;
    }
}

}  // namespace kh2coop
