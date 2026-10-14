# Effect pool peaks per room

The most of the game's effect pool each room has used, as logged by the DLL (`src/dll/fx_pool.cpp`, TODO 1.59).
The game's own pool is 512 KB; since session 34 co-op gives it 4 MB (`FX_POOL_KB` in kh2coop.ini). When a pool is
full, new particles fail and some effects crash the game (`exe+0x4B00DA`, `exe+0x4D3A84`; VERIFIED_OFFSETS).

- Each peak is the bytes in use at the busiest moment in that room (with the pool's 16-byte block headers), in
  KB, then the size of the pool it was seen in. **Bold** = more than 512 KB, more than the game's own pool holds.
- Host and friend are kept apart: each game has its own pool (the friend's game also shows the host's effects,
  and the other way around).
- Programs = the room's map / battle / event programs (`exe+0x717008`); the same room in another story step is
  another row.
- Filled in by `py tools/pool_peaks.py [--source two-PC] <logs>` (keeps the highest ever seen per row and the
  names written here; edit the Name column by hand as you like).

| Room | Programs | Name | Host | Friend |
|---|---|---|---|---|
| 0x02/0x14 | 213/213/213 | Data Axel (B_EX110_LV99) | 166 of 4096 (bench, one brain 2026-10-13) | 176 of 4096 (bench, one brain 2026-10-13) |
| 0x04/0x04 | 114/114/114 | Data Demyx (B_EX120_HB_LV99) | 72 of 4096 (bench, two brains 2026-10-13) | 65 of 4096 (bench, one brain 2026-10-13) |
| 0x04/0x21 | 143/143/143 | Larxene, 1065 HP (B_EX400) | 129 of 4096 (bench, one brain 2026-10-14) | 77 of 4096 (bench, one brain 2026-10-14) |
| 0x04/0x21 | 148/148/148 | Data Larxene, 2715 HP (B_EX400) | 128 of 4096 (bench, one brain 2026-10-14) | 107 of 4096 (bench, one brain 2026-10-14) |
| 0x04/0x21 | 155/155/155 | Larxene, 1065 HP (B_EX400) | 108 of 4096 (bench, one brain 2026-10-14) | 48 of 4096 (bench, one brain 2026-10-14) |
| 0x04/0x22 | 151/151/151 | B_EX370 (Marluxia?) with B_EX380 and B_EX410, 708 HP | 192 of 4096 (bench, one brain 2026-10-14) | 48 of 4096 (bench, one brain 2026-10-14) |
| 0x04/0x22 | 152/152/152 | B_EX370 (Marluxia?) with B_EX380 and B_EX410, 2172 HP | 185 of 4096 (bench, one brain 2026-10-14) | 56 of 4096 (bench, one brain 2026-10-14) |
| 0x05/0x0F | 97/97/97 | Data Xaldin (B_EX130_LV99) | 139 of 4096 (bench, two brains 2026-10-13) | 138 of 4096 (bench, two brains 2026-10-13) |
| 0x0C/0x07 | 67/67/67 | B_EX420 (Lingering Will?), 3400 HP | 78 of 4096 (bench, one brain 2026-10-14) | 81 of 4096 (bench, one brain 2026-10-14) |
| 0x0C/0x07 | 73/73/73 | B_EX420 (Lingering Will?), 3400 HP | 71 of 4096 (bench, one brain 2026-10-14) | 62 of 4096 (bench, one brain 2026-10-14) |
| 0x12/0x0A | 100/100/100 | Data Xigbar (B_EX140_LV99) | 70 of 4096 (bench, two brains 2026-10-13) | 85 of 4096 (bench, two brains 2026-10-13) |
| 0x12/0x0E | 101/101/101 | Data Saix (B_EX150_LV99, claymores B_EX210) | 122 of 4096 (bench, one brain 2026-10-14) | 93 of 4096 (bench, one brain 2026-10-14) |
| 0x12/0x0F | 102/102/102 | Data Luxord (B_EX160_LV99, cards B_EX220_LV99) | 133 of 4096 (bench, one brain 2026-10-14) | 102 of 4096 (bench, one brain 2026-10-14) |
| 0x12/0x12 | 4/0/4 | The World That Never Was | 43 of 4096 (bench, two brains 2026-10-13) | 48 of 4096 (bench, two brains 2026-10-13) |
| 0x12/0x13 | 97/97/97 | Data Xemnas (B_EX170_LV99) | 114 of 4096 (bench, one brain 2026-10-14) | 115 of 4096 (bench, one brain 2026-10-14) |
| 0x12/0x14 | 98/98/98 | Data Xemnas, last part (B_EX170_LAST_LV99) | 122 of 4096 (bench, one brain 2026-10-14) | 136 of 4096 (bench, one brain 2026-10-14) |
| 0x12/0x15 | 99/99/99 | Data Roxas (B_EX390) | 186 of 4096 (bench, one brain 2026-10-13) | **1670** of 4096 (bench, two brains 2026-10-13) |
