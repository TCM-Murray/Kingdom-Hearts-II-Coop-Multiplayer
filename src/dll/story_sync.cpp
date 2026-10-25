// The host's story progress counts in the friend's game (TODO 5.1; your picks 2026-10-25, DECISIONS 'Story,
// cutscenes, rooms': the core areas and the ones the progress script also writes, a byte copy at room loads, an
// exact copy of the host, a chest the host opens is opened and its item given in the friend's game; Q2 / TODO 4.6,
// asked again 2026-10-25: the friend's chests the same way in the host's game).
//
// The save in memory (exe+0x9A98B0, "KH2J") holds the story in these areas (VERIFIED_OFFSETS section 3 and L133):
//   +0x10..0x1C90   each room's programs (map, btl, evt), which the room load picks from
//   +0x1C90..0x1EF0 story flags, 0x20 bytes per world
//   +0x1EF0..0x1FD0 bits and u16 pairs the flags' scripts write (00progress.bin ops 2-5)
//   +0x22F8..0x2390 rooms visited, 8 bytes per world
//   +0x2390..0x23AC bits of ops 8/9
//   +0x23AC..0x23E0 chests opened, one bit each
//   +0x23EC..0x23FF a byte per world (op 6)
//   +0x3FEC..0x4038 a 4-byte record per world (op 0xD)
// Left out: the journal-like areas +0x3724 and +0x4038, and everything per player (characters, items, munny, EXP).
// Here:
//   host:   while in a room, compares these areas with what it sent last, every 10 frames. On a change it sends the
//           whole block (8455 bytes) in 9 parts of up to 1024 ("KH2U", one generation number for the block), and
//           again every 3 s, as Steam may drop a part.
//   friend: keeps the last complete block. Just before each of our room loads (CommitLocation, exe+0x152BE0, which
//           every load path calls and which reads the programs from +0x10) it writes the block over our save's
//           areas, byte for byte, without running any of the flags' scripts; logged with what differed. A chest
//           bit the host's new block has and its last one didn't, and that ours doesn't have yet: the chest's item
//           (the treasure table, L134) goes into our bag through the game's own item add, exe+0x3C3F00(item, 100,
//           0), and the bit is set at once. More than 3 new chests in one block is a different save (the host
//           loaded another one): no items then.
//   A chest the friend opens: our hook on the chest take (exe+0x3A1B60, every chest's item and bit) tells the host
//           ("KH2U" kind 1), again every second until the host's block has the bit (30 s at most). The host, in a
//           room, gives itself the item and sets the bit unless it opened that chest too; its next block carries the
//           bit back. Until then our copy before a room load keeps the bit set, so the chest doesn't close again.
// The other player's chest in our room (your picks 2026-10-25, DECISIONS 'A chest the other player opens'): with its
//           item, our chest object of that treasure row (actor+0xC00) plays its opening motion 0x99 and loses its
//           'Open' (the chest's own steps of the game's open, exe+0x4025C0, L135), and the item goes into the game's
//           item window (exe+0x159550). The safety net, both games: 'Open' on a chest whose bit is already set (the
//           other player took it and our object missed it) only opens it: no item, no window.
// The block counts only while it is fresh (10 s): the host stopped sending at the title or in a load.
// A different save (your picks 2026-10-25): when our save has more than 4 story flags (+0x1C90) the host's
//           lacks, it is ahead of the host's or another playthrough, and the copy would erase that story. Then the
//           copy waits and the 'different save' screen opens (link_watch.cpp): Continue Playing = the host's story
//           from the next room load, Load = the title screen. A save that is only behind the host's (a rejoin from
//           an older save) is copied over without asking. In the 66 saves of one co-op file a later save lacks at
//           most 2 of an earlier one's flags (VERIFIED_OFFSETS `save+0x1C90`). Checked every half second in a
//           room and before each room load. The host is told ("KH2U" kind 2) and shows it in its information bar
//           (exe+0x17B710, L135). Only the first check after a save load opens the screen at once; later (mid-session)
//           our save must stay ahead for 5 s (your pick 2026-10-25): at a boss's end our game set 6 flags a second
//           before the host's block had them and the screen flashed up for half a second.

#include "story_sync.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "link_menu.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x5532484B;  // "KH2U"
constexpr std::uint16_t kVersion = 1;
constexpr std::uintptr_t kSave = 0x9A98B0;     // the save in memory, starts with "KH2J"
constexpr std::uintptr_t kNow = 0x717008;      // u8 world, room
constexpr std::uintptr_t kInField = 0x9BA8D0;  // u8: 0 while a room loads
constexpr std::uintptr_t kCommitLocation = 0x152BE0;
constexpr std::uint8_t kCommitLocationBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x56, 0x44,
                                                 0x0F, 0xB6, 0x11, 0x4C, 0x8B, 0xC1};
using PFN_CommitLocation = void(__fastcall*)(void*);
// Treasure table (L134): pointer; u16 count at +2, rows of 0xC from +4: +2 u16 item, +4 type (0 = chest), +5 world,
// +6 room, +0xA u16 chest bit.
constexpr std::uintptr_t kTreasures = 0x2A0F7A0;
// (item, 100 = to the bag, 0): false when it didn't fit; the game may move it on into a free item slot (Hi-Potion,
// bench 2026-10-25: Sora's record +0x3A)
constexpr std::uintptr_t kItemAdd = 0x3C3F00;
constexpr std::uint8_t kItemAddBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                          0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20};
using PFN_ItemAdd = bool(__fastcall*)(std::uint32_t, std::uint32_t, std::uint8_t);
constexpr std::uint32_t kToBag = 100;
constexpr std::uintptr_t kChestTake = 0x3A1B60;  // (treasure row): the item into the bag, the chest bit set
constexpr std::uint8_t kChestTakeBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89,
                                            0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20};
