@echo off
rem Starts the PSR-9000 emulator WITH the new home panel (patched in memory, ROM files untouched).
rem FLOPPY = a disk image to put in the drive at start (e.g. styles\8BEAT.img), or empty.
rem Disks can also be changed while running: Tab > File Manager > floppydisk.
set FLOPPY=
cd /d "%~dp0"
if not exist "roms\psr9000\p9000m.002" (
  echo Missing ROM files. See README.txt.
  pause
  exit /b 1
)
set FLOPT=
if not "%FLOPPY%"=="" set FLOPT=-flop "%FLOPPY%"
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -maximize -view "Centre Panel" -nounevenstretch -skip_gameinfo -nofilter -autoboot_script mod\apply.lua %FLOPT% %*
