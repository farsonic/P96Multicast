/*
 * P96Multicast - mctest: opens a screen on the Multicast card and draws a
 * known test pattern, for the end-to-end test (emu/run_test.sh).
 *
 *   mctest LIST                         list display modes
 *   mctest STATUS                       is the card loaded, is its sender running
 *   mctest DEPTH=8 SECS=10              the first Multicast mode with this depth
 *   mctest WIDTH=640 HEIGHT=480 DEPTH=16  a particular size
 *   mctest PUBSCREEN=MULTICAST WIDTH=800 HEIGHT=600 DEPTH=8
 *                                       a Workbench-like public screen on the
 *                                       card, open until Ctrl-C, for shells and
 *                                       programs (CON:.../SCREENMULTICAST)
 *   mctest PUBSCREEN=MULTICAST DEFAULT  ... and the default public screen
 *                                       while open: new shells go there
 *   mctest PUBSCREEN=MULTICAST BACK     ... opened behind the other screens: the
 *                                       card streams it while e.g. the Workbench
 *                                       stays in front on another board
 *   mctest WORKLOAD WIDTH=640 HEIGHT=480 DEPTH=8 [DUMP=Work:screen.raw]
 *                                       a Workbench-like screen on the card and
 *                                       everyday work on it (a console listing
 *                                       directories, windows moved and resized,
 *                                       drawing, scrolling, images), and for
 *                                       each step what the card sent: orders,
 *                                       pixels, KB. DUMP= then writes the screen
 *                                       as the card has it (p96view.py --raw's
 *                                       format), and keeps it 8 s longer
 *   mctest SPAN=2x2 WIDTH=640 HEIGHT=480 DEPTH=16 [MARK=Work:span] [STEP=10]
 *                                       for Wrap.card's SPAN=: a screen of 2x2
 *                                       times the mode's size, the test pattern
 *                                       in each head's tile, and a window that
 *                                       moves from head to head every STEP
 *                                       seconds, creating MARK-1, MARK-2, ...
 *                                       as it gets to head 1, 2, ...
 *
 * The pattern: eight vertical colour bars across the top half, a white
 * frame round the screen, and in the bottom half a 16x16 grid of the first
 * 256 pens (CLUT) or a red/green gradient (hi/true colour). The receiver
 * test checks the bars.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include "mc_status.h"
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <intuition/screens.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>
#include <stdio.h>

struct Library *CyberGfxBase;

/* cybergraphics.library FillPixelArray(); the NDK has no CGX headers */
ULONG __FillPixelArray(__reg("a6") void *, __reg("a1") struct RastPort *rp,
                       __reg("d0") UWORD x, __reg("d1") UWORD y, __reg("d2") UWORD w,
                       __reg("d3") UWORD h, __reg("d4") ULONG argb) = "\tjsr\t-150(a6)";
#define FillPixelArray(rp, x, y, w, h, argb) __FillPixelArray(CyberGfxBase, rp, x, y, w, h, argb)

/* cybergraphics.library WritePixelArray(), RECTFMT_RGB = 0 */
ULONG __WritePixelArray(__reg("a6") void *, __reg("a0") APTR src, __reg("d0") UWORD sx,
                        __reg("d1") UWORD sy, __reg("d2") UWORD mod, __reg("a1") struct RastPort *rp,
                        __reg("d3") UWORD dx, __reg("d4") UWORD dy, __reg("d5") UWORD w,
                        __reg("d6") UWORD h, __reg("d7") UBYTE fmt) = "\tjsr\t-126(a6)";
#define WritePixelArray(src, sx, sy, mod, rp, dx, dy, w, h, fmt) \
	__WritePixelArray(CyberGfxBase, src, sx, sy, mod, rp, dx, dy, w, h, fmt)

/* cybergraphics.library LockBitMapTagList() and UnLockBitMap() */
APTR __LockBitMapTagList(__reg("a6") void *, __reg("a0") APTR bm, __reg("a1") ULONG *tags) =
	"\tjsr\t-168(a6)";
void __UnLockBitMap(__reg("a6") void *, __reg("a0") APTR lock) = "\tjsr\t-174(a6)";
#define LockBitMapTagList(bm, tags) __LockBitMapTagList(CyberGfxBase, bm, tags)
#define UnLockBitMap(lock) __UnLockBitMap(CyberGfxBase, lock)
#define LBMI_BYTESPERPIX  0x84001005UL
#define LBMI_BYTESPERROW  0x84001006UL
#define LBMI_BASEADDRESS  0x84001007UL

