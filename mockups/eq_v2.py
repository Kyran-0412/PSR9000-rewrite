"""Master EQ page mockup v2 = v1 as its own page, opened from the home screen with side
button F; EXIT goes back home. The stock Mixing Console M.EQ page stays as it is.
v1 (1-bit, same style as home_v7).
Graph of the 5-band curve (line through the band points, like a DAW EQ), band select on
LCD 1-5, LCD 6 = Q, LCD 7 = GAIN, LCD 8 = FREQ (what the dial changes), dial = value.
Band frequencies/Q below are examples; the real ranges come from the firmware later.
Run: python3 mockups/eq_v1.py out.png [band 0-4] [mode 0=Q 1=GAIN 2=FREQ]"""
import sys, math, cmath
from PIL import Image, ImageDraw, ImageFont, ImageOps

W, H = 320, 240
F = '/usr/share/fonts/opentype/inter/'
def font(w, s): return ImageFont.truetype(F + 'Inter-%s.otf' % w, s)
f_tiny, f_small, f_val = font('Bold', 8), font('Bold', 10), font('Bold', 13)
sel = int(sys.argv[2]) if len(sys.argv) > 2 else 0
mode = int(sys.argv[3]) if len(sys.argv) > 3 else 1
K, Wt = 0, 1
img = Image.new('1', (W, H), 1)
d = ImageDraw.Draw(img); d.fontmode = '1'
def t(xy, s, f, fill=K, anchor='la'): d.text(xy, s, font=f, fill=fill, anchor=anchor)

bands = [dict(f=70, q=2.5, g=2), dict(f=250, q=1.2, g=-1), dict(f=1000, q=1.0, g=-2),
         dict(f=4000, q=1.4, g=0), dict(f=12000, q=0.8, g=3)]
GMAX = 12

def peak_db(fr, b, fs=44100.0):          # RBJ peaking filter magnitude in dB
    A = 10 ** (b['g'] / 40); w0 = 2 * math.pi * b['f'] / fs; al = math.sin(w0) / (2 * b['q'])
    z = cmath.exp(-1j * 2 * math.pi * fr / fs)
    num = (1 + al * A) - 2 * math.cos(w0) * z + (1 - al * A) * z * z
    den = (1 + al / A) - 2 * math.cos(w0) * z + (1 - al / A) * z * z
    return 20 * math.log10(abs(num / den))

# ---- status bar ----
d.rectangle((0, 0, W - 1, 18), fill=K)
t((8, 9), 'MASTER EQ', f_small, Wt, 'lm')
t((W - 8, 9), 'EXIT TO HOME', f_tiny, Wt, 'rm')
t((160, 9), 'USER 1', f_small, Wt, 'mm')

# ---- dotted texture, graph card on top ----
for yy in range(19, 136):
    for xx in range(W):
        if (yy % 4 == 0 and xx % 4 == 0) or (yy % 4 == 2 and xx % 4 == 2): d.point((xx, yy), K)
card = (6, 23, W - 7, 131)
d.rounded_rectangle(card, 7, fill=Wt, outline=K, width=2)
gx0, gx1, gy0, gy1 = 30, W - 14, 30, 116          # plot area
def fx(fr): return gx0 + (gx1 - gx0) * math.log10(fr / 20) / 3       # 20 Hz .. 20 kHz
def gy(db): return (gy0 + gy1) / 2 - (gy1 - gy0) / 2 * db / GMAX
for db in (-12, -6, 6, 12):                       # dotted dB lines
    y = round(gy(db))
    for xx in range(gx0, gx1, 3): d.point((xx, y), K)
    t((gx0 - 4, y), '%+d' % db, f_tiny, anchor='rm')
y = round(gy(0)); d.line((gx0, y, gx1, y), fill=K); t((gx0 - 4, y), '0', f_tiny, anchor='rm')
for fr, lab in ((100, '100'), (1000, '1k'), (10000, '10k')):
    x = round(fx(fr))
    for yy in range(gy0, gy1, 3): d.point((x, yy), K)
    t((x, 124), lab, f_tiny, anchor='mm')