using PFN_ChestTake = std::uint32_t(__fastcall*)(std::uintptr_t);
constexpr std::uintptr_t kInfoBar = 0x17B710;  // (text): the top-left information bar (L135)
constexpr std::uint8_t kInfoBarBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9};
using PFN_InfoBar = void(__fastcall*)(const std::uint8_t*);
// A chest object's own open, from the reaction command 'Open' (0x20): (chest, user) (L135)
constexpr std::uintptr_t kOpenChest = 0x4025C0;
constexpr std::uint8_t kOpenChestBytes[] = {0x48, 0x83, 0xEC, 0x48, 0x80, 0xB9, 0xF8, 0x0B, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xDB};
using PFN_OpenChest = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
constexpr std::uintptr_t kSetMotion = 0x3C86A0;  // (actor+0x158, motion, blend, 0)
constexpr std::uint8_t kSetMotionBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40};
using PFN_SetMotion = void(__fastcall*)(std::uintptr_t, int, float, float);
constexpr std::uintptr_t kBlend = 0x623BAC;      // float: the blend the open passes
constexpr std::uintptr_t kChestState = 0x40A500;  // (actor+0x540, 0x20, -1): the open's 'done' entry
constexpr std::uint8_t kChestStateBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x45, 0x33, 0xD2, 0x44, 0x8B, 0xDA};
using PFN_ChestState = void(__fastcall*)(std::uintptr_t, int, int);
constexpr std::uintptr_t kItemWindow = 0x159550;  // (name text, n): one box of the item window
constexpr std::uint8_t kItemWindowBytes[] = {0x4C, 0x8B, 0x05, 0x69, 0x2E, 0x96, 0x00, 0x4C, 0x8B, 0xD1};
using PFN_ItemWindow = void(__fastcall*)(std::uintptr_t, std::uint32_t);
constexpr std::uintptr_t kIconLoad = 0x3B0CC0;  // (n): loads the box's icon; a box waits until it's in (exe+0x3B0BF0)
constexpr std::uint8_t kIconLoadBytes[] = {0x83, 0xF9, 0xFF, 0x74, 0x4D, 0x53, 0x48, 0x83, 0xEC, 0x40};
using PFN_IconLoad = void(__fastcall*)(std::uint32_t);
constexpr std::uintptr_t kItemRecord = 0x3E39A0;  // (item) -> record (+8 u16 name message, +0x14 u16 n) or 0
constexpr std::uint8_t kItemRecordBytes[] = {0x48, 0x8B, 0x15, 0xC9, 0x19, 0x64, 0x02, 0x45, 0x33, 0xC0};
using PFN_ItemRecord = std::uintptr_t(__fastcall*)(std::uint32_t);
constexpr std::uintptr_t kMessage = 0x3AD9A0;  // (message id) -> text or 0
constexpr std::uint8_t kMessageBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
using PFN_Message = std::uintptr_t(__fastcall*)(std::uint32_t);
constexpr std::uintptr_t kListHead = 0x2A171C8;  // the active actor list's first actor (our Sora)
constexpr std::uintptr_t kNextActor = 0xA90;     // u32 handle of the next
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;
constexpr std::uintptr_t kChestRow = 0xC00, kChestFlags = 0x120, kChestStateList = 0x540;
constexpr std::uint32_t kChestOpened = 0x20;
constexpr int kMotionOpening = 0x99;

struct Area {
    std::uint32_t offset, size;
    const char* name;
};
constexpr Area kAreas[] = {
    {0x10, 0x1C80, "room programs"},     {0x1C90, 0x260, "story flags"},  {0x1EF0, 0xE0, "script bits and pairs"},
    {0x22F8, 0x98, "rooms visited"},     {0x2390, 0x1C, "script bits"},   {0x23AC, 0x34, "chests"},
    {0x23EC, 0x13, "world bytes"},       {0x3FEC, 0x4C, "world records"},
};
constexpr int kAreaCount = sizeof(kAreas) / sizeof(kAreas[0]);
constexpr std::uint32_t kChestsOffset = 0x23AC, kChestsSize = 0x34;
constexpr std::uint32_t kFlagsOffset = 0x1C90, kFlagsSize = 0x260, kWorldFlags = 0x20;

constexpr std::uint32_t BlockSize() {
    std::uint32_t n = 0;
    for (const Area& a : kAreas) n += a.size;
    return n;
}
constexpr std::uint32_t kBlockSize = BlockSize();
constexpr std::uint32_t kPartSize = 1024;
constexpr std::uint32_t kParts = (kBlockSize + kPartSize - 1) / kPartSize;
static_assert(kParts <= 32, "one bit per part");

constexpr int kCompareFrames = 10, kResendFrames = 180;
constexpr ULONGLONG kFreshMs = 10000;
constexpr int kMaxNewChests = 3;
constexpr int kChestResendFrames = 60;
constexpr ULONGLONG kChestGiveUpMs = 30000;
constexpr int kExtraFlags = 4;  // more of our story flags than this that the host's lacks: a different save
constexpr ULONGLONG kAheadMs = 5000;  // mid-session, ahead this long before the screen opens
constexpr int kCheckFrames = 30;
constexpr int kNoteResendFrames = 60, kNoteSendFrames = 300;

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;  // 0 a part of the host's block, 1 a chest the friend opened (ChestPacket)
    std::uint32_t generation;
    std::uint16_t part, parts, offset, size;
};
struct ChestPacket {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;
    std::uint16_t bit;
};
struct NotePacket {  // kind 2: the friend's save check, for the host's information bar
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t kind;
    std::uint8_t seq;    // one per change
    std::uint8_t state;  // 0 no longer differs, 1 differs (the screen is up), 2 Continue Playing, 3 Load
    std::uint16_t ours, theirs;  // story flags only the friend's save / only the host's has
};
struct Packet {
    Header h;
    std::uint8_t data[kPartSize];
};
#pragma pack(pop)

