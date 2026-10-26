/*
 * P96Multicast - the sender process.
 *
 * Nothing tells a card when pixels change: rtg.library and applications
 * write straight into the video RAM. So the sender polls. FPS times a second
 * it compares the visible part of the displayed bitmap with a shadow copy,
 * tile by tile, and sends each changed tile to the multicast group (see
 * include/p96mc_proto.h). Tiles left over when the byte budget of a pass
 * runs out are still different from the shadow and go out in the next pass.
 * A slow background refresh resends unchanged tiles from the shadow, and
 * MODE and PALETTE go out once a second, so receivers that join late or
 * lose packets catch up.
 *
 * bsdsocket.library bases belong to the task that opened them, so the
 * sender opens its own, waiting until a TCP/IP stack is running.
 */
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include "mcard.h"
#include "p96mc_proto.h"

#define TILE_ROWBYTES 64  /* tile width in bytes (before clipping) */
#define TILE_H        16

/*
 * BLITTER=1 accounting: a tile a recorded drawing call touched counts as
 * explained for this many passes (the call is recorded before it draws, and
 * a slow pass or the byte budget can leave a tile unchecked for a while).
 */
#define DAMAGE_AGE    50
#define KIND_MODE     0xff  /* a new mode: not counted */
#define KIND_SWITCH   0xfd  /* another bitmap put on display */
#define KIND_NONE     0xfe

struct Sender {
	struct McCard *mc;
	struct Library *sockbase;
	LONG   sock;
	struct sockaddr_in to;
	ULONG  stream;
	ULONG  frame;
	ULONG  pass;

	/* geometry of the visible screen, from the last snapshot */
	ULONG  serial;
	ULONG  pal_serial;
	UBYTE *src;      /* top left visible pixel */
	ULONG  bpr;      /* source bytes per row */
	UWORD  w, h;
	UWORD  bpp;
	RGBFTYPE fmt;
	UWORD  display;

	UWORD  tw, th;   /* tile size in pixels */
	UWORD  tcols, trows;
	ULONG  ntiles;
	ULONG  cursor;   /* next tile to check */
	ULONG  rcursor;  /* next tile to refresh */

	UBYTE *shadow;   /* w * bpp bytes per row, what receivers were sent */
	ULONG  shadowsize;
	ULONG  srow;

	/* BLITTER=1 accounting, per tile: passes left as explained, and by what */
	UBYTE *touched;  /* 2 * ntiles bytes: ages, then kinds */
	ULONG  touchedsize;
	UBYTE *bm;       /* the bitmap on display */
	ULONG  dtail;    /* damage rectangles taken off the ring so far */

	/* statistics, for the log */
	ULONG  packets, bytes, errors;
	LONG   lasterror;
	BOOL   logstarted;

	UBYTE  pkt[MC_MAX_DGRAM];
};

/*
 * A line in the log file, if LOG= is set; up to four LONG arguments. The
 * file is opened for each line, so it can be read while the card runs.
 * The second board logs to LOG.2, the third to LOG.3, ...
 */
static void mc_log(struct Sender *s, const char *fmt, LONG a, LONG b, LONG c, LONG d)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	char path[sizeof(s->mc->cfg.log) + 2];
	APTR oldwin;
	LONG args[4];
	BPTR fh;
	int i;

	if (!s->mc->cfg.log[0])
		return;
	for (i = 0; s->mc->cfg.log[i]; i++)
		path[i] = s->mc->cfg.log[i];
	if (s->mc->boardnum) {
		path[i++] = '.';
		path[i++] = '1' + s->mc->boardnum;
	}
	path[i] = 0;
	args[0] = a;
	args[1] = b;
	args[2] = c;
	args[3] = d;
	oldwin = me->pr_WindowPtr;
	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)path, s->logstarted ? MODE_READWRITE : MODE_NEWFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return;
	s->logstarted = TRUE;
	Seek(fh, 0, OFFSET_END);
	VFPrintf(fh, (STRPTR)fmt, args);
	Close(fh);
}

