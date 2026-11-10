// The Gummi menus in co-op (TODO 5.2, stage 1 of docs/GUMMI_COOP.md; Ghidra leads L206-L221).
//
// Messages: Gummi mode has no field HUD, so the INFO bar can't show there (VERIFIED_OFFSETS row *(exe+0xABE828)). Our
// text goes in a small copy of the menu's orange help bar at the top right, drawn with its body and text marker only
// (your look tests 2a-2d, MODLOG session 58). It closes when no message is up, so the Gummi Editor's screens stay clear.
//
// Sync: each game tells the other a few times a second where it is in the menus and what it has pressed (Status). The
// Host alone decides when a hold ends, and says so once (kGoShips, kGoLaunch); either player's Abort goes out once.
// - top: Start. The Client's always waits; the Host's waits only when there's no mission list for both (answer 21).
// - mission: the Host's pick waits for the Client's Start. The Client never sees its own list: it adopts the Host's pick.
// - body: Circle sends both to the top menu (answer 22).
// - lastcheck: both press Start; an Abort wins and sends both back where the game's own Abort goes (answer 12).
// A press is held by answering 'no press' from the screen's own input function, so the screen simply carries on.
//
// Kept across frames: only our bar window. It is dropped on any frame the menu isn't alive (its teardown frees every
// window, and addresses repeat across visits), and each frame needs the game's own check exe+0x251B60 and our offset
// and scale still on it (a new window of the game's at the same address would have 0 and 1.0). Everything else (lists,
// screens) is used inside the hook the game called us with.

#include "gummi_menu.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "link_menu.hpp"
#include "scene_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kMenuPool = 0xAFB420;   // ptr: the Gummi menu mode's pool, 0 = not running (L207)
constexpr std::uintptr_t kMenuModel = 0xAF9FA0;  // ptr: set with the pool at menu start, 0 at its end (L207)
constexpr std::uintptr_t kScreen = 0xAF9FB0;     // char[]: the menu screen's name ("top", "body", ...) (L206)
constexpr std::uintptr_t kLiveWindows = 0xAFB4B8;  // ptr: the live window list (L206)
constexpr std::uintptr_t kScheduler = 0xAFB4E0;    // ptr: the window scheduler; exe+0x251C40 doesn't check it (L208)
constexpr std::uintptr_t kMenuStack = 0xAF9E00;    // ptr: the screen stack (L220)
constexpr std::uintptr_t kStackIndex = 0x50;       // i32 on it: the current entry; 1 = the next step back lands on top
constexpr std::uintptr_t kRecords = 0x9B9990;      // the save's mission records, 0x40 bytes each (L199, L217)
constexpr std::uintptr_t kRecMission = 0x19, kRecCleared = 0x1D, kRecOpen = 0x1E;  // u8s of a record (L216, L217, L227)

constexpr std::uintptr_t kListValues = 0x80, kListCursor = 0xB8, kListCancel = 0xC8;  // selector fields (L219)
constexpr std::uintptr_t kLaunchTimerDone = 0xE8;  // u8 from the list: lastcheck's +0xF8, Circle / Abort count only once set
constexpr std::uintptr_t kMissionCursor = 0x228, kMissionEx = 0x398;  // gm::MISSION: i32 cursor, u8 EX (L221)
constexpr std::uint64_t kPadConfirm = 4, kPadCancel = 8;  // processed pad, pressed bits (L221)
constexpr std::uint16_t kRawCircle = 0x2000;  // the raw pad's Circle (tools/vpad.py): 'cancel' on every Gummi screen
constexpr std::int16_t kCmdLand = 0x9F;       // the map's command on a world (exe+0x394FC0)

constexpr std::uintptr_t kMakeWindow = 0x252B00;  // (type, priority, flag) -> window, opened (alloc, init, add, open)
constexpr std::uint8_t kMakeWindowBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                             0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kWindowText = 0x271850;  // (window, id): its text = message id
constexpr std::uint8_t kWindowTextBytes[] = {0x48, 0x81, 0xC1, 0xE0, 0x01, 0x00, 0x00, 0xE9};
constexpr std::uintptr_t kWindowLive = 0x251B60;  // (window) -> in the live list and not marked for deletion
constexpr std::uint8_t kWindowLiveBytes[] = {0x48, 0x85, 0xC9, 0x74, 0x2C, 0x48, 0x8B, 0x15};
constexpr std::uintptr_t kCloseWindow = 0x271310;  // (window): close animation, then it deletes itself
constexpr std::uint8_t kCloseWindowBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xC0,
                                              0x48, 0x8B, 0xD9, 0x48, 0x89, 0x81, 0x70, 0x02};
