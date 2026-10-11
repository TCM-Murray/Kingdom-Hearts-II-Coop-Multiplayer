// The host's Sora wears the gold crown, the friend's none (TODO 3.13, user picks session 26: the friend's
// own crown stays hidden even if their save has Proofs; where no crown can be shown, both look the same).
//
// How the game does it (VERIFIED_OFFSETS rows 'Sora's crown', 'The crown is a texture animation', 'Crown
// geometry'): Sora's model has a small crown mesh (group 3, part 1, bone 140) whose texture is a texture
// animation with four patterns: 0x9A copper, 0x9B silver, 0x9C gold, 0xA4 none (blank). The game's pick
// exe+0x3A9E20(actor) counts the Proofs the player owns and sets the pattern with exe+0x3CD8A0(actor,
// pattern), on that one actor. It runs when an actor is set up (every room load), never for cutscene actors.
//
// Field: our hook on the pick chooses for our own Sora and the other player's Sora copy (the host's Sora
// gold, the friend's none, in both games); other actors keep the game's pick. A newly seen Sora or copy also
// gets its pattern once from CrownFrame, since the copy may appear after the game's pick ran.
// Cutscenes: the scene's body Sora (ACTOR_SORA, or P_EX100 by name) has the same crown: the scene's own Sora
// (the middle one, the one the close-ups show) wears it in both games, the twin beside it none. The close-up head
// models (H_EX500, ACTOR_SORA_H...) and the costume / Twilight Town models have no crown (TODO 3.13c/d).
// A close-up hides the body with SeqPart (parts shown: everything but part 0), which left the crown
// floating in the air (user, session 26); our SeqPart hook drops part 1 from any list without part 0.

#include "crown.hpp"

#include <windows.h>

#include <cstdint>

#include "common.hpp"
#include "cutscene_twin.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kPick = 0x3A9E20;  // (actor): Proofs owned -> pattern, then the setter
constexpr std::uint8_t kPickBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
                                       0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kSetter = 0x3CD8A0;  // (actor, pattern): texture animation pattern on that actor
constexpr std::uintptr_t kSeqPart = 0x2D1310;  // cutscene command 0x0D (command, task)
constexpr std::uint8_t kSeqPartBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
                                          0x8B, 0xF9, 0x0F, 0xBF, 0x49, 0x04};
constexpr std::uintptr_t kFindSceneActor = 0x2CB300;  // (actor id) -> int[10] {object, id, handle, ...} or 0
constexpr std::uintptr_t kSceneActors = 0xB653E0;     // -> 0x60 rows of 40 bytes {object, id, handle, ...}
constexpr std::uintptr_t kDecodeHandle = 0x4AD270;    // (u32 handle) -> object or 0
constexpr int kSceneRows = 0x60;
constexpr std::uintptr_t kListHead = 0x2A171C8;  // u64 -> the player's actor in the field
constexpr std::uintptr_t kInField = 0x9BA8D0;    // u8: 0 while a room loads
constexpr int kGold = 0x9C, kNone = 0xA4;
constexpr int kActorSora = 571, kFieldSora = 84;  // the objects whose model has the crown (body, not close-up)

using PFN_Pick = void(__fastcall*)(std::uintptr_t);
using PFN_Setter = void(__fastcall*)(std::uintptr_t, int);
using PFN_Command = void(__fastcall*)(std::uintptr_t, std::uintptr_t);
using PFN_FindSceneActor = int*(__fastcall*)(int);
using PFN_DecodeHandle = std::uintptr_t(__fastcall*)(std::uint32_t);

PFN_Pick g_realPick = nullptr;
PFN_Command g_realSeqPart = nullptr;
bool g_on = false, g_host = false;
// The actors we last gave a pattern from CrownFrame, and their model work (actor+0x7B0, holds the crown's
// texture animation): a new one (Drive Form, model reload) starts over with the model's first pattern.
std::uintptr_t g_player = 0, g_playerWork = 0, g_copy = 0, g_copyWork = 0;
std::uint32_t g_sceneDone[32] = {};       // handles of scene Soras given a pattern (a new scene, new handles)
int g_sceneNext = 0;
std::uint32_t g_logs = 0, g_partFixes = 0;

int OurPattern() { return g_host ? kGold : kNone; }
int TheirPattern() { return g_host ? kNone : kGold; }

void Set(std::uintptr_t actor, int pattern) {
    reinterpret_cast<PFN_Setter>(ExeBase() + kSetter)(actor, pattern);
}

// The pattern for a field actor, or -1 = not one of the two Soras (the game's own pick).
int FieldPattern(std::uintptr_t actor) {
    if (actor && actor == PuppetCloneActor()) return TheirPattern();
    if (actor && actor == *reinterpret_cast<const std::uintptr_t*>(ExeBase() + kListHead) && PuppetIsFieldPlayer(actor))
        return OurPattern();
    return -1;
}

