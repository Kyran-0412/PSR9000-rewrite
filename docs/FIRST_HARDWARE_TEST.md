# First test on the real PSR-9000

Two disk sets, built by `mod/home/make_image.py` (the files contain Yamaha's code, so they are
not in this repository):

| Set | What changes | Built with |
|---|---|---|
| TEST 1 | only the boot splash (KyranOS logo); everything else stock | `make_image.py --splash-only` |
| TEST 2 | the full mod: splash, new home screen, Master EQ page, setlist | `make_image.py` |

Both change only `p9000m.002` and `p9000m.004` (MAIN PROGRAM disks 2 and 4). Disks 1 and 3
are Yamaha's files, unchanged. The SUB PROGRAM is never touched: do not use the installer's
SUB PROGRAM option. Both checksums are fixed (whole-image byte sum 0xFF; test-mode Rom Chk1
value unchanged, 4470720F). Both sets were booted in the emulator from these exact files.

Why TEST 1 first: it checks the update route itself (modified files on disk -> installer ->
keyboard boots) with the smallest possible change, and lets the way back be tested before
any of our code runs on the keyboard. See docs/RECOVERY.md.

## Before anything (nothing is written)

1. Pick a quiet time, not right before a service. Keep the keyboard plugged in; never switch
   off while the installer is writing.
2. Hold [START/STOP] and switch on. The PROGRAM INSTALLER page should appear. Switch off
   without pressing anything else.
3. Back up your data: [DISK/SCSI] > BACKUP to a blank floppy (as Yamaha recommends before any
   update). Note any custom voices, registrations and settings you care about.
4. Have a known-good set of Yamaha's original MAIN PROGRAM disks 1-4 (the church set, or fresh
   disks made from PSR9000_OS306.zip), write-protect tabs ON.

## Making the test disks

For each set, 4 floppies (1.44 MB). Format each one, then copy exactly one file onto it,
keeping the file name:

| Disk label | File |
|---|---|
| MOD TEST n - MAIN PROGRAM 1 | p9000m.001 |
| MOD TEST n - MAIN PROGRAM 2 | p9000m.002 |
| MOD TEST n - MAIN PROGRAM 3 | p9000m.003 |
| MOD TEST n - MAIN PROGRAM 4 | p9000m.004 |

Disks 1 and 3 can be shared between both sets and your original set (they are identical).

## TEST 1: splash only

1. Switch off. Hold [START/STOP], switch on: PROGRAM INSTALLER.
2. Choose MAIN PROGRAM ([B]) and insert the TEST 1 disks as asked, the same way as a normal
   Yamaha update.
3. Restart. Expected: the KyranOS logo at start-up, then the normal Yamaha home screen.
4. Test mode (hold DEMO + EXIT + INTRO A at power-on), test 002 Rom Chk1: should say OK.
   Leave test mode by switching off.
5. **Check the way back:** reinstall Yamaha's original MAIN PROGRAM disks 1-4. Expected: the
   Yamaha splash returns. Only continue when this worked.

## TEST 2: the full mod

1. Install the TEST 2 disks the same way.
2. Check, and note anything odd (photos help):
   - KyranOS splash, then the new home screen; no flicker, nothing from the old screen showing.
   - LCD 1 TEMPO (dial changes tempo), LCD 2-4 select R1-R3, LCD 5-8 VOL/BRT/REV/PAN with the
     dial; REV DOWN switches reverb off/on. **Listen**: volume, brightness, reverb and pan must
     really change the sound (the emulator could not check sound).
   - Side button F: Master EQ page; LCD 1-5 band, LCD 6-8 Q/GAIN/FREQ, dial; EXIT back home.
     Compare the values with the stock M.EQ page (MIXING CONSOLE) and listen.
   - Voice changes, part on/off, transpose/octave, style selection: the home screen follows.
   - Side button G: setlist (docs/SETLIST.md). Use a spare floppy for it, not one of the
     update disks. Create a song (NEW), give it a title (NAME), pick a registration memory that
     you have stored, SAVE, then OPEN it:
     - the registration should load, and both lights should go solid (MIDI SENT, REGIST LOADED);
     - with FL Studio connected to MIDI OUT, the drone should change.
     Switch off and on, press G: the song should come back from the floppy. Check the floppy on
     a PC: it should hold KYRANOS.SET.
   - Normal use for a while: menus, registration memory, styles, MIXER, other pages.

## If something goes wrong

Blank or frozen screen, or anything that does not come back: switch off, hold [START/STOP],
switch on, and reinstall Yamaha's original MAIN PROGRAM disks 1-4. Restore your data from the
backup if settings were reset.