constexpr std::uintptr_t kGetPad = 0x39B780;  // (0) -> the processed pad; +8 pressed bits
constexpr std::uint8_t kGetPadBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D};
constexpr std::uintptr_t kGetRoute = 0x151B20;  // () -> the Gummi route
constexpr std::uint8_t kGetRouteBytes[] = {0x8B, 0x05, 0x62, 0x6C, 0x85, 0x00, 0xC3};
constexpr std::uintptr_t kGetMission = 0x151AE0;  // () -> the mission being flown, 1-3
constexpr std::uint8_t kGetMissionBytes[] = {0x8B, 0x05, 0x1E, 0x51, 0x5C, 0x00, 0xB9, 0x01};
constexpr std::uintptr_t kGetEx = 0x151B30;  // () -> low byte nonzero: the EX mission
constexpr std::uint8_t kGetExBytes[] = {0x80, 0x3D, 0x55, 0x6C, 0x85, 0x00, 0x00, 0x0F};
constexpr std::uintptr_t kSetEx = 0x151B40;  // (u8): the mission's EX flag
constexpr std::uint8_t kSetExBytes[] = {0x88, 0x0D, 0x46, 0x6C, 0x85, 0x00, 0xC3};
constexpr std::uintptr_t kSetMission = 0x151B50;  // (1-3): the mission
constexpr std::uint8_t kSetMissionBytes[] = {0xBA, 0x01, 0x00, 0x00, 0x00, 0x3B, 0xCA};
constexpr std::uintptr_t kGetHelpTask = 0x276600;  // () -> the help task
constexpr std::uint8_t kGetHelpTaskBytes[] = {0x48, 0x8B, 0x05, 0x59, 0x5F, 0x88, 0x00, 0xC3};
constexpr std::uintptr_t kCloseHelp = 0x276610;  // (help task): closes its window, as a screen with none does
constexpr std::uint8_t kCloseHelpBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B};
constexpr std::uintptr_t kRefuseA = 0x3966C0, kRefuseB = 0x399CD0;  // (): the map's own 'can't go there' tidying
constexpr std::uint8_t kRefuseABytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B};
constexpr std::uint8_t kRefuseBBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x35};

constexpr std::uintptr_t kSelector = 0x274850;  // gm::GUMIMENU_SELECTOR's update: 0 confirm, 1 cancel, -1 nothing (L219)
constexpr std::uint8_t kSelectorBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kBodyInput = 0x278CB0;  // ship selection's input: 0 confirm, 1 cancel (L221)
constexpr std::uint8_t kBodyInputBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kMissionInput = 0x24E9E0;  // the mission list's input, once a frame (L217, L221)
constexpr std::uint8_t kMissionInputBytes[] = {0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kSetScreen = 0x2435B0;  // (stack, next): writes the current entry, builds that screen (L220)
constexpr std::uint8_t kSetScreenBytes[] = {0x48, 0x63, 0x41, 0x50, 0x44, 0x8B, 0xC2, 0x44, 0x8B, 0xD2};
constexpr std::uintptr_t kWorldCommand = 0x394FC0;  // the map's command on what's under the ship
constexpr std::uint8_t kWorldCommandBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57};

// Window fields (L210-L212); the window's YI::SEQUENCE is at +0x40.
constexpr std::uintptr_t kSeqFlags = 0x40 + 0xE8;       // u32, bit 1: scale by the float below
constexpr std::uintptr_t kSeqScale = 0x40 + 0x178;      // f32, all axes (x, y at +4, +8), 1.0 from init
constexpr std::uintptr_t kSeqListBuilt = 0x40 + 0xF0;   // u8: the group's animation list is built (until a group change)
constexpr std::uintptr_t kSeqAnimCount = 0x40 + 0x100;  // u32: animations drawn from that list
constexpr std::uintptr_t kTextSize = 0x1E4;             // s32: the text's font size
constexpr std::uintptr_t kHold = 0x288;                 // u32, bit 2: keep kTextSize (else the group's each frame)
constexpr std::uintptr_t kOffsetX = 0x3B4, kOffsetY = 0x3B8;  // s32: moves the whole window

constexpr float kSmallScale = 0.6f;
constexpr int kSmallFont = 10;      // the bar's own: 16
constexpr int kSmallX = 184, kSmallY = -345;  // box center (256, 376) -> (440, 31): top right
// Every group of the orange bar starts with its body, text marker and portrait (then 6 sparkles when idle).
constexpr std::uint32_t kBarAnims = 3;
// Our bar shows Chip: the window draws from its sequence's sprite table, so ours gets a copy with the portrait's
// texture rectangle moved to where tools/make_chip_portrait.py put him. The game's own help bar keeps Dale.
constexpr std::uintptr_t kSeqSprites = 0x40 + 8;  // pointer: the sequence's sprite table
constexpr std::size_t kSpriteBytes = 0x2C, kSprites = 167, kPortrait = 97;
constexpr std::uintptr_t kSpriteUv = 4;           // 4 s32: left, top, right, bottom on the texture
constexpr std::int32_t kDaleUv[4] = {211, 76, 255, 137};
constexpr std::int32_t kChipUv[4] = {340, 57, 384, 118};
// Centered text moves right by half its leading spaces: 12 centered it in the box (test 2b, about 3 units each).
constexpr const char* kPad = "            ";
constexpr int kHelpPriority = 0x1E461;    // the help bar's scheduler priority
constexpr int kHelpLook = 0x15A;          // its look on `top` / `mission` / `factory` ('o', table exe+0x7358C0)
constexpr int kMsgGummi = 0x7F42;         // our message id (link_menu.cpp: 0x7F40, 0x7F41; gumi.bar ends at 0x54C0)
constexpr int kRetryFrames = 240;         // the game gave no window: try again after this
constexpr int kSayFrames = 240;           // how long a one-off message stays up (4 s)
constexpr int kMaxLogLines = 300;

