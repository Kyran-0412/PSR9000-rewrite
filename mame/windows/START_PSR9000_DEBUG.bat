@echo off
rem Same as START_PSR9000_NEW_HOME.bat, but MAME also writes error.log (hardware log) in this folder.
rem If the screen freezes: note what you pressed, quit with Esc, and send error.log.
cd /d "%~dp0"
start "" psr9000.exe psr9000 -rompath roms -nvram_directory nvram -cfg_directory cfg -window -skip_gameinfo -nofilter -autoboot_script mod\apply.lua -log %*
