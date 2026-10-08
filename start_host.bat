@echo off
rem Starts this PC's game as the co-op HOST (the friend connects to it over Radmin VPN).
rem Double-click this file, or run it from a terminal. Close the game normally when done.
rem Settings: same co-op features as the friend's kh2coop.ini (both PCs must match).
rem Shared spawning and the downed rule are on by default and need no line here.
rem Your own settings (how long you stay down, Steam achievements) are in kh2coop_player.ini
rem next to "KINGDOM HEARTS II FINAL MIX.exe"; the mod writes it on the first start.
cd /d "%~dp0"
set KH2COOP_PAUSE_SYNC=1
set KH2COOP_WORLD_SYNC=1
set KH2COOP_AGGRO_LOG=1
set KH2COOP_COPY_AGGRO=1
set KH2COOP_CLONE_TARGET=1
set KH2COOP_PUPPET_COLLIDE=1
rem --single: one game copy; --host-auto: learn the friend's address from its first packet;
rem --pad-a 0: your controller in XInput slot 0.
py tools\launch_pair.py --single --host-auto --pad-a 0
pause
