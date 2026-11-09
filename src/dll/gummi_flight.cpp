// The Gummi flight in co-op (TODO 5.2 stage 1 items 5-6, docs/GUMMI_COOP.md). Each player flies their own copy; this keeps
// the moments around the flight together:
//   - the 'Gummi Battle Tips' window: the first to press waits for the other, so both flights start together;
//   - the pause (pause_sync.cpp opens and closes it in both games): only the one who paused uses its list; a Retry from
//     it restarts both;
//   - the Game Over and results lists: the first there waits until both are; then only the Host's list responds and the
//     Client's game takes the Host's pick;
//   - quitting through the pause counts as shot down: that player waits on the Game Over list;
//   - a clear counts in both saves; rank and rewards stay each player's own.
// With KH2COOP_GUMMI_SHIP_TEST=1 the other player's ship flies in our mission, and the enemies are shared: each game keeps its
// own (they stay in step, F459), each enemy goes for the nearer ship, and kills and score reach the other game.
// Without the other game (no link, or it isn't in a flight) nothing is held.
//
// KH2COOP_GOD=1 (tests only, never in the friend package): the ship's HP stays full, so a bench flight reaches its goal.
//
// Kept across frames: nothing of the game's. The ship and the list are read each frame from the game's own pointers
// (gb::PLAYER's constructor sets the ship's and its destructor clears it). The other player's gb::FRIEND isn't kept either:
// the game hands it to the hook each frame, and its slot number says whether it's the one we filled.

#include "gummi_flight.hpp"

#include <cmath>
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

// The other player's ship in our flight (stage 2, a test so far: KH2COOP_GUMMI_SHIP_TEST=1): their chosen ship's data goes
// into an empty Teeny Ship blueprint, and the game builds a gb::FRIEND from it.
constexpr std::uintptr_t kShipChosen = 0x9A8824;  // u8: a ship is chosen
constexpr std::uintptr_t kShipData = 0x9A8880;    // the chosen ship's data
constexpr std::size_t kShipBytes = 0x800;
constexpr int kShipChunks = 2;
constexpr std::size_t kChunkBytes = kShipBytes / kShipChunks;
constexpr std::uintptr_t kShipParts = 10;  // u16 in the ship data: its part count
constexpr std::uintptr_t kTeenyBlueprints[] = {0xAEF4B0, 0xAEF4D8};  // gb's blueprint objects for the two Teeny Ships
constexpr std::uintptr_t kBlueprintData = 8;                         // ptr: the ship data, 0 = empty
constexpr std::uintptr_t kFillBlueprints = 0x1FA590;                 // (heap): fills the blueprints for a flight
constexpr std::uint8_t kFillBlueprintsBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                                 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8};
constexpr std::uintptr_t kLoadBlueprint = 0x2061D0;  // (blueprint, ship data, heap)
constexpr std::uint8_t kLoadBlueprintBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24,
                                                0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xF0};
// It flies where the other player flies: these three gb::OBJ vectors are what the game draws a ship from.
constexpr std::uintptr_t kObjVectors[] = {0x08, 0x28, 0x38};  // position on the rail's plane; the two directions that turn it
constexpr int kVectors = 3;
constexpr std::uintptr_t kPlaceAcrossRail = 0xAEA1A0 + 0x80;  // f32 x, then y: our ship's place; the stick only adds to it
constexpr float kStartApart = 50.0f;                          // the Host starts this far left of the rail's centre, the Client right
constexpr std::uintptr_t kFriendSlot = 0x438;       // i32: gb::FRIEND's Teeny Ship slot
constexpr std::uintptr_t kFriendMatrix = 0x22B4B0;  // (its VTABLE object, friend): builds what's drawn from those vectors
constexpr std::uint8_t kFriendMatrixBytes[] = {0x48, 0x8B, 0xCA, 0xE9, 0x38, 0x9F, 0xFB, 0xFF};
constexpr float kPoseLimit = 100000.0f;  // the farthest a value in a pose from the network may be
// A friend's stats add to the player's: the other player's go to a state of their own.
constexpr std::uintptr_t kShipStats = 0x1FB8B0;  // (gumiship, gb::PLAYER_STATE)
constexpr std::uint8_t kShipStatsBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                            0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
constexpr std::uintptr_t kGumishipBlueprint = 0x90;   // i32: 0 the player's blueprint, slot + 1 a friend's
constexpr std::uintptr_t kNewPlayerState = 0x225C30;  // (state): gb::PLAYER_STATE's constructor
constexpr std::uint8_t kNewPlayerStateBytes[] = {0x48, 0x8D, 0x05, 0x39, 0x02, 0x39, 0x00, 0x48,
                                                 0xC7, 0x41, 0x3C, 0x64, 0x00, 0x00, 0x00};