static void put16(UBYTE *p, UWORD v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static void put32(UBYTE *p, ULONG v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void header(struct Sender *s, UBYTE type, UWORD flags)
{
	put32(s->pkt, MC_MAGIC);
	s->pkt[4] = MC_VERSION;
	s->pkt[5] = type;
	put16(s->pkt + 6, flags);
	put32(s->pkt + 8, s->stream);
	put32(s->pkt + 12, s->frame);
}

static BOOL send_pkt(struct Sender *s, ULONG len)
{
	struct Library *SocketBase = s->sockbase;

	if (sendto(s->sock, s->pkt, len, 0, (struct sockaddr *)&s->to, sizeof(s->to)) == (LONG)len) {
		s->packets++;
		s->bytes += len;
		return TRUE;
	}
	if (!s->errors++ || Errno() != s->lasterror)
		mc_log(s, "sendto failed: errno %ld (%ld errors so far)\n", Errno(), s->errors, 0, 0);
	s->lasterror = Errno();
	return FALSE;
}

static void send_mode(struct Sender *s)
{
	UBYTE *p = s->pkt + MC_HDR_SIZE;

	header(s, MC_MODE, 0);
	put16(p, s->w);
	put16(p + 2, s->h);
	p[4] = s->fmt;
	p[5] = s->bpp;
	p[6] = s->display;
	p[7] = 0;
	put16(p + 8, s->tw);
	put16(p + 10, s->th);
	put16(p + 12, s->mc->cfg.fps);
	put16(p + 14, 0);
	send_pkt(s, MC_HDR_SIZE + 16);
}

static void send_palette(struct Sender *s)
{
	struct McCard *mc = s->mc;
	UBYTE *p = s->pkt + MC_HDR_SIZE;

	header(s, MC_PALETTE, 0);
	put16(p, 0);
	put16(p + 2, 256);
	CopyMem(mc->pal, p + 4, 256 * 3);
	send_pkt(s, MC_HDR_SIZE + 4 + 256 * 3);
}

static void send_sync(struct Sender *s, UWORD tiles)
{
	UBYTE *p = s->pkt + MC_HDR_SIZE;

	header(s, MC_SYNC, 0);
	put16(p, tiles);
	put16(p + 2, 0);
	send_pkt(s, MC_HDR_SIZE + 4);
}

/* TRUE if n bytes at a and b differ; a and b may be unaligned (68020+) */
static BOOL differ(const UBYTE *a, const UBYTE *b, ULONG n)
{
	const ULONG *la = (const ULONG *)a, *lb = (const ULONG *)b;

	for (; n >= 4; n -= 4)
		if (*la++ != *lb++)
			return TRUE;
	a = (const UBYTE *)la;
	b = (const UBYTE *)lb;
	while (n--)
		if (*a++ != *b++)
			return TRUE;
	return FALSE;
}

/* tile t: pixel position and clipped size */
static void tile_rect(struct Sender *s, ULONG t, UWORD *x, UWORD *y, UWORD *w, UWORD *h)
{
	*x = (t % s->tcols) * s->tw;
	*y = (t / s->tcols) * s->th;
	*w = s->w - *x < s->tw ? s->w - *x : s->tw;
	*h = s->h - *y < s->th ? s->h - *y : s->th;
}

/* does the visible tile differ from the shadow? */
static BOOL tile_changed(struct Sender *s, UWORD x, UWORD y, UWORD w, UWORD h)
{
	const UBYTE *src = s->src + y * s->bpr + x * s->bpp;
	const UBYTE *sh = s->shadow + y * s->srow + x * s->bpp;
	ULONG n = (ULONG)w * s->bpp;

	while (h--) {
		if (differ(src, sh, n))
			return TRUE;
		src += s->bpr;
		sh += s->srow;
	}
	return FALSE;
}

/* copy the visible tile into the shadow */
static void tile_capture(struct Sender *s, UWORD x, UWORD y, UWORD w, UWORD h)
{
	const UBYTE *src = s->src + y * s->bpr + x * s->bpp;
	UBYTE *sh = s->shadow + y * s->srow + x * s->bpp;
	ULONG n = (ULONG)w * s->bpp;

	while (h--) {
		CopyMem((APTR)src, sh, n);
		src += s->bpr;
		sh += s->srow;
	}
}

/* send a tile from the shadow; returns the bytes sent, 0 on failure */
static ULONG tile_send(struct Sender *s, UWORD x, UWORD y, UWORD w, UWORD h, UWORD flags)
{
	UBYTE *p = s->pkt + MC_HDR_SIZE;
	UBYTE *d = p + 10;
	const UBYTE *sh = s->shadow + y * s->srow + x * s->bpp;
	ULONG n = (ULONG)w * s->bpp, len, r, i;
	BOOL solid = TRUE;

	/* one colour? then one pixel is enough (no division per byte on 68k) */
	for (r = 0; r < h && solid; r++) {
		const UBYTE *row = sh + r * s->srow;
		ULONG j = 0;

		for (i = 0; i < n; i++) {
			if (row[i] != sh[j]) {
				solid = FALSE;
				break;
			}
			if (++j == s->bpp)
				j = 0;
		}
	}

	header(s, MC_TILE, flags);
	put16(p, x);
	put16(p + 2, y);
	put16(p + 4, w);
	put16(p + 6, h);
	p[9] = 0;
	if (solid) {
		p[8] = MC_ENC_SOLID;
		CopyMem((APTR)sh, d, s->bpp);
		len = MC_HDR_SIZE + 10 + s->bpp;
	} else {
		p[8] = MC_ENC_RAW;
		for (r = 0; r < h; r++) {
			CopyMem((APTR)(sh + r * s->srow), d, n);
			d += n;
		}
		len = MC_HDR_SIZE + 10 + n * h;
	}
	return send_pkt(s, len) ? len : 0;
}

/*
 * Take a consistent copy of the display state. Returns FALSE when there is
 * nothing to show.
 */
static BOOL snapshot(struct Sender *s, BOOL *geometry)
{
	struct McCard *mc = s->mc;
	UBYTE *bm;
	UWORD bmw, w, h, display;
	WORD xoff, yoff;
	RGBFTYPE fmt;
	ULONG serial, bpp;

	Forbid();
	serial = mc->serial;
	bm = mc->bm_mem;
	bmw = mc->bm_width;
	xoff = mc->xoff;
	yoff = mc->yoff;
	w = mc->width;
	h = mc->height;
	fmt = mc->format;
	display = mc->display && mc->moniswitch;
	Permit();

	*geometry = FALSE;
	if (serial == s->serial)
		return s->src != NULL && s->display;
	s->serial = serial;
	if (bm != s->bm && s->bm && s->touched) {
		/* another screen in the same mode: all of it replaced at once */
		ULONG t;

		for (t = 0; t < s->ntiles; t++) {
			s->touched[t] = DAMAGE_AGE;
			s->touched[s->ntiles + t] = KIND_SWITCH;
		}
	}
	s->bm = bm;

	bpp = mc_bytesperpixel(fmt);
	if (!bm || !bpp || !w || !h) {
		s->src = NULL;
		s->display = 0;
		return FALSE;
	}
	if (bmw < w)
		bmw = w;
	if (xoff < 0)
		xoff = 0;
	if (yoff < 0)
		yoff = 0;
	s->bpr = mc_bytesperrow(bmw, fmt);
	s->src = bm + yoff * s->bpr + xoff * bpp;

	if (w != s->w || h != s->h || fmt != s->fmt || display != s->display) {
		*geometry = TRUE;
		s->w = w;
		s->h = h;
		s->fmt = fmt;
		s->bpp = bpp;
		s->display = display;
		s->tw = TILE_ROWBYTES / bpp;
		s->th = TILE_H;
		s->tcols = (w + s->tw - 1) / s->tw;
		s->trows = (h + s->th - 1) / s->th;
		s->ntiles = (ULONG)s->tcols * s->trows;
		s->cursor = s->rcursor = 0;
		s->srow = (ULONG)w * bpp;
		if (s->shadow)
			FreeMem(s->shadow, s->shadowsize);
		s->shadowsize = s->srow * h;
		s->shadow = AllocMem(s->shadowsize, MEMF_PUBLIC);
		if (!s->shadow) {
			s->shadowsize = 0;
			s->src = NULL;
			return FALSE;
		}
		/* a new mode explains all of its tiles */
		if (s->touched)
			FreeMem(s->touched, s->touchedsize);
		s->touchedsize = 2 * s->ntiles;
		s->touched = AllocMem(s->touchedsize, MEMF_PUBLIC);
		if (s->touched) {
			ULONG t;

			for (t = 0; t < s->ntiles; t++) {
				s->touched[t] = DAMAGE_AGE;
				s->touched[s->ntiles + t] = KIND_MODE;
			}
		}
		s->dtail = mc->damage_head;
		/* make every tile differ, so the whole screen goes out */
		{
			ULONG t;
			UWORD x, y, tw, th;

			for (t = 0; t < s->ntiles; t++) {
				tile_rect(s, t, &x, &y, &tw, &th);
				tile_capture(s, x, y, tw, th);
				s->shadow[y * s->srow + x * bpp] ^= 0xff;
			}
		}
	}
	return display != 0;
}

/* BLITTER=1: the rectangles the drawing calls recorded mark their tiles */
static void take_damage(struct Sender *s)
{
	struct McCard *mc = s->mc;
	struct McRect r;
	ULONG head;
	LONG tx, ty, tx0, tx1, ty1;

	if (!s->touched)
		return;
	head = mc->damage_head;
	if (head - s->dtail > MC_DAMAGE_RING) {
		/* fell behind: the oldest are overwritten, so all tiles count */
		ULONG t;

		for (t = 0; t < s->ntiles; t++)
			s->touched[t] = DAMAGE_AGE;
		if (mc->boardnum == 0)
			mc_status->damage_lost++;
		s->dtail = head;
		return;
	}
	for (; s->dtail != head; s->dtail++) {
		Forbid();
		r = mc->damage[s->dtail % MC_DAMAGE_RING];
		Permit();
		if (r.x >= s->w || r.y >= s->h)
			continue;
		tx0 = r.x / s->tw;
		tx1 = (r.x + r.w - 1) / s->tw;
		ty1 = (r.y + r.h - 1) / s->th;
		if (tx1 >= s->tcols)
			tx1 = s->tcols - 1;
		if (ty1 >= s->trows)
			ty1 = s->trows - 1;
		for (ty = r.y / s->th; ty <= ty1; ty++)
			for (tx = tx0; tx <= tx1; tx++) {
				ULONG t = ty * s->tcols + tx;

				s->touched[t] = DAMAGE_AGE;
				s->touched[s->ntiles + t] = r.kind;
			}
	}
}

static void update_pass(struct Sender *s)
{
	struct McConfig *cfg = &s->mc->cfg;
	BOOL geometry, visible;
	LONG budget = cfg->rate / cfg->fps;
	ULONG t, checked, sent = 0, refresh;
	UWORD x, y, w, h;

	visible = snapshot(s, &geometry);
	s->pass++;
	if (geometry)
		mc_log(s, "screen %ld x %ld, format %ld, display %ld\n",
		       s->w, s->h, s->fmt, s->display);
	if (s->pass % (10 * cfg->fps) == 0)
		mc_log(s, "%ld packets, %ld KB sent, %ld errors\n",
		       s->packets, s->bytes >> 10, s->errors, 0);
	if (geometry || s->pass % cfg->fps == 0) {
		send_mode(s);
		if (s->fmt == RGBFB_CLUT && visible)
			send_palette(s);
		s->pal_serial = s->mc->pal_serial;
	}
	if (!visible)
		return;
	if (s->mc->pal_serial != s->pal_serial) {
		s->pal_serial = s->mc->pal_serial;
		if (s->fmt == RGBFB_CLUT)
			send_palette(s);
	}
	if (s->mc->cfg.blitter)
		take_damage(s);

	/* changed tiles, starting where the last pass stopped */
	for (checked = 0; checked < s->ntiles && budget > 0; checked++) {
		t = s->cursor;
		if (++s->cursor >= s->ntiles)
			s->cursor = 0;
		tile_rect(s, t, &x, &y, &w, &h);
		if (!tile_changed(s, x, y, w, h))
			continue;
		if (s->touched && s->mc->boardnum == 0 && s->touched[s->ntiles + t] != KIND_MODE) {
			/* not one that a new mode sent out */
			UBYTE kind = s->touched[s->ntiles + t];

			mc_status->tiles_changed++;
			if (s->touched[t]) {
				mc_status->tiles_explained++;
				if (kind < MC_BLIT_KINDS)
					mc_status->tiles_by[kind]++;
				else if (kind == KIND_SWITCH)
					mc_status->tiles_switch++;
			}
		}
		if (s->touched)
			s->touched[s->ntiles + t] = KIND_NONE;
		tile_capture(s, x, y, w, h);
		{
			ULONG n = tile_send(s, x, y, w, h, 0);

			if (!n) {
				/* not sent: leave it different, retry next pass */
				s->shadow[y * s->srow + x * s->bpp] ^= 0xff;
				s->cursor = t;
				budget = 0;
				break;
			}
			budget -= n;
			sent++;
		}
	}

	/* background refresh: the whole screen every cfg->refresh seconds */
	if (cfg->refresh) {
		refresh = (s->ntiles + (ULONG)cfg->refresh * cfg->fps - 1) /
		          ((ULONG)cfg->refresh * cfg->fps);
		while (refresh-- && budget > 0) {
			t = s->rcursor;
			if (++s->rcursor >= s->ntiles)
				s->rcursor = 0;
			tile_rect(s, t, &x, &y, &w, &h);
			if (tile_changed(s, x, y, w, h))
				continue;  /* goes out as a change anyway */
			budget -= tile_send(s, x, y, w, h, MC_FLAG_REFRESH);
			sent++;
		}
	}

	if (sent) {
		send_sync(s, sent);
		s->frame++;
	}
	if (s->touched)
		for (t = 0; t < s->ntiles; t++)
			if (s->touched[t])
				s->touched[t]--;
}

static BOOL open_socket(struct Sender *s)
{
	struct Library *SocketBase = s->sockbase;
	UBYTE ttl = s->mc->cfg.ttl;

	s->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s->sock < 0)
		return FALSE;
	/* only matters for multicast destinations; not every stack has it */
	setsockopt(s->sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	s->to.sin_len = sizeof(s->to);
	s->to.sin_family = AF_INET;
	s->to.sin_port = s->mc->cfg.port + s->mc->boardnum;
	s->to.sin_addr.s_addr = s->mc->cfg.dest;
	return TRUE;
}

/*
 * Open bsdsocket.library only when a stack has already put it in memory:
 * OpenLibrary() alone would load whatever bsdsocket.library is in LIBS:, and
 * starting a stack that way at boot would get in the way of the real one.
 */
static struct Library *open_running_stack(void)
{
	struct Library *lib = NULL;

	Forbid();
	if (FindName(&SysBase->LibList, "bsdsocket.library"))
		lib = OpenLibrary("bsdsocket.library", 3);
	Permit();
	return lib;
}

static void SenderMain(void)
{
	struct Task *me = FindTask(NULL);
	struct McCard *mc;
	struct Sender *s;
	struct DateStamp ds;
	ULONG ticks;

	/* the parent sets tc_UserData right after creating us */
	while (!(mc = me->tc_UserData))
		Delay(1);

	s = AllocMem(sizeof(*s), MEMF_PUBLIC | MEMF_CLEAR);
	if (!s)
		return;
	s->mc = mc;
	s->serial = mc->serial - 1;
	s->sock = -1;
	DateStamp(&ds);
	s->stream = (ds.ds_Days << 20) ^ (ds.ds_Minute << 12) ^ (ds.ds_Tick << 4) ^ (ULONG)mc;

	mc_config_env(&mc->cfg);
	mc_log(s, MC_CARDNAME " " MC_VERSION_STR " sender started, vram %ld KB at $%lx\n",
	       mc->cfg.vramsize >> 10, (LONG)mc->bi->MemoryBase, 0, 0);
	mc_log(s, "waiting for a TCP/IP stack\n", 0, 0, 0, 0);

	/* wait for a TCP/IP stack */
	while (!mc->sender_quit) {
		s->sockbase = open_running_stack();
		if (s->sockbase) {
			/* the network settings may have changed since boot */
			mc_config_env(&mc->cfg);
			if (open_socket(s)) {
				ULONG d = mc->cfg.dest;

				mc_log(s, "sending to %ld.%ld.%ld.%ld", d >> 24, (d >> 16) & 255,
				       (d >> 8) & 255, d & 255);
				mc_log(s, ":%ld, %ld fps, %ld KB/s\n", mc->cfg.port + mc->boardnum, mc->cfg.fps,
				       mc->cfg.rate >> 10, 0);
				break;
			}
			mc_log(s, "socket() failed\n", 0, 0, 0, 0);
			CloseLibrary(s->sockbase);
			s->sockbase = NULL;
		}
		if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
			break;
		Delay(2 * TICKS_PER_SECOND);
	}

	ticks = TICKS_PER_SECOND / mc->cfg.fps;
	if (!ticks)
		ticks = 1;
	while (s->sockbase && !mc->sender_quit) {
		if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
			break;
		update_pass(s);
		Delay(ticks);
	}

	if (s->sockbase) {
		struct Library *SocketBase = s->sockbase;

		CloseSocket(s->sock);
		CloseLibrary(SocketBase);
	}
	mc_log(s, "sender stopped\n", 0, 0, 0, 0);
	if (s->shadow)
		FreeMem(s->shadow, s->shadowsize);
	if (s->touched)
		FreeMem(s->touched, s->touchedsize);
	FreeMem(s, sizeof(*s));
	Forbid();
	mc->sender = NULL;
}

BOOL mc_sender_start(struct McCard *mc)
{
	/* "Multicast.card sender", then "Multicast.card sender 2", ... */
	static char names[MC_MAXBOARDS][24];
	char *name = names[mc->boardnum];
	struct Process *p;

	CopyMem("Multicast.card sender", name, 22);
	if (mc->boardnum) {
		name[21] = ' ';
		name[22] = '1' + mc->boardnum;
		name[23] = 0;
	}
	Forbid();
	p = CreateNewProcTags(NP_Entry, (ULONG)SenderMain,
	                      NP_Name, (ULONG)name,
	                      NP_Priority, mc->cfg.pri,
	                      NP_StackSize, 8192,
	                      TAG_END);
	if (p) {
		p->pr_Task.tc_UserData = mc;
		mc->sender = p;
	}
	Permit();
	return p != NULL;
}