bool g_on = false, g_host = false;
PFN_CommitLocation g_commit = nullptr;
PFN_ItemAdd g_itemAdd = nullptr;
PFN_ChestTake g_chestTake = nullptr;
PFN_InfoBar g_infoBar = nullptr;
PFN_OpenChest g_openChest = nullptr;
PFN_SetMotion g_setMotion = nullptr;
PFN_ChestState g_chestState = nullptr;
PFN_ItemWindow g_itemWindow = nullptr;
PFN_IconLoad g_iconLoad = nullptr;
PFN_ItemRecord g_itemRecord = nullptr;
PFN_Message g_message = nullptr;
std::uint64_t g_frame = 0;

// Host.
std::uint8_t g_sent[kBlockSize];
bool g_sentValid = false;
std::uint32_t g_generation = 0;
std::uint64_t g_lastSendFrame = 0;
int g_changeLogs = 0;
std::uint16_t g_friendChests[16];  // chests the friend opened, given here once we're in a room
int g_friendChestCount = 0;
bool g_noteSeen = false;
std::uint8_t g_noteSeq = 0;  // the friend's last note
int g_noteShow = -1;         // its state, to show once we're in a room

// Friend.
std::uint8_t g_parts[kBlockSize];  // the generation being put together
std::uint32_t g_partsGeneration = 0, g_partsHave = 0;
std::uint8_t g_hostBlock[kBlockSize];  // the last complete block
bool g_haveHost = false;
std::uint32_t g_hostGeneration = 0;
ULONGLONG g_hostAt = 0;
std::uint8_t g_lastChests[kChestsSize];  // the chests of the host's block before
bool g_lastChestsValid = false;
std::uint16_t g_owed[16];  // chest bits whose item we still give, once in a room (our own bit wasn't set when the
                           // block came; a room load's copy may set it before)
int g_owedCount = 0;
std::uint32_t g_copies = 0, g_given = 0;
struct Mine {
    std::uint16_t bit;
    ULONGLONG at;
};
Mine g_mine[16];  // chests we opened that the host's block doesn't have yet
int g_mineCount = 0;
bool g_differs = false;   // our save has story the host's lacks: the copy waits for the player's pick
bool g_accepted = false;  // Continue Playing picked: the next copy goes ahead anyway
bool g_checkedSinceLoad = false;  // a check since this save's load (or our start): later ones are mid-session
ULONGLONG g_aheadSince = 0;       // mid-session, our save went ahead then: the screen waits kAheadMs
std::uint8_t g_acceptedFlags[kFlagsSize];  // our story flags at that pick: another save loaded since doesn't count
bool g_leftToLoad = false;  // Load picked: no check until the next room load (the other save's first)
NotePacket g_note {};     // our last note, resent for a while
std::uint64_t g_noteUntil = 0;

template <typename T>
T Read(std::uintptr_t address) {
    return *reinterpret_cast<const volatile T*>(address);
}

std::uintptr_t Save() { return ExeBase() + kSave; }

bool SaveReady() { return std::memcmp(reinterpret_cast<const void*>(Save()), "KH2J", 4) == 0; }

bool InRoom() {
    std::uintptr_t now = ExeBase() + kNow;
    return Read<std::uint8_t>(ExeBase() + kInField) != 0 && Read<std::uint8_t>(now) < 0x13 && SaveReady();
}

// The block's place of an area.
std::uint32_t InBlock(std::uint32_t offset) {
    std::uint32_t at = 0;
    for (const Area& a : kAreas) {
        if (a.offset == offset) return at;
        at += a.size;
    }
    return 0;
}

std::uint32_t ChestsInBlock() { return InBlock(kChestsOffset); }

void Gather(std::uint8_t* out) {
    std::uint32_t at = 0;
    for (const Area& a : kAreas) {
        std::memcpy(out + at, reinterpret_cast<const void*>(Save() + a.offset), a.size);
        at += a.size;
    }
}

// Bytes that differ between two blocks, per area; returns the total.
std::uint32_t Differ(const std::uint8_t* x, const std::uint8_t* y, std::uint32_t (&perArea)[kAreaCount]) {
    std::uint32_t at = 0, total = 0;
    for (int i = 0; i < kAreaCount; ++i) {
        perArea[i] = 0;
        for (std::uint32_t k = 0; k < kAreas[i].size; ++k)
            if (x[at + k] != y[at + k]) ++perArea[i];
        total += perArea[i];
        at += kAreas[i].size;
    }
    return total;
}

void DifferText(const std::uint32_t (&perArea)[kAreaCount], char* out, int size) {
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < kAreaCount && n < size; ++i) {
        if (!perArea[i]) continue;
        int w = std::snprintf(out + n, size - n, "%s%s %u", n ? ", " : "", kAreas[i].name, perArea[i]);
        if (w < 0) break;
        n += w;
    }
    if (!n) std::snprintf(out, size, "nothing");
}

