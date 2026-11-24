// The other Sora's copy on its own stat sheet (TODO 3.16, 3.4; DECISIONS 'The copy carries its player's whole Sora
// record'; research L1977-L1986, session 80).
//
// The game keeps one stat sheet per character in a pool (L1977), so the copy and our Sora shared Sora's: hits on the
// copy were worked out with our Defense, armor and HP (L1980), gave us Damage Drive and MP Rage (L1981), and our MP
// recharge ran twice a frame (L1983). Here the copy gets a sheet of its own, made the way exe+0x3C03F0 makes one but in
// our memory, outside the pool, so the game's by-character lookup (exe+0x3C04E0) never hands it to anyone else:
//   - exe+0x3C0010 sets it up (zeroes, defaults, one user, the ability block), +0x260 = 1 (Sora's level table), +0x250
//     = the handle of our copy of the record (the other player's from peer_sora.cpp, checked first), +0x254 and +0x26C
//     stay 0 (no form, no Drive gauge, nothing written to a save); the game's rebuild (exe+0x3C1D90) does the rest:
//     level, HP, MP, strength, magic, defense, AP, armor, accessories, abilities (L1978);
//   - once per copy, while its +0x5C0 is still our Sora's: +0x268 = the copy, its users pinned high (the copy's
//     teardown runs the release exe+0x3C07E0 on it, which would push a pool index at 0), then the copy's +0x5C0 = ours
//     and the shared sheet's second user released;
//   - each frame its HP and MP are the other player's (the pose), HP never below 1: only the game's HP change starts
//     a KO (L1982); a changed record rebuilds it;
//   - our Sora's level-up rebuilds the player pointer's sheet (L1979): while aggro.cpp has the copy there, the hook on
//     exe+0x3ECF30 puts our Sora back for the call.
// The copy's refreshes (puppet.cpp HookSheetRefresh) run exe+0x3D6850 on it: a commit into our record copy, a rebuild.
// shortcut: the copy has no Drive Form bonuses or Anti factor (its +0x254 / +0xDE0 stay 0); add them if a fight shows
// the difference.
// KH2COOP_COPY_SHEET: 1 = the other player's record (default), 2 = our own record (test: today's numbers on a sheet of
// its own), 0 = off (the shared sheet, as before).

#include "copy_sheet.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "avatar_link.hpp"
#include "common.hpp"
#include "game.hpp"
#include "peer_sora.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kSheetSize = 0x278;
constexpr std::uintptr_t kActorSheet = 0x5C0;
constexpr std::uintptr_t kSheetRecord = 0x250, kSheetChar = 0x260, kSheetUsers = 0x264, kSheetOwner = 0x268;
constexpr std::uintptr_t kSheetMp = 0x180, kSheetMaxMp = 0x184;  // F38
constexpr std::int32_t kPinnedUsers = 0x40000000;
constexpr std::uintptr_t kSheetInit = 0x3C0010;     // (sheet): zeroes, defaults, one user, the ability block (L1977)
constexpr std::uintptr_t kSheetBuild = 0x3C1D90;    // (sheet, record or 0 = +0x250's) (L1978)
constexpr std::uintptr_t kSheetRelease = 0x3C07E0;  // (sheet): commit, users--, below 1 back to the pool (L1977)
constexpr std::uintptr_t kLevelUp = 0x3ECF30;       // (exp): EXP, level-ups, rebuilds the player pointer's sheet (L1979)
constexpr std::uint8_t kLevelUpBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                          0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
// The record (OpenKH SaveCharacter.cs, CharacterFinalMix): +0 / +2 weapons, +4..+7 HP and MP now / max, +0xF level,
// +0x10 / +0x11 armor / accessory slots, 8 each at +0x14 / +0x24, 0x50 abilities at +0x54 (top bit = equipped).
constexpr int kSlots = 8, kAbilities = 0x50;
// An item row's kind, u8 +2 (OpenKH Item.cs): Keyblade 2 .. the last weapon 0xD, armor 0xE, accessory 0xF, ability 0x13.
constexpr std::uint8_t kKeyblade = 2, kLastWeapon = 0xD, kArmor = 0xE, kAccessory = 0xF, kAbility = 0x13;

