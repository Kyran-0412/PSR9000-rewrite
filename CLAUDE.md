# PSR-9000 Firmware Mod Project

Reverse-engineering the Yamaha PSR-9000 arranger keyboard (firmware V3.06) to add custom
multi-sample instruments, firmware features, a redesigned UI, and eventually a MAME driver
for testing without the hardware.

## Goals (what Kyran wants)
Used live at church. Keep all sounds and styles as they are. Approach: patch the existing
firmware (hook/replace screens and handlers, new code in free space), not a from-scratch rewrite.
1. Home page redesign. Kyran picks parts/layers with the panel buttons right of centre, never
   with the on-screen R1/R2/R3 buttons, so the home screen becomes display-only for parts:
   show the ACTIVE layers (R1/R2/R3 voice + on/off) in the centre. LEFT/SONG/STYLE can shrink
   to a status line. Frees the 10 side buttons (A-E left, F-J right) for new functions.
2. Bottom row (LCD buttons 1-8, UP/DOWN pairs): (v5) button 1 = TEMPO (selected: the dial
   changes tempo), buttons 2-4 SELECT THE PART (R1/R2/R3), buttons 5-7 = controls, 8 free;
   the DIAL changes the value (like the rest
   of the PSR-9000; the stock home mixer uses up/down per channel, which annoys him). Other
   volumes (SONG, ACMP, M.PAD, LEFT, MIC, song/style tracks) move to a sub-menu.
3. Controls (decided): VOLUME (CC7), BRIGHT (CC74), REVERB (param id 11; DOWN toggles on/off and restores
   the previous level). Slot 8 left empty for later. NOT octave/transpose (own panel buttons).
4. Display is 1-bit; LCD tint is hardware. Kyran wants a more modern look: flat, Inter-style
   font baked to bitmaps, rounded cards, inverted highlight for selection.
5. Boot splash: KyranOS, Kyran's own logo (mockups/splash_v2.py + splash_kyran_source.webp),
   DONE in the mod (v1 generated design kept in splash_v1.py).
6. More later.
Current layout: mockups/home_v7.py (= v6 + dotted texture behind the layers, solid panels behind
off layers/LEFT, bottom section inverted to a blue panel; v6 = v5 + Yamaha-style blue status bar with yellow text and the
stock transpose/octave icons, metronome icon under TEMPO; v5 = v4 + TEMPO box on LCD 1,
everything else one button right;
v4 = v3 + Bold text, 2 px borders, border around each control; v1-v4 are earlier drafts).
IMPLEMENTED (full screen as mockup v7, mod v8): mod/home/ (C code + hooks), docs/HOME_MOD.md.
Kyran wants the mockup design EXACTLY; do not invent a different layout.
Emulator test pass vs the V3 manual: docs/TEST_REPORT.md.
MASTER EQ page (mod v9, mockups/eq_v2.py + home_v8.py EQ tab): side button F on home opens it,
LCD 1-5 band, LCD 6-8 Q/GAIN/FREQ, dial = value, curve graph, EXIT = home. Stock M.EQ page kept.
EQ param API (category 12, ids 19-24) and hooks: docs/HOME_MOD.md "Master EQ page".

## Leads for the goals (main_prog.bin offsets)
- Parts are named RIGHT 1 / RIGHT 2 / RIGHT 3 / LEFT (0x3D0060, 0x3D0FF4).
- Panel button / assignable-function name list at 0x3AEC60-0x3AF1EC: includes MAIN MIXER,
  MIXING CONSOLE, DIRECT ACCESS, PART ON/OFF, LCD A-J, LCD 1-8 UP/DOWN, PART SELECT
  RIGHT1/2/3/LEFT, PART ON/OFF RIGHT1/2/3/LEFT, REGIST 1-8. Find which code indexes this list to
  get the button-ID numbering (needed to remap buttons).
- Per-part controller targets at 0x3D0F4C: MODULATION, VOLUME, PAN, EXPRESSION, HARMONIC CNT,
  RELEASE/ATTACK TIME, BRIGHTNESS, REVERB SEND, CHORUS SEND, applied to RIGHT 1/2/3/LEFT.
  Likely the foot pedal / controller assign table, so the firmware already has a "set brightness /
  reverb send for part N" path we can call from a new home-page control.