constexpr std::size_t kPlayerStateBytes = 0x68;
// Shared enemies. For an enemy's step our ship's place is the other ship's when theirs is nearer; what it does there is only
// for show here, because the other game's copy of that enemy does it to them for real.
constexpr std::uintptr_t kObjectStep = 0x1E7880;  // (gb::OBJ): one object's step for this frame
constexpr std::uint8_t kObjectStepBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x0F, 0xB6,
                                             0x81, 0xFC, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF9};
constexpr std::uintptr_t kObjPlace = 0x1E6BA0;  // (gb::OBJ, out vec4): its place in the world
constexpr std::uint8_t kObjPlaceBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83,
                                           0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kObjType = 0xAC;                  // i32: 2 and 3 are enemies
constexpr std::int32_t kEnemyTypeA = 2, kEnemyTypeB = 3;
constexpr std::uintptr_t kObjRow = 0x238;                  // ptr: the row it was built from, +0 i32 its kind
constexpr std::uintptr_t kEnemyFlags = 0x470;              // u32: bit 0 going down
constexpr std::uint32_t kEnemyGoingDown = 1;
constexpr std::uintptr_t kEnemyContact = 0x213D70;  // (enemy): it touches our ship
constexpr std::uint8_t kEnemyContactBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0x91,
                                               0x70, 0x04, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x8B};
constexpr std::uintptr_t kEnemyDown = 0x214350;  // (enemy, award): 1 adds its score, 0 only blows it up and removes it
constexpr std::uint8_t kEnemyDownBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x48, 0x83,
                                            0xEC, 0x40, 0x8B, 0x81, 0x70, 0x04, 0x00, 0x00};
constexpr std::uintptr_t kAddScore = 0x207A50;  // (points)
constexpr std::uint8_t kAddScoreBytes[] = {0x80, 0x3D, 0xDD, 0x36, 0x52, 0x00, 0x00, 0x75,
                                           0x31, 0x03, 0x0D, 0xD1, 0x36, 0x52, 0x00, 0x89};
constexpr std::uintptr_t kScore = 0x72B130;         // i32
constexpr std::uintptr_t kFriendAbsorb = 0x22B770;  // (friend, shot): a friend takes the shot, +100 score
constexpr std::uint8_t kFriendAbsorbBytes[] = {0x48, 0x83, 0xEC, 0x28, 0xE8, 0x07, 0x82, 0x00,
                                               0x00, 0x48, 0x8D, 0x0D, 0xC0, 0xB9, 0x8C, 0x00};
constexpr float kKillRange = 80.0f;       // on each axis: two games' copies of one enemy were at most about 70 apart
constexpr int kMaxKills = 64;             // a laser volley can down a whole screen at once (44 enemies seen on one)
constexpr int kKillFrames = 90;           // how long a kill waits for an enemy the other game hasn't made yet
constexpr int kOwnKillFrames = 120;       // how long a kill of ours can still turn out to be the other game's too
constexpr int kScoreEvery = 6;            // frames between score messages
constexpr std::int32_t kScoreLimit = 1000000;  // the most one message from the network may add
constexpr int kStatsEvery = 300;          // frames between the log's counts