using PFN_Sheet = void(__fastcall*)(std::uintptr_t);
using PFN_SheetBuild = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_Encode = std::uint32_t(__fastcall*)(std::uintptr_t);
using PFN_Item = std::uintptr_t(__fastcall*)(int);
using PFN_LevelUp = int(__fastcall*)(int);
PFN_LevelUp g_realLevelUp = nullptr;

int g_mode = -1;
alignas(16) std::uint8_t g_sheet[kSheetSize];        // the copy's sheet
alignas(16) std::uint8_t g_record[kSoraRecordSize];  // the record it's built from (+0x250); its commits write here
std::uint8_t g_raw[kSoraRecordSize];                 // the source as it was, before the checks: a change rebuilds
std::uintptr_t g_copy = 0;                           // the copy on g_sheet
std::uintptr_t g_refused = 0;                        // a copy we didn't swap (logged once)
bool g_off = false;                                  // switched off for good (a handle didn't come back right)
bool g_built = false;                                // g_sheet was built at least once (CopySheetSnapshot)
int g_logs = 0;

int Mode() {
    if (g_mode < 0) g_mode = EnvInt("KH2COOP_COPY_SHEET", 1);
    return g_mode;
}
std::uintptr_t Exe(std::uintptr_t rva) { return ExeBase() + rva; }
std::uintptr_t Sheet() { return reinterpret_cast<std::uintptr_t>(g_sheet); }
std::uint16_t U16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
void PutU16(std::uint8_t* p, std::uint16_t v) { std::memcpy(p, &v, 2); }

// The other PC's record made safe for the game's builder, which reads the slot counts unclamped and the item rows
// unchecked (L1978): level 1-99, HP 1 to max, MP to max, at most 8 slots, and each slot's id an item of that slot's kind
// in our game's table (a weapon: else ours; an armor, accessory or ability: else emptied, also a lone 'equipped' bit).
// Returns the fields changed.
int Check(std::uint8_t* r) {
    auto item = reinterpret_cast<PFN_Item>(Exe(game::kItemRecord));
    auto kind = [&item](std::uint16_t id) -> int {
        std::uintptr_t row = id ? item(id) : 0;
        std::uint8_t k = 0;
        return row && ReadAny(row + 2, &k) ? k : -1;
    };
    const auto* ours = reinterpret_cast<const std::uint8_t*>(Exe(game::kSoraRecord));
    int fixed = 0;
    auto fix = [&fixed](std::uint8_t& v, int lo, int hi) {
        if (v < lo || v > hi) v = static_cast<std::uint8_t>(v < lo ? lo : hi), ++fixed;
    };
    fix(r[0xF], 1, 99);
    fix(r[5], 1, 255);
    fix(r[4], 1, r[5]);
    fix(r[6], 0, r[7]);
    fix(r[0x10], 0, kSlots);
    fix(r[0x11], 0, kSlots);
    for (int at : {0, 2}) {
        std::uint16_t id = U16(r + at);
        int k = kind(id);
        if (id && (at == 0 ? k != kKeyblade : k < kKeyblade || k > kLastWeapon)) PutU16(r + at, U16(ours + at)), ++fixed;
    }
    for (int i = 0; i < 2 * kSlots; ++i) {
        std::uint16_t id = U16(r + 0x14 + 2 * i);
        if (id && kind(id) != (i < kSlots ? kArmor : kAccessory)) PutU16(r + 0x14 + 2 * i, 0), ++fixed;
    }
    for (int i = 0; i < kAbilities; ++i) {
        std::uint16_t v = U16(r + 0x54 + 2 * i);
        if (v && kind(v & 0x7FFF) != kAbility) PutU16(r + 0x54 + 2 * i, 0), ++fixed;
    }
    return fixed;
}

// The record the sheet is built from: the other player's, or ours (test mode 2). Null while none came.
const std::uint8_t* Source() {
    return Mode() == 2 ? reinterpret_cast<const std::uint8_t*>(Exe(game::kSoraRecord)) : PeerSoraRecord();
}