- Second parameter list at 0x39398C: VOLUME, PAN, EXPRESSION, BRIGHTNESS, HARMONIC CONTENT,
  REVERB DEPTH, CHORUS DEPTH, DSP3 DEPTH (probably song/sequencer event names).
- Numbered list "007: Volume", "074: Brightness", "091: Reverb Send Level" (0x34DC04, 0x34E344,
  0x34E528) = MIDI control-change names (CC7/74/91). Brightness = CC74, reverb send = CC91, so
  each goal is "send CC74/CC91 to part N internally".
- "MAIN MIXER" screen title 0x3AEDD4, "MIXING CONSOLE" 0x3AEC98 / 0x3B6438.

## Code map for the home-screen goals (found 2026-11, main_prog.bin)
Panel button IDs (from the SW/LED test table at 0x3AD240: 12-byte entries = name ptr, u16 switch
ID, u16 LED, test note): LCD A-J = 0x88-0x91, LCD n UP = 0x92+2(n-1), LCD n DOWN = 0x93+2(n-1)
(so 1UP=0x92 ... 8DOWN=0xA1), MAIN MIXER 0x83, PART ON/OFF 0x84, PAGE NEXT 0x85, BACK 0x86,
EXIT 0x87, DIRECT ACCESS 0x05, PART SELECT LEFT/R1/R2/R3 0x4F-0x52, PART ON/OFF L/R1/R2/R3
0x53-0x56. The DATA DIAL is not in this table (separate encoder path, not found yet).

Main Mixer button handling (page 1, MAIN VOLUME):
- 0x2606DC: button ID -> channel 0-7. 0x26073C(id, step): UP = +step, DOWN = -step.
- 0x2607D2(delta, channel, mode): reads current value with a getter, clamps 0..127
  (0x25FA58(cur, delta, max, min)), then posts the change.
- Channel -> getter / param ID: SONG 0x51218/44, ACMP 0x5104A/28, M.PAD 0x51298/51,
  LEFT/R1/R2/R3 = 0x50C18(part)/5, MIC 0x51308/57.
Part numbers everywhere: 0=R1, 1=R2, 2=R3, 3=LEFT (column->part table 0x3B3D31: 00 00 00 03 00 01 02).

Parameter change API: 0x25CE4(category, id, len, data*) -> 0x2ACDE -> 0x2ABC6 (posts a message;
another task applies it). Keyboard-part params are category 2, data = [part, value(, 1)]:
  id 5 VOLUME, 6 PAN, 7 EQ LOW, 8 EQ HIGH (VOL/EQ handler 0x268358),
  id 9 HARMONIC CONTENT, id 10 BRIGHTNESS (FILT handler 0x26BA8A).
  Song uses ids 44-47 (+41-43 effects), ACMP 28-31 (+25-27), M.PAD 51-54, MIC 57/58.
Getters (part -> value): 0x50C18 volume (field 0), 0x50C34 pan (1), 0x50C50/0x50C6C EQ (6/7),
  0x50CB8 harmonic (80), 0x50CD4 BRIGHTNESS (82). Pattern: 0x4D718(part)->channel, then
  0x1B4AF2(channel, field). 0x408E8(part, index) = get keyboard-part setting by index:
  0 vol, 1 pan, 2-3 EQ, 4 harmonic, 5 bright, 6 REVERB SEND (0x50CF0), 7 chorus (0x50D06),
  8 DSP (0x50D1C), 10-13 signed/other (octave, tuning...).
