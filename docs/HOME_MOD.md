# New home panel (mod v1)

Replaces the bottom half of the home screen (the stock MAIN VOLUME mixer page) with:

```
 [ R1 ][ R2 ][ R3 ] | [VOLUME][BRIGHT][REVERB][ ---- ][ ---- ]
  LCD1  LCD2  LCD3     LCD4    LCD5    LCD6    LCD7    LCD8
```
- LCD 1-3 (UP or DOWN): select the part. The part cards show the selected control's
  value for each part.
- LCD 4-6: select the control. The control cards show the selected part's values.
- DATA DIAL: changes the selected control of the selected part (0-127). Only while this
  panel is showing; on every other screen the dial works exactly as before (tempo on the
  other mixer pages, lists, menus...).
- LCD 6 DOWN: reverb off for the selected part; again: back on at the previous level
  (40 if it was never on).
- Header strip shows the selection ("R2  REVERB"). LCD 7-8 are free for later.
- Other volumes (SONG, ACMP, M.PAD, LEFT, MIC): Mixing Console VOL/EQ page (unchanged).
- The top half (LEFT/SONG/STYLE/R1-R3 boxes, BPM/TRP/OCT bar) is still stock (phase 2).

## How it is built
`mod/home/hp.c` (C, ~4 KB with fonts) compiled for SH-2 with the SH-4 cross compiler
(`apt install gcc-sh4-linux-gnu`; `build.py` rejects SH-3/SH-4-only instructions and any
library call). Code + Inter fonts live in the unused 0xFF area at 0x3EE400.

Hooks (all listed with original bytes in `mod/home/build/patches.json`):
| Where | What |
|---|---|
| event table 0x3B1538.. (16 entries) | LCD 1-8 UP/DOWN -> `hp_step`, auto-repeat off |
| event table 0x3B14B0.. (8 pairs) | UP+DOWN together -> `hp_select` (does nothing) |
| page-1 callbacks 0x3B1488.. (8) | "value changed" -> `hp_changed` (redraw) |
| 0x6D45C (stock tempo/dial handler) | -> `hp_dial`; trampoline re-runs the original |
| 0x24546 + literal 0x24574 (end of UI flush) | -> `hp_tick`: original tail, then redraw if the panel area was overdrawn or a value changed |

Facts used: RAM 0x10BA718 = active button table (0x3B152C = MAIN VOLUME page has the
buttons, i.e. home screen visible); framebuffer 0x10C8AEC (40 bytes/row, bit 7 = left,
1 = dark); 0x10BF60(fb, rect{x,y,w,h}) sends a rectangle to the LCD; parameters posted with
0x25CE4(2, id, 2, [part, value]) (id 5 volume, 10 brightness, 11 reverb send). No spare RAM
is known, so the state lives in the unused DMA channel 3 registers (SAR3/DAR3/DMATCR3).

## Testing
- Emulator, in memory: `-autoboot_script mod/home/build/apply.lua` (Windows:
  START_PSR9000_NEW_HOME.bat). Tested: selection, dial, reverb toggle, redraw after
  FUNCTION/MEMORY popup, dial still = tempo on other mixer pages.
- Brightness cannot be checked in the emulator: the firmware reads it back from the sound
  board, which is faked (always 0). It sends CC74 correctly.
- Real image: `python3 mod/home/make_image.py` -> `firmware/mod/p9000m.002-.004` with Rom
  Chk1 and the byte sum fixed (only .002 and .004 change). Boots in the emulator from the
  files alone. NOT tested on the keyboard; follow docs/RECOVERY.md first.

## Known limits / ideas
- Drawing is pixel-by-pixel for text; fast enough in the emulator, check feel on hardware.
- Part on/off state is not shown yet (phase 2 with the top half).
- The dial no longer changes tempo on the home screen (use TAP TEMPO, or slot 7 could become
  TEMPO).