# curve: light dither between curve and 0 dB, then a 2 px line
pts = []
for x in range(gx0, gx1 + 1):
    fr = 20 * 10 ** (3 * (x - gx0) / (gx1 - gx0))
    pts.append((x, gy(max(-GMAX, min(GMAX, sum(peak_db(fr, b) for b in bands))))))
y0 = gy(0)
for x, y in pts:
    for yy in range(round(min(y, y0)), round(max(y, y0)) + 1):
        if (x + yy) % 2 == 0 and yy % 2 == 0: d.point((x, yy), K)
d.line(pts, fill=K, width=2)
# band points (selected = filled, bigger)
for i, b in enumerate(bands):
    x, y = fx(b['f']), gy(b['g'])
    if i == sel:
        d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=K); t((x, y), str(i + 1), f_tiny, Wt, 'mm')
    else:
        d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=Wt, outline=K, width=2); t((x, y), str(i + 1), f_tiny, K, 'mm')

# ---- bottom panel (drawn as ink-on-paper, then inverted like home_v7) ----
top = 136
d.rectangle((0, top, W, H), fill=Wt)
B = bands[sel]
fs = '%d Hz' % B['f'] if B['f'] < 1000 else '%g kHz' % (B['f'] / 1000)
t((100, top + 9), 'BAND', f_tiny, anchor='mm')
t((260, top + 9), 'EQ%d  %s  Q %.1f' % (sel + 1, fs, B['q']), f_tiny, anchor='mm')
for yy in range(top + 18, H - 3, 2): d.point((200, yy), K)
for i, b in enumerate(bands):                     # LCD 1-5: band select, shows its gain
    x = i * 40
    bx = (x + 5, top + 20, x + 35, top + 44)
    if i == sel: d.rounded_rectangle(bx, 8, fill=K); fg = Wt
    else: d.rounded_rectangle(bx, 8, outline=K, width=2); fg = K
    t((x + 20, top + 32), 'EQ%d' % (i + 1), f_small, fg, 'mm')
    t((x + 20, top + 56), '%+ddB' % b['g'] if b['g'] else '0dB', f_tiny, anchor='mm')
    fl = '%d' % b['f'] if b['f'] < 1000 else '%gk' % (b['f'] / 1000)
    t((x + 20, top + 70), fl, f_tiny, anchor='mm')
modes = [('Q', '%.1f' % B['q']), ('GAIN', ('%+d' % B['g']) if B['g'] else '0'), ('FREQ', fl if False else (('%d' % B['f']) if B['f'] < 1000 else '%gk' % (B['f'] / 1000)))]
for j, (name, val) in enumerate(modes):          # LCD 6-8: what the dial changes
    x = (j + 5) * 40
    bx = (x + 1, top + 17, x + 38, H - 3)
    if j == mode: d.rounded_rectangle(bx, 7, fill=K); fg = Wt
    else: d.rounded_rectangle(bx, 7, outline=K, width=1); fg = K
    t((x + 20, top + 29), name, f_tiny, fg, 'mm')
    t((x + 20, top + 47), val, f_val, fg, 'mm')
    t((x + 20, H - 16), 'dB' if name == 'GAIN' else 'Hz' if name == 'FREQ' else '', f_tiny, fg, 'mm')
bottom = img.crop((0, top, W, H))
img.paste(ImageOps.invert(bottom.convert('L')).convert('1'), (0, top))

out = sys.argv[1] if len(sys.argv) > 1 else 'eq_v2.png'
rgb = Image.new('RGB', (W, H)); rgb.putdata([(0xfb, 0xf3, 0xa8) if v else (0x36, 0x41, 0xcf) for v in img.get_flattened_data()])
rgb.resize((W * 3, H * 3), Image.NEAREST).save(out)