constexpr int kStatusEvery = 10;         // frames
constexpr int kLoadGraceFrames = 1800;   // the tips wait this long for a game that hasn't reached its flight

constexpr std::uint32_t kMagic = 0x6632484B;  // "KH2f"
constexpr std::uint8_t kVersion = 1;

enum Phase : std::uint8_t { kOutside, kTips, kTipsPressed, kFlying, kPaused, kDown, kBusy, kAtList, kPhaseCount };
const char* const kPhaseName[kPhaseCount] = {"outside a flight", "at the tips",       "at the tips, pressed",
                                             "flying",           "paused",            "shot down",
                                             "at the goal",      "at the end list"};
enum Kind : std::uint8_t { kStatus, kPick, kRestart, kCleared, kShipChunk, kShipPose, kKill, kScoreAdd };

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic, build;
    std::uint8_t version, kind, arg;  // kStatus: our Phase; kPick: the list item the Host confirmed
};
struct ShipPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind, index, pad;
    std::uint32_t sum;  // of the whole ship: the two halves of one ship carry the same
    std::uint8_t data[kChunkBytes];
};
struct PosePacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    float pose[kVectors * 3];  // kObjVectors' x, y, z in order
};
struct KillPacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    std::int32_t enemyKind;  // its row's id
    float place[3];
    std::int32_t points;  // what it gave the sender
};
struct ScorePacket {
    std::uint32_t magic, build;
    std::uint8_t version, kind;
    std::int32_t points;
};
#pragma pack(pop)

using PFN_ListInput = std::int32_t(__fastcall*)(std::uintptr_t);
using PFN_StartEvent = std::uintptr_t(__fastcall*)(std::uintptr_t);
using PFN_FillBlueprints = void(__fastcall*)(std::uintptr_t);
using PFN_LoadBlueprint = void(__fastcall*)(std::uintptr_t, const void*, std::uintptr_t);
using PFN_FriendMatrix = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_ShipStats = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_NewPlayerState = void(__fastcall*)(void*);
using PFN_ObjectStep = void(__fastcall*)(std::uintptr_t);
using PFN_ObjPlace = void(__fastcall*)(std::uintptr_t, float*);
using PFN_EnemyContact = void(__fastcall*)(std::uintptr_t);
using PFN_EnemyDown = void(__fastcall*)(std::uintptr_t, std::uint8_t);
using PFN_AddScore = void(__fastcall*)(std::int32_t);
using PFN_FriendAbsorb = void(__fastcall*)(std::uintptr_t, std::uintptr_t);

bool g_on = false;
PFN_ListInput g_realListInput = nullptr;
PFN_StartEvent g_realStartEvent = nullptr;
PFN_FillBlueprints g_realFillBlueprints = nullptr;
PFN_LoadBlueprint g_loadBlueprint = nullptr;
std::uint8_t g_shipSent[kShipBytes] = {};       // the ship we last told the other game about
bool g_shipSentValid = false;
std::uint8_t g_peerShipIn[kShipBytes] = {};     // the other's ship as its halves arrive
std::uint32_t g_peerShipInSum = 0;
int g_peerShipInHave = 0;                       // bit per half
std::uint8_t g_peerShip[kShipBytes] = {};       // stays put: the game keeps a pointer to it for the flight
bool g_peerShipReady = false;
PFN_FriendMatrix g_realFriendMatrix = nullptr;
PFN_ShipStats g_realShipStats = nullptr;
PFN_NewPlayerState g_newPlayerState = nullptr;
alignas(16) std::uint8_t g_otherState[kPlayerStateBytes] = {};  // stays put: their gumiship keeps a pointer to it
int g_otherSlot = -1;                                           // this flight's Teeny Ship slot holding the other player's ship
float g_peerPose[kVectors * 3] = {};
bool g_peerPoseReady = false;
PFN_ObjectStep g_realObjectStep = nullptr;
PFN_ObjPlace g_objPlace = nullptr;
PFN_EnemyContact g_realEnemyContact = nullptr;
PFN_EnemyDown g_realEnemyDown = nullptr;
PFN_AddScore g_realAddScore = nullptr;
PFN_FriendAbsorb g_realFriendAbsorb = nullptr;
bool g_aimingAtOther = false;  // this enemy's step sees the other ship as ours
struct PendingKill {
    std::int32_t enemyKind;
    float place[3];
    int frames;
    std::int32_t points;
};
PendingKill g_kills[kMaxKills] = {};
int g_killCount = 0;
std::int32_t g_scoreOut = 0;  // points of ours the other game hasn't heard about
int g_scoreFrames = 0;
PendingKill g_own[kMaxKills] = {};  // kills we just told the other game about, with the points each gave
int g_ownCount = 0;
int g_statEnemies = 0, g_statAimed = 0, g_statSent = 0, g_statDone = 0, g_statBoth = 0, g_statLost = 0, g_statFull = 0, g_statPeak = 0, g_statDiffer = 0;
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

