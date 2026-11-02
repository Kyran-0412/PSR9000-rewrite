"""KyranOS boot splash v2: Kyran's own logo (mockups/splash_kyran_source.webp, yellow on navy)
converted to the 320x240 1-bit LCD.

  python3 mockups/splash_v2.py [out_dir]   -> splash_preview.png (LCD colours, 3x),
                                             splash.bin (Yamaha compressed format, for the mod)

Yellow -> pixel 1 (light LCD colour), navy -> 0 (blue): same polarity as Yamaha's splash.
The source is pixel art at ~4.5x, so it is scaled with area averaging and then thresholded on
how yellow each pixel is.
"""
import os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from splash_v1 import encode, decode, W, H   # same encoder / round-trip check

src = Image.open(os.path.join(HERE, 'splash_kyran_source.webp')).convert('RGB')
# "yellowness": red channel separates yellow (~248) from navy (~1) best
r = src.split()[0]
small = r.resize((W, H), Image.BOX)
bits = small.point(lambda v: 1 if v >= 128 else 0, mode='1')

if __name__ == '__main__':
    out_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out_dir, exist_ok=True)
    data = encode(bits)
    assert decode(data, W, H) == [1 if v else 0 for v in bits.getdata()], 'encoder does not round-trip'
    open(os.path.join(out_dir, 'splash.bin'), 'wb').write(data)
    pal = Image.new('RGB', (W, H))
    pal.putdata([(0xfb, 0xf3, 0xa8) if v else (0x36, 0x41, 0xcf) for v in bits.getdata()])
    pal.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(out_dir, 'splash_preview.png'))
    print('splash: %d bytes compressed' % len(data))
