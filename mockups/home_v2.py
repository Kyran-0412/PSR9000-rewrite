"""Home screen mockup v2: pure 1-bit (black/white), flatter 'modern' style.
Our own design (Inter font, OFL licence). Run: python3 mockups/home_v2.py out.png [scale]"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
F = '/usr/share/fonts/opentype/inter/'
def font(w, s): return ImageFont.truetype(F + 'Inter-%s.otf' % w, s)
f_tiny, f_small, f_smallb = font('SemiBold', 8), font('Medium', 10), font('Bold', 10)
f_name, f_val = font('Bold', 15), font('Bold', 13)

img = Image.new('1', (W, H), 1)              # 1 = white, 0 = black (for drawing)
d = ImageDraw.Draw(img); d.fontmode = '1'
K, Wt = 0, 1
def t(xy, s, f, fill=K, anchor='la'): d.text(xy, s, font=f, fill=fill, anchor=anchor)
def dotted_h(x0, x1, y):
    for x in range(x0, x1, 2): d.point((x, y), K)

# ---- status bar -------------------------------------------------------
t((8, 9), 'Heart Beat', f_smallb, anchor='lm')
x = W - 8
for label in ('OCT 0', 'TRP 0', '140 BPM'):
    w = d.textlength(label, font=f_tiny) + 10
    d.rounded_rectangle((x - w, 3, x, 15), 6, outline=K)
    t((x - w / 2, 9), label, f_tiny, anchor='mm')
    x -= w + 4
d.line((0, 19, W, 19), fill=K)

# ---- active layers (display only) ----------------------------------------
parts = [('R1', 'Live! Grand', True), ('R2', 'Live! Strs', True), ('R3', 'Cool! Organ', False)]
for i, (p, name, on) in enumerate(parts):
    y = 25 + i * 32
    box = (38, y, W - 39, y + 27)
    if on:
        d.rounded_rectangle(box, 7, fill=K)
        d.rounded_rectangle((44, y + 7, 64, y + 20), 6, fill=Wt)
        t((54, y + 14), p, f_tiny, K, 'mm')
        t((74, y + 14), name, f_name, Wt, 'lm')
        d.ellipse((box[2] - 14, y + 11, box[2] - 9, y + 16), fill=Wt)
    else:
        # inactive: dashed outline, light text
        for xx in range(box[0] + 6, box[2] - 6, 4):
            d.point((xx, y), K); d.point((xx, y + 27), K)
        for yy in range(y + 6, y + 22, 4):
            d.point((box[0], yy), K); d.point((box[2], yy), K)
        d.rounded_rectangle((44, y + 7, 64, y + 20), 6, outline=K)
        t((54, y + 14), p, f_tiny, K, 'mm')
        t((74, y + 14), name, font('Regular', 15), K, 'lm')
        t((box[2] - 8, y + 14), 'OFF', f_tiny, K, 'rm')
t((W // 2, 131), 'LEFT   Galaxy EP', f_small, anchor='mm')

# ---- side buttons: only draw the ones that do something -------------------
def side(i, label, right):
    y = 25 + i * 24 + 10
    if right:
        d.line((W - 6, y, W - 1, y), fill=K); t((W - 9, y), label, f_tiny, anchor='rm')
    else:
        d.line((0, y, 5, y), fill=K); t((8, y), label, f_tiny, anchor='lm')
side(4, 'MIXER', False)

# ---- channel strip ------------------------------------------------------
top = 146
d.line((0, top, W, top), fill=K)
groups = [(0, 3, 'VOLUME'), (3, 6, 'BRIGHTNESS'), (6, 8, 'REVERB')]
for a, b, name in groups:
    x0, x1 = a * 40 + 6, b * 40 - 6
    t(((x0 + x1) / 2, top + 9), name, f_tiny, anchor='mm')
chans = [('R1', 100), ('R2', 82), ('R3', 64), ('R1', 64), ('R2', 80), ('R3', 64),
         ('ALL', 40), (None, None)]
sel = 3
for i, (lab, val) in enumerate(chans):
    x = i * 40
    if i in (3, 6):
        for yy in range(top + 18, H - 4, 2): d.point((x, yy), K)
    if lab is None:
        continue
    fg = K
    if i == sel:
        d.rounded_rectangle((x + 3, top + 17, x + 37, H - 3), 7, fill=K); fg = Wt
    t((x + 20, top + 28), str(val), f_val, fg, 'mm')
    # thin rounded meter
    y0, y1 = top + 40, H - 22
    for yy in range(y0, y1 + 1, 2): d.point((x + 20, yy), fg)       # dotted track
    fh = int((y1 - y0) * val / 127)
    d.rounded_rectangle((x + 17, y1 - fh, x + 23, y1), 3, fill=fg)
    t((x + 20, H - 12), lab, f_tiny, fg, 'mm')
# reverb on/off pill on the spare slot's neighbour
x = 7 * 40
d.rounded_rectangle((x + 6, top + 22, x + 34, top + 34), 6, fill=K)
t((x + 20, top + 28), 'ON', f_tiny, Wt, 'mm')

out = sys.argv[1] if len(sys.argv) > 1 else 'home_v2.png'
sc = int(sys.argv[2]) if len(sys.argv) > 2 else 3
img.resize((W * sc, H * sc), Image.NEAREST).save(out)