static const char template[] = "LIST/S,STATUS/S,NAME/K,WIDTH/N,HEIGHT/N,DEPTH/N,SECS/N,PUBSCREEN/K,DEFAULT/S,BACK/S,SPAN/K,MARK/K,STEP/N,WORKLOAD/S,DUMP/K";
enum { A_LIST, A_STATUS, A_NAME, A_WIDTH, A_HEIGHT, A_DEPTH, A_SECS, A_PUBSCREEN, A_DEFAULT, A_BACK,
       A_SPAN, A_MARK, A_STEP, A_WORKLOAD, A_DUMP, A_COUNT };

static const char *blit_names[MC_BLIT_KINDS + 1] = {
	"FillRect", "InvertRect", "BlitRect", "BlitTemplate", "BlitPattern", "DrawLine",
	"BlitRectNoMaskComplete", "BlitPlanar2Chunky", "BlitPlanar2Direct", "copied by Wrap.card"
};

/* SPAN's window: its colour, and where it sits in each head's tile */
#define SPAN_WINDOW_RGB 0xff8000

/* the bar colours, also known to tests/check_frame.py */
static const ULONG bars[8] = {
	0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000
};

static BOOL mode_name(ULONG id, char *buf, int len)
{
	struct NameInfo ni;
	DisplayInfoHandle h = FindDisplayInfo(id);

	buf[0] = 0;
	if (!h || !GetDisplayInfoData(h, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, 0))
		return FALSE;
	strncpy(buf, (char *)ni.Name, len - 1);
	buf[len - 1] = 0;
	return TRUE;
}

static BOOL mode_dims(ULONG id, UWORD *w, UWORD *h, UWORD *depth)
{
	struct DimensionInfo di;
	DisplayInfoHandle hd = FindDisplayInfo(id);

	if (!hd || !GetDisplayInfoData(hd, (UBYTE *)&di, sizeof(di), DTAG_DIMS, 0))
		return FALSE;
	*w = di.Nominal.MaxX - di.Nominal.MinX + 1;
	*h = di.Nominal.MaxY - di.Nominal.MinY + 1;
	*depth = di.MaxDepth;
	return TRUE;
}

static void list_modes(void)
{
	ULONG id = INVALID_ID;
	char name[64];
	UWORD w, h, d;

	while ((id = NextDisplayInfo(id)) != INVALID_ID) {
		mode_name(id, name, sizeof(name));
		if (!mode_dims(id, &w, &h, &d))
			w = h = d = 0;
		printf("%08lx %4u x %4u x %2u  %s%s\n", id, w, h, d, name,
		       ModeNotAvailable(id) ? "  (not available)" : "");
	}
}

/*
 * First mode whose name contains name, with enough depth and this size
 * (*want_w == 0: any size; the size and depth found are returned).
 */
static ULONG find_mode(const char *name, UWORD *want_w, UWORD *want_h, UWORD *want_d)
{
	ULONG id = INVALID_ID;
	char buf[64];
	UWORD w, h, d;

	while ((id = NextDisplayInfo(id)) != INVALID_ID) {
		char *m;

		/* the whole board name: "Multicast" is not "Multicast2" */
		if (!mode_name(id, buf, sizeof(buf)) || !(m = strstr(buf, name)))
			continue;
		m += strlen(name);
		if ((*m >= '0' && *m <= '9') || (*m >= 'A' && *m <= 'Z') || (*m >= 'a' && *m <= 'z'))
			continue;
		if (!mode_dims(id, &w, &h, &d) || d < *want_d)
			continue;
		if (*want_w && (w != *want_w || h != *want_h))
			continue;
		*want_w = w;
		*want_h = h;
		*want_d = d;
		return id;
	}
	return INVALID_ID;
}

static const char *screen_error(ULONG err)
{
	switch (err) {
	case OSERR_NOMONITOR:   return "no monitor for this mode (NOMONITOR)";
	case OSERR_NOCHIPS:     return "the graphics hardware cannot do it (NOCHIPS)";
	case OSERR_NOMEM:       return "out of memory (NOMEM)";
	case OSERR_NOCHIPMEM:   return "out of Chip RAM (NOCHIPMEM)";
	case OSERR_PUBNOTUNIQUE: return "public screen name in use";
	case OSERR_UNKNOWNMODE: return "unknown display mode (UNKNOWNMODE)";
	case OSERR_TOODEEP:     return "too deep for this mode (TOODEEP)";
	case OSERR_ATTACHFAIL:  return "attach failed";
	case OSERR_NOTAVAILABLE: return "mode not available (NOTAVAILABLE)";
	}
	return "unknown reason";
}