REVERB send per keyboard part = FIELD 3 of the same channel record as volume/brightness
  (chorus = field 4). Records: base 0x39E434 + 0x1F8 R1 (0x39E62C), +0x24C R2 (0x39E680),
  +0x2F4 R3 (0x39E728), +0x2A0 LEFT (0x39E6D4); 0x4D718(part) returns these.
  Read: 0x24369A..0x2436CA (= 0x1B4AF2(record, 3)), reached as system params 5-8 through
  0x23CF60 (getter tables 0x3AC128 sub0 = field 3, 0x3AC150 sub1 = field 4).
  Field read/write is virtual: record+4 = method table (0x1CA552 = slot +4 get, +28 get default,
  +8/+12/+16.. not identified). Field lists per record at 0x32CA2C (kbd parts: 0,1,6,7,80,82)
  and 0x32CA88 (others: 0,1,6,7,80,82,3,4,5).
  WRITE (found): Mixing Console > EFF DEPTH tab (reached via MIXER button, then the top
  side buttons A/F page left/right through the tabs). Keyboard-part variant code 0x26EB68..;
  value-change handler 0x26FEFA(col, ?, newval): row var 0x11A6E38 (1 DSP, 2 CHORUS, 3 REVERB),
  column -> part table 0x3B4C49 (cols 3-6 = LEFT,R1,R2,R3). Posts 0x25CE4(2, id, 2, [part, value]):
    id 11 = REVERB send, id 12 = CHORUS send, id 13 = DSP depth (keyboard parts).
  Other columns: SONG 48/49/50, ACMP 32/33/34, M.PAD 55/56/(DSP n/a), MIC 59/60/61.
  So all three home-screen controls are category-2 messages with data [part, value]:
    VOLUME id 5 (get 0x50C18), BRIGHT id 10 (get 0x50CD4), REVERB id 11 (get 0x50CF0).

DATA DIAL (found):
- Dial events from the panel event table at 0x3333F0 call 0x6D30E (one direction) and 0x6D2FE
  (other direction). Both jump to the function pointer stored in RAM 0x108F358 with
  (amount, direction 0/1). Mode byte at 0x108F35C.
- 0x6D0C4(fn) = install a screen's own dial handler (mode 2). 0x6D0D2 = restore the default.
  Used by every Mixing Console page (0x264026, 0x2651E0, ...).
- Default handler = 0x6D45C (tempo): 0x6D4F0(delta, cur) clamps 32..280 and posts
  0x25CE4(18, 0, 2, [tempo hi, lo]). That is why the dial changes tempo on the home screen.
- Main Mixer page event table 0x3B14A8: 16-byte entries {u16 btn, u16 btn, handler, ...}:
  UP/DOWN pair -> 0x2606DC (select channel), each button -> 0x26073C (step, repeat 0x64);
  default handler 0x260670 passes unhandled events (incl. dial) to the focused fader widget
  (0xDEFA4), otherwise to 0x6833C -> 0x684E4 (leave mixer). So on the Main Mixer the dial goes
  to the selected fader widget, not through 0x108F358.
- PLAN for the new home screen: install our own dial handler with 0x6D0C4 while the new screen
  is shown (dial adjusts the selected control), restore with 0x6D0D2 when leaving.

## Ground rules
- Yamaha firmware binaries and the service manual are copyrighted: NEVER commit them.
  They live in `firmware/` and `docs/private/`, both in `.gitignore`.
- Owner is Kyran. He tests on the real keyboard only occasionally, so prefer work that can be
  verified offline (scripts, Ghidra, MAME).
- Never suggest flashing a modified image without a recovery plan (see "Bootloaders").

## Bootloaders and recovery (see docs/RECOVERY.md)
- The PROGRAM INSTALLER (hold START/STOP at power-on) is in the protected boot area: no
  installer strings exist in p9000m.002-004, and updates never write main 0x0-0x1FFFF or
  sub 0x0-0x7FFF. So a broken main program should be reinstallable from the original disks.
- Rules: known-good disks tested first, user data backed up, smallest possible first change,
  both checksums fixed, never touch flash-writing code, never change main and sub together.

## Source files (not in repo)
- `PSR9000_OS306.zip` from Yamaha UK support (firm-psr-9000 page). Contains:
  - `p9000m.001` (0x110000) resource pack: UI bitmaps + 5-language messages
  - `p9000m.002` (0x160000), `p9000m.003` (0x160000), `p9000m.004` (0x140000)
  - `p9000s.bin` (0x100000) sub CPU program
  - `9000update.pdf` install instructions
- Service manual PDF (block diagram, parts list, test mode).
- Installed on Kyran's keyboard: main 3.06, sub 3.01 (per test mode).
- Kyran also has 2 floppies "Factory Setup Restore Disks": he checked, they hold factory
  SETTINGS (setup/registration-type data), not styles or voices. Useful later for realistic
  emulator flash contents; they cannot replace the mask ROMs (IC29 styles, IC33 voice
  parameters, wave ROMs). Keep private in firmware/ if copied.

