// Companions leave and come back the same way on both screens for Drive Forms and summons (TODO 1.98; DECISIONS C1,
// C2; you, 2026-10-21: "as close to the original game as possible"). Limits do the same in limit_sync.cpp.
//
// The game (VERIFIED_OFFSETS lead 'Drive Forms and summons send the companions away like a Limit'): the form change
// exe+0x40CE60 calls exe+0x400B20(form), which sends the party member of the form's place away with
// exe+0x400990(member, fade) (Valor: Goofy's place, Wisdom: Donald's, Limit Form: nobody, Master / Final / Anti:
// both); the summon start exe+0x3DE460 sends both with exe+0x400A30(fade). Each puts the member's party place
// (1 = Donald's, 2 = Goofy's, 3 = the world ally's) on the bring-back list exe+0x2AE9788 (count
// exe+0x2AE9790); the form's end (exe+0x40CC00 / exe+0x40CF20) and the summon's end (exe+0x3DE080) build everyone on
// it again with exe+0x400440. The other game ran none of it.
// Here:
//   - The places our own form or summon sent away (the two leave calls, told apart by their call sites) are "ours"
//     until that form's or summon's end brings them back; each game streams them ("KH2O").
//   - While the other player holds a place, our member in that place leaves with the same game call and fade
//     (checked every frame: a member built again meanwhile leaves too). The game itself then hides a form or summon
//     that needs it (exe+0x4005E0: no live member in the place = not offered), as it does in one game (C2).
//   - The bring-back exe+0x400440 builds only the places nobody holds any more: the other player's form, summon or
//     Limit (limit_sync.cpp, not its partner) still holding one keeps it on the list, and so does our own form or
//     summon unless this is its own end. When the other player lets a place go, we call the bring-back ourselves.
//
// Their state (TODO 1.98 part 2, 1.43; DECISIONS C3): the friend's companions copy the host's (world_sync.cpp), but
// only pose and HP above 0. The host's game alone runs them, so here:
//   - The host streams each companion's HP, MP, MP recharge and KO ("KH2S", status). The friend's copies take the MP
//     as it is; a KO the game's way (its HP change to 0: the dead bit, the get-up timer) and the get-up the game's
//     way too (an HP gain on a KO'd member revives it, exe+0x3C3C30); a plain HP write of 0 does nothing.
//   - The friend's copies don't think (exe+0x1B53B0): the brain picked heals with MP that never changed in that
//     game (you, 2026-10-21: the friend's Donald tried to heal Sora without the MP for it).
//   - An HP or MP gain on a friend's copy (a Cure or an item from the friend) goes to the host's game, which applies
//     it to its own companion (a KO'd one gets up, as in one game); the copy then follows the host's status. The
//     friend's game's own gains on a copy (exe+0x3C0C30's +1s) are dropped: one got a KO'd copy up for a frame.
//
// Party swaps (TODO 1.51; DECISIONS C3: one party for both players, either player's swap counts): the Party command,
// the main menu's party screen and the game's own automatic swap all end in the world's party entry
// (exe+0x3E2E20(-1): {player, active 1, active 2, bench} as party places). Each game puts its benched place in the
// "KH2O" stream; a game whose bench differs from the party's runs the game's own field swap exe+0x3E8F80 on the
// member that should go (the fade, the files, the build, the entry: exe+0x3E90E0). The host's party is the truth; the
// friend counts the swaps its own player made, and the host takes the friend's bench as the party when that count
// moves and echoes the count, so two swaps at once can't answer each other for ever.
// COMPANION_SYNC=0 in kh2coop.ini turns it off (both PCs).

#include "companion_sync.hpp"

#include <windows.h>

#include <intrin.h>

#include <cstdint>
#include <cstring>

#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "game.hpp"
#include "limit_sync.hpp"
#include "party_hud.hpp"
#include "puppet.hpp"
#include "summon_sync.hpp"
#include "world_census.hpp"
#include "world_sync.hpp"

namespace kh2coop {
namespace {

constexpr std::uint32_t kMagic = 0x4F32484B;  // "KH2O"
constexpr std::uint16_t kVersion = 2;  // 2: the summon's actors and their pose (summon_sync.cpp)
constexpr std::uintptr_t kFriendSlots = 0x2A239B0;    // u64[2] party members
constexpr std::uintptr_t kForm = 0x9ACDD4;            // u8 current Drive Form (0 = none)
constexpr std::uintptr_t kSummonState = 0x2A24EDC;    // u32, 2 = a summon is out
constexpr std::uintptr_t kList = 0x2AE9788;           // u32[] party places to build again
constexpr std::uintptr_t kListCount = 0x2AE9790;      // i32
constexpr int kListMax = 2;                           // the game's own list (two places; the next field follows)
constexpr std::uintptr_t kPartyTable = 0x2A252F0;    // exe+0x3E2E40(): u16 object id per place at +0x10
constexpr std::uintptr_t kDriveFade = 0x623BC0;       // f32: the fade exe+0x400B20 passes
constexpr std::uintptr_t kObjRow = 0x918, kRemoveFlags = 0x9B8;  // +0x9B8 bit 4: being removed
constexpr std::uintptr_t kCharType = 0x3B5B20;        // (actor) -> character type (2 Donald's kind, 3 Goofy's)
constexpr std::uintptr_t kPlaceOf = 0x3E31C0;         // (type) -> party place (0 Sora, 1, 2, 3 other)
constexpr std::uintptr_t kLive = 0x3BA720;            // (actor) -> in the actor list and not going
constexpr std::uintptr_t kLeave = 0x400990;           // (member, float fade)
constexpr std::uint8_t kLeaveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30};
constexpr std::uintptr_t kLeaveBoth = 0x400A30;       // (float fade): both members (summon, Mickey's Game Over)
constexpr std::uint8_t kLeaveBothBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56};
constexpr std::uintptr_t kBringBack = 0x400440;       // (): build everyone on the list again
constexpr std::uint8_t kBringBackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
// Return addresses of the game's calls (after the CALL).
constexpr std::uint32_t kFromDriveLeave = 0x400B92;   // exe+0x400B20 -> exe+0x400990
constexpr std::uint32_t kFromSummonLeave = 0x3DE5B6;  // exe+0x3DE460 -> exe+0x400A30
constexpr std::uint32_t kFromDriveEnd[] = {0x40CC6B, 0x40CF96};  // exe+0x40CC00 / exe+0x40CF20 -> exe+0x400440
// The summon's end exe+0x3DE080 is a task callback (handed over by exe+0x3DE010) that ends with a tail jump to the
// bring-back, so no return address tells it apart: it is hooked, and its bring-back counts as our summon's end.
constexpr std::uintptr_t kSummonEnd = 0x3DE080;
constexpr std::uint8_t kSummonEndBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9};
// The held places go every 6 frames and their end is in that stream (places 0), so this silence only frees what a
// game that went quiet (crashed, the link down) held, logged when it fires. The old 1 s freed them on a lag spike for
// a moment (TODO 8.11; 6.3).
constexpr std::uint32_t kHoldCapMs = 5000;
constexpr std::uint32_t kStatusFreshMs = 1000;  // the host's companion status this old is no longer copied
// Status (part 2).
constexpr std::uint32_t kStatusMagic = 0x5332484B;  // "KH2S"
constexpr std::uintptr_t kStats = 0x5C0;            // stat block: {cur, max, min} i32 each; HP stat 0, MP stat 32
constexpr std::uintptr_t kMpCur = 0x180, kMpMax = 0x184, kMpCharge = 0x1C0;  // in the block; charge f32 > 0 = recharging
constexpr int kStatMp = 32;
constexpr std::uint32_t kDownBit = 4;               // actor+0x9B8 bit 2: KO'd
constexpr std::uintptr_t kReviveFlags = 0x6C8;      // bit 14: an HP gain revives (exe+0x3D2EB0)
constexpr std::uintptr_t kRevive = 0x3C3C30;        // (actor, gain) -> scaled gain: clears the KO, motion 54 -> 55
constexpr std::uintptr_t kThink = 0x1B53B0;         // a companion's think (actor+0xE00, run by the brain exe+0x1B15F0)
constexpr std::uint8_t kThinkBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0x81, 0xB8, 0x09, 0x00, 0x00};
// Party swaps.
constexpr std::uintptr_t kPartyEntry = 0x3E2E20;    // (world, -1 = this one) -> u8[4] party places & 0x1F, [3] the bench
constexpr std::uintptr_t kSwapAllowed = 0x3E8F30;   // () -> the game's gate: no Limit, a bench, no swap under way
constexpr std::uintptr_t kCanSwap = 0x3C3950;       // (member) -> here, not held, nothing bound to it
constexpr std::uintptr_t kFieldSwap = 0x3E8F80;     // (member): starts the swap task; the entry changes at its end
constexpr std::uint8_t kFieldSwapBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x33, 0xD2, 0xC7, 0x44, 0x24, 0x30};
constexpr std::uint32_t kSwapGapMs = 2000;          // between two swaps we start (the task's own lock comes first)