std::uint32_t ShipSum(const std::uint8_t* ship) {
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < kShipBytes; ++i) h = (h ^ ship[i]) * 16777619u;
    return h;
}

// shortcut: sent once per ship, so a game restarted after our pick doesn't get it; send again on a peer restart when stage 2 is real.
void ShipFrame() {
    std::uint8_t chosen = 0;
    if (!ReadAny(ExeBase() + kShipChosen, &chosen) || !chosen || !GummiMenuLinked()) return;
    const auto* const ship = reinterpret_cast<const std::uint8_t*>(ExeBase() + kShipData);
    if (g_shipSentValid && std::memcmp(ship, g_shipSent, kShipBytes) == 0) return;
    std::memcpy(g_shipSent, ship, kShipBytes);
    g_shipSentValid = true;
    ShipPacket p {kMagic, AvatarLinkBuild(), kVersion, kShipChunk, 0, 0, ShipSum(ship), {}};
    for (int i = 0; i < kShipChunks; ++i) {
        p.index = static_cast<std::uint8_t>(i);
        std::memcpy(p.data, ship + i * kChunkBytes, kChunkBytes);
        AvatarLinkSendReliable(&p, sizeof(p));
    }
    Log("gummi flight: our ship sent to the other game (sum %08X, %u parts)", p.sum, *reinterpret_cast<const std::uint16_t*>(ship + kShipParts));
}

void OnShipPacket(const char* buf) {
    ShipPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipChunk || p.index >= kShipChunks)
        return;
    if (p.sum != g_peerShipInSum) {
        g_peerShipInSum = p.sum;
        g_peerShipInHave = 0;
    }
    std::memcpy(g_peerShipIn + p.index * kChunkBytes, p.data, kChunkBytes);
    g_peerShipInHave |= 1 << p.index;
    if (g_peerShipInHave != (1 << kShipChunks) - 1) return;
    const std::uint16_t parts = *reinterpret_cast<const std::uint16_t*>(g_peerShipIn + kShipParts);
    if (ShipSum(g_peerShipIn) != p.sum || parts == 0 || parts >= kShipBytes) {  // the game walks these parts: only a whole ship
        Log("gummi flight: the other player's ship refused (sum %08X, %u parts)", p.sum, parts);
        return;
    }
    std::memcpy(g_peerShip, g_peerShipIn, kShipBytes);
    g_peerShipReady = true;
    Log("gummi flight: the other player's ship arrived (sum %08X, %u parts)", p.sum, parts);
}

bool ShipTest() {
    static const bool test = EnvInt("KH2COOP_GUMMI_SHIP_TEST", 0) == 1;
    return test;
}

