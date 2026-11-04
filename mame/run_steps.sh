#!/bin/bash
# Scripted emulator run with mame/crawl.lua, restarting when MAME hangs at start-up.
#   mame/run_steps.sh DIR SECONDS [extra MAME args, e.g. -flop DIR/disk.img]
# DIR holds st.txt (crawl steps) and optionally pre.lua (run first, e.g. a dofile of
# mod/home/build/apply.lua) and disk0.img (copied to DIR/disk.img before every try).
# Output: DIR/c.log (snapshot list), DIR/snap/psr9000/*.png, DIR/o.txt (MAME output).
# Environment: MAME (default /home/user/mame-build/mame), ROMPATH (default firmware/roms),
# NVRAM (prepared nvram folder, default /tmp/emu/nvram; copied, never written).
HERE=$(cd "$(dirname "$0")/.." && pwd)
D=$(readlink -f "$1"); S=$2; shift 2
MAME=${MAME:-/home/user/mame-build/mame}
ROMPATH=${ROMPATH:-$HERE/firmware/roms}
NVRAM=${NVRAM:-/tmp/emu/nvram}
for try in 1 2 3 4; do
  rm -rf "$D/nv" "$D/snap" "$D/alive"; cp -r "$NVRAM" "$D/nv"
  [ -f "$D/disk0.img" ] && cp "$D/disk0.img" "$D/disk.img"
  { echo "local _al = false"
    echo "emu.register_periodic(function() if not _al and manager.machine.time:as_double() > 1 then _al = true; io.open(\"$D/alive\", \"w\"):close() end end)"
    [ -f "$D/pre.lua" ] && echo "dofile(\"$D/pre.lua\")"
    cat "$HERE/mame/crawl.lua"; } > "$D/run.lua"
  ( cd "$MAME" && STEPS="$D/st.txt" CRAWLLOG="$D/c.log" SDL_VIDEODRIVER=dummy exec timeout 1200 ./psr9000 psr9000 \
      -rompath "$ROMPATH" -video soft -sound none -nothrottle -skip_gameinfo -window -seconds_to_run "$S" \
      -snapshot_directory "$D/snap" -snapview native -nvram_directory "$D/nv" -autoboot_script "$D/run.lua" "$@" \
      > "$D/o.txt" 2>&1 ) &
  pid=$!
  for i in $(seq 40); do [ -f "$D/alive" ] && break; sleep 1; done
  if [ -f "$D/alive" ]; then wait $pid; exit $?; fi
  echo "start-up hang, retry $try" >&2; pkill -9 -P $pid; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
done
exit 1
