"""Extract 1-bit UI images from PSR-9000 resource pack p9000m.001.

Format (found 2026-11): the file loads at 0x4F0000 (first word of the file).
Each image = pixel data, then (somewhere after it) a 12-byte record:
  u16 width, u16 height, u32 flags, u32 pointer-to-pixel-data (absolute, base 0x4F0000)
flags 0 = raw: rows padded to whole bytes, MSB = leftmost pixel, bit 1 = dark (on).
flags 0x01000000 = compressed: u16 byte length, then a pixel stream, row-major, no row padding:
  0x00-0x7F  7 literal pixels (bit 6 first)
  0x80-0xBF  run of (n & 0x3F) + 1 pixels of value 0
  0xC0-0xFF  run of (n & 0x3F) + 1 pixels of value 1
  A literal that runs past the end of a row is cut off there (its leftover pixels are
  dropped, not wrapped), so every row starts on a fresh byte. Verified: all 237
  compressed images decode to exactly width*height pixels.
  Pixel value 1 = dark (on), 0 = background.
The main program points at the 12-byte records, not at the pixels.
"""
import struct, sys, os
from PIL import Image

BASE = 0x4F0000

def records(res):
    out = []
    for i in range(0, len(res) - 12, 2):
        w, h, f, p = struct.unpack('>HHII', res[i:i+12])
        if BASE <= p < BASE + i and 0 < w <= 640 and 0 < h <= 480 and f in (0, 0x01000000):
            off = p - BASE
            raw = ((w + 7) // 8) * h
            if f == 0 and i - off < raw:
                continue  # data would overlap the record: false positive
            out.append(dict(rec=BASE + i, w=w, h=h, flags=f, data=off))
    return out

def raw_image(res, r):
    bpr = (r['w'] + 7) // 8
    d = res[r['data']:r['data'] + bpr * r['h']]
    im = Image.frombytes('1', (bpr * 8, r['h']), bytes(b ^ 0xFF for b in d))
    return im.crop((0, 0, r['w'], r['h']))  # inverted so dark pixels render black

def comp_image(res, r):
    n = struct.unpack('>H', res[r['data']:r['data'] + 2])[0]
    s = res[r['data'] + 2:r['data'] + 2 + n]
    w, h = r['w'], r['h']
    px, x = [], 0
    for b in s:
        if b < 0x80:
            bits = [(b >> k) & 1 for k in range(6, -1, -1)][:w - x]
        else:
            bits = [1 if b & 0x40 else 0] * ((b & 0x3F) + 1)
        for v in bits:
            px.append(v)
            x += 1
            if x == w:
                x = 0
                if b < 0x80:
                    break
    px = (px + [0] * (w * h))[:w * h]
    im = Image.new('1', (w, h))
    im.putdata([0 if v else 255 for v in px])  # match raw images: background white
    return im

def image(res, r):
    return comp_image(res, r) if r['flags'] else raw_image(res, r)

if __name__ == '__main__':
    res = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    recs = records(res)
    n = 0
    for r in recs:
        tag = 'c' if r['flags'] else 'r'
        image(res, r).save(os.path.join(out, '%06X_%dx%d%s.png' % (r['rec'], r['w'], r['h'], tag)))
        n += 1
    print(len(recs), 'records,', n, 'images written')
