@echo off
rem Starts the PSR-9000 emulator WITH the new home panel (patched in memory, ROM files untouched).
cd /d "%~dp0"
if not exist "roms\psr9000\p9000m.002" (
  echo Missing ROM files. See README.txt.
  pause
  exit /b 1
)
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -skip_gameinfo -nofilter -autoboot_script mod\apply.lua %*
