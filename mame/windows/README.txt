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
