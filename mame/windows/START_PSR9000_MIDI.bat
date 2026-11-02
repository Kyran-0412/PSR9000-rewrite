@echo off
rem Starts the emulator (new home screen) with MIDI A connected to the PC.
rem MIDIOUT = where the emulated keyboard sends its MIDI (see LIST_MIDI_PORTS.bat for names):
rem   "Microsoft GS Wavetable Synth"  = Windows' built-in synth (simple test)
rem   "loopMIDI Port"                 = virtual cable to a VST host / DAW (Keyscape etc.)
rem MIDIIN  = a MIDI keyboard to play the emulated PSR with (leave empty for none).
set MIDIOUT=Microsoft GS Wavetable Synth
set MIDIIN=
rem FLOPPY = a disk image to put in the drive at start (e.g. styles\8BEAT.img), or empty.
rem Disks can also be changed while running: Tab > File Manager > floppydisk.
set FLOPPY=
cd /d "%~dp0"
if not exist "roms\psr9000\p9000m.002" (
  echo Missing ROM files. See README.txt.
  pause
  exit /b 1
)
set INOPT=
if not "%MIDIIN%"=="" set INOPT=-midiin1 "%MIDIIN%"
set FLOPT=
if not "%FLOPPY%"=="" set FLOPT=-flop "%FLOPPY%"
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -maximize -view "Centre Panel" -nounevenstretch -skip_gameinfo -nofilter -autoboot_script mod\apply.lua -midiout1 "%MIDIOUT%" %INOPT% %FLOPT% %*