using PFN_Leave = void(__fastcall*)(std::uintptr_t, float);
using PFN_LeaveBoth = void(__fastcall*)(float);
using PFN_Void = void(__fastcall*)();
using PFN_Task = void(__fastcall*)(std::uintptr_t);
using PFN_IntOfActor = int(__fastcall*)(std::uintptr_t);
using PFN_IntOfInt = int(__fastcall*)(int);
using PFN_Live = std::uint8_t(__fastcall*)(std::uintptr_t);
using PFN_Revive = int(__fastcall*)(std::uintptr_t, int);
using PFN_PartyEntry = const std::uint8_t*(__fastcall*)(int);
using PFN_Bool = std::uint8_t(__fastcall*)();

#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;   // the sender's world and room
    std::uint8_t places;  // bit 0: Donald's place, bit 1: Goofy's, held away by the sender's own form or summon
    std::uint8_t form;    // for the log
    std::uint8_t summon;  // the sender's summon state: 1 its entry plays, 2 the summon is out, 3 it leaves
    std::uint8_t party;   // the benched party place + 1 (0 = no bench here): the host's = the party both games should
                          // have, the friend's = its own
    std::uint8_t swaps;   // the friend's: swaps its own player made; the host's: the friend's count it has taken up
    std::uint8_t pad;
    SummonPose summons[2];  // the sender's own summon while it is out (state 2), else obj 0
    StitchScreen stitch;    // its screen state when that summon is Stitch
};
struct Member {
    std::uint32_t objId;
    std::int32_t hp, maxHp, mp, maxMp;
    float charge;  // MP recharge
    std::uint8_t down;
    std::uint8_t hit;  // the hit events its gauge played in the host's game (party_hud.cpp's hit byte)
    std::uint8_t pad[2];
};
enum : std::uint8_t { kStatus = 0, kGain = 1, kTurnAsk = 2, kTurnAnswer = 3 };
struct Status {  // host -> friend
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;
    std::uint8_t kind;  // kStatus
    std::uint8_t count;
    Member m[2];
};
struct Gain {  // friend -> host: an HP or MP gain on our copy of the host's companion
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;
    std::uint8_t kind;  // kGain
    std::uint8_t pad;
    std::uint32_t objId;
    std::int32_t delta, stat, react;
};
// One move at a time for the same companion (see HoldMove below). Friend -> host: may this move start? (kTurnAsk);
// host -> friend: yes or no (kTurnAnswer). Reliable, under the status magic, told apart by their size.
struct Turn {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint32_t seq;
    std::uint16_t room;
    std::uint8_t kind;
    std::uint8_t ok;    // the answer: 1 yes
    std::uint8_t mask;  // what the move needs: bits 0 to 2 the places (as Packet::places), bit 7 the one Limit
    std::uint8_t held;  // the answer: what of it the host's own move holds
    std::uint16_t id, type;  // the command (the log)
};
#pragma pack(pop)
static_assert(sizeof(Turn) != sizeof(Status) && sizeof(Turn) != sizeof(Gain), "told apart by size");

bool g_on = false;
bool g_host = false;
std::uint64_t g_frame = 0;
std::uint32_t g_seq = 0;
PFN_Leave g_realLeave = nullptr;
PFN_LeaveBoth g_realLeaveBoth = nullptr;
PFN_Void g_realBringBack = nullptr;
PFN_Task g_realSummonEnd = nullptr;
bool g_summonEnding = false;    // inside our summon's end

std::uint8_t g_ours = 0;        // places our own form or summon sent away (bits as in Packet::places)
const char* g_oursWhy = "";
std::uint8_t g_ourSent = 0;     // places we sent away for the other player
bool g_ourCall = false;         // inside our own leave / bring-back call
Packet g_peer{};
std::uint32_t g_peerMs = 0;
std::uint8_t g_peerWas = 0;     // the other player's held places as last applied
const char* g_peerWhy = "Drive Form";  // what the other player holds them for (the log)
std::uint16_t g_room = 0;
std::uint32_t g_kept = 0;       // list entries the bring-back kept (log)
// Status (part 2)
using PFN_Think = void(__fastcall*)(std::uintptr_t);
PFN_Think g_realThink = nullptr;
bool g_statusOn = false;
std::uint32_t g_statusSeq = 0, g_gainSeq = 0;
std::uint8_t g_downSent = 0;    // host: the KO bits last sent (bit per slot)
std::uint16_t g_hitsSent = 0;   // host: the two members' hit bytes last sent
Status g_status{};              // friend: the host's latest
std::uint32_t g_statusMs = 0;
std::uint32_t g_skippedThinks = 0, g_kos = 0, g_revives = 0, g_gainsSent = 0, g_gainsApplied = 0, g_mpWrites = 0;
std::uint32_t g_gainsDropped = 0;  // friend: gains on our copies the host's status owns (ours, the game's own)
// Party swaps
bool g_swapOn = false;
std::uint8_t g_bench = 0;        // our benched place + 1 as last read (0 = no bench in this world)
std::uint8_t g_target = 0;       // the bench both games should have, + 1; 0 = nothing to follow
std::uint8_t g_swaps = 0;        // friend: swaps our own player made; host: the friend's count taken up
bool g_swapsKnown = false;       // host: the friend's count seen since the link came up
std::uint32_t g_swapStartMs = 0;
std::uint8_t g_swapStuck = 0;    // the target last logged as not possible here

