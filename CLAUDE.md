# PSR-9000 Firmware Mod Project

Reverse-engineering the Yamaha PSR-9000 arranger keyboard (firmware V3.06) to add custom
multi-sample instruments, firmware features, a redesigned UI, and eventually a MAME driver
for testing without the hardware.

## Ground rules
- Yamaha firmware binaries and the service manual are copyrighted: NEVER commit them.
  They live in `firmware/` and `docs/private/`, both in `.gitignore`.
- Owner is Kyran. He tests on the real keyboard only occasionally, so prefer work that can be
  verified offline (scripts, Ghidra, MAME).
- Never suggest flashing a modified image without a recovery plan (see "Bootloaders").

## Source files (not in repo)
- `PSR9000_OS306.zip` from Yamaha UK support (firm-psr-9000 page). Contains:
  - `p9000m.001` (0x110000) resource pack: UI bitmaps + 5-language messages
  - `p9000m.002` (0x160000), `p9000m.003` (0x160000), `p9000m.004` (0x140000)
  - `p9000s.bin` (0x100000) sub CPU program
  - `9000update.pdf` install instructions
- Service manual PDF (block diagram, parts list, test mode).
- Installed on Kyran's keyboard: main 3.06, sub 3.01 (per test mode).

## Hardware (from service manual)
- DM board: main CPU SH7043 (HD6437043, IC1). Program flash IC6/IC7 = 2x 28F160S5 (4MB total).
  Work DRAM IC2/IC3. Flash IC27 (data, 16Mbit), flash IC28 (style, 16Mbit), mask IC29 (style).
  Video gate array, FDC, SCSI (IC31), SIO4 (IC26), optional IDE HDD.
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
  VERIFIED on the files: byte sum of the whole image == 0xFF (mod 256) for both main
  (total 0x1670FF) and sub (0x3AE0FF). 16-bit byte/word sums show no pattern, so this looks
  8-bit. After patching, fix by adjusting a trailer byte. Still unconfirmed against the
  keyboard's own check routine (see test-mode table below).
  Trailer bytes after the tag: main `44 70 72 0F 35 FF B7 FF`, sub `6A 03 41 41 EF FF 23 FF`
  (meaning unknown; maybe the adjustment bytes plus a stored sum).
- Test-mode menu: name-pointer table at main 0x247FF4 ("Version", "Rom Chk1", ... "WaveRam Chk2",
  strings at 0x3AD930+). It sits in the literal pool of a switch on the test number (mov.l/bra
  pairs around 0x247F00-0x248120). Follow case 1 (Rom Chk1) in Ghidra to find the checksum code.
  The table lists tests this model lacks (Output Sub3-6, Plugin-1/2, MC Chk1, Battery Chk,
  Yellow/Key LEDs), so the test code is probably shared with sibling models.
  Other refs: "Flash Protect:" 0x3ADDCC (used at 0x248BE8), "PROTECT" 0x3ADE8C (0x248EE0, 0x249218),
  "Flash Chk1 OK" 0x3AE410 (0x24C01C).
- `p9000m.001`: starts with header `00 4F 00 00 00 11 00 00` (0x110000 = file size).
  Byte-sum rule does NOT hold here; checksum + load address still unknown.
  Contains 1-bit LCD bitmaps, 320px wide (40 bytes/row), e.g. owner-name entry screen
  near offset 0x6000 + 1180*40.
- Pointer clusters in main code to external ranges ~0x00C00000, 0x01000000, 0x02000000,
  0x05000000: likely DRAM / LCDC / gate arrays / main-sub link. Not yet identified.

## Plan
1. Ghidra: import main_prog.bin (SH-2 BE, base 0), entry 0x206E8; sub entry 0x85D4.
   Add SH7043 on-chip RAM/peripheral blocks from the Renesas SH7043 hardware manual.
2. Build memory map (find LCDC via SED1335 command bytes 0x40/0x42/0x44/0x46).
3. Tool: extract all UI bitmaps from p9000m.001 to PNG; crack its header/checksum.
4. Confirm checksum algorithm from the code that runs test 002 / update verification.
5. MAME driver skeleton: stub vector table at 0 jumping to 0x206E8 (real bootloader not
   in update files). Milestone: boot screen in emulation. Then panel input, sub CPU, SWP30.
6. Later: .VIC custom voice format, debug output over TO HOST serial / MIDI SysEx,
   first harmless real-hardware test (string edit), font replacement, UI redesign.

## Background ideas
- Multi-samples: waveforms split by key; velocity layers via up to 8 elements per voice
  with VELOCITY LIMIT. V3 reduced custom voice memory.
- Storage: Gotek floppy emulator, ZuluSCSI/SCSI2SD on SCSI port, CF-to-IDE for HDD bay.
- UI reference: Playdate (400x240 1-bit). SED1335 has layers + hardware scroll.
- MAME already emulates SWP30 (MU100) and related chips; use as format reference.