void SendBlock() {
    Packet p {};
    p.h.magic = kMagic;
    p.h.version = kVersion;
    p.h.build = AvatarLinkBuild();
    p.h.generation = g_generation;
    p.h.parts = static_cast<std::uint16_t>(kParts);
    for (std::uint32_t i = 0; i < kParts; ++i) {
        std::uint32_t offset = i * kPartSize, size = kBlockSize - offset < kPartSize ? kBlockSize - offset : kPartSize;
        p.h.part = static_cast<std::uint16_t>(i);
        p.h.offset = static_cast<std::uint16_t>(offset);
        p.h.size = static_cast<std::uint16_t>(size);
        std::memcpy(p.data, g_sent + offset, size);
        AvatarLinkSendRaw(&p, static_cast<int>(sizeof(Header) + size));
    }
    g_lastSendFrame = g_frame;
}

void HostFrame() {
    if (!InRoom()) return;
    if (g_sentValid && g_frame % kCompareFrames != 0 && g_frame - g_lastSendFrame < kResendFrames) return;
    static std::uint8_t now[kBlockSize];
    Gather(now);
    if (!g_sentValid || std::memcmp(now, g_sent, kBlockSize) != 0) {
        std::uint32_t perArea[kAreaCount];
        std::uint32_t total = g_sentValid ? Differ(g_sent, now, perArea) : kBlockSize;
        std::memcpy(g_sent, now, kBlockSize);
        ++g_generation;
        if (g_changeLogs < 200) {
            ++g_changeLogs;
            char text[200];
            if (g_sentValid) DifferText(perArea, text, sizeof(text));
            else std::snprintf(text, sizeof(text), "the first block");
            std::uintptr_t at = ExeBase() + kNow;
            Log("story sync: our story changed in %02X/%02X (%u bytes: %s); sent as block %u", Read<std::uint8_t>(at),
                Read<std::uint8_t>(at + 1), total, text, g_generation);
        }
        g_sentValid = true;
        SendBlock();
    } else if (g_frame - g_lastSendFrame >= kResendFrames) {
        SendBlock();
    }
}

// The treasure row with this chest bit, or 0.
std::uintptr_t ChestRow(std::uint16_t bit) {
    std::uintptr_t table = Read<std::uintptr_t>(ExeBase() + kTreasures);
    if (!table) return 0;
    std::uint16_t count = Read<std::uint16_t>(table + 2);
    if (count > 4000) return 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uintptr_t row = table + 4 + 0xC * static_cast<std::uintptr_t>(i);
        if (Read<std::uint8_t>(row + 4) == 0 && Read<std::uint16_t>(row + 0xA) == bit) return row;
    }
    return 0;
}

bool OurChest(std::uint16_t bit) {
    return (Read<std::uint8_t>(Save() + kChestsOffset + bit / 8) >> (bit % 8)) & 1;
}

void MarkChest(std::uint16_t bit) {
    std::uint8_t* b = reinterpret_cast<std::uint8_t*>(Save() + kChestsOffset + bit / 8);
    *b = static_cast<std::uint8_t>(*b | 1 << (bit % 8));
}

std::uintptr_t Decode(std::uint32_t handle) {
    return reinterpret_cast<std::uintptr_t(__fastcall*)(std::uint32_t)>(ExeBase() + kDecodeHandle)(handle);
}

// Our chest object of this treasure row in the room we're in, or 0.
std::uintptr_t ChestActor(std::uintptr_t row) {
    std::uintptr_t now = ExeBase() + kNow;
    if (!row || Read<std::uint8_t>(row + 5) != Read<std::uint8_t>(now) ||
        Read<std::uint8_t>(row + 6) != Read<std::uint8_t>(now + 1))
        return 0;
    std::uintptr_t a = Read<std::uintptr_t>(ExeBase() + kListHead);
    for (int n = 0; a && n < 512; ++n, a = Decode(Read<std::uint32_t>(a + kNextActor)))
        if (Read<std::uintptr_t>(a + kChestRow) == row) return a;
    return 0;
}

// The chest's own steps of the game's open: its opening motion, 'Open' gone. False when it was open already.
bool PlayOpening(std::uintptr_t chest) {
    if (!g_setMotion || !g_chestState || (Read<std::uint32_t>(chest + kChestFlags) & kChestOpened)) return false;
    g_setMotion(chest + 0x158, kMotionOpening, Read<float>(ExeBase() + kBlend), 0.0f);
    g_chestState(chest + kChestStateList, 0x20, -1);
    *reinterpret_cast<std::uint32_t*>(chest + kChestFlags) |= kChestOpened;
    return true;
}

// The item's box in the game's item window, as a chest of ours shows it.
bool ShowItem(std::uint16_t item) {
    if (!g_itemWindow || !g_iconLoad || !g_itemRecord || !g_message) return false;
    std::uintptr_t rec = g_itemRecord(item);
    if (!rec) return false;
    std::uintptr_t name = g_message(Read<std::uint16_t>(rec + 8));
    if (!name) return false;
    std::uint16_t icon = Read<std::uint16_t>(rec + 0x14);
    g_iconLoad(icon);  // as the game's chest take does
    g_itemWindow(name, icon);
    return true;
}

// A chest the other player opened: its item into our bag, its bit set; in our room, our chest object opens too.
void GiveChest(std::uint16_t bit, const char* who) {
    std::uintptr_t row = ChestRow(bit);
    if (!row) {
        Log("story sync: %s opened chest %u, which has no treasure row here: only marked", who, bit);
    } else {
        std::uint16_t item = Read<std::uint16_t>(row + 2);
        bool added = g_itemAdd && g_itemAdd(item, kToBag, 0);
        if (added) ++g_given;
        bool shown = added && ShowItem(item);
        std::uintptr_t chest = ChestActor(row);
        bool opened = chest && PlayOpening(chest);
        Log("story sync: %s opened chest %u (%02X/%02X): its item 0x%X %s%s; %s", who, bit, Read<std::uint8_t>(row + 5),
            Read<std::uint8_t>(row + 6), item, added ? "is ours (bag or an item slot)" : "did NOT fit",
            shown ? ", in the item window" : added ? ", NOT in the item window" : "",
            opened ? "our chest object opens" : chest ? "our chest object was open already" : "not in our room");
    }
    MarkChest(bit);
}