static void draw_clut(struct Screen *scr, UWORD w, UWORD h)
{
	struct RastPort *rp = &scr->RastPort;
	int i;

	/* pens 0..7: the bars; 8..255: a grey ramp */
	for (i = 0; i < 8; i++)
		SetRGB32(&scr->ViewPort, i, (bars[i] >> 16) * 0x01010101UL,
		         ((bars[i] >> 8) & 0xff) * 0x01010101UL, (bars[i] & 0xff) * 0x01010101UL);
	for (i = 8; i < 256; i++)
		SetRGB32(&scr->ViewPort, i, i * 0x01010101UL, i * 0x01010101UL, i * 0x01010101UL);

	SetAPen(rp, 7);
	RectFill(rp, 0, 0, w - 1, h - 1);
	for (i = 0; i < 8; i++) {
		SetAPen(rp, i);
		RectFill(rp, i * w / 8, 0, (i + 1) * w / 8 - 1, h / 2 - 1);
	}
	for (i = 0; i < 256; i++) {
		int cw = w / 16, ch = (h / 2) / 16;
		int x = (i % 16) * cw, y = h / 2 + (i / 16) * ch;

		SetAPen(rp, i);
		RectFill(rp, x, y, x + cw - 1, y + ch - 1);
	}
	SetAPen(rp, 0);
	Move(rp, 0, 0);
	Draw(rp, w - 1, 0);
	Draw(rp, w - 1, h - 1);
	Draw(rp, 0, h - 1);
	Draw(rp, 0, 0);
}

/* the pattern in the w x h rectangle at ox,oy */
static void draw_rgb_at(struct RastPort *rp, int ox, int oy, UWORD w, UWORD h)
{
	int i, y;

	FillPixelArray(rp, ox, oy, w, h, 0x000000);
	for (i = 0; i < 8; i++)
		FillPixelArray(rp, ox + i * w / 8, oy, (i + 1) * w / 8 - i * w / 8, h / 2, bars[i]);
	for (y = h / 2; y < h; y += 4)
		for (i = 0; i < 16; i++) {
			ULONG r = (y - h / 2) * 255 / (h / 2), g = i * 17;

			FillPixelArray(rp, ox + i * w / 16, oy + y, w / 16, 4, (r << 16) | (g << 8) | 0x40);
		}
	FillPixelArray(rp, ox, oy, w, 1, 0xffffff);
	FillPixelArray(rp, ox, oy + h - 1, w, 1, 0xffffff);
	FillPixelArray(rp, ox, oy, 1, h, 0xffffff);
	FillPixelArray(rp, ox + w - 1, oy, 1, h, 0xffffff);
}

static void draw_rgb(struct Screen *scr, UWORD w, UWORD h)
{
	draw_rgb_at(&scr->RastPort, 0, 0, w, h);
}

static const char *errors[] = {
	"none",
	"no memory for the card's context",
	"not even 1 MB of Fast RAM free for video memory (plus 512 KB spare)",
	"could not create the sender process",
};

static void status(void)
{
	struct McBase *card;
	struct McStatus st;
	struct Task *sender;
	struct Library *sock;

	Forbid();
	card = (struct McBase *)FindName(&SysBase->LibList, "Multicast.card");
	if (card)
		st = card->status;
	sender = FindTask("Multicast.card sender");
	sock = (struct Library *)FindName(&SysBase->LibList, "bsdsocket.library");
	Permit();

	printf("Multicast.card: %s", card ? "loaded" : "not loaded");
	if (card)
		printf(", version %u.%u, open count %u", card->lib.lib_Version,
		       card->lib.lib_Revision, card->lib.lib_OpenCnt);
	printf("\n");
	if (card && st.magic == MC_STATUS_MAGIC) {
		printf("  FindCard called %u times, InitCard %u times, boards set up: %u\n",
		       st.findcard_calls, st.initcard_calls, st.cards);
		printf("  last error: %s\n", st.error < 4 ? errors[st.error] : "?");
		printf("  largest free Fast RAM at FindCard: %lu KB, video memory: %lu KB\n",
		       st.fast_largest >> 10, st.vramsize >> 10);
		printf("  SetGC called %lu times, SetPanning %lu times%s\n", st.setgc_calls,
		       st.panning_calls, st.setgc_calls ? "" : " (no screen shown on the card yet)");
		printf("  screen modes: %u from P96's settings when InitCard ran, %u added by the card\n",
		       st.modes_before, st.modes_added);
		if (card->lib.lib_Version >= 2) {
			int k;

			printf("  Copper: %lu KB sent in %lu datagrams, %lu send errors\n", st.sent_kb,
			       st.datagrams, st.send_errors);
			printf("    pixels: %lu tiles, %lu KB; viewers: %u, %lu messages heard, %lu repairs, "
			       "%lu tiles asked for\n", st.pixel_tiles, st.pixel_kb, st.viewers, st.heard,
			       st.repairs, st.wanted);
			printf("    orders: %lu fills, %lu copies, %lu inverts, %lu texts, %lu patterns\n",
			       st.orders[1], st.orders[3], st.orders[2], st.orders[4], st.orders[5]);
			printf("    the whole screen compared every %u s; order ring overflows: %lu\n",
			       st.sweep_secs, st.ring_lost);
			printf("    pointer: %s, %lu moves\n", st.pointer ? "drawn by the viewers" :
			       "not shown by the card", st.pointer_moves);
			printf("  BLITTER=%u: drawing calls the card hears about\n", st.blitter);
			for (k = 0; k <= MC_BLIT_KINDS; k++)
				if (st.blit_calls[k])
					printf("    %-24s %6lu calls, %6lu on screen\n", blit_names[k],
					       st.blit_calls[k], k < MC_BLIT_KINDS ? st.blit_screen[k] : 0);
		}
	} else if (card) {
		printf("  (version 1.0: no status block)\n");
	}
	printf("sender task: %s", sender ? "running" : "not running");
	if (sender)
		printf(", state %u, priority %d", sender->tc_State, sender->tc_Node.ln_Pri);
	printf("\nbsdsocket.library: %s\n", sock ? "in memory" : "not in memory");
	printf("Fast RAM free now: %lu KB, largest block %lu KB\n",
	       AvailMem(MEMF_FAST) >> 10, AvailMem(MEMF_FAST | MEMF_LARGEST) >> 10);
}