// Rebuilds the sheet from `raw` (checked first unless it's our own).
void Build(const std::uint8_t* raw) {
    std::memcpy(g_raw, raw, kSoraRecordSize);
    std::memcpy(g_record, raw, kSoraRecordSize);
    int fixed = Mode() == 2 ? 0 : Check(g_record);
    reinterpret_cast<PFN_SheetBuild>(Exe(kSheetBuild))(Sheet(), 0);
    if (g_logs++ < 40)
        Log("copy sheet: built from %s record: level %u, HP max %d, strength %u, magic %u, defense %u, AP %u%s",
            Mode() == 2 ? "our own (test)" : "the other player's", g_record[0xF], *reinterpret_cast<int*>(g_sheet + 4),
            U16(g_sheet + 0x188), U16(g_sheet + 0x18A), U16(g_sheet + 0x18C), U16(g_sheet + 0x18E),
            fixed ? " (some fields our game can't take were fixed)" : "");
    if (fixed && g_logs < 40) Log("copy sheet: %d fields of the other player's record fixed", fixed);
}

// The other player's HP and MP on the sheet, HP never below 1 (a KO starts only through the game's HP change, L1982).
void LiveHpMp() {
    int hp = 0, max = 0, mp = 0, maxMp = 0;
    if (Mode() == 2) {
        std::uintptr_t sora = 0, sheet = 0;
        if (!ReadAny(Exe(game::kListHead), &sora) || !sora || !ReadAny(sora + kActorSheet, &sheet) || !sheet ||
            !ReadAny(sheet, &hp) || !ReadAny(sheet + 4, &max) || !ReadAny(sheet + kSheetMp, &mp) ||
            !ReadAny(sheet + kSheetMaxMp, &maxMp))
            return;
    } else {
        PeerPose peer;
        if (!AvatarLinkPeerNow(peer)) return;
        hp = peer.hp;
        max = peer.maxHp;
        mp = peer.mp;
        maxMp = peer.maxMp;
    }
    if (max < 1) return;
    std::memcpy(g_sheet + 0, &(hp = hp < 1 ? 1 : hp > max ? max : hp), 4);
    std::memcpy(g_sheet + 4, &max, 4);
    if (maxMp < 1) return;
    std::memcpy(g_sheet + kSheetMp, &(mp = mp < 0 ? 0 : mp > maxMp ? maxMp : mp), 4);
    std::memcpy(g_sheet + kSheetMaxMp, &maxMp, 4);
}

// Gives `copy` our sheet while its +0x5C0 is still our Sora's (shared). True once it's on ours.
bool Swap(std::uintptr_t copy) {
    std::uintptr_t shared = 0, sora = 0, soraSheet = 0;
    if (!ReadAny(copy + kActorSheet, &shared)) return false;
    if (shared == Sheet()) return g_copy = copy, true;  // already (the menu hid the copy for a moment)
    const std::uint8_t* raw = Source();
    if (!raw) return false;  // no record yet: the shared sheet until one comes
    std::int32_t users = 0;
    bool ok = ReadAny(Exe(game::kListHead), &sora) && sora && ReadAny(sora + kActorSheet, &soraSheet) && shared &&
              shared == soraSheet && ReadAny(shared + kSheetUsers, &users) && users >= 2;
    if (!ok) {
        if (g_refused != copy && g_logs++ < 40)
            Log("copy sheet: the copy's sheet %p isn't our Sora's (%p, users %d): left as it is",
                reinterpret_cast<void*>(shared), reinterpret_cast<void*>(soraSheet), users);
        g_refused = copy;
        return false;
    }
    auto encode = reinterpret_cast<PFN_Encode>(Exe(game::kEncodeHandle));
    std::uint32_t record = encode(reinterpret_cast<std::uintptr_t>(g_record));
    if (DecodeHandle(record) != reinterpret_cast<std::uintptr_t>(g_record)) {  // a full handle table (L1986)
        g_off = true;
        Log("copy sheet: OFF: our record's handle %#x doesn't lead back to it; the copy keeps the shared sheet", record);
        return false;
    }
    reinterpret_cast<PFN_Sheet>(Exe(kSheetInit))(Sheet());
    *reinterpret_cast<std::int32_t*>(g_sheet + kSheetUsers) = kPinnedUsers;  // before anything that can fault
    *reinterpret_cast<std::int32_t*>(g_sheet + kSheetChar) = 1;
    *reinterpret_cast<std::uint32_t*>(g_sheet + kSheetRecord) = record;
    Build(raw);
    *reinterpret_cast<std::uint32_t*>(g_sheet + kSheetOwner) = encode(copy);
    LiveHpMp();
    *reinterpret_cast<std::uintptr_t*>(copy + kActorSheet) = Sheet();
    reinterpret_cast<PFN_Sheet>(Exe(kSheetRelease))(shared);  // the copy's user of ours; its commit is the game's own
    *reinterpret_cast<std::uint32_t*>(shared + kSheetOwner) = encode(sora);  // the newest actor built named itself
    g_copy = copy;
    g_built = true;
    Log("copy sheet: the copy %p has its own sheet %p (ours %p, now %d user)", reinterpret_cast<void*>(copy),
        reinterpret_cast<void*>(Sheet()), reinterpret_cast<void*>(shared), users - 1);
    return true;
}