void __fastcall HookFillBlueprints(std::uintptr_t heap) {
    g_realFillBlueprints(heap);
    g_otherSlot = -1;
    g_peerPoseReady = false;
    if (!ShipTest() || !g_peerShipReady || !GummiMenuLinked()) return;
    for (int slot = 0; slot < 2; ++slot) {
        const std::uintptr_t blueprint = ExeBase() + kTeenyBlueprints[slot];
        std::uintptr_t data = 1;
        if (!ReadAny(blueprint + kBlueprintData, &data) || data) continue;
        g_loadBlueprint(blueprint, g_peerShip, heap);
        g_otherSlot = slot;
        Log("gummi flight: the other player's ship put in Teeny Ship slot %d", slot);
        return;
    }
    // shortcut: with both Teeny Ship slots taken the other player's ship isn't shown; replace one when stage 2 is real.
    Log("gummi flight: no free Teeny Ship slot for the other player's ship");
}

void __fastcall HookShipStats(std::uintptr_t gumiship, std::uintptr_t state) {
    std::int32_t blueprint = -1;
    if (g_otherSlot >= 0 && ReadAny(gumiship + kGumishipBlueprint, &blueprint) && blueprint == g_otherSlot + 1) {
        g_newPlayerState(g_otherState);
        state = reinterpret_cast<std::uintptr_t>(g_otherState);
    }
    g_realShipStats(gumiship, state);
}

// shortcut: uses the newest pose with no smoothing; add it if the two-PC run looks jittery.
bool ShipOut(std::uint8_t phase) { return phase == kTips || phase == kTipsPressed || phase == kFlying; }

void __fastcall HookFriendMatrix(std::uintptr_t table, std::uintptr_t ship) {
    std::int32_t slot = -1;
    if (g_otherSlot >= 0 && g_peerPoseReady && ShipOut(g_peer) && ReadAny(ship + kFriendSlot, &slot) && slot == g_otherSlot)
        for (int i = 0; i < kVectors; ++i) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[i]), g_peerPose + i * 3, 3 * sizeof(float));
    g_realFriendMatrix(table, ship);
}

void SendPose(std::uintptr_t ship) {
    PosePacket p {kMagic, AvatarLinkBuild(), kVersion, kShipPose, {}};
    for (int i = 0; i < kVectors * 3; ++i) {
        float value = 0.0f;
        if (!ReadAny(ship + kObjVectors[i / 3] + (i % 3) * sizeof(float), &value)) return;
        p.pose[i] = value;
    }
    AvatarLinkSendRaw(&p, sizeof(p));
}

void OnPosePacket(const char* buf) {
    PosePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kShipPose) return;
    for (const float v : p.pose)
        if (!std::isfinite(v) || std::fabs(v) > kPoseLimit) return;  // the game draws from these
    std::memcpy(g_peerPose, p.pose, sizeof(g_peerPose));
    g_peerPoseReady = true;
}

bool Sharing() { return ShipTest() && GummiMenuLinked() && g_otherSlot >= 0 && g_peerPoseReady && ShipOut(g_peer); }

bool ReadPlace(std::uintptr_t obj, float* out) {
    for (int i = 0; i < 3; ++i)
        if (!ReadAny(obj + kObjVectors[0] + i * sizeof(float), &out[i])) return false;
    return true;
}

float Dist2(const float* a, const float* b) {
    return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
}

// True when the other ship is nearer: the ship's place is then theirs and `mine` holds ours, to put back after the step.
bool OtherNearer(std::uintptr_t enemy, std::uintptr_t ship, float* mine) {
    alignas(16) float at[4] = {0, 0, 0, 1}, us[4] = {0, 0, 0, 1}, them[4] = {0, 0, 0, 1};
    if (!ReadPlace(ship, mine)) return false;
    void* const place = reinterpret_cast<void*>(ship + kObjVectors[0]);
    g_objPlace(enemy, at);
    g_objPlace(ship, us);
    std::memcpy(place, g_peerPose, 3 * sizeof(float));
    g_objPlace(ship, them);
    if (Dist2(at, them) < Dist2(at, us)) return true;
    std::memcpy(place, mine, 3 * sizeof(float));
    return false;
}

