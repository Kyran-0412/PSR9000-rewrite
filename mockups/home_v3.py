"""Home screen mockup v3 (1-bit). Bottom row: buttons 1-3 select the part (R1/R2/R3),
buttons 4-8 select a control for that part, the dial changes the value.
Run: python3 mockups/home_v3.py out.png [sel_part 0-2] [sel_ctrl 0-4]"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
F = '/usr/share/fonts/opentype/inter/'
def font(w, s): return ImageFont.truetype(F + 'Inter-%s.otf' % w, s)
f_tiny, f_small, f_smallb = font('SemiBold', 8), font('Medium', 10), font('Bold', 10)
f_name, f_val, f_reg = font('Bold', 14), font('Bold', 13), font('Regular', 14)

sel_part = int(sys.argv[2]) if len(sys.argv) > 2 else 0
sel_ctrl = int(sys.argv[3]) if len(sys.argv) > 3 else 1

img = Image.new('1', (W, H), 1)
d = ImageDraw.Draw(img); d.fontmode = '1'
K, Wt = 0, 1
def t(xy, s, f, fill=K, anchor='la'): d.text(xy, s, font=f, fill=fill, anchor=anchor)
def dashed_box(b, r=0):
    x0, y0, x1, y1 = b
    for x in range(x0 + 6, x1 - 5, 4): d.point((x, y0), K); d.point((x, y1), K)
    for y in range(y0 + 6, y1 - 5, 4): d.point((x0, y), K); d.point((x1, y), K)

# example state per part: name, on, vol, bright, reverb(level,on), chorus, octave
parts = [dict(p='R1', name='Live! Grand', on=True,  v=[100, 64, 40, 0, 0], rev=True),
         dict(p='R2', name='Live! Strs',  on=True,  v=[82, 80, 64, 20, 0], rev=True),
         dict(p='R3', name='Cool! Organ', on=False, v=[64, 64, 40, 0, -1], rev=False)]
ctrls = ['VOLUME', 'BRIGHT', 'REVERB', 'CHORUS', 'OCTAVE']

# ---- status bar ----
t((8, 9), 'Heart Beat', f_smallb, anchor='lm')
x = W - 8
for label in ('OCT 0', 'TRP 0', '140 BPM'):
    w = d.textlength(label, font=f_tiny) + 10
    d.rounded_rectangle((x - w, 3, x, 15), 6, outline=K)
    t((x - w / 2, 9), label, f_tiny, anchor='mm'); x -= w + 4
d.line((0, 19, W, 19), fill=K)

# ---- layer cards (display only; selected part marked with a side tab) ----
for i, P in enumerate(parts):
    y = 25 + i * 31
    box = (30, y, W - 31, y + 26)
    on = P['on']
    if on:
        d.rounded_rectangle(box, 7, fill=K); fg = Wt
    else:
        dashed_box(box); fg = K
    d.rounded_rectangle((36, y + 6, 56, y + 19), 6, fill=Wt if on else None, outline=K if not on else None)
    t((46, y + 13), P['p'], f_tiny, K, 'mm')
    t((64, y + 13), P['name'], f_name if on else f_reg, fg, 'lm')
    t((box[2] - 8, y + 13), 'VOL %d  BRT %d' % (P['v'][0], P['v'][1]) if on else 'OFF', f_tiny, fg, 'rm')
    if i == sel_part:                       # selection marker: arrow tab on the left
        d.polygon([(22, y + 7), (27, y + 13), (22, y + 19)], fill=K)
t((W // 2, 124), 'LEFT   Galaxy EP', f_small, anchor='mm')

# ---- bottom row ----
top = 136
d.line((0, top, W, top), fill=K)
t((60, top + 9), 'PART', f_tiny, anchor='mm')
P = parts[sel_part]
t((200, top + 9), '%s  %s' % (P['p'], P['name'].upper()), f_tiny, anchor='mm')
for yy in range(top + 18, H - 3, 2): d.point((120, yy), K)

# part selectors (buttons 1-3)
for i, Q in enumerate(parts):
    x = i * 40
    b = (x + 5, top + 20, x + 35, top + 44)
    if i == sel_part:
        d.rounded_rectangle(b, 8, fill=K); fg = Wt
    else:
        d.rounded_rectangle(b, 8, outline=K); fg = K
    t((x + 20, top + 32), Q['p'], f_val, fg, 'mm')
    t((x + 20, top + 56), 'ON' if Q['on'] else 'OFF', f_tiny, anchor='mm')
    t((x + 20, top + 70), Q['name'].split()[-1][:6], f_tiny, anchor='mm')

# controls (buttons 4-8) for the selected part
for j, name in enumerate(ctrls):
    x = (j + 3) * 40
    val = P['v'][j]
    if j == sel_ctrl:
        d.rounded_rectangle((x + 3, top + 17, x + 37, H - 3), 7, fill=K); fg = Wt
    else:
        fg = K
    t((x + 20, top + 26), name, f_tiny, fg, 'mm')
    txt = ('%+d' % val if val else '0') if name == 'OCTAVE' else str(val)
    t((x + 20, top + 41), txt, f_val, fg, 'mm')
    y0, y1 = top + 54, H - 22
    if name == 'OCTAVE':                     # 5 dots for -2..+2
        for k in range(5):
            cy = y1 - k * (y1 - y0) // 4
            r = 3 if k - 2 == val else 1
            d.ellipse((x + 20 - r, cy - r, x + 20 + r, cy + r), fill=fg)
    else:
        for yy in range(y0, y1 + 1, 2): d.point((x + 20, yy), fg)
        fh = int((y1 - y0) * val / 127)
        if fh: d.rounded_rectangle((x + 17, y1 - fh, x + 23, y1), 3, fill=fg)
    if name == 'REVERB':
        on = P['rev']
        b = (x + 9, H - 16, x + 31, H - 6)
        if on: d.rounded_rectangle(b, 5, fill=fg); t((x + 20, H - 11), 'ON', f_tiny, Wt if fg == K else K, 'mm')
        else: d.rounded_rectangle(b, 5, outline=fg); t((x + 20, H - 11), 'OFF', f_tiny, fg, 'mm')

out = sys.argv[1] if len(sys.argv) > 1 else 'home_v3.png'
img.resize((W * 3, H * 3), Image.NEAREST).save(out)
