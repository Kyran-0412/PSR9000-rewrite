"""KyranOS boot splash (320x240, 1-bit), drawn like Yamaha's own splash: shading made of dither
patterns so it still looks 3D with two colours.

  python3 mockups/splash_v1.py [out_dir]   -> splash_preview.png (LCD colours, 3x),
                                             splash.bin (Yamaha compressed format, for the mod)

Pixel value 1 = the light LCD colour (pastel yellow), 0 = blue: the same way round as
Yamaha's splash, so it shows like the stock one does on the keyboard.
"""
import os, struct, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

W, H = 320, 240
F = '/usr/share/fonts/opentype/inter/'
def font(w, s): return ImageFont.truetype(F + 'Inter-%s.otf' % w, s)

BAYER8 = [[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
          [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
          [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
          [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]]

def dither(g):
    """ordered (Bayer) dither: clean regular patterns, like printed shading"""
    out = Image.new('1', g.size, 0)
    gp, op = g.load(), out.load()
    for y in range(g.height):
        for x in range(g.width):
            op[x, y] = 1 if gp[x, y] > (BAYER8[y % 8][x % 8] + 0.5) * 4 else 0
    return out

# ---------------------------------------------------------------- shading (greyscale)
g = Image.new('L', (W, H), 0)
d = ImageDraw.Draw(g)

# soft glow behind the title (kept faint so the dither stays calm)
glow = Image.new('L', (W, H), 0)
ImageDraw.Draw(glow).ellipse((70, 30, 250, 130), fill=70)
glow = glow.filter(ImageFilter.GaussianBlur(26))
g = ImageChops.lighter(g, glow)

bits = dither(g)
bd = ImageDraw.Draw(bits)
bd.fontmode = '1'

# perspective grid floor (crisp lines): horizon y=150, vanishing point x=160
HOR, VX = 150, 160
bd.line((0, HOR, W, HOR), fill=1)
for i in range(-9, 10):
    xb = VX + i * 46
    for t in range(0, 401):
        y = HOR + (H - HOR) * t / 400
        x = VX + (xb - VX) * t / 400
        if 0 <= x < W and HOR + 6 <= int(y) < H:
            bits.putpixel((int(x), int(y)), 1)
for dy in (6, 11, 18, 28, 42, 62, 86):        # perspective spacing
    bd.line((0, HOR + dy, W, HOR + dy), fill=1)

# 3D title: dithered extrusion + solid front face + outline
title_font = font('Black', 60)
TITLE = 'KyranOS'
tw = bd.textlength(TITLE, font=title_font)
tx, ty = (W - tw) / 2 - 4, 40
def text_mask(dx, dy):
    m = Image.new('1', (W, H), 0)
    ImageDraw.Draw(m).text((tx + dx, ty + dy), TITLE, font=title_font, fill=1)
    return m
face = text_mask(0, 0)
DEPTH = 9
extr = Image.new('1', (W, H), 0)
for k in range(1, DEPTH + 1):
    extr = ImageChops.logical_or(extr, text_mask(k * 0.6, k))
extr = ImageChops.subtract(extr, face)
# extrusion shading: 50 % checker near the face, sparser further back (depth shading)
shade = Image.new('L', (W, H), 0)
sd = ImageDraw.Draw(shade)
for k in range(DEPTH, 0, -1):
    sd.bitmap((0, 0), text_mask(k * 0.6, k), fill=int(150 - 90 * k / DEPTH))
shade_bits = dither(shade)
bits.paste(0, (0, 0), extr)
bits.paste(1, (0, 0), ImageChops.logical_and(extr, shade_bits))
# dark outline around face and extrusion
whole = ImageChops.logical_or(face, extr)
grow = whole.convert('L').filter(ImageFilter.MaxFilter(3)).convert('1')
bits.paste(0, (0, 0), ImageChops.subtract(grow, whole))
edge = ImageChops.subtract(face.convert('L').filter(ImageFilter.MaxFilter(3)).convert('1'), face)
bits.paste(0, (0, 0), ImageChops.logical_and(edge, extr))
bits.paste(1, (0, 0), face)                     # solid light face

# subtitle in a dark band
sub = 'PSR-9000  ·  custom firmware'
sf = font('Bold', 10)
sw = bd.textlength(sub, font=sf)
bd.rectangle(((W - sw) / 2 - 10, 121, (W + sw) / 2 + 10, 137), fill=0)
bd.text(((W - sw) / 2, 129), sub, font=sf, fill=1, anchor='lm')
bd.line(((W - sw) / 2 - 10, 139, (W + sw) / 2 + 10, 139), fill=1)


# ---------------------------------------------------------------- outputs
def encode(img):
    """Yamaha compressed image format (see CLAUDE.md): u16 length, then per pixel stream:
    0x00-0x7F = 7 literal pixels (cut at the row end), 0x80+n = n+1 zeros, 0xC0+n = n+1 ones.
    Rows are encoded separately (runs never cross a row end)."""
    px = img.load()
    out = bytearray()
    for y in range(img.height):
        row = [px[x, y] for x in range(img.width)]
        x = 0
        while x < len(row):
            v, n = row[x], 1
            while x + n < len(row) and row[x + n] == v and n < 64:
                n += 1
            if n >= 8 or x + n == len(row) and n >= len(row) - x:
                out.append((0xC0 if v else 0x80) | (n - 1))
                x += n
            else:
                lit = row[x:x + 7] + [0] * (7 - len(row[x:x + 7]))
                out.append(sum(b << (6 - i) for i, b in enumerate(lit)))
                x += 7
    return struct.pack('>H', len(out)) + bytes(out)


def decode(data, w, h):        # same as tools/res_extract.py comp_image
    n = struct.unpack('>H', data[:2])[0]
    px, x = [], 0
    for b in data[2:2 + n]:
        if b < 0x80:
            bitsr = [(b >> k) & 1 for k in range(6, -1, -1)][:w - x]
        else:
            bitsr = [1 if b & 0x40 else 0] * ((b & 0x3F) + 1)
        for v in bitsr:
            px.append(v)
            x += 1
            if x == w:
                x = 0
                if b < 0x80:
                    break
    return px[:w * h]


if __name__ == '__main__':
    out_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out_dir, exist_ok=True)
    data = encode(bits)
    assert decode(data, W, H) == list(bits.getdata()), 'encoder does not round-trip'
    open(os.path.join(out_dir, 'splash.bin'), 'wb').write(data)
    pal = Image.new('RGB', (W, H))
    pal.putdata([(0xfb, 0xf3, 0xa8) if v else (0x36, 0x41, 0xcf) for v in bits.getdata()])
    pal.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(out_dir, 'splash_preview.png'))
    print('splash: %d bytes compressed' % len(data))
