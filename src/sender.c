/*
 * P96Multicast - the sender process: the screen as a Copper stream
 * (include/copper.h, docs/COPPER.md).
 *
 * FPS times a second the sender
 *
 *  - takes the orders the drawing functions put on the ring (orders.c),
 *    draws each into its shadow, the copy of what the viewers have, and
 *    sends it: a fill, a copy, a line of text in a few bytes;
 *  - compares the screen with the shadow where it has reason to: what the
 *    ring says was drawn some other way, tiles that changed lately, where
 *    the pointer is, and a slice of the rest (the background scan, which
 *    finds whatever nobody called a function for, as fast as SCAN= and
 *    CPU= allow); tiles that differ go out as pixels;
 *  - sends what viewers asked for: datagrams they missed, again from the
 *    history of the last HISTORY= KB, or tiles from the shadow.
 *
 * Datagrams are filled with records and numbered. Viewers answer to the
 * address they come from, so this works through NAT too.
 *
 * bsdsocket.library bases belong to the task that opened them, so the
 * sender opens its own, waiting until a TCP/IP stack is running.
 */
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/bsdsocket.h>
#include <sys/ioctl.h>
#include "mcard.h"
#include "copper.h"
#include "render.h"

#define TILE_ROWBYTES 64             /* tile width in bytes (before clipping) */
#define TILE_H        16
#define MAX_BODY      (CU_MAX_DGRAM - CU_HDR_SIZE - 3)
#define MAX_VIEWERS   8
#define TILE_BYTES    (TILE_ROWBYTES * TILE_H)

struct Device *TimerBase;

struct Viewer {
	ULONG addr;
	UWORD port;
	ULONG last;                      /* pass last heard from */
};

struct Sender {
	struct McCard *mc;
	UWORD  view;                     /* which part of a SPLIT= screen, 0 for the first */
	WORD   vx, vy;                   /* its top left on the screen */
	ULONG  lost_seen;                /* mc->ring_lost, last handled */
	struct McConfig *cfg;
	struct Library *sockbase;
	struct IntuitionBase *ibase;
	LONG   sock;
	struct sockaddr_in to;
	ULONG  stream;
	ULONG  frame;
	ULONG  pass;

	/* the screen, from the last snapshot */
	ULONG  serial;
	UBYTE *bm;
	WORD   xoff, yoff;
	UBYTE *src;                      /* top left visible pixel */
	ULONG  bpr;                      /* source bytes per row */
	UWORD  w, h, bpp;
	RGBFTYPE fmt;
	UWORD  display;
	BOOL   visible;
	ULONG  pal_serial;
	ULONG  ptr_serial, pos_serial;
	WORD   ptr_vx, ptr_vy;           /* the pointer where the viewers have it */
	WORD   mouse_x, mouse_y;         /* Intuition's, last pass */
	UWORD  mouse_hot;                /* passes to keep comparing around it */

	/* tiles */
	UWORD  tw, th, tcols, trows;
	ULONG  ntiles;
	UBYTE *heat;                     /* passes to keep comparing it */
	UBYTE *soon;                     /* ... starting with the next pass */
	UBYTE *want;                     /* viewers asked for it */
	BOOL   any_heat, any_soon;       /* is anything in heat, soon at all */
	ULONG  tilemem;
	ULONG  sweep;                    /* next tile of the background scan */
	ULONG  sweep_n;                  /* tiles it compares per pass */
	ULONG  wantcur;                  /* next wanted tile */
	ULONG  nwant;

	struct CuSurf sh;                /* the shadow */
	ULONG  shadowsize;

	/* output: the history of datagrams, the one being filled is seq_next's */
	UBYTE *hist;
	UWORD *hlen;
	ULONG  nhist;
	ULONG  seq_next, seq_sent;
	UBYTE *cur;
	ULONG  curlen;
	LONG   budget;
	ULONG  last_emit;                /* pass of the last record */
	BOOL   emitted;                  /* this pass */

	/* SOLID tiles side by side become one record */
	UWORD  sol_x, sol_y, sol_w, sol_h;
	UBYTE  sol_px[4];
	BOOL   flushing;

	struct Viewer viewers[MAX_VIEWERS];
	ULONG  last_heard;
	ULONG  next_refresh;

	/* timing the passes, for the background scan */
	struct timerequest tr;
	BOOL   timer;
	ULONG  efreq;

	/* statistics, for the log */
	ULONG  packets, bytes, errors, pixbytes;
	LONG   lasterror;
	BOOL   logstarted;

