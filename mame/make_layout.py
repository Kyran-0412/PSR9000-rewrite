#!/usr/bin/env python3
"""Generate psr9000.lay: clickable PSR-9000 front panel for MAME.

Positions follow the panel drawing in the service manual (Panel Layout, top view),
redrawn flat. Each button is (port row, bit, label, x, y, w, h, shape).
Port ROWn / bit c = panel switch row n, column c (see INPUT_PORTS in ympsr9000.cpp).
"""
import sys

B = []  # buttons
T = []  # free text: (text, x, y, w, h, size class)
G = []  # group frames: (x, y, w, h)


def btn(row, col, label, x, y, w=40, h=20, shape='rect', top=None):
    B.append((row, col, label, x, y, w, h, shape, top))


def text(s, x, y, w, h=10):
    T.append((s, x, y, w, h))


# ---- left section: song, style, menu, accompaniment control ----
text('SONG', 10, 40, 118)
for i, (lab, col) in enumerate([('I', 0), ('II', 1), ('III', 2)]):
    btn(0, col, lab, 10, 62 + 42 * i, 56, 22)
btn(0, 3, 'IV', 72, 62, 56, 22)
btn(0, 4, 'V', 72, 104, 56, 22)
btn(0, 5, 'SETUP', 72, 146, 56, 22, top='SONG SETUP')
text('SONG FILE DIRECTORY', 10, 172, 118)
btn(0, 6, '', 140, 62, 40, 30, top='SONG PLAYER')
btn(0, 7, '', 140, 138, 40, 30, top='AUTO ACMP')

text('STYLE', 196, 40, 190)
styles = [('8 BEAT', 0, 8), ('SWING & JAZZ', 1, 2), ('LATIN', 1, 5),
          ('16 BEAT', 1, 0), ('R & B', 1, 3), ('MARCH & WALTZ', 1, 6),
          ('DANCE', 1, 1), ('COUNTRY', 1, 4), ('BALLROOM', 1, 7)]
