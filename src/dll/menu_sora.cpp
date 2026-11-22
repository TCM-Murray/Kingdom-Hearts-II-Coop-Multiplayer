// The other player's Sora on the pause menu's top screen (TODO 3.19; DECISIONS 'The pause menu's top screen shows the
// other player's Sora second in line'): its status box second in the row, 'Sora (P2)' in the host's game and 'Sora
// (P1)' in the friend's, and its model second in line, once the other player has joined (then with the last stats it
// sent). Display only: it's in none of the game's lists (the menu's party list holds 4 and finds everything by
// character id, L2007-L2010), so no sub-screen can pick it, and the top screen's cursor picks no box (L2014).
//   - The box (F799-F801): a second set of the game's box sprites, made by its allocator (exe+0x368670) with the two
//     globals pointed at ours for the call. Each top-screen build (exe+0x367B50 with 0, 0, 4) is followed by the same
//     build on our set from a one-member list of ours: Sora's entry with our name and a copy of the copy's sheet
//     (copy_sheet.cpp: the other player's record, so their level), with the last HP and MP the other player sent. As
//     the game's own boxes, it shows the values of that build (your pick). Box i sits at step x (count - i): ours takes
//     Sora's place and Sora's moves one step left; Donald, Goofy and the guest stay. Ours gets each call the game
//     makes on its own set right after it: the close animation when a command is picked (exe+0x368600; the boxes
//     leave the sprite list with their root), the idle loop again (exe+0x368AE0), the stop (exe+0x3688D0) and the
//     free (exe+0x368470); and a stop when
//     the builder builds another screen's boxes. A set still in the sprite list is never built again (exe+0x368D60 on
//     ours): the engine would list its sprites twice.
//   - The model (F802, L2018): the top screen's pass (exe+0x306400) makes at most 3, one per list entry. Once all of
//     them are live, we make the next slot with Sora's menu object, the way the pass makes one. While the pass runs,
//     slot creation (exe+0x2FEB20) gets the x of a row one model longer, ours second. The game's release
//     (exe+0x2FEF00) ends every slot when the top screen is left.
// Liveness: our box set is ours from our allocation to the free that follows the game's own. The menu's model object
// and party list are read again at each call, never kept. Our model's slot is made only while the game's own are all
// live (the draw walks as many slots as were made, L2018), and once per visit (the slots-made count).
// shortcut: the model is this world's Sora (the game's PLAYER menu object), so a partner playing as Roxas shows as Sora
// here (your pick 2026-11-22: fine for now); use the copy's body object once one is known to have the menu motion.
// KH2COOP_MENU_SORA=0 leaves it off; on with the Sora copy.

#include "menu_sora.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "copy_sheet.hpp"
#include "puppet.hpp"
#include "target_row.hpp"

namespace kh2coop {
namespace {

// The status boxes (F799-F801, L2014).
constexpr std::uintptr_t kBoxBuild = 0x367B50;  // (mode, kind, which): one box per member; the top screen's (0, 0, 4)
constexpr std::uint8_t kBoxBuildBytes[] = {0x44, 0x89, 0x44, 0x24, 0x18, 0x53, 0x55, 0x56,
                                           0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41};
constexpr std::uintptr_t kBoxStop = 0x3688D0;  // (): stops the root and the 4 boxes of the two globals
constexpr std::uint8_t kBoxStopBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                          0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kBoxFree = 0x368470;  // (): frees them, zeroes both globals
constexpr std::uint8_t kBoxFreeBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0xED,
                                          0x88, 0x88, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x16};
constexpr std::uintptr_t kBoxClose = 0x368600;  // (): the root's close animation (a command picked)
constexpr std::uint8_t kBoxCloseBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x5D,
                                           0x87, 0x88, 0x00, 0xE8, 0x10, 0xE8, 0xDF, 0xFF};
constexpr std::uintptr_t kBoxIdle = 0x368AE0;  // (): the root back to its idle loop (the top screen, on input)
constexpr std::uint8_t kBoxIdleBytes[] = {0x48, 0x0F, 0xBE, 0x05, 0x30, 0xDD, 0x3D, 0x00,
                                          0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0x09};
