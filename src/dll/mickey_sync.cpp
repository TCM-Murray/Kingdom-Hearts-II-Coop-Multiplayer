// Mickey's rescue in co-op (MICKEY_SYNC=1 on both PCs, default on; TODO 1.3, your design 2026-10-18,
// DECISIONS Q6).
//   - One roll for both: when a player goes down with the other in the room, the host draws for both
//     players, each with its own game's chance (Mickey allowed by the room's mission, chance =
//     max(0.8^appearances, 0.5)); the bigger margin wins, nobody if both miss. Decided early so both
//     games know it before their Game Over starts (~4 s after the last death).
//   - Both games take the Game Over's Mickey branch (the normal one can't be undone: it ends the room's
//     mission and removes the field's actors). The winner gets the white screen with its choices; the
//     loser's game holds at the state that would open them (white, no choices) until the winner picks.
//   - 'I won't give up': the loser's game leaves its Game Over the same way, minus Mickey's spawn, fades the
//     white back in (Mickey's spawn does that in the winner's game) and keeps its Sora down with no time
//     limit (downed.cpp). The winner's pose sends its lying Sora instead of Mickey (avatar_link.cpp).
//   - Healing Light (the winner's Sora is the player again): the loser gets up with full HP and its
//     Donald and Goofy come back (the Mickey branch sent them away; the game's own bring-back call).
//   - 'It's all over', Mickey dies, or nobody wins: the normal Game Over in both games, the host chooses
//     (game_over.cpp).
// Research: VERIFIED_OFFSETS rows 'Mickey rescue forced in co-op', 'Game Over screen fade' and the lead
// 'How a Game Over ends'. The Game Over object (exe+0x2AE8050): +0 kind, +0x10 state, byte +0x14 = Mickey
// branch, +0x48 = the white screen's choice callback.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

#include "avatar_link.hpp"
#include "common.hpp"
#include "downed.hpp"
#include "mickey_sync.hpp"
#include "puppet.hpp"

namespace kh2coop {
namespace {

constexpr std::uintptr_t kNow = 0x717008;          // u8 world, room
constexpr std::uintptr_t kInField = 0x9BA8D0;      // u8: 0 while a room loads
constexpr std::uintptr_t kBattle = 0x2A11404;      // u32: 1 while enemies are around
constexpr int kSettledFrames = 60;
constexpr std::uintptr_t kPlayerActor = 0x2A171C8; // list head: our Sora, Mickey during his rescue
constexpr std::uintptr_t kSoraHp = 0x2A23598;      // Sora's stat slot: u32 HP, u32 max HP
constexpr std::uintptr_t kGameOver = 0x2AE8050;    // Game Over object while it runs
constexpr std::uintptr_t kGoState = 0x10, kGoMickey = 0x14, kGoMickeyChoice = 0x48;
constexpr int kStateChoices = 5;                   // the Mickey branch opens the white screen's choices here
constexpr int kStateLeave = 7;                     // 'I won't give up': resume, Mickey's spawn, end
constexpr int kStateGaveUp = 8;                    // 'It's all over': the Continue / Load menu is up; stepping = end
constexpr int kChoiceGiveUp = 8, kChoiceMickey = 9;  // the white screen's callback options
constexpr std::uintptr_t kRollTable = 0x2AE5760;   // u64 -> +0xD8 f32 base, +0xDC f32 floor
constexpr std::uintptr_t kAppearances = 0x9AD660;  // u16: Mickey appearances in this save
constexpr std::uintptr_t kFade = 0xABB290;         // the Game Over's screen fade
constexpr int kFadeInFrames = 15;                  // the winner's game fades in about this long

constexpr std::uintptr_t kCtor = 0x3FC7A0;         // (obj, ?, kind): rolls Mickey, starts the Game Over
constexpr std::uint8_t kCtorBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
constexpr std::uintptr_t kEndMission = 0x3A3460;   // (): ends the room's mission (the ctor, no Mickey)
constexpr std::uint8_t kEndMissionBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
constexpr std::uintptr_t kUpdate = 0x3FCEC0;       // (obj) -> 0 = the Game Over is over
constexpr std::uint8_t kUpdateBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0x41, 0x10};
constexpr std::uintptr_t kSpawn = 0x4158C0;        // (): Mickey's spawn (state 7)
constexpr std::uint8_t kSpawnBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};
constexpr std::uintptr_t kChoice = 0x3FCC50;       // (option, menu): the white screen's choice callback
constexpr std::uint8_t kChoiceBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x83, 0xE9, 0x08};
constexpr std::uintptr_t kMickeyAllowed = 0x3A3BD0;  // () -> the room's mission allows Mickey
constexpr std::uint8_t kMickeyAllowedBytes[] = {0x48, 0x8B, 0x05, 0x91, 0xC3, 0x66, 0x02, 0x48, 0x85, 0xC0};
constexpr std::uintptr_t kFadeIn = 0x156C90;       // (fade, frames, white)
constexpr std::uint8_t kFadeInBytes[] = {0x44, 0x88, 0x81, 0x00, 0x01, 0x00, 0x00, 0xC7, 0x81, 0x08, 0x01};
constexpr std::uintptr_t kBringBack = 0x400440;    // (): build everyone on the bring-back list again
constexpr std::uint8_t kBringBackBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18};

