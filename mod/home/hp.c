/* New home screen for the PSR-9000 (V3.06): the agreed design, mockups/home_v5.py
 * (v4 + TEMPO box on LCD 1, parts on LCD 2-4, controls on LCD 5-7, LCD 8 free;
 * (v3 + bolder text, 2 px borders, a border around each control).
 *
 *   status line : style name ............................ (140 BPM)(TRP 0)(OCT 0)
 *   layers      : R1/R2/R3 cards with voice name, VOL/BRT (filled = on, dashed = off),
 *                 arrow at the selected part; LEFT voice line below
 *   bottom row  : LCD 1-3 = part R1/R2/R3 (ON/OFF + short name under each),
 *                 LCD 4-6 = VOLUME / BRIGHT / REVERB of the selected part, LCD 7-8 free.
 *                 DATA DIAL changes the selected control; LCD 6 DOWN switches reverb
 *                 off/on (restoring the previous level).
 *
 * The whole 320x240 screen is drawn here while the home screen is active (the stock home
 * screen keeps running underneath; whenever it draws something we draw over it again).
 *
 * Built with an SH-4 compiler in no-FPU mode; build.py rejects any instruction the SH-2
 * does not have and any library call. Rules: no division/modulo, no variable shifts
 * (shr/shl below), no writable globals (state is in spare DMA registers).
 */
#include "font.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

/* ---- firmware entry points and variables (main program V3.06) ---- */
#define FW(addr, type) ((type)(addr))
#define post_param   FW(0x25ce4, void (*)(int cat, int id, int len, u8 *data))
#define get_volume   FW(0x50c18, int (*)(int part))
#define get_bright   FW(0x50cd4, int (*)(int part))
#define get_reverb   FW(0x50cf0, int (*)(int part))
#define voice_name   FW(0x3fff4, const char *(*)(int part))   /* part 0-2 = R1-R3, 3 = LEFT */
#define part_on      FW(0x40048, int (*)(int part))
#define get_tempo    FW(0x904ce, int (*)(void))
#define get_transpose FW(0x51388, int (*)(void))               /* signed byte */
#define get_octave   FW(0x4005e, int (*)(void))                /* 0..2 = -1..+1 */
#define STYLE_NAME   (*(const char *volatile *)0x108f098)      /* set by the stock home screen */
#define lcd_refresh  FW(0x10bf60, void (*)(u8 *fb, const short *rect))
#define ui_flush_fn  FW(0x10bb0c, void (*)(void))
#define FRAMEBUFFER  ((u8 *)0x10c8aec)
#define LCD_BUSY     (*(volatile u8 *)0x10cb717)   /* 1 while the DMA copy to the LCD runs */
/* Button table of whatever screen gets panel events right now: 0x3B152C = MAIN VOLUME page
 * (home screen, bottom panel page 1). */
#define ACTIVE_TABLE (*(volatile u32 *)0x10ba718)
#define HOME_TABLE   0x3b152c
#define FLUSH_FLAG   (*(volatile u8 *)0x101de57)
#define REPAINT_ALL  (*(volatile u8 *)0x101de56)   /* UI flush: 1 = redraw the whole stock screen */

extern int orig_tempo_dial(int amount, int dir);   /* tramp.S: the old dial handler */

/* Shifts: the compiler would use SH-4 dynamic shift instructions even for constant shifts,
 * so all shifts go through these, built from the SH-2 fixed shifts. */
static inline u32 shr(u32 v, int n)
{
	while(n >= 16) { __asm__("shlr16 %0" : "+r"(v)); n -= 16; }
	while(n >= 8) { __asm__("shlr8 %0" : "+r"(v)); n -= 8; }
	while(n >= 2) { __asm__("shlr2 %0" : "+r"(v)); n -= 2; }
	if(n) __asm__("shlr %0" : "+r"(v) : : "t");
	return v;
}
static inline u32 shl(u32 v, int n)
{
	while(n >= 16) { __asm__("shll16 %0" : "+r"(v)); n -= 16; }
	while(n >= 8) { __asm__("shll8 %0" : "+r"(v)); n -= 8; }
	while(n >= 2) { __asm__("shll2 %0" : "+r"(v)); n -= 2; }
	if(n) __asm__("shll %0" : "+r"(v) : : "t");
	return v;
}
static inline u32 rotl1(u32 v) { __asm__("rotl %0" : "+r"(v) : : "t"); return v; }
static inline int mul(int a, int b)   /* forced mul.l: the compiler may turn constant multiplies into SH-4 shifts */
{
	int r;
	__asm__("mul.l %1,%2\n\tsts macl,%0" : "=r"(r) : "r"(a), "r"(b) : "macl");
	return r;
}
static int half(int v) { return v < 0 ? -(int)shr(-v, 1) : (int)shr(v, 1); }

