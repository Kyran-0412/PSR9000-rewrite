# Emulator test report (2026-11-02)

Firmware V3.06 in the MAME driver (sub CPU and SCSI faked, no voice/style/wave ROMs).
Test lists are in `mame/tests/` (A-D), run with `mame/tests/runtest.sh NAME` and
`mame/crawl.lua`. Reference: the V3 update manual (`docs/private/`, not in git), its
Direct Access chart and "Access:" paths. 372 screenshots were reviewed by hand.

| Part | What | Result |
|---|---|---|
| A | every panel button on its own (147) | correct page for all except the 7 noted below |
| B | Direct Access chart (68 combinations) | 68/68 open the page the manual lists |
| C | FUNCTION F1-F8 (+NEXT pages, Controller/Utility sub pages), Mixing Console 9 tabs both directions, SONG SETUP, DISK/SCSI menus, MIDI templates, Sound Creator, Digital Recording, Demo | all open and switch as described |
| D | transpose, octave, part on/off, part select, tempo dial, tap, volume hold, regist bank, MEMORY + REGIST store/recall, OTS, multipad, voice effects, START/STOP, DEMO | see below |

Working (D): transpose +/- (TRP shown), upper octave, part on/off, tempo via dial,
volume UP/DOWN incl. hold-repeat, bank +/- (wraps 01 <-> 64), registration store
("Now executing") and recall (TRP 2 restored, "REG.01-1" shown), M.PAD bank.

Not working, all traced to data we do not have:
- Voice select: choosing a voice in a voice list sends the change to the sound board, but
  the firmware first reads voice data back from the board (voice parameter ROM IC33, not
  dumped); the fake board answers 0 and the old voice stays. Names show "GrandPno".
- Styles: START/STOP has no style data (preset style ROM IC29 not dumped). After START,
  some menus (FUNCTION, DISK/SCSI, MIDI, SOUND CREATOR, DIGITAL REC, DEMO) do not open
  until restart. Avoid START/STOP in the emulator for now.
- Music Database list is empty (its data is in the missing ROMs/flash contents).
- OTS buttons do nothing visible (no style data).
- No sound (by design), no LEDs, no pedals/wheels, no floppy image tested yet.

Minor, to re-check on the keyboard: a few LCD buttons pressed inside some sub pages
(Controller > B, Utility > G, Song Setup > G) returned to the home screen in the test; may
be test timing.