constexpr std::uint32_t kMagic = 0x5A32484B;       // "KH2Z" ("KH2M" is motion_mirror.cpp's)
constexpr std::uint16_t kVersion = 1;
enum : std::uint8_t { kInfo = 0, kRoll = 1, kChoiceMade = 2, kRevived = 3, kMickeyDown = 4 };
enum : std::uint8_t { kByHealingLight = 0, kByLastHp = 1 };  // kRevived's value: how the winner's Sora got up
#pragma pack(push, 1)
struct Packet {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t type;
    std::uint32_t epoch;   // the roll this is about (kRoll: the roll's own number, 0 = no roll now)
    std::uint8_t value;    // kRoll: winner 0 nobody, 1 host, 2 friend; kChoiceMade: 8 / 9
    std::uint8_t eligible; // kInfo: Mickey allowed in the sender's room
    float chance;          // kInfo: the sender's chance
};
#pragma pack(pop)

constexpr std::uint32_t kPeerSilentMs = 5000;     // a held loser gives up waiting (the normal Game Over)
constexpr int kResends = 6;                        // an event goes out this many times, 10 frames apart

using PFN_Ctor = int*(__fastcall*)(int*, std::intptr_t, int);
using PFN_Void = void(__fastcall*)();
using PFN_Update = std::uint64_t(__fastcall*)(int*);
using PFN_Choice = void(__fastcall*)(int, std::uintptr_t);
using PFN_Allowed = bool(__fastcall*)();
using PFN_FadeIn = void(__fastcall*)(std::uintptr_t, int, std::uint8_t);
PFN_Ctor g_realCtor = nullptr;
PFN_Void g_realEndMission = nullptr, g_realSpawn = nullptr;
PFN_Update g_realUpdate = nullptr;
PFN_Choice g_realChoice = nullptr;

