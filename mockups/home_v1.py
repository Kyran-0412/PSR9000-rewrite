"""Mockup of a proposed home screen (320x240, 1-bit), drawn in the PSR-9000 LCD colours.
Not firmware art: our own design, safe to commit. Run: python3 mockups/home_v1.py out.png"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 320, 240
img = Image.new('1', (W, H), 0)          # 0 = background, 1 = dark pixel
d = ImageDraw.Draw(img)
small = ImageFont.load_default()          # ~6x11 bitmap font
big = ImageFont.load_default(size=15)

def text(xy, s, f=small, fill=1, anchor='la'):
    d.fontmode = '1'
    d.text(xy, s, font=f, fill=fill, anchor=anchor)

# --- status bar (info only, no buttons) ---
d.rectangle((0, 0, W - 1, 13), fill=1)
text((4, 1), 'STYLE  Heart Beat', fill=0)
text((W - 4, 1), 'TEMPO 140   TRANS 0   OCT 0', fill=0, anchor='ra')

# --- active layers, centre of screen, display only ---
parts = [('R1', 'Live! Grand', True), ('R2', 'Live! Strs', True), ('R3', 'Cool! Organ', False)]
for i, (p, name, on) in enumerate(parts):
    y = 19 + i * 33
    box = (40, y, W - 41, y + 28)
    if on:
        d.rounded_rectangle(box, 5, fill=1)
        text((50, y + 7), p, fill=0)
        text((80, y + 6), name, big, fill=0)
        text((box[2] - 8, y + 9), 'ON', fill=0, anchor='ra')
    else:
        d.rounded_rectangle(box, 5, outline=1)
        text((50, y + 7), p)
        text((80, y + 6), name, big)
        text((box[2] - 8, y + 9), 'OFF', anchor='ra')
text((W // 2, 120), 'LEFT  Galaxy EP', anchor='ma')

# --- side buttons A-J: free for later (shown as empty tabs) ---
for i in range(5):
    y = 19 + i * 24
    for x0 in (0, W - 34):
        d.rounded_rectangle((x0, y, x0 + 33, y + 18), 3, outline=1)
text((17, 19 + 4 * 24 + 4), 'MORE', anchor='ma')   # J/E opens full mixer sub-menu
text((W - 17, 19 + 4 * 24 + 4), 'MIX', anchor='ma')

# --- 8-channel strip: UP = select, dial = change value ---
d.rectangle((0, 138, W - 1, 150), fill=1)
text((W // 2, 139), 'UP = SELECT     DIAL = ADJUST', fill=0, anchor='ma')
chans = [('VOL', 'R1', 100), ('VOL', 'R2', 82), ('VOL', 'R3', 64),
         ('BRT', 'R1', 64), ('BRT', 'R2', 80), ('BRT', 'R3', 64),
         ('REV', 'ON', 40), ('', '', None)]
sel = 3
for i, (kind, part, val) in enumerate(chans):
    x = i * 40
    if i:
        d.line((x, 152, x, H - 1), fill=1)
    if val is None:
        continue
    if i == sel:
        d.rectangle((x + 1, 152, x + 39, H - 1), fill=1)
    fg = 0 if i == sel else 1
    text((x + 20, 154), kind, fill=fg, anchor='ma')
    text((x + 20, 166), str(val), fill=fg, anchor='ma')
    # bar meter
    top, bot = 180, 224
    d.rectangle((x + 14, top, x + 26, bot), outline=fg)
    fillh = int((bot - top) * val / 127)
    d.rectangle((x + 14, bot - fillh, x + 26, bot), fill=fg)
    text((x + 20, 227), part, fill=fg, anchor='ma')

out = sys.argv[1] if len(sys.argv) > 1 else 'home_v1.png'
lcd = Image.new('RGB', (W, H))
lcd.putdata([(40, 40, 170) if v else (225, 226, 205) for v in img.get_flattened_data()])
lcd.resize((W * 3, H * 3), Image.NEAREST).save(out)
