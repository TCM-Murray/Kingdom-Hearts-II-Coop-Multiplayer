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
//   - The loser sees a real Mickey (TODO 1.3 step c, your pick 2026-10-21: the game's own Mickey, puppeted;
//     MICKEY_PUPPET=0 = the step 2 way, no Mickey there): the loser's game runs Mickey's spawn too (its appearance
//     count put back after, so only the winner's save counts him), its buttons are blocked but Start, hits on him
//     are dropped, and after each of his updates he takes the winner's Mickey's spot, facing and motion ("KH2Z"
//     pose packets). The winner's Healing Light: the loser's Mickey leaves the game's way (exe+0x416100, what
//     trap_obj_recov_holylight calls), then the game's Sora revive (trap_obj_reraise, exe+0x3AA8D0). Mickey's last
//     HP: he leaves the same way and the loser's Sora stays down with the usual timer (downed.cpp).
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
// The loser's own Mickey (step c). Mickey leaves after Healing Light: (Mickey) -> the player back to the Sora kept
// at exe+0x2A11268, enemies back on it, Sora motion 253. Then the Sora revive (dead bit off, movement on, HP max).
constexpr std::uintptr_t kMickeyLeave = 0x416100;
constexpr std::uint8_t kMickeyLeaveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xF9};
// Mickey leaves after his last HP: the same hand-back, Sora motion 254 at half HP, and that exit's own scene
// (exe+0x3DB2F0 from Mickey's files; Healing Light's exit plays another one). The loser's game uses it when the winner's
// Mickey left that way: with Healing Light's exit there, its camera went off (your report 2026-10-21, Twin Lords bench).
constexpr std::uintptr_t kMickeyLastHpLeave = 0x416030;
constexpr std::uint8_t kMickeyLastHpLeaveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9};
constexpr std::uintptr_t kSoraRevive = 0x3AA8D0;  // (actor); downed.cpp checks its bytes, so it isn't hooked
constexpr std::uint8_t kSoraReviveBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30,
                                             0x83, 0xA1, 0xB8, 0x09, 0x00, 0x00, 0xFB};
// trap_obj_reraise (1/277): Healing Light's script call for the Sora revive. Playing Healing Light (motion 192) on
// our stand-in Mickey runs the script here too, 2.8 s in, while the winner's game took 7.7 s (bench 2026-10-21
// 07:12): so in the loser's game both this and Mickey's leave wait for the winner's word.
constexpr std::uintptr_t kTrapReraise = 0x42E1E0;
constexpr std::uint8_t kTrapReraiseBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x8B, 0x09, 0xE8};
using PFN_Trap = void(__fastcall*)(std::uint64_t*);
// The camera follows exe+0x718CB0; exe+0x164C60(actor, mode) sets it and restarts the follow (the player setup
// exe+0x3AA850 calls it, mode 0 in the field). Your report 2026-10-21: in the loser's game the camera went with the
// stand-in Mickey; it stays on the loser's own lying Sora instead.
constexpr std::uintptr_t kCameraTarget = 0x718CB0;
constexpr std::uintptr_t kCameraSet = 0x164C60;
constexpr std::uint8_t kCameraSetBytes[] = {0x48, 0x83, 0xEC, 0x28, 0x83, 0x3D};
// The HUD's player gauge shows entry 0 of exe+0xABCFE8; exe+0x168850(entry, actor) sets it and flags it changed. Your
// request 2026-10-21: the loser's HUD shows its own Sora, not the stand-in Mickey.
constexpr std::uintptr_t kHudParty = 0xABCFE8;
constexpr std::uintptr_t kHudSet = 0x168850;
constexpr std::uint8_t kHudSetBytes[] = {0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D};
// The command menu (YS::FIELD_COMMAND exe+0x2A10620) is rebuilt every frame by its tick (exe+0x3B2340 -> vtable+0x10 =
// exe+0x3F29E0) from its owner at +0xAE0, which Mickey's spawn makes Mickey. Your request 2026-10-21: the loser's menu
// shows its own Sora's commands, so the owner is our Sora during that build only (the menu's input stays Mickey's and
// is blocked anyway). Mickey's update also puts his triangle command in the menu's slot 1 every frame
// (exe+0x3B25C0(menu, slot, command, ...), exe+0x414EB0): dropped for our stand-in Mickey.
constexpr std::uintptr_t kMenuBuild = 0x3F29E0;
constexpr std::uint8_t kMenuBuildBytes[] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x91, 0xD8, 0x0B, 0x00, 0x00};
constexpr std::uintptr_t kMenuSlotSet = 0x3B25C0;
constexpr std::uint8_t kMenuSlotSetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
constexpr std::uintptr_t kMenuOwner = 0xAE0;
// What the loser saw (D-Charge / Jump / Pearl / Attack, bench screenshot 2026-10-21 08:30) is Mickey's own command set:
// his init (exe+0x414E30, vtable 1) calls exe+0x3DB140(actor, 1), which loads "comd" from the actor's files and puts
// it in the menu (exe+0x3B2A90, page +0x728; the build then skips the normal top commands). Skipped for our stand-in.
constexpr std::uintptr_t kCommandSet = 0x3DB140;
constexpr std::uint8_t kCommandSetBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
                                             0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20};