	UBYTE  tmp[TILE_BYTES + TILE_BYTES / 2 + 64];
	UBYTE  ctrl[CU_CTRL_MAX];
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
	if (s->mc->boardnum || s->view) {
		path[i++] = '.';
		path[i++] = '1' + s->mc->boardnum + s->view;
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

static ULONG get32(const UBYTE *p)
{
	return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static UWORD get16(const UBYTE *p)
{
	return (p[0] << 8) | p[1];
}

static BOOL first_board(struct Sender *s)
{
	return s->mc->boardnum == 0 && s->view == 0;
}

/* ---------------------------------------------------------------- datagrams */

/* datagrams made but not sent yet */
static ULONG backlog(struct Sender *s)
{
	return s->seq_next - s->seq_sent;
}

/* room for orders: the history must not overwrite what is not sent yet */
static BOOL room_orders(struct Sender *s)
{
	return backlog(s) < s->nhist / 2;
}

/* room for pixels: no more than the next two passes can send */
static BOOL room_pixels(struct Sender *s)
{
	return room_orders(s) &&
	       backlog(s) * CU_MAX_DGRAM < 2 * (s->cfg->rate / s->cfg->fps) + CU_MAX_DGRAM;
}

static void finish(struct Sender *s)
{
	if (!s->cur)
		return;
	s->hlen[s->seq_next % s->nhist] = s->curlen;
	s->seq_next++;
	s->cur = NULL;
}

static void flush_solid(struct Sender *s);
static void set_tail(struct Sender *s, ULONG tail);

/* what a sender process is started with: its board, and its part of the screen */
struct Start {
	struct McCard *mc;
	UWORD view;
};

/* a record of len bytes: returns where its body goes */
static UBYTE *rec(struct Sender *s, UBYTE type, ULONG len)
{
	UBYTE *p;

	if (s->sol_w && !s->flushing)
		flush_solid(s);
	if (s->cur && s->curlen + 3 + len > CU_MAX_DGRAM)
		finish(s);
	if (!s->cur) {
		s->cur = s->hist + (s->seq_next % s->nhist) * CU_MAX_DGRAM;
		put16(s->cur, CU_MAGIC);
		s->cur[2] = CU_VERSION;
		s->cur[3] = 0;
		put32(s->cur + 4, s->stream);
		put32(s->cur + 8, s->seq_next);
		s->curlen = CU_HDR_SIZE;
	}
	p = s->cur + s->curlen;
	p[0] = type;
	put16(p + 1, len);
	s->curlen += 3 + len;
	s->last_emit = s->pass;
	s->emitted = TRUE;
	return p + 3;
}

/* x, y, w, h: the start of most bodies */
static UBYTE *rect(UBYTE *p, LONG x, LONG y, LONG w, LONG h)
{
	put16(p, x);
	put16(p + 2, y);
	put16(p + 4, w);
	put16(p + 6, h);
	return p + 8;
}

static BOOL send_dgram(struct Sender *s, UBYTE *d, ULONG len)
{
	struct Library *SocketBase = s->sockbase;

	if (sendto(s->sock, d, len, 0, (struct sockaddr *)&s->to, sizeof(s->to)) == (LONG)len) {
		s->packets++;
		s->bytes += len;
		s->budget -= len;
		if (first_board(s)) {
			mc_status->datagrams++;
			mc_status->sent_kb = s->bytes >> 10;
		}
		return TRUE;
	}
	if (!s->errors++ || Errno() != s->lasterror)
		mc_log(s, "sendto failed: errno %ld (%ld errors so far)\n", Errno(), s->errors, 0, 0);
	s->lasterror = Errno();
	if (first_board(s))
		mc_status->send_errors++;
	return FALSE;
}

/* what is made, as far as the budget goes; the rest in the next pass */
static void transmit(struct Sender *s)
{
	while (s->seq_sent != s->seq_next && s->budget > 0) {
		ULONG slot = s->seq_sent % s->nhist;

		if (!send_dgram(s, s->hist + slot * CU_MAX_DGRAM, s->hlen[slot]))
			break;
		s->seq_sent++;
	}
}

/* ---------------------------------------------------------------- records */

static void send_surface(struct Sender *s)
{
	UBYTE *p = rec(s, CU_SURFACE, 12);

	put16(p, s->w);
	put16(p + 2, s->h);
	p[4] = s->fmt;
	p[5] = s->bpp;
	p[6] = s->visible;
	p[7] = 0;
	put16(p + 8, s->tw);
	put16(p + 10, s->th);
}

static void send_palette(struct Sender *s)
{
	UBYTE *p;

	s->pal_serial = s->mc->pal_serial;
	if (s->fmt != RGBFB_CLUT)
		return;
	p = rec(s, CU_PALETTE, 4 + 256 * 3);
	put16(p, 0);
	put16(p + 2, 256);
	CopyMem(s->mc->pal, p + 4, 256 * 3);
}

static void send_pointer(struct Sender *s)
{
	struct McCard *mc = s->mc;
	UWORD n, i;
	UBYTE *p;

	Forbid();
	s->ptr_serial = mc->ptr_serial;
	n = mc->ptr_h * 2 * ((mc->ptr_w + 15) / 16);
	p = rec(s, CU_POINTER, 13 + 2 * n);
	p[0] = mc->ptr_w;
	p[1] = mc->ptr_h;
	p[2] = mc->ptr_hx;
	p[3] = mc->ptr_hy;
	CopyMem(mc->ptr_rgb, p + 4, 9);
	for (i = 0; i < n; i++)
		put16(p + 13 + 2 * i, mc->ptr_data[i]);
	Permit();
}

static void send_pointer_pos(struct Sender *s)
{
	struct McCard *mc = s->mc;
	UBYTE *p = rec(s, CU_POINTERPOS, 6);

	s->pos_serial = mc->pos_serial;
	s->ptr_vx = mc->ptr_x - s->xoff - s->vx;
	s->ptr_vy = mc->ptr_y - s->yoff - s->vy;
	put16(p, s->ptr_vx);
	put16(p + 2, s->ptr_vy);
	p[4] = mc->ptr_on;
	p[5] = 0;
}

static void flush_solid(struct Sender *s)
{
	UBYTE *p;

	s->flushing = TRUE;
	p = rect(rec(s, CU_PIXELS, 10 + s->bpp), s->sol_x, s->sol_y, s->sol_w, s->sol_h);
	p[0] = CU_SOLID;
	p[1] = 0;
	CopyMem(s->sol_px, p + 2, s->bpp);
	s->flushing = FALSE;
	s->sol_w = 0;
}

static void tile_rect(struct Sender *s, ULONG t, UWORD *x, UWORD *y, UWORD *w, UWORD *h)
{
	*x = (t % s->tcols) * s->tw;
	*y = (t / s->tcols) * s->th;
	*w = *x + s->tw > s->w ? s->w - *x : s->tw;
	*h = *y + s->th > s->h ? s->h - *y : s->th;
}

/* a tile from the shadow, as pixels */
static void send_tile(struct Sender *s, ULONG t)
{
	UWORD x, y, w, h;
	UBYTE enc, *p;
	ULONG n;

	tile_rect(s, t, &x, &y, &w, &h);
	n = cu_encode(s->sh.mem + y * s->sh.bpr + x * s->bpp, s->sh.bpr, w, h, s->bpp,
	              s->tmp, sizeof(s->tmp), &enc);
	if (first_board(s))
		mc_status->pixel_tiles++;
	if (enc == CU_SOLID) {
		/* next to the last solid one, in the same colour: wider */
		if (s->sol_w && s->sol_y == y && s->sol_h == h && s->sol_x + s->sol_w == x &&
		    !cu_differ(s->sol_px, s->tmp, s->bpp)) {
			s->sol_w += w;
			return;
		}
		if (s->sol_w)
			flush_solid(s);
		s->sol_x = x;
		s->sol_y = y;
		s->sol_w = w;
		s->sol_h = h;
		CopyMem(s->tmp, s->sol_px, s->bpp);
		return;
	}
	p = rect(rec(s, CU_PIXELS, 10 + n), x, y, w, h);
	p[0] = enc;
	p[1] = 0;
	CopyMem(s->tmp, p + 2, n);
	s->pixbytes += n;
	if (first_board(s))
		mc_status->pixel_kb = s->pixbytes >> 10;
}

/* ---------------------------------------------------------------- tiles */

static void mark_tiles(struct Sender *s, UBYTE *map, LONG x, LONG y, LONG w, LONG h, UBYTE v)
{
	LONG tx0, tx1, ty0, ty1, tx, ty;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > s->w)
		w = s->w - x;
	if (y + h > s->h)
		h = s->h - y;
	if (w <= 0 || h <= 0)
		return;
	tx0 = x / s->tw;
	tx1 = (x + w - 1) / s->tw;
	ty0 = y / s->th;
	ty1 = (y + h - 1) / s->th;
	if (map == s->heat)
		s->any_heat = TRUE;
	else if (map == s->soon)
		s->any_soon = TRUE;
	for (ty = ty0; ty <= ty1; ty++) {
		UBYTE *m = map + ty * s->tcols;

		for (tx = tx0; tx <= tx1; tx++)
			if (m[tx] < v)
				m[tx] = v;
	}
}

/* the outer tiles only: where frames and borders are drawn around a big order */
static void mark_edges(struct Sender *s, LONG x, LONG y, LONG w, LONG h, UBYTE v)
{
	if ((w / s->tw) * (h / s->th) <= 16) {
		mark_tiles(s, s->soon, x, y, w, h, v);
		return;
	}
	mark_tiles(s, s->soon, x, y, w, 1, v);
	mark_tiles(s, s->soon, x, y + h - 1, w, 1, v);
	mark_tiles(s, s->soon, x, y, 1, h, v);
	mark_tiles(s, s->soon, x + w - 1, y, 1, h, v);
}

/* is anything in the rectangle watched: different, or maybe soon? */
static BOOL any_watched(struct Sender *s, LONG x, LONG y, LONG w, LONG h)
{
	LONG tx, ty, tx1 = (x + w - 1) / s->tw, ty1 = (y + h - 1) / s->th;

	for (ty = y / s->th; ty <= ty1; ty++)
		for (tx = x / s->tw; tx <= tx1; tx++) {
			ULONG t = ty * s->tcols + tx;

			if (s->heat[t] || s->soon[t])
				return TRUE;
		}
	return FALSE;
}

static void want_all(struct Sender *s)
{
	ULONG t;

	for (t = 0; t < s->ntiles; t++)
		s->want[t] = 1;
	s->nwant = s->ntiles;
	s->wantcur = 0;
}

/*
 * Compare tile t with the shadow, and send it if it differs. -1: it
 * differs, but there is no room to send it now.
 */
static int compare_tile(struct Sender *s, ULONG t)
{
	UWORD x, y, w, h, r;
	ULONG len;
	UBYTE *from, *to;

	tile_rect(s, t, &x, &y, &w, &h);
	len = (ULONG)w * s->bpp;
	from = s->src + y * s->bpr + x * s->bpp;
	to = s->sh.mem + y * s->sh.bpr + x * s->bpp;
	for (r = 0; r < h; r++)
		if (cu_differ(from + r * s->bpr, to + r * s->sh.bpr, len))
			break;
	if (r == h)
		return 0;
	if (!room_pixels(s))
		return -1;
	for (; r < h; r++)
		CopyMem(from + r * s->bpr, to + r * s->sh.bpr, len);
	if (s->want[t]) {
		s->want[t] = 0;
		s->nwant--;
	}
	send_tile(s, t);
	return 1;
}

/* ---------------------------------------------------------------- the screen */

static void free_tiles(struct Sender *s)
{
	if (s->sh.mem)
		FreeMem(s->sh.mem, s->shadowsize);
	if (s->heat)
		FreeMem(s->heat, s->tilemem);
	s->sh.mem = NULL;
	s->heat = NULL;
}

/*
 * Look at the display state. A new mode or format gets a new shadow, which
 * goes out whole; another bitmap or another scroll position keeps it, and
 * only what differs goes out. FALSE when there is nothing to show.
 */
static BOOL snapshot(struct Sender *s)
{
	struct McCard *mc = s->mc;
	UBYTE *bm;
	UWORD bmw, w, h, display;
	WORD xoff, yoff;
	RGBFTYPE fmt;
	ULONG serial, bpp, t;
	BOOL newmode, moved;

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

	if (serial == s->serial)
		return s->visible;
	s->serial = serial;
	bpp = mc_bytesperpixel(fmt);
	if (xoff < 0)
		xoff = 0;
	if (yoff < 0)
		yoff = 0;
	if (!bm || !bpp || !w || !h || !display) {
		if (s->visible || !s->w) {
			s->visible = FALSE;
			send_surface(s);
		}
		return FALSE;
	}
	if (bmw < w)
		bmw = w;
	/* SPLIT=: this sender's part of the screen */
	{
		UWORD cols = mc->cfg.split_cols ? mc->cfg.split_cols : 1;
		UWORD rows = mc->cfg.split_rows ? mc->cfg.split_rows : 1;

		w /= cols;
		h /= rows;
		s->vx = (s->view % cols) * w;
		s->vy = (s->view / cols) * h;
	}
	newmode = w != s->w || h != s->h || fmt != s->fmt || !s->sh.mem;
	moved = bm != s->bm || xoff != s->xoff || yoff != s->yoff;
	s->bm = bm;
	s->xoff = xoff;
	s->yoff = yoff;
	s->bpr = mc_bytesperrow(bmw, fmt);
	s->src = bm + (yoff + s->vy) * s->bpr + (xoff + s->vx) * bpp;

	if (newmode) {
		free_tiles(s);
		s->w = w;
		s->h = h;
		s->fmt = fmt;
		s->bpp = bpp;
		s->tw = TILE_ROWBYTES / bpp;
		s->th = TILE_H;
		s->tcols = (w + s->tw - 1) / s->tw;
		s->trows = (h + s->th - 1) / s->th;
		s->ntiles = (ULONG)s->tcols * s->trows;
		s->sh.bpr = (ULONG)w * bpp;
		s->sh.w = w;
		s->sh.h = h;
		s->sh.bpp = bpp;
		s->shadowsize = s->sh.bpr * h;
		s->tilemem = 3 * s->ntiles;
		s->sh.mem = AllocMem(s->shadowsize, MEMF_ANY);
		s->heat = AllocMem(s->tilemem, MEMF_ANY | MEMF_CLEAR);
		if (!s->sh.mem || !s->heat) {
			mc_log(s, "no memory for a %ld x %ld shadow\n", w, h, 0, 0);
			free_tiles(s);
			s->w = 0;
			s->visible = FALSE;
			return FALSE;
		}
		s->soon = s->heat + s->ntiles;
		s->want = s->soon + s->ntiles;
		s->sweep = 0;
		s->sweep_n = 1;
		/* the shadow is the screen, and the viewers get all of it */
		for (t = 0; t < h; t++)
			CopyMem(s->src + t * s->bpr, s->sh.mem + t * s->sh.bpr, s->sh.bpr);
		want_all(s);
		mc_log(s, "screen %ld x %ld, format %ld\n", w, h, fmt, 0);
	} else if (moved || !s->visible) {
		/* compare all of it with what the viewers have */
		for (t = 0; t < s->ntiles; t++)
			s->heat[t] = 1;
		s->any_heat = TRUE;
	}
	if (newmode || moved) {
		/* what is on the ring was drawn for the old screen */
		set_tail(s, mc->ring_head);
	}
	if (newmode || !s->visible) {
		s->visible = TRUE;
		send_surface(s);
		send_palette(s);
		send_pointer(s);
		send_pointer_pos(s);
	}
	return TRUE;
}

/* ---------------------------------------------------------------- orders */

/*
 * This sender has taken the ring up to tail. The writers may overwrite only
 * what every sender has taken: the ring's tail is the one furthest behind.
 */
static void set_tail(struct Sender *s, ULONG tail)
{
	struct McCard *mc = s->mc;
	ULONG oldest = tail, head;
	UWORD i;

	Forbid();
	head = mc->ring_head;
	mc->view_tail[s->view] = tail;
	for (i = 0; i < mc->nviews; i++)
		if (head - mc->view_tail[i] > head - oldest)
			oldest = mc->view_tail[i];
	mc->ring_tail = oldest;
	if (head - oldest < MC_RING_SIZE / 2)
		mc->ring_full = 0;
	Permit();
}

/* clip to the screen; *cx, *cy: how much was cut off on the left and the top */
static BOOL clip(struct Sender *s, LONG *x, LONG *y, LONG *w, LONG *h, LONG *cx, LONG *cy)
{
	*cx = *x < 0 ? -*x : 0;
	*cy = *y < 0 ? -*y : 0;
	*x += *cx;
	*w -= *cx;
	*y += *cy;
	*h -= *cy;
	if (*x + *w > s->w)
		*w = s->w - *x;
	if (*y + *h > s->h)
		*h = s->h - *y;
	return *w > 0 && *h > 0;
}

static void count_order(struct Sender *s, UBYTE type)
{
	if (first_board(s))
		mc_status->orders[type]++;
}

static void order_text(struct Sender *s, struct McText *m, LONG x, LONG y, LONG w, LONG h,
                       LONG cx, LONG cy)
{
	UBYTE fg[4], bg[4], *bits = (UBYTE *)(m + 1) + cy * m->stride, *p;
	ULONG bitx = m->bitx + cx, first = bitx >> 3, stride, rows, r, head;
	UBYTE mode = m->r.mode;

	bitx &= 7;
	bits += first;
	stride = (bitx + w + 7) >> 3;
	head = 14 + 2 * s->bpp;
	if (head + stride > MAX_BODY) {
		mark_tiles(s, s->soon, x, y, w, h, 3);
		return;
	}
	cu_pen(m->fg, s->bpp, fg);
	cu_pen(m->bg, s->bpp, bg);
	cu_text(&s->sh, x, y, w, h, mode, bits, bitx, m->stride, fg, bg);
	/* as many rows as fit in a datagram, at a time */
	while (h > 0) {
		rows = (MAX_BODY - head) / stride;
		if (rows > h)
			rows = h;
		p = rect(rec(s, CU_TEXT, head + rows * stride), x, y, w, rows);
		p[0] = mode;
		p[1] = 0;
		put16(p + 2, bitx);
		put16(p + 4, stride);
		CopyMem(fg, p + 6, s->bpp);
		CopyMem(bg, p + 6 + s->bpp, s->bpp);
		p += 6 + 2 * s->bpp;
		for (r = 0; r < rows; r++, p += stride, bits += m->stride)
			CopyMem(bits, p, stride);
		y += rows;
		h -= rows;
	}
	count_order(s, MC_R_TEXT);
}

static void order(struct Sender *s, struct McRec *r)
{
	LONG x = r->x - s->vx, y = r->y - s->vy, w = r->w, h = r->h, cx, cy;
	UBYTE px[4], *p;

	if (r->type == MC_R_PAD)
		return;
	if (r->type == MC_R_COPY) {
		struct McCopy *c = (struct McCopy *)r;
		LONG sx = c->sx - s->vx, sy = c->sy - s->vy;

		if (!clip(s, &x, &y, &w, &h, &cx, &cy))
			return;
		sx += cx;
		sy += cy;
		if (sx < 0 || sy < 0 || sx + w > s->w || sy + h > s->h) {
			/* from outside the screen: the viewers don't have that */
			mark_tiles(s, s->soon, x, y, w, h, 3);
			return;
		}
		cu_copy(&s->sh, sx, sy, x, y, w, h);
		p = rec(s, CU_COPY, 12);
		put16(p, sx);
		put16(p + 2, sy);
		rect(p + 4, x, y, w, h);
		/* corrections still due at the source move along */
		if (any_watched(s, sx, sy, w, h))
			mark_tiles(s, s->soon, x, y, w, h, 2);
		else
			mark_edges(s, x, y, w, h, 2);
		count_order(s, MC_R_COPY);
		return;
	}
	if (!clip(s, &x, &y, &w, &h, &cx, &cy))
		return;

	switch (r->type) {
	case MC_R_FILL:
		cu_pen(((struct McFill *)r)->pen, s->bpp, px);
		cu_fill(&s->sh, x, y, w, h, px);
		p = rect(rec(s, CU_FILL, 8 + s->bpp), x, y, w, h);
		CopyMem(px, p, s->bpp);
		mark_edges(s, x, y, w, h, 2);
		count_order(s, MC_R_FILL);
		break;

	case MC_R_INVERT:
		cu_invert(&s->sh, x, y, w, h);
		rect(rec(s, CU_INVERT, 8), x, y, w, h);
		mark_edges(s, x, y, w, h, 2);
		count_order(s, MC_R_INVERT);
		break;

	case MC_R_TEXT:
		order_text(s, (struct McText *)r, x, y, w, h, cx, cy);
		mark_edges(s, x, y, w, h, 2);
		break;

	case MC_R_PATTERN: {
		struct McPattern *m = (struct McPattern *)r;
		UBYTE fg[4], bg[4], *rows = (UBYTE *)(m + 1);
		UBYTE ppx = (m->px + cx) & 15, ppy = (m->py + cy) & m->rowmask;
		ULONG n = 2 * (m->rowmask + 1);

		cu_pen(m->fg, s->bpp, fg);
		cu_pen(m->bg, s->bpp, bg);
		cu_pattern(&s->sh, x, y, w, h, m->r.mode, rows, m->rowmask, ppx, ppy, fg, bg);
		p = rect(rec(s, CU_PATTERN, 12 + 2 * s->bpp + n), x, y, w, h);
		p[0] = m->r.mode;
		p[1] = m->rowmask;
		p[2] = ppx;
		p[3] = ppy;
		CopyMem(fg, p + 4, s->bpp);
		CopyMem(bg, p + 4 + s->bpp, s->bpp);
		CopyMem(rows, p + 4 + 2 * s->bpp, n);
		mark_edges(s, x, y, w, h, 2);
		count_order(s, MC_R_PATTERN);
		break;
	}

	default:
		/* drawn some other way: compare, from the next pass on */
		mark_tiles(s, s->soon, x, y, w, h, 3);
		break;
	}
}

static void take_orders(struct Sender *s)
{
	struct McCard *mc = s->mc;
	ULONG head, tail;
	ULONG t;

	if (!mc->ring)
		return;
	if (mc->ring_lost != s->lost_seen) {
		/* drawing went faster than the senders: compare everything */
		s->lost_seen = mc->ring_lost;
		set_tail(s, mc->ring_head);
		for (t = 0; t < s->ntiles; t++)
			if (!s->heat[t])
				s->heat[t] = 1;
		s->any_heat = TRUE;
		return;
	}
	head = mc->ring_head;
	tail = mc->view_tail[s->view];
	while (tail != head && room_orders(s)) {
		struct McRec *r = (struct McRec *)(mc->ring + (tail & (MC_RING_SIZE - 1)));

		order(s, r);
		tail += r->len;
	}
	set_tail(s, tail);
}

/* ---------------------------------------------------------------- viewers */

static void hear(struct Sender *s, struct sockaddr_in *from)
{
	struct Viewer *v, *oldest = &s->viewers[0];
	UWORD i, n = 0;

	s->last_heard = s->pass;
	for (i = 0; i < MAX_VIEWERS; i++) {
		v = &s->viewers[i];
		if (v->addr == from->sin_addr.s_addr && v->port == from->sin_port) {
			v->last = s->pass;
			goto count;
		}
		if (v->last < oldest->last)
			oldest = v;
	}
	oldest->addr = from->sin_addr.s_addr;
	oldest->port = from->sin_port;
	oldest->last = s->pass;
	mc_log(s, "a viewer at %ld.%ld.%ld.%ld\n", from->sin_addr.s_addr >> 24,
	       (from->sin_addr.s_addr >> 16) & 255, (from->sin_addr.s_addr >> 8) & 255,
	       from->sin_addr.s_addr & 255);
count:
	for (i = 0; i < MAX_VIEWERS; i++)
		if (s->viewers[i].last && s->pass - s->viewers[i].last < 10UL * s->cfg->fps)
			n++;
	if (first_board(s))
		mc_status->viewers = n;
}

/* a datagram again, if the history still has it */
static void repair(struct Sender *s, ULONG seq)
{
	ULONG slot = seq % s->nhist;
	UBYTE *d = s->hist + slot * CU_MAX_DGRAM;

	if ((LONG)(s->seq_sent - seq) <= 0 || s->seq_next - seq >= s->nhist)
		return;
	if (get32(d + 8) != seq)
		return;
	d[3] |= CU_F_REPAIR;
	if (send_dgram(s, d, s->hlen[slot]) && first_board(s))
		mc_status->repairs++;
}

static void message(struct Sender *s, UBYTE *m, LONG n, struct sockaddr_in *from)
{
	ULONG stream;
	LONG i;

	if (n < 8 || get16(m) != CU_MAGIC || m[2] != CU_VERSION)
		return;
	hear(s, from);
	if (first_board(s))
		mc_status->heard++;
	stream = get32(m + 4);
	if (stream != s->stream || !s->visible)
		return;
	switch (m[3]) {
	case CU_NAK: {
		/* repairs go first: they may overdraw the budget by a pass */
		LONG floor = -(LONG)(s->cfg->rate / s->cfg->fps);

		for (i = 8; i + 6 <= n && s->budget > floor; i += 6) {
			ULONG first = get32(m + i);
			UWORD count = get16(m + i + 4), k;

			for (k = 0; k < count && k < 64 && s->budget > floor; k++)
				repair(s, first + k);
		}
		break;
	}
	case CU_WANT:
		for (i = 8; i + 8 <= n; i += 8) {
			UWORD x = get16(m + i), y = get16(m + i + 2);
			UWORD w = get16(m + i + 4), h = get16(m + i + 6);

			if (!w || !h) {
				/* all of it, from the start */
				want_all(s);
				send_surface(s);
				send_palette(s);
				send_pointer(s);
				send_pointer_pos(s);
				break;
			}
			mark_tiles(s, s->want, x, y, w, h, 1);
		}
		/* count them again */
		s->nwant = 0;
		for (i = 0; i < s->ntiles; i++)
			if (s->want[i])
				s->nwant++;
		break;
	}
}

static void read_viewers(struct Sender *s)
{
	struct Library *SocketBase = s->sockbase;
	struct sockaddr_in from;
	socklen_t fromlen;
	LONG n;
	int i;

	for (i = 0; i < 32; i++) {
		fromlen = sizeof(from);
		n = recvfrom(s->sock, s->ctrl, sizeof(s->ctrl), 0, (struct sockaddr *)&from, &fromlen);
		if (n <= 0)
			break;
		message(s, s->ctrl, n, &from);
	}
}

/* ---------------------------------------------------------------- a pass */

/* where Intuition has the mouse, on the screen at the front */
static BOOL mouse(struct Sender *s, WORD *x, WORD *y)
{
	struct Screen *scr;

	if (!s->ibase)
		return FALSE;
	scr = s->ibase->FirstScreen;
	if (!scr)
		return FALSE;
	*x = scr->MouseX - s->xoff - s->vx;
	*y = scr->MouseY - s->yoff - s->vy;
	return TRUE;
}

static void compare(struct Sender *s)
{
	ULONG t, n;
	WORD mx, my;

	/* what the orders said to look at from now on */
	if (s->any_soon) {
		for (t = 0; t < s->ntiles; t++)
			if (s->soon[t]) {
				if (s->heat[t] < s->soon[t])
					s->heat[t] = s->soon[t];
				s->soon[t] = 0;
			}
		s->any_soon = FALSE;
		s->any_heat = TRUE;
	}

	/* around the pointer, while it moves (and a moment after) */
	if (mouse(s, &mx, &my)) {
		if (mx != s->mouse_x || my != s->mouse_y) {
			s->mouse_hot = s->cfg->fps / 2 + 1;
			mark_tiles(s, s->heat, s->mouse_x - 24, s->mouse_y - 24, 72, 72, 2);
			s->mouse_x = mx;
			s->mouse_y = my;
		}
		if (s->mouse_hot) {
			s->mouse_hot--;
			mark_tiles(s, s->heat, mx - 24, my - 24, 72, 72, 1);
		}
	}

	/* the watched tiles: changed lately, or told to */
	if (s->any_heat) {
		BOOL left = FALSE;

		for (t = 0; t < s->ntiles; t++) {
			if (!s->heat[t])
				continue;
			switch (compare_tile(s, t)) {
			case 1:
				/* it changes: watch it for a while */
				s->heat[t] = s->cfg->fps / 2 + 1;
				break;
			case 0:
				s->heat[t]--;
				break;
			default:
				return;   /* no room: the rest stay watched */
			}
			if (s->heat[t])
				left = TRUE;
		}
		s->any_heat = left;
	}

	/* the background scan */
	for (n = 0; n < s->sweep_n; n++) {
		t = s->sweep;
		switch (compare_tile(s, t)) {
		case 1:
			s->heat[t] = s->cfg->fps / 2 + 1;
			s->any_heat = TRUE;
			break;
		case -1:
			return;
		}
		if (++s->sweep >= s->ntiles)
			s->sweep = 0;
	}
}

/* what viewers asked for, from the shadow */
static void send_wanted(struct Sender *s)
{
	ULONG n;

	for (n = 0; s->nwant && n < s->ntiles && room_pixels(s); n++) {
		ULONG t = s->wantcur;

		if (++s->wantcur >= s->ntiles)
			s->wantcur = 0;
		if (!s->want[t])
			continue;
		s->want[t] = 0;
		s->nwant--;
		send_tile(s, t);
		if (first_board(s))
			mc_status->wanted++;
	}
}

/* the background scan's share of the CPU: SCAN= at the most, CPU= percent */
static void pace(struct Sender *s, struct EClockVal *start)
{
	struct EClockVal now;
	ULONG took, allowed, most;

	if (!s->timer || !s->ntiles)
		return;
	ReadEClock(&now);
	took = now.ev_lo - start->ev_lo;
	allowed = s->efreq / 100 * s->cfg->cpu / s->cfg->fps;
	most = (s->ntiles + (ULONG)s->cfg->scan * s->cfg->fps - 1) / ((ULONG)s->cfg->scan * s->cfg->fps);
	if (took > allowed)
		s->sweep_n -= s->sweep_n / 4;
	else
		s->sweep_n += s->sweep_n / 8 + 1;
	if (s->sweep_n > most)
		s->sweep_n = most;
	if (s->sweep_n < 1)
		s->sweep_n = 1;
	if (first_board(s))
		mc_status->sweep_secs = s->ntiles / (s->sweep_n * s->cfg->fps) + 1;
}

static void pass(struct Sender *s)
{
	struct McCard *mc = s->mc;
	struct McConfig *cfg = s->cfg;
	struct EClockVal start;
	LONG perpass = cfg->rate / cfg->fps;

	if (s->timer)
		ReadEClock(&start);
	s->pass++;
	s->emitted = FALSE;
	s->budget += perpass;
	if (s->budget > 2 * perpass)
		s->budget = 2 * perpass;

	if (snapshot(s)) {
		read_viewers(s);
		if (mc->pal_serial != s->pal_serial)
			send_palette(s);
		if (mc->ptr_serial != s->ptr_serial)
			send_pointer(s);
		if (mc->pos_serial != s->pos_serial ||
		    (mc->ptr_on && (mc->ptr_x - s->xoff - s->vx != s->ptr_vx ||
		                     mc->ptr_y - s->yoff - s->vy != s->ptr_vy)))
			send_pointer_pos(s);
		take_orders(s);
		compare(s);
		send_wanted(s);

		/* nobody talks back: the whole screen now and then, for listeners */
		if (cfg->refresh && s->pass - s->last_heard > 10UL * cfg->fps &&
		    (LONG)(s->pass - s->next_refresh) >= 0) {
			want_all(s);
			s->next_refresh = s->pass + (ULONG)cfg->refresh * cfg->fps;
		}
	} else {
		read_viewers(s);
	}
	/* a sign of life: viewers learn where to answer, and what they missed */
	if (s->pass - s->last_emit >= cfg->fps / 4 + 1)
		send_surface(s);
	if (s->emitted) {
		UBYTE *p = rec(s, CU_FRAME, 4);

		put32(p, ++s->frame);
	}
	finish(s);
	transmit(s);
	if (s->visible)
		pace(s, &start);

	if (s->pass % (10 * cfg->fps) == 0)
		mc_log(s, "%ld datagrams, %ld KB sent, %ld errors, background scan %ld s\n",
		       s->packets, s->bytes >> 10, s->errors,
		       s->ntiles && s->sweep_n ? s->ntiles / (s->sweep_n * cfg->fps) + 1 : 0);
}

/* ---------------------------------------------------------------- the process */

/*
 * Where this part of the screen goes: DEST2= ... if set; else a multicast
 * group of its own, DEST's plus the part's number (239.255.96.1, .2, ...),
 * so a device that joins one gets only that part; or DEST itself, unicast.
 */
static ULONG part_dest(struct Sender *s)
{
	struct McConfig *cfg = &s->mc->cfg;

	if (s->view && cfg->part_dest[s->view])
		return cfg->part_dest[s->view];
	if ((cfg->dest >> 28) == 0xe)
		return cfg->dest + s->view;
	return cfg->dest;
}

static BOOL open_socket(struct Sender *s)
{
	struct Library *SocketBase = s->sockbase;
	UBYTE ttl = s->mc->cfg.ttl;
	LONG one = 1;

	s->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s->sock < 0)
		return FALSE;
	/* only matters for multicast destinations; not every stack has it */
	setsockopt(s->sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	/* viewers' messages are read when there are some, never waited for */
	IoctlSocket(s->sock, FIONBIO, (APTR)&one);
	s->to.sin_len = sizeof(s->to);
	s->to.sin_family = AF_INET;
	s->to.sin_port = s->mc->cfg.port + s->mc->boardnum + s->view;
	s->to.sin_addr.s_addr = part_dest(s);
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

static BOOL alloc_history(struct Sender *s)
{
	s->nhist = s->cfg->history / CU_MAX_DGRAM;
	if (s->nhist < 32)
		s->nhist = 32;
	s->hist = AllocMem(s->nhist * CU_MAX_DGRAM, MEMF_ANY);
	s->hlen = AllocMem(s->nhist * sizeof(UWORD), MEMF_ANY | MEMF_CLEAR);
	return s->hist && s->hlen;
}

static void SenderMain(void)
{
	struct Task *me = FindTask(NULL);
	struct Start *start;
	struct McCard *mc;
	struct Sender *s;
	struct DateStamp ds;
	ULONG ticks;

	/* the parent sets tc_UserData right after creating us */
	while (!(start = me->tc_UserData))
		Delay(1);
	mc = start->mc;

	s = AllocMem(sizeof(*s), MEMF_PUBLIC | MEMF_CLEAR);
	if (!s)
		return;
	s->mc = mc;
	s->view = start->view;
	s->cfg = &mc->cfg;
	s->serial = mc->serial - 1;
	s->ptr_serial = mc->ptr_serial - 1;
	s->sock = -1;
	DateStamp(&ds);
	s->stream = (ds.ds_Days << 20) ^ (ds.ds_Minute << 12) ^ (ds.ds_Tick << 4) ^ (ULONG)mc ^
	            ((ULONG)s->view << 28);

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
				ULONG d = s->to.sin_addr.s_addr;

				mc_log(s, "Copper to %ld.%ld.%ld.%ld", d >> 24, (d >> 16) & 255,
				       (d >> 8) & 255, d & 255);
				mc_log(s, ":%ld, %ld fps, %ld KB/s\n", s->to.sin_port, mc->cfg.fps,
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

	if (s->sockbase && !alloc_history(s)) {
		mc_log(s, "no memory for the history\n", 0, 0, 0, 0);
		mc->sender_quit = 1;
	}
	if (OpenDevice("timer.device", UNIT_MICROHZ, (struct IORequest *)&s->tr, 0) == 0) {
		struct EClockVal ev;

		TimerBase = s->tr.tr_node.io_Device;
		s->efreq = ReadEClock(&ev);
		s->timer = s->efreq != 0;
	}
	s->ibase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);

	ticks = TICKS_PER_SECOND / mc->cfg.fps;
	if (!ticks)
		ticks = 1;
	while (s->sockbase && !mc->sender_quit) {
		if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
			break;
		pass(s);
		Delay(ticks);
	}

	if (s->sockbase) {
		struct Library *SocketBase = s->sockbase;

		CloseSocket(s->sock);
		CloseLibrary(SocketBase);
	}
	mc_log(s, "sender stopped\n", 0, 0, 0, 0);
	if (s->timer)
		CloseDevice((struct IORequest *)&s->tr);
	if (s->ibase)
		CloseLibrary((struct Library *)s->ibase);
	free_tiles(s);
	if (s->hist)
		FreeMem(s->hist, s->nhist * CU_MAX_DGRAM);
	if (s->hlen)
		FreeMem(s->hlen, s->nhist * sizeof(UWORD));
	{
		UWORD view = s->view;

		FreeMem(s, sizeof(*s));
		Forbid();
		if (!view)
			mc->sender = NULL;
	}
}

BOOL mc_sender_start(struct McCard *mc)
{
	/*
	 * "Multicast.card sender", "Multicast.card sender 2" for the second
	 * board, ...; with SPLIT=, the further parts' senders are
	 * "Multicast.card part 2" and so on.
	 */
	static char names[MC_MAXBOARDS][MC_MAXVIEWS][24];
	static struct Start starts[MC_MAXBOARDS][MC_MAXVIEWS];
	struct Process *p;
	UWORD v, n = mc->cfg.split_cols * mc->cfg.split_rows;

	if (n < 1 || n > MC_MAXVIEWS)
		n = 1;
	mc->nviews = n;
	for (v = 0; v < n; v++) {
		char *name = names[mc->boardnum][v];

		mc->view_tail[v] = mc->ring_head;
		if (v) {
			CopyMem("Multicast.card part ", name, 20);
			name[20] = '1' + v;
			name[21] = mc->boardnum ? '/' : 0;
			name[22] = '1' + mc->boardnum;
			name[23] = 0;
		} else {
			CopyMem("Multicast.card sender", name, 22);
			if (mc->boardnum) {
				name[21] = ' ';
				name[22] = '1' + mc->boardnum;
				name[23] = 0;
			}
		}
		starts[mc->boardnum][v].mc = mc;
		starts[mc->boardnum][v].view = v;
		Forbid();
		p = CreateNewProcTags(NP_Entry, (ULONG)SenderMain,
		                      NP_Name, (ULONG)name,
		                      NP_Priority, mc->cfg.pri,
		                      NP_StackSize, 8192,
		                      TAG_END);
		if (p) {
			p->pr_Task.tc_UserData = &starts[mc->boardnum][v];
			if (!v)
				mc->sender = p;
		}
		Permit();
		if (!p)
			return v > 0;
	}
	return TRUE;
}
