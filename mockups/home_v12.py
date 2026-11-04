"""Home screen mockup v12 = v11 + a "SET" tab next to side button G (under the EQ tab):
opens Setlist mode (mockups/setlist_v1.py).
v11 = v10 with real fader caps (a cap slides up a dotted track instead of
a filling bar) and the selected control's name pill sized to the name.
v10 = v9 with Yamaha's own bitmap fonts (read at run time from
firmware/main_prog.bin, not copied: font table 0x357B08) instead of Inter, labels in capitals:
font 1 (7 px bold) for labels, font 3 (10 px) for values and the status bar, font 4 (15 px) for
voice names. Control names shortened to VOL / BRT / REV / PAN (Yamaha letters are 7 px wide,
the columns 40 px).
v9 = v8 + PAN on LCD 8 (a knob, not a meter), no borders around the
controls: when selected, the dot above the name is filled and the name is inverted, dotted lines between the controls.
v8 = v7 + an "EQ" tab at the top right, next to side button F
(opens the new Master EQ page, mockups/eq_v2.py).
v7 (1-bit): v6 with Yamaha-style depth: dotted texture behind the
layer area, solid panels behind the off layers and the LEFT line, and the bottom section
inverted (blue panel, yellow text, selected items in yellow).
v6: v5 with a Yamaha-style status bar (filled bar, inverted
text, stock transpose/octave icons, no boxes, no BPM) and the stock metronome icon under the
TEMPO box; nothing above the controls while TEMPO is selected.
Icons are read from firmware/roms/psr9000/p9000m.001 when present (not in git).
v5: bottom row moved one button right and a TEMPO
box on LCD 1 (selected: the dial changes tempo; selecting R1-R3 gives the dial back to the
controls). Run: python3 home_v5.py out.png [sel_part 0-2, or -1 = TEMPO] [sel_ctrl 0-2] Bottom row: buttons 1-3 select the part (R1/R2/R3),
buttons 4-8 select a control for that part, the dial changes the value.
Run: python3 mockups/home_v3.py out.png [sel_part 0-2] [sel_ctrl 0-4]"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
F = '/usr/share/fonts/opentype/inter/'
def font(w, s): return ImageFont.truetype(F + 'Inter-%s.otf' % w, s)
f_tiny, f_small, f_smallb = font('Bold', 8), font('Bold', 10), font('Bold', 10)
f_name, f_val, f_reg = font('Bold', 14), font('Bold', 13), font('Bold', 14)

sel_part_arg = int(sys.argv[2]) if len(sys.argv) > 2 else 0
tempo_sel = sel_part_arg < 0
sel_part = 0 if tempo_sel else sel_part_arg
sel_ctrl = int(sys.argv[3]) if len(sys.argv) > 3 else 1

img = Image.new('1', (W, H), 1)
d = ImageDraw.Draw(img); d.fontmode = '1'
K, Wt = 0, 1
# ---- Yamaha bitmap fonts (main_prog.bin 0x357B08: 9 font pointers; per char 8 bytes:
# u32 glyph pointer, u8 width; glyph rows of ceil(w/8) bytes, height table 0x357B2C) ----
import os as _os, struct as _st
_MP = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), '..', 'firmware', 'main_prog.bin')
_rom = open(_MP, 'rb').read()
_FT = _st.unpack('>9I', _rom[0x357B08:0x357B2C]); _FH = list(_rom[0x357B2C:0x357B35])
YF = {id(f_tiny): 1, id(f_small): 1, id(f_smallb): 3, id(f_val): 3, id(f_name): 4, id(f_reg): 4}
def _g(fi, c):
    p, w = _st.unpack('>IB', _rom[_FT[fi] + ord(c) * 8:_FT[fi] + ord(c) * 8 + 5]); return p, w
def tlen(s, f): return sum(_g(YF[id(f)], c)[1] for c in s)
def t(xy, s, f, fill=K, anchor='la'):
    fi = YF[id(f)]; h = _FH[fi]
    x, y = xy
    w = tlen(s, f) - 1                       # last column of the last glyph is spacing
    if anchor[0] == 'm': x -= w // 2
    elif anchor[0] == 'r': x -= w
    if anchor[1] == 'm': y -= {1: 3, 3: 5, 4: 7}[fi]   # centre of the capitals
    for c in s:
        p, gw = _g(fi, c); bpr = (gw + 7) // 8
        for r in range(h):
            for k in range(gw):
                if _rom[p + r * bpr + k // 8] >> (7 - k % 8) & 1: d.point((x + k, y + r), fill)
        x += gw
def t_fit_old(xy, s, f, fill, avail):  # centred; letter spacing -1 px if the text would not fit
    w = d.textlength(s, font=f)
    if w <= avail:
        return t(xy, s, f, fill, 'mm')
    adv = [int(f.getlength(c)) - 1 for c in s]
    x = xy[0] - (sum(adv) + 1) // 2
    for c, a in zip(s, adv):
        d.text((x, xy[1]), c, font=f, fill=fill, anchor='lm'); x += a
def t_fit(xy, s, f, fill, avail): t(xy, s, f, fill, 'mm')
def dashed_box(b, r=0):            # 2x2 dashes every 5 px
    x0, y0, x1, y1 = b
    for x in range(x0 + 6, x1 - 6, 5): d.rectangle((x, y0, x + 1, y0 + 1), fill=K); d.rectangle((x, y1 - 1, x + 1, y1), fill=K)
    for y in range(y0 + 6, y1 - 6, 5): d.rectangle((x0, y, x0 + 1, y + 1), fill=K); d.rectangle((x1 - 1, y, x1, y + 1), fill=K)

# example state per part: name, on, vol, bright, reverb(level,on), chorus, octave
parts = [dict(p='R1', name='Live! Grand', on=True,  v=[100, 64, 40, 64, 0], rev=True),
         dict(p='R2', name='Live! Strs',  on=True,  v=[82, 80, 64, 44, 0], rev=True),
         dict(p='R3', name='Cool! Organ', on=False, v=[64, 64, 40, 64, -1], rev=False)]
ctrls = ['VOL', 'BRT', 'REV', 'PAN']
BPM = 140

# ---- status bar (Yamaha style) ----
import os, struct
RES = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'firmware', 'roms', 'psr9000', 'p9000m.001')
res = open(RES, 'rb').read() if os.path.exists(RES) else None
def icon(rec, sx, sy, w, h, dx, dy, inv=0):
    # stock image bits: 1 = paper, 0 = ink (blue on the real LCD); here ink = K
    if not res:
        d.rectangle((dx, dy, dx + w - 1, dy + h - 1), outline=Wt if inv else K); return
    ww, hh, fl, p = struct.unpack('>HHII', res[rec - 0x4F0000:rec - 0x4F0000 + 12])
    o = p - 0x4F0000
    if fl == 0:
        bpr = (ww + 7) // 8
        get = lambda x, y: (res[o + y * bpr + x // 8] >> (7 - x % 8)) & 1
    else:
        n = struct.unpack('>H', res[o:o + 2])[0]; pxs = []; x = 0
        for b in res[o + 2:o + 2 + n]:
            bits = [(b >> k) & 1 for k in range(6, -1, -1)][:ww - x] if b < 0x80 else [1 if b & 0x40 else 0] * ((b & 0x3F) + 1)
            for v in bits:
                pxs.append(v); x += 1
                if x == ww:
                    x = 0
                    if b < 0x80: break
        get = lambda x, y: pxs[y * ww + x]
    for y in range(h):
        for x in range(w):
            v = get(sx + x, sy + y) ^ inv
            d.point((dx + x, dy + y), Wt if v else K)
d.rectangle((0, 0, W - 1, 18), fill=K)
t((8, 9), 'Heart Beat', f_smallb, Wt, 'lm')
x = W - 8
t((x, 9), '0', f_smallb, Wt, 'rm'); x -= tlen('0', f_smallb) + 4
icon(0x5F7BAC, 0, 0, 19, 10, x - 19, 4); x -= 19 + 12
t((x, 9), '0', f_smallb, Wt, 'rm'); x -= tlen('0', f_smallb) + 3
icon(0x5F7CBC, 0, 0, 23, 12, x - 23, 3, 1)

# ---- dotted texture behind the layer area (Yamaha style: 1 ink dot in 8 pixels) ----
for yy in range(19, 136):
    for xx in range(W):
        if (yy % 4 == 0 and xx % 4 == 0) or (yy % 4 == 2 and xx % 4 == 2):
            d.point((xx, yy), K)

# ---- layer cards (display only; selected part marked with a side tab) ----
for i, P in enumerate(parts):
    y = 25 + i * 31
    box = (30, y, W - 31, y + 26)
    on = P['on']
    if on:
        d.rounded_rectangle(box, 7, fill=K); fg = Wt
    else:
        d.rounded_rectangle(box, 7, fill=Wt, outline=K, width=2); fg = K
    d.rounded_rectangle((36, y + 6, 56, y + 19), 6, fill=Wt if on else None, outline=K if not on else None, width=2)
    t((46, y + 13), P['p'], f_tiny, K, 'mm')
    t((64, y + 13), P['name'], f_name if on else f_reg, fg, 'lm')
    t((box[2] - 8, y + 13), 'VOL %d  BRT %d' % (P['v'][0], P['v'][1]) if on else 'OFF', f_tiny, fg, 'rm')
    if i == sel_part and not tempo_sel:     # selection marker: arrow tab on the left
        d.polygon([(22, y + 7), (27, y + 13), (22, y + 19)], fill=K)
lw = tlen('LEFT  Galaxy EP', f_small)
d.rounded_rectangle((W // 2 - lw // 2 - 8, 117, W // 2 + lw // 2 + 8, 131), 6, fill=Wt, outline=K, width=2)
t((W // 2, 124), 'LEFT  Galaxy EP', f_small, anchor='mm')

# ---- v8: side button F (top right) opens the Master EQ page ----
d.rounded_rectangle((W - 28, 20, W - 2, 36), 6, fill=K)
t((W - 18, 28), 'EQ', f_tiny, Wt, 'mm')
d.polygon([(W - 9, 24), (W - 5, 28), (W - 9, 32)], fill=Wt)
d.rounded_rectangle((W - 28, 59, W - 2, 75), 6, fill=K)          # G: Setlist
t((W - 18, 67), 'SET', f_tiny, Wt, 'mm')
d.polygon([(W - 7, 63), (W - 4, 67), (W - 7, 71)], fill=Wt)

# ---- bottom row ----
top = 136
d.line((0, top, W, top), fill=K)
t((100, top + 9), 'PART', f_tiny, anchor='mm')
P = parts[sel_part]
t((240, top + 9), '' if tempo_sel else '%s  %s' % (P['p'], P['name'].upper()), f_tiny, anchor='mm')
for yy in range(top + 18, H - 3, 2): d.point((40, yy), K); d.point((160, yy), K)   # dotted group dividers

# tempo box (button 1)
b = (5, top + 20, 35, top + 44)
if tempo_sel:
    d.rounded_rectangle(b, 8, fill=K); fg = Wt
else:
    d.rounded_rectangle(b, 8, outline=K, width=2); fg = K
t((20, top + 32), str(BPM), f_val, fg, 'mm')
icon(0x5F7B6C, 135, 133, 14, 12, 13, top + 50)
t((20, top + 70), 'BPM', f_tiny, anchor='mm')

# part selectors (buttons 1-3)
for i, Q in enumerate(parts):
    x = (i + 1) * 40
    b = (x + 5, top + 20, x + 35, top + 44)
    if i == sel_part and not tempo_sel:
        d.rounded_rectangle(b, 8, fill=K); fg = Wt
    else:
        d.rounded_rectangle(b, 8, outline=K, width=2); fg = K
    t((x + 20, top + 32), Q['p'], f_val, fg, 'mm')
    t((x + 20, top + 56), 'ON' if Q['on'] else 'OFF', f_tiny, anchor='mm')
    t((x + 20, top + 70), Q['name'].split()[-1][:5].upper(), f_tiny, anchor='mm')

# controls (buttons 5-8) for the selected part: no borders; a dot above the name is filled
# when the control is selected; dotted lines between the controls
import math
for j in (1, 2, 3):
    for yy in range(top + 18, H - 3, 2): d.point(((j + 4) * 40, yy), K)
for j, name in enumerate(ctrls):
    x = (j + 4) * 40
    val = P['v'][j]
    sel = j == sel_ctrl and not tempo_sel
    cx = x + 20
    if sel: d.ellipse((cx - 3, top + 18, cx + 3, top + 24), fill=K)
    else: d.ellipse((cx - 3, top + 18, cx + 3, top + 24), outline=K)
    if sel:                                  # selected: name inverted on a small pill
        pw = tlen(name, f_tiny) - 1 + 8
        d.rounded_rectangle((cx - pw // 2, top + 26, cx - pw // 2 + pw - 1, top + 36), 5, fill=K)
    t_fit((cx, top + 31), name, f_tiny, Wt if sel else K, 36)
    if name == 'PAN':
        txt = 'C' if val == 64 else ('L%d' % (64 - val) if val < 64 else 'R%d' % (val - 64))
    else:
        txt = str(val)
    t((cx, top + 45), txt, f_val, K, "mm")
    y0, y1 = top + 57, H - 22
    if name == 'PAN':                        # knob: -135 deg (L63) .. +135 deg (R63)
        cy, r = 205, 10
        d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=K, width=2)
        a = math.radians((val - 64) * 135 / 63)
        d.line((cx, cy, cx + round(math.sin(a) * (r - 2)), cy - round(math.cos(a) * (r - 2))), fill=K, width=2)
        d.point((cx, cy - r - 3), K)          # centre mark
        t((cx - 13, cy + 10), 'L', f_tiny, K, 'mm'); t((cx + 13, cy + 10), 'R', f_tiny, K, 'mm')
    else:
        for yy in range(y0, y1 + 1, 2): d.point((cx, yy), K)
        yc = y1 - 2 - int((y1 - y0 - 4) * val / 127)      # fader cap with a grip line
        d.rounded_rectangle((cx - 6, yc - 2, cx + 6, yc + 2), 2, fill=K)
        d.line((cx - 4, yc, cx + 4, yc), fill=Wt)
    if name == 'REV':
        on = P['rev']
        b = (x + 6, H - 16, x + 34, H - 6)
        d.rounded_rectangle(b, 5, fill=K)       # filled either way, text says ON / OFF
        t((cx, H - 11), 'ON' if on else 'OFF', f_tiny, Wt, 'mm')

# bottom section inverted (blue panel)
from PIL import ImageOps
bottom = img.crop((0, 136, W, H))
img.paste(ImageOps.invert(bottom.convert('L')).convert('1'), (0, 136))
out = sys.argv[1] if len(sys.argv) > 1 else 'home_v12.png'
rgb = Image.new('RGB', (W, H)); rgb.putdata([(0xfb, 0xf3, 0xa8) if v else (0x36, 0x41, 0xcf) for v in img.get_flattened_data()])
rgb.resize((W * 3, H * 3), Image.NEAREST).save(out)
