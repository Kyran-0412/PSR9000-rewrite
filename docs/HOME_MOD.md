# New home screen (mod v5: the agreed design, mockups/home_v6.py)

Draws the whole home screen like the v3 mockup:
- status line: style name, (BPM) (TRP) (OCT) pills;
- R1/R2/R3 layer cards: voice name, VOL/BRT; filled card = part on, dashed = off; arrow at
  the selected part; LEFT voice line below;
- bottom row: PART pills R1/R2/R3 (LCD 1-3) with ON/OFF and short voice name; header
  "R1  VOICE NAME"; controls VOLUME / BRIGHT / REVERB (LCD 4-6) with value, slider, reverb
  ON/OFF pill; LCD 7-8 dashed (free).
- DATA DIAL: changes the selected control (0-127) while this screen is showing; everywhere
  else the dial works as before. LCD 6 DOWN: reverb off / back on at the previous level.
- Other volumes (SONG, ACMP, M.PAD, LEFT, MIC): PART ON/OFF button shows the stock mixer
  pages (the whole stock home screen comes back there); MAIN MIXER returns to the new screen.

Data read from the firmware: voice_name(part) 0x3FFF4, part_on(part) 0x40048, tempo
0x904CE(), transpose (signed) 0x51388(), octave 0x4005E()-1, style name pointer at RAM
0x108F098 (kept by the stock home screen), volume/brightness/reverb getters.

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
| 0x24546 + literal 0x24574 (end of UI flush) | -> `hp_tick`: original tail, then redraw if the screen was overdrawn or anything shown changed; when another page takes over, sets 0x101DE56 so the stock screen repaints fully |

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

## Status bar v6 (mod v5)
Blue bar, yellow text: style name left; transpose icon + value and octave keyboard icon +
value right (no boxes, no BPM: tempo lives in the TEMPO box). Icons are Yamaha's own images,
read from the resource area at run time (transpose 0x5F7CBC drawn inverted, octave 0x5F7BAC
columns 0-18, metronome = 14x12 at 135,133 of the compressed home background 0x5F7B6C);
nothing is copied into the mod. Header above the controls is empty while TEMPO is selected.

## Layout v5 (mod v4)
LCD 1 = TEMPO box (shows the BPM; selected: the dial changes tempo, header "TEMPO nnn BPM"),
LCD 2-4 = R1/R2/R3 (selecting one gives the dial back to the controls), LCD 5-7 = VOLUME /
BRIGHT / REVERB (LCD 7 DOWN = reverb on/off), LCD 8 free.

## Yamaha-style depth (mod v8, mockups/home_v7.py)
Dotted texture (1 ink dot in 8 pixels, like the stock home background) behind the layer
area, solid panels behind the off layers and the LEFT line, and the bottom section drawn as
before then inverted (blue panel, yellow text, selected items yellow).

## KyranOS boot splash (mod v7)
CURRENT: mockups/splash_v2.py = Kyran's own logo (mockups/splash_kyran_source.webp) scaled to
320x240 and thresholded (yellow -> light pixels), ~2 KB compressed.
First version: mockups/splash_v1.py drew it (3D title with a dithered extrusion, faint dithered glow, retro
perspective floor) and encodes it in Yamaha's compressed image format (~5 KB, round-trip
checked). The boot code draws the stock splash with 0x10ABCA(fb, pixels, rect 0x3257B4, 2) at
0x25FAE; the pixel pointer literal at 0x26010 now points to our data right after the mod code.
Pixel 1 = light LCD colour, same polarity as Yamaha's splash.

## Flicker while turning the dial (fixed in mod v6)
Drawing started while the previous frame was still being copied to the LCD (DMA, 20 bytes per
step, ~30 ms per screen), so the LCD picked up a half-cleared screen about once a second.
draw_screen now waits for the copy to finish (flag 0x10CB717; queue read/write indexes
0x10CB715/0x10CB716). Measured over 240 frames of continuous dialing: before, 19 frames with
up to 6900 changed pixels; after, none (largest change 127 pixels = the value itself).

## Old widgets flashing (fixed in mod v4)
Stock widgets of the hidden home screen still redraw (e.g. transpose/octave) and sent their
area to the LCD before we repainted. The graphics library's 16 pointers to the LCD refresh
0x10BF60 now go to `hp_refresh`, which drops those requests while our screen is up. Checked
with snapshots every frame after transpose/octave: no in-between frame.