enum class Want { Game, Mickey, Normal };
enum class Role { None, Winner, Loser };
bool g_on = false, g_host = false;
std::uint64_t g_frame = 0;
// Each side's chance (kInfo).
bool g_peerEligible = false;
float g_peerChance = 0.0f;
std::uint32_t g_peerInfoMs = 0;
// The roll: decided by the host, received by the friend.
std::uint32_t g_rollEpoch = 0;     // 0 = no roll now
std::uint8_t g_rollWinner = 0;     // 0 nobody, 1 host, 2 friend
std::uint32_t g_rollMs = 0;        // friend: when the host last confirmed it
std::uint64_t g_calmSince = 0;     // host: everyone up since this frame
std::uint32_t g_epochs = 0;        // host: rolls so far
std::mt19937 g_rng;
int g_testWinner = -1;             // KH2COOP_TEST_MICKEY_WINNER=host/friend/nobody (bench): the roll's result
// This Game Over.
Want g_ctorWant = Want::Game;
Role g_role = Role::None;
std::uint32_t g_goEpoch = 0;
std::uint32_t g_peerChoiceEpoch = 0;
int g_peerChoice = 0;
bool g_skipSpawn = false, g_ourChoiceCall = false;
// Loser after the winner's 'It's all over': our Game Over object, held at state 8 until the Continue / Load
// reload removes it. The winner's game doesn't step it there (its white menu's open, exe+0x2E4C20, pauses the
// Game Over task; ours never opened): stepped, state 8 returns 0 and ends it before the menu shows.
std::uintptr_t g_gaveUpObj = 0;
// The rescue: Mickey is out in one of the two games.
bool g_rescue = false;
std::uintptr_t g_rescueSora = 0;   // the winner's own Sora (lying) while its Mickey is out
bool g_holdPending = false;        // loser: back in the field, put our Sora down (next frame)
std::uint8_t g_room[2] = {};
int g_loadFrames = 0;              // frames out of the field in a row
// Our chance, asked once per room after a settled second and again when a fight starts or ends (the mission may
// change then), never on the way out: the room's mission pointer (exe+0x2A0FF68, read by MickeyAllowed) can still
// point at the freed mission while a warp starts (bench 2026-10-18 01:47: an access violation at exe+0x3A3BE0).
int g_settled = 0;                 // frames in the field in this room in a row
bool g_chanceKnown = false, g_ourOk = false, g_battle = false;
float g_ourChance = 0.0f;
// The newest event we send, repeated.
Packet g_event {};
int g_eventLeft = 0;
// Events from the peer already handled (by type).
std::uint32_t g_handled[5] = {};

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const volatile T*>(ExeBase() + rva);
}

std::uintptr_t Head() { return Read<std::uintptr_t>(kPlayerActor); }
std::uint8_t Me() { return g_host ? 1 : 2; }
std::uintptr_t GameOverObj() { return Read<std::uintptr_t>(kGameOver); }

bool PeerInRoom(PeerPose& p) {
    return AvatarLinkPeerNow(p) && p.world == Read<std::uint8_t>(kNow) && p.room == Read<std::uint8_t>(kNow + 1);
}