## Hardware (from service manual)
- DM board: main CPU SH7043 (HD6437043, IC1). Program flash IC6/IC7 = 2x 28F160S5 (4MB total).
  Work DRAM IC2/IC3. Flash IC27 (data, 16Mbit), flash IC28 (style, 16Mbit), mask IC29 (style).
  Video gate array, FDC, SCSI (IC31), gate array MBCG46183 (IC26), optional IDE HDD.
  IC27/IC28 = 28F160S5 (data/style flash), IC29 = MX23C1610 2MB PRESET STYLE mask ROM (not in
  any update file; needs a hardware dump). Emulator status/memory map: docs/EMULATOR.md.
- TG board: sub CPU SH7043 (IC11). Program flash IC25/IC26 = 2x 28F400B5 (1MB total).
  Voice parameter mask ROM IC33 (16Mbit). 2x SWP30 tone generators (master IC1, slave IC12).
  Wave mask ROMs: IC44/IC45 (WAVE1 lo/hi), IC41/IC42 (WAVE2 lo/hi), 4x MX23C6410 = 32MB.
  SMI (IC16) drives 2x 72-pin SIMM slots (wave RAM, max 32MB each), same wave bus as ROMs.
- LCDC: SED1335F0B (XQ595A00) is IC4 on the DP board (LSI pin list p.19), not on DM.
  There is also a composite video out (test 065-070: NTSC/PAL R/G/B text), so the UI has a
  second display path through the video gate array.
- Service manual text can be extracted with `pdftotext -layout` (test program list is p.37-40).
- Test mode: hold DEMO + EXIT + INTRO A at power-on (auto mode); INTRO B = manual mode. 001 = versions + flash protect state,
  002 = Rom Chk1 (main/sub/param/style checksums), 005 = WaveRomChk1.
- Firmware installer: power on holding START/STOP with update disk.

## Firmware findings (verified)
- Files are RAW: no compression, no encryption, no byte interleave. Plain SH-2 big-endian code.
- Main program image = `p9000m.002 + .003 + .004` concatenated = exactly 4MB, loads at address 0
  (verified: absolute pointers to strings match file offsets).
  - Windows: `copy /b p9000m.002+p9000m.003+p9000m.004 main_prog.bin`
- Main: 0x00000-0x1FFFF is all FF in the update (protected bootloader region, not written by
  updater). App vector table at 0x20000: reset PC 0x000206E8, SP 0xFFFFFFFC.
- Sub: 0x0000-0x7FFF all FF (protected bootloader). Vector table at 0x8000: reset PC 0x000085D4,
  SP 0xFFFFFFFC.
- Copyright/build strings: main 0x323670 "PSR9000 MAIN ROM ... YAMAHA 1999", built Mar 03 2004;
  sub 0xD4A2C. Sub also contains "PSR9000 Wave Version 1.11".
- Main/sub mismatch check exists ("The main program and the sub program do not match").
- 56 Yamaha C source filenames left in strings (e.g. `sys_lib.c`, `rsm_vc.c`, `pnm_*.c`):
  use them to label functions.
- Each image ends with a tag: `306` + `2004MAR031628xx` + adjustment bytes.
  Two integrity rules, both VERIFIED on the original files:
  1. Whole-image byte sum == 0xFF (mod 256), main and sub (adjustment bytes at the end,
     main 0x3FFFFC/0x3FFFFE). Probably checked by installer/boot code (not in our files).
  2. Test-mode Rom Chk1 (main) = routine 0x24D892(0, 0x3FFFFF): sparse 4-lane byte sum over
     power-of-two offsets per 512 KB block, boot area assumed FF, compared with
     0x3FFFF8-0x3FFFFB (`44 70 72 0F`). Exact re-implementation: tools/romcheck.py.
     Rom Chk1 also checks sub (0x2559CC(10)), style 0x600000-0x7FFFFF (0x24DA30), param ROM.
- Test-mode menu: name-pointer table at main 0x247FF4 ("Version", "Rom Chk1", ... "WaveRam Chk2",
  strings at 0x3AD930+). It sits in the literal pool of a switch on the test number (mov.l/bra
  pairs around 0x247F00-0x248120). Follow case 1 (Rom Chk1) in Ghidra to find the checksum code.
  The table lists tests this model lacks (Output Sub3-6, Plugin-1/2, MC Chk1, Battery Chk,
  Yellow/Key LEDs), so the test code is probably shared with sibling models.
  Other refs: "Flash Protect:" 0x3ADDCC (used at 0x248BE8), "PROTECT" 0x3ADE8C (0x248EE0, 0x249218),
  "Flash Chk1 OK" 0x3AE410 (0x24C01C).