/*
 * Show that the picture is live: a white square runs along a black strip at
 * the bottom of the screen, and a bar below it fills up as the seconds pass.
 */
static void fill(struct Screen *scr, UWORD depth, int x, int y, int w, int h, int pen, ULONG rgb)
{
	if (w <= 0 || h <= 0)
		return;
	if (depth <= 8) {
		SetAPen(&scr->RastPort, pen);
		RectFill(&scr->RastPort, x, y, x + w - 1, y + h - 1);
	} else {
		FillPixelArray(&scr->RastPort, x, y, w, h, rgb);
	}
}

static void animate(struct Screen *scr, UWORD w, UWORD h, UWORD depth, UWORD secs)
{
	/* CLUT pens: 7 is black and 0 white (see draw_clut) */
	int strip = h - 40, sq = 24, x = 0, step = 8, t, ticks = secs * 10;

	fill(scr, depth, 1, strip, w - 2, 39, 7, 0x000000);
	for (t = 0; t < ticks; t++) {
		fill(scr, depth, x, strip + 4, sq, sq, 7, 0x000000);
		x += step;
		if (x < 0 || x + sq > w) {
			step = -step;
			x += 2 * step;
		}
		fill(scr, depth, x, strip + 4, sq, sq, 0, 0xffffff);
		fill(scr, depth, 1, h - 8, (w - 2) * (t + 1) / ticks, 6, 5, 0xff0000);
		Delay(TICKS_PER_SECOND / 10);
	}
}

static void mark(const char *prefix, int n)
{
	char name[120];
	BPTR fh;

	if (!prefix)
		return;
	snprintf(name, sizeof(name), "%s-%d", prefix, n);
	fh = Open((STRPTR)name, MODE_NEWFILE);
	if (fh)
		Close(fh);
}

/*
 * SPAN=CxR: one screen of C x R times the mode's size, larger than the
 * display, as Wrap.card's SPAN= shows it on C x R heads. The test pattern
 * goes in each head's tile, and an ordinary Intuition window (WX, WY in
 * the tile) moves from head to head, left to right and top to bottom.
 */
static int span(ULONG id, UWORD w, UWORD h, UWORD depth, UWORD cols, UWORD rows,
                const char *markname, UWORD step)
{
	int wx = w / 2 - 100, wy = h * 3 / 4 - 60;
	struct Screen *scr;
	struct Window *win;
	ULONG err = 0;
	int c, r, n;

	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, cols * w, SA_Height, rows * h,
	                     SA_Depth, depth, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
	                     SA_AutoScroll, TRUE, SA_Type, CUSTOMSCREEN,
	                     SA_ErrorCode, (ULONG)&err, TAG_END);
	if (!scr) {
		printf("mctest: cannot open a %dx%d screen: %s (error %lu)\n", cols * w, rows * h,
		       screen_error(err), err);
		return RETURN_ERROR;
	}
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++)
			draw_rgb_at(&scr->RastPort, c * w, r * h, w, h);
	win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, wx, WA_Top, wy,
	                     WA_Width, 200, WA_Height, 120, WA_Title, (ULONG)"Move me",
	                     WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SmartRefresh, TRUE,
	                     WA_Activate, TRUE, TAG_END);
	if (!win) {
		printf("mctest: cannot open the window\n");
		CloseScreen(scr);
		return RETURN_ERROR;
	}
	FillPixelArray(win->RPort, win->BorderLeft, win->BorderTop,
	               win->Width - win->BorderLeft - win->BorderRight,
	               win->Height - win->BorderTop - win->BorderBottom, SPAN_WINDOW_RGB);
	printf("mctest: %dx%d screen for %ux%u heads of %ux%u, depth %u\n", cols * w, rows * h,
	       cols, rows, w, h, depth);
	for (n = 0; n < cols * rows; n++) {
		c = n % cols;
		r = n / cols;
		MoveWindow(win, c * w + wx - win->LeftEdge, r * h + wy - win->TopEdge);
		Delay(TICKS_PER_SECOND / 2);
		printf("mctest: window on head %d, at %d,%d\n", n + 1, win->LeftEdge, win->TopEdge);
		fflush(stdout);
		mark(markname, n + 1);
		Delay(step * TICKS_PER_SECOND);
	}
	CloseWindow(win);
	CloseScreen(scr);
	printf("mctest: done\n");
	return RETURN_OK;
}