/* ---- state ----
 * No spare RAM is known, so the state lives in the address registers of DMA channel 3,
 * which the firmware never uses (no reference to 0xFFFF86F0-0xFFFF86FF):
 *   SAR3    = checksum of the screen (and values) right after we drew it
 *   DAR3    = [31..24] magic 0x5A, [23] dirty, [22..21] part, [20..19] control, [18] shown,
 *             [17] TEMPO selected (the dial changes tempo)
 *   DMATCR3 = reverb level to restore, 8 bits per part (24-bit register)
 * On power-up the registers hold garbage, so the magic byte tells us to start fresh.
 */
#define SAR3 (*(volatile u32 *)0xffff86f0)
#define DAR3 (*(volatile u32 *)0xffff86f4)
#define DMATCR3 (*(volatile u32 *)0xffff86f8)

static u32 st(void)
{
	u32 v = DAR3;
	if(shr(v, 24) != 0x5a) {
		v = 0x5a800000u;        /* R1, VOLUME, dirty */
		DAR3 = v;
		DMATCR3 = 0;
	}
	return v;
}
static int sel_part(void) { return shr(st(), 21) & 3; }
static int sel_ctrl(void) { return shr(st(), 19) & 3; }
static void set_sel(int part, int ctrl) { DAR3 = (st() & ~0x007a0000u) | shl(part, 21) | shl(ctrl, 19) | 0x800000u; }
static int tempo_sel(void) { return shr(st(), 17) & 1; }
static void set_tempo_sel(void) { DAR3 = st() | 0x820000u; }
static void set_dirty(void) { DAR3 = st() | 0x800000u; }
static int dirty(void) { return shr(st(), 23) & 1; }
static void clear_dirty(void) { DAR3 = st() & ~0x800000u; }
static int shown(void) { return shr(st(), 18) & 1; }
static void set_shown(int on) { DAR3 = on ? (st() | 0x40000u) : (st() & ~0x40000u); }
static u32 part_shift(u32 v, int part)
{
	if(part == 1) return shl(v, 8);
	if(part == 2) return shl(v, 16);
	return v;
}
static int reverb_saved(int part)
{
	u32 t = DMATCR3;
	if(part == 1) t = shr(t, 8);
	else if(part == 2) t = shr(t, 16);
	return t & 0x7f;
}
static void set_reverb_saved(int part, int v)
{
	DMATCR3 = (DMATCR3 & ~part_shift(0xff, part)) | part_shift(v & 0x7f, part);
}

/* ---- drawing into the 320x240 1-bit framebuffer (bit set = dark) ---- */
#define W 320
#define H 240
/* On the real LCD a SET bit shows as the light (yellow) colour and a clear bit as blue
 * (checked by Kyran against the keyboard). The mockup's white = yellow, black = blue. */
