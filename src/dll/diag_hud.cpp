// Diagnostic panel (diag_hud.hpp). The game thread gathers the numbers four times a second into a few lines of text
// (DiagHudFrame); the panel's own thread owns a small see-through window that stays over the game's window on the
// middle left, never takes focus or clicks, and paints those lines. It shows only while the game's window is in front.

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "avatar_link.hpp"
#include "common.hpp"
#include "diag_hud.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;  // u8 world, room
constexpr int kMaxLines = 14;
constexpr COLORREF kWhite = RGB(235, 235, 240), kGrey = RGB(160, 165, 180), kGood = RGB(90, 220, 120),
                   kWarn = RGB(240, 200, 70), kBad = RGB(240, 80, 80);

struct Line {
    wchar_t text[96];
    COLORREF color;
};
SRWLOCK g_lock = SRWLOCK_INIT;
Line g_lines[kMaxLines];
int g_lineCount = 0;
COLORREF g_accent = kGrey;
volatile LONG g_visible = 0;

// ---- Game thread: the numbers ----

struct Sample {
    std::uint32_t received, gaps;
};
constexpr int kSamples = 20;  // 5 s of 250 ms samples
Sample g_samples[kSamples];
int g_sampleCount = 0, g_sampleNext = 0;
std::uint32_t g_frames = 0;
ULONGLONG g_lastMs = 0;
std::uint32_t g_lastSent = 0, g_lastReceived = 0;
int g_fps = 0, g_outPerSec = 0, g_inPerSec = 0;

Line* g_build = nullptr;
int g_buildCount = 0;
void Add(COLORREF color, const wchar_t* fmt, ...) {
    if (g_buildCount >= kMaxLines) return;
    Line& l = g_build[g_buildCount++];
    va_list args;
    va_start(args, fmt);
    std::vswprintf(l.text, sizeof(l.text) / sizeof(l.text[0]), fmt, args);
    va_end(args);
    l.color = color;
}

COLORREF Worst(COLORREF a, COLORREF b) {
    auto rank = [](COLORREF c) { return c == kBad ? 2 : c == kWarn ? 1 : 0; };
    return rank(a) >= rank(b) ? a : b;
}