constexpr std::uintptr_t kBoxListed = 0x368D60;  // () -> u8: the root or a sprite of a box is still in the sprite list
constexpr std::uintptr_t kBoxAlloc = 0x368670;   // (): a root and 4 boxes into the two globals
constexpr std::uintptr_t kBoxRoot = 0xBF0D68;    // ptr: the boxes' root sprite
constexpr std::uintptr_t kBoxes = 0xBF0D70;      // ptr: 4 box blocks of kBoxBlock
constexpr std::uintptr_t kBoxBlock = 0x18D0;
constexpr std::uintptr_t kBoxState = 0x746818;   // u8[3]: the last build's mode, kind, which (0xFF after alloc, free)
constexpr std::uintptr_t kSetPos = 0x16B2A0;     // (sprite, x, y): its place in its parent (+0x1C8)
constexpr std::uintptr_t kLayoutStep = 0x192E00; // (root + 0x20) -> the step between two boxes
constexpr std::uintptr_t kRootLayout = 0x20;
constexpr std::uintptr_t kPartyList = 0xBEEC28;  // ptr: the menu's party list (F799)
constexpr std::size_t kListSize = 0x90, kEntrySize = 0x20;
constexpr std::uintptr_t kListRow = 0x88, kEntry = 8, kEntryName = 8, kEntrySheet = 0x18;
constexpr std::uintptr_t kSheetMp = 0x180, kSheetMaxMp = 0x184;  // F38
constexpr std::size_t kSheetSize = 0x278;

// The models (F802, L2018).
constexpr std::uintptr_t kModelPass = 0x306400;  // (): the top screen's models, at most 3
constexpr std::uint8_t kModelPassBytes[] = {0x48, 0x8B, 0xC4, 0x53, 0x55, 0x48, 0x83, 0xEC,
                                            0x78, 0x48, 0x8B, 0x0D, 0xE8, 0x88, 0x8E, 0x00};
constexpr std::uintptr_t kSlotCreate = 0x2FEB20;  // (obj, slot, object, motion, 2, x, y): the model object's vtable +0x28
constexpr std::uint8_t kSlotCreateBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                             0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48};
constexpr std::uintptr_t kMenuObj = 0xBEE660;        // ptr: the menu's model object (Tz::MenuObj)
constexpr std::uintptr_t kModelsPending = 0xBEED64;  // u8: the pass left a member whose files aren't loaded yet
constexpr std::uintptr_t kListObject = 0x2FC5B0;     // (list, i) -> the seat's menu object (Sora: 0x20000236)
constexpr std::uintptr_t kListReady = 0x2FC920;      // (list, i) -> its sheet and model files are there
constexpr std::uintptr_t kHighRow = 0x364920;        // () -> the models stand higher (y 0x3D, else 0x60)
constexpr std::uintptr_t kSetAngle = 0x2FF890, kSetAngle2 = 0x2FF8B0, kSetDepth = 0x2FFA50;  // (obj, slot, f32)
constexpr std::uintptr_t kSetOrder = 0x2FF760;       // (obj, slot, u8): draw order (Sora 0x11, the others 0x10)
constexpr std::uintptr_t kSetColor = 0x2FF530;       // (obj, slot, r, g, b, a or -1)
constexpr std::uintptr_t kSetSprite = 0x2FF970;      // (obj, slot, sprite): the layout sprite it follows
constexpr std::uintptr_t kModelSprite = 0xBEECF8;    // ptr: that sprite
constexpr std::uintptr_t kModelDepth = 0x5B22E8;     // f32: the depth the pass gives each model
constexpr std::uintptr_t kVtableCreate = 0x28;
constexpr std::uintptr_t kSlotState = 0x840, kSlotStateSize = 0x1C0, kSlotIndex = 0xF44, kSlotsMade = 0xF48;
constexpr std::uintptr_t kStateLoader = 0x88, kStateLive = 0x1B8;
constexpr std::uint32_t kMenuBit = 0x20000000, kMenuIdle = 0x8B;  // the menu object bit; MENU_IDLE (OpenKH)

