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
   The first boot also builds the internal memory (STYLE, CUSTOM VOICE, ...) like a
   factory reset. That is saved in the "nvram" folder, so later starts are quicker.

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
- Views (Tab > Video Options): Full Panel (default), Centre Panel (bigger screen),
  Screen Only.

Resetting the emulated keyboard: quit, then delete the "nvram" folder.
