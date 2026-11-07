// The Gummi menu's help bar (TODO 5.2, route 2 of session 58: co-op text shown in the Gummi menus' own help
// bar, so it looks native). Gummi mode has no field HUD, so the INFO bar can't show there (VERIFIED_OFFSETS
// row *(exe+0xABE828)).
//
// TEST BUILD (session 58), GUMMI_HELP_TEST in kh2coop_player.ini [player]; off by default (nothing here runs).
// Both values log the menu's state at each change.
// 1: a test sentence in the help bar on every Gummi menu screen, 20 frames after it opens.
// 2: a look test at the top right: a copy of the orange help bar, shrunk (L211) and moved (L210), drawn with its
//    body and text marker only (Dale's portrait and the sparkles dropped: the help bar below shows Dale). Every
//    4 s another of three text lengths, pushed right by leading spaces to the box's middle: the text's anchor
//    comes from the layout data the help bar below shares, so only the text itself can move it. On the Gummi
//    Editor's screens it closes (it would cover the editor's cost); a test message every 12 s there pops it in
//    for 4 s. Our window is kept across frames only while the menu runs without a break since we opened it (its
//    teardown frees every window, and addresses repeat across visits), and each frame only while the game's own
//    check exe+0x251B60 says it is live and it still carries our offset and scale (a new window of the game's at
//    the same address would have 0 and 1.0).
//
// How (Ghidra leads L206-L208):
// - The menu runs while *(exe+0xAFB420) (its pool) and *(exe+0xAF9FA0) are both non-zero (L207); only then is
//   *(exe+0xAF9F00) this visit's help object (it is never cleared). Nothing is kept across frames: each frame
//   reads them again.
// - exe+0x2437B0(id) sets the help bar's text to message `id`; it does nothing when there is no help object,
//   no window at help+8, or the window isn't live (the game's own check, L206). The window stores the pointer
//   the message lookup gave, once: link_menu.cpp's hook answers our id 0x7F42 with our static buffer.
// - `body`, `tiny` and `lastcheck` have no help window. We open one with the help task's own four calls
//   (L208) and put it at help+8; the help task closes it at the next screen change, as it does its own.
//   A cursor move there sets the item's help id (-1 on the launch's Start / Abort = an empty bar).

#include "gummi_menu.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "common.hpp"
#include "link_menu.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kMenuPool = 0xAFB420;   // ptr: the Gummi menu mode's pool, 0 = not running (L207)
constexpr std::uintptr_t kMenuModel = 0xAF9FA0;  // ptr: set with the pool at menu start, 0 at its end (L207)
constexpr std::uintptr_t kHelpObj = 0xAF9F00;    // ptr: gm::GUMIMENUHELP, its window at +8 (L206); stale after the end
constexpr std::uintptr_t kScreen = 0xAF9FB0;     // char[]: the menu screen's name ("top", "body", ...) (L206)
constexpr std::uintptr_t kLiveWindows = 0xAFB4B8;  // ptr: the live window list (L206)
constexpr std::uintptr_t kScheduler = 0xAFB4E0;    // ptr: the window scheduler; exe+0x251C40 doesn't check it (L208)

constexpr std::uintptr_t kSetHelp = 0x2437B0;  // (id): help window text = message id
constexpr std::uint8_t kSetHelpBytes[] = {0x8B, 0xD1, 0x48, 0x8B, 0x0D};
constexpr std::uintptr_t kNewWindow = 0x2529A0;  // (type) -> window
constexpr std::uint8_t kNewWindowBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xF9};
constexpr std::uintptr_t kAddWindow = 0x251C40;  // (window, 0, priority): on the window scheduler
constexpr std::uint8_t kAddWindowBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xC0};
constexpr std::uintptr_t kTypeWindow = 0x271710;  // (window, type): its look and animations
constexpr std::uint8_t kTypeWindowBytes[] = {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1};
constexpr std::uintptr_t kOpenWindow = 0x271210;  // (window): plays its open animation
constexpr std::uint8_t kOpenWindowBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xC0};

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

// Window fields (L210-L212); the window's YI::SEQUENCE is at +0x40.
constexpr std::uintptr_t kSeqFlags = 0x40 + 0xE8;       // u32, bit 1: scale by the float below
constexpr std::uintptr_t kSeqScale = 0x40 + 0x178;      // f32, all axes (x, y at +4, +8), 1.0 from init
constexpr std::uintptr_t kSeqListBuilt = 0x40 + 0xF0;   // u8: the group's animation list is built (until a group change)
constexpr std::uintptr_t kSeqAnimCount = 0x40 + 0x100;  // u32: animations drawn from that list
constexpr std::uintptr_t kSeqGroup = 0x40 + 0x158;      // s32: the animation group playing
constexpr std::uintptr_t kTextSize = 0x1E4;             // s32: the text's font size
constexpr std::uintptr_t kHold = 0x288;                 // u32, bit 2: keep kTextSize (else the group's each frame)
constexpr std::uintptr_t kOffsetX = 0x3B4, kOffsetY = 0x3B8;  // s32: moves the whole window

