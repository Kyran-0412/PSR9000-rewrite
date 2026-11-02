#!/usr/bin/env python3
"""Write modified main program files (p9000m.002-.004) with the home panel mod applied.

  python3 mod/home/make_image.py                 -> firmware/mod/p9000m.002 .003 .004 (+ main_prog_mod.bin)
  python3 mod/home/make_image.py --splash-only   -> firmware/mod/splash_only/... (first hardware
                                                    test: only the KyranOS boot splash changes)

Both integrity checks are recomputed (docs/RECOVERY.md):
  1. test-mode Rom Chk1 value at 0x3FFFF8-0x3FFFFB (tools/romcheck.py)
  2. whole-image byte sum == 0xFF, kept by the adjustment byte at 0x3FFFFC
Output stays in firmware/ (git-ignored): it contains Yamaha's code.
Do NOT install this on the keyboard without the recovery steps in docs/RECOVERY.md.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from romcheck import lanes  # noqa: E402

SPLIT = [('p9000m.002', 0x000000, 0x160000), ('p9000m.003', 0x160000, 0x160000), ('p9000m.004', 0x2C0000, 0x140000)]


def main():
    img = bytearray(open(os.path.join(ROOT, 'firmware', 'main_prog.bin'), 'rb').read())
    orig = bytes(img)
    assert len(img) == 0x400000
    end = len(img) - 1
    assert lanes(img, 0, end) == list(img[end - 7:end - 3]), 'original Rom Chk1 does not match'
    assert sum(img) & 0xFF == 0xFF, 'original byte sum is not 0xFF'
    splash_only = '--splash-only' in sys.argv
    mod = json.load(open(os.path.join(HERE, 'build', 'patches.json')))
    patches = mod['patches']
    if splash_only:
        patches = [p for p in patches if 'splash' in p['what']]
        assert len(patches) == 2, 'expected the splash data + its pointer'
    for p in patches:
        a, data = p['addr'], bytes.fromhex(p['bytes'])
        assert orig[a:a + len(data)] == bytes.fromhex(p['orig']), 'firmware differs at 0x%x' % a
        assert a + len(data) <= 0x3FFF00, 'patch reaches the tag/checksum area'
        img[a:a + len(data)] = data
    # 1. Rom Chk1
    img[end - 7:end - 3] = bytes(lanes(img, 0, end))
    # 2. byte sum: adjust 0x3FFFFC (the update's own adjustment byte; not sampled by Rom Chk1)
    img[0x3FFFFC] = (img[0x3FFFFC] + 0xFF - sum(img)) & 0xFF
    assert sum(img) & 0xFF == 0xFF
    assert lanes(img, 0, end) == list(img[end - 7:end - 3])
    out = os.path.join(ROOT, 'firmware', 'mod', 'splash_only' if splash_only else '')
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'main_prog_mod.bin'), 'wb').write(img)
    for name, a, n in SPLIT:
        open(os.path.join(out, name), 'wb').write(img[a:a + n])
    changed = sum(1 for x, y in zip(orig, img) if x != y)
    print('wrote firmware/mod/p9000m.002-.004: %d bytes changed, Rom Chk1 %s, byte sum OK'
          % (changed, img[end - 7:end - 3].hex().upper()))
    for name, a, n in SPLIT:
        print('  %s %s' % (name, 'changed' if orig[a:a + n] != img[a:a + n] else 'unchanged'))


if __name__ == '__main__':
    main()