/* ---------------------------------------------------------------- WORKLOAD */

static BOOL read_status(struct McStatus *st)
{
	struct McBase *card;
	BOOL ok = FALSE;

	Forbid();
	card = (struct McBase *)FindName(&SysBase->LibList, "Multicast.card");
	if (card && card->status.magic == MC_STATUS_MAGIC) {
		*st = card->status;
		ok = TRUE;
	}
	Permit();
	return ok;
}

/* let the sender catch up, then say what the step sent */
static void report(const char *step, struct McStatus *before)
{
	struct McStatus now;
	int k, n = 0;

	Delay(3 * TICKS_PER_SECOND);
	if (!read_status(&now))
		return;
	printf("step %-13s %6lu KB sent, %5lu datagrams, %5lu pixel tiles (%lu KB)\n", step,
	       now.sent_kb - before->sent_kb, now.datagrams - before->datagrams,
	       now.pixel_tiles - before->pixel_tiles, now.pixel_kb - before->pixel_kb);
	printf("    orders: %lu fills, %lu copies, %lu inverts, %lu texts, %lu patterns\n",
	       now.orders[1] - before->orders[1], now.orders[3] - before->orders[3],
	       now.orders[2] - before->orders[2], now.orders[4] - before->orders[4],
	       now.orders[5] - before->orders[5]);
	for (k = 0; k < MC_BLIT_KINDS; k++) {
		ULONG calls = now.blit_calls[k] - before->blit_calls[k];
		ULONG screen = now.blit_screen[k] - before->blit_screen[k];

		if (!calls)
			continue;
		printf("%s%s %lu (%lu on screen)", n++ % 3 ? ", " : "    ", blit_names[k], calls, screen);
		if (n % 3 == 0)
			printf("\n");
	}
	if (n % 3)
		printf("\n");
	if (now.ring_lost != before->ring_lost)
		printf("    order ring overflows: %lu\n", now.ring_lost - before->ring_lost);
	if (now.repairs != before->repairs || now.wanted != before->wanted)
		printf("    repairs: %lu datagrams, %lu tiles\n", now.repairs - before->repairs,
		       now.wanted - before->wanted);
	fflush(stdout);
	*before = now;
}

/* the screen's pixels as the card has them, for comparing with a viewer's */
static void dump(struct Screen *scr, const char *path)
{
	ULONG base = 0, bpr = 0, bpp = 0;
	ULONG tags[] = { LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr,
	                 LBMI_BYTESPERPIX, (ULONG)&bpp, TAG_END };
	UWORD w = scr->Width, h = scr->Height, y;
	UBYTE head[12] = { 0 };
	APTR lock;
	BPTR fh;

	if (!CyberGfxBase || !(lock = LockBitMapTagList(scr->RastPort.BitMap, tags))) {
		printf("mctest: cannot read the screen's bitmap\n");
		return;
	}
	fh = Open((STRPTR)path, MODE_NEWFILE);
	if (fh) {
		head[2] = w >> 8;
		head[3] = w;
		head[6] = h >> 8;
		head[7] = h;
		head[11] = bpp;
		Write(fh, head, 12);
		for (y = 0; y < h; y++)
			Write(fh, (APTR)(base + y * bpr), w * bpp);
		Close(fh);
	}
	UnLockBitMap(lock);
	printf("mctest: the screen (%ux%u, %lu bytes per pixel) is in %s\n", w, h, bpp, path);
}

static void inner_box(struct Window *win, int *x, int *y, int *w, int *h)
{
	*x = win->BorderLeft;
	*y = win->BorderTop;
	*w = win->Width - win->BorderLeft - win->BorderRight;
	*h = win->Height - win->BorderTop - win->BorderBottom;
}

/*
 * Everyday work on a Workbench-like screen on the card, a step at a time,
 * with BLITTER=1: after each step, how many of the tiles the sender found
 * changed a drawing call the card heard about explains, and which calls.
 */