bool Near(const float* a, const float* b) {
    return std::fabs(a[0] - b[0]) <= kKillRange && std::fabs(a[1] - b[1]) <= kKillRange && std::fabs(a[2] - b[2]) <= kKillRange;
}

bool EnemyKind(std::uintptr_t obj, std::int32_t* kind) {
    std::uintptr_t row = 0;
    return ReadAny(obj + kObjRow, &row) && row && ReadAny(row, kind);
}

// shortcut: the first enemy of that kind in range goes, so a neighbour of the same kind can go instead of the one shot; give enemies an id if a bench shows it matters.
void DownIfKilled(std::uintptr_t enemy) {
    std::int32_t kind = 0;
    std::uint32_t flags = kEnemyGoingDown;
    float at[3];
    if (!EnemyKind(enemy, &kind) || !ReadAny(enemy + kEnemyFlags, &flags) || (flags & kEnemyGoingDown) || !ReadPlace(enemy, at)) return;
    for (int i = 0; i < g_killCount; ++i) {
        if (g_kills[i].enemyKind != kind || !Near(g_kills[i].place, at)) continue;
        g_kills[i] = g_kills[--g_killCount];
        ++g_statDone;
        g_realEnemyDown(enemy, 0);
        return;
    }
}

void __fastcall HookObjectStep(std::uintptr_t obj) {
    std::int32_t type = 0;
    if (!ReadAny(obj + kObjType, &type) || (type != kEnemyTypeA && type != kEnemyTypeB)) {
        g_realObjectStep(obj);
        return;
    }
    const std::uintptr_t ship = Ship();
    float mine[3];
    g_aimingAtOther = ship && Sharing() && OtherNearer(obj, ship, mine);
    ++g_statEnemies;
    if (g_aimingAtOther) ++g_statAimed;
    g_realObjectStep(obj);
    if (g_aimingAtOther) std::memcpy(reinterpret_cast<void*>(ship + kObjVectors[0]), mine, sizeof(mine));
    g_aimingAtOther = false;
    if (g_killCount) DownIfKilled(obj);
}

void __fastcall HookEnemyContact(std::uintptr_t enemy) {
    if (!g_aimingAtOther) g_realEnemyContact(enemy);  // touching the other ship's copy does nothing here
}

// Both games put it down and each also got the other's points: each takes off the lower of the two, so both end on the same number.
void TakeOffLower(std::int32_t a, std::int32_t b) {
    const std::int32_t lower = a < b ? a : b;
    if (a != b) ++g_statDiffer;
    if (lower > 0 && g_realAddScore) g_realAddScore(-lower);
    ++g_statBoth;
}

// shortcut: a hit that doesn't kill stays in the game that made it, so a tough enemy's HP isn't shared; send the damage too if bosses need it.
void __fastcall HookEnemyDown(std::uintptr_t enemy, std::uint8_t award) {
    KillPacket p {kMagic, AvatarLinkBuild(), kVersion, kKill, 0, {}, 0};
    std::int32_t type = 0, before = 0, after = 0;
    const bool tell = award && ShipTest() && GummiMenuLinked() && Mine() == kFlying && ReadAny(enemy + kObjType, &type) &&
                      (type == kEnemyTypeA || type == kEnemyTypeB) && EnemyKind(enemy, &p.enemyKind) && ReadPlace(enemy, p.place) &&
                      ReadAny(ExeBase() + kScore, &before);
    g_realEnemyDown(enemy, award);
    if (!tell || !ReadAny(ExeBase() + kScore, &after)) return;
    p.points = after - before;
    AvatarLinkSendReliable(&p, sizeof(p));
    ++g_statSent;
    for (int i = 0; i < g_killCount; ++i) {
        if (g_kills[i].enemyKind != p.enemyKind || !Near(g_kills[i].place, p.place)) continue;
        // Their report got here first and we scored it anyway: the same correction OnKillPacket makes on their side.
        TakeOffLower(p.points, g_kills[i].points);
        g_kills[i] = g_kills[--g_killCount];
        return;
    }
    if (g_ownCount >= kMaxKills) {
        ++g_statFull;
        return;
    }
    g_own[g_ownCount++] = {p.enemyKind, {p.place[0], p.place[1], p.place[2]}, kOwnKillFrames, p.points};
    if (g_ownCount > g_statPeak) g_statPeak = g_ownCount;
}

