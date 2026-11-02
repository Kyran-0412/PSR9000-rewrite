@echo off
rem Starts the PSR-9000 emulator. Put the Yamaha V3.06 files in roms\psr9000 first.
rem FLOPPY = a disk image to put in the drive at start (e.g. styles\8BEAT.img), or empty.
rem Disks can also be changed while running: Tab > File Manager > floppydisk.
set FLOPPY=
cd /d "%~dp0"
if not exist "roms\psr9000\p9000m.002" (
  echo Missing ROM files. Copy p9000m.001, p9000m.002, p9000m.003, p9000m.004 and p9000s.bin
  echo from Yamaha's PSR9000_OS306.zip into the folder roms\psr9000 next to this file.
  pause
  exit /b 1
)
set FLOPT=
if not "%FLOPPY%"=="" set FLOPT=-flop "%FLOPPY%"
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -maximize -view "Centre Panel" -nounevenstretch -skip_gameinfo -nofilter %FLOPT% %*
