/*
 * P96Multicast - Wrap.card's heads: the first wrapped board's screen on up
 * to WRAP_MAXHEADS other boards.
 *
 * Each head is one board of a card driver (MIRROR=, HEAD=, HEAD1= ...),
 * set up on a BoardInfo of Wrap.card's own: rtg.library never sees it, so
 * nothing else draws on it or switches it. A process, MIRRORFPS times a
 * second, follows what the thunks recorded about the wrapped board: on a
 * new mode it sets each head's mode (ResolvePixelClock, SetClock, SetGC,
 * SetDAC, SetPanning), it passes palette changes on with SetColorArray and
 * the display state with SetSwitch and SetDisplay, and it copies each
 * head's part of the screen into the head's video memory.
 *
 * MIRROR= is one head showing what the wrapped board shows. SPAN=CxR is
 * C x R heads showing one big screen, each its tile, so windows move from
 * monitor to monitor by being dragged across. The big screen is either a
 * screen larger than its display mode (a 1280x960 screen in a 640x480 mode
 * for 2x2: the heads show the mode's size each, wherever the wrapped board
 * itself is scrolled to) or one big mode, split evenly between the heads.
 *
 * The pixels are copied as they are, so the heads have to offer the
 * wrapped board's pixel format.
 */
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>
#include "wrap.h"

/* the BoardInfo functions the heads get, a6 = the head card's base */
typedef ULONG (*ResolvePixelClockFn)(__reg("a0") struct BoardInfo *, __reg("a1") struct ModeInfo *,
                                     __reg("d0") ULONG clock, __reg("d7") ULONG fmt,
                                     __reg("a6") struct Library *);
typedef void (*SetClockFn)(__reg("a0") struct BoardInfo *, __reg("a6") struct Library *);
typedef void (*SetGCFn)(__reg("a0") struct BoardInfo *, __reg("a1") struct ModeInfo *,
                        __reg("d0") ULONG border, __reg("a6") struct Library *);
typedef void (*SetDACFn)(__reg("a0") struct BoardInfo *, __reg("d7") ULONG fmt,
                         __reg("a6") struct Library *);
typedef ULONG (*BytesPerRowFn)(__reg("a0") struct BoardInfo *, __reg("d0") ULONG width,
                               __reg("d7") ULONG fmt, __reg("a6") struct Library *);
typedef void (*SetPanningFn)(__reg("a0") struct BoardInfo *, __reg("a1") UBYTE *mem,
                             __reg("d0") ULONG width, __reg("d1") LONG x, __reg("d2") LONG y,
                             __reg("d7") ULONG fmt, __reg("a6") struct Library *);
typedef void (*SetColorArrayFn)(__reg("a0") struct BoardInfo *, __reg("d0") ULONG start,
                                __reg("d1") ULONG count, __reg("a6") struct Library *);
typedef ULONG (*SetStateFn)(__reg("a0") struct BoardInfo *, __reg("d0") ULONG state,
                            __reg("a6") struct Library *);

/* rtg.library's private part of a BoardInfo, after MemorySpaceSize: room to spare */
#define HEAD_BISIZE (sizeof(struct BoardInfo) + 8192)

struct Head {
	struct WrapHead *h;        /* its public state */
	struct ModeInfo mode;
	BOOL  ok;                  /* it shows the current mode: copy */
};

struct Heads {
	struct WrapBoard *src;     /* the wrapped board */
	struct Library *inner;     /* its card */
	struct WrapMirror *m;
	struct Head head[WRAP_MAXHEADS];

	/* the wrapped board's screen, from the last snapshot */
	ULONG serial, pal_serial;
	UWORD w, h, bm_width;      /* its mode and bitmap width */
	ULONG fmt, bpp;
	UBYTE *src_mem;            /* top left of what the heads show */
	ULONG src_bpr;
	UWORD tw, th;              /* each head's tile */
	UWORD on;
};

static struct Heads heads;

/*
 * A BoardInfo as rtg.library hands one to FindCard: library bases, the
 * lists and the lock initialised, every function an rts (some drivers call
 * the functions they leave alone, and a board nobody draws on needs none).
 */
static struct BoardInfo *new_boardinfo(struct Library *card, UWORD num)
{
	struct BoardInfo *bi = AllocMem(HEAD_BISIZE, MEMF_PUBLIC | MEMF_CLEAR);
	APTR *slots;
	int i;

	if (!bi)
		return NULL;
	slots = &bi->AllocCardMem;
	for (i = 0; i < WRAP_NSLOTS; i++)
		slots[i] = (APTR)LibNull;
	bi->ExecBase = SysBase;
	bi->UtilBase = OpenLibrary("utility.library", 37);
	bi->CardBase = card;
	bi->BoardNum = num;
	InitSemaphore(&bi->BoardLock);
	NewList((struct List *)&bi->ResolutionsList);
	NewList((struct List *)&bi->SpecialFeatures);
	NewList((struct List *)&bi->BitMapList);
	NewList((struct List *)&bi->MemList);
	return bi;
}