void __fastcall HookAddScore(std::int32_t points) {
    std::int32_t before = 0, after = 0;
    const bool had = ReadAny(ExeBase() + kScore, &before);
    g_realAddScore(points);
    if (had && ReadAny(ExeBase() + kScore, &after) && after > before && ShipTest() && GummiMenuLinked()) g_scoreOut += after - before;
}

void __fastcall HookFriendAbsorb(std::uintptr_t ship, std::uintptr_t shot) {
    std::int32_t slot = -1;
    if (g_otherSlot >= 0 && ReadAny(ship + kFriendSlot, &slot) && slot == g_otherSlot) return;  // only their copy: no score here
    g_realFriendAbsorb(ship, shot);
}

void OnKillPacket(const char* buf) {
    KillPacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kKill || p.points < 0 || p.points > kScoreLimit) return;
    for (const float v : p.place)
        if (!std::isfinite(v)) return;
    for (int i = 0; i < g_ownCount; ++i) {
        if (g_own[i].enemyKind != p.enemyKind || !Near(g_own[i].place, p.place)) continue;
        TakeOffLower(g_own[i].points, p.points);
        g_own[i] = g_own[--g_ownCount];
        return;
    }
    if (g_killCount >= kMaxKills) {
        ++g_statFull;
        return;
    }
    PendingKill& k = g_kills[g_killCount++];
    k.enemyKind = p.enemyKind;
    std::memcpy(k.place, p.place, sizeof(k.place));
    k.frames = kKillFrames;
    k.points = p.points;
}

void OnScorePacket(const char* buf) {
    ScorePacket p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kScoreAdd) return;
    if (p.points > 0 && p.points <= kScoreLimit && g_realAddScore) g_realAddScore(p.points);
}