using PFN_Void = void(__fastcall*)();
using PFN_BoxBuild = void(__fastcall*)(char, char, int);
using PFN_SetPos = void(__fastcall*)(std::uintptr_t, int, int);
using PFN_Step = int(__fastcall*)(std::uintptr_t);
using PFN_SlotCreate = void(__fastcall*)(std::uintptr_t, int, std::uint64_t, std::uint64_t, int, int, int);
using PFN_ListObject = std::uint32_t(__fastcall*)(std::uintptr_t, int);
using PFN_ListReady = std::uint8_t(__fastcall*)(std::uintptr_t, int);
using PFN_Flag = std::uint8_t(__fastcall*)();
using PFN_SetF = void(__fastcall*)(std::uintptr_t, int, float);
using PFN_SetU8 = void(__fastcall*)(std::uintptr_t, int, std::uint8_t);
using PFN_SetColor = void(__fastcall*)(std::uintptr_t, int, std::uint8_t, std::uint8_t, std::uint8_t, int);
using PFN_SetPtr = void(__fastcall*)(std::uintptr_t, int, std::uintptr_t);

PFN_BoxBuild g_realBuild = nullptr;
PFN_Void g_realStop = nullptr, g_realFree = nullptr, g_realClose = nullptr, g_realIdle = nullptr, g_realPass = nullptr;
PFN_SlotCreate g_realCreate = nullptr;
bool g_boxOn = false, g_modelOn = false;

std::uintptr_t g_root = 0, g_boxes = 0;          // our box set (the game's allocator); 0 when none
alignas(16) std::uint8_t g_list[kListSize];       // our one-member list
alignas(16) std::uint8_t g_sheet[kSheetSize];     // the other Sora's sheet as of the last build
// The game's globals while ours are in them (one call), to put back, also after a fault.
bool g_swapped = false;
std::uintptr_t g_gameRoot = 0, g_gameBoxes = 0, g_gameList = 0;
std::uint8_t g_gameState[3] = {};
// The model pass: whether this call leaves room for ours, and the model object whose slot 0 was made so.
bool g_inPass = false, g_room = false;
std::uintptr_t g_roomFor = 0;
int g_logs = 0;
const char* g_lastSkip = nullptr;

std::uintptr_t Exe(std::uintptr_t rva) { return ExeBase() + rva; }
template <typename T>
T& At(std::uintptr_t rva) {
    return *reinterpret_cast<T*>(Exe(rva));
}

void Skip(const char* why) {  // why no box this time, logged when it changes
    if (why != g_lastSkip && g_logs++ < 40) Log("menu sora: no box of the other Sora: %s", why);
    g_lastSkip = why;
}

// Ours in the game's two box globals (and, with `list`, its party list) for one call; SwapOut puts theirs back.
void SwapIn(std::uintptr_t list) {
    g_gameRoot = At<std::uintptr_t>(kBoxRoot);
    g_gameBoxes = At<std::uintptr_t>(kBoxes);
    g_gameList = At<std::uintptr_t>(kPartyList);
    std::memcpy(g_gameState, &At<std::uint8_t>(kBoxState), sizeof(g_gameState));
    g_swapped = true;
    At<std::uintptr_t>(kBoxRoot) = g_root;
    At<std::uintptr_t>(kBoxes) = g_boxes;
    if (list) At<std::uintptr_t>(kPartyList) = list;
}
void SwapOut() {
    if (!g_swapped) return;
    g_root = At<std::uintptr_t>(kBoxRoot);  // the allocator and the free change them
    g_boxes = At<std::uintptr_t>(kBoxes);
    At<std::uintptr_t>(kBoxRoot) = g_gameRoot;
    At<std::uintptr_t>(kBoxes) = g_gameBoxes;
    At<std::uintptr_t>(kPartyList) = g_gameList;
    std::memcpy(&At<std::uint8_t>(kBoxState), g_gameState, sizeof(g_gameState));
    g_swapped = false;
}

void OnOurs(PFN_Void fn) {  // the game's call `fn` on our set
    if (!g_boxes) return;
    SwapIn(0);
    fn();
    SwapOut();
}
void StopOurs() { OnOurs(g_realStop); }

bool OursListed() {
    if (!g_boxes) return false;
    SwapIn(0);
    const bool listed = reinterpret_cast<PFN_Flag>(Exe(kBoxListed))() != 0;
    SwapOut();
    return listed;
}

void FreeOurs() {
    if (!g_boxes && !g_root) return;
    SwapIn(0);
    g_realFree();
    SwapOut();
    if (g_logs++ < 40) Log("menu sora: our box set freed with the game's");
}

int Clamp(int v, int max) { return v < 0 ? 0 : v > max ? max : v; }

