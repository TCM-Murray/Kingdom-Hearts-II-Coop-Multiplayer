@echo off
rem Starts this PC's game as the co-op HOST.
rem How the friend connects: CONNECT=steam = through Steam (they use Join Game on you in the
rem Steam friends list once co-op is on); CONNECT=local = over Radmin VPN or a local network (their PEER=local, LOCAL_IP = your Radmin IP).
set CONNECT=steam
rem Double-click this file, or run it from a terminal. Close the game normally when done.
rem Settings: same co-op features as the friend's kh2coop.ini (both PCs must match).
rem Shared spawning, the downed rule, LOAD_BARRIER (both games leave loads together) and
rem AI_NEAREST_PLAYER (far enemies near the friend stay)
rem are on by default and need no line here.
rem Your own settings (how long you stay down, Steam achievements) are in kh2coop_player.ini
rem next to "KINGDOM HEARTS II FINAL MIX.exe"; the mod writes it on the first start.
cd /d "%~dp0"
set KH2COOP_INTERP_MS=50
set KH2COOP_PAUSE_SYNC=1
set KH2COOP_WORLD_SYNC=1
set KH2COOP_COPY_AGGRO=1
set KH2COOP_CLONE_TARGET=1
set KH2COOP_PUPPET_COLLIDE=1
rem The other player's ship in Gummi missions (a test so far): both PCs need it (GUMMI_SHIP_TEST=1 in the friend's kh2coop.ini).
set KH2COOP_GUMMI_SHIP_TEST=1
rem The other player's Limit effects (a test so far): its motion triggers, its script's effects and their moves
rem (Duck Flare's rockets, Knocksmash's shield; 0 if they stutter). Both PCs need them (LIMIT_FX_TRIGGERS=1,
rem LIMIT_FX_SCRIPT=1 and LIMIT_FX_MOVES=1 in the friend's kh2coop.ini).
set KH2COOP_LIMIT_FX_TRIGGERS=1
set KH2COOP_LIMIT_FX_SCRIPT=1
set KH2COOP_LIMIT_FX_MOVES=1
rem The other player's Drive Form effects (a test so far): from the form's own file, the Revert burst and the form's
rem appear effects, attacks started again, Wisdom's shots. Both PCs need them (the same four lines in kh2coop.ini).
set KH2COOP_FORM_LOOK_FX=1
set KH2COOP_FORM_LOOK_BURSTS=1
set KH2COOP_PUPPET_RESTART=1
set KH2COOP_FORM_FX_SHOTS=1
rem The other player's summon as an actor in this game (a test so far): it follows theirs and leaves with it. Both PCs
rem need it (SUMMON_SYNC=1 in the friend's kh2coop.ini).
set KH2COOP_SUMMON_SYNC=1
rem A room's enemy limit per spawn point (on since 2026-11-20): 1 = the room's Capacity counts for each spawn group by itself,
rem so a second group no longer waits for the first one's enemies to die; 0 = the game's own limit. Both PCs need the same
rem (NO_ENEMY_LIMIT in the friend's kh2coop.ini).
set KH2COOP_NO_ENEMY_LIMIT=1
rem Agrabah's carpet missions (a test so far, TODO 5.36): the other player is a real Sora copy on their own carpet;
rem the Light Cycle and the sidecar keep the stand-in. Both PCs need it (DIRECT_COPY=1 in the friend's kh2coop.ini).
set KH2COOP_DIRECT_COPY=1
rem The test logs (the parity sampler for tools\parity_compare.py, script calls, effects, enemy traces, claims, grabs,
rem heals, aggro) follow DEV_LOGS in kh2coop_player.ini: 1 = on (the default while we test), 0 = play mode, small logs.
rem One alone: set KH2COOP_PARITY / FX_LOG / TRAP_LOG / ENEMY_TRACE / CLAIM_LOG / GRAB_TRACE / HEAL_TRACE / STAR_LOG /
rem AGGRO_LOG to 1 or 0 here.
rem Saves: LOAD (CO-OP) on the title = the co-op save file ("KH2 Coop saves" next to the game exe),
rem NEW GAME / LOAD = your normal Steam save with the mod off. (Our tests set SAVE_GUARD=1 instead.)
set KH2COOP_SAVE_GUARD=0
rem --single: one game copy; --steam: friends join through Steam; --host-auto (Radmin): learn
rem the friend's address from its first packet; --pad-a 0: your controller in XInput slot 0.
if /i "%CONNECT%"=="local" (
    py tools\launch_pair.py --single --host-auto --pad-a 0
) else (
    py tools\launch_pair.py --single --steam --pad-a 0
)
pause