static void free_boardinfo(struct BoardInfo *bi)
{
	if (bi->UtilBase)
		CloseLibrary(bi->UtilBase);
	FreeMem(bi, HEAD_BISIZE);
}

static void set_state(struct Head *hd, UWORD on)
{
	struct BoardInfo *bi = hd->h->bi;

	((SetStateFn)bi->SetSwitch)(bi, on, hd->h->lib);
	((SetStateFn)bi->SetDisplay)(bi, on, hd->h->lib);
	hd->h->display = on;
}

static void set_palette(struct Heads *hs, struct Head *hd)
{
	struct BoardInfo *bi = hd->h->bi;
	int i;

	for (i = 0; i < 256; i++)
		bi->CLUT[i] = hs->src->bi->CLUT[i];
	((SetColorArrayFn)bi->SetColorArray)(bi, 0, 256, hd->h->lib);
}

/* a head's mode: the wrapped board's, at the tile's size */
static void set_mode(struct Heads *hs, struct Head *hd, UWORD x, UWORD y)
{
	struct WrapHead *h = hd->h;
	struct BoardInfo *bi = h->bi;
	struct Library *card = h->lib;
	ULONG bpr;

	hd->ok = FALSE;
	h->x = x;
	h->y = y;
	if (!(bi->RGBFormats & RGBFF(hs->fmt))) {
		h->error = WRAP_MERR_FORMAT;
		return;
	}
	hd->mode = hs->src->disp.mode;
	hd->mode.Node.ln_Succ = hd->mode.Node.ln_Pred = NULL;
	if (hd->mode.Width != hs->tw || hd->mode.Height != hs->th) {
		/* a part of one big mode: the same refresh, roughly */
		hd->mode.HorTotal = (ULONG)hd->mode.HorTotal * hs->tw / hd->mode.Width;
		hd->mode.VerTotal = (ULONG)hd->mode.VerTotal * hs->th / hd->mode.Height;
		hd->mode.PixelClock = hd->mode.PixelClock / hd->mode.Width * hs->tw /
		                      hd->mode.Height * hs->th;
		hd->mode.Width = hs->tw;
		hd->mode.Height = hs->th;
	}
	bi->ModeInfo = &hd->mode;
	bi->RGBFormat = hs->fmt;
	((ResolvePixelClockFn)bi->ResolvePixelClock)(bi, &hd->mode, hd->mode.PixelClock, hs->fmt,
	                                             card);
	((SetClockFn)bi->SetClock)(bi, card);
	((SetGCFn)bi->SetGC)(bi, &hd->mode, 0, card);
	((SetDACFn)bi->SetDAC)(bi, hs->fmt, card);
	bpr = ((BytesPerRowFn)bi->CalculateBytesPerRow)(bi, hs->tw, hs->fmt, card);
	if (!bpr || bpr * hs->th > bi->MemorySize) {
		h->error = WRAP_MERR_TOOBIG;
		return;
	}
	h->mem = bi->MemoryBase;
	h->bpr = bpr;
	((SetPanningFn)bi->SetPanning)(bi, h->mem, hs->tw, 0, 0, hs->fmt, card);
	if (hs->fmt == RGBFB_CLUT)
		set_palette(hs, hd);
	h->width = hs->tw;
	h->height = hs->th;
	h->format = hs->fmt;
	h->setups++;
	h->error = WRAP_MERR_NONE;
	hd->ok = TRUE;
}

/*
 * The wrapped board shows something new: where the heads' tiles are, and
 * their mode if it changed. A mirror shows what is visible; a span shows
 * the whole bitmap from its top left.
 */
static void follow(struct Heads *hs, struct WrapDisplay *d)
{
	struct WrapMirror *m = hs->m;
	struct BoardInfo *sbi = hs->src->bi;
	BOOL span = m->nheads > 1;
	BOOL newmode;
	UWORD tw, th, i, on;

	if (!d->width || !d->height || !d->mem || !wrap_bytesperpixel(d->format)) {
		tw = th = 0;
	} else if (!span) {
		tw = d->width;
		th = d->height;
	} else if (d->bm_width >= m->cols * d->width) {
		tw = d->width;            /* a screen larger than its mode */
		th = d->height;
	} else {
		tw = d->width / m->cols;  /* one big mode */
		th = d->height / m->rows;
	}
	newmode = tw != hs->tw || th != hs->th || d->format != hs->fmt ||
	          d->bm_width != hs->bm_width;
	hs->w = d->width;
	hs->h = d->height;
	hs->fmt = d->format;
	hs->bpp = wrap_bytesperpixel(d->format);
	hs->tw = tw;
	hs->th = th;
	if (tw && (newmode || !hs->src_bpr))
		/* the wrapped card's rows, through its thunk, which sets its a6 */
		hs->src_bpr = ((BytesPerRowFn)sbi->CalculateBytesPerRow)(sbi, d->bm_width, d->format,
		                                                         hs->inner);
	hs->bm_width = d->bm_width;
	hs->src_mem = d->mem;
	if (tw && !span)
		hs->src_mem += d->yoff * hs->src_bpr + d->xoff * hs->bpp;

	on = tw && d->display && d->moniswitch;
	for (i = 0; i < m->nheads; i++) {
		struct Head *hd = &hs->head[i];

		if (!hd->h->bi)
			continue;
		if (tw && (newmode || !hd->ok))
			set_mode(hs, hd, (i % m->cols) * tw, (i / m->cols) * th);
		if ((on && hd->ok) != hd->h->display)
			set_state(hd, on && hd->ok);
	}
}

