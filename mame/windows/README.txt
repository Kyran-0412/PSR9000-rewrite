PSR-9000 emulator for Windows (work in progress)
================================================

This runs the real PSR-9000 V3.06 main firmware in MAME. There is NO SOUND: the sound
board is faked, and the voice/style ROM chips are not available. Every part shows
"GrandPno" and the style list is empty for the same reason.

Setup (once)
1. Unzip this folder anywhere, e.g. Documents\PSR9000-emu.
2. Make a folder "roms", and inside it a folder "psr9000".
3. From Yamaha's PSR9000_OS306.zip copy these 5 files into roms\psr9000:
      p9000m.001  p9000m.002  p9000m.003  p9000m.004  p9000s.bin
4. Double-click START_PSR9000.bat.
   The first time it shows a red warning ("does not work"): press any key or click.
   The FIRST start takes a few minutes: the screen shows "Initializing SETUP",
   "Initializing STYLE", ... while the firmware formats its emulated flash memory,
   exactly like a factory reset. Leave it running until the home screen appears, then
   quit with Esc. This is saved in the "nvram" folder, so later starts take seconds.

Using it
- Click the panel buttons with the mouse. They light up blue while pressed.
- Data dial: click the left (-) or right (+) half, hold to keep turning.
  Keyboard: Left/Right arrows or Down/Up arrows.
- Keys (optional):
      LCD 1-8 UP   = 1 2 3 4 5 6 7 8      LCD 1-8 DOWN = Q W E R T Y U I
      LCD A-E      = A S D F G             LCD F-J      = H J K L ;
      EXIT = Backspace   MIXER = M   DIRECT ACCESS = N   PART ON/OFF = ,
      PAGE BACK/NEXT = [ ]   MIXING CONSOLE = Z   FUNCTION = X   START/STOP = Space
      Numpad 0-3 = PART SELECT L/R1/R2/R3    Numpad 4-7 = PART ON/OFF L/R1/R2/R3
      Numpad - + = UPPER OCTAVE
- MAME keys to know: Esc = quit, Tab = MAME menu (change keys, Video Options > view),
  P = pause, F12 = screenshot (saved in "snap").
- Views (Tab > Video Options): Centre Panel (default), Full Panel, Screen Only.
- Pixel-exact LCD: the .bat files open the Centre Panel maximized with "-nounevenstretch",
  so the LCD is always shown at a whole-number zoom (2x on a 1920x1080 monitor) and no pixel
  row or column is dropped or doubled. The Full Panel (1970 px wide) does not fit a
  1920 px monitor, so MAME has to shrink it slightly and thin lines can vanish: use it only
  to find a button, not to judge the screen design. Screen Only gives the biggest LCD.
  If an old setup still opens the Full Panel, pick Centre Panel once in Tab > Video Options
  (MAME remembers it in the cfg folder).

Resetting the emulated keyboard: quit, then delete the "nvram" folder.

New home panel (test version)
- START_PSR9000_NEW_HOME.bat starts the same emulator with the new home screen panel
  (bottom half): LCD 1-3 = part R1/R2/R3, LCD 4-6 = VOLUME/BRIGHT/REVERB, the dial
  changes the value, LCD 6 DOWN switches reverb off/on. Your ROM files are not changed;
  the mod is applied in memory from mod\apply.lua.
- BRIGHT always reads 0 in the emulator (the faked sound board cannot report it); on
  the real keyboard it shows the actual value.

MIDI (hear what the emulated keyboard plays)
- The emulated PSR-9000 sends real MIDI out of its MIDI A port, exactly like the keyboard.
  There is no PSR sound in the emulator (the sound chips' samples are not available), but
  you can send that MIDI to a synth on the PC.
- Simple test: START_PSR9000_MIDI.bat sends MIDI A to "Microsoft GS Wavetable Synth"
  (Windows' built-in General MIDI synth).
- LIST_MIDI_PORTS.bat shows the port names on your PC. Edit the MIDIOUT / MIDIIN lines in
  START_PSR9000_MIDI.bat to pick others. MIDIIN lets a USB MIDI keyboard play the emulated PSR.
- VST synths (Keyscape, etc.):
    1. Install loopMIDI (free, Tobias Erichsen) and create a port, e.g. "loopMIDI Port".
    2. Set MIDIOUT=loopMIDI Port in START_PSR9000_MIDI.bat.
    3. In Keyscape standalone (or a DAW / VST host such as Reaper or Cantabile), choose
       "loopMIDI Port" as the MIDI input. In a DAW you can send each MIDI channel to a
       different instrument (R1 to Keyscape, drums to a drum VST, ...).
- What is sent depends on the keyboard's own MIDI settings (FUNCTION > MIDI): the template
  "All Parts" also sends the style parts (drums, bass, chords...).
- The real keyboard can do the same today without any mod: MIDI OUT -> USB MIDI interface ->
  PC running Keyscape.

Floppy disks and styles
- The floppy drive works: the "styles" folder has disk images (one per style category).
- Put a disk in: set FLOPPY=styles\8BEAT.img in the .bat file, or while running press Tab >
  File Manager > floppydisk and pick the .img file (change disks the same way).
- Load styles like on the keyboard: press a STYLE button > STYLE MANAGER (LCD 8) >
  LOAD STYLE INTO FLASH ROM (LCD A). LCD C switches SINGLE / MULTI / ALL (ALL = whole disk),
  then [NEXT], pick the Flash location, [NEXT], YES (LCD G). The styles then appear under the
  FLASH STYLE buttons and stay there (saved in the nvram folder).
- The style flash holds about 2 MB, like the real keyboard: a few categories, not all at once.
- Styles play (START/STOP) and their notes come out of MIDI OUT (see the MIDI section), but
  there is still no sound inside the emulator.
- Make your own disk images from .sty files: python tools/make_floppy.py out.img *.sty
  (in the GitHub repository).

Setlist (new home screen, side button G)
- The setlist is saved on the floppy as KYRANOS.SET. Use the empty disk image
  disks\SETLIST.img for it: set FLOPPY=disks\SETLIST.img in START_PSR9000_NEW_HOME.bat, or
  swap it in with Tab > File Manager. The emulator writes to the .img file, so the setlist is
  still there next time.
- Opening a song sends its FL configuration (MIDI channel 15) and key (channel 16) out of MIDI A
  and B, so with START_PSR9000_MIDI.bat and loopMIDI the FL Studio script can be tried on the PC.
- The registration memories are empty in a fresh emulator: store some first (MEMORY + REGIST n),
  otherwise the performance screen shows REGIST FAILED.
- How it works: docs/SETLIST.md in the GitHub repository.