## Flicker (fixed in mod v3)
Two causes, both about redrawing once a second although nothing changed: the stock fader
"update" callbacks are polled every second (they no longer mark the screen dirty), and the
stock UI flush (0x10BB0C) copies the stock screen over ours about once a second (skipped while
our screen is up). Verified in the emulator: no framebuffer writes at all over 14 s idle.

## PAN + borderless controls (mod v10, mockups/home_v9.py)
LCD 8 = PAN of the selected part (category 2 id 6, getter 0x50C34; 64 = C, shown L1-L63 /
R1-R63) drawn as a knob (-135..+135 degrees, pointer table in hp.c). The controls have no
borders: the selected one has a filled dot above its name and the name inverted on a pill;
dotted lines separate the controls.

## Master EQ page (mod v9, mockups/eq_v2.py, home tab: mockups/home_v8.py)
Own page on top of the home screen; the stock Mixing Console M.EQ page is unchanged and edits
the same settings.
- Open: side button F (top right; "EQ" tab on the home screen). Close: EXIT or F again. Going
  to any other screen also closes it.
- LCD 1-5: select band EQ1-EQ5. LCD 6/7/8: the dial changes Q / GAIN / FREQ of that band.
  UP+DOWN together: reset (LCD 1-5: that band's gain; LCD 6-8: that knob), like the stock page.
- Graph: 20 Hz-20 kHz (4.5 px per XG frequency step), +-12 dB, curve = sum of one bell per band
  (gain / (1 + (1.39 Q d)^2), d in octaves): a picture of the curve, not an exact response.
- Firmware interface (found in the stock page code 0x275E00-0x276F00, src pnm_*):
  read gain 0x4D27A(band) (64 = 0 dB), freq 0x4D240(band) (index into the XG table at
  0x3E957C: 0 = Thru, 1 = 22 Hz .. 59 = 18 kHz, 60 = Thru), Q 0x4D1DA(band) (Q x 10),
  preset 0x4D1CA() (system params 0xCD+b, 0xD2+b, 0xD7+b, 0xCC);
  write 0x25CE4(12, id, 2, [band, value]): id 21 Q, 22 FREQ, 23 GAIN; id 19 [band, k] = reset
  knob k (0 Q, 1 FREQ, 2 GAIN) to default; id 20 [n] = preset (0-1 PRESET1-2, 2-3 USER1-2);
  id 24 [v] = total gain adjust.
- Limits: the stock page asks the tone generator (0x4D2B4/0x4D2C8 Q, 0x4D2DC/0x4D37A freq per
  band, 0x4D3F0/0x4D404 gain); in the emulator that query is faked, so the mod uses the
  standard XG Multi EQ ranges: gain -12..+12 dB, Q 0.1-12.0, freq EQ1 32 Hz-2.0 kHz, EQ2-4
  100 Hz-10 kHz, EQ5 500 Hz-16 kHz. CHECK ON THE KEYBOARD against the stock page.
- Band shape: no shape setting is sent by the stock page (XG has shelf/peak for EQ1/EQ5);
  the graph draws every band as a bell.
- Hooks: home LCD F handler entry 0x332C24 (was 0x6A124: select RIGHT 1) -> hp_side_f;
  home panel default handler 0x3B152C (was 0x260670, gets EXIT) -> hp_default.
  Per-button handlers live in RAM 0x103EF40 + 4*button (entry+0 = press handler), filled from
  ROM tables like 0x332C14 (home LCD E-H).
- Emulator: the preset name is not shown (the faked tone generator leaves the preset value
  outside 0-3).

## Known limits / ideas
- The stock home screen keeps running underneath and sometimes redraws a widget; the new
  screen is redrawn over it at the next UI flush (brief flicker possible). If the LCD refresh
  queue is full the update is retried at the next flush (seen in the emulator as a half-drawn
  frame for a moment after several quick button presses). Watch for this on the keyboard.
- Drawing is pixel-by-pixel for text; fast enough in the emulator, check feel on hardware.
- Part on/off state is not shown yet (phase 2 with the top half).
- The dial no longer changes tempo on the home screen (use TAP TEMPO, or slot 7 could become
  TEMPO).