static void heads_pass(struct Heads *hs)
{
	struct WrapDisplay d;
	struct WrapMirror *m = hs->m;
	BOOL newpal;
	UWORD i;

	Forbid();
	d = hs->src->disp;
	Permit();

	if (d.serial != hs->serial) {
		hs->serial = d.serial;
		follow(hs, &d);
	}
	newpal = d.pal_serial != hs->pal_serial;
	hs->pal_serial = d.pal_serial;

	for (i = 0; i < m->nheads; i++) {
		struct Head *hd = &hs->head[i];
		struct WrapHead *h = hd->h;
		UBYTE *src, *dst;
		ULONG len, y;

		if (!hd->ok || !h->display)
			continue;
		if (newpal && hs->fmt == RGBFB_CLUT)
			set_palette(hs, hd);
		src = hs->src_mem + h->y * hs->src_bpr + h->x * hs->bpp;
		dst = h->mem;
		len = h->width * hs->bpp;
		for (y = 0; y < h->height; y++) {
			CopyMem(src, dst, len);
			src += hs->src_bpr;
			dst += h->bpr;
		}
		h->frames++;
	}
	m->passes++;
}

static void HeadsMain(void)
{
	struct Task *me = FindTask(NULL);
	struct Heads *hs;
	ULONG ticks;
	UWORD i;

	/* the parent sets tc_UserData right after creating us */
	while (!(hs = me->tc_UserData))
		Delay(1);
	ticks = TICKS_PER_SECOND / hs->m->fps;
	if (!ticks)
		ticks = 1;
	hs->serial = hs->src->disp.serial - 1;
	while (!(SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)) {
		heads_pass(hs);
		Delay(ticks);
	}
	for (i = 0; i < hs->m->nheads; i++)
		if (hs->head[i].h->bi)
			set_state(&hs->head[i], 0);
	Forbid();
	hs->m->task = NULL;
}

/* one head: its card, a board of it, set up */
static void head_start(struct WrapHead *h, UWORD num)
{
	struct BoardInfo *bi;
	struct Library *card;

	card = wrap_open_card(h->card);
	if (!card) {
		h->error = WRAP_MERR_OPEN;
		return;
	}
	bi = new_boardinfo(card, num);
	if (!bi) {
		h->error = WRAP_MERR_NOMEM;
		CloseLibrary(card);
		return;
	}
	if (!InnerFindCard(bi, card)) {
		h->error = WRAP_MERR_NOBOARD;
		free_boardinfo(bi);
		CloseLibrary(card);
		return;
	}
	/* a board that was found cannot be given back: from here on it stays */
	h->lib = card;
	if (!InnerInitCard(bi, NULL, card)) {
		h->error = WRAP_MERR_INIT;
		return;
	}
	h->bi = bi;
}

/*
 * From Wrap.card's InitCard, for the first wrapped board, in rtg.library's
 * process. Heads that cannot be set up are left out; the wrapped board
 * works either way.
 */
void wrap_mirror_start(struct WrapBoard *b, struct Library *inner, struct WrapMirror *m)
{
	struct Heads *hs = &heads;
	struct Process *p;
	UWORD i, n = 0;

	hs->src = b;
	hs->inner = inner;
	hs->m = m;
	for (i = 0; i < m->nheads; i++) {
		hs->head[i].h = &m->head[i];
		head_start(&m->head[i], i + 1);
		if (m->head[i].bi)
			n++;
	}
	if (!n)
		return;

	Forbid();
	p = CreateNewProcTags(NP_Entry, (ULONG)HeadsMain,
	                      NP_Name, (ULONG)"Wrap.card heads",
	                      NP_Priority, -1,
	                      NP_StackSize, 4096,
	                      TAG_END);
	if (p) {
		p->pr_Task.tc_UserData = hs;
		m->task = p;
	}
	Permit();
	if (!p)
		for (i = 0; i < m->nheads; i++)
			m->head[i].error = WRAP_MERR_NOTASK;
}