constexpr int kFlipFrames = 240;    // 4 s each text, and a pop-in's length
constexpr int kEditorMessageFrames = 720;  // a test message every 12 s on the editor's screens
constexpr float kSmallScale = 0.6f;
constexpr int kSmallFont = 10;      // the bar's own: 16
constexpr int kSmallX = 181, kSmallY = -345;  // box center (256, 376) -> (437, 31): top right (tests 2a: 154, 2b: 174)
// Every group of the orange bar starts with its body and text marker, then Dale (then 6 sparkles when idle).
constexpr std::uint32_t kBarAnims = 2;
// Centered text moves right by half its leading spaces: 12 centered it in the box (test 2b, about 3 units each).
constexpr const char* kPad = "            ";
constexpr const char* kLookTexts[] = {"Partner ready", "Waiting for your partner.",
                                      "Your partner is in the Gummi Editor."};  // 13, 25, 36 letters
constexpr int kMaxLookLines = 120;

constexpr int kHelpType = 0xF;           // the help bar window (help task exe+0x251E60)
constexpr int kHelpPriority = 0x1E461;    // its scheduler priority there
constexpr int kHelpLook = 0x15A;          // its look on `top` / `mission` / `factory` ('o', table exe+0x7358C0)
constexpr int kMsgGummi = 0x7F42;         // our message id (link_menu.cpp: 0x7F40, 0x7F41; gumi.bar ends at 0x54C0)
constexpr int kWaitFrames = 20;           // after a screen change: the help task has opened or closed its window
constexpr int kMaxLogLines = 300;

using PFN_Int = void(__fastcall*)(int);
using PFN_New = std::uintptr_t(__fastcall*)(int);
using PFN_Add = void(__fastcall*)(std::uintptr_t, std::uintptr_t, int);
using PFN_Type = void(__fastcall*)(std::uintptr_t, int);
using PFN_Open = void(__fastcall*)(std::uintptr_t);
using PFN_Make = std::uintptr_t(__fastcall*)(int, int, int);
using PFN_Live = bool(__fastcall*)(std::uintptr_t);

bool g_test = false;
int g_mode = 0;
PFN_Make g_makeWindow = nullptr;
PFN_Type g_windowText = nullptr;
PFN_Live g_windowLive = nullptr;
PFN_Open g_closeWindow = nullptr;
std::uintptr_t g_ours = 0;  // our look test window, 0 = none; never kept past a frame the menu isn't running
bool g_closing = false;     // we closed it: it plays its close, then deletes itself
int g_popFrames = 0;        // frames left of a pop-in on an editor screen
int g_openWait = 0;         // frames before trying again when the game gave no window
int g_lookText = -1;        // the kLookTexts entry shown
std::int32_t g_dropGroup = -1;  // the animation group we last cut to kBarAnims (for the log)
int g_lookFrames = 0, g_lookLines = 0;
PFN_Int g_setHelp = nullptr;
PFN_New g_newWindow = nullptr;
PFN_Add g_addWindow = nullptr;
PFN_Type g_typeWindow = nullptr;
PFN_Open g_openWindow = nullptr;

std::uint8_t g_text[96] = {};
bool g_textSet = false;
char g_lastScreen[24] = {};
int g_sinceChange = 0;
bool g_done = false;  // this screen's test sentence is shown
bool g_sawHelpScreen = false;  // this visit showed a help screen: the screen name is this visit's, not the last one's
std::uintptr_t g_logged[6] = {};
int g_logLines = 0;

bool Code(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n) {
    return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0;
}

std::uintptr_t Ptr(std::uintptr_t at) {
    std::uintptr_t v = 0;
    return ReadAny(at, &v) ? v : 0;
}