constexpr std::uintptr_t kPlayerPtr = 0x2A105D0;   // "the player": Mickey while he is out
constexpr std::uintptr_t kKeptSora = 0x2A11268;    // the Sora put aside while Mickey is the player
constexpr std::uintptr_t kObjRow = 0x918;          // actor -> object table row (u32 id, model name at +8)
constexpr std::uintptr_t kActorEntity = 0x640;
constexpr std::uintptr_t kEntityPos = 0x30, kEntitySin = 0x40, kEntityCos = 0x48, kEntityAngle = 0x4C;
constexpr std::uintptr_t kActorMotion = 0x180;     // u32 motion id
constexpr std::uintptr_t kActorMotionBank = 0x2B8; // != 0: the motion is from another file's set
constexpr std::uintptr_t kActorVelocity = 0xB98, kActorCarried = 0x690, kActorAccel = 0xA48;
constexpr std::uintptr_t kActorStats = 0x5C0;      // -> i32 HP, i32 max HP
constexpr std::uint16_t kStartButton = 0x0008;
constexpr std::uint32_t kPoseFreshMs = 500;        // older poses don't move our Mickey

constexpr std::uint32_t kMagic = 0x5A32484B;       // "KH2Z" ("KH2M" is motion_mirror.cpp's)
constexpr std::uint16_t kVersion = 1;
enum : std::uint8_t { kInfo = 0, kRoll = 1, kChoiceMade = 2, kRevived = 3, kMickeyDown = 4, kMickeyPose = 5 };
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
struct PosePacket {        // kMickeyPose, every frame while the sender's Mickey is out
    std::uint32_t magic;
    std::uint16_t version;
    std::uint32_t build;
    std::uint8_t type;
    std::uint32_t epoch;
    float pos[3];
    float angle;
    std::uint32_t motion;
    std::uint8_t otherBank;
    std::int32_t hp, maxHp;
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
using PFN_Actor = void(__fastcall*)(std::uintptr_t);
PFN_Actor g_realLeave = nullptr;    // hooked only with MICKEY_PUPPET
PFN_Actor g_realRevive = nullptr;   // the plain function (not hooked)
PFN_Trap g_realReraise = nullptr;
using PFN_MenuBuild = void(__fastcall*)(std::uintptr_t);
using PFN_MenuSlotSet = std::uintptr_t(__fastcall*)(std::uintptr_t, int, std::uint32_t, void*, std::uint32_t, int);
PFN_MenuBuild g_realMenuBuild = nullptr;
PFN_MenuSlotSet g_realMenuSlotSet = nullptr;
using PFN_CommandSet = void(__fastcall*)(std::uintptr_t, int);
PFN_CommandSet g_realCommandSet = nullptr;

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
bool g_theirMickey = false;        // loser: the other player's Mickey is out (from their choice to their 'revived')
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
// Step c, the loser's own Mickey, driven by the winner's.
bool g_puppetOn = true;            // MICKEY_PUPPET (default 1)
std::uintptr_t g_ourMickey = 0;    // loser: our game's Mickey while he stands in for the winner's
std::uintptr_t g_loserSora = 0;    // loser: our Sora, lying, while our Mickey is out
bool g_loserSpawning = false;      // loser: inside the Game Over update that spawns our stand-in Mickey
std::uintptr_t g_loserSpawnSora = 0;
PosePacket g_pose {};              // loser: the winner's Mickey, newest
std::uint32_t g_poseMs = 0;
std::uint32_t g_poses = 0, g_motions = 0, g_motionsMissing = 0;
std::uint32_t g_lastSetMotion = 0xFFFFFFFF;
// Loser, after our Mickey left: our Sora goes down (DownedMickeyHold, next frame), then this.
bool g_releaseAfterHold = false, g_reviveAfterHold = false, g_endAfterHold = false;
// Winner, from our Sora being the player again until Mickey leaves the actor list: the pose we send is our Sora's.
std::uintptr_t g_handBackSora = 0, g_handBackMickey = 0;
// Loser, from our Mickey's exit until downed.cpp holds our Sora: kept lying here. The exit starts the get-up (253) and
// the actor list's head (what downed.cpp follows) turns back to Sora ~4.8 s later (Twin Lords bench 2026-10-21 07:32).
std::uintptr_t g_lyingSora = 0;
constexpr int kLyingMotion = 252;

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

std::uint32_t ObjId(std::uintptr_t a) {
    __try {
        std::uintptr_t row = *reinterpret_cast<const volatile std::uintptr_t*>(a + kObjRow);
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? *reinterpret_cast<const volatile std::uint32_t*>(row) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* ModelOf(std::uintptr_t a) {
    __try {
        std::uintptr_t row = a ? *reinterpret_cast<const volatile std::uintptr_t*>(a + kObjRow) : 0;
        return row > ExeBase() && row < ExeBase() + 0x3000000 ? reinterpret_cast<const char*>(row + 8) : "?";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "?";
    }
}

// Loser: the camera on our own lying Sora, not on the stand-in Mickey.
bool g_cameraOk = false;  // the setter's bytes match
std::uint32_t g_cameraSets = 0;
bool g_hudOk = false;
std::uint32_t g_hudSets = 0;
void HudOnOurSora() {
    if (!g_hudOk || !g_loserSora || Read<std::uintptr_t>(kHudParty) == g_loserSora) return;
    __try {
        reinterpret_cast<void(__fastcall*)(int, std::uintptr_t)>(ExeBase() + kHudSet)(0, g_loserSora);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_hudOk = false;
        Log("mickey: exception setting the HUD on our Sora: left on Mickey");
        return;
    }
    if (g_hudSets++ < 5) Log("mickey: the HUD's player gauge on our own Sora (not on our stand-in Mickey)");
}

void CameraOnOurSora() {
    if (!g_cameraOk || !g_loserSora || Read<std::uintptr_t>(kCameraTarget) == g_loserSora) return;
    __try {
        reinterpret_cast<void(__fastcall*)(std::uintptr_t, int)>(ExeBase() + kCameraSet)(g_loserSora, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_cameraOk = false;
        Log("mickey: exception setting the camera on our Sora: left on Mickey");
        return;
    }
    if (g_cameraSets++ < 5) Log("mickey: the camera on our own Sora (not on our stand-in Mickey)");
}

// Our Mickey is gone (left, or the room went): the buttons are ours again.
void ForgetOurMickey() {
    if (g_ourMickey) {
        InputBlock(0);
        Log("mickey: our Mickey stand-in done (%u poses, %u motions set, %u not in his set)", g_poses, g_motions,
            g_motionsMissing);
    }
    g_ourMickey = 0;
    g_loserSora = 0;
    g_lastSetMotion = 0xFFFFFFFF;
}

void Reset(const char* why) {
    if (g_role != Role::None || g_rescue || g_holdPending) Log("mickey: reset (%s)", why);
    g_role = Role::None;
    g_rescue = false;
    g_theirMickey = false;
    g_rescueSora = 0;
    g_holdPending = false;
    g_releaseAfterHold = g_reviveAfterHold = g_endAfterHold = false;
    g_lyingSora = 0;
    g_handBackSora = g_handBackMickey = 0;
    g_skipSpawn = false;
    g_gaveUpObj = 0;
    ForgetOurMickey();
}

// The loser's Mickey leaves the game's way (Healing Light's exe+0x416100): the player back to our Sora. False if he
// already left (his own motion's script may have done it) or the call doesn't match this build.
bool OurMickeyLeaves(bool lastHp) {
    std::uintptr_t mickey = g_ourMickey;
    bool out = mickey && Read<std::uintptr_t>(kPlayerPtr) == mickey;
    ForgetOurMickey();
    if (!out) return false;
    auto leave = g_realLeave;
    if (lastHp && std::memcmp(reinterpret_cast<const void*>(ExeBase() + kMickeyLastHpLeave), kMickeyLastHpLeaveBytes,
                              sizeof(kMickeyLastHpLeaveBytes)) == 0)
        leave = reinterpret_cast<PFN_Actor>(ExeBase() + kMickeyLastHpLeave);
    if (!leave) {
        Log("mickey: Mickey's leave (exe+0x416100) isn't hooked (other build?): our Mickey stays");
        return false;
    }
    __try {
        leave(mickey);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("mickey: exception in Mickey's leave");
        return false;
    }
    g_handBackSora = Read<std::uintptr_t>(kPlayerPtr);  // our Sora; Mickey stays the list head a few seconds (your
    g_handBackMickey = mickey;                          // report 2026-10-21: the winner saw our copy spin meanwhile)
    Log("mickey: our Mickey left (the game's %s exit): the player is %s again",
        leave == g_realLeave ? "Healing Light" : "last-HP",
        Read<std::uintptr_t>(kPlayerPtr) == Head() ? "our Sora" : ModelOf(Read<std::uintptr_t>(kPlayerPtr)));
    return true;
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
        std::uintptr_t sora = Head();
        std::uint16_t counts[2] = {Read<std::uint16_t>(kAppearances), Read<std::uint16_t>(kAppearances + 2)};
        g_skipSpawn = !g_puppetOn;
        g_loserSpawning = g_puppetOn;
        g_loserSpawnSora = sora;
        std::uint64_t more = g_realUpdate(obj);  // resumes the world and ends the Game Over (+ Mickey's spawn)
        g_skipSpawn = false;
        g_loserSpawning = false;
        g_role = Role::None;
        g_rescue = true;
        g_theirMickey = true;
        std::uintptr_t player = Read<std::uintptr_t>(kPlayerPtr);
        if (g_puppetOn && player && player != sora) {
            // Only the winner's save counts this Mickey (the spawn raised ours too).
            auto* count = reinterpret_cast<std::uint16_t*>(ExeBase() + kAppearances);
            count[0] = counts[0];
            count[1] = counts[1];
            g_ourMickey = player;
            g_loserSora = Read<std::uintptr_t>(kKeptSora);
            g_poses = g_motions = g_motionsMissing = 0;
            g_lastSetMotion = 0xFFFFFFFF;
            InputBlock(static_cast<std::uint16_t>(~kStartButton));
            g_cameraSets = g_hudSets = 0;
            CameraOnOurSora();
            HudOnOurSora();
            Log("mickey: the other player chose 'I won't give up': back to the field with our own Mickey (%s, object %#x, "
                "exe+0x%llX) standing in for theirs; our Sora (%s) lies; our buttons blocked but Start", ModelOf(player),
                ObjId(player), static_cast<unsigned long long>(player - ExeBase()),
                g_loserSora == sora ? "kept by the game" : "NOT the one the game kept");
            return more;
        }
        if (g_puppetOn) Log("mickey: Mickey's spawn made no new player here: the step 2 way (no Mickey on our screen)");
        reinterpret_cast<PFN_FadeIn>(ExeBase() + kFadeIn)(ExeBase() + kFade, kFadeInFrames, 1);
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

// After our Mickey's exit, when our Sora stays down: back to lying at once (the exit started its get-up).
void KeepLying(std::uintptr_t sora) {
    if (!sora || !PuppetHasMotion(sora, kLyingMotion)) return;
    g_lyingSora = sora;
    PuppetSetMotion(sora, kLyingMotion);
    InputBlock(static_cast<std::uint16_t>(~kStartButton));  // until the down's own block
    Log("mickey: our Sora kept lying (motion %d) until the down takes over", kLyingMotion);
}

// The game's own Healing Light exit on our stand-in Mickey waits for the winner's (kRevived runs it).
std::uint32_t g_heldExits = 0;
void __fastcall HookLeave(std::uintptr_t mickey) {
    if (g_ourMickey && mickey == g_ourMickey) {
        if (g_heldExits++ < 5) Log("mickey: our Mickey's Healing Light exit held: it runs when the other player's Sora is up");
        return;
    }
    g_realLeave(mickey);
}

// The loser's command menu: built for our own Sora while our Mickey owns it; his slot 1 command dropped.
std::uint32_t g_menuBuilds = 0, g_menuSlotsDropped = 0;
void __fastcall HookMenuBuild(std::uintptr_t menu) {
    auto& owner = *reinterpret_cast<std::uintptr_t*>(menu + kMenuOwner);
    if (!g_ourMickey || !g_loserSora || owner != g_ourMickey) {
        g_realMenuBuild(menu);
        return;
    }
    owner = g_loserSora;
    g_realMenuBuild(menu);
    owner = g_ourMickey;
    if (g_menuBuilds++ == 0) Log("mickey: the command menu built for our own Sora (not our stand-in Mickey)");
}

std::uint32_t g_commandSetsSkipped = 0;
void __fastcall HookCommandSet(std::uintptr_t actor, int mode) {
    if ((g_ourMickey && actor == g_ourMickey) || (g_loserSpawning && actor != g_loserSpawnSora)) {
        if (g_commandSetsSkipped++ < 3)
            Log("mickey: our stand-in Mickey's own command set skipped (%s): the menu stays our Sora's",
                g_ourMickey ? "his first update" : "in his spawn");
        return;
    }
    g_realCommandSet(actor, mode);
}

std::uintptr_t __fastcall HookMenuSlotSet(std::uintptr_t menu, int slot, std::uint32_t command, void* source,
                                          std::uint32_t a5, int priority) {
    if (g_ourMickey && *reinterpret_cast<std::uintptr_t*>(menu + kMenuOwner) == g_ourMickey) {
        if (g_menuSlotsDropped++ == 0) Log("mickey: our Mickey's menu slot %d command %u dropped", slot, command);
        return 0;
    }
    return g_realMenuSlotSet(menu, slot, command, source, a5, priority);
}

void __fastcall HookReraise(std::uint64_t* args) {
    if (g_ourMickey) {
        if (g_heldExits++ < 5) Log("mickey: Healing Light's Sora revive (trap_obj_reraise) held: it runs with our Mickey's exit");
        return;
    }
    g_realReraise(args);
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
        g_theirMickey = false;
        if (g_ourMickey) {  // our Mickey stands in: he leaves first, then our Sora as the winner's
            std::uintptr_t sora = g_loserSora;
            bool left = OurMickeyLeaves(p.value == kByLastHp);
            bool up = static_cast<std::int32_t>(Read<std::uint32_t>(kSoraHp)) > 0;
            if (p.value == kByLastHp) {
                g_holdPending = true;  // down as in step 2 (once our Sora is the list head again), timer on
                g_releaseAfterHold = true;
                if (left) KeepLying(sora);
            } else if (!left) {
                // He wasn't the player any more: his own Healing Light motion may have run the game's exit here.
                Log("mickey: Healing Light: our Mickey had already left (our Sora HP %d)%s", static_cast<int>(
                    Read<std::uint32_t>(kSoraHp)), up ? "" : ": we get up as in step 2");
                if (!up) {
                    g_holdPending = true;
                    g_reviveAfterHold = true;
                    KeepLying(sora);
                }
            } else if (sora && g_realRevive) {
                __try {
                    g_realRevive(sora);  // what trap_obj_reraise does
                    Log("mickey: Healing Light: our Sora revived the game's way (trap_obj_reraise), HP %d",
                        static_cast<int>(Read<std::uint32_t>(kSoraHp)));
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    Log("mickey: exception in the Sora revive");
                }
            }
            __try {
                reinterpret_cast<PFN_Void>(ExeBase() + kBringBack)();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log("mickey: bringing our allies back failed (exception)");
            }
            Log(p.value == kByLastHp ? "mickey: their Mickey's last HP: our Mickey left, we stay down (usual timer), allies back"
                                     : "mickey: Healing Light: our Mickey left, we get up, allies back");
            break;
        }
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
        if (g_ourMickey) {  // our Mickey leaves, then our Sora dies as in step 2 (the normal Game Over)
            std::uintptr_t sora = g_loserSora;
            if (OurMickeyLeaves(true)) KeepLying(sora);
            g_holdPending = true;
            g_endAfterHold = true;
            Log("mickey: the other player's Mickey is down: our Mickey leaves, then our Game Over too");
            break;
        }
        DownedMickeyEnd();
        Log("mickey: the other player's Mickey is down: our Game Over too");
        break;
    }
}

}  // namespace

std::uintptr_t MickeyRescueSora() {
    if (!g_on || !g_rescue) return 0;
    if (g_ourMickey && g_loserSora) return Head() != g_loserSora ? g_loserSora : 0;  // the loser's own Mickey is out
    if (!g_rescueSora) return 0;
    return Head() != g_rescueSora ? g_rescueSora : 0;
}

std::uintptr_t MickeyHandBackSora() {
    return g_on && g_handBackSora && Head() == g_handBackMickey ? g_handBackSora : 0;
}

// After each of our Mickey's updates: the winner's Mickey's spot, facing and motion.
void MickeyAfterUpdate(std::uintptr_t actor) {
    if (!g_ourMickey || actor != g_ourMickey || AvatarLinkNowMs() - g_poseMs > kPoseFreshMs) return;
    const PosePacket& p = g_pose;
    std::uintptr_t entity = actor + kActorEntity;
    auto* pos = reinterpret_cast<float*>(entity + kEntityPos);
    float dx = p.pos[0] - pos[0], dy = p.pos[1] - pos[1], dz = p.pos[2] - pos[2];
    float t = dx * dx + dy * dy + dz * dz > 300.0f * 300.0f ? 1.0f : 0.5f;  // far (first frame): at once, else smoothed
    pos[0] += dx * t;
    pos[1] += dy * t;
    pos[2] += dz * t;
    *reinterpret_cast<float*>(entity + kEntityAngle) = p.angle;
    *reinterpret_cast<float*>(entity + kEntitySin) = std::sin(p.angle);
    *reinterpret_cast<float*>(entity + kEntityCos) = std::cos(p.angle);
    std::memset(reinterpret_cast<void*>(actor + kActorVelocity), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorCarried), 0, 3 * sizeof(float));
    std::memset(reinterpret_cast<void*>(actor + kActorAccel), 0, 0x18);
    if (p.motion == g_lastSetMotion) return;
    g_lastSetMotion = p.motion;
    if (p.otherBank || !PuppetHasMotion(actor, static_cast<int>(p.motion))) {
        if (g_motionsMissing++ < 20)
            Log("mickey: their Mickey's motion %u isn't in our Mickey's set%s: not played", p.motion,
                p.otherBank ? " (from another file)" : "");
        return;
    }
    PuppetSetMotion(actor, static_cast<int>(p.motion));
    if (g_motions++ < 60) Log("mickey: our Mickey plays their Mickey's motion %u", p.motion);
}

bool MickeyStandIn(std::uintptr_t actor) { return actor && (actor == g_ourMickey || actor == g_lyingSora); }

bool MickeyTheirsOut() { return g_on && g_theirMickey; }

void MickeySyncInit(bool host) {
    if (EnvInt("KH2COOP_MICKEY_SYNC", 1) != 1) return;
    g_host = host;
    g_puppetOn = EnvInt("KH2COOP_MICKEY_PUPPET", 1) == 1;
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
    if (g_on && g_puppetOn) {
        if (same(kSoraRevive, kSoraReviveBytes, sizeof(kSoraReviveBytes)))
            g_realRevive = reinterpret_cast<PFN_Actor>(ExeBase() + kSoraRevive);
        g_cameraOk = same(kCameraSet, kCameraSetBytes, sizeof(kCameraSetBytes));
        g_hudOk = same(kHudSet, kHudSetBytes, sizeof(kHudSetBytes));
        if (!g_hudOk) Log("mickey sync: the HUD setter doesn't match this build: the loser's HUD shows Mickey");
        if (!g_cameraOk) Log("mickey sync: the camera setter doesn't match this build: the loser's camera stays on Mickey");
        bool hooked = g_realRevive &&
                      HookFunction(kMickeyLeave, kMickeyLeaveBytes, sizeof(kMickeyLeaveBytes), reinterpret_cast<void*>(HookLeave),
                                   reinterpret_cast<void**>(&g_realLeave), "Mickey's Healing Light exit") &&
                      HookFunction(kTrapReraise, kTrapReraiseBytes, sizeof(kTrapReraiseBytes),
                                   reinterpret_cast<void*>(HookReraise), reinterpret_cast<void**>(&g_realReraise),
                                   "trap_obj_reraise (Healing Light's revive)");
        if (hooked && !(HookFunction(kCommandSet, kCommandSetBytes, sizeof(kCommandSetBytes), reinterpret_cast<void*>(HookCommandSet),
                                     reinterpret_cast<void**>(&g_realCommandSet), "an actor's own command set") &&
                        HookFunction(kMenuBuild, kMenuBuildBytes, sizeof(kMenuBuildBytes), reinterpret_cast<void*>(HookMenuBuild),
                                     reinterpret_cast<void**>(&g_realMenuBuild), "command menu build") &&
                        HookFunction(kMenuSlotSet, kMenuSlotSetBytes, sizeof(kMenuSlotSetBytes),
                                     reinterpret_cast<void*>(HookMenuSlotSet), reinterpret_cast<void**>(&g_realMenuSlotSet),
                                     "command menu slot")))
            Log("mickey sync: the command menu hooks don't match this build: the loser's menu shows Mickey's");
        if (!hooked) {
            g_puppetOn = false;
            Log("mickey sync: Mickey's exit / the Sora revive don't match this build: no Mickey in the loser's game");
        }
    }
    Log("mickey sync: %s%s", g_on ? "on (one roll for both; the winner plays Mickey, the other player waits down)"
                                  : "OFF: a hook could not be installed",
        g_on ? (g_puppetOn ? "; the loser's game shows its own Mickey, driven by the winner's" : "; MICKEY_PUPPET=0: no Mickey "
                                                                                               "in the loser's game") : "");
}

void MickeySyncOnPacket(const char* buf, int n) {
    if (g_on && n == static_cast<int>(sizeof(PosePacket))) {
        PosePacket p;
        std::memcpy(&p, buf, sizeof(p));
        if (p.version != kVersion || p.build != AvatarLinkBuild() || p.type != kMickeyPose) return;
        g_pose = p;
        g_poseMs = AvatarLinkNowMs();
        if (g_ourMickey && g_poses++ == 0)
            Log("mickey: first pose of their Mickey: (%.0f, %.0f, %.0f), motion %u, HP %d/%d", p.pos[0], p.pos[1],
                p.pos[2], p.motion, p.hp, p.maxHp);
        return;
    }
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
        if (g_holdPending && Read<std::uint8_t>(kInField) && Head() && Head() == Read<std::uintptr_t>(kPlayerPtr)) {
            g_holdPending = false;
            g_lyingSora = 0;
            DownedMickeyHold();
            if (g_releaseAfterHold) DownedMickeyRelease();
            if (g_reviveAfterHold) DownedMickeyRevive();
            if (g_endAfterHold) DownedMickeyEnd();
            g_releaseAfterHold = g_reviveAfterHold = g_endAfterHold = false;
        }
        if (g_ourMickey && (Read<std::uintptr_t>(kPlayerPtr) != g_ourMickey || !g_rescue)) {
            Log("mickey: our Mickey is no longer the player (player now %s): stand-in over",
                ModelOf(Read<std::uintptr_t>(kPlayerPtr)));
            ForgetOurMickey();
        }
        if (g_ourMickey) {  // again if anything gave them back to Mickey
            CameraOnOurSora();
            HudOnOurSora();
        }
        // The winner: our Mickey's pose for the other game's Mickey.
        if (g_rescue && g_rescueSora && Head() && Head() != g_rescueSora && !GameOverObj()) {
            std::uintptr_t m = Head();
            PosePacket p {kMagic, kVersion, AvatarLinkBuild(), kMickeyPose, g_goEpoch, {}, 0.0f, 0, 0, 0, 0};
            std::uintptr_t entity = m + kActorEntity;
            for (int i = 0; i < 3; ++i) p.pos[i] = *reinterpret_cast<const volatile float*>(entity + kEntityPos + 4 * i);
            p.angle = *reinterpret_cast<const volatile float*>(entity + kEntityAngle);
            p.motion = *reinterpret_cast<const volatile std::uint32_t*>(m + kActorMotion);
            p.otherBank = *reinterpret_cast<const volatile std::uintptr_t*>(m + kActorMotionBank) ? 1 : 0;
            std::uintptr_t stats = *reinterpret_cast<const volatile std::uintptr_t*>(m + kActorStats);
            if (stats) {
                p.hp = *reinterpret_cast<const volatile std::int32_t*>(stats);
                p.maxHp = *reinterpret_cast<const volatile std::int32_t*>(stats + 4);
            }
            AvatarLinkSendRaw(&p, sizeof(p));
        }
        // The winner: our Sora is the player again. Healing Light gives full HP; Mickey's last HP (a hit that would
        // KO him leaves 1) gets Sora up with half (bench 2026-10-18 01:50, motion 254). By the player pointer, which
        // Mickey's exit sets in the frame our get-up starts, so the other player's get-up starts with ours (your request
        // 2026-10-21): the list head only turns back at the get-up's end (4.8 s later, bench 07:24:48.5 -> 53.3).
        if (g_handBackSora && Head() != g_handBackMickey) g_handBackSora = g_handBackMickey = 0;  // Mickey's gone
        std::int32_t hp = static_cast<std::int32_t>(Read<std::uint32_t>(kSoraHp));
        if (g_rescue && g_rescueSora && !GameOverObj() && Read<std::uintptr_t>(kPlayerPtr) == g_rescueSora && hp > 0) {
            if (Head() != g_rescueSora) {  // until the list head is our Sora again, the pose we send stays our Sora's
                g_handBackSora = g_rescueSora;
                g_handBackMickey = Head();
            }
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
