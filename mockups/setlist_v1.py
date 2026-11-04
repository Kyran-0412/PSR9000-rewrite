"""Setlist / Performance mode mockups v1 (KyranOS), same visual language as home_v11:
Yamaha bitmap fonts (read from firmware/main_prog.bin at run time), blue status bar with yellow
text, dotted texture, inverted (blue) button strip, side-button tabs aligned with the real
buttons (A-E left, F-J right at y = 27 + 39*i).

  python3 mockups/setlist_v1.py out_dir   -> one PNG per screen (3x, LCD colours)

Screens: setlist_empty, setlist_list, song_edit, song_new, performance, confirm_next,
confirm_delete. Controls:
- Setlist viewer: dial or I/J = select song; LCD 1 MOVE UP, 2 MOVE DOWN, 3 EDIT, 4 DELETE,
  5 COPY (duplicate), 6 OPEN, 7 NEW, 8 HOME.
- Edit/New: side A-E = field (TITLE, KEY, BPM, FL CONFIG, REGIST), dial = value; I/J = section;
  LCD 1 ADD, 2 DELETE, 3 MOVE UP, 4 MOVE DOWN (sections), 6 NAME (Yamaha naming screen for the
  title), 7 BACK (cancel), 8 SAVE.
- Performance: LCD 1 PREV, 2 NEXT (both ask first), 7 HOME, 8 LIST; status lights in the middle.
- Confirm prompts: YES = side button G, NO = side button H (like Yamaha's own dialogs).
"""
import os, struct, sys
from PIL import Image, ImageDraw, ImageOps

W, H = 320, 240
K, Wt = 0, 1                                   # ink (blue), paper (yellow)
ROM = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'firmware', 'main_prog.bin'), 'rb').read()
FT = struct.unpack('>9I', ROM[0x357B08:0x357B2C]); FH = list(ROM[0x357B2C:0x357B35])
LABEL, VALUE, NAME = 1, 3, 4                   # Yamaha font ids: 7 px, 10 px, 15 px
STRIP = 203                                    # top of the button strip

KEYS = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']
SONGS = [
    dict(t='Way Maker', k='E', bpm=68, cfg=1, reg=(5, 1)),
    dict(t='Goodness of God', k='Ab', bpm=63, cfg=2, reg=(5, 2)),
    dict(t='Build My Life', k='C', bpm=70, cfg=1, reg=(5, 3)),
    dict(t='Holy Forever', k='A', bpm=72, cfg=3, reg=(5, 4)),
    dict(t='Gratitude', k='B', bpm=74, cfg=1, reg=(6, 1)),
    dict(t='King of Kings', k='D', bpm=68, cfg=2, reg=(6, 2)),
]
ARR = ['Intro', 'Verse', 'Pre-Chorus', 'Chorus', 'Verse', 'Pre-Chorus', 'Chorus', 'Bridge',
       'Bridge', 'Instrumental', 'Chorus', 'Tag', 'Outro']


def numbered(arr):
    """automatic numbering: a type that appears more than once gets 1, 2, 3..."""
    total = {s: arr.count(s) for s in arr}
    seen, out = {}, []
    for s in arr:
        seen[s] = seen.get(s, 0) + 1
        out.append('%s %d' % (s, seen[s]) if total[s] > 1 else s)
    return out