// The game made it this frame: nothing else has touched it yet.
std::uintptr_t MakeLookWindow() {
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

void LookLog(const char* what, std::uintptr_t w) {
    if (g_lookLines++ < kMaxLookLines)
        Log("gummi menu: look test: %s (window %llx)", what, static_cast<unsigned long long>(w));
}

// Still our window: live by the game's own check, and still carrying the marks MakeLookWindow wrote.
bool StillOurs(std::uintptr_t w) {
    std::int32_t x = 0, y = 0;
    float scale = 0;
    return g_windowLive(w) && ReadAny(w + kOffsetX, &x) && x == kSmallX && ReadAny(w + kOffsetY, &y) &&
           y == kSmallY && ReadAny(w + kSeqScale, &scale) && scale == kSmallScale;
}

// Mode 2, each frame. `ready`: the menu runs, its window lists exist, and this visit showed a help screen.
// `editor`: a Gummi Editor screen (your pick: closed there, a new message pops it in for 4 s).
void LookTestFrame(bool ready, bool editor) {
    if (!ready) {
        if (g_ours) LookLog("the menu ended, the game frees our window", g_ours);
        g_ours = 0;
        g_closing = false;
        g_popFrames = 0;
        g_lookFrames = 0;
        return;
    }
    if (g_ours && !StillOurs(g_ours)) {
        LookLog(g_closing ? "our window closed" : "our window is gone", g_ours);
        g_ours = 0;
        g_closing = false;
    }
    std::int32_t group = 0;
    std::uint8_t built = 0;
    std::uint32_t count = 0;
    if (g_ours && ReadAny(g_ours + kSeqListBuilt, &built) && built && ReadAny(g_ours + kSeqAnimCount, &count) &&
        count > kBarAnims && ReadAny(g_ours + kSeqGroup, &group)) {
        *reinterpret_cast<std::uint32_t*>(g_ours + kSeqAnimCount) = kBarAnims;  // holds until the next group change
        if (group != g_dropGroup) {
            char line[64];
            std::snprintf(line, sizeof(line), "Dale dropped in group %d (%u animations)", group, count);
            LookLog(line, g_ours);
        }
        g_dropGroup = group;
    }
    if (g_lookFrames++ % (editor ? kEditorMessageFrames : kFlipFrames) == 0) {  // a test message
        g_lookText = (g_lookText + 1) % static_cast<int>(sizeof(kLookTexts) / sizeof(kLookTexts[0]));
        char line[80];
        std::snprintf(line, sizeof(line), "%s%s", kPad, kLookTexts[g_lookText]);
        LinkMenuEncode(line, g_text, sizeof(g_text));
        if (editor) g_popFrames = kFlipFrames;
        std::snprintf(line, sizeof(line), "text '%s'%s", kLookTexts[g_lookText], editor ? ", pops in" : "");
        LookLog(line, g_ours);
    }
    bool show = !editor || g_popFrames > 0;
    if (g_popFrames > 0) --g_popFrames;
    if (show && !g_ours) {  // while ours still closes, the new one waits until it's gone
        if (g_openWait > 0) {
            --g_openWait;
            return;
        }
        g_ours = MakeLookWindow();
        g_openWait = g_ours ? 0 : kFlipFrames;
        g_dropGroup = -1;
        LookLog("opened the small help bar", g_ours);
    } else if (!show && g_ours && !g_closing) {
        g_closeWindow(g_ours);  // the Dale cut above goes on until StillOurs lets go
        g_closing = true;
        LookLog("closing it: an editor screen", g_ours);
    }
}

bool HelpScreen(const char* s) {
    static const char* const kHelpScreens[] = {"top", "mission", "factory", "collection", "tutorial"};
    for (const char* h : kHelpScreens)
        if (std::strcmp(s, h) == 0) return true;
    return false;
}

// The Gummi Editor's screens (runs kh2coop_23488, kh2coop_30148: top -> factory -> fbody -> edit; ftiny).
bool EditorScreen(const char* s) {
    static const char* const kEditorScreens[] = {"factory", "fbody", "edit", "ftiny"};
    for (const char* e : kEditorScreens)
        if (std::strcmp(s, e) == 0) return true;
    return false;
}

}  // namespace

void GummiMenuInit() {
    g_mode = EnvInt("KH2COOP_GUMMI_HELP_TEST", 0);
    g_test = g_mode == 1 || g_mode == 2;
    if (!g_test) return;
    bool code = Code(kSetHelp, kSetHelpBytes, sizeof(kSetHelpBytes)) &&
                Code(kNewWindow, kNewWindowBytes, sizeof(kNewWindowBytes)) &&
                Code(kAddWindow, kAddWindowBytes, sizeof(kAddWindowBytes)) &&
                Code(kTypeWindow, kTypeWindowBytes, sizeof(kTypeWindowBytes)) &&
                Code(kOpenWindow, kOpenWindowBytes, sizeof(kOpenWindowBytes));
    if (code && LinkMenuOn()) {
        g_setHelp = reinterpret_cast<PFN_Int>(ExeBase() + kSetHelp);
        g_newWindow = reinterpret_cast<PFN_New>(ExeBase() + kNewWindow);
        g_addWindow = reinterpret_cast<PFN_Add>(ExeBase() + kAddWindow);
        g_typeWindow = reinterpret_cast<PFN_Type>(ExeBase() + kTypeWindow);
        g_openWindow = reinterpret_cast<PFN_Open>(ExeBase() + kOpenWindow);
    }
    if (g_mode == 2) {
        if (g_setHelp && Code(kMakeWindow, kMakeWindowBytes, sizeof(kMakeWindowBytes)) &&
            Code(kWindowText, kWindowTextBytes, sizeof(kWindowTextBytes)) &&
            Code(kWindowLive, kWindowLiveBytes, sizeof(kWindowLiveBytes)) &&
            Code(kCloseWindow, kCloseWindowBytes, sizeof(kCloseWindowBytes))) {
            g_makeWindow = reinterpret_cast<PFN_Make>(ExeBase() + kMakeWindow);
            g_windowText = reinterpret_cast<PFN_Type>(ExeBase() + kWindowText);
            g_windowLive = reinterpret_cast<PFN_Live>(ExeBase() + kWindowLive);
            g_closeWindow = reinterpret_cast<PFN_Open>(ExeBase() + kCloseWindow);
            g_textSet = true;  // g_text is written before our window opens
        }
        Log("gummi menu: TEST 2 on: %s",
            g_makeWindow ? "the top right look test (small help bar without Dale, 3 text lengths)"
                         : "log only (other window code, or no message lookup hook)");
        return;
    }
    Log("gummi menu: TEST on: %s", g_setHelp ? "a test sentence in the help bar of each Gummi menu screen"
                                             : code ? "log only (no message lookup hook)"
                                                    : "log only (the menu code differs: other game build?)");
}

