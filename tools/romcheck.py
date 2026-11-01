"""Re-implementation of the PSR-9000 'Rom Chk1' main-program check (main_prog.bin 0x24D892).

It is a sparse, address-line style checksum, not a sum of every byte:
- 4 byte lanes (offsets 0..3 of a 32-bit word), each an 8-bit running sum.
- Each lane starts at 0xFF, then gets +0xFF once per power-of-two offset 4..0x10000
  inside the protected boot area (the code assumes that area reads 0xFF; it is not read).
- Then it reads the 4 bytes at 0x20000 and 0x40000, and for every 512 KB block
  B = 0x80000, 0x100000, ... below `end`: B+0, then B+4, B+8, B+16, ... (< B+0x80000).
- Result is compared with the bytes at end-7 .. end-4 (0x3FFFF8..0x3FFFFB for main).
Run: python3 tools/romcheck.py firmware/main_prog.bin [--fix]
"""
import sys

def lanes(img, start=0, end=0x3FFFFF):
    s = [0xFF] * 4
    r6 = 4
    while r6 < 0x20000:
        if start + r6 >= end:
            return s
        s = [(x + 0xFF) & 0xFF for x in s]
        r6 <<= 1
    def add(a):
        for k in range(4):
            s[k] = (s[k] + img[a + k]) & 0xFF
    add(start + r6)          # 0x20000
    add(start + (r6 << 1))   # 0x40000
    b = start + 0x80000
    while b < end:
        add(b)
        r6 = 4
        while b + r6 < end and r6 < 0x80000:
            add(b + r6)
            r6 <<= 1
        b += 0x80000
    return s

if __name__ == '__main__':
    path = sys.argv[1]
    img = bytearray(open(path, 'rb').read())
    end = len(img) - 1
    calc = lanes(img, 0, end)
    stored = list(img[end - 7:end - 3])
    print('calculated', ' '.join('%02X' % x for x in calc))
    print('stored    ', ' '.join('%02X' % x for x in stored))
    print('MATCH' if calc == stored else 'MISMATCH')
    print('whole-image byte sum & 0xFF = %02X' % (sum(img) & 0xFF))
