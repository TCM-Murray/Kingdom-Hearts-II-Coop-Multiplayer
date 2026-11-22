// The other player's Sora in the item and Cure target list (TODO 3.17; your picks 2026-11-11 and 2026-11-13).
//
// The game's list (command menu page 4) holds the player and party members 1 and 2 (L271), so with two companions
// the copy has no row. After each build we add one: command 797 "Sora (P1)" in the friend's game, 798 "Sora (P2)"
// in the host's (openkh_mod/cmd_other_sora.yml, which only a Mods Manager Build puts into 03system.bin; the texts are
// ours, through link_menu.cpp's message hook). With an empty party slot the game lists the copy itself as "Sora":
// that row becomes ours.
// - The game looks a row's command up with no null check (L319), so a missing entry (no Build) would crash the menu:
//   we look for it in the live list ourselves (F550) and add no row without it.
// - The confirm (L320) takes the actor from menu+0xB80[cursor], which has 3 slots: at cursor 3 it would read page 8's
//   data (+0xB98). Our confirm hook puts the copy in the row's slot for the call and puts the old value back.
// - The build refills the page only in some frames (L322): a page that already has our row wasn't refilled.
// Liveness: nothing is kept across frames. The copy is asked for per build and per confirm (PuppetCloneActor); the
// slot is written and restored inside one call; the list check keeps the list's address and a yes/no, and looks
// again when the address changes.

#include "target_row.hpp"

#include <windows.h>

#include <cstdint>

#include "avatar_link.hpp"
#include "common.hpp"
#include "limit_sync.hpp"
#include "link_menu.hpp"
#include "mickey_sync.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kCmdTable = 0x2A161E8;  // -> the command list: +4 count, 0x30-byte rows from +8 (F550)
constexpr std::uintptr_t kAddRow = 0x3B0F20;     // (page, command, target info, flag) -> its row; no bound (L321)
constexpr std::uintptr_t kConfirm = 0x3F22E0;    // (menu) -> non-zero once the command is queued (L320)
constexpr std::uint8_t kConfirmBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                          0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x40};
constexpr std::uintptr_t kShownPage = 0x9A8;   // menu+: -> the page stack's top slot -> the page on screen (L320)
constexpr std::uintptr_t kTargetPage = 0x228;  // menu+: page 4, the item and Cure target list (L271)
constexpr std::uintptr_t kPageActors = 0xB80;  // menu+: page 4's actors, 3 slots (L271)
constexpr std::uintptr_t kPageCount = 0x10, kPageRows = 0x14, kRowSize = 0xC, kRowCommand = 2, kRowState = 5;
constexpr std::uintptr_t kPageCursor = 0x74;                        // (L321)
constexpr std::uintptr_t kActorFlags = 0x6C8, kActorStats = 0x5C0;  // the build's own checks on a member (L271)
constexpr std::uint16_t kRowP1 = 797, kRowP2 = 798;                 // openkh_mod/cmd_other_sora.yml
constexpr int kTextP1 = 0x7F43, kTextP2 = 0x7F44;  // their MessageIds without bit 15 (link_menu.cpp has 0x7F40-0x7F42)

using PFN_AddRow = std::uintptr_t(__fastcall*)(std::uintptr_t, std::uint32_t, const void*, int);
using PFN_Confirm = std::uint64_t(__fastcall*)(std::uintptr_t);
PFN_Confirm g_realConfirm = nullptr;
bool g_on = false, g_textAsked = false;
std::uint8_t g_textP1[16], g_textP2[16];
std::uintptr_t g_listSeen = 0;  // the command list we last looked in, and whether it has our row's command
bool g_inList = false;
int g_lastAdded = -2;  // the row index last logged (-1 = none)

std::uint16_t OurRow() { return AvatarLinkIsHost() ? kRowP2 : kRowP1; }  // the host's copy is the friend's Sora

bool InList(std::uint16_t id) {
    std::uintptr_t list = 0;
    if (!ReadAny(ExeBase() + kCmdTable, &list) || !list) return false;
    if (list == g_listSeen) return g_inList;
    g_listSeen = list;
    g_inList = false;
    int count = 0;
    ReadAny(list + 4, &count);
    for (int i = 0; i < count && i < 4096 && !g_inList; ++i) {
        std::uint16_t row = 0;
        g_inList = ReadAny(list + 8 + static_cast<std::uintptr_t>(i) * 0x30, &row) && row == id;
    }
    Log("target row: command %u %s the game's command list (%d commands)%s", id, g_inList ? "is in" : "is NOT in",
        count, g_inList ? "" : ": no row for the other Sora (KH2 Coop's 03system.bin patch not built? Mods Manager: Build)");
    return g_inList;
}