int FindMine(std::uint16_t bit) {
    for (int i = 0; i < g_mineCount; ++i)
        if (g_mine[i].bit == bit) return i;
    return -1;
}

void DropMine(int i) { g_mine[i] = g_mine[--g_mineCount]; }

bool Has(const std::uint8_t* chests, std::uint32_t bit) { return (chests[bit / 8] >> (bit % 8)) & 1; }

// A complete block from the host: the chests it opened since its last one, which we haven't, are owed; ours that it
// now has are done.
void OnBlock() {
    const std::uint8_t* chests = g_hostBlock + ChestsInBlock();
    if (g_lastChestsValid && SaveReady()) {
        std::uint16_t fresh[kMaxNewChests + 1];
        int n = 0;
        for (std::uint32_t bit = 1; bit < kChestsSize * 8; ++bit)
            if (Has(chests, bit) && !Has(g_lastChests, bit) && FindMine(static_cast<std::uint16_t>(bit)) < 0 &&
                n <= kMaxNewChests)
                fresh[n++] = static_cast<std::uint16_t>(bit);
        if (n > kMaxNewChests) {
            Log("story sync: the host's block %u has more than %d new chests (another save?): no items for them",
                g_hostGeneration, kMaxNewChests);
        } else {
            for (int i = 0; i < n; ++i) {
                if (OurChest(fresh[i])) {
                    Log("story sync: the host opened chest %u, which we opened too: no second item", fresh[i]);
                } else if (g_owedCount < static_cast<int>(sizeof(g_owed) / sizeof(g_owed[0]))) {
                    g_owed[g_owedCount++] = fresh[i];
                }
            }
        }
    }
    for (int i = g_mineCount - 1; i >= 0; --i) {
        if (!Has(chests, g_mine[i].bit)) continue;
        Log("story sync: the host's block %u has our chest %u", g_hostGeneration, g_mine[i].bit);
        DropMine(i);
    }
    std::memcpy(g_lastChests, chests, kChestsSize);
    g_lastChestsValid = true;
}

void GiveOwed() {
    if (!g_owedCount || !InRoom()) return;
    for (int i = 0; i < g_owedCount; ++i) GiveChest(g_owed[i], "the host");
    g_owedCount = 0;
}

void SendChest(std::uint16_t bit) {
    ChestPacket c {kMagic, kVersion, AvatarLinkBuild(), 1, bit};
    AvatarLinkSendRaw(&c, sizeof(c));
}

// Friend: our chests the host hasn't confirmed yet, again every second.
void ResendMine() {
    if (!g_mineCount || g_frame % kChestResendFrames != 0) return;
    ULONGLONG now = GetTickCount64();
    for (int i = g_mineCount - 1; i >= 0; --i) {
        if (g_haveHost && Has(g_hostBlock + ChestsInBlock(), g_mine[i].bit)) {  // it had opened that chest itself
            Log("story sync: the host's block %u has our chest %u", g_hostGeneration, g_mine[i].bit);
            DropMine(i);
        } else if (now - g_mine[i].at > kChestGiveUpMs) {
            Log("story sync: the host never confirmed our chest %u (30 s): the next room load may close it again",
                g_mine[i].bit);
            DropMine(i);
        } else {
            SendChest(g_mine[i].bit);
        }
    }
}

// Host: the chests the friend opened, once we're in a room.
void GiveFriendChests() {
    if (!g_friendChestCount || !InRoom()) return;
    for (int i = 0; i < g_friendChestCount; ++i) {
        std::uint16_t bit = g_friendChests[i];
        if (OurChest(bit)) Log("story sync: the friend opened chest %u, which we opened too: no second item", bit);
        else GiveChest(bit, "the friend");
    }
    g_friendChestCount = 0;
}

int Bits(std::uint32_t b) {
    int n = 0;
    for (; b; b &= b - 1) ++n;
    return n;
}

// Story flags only our save has / only the host's has, and in how many worlds.
struct Gap {
    int ours, theirs, oursWorlds, theirsWorlds;
};
Gap CompareFlags() {
    Gap g {};
    const std::uint8_t* host = g_hostBlock + InBlock(kFlagsOffset);
    const std::uint8_t* ours = reinterpret_cast<const std::uint8_t*>(Save() + kFlagsOffset);
    for (std::uint32_t w = 0; w < kFlagsSize / kWorldFlags; ++w) {
        int a = 0, b = 0;
        for (std::uint32_t i = w * kWorldFlags; i < (w + 1) * kWorldFlags; ++i) {
            a += Bits(ours[i] & ~host[i] & 0xFFu);
            b += Bits(host[i] & ~ours[i] & 0xFFu);
        }
        g.ours += a;
        g.theirs += b;
        g.oursWorlds += a != 0;
        g.theirsWorlds += b != 0;
    }
    return g;
}

void SendNote() { AvatarLinkSendRaw(&g_note, sizeof(g_note)); }