template <typename T>
T Read(std::uintptr_t a) {
    return *reinterpret_cast<const volatile T*>(a);
}

std::uint16_t Room() { return Read<std::uint16_t>(ExeBase() + game::kNow); }
std::uintptr_t Slot(int i) { return Read<std::uintptr_t>(ExeBase() + kFriendSlots + 8 * i); }

const char* Model(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// A party member that is here and not already leaving.
bool Staying(std::uintptr_t a) {
    __try {
        return a && reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a) && !(Read<std::uint32_t>(a + kRemoveFlags) & 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The party place of a member (1 Donald's, 2 Goofy's), 0 for anything else.
int PlaceOf(std::uintptr_t a) {
    __try {
        // A summon's type maps to place 3 too (L1519), and it is nobody's companion: object type 0x13 has no place.
        std::uintptr_t row = 0;
        std::uint8_t objType = 0;
        if (ReadAny(a + kObjRow, &row) && row && ReadAny(row + 4, &objType) && objType == 0x13) return 0;
        int type = reinterpret_cast<PFN_IntOfActor>(ExeBase() + kCharType)(a);
        int place = reinterpret_cast<PFN_IntOfInt>(ExeBase() + kPlaceOf)(type);
        return place >= 1 && place <= 3 ? place : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Places 1-3: Donald's, Goofy's, the world ally's (bench 2026-11-15: Aladdin wasn't 1 or 2 and stayed in the other
// game through an Anti Form).
std::uint8_t Bit(int place) { return place >= 1 && place <= 3 ? static_cast<std::uint8_t>(1 << (place - 1)) : 0; }

const char* PlaceName(int place) {
    return place == 1 ? "Donald's place" : place == 2 ? "Goofy's place" : place == 3 ? "the world ally's place" : "another place";
}

std::uint8_t PeerPlaces() {
    bool fresh = g_peer.room == Room() && AvatarLinkNowMs() - g_peerMs < kHoldCapMs;
    // While their summon stands in our game as an actor of ours (summon_sync.cpp), what they held stays held: its free
    // clears the party slot it was built in without a check (L1518), so no companion may be built there before.
    return static_cast<std::uint8_t>((fresh ? g_peer.places : 0) | (SummonSyncOut() ? g_peerWas : 0));
}

// ---- One move at a time for the same companion (TODO 1.198, 1.275; your picks 2026-11-23) ----
// Your rule (DECISIONS Q5): never two moves that need the same companion. Once a move runs, the other game already
// greys what clashes with it (the companions it took are sent away there, a Limit is refused). The gap was the moment
// before either game knew of the other's press: two Wisdom Forms at once (bench formlook, 2026-11-14). So the host
// decides: its own press is carried out at once; the friend's game holds its move at the carry-out step, asks, and
// carries it out on the host's yes. The host says no while its own running move holds a companion the asked one
// needs, and after a yes it drops its own clashing press for a moment (the friend's move is on its way).
// What a move needs: a Drive by its form (Valor Goofy, Wisdom Donald, Master and Final everyone, Limit Form nobody);
// a summon everyone; a party Limit its partners, from its row of the game's Limit table, and the one Limit (L2);
// Limit Form's own Limits only the one Limit. A running Limit of the host's own holds everyone (it sends every ally
// but its partner away, L1).
constexpr std::uint8_t kEveryone = 0x07, kOneLimit = 0x80;
constexpr std::uintptr_t kCommandRow = 0xDD0;  // actor: the picked command's table row; a Limit's id is read from it
constexpr std::uint32_t kTurnWaitMs = 500;     // no answer this long: carried out anyway (a fair press isn't dropped)
constexpr std::uint32_t kAgreedMs = 1000;      // host: what it agreed to stays the friend's this long, until its game shows it
constexpr int kRecordBytes = 0x40;             // the command record as the carry-out reads it (to +0x1C)

// A party Limit's partners by its command: Donald 0x01, Goofy 0x02, a world ally 0x04; -1 = no Limit of the table
// (the Limit list's own entry, a follow-up: nothing to ask for).
int LimitPartners(std::uint16_t command) {
    std::uintptr_t table = 0;
    std::int32_t count = 0;
    if (!ReadAny(ExeBase() + game::kLimtTable, &table) || !table || !ReadAny(table + 4, &count) || count <= 0 || count > 256)
        return -1;
    for (int i = 0; i < count; ++i) {
        std::uintptr_t row = table + 8 + 0x40 * static_cast<std::uintptr_t>(i);
        std::uint16_t cmd = 0;
        std::uint32_t head = 0;  // id, then the three characters
        if (!ReadAny(row + 0x28, &cmd) || cmd != command || !ReadAny(row, &head)) continue;
        int mask = 0;
        for (int k = 1; k <= 3; ++k) {
            std::uint8_t c = static_cast<std::uint8_t>(head >> (8 * k));
            mask |= c == 2 ? 0x01 : c == 3 ? 0x02 : c > 3 ? 0x04 : 0;
        }
        return mask;
    }
    return -1;
}

// -1 = not such a move, else what it needs (0 = nothing: never asked).
int MoveNeeds(const std::uint16_t* command) {
    switch (command[1]) {
        case 4:  // a Drive: the form is the record's third word (exe+0x405440)
            switch (static_cast<std::int16_t>(command[2])) {
                case 1: return 0x02;
                case 2: return 0x01;
                case 3: return 0;
                case 4: case 5: return kEveryone;
            }
            return kEveryone;  // a form we don't know: as the heaviest
        case 0x48: return kEveryone;
        case 0x51: {
            int partners = LimitPartners(command[0]);
            return partners < 0 ? -1 : partners | kOneLimit;
        }
        case 0x2C3: return kOneLimit;
    }
    return -1;
}

// As the other game is told (to its end mark): a Limit pressed in the gap after it is a fair press (bench limit_back).
bool OurLimitRuns() { return LimitSyncOurLimit(); }

// Friend: the move that waits for the host's answer. Proof at the carry-out that it is still that press: our Sora is
// the same actor, its picked command's row is the same, the room is the same, we are in the field and not down; the
// record itself is our own copy.
struct HeldMove {
    bool valid;
    std::uint8_t answer;  // 0 none yet, 1 yes, 2 no
    std::uint8_t mask, held;
    std::uint16_t room;
    std::uint32_t seq, askedMs;
    std::uintptr_t actor, row;
    alignas(8) std::uint8_t record[kRecordBytes];
} g_move {};
bool g_carrying = false;  // inside our carry-out of an agreed move
std::uint32_t g_turnSeq = 0, g_turnLogs = 0;
// Host: what it agreed to for the friend, and until when.
std::uint8_t g_agreed = 0;
std::uint32_t g_agreedAt = 0;

void SendTurn(std::uint8_t kind, std::uint32_t seq, std::uint8_t ok, std::uint8_t mask, std::uint8_t held, std::uint16_t id,
              std::uint16_t type) {
    Turn t {kStatusMagic, kVersion, AvatarLinkBuild(), seq, Read<std::uint16_t>(ExeBase() + game::kNow), kind, ok, mask,
            held, id, type};
    AvatarLinkSendReliable(&t, sizeof(t));
}

// Friend, each frame: the held move's answer, or the wait's end.
void TurnFrame() {
    if (g_host || !g_move.valid) return;
    std::uint32_t waited = AvatarLinkNowMs() - g_move.askedMs;
    if (!g_move.answer && waited < kTurnWaitMs) return;
    HeldMove m = g_move;
    g_move.valid = false;
    const auto* rec = reinterpret_cast<const std::uint16_t*>(m.record);
    if (m.answer == 2) {
        Log("companion sync: our command 0x%X (type 0x%X) refused by the host: its own move holds what it needs (0x%X of "
            "0x%X)", rec[0], rec[1], m.held, m.mask);
        return;
    }
    std::uintptr_t sora = Read<std::uintptr_t>(ExeBase() + game::kListHead), row = 0;
    const char* why = sora != m.actor ? "our Sora is another actor now"
                      : !ReadAny(sora + kCommandRow, &row) || row != m.row ? "another command was picked since"
                      : Read<std::uint16_t>(ExeBase() + game::kNow) != m.room ? "the room changed"
                      : !Read<std::uint8_t>(ExeBase() + game::kInField) ? "we are not in the field"
                      : DownedIsDown() ? "our Sora is down" : nullptr;
    if (why) {
        Log("companion sync: our command 0x%X (type 0x%X), %s after %u ms, not carried out: %s", rec[0], rec[1],
            m.answer ? "agreed to" : "unanswered", waited, why);
        return;
    }
    if (g_turnLogs++ < 60)
        Log("companion sync: our command 0x%X (type 0x%X) carried out %u ms after the press: %s", rec[0], rec[1], waited,
            m.answer ? "the host agreed" : "no answer from the host, so as if alone");
    g_carrying = true;
    PuppetCarryOut(sora, rec);
    g_carrying = false;
}

// Host: the friend asks.
void OnTurnAsk(const Turn& t) {
    std::uint32_t now = AvatarLinkNowMs();
    std::uint8_t ours = static_cast<std::uint8_t>(g_ours | (OurLimitRuns() ? kEveryone | kOneLimit : 0));
    std::uint8_t held = t.room == Read<std::uint16_t>(ExeBase() + game::kNow) ? static_cast<std::uint8_t>(ours & t.mask) : 0;
    if (!held) {
        g_agreed = t.mask;
        g_agreedAt = now;
    }
    SendTurn(kTurnAnswer, t.seq, held ? 0 : 1, t.mask, held, t.id, t.type);
    if (g_turnLogs++ < 60)
        Log("companion sync: the friend asks for command 0x%X (type 0x%X, needs 0x%X): %s (our own move holds 0x%X)", t.id,
            t.type, t.mask, held ? "no" : "yes", ours);
}

std::uint32_t Rva(void* ret) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ret) - ExeBase()); }

// The list holds two entries before its count: an append while both are there (a member sent away whose place is
// already listed) writes over the count. After each leave: the places from before plus the new one, once each.
struct ListSnap {
    std::uint32_t e[kListMax];
    int n;
};
ListSnap TakeList() {
    ListSnap s{};
    s.n = Read<std::int32_t>(ExeBase() + kListCount);
    if (s.n < 0 || s.n > kListMax) s.n = 0;
    for (int i = 0; i < s.n; ++i) s.e[i] = Read<std::uint32_t>(ExeBase() + kList + 4 * i);
    return s;
}
std::uint32_t g_listRepairs = 0;
void RepairList(const ListSnap& before, const int* added, int adds) {
    ListSnap fixed = before;
    bool dropped = false;
    for (int k = 0; k < adds; ++k) {
        if (added[k] < 0) continue;
        bool have = false;
        for (int i = 0; i < fixed.n; ++i) have = have || fixed.e[i] == static_cast<std::uint32_t>(added[k]);
        if (have) continue;
        if (fixed.n < kListMax)
            fixed.e[fixed.n++] = static_cast<std::uint32_t>(added[k]);
        else
            dropped = true;
    }
    auto* list = reinterpret_cast<std::uint32_t*>(ExeBase() + kList);
    auto* count = reinterpret_cast<std::int32_t*>(ExeBase() + kListCount);
    bool same = *count == fixed.n;
    for (int i = 0; same && i < fixed.n; ++i) same = list[i] == fixed.e[i];
    if (same) return;
    for (int i = 0; i < fixed.n; ++i) list[i] = fixed.e[i];
    *count = fixed.n;
    if (g_listRepairs++ < 10)
        Log("companion sync: the bring-back list got a place twice%s: kept once (%d entries)", dropped ? " or a third" : "",
            fixed.n);
}

// The place the game puts on the list for a member (exe+0x3E31C0 of its type), -1 if unknown.
int RawPlace(std::uintptr_t a) {
    __try {
        return reinterpret_cast<PFN_IntOfInt>(ExeBase() + kPlaceOf)(reinterpret_cast<PFN_IntOfActor>(ExeBase() + kCharType)(a));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void __fastcall HookLeave(std::uintptr_t member, float fade) {
    std::uint32_t from = Rva(_ReturnAddress());
    if (!g_ourCall && from == kFromDriveLeave) {
        int place = PlaceOf(member);
        g_ours |= Bit(place);
        g_oursWhy = "our Drive Form";
        Log("companion sync: our Drive Form sends our %s (%s) away: the other game sends its own away too", Model(member),
            PlaceName(place));
    }
    ListSnap before = TakeList();
    int added = RawPlace(member);
    g_realLeave(member, fade);
    RepairList(before, &added, 1);
}

void __fastcall HookLeaveBoth(float fade) {
    std::uint32_t from = Rva(_ReturnAddress());
    if (!g_ourCall) {
        // The summon's start, or the Game Over's Mickey branch: held like a summon's until our own game brings them
        // back (Sora in control again), so the other game's allies don't come back mid get-up (you, 2026-11-15).
        for (int i = 0; i < 2; ++i)
            if (Staying(Slot(i))) g_ours |= Bit(PlaceOf(Slot(i)));
        g_oursWhy = from == kFromSummonLeave ? "our summon" : "our Mickey rescue";
        Log("companion sync: %s sends our party away (places 0x%X): the other game sends its own away too", g_oursWhy,
            g_ours);
    }
    ListSnap before = TakeList();
    int added[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        __try {
            if (Slot(i) && reinterpret_cast<PFN_Live>(ExeBase() + kLive)(Slot(i))) added[i] = RawPlace(Slot(i));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_realLeaveBoth(fade);
    RepairList(before, added, 2);
}

// The object id the party table builds for a place (what exe+0x400440 passes to the member build exe+0x3C2FC0).
std::uint32_t PartyObjId(int place) {
    __try {
        return place >= 0 && place < 4 ? Read<std::uint16_t>(ExeBase() + kPartyTable + 0x10 + 2 * place) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Who still holds a place on the list: the other player (form, summon, or its Limit unless that member is the
// Limit's partner), or our own form or summon unless this call is its end.
bool StillHeld(int place, bool ourEnd) {
    if (PeerPlaces() & Bit(place)) return true;
    if (LimitSyncPeerHolds(PartyObjId(place))) return true;
    return !ourEnd && (g_ours & Bit(place));
}

void __fastcall HookBringBack() {
    std::uint32_t from = Rva(_ReturnAddress());
    // Our hold is over at the form's or summon's own end, or once neither is up: a form ended by a Game Over and
    // Mickey's revive brings the party back from elsewhere (two PCs 2026-11-01 17:22, Goofy kept away in both games).
    bool oursUp = Read<std::uint8_t>(ExeBase() + kForm) != 0 || Read<std::uint32_t>(ExeBase() + kSummonState) == 2;
    bool ourEnd = !g_ourCall && (from == kFromDriveEnd[0] || from == kFromDriveEnd[1] || g_summonEnding ||
                                 (g_ours && !oursUp));
    auto* list = reinterpret_cast<std::uint32_t*>(ExeBase() + kList);
    auto* count = reinterpret_cast<std::int32_t*>(ExeBase() + kListCount);
    std::uint32_t keep[kListMax] = {};
    int kept = 0, n = *count;
    if (n < 0 || n > kListMax) {  // not the shape we know: leave it to the game
        g_realBringBack();
        return;
    }
    std::uint32_t build[kListMax] = {};
    int builds = 0;
    for (int i = 0; i < n; ++i) {
        int place = static_cast<int>(list[i]);
        if (StillHeld(place, ourEnd))
            keep[kept++] = list[i];
        else
            build[builds++] = list[i];
    }
    if (ourEnd) {
        Log("companion sync: %s ended: places 0x%X come back%s", g_oursWhy, g_ours,
            kept ? ", except those the other player still holds" : "");
        g_ours = 0;
    }
    if (kept) {
        if (g_kept++ < 20)
            for (int i = 0; i < kept; ++i)
                Log("companion sync: the bring-back keeps %s away: %s", PlaceName(static_cast<int>(keep[i])),
                    PeerPlaces() & Bit(static_cast<int>(keep[i])) ? "the other player's form or summon holds it"
                    : (g_ours & Bit(static_cast<int>(keep[i]))) ? "our own form or summon holds it"
                                                                : "the other player's Limit holds it");
        for (int i = 0; i < builds; ++i) list[i] = build[i];
        *count = builds;
    }
    g_realBringBack();
    for (int i = 0; i < kept; ++i) list[i] = keep[i];
    *count = kept;
}

void __fastcall HookSummonEnd(std::uintptr_t task) {
    g_summonEnding = true;
    g_realSummonEnd(task);
    g_summonEnding = false;
}

// ---- Status (part 2) ----
std::uint32_t ObjIdOf(std::uintptr_t a) {
    __try {
        std::uintptr_t row = Read<std::uintptr_t>(a + kObjRow);
        std::uintptr_t exe = ExeBase();
        return row > exe && row < exe + 0x3000000 ? Read<std::uint32_t>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool Down(std::uintptr_t a) { return (Read<std::uint32_t>(a + kRemoveFlags) & kDownBit) != 0; }

// A member in the actor list (also KO'd): its stat block, else 0.
std::uintptr_t StatsOf(std::uintptr_t a) {
    __try {
        if (!a || !reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a)) return 0;
        return Read<std::uintptr_t>(a + kStats);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void __fastcall HookThink(std::uintptr_t self) {
    bool skip = g_statusOn && WorldSyncSkipAi(self);  // its pose and actions are the other game's
    if (skip)
        ++g_skippedThinks;
    else
        g_realThink(self);
    WorldCensusThinkTrace(self, skip);
}

// Host: our companions' status, every 6 frames and at once when a KO starts or ends.
void SendStatus() {
    Status p{kStatusMagic, kVersion, AvatarLinkBuild(), 0, Room(), kStatus, 0, {}};
    std::uint8_t down = 0;
    std::uint16_t hits = 0;
    __try {
        for (int i = 0; i < 2; ++i) {
            std::uintptr_t a = Slot(i), st = StatsOf(a);
            if (!st) continue;
            Member& m = p.m[p.count++];
            m.objId = ObjIdOf(a);
            m.hp = Read<std::int32_t>(st);
            m.maxHp = Read<std::int32_t>(st + 4);
            m.mp = Read<std::int32_t>(st + kMpCur);
            m.maxMp = Read<std::int32_t>(st + kMpMax);
            m.charge = Read<float>(st + kMpCharge);
            m.down = Down(a) ? 1 : 0;
            m.hit = PartyHudMemberHit(a);
            hits |= static_cast<std::uint16_t>(m.hit << (8 * i));
            if (m.down) down |= static_cast<std::uint8_t>(1 << i);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (g_frame % 6 != 0 && down == g_downSent && hits == g_hitsSent) return;  // a KO or a hit event: at once
    g_hitsSent = hits;
    if (down != g_downSent)
        Log("companion sync: our companions' KO now 0x%X (bit per party slot): sent to the friend at once", down);
    g_downSent = down;
    p.seq = ++g_statusSeq;
    AvatarLinkSendRaw(&p, static_cast<int>(sizeof(p)));
}

// Friend: our copy of a host's companion takes its status.
void ApplyStatus(std::uintptr_t a, const Member& m) {
    std::uintptr_t st = StatsOf(a);
    if (!st) return;
    PartyHudHostHit(a, m.hit);
    const char* model = Model(a);
    bool down = Down(a);
    if (m.down && !down) {  // the game's HP change to 0: the KO (dead bit, timer); the host's motion stream shows it
        int hp = Read<std::int32_t>(st);
        int after = PuppetApplyHp(a, -(hp > 0 ? hp : 1), 0);
        ++g_kos;
        Log("companion sync: the host's %s is KO'd: ours too (HP %d -> %d, KO %s)", model, hp, after,
            Down(a) ? "yes" : "NO (the game didn't take it)");
    } else if (!m.down && down && m.hp > 0) {  // the game's revive: an HP gain on a KO'd member
        bool byGain = (Read<std::uint32_t>(a + kReviveFlags) & 0x4000) != 0;
        if (byGain) PuppetApplyHp(a, m.hp, 0);
        bool direct = Down(a);
        if (direct) reinterpret_cast<PFN_Revive>(ExeBase() + kRevive)(a, m.hp);
        *reinterpret_cast<std::int32_t*>(st) = m.hp;
        ++g_revives;
        Log("companion sync: the host's %s got up: ours too, HP %d (%s)%s", model, m.hp,
            direct ? "the game's revive call" : "an HP gain, the game's way", Down(a) ? ", STILL KO'd" : "");
    }
    auto* mp = reinterpret_cast<std::int32_t*>(st + kMpCur);
    if (mp[0] != m.mp || mp[1] != m.maxMp || Read<float>(st + kMpCharge) != m.charge) {
        mp[0] = m.mp;
        mp[1] = m.maxMp;
        *reinterpret_cast<float*>(st + kMpCharge) = m.charge;
        ++g_mpWrites;
    }
}

void ApplyHostStatus() {
    if (AvatarLinkNowMs() - g_statusMs > kStatusFreshMs || g_status.room != Room()) return;
    for (int i = 0; i < g_status.count && i < 2; ++i) {
        std::uintptr_t a = WorldSyncMirroredCompanion(g_status.m[i].objId);
        if (!a) continue;
        __try {
            ApplyStatus(a, g_status.m[i]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            static int logged = 0;
            if (logged++ < 10) Log("companion sync: applying the host's status to our companion failed (exception)");
        }
    }
}

// Host: the friend's gain, on our companion, the game's way (a KO'd one gets up).
void ApplyGain(const Gain& g) {
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!StatsOf(a) || ObjIdOf(a) != g.objId) continue;
        bool down = Down(a);
        int after = PuppetApplyStat(a, g.delta, g.stat, g.react);
        ++g_gainsApplied;
        Log("companion sync: the friend's %s +%d on our %s -> %d%s", g.stat == 0 ? "HP" : "MP", g.delta, Model(a), after,
            down ? (Down(a) ? " (KO'd: it stays down)" : " (it gets up)") : "");
        return;
    }
    Log("companion sync: the friend's gain (+%d, stat %d) for a companion we don't have (object 0x%X): dropped", g.delta,
        g.stat, g.objId);
}

// ---- Party swaps ----
// Our benched party place + 1 in this world: 2 Donald's, 3 Goofy's, 4 the world ally's; 0 = nobody benched.
std::uint8_t Bench() {
    std::uint8_t b = reinterpret_cast<PFN_PartyEntry>(ExeBase() + kPartyEntry)(-1)[3] & 0x1F;
    return b >= 1 && b <= 3 ? static_cast<std::uint8_t>(b + 1) : 0;
}

const char* BenchName(std::uint8_t bench) {
    return bench == 2 ? "Donald" : bench == 3 ? "Goofy" : bench == 4 ? "the world's ally" : "nobody";
}

// Our party as the other game has it: the member in the target's place goes to the bench, the game's own way.
void FollowParty(std::uint8_t bench) {
    std::uint32_t now = AvatarLinkNowMs();
    if (!g_target || !bench || g_target == bench || now - g_swapStartMs < kSwapGapMs) return;
    // A member away for a Limit, form or summon comes back through the bring-back list: not while any of that runs.
    if (PeerPlaces() || g_ours || g_ourSent || PuppetOurLimitRunning() || LimitSyncPeerLimit()) return;
    // An open menu holds its own copy of the party and starts a swap of its own when it closes (exe+0x36B460).
    if (Read<std::uint8_t>(ExeBase() + game::kOpenMenu) != 0xFF) return;
    if (!reinterpret_cast<PFN_Bool>(ExeBase() + kSwapAllowed)()) return;
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!Staying(a) || RawPlace(a) != g_target - 1) continue;
        if (!reinterpret_cast<PFN_Live>(ExeBase() + kCanSwap)(a)) return;  // held or busy: next frame
        Log("companion sync: party swap: our %s goes to the bench for %s, as in the other game (the game's own swap)",
            Model(a), BenchName(bench));
        reinterpret_cast<PFN_Task>(ExeBase() + kFieldSwap)(a);
        g_swapStartMs = now;
        return;
    }
    if (g_swapStuck != g_target) {
        g_swapStuck = g_target;
        Log("companion sync: party swap: the other game benched %s, who isn't in our party here (our bench: %s): "
            "nothing swapped", BenchName(g_target), BenchName(bench));
    }
}

void PartyFrame() {
    std::uint8_t bench = Bench();
    std::uint32_t age = AvatarLinkNowMs() - g_peerMs;
    bool fresh = g_peer.room == Room() && age < kHoldCapMs;
    if (bench != g_bench) {  // our party entry changed: the swap we made for the other game, or one of our own
        bool own = bench != g_target;
        Log("companion sync: party swap: our bench %s -> %s (%s)", BenchName(g_bench), BenchName(bench),
            own ? "our own swap: the other game follows" : "as the other game's");
        g_bench = bench;
        if (g_host)
            g_target = bench;
        else if (own) {
            ++g_swaps;  // the host's party is older than this swap until it echoes the count
            g_target = bench;
        }
    }
    if (g_host) {
        if (age >= kHoldCapMs) g_swapsKnown = false;  // the friend went quiet: its count starts again
        if (fresh && (!g_swapsKnown || g_peer.swaps != g_swaps)) {
            if (g_swapsKnown && g_peer.party) {
                g_target = g_peer.party;
                Log("companion sync: party swap: the friend benched %s (its swap %u): our party follows",
                    BenchName(g_target), g_peer.swaps);
            }
            g_swaps = g_peer.swaps;
            g_swapsKnown = true;
        }
    } else if (fresh && g_peer.swaps == g_swaps) {
        g_target = g_peer.party;  // the host has heard of every swap of ours: its party is the one
    }
    if (fresh) FollowParty(bench);
}

// Our own summon's actor for the other game: what it is, where, and its motion.
void FillSummon(std::uintptr_t a, SummonPose* s) {
    __try {
        if (!a || !reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a)) return;
        std::uintptr_t entity = a + 0x640, row = 0, bank = 0;  // its entity: +0x30 position, +0x4C facing
        SummonPose p{};
        bool ok = ReadAny(a + kObjRow, &row) && row && ReadAny(row, &p.obj) && ReadAny(entity + 0x4C, &p.angle) &&
                  ReadAny(a + 0x180, &p.motion) && ReadAny(a + 0x2B8, &bank);
        for (int i = 0; i < 3; ++i) ok = ok && ReadAny(entity + 0x30 + 4 * i, &p.pos[i]);
        p.otherBank = bank ? 1 : 0;
        if (ok) *s = p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s->obj = 0;
    }
}

// Our own Stitch's screen state for the other game (see StitchScreen).
void FillStitch(std::uintptr_t a, StitchScreen* s) {
    __try {
        if (!a || !reinterpret_cast<PFN_Live>(ExeBase() + kLive)(a)) return;
        std::uintptr_t row = 0, helper = 0;
        std::uint16_t character = 0;
        if (!ReadAny(a + kObjRow, &row) || !row || !ReadAny(row + 0x4C, &character) || character != 0xC8) return;  // Stitch
        if (!ReadAny(a + 0xDF8, &helper) || !helper || !Readable(helper + 0x650, 0x144)) return;
        std::memcpy(s->bytes, reinterpret_cast<const void*>(helper + 0x650), sizeof(s->bytes));
        if (ReadAny(helper + 0x790, &s->state)) s->on = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s->on = 0;
    }
}

void Send() {
    Packet p{kMagic, kVersion, AvatarLinkBuild(), ++g_seq, Room(), g_ours, Read<std::uint8_t>(ExeBase() + kForm),
             static_cast<std::uint8_t>(Read<std::uint32_t>(ExeBase() + kSummonState)),
             g_host && g_target ? g_target : g_bench, g_swaps, 0, {}, {}};
    if (p.summon == 2) {
        for (int k = 0; k < 2; ++k) FillSummon(Read<std::uintptr_t>(ExeBase() + game::kSummonSlots + 8 * k), &p.summons[k]);
        FillStitch(Read<std::uintptr_t>(ExeBase() + game::kSummonSlots), &p.stitch);
    }
    // The summon's part only while one is out: without it the packet is the 22 bytes it always was (it goes every
    // frame through a Drive Form too).
    AvatarLinkSendRaw(&p, static_cast<int>(p.summon == 2 ? sizeof(p) : offsetof(Packet, summons)));
}

// The other player holds these places: our members in them leave (the game's own call, the form's fade).
void SendAway(std::uint8_t places, bool start) {
    if (PuppetOurLimitRunning()) {  // our Limit sent the others away; the one staying is its partner
        if (start) Log("companion sync: the other player's form or summon started while our Limit runs: our partner stays");
        return;
    }
    for (int i = 0; i < 2; ++i) {
        std::uintptr_t a = Slot(i);
        if (!Staying(a)) continue;
        int place = PlaceOf(a);
        if (!(places & Bit(place))) continue;
        __try {
            g_ourCall = true;
            g_realLeave(a, Read<float>(ExeBase() + kDriveFade));
            g_ourCall = false;
            g_ourSent |= Bit(place);
            Log("companion sync: our %s (%s) leaves: the other player's %s holds it%s", Model(a), PlaceName(place),
                g_peerWhy, start ? "" : " (it came back meanwhile)");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_ourCall = false;
            Log("companion sync: sending our %s away failed (exception)", Model(a));
        }
    }
}

// The other player let places go: build ours again (the bring-back keeps what anyone still holds).
void BringBack(std::uint8_t freed) {
    ListSnap now = TakeList();
    bool listed = false;
    for (int i = 0; i < now.n; ++i) listed = listed || (freed & Bit(static_cast<int>(now.e[i])));
    g_ourSent &= static_cast<std::uint8_t>(~freed);
    if (!listed) return;
    __try {
        g_ourCall = true;
        HookBringBack();
        g_ourCall = false;
        Log("companion sync: the other player's %s let places 0x%X go: ours brought back (the game's own call)", g_peerWhy,
            freed);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_ourCall = false;
        Log("companion sync: bringing our members back failed (exception)");
    }
}

bool CodeMatches(std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n, const char* name) {
    if (std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0) return true;
    Log("companion sync: %s at exe+0x%llX isn't the expected code: off", name, static_cast<unsigned long long>(rva));
    return false;
}

}  // namespace

void CompanionSyncInit(bool host) {
    g_host = host;
    if (EnvInt("KH2COOP_COMPANION_SYNC", 1) != 1) {
        Log("companion sync: off (COMPANION_SYNC=0)");
        return;
    }
    g_on = HookFunction(kLeave, kLeaveBytes, sizeof(kLeaveBytes), reinterpret_cast<void*>(HookLeave),
                        reinterpret_cast<void**>(&g_realLeave), "party member leave (Drive Form, Limit)") &&
           HookFunction(kLeaveBoth, kLeaveBothBytes, sizeof(kLeaveBothBytes), reinterpret_cast<void*>(HookLeaveBoth),
                        reinterpret_cast<void**>(&g_realLeaveBoth), "both party members leave (summon)") &&
           HookFunction(kBringBack, kBringBackBytes, sizeof(kBringBackBytes), reinterpret_cast<void*>(HookBringBack),
                        reinterpret_cast<void**>(&g_realBringBack), "party bring-back") &&
           HookFunction(kSummonEnd, kSummonEndBytes, sizeof(kSummonEndBytes), reinterpret_cast<void*>(HookSummonEnd),
                        reinterpret_cast<void**>(&g_realSummonEnd), "summon end");
    Log("companion sync: %s", g_on ? "on: a Drive Form or summon in either game sends the same companions away in both"
                                   : "OFF: a hook could not be installed");
    if (!g_on) return;
    g_statusOn = HookFunction(kThink, kThinkBytes, sizeof(kThinkBytes), reinterpret_cast<void*>(HookThink),
                              reinterpret_cast<void**>(&g_realThink), "companion think");
    g_swapOn = CodeMatches(kFieldSwap, kFieldSwapBytes, sizeof(kFieldSwapBytes), "the field party swap");
    Log("companion sync: party swaps %s", g_swapOn ? "on: a swap in either game is made in the other too" : "OFF");
    Log("companion sync: status %s", !g_statusOn ? "OFF: the think hook could not be installed"
                                     : host       ? "on: our companions' HP, MP and KO go to the friend"
                                                  : "on: our companions take the host's HP, MP and KO; they don't think");
}

bool CompanionSyncHostOwns(std::uintptr_t actor) {
    if (!g_statusOn || g_host || !WorldSyncSkipAi(actor) || AvatarLinkNowMs() - g_statusMs > kStatusFreshMs) return false;
    ++g_gainsDropped;
    return true;
}

void CompanionSyncForwardGain(std::uintptr_t actor, int delta, int stat, int react) {
    if (!g_statusOn || g_host || delta <= 0 || (stat != 0 && stat != kStatMp) || !WorldSyncSkipAi(actor)) return;
    std::uint32_t objId = ObjIdOf(actor);
    if (!objId) return;
    Gain g{kStatusMagic, kVersion, AvatarLinkBuild(), ++g_gainSeq, Room(), kGain, 0, objId, delta, stat, react};
    AvatarLinkSendReliable(&g, static_cast<int>(sizeof(g)));  // once, in any order (the room checked there)
    if (g_gainsSent++ < 40)
        Log("companion sync: %s +%d on our %s: sent to the host's game (its companion takes it)", stat == 0 ? "HP" : "MP",
            delta, Model(actor));
}

void CompanionSyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        TurnFrame();
        std::uint16_t room = Room();
        if (room != g_room) {  // a room load builds the party again by itself
            if (g_ourSent || g_peerWas) Log("companion sync: room changed: places we sent away forgotten (0x%X)", g_ourSent);
            g_room = room;
            g_ourSent = 0;
            g_peerWas = 0;
            g_bench = Bench();  // the entry is per world: a new room's bench is no swap
            g_target = g_host ? g_bench : 0;
            g_swapStuck = 0;
            if (g_ours && Read<std::uint8_t>(ExeBase() + kForm) == 0 && Read<std::uint32_t>(ExeBase() + kSummonState) != 2) {
                Log("companion sync: room changed with no form or summon of ours up: places 0x%X ours no more", g_ours);
                g_ours = 0;
            }
        }
        // Our summon is out and nothing of ours is held: a room change (the load resets the summon state, and the new
        // room's summon is spawned with no leave call). The other game had its companions back there while ours
        // stayed away (your two-copy run 2026-11-15): the party's active places are ours again.
        if (!g_ours && Read<std::uint32_t>(ExeBase() + kSummonState) == 2) {
            std::uint8_t bench = Bench();  // the benched place + 1; 0 = this world has no third member
            g_ours = static_cast<std::uint8_t>(bench ? 0x7 & ~Bit(bench - 1) : 0x3);
            g_oursWhy = "our summon";
            Log("companion sync: our summon is out in this room (places 0x%X): the other game sends its own away too", g_ours);
        }
        if (g_swapOn) PartyFrame();
        if (g_frame % 6 == 0 || g_ours) Send();
        if (g_statusOn && g_host) SendStatus();
        if (g_statusOn && !g_host) ApplyHostStatus();
        if (g_statusOn && g_frame % 600 == 0 && (g_skippedThinks || g_kos || g_revives || g_gainsSent || g_gainsApplied))
            Log("companion sync: status so far: %u thinks skipped, %u KOs and %u get-ups copied, %u MP writes, gains "
                "%u dropped here, %u sent / %u applied", g_skippedThinks, g_kos, g_revives, g_mpWrites, g_gainsDropped,
                g_gainsSent, g_gainsApplied);
        std::uint8_t peer = PeerPlaces();
        if (!peer && g_peerWas && g_peer.places && g_peer.room == room)
            Log("companion sync: no word from the other game for %u ms: the places it held taken as free", kHoldCapMs);
        if (peer & ~g_peerWas) g_peerWhy = g_peer.summon ? "summon" : g_peer.form ? "Drive Form" : "Mickey rescue";
        if (peer) SendAway(peer, (peer & ~g_peerWas) != 0);
        std::uint8_t freed = static_cast<std::uint8_t>(g_peerWas & ~peer);
        if (freed) BringBack(freed);
        if (peer != g_peerWas)
            Log("companion sync: the other player holds places 0x%X (form %u, summon %u)", peer, g_peer.form,
                g_peer.summon);
        g_peerWas = peer;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int logged = 0;
        if (logged++ < 10) Log("companion sync: exception in frame update");
    }
}

void CompanionSyncOnPacket(const char* buf, int n) {
    std::uint32_t magic = 0;
    if (n >= 4) std::memcpy(&magic, buf, 4);
    if (magic == kStatusMagic && n == static_cast<int>(sizeof(Turn))) {
        Turn t;
        std::memcpy(&t, buf, sizeof(t));
        if (!g_on || t.version != kVersion || t.build != AvatarLinkBuild()) return;
        if (g_host && t.kind == kTurnAsk) {
            OnTurnAsk(t);
        } else if (!g_host && t.kind == kTurnAnswer && g_move.valid && t.seq == g_move.seq) {
            g_move.answer = t.ok ? 1 : 2;
            g_move.held = t.held;
        }
        return;
    }
    if (magic == kStatusMagic) {
        if (!g_statusOn) return;
        if (!g_host && n == static_cast<int>(sizeof(Status))) {
            Status p;
            std::memcpy(&p, buf, sizeof(p));
            if (p.version != kVersion || p.build != AvatarLinkBuild() || p.kind != kStatus) return;
            if (p.seq + 1000 < g_status.seq) g_status.seq = 0;  // the other game restarted
            if (p.seq <= g_status.seq) return;
            g_status = p;
            g_statusMs = AvatarLinkNowMs();
        } else if (g_host && n == static_cast<int>(sizeof(Gain))) {
            Gain g;
            std::memcpy(&g, buf, sizeof(g));
            if (g.version != kVersion || g.build != AvatarLinkBuild() || g.kind != kGain) return;
            if (g.room == Room()) ApplyGain(g);  // reliable: each gain comes once
        }
        return;
    }
    if (!g_on || (n != static_cast<int>(sizeof(Packet)) && n != static_cast<int>(offsetof(Packet, summons)))) return;
    Packet p{};  // the short form: no summon out
    std::memcpy(&p, buf, static_cast<std::size_t>(n));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.seq + 1000 < g_peer.seq) g_peer.seq = 0;  // the other game restarted
    if (p.seq <= g_peer.seq) return;
    g_peer = p;
    g_peerMs = AvatarLinkNowMs();
}

bool CompanionSyncListed() { return TakeList().n > 0; }

bool CompanionSyncHoldMove(std::uintptr_t actor, const std::uint16_t* command) {
    if (!g_on || g_carrying || !actor) return false;
    int needs = MoveNeeds(command);
    if (needs <= 0) return false;
    // A Limit's follow-ups are no start: our own Limit runs already.
    if ((command[1] == 0x51 || command[1] == 0x2C3) && OurLimitRuns()) return false;
    std::uint32_t now = AvatarLinkNowMs();
    if (g_host) {
        if (!(g_agreed & needs) || now - g_agreedAt >= kAgreedMs) return false;
        Log("companion sync: our command 0x%X (type 0x%X) dropped: the friend's move, agreed to %u ms ago, needs the same "
            "(0x%X of 0x%X)", command[0], command[1], now - g_agreedAt, g_agreed & needs, needs);
        return true;
    }
    // Nobody to clash with: the other player isn't in our room, or its game is silent.
    if (g_peer.room != Room() || now - g_peerMs >= kHoldCapMs) return false;
    if (g_move.valid) return true;  // one waits already: a second press in the wait is dropped
    HeldMove m {};
    if (!ReadAny(actor + kCommandRow, &m.row)) return false;
    for (int i = 0; i < kRecordBytes; i += 8)  // unreadable: the game's own carry-out goes on with it
        if (!ReadAny(reinterpret_cast<std::uintptr_t>(command) + i, reinterpret_cast<std::uint64_t*>(m.record + i))) return false;
    m.valid = true;
    m.mask = static_cast<std::uint8_t>(needs);
    m.room = Room();
    m.seq = ++g_turnSeq;
    m.askedMs = now;
    m.actor = actor;
    g_move = m;
    SendTurn(kTurnAsk, m.seq, 0, m.mask, 0, command[0], command[1]);
    if (g_turnLogs++ < 60)
        Log("companion sync: our command 0x%X (type 0x%X) needs 0x%X: asking the host first", command[0], command[1], needs);
    return true;
}

int CompanionSyncPeerSummonState() {
    return g_on && g_peer.room == Room() && AvatarLinkNowMs() - g_peerMs < kHoldCapMs ? g_peer.summon : 0;
}

const StitchScreen* CompanionSyncPeerStitch() {
    return g_on && g_peer.stitch.on && g_peer.room == Room() && AvatarLinkNowMs() - g_peerMs < 500 ? &g_peer.stitch : nullptr;
}

bool CompanionSyncPeerSummon(int k, SummonPose* out, bool* poseFresh) {
    std::uint32_t age = AvatarLinkNowMs() - g_peerMs;
    if (!g_on || k < 0 || k > 1 || g_peer.room != Room() || age >= kHoldCapMs || !g_peer.summons[k].obj) return false;
    *out = g_peer.summons[k];
    *poseFresh = age < 500;
    return true;
}

}  // namespace kh2coop