class Screen:
    def __init__(self):
        self.img = Image.new('1', (W, H), Wt)
        self.d = ImageDraw.Draw(self.img)

    def glyph(self, fi, c):
        p, w = struct.unpack('>IB', ROM[FT[fi] + ord(c) * 8:FT[fi] + ord(c) * 8 + 5]); return p, w

    def tlen(self, s, fi): return sum(self.glyph(fi, c)[1] for c in s) - 1

    def t(self, x, y, s, fi, fill=K, anchor='l'):
        """y = middle of the capitals, like the mod's text()"""
        if anchor == 'm': x -= self.tlen(s, fi) // 2
        elif anchor == 'r': x -= self.tlen(s, fi)
        y -= {1: 3, 3: 5, 4: 7}[fi]
        for c in s:
            p, gw = self.glyph(fi, c); bpr = (gw + 7) // 8
            for r in range(FH[fi]):
                for k in range(gw):
                    if ROM[p + r * bpr + k // 8] >> (7 - k % 8) & 1: self.d.point((x + k, y + r), fill)
            x += gw

    def rr(self, b, r=6, fill=None, outline=None, width=1): self.d.rounded_rectangle(b, r, fill=fill, outline=outline, width=width)

    def tri(self, cx, cy, up=True, fill=K, s=3):
        if up: self.d.polygon([(cx - s, cy + s // 2 + 1), (cx + s, cy + s // 2 + 1), (cx, cy - s // 2 - 1)], fill=fill)
        else: self.d.polygon([(cx - s, cy - s // 2 - 1), (cx + s, cy - s // 2 - 1), (cx, cy + s // 2 + 1)], fill=fill)

    def status(self, left, right=''):
        self.d.rectangle((0, 0, W - 1, 18), fill=K)
        self.t(8, 9, left, VALUE, Wt)
        if right: self.t(W - 8, 9, right, LABEL, Wt, 'r')

    def hatch(self, y0, y1):
        for y in range(y0, y1 + 1):
            for x in range(W):
                if (y % 4 == 0 and x % 4 == 0) or (y % 4 == 2 and x % 4 == 2): self.d.point((x, y), K)

    def side_tab(self, right, i, text='', up=None, filled=True):
        """tab next to side button i (0-4) on the left (A-E) or right (F-J)"""
        y = 27 + 39 * i
        w = max(24, self.tlen(text, LABEL) + 14) if text else 18
        x0, x1 = (W - 2 - w, W - 2) if right else (1, 1 + w)
        self.rr((x0, y - 8, x1, y + 8), 6, fill=K if filled else Wt, outline=K, width=2)
        fg = Wt if filled else K
        if text: self.t((x0 + x1) // 2 + (2 if up is not None else 0), y, text, LABEL, fg, 'm')
        if up is not None:
            self.tri(x0 + 7 if text else (x0 + x1) // 2, y, up, fg)

    def strip(self, buttons, header=''):
        """bottom strip: 8 columns of 40 px; buttons[i] = (line1, line2, state) with state
        'on' (filled), 'off' (outline), 'dis' (dashed, not available) or None (empty)"""
        top = STRIP
        self.d.rectangle((0, top, W - 1, H - 1), fill=Wt)
        for i, b in enumerate(buttons):
            if not b: continue
            l1, l2, st = b
            x = i * 40
            box = (x + 3, top + 4, x + 36, H - 4)
            if st == 'on': self.rr(box, 6, fill=K); fg = Wt
            elif st == 'off': self.rr(box, 6, outline=K, width=2); fg = K
            else:
                for xx in range(box[0] + 4, box[2] - 3, 4): self.d.point((xx, box[1]), K); self.d.point((xx, box[3]), K)
                for yy in range(box[1] + 4, box[3] - 3, 4): self.d.point((box[0], yy), K); self.d.point((box[2], yy), K)
                fg = K
            cx = (box[0] + box[2]) // 2
            if l2:
                self.t(cx, top + 13, l1, LABEL, fg, 'm'); self.t(cx, top + 25, l2, LABEL, fg, 'm')
            else:
                self.t(cx, top + 19, l1, LABEL, fg, 'm')
        inv = self.img.crop((0, top, W, H))
        self.img.paste(ImageOps.invert(inv.convert('L')).convert('1'), (0, top))

    def save(self, path):
        rgb = Image.new('RGB', (W, H))
        rgb.putdata([(0xfb, 0xf3, 0xa8) if v else (0x36, 0x41, 0xcf) for v in self.img.get_flattened_data()])
        rgb.resize((W * 3, H * 3), Image.NEAREST).save(path)


VIEWER_BTNS = [('MOVE', 'UP', 'off'), ('MOVE', 'DOWN', 'off'), ('EDIT', '', 'off'), ('DEL', '', 'off'),
               ('COPY', '', 'off'), ('OPEN', '', 'on'), ('NEW', '', 'off'), ('HOME', '', 'off')]


def setlist(songs, sel, active=None):
    s = Screen()
    s.status('SETLIST', '%d / 20 SONGS' % len(songs))
    s.hatch(19, STRIP - 1)
    if not songs:
        s.rr((40, 70, 280, 140), 8, fill=Wt, outline=K, width=2)
        s.t(160, 92, 'NO SONGS YET', VALUE, K, 'm')
        s.t(160, 112, 'PRESS NEW (BUTTON 7)', LABEL, K, 'm')
        s.t(160, 124, 'TO CREATE YOUR FIRST SONG', LABEL, K, 'm')
        s.strip([(a, b, 'dis') for a, b, _ in VIEWER_BTNS[:6]] + [('NEW', 'SONG', 'on'), ('HOME', '', 'off')])
        return s
    for i, so in enumerate(songs[:8]):
        y = 23 + 22 * i
        on = i == sel
        box = (6, y, 286, y + 18)
        if on: s.rr(box, 6, fill=K); fg = Wt
        else: s.rr(box, 6, fill=Wt, outline=K, width=2); fg = K
        s.rr((10, y + 3, 34, y + 15), 6, fill=Wt if on else None, outline=None if on else K, width=2)
        s.t(22, y + 9, '%02d' % (i + 1), LABEL, K, 'm')
        s.t(41, y + 9, so['t'], VALUE, fg)
        s.t(280, y + 9, '%-2s  %3d' % (so['k'], so['bpm']), LABEL, fg, 'r')
        if active == i:                         # the song currently being performed
            s.rr((196, y + 4, 226, y + 14), 5, fill=Wt if on else K)
            s.t(211, y + 9, 'LIVE', LABEL, K if on else Wt, 'm')
    s.side_tab(True, 3, '', up=True)            # I = up
    s.side_tab(True, 4, '', up=False)           # J = down
    s.strip(VIEWER_BTNS)
    return s


def song_edit(song, field, sec, new=False):
    s = Screen()
    s.status('NEW SONG' if new else 'EDIT SONG', '' if new else 'SONG 04')
    s.hatch(19, STRIP - 1)
    fields = [('TITLE', song['t']), ('KEY', song['k']), ('BPM', str(song['bpm'])),
              ('FL CONFIG', str(song['cfg'])), ('REGIST', 'BANK %02d - %d' % song['reg'])]
    for i, (lab, val) in enumerate(fields):     # aligned with side buttons A-E
        y = 29 + 39 * i                         # 2 px below the button centre: clears the header
        on = i == field
        box = (14, y - 8, 152, y + 8)          # one line: label left, value right
        if on: s.rr(box, 7, fill=K); fg = Wt
        else: s.rr(box, 7, fill=Wt, outline=K, width=2); fg = K
        s.t(21, y, lab, LABEL, fg)
        s.t(146, y, val, VALUE, fg, 'r')
        s.d.polygon([(2, y - 5), (8, y), (2, y + 5)], fill=K if on else Wt, outline=K)   # side-button pointer
    # arrangement column, sections chosen with I / J
    s.rr((158, 22, 296, STRIP - 5), 7, fill=Wt, outline=K, width=2)
    names = numbered(ARR)
    s.t(166, 31, 'ARRANGEMENT', LABEL, K)
    s.t(290, 31, '%d/16' % len(ARR), LABEL, K, 'r')
    s.d.line((162, 39, 292, 39), fill=K)
    first = 0
    for j, nm in enumerate(names[first:first + 12]):
        y = 47 + 12 * j
        on = (first + j) == sec
        if on: s.rr((161, y - 6, 293, y + 5), 4, fill=K)
        fg = Wt if on else K
        s.t(176, y, str(first + j + 1), LABEL, fg, 'r')
        s.t(182, y, nm.upper(), LABEL, fg)
    s.side_tab(True, 3, '', up=True)
    s.side_tab(True, 4, '', up=False)
    s.strip([('ADD', '', 'off'), ('DEL', '', 'off'), ('MOVE', 'UP', 'off'), ('MOVE', 'DOWN', 'off'),
              None, ('NAME', '', 'off'), ('BACK', '', 'off'), ('SAVE', '', 'on')])
    return s


def performance(song, idx, total, nxt, midi='ok', reg='ok'):
    s = Screen()
    s.status('SONG %d / %d' % (idx + 1, total), 'NEXT: ' + nxt.upper() if nxt else 'LAST SONG')
    s.t(10, 33, song['t'], NAME, K)             # biggest text on the screen
    x = 10
    for lab in ('KEY %s' % song['k'], '%d BPM' % song['bpm']):
        w = s.tlen(lab, VALUE) + 12
        s.rr((x, 46, x + w, 60), 6, fill=K)
        s.t(x + w // 2, 53, lab, VALUE, Wt, 'm')
        x += w + 6
    s.t(W - 10, 53, 'FL %d   REG %02d-%d' % (song['cfg'], song['reg'][0], song['reg'][1]), LABEL, K, 'r')
    s.rr((6, 66, W - 7, STRIP - 5), 7, fill=Wt, outline=K, width=2)
    names = numbered(ARR)
    for j, nm in enumerate(names[:16]):
        col, row = divmod(j, 8)
        x = 14 + 152 * col
        y = 77 + 15 * row
        s.rr((x, y - 6, x + 18, y + 6), 5, outline=K, width=2)
        s.t(x + 9, y, str(j + 1), LABEL, K, 'm')
        s.t(x + 25, y, nm.upper(), VALUE, K)
    if len(names) > 8:
        s.d.line((160, 72, 160, STRIP - 11), fill=K)

    def light(cx, label, state, y):
        if state == 'ok': s.d.ellipse((cx - 4, y - 4, cx + 4, y + 4), fill=K)
        elif state == 'wait': s.d.ellipse((cx - 4, y - 4, cx + 4, y + 4), outline=K, width=2)
        else:
            s.d.line((cx - 4, y - 4, cx + 4, y + 4), fill=K, width=2); s.d.line((cx - 4, y + 4, cx + 4, y - 4), fill=K, width=2)
        s.t(cx + 8, y, label, LABEL, K)
    s.strip([('PREV', '', 'off'), ('NEXT', '', 'off'), None, None, None, None, ('HOME', '', 'off'), ('LIST', '', 'off')])
    # status lights live in the inverted strip: draw them inverted
    tmp = Screen(); tmp.img = s.img.crop((0, 0, W, H)); tmp.d = ImageDraw.Draw(tmp.img)
    inv = ImageOps.invert(s.img.crop((0, STRIP, W, H)).convert('L')).convert('1')
    s.img.paste(inv, (0, STRIP)); s.d = ImageDraw.Draw(s.img)
    light(100, {'ok': 'MIDI SENT', 'wait': 'MIDI SENDING', 'fail': 'MIDI FAILED'}[midi], midi, STRIP + 12)
    light(100, {'ok': 'REGIST LOADED', 'wait': 'REGIST LOADING', 'fail': 'REGIST FAILED'}[reg], reg, STRIP + 26)
    inv = ImageOps.invert(s.img.crop((0, STRIP, W, H)).convert('L')).convert('1')
    s.img.paste(inv, (0, STRIP)); s.d = ImageDraw.Draw(s.img)
    return s


def dialog(base, title, line1, line2):
    s = base
    s.rr((16, 34, 304, 128), 8, fill=Wt, outline=K, width=2)
    s.rr((19, 37, 301, 125), 6, outline=K, width=1)
    s.t(30, 52, title, VALUE, K)
    s.t(30, 75, line1, NAME, K)
    s.t(30, 98, line2, LABEL, K)
    for i, lab in ((1, 'YES'), (2, 'NO')):      # side buttons G / H
        y = 27 + 39 * i
        s.rr((262, y - 9, 316, y + 9), 7, fill=K if lab == 'YES' else Wt, outline=K, width=2)
        s.t(289, y, lab, LABEL, Wt if lab == 'YES' else K, 'm')
    return s


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(out, exist_ok=True)
    setlist([], 0).save(os.path.join(out, 'setlist_empty.png'))
    setlist(SONGS, 3, active=3).save(os.path.join(out, 'setlist_list.png'))
    song_edit(SONGS[3], 0, 3).save(os.path.join(out, 'song_edit.png'))
    song_edit(dict(t='New Song', k='C', bpm=120, cfg=1, reg=(1, 1)), 1, -1, new=True).save(os.path.join(out, 'song_new.png'))
    performance(SONGS[3], 3, len(SONGS), SONGS[4]['t']).save(os.path.join(out, 'performance.png'))
    performance(SONGS[3], 3, len(SONGS), SONGS[4]['t'], midi='ok', reg='wait').save(os.path.join(out, 'performance_pending.png'))
    dialog(performance(SONGS[3], 3, len(SONGS), SONGS[4]['t']), 'GO TO NEXT SONG?', '5. Gratitude',
           'KEY B  -  74 BPM  -  REG 06-1').save(os.path.join(out, 'confirm_next.png'))
    dialog(setlist(SONGS, 3, active=3), 'DELETE THIS SONG?', '4. Holy Forever',
           'REGIST AND FL CONFIG STAY').save(os.path.join(out, 'confirm_delete.png'))
    print('written to', out)
