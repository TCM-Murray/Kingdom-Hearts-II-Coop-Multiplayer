// A story fight's mission values are the host's and the friend's game shows them (TODO 1.22: "owned by the host and
// shown on the friend"; you, 2026-10-15: "Implement 1, 2 and 4", 1 = the mission gauges, timers and counters).
//
// The mission (the object at exe+0x2A0FF68, VERIFIED_OFFSETS 'Mission object') holds the HUD's timer, 3 counters and
// 3 gauges: Data Demyx's 30 s timer and clone counter, Luxord's two time gauges, the 1000 Heartless count. Both games
// run the mission's own script (scene_sync.cpp), but much of what changes the values didn't run in the friend's game:
// the bosses' scripts are skipped there (one brain; Data Demyx's B_EX120_HB_LV99 sets up his timer and counter, Luxord's
// B_EX150_LV99 drains his gauges every frame), and the engine changes some values itself (prizes, beaten enemies). So
// the friend's HUD showed other values or none, and its mission could end or fail on its own.
// Here:
//   host:   sends its mission's values every frame ("KH2N"): which mission (its file name, +0x40) in which room, the
//           timer (exe+0xABB850..0xABB868: clock, pause bits, start, duration, value, state, mode), each counter's and
//           gauge's flags, value and maximum.
//   friend: while the host's packets are fresh and name the same mission in our room ("the host owns it here"):
//           - each counter and gauge in use on both sides takes the host's value through the game's own setter
//             (exe+0x3FA230 counter, exe+0x3FA310 gauge), so the HUD moves and our mission script gets the same
//             "empty" / "full" signals (triggers 0xE / 0x7B, 0x88 / 0x89) the host's got;
//           - our own scripts' value calls (set, add, increment, decrement count; set, add gauge, ratio; a counter's
//             digit) are held: the host's values count. Logged, and so is a value our game changed by itself (a prize,
//             a beaten enemy), so a playthrough shows which of the friend's own contributions are lost;
//           - a gauge our game changed by itself goes to the host, which adds it (TODO 1.272, your pick 2026-11-23): it is
//             our own Sora's pickup, a prize the host's game never had (Hostile Program's clusters). The engine's only
//             writer of a mission gauge is the prize pickup exe+0x3DC650, for the game's own player alone (L2032), and
//             our scripts' calls are held. A counter isn't sent: a beaten enemy is counted in both games;
//           - the timer: paused and unpaused with the host's (pause bit 2), and set to the host's value when it is
//             more than 15 frames off (its start, from the clock, so the game's own countdown goes on from there);
//             never while ours is paused: it stays frozen, then jumps (TODO 1.196).
//   The bosses' setup calls (show the HUD, set the timer, a counter's start and end, information messages) are run
//   on the friend's copy of the boss by world_mirror.cpp; the mission's failure is the host's (scene_sync.cpp).
// MISSION_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include "mission_sync.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "game.hpp"
#include "puppet.hpp"
#include "world_census.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4E32484B;  // "KH2N"
constexpr std::uint16_t kVersion = 2;  // v2: the other player's counter (TODO 1.291)
constexpr std::uintptr_t kMission = 0x2A0FF68;  // the mission object, 0 = none
constexpr std::uintptr_t kMissionFlags = 4;     // bit 2: the setters are off (exe+0x3A39F0); 0x14: ended or failed
constexpr std::uint32_t kSettersOff = 4, kEnded = 0x14;
constexpr std::uintptr_t kMissionName = 0x40;   // its file's name, e.g. "B_EX150_LV99"
// 3 counters at +0x160 (0x50 apart), 3 gauges at +0x250 (0x48 apart); in each: +0x18 flags (bit 0 in use, bit 4
// turned off), +0x24 maximum, +0x38 value (counter: int, gauge: float).
constexpr std::uintptr_t kCounters = 0x160, kCounterSize = 0x50, kGauges = 0x250, kGaugeSize = 0x48;
constexpr std::uintptr_t kSlotFlags = 0x18, kSlotMax = 0x24, kSlotValue = 0x38;
constexpr std::uint32_t kSlotOn = 1, kSlotOff = 0x10;
// The mission timer: u32 clock (+1 a frame unless a pause bit is set), pause bits, start (the clock then), duration
// and value (frames), state (1 starting, 2 running, 0 stopped), u8 mode (1 counts down).
constexpr std::uintptr_t kTimer = 0xABB850;
constexpr std::uintptr_t kTimerClock = 0, kTimerPause = 4, kTimerStart = 8, kTimerDuration = 0xC, kTimerValue = 0x10,
                         kTimerState = 0x14, kTimerMode = 0x18;
