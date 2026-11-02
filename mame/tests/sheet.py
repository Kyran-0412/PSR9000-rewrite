import sys, glob
from PIL import Image, ImageDraw
n=sys.argv[1]
labels=[l.rstrip('\n').split(' ',2) for l in open(f'/tmp/emu/test/{n}.log') if l[:4].isdigit()]
fs=sorted(glob.glob(f'/tmp/emu/test/snap_{n}/*.png'))
per=12; cols=4
for s in range(0,len(fs),per):
    sheet=Image.new('RGB',(cols*330,((per+cols-1)//cols)*262),'white'); d=ImageDraw.Draw(sheet)
    for k,f in enumerate(fs[s:s+per]):
        im=Image.open(f).convert('RGB'); x=(k%cols)*330; y=(k//cols)*262
        sheet.paste(im,(x,y+18)); i=s+k
        d.text((x+2,y+3),f'{i} '+(labels[i][2] if i<len(labels) else '?'),fill='black')
    sheet.save(f'/tmp/emu/test/{n}_sheet{s//per:02d}.png')
print(n, len(fs),'snaps', len(labels),'labels')