void ReadRoom(std::uint8_t& world, std::uint8_t& room) {
    __try {
        world = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow);
        room = *reinterpret_cast<const volatile std::uint8_t*>(ExeBase() + kNow + 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void Gather() {
    Line lines[kMaxLines];
    g_build = lines;
    g_buildCount = 0;
    COLORREF accent = kGrey;
    std::uint8_t world = 0, room = 0;
    ReadRoom(world, room);
    if (!CoopActive()) {
        Add(kWhite, L"KH2 CO-OP   (F3 hides)");
        Add(kGrey, L"Co-op is off");
        Add(kGrey, L"Room %02X/%02X   FPS %d", world, room, g_fps);
    } else {
        LinkDiag l;
        AvatarLinkDiag(l);
        WorldDiag w;
        WorldSyncDiag(w);
        // Loss over the last 5 s: missing packets against all the peer sent.
        g_samples[g_sampleNext] = {l.received, l.gaps};
        g_sampleNext = (g_sampleNext + 1) % kSamples;
        if (g_sampleCount < kSamples) ++g_sampleCount;
        const Sample& old = g_samples[(g_sampleNext - g_sampleCount + kSamples) % kSamples];
        std::uint32_t got = l.received - old.received, missed = l.gaps - old.gaps;
        double loss = got + missed ? 100.0 * missed / (got + missed) : 0.0;

        bool waiting = !l.peerKnown || l.peerAgeMs == 0xFFFFFFFFu;
        COLORREF ageColor = l.peerAgeMs > 2000 ? kBad : l.peerAgeMs > 500 ? kWarn : kGood;
        COLORREF lossColor = loss > 10.0 ? kBad : loss > 2.0 ? kWarn : kGood;
        COLORREF pingColor = l.rttMs < 0 ? kGrey : l.rttMs > 300 ? kBad : l.rttMs > 150 ? kWarn : kGood;
        COLORREF jitterColor = l.jitterMs > 150 ? kBad : l.jitterMs > 80 ? kWarn : kGood;
        Add(kWhite, L"KH2 CO-OP   %s   %s   (F3 hides)", l.host ? L"HOST" : L"FRIEND", l.steam ? L"Steam" : L"UDP");
        if (waiting) {
            accent = kWarn;
            Add(kWarn, L"Connection: waiting for the other player");
        } else {
            accent = Worst(Worst(ageColor, lossColor), Worst(pingColor == kGrey ? kGood : pingColor, jitterColor));
            Add(accent, L"Connection: %s", accent == kBad ? (l.peerAgeMs > 2000 ? L"LOST" : L"BAD")
                                            : accent == kWarn ? L"UNSTABLE" : L"GOOD");
            if (l.rttMs >= 0)
                Add(pingColor, L"Ping %d ms", l.rttMs);
            else
                Add(kGrey, L"Ping: no answer yet (other PC on an older build?)");
            Add(jitterColor, L"Jitter %d ms", l.jitterMs);
            Add(lossColor, L"Loss %.1f %% (last 5 s)   late %u", loss, l.late);
            Add(ageColor, L"Last packet %u ms ago", l.peerAgeMs);
        }
        Add(kGrey, L"Packets/s  out %d   in %d", g_outPerSec, g_inPerSec);
        Add(kGrey, L"Playback delay %d ms", l.interpMs);
        bool sameRoom = waiting || (l.peerWorld == world && l.peerRoom == room);
        if (waiting)
            Add(kGrey, L"Room %02X/%02X", world, room);
        else
            Add(sameRoom ? kGrey : kWarn, L"Room %02X/%02X   other player %02X/%02X", world, room, l.peerWorld, l.peerRoom);
        if (w.on && w.host) {
            Add(kGrey, L"Enemies sent %d   (%d part%s a frame)", w.hostEnemies, w.partsLastFrame,
                w.partsLastFrame == 1 ? L"" : L"s");
        } else if (w.on) {
            COLORREF worldColor = w.hostPacketAgeMs == 0xFFFFFFFFu ? kGrey
                                  : w.hostPacketAgeMs > 2000       ? kBad
                                  : w.hostPacketAgeMs > 500        ? kWarn
                                                                   : kGrey;
            Add(kGrey, L"Enemies  host %d   here %d   copying %d", w.hostEnemies, w.ourEnemies, w.bound);
            if (w.hostPacketAgeMs != 0xFFFFFFFFu) Add(worldColor, L"Host world update %u ms ago", w.hostPacketAgeMs);
            if (worldColor != kGrey) accent = Worst(accent, worldColor);
        }
        Add(kGrey, L"FPS %d", g_fps);
    }
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < g_buildCount; ++i) g_lines[i] = lines[i];
    g_lineCount = g_buildCount;
    g_accent = accent;
    ReleaseSRWLockExclusive(&g_lock);
}

// ---- Panel thread: the window ----

HWND g_game = nullptr, g_panel = nullptr;
int g_fontHeight = 16;
bool g_f3Down = false;

BOOL CALLBACK FindGame(HWND hwnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || hwnd == g_panel || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;
    g_game = hwnd;
    return FALSE;
}

void Place() {
    if (!g_game || !IsWindow(g_game)) {
        g_game = nullptr;
        EnumWindows(FindGame, 0);
    }
    bool front = g_game && static_cast<HWND>(RealForegroundWindow()) == g_game;
    bool f3 = front && (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    if (f3 && !g_f3Down) {
        LONG now = InterlockedXor(&g_visible, 1) ^ 1;
        Log("diagnostic panel: %s (F3)", now ? "shown" : "hidden");
    }
    g_f3Down = f3;
    RECT client {};
    bool show = g_visible && front && !IsIconic(g_game) && GetClientRect(g_game, &client) && client.bottom > 0;
    if (!show) {
        if (IsWindowVisible(g_panel)) ShowWindow(g_panel, SW_HIDE);
        return;
    }
    POINT origin {0, 0};
    ClientToScreen(g_game, &origin);
    g_fontHeight = (std::max)(14, static_cast<int>(client.bottom / 48));
    AcquireSRWLockShared(&g_lock);
    int lines = g_lineCount;
    ReleaseSRWLockShared(&g_lock);
    int pad = g_fontHeight / 2, lineHeight = g_fontHeight + g_fontHeight / 5;
    int width = g_fontHeight * 20, height = pad * 2 + (std::max)(1, lines) * lineHeight;
    int x = origin.x + g_fontHeight, y = origin.y + (client.bottom - height) / 2;
    SetWindowPos(g_panel, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_panel, nullptr, FALSE);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT r;
    GetClientRect(hwnd, &r);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 24));
    FillRect(mem, &r, bg);
    DeleteObject(bg);
    Line lines[kMaxLines];
    int count;
    COLORREF accent;
    AcquireSRWLockShared(&g_lock);
    count = g_lineCount;
    for (int i = 0; i < count; ++i) lines[i] = g_lines[i];
    accent = g_accent;
    ReleaseSRWLockShared(&g_lock);
    RECT bar {0, 0, (std::max)(3, g_fontHeight / 5), r.bottom};
    HBRUSH accentBrush = CreateSolidBrush(accent);
    FillRect(mem, &bar, accentBrush);
    DeleteObject(accentBrush);
    HFONT font = CreateFontW(-g_fontHeight, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    int pad = g_fontHeight / 2, lineHeight = g_fontHeight + g_fontHeight / 5;
    for (int i = 0; i < count; ++i) {
        SetTextColor(mem, lines[i].color);
        TextOutW(mem, bar.right + pad, pad + i * lineHeight, lines[i].text, static_cast<int>(std::wcslen(lines[i].text)));
    }
    BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldFont);
    DeleteObject(font);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            Place();
            return 0;
        case WM_PAINT:
            Paint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI PanelThread(void*) {
    // The game's window is measured in real pixels: be per-monitor DPI aware on this thread (Windows 10+).
    using PFN_SetDpi = HANDLE(WINAPI*)(HANDLE);
    if (auto setDpi = reinterpret_cast<PFN_SetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext")))
        setDpi(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)));  // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    WNDCLASSEXW wc {sizeof(wc)};
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"kh2coop_diag_panel";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        Log("diagnostic panel: window class not registered (error %lu)", GetLastError());
        return 0;
    }
    g_panel = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                              wc.lpszClassName, L"KH2 Co-op diagnostics", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr,
                              wc.hInstance, nullptr);
    if (!g_panel) {
        Log("diagnostic panel: window not made (error %lu)", GetLastError());
        return 0;
    }
    SetLayeredWindowAttributes(g_panel, 0, 215, LWA_ALPHA);
    SetTimer(g_panel, 1, 100, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

}  // namespace

void DiagHudInit() {
    g_visible = EnvInt("KH2COOP_DIAG_HUD", 0) == 1 ? 1 : 0;
    HANDLE t = CreateThread(nullptr, 0, PanelThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    Log("diagnostic panel: %s at the start; F3 shows / hides it (DIAG_HUD in kh2coop_player.ini)",
        g_visible ? "shown" : "hidden");
}

void DiagHudFrame() {
    ++g_frames;
    ULONGLONG now = GetTickCount64();
    if (!g_lastMs) g_lastMs = now;
    if (now - g_lastMs < 250 || !g_visible) {
        if (!g_visible) g_lastMs = 0, g_frames = 0;
        return;
    }
    ULONGLONG elapsed = now - g_lastMs;
    g_fps = static_cast<int>(g_frames * 1000 / elapsed);
    LinkDiag l;
    AvatarLinkDiag(l);
    g_outPerSec = static_cast<int>((l.sent - g_lastSent) * 1000 / elapsed);
    g_inPerSec = static_cast<int>((l.received - g_lastReceived) * 1000 / elapsed);
    g_lastSent = l.sent;
    g_lastReceived = l.received;
    g_frames = 0;
    g_lastMs = now;
    Gather();
}

}  // namespace kh2coop