constexpr std::uint32_t kMissionPause = 2;  // exe+0x157130(2), the mission's pause_timer
constexpr int kTimerSlack = 15;             // frames
constexpr std::uintptr_t kCounterSet = 0x3FA230;  // (counter, int): clamps, signals 0xE at 0 and 0x7B at the maximum
constexpr std::uintptr_t kGaugeSet = 0x3FA310;    // (gauge, float): clamps, signals 0x88 at 0 and 0x89 at the maximum
using PFN_CounterSet = void(__fastcall*)(std::uintptr_t, std::int32_t);
using PFN_GaugeSet = void(__fastcall*)(std::uintptr_t, float);
constexpr ULONGLONG kFreshMs = 1000;
constexpr std::uint32_t kTagInt = 0x544E4940, kTagFloat = 0x544C4640;  // "@INT" "@FLT"

#pragma pack(push, 1)
struct Slot {
    std::uint32_t flags;
    std::uint32_t value;  // int, or a gauge's float bits
    std::int32_t max;
};
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint8_t world, room;
    std::uint16_t programs[3];
    std::uint8_t present, mode;  // a mission is there; the timer's mode
    std::uint32_t flags, name;   // the mission's flags (+4) and its name's hash
    std::uint32_t pause;
    std::int32_t duration, value, state;
    Slot counters[3], gauges[3];
    std::int32_t extra;  // the friend's Sora's count for kPlayerCounters, -1 = none
};
// Friend -> host, reliable: our own Sora's pickup changed a gauge (TODO 1.272). The state packet's magic, told apart by
// its size and by who gets it.
struct OwnGauge {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t name;  // the mission's name hash
    std::uint8_t world, room, gauge;
    float delta;
};
#pragma pack(pop)
static_assert(sizeof(OwnGauge) != sizeof(Packet), "the two are told apart by size");
// The mission's +0x6A0 (L2033): the game's own gauge set exe+0x3FBCB0 does nothing while it is non-zero.
constexpr std::uintptr_t kMissionGaugeHold = 0x6A0;

bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;
std::uint32_t g_loggedName = 0;  // the mission whose name we logged last (both sides)
// Host: the values sent last frame, to log the changes that aren't a boss's steady drain (who changes what, for the
// playthrough logs).
Packet g_prevSent {};
bool g_prevValid = false;
int g_changeLines = 0;
std::uintptr_t g_loggedMission = 0;
// Host: the friend's pickups not added yet (they come with the link's packets and wait for our frame code), with the
// mission and room they were picked up in. Numbers only: nothing of the game's is kept.
float g_friendAdds[3] {};
std::uint32_t g_friendAddsName = 0, g_friendAddsPlace = 0, g_friendAdded = 0;
int g_addLogs = 0;

// Friend.
Packet g_hostPacket {};
bool g_haveHost = false;
ULONGLONG g_hostAt = 0;
std::uint32_t g_lastSeq = 0;
bool g_owning = false;
std::uintptr_t g_ownedMission = 0;
std::uint32_t g_ownedName = 0;
struct Last {
    std::uint32_t value;
    bool valid;
};
Last g_last[2][3];               // [gauge?][slot]: the value our setter left, to tell when our game changed it since
std::uint32_t g_mismatchLogged;  // bits: slots whose use differed from the host's (logged once per mission)
bool g_timerMismatchLogged = false;
int g_timerMismatchFrames = 0;  // frames in a row the timers differed (one starts a frame before the other)
std::uint32_t g_applied = 0, g_localChanges = 0, g_held = 0, g_timerMoves = 0, g_timerPauses = 0;
int g_heldLogs = 0, g_changeLogs = 0, g_moveLogs = 0, g_pausedLogs = 0;
std::uint32_t g_ownSent = 0;  // our Sora's gauge pickups sent to the host
int g_ownLogs = 0;
bool g_timerPaused = false;  // our timer was paused last frame (ApplyTimer leaves it alone then)
std::uint64_t g_statsTick = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

struct Place {
    std::uint8_t world, room;
    std::uint16_t programs[3];
};

Place Here() {
    std::uintptr_t now = ExeBase() + game::kNow;
    Place p {Read<std::uint8_t>(now), Read<std::uint8_t>(now + 1), {}};
    for (int i = 0; i < 3; ++i) p.programs[i] = Read<std::uint16_t>(now + 4 + 2 * i);
    return p;
}

std::uintptr_t Mission() { return Read<std::uintptr_t>(ExeBase() + kMission); }

void MissionName(std::uintptr_t m, char (&out)[33]) {
    out[0] = 0;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(m + kMissionName), 32);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
    }
    out[32] = 0;
    for (char& c : out) {
        if (!c) break;
        if (c < 0x20 || c > 0x7E) c = '?';
    }
}

std::uint32_t HashText(const char* text) {
    std::uint32_t h = 2166136261u;
    for (const char* c = text; *c; ++c) h = (h ^ static_cast<std::uint8_t>(*c)) * 16777619u;
    return h;
}

