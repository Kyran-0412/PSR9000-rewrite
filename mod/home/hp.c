/* New home screen panel for the PSR-9000 (V3.06), replaces the MAIN VOLUME page drawing.
 *
 * LCD 1-3 (UP or DOWN) select the part R1/R2/R3.
 * LCD 4-6 select the control VOLUME / BRIGHT / REVERB; LCD 6 DOWN switches reverb off and
 * back on (restoring the previous level). LCD 7-8 are free for later.
 * The DATA DIAL changes the selected control of the selected part while this panel is
 * showing; everywhere else it still does what it did before (tempo, lists, ...).
 *
 * Built with an SH-4 compiler in no-FPU mode; build.py rejects any instruction the SH-2
 * does not have. Rules for this file: no division/modulo except by constants, no variable
 * shifts (use the bit tables), no libc.
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
#define lcd_refresh  FW(0x10bf60, void (*)(u8 *fb, const short *rect))
#define ui_flush_fn  FW(0x10bb0c, void (*)(void))
#define FRAMEBUFFER  ((u8 *)0x10c8aec)
/* Button table of whatever screen gets panel events right now: 0x3B152C = MAIN VOLUME page
 * (home screen, bottom panel page 1). */
#define ACTIVE_TABLE (*(volatile u32 *)0x10ba718)
#define HOME_TABLE   0x3b152c
#define FLUSH_FLAG   (*(volatile u8 *)0x101de57)

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


/* ---- state ----
 * The firmware has no spare RAM we know of, so the state lives in the address registers of
 * DMA channel 3, which the firmware never uses (no reference to 0xFFFF86F0-0xFFFF86FF):
 *   SAR3 = checksum of the panel area right after we drew it
 *   DAR3 = [31..24] magic 0x5A, [23] dirty, [22..21] part, [20..19] control,
 *          [18..0] unused; saved reverb levels are kept in the firmware itself (see below)
 * On power-up DAR3 holds garbage, so the magic byte tells us to start fresh.
 */
#define SAR3 (*(volatile u32 *)0xffff86f0)
#define DAR3 (*(volatile u32 *)0xffff86f4)

static u32 st(void)
{
	u32 v = DAR3;
	if(shr(v, 24) != 0x5a) {
		v = 0x5a800000u;        /* R1, VOLUME, dirty */
		DAR3 = v;
	}
	return v;
}
static int sel_part(void) { return shr(st(), 21) & 3; }
static int sel_ctrl(void) { return shr(st(), 19) & 3; }
static void set_sel(int part, int ctrl) { DAR3 = (st() & ~0x00780000u) | shl(part, 21) | shl(ctrl, 19) | 0x800000u; }
static void set_dirty(void) { DAR3 = st() | 0x800000u; }
static int dirty(void) { return shr(st(), 23) & 1; }
static void clear_dirty(void) { DAR3 = st() & ~0x800000u; }

/* Reverb level to restore when switching reverb back on, per part. 7 bits each would not
 * fit next to the selection, so it goes in DMATCR3 (24 bits, also unused). */
#define DMATCR3 (*(volatile u32 *)0xffff86f8)
static u32 part_shift(u32 v, int part)   /* v << (8 * part) without a variable shift */
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

#define PANEL_Y 154
#define PANEL_H 86