void Step() {
    std::uintptr_t copy = PuppetCloneActor();
    if (!copy) {  // none here, or the menu hides it: a gone copy's teardown released our sheet (pinned: nothing pushed)
        g_copy = g_refused = 0;  // actor memory comes back at the same address in the next room
        return;
    }
    if (copy != g_copy) {
        g_copy = 0;
        if (copy == g_refused || !Swap(copy)) return;
    }
    std::uintptr_t now = 0;
    if (!ReadAny(copy + kActorSheet, &now) || now != Sheet()) {  // something gave it another sheet: let go
        if (g_logs++ < 40) Log("copy sheet: the copy's sheet became %p: ours let go", reinterpret_cast<void*>(now));
        g_refused = copy;
        g_copy = 0;
        return;
    }
    const std::uint8_t* raw = Source();
    if (raw && std::memcmp(raw, g_raw, kSoraRecordSize) != 0) Build(raw);
    LiveHpMp();
}

// Our Sora's level-up rebuilds the player pointer's sheet (L1979), which is the copy while aggro.cpp runs an enemy's
// script near it.
// shortcut: our Sora goes back as the list head, not a Drive Form's body, so in a form the level-up's effect plays on
// the hidden base actor; take aggro.cpp's swapped-out actor if a form level-up there is ever seen.
int __fastcall HookLevelUp(int exp) {
    auto* player = reinterpret_cast<std::uintptr_t*>(Exe(game::kPlayerPtr));
    std::uintptr_t was = *player, sora = 0, sheet = 0;
    bool swap = was && ReadAny(was + kActorSheet, &sheet) && sheet == Sheet() && ReadAny(Exe(game::kListHead), &sora) &&
                sora && sora != was;
    if (swap) *player = sora;
    static int logs = 0;
    if (swap && logs++ < 5) Log("copy sheet: EXP %d while the player pointer held the copy: our Sora's sheet takes it", exp);
    // The EXP still needed for the next level: the kill grant (exe+0x410AF0) draws it in the HUD's 7 digits. Returned
    // as nothing, the swap case left a pointer's low half there: an eighth digit, the host's crash at exe+0x192E12
    // (TODO 1.281).
    int next = g_realLevelUp(exp);
    if (swap && *player == sora) *player = was;
    return next;
}

}  // namespace

void CopySheetInit() {
    if (Mode() == 0) return;
    HookFunction(kLevelUp, kLevelUpBytes, sizeof(kLevelUpBytes), reinterpret_cast<void*>(HookLevelUp),
                 reinterpret_cast<void**>(&g_realLevelUp), "EXP and level-up (our Sora's sheet, not the copy's)");
    Log("copy sheet: %s", Mode() == 2 ? "TEST: the copy's own sheet from our own record"
                                      : "the copy's own sheet from the other player's record");
}

bool CopySheetSnapshot(std::uint8_t* out, std::size_t size) {
    if (Mode() == 0 || g_off || !g_built || size < kSheetSize) return false;  // g_off: a build may have faulted
    std::uint32_t record = 0;
    std::memcpy(&record, g_sheet + kSheetRecord, 4);
    if (DecodeHandle(record) != reinterpret_cast<std::uintptr_t>(g_record)) return false;  // the level's read
    std::memcpy(out, g_sheet, kSheetSize);
    return true;
}

void CopySheetFrame() {
    if (Mode() == 0 || g_off || !g_realLevelUp) return;
    __try {
        Step();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_off = true;
        Log("copy sheet: OFF: exception (the copy keeps the sheet it has)");
    }
}

}  // namespace kh2coop