static int workload(ULONG id, UWORD w, UWORD h, UWORD depth, const char *dumpfile)
{
	static UWORD pattern[2] = { 0xaaaa, 0x5555 };
	static const char *line = "The quick brown fox jumps over the lazy dog";
	struct McStatus st, start;
	struct Screen *scr;
	struct Window *win;
	struct BitMap *friend, *planar;
	struct RastPort trp;
	BPTR con = 0, nil;
	ULONG err = 0;
	UBYTE *pix;
	int i, ix, iy, iw, ih;

	if (!read_status(&st) || !st.blitter) {
		printf("mctest: WORKLOAD needs Multicast.card with BLITTER=1\n");
		return RETURN_ERROR;
	}
	start = st;
	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
	                     SA_LikeWorkbench, TRUE, SA_PubName, (ULONG)"MCWORK",
	                     SA_Title, (ULONG)"Workload", SA_Type, PUBLICSCREEN, SA_SharePens, TRUE,
	                     SA_ErrorCode, (ULONG)&err, TAG_END);
	if (!scr) {
		printf("mctest: cannot open the screen: %s (error %lu)\n", screen_error(err), err);
		return RETURN_ERROR;
	}
	PubScreenStatus(scr, 0);
	printf("mctest: workload on a %ux%ux%u screen\n", w, h, depth);
	report("screen", &st);

	/* a console listing directories: text and scrolling */
	{
		char spec[80];

		snprintf(spec, sizeof(spec), "CON:0/%d/%d/%d/Console/SCREENMCWORK",
		         (int)scr->BarHeight + 1, (int)w, (int)h / 2);
		con = Open((STRPTR)spec, MODE_NEWFILE);
	}
	if (con) {
		nil = Open((STRPTR)"NIL:", MODE_OLDFILE);
		SystemTags((STRPTR)"Dir SYS:C", SYS_Input, nil, SYS_Output, con, TAG_END);
		SystemTags((STRPTR)"List SYS:Libs", SYS_Input, nil, SYS_Output, con, TAG_END);
		if (nil)
			Close(nil);
		report("console", &st);
	} else {
		printf("mctest: no console on the screen, skipped\n");
	}

	win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, w / 3, WA_Top, h / 3,
	                     WA_Width, w / 2, WA_Height, h / 2, WA_Title, (ULONG)"Work",
	                     WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
	                     WA_CloseGadget, TRUE, WA_SmartRefresh, TRUE, WA_MinWidth, 100,
	                     WA_MinHeight, 60, WA_MaxWidth, -1, WA_MaxHeight, -1,
	                     WA_Activate, TRUE, TAG_END);
	if (!win) {
		printf("mctest: cannot open a window\n");
		if (con)
			Close(con);
		CloseScreen(scr);
		return RETURN_ERROR;
	}
	report("window", &st);

	for (i = 0; i < 12; i++) {
		MoveWindow(win, i < 6 ? -16 : 16, i < 6 ? -8 : 8);
		Delay(5);
	}
	SizeWindow(win, -40, -30);
	Delay(10);
	SizeWindow(win, 40, 30);
	Delay(10);
	WindowToBack(win);
	Delay(10);
	WindowToFront(win);
	report("move/size", &st);

	inner_box(win, &ix, &iy, &iw, &ih);
	for (i = 0; i < 20; i++) {
		SetAPen(win->RPort, 1 + i % 7);
		RectFill(win->RPort, ix + (i * 13) % (iw - 40), iy + (i * 7) % (ih - 30),
		         ix + (i * 13) % (iw - 40) + 30, iy + (i * 7) % (ih - 30) + 20);
	}
	for (i = 0; i < 40; i++) {
		SetAPen(win->RPort, 1 + i % 3);
		Move(win->RPort, ix + i * (iw - 1) / 40, iy);
		Draw(win->RPort, ix + iw - 1 - i * (iw - 1) / 40, iy + ih - 1);
	}
	SetAPen(win->RPort, 1);
	for (i = 0; i < 12 && 10 + i * 9 < ih; i++) {
		Move(win->RPort, ix + 4, iy + 10 + i * 9);
		Text(win->RPort, (STRPTR)line, strlen(line) < iw / 8 ? strlen(line) : iw / 8);
	}
	SetDrMd(win->RPort, COMPLEMENT);
	for (i = 0; i < 5; i++)
		RectFill(win->RPort, ix + 10 + i * 20, iy + 10, ix + 40 + i * 20, iy + 40);
	SetDrMd(win->RPort, JAM1);
	SetAfPt(win->RPort, pattern, 1);
	for (i = 0; i < 5; i++)
		RectFill(win->RPort, ix + 10 + i * 20, iy + 50, ix + 40 + i * 20, iy + 80);
	SetAfPt(win->RPort, NULL, 0);
	report("drawing", &st);

	for (i = 0; i < 30; i++) {
		ScrollRaster(win->RPort, 0, 9, ix, iy, ix + iw - 1, iy + ih - 1);
		Move(win->RPort, ix + 4, iy + ih - 3);
		Text(win->RPort, (STRPTR)line, strlen(line) < iw / 8 ? strlen(line) : iw / 8);
		Delay(2);
	}
	report("scrolling", &st);

	/* images: from a bitmap like the screen's, from a planar one, from memory */
	friend = AllocBitMap(64, 64, depth, BMF_CLEAR, scr->RastPort.BitMap);
	planar = AllocBitMap(64, 64, 4, BMF_CLEAR, NULL);
	pix = AllocMem(64 * 64 * 3, MEMF_ANY);
	if (friend) {
		InitRastPort(&trp);
		trp.BitMap = friend;
		SetAPen(&trp, 3);
		RectFill(&trp, 8, 8, 55, 55);
		for (i = 0; i < 6; i++)
			BltBitMapRastPort(friend, 0, 0, win->RPort, ix + i * 20, iy + 20, 64, 64, 0xc0);
	}
	if (planar) {
		InitRastPort(&trp);
		trp.BitMap = planar;
		SetAPen(&trp, 5);
		RectFill(&trp, 4, 4, 59, 59);
		for (i = 0; i < 6; i++)
			BltBitMapRastPort(planar, 0, 0, win->RPort, ix + i * 20, iy + 60, 64, 64, 0xc0);
	}
	if (pix) {
		for (i = 0; i < 64 * 64 * 3; i++)
			pix[i] = i * 7;
		for (i = 0; i < 6; i++) {
			if (depth > 8 && CyberGfxBase)
				WritePixelArray(pix, 0, 0, 64 * 3, win->RPort, ix + i * 20, iy + 100, 64, 64, 0);
			else
				WriteChunkyPixels(win->RPort, ix + i * 20, iy + 100, ix + i * 20 + 63,
				                  iy + 163, pix, 64);
		}
	}
	report("images", &st);

	CloseWindow(win);
	report("close window", &st);
	if (con) {
		Close(con);
		report("close console", &st);
	}
	if (friend)
		FreeBitMap(friend);
	if (planar)
		FreeBitMap(planar);
	if (pix)
		FreeMem(pix, 64 * 64 * 3);
	report("idle", &st);
	report("total", &start);
	if (dumpfile) {
		/* the screen stays as it is a while longer, for the viewer to be compared */
		dump(scr, dumpfile);
		fflush(stdout);
		Delay(8 * TICKS_PER_SECOND);
	}
	while (!(PubScreenStatus(scr, PSNF_PRIVATE) & 1))
		Delay(TICKS_PER_SECOND);
	CloseScreen(scr);
	printf("mctest: done\n");
	return RETURN_OK;
}