#define K 0      /* ink (blue) */
#define WT 1     /* paper (yellow) */
static const u8 bit[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
static const u8 lmask[8] = { 0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01 };

static void px(int x, int y, int on)
{
	u8 *p;
	if(x < 0 || x >= W || y < 0 || y >= H)
		return;
	p = FRAMEBUFFER + y * 40 + shr(x, 3);
	if(on)
		*p |= bit[x & 7];
	else
		*p &= ~bit[x & 7];
}

/* horizontal span x0..x1 inclusive, a byte at a time */
static void hspan(int x0, int x1, int y, int on)
{
	int b0, b1, b;
	u8 *row;
	if(y < 0 || y >= H) return;
	if(x0 < 0) x0 = 0;
	if(x1 >= W) x1 = W - 1;
	if(x1 < x0) return;
	row = FRAMEBUFFER + y * 40;
	b0 = shr(x0, 3);
	b1 = shr(x1, 3);
	for(b = b0; b <= b1; b++) {
		u8 m = 0xff;
		if(b == b0) m &= lmask[x0 & 7];
		if(b == b1 && (x1 & 7) != 7) m &= ~lmask[(x1 & 7) + 1];
		if(on) row[b] |= m; else row[b] &= ~m;
	}
}

static void fill(int x0, int y0, int x1, int y1, int on)   /* inclusive corners */
{
	int y;
	for(y = y0; y <= y1; y++)
		hspan(x0, x1, y, on);
}

/* inset of a rounded corner at row dy (0 = outermost row) for radius r, like PIL */
static int corner_inset(int r, int dy)
{
	int d = r - dy, dx = r;
	/* largest dx with dx^2 + d^2 <= r^2 (+r for a softer edge) */
	while(dx > 0 && dx * dx + d * d > r * r + r)
		dx--;
	return r - dx;
}

/* PIL rounded_rectangle((x0,y0,x1,y1), r, fill/outline) */
static void rrect(int x0, int y0, int x1, int y1, int r, int filled, int on)
{
	int y, h = y1 - y0 + 1;
	if(r * 2 > h) r = half(h);
	if(r * 2 > x1 - x0 + 1) r = half(x1 - x0 + 1);
	for(y = y0; y <= y1; y++) {
		int dy = y - y0, inset = 0;
		if(dy < r) inset = corner_inset(r, dy);
		else if(y1 - y < r) inset = corner_inset(r, y1 - y);
		if(filled)
			hspan(x0 + inset, x1 - inset, y, on);
		else if(y == y0 || y == y1)
			hspan(x0 + inset, x1 - inset, y, on);
		else {
			/* side pixels, including the curve steps */
			int nin = inset;
			if(dy < r && dy + 1 <= r) nin = corner_inset(r, dy + 1);
			if(y1 - y < r && y1 - y - 1 >= 0) nin = corner_inset(r, y1 - y - 1);
			if(nin > inset) nin = inset;
			hspan(x0 + nin, x0 + inset, y, on);
			hspan(x1 - inset, x1 - nin, y, on);
		}
	}
}

/* PIL rounded_rectangle outline with width=2 (outer edge + one pixel inside) */
static void rrect2(int x0, int y0, int x1, int y1, int r, int on)
{
	rrect(x0, y0, x1, y1, r, 0, on);
	rrect(x0 + 1, y0 + 1, x1 - 1, y1 - 1, r > 1 ? r - 1 : 1, 0, on);
}

/* mockup v4 dashed_box: 2x2 dashes every 5 px, 6 px in from the corners */
static void dashed_box(int x0, int y0, int x1, int y1)
{
	int x, y;
	for(x = x0 + 6; x < x1 - 6; x += 5) { fill(x, y0, x + 1, y0 + 1, K); fill(x, y1 - 1, x + 1, y1, K); }
	for(y = y0 + 6; y < y1 - 6; y += 5) { fill(x0, y, x0 + 1, y + 1, K); fill(x1 - 1, y, x1, y + 1, K); }
}

static const glyph_t *find_glyph(const font_t *f, char c)
{
	int i;
	for(i = 0; i < f->count; i++)
		if(f->glyphs[i].ch == c)
			return &f->glyphs[i];
	return 0;
}

static int text_width(const font_t *f, const char *s)
{
	int w = 0;
	for(; *s; s++) {
		const glyph_t *g = find_glyph(f, *s);
		w += g ? g->advance : f->space;
	}
	return w;
}

/* anchor like PIL: h = 'l' left, 'm' middle, 'r' right; vertical is always 'm' */
static void text(int x, int y, const char *s, const font_t *f, int on, char h)
{
	int base = y + f->mid;
	if(h == 'm') x -= half(text_width(f, s));
	else if(h == 'r') x -= text_width(f, s);
	for(; *s; s++) {
		const glyph_t *g = find_glyph(f, *s);
		int gx, gy;
		if(!g) {
			x += f->space;
			continue;
		}
		for(gy = 0; gy < g->h; gy++) {
			const u8 *row = f->bits + g->offset + gy * g->stride;
			for(gx = 0; gx < g->w; gx++)
				if(row[shr(gx, 3)] & bit[gx & 7])
					px(x + g->x + gx, base + g->y + gy, on);
		}
		x += g->advance;
	}
}

/* ---- the firmware's own images (p9000m.001, mapped at 0x4F0000) ----
 * Record = u16 w, u16 h, u32 flags (0 raw, 0x01000000 compressed), u32 pointer to pixels.
 * The stock screens copy image bits to the framebuffer unchanged, so a bit is a framebuffer
 * bit (1 = yellow, 0 = blue on the real LCD). Nothing is copied into this mod: the pixels are
 * read from Yamaha's resource area at run time. */
#define IMG_OCTAVE0   0x5f7bac   /* 34x10 octave "0": keyboard block in columns 0-18 */
#define IMG_TRANSPOSE 0x5f7cbc   /* 23x12 transpose icon (keyboard mode) */
#define IMG_HOME_BG   0x5f7b6c   /* 320x240 stock home background (compressed), metronome
                                  * icon at 135,133 size 14x12 */
struct img_rec { u16 w, h; u32 flags; const u8 *px; };

/* copy part of a raw image; inv: draw inverted (for the blue status bar) */
static void blit_raw(u32 rec, int sx, int sy, int w, int h, int dx, int dy, int inv)
{
	const struct img_rec *r = (const struct img_rec *)rec;
	int bpr = shr(r->w + 7, 3), x, y;
	for(y = 0; y < h; y++) {
		const u8 *row = r->px + mul(sy + y, bpr);
		for(x = 0; x < w; x++) {
			int b = (row[shr(sx + x, 3)] & bit[(sx + x) & 7]) ? 1 : 0;
			px(dx + x, dy + y, b ^ inv);
		}
	}
}

/* copy part of a compressed image: u16 length, then 0x00-0x7F = 7 literal pixels (cut at the
 * row end), 0x80+n = n+1 pixels of 0, 0xC0+n = n+1 pixels of 1 */
static void blit_comp(u32 rec, int sx, int sy, int w, int h, int dx, int dy)
{
	const struct img_rec *r = (const struct img_rec *)rec;
	const u8 *p = r->px + 2, *end = p + (r->px[0] * 256 + r->px[1]);
	int x = 0, y = 0;
	while(p < end && y < sy + h) {
		u8 c = *p++;
		if(c < 0x80) {
			int k;
			for(k = 6; k >= 0; k--) {
				if(y >= sy && x >= sx && x < sx + w)
					px(dx + x - sx, dy + y - sy, (c & bit[7 - k]) ? 1 : 0);
				if(++x == r->w) { x = 0; y++; break; }
			}
		} else {
			int n = (c & 0x3f) + 1, v = (c & 0x40) ? 1 : 0;
			while(n--) {
				if(y >= sy && y < sy + h && x >= sx && x < sx + w)
					px(dx + x - sx, dy + y - sy, v);
				if(++x == r->w) { x = 0; y++; }
			}
		}
	}
}

/* ---- small string helpers (no libc) ---- */
static char *cat(char *d, const char *s) { while(*s) *d++ = *s++; *d = 0; return d; }

static char *num(char *d, int v, int sign)
{
	static const short pow10[3] = { 100, 10, 1 };
	int i, started = 0;
	if(v < 0) { *d++ = '-'; v = -v; }
	else if(sign && v > 0) *d++ = '+';
	for(i = 0; i < 3; i++) {
		char c = '0';
		while(v >= pow10[i]) { v -= pow10[i]; c++; }
		if(c != '0' || started || i == 2) { *d++ = c; started = 1; }
	}
	*d = 0;
	return d;
}

/* voice name, trailing spaces removed */
static void copy_name(char *d, const char *s, int max)
{
	int n = 0;
	if(!s) s = "";
	while(s[n] && n < max) { d[n] = s[n]; n++; }
	while(n && d[n - 1] == ' ') n--;
	d[n] = 0;
}

static void upper(char *s) { for(; *s; s++) if(*s >= 'a' && *s <= 'z') *s -= 32; }

/* last word, at most 6 letters (mockup: name.split()[-1][:6]) */
static void short_name(char *d, const char *name)
{
	const char *w = name, *p;
	int n = 0;
	for(p = name; *p; p++)
		if(*p == ' ' && p[1]) w = p + 1;
	while(w[n] && n < 6) { d[n] = w[n]; n++; }
	d[n] = 0;
}

/* ---- parameters ---- */
static int get_ctrl(int part, int ctrl)
{
	switch(ctrl) {
	case 0: return get_volume(part);
	case 1: return get_bright(part);
	default: return get_reverb(part);
	}
}

static void set_ctrl(int part, int ctrl, int value)
{
	static const u8 id[3] = { 5, 10, 11 };   /* VOLUME, BRIGHTNESS, REVERB SEND (category 2) */
	u8 data[2];
	if(value < 0) value = 0;
	if(value > 127) value = 127;
	data[0] = part;
	data[1] = value;
	post_param(2, id[ctrl], 2, data);
}

/* ---- the screen ---- */
static const char *const part_label[3] = { "R1", "R2", "R3" };
static const char *const ctrl_name[3] = { "VOLUME", "BRIGHT", "REVERB" };

/* everything shown on screen, so a change made elsewhere (other task, registration recall,
 * part buttons, voice select) triggers a redraw */
static u32 values_sig(void)
{
	u32 s = 0;
	int p, c;
	const char *n;
	for(p = 0; p < 3; p++) {
		for(c = 0; c < 3; c++)
			s = rotl1(rotl1(rotl1(s))) ^ (u32)(get_ctrl(p, c) & 0xff);
		s = rotl1(s) ^ (u32)(part_on(p) & 1) ^ (u32)voice_name(p);
	}
	s ^= (u32)voice_name(3);
	n = STYLE_NAME;
	s = rotl1(s) ^ (u32)n ^ (u32)get_tempo() ^ shl(get_transpose() & 0xff, 8) ^ shl(get_octave() & 3, 16);
	return s;
}

static u32 screen_sum(void)
{
	const u32 *p = (const u32 *)FRAMEBUFFER;
	u32 s = 0;
	int i;
	for(i = 0; i < H * 10; i++)
		s = rotl1(s) ^ p[i];
	return s;
}

/* op/oc/ov: a value we just sent that the firmware may not have applied yet (op < 0: none) */
static int val(int p, int c, int op, int oc, int ov)
{
	return (p == op && c == oc) ? ov : get_ctrl(p, c);
}

/* Wait until the previous frame has been copied to the LCD. Drawing while that copy runs
 * lets the LCD pick up a half-cleared screen (the flicker while turning the dial). */
static void wait_lcd_idle(void)
{
	int n = 2000000;                  /* safety limit, far more than one frame */
	while(LCD_BUSY && --n)
		;
}

static void draw_screen(int op, int oc, int ov)
{
	static const short rect[4] = { 0, 0, W, H };
	char buf[40], name[3][24], *e;
	int i, j, x, y, sp = sel_part(), sc = sel_ctrl(), ts = tempo_sel(), on[3];

	for(i = 0; i < 3; i++) {
		copy_name(name[i], voice_name(i), 20);
		on[i] = part_on(i) & 1;
	}
	wait_lcd_idle();
	fill(0, 0, W - 1, H - 1, WT);

	/* ---- status bar: Yamaha style, blue bar with yellow text and the stock icons ---- */
	fill(0, 0, W - 1, 18, K);
	copy_name(buf, STYLE_NAME, 24);
	text(8, 9, buf, &f_smallb, WT, 'l');
	x = W - 8;
	num(buf, get_octave() - 1, 1);                 /* octave: keyboard icon + value */
	text(x, 9, buf, &f_smallb, WT, 'r');
	x -= text_width(&f_smallb, buf) + 4;
	blit_raw(IMG_OCTAVE0, 0, 0, 19, 10, x - 19, 4, 0);
	x -= 19 + 12;
	num(buf, (signed char)get_transpose(), 1);     /* transpose: arrows + keyboard icon + value */
	text(x, 9, buf, &f_smallb, WT, 'r');
	x -= text_width(&f_smallb, buf) + 3;
	blit_raw(IMG_TRANSPOSE, 0, 0, 23, 12, x - 23, 3, 1);

	/* ---- layer cards (display only; selected part marked with a side tab) ---- */
	for(i = 0; i < 3; i++) {
		int fg;
		y = 25 + mul(i, 31);
		if(on[i]) {
			rrect(30, y, W - 31, y + 26, 7, 1, K);
			fg = WT;
			rrect(36, y + 6, 56, y + 19, 6, 1, WT);
		} else {
			dashed_box(30, y, W - 31, y + 26);
			fg = K;
			rrect2(36, y + 6, 56, y + 19, 6, K);
		}
		text(46, y + 13, part_label[i], &f_tiny, K, 'm');
		text(64, y + 13, name[i], &f_name, fg, 'l');
		if(on[i]) {
			e = cat(buf, "VOL ");
			e = num(e, val(i, 0, op, oc, ov), 0);
			e = cat(e, "  BRT ");
			num(e, val(i, 1, op, oc, ov), 0);
		} else
			cat(buf, "OFF");
		text(W - 31 - 8, y + 13, buf, &f_tiny, fg, 'r');
		if(i == sp && !ts) {        /* selection marker: arrow tab on the left */
			int k;
			static const u8 tip[7] = { 5, 4, 3, 3, 2, 1, 0 };   /* triangle (22,y+7) (27,y+13) (22,y+19) */
			for(k = 0; k <= 6; k++) {
				hspan(22, 22 + tip[k], y + 13 - k, K);
				hspan(22, 22 + tip[k], y + 13 + k, K);
			}
		}
	}
	e = cat(buf, "LEFT   ");
	copy_name(e, voice_name(3), 20);
	text(W / 2, 124, buf, &f_small, K, 'm');

	/* ---- bottom row ---- */
	{
		const int top = 136;
		hspan(0, W - 1, top, K);
		text(100, top + 9, "PART", &f_tiny, K, 'm');
		if(ts)
			buf[0] = 0;                      /* tempo selected: nothing above the controls */
		else {
			e = cat(buf, part_label[sp]);
			e = cat(e, "  ");
			cat(e, name[sp]);
			upper(buf);
		}
		text(240, top + 9, buf, &f_tiny, K, 'm');
		for(y = top + 18; y < H - 3; y += 2) {
			px(40, y, K);
			px(160, y, K);
		}

		/* tempo box (button 1) */
		{
			int fg;
			if(ts) { rrect(5, top + 20, 35, top + 44, 8, 1, K); fg = WT; }
			else { rrect2(5, top + 20, 35, top + 44, 8, K); fg = K; }
			num(buf, get_tempo(), 0);
			text(20, top + 32, buf, &f_val, fg, 'm');
			blit_comp(IMG_HOME_BG, 135, 133, 14, 12, 13, top + 50);   /* stock metronome icon */
			text(20, top + 70, "BPM", &f_tiny, K, 'm');
		}

		/* part selectors (buttons 2-4) */
		for(i = 0; i < 3; i++) {
			int fg;
			x = (i + 1) * 40;
			if(i == sp && !ts) { rrect(x + 5, top + 20, x + 35, top + 44, 8, 1, K); fg = WT; }
			else { rrect2(x + 5, top + 20, x + 35, top + 44, 8, K); fg = K; }
			text(x + 20, top + 32, part_label[i], &f_val, fg, 'm');
			text(x + 20, top + 56, on[i] ? "ON" : "OFF", &f_tiny, K, 'm');
			short_name(buf, name[i]);
			text(x + 20, top + 70, buf, &f_tiny, K, 'm');
		}

		/* controls (buttons 5-8) for the selected part */
		for(j = 0; j < 4; j++) {
			int fg, v, y0 = top + 54, y1 = H - 22, fh;
			x = (j + 4) * 40;
			if(j >= 3) {                     /* free slot: faint dashed outline only */
				dashed_box(x, top + 17, x + 38, H - 3);
				continue;
			}
			v = val(sp, j, op, oc, ov);
			if(j == sc && !ts) { rrect(x, top + 17, x + 38, H - 3, 7, 1, K); fg = WT; }
			else { rrect(x, top + 17, x + 38, H - 3, 7, 0, K); fg = K; }
			text(x + 19, top + 27, ctrl_name[j], &f_tiny, fg, 'm');
			num(buf, v, 0);
			text(x + 19, top + 41, buf, &f_val, fg, 'm');
			for(y = y0; y <= y1; y += 2)
				px(x + 20, y, fg);
			fh = 0;
			{ int t = (y1 - y0) * v; while(t >= 127) { t -= 127; fh++; } }   /* (y1-y0)*v/127 */
			if(fh)
				rrect(x + 17, y1 - fh, x + 23, y1, 3, 1, fg);
			if(j == 2) {                     /* REVERB on/off pill */
				if(v) { rrect(x + 9, H - 16, x + 31, H - 6, 5, 1, fg); text(x + 20, H - 11, "ON", &f_tiny, fg == K ? WT : K, 'm'); }
				else { rrect2(x + 9, H - 16, x + 31, H - 6, 5, fg); text(x + 20, H - 11, "OFF", &f_tiny, fg, 'm'); }
			}
		}
	}

	lcd_refresh(FRAMEBUFFER, rect);
	SAR3 = screen_sum() ^ values_sig();
	clear_dirty();
}

static int visible(void)
{
	return ACTIVE_TABLE == HOME_TABLE;
}

/* ---- hooks ---- */

/* LCD refresh requests of the stock graphics library (its 16 pointers to 0x10BF60 now point
 * here). While our screen is up, stock widgets still redraw themselves underneath (e.g. the
 * transpose/octave boxes); sending those areas to the LCD made the old widgets flash for a
 * frame. Our own drawing calls 0x10BF60 directly. */
int hp_refresh(u8 *fb, const short *rect)
{
	if(visible())
		return 0;
	lcd_refresh(fb, rect);
	return 0;
}

/* update callbacks of the 8 stock faders (table 0x3B1488). The firmware calls them about once
 * a second whether or not anything changed, so they must not force a redraw (that was the
 * flicker); real changes are caught by values_sig() in hp_tick. */
void hp_changed(void)
{
}

/* UP+DOWN pressed together (stock: reset to default): not used */
void hp_select(int btn)
{
	(void)btn;
}

/* one press of LCD n UP/DOWN (button id 0x92 + 2(n-1), DOWN = odd id); auto-repeat is
 * switched off for these buttons in the event table */
void hp_step(int btn, int step)
{
	int n = shr((btn & 0xffff) - 0x92, 1);   /* 0..7 = LCD 1..8 */
	int down = btn & 1;
	(void)step;
	if(n < 0 || n > 7)
		return;
	if(n == 0)
		set_tempo_sel();                     /* LCD 1: TEMPO, the dial changes tempo */
	else if(n < 4)
		set_sel(n - 1, sel_ctrl());          /* LCD 2-4: R1-R3 */
	else if(n < 7) {
		int c = n - 4, p = sel_part();       /* LCD 5-7: VOLUME / BRIGHT / REVERB */
		if(c == 2 && down) {
			/* reverb on/off for the selected part */
			int v = get_reverb(p);
			int nv = v ? 0 : (reverb_saved(p) ? reverb_saved(p) : 40);
			if(v)
				set_reverb_saved(p, v);
			set_ctrl(p, 2, nv);
			set_sel(p, c);
			if(visible())
				draw_screen(p, 2, nv);
			return;
		}
		set_sel(p, c);
	}
	if(visible())
		draw_screen(-1, 0, 0);
}

/* data dial: amount = clicks, dir 1 = up */
int hp_dial(int amount, int dir)
{
	int v;
	if(!visible())
		return orig_tempo_dial(amount, dir);
	if(tempo_sel()) {
		v = orig_tempo_dial(amount, dir);
		set_dirty();
		return v;
	}
	v = get_ctrl(sel_part(), sel_ctrl()) + (dir ? amount : -amount);
	if(v < 0) v = 0;
	if(v > 127) v = 127;
	set_ctrl(sel_part(), sel_ctrl(), v);
	draw_screen(sel_part(), sel_ctrl(), v);
	return 0;
}

/* end of the UI flush (0x24522): first the original tail, then our redraw if needed */
void hp_tick(void)
{
	if(FLUSH_FLAG) {
		/* The stock flush copies the stock home screen over ours (about once a second) and
		 * would make our screen flicker; while our screen is up there is nothing to flush. */
		FLUSH_FLAG = 0;
		if(!visible())
			ui_flush_fn();
	}
	if(visible()) {
		set_shown(1);
		if(dirty() || (screen_sum() ^ values_sig()) != SAR3)
			draw_screen(-1, 0, 0);
	} else if(shown()) {
		/* another page or screen took over: let the stock screen repaint everything */
		set_shown(0);
		REPAINT_ALL = 1;
	}
}