// The file's name without its folder: the folder is the game's language ('msn/us/TT08_MS701.bar' in an English game,
// 'msn/sp/TT08_MS701.bar' in a Spanish one; two-PC run 2026-10-15: the friend's game never took the host's mission).
std::uint32_t NameHash(std::uintptr_t m) {
    char name[33];
    MissionName(m, name);
    const char* file = name;
    for (const char* c = name; *c; ++c)
        if (*c == '/') file = c + 1;
    return HashText(file);
}

std::uintptr_t CounterSlot(std::uintptr_t m, int i) { return m + kCounters + i * kCounterSize; }
std::uintptr_t GaugeSlot(std::uintptr_t m, int i) { return m + kGauges + i * kGaugeSize; }

Slot ReadSlot(std::uintptr_t slot) {
    return {Read<std::uint32_t>(slot + kSlotFlags), Read<std::uint32_t>(slot + kSlotValue),
            Read<std::int32_t>(slot + kSlotMax)};
}

bool InUse(std::uint32_t flags) { return (flags & (kSlotOn | kSlotOff)) == kSlotOn; }

// ---- the other player's counter (TODO 1.291) ----
// A mission counter that shows the game's own player's count (its script reads trap_sysobj_player's) shows one Sora's.
// For the missions below both games show one more counter, the friend's Sora's, under the host's (your pick
// 2026-11-25; DECISIONS 'Exceptions to the general rules'). The game has three counter slots and these missions use
// them all (L2104), so ours is a copy of the source counter's YS::MISSION_COUNT in our memory, added to the mission's
// list of 2D parts: the game's own walks show and hide it with the rest (L2109), its widget reads its value through a
// pointer into our copy (L2110), and its row is the list's next one (the append's own rule), so it sits one counter
// lower in the source's group and moves with it when the INFO bar pushes the rows down (F924). Its value: the copy's
// actor field in the host's game, the host's packet in the friend's.
struct PlayerCounter {
    const char* file;      // the mission file, without its language folder
    int source;            // the counter that shows our own Sora's count
    std::uintptr_t field;  // the Sora actor's field it counts
};
constexpr PlayerCounter kPlayerCounters[] = {
    // the Grim Reaper (Harbor 10/01): medals (F918, F922); TODO 1.291
    {"CA01_MS204.bar", 0, 0xD1C},
};
constexpr std::uintptr_t kCountVtable = 0x5C4248;            // YS::MISSION_COUNT (L2104)
constexpr std::uintptr_t kParts = 0x3D0, kPartCount = 0x418;  // the mission's 2D parts: pointers, s32 count (L2109)
constexpr int kMaxParts = 9;
constexpr std::uintptr_t kCountEvent = 0x1C, kCountWarnAt = 0x28, kCountWarnText = 0x34, kCountRow = 0x30,
                         kCountImage = 0x8, kCountWidget = 0x40;
constexpr std::uint32_t kPartShown = 4, kPartWarned = 8, kPartDisabled = 0x10;
constexpr int kPartShow = 2, kPartHide = 3;     // YS::MISSION_COUNT's vtable slots (L2105)
alignas(16) std::uint8_t g_extra[kCounterSize];  // static: its widget reads +0x38 and +0x24 every frame (L2110)
const PlayerCounter* g_extraRow = nullptr;      // set while g_extra is in a mission's list
int g_extraLogs = 0;

std::uintptr_t Extra() { return reinterpret_cast<std::uintptr_t>(g_extra); }
template <typename T>
T& ExtraField(std::uintptr_t offset) {
    return *reinterpret_cast<T*>(Extra() + offset);
}

const PlayerCounter* PlayerCounterRow(std::uintptr_t m) {
    const std::uint32_t name = NameHash(m);
    for (const PlayerCounter& row : kPlayerCounters)
        if (HashText(row.file) == name) return &row;
    return nullptr;
}

bool Listed(std::uintptr_t m, std::uintptr_t part) {
    const std::int32_t n = Read<std::int32_t>(m + kPartCount);
    for (std::int32_t i = 0; i < n && i < kMaxParts; ++i)
        if (Read<std::uintptr_t>(m + kParts + 8 * i) == part) return true;
    return false;
}

// The HUD layer a widget was made in: exe+0x166950 keeps its task at +0x18 and the layer at the task's +0x18 (L2110).
std::uintptr_t WidgetLayer(std::uintptr_t widget) {
    std::uintptr_t task = 0, layer = 0;
    return widget && ReadAny(widget + 0x18, &task) && task && ReadAny(task + 0x18, &layer) ? layer : 0;
}

void CallPart(int slot) {  // through our copy's vtable: the game's YS::MISSION_COUNT (checked when copied)
    using PFN_Part = void(__fastcall*)(std::uintptr_t);
    const std::uintptr_t vtable = ExtraField<std::uintptr_t>(0);
    reinterpret_cast<PFN_Part>(Read<std::uintptr_t>(vtable + 8 * slot))(Extra());
}

