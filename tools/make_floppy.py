"""Build a 1.44 MB DOS (FAT12) floppy image from files, for the emulator's floppy drive.

  python3 tools/make_floppy.py out.img file1.sty file2.sty ...

Each file gets a DOS 8.3 name plus a Windows-style long name (VFAT LFN entries), so the
keyboard can show "60's Rock 1 152.sty". Stops with an error when the disk is full.
Mount in MAME with  -flop out.img  (raw .img is one of MAME's PC floppy formats).
"""
import os, re, struct, sys

SECT, TOTAL, FATS, FAT_SECT, ROOT_ENTRIES, SPT, HEADS = 512, 2880, 2, 9, 224, 18, 2
ROOT_SECT = ROOT_ENTRIES * 32 // SECT
DATA0 = 1 + FATS * FAT_SECT + ROOT_SECT          # first data sector (cluster 2)
CLUSTERS = TOTAL - DATA0


def short_name(name, used):
    base, ext = os.path.splitext(os.path.basename(name))
    clean = re.sub(r'[^A-Z0-9]', '', base.upper()) or 'FILE'
    ext = re.sub(r'[^A-Z0-9]', '', ext.upper())[:3]
    for n in range(1, 1000):
        b = clean[:8] if n == 1 else clean[:8 - len('~%d' % n)] + '~%d' % n
        key = (b, ext)
        if key not in used:
            used.add(key)
            return b.ljust(8).encode() + ext.ljust(3).encode()
    raise SystemExit('too many similar names')


def lfn_entries(long_name, short11):
    """VFAT long-name directory entries (stored before the 8.3 entry, last part first)."""
    csum = 0
    for b in short11:
        csum = (((csum & 1) << 7) + (csum >> 1) + b) & 0xFF
    units = [ord(c) for c in long_name] + [0]
    parts = [units[i:i + 13] for i in range(0, len(units), 13)]
    out = []
    for n, part in enumerate(parts, 1):
        part = part + [0xFFFF] * (13 - len(part))
        e = bytearray(32)
        e[0] = n | (0x40 if n == len(parts) else 0)
        e[1:11] = b''.join(struct.pack('<H', u) for u in part[0:5])
        e[11], e[12], e[13] = 0x0F, 0, csum
        e[14:26] = b''.join(struct.pack('<H', u) for u in part[5:11])
        e[28:32] = b''.join(struct.pack('<H', u) for u in part[11:13])
        out.append(bytes(e))
    return list(reversed(out))


def build(paths):
    img = bytearray(TOTAL * SECT)
    boot = bytearray(SECT)
    boot[0:3] = b'\xEB\x3C\x90'
    boot[3:11] = b'KYRANOS '
    struct.pack_into('<HBHBHHBHHHLL', boot, 11, SECT, 1, 1, FATS, ROOT_ENTRIES, TOTAL, 0xF0,
                     FAT_SECT, SPT, HEADS, 0, 0)
    boot[36:62] = struct.pack('<BBBL11s8s', 0, 0, 0x29, 0x4B4F5321, b'STYLES     ', b'FAT12   ')
    boot[510:512] = b'\x55\xAA'
    img[0:SECT] = boot
    fat = [0] * (CLUSTERS + 2)
    fat[0], fat[1] = 0xFF0, 0xFFF
    root = bytearray(ROOT_SECT * SECT)
    used, nxt, slot = set(), 2, 0
    for i, p in enumerate(paths):
        data = open(p, 'rb').read()
        n = max(1, (len(data) + SECT - 1) // SECT)
        if nxt + n > CLUSTERS + 2:
            raise SystemExit('disk full at %s (%d files fit)' % (p, i))
        first = nxt
        for k in range(n):
            c = nxt + k
            fat[c] = 0xFFF if k == n - 1 else c + 1
            off = (DATA0 + c - 2) * SECT
            img[off:off + SECT] = data[k * SECT:(k + 1) * SECT].ljust(SECT, b'\0')
        nxt += n
        sn = short_name(p, used)
        entries = lfn_entries(os.path.basename(p), sn)
        entries.append(sn + struct.pack('<B10xHHHL', 0x20, 0, 0x21, first, len(data)))
        if slot + len(entries) > ROOT_ENTRIES:
            raise SystemExit('root directory full at %s (%d files fit)' % (p, i))
        for e in entries:
            root[slot * 32:(slot + 1) * 32] = e
            slot += 1
    fb = bytearray(FAT_SECT * SECT)
    for c in range(0, len(fat), 2):            # 12-bit entries, two per 3 bytes
        a, b = fat[c], fat[c + 1] if c + 1 < len(fat) else 0
        o = c * 3 // 2
        if o + 3 <= len(fb):
            fb[o:o + 3] = bytes((a & 0xFF, (a >> 8) | ((b & 0xF) << 4), b >> 4))
    for k in range(FATS):
        img[(1 + k * FAT_SECT) * SECT:(1 + (k + 1) * FAT_SECT) * SECT] = fb
    img[(1 + FATS * FAT_SECT) * SECT:DATA0 * SECT] = root
    return img, nxt - 2


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    img, used = build(sys.argv[2:])
    open(sys.argv[1], 'wb').write(img)
    print('%s: %d files, %d of %d KB used' % (sys.argv[1], len(sys.argv) - 2, used // 2, CLUSTERS // 2))
