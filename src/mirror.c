/*
 * P96Multicast - Wrap.card's mirror: the first wrapped board's screen on a
 * second board.
 *
 * MIRROR=card names a card driver. Wrap.card sets up one board of it on a
 * BoardInfo of its own (rtg.library never sees this board, so nothing else
 * draws on it or switches it) and starts a process that, MIRRORFPS times a
 * second, follows what the thunks recorded about the wrapped board: on a
 * new mode it sets the same mode on the mirror (ResolvePixelClock, SetClock,
 * SetGC, SetDAC, SetPanning), it passes palette changes on with
 * SetColorArray and the display state with SetSwitch and SetDisplay, and it
 * copies the visible part of the wrapped board's screen into the mirror's
 * video memory. The pixels are copied as they are, so the mirror has to
 * offer the same pixel format.
 */
#include <exec/memory.h>
#include <exec/execbase.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>
#include "wrap.h"

/* the BoardInfo functions the mirror calls, a6 = the mirror card's base */
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
#define MIRROR_BISIZE (sizeof(struct BoardInfo) + 8192)

struct Mirror {
	struct WrapBoard *src;     /* the wrapped board */
	struct Library *inner;     /* its card */
	struct WrapMirror *m;
	struct BoardInfo *bi;      /* the mirror's board */
	struct Library *card;      /* the mirror's card */
	struct ModeInfo mode;      /* the mirror's mode */

	/* the wrapped board's screen, from the last snapshot */
	ULONG serial, pal_serial;
	UBYTE *src_mem;            /* top left visible pixel */
	ULONG src_bpr;
	UWORD w, h, bm_width;
	ULONG fmt, bpp;
	UWORD on;
	BOOL  ok;                  /* the mirror shows this mode: copy */
};

static struct Mirror mirror;

/*
 * A BoardInfo as rtg.library hands one to FindCard: library bases, the
 * lists and the lock initialised, every function an rts (some drivers call
 * the functions they leave alone, and a board nobody draws on needs none).
 */
static struct BoardInfo *new_boardinfo(struct Library *card)
{
	struct BoardInfo *bi = AllocMem(MIRROR_BISIZE, MEMF_PUBLIC | MEMF_CLEAR);
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
	bi->BoardNum = 1;
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
	FreeMem(bi, MIRROR_BISIZE);
}

static void set_state(struct Mirror *mr, UWORD on)
{
	struct BoardInfo *bi = mr->bi;

	((SetStateFn)bi->SetSwitch)(bi, on, mr->card);
	((SetStateFn)bi->SetDisplay)(bi, on, mr->card);
	mr->m->display = on;
}

static void set_palette(struct Mirror *mr)
{
	struct BoardInfo *bi = mr->bi;
	int i;

	for (i = 0; i < 256; i++)
		bi->CLUT[i] = mr->src->bi->CLUT[i];
	((SetColorArrayFn)bi->SetColorArray)(bi, 0, 256, mr->card);
}

/* the wrapped board shows a new mode: the same on the mirror */
static void set_mode(struct Mirror *mr, struct WrapDisplay *d)
{
	struct BoardInfo *bi = mr->bi;
	struct BoardInfo *sbi = mr->src->bi;
	struct WrapMirror *m = mr->m;
	ULONG bpr;

	mr->ok = FALSE;
	mr->w = d->width;
	mr->h = d->height;
	mr->bm_width = d->bm_width;
	mr->fmt = d->format;
	mr->bpp = wrap_bytesperpixel(d->format);
	if (!mr->w || !mr->h || !mr->bpp || !d->mem)
		return;
	if (!(bi->RGBFormats & RGBFF(mr->fmt))) {
		m->error = WRAP_MERR_FORMAT;
		return;
	}

	/* the wrapped card's rows, through its thunk, which sets its a6 */
	mr->src_bpr = ((BytesPerRowFn)sbi->CalculateBytesPerRow)(sbi, mr->bm_width, mr->fmt,
	                                                         mr->inner);
	mr->src_mem = d->mem + d->yoff * mr->src_bpr + d->xoff * mr->bpp;

	mr->mode = d->mode;
	mr->mode.Node.ln_Succ = mr->mode.Node.ln_Pred = NULL;
	bi->ModeInfo = &mr->mode;
	bi->RGBFormat = mr->fmt;
	((ResolvePixelClockFn)bi->ResolvePixelClock)(bi, &mr->mode, mr->mode.PixelClock, mr->fmt,
	                                             mr->card);
	((SetClockFn)bi->SetClock)(bi, mr->card);
	((SetGCFn)bi->SetGC)(bi, &mr->mode, 0, mr->card);
	((SetDACFn)bi->SetDAC)(bi, mr->fmt, mr->card);
	bpr = ((BytesPerRowFn)bi->CalculateBytesPerRow)(bi, mr->w, mr->fmt, mr->card);
	if (!bpr || bpr * mr->h > bi->MemorySize) {
		m->error = WRAP_MERR_TOOBIG;
		return;
	}
	m->mem = bi->MemoryBase;
	m->bpr = bpr;
	((SetPanningFn)bi->SetPanning)(bi, m->mem, mr->w, 0, 0, mr->fmt, mr->card);
	if (mr->fmt == RGBFB_CLUT)
		set_palette(mr);
	m->width = mr->w;
	m->height = mr->h;
	m->format = mr->fmt;
	m->setups++;
	m->error = WRAP_MERR_NONE;
	mr->ok = TRUE;
}

