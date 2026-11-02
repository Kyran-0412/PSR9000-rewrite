@echo off
rem Shows the MIDI port names Windows offers; copy one into START_PSR9000_MIDI.bat.
cd /d "%~dp0"
psr9000.exe -listmidi
pause
