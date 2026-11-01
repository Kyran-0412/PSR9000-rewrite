# Recovery plan (read before flashing anything)

Status: researched from the firmware files and Yamaha's update manual. Steps marked
**[on keyboard]** still need a harmless check on the real instrument.

## Why a bad main program should be recoverable

1. **The installer is not part of the firmware we modify.**
   Yamaha's manual: hold `[START/STOP]` while switching on and the *PROGRAM INSTALLER* page
   appears; `[B]` loads MAIN PROGRAM disks 1-4, `[C]` loads the SUB PROGRAM.
   None of the installer's text ("PROGRAM INSTALLER", "MAIN PROGRAM 1..4", file names) exists
   in the main program (`p9000m.002-004`), and the resource pack only has generic disk messages.
   So the installer lives in the protected boot area.

2. **The update never writes the boot area.**
   In the update files, main 0x00000-0x1FFFF and sub 0x0000-0x7FFF are all `FF`. The boot
   code that checks `[START/STOP]` and runs the installer is therefore left alone by every
   update, including ours.

3. **Yamaha expects failed installs to be reinstalled.** The manual warns that switching off
   or pulling a disk mid-install "can require a complete reinstallation", i.e. the installer
   still starts after a broken main program.

Conclusion: if a modified main program crashes or shows a blank screen, power off, hold
`[START/STOP]`, power on, and reinstall the original Yamaha files. This only works if the boot
code checks the button **before** it jumps to the main program, which point 3 strongly suggests.

## Integrity checks we must keep passing

Both verified against the original files with `tools/romcheck.py`:

- **Test-mode "Rom Chk1" (main):** routine at 0x24D892. Sparse address-line checksum, 4 byte
  lanes, samples only power-of-two offsets in each 512 KB block, assumes FF in the boot area.
  Result is stored at 0x3FFFF8-0x3FFFFB (`44 70 72 0F` in V3.06). Re-implemented exactly.
- **Whole-image byte sum == 0xFF (mod 256)** for main and sub. Kept true by the adjustment
  bytes at the very end (main 0x3FFFFC/0x3FFFFE). Probably what the installer or boot code
  checks; not confirmed (that code is in the boot area we don't have).

A patch tool must recompute both (only the sampled addresses affect the first; keep patches
away from them where possible) before any image goes near the keyboard.

## Status on Kyran's keyboard (2026-11)
- Installer confirmed working: Kyran updated the keyboard to main 3.06 / sub 3.01 recently
  using the PROGRAM INSTALLER from floppy.
- Original update disks exist (stored at church). 10 blank floppies available, and the
  official files from Yamaha's site can be written to fresh disks at any time.
- Modified files will always be exactly the same size as the originals (the 4 MB image is
  split the same way), so they fit on the same 1.44 MB disks.

## Rules for the first real-hardware tests

1. Keep a **known-good set of update disks** (original Yamaha files), write-protect tab on.
   Put modified firmware only on separate, clearly labelled disks (e.g. "MOD TEST 1").
2. **Back up user data first** (DISK/SCSI > BACKUP/RESTORE > BACKUP), as Yamaha recommends:
   reinstalling can reset custom voices and settings.
3. First modified image = **smallest possible change** (e.g. one text string edited), both
   checksums fixed. Only after it boots and reinstalling the original also works do we move to
   code changes.
4. Never change the sub program and main program in the same test.
5. Never modify code that writes flash.

## Last-resort fallback

Program flash is 2x Intel 28F160S5 (IC6/IC7) on the DM board. If the boot area were ever
damaged, the chips can be read/written with an external programmer (desolder or clip).
Treat this as the emergency option only; nothing we plan touches the boot area.

## Checks to do on the keyboard (harmless, nothing is written)

- **[on keyboard]** Hold `[START/STOP]` and switch on. Confirm the PROGRAM INSTALLER page
  appears, then switch off without pressing anything else. Nothing is written until a load
  is started with `[B]`/`[C]` and `[START/STOP]`.
- **[on keyboard]** Test mode (hold `DEMO + EXIT + INTRO A` at power-on), test 001: note the
  "Flash Protect" state shown, and test 002 (Rom Chk1) should report OK.