// After the game's top-screen build: the same build on our set, from our one-member list.
void BuildOurs() {
    PeerPose peer;
    const std::uint8_t* name = TargetRowOtherSoraName();
    const std::uintptr_t list = At<std::uintptr_t>(kPartyList), gameRoot = At<std::uintptr_t>(kBoxRoot),
                         gameBoxes = At<std::uintptr_t>(kBoxes);
    std::int32_t count = 0;
    const char* why = !AvatarLinkPeerLast(peer)                              ? "the other player hasn't joined"
                      : !name                                                ? "no name text"
                      : !list || !ReadAny(list, &count) || count < 1 || count > 4 ? "no party list"
                      : !gameRoot || !gameBoxes                              ? "no box set of the game's"
                      : !CopySheetSnapshot(g_sheet, sizeof(g_sheet))         ? "no sheet of the copy's yet"
                      : OursListed()                                         ? "ours still in the sprite list"
                                                                             : nullptr;
    if (!why) {
        if (peer.maxHp > 0) {
            std::int32_t hp = Clamp(peer.hp, peer.maxHp);
            std::memcpy(g_sheet + 0, &hp, 4);
            std::memcpy(g_sheet + 4, &peer.maxHp, 4);
        }
        if (peer.maxMp > 0) {
            std::int32_t mp = Clamp(peer.mp, peer.maxMp);
            std::memcpy(g_sheet + kSheetMp, &mp, 4);
            std::memcpy(g_sheet + kSheetMaxMp, &peer.maxMp, 4);
        }
        std::int32_t maxHp = 0, maxMp = 0;
        std::memcpy(&maxHp, g_sheet + 4, 4);
        std::memcpy(&maxMp, g_sheet + kSheetMaxMp, 4);
        if (maxHp < 1 || maxMp < 1) why = "no max HP or MP (the box divides by them)";
    }
    if (why) {
        Skip(why);
        StopOurs();
        return;
    }
    // Sora's entry (seat, id 1, its objects) with our name and sheet; the row pointer as the game's.
    std::memset(g_list, 0, sizeof(g_list));
    *reinterpret_cast<std::int32_t*>(g_list) = 1;
    std::memcpy(g_list + kEntry, reinterpret_cast<const void*>(list + kEntry), kEntrySize);
    std::memcpy(g_list + kListRow, reinterpret_cast<const void*>(list + kListRow), 8);
    *reinterpret_cast<const std::uint8_t**>(g_list + kEntry + kEntryName) = name;
    *reinterpret_cast<std::uintptr_t*>(g_list + kEntry + kEntrySheet) = reinterpret_cast<std::uintptr_t>(g_sheet);
    if (!g_boxes) {
        SwapIn(0);
        reinterpret_cast<PFN_Void>(Exe(kBoxAlloc))();
        SwapOut();
        if (!g_boxes || !g_root) {
            Skip("the game's allocator gave no box set");
            return;
        }
        if (g_logs++ < 40) Log("menu sora: our box set %p (root %p)", reinterpret_cast<void*>(g_boxes), reinterpret_cast<void*>(g_root));
    }
    SwapIn(reinterpret_cast<std::uintptr_t>(g_list));
    g_realBuild(0, 0, 4);
    SwapOut();
    const int step = reinterpret_cast<PFN_Step>(Exe(kLayoutStep))(gameRoot + kRootLayout);
    auto setPos = reinterpret_cast<PFN_SetPos>(Exe(kSetPos));
    // Five boxes: the row two thirds of a box to the right, its first box off the command list (your mock, 2026-11-22).
    const int shift = count == 4 ? -step * 2 / 3 : 0;
    setPos(gameBoxes, step * (count + 1) + shift, 0);  // Sora's box one step further
    for (int i = 1; shift && i < count; ++i) setPos(gameBoxes + i * kBoxBlock, step * (count - i) + shift, 0);
    setPos(g_boxes, step * count + shift, 0);  // ours in Sora's place
    g_lastSkip = nullptr;
    if (g_logs++ < 40) {
        std::int32_t v[4] = {};
        std::memcpy(&v[0], g_sheet, 8);
        std::memcpy(&v[2], g_sheet + kSheetMp, 8);
        Log("menu sora: box of the other Sora built, %d members + ours, step %d (HP %d/%d, MP %d/%d)", count, step,
            v[0], v[1], v[2], v[3]);
    }
}

void Fail(const char* where) {
    SwapOut();
    g_boxOn = g_modelOn = false;
    Log("menu sora: OFF: exception in %s (the game's globals put back)", where);
}