static const u8 bit[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

/* ---- drawing into the 320x240 1-bit framebuffer (bit set = dark) ---- */
static void px(int x, int y, int on)
{
	u8 *p;
	if(x < 0 || x >= 320 || y < 0 || y >= 240)
		return;
	p = FRAMEBUFFER + y * 40 + shr(x, 3);
	if(on)
		*p |= bit[x & 7];
	else
		*p &= ~bit[x & 7];
}

/* fill a rectangle a byte at a time where possible */
static const u8 lmask[8] = { 0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01 };
static void fill(int x0, int y0, int w, int h, int on)
{
	int x1 = x0 + w, y, b0, b1, b;
	u8 m0, m1;
	if(w <= 0 || h <= 0)
		return;
	b0 = shr(x0, 3);
	b1 = shr(x1 - 1, 3);
	m0 = lmask[x0 & 7];
	m1 = ~lmask[((x1 - 1) & 7) + 1 > 7 ? 0 : ((x1 - 1) & 7) + 1];
	if(((x1 - 1) & 7) == 7)
		m1 = 0xff;
	for(y = y0; y < y0 + h; y++) {
		u8 *row = FRAMEBUFFER + y * 40;
		for(b = b0; b <= b1; b++) {
			u8 m = 0xff;
			if(b == b0) m &= m0;
			if(b == b1) m &= m1;
			if(on) row[b] |= m; else row[b] &= ~m;
		}
	}
}

/* rounded rectangle, filled or outline */
static void rrect(int x0, int y0, int w, int h, int r, int filled, int on)
{
	int x, y;
	for(y = 0; y < h; y++) {
		int inset = 0;
		if(y < r)
			inset = r - y;
		else if(y >= h - r)
			inset = y - (h - r - 1);
		if(inset > r)
			inset = r;
		/* corner shape: 2 px radius looks round enough at this size */
		for(x = inset; x < w - inset; x++) {
			int edge = (y == 0 || y == h - 1 || x == inset || x == w - inset - 1);
			if(filled || edge)
				px(x0 + x, y0 + y, on);
		}
	}
}

static void dashed_rect(int x0, int y0, int w, int h)
{
	int i;
	for(i = 2; i < w - 2; i += 3) { px(x0 + i, y0, 1); px(x0 + i, y0 + h - 1, 1); }
	for(i = 2; i < h - 2; i += 3) { px(x0, y0 + i, 1); px(x0 + w - 1, y0 + i, 1); }
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

/* draws s with its top-left at (x, y); on = 1 dark, 0 light */
static void text(const font_t *f, int x, int y, const char *s, int on)
{
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
					px(x + g->x + gx, y + g->y + gy, on);
		}
		x += g->advance;
	}
}

static void text_c(const font_t *f, int cx, int y, const char *s, int on)
{
	text(f, cx - (int)shr(text_width(f, s), 1), y, s, on);
}

static void number(char *buf, int v, int sign)
{
	static const short pow10[3] = { 100, 10, 1 };
	int i, started = 0;
	if(v < 0) { *buf++ = '-'; v = -v; }
	else if(sign && v > 0) *buf++ = '+';
	for(i = 0; i < 3; i++) {
		char d = '0';
		while(v >= pow10[i]) { v -= pow10[i]; d++; }
		if(d != '0' || started || i == 2) { *buf++ = d; started = 1; }
	}
	*buf = 0;
}

/* ---- parameters ---- */
static int get_ctrl(int part, int ctrl)
{
	switch(ctrl) {
	case 0: return get_volume(part);
	case 1: return get_bright(part);   /* 0..127 like the Mixing Console FILT page */
	default: return get_reverb(part);
	}
}

static void set_ctrl(int part, int ctrl, int value)
{
	static const u8 id[3] = { 5, 10, 11 };   /* VOLUME, BRIGHTNESS, REVERB SEND (category 2) */
	u8 data[2];
	if(value < 0)
		value = 0;
	if(value > 127)
		value = 127;
	data[0] = part;
	data[1] = value;
	post_param(2, id[ctrl], 2, data);
}

static void value_text(char *buf, int ctrl, int v)
{
	if(ctrl == 2 && v == 0) {
		buf[0] = 'O'; buf[1] = 'F'; buf[2] = 'F'; buf[3] = 0;
	} else
		number(buf, v, 0);
}

/* ---- the panel ---- */
static const char *const part_name[3] = { "R1", "R2", "R3" };
static const char *const ctrl_name[3] = { "VOLUME", "BRIGHT", "REVERB" };

/* all 9 values, so a change made by another task (or a registration recall) shows up */
static u32 values_sig(void)
{
	u32 s = 0;
	int p, c;
	for(p = 0; p < 3; p++)
		for(c = 0; c < 3; c++)
			s = rotl1(rotl1(rotl1(s))) ^ (u32)(get_ctrl(p, c) & 0xff);
	return s;
}

static u32 panel_sum(void)
{
	const u32 *p = (const u32 *)(FRAMEBUFFER + PANEL_Y * 40);
	u32 s = 0;
	int i;
	for(i = 0; i < PANEL_H * 10; i++)
		s = rotl1(s) ^ p[i];
	return s;
}

