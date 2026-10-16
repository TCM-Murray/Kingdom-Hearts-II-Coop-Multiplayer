#pragma once
// Diagnostic panel (your request 2026-10-16): the connection's and co-op's state in real time on the middle left of
// the game's window, in a small see-through window of ours on top of it (not drawn inside the game's picture, so it
// shows with the game windowed or borderless, not in exclusive fullscreen). F3 shows / hides it; DIAG_HUD=1 in
// kh2coop_player.ini shows it from the start.

namespace kh2coop {

void DiagHudInit();   // from OnInit: reads DIAG_HUD, starts the panel's own thread
void DiagHudFrame();  // from OnFrame (game thread): gathers the numbers a few times a second

}  // namespace kh2coop