const std::uint8_t* GummiMenuText() { return g_textSet ? g_text : nullptr; }

void GummiMenuFrame() {
    if (!g_test) return;
    const std::uintptr_t base = ExeBase();
    std::uintptr_t pool = Ptr(base + kMenuPool), model = Ptr(base + kMenuModel);
    bool alive = pool && model;
    std::uintptr_t help = alive ? Ptr(base + kHelpObj) : 0;
    std::uintptr_t window = help ? Ptr(help + 8) : 0;
    std::uintptr_t live = Ptr(base + kLiveWindows), sched = Ptr(base + kScheduler);
    std::uint64_t name[3] = {};  // the screen name, 16 chars at most, zero-ended by name[2]
    if (alive) {
        ReadAny(base + kScreen, &name[0]);
        ReadAny(base + kScreen + 8, &name[1]);
    }
    const char* screen = reinterpret_cast<const char*>(name);

    // the two lists as set / unset: their pointers may move with every window
    std::uintptr_t now[6] = {pool, model, help, window, live != 0, sched != 0};
    bool screenChanged = std::strcmp(screen, g_lastScreen) != 0;
    if ((screenChanged || std::memcmp(now, g_logged, sizeof(now)) != 0) && g_logLines < kMaxLogLines) {
        ++g_logLines;
        Log("gummi menu: pool %llx model %llx help %llx window %llx live list %s scheduler %s screen '%s'",
            static_cast<unsigned long long>(pool), static_cast<unsigned long long>(model),
            static_cast<unsigned long long>(help), static_cast<unsigned long long>(window), live ? "set" : "0",
            sched ? "set" : "0", screen);
        std::memcpy(g_logged, now, sizeof(now));
    }
    if (screenChanged) {
        strncpy_s(g_lastScreen, screen, _TRUNCATE);
        g_sinceChange = 0;
        g_done = false;
    }
    if (!alive) g_sawHelpScreen = false;
    else if (HelpScreen(screen)) g_sawHelpScreen = true;
    if (g_mode == 2) {
        if (g_makeWindow) LookTestFrame(alive && live && sched && g_sawHelpScreen, EditorScreen(screen));
        return;
    }
    if (!alive || !help || !g_setHelp || g_done || !screen[0] || ++g_sinceChange < kWaitFrames) return;
    g_done = true;

    bool ours = false;
    if (!window && !HelpScreen(screen)) {
        if (!live || !sched || !g_sawHelpScreen) {
            Log("gummi menu: test: no help window opened on '%s' (live list %s, scheduler %s, help screen seen %d)",
                screen, live ? "set" : "0", sched ? "set" : "0", g_sawHelpScreen);
            return;
        }
        std::uintptr_t w = g_newWindow(kHelpType);
        if (!w) {
            Log("gummi menu: test: the game gave no window on '%s'", screen);
            return;
        }
        g_addWindow(w, 0, kHelpPriority);
        g_typeWindow(w, kHelpLook);
        g_openWindow(w);
        *reinterpret_cast<std::uintptr_t*>(help + 8) = w;  // the help task closes it at the next screen change
        window = w;
        ours = true;
    }
    char line[80];
    std::snprintf(line, sizeof(line), "Co op test on %s: Waiting for your partner.", screen);
    LinkMenuEncode(line, g_text, sizeof(g_text));
    g_textSet = true;
    g_setHelp(kMsgGummi);
    Log("gummi menu: test: sentence set on '%s' (window %llx%s)", screen, static_cast<unsigned long long>(window),
        ours ? ", opened by us" : "");
}

}  // namespace kh2coop
