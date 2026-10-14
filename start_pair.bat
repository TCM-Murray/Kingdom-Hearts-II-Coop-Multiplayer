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
set KH2COOP_DEBUG=1
rem The Sora copy joins beside Donald and Goofy instead of taking Donald's place, and one brain
rem (the host's) runs the enemies: the same as start_host.bat and the friend package.
set KH2COOP_EXTRA_COPY=1
set KH2COOP_ONE_BRAIN=1
rem Both copies start windowed (the game reads a windowed copy of its settings; your real settings file is untouched).
set KH2COOP_DISPLAY_MODE=2
rem Main monitor (2560x1440): host on the left half, friend on the right half.
py tools\launch_pair.py --host-auto --pad-a 0 --pad-b 1 --monitor-x 0 --monitor-y 0 --width 1280 --height 760
pause
