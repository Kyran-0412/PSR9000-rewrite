#!/usr/bin/env python3
"""Build the new home panel mod.

  python3 mod/home/build.py            -> mod/home/build/{hp.elf,hp.bin,patches.json,apply.lua}

Steps: compile hp.c + tramp.S with the SH-4
cross compiler in no-FPU big-endian mode, link into the free ROM area, refuse any
instruction the SH-2 does not have, then list every byte change for the firmware image.
The firmware image itself is never written here (see make_image.py for that).
"""
import json, os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'build')
ROOT = os.path.dirname(os.path.dirname(HERE))
BASE = 0x3EE400          # start of our code (free 0xFF area is 0x3EE308-0x3FFFE5)
LIMIT = 0x3FFF00         # keep well clear of the version tag / checksum bytes at the end
CC = 'sh4-linux-gnu-gcc'
PFX = 'sh4-linux-gnu-'

def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit(' '.join(cmd) + '\n' + r.stdout + r.stderr)
    return r.stdout


FORBIDDEN = re.compile(r'^(shad|shld|pref|movca\.l|ocbi|ocbp|ocbwb|ldtlb|clrs|sets|movco\.l|movli\.l|synco|icbi|prefi|f[a-z.]+)$')


def main():
    os.makedirs(OUT, exist_ok=True)
    flags = ['-m4-nofpu', '-mb', '-mrenesas', '-O2', '-ffreestanding', '-fno-pic', '-fno-builtin',
             '-fno-jump-tables', '-nostdlib', '-I', HERE, '-Wall']
    objs = []
    for src in ('hp.c', 'tramp.S'):
        o = os.path.join(OUT, os.path.basename(src) + '.o')
        run(CC, *flags, '-c', os.path.join(HERE, src), '-o', o)
        objs.append(o)
    ld = os.path.join(OUT, 'link.ld')
    open(ld, 'w').write('SECTIONS { . = 0x%x; .text : { *(.text*) } .rodata : { *(.rodata*) } '
                        '.data : { *(.data*) *(.bss*) *(COMMON) } /DISCARD/ : { *(.comment) *(.note*) } }\n' % BASE)
    elf = os.path.join(OUT, 'hp.elf')
    run(PFX + 'ld', '-EB', '-T', ld, '-o', elf, *objs)
    # no RAM data allowed: the code runs from ROM and keeps its state in spare registers
    sec = run(PFX + 'objdump', '-h', elf)
    for m in re.finditer(r'^\s*\d+\s+(\.data)\s+([0-9a-f]+)', sec, re.M):
        if int(m.group(2), 16):
            sys.exit('error: writable data in the mod (.data/.bss); keep state in registers')
    dis = run(PFX + 'objdump', '-d', '--no-show-raw-insn', elf)
    # literal pools sit between functions: every PC-relative load target is data
    data = set()
    for m in re.finditer(r'mov\.([lw])\s+([0-9a-f]+) <', dis):
        a = int(m.group(2), 16)
        data.update(range(a, a + (4 if m.group(1) == 'l' else 2)))
    bad = []
    for l in dis.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t(\S+)', l)
        if m and int(m.group(1), 16) not in data and FORBIDDEN.match(m.group(2)):
            bad.append(l)
    if bad:
        sys.exit('error: instructions the SH-2 does not have:\n' + '\n'.join(bad[:20]))
    calls = [l for l in dis.splitlines() if '__' in l and ('jsr' in l or 'bsr' in l or '.long' in l)]
    syms = {}
    for line in run(PFX + 'nm', elf).splitlines():
        a, t, n = line.split()
        syms[n.lstrip('_')] = int(a, 16)
    und = [l for l in run(PFX + 'nm', '-u', elf).splitlines() if l.strip()]
    if und:
        sys.exit('error: undefined symbols (library calls?): ' + ', '.join(und))
    binf = os.path.join(OUT, 'hp.bin')
    run(PFX + 'objcopy', '-O', 'binary', '-j', '.text', '-j', '.rodata', elf, binf)
    blob = open(binf, 'rb').read()
    end = BASE + len(blob)
    if end > LIMIT:
        sys.exit('error: mod too big (%x > %x)' % (end, LIMIT))

    # boot splash (mockups/splash_v2.py: Kyran's logo): our image in Yamaha's compressed format, right
    # after the code; the boot code's pointer to the stock splash pixels (0x26010) is changed
    run(sys.executable, os.path.join(ROOT, 'mockups', 'splash_v2.py'), OUT)
    splash = open(os.path.join(OUT, 'splash.bin'), 'rb').read()
    splash_at = (end + 3) & ~3
    end = splash_at + len(splash)
    if end > LIMIT:
        sys.exit('error: mod + splash too big (%x > %x)' % (end, LIMIT))

    def be32(v): return struct.pack('>I', v)
    def be16(*ws): return b''.join(struct.pack('>H', w) for w in ws)
    patches = [(BASE, blob, 'mod code'),
               (splash_at, splash, 'KyranOS boot splash (compressed 320x240)'),
               (0x26010, be32(splash_at), 'boot splash pixel pointer -> ours')]
    # data dial: stock handler 0x6D45C -> hp_dial (the trampoline re-runs what we overwrite)
    patches.append((0x6D45C, be16(0xD001, 0x402B, 0x0009, 0x0009) + be32(syms['hp_dial']), 'dial handler hook'))
    # end of the UI flush 0x24522: jump to hp_tick, which also does the original tail
    patches.append((0x24546, be16(0xD00B, 0x402B, 0x4F26), 'UI flush hook'))
    patches.append((0x24574, be32(syms['hp_tick']), 'UI flush hook literal'))
    for i in range(8):     # Main Mixer page 1 event table: UP/DOWN pair handlers
        patches.append((0x3B14B0 + 16 * i, be32(syms['hp_select']), 'LCD %d pair handler' % (i + 1)))
    for i in range(16):    # per-button handlers: no auto-repeat (0x64 -> 0), new handler
        patches.append((0x3B1538 + 16 * i, be16(0), 'LCD button: no auto-repeat'))
        patches.append((0x3B153C + 16 * i, be32(syms['hp_step']), 'LCD button handler'))
    for a in (0x1056CC, 0x1058FC, 0x105A9C, 0x105B84, 0x105E20, 0x106424, 0x10694C, 0x106E54,
              0x107C28, 0x108B50, 0x109A78, 0x10A94C, 0x10B2D0, 0x10B868, 0x10BADC, 0x10BB20):
        assert struct.unpack('>I', open(os.path.join(ROOT, 'firmware', 'main_prog.bin'), 'rb').read()[a:a + 4])[0] == 0x10BF60
        patches.append((a, be32(syms['hp_refresh']), 'graphics library LCD refresh -> filter'))
    rom0 = open(os.path.join(ROOT, 'firmware', 'main_prog.bin'), 'rb').read()
    assert rom0[0x3B152C:0x3B1530] == be32(0x260670)
    for a, orig, what in ((0x332C14, 0x6A0B8, 'A (restore)'), (0x332C24, 0x6A124, 'F -> Master EQ page'),
                          (0x332C34, 0x6A142, 'G (R2 solo)'), (0x332C44, 0x6A160, 'H (R3 solo)')):
        assert rom0[a:a + 4] == be32(orig)
        patches.append((a, be32(syms['hp_side']), 'home side button ' + what))
    patches.append((0x3B152C, be32(syms['hp_default']), 'home panel default handler (EXIT closes EQ)'))
    for i in range(8):     # fader value-changed callbacks -> redraw
        patches.append((0x3B1488 + 4 * i, be32(syms['hp_changed']), 'value changed callback'))

    rom = open(os.path.join(ROOT, 'firmware', 'main_prog.bin'), 'rb').read()
    for a, data, why in patches:
        if a >= BASE:
            assert rom[a:a + len(data)] == b'\xff' * len(data), 'free area not empty'
    js = [{'addr': a, 'bytes': data.hex(), 'what': why, 'orig': rom[a:a + len(data)].hex()} for a, data, why in patches]
    json.dump({'base': BASE, 'end': end, 'symbols': {k: v for k, v in syms.items() if k.startswith('hp_')},
               'patches': js}, open(os.path.join(OUT, 'patches.json'), 'w'), indent=1)
    # MAME Lua patcher (emulator only: patches the ROM copy in memory at power-on)
    lua = ['-- generated by mod/home/build.py: applies the mod to the emulated ROM at power-on',
           'local reg = manager.machine.memory.regions[":maincpu"]',
           'local sp = manager.machine.devices[":maincpu"].spaces["program"]',
           'local P = {']
    for a, data, why in patches:
        lua.append('  {0x%x, "%s"},' % (a, data.hex()))
    lua += ['}',
            'local sw = nil',
            'for _, p in ipairs(P) do',
            '  local a, h = p[1], p[2]',
            '  for i = 0, #h / 2 - 1 do',
            '    local v = tonumber(h:sub(2 * i + 1, 2 * i + 2), 16)',
            '    if sw == nil then reg:write_u8(a + i, v); sw = (sp:read_u8(a + i) == v) and 0 or 3; reg:write_u8(a + i, sp:read_u8(a + i)) end',
            '    reg:write_u8((a + i) ~ sw, v)',
            '  end',
            'end',
            'print(string.format("home panel mod applied: %d patches, code 0x%x-0x%x", #P, ' + str(BASE) + ', ' + str(end) + '))']
    open(os.path.join(OUT, 'apply.lua'), 'w').write('\n'.join(lua) + '\n')
    print('mod: 0x%x-0x%x (%d bytes), %d patches' % (BASE, end, len(blob), len(patches)))
    for k in sorted(syms):
        if k.startswith('hp_') or k == 'orig_tempo_dial':
            print('  %-16s 0x%x' % (k, syms[k]))


if __name__ == '__main__':
    main()
