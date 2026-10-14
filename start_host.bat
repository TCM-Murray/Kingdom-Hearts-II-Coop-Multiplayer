@echo off
rem Starts this PC's game as the co-op HOST.
rem How the friend connects: CONNECT=steam = through Steam (they use Join Game on you in the
rem Steam friends list once co-op is on); CONNECT=local = over Radmin VPN or a local network (their PEER=local, LOCAL_IP = your Radmin IP).
set CONNECT=local
rem Double-click this file, or run it from a terminal. Close the game normally when done.
rem Settings: same co-op features as the friend's kh2coop.ini (both PCs must match).
rem Shared spawning, the downed rule, LOAD_BARRIER (both games leave loads together) and
rem AI_NEAREST_PLAYER (far enemies near the friend stay)
rem are on by default and need no line here.
rem Your own settings (how long you stay down, Steam achievements) are in kh2coop_player.ini
rem next to "KINGDOM HEARTS II FINAL MIX.exe"; the mod writes it on the first start.
cd /d "%~dp0"
set KH2COOP_PAUSE_SYNC=1
set KH2COOP_WORLD_SYNC=1
set KH2COOP_AGGRO_LOG=1
set KH2COOP_COPY_AGGRO=1
set KH2COOP_CLONE_TARGET=1
set KH2COOP_PUPPET_COLLIDE=1
rem Extra copy (test of the extra-copy branch): the other player's Sora is spawned beside Donald and Goofy
rem (or the world's allies) instead of taking a party spot. Must match EXTRA_COPY in the friend's kh2coop.ini.
set KH2COOP_EXTRA_COPY=1
rem One brain (test, TODO 1.55): every enemy in the friend's game copies yours instead of thinking.
rem Must match ONE_BRAIN in the friend's kh2coop.ini (on by default; 0 = off).
set KH2COOP_ONE_BRAIN=1
rem Parity sampler (test): one line per enemy every 250 ms in the log, for tools\parity_compare.py.
set KH2COOP_PARITY=1
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