constexpr std::uint32_t kMagic = 0x6732484B;  // "KH2g"
constexpr std::uint8_t kVersion = 1;
constexpr ULONGLONG kTheirFreshMs = 2000;     // the other game's status counts for this long
constexpr std::uint32_t kLinkFreshMs = 3000;  // the other game's link: quiet longer = no other player
constexpr int kStatusEvery = 20;              // frames between status packets (also sent at each change)
constexpr int kForceFrames = 900, kForceEvery = 15;  // an Abort's forced Circle: for up to 15 s, one press in 15 frames

enum Screen : std::uint8_t { kNone, kTop, kMission, kBody, kTiny, kLaunch, kEditor, kOther };
enum Hold : std::uint8_t { kHoldNone, kHoldStart, kHoldMission, kHoldLaunch };
enum Kind : std::uint8_t { kStatus, kGoShips, kGoLaunch, kAbort };

#pragma pack(push, 1)
struct Status {
    std::uint8_t screen = kNone, unlocked = 0, start = 0, pick = 0, launch = 0;
    std::int8_t route = -1;
};
struct Packet {
    std::uint32_t magic, build;
    std::uint8_t version, kind, arg;  // kGoShips: the pick; kAbort: 1 = both to the top menu
    Status status;
};
#pragma pack(pop)