- `p9000m.001` = UI image pack, DECODED (tools/res_extract.py extracts all 1030 images to PNG).
  Loads at 0x4F0000 (header word 0 = load address, word 1 = size). Each image = pixel data plus a
  12-byte record: u16 w, u16 h, u32 flags, u32 ptr-to-pixels. Main code points at the RECORDS
  (909 literal refs). flags 0 = raw 1bpp rows (byte padded); flags 0x01000000 = compressed:
  u16 len, then 0x00-0x7F = 7 literal px, 0x80+n = n+1 px off, 0xC0+n = n+1 px on; a literal is
  cut off at row end. Bit 1 = "ink" in the bitmaps, BUT on the real LCD a set framebuffer bit
  shows LIGHT (pastel yellow) and a clear bit BLUE (confirmed by Kyran on the keyboard; the
  emulator palette and mod/home use this). 237 compressed + 793 raw, all decode exactly.
- UI screens are built from widget objects: 0xDEB18 = widget_set_image (stores record ptr at +32).
- Home screen: top half = image record 0x5F7B6C (drawn by code at ~0x69640, also loads the
  octave -1/0/+1 and bottom-bar icons). Song/lyrics variant = 0x5A1F64 (code ~0x1DC6C0).
- Home bottom half = MAIN MIXER panel, code 0x25FFC0-0x262A40. Panel at (0,154) 320x86, frame
  record 0x520308, title strip at (0,156). 4 pages: MAIN VOLUME (0x520FB8, built ~0x25FFF4),
  PART ON/OFF ACMP (RHY1..PHR2), SONG TR1-8, SONG TR9-16. 8 channels, 40px apart, fader widget
  at (40*i+2, 181), range 0-127. Page-1 labels 0x3B1458 (6-byte stride): SONG ACMP M.PAD LEFT
  R1 R2 R3 MIC. Fader art = 29 pre-drawn 28x35 frames (0x504F48..); rotary knob = 13 frames.
- Mixing Console screens: 0x524C30.. (tabs VOL/EQ FILT EFF DEPTH EFF TYPE TUNE N.EQ VOICE
  LINE OUT; parts MASTER SONG KBD LEFT R1 R2 R3), code near 0x2638C4. BRIGHTNESS lives on FILT.
- Pointer clusters in main code to external ranges ~0x00C00000, 0x01000000, 0x02000000,
  0x05000000: likely DRAM / LCDC / gate arrays / main-sub link. Not yet identified.

## Plan
1. Ghidra: import main_prog.bin (SH-2 BE, base 0), entry 0x206E8; sub entry 0x85D4.
   Add SH7043 on-chip RAM/peripheral blocks from the Renesas SH7043 hardware manual.
2. Build memory map (find LCDC via SED1335 command bytes 0x40/0x42/0x44/0x46).
3. DONE: tools/res_extract.py extracts all UI bitmaps (checksum of .001 still unknown).
4. DONE (Rom Chk1, tools/romcheck.py). Installer/boot check still unseen (boot area not in files).
5. MAME driver: HOME SCREEN REACHED in emulation (docs/EMULATOR.md 3d; sub CPU + SCSI faked,
   MAME core patches in mame/patches/). Next: panel input (UART ch3 HLE), Windows build.
6. Later: .VIC custom voice format, debug output over TO HOST serial / MIDI SysEx,
   first harmless real-hardware test (string edit), font replacement, UI redesign.

## Background ideas
- Multi-samples: waveforms split by key; velocity layers via up to 8 elements per voice
  with VELOCITY LIMIT. V3 reduced custom voice memory.
- Storage: Gotek floppy emulator, ZuluSCSI/SCSI2SD on SCSI port, CF-to-IDE for HDD bay.
- UI reference: Playdate (400x240 1-bit). SED1335 has layers + hardware scroll.
- MAME already emulates SWP30 (MU100) and related chips; use as format reference.
