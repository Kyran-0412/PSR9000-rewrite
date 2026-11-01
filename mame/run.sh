#!/bin/sh
# run.sh seconds "shot times"   (keeps nvram between runs unless FRESH=1)
cd /tmp/emu && rm -rf snap error.log; [ "$FRESH" = 1 ] && rm -rf nvram
SHOTS="$2" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 900 /home/user/mame-build/mame/psr9000 psr9000 -rompath /home/user/PSR9000-rewrite/firmware/roms -video soft -sound none -nothrottle -skip_gameinfo -window -seconds_to_run $1 -snapshot_directory /tmp/emu/snap -nvram_directory /tmp/emu/nvram -autoboot_script snap3.lua -log > stdout.txt 2>&1
python3 - <<'PY'
import glob
from PIL import Image
fs=sorted(glob.glob('/tmp/emu/snap/psr9000/*.png'))
if fs:
    ims=[Image.open(f).convert('RGB') for f in fs]; w,h=ims[0].size; c=2; r=(len(ims)+1)//2
    sh=Image.new('RGB',(w*c+10,h*r+10*(r-1)),'gray')
    for k,im in enumerate(ims): sh.paste(im,((k%c)*(w+10),(k//c)*(h+10)))
    sh.save('/tmp/emu/snaps.png'); print(len(fs),'snaps')
PY
sed -E 's/= [0-9A-F]+ /= V /; s/\(0[0-9A-F]{7}\)/(PC)/' error.log | cut -c1-110 | sort | uniq -c | sort -rn | head -${3:-25}