void __fastcall HookPick(std::uintptr_t actor) {
    int pattern = -1;
    __try {
        pattern = FieldPattern(actor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (pattern < 0) return g_realPick(actor);
    Set(actor, pattern);
}

bool CrownModel(int object) { return object == kActorSora || object == kFieldSora; }

// A close-up hides the body Sora with a SeqPart list of the parts to show that lacks part 0; the crown
// (part 1) would stay and float: drop it from such a list. Edited in place (the script stays loaded for the
// scene; our twin copy or the room file's own), so the game's handler reads the fixed list.
void FixParts(std::uintptr_t cmd) {
    std::uint16_t size = *reinterpret_cast<const std::uint16_t*>(cmd);
    int id = *reinterpret_cast<const std::int16_t*>(cmd + 8);
    int* row = reinterpret_cast<PFN_FindSceneActor>(ExeBase() + kFindSceneActor)(id);
    if (!row || !CrownModel(row[0])) return;
    auto* parts = reinterpret_cast<std::int16_t*>(cmd + 10);
    int n = 0;
    while (10 + 2 * (n + 1) <= size && parts[n] != -1) ++n;
    bool body = false, crown = false;
    for (int i = 0; i < n; ++i) {
        body = body || parts[i] == 0;
        crown = crown || parts[i] == 1;
    }
    if (body || !crown) return;
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (parts[i] != 1) parts[k++] = parts[i];
    for (; k < n; ++k) parts[k] = -1;
    if (g_partFixes++ < 20)
        Log("crown: close-up at scene frame %d hides Sora's body (actor id %d): crown hidden with it",
            *reinterpret_cast<const std::int16_t*>(cmd + 4), id);
}

void __fastcall HookSeqPart(std::uintptr_t cmd, std::uintptr_t task) {
    __try {
        FixParts(cmd);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_realSeqPart(cmd, task);  // task (rdx) must reach the handler: it waits for its frame through it
}

void SceneFrame() {
    auto rows = *reinterpret_cast<const std::uint8_t* const*>(ExeBase() + kSceneActors);
    if (!rows) return;
    bool any = false;
    for (int i = 0; i < kSceneRows; ++i) {
        const auto* row = reinterpret_cast<const std::int32_t*>(rows + 40 * i);
        if (!CrownModel(row[0]) || !row[2]) continue;
        any = true;
        auto handle = static_cast<std::uint32_t>(row[2]);
        bool done = false;
        for (std::uint32_t h : g_sceneDone) done = done || h == handle;
        if (done) continue;
        std::uintptr_t actor = reinterpret_cast<PFN_DecodeHandle>(ExeBase() + kDecodeHandle)(handle);
        if (!actor) continue;
        // The scene's own Sora wears it in both games, the twin never: the twin stands at the same side
        // spot in both games, so a crown by player (host gold) swapped places between the two screens and
        // didn't tell anyone who's who (user test session 26). Close-ups show the scene's own Sora too.
        bool twin = CutsceneTwinIsTwin(row[1]);
        int pattern = twin ? kNone : kGold;
        Set(actor, pattern);
        g_sceneDone[g_sceneNext] = handle;
        g_sceneNext = (g_sceneNext + 1) % 32;
        if (g_logs++ < 100)
            Log("crown: cutscene %s (actor id %d, object %d): %s", twin ? "twin" : "Sora", row[1], row[0],
                pattern == kGold ? "gold crown" : "no crown");
    }
    if (!any) {  // no scene Sora now: the next scene's may reuse a handle
        for (std::uint32_t& h : g_sceneDone) h = 0;
    }
}

std::uintptr_t Work(std::uintptr_t actor) { return *reinterpret_cast<const std::uintptr_t*>(actor + 0x7B0); }

void FieldFrame() {
    std::uintptr_t exe = ExeBase();
    if (*reinterpret_cast<const volatile std::uint8_t*>(exe + kInField) == 0) {
        g_player = g_copy = 0;  // the actors go away with the room
        return;
    }
    std::uintptr_t head = *reinterpret_cast<const std::uintptr_t*>(exe + kListHead);
    if (head && (head != g_player || Work(head) != g_playerWork) && PuppetIsFieldPlayer(head) && Work(head)) {
        g_player = head;
        g_playerWork = Work(head);
        Set(head, OurPattern());
        if (g_logs++ < 100) Log("crown: our Sora: %s", OurPattern() == kGold ? "gold crown" : "no crown");
    }
    std::uintptr_t copy = PuppetCloneActor();
    if (copy && (copy != g_copy || Work(copy) != g_copyWork) && Work(copy)) {
        g_copy = copy;
        g_copyWork = Work(copy);
        Set(copy, TheirPattern());
        if (g_logs++ < 100) Log("crown: the other player's Sora copy: %s", TheirPattern() == kGold ? "gold crown" : "no crown");
    }
}

}  // namespace

void CrownInit(bool host) {
    g_host = host;
    if (EnvInt("KH2COOP_CROWN", 1) != 1) {
        Log("crown: off (CROWN=0): each Sora shows the crown of its own save's Proofs");
        return;
    }
    g_on = HookFunction(kPick, kPickBytes, sizeof(kPickBytes), reinterpret_cast<void*>(HookPick),
                        reinterpret_cast<void**>(&g_realPick), "crown pick") &&
           HookFunction(kSeqPart, kSeqPartBytes, sizeof(kSeqPartBytes), reinterpret_cast<void*>(HookSeqPart),
                        reinterpret_cast<void**>(&g_realSeqPart), "cutscene SeqPart");
    Log("crown: %s", g_on ? (host ? "on: our Sora (host) gold, the friend's none"
                                  : "on: the host's Sora gold, ours (friend) none")
                          : "OFF: a hook failed");
}

void CrownFrame() {
    if (!g_on) return;
    __try {
        FieldFrame();
        SceneFrame();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace kh2coop