for i, (lab, row, col) in enumerate(styles):
    btn(row, col, '', 196 + 66 * (i % 3), 62 + 42 * (i // 3), 58, 22, top=lab)
text('PRESET STYLE', 196, 172, 190)
flash = [('I', 1, 8), ('IV', 2, 2), ('VII', 2, 5), ('II', 2, 0), ('V', 2, 3), ('VIII', 2, 6),
         ('III', 2, 1), ('VI', 2, 4), ('DISK', 2, 7)]
for i, (lab, row, col) in enumerate(flash):
    btn(row, col, lab, 402 + 50 * (i % 3), 62 + 42 * (i // 3), 44, 22)
text('FLASH STYLE', 402, 172, 144)

text('MENU', 562, 40, 70)
btn(5, 0, '', 566, 62, 20, 20, 'disk', top='DISK')
btn(5, 1, '', 590, 62, 20, 20, 'disk', top='FUNC')
btn(5, 2, '', 614, 62, 20, 20, 'disk', top='MIDI')
text('TRANSPOSE', 562, 110, 74)
btn(5, 3, '-', 566, 124, 32, 20)
btn(5, 4, '+', 602, 124, 32, 20)

text('ACCOMPANIMENT CONTROL', 10, 196, 170)
row1 = [(2, 8, 'INTRO I'), (3, 0, 'II'), (3, 1, 'III'), (3, 2, 'TAP'),
        (3, 3, 'END I'), (3, 4, 'II'), (3, 5, 'III'), (3, 6, 'FADE'), (3, 7, 'SYNC STP'), (3, 8, 'SYNC ST')]
for i, (r, c, lab) in enumerate(row1):
    btn(r, c, lab, 130 + 50 * i, 214, 44, 26)
row2 = [(4, 0, 'A'), (4, 1, 'B'), (4, 2, 'C'), (4, 3, 'D'), (4, 4, 'FILL <'), (4, 5, 'SELF'),
        (4, 6, 'BREAK'), (4, 7, 'FILL >'), (4, 8, 'START')]
for i, (r, c, lab) in enumerate(row2):
    btn(r, c, lab, 130 + 50 * i + (25 if i == 8 else 0), 252, 44 if i < 8 else 64, 26)
text('MAIN VARIATION', 130, 282, 194)
text('FILL IN & BREAK', 330, 282, 194)

# ---- centre: display and the buttons around it ----
SX, SY = 780, 40   # screen 320x240
text('DIGITAL STUDIO', 662, 30, 90)
btn(5, 5, '', 700, 62, 22, 22, 'disk', top='SOUND CREATOR')
btn(5, 6, '', 700, 104, 22, 22, 'disk', top='DIGITAL REC')
btn(5, 7, '', 700, 146, 22, 22, 'disk', top='MIXING CONSOLE')
for i, (r, c, lab) in enumerate([(5, 8, 'A'), (6, 0, 'B'), (6, 1, 'C'), (6, 2, 'D'), (6, 3, 'E')]):
    btn(r, c, lab, SX - 26, SY + 18 + 46 * i, 22, 30)
for i, (r, c, lab) in enumerate([(14, 0, 'F'), (14, 1, 'G'), (14, 2, 'H'), (14, 3, 'I'), (14, 4, 'J')]):
    btn(r, c, lab, SX + 324, SY + 18 + 46 * i, 22, 30)

text('MULTI PAD', 652, 186, 100)
btn(6, 4, 'STOP', 652, 204, 30, 18)
btn(6, 5, '-', 686, 204, 30, 18)
btn(6, 6, '+', 720, 204, 30, 18)
for i, c in enumerate([7, 8]):
    btn(6, c, str(i + 1), 652 + 50 * i, 230, 44, 20)
for i, c in enumerate([0, 1]):
    btn(7, c, str(i + 3), 652 + 50 * i, 256, 44, 20)

btn(7, 4, 'DIRECT', 680, 300, 50, 40, top='DIRECT ACCESS')
btn(7, 6, '', 744, 296, 26, 26, 'disk', top='MIXER')
btn(7, 5, '', 744, 344, 26, 26, 'disk', top='PART ON/OFF')
for i in range(8):
    x = SX + 40 * i + 6
    btn(8, i, '▲', x, 296, 28, 34)
    btn(9, i, '▼', x, 342, 28, 34)
    text(str(i + 1), x, 378, 28)
btn(7, 7, 'EXIT', SX + 330, 304, 46, 40)

text('DEMO', 1150, 40, 40)
btn(13, 8, '', 1158, 54, 22, 22, 'disk')
text('VOICE EFFECT', 1140, 86, 96)
fx = [(10, 0, 'TOUCH'), (10, 3, 'SUSTAIN'), (10, 1, 'DSP'), (10, 4, 'SLOW/FST'),
      (10, 2, 'HARM/ECHO'), (10, 5, 'POLY')]
for i, (r, c, lab) in enumerate(fx):
    btn(r, c, '', 1144 + 48 * (i % 2), 112 + 46 * (i // 2), 22, 22, 'disk', top=lab)
text('PAGE CONTROL', 1140, 248, 96)
btn(14, 5, '<', 1140, 262, 44, 20, top=None)
btn(14, 6, '>', 1190, 262, 44, 20)

# data dial: two clickable halves (DIALBTN bit0 = down, bit1 = up)
DIAL = (1150, 300, 96)

# ---- right section: database, OTS, registration, voices, parts ----
RX = 1270
btn(14, 7, '...', RX, 62, 60, 20, top='MUSIC DATABASE')
text('ONE TOUCH SETTING', RX, 92, 110)
for i, (r, c) in enumerate([(14, 8), (15, 0), (15, 1), (15, 2)]):
    btn(r, c, str(i + 1), RX + 56 * (i % 2), 108 + 30 * (i // 2), 50, 24)

text('VOICE', RX + 130, 40, 60)
v1 = [(10, 6, 'PIANO'), (11, 0, 'ACCORDION'), (11, 1, 'GUITAR'), (11, 3, 'TRUMPET'),
      (11, 5, 'SAXOPHONE'), (11, 7, 'CHOIR & PAD'), (12, 0, 'PERCUSSION'), (12, 2, 'ORGAN FLUTES')]
v2 = [(10, 7, 'E.PIANO'), (10, 8, 'ORGAN'), (11, 2, 'STRINGS'), (11, 4, 'BRASS'),
      (11, 6, 'FLUTE'), (11, 8, 'SYNTH'), (12, 1, 'XG'), (12, 3, 'CUSTOM')]
# real panel order (top row): PIANO ORGAN GUITAR TRUMPET SAXOPHONE CHOIR&PAD PERCUSSION ORGAN FLUTES
v1 = [(10, 6, 'PIANO'), (10, 8, 'ORGAN'), (11, 1, 'GUITAR'), (11, 3, 'TRUMPET'),
      (11, 5, 'SAXOPHONE'), (11, 7, 'CHOIR & PAD'), (12, 0, 'PERCUSSION'), (12, 2, 'ORGAN FLUTES')]
v2 = [(10, 7, 'E.PIANO'), (11, 0, 'ACCORDION'), (11, 2, 'STRINGS'), (11, 4, 'BRASS'),
      (11, 6, 'FLUTE'), (11, 8, 'SYNTH'), (12, 1, 'XG'), (12, 3, 'CUSTOM')]
for i, (r, c, lab) in enumerate(v1):
    btn(r, c, '', RX + 130 + 58 * i, 62, 52, 20, top=lab)
for i, (r, c, lab) in enumerate(v2):
    btn(r, c, '', RX + 130 + 58 * i, 104, 52, 20, top=lab)

text('PART SELECT', RX + 246, 136, 226)
for i, (c, lab) in enumerate([(4, 'L'), (5, 'R1'), (6, 'R2'), (7, 'R3')]):
    btn(12, c, lab, RX + 246 + 58 * i, 150, 52, 22)
text('PART ON/OFF', RX + 188, 210, 284)
btn(12, 8, '', RX + 188, 184, 52, 22, top=None)
text('LEFT HOLD', RX + 188, 172, 52)
for i, (c, lab) in enumerate([(0, 'L'), (1, 'R1'), (2, 'R2'), (3, 'R3')]):
    btn(13, c, lab, RX + 246 + 58 * i, 184, 52, 22)
text('UPPER OCTAVE', RX + 490, 136, 90)
btn(15, 3, '-', RX + 490, 150, 40, 22)
btn(15, 4, '+', RX + 540, 150, 40, 22)

text('REGISTRATION MEMORY', RX, 222, 170)
btn(13, 7, '', RX, 240, 36, 20, top='FREEZE')
btn(15, 7, '-', RX + 44, 240, 36, 20, top='BANK')
btn(15, 8, '+', RX + 86, 240, 36, 20)
for i in range(8):
    btn(16, i, str(i + 1), RX + 130 + 46 * i, 240, 40, 26)
btn(16, 8, 'MEM', RX + 130 + 46 * 8 + 6, 240, 44, 26, top='MEMORY')

text('VOCAL HARMONY', RX + 600, 40, 90)
vh = [(13, 4, 'TALK'), (13, 5, 'DSP(8)'), (13, 6, 'V.H.(9)'), (15, 5, 'SELECT'), (15, 6, 'MIC SETUP')]
for i, (r, c, lab) in enumerate(vh):
    btn(r, c, '', RX + 634, 64 + 42 * i, 22, 22, 'disk', top=lab)

W, H = RX + 700, 420

# ---------------------------------------------------------------- output
PANEL = '0.24,0.25,0.27'
FRAME = '0.33,0.34,0.37'
BTN = '0.86,0.87,0.88'
BTN_ON = '0.45,0.62,0.95'
LABEL = '0.82,0.84,0.88'


def color(s):
    r, g, b = s.split(',')
    return f'<color red="{r}" green="{g}" blue="{b}" />'


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


out = ['<?xml version="1.0"?>',
       '<!--',
       'license:CC0-1.0',
       'PSR-9000 front panel (flat redraw of the service manual panel drawing).',
       'Generated by make_layout.py: edit that, not this file.',
       '-->',
       '<mamelayout version="2">']
out.append(f'<element name="panel"><rect>{color(PANEL)}</rect></element>')
out.append(f'<element name="frame"><rect>{color(FRAME)}</rect></element>')
out.append(f'<element name="bezel"><rect>{color("0.10,0.10,0.11")}</rect></element>')
labels = {}


def label_el(s, size='lbl'):
    key = (s, size)
    if key not in labels:
        n = f'{size}{len(labels)}'
        labels[key] = n
        c = LABEL if size == 'lbl' else '0.15,0.15,0.17'
        out.append(f'<element name="{n}"><text string="{esc(s)}">{color(c)}</text></element>')
    return labels[key]


shapes = {}


def shape_el(shape, lab):
    key = (shape, lab)
    if key not in shapes:
        n = f'b{len(shapes)}'
        shapes[key] = n
        el = [f'<element name="{n}">']
        for st, col in ((0, BTN), (1, BTN_ON)):
            el.append(f'<{shape} state="{st}">{color(col)}</{shape}>')
        if lab:
            el.append(f'<text string="{esc(lab)}">{color("0.12,0.12,0.14")}'
                      f'<bounds x="0.08" y="0.18" width="0.84" height="0.64" /></text>')
        el.append('</element>')
        out.extend(el)
    return shapes[key]


items = []
for (row, col, lab, x, y, w, h, shape, top) in B:
    el = shape_el(shape, lab)
    items.append(f'<element ref="{el}" inputtag="ROW{row}" inputmask="0x{1 << col:03x}">'
                 f'<bounds x="{x}" y="{y}" width="{w}" height="{h}" /></element>')
    if top:
        tw = max(w + 24, len(top) * 6)
        items.append(f'<element ref="{label_el(top)}"><bounds x="{x + w / 2 - tw / 2:.1f}" y="{y - 11}" '
                     f'width="{tw}" height="9" /></element>')
for (s, x, y, w, h) in T:
    items.append(f'<element ref="{label_el(s)}"><bounds x="{x}" y="{y}" width="{w}" height="{h}" /></element>')

dx, dy, dd = DIAL
out.append(f'<element name="dialbody"><disk>{color("0.70,0.71,0.73")}</disk>'
           f'<disk>{color("0.55,0.56,0.58")}<bounds x="0.3" y="0.3" width="0.4" height="0.4" /></disk></element>')
for name, sym in (('dial_dn', '−'), ('dial_up', '+')):
    out.append(f'<element name="{name}"><rect state="0"><color alpha="0" /></rect>'
               f'<rect state="1"><color red="0.45" green="0.62" blue="0.95" alpha="0.45" /></rect>'
               f'<text string="{sym}">{color("0.12,0.12,0.14")}<bounds x="0.1" y="0.3" width="0.3" height="0.4" /></text>'
               '</element>' if name == 'dial_dn' else
               f'<element name="{name}"><rect state="0"><color alpha="0" /></rect>'
               f'<rect state="1"><color red="0.45" green="0.62" blue="0.95" alpha="0.45" /></rect>'
               f'<text string="{sym}">{color("0.12,0.12,0.14")}<bounds x="0.6" y="0.3" width="0.3" height="0.4" /></text>'
               '</element>')
dial_items = [
    f'<element ref="dialbody"><bounds x="{dx}" y="{dy}" width="{dd}" height="{dd}" /></element>',
    f'<element ref="dial_dn" inputtag="DIALBTN" inputmask="0x01"><bounds x="{dx}" y="{dy}" width="{dd / 2}" height="{dd}" /></element>',
    f'<element ref="dial_up" inputtag="DIALBTN" inputmask="0x02"><bounds x="{dx + dd / 2}" y="{dy}" width="{dd / 2}" height="{dd}" /></element>',
    f'<element ref="{label_el("DATA DIAL")}"><bounds x="{dx}" y="{dy + dd + 2}" width="{dd}" height="9" /></element>',
]


def view(name, x0, y0, x1, y1):
    v = [f'<view name="{name}">',
         f'<element ref="panel"><bounds x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" /></element>',
         f'<element ref="bezel"><bounds x="{SX - 6}" y="{SY - 6}" width="332" height="252" /></element>',
         f'<screen index="0"><bounds x="{SX}" y="{SY}" width="320" height="240" /></screen>']
    v += items + dial_items
    v.append(f'<bounds x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" />')
    v.append('</view>')
    return v


out += view('Full Panel', 0, 0, W, H)
out += view('Centre Panel', 640, 20, 1260, H)
out += ['<view name="Screen Only">', '<screen index="0"><bounds x="0" y="0" width="320" height="240" /></screen>', '</view>']
out.append('</mamelayout>')
open(sys.argv[1] if len(sys.argv) > 1 else 'psr9000.lay', 'w').write('\n'.join(out) + '\n')
print(len(B), 'buttons')
