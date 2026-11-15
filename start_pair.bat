@echo off
rem Starts TWO copies of the game on this PC for a local co-op test with two controllers:
rem left window = host (controller in XInput slot 0), right window = friend (slot 1).
rem Since Steam's update of 2026-10-14 the game only sees Steam's controllers: slot 0 and 1 (the order is Steam's;
rem the PS4 controller came first). Before, a real Xbox pad was slot 0 and the PS4 one slot 2.
rem Same co-op settings as start_host.bat. KH2COOP_DEBUG=1 lets Claude send test commands
rem (for example knocking a player down) while you play.
rem Double-click this file, or run it from a terminal. Close both games normally when done.
cd /d "%~dp0"
set KH2COOP_PAUSE_SYNC=1
set KH2COOP_WORLD_SYNC=1
set KH2COOP_COPY_AGGRO=1
set KH2COOP_CLONE_TARGET=1
set KH2COOP_PUPPET_COLLIDE=1
rem The other player's Limit and Drive Form effects, as start_host.bat sets them (tests so far): without these the copy
rem shows no Limit script effects, no form effects, no repeated attack motions, no Wisdom shots or Limit Form Limits.
set KH2COOP_LIMIT_FX_TRIGGERS=1
set KH2COOP_LIMIT_FX_SCRIPT=1
set KH2COOP_LIMIT_FX_MOVES=1
set KH2COOP_FORM_LOOK_FX=1
set KH2COOP_FORM_LOOK_BURSTS=1
set KH2COOP_PUPPET_RESTART=1
set KH2COOP_FORM_FX_SHOTS=1
rem The other player's summon as an actor in this game (a test so far): it follows theirs and leaves with it. Both PCs
rem need it (SUMMON_SYNC=1 in the friend's kh2coop.ini).
set KH2COOP_SUMMON_SYNC=1
set KH2COOP_DEBUG=1
rem Both copies start windowed (the game reads a windowed copy of its settings; your real settings file is untouched).
set KH2COOP_DISPLAY_MODE=2
rem Dev tool: the "Warp key" window (minimised in the taskbar) lists every room and fight; pick one there, then Delete
rem with a game window in front warps the host to it (the friend follows). It starts on Timeless River's steamboat
rem fight, keeps your last pick, and closes with the games. Load a save of that world first. Delete the line to turn
rem it off; another key: tools\warp_key.py 27701 f9 (see the file's first lines).
start "" pyw tools\warp_key.py 27701 delete
rem Main monitor (2560x1440): host on the left half, friend on the right half.
py tools\launch_pair.py --host-auto --pad-a 0 --pad-b 1 --monitor-x 0 --monitor-y 0 --width 1280 --height 760
pause