static void draw_card(int col, const char *label, const char *val, int level, int selected, int empty)
{
	int x = col * 40 + 1, y = PANEL_Y + 14, w = 38, h = 70;
	int fg = selected ? 0 : 1;
	if(empty) {
		dashed_rect(x, y, w, h);
		return;
	}
	rrect(x, y, w, h, 3, selected, 1);
	text_c(&font_big, x + 19, y + 14, val, fg);
	if(level >= 0) {           /* thin level bar */
		int bw = shr(level * 28 + 64, 7);
		if(bw < 0) bw = 0;
		rrect(x + 5, y + 40, 28, 5, 1, 0, fg);
		fill(x + 6, y + 41, bw > 26 ? 26 : bw, 3, fg);
	}
	text_c(&font_small, x + 19, y + h - 13, label, fg);
}

/* op/oc/ov: a value we just sent that the firmware may not have applied yet (op < 0: none) */
static void draw_panel(int op, int oc, int ov)
{
	static const short rect[4] = { 0, PANEL_Y, 320, PANEL_H };
	char buf[8], head[24];
	int i, v;

	fill(0, PANEL_Y, 320, PANEL_H, 0);
	/* header strip: "R1  VOLUME" */
	fill(0, PANEL_Y, 320, 11, 1);
	{
		const char *p = part_name[sel_part()], *c = ctrl_name[sel_ctrl()];
		int n = 0;
		while(*p) head[n++] = *p++;
		head[n++] = ' '; head[n++] = ' ';
		while(*c) head[n++] = *c++;
		head[n] = 0;
		text_c(&font_small, 160, PANEL_Y + 2, head, 0);
	}
	/* parts: value of the selected control for each part */
	for(i = 0; i < 3; i++) {
		v = (i == op && sel_ctrl() == oc) ? ov : get_ctrl(i, sel_ctrl());
		value_text(buf, sel_ctrl(), v);
		draw_card(i, part_name[i], buf, v, i == sel_part(), 0);
	}
	/* controls: values for the selected part */
	for(i = 0; i < 3; i++) {
		v = (sel_part() == op && i == oc) ? ov : get_ctrl(sel_part(), i);
		value_text(buf, i, v);
		draw_card(3 + i, ctrl_name[i], buf, v, i == sel_ctrl(), 0);
	}
	draw_card(6, "", "", -1, 0, 1);
	draw_card(7, "", "", -1, 0, 1);
	/* divider between part and control groups */
	fill(120, PANEL_Y + 16, 1, 66, 1);

	lcd_refresh(FRAMEBUFFER, rect);
	SAR3 = panel_sum() ^ values_sig();
	clear_dirty();
}

static int visible(void)
{
	return ACTIVE_TABLE == HOME_TABLE;
}

/* ---- hooks ---- */

/* value-changed callbacks of the 8 faders (table 0x3B1488): just redraw */
void hp_changed(void)
{
	set_dirty();
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
	if(n < 3)
		set_sel(n, sel_ctrl());
	else if(n < 6) {
		int c = n - 3, p = sel_part();
		if(c == 2 && down) {
			/* reverb on/off for the selected part */
			int v = get_reverb(p);
			int nv = v ? 0 : (reverb_saved(p) ? reverb_saved(p) : 40);
			if(v)
				set_reverb_saved(p, v);
			set_ctrl(p, 2, nv);
			set_sel(p, c);
			if(visible())
				draw_panel(p, 2, nv);
			return;
		}
		set_sel(p, c);
	}
	if(visible())
		draw_panel(-1, 0, 0);
}

/* data dial: amount = clicks, dir 1 = up */
int hp_dial(int amount, int dir)
{
	int v;
	if(!visible())
		return orig_tempo_dial(amount, dir);
	v = get_ctrl(sel_part(), sel_ctrl()) + (dir ? amount : -amount);
	if(v < 0) v = 0;
	if(v > 127) v = 127;
	set_ctrl(sel_part(), sel_ctrl(), v);
	draw_panel(sel_part(), sel_ctrl(), v);
	return 0;
}

/* end of the UI flush (0x24522): first the original tail, then our redraw if needed */
void hp_tick(void)
{
	if(FLUSH_FLAG) {
		FLUSH_FLAG = 0;
		ui_flush_fn();
	}
	if(visible() && (dirty() || (panel_sum() ^ values_sig()) != SAR3))
		draw_panel(-1, 0, 0);
}
