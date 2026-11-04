/* ================= Setlist / Performance mode (mockups/setlist_v1.py) =================
 * Included by hp.c (shares its drawing helpers). Firmware facts: docs/SETLIST.md.
 *
 * Screens (all drawn over the stock home screen, like the Master EQ page):
 *   LIST  setlist viewer: dial / I / J select; LCD 1 MOVE UP, 2 MOVE DOWN, 3 EDIT, 4 DEL,
 *         5 COPY, 6 OPEN, 7 NEW, 8 HOME
 *   EDIT  new / edit song: side A-E pick the field, dial changes it; I / J pick a section,
 *         dial changes its type; LCD 1 ADD, 2 DEL, 3 MOVE UP, 4 MOVE DOWN (sections),
 *         6 NAME (naming screen), 7 BACK, 8 SAVE
 *   NAME  naming screen like Yamaha's: dial = character, D / E = row, LCD 1 CLR, 2 DEL,
 *         3 left, 4 right, 5 ENTRY, 7 CANCEL, 8 END
 *   PERF  performance: LCD 1 PREV, 2 NEXT (both ask first), 7 HOME, 8 LIST
 *   dialogs: YES = side G, NO = side H (EXIT = NO)
 *
 * RAM: 5 blocks of the kernel's pool 1 (docs/SETLIST.md), taken the first time the setlist
 * opens and kept. Block 0 = struct sl (found again by its magic), blocks 1-4 = 6 songs each.
 * The setlist is stored on the floppy as KYRANOS.SET (8-byte header + 40 bytes per song).
 */

#define pool_get    FW(0x22ba4, int (*)(void **out, int pool))     /* 0 = ok, block + 4 in *out */
#define POOL1       0x12bb2e4         /* 19 blocks, 0x10C apart: 4-byte tag "_MBK" + 0x108 bytes */
#define post_byte   FW(0x25cde, void (*)(int cat, int id, int value))
#define reg_exists  FW(0x2bb778, int (*)(int bank, int mem))
#define REG_BANK    (*(volatile u8 *)0x11af1a8)     /* current registration bank 0-63 */
#define REG_MEM     (*(volatile u8 *)0x11af1aa)     /* current memory 0-7 */
#define REG_REQ     (*(volatile u8 *)0x11af25e)     /* memory asked for by the last recall */
#define midi_send   FW(0x2562fc, void (*)(const u8 *msg, int len, int port))
#define MIDI_OFF(p) (*(volatile u8 *)(0x11a5ae0 + shl(p, 2)))
#define MIDI_RING(p) (0x10917f8 + mul(p, 0x1004))   /* + 0x1000 write index, + 0x1002 read index */
#define fs_open     FW(0x30243e, int (*)(const char *path, int mode))   /* mode 0 read, 4 create */
#define fs_read     FW(0x302510, int (*)(int fd, void *buf, int len))
#define fs_write    FW(0x3025a6, int (*)(int fd, const void *buf, int len))
#define fs_close    FW(0x302482, int (*)(int fd))
#define fs_remove   FW(0x302618, int (*)(const char *path))   /* delete a file (mode 4 appends) */
#define drive_path  FW(0x22b28a, int (*)(const char *drive, char *out))
#define drive_flush FW(0x22cd06, int (*)(const char *drive))   /* write the drive's cache to disk */

#define MAXSONGS 20
#define MAXSEC   16
#define TITLE_MAX 15
#define STRIP    203
#define NSECTYPES 15
#define NCHARS   78          /* naming screen: 3 rows of 26 */

/* time limits for the status lights, in hp_tick calls (the UI flush: measured about 1 per second) */
#define MIDI_TIMEOUT 3
#define REG_TIMEOUT  5

struct song {                /* 40 bytes, also the record in the file */
	u8 id, key, bpm_hi, bpm_lo, cfg, bank, mem, nsec;
	char title[16];
	u8 sec[16];
};

enum { M_HOME, M_LIST, M_EDIT, M_NAME, M_PERF };
enum { D_NONE, D_DELETE, D_PREV, D_NEXT, D_DISCARD, D_MSG };
enum { S_IDLE, S_WAIT, S_OK, S_FAIL };
enum { MSG_FULL = 1, MSG_SAVE, MSG_NOFILE, MSG_SECFULL, MSG_BADFILE };

struct sl {
	char magic[8];
	struct song *blk[4];
	u32 ticks;
	u8 count, next_id, loaded, moved;
	u8 mode, dlg, sel, top;
	u8 active, edit_idx, field, target;       /* target: 0 = field, 1 = section */
	u8 secsel, sectop, changed, msg;
	u8 name_pos, name_ch, midi_ports, pad;
	u8 midi_st, reg_st, reg_bank, reg_mem;
	u16 midi_pos[2];
	u16 midi_wait, reg_wait;
	struct song ed;                           /* the song being edited */
	char name[16];                            /* naming screen copy of the title */
};