void __fastcall HookBuild(char mode, char kind, int which) {
    g_realBuild(mode, kind, which);
    if (!g_boxOn) return;
    __try {
        if (mode == 0 && kind == 0 && which == 4) {
            BuildOurs();
        } else {
            StopOurs();  // another screen's boxes (Items' use-on-member): ours don't belong there
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("the box build");
    }
}

void __fastcall HookStop() {
    g_realStop();
    __try {
        StopOurs();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("the box stop");
    }
}

void __fastcall HookClose() {
    g_realClose();
    __try {
        OnOurs(g_realClose);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("the box close");
    }
}

void __fastcall HookIdle() {
    g_realIdle();
    __try {
        OnOurs(g_realIdle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("the box idle");
    }
}

void __fastcall HookFree() {
    g_realFree();
    __try {
        FreeOurs();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("the box free");
    }
}

// The models.
int ModelCount(std::uintptr_t list) {  // the pass's own count: one per list entry, at most 3
    std::int32_t count = 0;
    if (!list || !ReadAny(list, &count) || count < 0) return 0;
    return count > 3 ? 3 : count;
}
int ModelX(int place, int models) { return 0x2F + 0x62 * place - (models * 0x62 - 0x62) / models; }  // the pass's x
// x in our row of n + 1 models (`n` = the pass's own), centred where the pass centres its row of n (your ask
// 2026-11-22: the pass's formula for n + 1 put the row too far right, the last model on the portraits).
int RowX(int place, int n) { return ModelX(0, n) + 0x62 * place - 0x31; }
std::uintptr_t SlotState(std::uintptr_t obj, int slot) {
    return obj + kSlotState + *reinterpret_cast<const std::uint8_t*>(obj + kSlotIndex + slot) * kSlotStateSize;
}
bool SlotLive(std::uintptr_t obj, int slot) {
    return *reinterpret_cast<const std::uint8_t*>(SlotState(obj, slot) + kStateLive) != 0;
}

// After the pass: our model in the next slot, made the way the pass makes one.
void MakeOurModel() {
    const std::uintptr_t obj = At<std::uintptr_t>(kMenuObj), list = At<std::uintptr_t>(kPartyList);
    if (!obj || obj != g_roomFor || !list || At<std::uint8_t>(kModelsPending)) return;
    const int n = ModelCount(list);
    std::int32_t made = 0;
    if (n < 1 || !ReadAny(obj + kSlotsMade, &made) || made != n) return;  // n + 1: ours is there
    for (int i = 0; i < n; ++i)
        if (!SlotLive(obj, i)) return;
    std::uintptr_t loader = 0;
    if (!ReadAny(SlotState(obj, n) + kStateLoader, &loader) || loader) return;  // made before and never released
    if (!reinterpret_cast<PFN_ListReady>(Exe(kListReady))(list, 0)) return;
    const std::uint32_t object = reinterpret_cast<PFN_ListObject>(Exe(kListObject))(list, 0) | kMenuBit;
    const int y = reinterpret_cast<PFN_Flag>(Exe(kHighRow))() ? 0x3D : 0x60;
    std::uintptr_t vtable = 0, create = 0;
    if (!ReadAny(obj, &vtable) || !vtable || !ReadAny(vtable + kVtableCreate, &create) || !create) return;
    reinterpret_cast<PFN_SlotCreate>(create)(obj, n, object, kMenuIdle, 2, RowX(1, n), y);
    reinterpret_cast<PFN_SetF>(Exe(kSetDepth))(obj, n, At<float>(kModelDepth));
    reinterpret_cast<PFN_SetF>(Exe(kSetAngle))(obj, n, 0.0f);
    reinterpret_cast<PFN_SetF>(Exe(kSetAngle2))(obj, n, 0.0f);
    reinterpret_cast<PFN_SetU8>(Exe(kSetOrder))(obj, n, 0x10);
    reinterpret_cast<PFN_SetColor>(Exe(kSetColor))(obj, n, 0x60, 0x60, 0x60, -1);
    reinterpret_cast<PFN_SetPtr>(Exe(kSetSprite))(obj, n, At<std::uintptr_t>(kModelSprite));
    const bool live = SlotLive(obj, n);
    if (!live) *reinterpret_cast<std::int32_t*>(obj + kSlotsMade) = n;  // the draw walks only live slots (L2018)
    if (g_logs++ < 40)
        Log("menu sora: model of the other Sora in slot %d (object %#x, x %d of %d models)%s", n, object,
            RowX(1, n), n + 1, live ? "" : ": NOT live, slot count put back");
}

// Before the game makes its slots: room for ours only when ours can be made (no gap in second place).
bool OurModelPossible(std::uintptr_t obj) {
    PeerPose peer;
    const std::uintptr_t list = At<std::uintptr_t>(kPartyList);
    const int n = ModelCount(list);
    std::uintptr_t loader = 0;
    return g_modelOn && obj && n >= 1 && AvatarLinkPeerLast(peer) &&
           ReadAny(SlotState(obj, n) + kStateLoader, &loader) && !loader &&  // a stuck slot of ours (never live)
           reinterpret_cast<PFN_ListReady>(Exe(kListReady))(list, 0);
}

void __fastcall HookPass() {
    const std::uintptr_t obj = At<std::uintptr_t>(kMenuObj);
    if (obj && !SlotLive(obj, 0)) g_roomFor = 0;  // a new visit: slot 0 decides again
    g_room = false;
    __try {
        g_room = OurModelPossible(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("our model's check");
    }
    g_inPass = true;
    g_realPass();
    g_inPass = false;
    if (!g_modelOn || !g_room) return;
    __try {
        MakeOurModel();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Fail("our model");
    }
}

void __fastcall HookCreate(std::uintptr_t obj, int slot, std::uint64_t object, std::uint64_t motion, int a5, int x,
                           int y) {
    if (g_inPass && slot >= 0 && slot < 3) {
        if (slot == 0) g_roomFor = g_room ? obj : 0;
        if (g_roomFor == obj) x = RowX(slot == 0 ? 0 : slot + 1, ModelCount(At<std::uintptr_t>(kPartyList)));
    }
    g_realCreate(obj, slot, object, motion, a5, x, y);
}

}  // namespace

void MenuSoraInit() {
    if (EnvInt("KH2COOP_MENU_SORA", 1) == 0) {
        Log("menu sora: off (KH2COOP_MENU_SORA=0)");
        return;
    }
    // The free, the stop and the close before the build: never a set of ours the game's own calls wouldn't follow.
    g_boxOn = HookFunction(kBoxFree, kBoxFreeBytes, sizeof(kBoxFreeBytes), reinterpret_cast<void*>(HookFree),
                           reinterpret_cast<void**>(&g_realFree), "pause menu box free (the other Sora's box)") &&
              HookFunction(kBoxStop, kBoxStopBytes, sizeof(kBoxStopBytes), reinterpret_cast<void*>(HookStop),
                           reinterpret_cast<void**>(&g_realStop), "pause menu box stop (the other Sora's box)") &&
              HookFunction(kBoxClose, kBoxCloseBytes, sizeof(kBoxCloseBytes), reinterpret_cast<void*>(HookClose),
                           reinterpret_cast<void**>(&g_realClose), "pause menu box close (the other Sora's box)") &&
              HookFunction(kBoxIdle, kBoxIdleBytes, sizeof(kBoxIdleBytes), reinterpret_cast<void*>(HookIdle),
                           reinterpret_cast<void**>(&g_realIdle), "pause menu box idle (the other Sora's box)") &&
              HookFunction(kBoxBuild, kBoxBuildBytes, sizeof(kBoxBuildBytes), reinterpret_cast<void*>(HookBuild),
                           reinterpret_cast<void**>(&g_realBuild), "pause menu box build (the other Sora's box)");
    // The slot hook before the pass's: never our model without room made for it.
    g_modelOn = HookFunction(kSlotCreate, kSlotCreateBytes, sizeof(kSlotCreateBytes),
                             reinterpret_cast<void*>(HookCreate), reinterpret_cast<void**>(&g_realCreate),
                             "pause menu model slot (room for the other Sora)") &&
                HookFunction(kModelPass, kModelPassBytes, sizeof(kModelPassBytes), reinterpret_cast<void*>(HookPass),
                             reinterpret_cast<void**>(&g_realPass), "pause menu models (the other Sora's)");
    Log("menu sora: box %s, model %s", g_boxOn ? "on" : "OFF (a hook failed)", g_modelOn ? "on" : "OFF (a hook failed)");
}

}  // namespace kh2coop
