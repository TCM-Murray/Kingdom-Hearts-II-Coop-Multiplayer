#pragma once
// Aggro log: who hits enemies and whom SetTarget picks (KH2COOP_AGGRO_LOG=1).

namespace kh2coop {

void AggroInit();   // from OnInit
void AggroFrame();  // from OnFrame

}  // namespace kh2coop