void Note(std::uint8_t state, const Gap& g) {
    g_note = {kMagic, kVersion, AvatarLinkBuild(), 2, static_cast<std::uint8_t>(g_note.seq + 1), state,
              static_cast<std::uint16_t>(g.ours), static_cast<std::uint16_t>(g.theirs)};
    g_noteUntil = g_frame + kNoteSendFrames;
    SendNote();
}

// Friend: does our save have story the host's lacks? With a fresh block and the save there only.
void CheckSave() {
    if (g_leftToLoad || !g_haveHost || GetTickCount64() - g_hostAt >= kFreshMs || !SaveReady()) return;
    Gap g = CompareFlags();
    bool differs = g.ours > kExtraFlags && !g_accepted;
    bool first = !g_checkedSinceLoad;
    g_checkedSinceLoad = true;
    if (differs && !g_differs && !first) {
        ULONGLONG now = GetTickCount64();
        if (!g_aheadSince) {
            g_aheadSince = now;
            Log("story sync: mid-session our save went %d story flags ahead of the host's: the screen waits %llu s",
                g.ours, kAheadMs / 1000);
        }
        if (now - g_aheadSince < kAheadMs) return;
    }
    if (!differs && g_aheadSince)
        Log("story sync: the host's block caught up within %llu s (%d flags ahead now): no screen", kAheadMs / 1000,
            g.ours);
    g_aheadSince = 0;
    if (differs == g_differs) return;
    g_differs = differs;
    if (differs) {
        Log("story sync: our save has %d story flags the host's lacks (in %d worlds; the host's has %d we lack, in %d): "
            "ahead of the host or another playthrough: the copy waits for the player's pick ('different save' screen)",
            g.ours, g.oursWorlds, g.theirs, g.theirsWorlds);
        Note(1, g);
    } else {
        Log("story sync: our save no longer has more than %d story flags the host's lacks (%d): the copy goes on",
            kExtraFlags, g.ours);
        Note(0, g);
    }
}