// Host: what the friend's counter shows, or -1 (not one of ours, no copy).
std::int32_t ExtraValueHost(std::uintptr_t m) {
    const PlayerCounter* row = m ? PlayerCounterRow(m) : nullptr;
    const std::uintptr_t copy = row ? PuppetCloneActor() : 0;
    std::int32_t value = -1;
    return copy && ReadAny(copy + row->field, &value) ? value : -1;
}

// Both games, each frame in the field. value < 0: no news (the shown one stays).
void ExtraCounterFrame(std::uintptr_t m, std::int32_t value) {
    if (!m) return;
    if (!Listed(m, Extra())) {
        // Not in this mission's list: none of ours yet, or a new mission (its constructor emptied the list). A widget
        // our copy had belongs to the old list now, as the game's own counters' do (the room's HUD teardown frees
        // them, L2110): forgotten, never hidden.
        if (g_extraRow) {
            ExtraField<std::uintptr_t>(kCountWidget) = 0;
            g_extraRow = nullptr;
        }
        const PlayerCounter* row = PlayerCounterRow(m);
        if (!row || value < 0) return;  // not one of ours, or no other player
        const std::uintptr_t source = CounterSlot(m, row->source);
        const std::int32_t n = Read<std::int32_t>(m + kPartCount);
        // The mission's loader set the source up and listed it (its look loaded, the game's class); the list has room.
        if (!InUse(Read<std::uint32_t>(source + kSlotFlags)) || !Read<std::uintptr_t>(source + kCountImage) ||
            Read<std::uintptr_t>(source) != ExeBase() + kCountVtable || !Listed(m, source) || n < 0 || n >= kMaxParts)
            return;
        std::memcpy(g_extra, reinterpret_cast<const void*>(source), kCounterSize);  // its look, group, digits, maximum
        ExtraField<std::uint32_t>(kSlotFlags) &= ~(kPartShown | kPartWarned);
        ExtraField<std::uintptr_t>(kCountWidget) = 0;
        ExtraField<std::int32_t>(kCountEvent) = -1;  // none of the source's mission events at 0 or the maximum (L2111)
        ExtraField<std::int32_t>(kCountWarnAt) = 0;  // no warning blink or message (L2111)
        ExtraField<std::int32_t>(kCountWarnText) = 0;
        ExtraField<std::int32_t>(kCountRow) = n + 3;  // the append's own rule (L2109): the next row in its group
        ExtraField<std::int32_t>(kSlotValue) = value;
        *reinterpret_cast<std::uintptr_t*>(m + kParts + 8 * n) = Extra();
        *reinterpret_cast<std::int32_t*>(m + kPartCount) = n + 1;
        g_extraRow = row;
        if (g_extraLogs++ < 20)
            Log("mission sync: the other player's counter added to mission '%s' (counter %d's look, row %d, value %d)",
                row->file, row->source, n + 3, value);
    }
    const std::uintptr_t source = CounterSlot(m, g_extraRow->source);
    const std::uint32_t sourceFlags = Read<std::uint32_t>(source + kSlotFlags);
    std::uint32_t& flags = ExtraField<std::uint32_t>(kSlotFlags);
    flags = (flags & ~kPartDisabled) | (sourceFlags & kPartDisabled);  // the source's 4/44 disable is ours too (L2109)
    const std::uintptr_t sourceWidget = Read<std::uintptr_t>(source + kCountWidget);
    const bool sourceShown = (sourceFlags & kPartShown) && sourceWidget;
    const bool shown = (flags & kPartShown) || ExtraField<std::uintptr_t>(kCountWidget);
    if (shown && !sourceShown) {
        CallPart(kPartHide);  // the source hidden alone (4/44): ours too (a list walk hides both at once)
        if (g_extraLogs++ < 20) Log("mission sync: the other player's counter hidden with counter %d", g_extraRow->source);
    } else if (!shown && sourceShown) {
        // Shown with the source, in the HUD layer its widget is in (ours is made in the current one, L2110).
        const std::uintptr_t layer = Read<std::uintptr_t>(ExeBase() + game::kHudSystem);
        if (layer && layer == WidgetLayer(sourceWidget)) {
            CallPart(kPartShow);
            if (g_extraLogs++ < 20) Log("mission sync: the other player's counter shown with counter %d", g_extraRow->source);
        }
    }
    if (value >= 0) reinterpret_cast<PFN_CounterSet>(ExeBase() + kCounterSet)(Extra(), value);
}

const char* ValueText(bool gauge, std::uint32_t v, char* buf, int size) {
    if (gauge) {
        float f;
        std::memcpy(&f, &v, sizeof(f));
        std::snprintf(buf, size, "%.3f", f);
    } else {
        std::snprintf(buf, size, "%d", static_cast<std::int32_t>(v));
    }
    return buf;
}