static void mirror_pass(struct Mirror *mr)
{
	struct WrapDisplay d;
	struct WrapMirror *m = mr->m;
	UWORD on;
	UBYTE *src, *dst;
	ULONG len, y;

	Forbid();
	d = mr->src->disp;
	Permit();

	if (d.serial != mr->serial) {
		mr->serial = d.serial;
		if (d.width != mr->w || d.height != mr->h || d.format != mr->fmt ||
		    d.bm_width != mr->bm_width || !mr->ok)
			set_mode(mr, &d);
		else
			mr->src_mem = d.mem + d.yoff * mr->src_bpr + d.xoff * mr->bpp;
		on = d.display && d.moniswitch && mr->ok;
		if (on != m->display)
			set_state(mr, on);
	}
	if (!mr->ok || !m->display)
		return;
	if (d.pal_serial != mr->pal_serial) {
		mr->pal_serial = d.pal_serial;
		if (mr->fmt == RGBFB_CLUT)
			set_palette(mr);
	}

	src = mr->src_mem;
	dst = m->mem;
	len = mr->w * mr->bpp;
	for (y = 0; y < mr->h; y++) {
		CopyMem(src, dst, len);
		src += mr->src_bpr;
		dst += m->bpr;
	}
	m->frames++;
}

static void MirrorMain(void)
{
	struct Task *me = FindTask(NULL);
	struct Mirror *mr;
	ULONG ticks;

	/* the parent sets tc_UserData right after creating us */
	while (!(mr = me->tc_UserData))
		Delay(1);
	ticks = TICKS_PER_SECOND / mr->m->fps;
	if (!ticks)
		ticks = 1;
	mr->serial = mr->src->disp.serial - 1;
	while (!(SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)) {
		mirror_pass(mr);
		Delay(ticks);
	}
	set_state(mr, 0);
	Forbid();
	mr->m->task = NULL;
}

/*
 * From Wrap.card's InitCard, for the first wrapped board, in rtg.library's
 * process. A mirror that cannot be set up leaves the wrapped board alone.
 */
void wrap_mirror_start(struct WrapBoard *b, struct Library *inner, struct WrapMirror *m)
{
	struct Mirror *mr = &mirror;
	struct BoardInfo *bi;
	struct Process *p;

	mr->src = b;
	mr->inner = inner;
	mr->m = m;
	mr->card = wrap_open_card(m->card);
	if (!mr->card) {
		m->error = WRAP_MERR_OPEN;
		return;
	}
	bi = new_boardinfo(mr->card);
	if (!bi) {
		m->error = WRAP_MERR_NOMEM;
		CloseLibrary(mr->card);
		return;
	}
	if (!InnerFindCard(bi, mr->card)) {
		m->error = WRAP_MERR_NOBOARD;
		free_boardinfo(bi);
		CloseLibrary(mr->card);
		return;
	}
	/* a board that was found cannot be given back: from here on it stays */
	m->lib = mr->card;
	if (!InnerInitCard(bi, NULL, mr->card)) {
		m->error = WRAP_MERR_INIT;
		return;
	}
	mr->bi = bi;
	m->bi = bi;

	Forbid();
	p = CreateNewProcTags(NP_Entry, (ULONG)MirrorMain,
	                      NP_Name, (ULONG)"Wrap.card mirror",
	                      NP_Priority, -1,
	                      NP_StackSize, 4096,
	                      TAG_END);
	if (p) {
		p->pr_Task.tc_UserData = mr;
		m->task = p;
	}
	Permit();
	if (!p)
		m->error = WRAP_MERR_NOTASK;
}
