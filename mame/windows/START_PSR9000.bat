@echo off
rem Starts the PSR-9000 emulator. Put the Yamaha V3.06 files in roms\psr9000 first.
cd /d "%~dp0"
if not exist "roms\psr9000\p9000m.002" (
  echo Missing ROM files. Copy p9000m.001, p9000m.002, p9000m.003, p9000m.004 and p9000s.bin
  echo from Yamaha's PSR9000_OS306.zip into the folder roms\psr9000 next to this file.
  pause
  exit /b 1
)
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -skip_gameinfo -nofilter %*
