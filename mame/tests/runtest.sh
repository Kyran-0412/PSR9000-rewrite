#!/bin/sh
# runtest.sh NAME : runs NAME.txt in chunks of CHUNK lines (MAME skips its warning screen
# only for -seconds_to_run <= 300), each from a copy of the base nvram; makes NAME_sheet*.png
N=$1; cd /tmp/emu/test; rm -rf snap_$N snap_${N}_part* nv_$N ${N}_part* ${N}.log ${N}_sheet*; mkdir -p snap_$N
grep -v '^quit$' $N.txt | split -l ${CHUNK:-120} -d -a 2 - ${N}_part
ulimit -f 2000000
for P in ${N}_part??; do
  echo quit >> $P; rm -rf nv_$N; cp -r /tmp/emu/nvram nv_$N
  STEPS=/tmp/emu/test/$P CRAWLLOG=/tmp/emu/test/$P.log SDL_VIDEODRIVER=dummy timeout 1500 /home/user/mame-build/mame/psr9000 psr9000 -rompath /home/user/PSR9000-rewrite/firmware/roms -video soft -sound none -nothrottle -skip_gameinfo -window -seconds_to_run 295 -snapshot_directory /tmp/emu/test/snap_$P -snapview native -nvram_directory /tmp/emu/test/nv_$N -autoboot_script /home/user/PSR9000-rewrite/mame/crawl.lua > $P.out 2>&1
  for f in $(ls snap_$P/psr9000/*.png 2>/dev/null); do cp $f snap_$N/$(ls snap_$N | wc -l | xargs printf '%05d').png; done
  cat $P.log >> $N.log
done
python3 /tmp/emu/test/sheet.py $N