// Our game's own rule (the Game Over ctor): Mickey only where the room's mission allows him; then
// max(base^appearances, floor). (The ctor also skips him while a companion is held by an enemy; not checked.)
bool OurChance(float& chance) {
    chance = 0.0f;
    __try {
        if (!reinterpret_cast<PFN_Allowed>(ExeBase() + kMickeyAllowed)()) return false;
        std::uintptr_t table = Read<std::uintptr_t>(kRollTable);
        if (!table) return false;
        float base = *reinterpret_cast<const float*>(table + 0xD8), floor = *reinterpret_cast<const float*>(table + 0xDC);
        chance = std::pow(base, static_cast<float>(Read<std::uint16_t>(kAppearances)));
        if (chance < floor) chance = floor;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Send(std::uint8_t type, std::uint32_t epoch, std::uint8_t value, std::uint8_t eligible = 0, float chance = 0.0f) {
    Packet p {kMagic, kVersion, AvatarLinkBuild(), type, epoch, value, eligible, chance};
    AvatarLinkSendRaw(&p, sizeof(p));
}

void SendEvent(std::uint8_t type, std::uint32_t epoch, std::uint8_t value) {
    g_event = {kMagic, kVersion, AvatarLinkBuild(), type, epoch, value, 0, 0.0f};
    g_eventLeft = kResends;
    AvatarLinkSendRaw(&g_event, sizeof(g_event));
}

const char* WinnerName(std::uint8_t w) { return w == 0 ? "nobody" : w == Me() ? "us" : "the other player"; }

// Host: someone is down (or dead) with the other player in the room: draw for both now.
void Roll() {
    float mine = g_ourChance;
    bool ours = g_chanceKnown && g_ourOk;
    bool theirs = g_peerEligible && AvatarLinkNowMs() - g_peerInfoMs < 3000;
    std::uniform_real_distribution<float> draw(0.0f, 1.0f);
    float rh = draw(g_rng), rf = draw(g_rng);
    float mh = ours ? mine - rh : -1.0f, mf = theirs ? g_peerChance - rf : -1.0f;
    g_rollWinner = mh <= 0.0f && mf <= 0.0f ? 0 : mh >= mf ? 1 : 2;
    if (g_testWinner >= 0) g_rollWinner = static_cast<std::uint8_t>(g_testWinner);  // bench only
    g_rollEpoch = ++g_epochs;
    Log("mickey: roll %u: host %s (chance %.2f, drew %.2f), friend %s (chance %.2f, drew %.2f) -> %s gets Mickey",
        g_rollEpoch, ours ? "allowed" : "not allowed", mine, rh, theirs ? "allowed" : "not allowed", g_peerChance, rf,
        g_rollWinner == 0 ? "nobody" : g_rollWinner == 1 ? "the host" : "the friend");
    if (g_testWinner >= 0) Log("mickey: (test setting KH2COOP_TEST_MICKEY_WINNER: the winner is set, not drawn)");
    Send(kRoll, g_rollEpoch, g_rollWinner);
}

bool RollValid() { return g_rollEpoch != 0 && (g_host || AvatarLinkNowMs() - g_rollMs < 1500); }

Want Decide(int kind) {
    PeerPose p;
    if (kind != 0 || !g_on || !CoopActive() || !PeerInRoom(p)) return Want::Game;
    if (g_rescue) return Want::Normal;  // Mickey died (or we follow his death): the normal Game Over
    if (!RollValid()) {
        Log("mickey: Game Over without a roll (no word from the host?): the game's own roll");
        return Want::Game;
    }
    return g_rollWinner ? Want::Mickey : Want::Normal;
}

void Reset(const char* why) {
    if (g_role != Role::None || g_rescue || g_holdPending) Log("mickey: reset (%s)", why);
    g_role = Role::None;
    g_rescue = false;
    g_rescueSora = 0;
    g_holdPending = false;
    g_skipSpawn = false;
    g_gaveUpObj = 0;
}

int* __fastcall HookCtor(int* obj, std::intptr_t a2, int kind) {
    g_gaveUpObj = 0;
    Want want = Decide(kind);
    bool mickeyOut = g_rescue && g_rescueSora;  // our own Mickey just died
    std::uintptr_t sora = Head();
    g_ctorWant = want;
    int* result = g_realCtor(obj, a2, kind);
    g_ctorWant = Want::Game;
    if (want == Want::Game) return result;
    auto go = reinterpret_cast<std::uintptr_t>(obj);
    auto* branch = reinterpret_cast<std::uint8_t*>(go + kGoMickey);
    auto* callback = reinterpret_cast<std::uintptr_t*>(go + kGoMickeyChoice);
    if (want == Want::Mickey) {
        bool rolled = *branch != 0;
        *branch = 1;
        *callback = ExeBase() + kChoice;
        g_role = g_rollWinner == Me() ? Role::Winner : Role::Loser;
        g_goEpoch = g_rollEpoch;
        g_rescueSora = g_role == Role::Winner ? sora : 0;
        Log("mickey: Game Over, roll %u: %s (the game's own roll said %s)", g_goEpoch,
            g_role == Role::Winner ? "we won Mickey: the white screen with its choices"
                                   : "the other player won Mickey: the white screen without choices, we wait for theirs",
            rolled ? "Mickey" : "no Mickey");
        return result;
    }
    if (*branch) {  // the game rolled Mickey itself: back to the normal Game Over
        *branch = 0;
        *callback = 0;
        g_realEndMission();
    }
    g_role = Role::None;
    if (mickeyOut) {
        g_rescueSora = 0;
        SendEvent(kMickeyDown, g_goEpoch, 0);
        Log("mickey: our Mickey is down: the normal Game Over (the other player's too)");
    } else {
        Log("mickey: Game Over, %s: the normal Game Over", g_rescue ? "after the other player's Mickey" : "nobody won Mickey");
    }
    return result;
}

void __fastcall HookEndMission() {
    if (g_ctorWant == Want::Mickey) {
        Log("mickey: the room's mission stays (the Mickey branch)");
        return;
    }
    g_realEndMission();
}

void __fastcall HookSpawn() {
    if (g_skipSpawn) {
        Log("mickey: no Mickey in our game (the other player has him)");
        return;
    }
    g_realSpawn();
}

void __fastcall HookChoice(int option, std::uintptr_t menu) {
    if (!g_ourChoiceCall && g_role == Role::Winner && (option == kChoiceGiveUp || option == kChoiceMickey)) {
        SendEvent(kChoiceMade, g_goEpoch, static_cast<std::uint8_t>(option));
        g_role = Role::None;
        if (option == kChoiceMickey) {
            g_rescue = true;
            Log("mickey: we chose 'I won't give up': Mickey comes; the other player's game goes back to the field");
        } else {
            g_rescueSora = 0;
            Log("mickey: we chose 'It's all over': the normal Game Over in both games");
        }
    }
    g_realChoice(option, menu);
}

std::uint64_t __fastcall HookUpdate(int* obj) {
    if (g_gaveUpObj && reinterpret_cast<std::uintptr_t>(obj) == g_gaveUpObj && obj[kGoState / 4] == kStateGaveUp)
        return 1;  // the Continue / Load menu stays up (the winner's game: the same, unstepped)
    if (g_role != Role::Loser || reinterpret_cast<std::uintptr_t>(obj) != GameOverObj() ||
        obj[kGoState / 4] != kStateChoices)
        return g_realUpdate(obj);
    bool answered = g_peerChoiceEpoch == g_goEpoch && g_peerChoice != 0;
    bool silent = AvatarLinkPeerAgeMs() > kPeerSilentMs;
    if (answered && g_peerChoice == kChoiceMickey) {
        obj[kGoState / 4] = kStateLeave;
        g_skipSpawn = true;
        std::uint64_t more = g_realUpdate(obj);  // resumes the world and ends the Game Over (no Mickey)
        g_skipSpawn = false;
        reinterpret_cast<PFN_FadeIn>(ExeBase() + kFadeIn)(ExeBase() + kFade, kFadeInFrames, 1);
        g_role = Role::None;
        g_rescue = true;
        g_holdPending = true;
        Log("mickey: the other player chose 'I won't give up': back to the field (white fades in), our Sora stays down");
        return more;
    }
    if ((answered && g_peerChoice == kChoiceGiveUp) || silent) {
        // The white menu first, as in the winner's game: its open (exe+0x2E4C20) sets up what the Continue / Load
        // menu needs (without it: a black screen, bench 2026-10-18 01:25). Picked in the same update, so it isn't drawn.
        std::uint64_t more = g_realUpdate(obj);
        if (obj[kGoState / 4] == kStateChoices) return more;  // not ready yet (exe+0x400840): next frame
        g_role = Role::None;
        g_ourChoiceCall = true;
        g_realChoice(kChoiceGiveUp, 0);  // the white screen's 'It's all over': the normal Game Over menu
        g_ourChoiceCall = false;
        g_gaveUpObj = reinterpret_cast<std::uintptr_t>(obj);
        Log("mickey: %s: the normal Game Over", silent ? "no word from the other player" : "the other player chose 'It's all over'");
        return 1;
    }
    return 1;  // white, no choices: the winner chooses
}

void OnEvent(const Packet& p) {
    if (p.type > kMickeyDown || p.epoch <= g_handled[p.type]) return;
    g_handled[p.type] = p.epoch;
    switch (p.type) {
    case kChoiceMade:
        g_peerChoiceEpoch = p.epoch;
        g_peerChoice = p.value;
        Log("mickey: the other player chose %s (roll %u)", p.value == kChoiceMickey ? "'I won't give up'" : "'It's all over'",
            p.epoch);
        break;
    case kRevived:
        if (!g_rescue) break;
        g_rescue = false;
        if (p.value == kByLastHp)
            DownedMickeyRelease();
        else
            DownedMickeyRevive();
        __try {
            reinterpret_cast<PFN_Void>(ExeBase() + kBringBack)();
            Log(p.value == kByLastHp ? "mickey: their Mickey's last HP revived the other player (half HP): we stay down, "
                                       "our allies are back"
                                     : "mickey: Healing Light revived the other player: we get up too, our allies are back");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("mickey: bringing our allies back failed (exception)");
        }
        break;
    case kMickeyDown:
        if (!g_rescue) break;
        DownedMickeyEnd();
        Log("mickey: the other player's Mickey is down: our Game Over too");
        break;
    }
}

}  // namespace

std::uintptr_t MickeyRescueSora() {
    if (!g_on || !g_rescue || !g_rescueSora) return 0;
    return Head() != g_rescueSora ? g_rescueSora : 0;
}

void MickeySyncInit(bool host) {
    if (EnvInt("KH2COOP_MICKEY_SYNC", 1) != 1) return;
    g_host = host;
    g_rng.seed(static_cast<std::uint32_t>(GetTickCount64() ^ GetCurrentProcessId()));
    char test[16] = {};
    if (EnvStr("KH2COOP_TEST_MICKEY_WINNER", test, sizeof(test)))
        g_testWinner = _stricmp(test, "host") == 0 ? 1 : _stricmp(test, "friend") == 0 ? 2 : 0;
    auto same = [](std::uintptr_t rva, const std::uint8_t* bytes, std::size_t n) {
        return std::memcmp(reinterpret_cast<const void*>(ExeBase() + rva), bytes, n) == 0;
    };
    if (!same(kMickeyAllowed, kMickeyAllowedBytes, sizeof(kMickeyAllowedBytes)) ||
        !same(kFadeIn, kFadeInBytes, sizeof(kFadeInBytes)) || !same(kBringBack, kBringBackBytes, sizeof(kBringBackBytes))) {
        Log("mickey sync: OFF: function bytes differ (other game build?)");
        return;
    }
    g_on = HookFunction(kCtor, kCtorBytes, sizeof(kCtorBytes), reinterpret_cast<void*>(HookCtor),
                        reinterpret_cast<void**>(&g_realCtor), "Game Over start (Mickey roll)") &&
           HookFunction(kEndMission, kEndMissionBytes, sizeof(kEndMissionBytes), reinterpret_cast<void*>(HookEndMission),
                        reinterpret_cast<void**>(&g_realEndMission), "mission end (Game Over)") &&
           HookFunction(kUpdate, kUpdateBytes, sizeof(kUpdateBytes), reinterpret_cast<void*>(HookUpdate),
                        reinterpret_cast<void**>(&g_realUpdate), "Game Over update") &&
           HookFunction(kSpawn, kSpawnBytes, sizeof(kSpawnBytes), reinterpret_cast<void*>(HookSpawn),
                        reinterpret_cast<void**>(&g_realSpawn), "Mickey's spawn") &&
           HookFunction(kChoice, kChoiceBytes, sizeof(kChoiceBytes), reinterpret_cast<void*>(HookChoice),
                        reinterpret_cast<void**>(&g_realChoice), "Mickey's white screen choice");
    Log("mickey sync: %s", g_on ? "on (one roll for both; the winner plays Mickey, the other player waits down)"
                                : "OFF: a hook could not be installed");
}

void MickeySyncOnPacket(const char* buf, int n) {
    if (!g_on || n != static_cast<int>(sizeof(Packet))) return;
    Packet p;
    std::memcpy(&p, buf, sizeof(p));
    if (p.version != kVersion || p.build != AvatarLinkBuild()) return;
    if (p.type == kInfo) {
        g_peerEligible = p.eligible != 0;
        g_peerChance = p.chance;
        g_peerInfoMs = AvatarLinkNowMs();
        return;
    }
    if (p.type == kRoll) {
        if (g_host) return;
        if (p.epoch != g_rollEpoch && p.epoch)
            Log("mickey: the host's roll %u: %s gets Mickey", p.epoch, WinnerName(p.value));
        g_rollEpoch = p.epoch;
        g_rollWinner = p.value;
        g_rollMs = AvatarLinkNowMs();
        return;
    }
    OnEvent(p);
}

void MickeySyncFrame() {
    if (!g_on) return;
    ++g_frame;
    __try {
        // A room change or a load (Continue reloads the same room): this Game Over and rescue are over.
        std::uint8_t room[2] = {Read<std::uint8_t>(kNow), Read<std::uint8_t>(kNow + 1)};
        bool inField = Read<std::uint8_t>(kInField) != 0;
        g_loadFrames = inField ? 0 : g_loadFrames + 1;
        bool moved = room[0] != g_room[0] || room[1] != g_room[1];
        if (moved || g_loadFrames == 30) {
            if (!GameOverObj()) Reset(g_loadFrames == 30 ? "a room load" : "room change");
            g_room[0] = room[0];
            g_room[1] = room[1];
        }
        g_settled = inField && !moved ? g_settled + 1 : 0;
        if (moved) g_chanceKnown = false;
        bool battle = Read<std::uint32_t>(kBattle) != 0;
        if (battle != g_battle) {
            g_battle = battle;
            g_chanceKnown = false;
        }
        if (!g_chanceKnown && g_settled >= kSettledFrames) {
            g_ourOk = OurChance(g_ourChance);
            g_chanceKnown = true;
        }
        if (g_frame % 30 == 0) Send(kInfo, 0, 0, g_chanceKnown && g_ourOk ? 1 : 0, g_ourChance);
        if (g_eventLeft > 0 && g_frame % 10 == 0) {
            --g_eventLeft;
            AvatarLinkSendRaw(&g_event, sizeof(g_event));
        }
        if (g_host) {
            PeerPose p;
            bool together = PeerInRoom(p) && Read<std::uint8_t>(kInField) && Head();  // not during a load
            bool ourDown = DownedIsLying() || static_cast<std::int32_t>(Read<std::uint32_t>(kSoraHp)) <= 0 || GameOverObj();
            bool theirDown = together && (p.downed || p.hp <= 0);
            if (together && (ourDown || theirDown) && !g_rollEpoch) Roll();
            bool calm = !ourDown && !theirDown && !g_rescue && g_role == Role::None;
            g_calmSince = calm ? (g_calmSince ? g_calmSince : g_frame) : 0;
            if (g_rollEpoch && g_calmSince && g_frame - g_calmSince > 120) {
                Log("mickey: everyone is up: roll %u dropped", g_rollEpoch);
                g_rollEpoch = 0;
            }
            if (g_frame % 10 == 0) Send(kRoll, g_rollEpoch, g_rollWinner);
        }
        if (g_holdPending && Read<std::uint8_t>(kInField) && Head()) {
            g_holdPending = false;
            DownedMickeyHold();
        }
        // The winner: our Sora is the player again. Healing Light gives full HP; Mickey's last HP (a hit that would
        // KO him leaves 1) gets Sora up with half (bench 2026-10-18 01:50, motion 254).
        std::int32_t hp = static_cast<std::int32_t>(Read<std::uint32_t>(kSoraHp));
        if (g_rescue && g_rescueSora && !GameOverObj() && Head() == g_rescueSora && hp > 0) {
            bool full = hp >= static_cast<std::int32_t>(Read<std::uint32_t>(kSoraHp + 4));
            Log(full ? "mickey: Healing Light: our Sora is up; the other player gets up too"
                     : "mickey: Mickey's last HP got our Sora up with HP %d: the other player stays down (normal down)", hp);
            SendEvent(kRevived, g_goEpoch, full ? kByHealingLight : kByLastHp);
            g_rescue = false;
            g_rescueSora = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace kh2coop