std::uint16_t RowCommand(std::uintptr_t page, int i) {
    std::uint16_t id = 0;
    ReadAny(page + kPageRows + static_cast<std::uintptr_t>(i) * kRowSize + kRowCommand, &id);
    return id;
}

std::uint64_t __fastcall HookConfirm(std::uintptr_t menu) {
    std::uintptr_t top = 0, page = 0;
    int cursor = -1;
    if (!g_on || !ReadAny(menu + kShownPage, &top) || !top || !ReadAny(top, &page) || page != menu + kTargetPage ||
        !ReadAny(page + kPageCursor, &cursor) || cursor < 0 || cursor > 3 || RowCommand(page, cursor) != OurRow())
        return g_realConfirm(menu);
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy || LimitSyncAnyLimit()) {
        Log("target row: the other Sora's row picked, but %s: refused", copy ? "a Limit runs" : "its copy is gone");
        return 0;
    }
    auto slot = reinterpret_cast<std::uintptr_t*>(menu + kPageActors) + cursor;  // cursor 3: page 8's data (+0xB98)
    std::uintptr_t kept = *slot;
    *slot = copy;
    std::uint64_t result = g_realConfirm(menu);
    *slot = kept;
    Log("target row: the other Sora's row (row %d) picked -> %s", cursor,
        static_cast<std::uint8_t>(result) ? "queued" : "not queued");
    return result;
}

}  // namespace

void TargetRowInit() {
    LinkMenuEncode("Sora (P1)", g_textP1, sizeof(g_textP1));
    LinkMenuEncode("Sora (P2)", g_textP2, sizeof(g_textP2));
    // Never a row without the confirm hook: the game's own confirm would read page 8's data as the actor.
    g_on = MickeyMenuBuildHook() &&
           HookFunction(kConfirm, kConfirmBytes, sizeof(kConfirmBytes), reinterpret_cast<void*>(HookConfirm),
                        reinterpret_cast<void**>(&g_realConfirm), "command menu confirm (the other Sora's target row)");
    Log("target row: %s", g_on ? "on: the other player's Sora gets its own row in the item and Cure target list"
                                : "OFF: a menu hook doesn't match this build");
}

void TargetRowAfterBuild(std::uintptr_t menu) {
    if (!g_on || !LinkMenuOn()) return;
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy || LimitSyncAnyLimit()) return;  // in a Limit the copy is no party member (puppet.cpp HookGetFriend)
    std::uint16_t id = OurRow();
    if (!InList(id)) return;
    std::uintptr_t page = menu + kTargetPage;
    auto& count = *reinterpret_cast<int*>(page + kPageCount);
    if (count < 0 || count > 3) return;
    for (int i = 0; i < count; ++i)
        if (RowCommand(page, i) == id) return;  // the page wasn't refilled this frame: our row is still there
    std::uint32_t flags = 0;
    std::uintptr_t stats = 0;
    if (!ReadAny(copy + kActorFlags, &flags) || (flags & (1u << 22)) || !ReadAny(copy + kActorStats, &stats) || !stats)
        return;
    const auto* actors = reinterpret_cast<const std::uintptr_t*>(menu + kPageActors);
    bool renamed = count > 0 && actors[count - 1] == copy;  // listed by the game (an empty party slot), as "Sora"
    if (renamed) --count;
    std::uintptr_t row = reinterpret_cast<PFN_AddRow>(ExeBase() + kAddRow)(page, id, nullptr, 1);
    int at = count - 1;
    if (at != g_lastAdded) {
        g_lastAdded = at;
        std::uint8_t state = 0xFF;
        if (row) ReadAny(row + kRowState, &state);
        Log("target row: the other Sora's row %u is row %d of %d (%s; state %u%s)", id, at, count,
            renamed ? "in place of the game's 'Sora' row" : "added", state, state >= 4 ? ", greyed: not pickable" : "");
    }
}

const std::uint8_t* TargetRowText(int id) {
    if (!g_on || (id != kTextP1 && id != kTextP2)) return nullptr;
    if (!g_textAsked) {
        g_textAsked = true;
        Log("target row: the game asked for our row's text (%#x)", id);
    }
    return id == kTextP1 ? g_textP1 : g_textP2;
}

const std::uint8_t* TargetRowOtherSoraName() {
    if (!g_textP1[0]) return nullptr;
    return OurRow() == kRowP2 ? g_textP2 : g_textP1;
}

}  // namespace kh2coop