void LogMissionChange(std::uintptr_t m) {
    std::uint32_t name = m ? NameHash(m) : 0;
    if (m == g_loggedMission && name == g_loggedName) return;
    g_loggedMission = m;
    g_loggedName = name;
    if (!m) return;
    char text[33];
    MissionName(m, text);
    Place here = Here();
    char uses[64];
    int n = 0;
    for (int i = 0; i < 3; ++i)
        n += std::snprintf(uses + n, sizeof(uses) - n, "%s%c", i ? "" : "counters ",
                           InUse(Read<std::uint32_t>(CounterSlot(m, i) + kSlotFlags)) ? 'x' : '-');
    for (int i = 0; i < 3; ++i)
        n += std::snprintf(uses + n, sizeof(uses) - n, "%s%c", i ? "" : ", gauges ",
                           InUse(Read<std::uint32_t>(GaugeSlot(m, i) + kSlotFlags)) ? 'x' : '-');
    Log("mission sync: mission '%s' in 0x%02X/0x%02X programs %u/%u/%u (%s)%s", text, here.world, here.room,
        here.programs[0], here.programs[1], here.programs[2], uses,
        g_host ? "; sending its timer, counters and gauges to the friend's game" : "");
}

// ---- host ----
// Counter changes, and gauge changes of 0.05 or more in one frame (Luxord drains both gauges 0.0037 a frame).
void LogHostChanges(const Packet& p) {
    bool same = g_prevValid && p.present && g_prevSent.present && p.name == g_prevSent.name;
    if (!same) g_changeLines = 0;
    for (int i = 0; same && i < 3 && g_changeLines < 300; ++i) {
        const Slot &c = p.counters[i], &pc = g_prevSent.counters[i];
        if (InUse(c.flags) && InUse(pc.flags) && c.value != pc.value) {
            ++g_changeLines;
            Log("mission sync: counter %d %d -> %d", i, static_cast<std::int32_t>(pc.value),
                static_cast<std::int32_t>(c.value));
        }
        const Slot &g = p.gauges[i], &pg = g_prevSent.gauges[i];
        float a, b;
        std::memcpy(&a, &pg.value, sizeof(a));
        std::memcpy(&b, &g.value, sizeof(b));
        if (InUse(g.flags) && InUse(pg.flags) && (b - a >= 0.05f || a - b >= 0.05f)) {
            ++g_changeLines;
            Log("mission sync: gauge %d %.3f -> %.3f (%+.3f)", i, a, b, b - a);
        }
    }
    g_prevSent = p;
    g_prevValid = true;
}