static const char SL_MAGIC[8] = { 'K', 'y', 'O', 'S', '-', 'S', 'L', '1' };
static const char *const KEYS[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char *const SECTION[NSECTYPES] = {
	"INTRO", "VERSE", "PRE-CHORUS", "CHORUS", "POST-CHORUS", "BRIDGE", "INSTRUMENTAL",
	"INTERLUDE", "TURNAROUND", "BREAKDOWN", "VAMP", "TAG", "REFRAIN", "OUTRO", "ENDING" };
static const char CHARS[NCHARS + 1] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890!#$%&'()-@^_{}.,";

/* ---- memory ---- */
static void bzero8(void *p, int n) { u8 *d = p; while(n--) *d++ = 0; }
static void bcopy8(void *dst, const void *src, int n) { u8 *d = dst; const u8 *s = src; while(n--) *d++ = *s++; }

static struct sl *sl_find(void)
{
	int k, i;
	for(k = 0; k < 19; k++) {
		const u8 *b = (const u8 *)(POOL1 + mul(k, 0x10c));
		if(b[0] != '_' || b[1] != 'M' || b[2] != 'B' || b[3] != 'K')
			continue;
		for(i = 0; i < 8 && b[4 + i] == (u8)SL_MAGIC[i]; i++)
			;
		if(i == 8)
			return (struct sl *)(b + 4);
	}
	return 0;
}

static struct sl *sl_new(void)
{
	void *p[5];
	struct sl *s;
	int i;
	for(i = 0; i < 5; i++)
		if(pool_get(&p[i], 1) != 0)
			return 0;
	s = p[0];
	bzero8(s, 256);
	bcopy8(s->magic, SL_MAGIC, 8);
	for(i = 0; i < 4; i++)
		s->blk[i] = p[i + 1];
	s->active = 0xff;
	s->next_id = 1;
	return s;
}

static struct song *song(struct sl *s, int i)
{
	int b = 0;
	while(i >= 6) { i -= 6; b++; }
	return s->blk[b] + i;
}

static int bpm_of(const struct song *so) { return shl(so->bpm_hi, 8) | so->bpm_lo; }
static void set_bpm(struct song *so, int v) { so->bpm_hi = shr(v, 8); so->bpm_lo = v & 0xff; }

static int new_id(struct sl *s)
{
	int tries, i;
	for(tries = 0; tries < 256; tries++) {
		int id = s->next_id ? s->next_id : 1, used = 0;
		s->next_id = id + 1;
		for(i = 0; i < s->count; i++)
			if(song(s, i)->id == id) used = 1;
		if(!used) return id;
	}
	return 0;
}

/* ---- the file KYRANOS.SET on the floppy ---- */
static int sl_path(char *p)
{
	if(drive_path("FDDRV", p) < 0)
		return -1;
	while(*p) p++;
	cat(p, "\\KYRANOS.SET");
	return 0;
}

static void busy_box(const char *t)
{
	static const short rect[4] = { 0, 0, W, H };
	wait_lcd_idle();
	rrect(80, 96, 240, 130, 8, 1, WT);
	rrect2(80, 96, 240, 130, 8, K);
	text(160, 113, t, FT_VALUE, K, 'm');
	lcd_refresh(FRAMEBUFFER, rect);
}

static int sl_save(struct sl *s)
{
	char path[40];
	u8 hdr[8];
	int fd, i, r = 0;
	busy_box("SAVING SETLIST...");
	if(sl_path(path) < 0)
		return -1;
	fs_remove(path);                         /* open mode 4 adds to an existing file */
	fd = fs_open(path, 4);
	if(fd < 0)
		return fd;
	hdr[0] = 'K'; hdr[1] = 'S'; hdr[2] = 'E'; hdr[3] = 'T';
	hdr[4] = 1; hdr[5] = s->count; hdr[6] = s->next_id; hdr[7] = 0;
	if(fs_write(fd, hdr, 8) < 0) r = -1;
	for(i = 0; i < s->count && !r; i++)
		if(fs_write(fd, song(s, i), sizeof(struct song)) < 0) r = -1;
	fs_close(fd);
	drive_flush("FDDRV");                    /* as the firmware does after its own disk writes */
	if(!r) { s->moved = 0; s->loaded = 1; }
	return r;
}

static void save_or_warn(struct sl *s)
{
	if(sl_save(s) < 0) { s->dlg = D_MSG; s->msg = MSG_SAVE; }
}

/* 0 = loaded, 1 = no file (or no disk), 2 = damaged file (left alone) */
static int sl_load(struct sl *s)
{
	char path[40];
	u8 hdr[8];
	int fd, i, n, r = 0;
	busy_box("LOADING SETLIST...");
	if(sl_path(path) < 0)
		return 1;
	fd = fs_open(path, 0);
	if(fd < 0)
		return 1;
	n = fs_read(fd, hdr, 8);
	if(n != 8 || hdr[0] != 'K' || hdr[1] != 'S' || hdr[2] != 'E' || hdr[3] != 'T' || hdr[4] != 1 || hdr[5] > MAXSONGS)
		r = 2;
	for(i = 0; !r && i < hdr[5]; i++) {
		struct song *so = song(s, i);
		if(fs_read(fd, so, sizeof(struct song)) != (int)sizeof(struct song))
			r = 2;
		else {                               /* keep everything in range */
			so->title[TITLE_MAX] = 0;
			if(so->key > 11) so->key = 0;
			if(so->cfg > 127) so->cfg = 0;
			if(so->bank > 63) so->bank = 0;
			if(so->mem > 7) so->mem = 0;
			if(so->nsec > MAXSEC) so->nsec = MAXSEC;
			for(n = 0; n < so->nsec; n++)
				if(so->sec[n] >= NSECTYPES) so->sec[n] = 1;
		}
	}
	fs_close(fd);
	if(r)
		return r;
	s->count = hdr[5];
	s->next_id = hdr[6];
	s->sel = s->top = 0;
	s->active = 0xff;
	s->loaded = 1;
	return 0;
}

/* ---- activating a song: registration + MIDI (docs/SETLIST.md) ---- */
static void send3(int port, int a, int b, int c)
{
	u8 m[3];
	m[0] = a; m[1] = b; m[2] = c;
	midi_send(m, 3, port);
}

static int midi_done(struct sl *s, int p);

static void activate(struct sl *s, int i)
{
	struct song *so = song(s, i);
	int p;
	s->active = i;
	s->mode = M_PERF;
	s->dlg = D_NONE;
	/* registration: set the bank, then recall the memory; the recall handler writes the
	 * memory number to REG_REQ first, so a marker there tells us it has run */
	s->reg_bank = so->bank;
	s->reg_mem = so->mem;
	s->reg_wait = 0;
	if(reg_exists(so->bank, so->mem) & 0xff) {
		REG_REQ = 0xff;
		post_byte(11, 1, so->bank);
		post_byte(11, 5, so->mem);
		s->reg_st = S_WAIT;
	} else
		s->reg_st = S_FAIL;
	/* MIDI A and B: channel 15 note = FL configuration ID, channel 16 note 60-71 = key */
	s->midi_ports = 0;
	for(p = 0; p < 2; p++) {
		if(MIDI_OFF(p))
			continue;
		send3(p, 0x9e, so->cfg, 127);
		send3(p, 0x8e, so->cfg, 64);
		send3(p, 0x9f, 60 + so->key, 127);
		send3(p, 0x8f, 60 + so->key, 64);
		s->midi_pos[p] = *(volatile u16 *)(MIDI_RING(p) + 0x1000);
		s->midi_ports |= p ? 2 : 1;
	}
	s->midi_wait = 0;
	s->midi_st = s->midi_ports ? S_WAIT : S_FAIL;
	/* 24 bytes take about 8 ms at 31250 baud: wait a little so the light is right at once
	 * (the transmit interrupt keeps running meanwhile); otherwise hp_tick checks again */
	for(p = 0; p < 400000 && s->midi_st == S_WAIT; p++)
		if((!(s->midi_ports & 1) || midi_done(s, 0)) && (!(s->midi_ports & 2) || midi_done(s, 1)))
			s->midi_st = S_OK;
}

/* has the transmit interrupt sent everything up to our message's end? */
static int midi_done(struct sl *s, int p)
{
	u32 ring = MIDI_RING(p);
	int rd = *(volatile u16 *)(ring + 0x1002), wr = *(volatile u16 *)(ring + 0x1000);
	int left = (s->midi_pos[p] - rd) & 0xfff, queued = (wr - rd) & 0xfff;
	return left == 0 || left > queued;
}

/* called on every hp_tick: follow the status lights; returns 1 when one changed */
static int sl_poll(struct sl *s)
{
	int ch = 0;
	s->ticks++;
	if(s->midi_st == S_WAIT) {
		int ok = 1, p;
		for(p = 0; p < 2; p++)
			if((s->midi_ports & (p ? 2 : 1)) && !midi_done(s, p)) ok = 0;
		if(ok) { s->midi_st = S_OK; ch = 1; }
		else if(++s->midi_wait > MIDI_TIMEOUT) { s->midi_st = S_FAIL; ch = 1; }
	}
	if(s->reg_st == S_WAIT) {
		if(REG_REQ == s->reg_mem && REG_BANK == s->reg_bank && REG_MEM == s->reg_mem) { s->reg_st = S_OK; ch = 1; }
		else if(++s->reg_wait > REG_TIMEOUT) { s->reg_st = S_FAIL; ch = 1; }
	}
	return ch;
}

/* ---- drawing (mockups/setlist_v1.py) ---- */
static char *num2(char *d, int v) { if(v < 10) *d++ = '0'; return num(d, v, 0); }

static void status_bar(const char *left, const char *right)
{
	fill(0, 0, W - 1, 18, K);
	text(8, 9, left, FT_VALUE, WT, 'l');
	if(right && *right)
		text(W - 8, 9, right, FT_LABEL, WT, 'r');
}

/* triangle pointing up or down, 7 px wide, 5 px tall, tip 2 px from cy */
static void tri(int cx, int cy, int up, int on)
{
	static const u8 hw[5] = { 0, 1, 1, 2, 3 };
	int k;
	for(k = 0; k < 5; k++)
		hspan(cx - hw[k], cx + hw[k], up ? cy - 2 + k : cy + 2 - k, on);
}

/* tab next to side button i (0-4) on the left (A-E) or right (F-J); arrow -1 none, 1 up, 0 down */
static void side_tab(int right, int i, const char *t, int arrow, int filled)
{
	int y = 27 + mul(i, 39), w = 18, x0, x1, fg = filled ? WT : K;
	if(t) { w = text_width(FT_LABEL, t) + 14; if(w < 24) w = 24; }
	x0 = right ? W - 2 - w : 1;
	x1 = right ? W - 2 : 1 + w;
	rrect(x0, y - 8, x1, y + 8, 6, 1, filled ? K : WT);
	rrect2(x0, y - 8, x1, y + 8, 6, K);
	if(t)
		text(half(x0 + x1) + (arrow >= 0 ? 2 : 0), y, t, FT_LABEL, fg, 'm');
	if(arrow >= 0)
		tri(t ? x0 + 7 : half(x0 + x1), y, arrow, fg);
}

enum { B_NONE, B_ON, B_OFF, B_DIS };

static void strip_btn(int i, const char *l1, const char *l2, int st)
{
	int x = mul(i, 40), x0 = x + 3, y0 = STRIP + 4, x1 = x + 36, y1 = H - 4, fg = K, cx, k;
	if(st == B_NONE)
		return;
	if(st == B_ON) { rrect(x0, y0, x1, y1, 6, 1, K); fg = WT; }
	else if(st == B_OFF) rrect2(x0, y0, x1, y1, 6, K);
	else {                                       /* not available: dotted outline */
		for(k = x0 + 4; k < x1 - 3; k += 4) { px(k, y0, K); px(k, y1, K); }
		for(k = y0 + 4; k < y1 - 3; k += 4) { px(x0, k, K); px(x1, k, K); }
	}
	cx = half(x0 + x1);
	if(l2 && *l2) {
		text(cx, STRIP + 13, l1, FT_LABEL, fg, 'm');
		text(cx, STRIP + 25, l2, FT_LABEL, fg, 'm');
	} else
		text(cx, STRIP + 19, l1, FT_LABEL, fg, 'm');
}

static void strip_begin(void) { fill(0, STRIP, W - 1, H - 1, WT); }
static void strip_end(void) { invert_rows(STRIP, H - 1); }

/* section name with automatic numbering ("CHORUS 2") */
static char *sec_name(char *d, const struct song *so, int j)
{
	int t = so->sec[j], k, total = 0, nth = 0;
	for(k = 0; k < so->nsec; k++)
		if(so->sec[k] == t) { total++; if(k <= j) nth++; }
	d = cat(d, SECTION[t]);
	if(total > 1) { *d++ = ' '; d = num(d, nth, 0); }
	return d;
}

static void dialog_box(const char *title, const char *l1, const char *l2, const char *yes, const char *no)
{
	rrect(16, 34, 304, 128, 8, 1, WT);
	rrect2(16, 34, 304, 128, 8, K);
	rrect(19, 37, 301, 125, 6, 0, K);
	text(30, 52, title, FT_VALUE, K, 'l');
	text(30, 75, l1, FT_NAME, K, 'l');
	text(30, 98, l2, FT_LABEL, K, 'l');
	rrect(262, 57, 316, 75, 7, 1, K);            /* YES next to side button G */
	text(289, 66, yes, FT_LABEL, WT, 'm');
	if(no) {                                     /* NO next to side button H */
		rrect(262, 96, 316, 114, 7, 1, WT);
		rrect2(262, 96, 316, 114, 7, K);
		text(289, 105, no, FT_LABEL, K, 'm');
	}
}

static char *song_line(char *d, int i, const struct song *so)   /* "5. Gratitude" */
{
	d = num(d, i + 1, 0);
	d = cat(d, ". ");
	return cat(d, so->title);
}

static char *reg_text(char *d, const struct song *so)        /* "05-1" */
{
	d = num2(d, so->bank + 1);
	*d++ = '-';
	return num(d, so->mem + 1, 0);
}

static void draw_dialog(struct sl *s)
{
	char l1[32], l2[48], *e;
	const struct song *so;
	int to;
	switch(s->dlg) {
	case D_DELETE:
		so = song(s, s->sel);
		song_line(l1, s->sel, so);
		dialog_box("DELETE THIS SONG?", l1, "REGIST AND FL CONFIG STAY", "YES", "NO");
		break;
	case D_PREV:
	case D_NEXT:
		to = s->dlg == D_NEXT ? s->active + 1 : s->active - 1;
		so = song(s, to);
		song_line(l1, to, so);
		e = cat(l2, "KEY ");
		e = cat(e, KEYS[so->key]);
		e = cat(e, "  -  ");
		e = num(e, bpm_of(so), 0);
		e = cat(e, " BPM  -  REG ");
		reg_text(e, so);
		dialog_box(s->dlg == D_NEXT ? "GO TO NEXT SONG?" : "GO TO PREVIOUS SONG?", l1, l2, "YES", "NO");
		break;
	case D_DISCARD:
		dialog_box("DISCARD CHANGES?", s->ed.title, "THE SAVED SONG STAYS AS IT WAS", "YES", "NO");
		break;
	case D_MSG:
		if(s->msg == MSG_FULL) dialog_box("SETLIST FULL", "20 songs is the limit", "DELETE A SONG TO MAKE ROOM", "OK", 0);
		else if(s->msg == MSG_SAVE) dialog_box("COULD NOT SAVE", "Check the floppy disk", "INSERT A DISK THAT IS NOT WRITE PROTECTED", "OK", 0);
		else if(s->msg == MSG_NOFILE) dialog_box("NO SETLIST ON DISK", "Starting a new setlist", "IT IS SAVED AS KYRANOS.SET ON THE FLOPPY", "OK", 0);
		else if(s->msg == MSG_SECFULL) dialog_box("ARRANGEMENT FULL", "16 sections is the limit", "DELETE A SECTION TO ADD ANOTHER", "OK", 0);
		else dialog_box("SETLIST FILE DAMAGED", "KYRANOS.SET not loaded", "THE FILE IS LEFT ON THE DISK AS IT IS", "OK", 0);
		break;
	}
}

static void sl_frame_end(struct sl *s)
{
	static const short rect[4] = { 0, 0, W, H };
	if(s->dlg)
		draw_dialog(s);
	lcd_refresh(FRAMEBUFFER, rect);
	SAR3 = screen_sum();
	clear_dirty();
}

static void draw_list(struct sl *s)
{
	static const char *const l1[8] = { "MOVE", "MOVE", "EDIT", "DEL", "COPY", "OPEN", "NEW", "HOME" };
	static const char *const l2[8] = { "UP", "DOWN", "", "", "", "", "", "" };
	char buf[32], *e;
	int i;
	wait_lcd_idle();
	fill(0, 0, W - 1, H - 1, WT);
	e = num(buf, s->count, 0);
	cat(e, " / 20 SONGS");
	status_bar("SETLIST", buf);
	hatch(19, STRIP - 1);
	strip_begin();
	if(!s->count) {
		rrect(40, 70, 280, 140, 8, 1, WT);
		rrect2(40, 70, 280, 140, 8, K);
		text(160, 92, "NO SONGS YET", FT_VALUE, K, 'm');
		text(160, 112, "PRESS NEW (BUTTON 7)", FT_LABEL, K, 'm');
		text(160, 124, "TO CREATE YOUR FIRST SONG", FT_LABEL, K, 'm');
		for(i = 0; i < 6; i++)
			strip_btn(i, l1[i], l2[i], B_DIS);
		strip_btn(6, "NEW", "SONG", B_ON);
		strip_btn(7, "HOME", "", B_OFF);
	} else {
		for(i = 0; i < 8 && s->top + i < s->count; i++) {
			int idx = s->top + i, y = 23 + mul(i, 22), on = idx == s->sel, fg = on ? WT : K, k;
			const struct song *so = song(s, idx);
			if(on) rrect(6, y, 286, y + 18, 6, 1, K);
			else { rrect(6, y, 286, y + 18, 6, 1, WT); rrect2(6, y, 286, y + 18, 6, K); }
			if(on) rrect(10, y + 3, 34, y + 15, 6, 1, WT);
			else rrect2(10, y + 3, 34, y + 15, 6, K);
			num2(buf, idx + 1);
			text(22, y + 9, buf, FT_LABEL, K, 'm');
			text(41, y + 9, so->title, FT_VALUE, fg, 'l');
			e = cat(buf, KEYS[so->key]);        /* '%-2s  %3d' */
			if(!KEYS[so->key][1]) *e++ = ' ';
			e = cat(e, "  ");
			k = bpm_of(so);
			if(k < 100) *e++ = ' ';
			if(k < 10) *e++ = ' ';
			num(e, k, 0);
			text(280, y + 9, buf, FT_LABEL, fg, 'r');
			if(idx == s->active) {               /* the song being performed */
				rrect(196, y + 4, 226, y + 14, 5, 1, on ? WT : K);
				text(211, y + 9, "LIVE", FT_LABEL, on ? K : WT, 'm');
			}
		}
		side_tab(1, 3, 0, 1, 1);                 /* I = up */
		side_tab(1, 4, 0, 0, 1);                 /* J = down */
		for(i = 0; i < 8; i++)
			strip_btn(i, l1[i], l2[i], i == 5 ? B_ON : B_OFF);
	}
	strip_end();
	sl_frame_end(s);
}

static void draw_edit(struct sl *s)
{
	static const char *const lab[5] = { "TITLE", "KEY", "BPM", "FL CONFIG", "REGIST" };
	const struct song *so = &s->ed;
	char buf[32], *e;
	int i, j, y;
	wait_lcd_idle();
	fill(0, 0, W - 1, H - 1, WT);
	if(s->edit_idx == 0xff)
		buf[0] = 0;
	else {
		e = cat(buf, "SONG ");
		num2(e, s->edit_idx + 1);
	}
	status_bar(s->edit_idx == 0xff ? "NEW SONG" : "EDIT SONG", buf);
	hatch(19, STRIP - 1);
	for(i = 0; i < 5; i++) {                     /* fields next to side buttons A-E */
		int on = i == s->field, fg = on ? WT : K, f = FT_VALUE, k;
		static const u8 pw[6] = { 6, 4, 3, 2, 1, 0 };   /* pointer width by distance from the tip row */
		y = 29 + mul(i, 39);
		if(on) rrect(14, y - 8, 152, y + 8, 7, 1, K);
		else { rrect(14, y - 8, 152, y + 8, 7, 1, WT); rrect2(14, y - 8, 152, y + 8, 7, K); }
		text(21, y, lab[i], FT_LABEL, fg, 'l');
		switch(i) {
		case 0: cat(buf, so->title); break;
		case 1: cat(buf, KEYS[so->key]); break;
		case 2: num(buf, bpm_of(so), 0); break;
		case 3: num(buf, so->cfg, 0); break;
		default: e = cat(buf, "BANK "); e = num2(e, so->bank + 1); e = cat(e, " - "); num(e, so->mem + 1, 0);
		}
		if(text_width(f, buf) > 146 - 21 - text_width(FT_LABEL, lab[i]) - 6)
			f = FT_LABEL;                        /* long titles: smaller font */
		text(146, y, buf, f, fg, 'r');
		for(k = -5; k <= 5; k++) {               /* side-button pointer (2,y-5) (8,y) (2,y+5) */
			int w = pw[k < 0 ? -k : k];
			if(on) hspan(2, 2 + w, y + k, K);
			else { hspan(2, 2 + w, y + k, WT); px(2, y + k, K); px(2 + w, y + k, K); }
		}
	}
	/* arrangement column, sections chosen with I / J */
	rrect(158, 22, 296, STRIP - 5, 7, 1, WT);
	rrect2(158, 22, 296, STRIP - 5, 7, K);
	text(166, 31, "ARRANGEMENT", FT_LABEL, K, 'l');
	e = num(buf, so->nsec, 0);
	cat(e, "/16");
	text(290, 31, buf, FT_LABEL, K, 'r');
	hspan(162, 292, 39, K);
	for(j = 0; j < 12 && s->sectop + j < so->nsec; j++) {
		int idx = s->sectop + j, on = idx == s->secsel, fg = on ? WT : K;
		y = 47 + mul(j, 12);
		if(on) rrect(161, y - 6, 293, y + 5, 4, 1, K);
		num(buf, idx + 1, 0);
		text(176, y, buf, FT_LABEL, fg, 'r');
		sec_name(buf, so, idx);
		text(182, y, buf, FT_LABEL, fg, 'l');
	}
	side_tab(1, 3, 0, 1, 1);
	side_tab(1, 4, 0, 0, 1);
	strip_begin();
	strip_btn(0, "ADD", "", so->nsec < MAXSEC ? B_OFF : B_DIS);
	strip_btn(1, "DEL", "", so->nsec ? B_OFF : B_DIS);
	strip_btn(2, "MOVE", "UP", so->nsec > 1 ? B_OFF : B_DIS);
	strip_btn(3, "MOVE", "DOWN", so->nsec > 1 ? B_OFF : B_DIS);
	strip_btn(5, "NAME", "", B_OFF);
	strip_btn(6, "BACK", "", B_OFF);
	strip_btn(7, "SAVE", "", B_ON);
	strip_end();
	sl_frame_end(s);
}

/* naming screen, laid out like Yamaha's (Disk > Backup > name): hint text, NAME field,
 * a grid of characters chosen with the dial, CLR / DEL / arrows / ENTRY, CANCEL / END */
static void draw_name(struct sl *s)
{
	char buf[2];
	int i, x, y, n = 0;
	wait_lcd_idle();
	fill(0, 0, W - 1, H - 1, WT);
	status_bar("SONG TITLE", "UP TO 15 CHARACTERS");
	hatch(19, STRIP - 1);
	rrect(24, 24, 296, 76, 7, 1, WT);
	rrect2(24, 24, 296, 76, 7, K);
	text(34, 35, "Name the song.", FT_VALUE, K, 'l');
	text(34, 51, "DIAL: CHOOSE A CHARACTER    ENTRY: ADD IT", FT_LABEL, K, 'l');
	text(34, 64, "END: DONE    CANCEL: KEEP THE OLD NAME", FT_LABEL, K, 'l');
	/* NAME field with the cursor */
	rrect(24, 82, 296, 106, 7, 1, WT);
	rrect2(24, 82, 296, 106, 7, K);
	rrect(30, 87, 70, 101, 6, 1, K);
	text(50, 94, "NAME", FT_LABEL, WT, 'm');
	text(80, 94, s->name, FT_NAME, K, 'l');
	while(s->name[n]) n++;
	{
		char pre[16];
		int cx;
		for(i = 0; i < s->name_pos && i < n; i++) pre[i] = s->name[i];
		pre[i] = 0;
		cx = 80 + (i ? text_width(FT_NAME, pre) + 1 : 0);
		fill(cx, 102, cx + 7, 103, K);
	}
	/* character grid: 3 rows of 26 */
	rrect(24, 112, 296, 196, 7, 1, WT);
	rrect2(24, 112, 296, 196, 7, K);
	buf[1] = 0;
	for(i = 0; i < NCHARS; i++) {
		int r = 0, c = i, on = i == s->name_ch;
		while(c >= 26) { c -= 26; r++; }
		x = 47 + mul(c, 9);
		y = 130 + mul(r, 24);
		buf[0] = CHARS[i];
		if(on) rrect(x - 5, y - 8, x + 5, y + 8, 3, 1, K);
		text(x, y, buf, FT_VALUE, on ? WT : K, 'm');
	}
	side_tab(0, 3, 0, 1, 1);                     /* D = row up */
	side_tab(0, 4, 0, 0, 1);                     /* E = row down */
	strip_begin();
	strip_btn(0, "CLR", "", B_OFF);
	strip_btn(1, "DEL", "", B_OFF);
	strip_btn(2, "<", "", B_OFF);
	strip_btn(3, ">", "", B_OFF);
	strip_btn(4, "ENTRY", "", B_OFF);
	strip_btn(6, "CANCEL", "", B_OFF);
	strip_btn(7, "END", "", B_ON);
	strip_end();
	sl_frame_end(s);
}

static void light(int cx, int y, const char *label, int st)
{
	int k;
	if(st == S_OK) rrect(cx - 4, y - 4, cx + 4, y + 4, 4, 1, K);
	else if(st == S_FAIL)
		for(k = -4; k <= 4; k++) {
			fill(cx + k, y + k, cx + k + 1, y + k, K);
			fill(cx + k, y - k, cx + k + 1, y - k, K);
		}
	else rrect2(cx - 4, y - 4, cx + 4, y + 4, 4, K);
	text(cx + 8, y, label, FT_LABEL, K, 'l');
}

static void draw_perf(struct sl *s)
{
	const struct song *so = song(s, s->active);
	char buf[40], *e;
	int x, j, i;
	wait_lcd_idle();
	fill(0, 0, W - 1, H - 1, WT);
	e = cat(buf, "SONG ");
	e = num(e, s->active + 1, 0);
	e = cat(e, " / ");
	num(e, s->count, 0);
	{
		char r[40];
		if(s->active + 1 < s->count) {
			e = cat(r, "NEXT: ");
			cat(e, song(s, s->active + 1)->title);
			upper(r);
		} else
			cat(r, "LAST SONG");
		status_bar(buf, r);
	}
	text(10, 33, so->title, FT_NAME, K, 'l');     /* the biggest text on the screen */
	x = 10;
	for(i = 0; i < 2; i++) {                     /* KEY and BPM pills */
		int w;
		if(i == 0) { e = cat(buf, "KEY "); cat(e, KEYS[so->key]); }
		else { e = num(buf, bpm_of(so), 0); cat(e, " BPM"); }
		w = text_width(FT_VALUE, buf) + 12;
		rrect(x, 46, x + w, 60, 6, 1, K);
		text(x + half(w), 53, buf, FT_VALUE, WT, 'm');
		x += w + 6;
	}
	e = cat(buf, "FL ");
	e = num(e, so->cfg, 0);
	e = cat(e, "   REG ");
	reg_text(e, so);
	text(W - 10, 53, buf, FT_LABEL, K, 'r');
	rrect(6, 66, W - 7, STRIP - 5, 7, 1, WT);
	rrect2(6, 66, W - 7, STRIP - 5, 7, K);
	for(j = 0; j < so->nsec; j++) {               /* full arrangement, 2 columns of 8 */
		int col = j >= 8, row = col ? j - 8 : j, y;
		x = col ? 166 : 14;
		y = 77 + mul(row, 15);
		rrect2(x, y - 6, x + 18, y + 6, 5, K);
		num(buf, j + 1, 0);
		text(x + 9, y, buf, FT_LABEL, K, 'm');
		sec_name(buf, so, j);
		text(x + 25, y, buf, FT_VALUE, K, 'l');
	}
	if(so->nsec > 8)
		for(j = 72; j <= STRIP - 11; j++) px(160, j, K);
	strip_begin();
	strip_btn(0, "PREV", "", s->active > 0 ? B_OFF : B_DIS);
	strip_btn(1, "NEXT", "", s->active + 1 < s->count ? B_OFF : B_DIS);
	strip_btn(6, "HOME", "", B_OFF);
	strip_btn(7, "LIST", "", B_OFF);
	light(100, STRIP + 12, s->midi_st == S_OK ? "MIDI SENT" : s->midi_st == S_FAIL ? "MIDI FAILED" : "MIDI SENDING", s->midi_st);
	light(100, STRIP + 26, s->reg_st == S_OK ? "REGIST LOADED" : s->reg_st == S_FAIL ? "REGIST FAILED" : "REGIST LOADING", s->reg_st);
	strip_end();
	sl_frame_end(s);
}

static void sl_draw(struct sl *s)
{
	if(s->mode == M_PERF && s->active >= s->count)
		s->mode = M_LIST;
	if(s->mode == M_LIST) draw_list(s);
	else if(s->mode == M_EDIT) draw_edit(s);
	else if(s->mode == M_NAME) draw_name(s);
	else if(s->mode == M_PERF) draw_perf(s);
}

/* ---- actions ---- */
static void swap_songs(struct sl *s, int a, int b)
{
	struct song t, *pa = song(s, a), *pb = song(s, b);
	bcopy8(&t, pa, sizeof t);
	bcopy8(pa, pb, sizeof t);
	bcopy8(pb, &t, sizeof t);
	if(s->active == a) s->active = b;
	else if(s->active == b) s->active = a;
}

static void list_scroll(struct sl *s)
{
	if(s->sel >= s->count) s->sel = s->count ? s->count - 1 : 0;
	if(s->sel < s->top) s->top = s->sel;
	if(s->sel >= s->top + 8) s->top = s->sel - 7;
}

static void sec_scroll(struct sl *s)
{
	if(s->secsel >= s->ed.nsec) s->secsel = s->ed.nsec ? s->ed.nsec - 1 : 0;
	if(s->secsel < s->sectop) s->sectop = s->secsel;
	if(s->secsel >= s->sectop + 12) s->sectop = s->secsel - 11;
}

static void delete_song(struct sl *s, int i)
{
	int k;
	for(k = i; k + 1 < s->count; k++)
		bcopy8(song(s, k), song(s, k + 1), sizeof(struct song));
	s->count--;
	if(s->active == i) s->active = 0xff;
	else if(s->active != 0xff && s->active > i) s->active--;
	list_scroll(s);
}

static void insert_song(struct sl *s, int at, const struct song *src)
{
	int k;
	for(k = s->count; k > at; k--)
		bcopy8(song(s, k), song(s, k - 1), sizeof(struct song));
	bcopy8(song(s, at), src, sizeof(struct song));
	s->count++;
	if(s->active != 0xff && s->active >= at) s->active++;
}

static void start_edit(struct sl *s, int idx)
{
	if(idx == 0xff) {
		bzero8(&s->ed, sizeof s->ed);
		cat(s->ed.title, "New Song");
		set_bpm(&s->ed, 120);
		s->ed.cfg = 1;
	} else
		bcopy8(&s->ed, song(s, idx), sizeof s->ed);
	s->edit_idx = idx;
	s->field = 0;
	s->target = 0;
	s->secsel = s->sectop = 0;
	s->changed = 0;
	s->mode = M_EDIT;
}

static void save_edit(struct sl *s)
{
	if(s->edit_idx == 0xff) {
		if(s->count >= MAXSONGS) { s->dlg = D_MSG; s->msg = MSG_FULL; return; }
		s->ed.id = new_id(s);
		insert_song(s, s->count, &s->ed);    /* new songs go to the end of the list */
		s->sel = s->count - 1;
	} else {
		bcopy8(song(s, s->edit_idx), &s->ed, sizeof s->ed);   /* only this song changes */
		s->sel = s->edit_idx;
	}
	s->mode = M_LIST;
	list_scroll(s);
	save_or_warn(s);
}

static void sec_add(struct sl *s)
{
	struct song *so = &s->ed;
	int at = so->nsec ? s->secsel + 1 : 0, k;
	if(so->nsec >= MAXSEC) { s->dlg = D_MSG; s->msg = MSG_SECFULL; return; }
	for(k = so->nsec; k > at; k--) so->sec[k] = so->sec[k - 1];
	so->sec[at] = so->nsec ? 1 : 0;              /* first section INTRO, then VERSE; dial changes it */
	so->nsec++;
	s->secsel = at;
	s->target = 1;
	s->changed = 1;
	sec_scroll(s);
}

static void sec_del(struct sl *s)
{
	struct song *so = &s->ed;
	int k;
	if(!so->nsec) return;
	for(k = s->secsel; k + 1 < so->nsec; k++) so->sec[k] = so->sec[k + 1];
	so->nsec--;
	s->changed = 1;
	sec_scroll(s);
}

static void sec_move(struct sl *s, int d)
{
	struct song *so = &s->ed;
	int a = s->secsel, b = a + d, t;
	if(b < 0 || b >= so->nsec) return;
	t = so->sec[a]; so->sec[a] = so->sec[b]; so->sec[b] = t;
	s->secsel = b;
	s->changed = 1;
	sec_scroll(s);
}

static int name_len(struct sl *s) { int n = 0; while(s->name[n]) n++; return n; }

static void name_key(struct sl *s, int n)
{
	int len = name_len(s), k;
	switch(n) {
	case 0: s->name[0] = 0; s->name_pos = 0; break;                       /* CLR */
	case 1:                                                                /* DEL */
		if(s->name_pos < len) k = s->name_pos;
		else if(len) { k = len - 1; s->name_pos = k; }
		else break;
		for(; s->name[k]; k++) s->name[k] = s->name[k + 1];
		break;
	case 2: if(s->name_pos) s->name_pos--; break;                         /* left */
	case 3:                                                                /* right: past the end adds a space */
		if(s->name_pos < len) s->name_pos++;
		else if(len < TITLE_MAX) { s->name[len] = ' '; s->name[len + 1] = 0; s->name_pos++; }
		break;
	case 4:                                                                /* ENTRY: write the character */
		if(s->name_pos >= TITLE_MAX) break;
		s->name[s->name_pos] = CHARS[s->name_ch];
		if(s->name_pos >= len) s->name[s->name_pos + 1] = 0;
		s->name_pos++;
		break;
	case 6: s->mode = M_EDIT; break;                                      /* CANCEL */
	case 7:                                                                /* END */
		while(len && s->name[len - 1] == ' ') s->name[--len] = 0;
		if(len) { bcopy8(s->ed.title, s->name, 16); s->changed = 1; }
		s->mode = M_EDIT;
		break;
	}
}

static void sl_yes(struct sl *s)
{
	int d = s->dlg;
	s->dlg = D_NONE;
	if(d == D_DELETE) { delete_song(s, s->sel); save_or_warn(s); }
	else if(d == D_PREV) activate(s, s->active - 1);
	else if(d == D_NEXT) activate(s, s->active + 1);
	else if(d == D_DISCARD) s->mode = M_LIST;
}

static void sl_leave_list(struct sl *s) { if(s->moved) save_or_warn(s); }

/* LCD 1-8 (n = 0..7) */
static void sl_button(struct sl *s, int n)
{
	if(s->dlg)
		return;
	switch(s->mode) {
	case M_LIST:
		if(n == 7) { sl_leave_list(s); s->mode = M_HOME; return; }
		if(n == 6) {
			if(s->count >= MAXSONGS) { s->dlg = D_MSG; s->msg = MSG_FULL; }
			else { sl_leave_list(s); start_edit(s, 0xff); }
			return;
		}
		if(!s->count) return;
		if(n == 0 && s->sel > 0) { swap_songs(s, s->sel, s->sel - 1); s->sel--; s->moved = 1; }
		else if(n == 1 && s->sel + 1 < s->count) { swap_songs(s, s->sel, s->sel + 1); s->sel++; s->moved = 1; }
		else if(n == 2) { sl_leave_list(s); start_edit(s, s->sel); }
		else if(n == 3) s->dlg = D_DELETE;
		else if(n == 4) {                        /* duplicate: independent copy, new id */
			struct song t;
			if(s->count >= MAXSONGS) { s->dlg = D_MSG; s->msg = MSG_FULL; return; }
			bcopy8(&t, song(s, s->sel), sizeof t);
			t.id = new_id(s);
			insert_song(s, s->sel + 1, &t);
			s->sel++;
			save_or_warn(s);
		} else if(n == 5) { sl_leave_list(s); activate(s, s->sel); }
		list_scroll(s);
		break;
	case M_EDIT:
		if(n == 0) sec_add(s);
		else if(n == 1) sec_del(s);
		else if(n == 2) sec_move(s, -1);
		else if(n == 3) sec_move(s, 1);
		else if(n == 5) {
			bcopy8(s->name, s->ed.title, 16);
			s->name_pos = name_len(s);
			if(s->name_pos > TITLE_MAX - 1) s->name_pos = TITLE_MAX - 1;
			s->mode = M_NAME;
		} else if(n == 6) { if(s->changed) s->dlg = D_DISCARD; else s->mode = M_LIST; }
		else if(n == 7) save_edit(s);
		break;
	case M_NAME:
		name_key(s, n);
		break;
	case M_PERF:
		if(n == 0 && s->active > 0) s->dlg = D_PREV;
		else if(n == 1 && s->active + 1 < s->count) s->dlg = D_NEXT;
		else if(n == 6) s->mode = M_HOME;
		else if(n == 7) { s->sel = s->active; list_scroll(s); s->mode = M_LIST; }
		break;
	}
}

/* side buttons A-J (n = 0..9) */
static void sl_side(struct sl *s, int n)
{
	if(s->dlg) {
		if(n == 6) sl_yes(s);
		else if(n == 7 && s->dlg != D_MSG) s->dlg = D_NONE;
		return;
	}
	if(s->mode == M_LIST && s->count) {
		if(n == 8 && s->sel > 0) s->sel--;
		else if(n == 9 && s->sel + 1 < s->count) s->sel++;
		list_scroll(s);
	} else if(s->mode == M_EDIT) {
		if(n < 5) { s->field = n; s->target = 0; }
		else if(n == 8 && s->ed.nsec) { if(s->secsel > 0) s->secsel--; s->target = 1; }
		else if(n == 9 && s->ed.nsec) { if(s->secsel + 1 < s->ed.nsec) s->secsel++; s->target = 1; }
		sec_scroll(s);
	} else if(s->mode == M_NAME) {
		if(n == 3 && s->name_ch >= 26) s->name_ch -= 26;
		else if(n == 4 && s->name_ch + 26 < NCHARS) s->name_ch += 26;
	}
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrapi(int v, int n) { while(v < 0) v += n; while(v >= n) v -= n; return v; }

static void sl_dial(struct sl *s, int d)
{
	struct song *so = &s->ed;
	if(s->dlg)
		return;
	if(s->mode == M_LIST && s->count) {
		s->sel = clampi(s->sel + d, 0, s->count - 1);
		list_scroll(s);
	} else if(s->mode == M_EDIT) {
		if(s->target == 1 && so->nsec)
			so->sec[s->secsel] = wrapi(so->sec[s->secsel] + d, NSECTYPES);
		else if(s->field == 1) so->key = wrapi(so->key + d, 12);
		else if(s->field == 2) set_bpm(so, clampi(bpm_of(so) + d, 20, 300));
		else if(s->field == 3) so->cfg = clampi(so->cfg + d, 0, 127);
		else if(s->field == 4) {
			int r = clampi(shl(so->bank, 3) + so->mem + d, 0, 511);
			so->bank = shr(r, 3);
			so->mem = r & 7;
		} else
			return;                              /* TITLE: use NAME (LCD 6) */
		s->changed = 1;
	} else if(s->mode == M_NAME)
		s->name_ch = wrapi(s->name_ch + d, NCHARS);
}

/* side button G on the home screen: open the setlist (the performance screen if a song is
 * active, as left with HOME) */
static void sl_enter(void)
{
	struct sl *s = sl_find();
	if(!s) s = sl_new();
	if(!s) return;
	if(!s->loaded && !s->count) {
		int r = sl_load(s);
		if(r) { s->dlg = D_MSG; s->msg = r == 2 ? MSG_BADFILE : MSG_NOFILE; }
		if(r == 1) s->loaded = 1;                /* no file: start a new one (saved on the first change) */
	}
	s->mode = (s->active != 0xff && s->active < s->count) ? M_PERF : M_LIST;
	list_scroll(s);
}

/* EXIT: one level back */
static void sl_exit(struct sl *s)
{
	if(s->dlg) { s->dlg = D_NONE; return; }   /* = NO (or OK on a message) */
	if(s->mode == M_NAME) s->mode = M_EDIT;
	else if(s->mode == M_EDIT) { if(s->changed) s->dlg = D_DISCARD; else s->mode = M_LIST; }
	else if(s->mode == M_LIST) { sl_leave_list(s); s->mode = M_HOME; }
	else s->mode = M_HOME;
}

/* the setlist screen that should be on top now, or 0 */
static struct sl *sl_shown(void)
{
	struct sl *s = sl_find();
	return (s && s->mode != M_HOME && visible() && !eq_on()) ? s : 0;
}