/*
 * A public screen on the card, like the Workbench screen, until Ctrl-C and
 * no visitor windows are left.
 */
static int pubscreen(char *name, ULONG id, UWORD w, UWORD h, UWORD depth, BOOL def, BOOL back)
{
	struct Screen *scr;
	ULONG err = 0;
	char olddefault[MAXPUBSCREENNAME + 1];

	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h,
	                     SA_Depth, depth, SA_PubName, (ULONG)name,
	                     SA_Title, (ULONG)"Multicast screen", SA_LikeWorkbench, TRUE,
	                     SA_SharePens, TRUE, SA_Type, PUBLICSCREEN,
	                     SA_ErrorCode, (ULONG)&err, TAG_END);
	if (!scr) {
		printf("mctest: cannot open public screen %s: %s (error %lu)\n", name,
		       screen_error(err), err);
		return RETURN_ERROR;
	}
	PubScreenStatus(scr, 0);
	if (back)
		ScreenToBack(scr);
	if (def) {
		GetDefaultPubScreen(olddefault);
		SetDefaultPubScreen(name);
	}
	printf("mctest: public screen %s open on mode %08lx, %ux%ux%u%s\n"
	       "        open windows on it, e.g. NewShell \"CON:20/20/600/200/Shell/CLOSE/SCREEN%s\"\n"
	       "        Ctrl-C (or Break) closes it once its windows are closed\n",
	       name, id, w, h, depth, def ? ", the default public screen" : "", name);
	fflush(stdout);
	for (;;) {
		Wait(SIGBREAKF_CTRL_C);
		/* bit 0 of the old status is clear while visitors are on it */
		if (PubScreenStatus(scr, PSNF_PRIVATE) & 1)
			break;
		printf("mctest: %s still has windows on it; close them, then Ctrl-C again\n", name);
		fflush(stdout);
	}
	if (def)
		SetDefaultPubScreen(olddefault[0] ? olddefault : NULL);
	CloseScreen(scr);
	printf("mctest: public screen %s closed\n", name);
	return RETURN_OK;
}