void SendState() {
    std::uintptr_t exe = ExeBase();
    std::uintptr_t m = Mission();
    if (!m && g_frame % 30 != 0) return;  // no mission: a word now and then, so the friend's game stops taking values
    Packet p {};
    p.magic = kMagic;
    p.version = kVersion;
    p.build = AvatarLinkBuild();
    p.seq = ++g_seq;
    Place here = Here();
    p.world = here.world;
    p.room = here.room;
    std::memcpy(p.programs, here.programs, sizeof(p.programs));
    p.extra = -1;
    __try {
        if (m) {
            p.present = 1;
            p.extra = ExtraValueHost(m);
            p.flags = Read<std::uint32_t>(m + kMissionFlags);
            p.name = NameHash(m);
            for (int i = 0; i < 3; ++i) {
                p.counters[i] = ReadSlot(CounterSlot(m, i));
                p.gauges[i] = ReadSlot(GaugeSlot(m, i));
            }
        }
        std::uintptr_t t = exe + kTimer;
        p.pause = Read<std::uint32_t>(t + kTimerPause);
        p.duration = Read<std::int32_t>(t + kTimerDuration);
        p.value = Read<std::int32_t>(t + kTimerValue);
        p.state = Read<std::int32_t>(t + kTimerState);
        p.mode = Read<std::uint8_t>(t + kTimerMode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    AvatarLinkSendRaw(&p, sizeof(p));
    LogHostChanges(p);
}

// ---- friend ----
bool HostOwnsHere() {
    if (!g_on || g_host || !g_haveHost || GetTickCount64() - g_hostAt > kFreshMs) return false;
    const Packet& h = g_hostPacket;
    if (!h.present || (h.flags & kEnded)) return false;
    std::uintptr_t exe = ExeBase();
    if (!Read<std::uint8_t>(exe + game::kInField)) return false;
    Place here = Here();
    if (here.world != h.world || here.room != h.room || std::memcmp(here.programs, h.programs, sizeof(h.programs)) != 0)
        return false;
    std::uintptr_t m = Mission();
    return m && NameHash(m) == h.name;
}

// Friend: see the header. The change as our game made it (its own add already scaled it where the game does).
void SendOwnGauge(int i, std::uint32_t before, std::uint32_t after) {
    float a, b;
    std::memcpy(&a, &before, sizeof(a));
    std::memcpy(&b, &after, sizeof(b));
    const float delta = b - a;
    if (!(delta == delta) || delta == 0.0f) return;  // not a number, or nothing
    Place here = Here();
    OwnGauge p {kMagic, kVersion, AvatarLinkBuild(), g_ownedName, here.world, here.room, static_cast<std::uint8_t>(i), delta};
    AvatarLinkSendReliable(&p, sizeof(p));
    ++g_ownSent;
    if (g_ownLogs++ < 40)
        Log("mission sync: our Sora's pickup changed gauge %d by %+.3f (%.3f -> %.3f): sent to the host's game to add", i,
            delta, a, b);
}

// Host: the friend's pickups, through the game's own setter under the conditions of its own set (exe+0x3FBCB0: setters
// on, +0x6A0 zero, the gauge in use): it clamps and signals a full gauge as our own pickup does. The game's add
// (exe+0x3FB3A0) isn't called again: it scales by mission name and frame time, and the friend's game already did.
void AddFriendGauges() {
    if (!g_friendAddsName) return;
    std::uintptr_t m = Read<std::uint8_t>(ExeBase() + game::kInField) ? Mission() : 0;
    Place here = Here();
    const bool same = m && NameHash(m) == g_friendAddsName &&
                      ((static_cast<std::uint32_t>(here.world) << 8) | here.room) == g_friendAddsPlace;
    const bool open = same && !(Read<std::uint32_t>(m + kMissionFlags) & kSettersOff) &&
                      Read<std::int32_t>(m + kMissionGaugeHold) == 0;
    for (int i = 0; i < 3; ++i) {
        const float d = g_friendAdds[i];
        if (d == 0.0f) continue;
        g_friendAdds[i] = 0.0f;
        std::uintptr_t slot = open ? GaugeSlot(m, i) : 0;
        if (!slot || !InUse(Read<std::uint32_t>(slot + kSlotFlags))) {
            if (g_addLogs++ < 40)
                Log("mission sync: the friend's pickup (%+.3f to gauge %d) dropped: %s", d, i,
                    !same ? "that mission isn't running here (ended, or another room)"
                    : !open ? "our mission takes no values now" : "the gauge isn't in use here");
            continue;
        }
        float before, after;
        std::uint32_t raw = Read<std::uint32_t>(slot + kSlotValue);
        std::memcpy(&before, &raw, sizeof(before));
        reinterpret_cast<PFN_GaugeSet>(ExeBase() + kGaugeSet)(slot, before + d);
        raw = Read<std::uint32_t>(slot + kSlotValue);
        std::memcpy(&after, &raw, sizeof(after));
        ++g_friendAdded;
        if (g_addLogs++ < 40)
            Log("mission sync: the friend's pickup adds %+.3f to gauge %d: %.3f -> %.3f (%u so far)", d, i, before, after,
                g_friendAdded);
    }
    g_friendAddsName = 0;
}

void ApplySlot(std::uintptr_t slot, const Slot& h, bool gauge, int i) {
    Slot ours = ReadSlot(slot);
    bool hostOn = InUse(h.flags), ourOn = InUse(ours.flags);
    std::uint32_t bit = 1u << (gauge * 3 + i);
    if (hostOn != ourOn) {
        if (!(g_mismatchLogged & bit)) {
            g_mismatchLogged |= bit;
            Log("mission sync: %s %d is %s in the host's game but %s in ours (flags 0x%X / 0x%X): left as it is",
                gauge ? "gauge" : "counter", i, hostOn ? "in use" : "not in use", ourOn ? "in use" : "not",
                h.flags, ours.flags);
        }
        g_last[gauge][i].valid = false;
        return;
    }
    if (!hostOn) return;
    Last& last = g_last[gauge][i];
    const bool own = last.valid && ours.value != last.value;  // our game changed it since our last set
    if (own && gauge) SendOwnGauge(i, last.value, ours.value);  // before the test below: the host's may equal it by chance
    if (ours.value == h.value) {
        last = {ours.value, true};
        return;
    }
    char a[24], b[24], c[24];
    if (own) {
        ++g_localChanges;
        if (!gauge && g_changeLogs++ < 40)  // a gauge's has SendOwnGauge's line
            Log("mission sync: our game changed %s %d from %s to %s by itself; the host's %s replaces it",
                gauge ? "gauge" : "counter", i, ValueText(gauge, last.value, a, sizeof(a)),
                ValueText(gauge, ours.value, b, sizeof(b)), ValueText(gauge, h.value, c, sizeof(c)));
    }
    if (ours.max != h.max && !(g_mismatchLogged & (bit << 8))) {
        g_mismatchLogged |= bit << 8;
        Log("mission sync: %s %d's maximum is %d in the host's game but %d in ours", gauge ? "gauge" : "counter", i,
            h.max, ours.max);
    }
    std::uintptr_t exe = ExeBase();
    if (gauge) {
        float f;
        std::memcpy(&f, &h.value, sizeof(f));
        reinterpret_cast<PFN_GaugeSet>(exe + kGaugeSet)(slot, f);
    } else {
        reinterpret_cast<PFN_CounterSet>(exe + kCounterSet)(slot, static_cast<std::int32_t>(h.value));
    }
    last = {Read<std::uint32_t>(slot + kSlotValue), true};
    ++g_applied;
}

void ApplyTimer() {
    const Packet& h = g_hostPacket;
    std::uintptr_t t = ExeBase() + kTimer;
    auto* pause = reinterpret_cast<std::uint32_t*>(t + kTimerPause);
    if ((h.pause & kMissionPause) != (*pause & kMissionPause)) {
        *pause = (*pause & ~kMissionPause) | (h.pause & kMissionPause);
        ++g_timerPauses;
    }
    std::int32_t state = Read<std::int32_t>(t + kTimerState), duration = Read<std::int32_t>(t + kTimerDuration);
    std::uint8_t mode = Read<std::uint8_t>(t + kTimerMode);
    if (h.state != 2 || state != 2 || mode != h.mode || duration != h.duration) {
        if (h.state == 2 || state == 2) ++g_timerMismatchFrames;
        else g_timerMismatchFrames = 0;
        if (g_timerMismatchFrames >= 60 && !g_timerMismatchLogged) {
            g_timerMismatchLogged = true;
            Log("mission sync: the timers differ: the host's state %d mode %u %d frames (now %d), ours state %d mode %u "
                "%d frames (now %d); not moved", h.state, h.mode, h.duration, h.value, state, mode, duration,
                Read<std::int32_t>(t + kTimerValue));
        }
        return;
    }
    g_timerMismatchFrames = 0;
    // A pause bit (our RC scene, a menu, a summon, the host's mission pause) stops our clock and the value's update
    // (L334, L335): the value is stale, so leave it frozen and set it once it runs again (your pick 'Freeze, then
    // jump', TODO 1.196: moving it meanwhile piled up and ran our timer out, F574).
    if (*pause) {
        if (!g_timerPaused && g_pausedLogs++ < 20)
            Log("mission sync: our timer is paused by %s (bits 0x%X): not moved until it runs again",
                (*pause & ~kMissionPause) ? "our own game" : "the host's mission pause", *pause);
        g_timerPaused = true;
        return;
    }
    g_timerPaused = false;
    std::int32_t diff = h.value - Read<std::int32_t>(t + kTimerValue);
    if (diff >= -kTimerSlack && diff <= kTimerSlack) return;
    // The tick writes value = start + duration - clock counting down, clock - start counting up (L334): a start set
    // from the clock gives the host's value at the next tick, and the same start however often it runs before then.
    auto clock = static_cast<std::int32_t>(Read<std::uint32_t>(t + kTimerClock));
    *reinterpret_cast<std::int32_t*>(t + kTimerStart) = mode == 1 ? clock + h.value - duration : clock - h.value;
    ++g_timerMoves;
    if (g_moveLogs++ < 20)
        Log("mission sync: our timer was %d frames off the host's (%d): moved", diff, h.value);
}

void FriendFrame() {
    if (!HostOwnsHere()) {
        if (g_owning) Log("mission sync: the host's game no longer owns the mission here (ended, left, or no word)");
        g_owning = false;
        return;
    }
    std::uintptr_t m = Mission();
    if (!g_owning || m != g_ownedMission || g_hostPacket.name != g_ownedName) {
        g_owning = true;
        g_ownedMission = m;
        g_ownedName = g_hostPacket.name;
        std::memset(g_last, 0, sizeof(g_last));
        g_mismatchLogged = 0;
        g_timerMismatchLogged = false;
        g_timerMismatchFrames = 0;
        g_heldLogs = g_changeLogs = g_moveLogs = g_pausedLogs = g_ownLogs = 0;
        g_timerPaused = false;
        char text[33];
        MissionName(m, text);
        Log("mission sync: the host's game owns mission '%s' here: its timer, counters and gauges count in ours", text);
    }
    if (Read<std::uint32_t>(m + kMissionFlags) & kSettersOff) return;  // ours ended: nothing to set
    for (int i = 0; i < 3; ++i) ApplySlot(CounterSlot(m, i), g_hostPacket.counters[i], false, i);
    for (int i = 0; i < 3; ++i) ApplySlot(GaugeSlot(m, i), g_hostPacket.gauges[i], true, i);
    ApplyTimer();
}

const char* HeldName(int index) {
    switch (index) {
        case 5: return "set_count";
        case 6: return "increment_count";
        case 8: return "set_gauge";
        case 9: return "add_gauge";
        case 10: return "set_gauge_ratio";
        case 20: return "decrement_count";
        case 41: return "add_count";
        case 48: return "set_count_figure_num";
    }
    return nullptr;
}

int ArgText(std::uint64_t v, char* out, int size) {
    std::uint32_t tag = static_cast<std::uint32_t>(v >> 32), lo = static_cast<std::uint32_t>(v);
    int n;
    if (tag == kTagFloat) {
        float f;
        std::memcpy(&f, &lo, sizeof(f));
        n = std::snprintf(out, size, "%.4g", f);
    } else if (tag == kTagInt) {
        n = std::snprintf(out, size, "%d", static_cast<std::int32_t>(lo));
    } else {
        n = std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(v));
    }
    return n < 0 ? 0 : n >= size ? size - 1 : n;
}

const char* ModelOf(std::uintptr_t a) {
    if (!a) return "-";
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + 0x918);
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

}  // namespace

void MissionSyncInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_MISSION_SYNC", 1) == 1;
    Log("mission sync: %s", !g_on ? "off (MISSION_SYNC=0)"
                            : host ? "on (host): our missions' timer, counters and gauges are sent to the friend's game"
                                   : "on (friend): the host's mission timer, counters and gauges count in our game");
}

void MissionSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        LogMissionChange(Read<std::uint8_t>(ExeBase() + game::kInField) ? Mission() : 0);
        if (g_host) {
            AddFriendGauges();
            SendState();
        } else {
            FriendFrame();
        }
        if (Read<std::uint8_t>(ExeBase() + game::kInField)) {
            const std::uintptr_t m = Mission();
            ExtraCounterFrame(m, g_host ? (m ? ExtraValueHost(m) : -1) : HostOwnsHere() ? g_hostPacket.extra : -1);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 5) Log("mission sync: exception in the frame update");
    }
    if (g_host) return;
    std::uint64_t now = GetTickCount64();
    if (now - g_statsTick < 10000 || !(g_applied || g_held || g_timerMoves)) return;
    g_statsTick = now;
    Log("mission sync: host values set %u, our own changes replaced %u (gauge pickups sent to the host %u), our value "
        "calls held %u, timer moved %u, timer pauses copied %u", g_applied, g_localChanges, g_ownSent, g_held,
        g_timerMoves, g_timerPauses);
}

