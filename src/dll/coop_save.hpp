#pragma once
// The co-op save file (TODO 5.7, see coop_save.cpp): LOAD (CO-OP) lists and
// writes it, NEW GAME / LOAD the player's normal Steam save.

namespace kh2coop {

void CoopSaveInit();  // from OnInit, before TitleMenuInit

// From the title's confirm: true = use the co-op save file from now on (opens it the
// first time, starting it as a copy of the Steam save); false = the normal Steam save.
// Returns false if the co-op save file can't be used (the reason is logged).
bool CoopSaveSelect(bool coop);

}  // namespace kh2coop