int main(void)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	const char *name = "Multicast";
	UWORD w = 0, h = 0, depth = 8, secs = 10, want_d;
	ULONG id, err = 0, na;
	struct Screen *scr;

	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), "mctest");
		return RETURN_FAIL;
	}
	if (args[A_NAME])
		name = (const char *)args[A_NAME];
	if (args[A_WIDTH])
		w = *(LONG *)args[A_WIDTH];
	if (args[A_HEIGHT])
		h = *(LONG *)args[A_HEIGHT];
	if (args[A_DEPTH])
		depth = *(LONG *)args[A_DEPTH];
	if (args[A_SECS])
		secs = *(LONG *)args[A_SECS];

	if (args[A_STATUS]) {
		status();
		FreeArgs(rda);
		return RETURN_OK;
	}
	if (args[A_LIST]) {
		list_modes();
		FreeArgs(rda);
		return RETURN_OK;
	}

	/* 32 bit modes report a depth of 24 */
	want_d = depth > 24 ? 24 : depth;
	if (!args[A_WIDTH] || !args[A_HEIGHT])
		w = h = 0;
	id = find_mode(name, &w, &h, &want_d);
	if (id == INVALID_ID) {
		if (w)
			printf("mctest: no \"%s\" mode %ux%u with depth %u\n", name, w, h, depth);
		else
			printf("mctest: no \"%s\" mode with depth %u\n", name, depth);
		FreeArgs(rda);
		return RETURN_ERROR;
	}
	/* open with the mode's own depth: 24 for P96's 32 bit modes, 32 on AROS */
	if (depth > 8)
		depth = want_d;
	na = ModeNotAvailable(id);
	printf("mctest: mode %08lx, %ux%u, depth %u%s\n", id, w, h, depth,
	       na ? " - graphics.library says not available" : "");
	if (na)
		printf("mctest: ModeNotAvailable() = $%lx (1 = no chips, 2 = no monitor, 4 = not with genlock)\n", na);

	CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
	if (depth > 8 && !CyberGfxBase) {
		printf("mctest: needs cybergraphics.library for depth %u\n", depth);
		FreeArgs(rda);
		return RETURN_ERROR;
	}

	if (args[A_WORKLOAD]) {
		int rc = workload(id, w, h, depth, (const char *)args[A_DUMP]);

		if (CyberGfxBase)
			CloseLibrary(CyberGfxBase);
		FreeArgs(rda);
		return rc;
	}

	if (args[A_SPAN]) {
		const char *sp = (const char *)args[A_SPAN];
		int cols = 0, rows = 0, rc;

		while (*sp >= '0' && *sp <= '9')
			cols = cols * 10 + *sp++ - '0';
		if (*sp == 'x' || *sp == 'X')
			sp++;
		while (*sp >= '0' && *sp <= '9')
			rows = rows * 10 + *sp++ - '0';
		if (cols < 1 || rows < 1 || depth <= 8) {
			printf("mctest: SPAN=CxR, e.g. 2x2, with DEPTH=16 or 32\n");
			rc = RETURN_ERROR;
		} else {
			rc = span(id, w, h, depth, cols, rows, (const char *)args[A_MARK],
			          args[A_STEP] ? *(LONG *)args[A_STEP] : 10);
		}
		if (CyberGfxBase)
			CloseLibrary(CyberGfxBase);
		FreeArgs(rda);
		return rc;
	}

	if (args[A_PUBSCREEN]) {
		int rc = pubscreen((char *)args[A_PUBSCREEN], id, w, h, depth,
		                   args[A_DEFAULT] != 0, args[A_BACK] != 0);

		if (CyberGfxBase)
			CloseLibrary(CyberGfxBase);
		FreeArgs(rda);
		return rc;
	}

	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h,
	                     SA_Depth, depth, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
	                     SA_Type, CUSTOMSCREEN, SA_ErrorCode, (ULONG)&err, TAG_END);
	if (!scr) {
		printf("mctest: cannot open screen: %s (error %lu)\n", screen_error(err), err);
		if (CyberGfxBase)
			CloseLibrary(CyberGfxBase);
		FreeArgs(rda);
		return RETURN_ERROR;
	}
	if (depth <= 8)
		draw_clut(scr, w, h);
	else
		draw_rgb(scr, w, h);
	printf("mctest: drawn, animating for %u s\n", secs);
	animate(scr, w, h, depth, secs);
	CloseScreen(scr);
	if (CyberGfxBase)
		CloseLibrary(CyberGfxBase);
	FreeArgs(rda);
	printf("mctest: done\n");
	return RETURN_OK;
}