void MissionSyncOnPacket(const char* buf, int n) {
    if (g_on && g_host && n == static_cast<int>(sizeof(OwnGauge))) {  // the friend's pickup: added in our frame code
        OwnGauge o;
        std::memcpy(&o, buf, sizeof(o));
        if (o.version != kVersion || o.build != AvatarLinkBuild() || o.gauge >= 3 || !(o.delta == o.delta)) return;
        const std::uint32_t place = (static_cast<std::uint32_t>(o.world) << 8) | o.room;
        if (o.name != g_friendAddsName || place != g_friendAddsPlace) std::memset(g_friendAdds, 0, sizeof(g_friendAdds));
        g_friendAddsName = o.name;
        g_friendAddsPlace = place;
        g_friendAdds[o.gauge] += o.delta;
        return;
    }
    if (!g_on || g_host || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (g_haveHost && p.seq <= g_lastSeq && g_lastSeq - p.seq < 600) return;  // late (an older frame)
    g_lastSeq = p.seq;
    g_hostPacket = p;
    g_haveHost = true;
    g_hostAt = GetTickCount64();
}

bool MissionSyncHostOwnsHere() { return HostOwnsHere(); }

bool MissionSyncHolds(int table, int index, const std::uint64_t* args) {
    if (!g_on || g_host || table != 4) return false;
    const char* name = HeldName(index);
    if (!name || !HostOwnsHere()) return false;
    ++g_held;
    if (g_heldLogs < 30) {
        ++g_heldLogs;
        char text[64];
        int n = ArgText(args[0], text, sizeof(text));
        if (index != 6 && index != 20) {  // increment / decrement: one argument (the counter)
            n += std::snprintf(text + n, sizeof(text) - n, ", ");
            ArgText(args[1], text + n, static_cast<int>(sizeof(text)) - n);
        }
        Log("mission sync: our script's mission_%s(%s) by %s held: the host's values count here", name, text,
            ModelOf(WorldCensusScriptActor()));
    }
    return true;
}

bool MissionSyncCovers(int table, int index) {
    if (table != 4) return false;
    switch (index) {
        case 3: case 11: case 16: case 22:  // the endings (scene_sync.cpp)
        case 12: case 21: case 26: case 29:  // getters
            return true;
    }
    return HeldName(index) != nullptr;
}

}  // namespace kh2coop
