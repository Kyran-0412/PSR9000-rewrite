#!/bin/sh
# Run the PSR-9000 driver without a display, saving LCD snapshots and the error log.
# Usage: mame/run_headless.sh [seconds] [snapshot-interval]
# Needs: MAME built with mame/ympsr9000.cpp, ROM files in $ROMPATH/psr9000/
#   (p9000m.001 .. p9000m.004 from Yamaha's V3.06 update; not distributed here).
MAME=${MAME:-/home/user/mame-build/mame/psr9000}
ROMPATH=${ROMPATH:-$(dirname "$0")/../firmware/roms}
OUT=${OUT:-$(dirname "$0")/../firmware/emu_out}
mkdir -p "$OUT"
SNAP_TOTAL=${1:-20} SNAP_INTERVAL=${2:-5} SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$MAME" psr9000 -rompath "$ROMPATH" -snapshot_directory "$OUT" -snapname "%g/%i" \
  -video soft -sound none -nothrottle -skip_gameinfo -window -log \
  -autoboot_script "$(dirname "$0")/snap.lua" > "$OUT/stdout.txt" 2>&1
mv -f error.log "$OUT/error.log" 2>/dev/null
echo "snapshots and logs in $OUT"