void SharedFrame(Phase now) {
    for (int i = g_ownCount - 1; i >= 0; --i)
        if (--g_own[i].frames <= 0) g_own[i] = g_own[--g_ownCount];
    for (int i = g_killCount - 1; i >= 0; --i)
        if (--g_kills[i].frames <= 0) {
            g_kills[i] = g_kills[--g_killCount];
            ++g_statLost;
        }
    if (now == kOutside || now == kTips) {
        g_killCount = g_ownCount = g_scoreOut = g_scoreFrames = 0;
        return;
    }
    if (g_scoreOut && ++g_scoreFrames >= kScoreEvery) {
        ScorePacket p {kMagic, AvatarLinkBuild(), kVersion, kScoreAdd, g_scoreOut};
        AvatarLinkSendReliable(&p, sizeof(p));
        g_scoreOut = g_scoreFrames = 0;
    }
    if (g_frame % kStatsEvery == 0 && g_statEnemies) {
        Log("gummi flight: enemy steps aimed at the other ship %d of %d; kills sent %d, done here %d, in both %d, lost %d; list full %d, most in it %d, points differed %d", g_statAimed, g_statEnemies, g_statSent, g_statDone, g_statBoth, g_statLost, g_statFull, g_statPeak, g_statDiffer);
        g_statAimed = g_statEnemies = 0;
    }
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
    if (g_on && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kLoadBlueprint), kLoadBlueprintBytes, sizeof(kLoadBlueprintBytes)) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(ExeBase() + kNewPlayerState), kNewPlayerStateBytes, sizeof(kNewPlayerStateBytes)) == 0) {
        g_loadBlueprint = reinterpret_cast<PFN_LoadBlueprint>(ExeBase() + kLoadBlueprint);
        g_newPlayerState = reinterpret_cast<PFN_NewPlayerState>(ExeBase() + kNewPlayerState);
        // The blueprints last: without the other two hooks no second ship is built at all.
        (void)(HookFunction(kShipStats, kShipStatsBytes, sizeof(kShipStatsBytes), reinterpret_cast<void*>(HookShipStats),
                            reinterpret_cast<void**>(&g_realShipStats), "Gummi flight ship stats") &&
               HookFunction(kFriendMatrix, kFriendMatrixBytes, sizeof(kFriendMatrixBytes), reinterpret_cast<void*>(HookFriendMatrix),
                            reinterpret_cast<void**>(&g_realFriendMatrix), "Gummi flight friend matrix") &&
               HookFunction(kFillBlueprints, kFillBlueprintsBytes, sizeof(kFillBlueprintsBytes), reinterpret_cast<void*>(HookFillBlueprints),
                            reinterpret_cast<void**>(&g_realFillBlueprints), "Gummi flight blueprints"));
    }
    // A test so far, like the second ship; the object step last, since aiming needs the contact guard.
    if (g_on && ShipTest() && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kObjPlace), kObjPlaceBytes, sizeof(kObjPlaceBytes)) == 0) {
        g_objPlace = reinterpret_cast<PFN_ObjPlace>(ExeBase() + kObjPlace);
        (void)(HookFunction(kEnemyDown, kEnemyDownBytes, sizeof(kEnemyDownBytes), reinterpret_cast<void*>(HookEnemyDown),
                            reinterpret_cast<void**>(&g_realEnemyDown), "Gummi flight enemy down") &&
               HookFunction(kAddScore, kAddScoreBytes, sizeof(kAddScoreBytes), reinterpret_cast<void*>(HookAddScore),
                            reinterpret_cast<void**>(&g_realAddScore), "Gummi flight score") &&
               HookFunction(kFriendAbsorb, kFriendAbsorbBytes, sizeof(kFriendAbsorbBytes), reinterpret_cast<void*>(HookFriendAbsorb),
                            reinterpret_cast<void**>(&g_realFriendAbsorb), "Gummi flight friend absorb") &&
               HookFunction(kEnemyContact, kEnemyContactBytes, sizeof(kEnemyContactBytes), reinterpret_cast<void*>(HookEnemyContact),
                            reinterpret_cast<void**>(&g_realEnemyContact), "Gummi flight enemy contact") &&
               HookFunction(kObjectStep, kObjectStepBytes, sizeof(kObjectStepBytes), reinterpret_cast<void*>(HookObjectStep),
                            reinterpret_cast<void**>(&g_realObjectStep), "Gummi flight object step"));
    }
    Log("gummi flight: %s", g_on ? "on: both players' flights start, pause and end together" : "off (a hook failed)");
}

bool GummiFlightFlying() {
    const std::uintptr_t ship = Ship();
    return ship && ShipState(ship) == kFlyingState;
}

void GummiFlightFrame() {
    GodFrame();
    if (!g_on) return;
    ShipFrame();
    const Phase now = Mine();
    SharedFrame(now);
    if (now == kTips && g_mine != kTips && g_mine != kTipsPressed && ShipTest() && GummiMenuLinked())
        *reinterpret_cast<float*>(ExeBase() + kPlaceAcrossRail) = AvatarLinkIsHost() ? -kStartApart : kStartApart;
    if (ShipOut(now) && ShipTest() && GummiMenuLinked()) SendPose(Ship());
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
    if (g_on && n == static_cast<int>(sizeof(ShipPacket))) {
        OnShipPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(PosePacket))) {
        OnPosePacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(KillPacket))) {
        OnKillPacket(buf);
        return;
    }
    if (g_on && n == static_cast<int>(sizeof(ScorePacket))) {
        OnScorePacket(buf);
        return;
    }
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