using PFN_Make = std::uintptr_t(__fastcall*)(int, int, int);
using PFN_Text = void(__fastcall*)(std::uintptr_t, int);
using PFN_Live = bool(__fastcall*)(std::uintptr_t);
using PFN_Close = void(__fastcall*)(std::uintptr_t);
using PFN_GetPad = std::uintptr_t(__fastcall*)(int);
using PFN_GetInt = std::int32_t(__fastcall*)();
using PFN_SetEx = void(__fastcall*)(std::uint8_t);
using PFN_SetInt = void(__fastcall*)(std::int32_t);
using PFN_GetPtr = std::uintptr_t(__fastcall*)();
using PFN_Void = void(__fastcall*)();
using PFN_Selector = std::int32_t(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_BodyInput = std::uint64_t(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_MissionInput = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
using PFN_SetScreen = void(__fastcall*)(std::uintptr_t, std::int32_t);
using PFN_WorldCommand = void(__fastcall*)(std::uintptr_t, std::uintptr_t);

bool g_on = false;
PFN_Make g_makeWindow = nullptr;
PFN_Text g_windowText = nullptr;
PFN_Live g_windowLive = nullptr;
PFN_Close g_closeWindow = nullptr;
PFN_GetPad g_getPad = nullptr;
PFN_GetInt g_getRoute = nullptr, g_getMission = nullptr, g_getEx = nullptr;
PFN_SetEx g_setEx = nullptr;
PFN_SetInt g_setMission = nullptr;
PFN_GetPtr g_getHelpTask = nullptr;
PFN_Close g_closeHelp = nullptr;
PFN_Void g_refuseA = nullptr, g_refuseB = nullptr;
PFN_Selector g_realSelector = nullptr;
PFN_BodyInput g_realBodyInput = nullptr;
PFN_MissionInput g_realMissionInput = nullptr;
PFN_SetScreen g_realSetScreen = nullptr;
PFN_WorldCommand g_realWorldCommand = nullptr;

// The bar.
std::uintptr_t g_ours = 0;  // our window, 0 = none; never kept past a frame the menu isn't running
bool g_closing = false;     // we closed it: it plays its close, then deletes itself
int g_openWait = 0;         // frames before trying again when the game gave no window
int g_sayFrames = 0;        // frames left of the message up; -1 = until SayOff
std::uint8_t g_text[96] = {};
bool g_textSet = false;
char g_holdText[64] = {};   // the message of the hold we're in, shown again after a one-off message
bool g_sawHelpScreen = false;  // this visit showed a help screen: the screen name is this visit's, not the last one's

// The sync.
Hold g_hold = kHoldNone;
bool g_releaseHold = false;  // the Host's word came: the hold's screen lets the press through on its next call
bool g_go = false;           // the Host's kGoShips arrived: top lets Start through, held or not
bool g_redirect = false;     // the next move to the mission list goes to ship selection instead
bool g_session = false;      // both players went on to ship selection together
bool g_launched = false;     // both pressed Start at the launch
std::uint8_t g_pick = 0;     // the Host's held pick: mission 1-3, bit 7 = EX
int g_forceFrames = 0;       // > 0: the other player's Abort is sending us back
bool g_forceTop = false;     // ... to the top menu
Status g_sent, g_their;
ULONGLONG g_theirAt = 0;
bool g_theirValid = false;
Screen g_lastScreen = kNone;
int g_frame = 0, g_logLines = 0;
int g_testUnlocked = -1;  // bench only: GUMMI_CLIENT_UNLOCKED, the Client's unlocks as a mask

bool Code(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n) {
    return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0;
}

std::uintptr_t Ptr(std::uintptr_t at) {
    std::uintptr_t v = 0;
    return ReadAny(at, &v) ? v : 0;
}

template <typename... Args>
void Note(const char* fmt, Args... args) {
    if (g_logLines++ < kMaxLogLines) Log(fmt, args...);
}

const char* Other() { return AvatarLinkIsHost() ? "Client" : "Host"; }

// ---- the bar ----

void Say(int frames, const char* text) {
    char line[96];
    std::snprintf(line, sizeof(line), "%s%s", kPad, text);
    LinkMenuEncode(line, g_text, sizeof(g_text));
    g_textSet = true;
    g_sayFrames = frames;
    Note("gummi menu: says '%s'", text);
}

// A one-off message about the other player.
void SayAbout(const char* format) {
    char text[64];
    std::snprintf(text, sizeof(text), format, Other());
    Say(kSayFrames, text);
}

// The message of a hold: up until the hold ends, and back after any one-off message.
void SayHold(const char* format) {
    std::snprintf(g_holdText, sizeof(g_holdText), format, Other());
    Say(-1, g_holdText);
}

void SayOff() {
    g_holdText[0] = 0;
    if (g_sayFrames < 0) g_sayFrames = 0;
}

// The game made it this frame: nothing else has touched it yet.
std::uintptr_t MakeBar() {
    std::uintptr_t w = g_makeWindow(kHelpLook, kHelpPriority, 0);
    if (!w) return 0;
    *reinterpret_cast<std::uint32_t*>(w + kSeqFlags) |= 2;
    *reinterpret_cast<float*>(w + kSeqScale) = kSmallScale;
    *reinterpret_cast<std::uint32_t*>(w + kHold) |= 4;
    *reinterpret_cast<std::int32_t*>(w + kTextSize) = kSmallFont;
    *reinterpret_cast<std::int32_t*>(w + kOffsetX) = kSmallX;
    *reinterpret_cast<std::int32_t*>(w + kOffsetY) = kSmallY;
    g_windowText(w, kMsgGummi);  // the window keeps the pointer to g_text and draws it each frame
    return w;
}

// Still our window: live by the game's own check, and still carrying the marks MakeBar wrote.
bool StillOurs(std::uintptr_t w) {
    std::int32_t x = 0, y = 0;
    float scale = 0;
    return g_windowLive(w) && ReadAny(w + kOffsetX, &x) && x == kSmallX && ReadAny(w + kOffsetY, &y) &&
           y == kSmallY && ReadAny(w + kSeqScale, &scale) && scale == kSmallScale;
}

// Only when the table is the one we expect (Dale where we read him); else the bar keeps what the game gave it.
void ShowChip(std::uintptr_t w) {
    static std::uint8_t chipSprites[kSprites * kSpriteBytes];
    std::uintptr_t table = 0;
    if (!ReadAny(w + kSeqSprites, &table) || !table || table == reinterpret_cast<std::uintptr_t>(chipSprites)) return;
    const std::uintptr_t uv = table + kPortrait * kSpriteBytes + kSpriteUv;
    for (int i = 0; i < 4; ++i) {
        std::int32_t v = 0;
        if (!ReadAny(uv + i * sizeof(v), &v) || v != kDaleUv[i]) return;
    }
    std::uint8_t last = 0;
    if (!ReadAny(table + sizeof(chipSprites) - 1, &last)) return;
    std::memcpy(chipSprites, reinterpret_cast<const void*>(table), sizeof(chipSprites));
    std::memcpy(chipSprites + kPortrait * kSpriteBytes + kSpriteUv, kChipUv, sizeof(kChipUv));
    *reinterpret_cast<std::uintptr_t*>(w + kSeqSprites) = reinterpret_cast<std::uintptr_t>(chipSprites);
}

// `ready`: the menu runs, its window lists exist, and this visit showed a help screen.
void BarFrame(bool ready) {
    if (g_sayFrames > 0 && --g_sayFrames == 0 && g_hold != kHoldNone && g_holdText[0]) Say(-1, g_holdText);
    if (!ready) {
        g_ours = 0;
        g_closing = false;
        return;
    }
    if (g_ours && !StillOurs(g_ours)) {
        g_ours = 0;
        g_closing = false;
    }
    if (g_ours) ShowChip(g_ours);
    std::uint8_t built = 0;
    std::uint32_t count = 0;
    if (g_ours && ReadAny(g_ours + kSeqListBuilt, &built) && built && ReadAny(g_ours + kSeqAnimCount, &count) &&
        count > kBarAnims)
        *reinterpret_cast<std::uint32_t*>(g_ours + kSeqAnimCount) = kBarAnims;  // holds until the next group change
    if (g_sayFrames != 0 && !g_ours) {  // while ours still closes, the new one waits until it's gone
        if (g_openWait > 0) {
            --g_openWait;
            return;
        }
        g_ours = MakeBar();
        g_openWait = g_ours ? 0 : kRetryFrames;
    } else if (g_sayFrames == 0 && g_ours && !g_closing) {
        g_closeWindow(g_ours);  // the sparkle cut above goes on until StillOurs lets go
        g_closing = true;
    }
}

// ---- where we are ----

Screen ReadScreen() {
    const std::uintptr_t base = ExeBase();
    if (!Ptr(base + kMenuPool) || !Ptr(base + kMenuModel)) return kNone;
    std::uint64_t name[3] = {};  // 16 chars at most, zero-ended by name[2]
    ReadAny(base + kScreen, &name[0]);
    ReadAny(base + kScreen + 8, &name[1]);
    const char* s = reinterpret_cast<const char*>(name);
    if (!std::strcmp(s, "top")) return kTop;
    if (!std::strcmp(s, "mission")) return kMission;
    if (!std::strcmp(s, "body")) return kBody;
    if (!std::strcmp(s, "tiny")) return kTiny;
    if (!std::strcmp(s, "lastcheck")) return kLaunch;
    if (!std::strcmp(s, "factory") || !std::strcmp(s, "fbody") || !std::strcmp(s, "edit") || !std::strcmp(s, "ftiny"))
        return kEditor;
    return kOther;  // also the menu's first frames, before a screen has written its name
}

std::uintptr_t Record(std::int32_t route, int mission, bool ex) {
    return ExeBase() + kRecords + static_cast<std::intptr_t>((ex ? 0x1A : -1) + mission + route * 3) * 0x40;
}

bool RecordFlag(std::int32_t route, int mission, bool ex, std::uintptr_t field) {
    std::uint8_t v = 0;
    ReadAny(Record(route, mission, ex) + field, &v);
    return v != 0;
}

// What our save has on this route, as the menu's own tests read it: bit 0-2 Mission 1-3, bit 3-5 their EX.
std::uint8_t Unlocked(std::int32_t route) {
    if (!AvatarLinkIsHost() && g_testUnlocked >= 0) return static_cast<std::uint8_t>(g_testUnlocked);
    std::uint8_t mask = 1;
    if (RecordFlag(route, 1, false, kRecCleared)) mask |= 2;
    if (RecordFlag(route, 3, false, kRecOpen)) mask |= 4;
    for (int n = 0; n < 3; ++n)
        if (RecordFlag(route, n + 1, true, kRecOpen)) mask |= static_cast<std::uint8_t>(8 << n);
    return mask;
}

int MissionCount(std::uint8_t mask) { return mask & 4 ? 3 : mask & 2 ? 2 : 1; }

bool Linked() { return AvatarLinkPeerAgeMs() < kLinkFreshMs; }
bool TheirsFresh() { return g_theirValid && Linked() && GetTickCount64() - g_theirAt < kTheirFreshMs; }
bool PartnerHere(std::int32_t route) { return TheirsFresh() && g_their.screen != kNone && g_their.route == route; }

Status Mine(Screen s) {
    Status st;
    st.screen = s;
    if (s == kNone) return st;
    std::int32_t route = g_getRoute();
    st.route = static_cast<std::int8_t>(route);
    st.unlocked = Unlocked(route);
    st.start = g_hold == kHoldStart;
    st.pick = g_hold == kHoldMission ? g_pick : 0;
    st.launch = g_hold == kHoldLaunch || g_launched;
    return st;
}

void Send(Kind kind, std::uint8_t arg, const Status& st) {
    Packet p {kMagic, AvatarLinkBuild(), kVersion, kind, arg, st};
    if (kind == kStatus) AvatarLinkSendRaw(&p, sizeof(p));
    else AvatarLinkSendReliable(&p, sizeof(p));
}

void SendEvent(Kind kind, std::uint8_t arg) { Send(kind, arg, g_sent); }

void ClearHold() {
    g_hold = kHoldNone;
    g_releaseHold = false;
    g_pick = 0;
    SayOff();
}

// A pick (mission 1-3, bit 7 = EX) becomes this game's mission, and the way on is ship selection.
void Adopt(std::uint8_t pick) {
    g_setEx((pick & 0x80) ? 1 : 0);
    g_setMission(pick & 0x7F);
    g_redirect = true;
}

// ---- the holds ----

std::uint64_t Pressed() {
    std::uint64_t bits = 0;
    ReadAny(g_getPad(0) + 8, &bits);
    return bits;
}

std::int32_t ListItem(std::uintptr_t list) {
    std::int32_t cursor = 0, item = -1;
    ReadAny(list + kListCursor, &cursor);
    ReadAny(list + kListValues + static_cast<std::intptr_t>(cursor) * 4, &item);
    return item;
}

// The top menu's list. While held its input doesn't run: the cursor stays on Start and only Circle counts.
std::int32_t TopInput(std::uintptr_t list, std::uintptr_t a2) {
    if (g_go || (g_hold == kHoldStart && g_releaseHold)) {
        g_go = false;
        ClearHold();
        g_session = true;
        *reinterpret_cast<std::int32_t*>(list + kListCursor) = 0;
        Note("gummi menu: our Start goes through");
        return 0;
    }
    if (g_hold == kHoldStart) {
        std::uint64_t cancel = 0;
        ReadAny(list + kListCancel, &cancel);
        if (Pressed() & cancel) {
            ClearHold();
            Note("gummi menu: our Start is called off (Circle)");
        }
        return -1;
    }
    std::int32_t r = g_realSelector(list, a2);
    if (r != 0 || ListItem(list) != 0 || !Linked()) return r;
    std::int32_t route = g_getRoute();
    if (!PartnerHere(route)) {
        SayAbout("%s isn't in the Gummi menu");
        Note("gummi menu: our Start refused: the other player isn't in this Gummi menu");
        return -1;
    }
    bool hasList = MissionCount(Unlocked(route)) >= 2 && MissionCount(g_their.unlocked) >= 2;
    if (AvatarLinkIsHost() && hasList) return r;  // on to the Host's mission list
    g_hold = kHoldStart;
    SayHold(AvatarLinkIsHost() || !hasList ? "Waiting for %s to Start" : "Waiting for %s to select a mission");
    Note("gummi menu: our Start is held (%s)", hasList ? "the Host's mission list" : "no list for both");
    return -1;
}

// The launch's Start / Abort. While held the list keeps running, so Abort can still be chosen (Abort wins).
std::int32_t LaunchInput(std::uintptr_t list, std::uintptr_t a2) {
    if (!g_session || !Linked()) return g_realSelector(list, a2);
    if (g_hold == kHoldLaunch && g_releaseHold) {
        ClearHold();
        *reinterpret_cast<std::int32_t*>(list + kListCursor) = 0;
        Note("gummi menu: both pressed Start: the launch goes");
        return 0;
    }
    std::int32_t r = g_realSelector(list, a2);
    std::int32_t item = ListItem(list);
    if (r == 0 && item == 0) {
        if (g_hold != kHoldLaunch) {
            g_hold = kHoldLaunch;
            SayHold("Waiting for %s to Start");
            Note("gummi menu: our launch Start is held");
        }
        return -1;
    }
    std::uint8_t timerDone = 0;
    ReadAny(list + kLaunchTimerDone, &timerDone);
    if ((r == 1 || (r == 0 && item == 1)) && timerDone && !g_forceFrames) {  // not the Circle we press for the other's Abort
        ClearHold();
        SendEvent(kAbort, 0);
        Note("gummi menu: our Abort: both go back");
    }
    return r;
}

std::int32_t __fastcall HookSelector(std::uintptr_t list, std::uintptr_t a2) {
    Screen s = ReadScreen();
    if (s == kTop) return TopInput(list, a2);
    if (s == kLaunch) return LaunchInput(list, a2);
    return g_realSelector(list, a2);
}

// The Host's mission list. While held its input doesn't run: the cursor stays on the pick and only Circle counts.
std::uintptr_t __fastcall HookMissionInput(std::uintptr_t a1, std::uintptr_t screen, std::uintptr_t a3) {
    if (g_hold == kHoldMission) {
        if (g_releaseHold) {
            ClearHold();
            g_session = true;
            auto* pressed = reinterpret_cast<std::uint64_t*>(g_getPad(0) + 8);
            std::uint64_t saved = *pressed;
            *pressed = kPadConfirm;  // the list's own confirm sets the mission and moves on
            std::uintptr_t r = g_realMissionInput(a1, screen, a3);
            *pressed = saved;
            Note("gummi menu: our mission goes through");
            return r;
        }
        if (Pressed() & kPadCancel) {
            ClearHold();
            Note("gummi menu: our mission pick is called off (Circle)");
        }
        return a1;
    }
    if (!AvatarLinkIsHost() || !Linked() || !(Pressed() & kPadConfirm)) return g_realMissionInput(a1, screen, a3);
    std::int32_t route = g_getRoute(), cursor = 0;
    std::uint8_t ex = 0;
    ReadAny(screen + kMissionCursor, &cursor);
    ReadAny(screen + kMissionEx, &ex);
    if (!PartnerHere(route)) {
        SayAbout("%s isn't in the Gummi menu");
        return a1;
    }
    std::uint8_t bit = static_cast<std::uint8_t>(1 << (cursor + (ex ? 3 : 0)));
    if (!(g_their.unlocked & bit)) {
        SayAbout("%s hasn't unlocked this mission");
        Note("gummi menu: our pick of mission %d%s refused: the other save doesn't have it", cursor + 1, ex ? " EX" : "");
        return a1;
    }
    g_pick = static_cast<std::uint8_t>((cursor + 1) | (ex ? 0x80 : 0));
    g_hold = kHoldMission;
    SayHold("Waiting for %s to Start");
    Note("gummi menu: our pick of mission %d%s is held", cursor + 1, ex ? " EX" : "");
    return a1;
}

// Ship selection: Circle sends both to the top menu (answer 22). The game's own step back then lands there.
std::uint64_t __fastcall HookBodyInput(std::uintptr_t body, std::uintptr_t a2) {
    std::uint64_t r = g_realBodyInput(body, a2);
    if (static_cast<std::int32_t>(r) == 1 && g_session && Linked() && ReadScreen() == kBody) {
        *reinterpret_cast<std::int32_t*>(Ptr(ExeBase() + kMenuStack) + kStackIndex) = 1;
        g_session = false;
        SendEvent(kAbort, 1);
        Note("gummi menu: our Circle in ship selection: both go to the top menu");
    }
    return r;
}

// A Start that adopted the Host's mission goes to ship selection, not to this game's own list.
void __fastcall HookSetScreen(std::uintptr_t stack, std::int32_t next) {
    if (!g_redirect || (next != 1 && next != 2)) return g_realSetScreen(stack, next);
    g_redirect = false;
    g_realSetScreen(stack, 2);
    if (next == 1) g_closeHelp(g_getHelpTask());  // what the way with no list does after the same call
}

// The Host can't land while the Client is in the Gummi menu (answer 17): refused the way the map refuses a locked world.
void __fastcall HookWorldCommand(std::uintptr_t actor, std::uintptr_t command) {
    std::int16_t id = 0;
    if (ReadAny(command + 2, &id) && id == kCmdLand && AvatarLinkIsHost() && TheirsFresh() && g_their.screen != kNone) {
        SceneSyncSayClientInGummi();
        g_refuseA();
        g_refuseB();
        Note("gummi menu: landing refused: the Client is in the Gummi menu");
        return;
    }
    g_realWorldCommand(actor, command);
}

// An Abort of the other player's is pressed for us: Circle is 'back' on every one of these screens.
void ForceFrame(Screen s) {
    if (!g_forceFrames) return;
    if (s == kNone || (g_forceTop ? s == kTop : s != kLaunch) || --g_forceFrames == 0) {
        if (s == kLaunch || (g_forceTop && s != kTop && s != kNone)) Note("gummi menu: gave up going back");
        g_forceFrames = 0;
        return;
    }
    if (g_forceTop) *reinterpret_cast<std::int32_t*>(Ptr(ExeBase() + kMenuStack) + kStackIndex) = 1;
    if (g_forceFrames % kForceEvery == 0) InputInject(kRawCircle, 3);
}

void HostFrame() {
    if (!AvatarLinkIsHost() || !TheirsFresh() || g_releaseHold || g_hold == kHoldNone) return;
    bool theirStart = g_their.start && g_their.screen == kTop && g_their.route == g_sent.route;
    if (theirStart && (g_hold == kHoldMission || g_hold == kHoldStart)) {
        std::uint8_t pick = g_hold == kHoldMission ? g_pick : 1;
        if (g_hold == kHoldStart) Adopt(pick);
        SendEvent(kGoShips, pick);
        g_releaseHold = true;
        Note("gummi menu: both ready: mission %d%s for both", pick & 0x7F, pick & 0x80 ? " EX" : "");
    } else if (g_hold == kHoldLaunch && g_their.launch && g_their.screen == kLaunch && g_session) {
        SendEvent(kGoLaunch, 0);
        g_releaseHold = g_launched = true;
    }
}

void OnAbort(bool toTop) {
    SayAbout("%s aborted");
    Screen s = ReadScreen();
    ClearHold();
    Note("gummi menu: the other player aborted (%s)", toTop ? "both to the top menu" : "back from the launch");
    if (toTop) g_session = false;
    if (s == kLaunch || (toTop && (s == kBody || s == kTiny))) {
        g_forceTop = toTop;
        g_forceFrames = kForceFrames;
    }
}

void OnStatus(const Status& now) {
    Status was = g_their;
    bool known = g_theirValid;
    g_their = now;
    g_theirAt = GetTickCount64();
    g_theirValid = true;
    if (!known || ReadScreen() == kNone) return;
    if (now.start && !was.start) SayAbout("%s ready for the mission");
    if (now.pick && !was.pick) SayAbout("%s selected mission");
    if (now.launch && !was.launch) SayAbout("%s is ready!");
    if (was.screen != kNone && now.screen == kNone && !was.launch) SayAbout("%s wants to leave the mission");
}

}  // namespace

void GummiMenuInit() {
    g_testUnlocked = EnvInt("KH2COOP_GUMMI_CLIENT_UNLOCKED", -1);
    bool code = Code(kMakeWindow, kMakeWindowBytes, sizeof(kMakeWindowBytes)) &&
                Code(kWindowText, kWindowTextBytes, sizeof(kWindowTextBytes)) &&
                Code(kWindowLive, kWindowLiveBytes, sizeof(kWindowLiveBytes)) &&
                Code(kCloseWindow, kCloseWindowBytes, sizeof(kCloseWindowBytes)) &&
                Code(kGetPad, kGetPadBytes, sizeof(kGetPadBytes)) && Code(kGetRoute, kGetRouteBytes, sizeof(kGetRouteBytes)) &&
                Code(kGetMission, kGetMissionBytes, sizeof(kGetMissionBytes)) && Code(kGetEx, kGetExBytes, sizeof(kGetExBytes)) &&
                Code(kSetEx, kSetExBytes, sizeof(kSetExBytes)) &&
                Code(kSetMission, kSetMissionBytes, sizeof(kSetMissionBytes)) &&
                Code(kGetHelpTask, kGetHelpTaskBytes, sizeof(kGetHelpTaskBytes)) &&
                Code(kCloseHelp, kCloseHelpBytes, sizeof(kCloseHelpBytes)) &&
                Code(kRefuseA, kRefuseABytes, sizeof(kRefuseABytes)) && Code(kRefuseB, kRefuseBBytes, sizeof(kRefuseBBytes));
    if (!code || !LinkMenuOn() || !InputInjectEnable()) {
        Log("gummi menu: off (%s)", code ? "no message lookup hook or input hook" : "the menu code differs: other game build?");
        return;
    }
    const std::uintptr_t base = ExeBase();
    g_makeWindow = reinterpret_cast<PFN_Make>(base + kMakeWindow);
    g_windowText = reinterpret_cast<PFN_Text>(base + kWindowText);
    g_windowLive = reinterpret_cast<PFN_Live>(base + kWindowLive);
    g_closeWindow = reinterpret_cast<PFN_Close>(base + kCloseWindow);
    g_getPad = reinterpret_cast<PFN_GetPad>(base + kGetPad);
    g_getRoute = reinterpret_cast<PFN_GetInt>(base + kGetRoute);
    g_getMission = reinterpret_cast<PFN_GetInt>(base + kGetMission);
    g_getEx = reinterpret_cast<PFN_GetInt>(base + kGetEx);
    g_setEx = reinterpret_cast<PFN_SetEx>(base + kSetEx);
    g_setMission = reinterpret_cast<PFN_SetInt>(base + kSetMission);
    g_getHelpTask = reinterpret_cast<PFN_GetPtr>(base + kGetHelpTask);
    g_closeHelp = reinterpret_cast<PFN_Close>(base + kCloseHelp);
    g_refuseA = reinterpret_cast<PFN_Void>(base + kRefuseA);
    g_refuseB = reinterpret_cast<PFN_Void>(base + kRefuseB);
    g_on = HookFunction(kSelector, kSelectorBytes, sizeof(kSelectorBytes), reinterpret_cast<void*>(HookSelector),
                        reinterpret_cast<void**>(&g_realSelector), "Gummi menu selector") &&
           HookFunction(kBodyInput, kBodyInputBytes, sizeof(kBodyInputBytes), reinterpret_cast<void*>(HookBodyInput),
                        reinterpret_cast<void**>(&g_realBodyInput), "Gummi ship selection input") &&
           HookFunction(kMissionInput, kMissionInputBytes, sizeof(kMissionInputBytes),
                        reinterpret_cast<void*>(HookMissionInput), reinterpret_cast<void**>(&g_realMissionInput),
                        "Gummi mission list input") &&
           HookFunction(kSetScreen, kSetScreenBytes, sizeof(kSetScreenBytes), reinterpret_cast<void*>(HookSetScreen),
                        reinterpret_cast<void**>(&g_realSetScreen), "Gummi menu screen change") &&
           HookFunction(kWorldCommand, kWorldCommandBytes, sizeof(kWorldCommandBytes),
                        reinterpret_cast<void*>(HookWorldCommand), reinterpret_cast<void**>(&g_realWorldCommand),
                        "World Map command");
    Log("gummi menu: %s", g_on ? "on: both players start a Gummi mission together" : "off (a hook failed)");
}

const std::uint8_t* GummiMenuText() { return g_textSet ? g_text : nullptr; }

bool GummiMenuPartnerThere(std::int32_t route) { return g_on && PartnerHere(route); }

bool GummiMenuLinked() { return g_on && Linked(); }

std::uint64_t* GummiMenuPressed() { return g_on ? reinterpret_cast<std::uint64_t*>(g_getPad(0) + 8) : nullptr; }

void GummiMenuCountClear() {
    if (!g_on) return;
    const std::int32_t route = g_getRoute(), mission = g_getMission();
    const bool ex = (g_getEx() & 0xFF) != 0;
    auto* const record = reinterpret_cast<std::uint8_t*>(Record(route, mission, ex));
    if (record[kRecMission] == 1 && !record[kRecCleared]) {  // as the tally's own first clear does (exe+0x209B50)
        *reinterpret_cast<std::uint8_t*>(Record(route, 1, false) + kRecOpen) = 1;
        *reinterpret_cast<std::uint8_t*>(Record(route, 2, false) + kRecOpen) = 1;
    }
    record[kRecCleared] = 1;
    Log("gummi menu: the other player's clear counts here: route %d mission %d%s", route, mission, ex ? " EX" : "");
}

void GummiMenuFrame() {
    if (!g_on) return;
    const std::uintptr_t base = ExeBase();
    Screen s = ReadScreen();
    if (s == kNone) {
        g_sawHelpScreen = false;
    } else if (s == kTop || s == kMission) {
        g_sawHelpScreen = true;
    }
    BarFrame(s != kNone && Ptr(base + kLiveWindows) && Ptr(base + kScheduler) && g_sawHelpScreen);
    if (s != g_lastScreen) {
        Note("gummi menu: screen %d (was %d)", s, g_lastScreen);
        if ((g_lastScreen == kBody || g_lastScreen == kTiny || g_lastScreen == kLaunch) && (s == kTop || s == kMission))
            g_session = false;
        g_lastScreen = s;
    }
    if (s == kNone) {
        g_session = g_launched = g_go = g_redirect = false;
        g_forceFrames = 0;
        g_sayFrames = 0;
        ClearHold();
    } else if ((g_hold == kHoldStart && s != kTop) || (g_hold == kHoldMission && s != kMission) ||
               (g_hold == kHoldLaunch && s != kLaunch) || (g_hold != kHoldNone && !Linked())) {
        ClearHold();
    }
    ForceFrame(s);
    Status now = Mine(s);
    if (Linked() && (++g_frame % kStatusEvery == 0 || std::memcmp(&now, &g_sent, sizeof(now)) != 0))
        Send(kStatus, 0, now);
    g_sent = now;
    HostFrame();
}

void GummiMenuOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.magic != kMagic || p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.kind == kStatus) {
        OnStatus(p.status);
    } else if (p.kind == kAbort) {
        OnAbort(p.arg != 0);
    } else if (AvatarLinkIsHost()) {
        return;  // the two below come from the Host
    } else if (p.kind == kGoShips) {
        if (ReadScreen() != kTop) {  // we left the top menu just as the Host's word came: the Host's ship selection ends
            Note("gummi menu: the Host's mission came with us away from the top menu: sent back");
            SendEvent(kAbort, 1);
            return;
        }
        Adopt(p.arg);
        g_go = true;
    } else if (p.kind == kGoLaunch) {
        if (g_hold == kHoldLaunch) g_releaseHold = g_launched = true;
    }
}

}  // namespace kh2coop