std::uint32_t __fastcall HookChestTake(std::uintptr_t row) {
    std::uint32_t result = g_chestTake(row);
    if (g_on && !g_host && CoopActive()) {
        __try {
            std::uint16_t bit = Read<std::uint16_t>(row + 0xA);
            if (Read<std::uint8_t>(row + 4) == 0 && bit && bit < kChestsSize * 8 && FindMine(bit) < 0 &&
                g_mineCount < static_cast<int>(sizeof(g_mine) / sizeof(g_mine[0]))) {
                g_mine[g_mineCount++] = {bit, GetTickCount64()};
                SendChest(bit);
                Log("story sync: we opened chest %u (%02X/%02X, item 0x%X): sent to the host", bit,
                    Read<std::uint8_t>(row + 5), Read<std::uint8_t>(row + 6), Read<std::uint16_t>(row + 2));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int logged = 0;
            if (logged++ < 5) Log("story sync: exception reading a chest we opened");
        }
    }
    return result;
}

// 'Open' on a chest: one whose bit is already set (the other player took it; our object was set up before) only
// opens, without the chest take (no second item, no window).
void __fastcall HookOpenChest(std::uintptr_t chest, std::uintptr_t user) {
    if (g_on && CoopActive()) {
        __try {
            std::uintptr_t row = Read<std::uintptr_t>(chest + kChestRow);
            std::uint16_t bit = row ? Read<std::uint16_t>(row + 0xA) : 0;
            if (bit && bit < kChestsSize * 8 && OurChest(bit)) {
                bool opened = PlayOpening(chest);
                Log("story sync: 'Open' on chest %u, which the other player took already: %s, no item", bit,
                    opened ? "it only opens" : "it was open already");
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int logged = 0;
            if (logged++ < 5) Log("story sync: exception checking a chest's 'Open'");
        }
    }
    g_openChest(chest, user);
}

void __fastcall HookCommitLocation(void* packet) {
    if (g_on && !g_host && CoopActive()) {
        __try {
            g_leftToLoad = false;
            if (g_accepted) {
                const std::uint8_t* now = reinterpret_cast<const std::uint8_t*>(Save() + kFlagsOffset);
                int changed = 0;
                for (std::uint32_t i = 0; i < kFlagsSize; ++i) changed += Bits(now[i] ^ g_acceptedFlags[i]);
                if (changed > kExtraFlags) {
                    g_accepted = false;
                    Log("story sync: our story differs by %d flags from the one Continue Playing was picked on "
                        "(another save loaded): the pick no longer counts", changed);
                }
            }
            CheckSave();
            if (g_differs || g_aheadSince) {  // waiting out the 5 s too: the copy would erase our new flags
                static int logged = 0;
                if (logged++ < 10)
                    Log("story sync: room load while our save differs from the host's: our story stays (the player's "
                        "pick first)");
            } else if (g_haveHost && GetTickCount64() - g_hostAt < kFreshMs && SaveReady()) {
                static std::uint8_t ours[kBlockSize];
                Gather(ours);
                std::uint32_t perArea[kAreaCount];
                std::uint32_t total = Differ(ours, g_hostBlock, perArea);
                std::uint32_t at = 0;
                for (const Area& a : kAreas) {
                    std::memcpy(reinterpret_cast<void*>(Save() + a.offset), g_hostBlock + at, a.size);
                    at += a.size;
                }
                for (int i = 0; i < g_mineCount; ++i) MarkChest(g_mine[i].bit);  // not in the host's block yet
                ++g_copies;
                if (g_accepted) Log("story sync: Continue Playing: the host's story is ours now");
                g_accepted = false;
                const std::uint8_t* p = static_cast<const std::uint8_t*>(packet);
                if (total || g_copies <= 3) {
                    char text[200];
                    DifferText(perArea, text, sizeof(text));
                    Log("story sync: the host's block %u copied before our load of %02X/%02X (%u bytes differed: %s%s)",
                        g_hostGeneration, p[0], p[1], total, text,
                        g_mineCount ? "; our chests the host hasn't confirmed kept" : "");
                }
            } else if (g_haveHost) {
                static int logged = 0;
                if (logged++ < 10) Log("story sync: room load with no fresh block from the host: our story stays");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int logged = 0;
            if (logged++ < 5) Log("story sync: exception in the copy before a room load");
        }
    }
    g_commit(packet);
}

void OnNotePacket(const char* buf, int n) {
    if (!g_host || n != static_cast<int>(sizeof(NotePacket))) return;
    NotePacket c;
    std::memcpy(&c, buf, sizeof(c));
    if (c.version != kVersion || c.build != AvatarLinkBuild() || (g_noteSeen && c.seq == g_noteSeq)) return;
    g_noteSeen = true;
    g_noteSeq = c.seq;
    static const char* const kWhat[] = {"no longer differs from ours: it gets our story",
                                        "differs from ours (ahead or another playthrough): the friend picks",
                                        "differs from ours: the friend picked Continue Playing (our story)",
                                        "differs from ours: the friend picked Load (back to the title)"};
    Log("story sync: the friend's save %s; story flags only theirs %u, only ours %u",
        c.state < 4 ? kWhat[c.state] : "?", c.ours, c.theirs);
    if (c.state >= 1 && c.state <= 3) g_noteShow = c.state;
}

// Host: the friend's note in our information bar, once we're in a room.
void ShowNote() {
    if (g_noteShow < 0 || !InRoom()) return;
    static std::uint8_t text[3][64];
    static const char* const kText[] = {"The friend's save differs from yours.", "The friend plays on with your story.",
                                        "The friend is loading another save."};
    int i = g_noteShow - 1;
    g_noteShow = -1;
    if (!g_infoBar) return;
    LinkMenuEncode(kText[i], text[i], sizeof(text[i]));
    g_infoBar(text[i]);
}

void OnChestPacket(const char* buf, int n) {
    if (!g_host || n != static_cast<int>(sizeof(ChestPacket))) return;
    ChestPacket c;
    std::memcpy(&c, buf, sizeof(c));
    if (c.version != kVersion || c.build != AvatarLinkBuild() || !c.bit || c.bit >= kChestsSize * 8) return;
    for (int i = 0; i < g_friendChestCount; ++i)
        if (g_friendChests[i] == c.bit) return;  // a resend
    if (g_friendChestCount < static_cast<int>(sizeof(g_friendChests) / sizeof(g_friendChests[0])))
        g_friendChests[g_friendChestCount++] = c.bit;
}

}  // namespace

void StorySyncInit(bool host) {
    g_host = host;
    g_on = EnvInt("KH2COOP_STORY_SYNC", 1) == 1;
    if (!g_on) {
        Log("story sync: off (STORY_SYNC=0)");
        return;
    }
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kItemAdd), kItemAddBytes, sizeof(kItemAddBytes)) == 0)
        g_itemAdd = reinterpret_cast<PFN_ItemAdd>(ExeBase() + kItemAdd);
    else
        Log("story sync: the item add's bytes differ (different game build?): the other player's chests give no items");
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + kInfoBar), kInfoBarBytes, sizeof(kInfoBarBytes)) == 0)
        g_infoBar = reinterpret_cast<PFN_InfoBar>(ExeBase() + kInfoBar);
    else if (host)
        Log("story sync: the information bar's bytes differ (different game build?): the friend's save notes only "
            "in this log");
    auto bytesMatch = [](std::uintptr_t rva, const std::uint8_t* bytes, std::size_t size) {
        return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, size) == 0;
    };
    if (bytesMatch(kSetMotion, kSetMotionBytes, sizeof(kSetMotionBytes)) &&
        bytesMatch(kChestState, kChestStateBytes, sizeof(kChestStateBytes))) {
        g_setMotion = reinterpret_cast<PFN_SetMotion>(ExeBase() + kSetMotion);
        g_chestState = reinterpret_cast<PFN_ChestState>(ExeBase() + kChestState);
    } else {
        Log("story sync: the chest motion's bytes differ (different game build?): the other player's chests don't "
            "open in our room");
    }
    if (bytesMatch(kItemWindow, kItemWindowBytes, sizeof(kItemWindowBytes)) &&
        bytesMatch(kIconLoad, kIconLoadBytes, sizeof(kIconLoadBytes)) &&
        bytesMatch(kItemRecord, kItemRecordBytes, sizeof(kItemRecordBytes)) &&
        bytesMatch(kMessage, kMessageBytes, sizeof(kMessageBytes))) {
        g_itemWindow = reinterpret_cast<PFN_ItemWindow>(ExeBase() + kItemWindow);
        g_iconLoad = reinterpret_cast<PFN_IconLoad>(ExeBase() + kIconLoad);
        g_itemRecord = reinterpret_cast<PFN_ItemRecord>(ExeBase() + kItemRecord);
        g_message = reinterpret_cast<PFN_Message>(ExeBase() + kMessage);
    } else {
        Log("story sync: the item window's bytes differ (different game build?): the other player's items show no box");
    }
    if (!HookFunction(kOpenChest, kOpenChestBytes, sizeof(kOpenChestBytes), reinterpret_cast<void*>(HookOpenChest),
                      reinterpret_cast<void**>(&g_openChest), "OpenChest (story sync)"))
        Log("story sync: the chest open could not be hooked: a chest the other player took can give a second item");
    if (host) {
        Log("story sync: on (host): our story progress (%u bytes, %u parts) is sent to the friend's game; the friend's "
            "chests are opened here too", kBlockSize, kParts);
        return;
    }
    g_on = HookFunction(kCommitLocation, kCommitLocationBytes, sizeof(kCommitLocationBytes),
                        reinterpret_cast<void*>(HookCommitLocation), reinterpret_cast<void**>(&g_commit),
                        "CommitLocation (story sync)");
    if (g_on && !HookFunction(kChestTake, kChestTakeBytes, sizeof(kChestTakeBytes),
                              reinterpret_cast<void*>(HookChestTake), reinterpret_cast<void**>(&g_chestTake),
                              "ChestTake (story sync)"))
        Log("story sync: the chest take could not be hooked: our chests stay ours (the host's copy closes them again)");
    Log("story sync: %s", g_on ? "on (friend): the host's story progress is copied over ours before each room load"
                               : "OFF: CommitLocation could not be hooked");
}

void StorySyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        if (g_host) {
            GiveFriendChests();
            HostFrame();
            ShowNote();
        } else {
            GiveOwed();
            ResendMine();
            if (g_frame % kCheckFrames == 0 && InRoom()) CheckSave();
            if (g_frame < g_noteUntil && g_frame % kNoteResendFrames == 0) SendNote();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 5) Log("story sync: exception in the frame update");
    }
    if (g_frame % 36000 == 0 && (g_copies || g_given))
        Log("story sync: copies before room loads %u, chest items given %u", g_copies, g_given);
}

void StorySyncOnPacket(const char* buf, int n) {
    if (!g_on || n < 11) return;
    if (static_cast<std::uint8_t>(buf[10]) == 1) {  // the kind byte, after magic, version and build
        OnChestPacket(buf, n);
        return;
    }
    if (static_cast<std::uint8_t>(buf[10]) == 2) {
        OnNotePacket(buf, n);
        return;
    }
    if (g_host || n < static_cast<int>(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, buf, sizeof(h));
    if (h.version != kVersion || h.build != AvatarLinkBuild() || h.kind != 0 || h.parts != kParts || h.part >= kParts)
        return;
    if (h.offset != h.part * kPartSize || h.offset + h.size > kBlockSize ||
        n != static_cast<int>(sizeof(Header) + h.size))
        return;
    if (g_haveHost && h.generation == g_hostGeneration) {  // a resend of the block we have
        g_hostAt = GetTickCount64();
        return;
    }
    if (h.generation != g_partsGeneration) {
        g_partsGeneration = h.generation;
        g_partsHave = 0;
    }
    std::memcpy(g_parts + h.offset, buf + sizeof(Header), h.size);
    g_partsHave |= 1u << h.part;
    if (g_partsHave != (1u << kParts) - 1) return;
    std::memcpy(g_hostBlock, g_parts, kBlockSize);
    bool first = !g_haveHost;
    g_haveHost = true;
    g_hostGeneration = h.generation;
    g_hostAt = GetTickCount64();
    g_partsHave = 0;
    if (first) Log("story sync: the host's first block (%u) is here", h.generation);
    __try {
        OnBlock();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 5) Log("story sync: exception reading the host's chests");
    }
}

bool StorySyncSaveDiffers() { return g_on && !g_host && g_differs; }

const char* StorySyncScreenLine() {
    return "Your save differs from the Host's.\nContinue Playing uses the Host's story.";
}

void StorySyncChoose(bool hostStory) {
    if (!g_differs) return;
    g_differs = false;
    Gap g {};
    __try {
        g = CompareFlags();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (hostStory) {
        g_accepted = true;
        std::memcpy(g_acceptedFlags, reinterpret_cast<const void*>(Save() + kFlagsOffset), kFlagsSize);
        Log("story sync: Continue Playing: the host's story is copied over ours at the next room load");
        Note(2, g);
    } else {
        g_leftToLoad = true;
        Log("story sync: Load: our save is left as it is; to the title screen");
        Note(3, g);
    }
}

void StorySyncOnLeave() {
    if (!g_on || g_host) return;
    if (g_accepted || g_differs) Log("story sync: to the title screen: the 'different save' pick is dropped");
    g_accepted = g_differs = false;
    g_checkedSinceLoad = false;
    g_aheadSince = 0;
    g_leftToLoad = true;  // until the next room load, the next save's first
}

void StorySyncDebugChest(int bit) {
    if (!g_on || g_host || !g_chestTake) {
        Log("story sync: test chest %d: only in the friend's game with its chest hook", bit);
        return;
    }
    __try {
        std::uintptr_t row = ChestRow(static_cast<std::uint16_t>(bit));
        if (!row) {
            Log("story sync: test chest %d: no treasure row", bit);
            return;
        }
        Log("story sync: test chest %d: opened through the game's chest take", bit);
        HookChestTake(row);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("story sync: test chest %d: exception", bit);
    }
}

void StorySyncDebugOpen(int bit) {
    if (!g_on || !g_openChest) {
        Log("story sync: test open %d: no chest open hook", bit);
        return;
    }
    __try {
        std::uintptr_t chest = ChestActor(ChestRow(static_cast<std::uint16_t>(bit)));
        std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + kListHead);
        if (!chest || !sora) {
            Log("story sync: test open %d: no chest object of it in this room", bit);
            return;
        }
        Log("story sync: test open %d: 'Open' on our chest object %p (flags 0x%X)", bit, reinterpret_cast<void*>(chest),
            Read<std::uint32_t>(chest + kChestFlags));
        HookOpenChest(chest, sora);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("story sync: test open %d: exception", bit);
    }
}

}  // namespace kh2coop
