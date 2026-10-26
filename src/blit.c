/*
 * P96Multicast - BLITTER=1: hear about the drawing.
 *
 * Nothing tells a frame buffer card what changed, so the sender compares the
 * whole screen with a shadow copy every pass. But rtg.library hands much of
 * its drawing to the card's blitter functions when a card has them (AROS's
 * p96gfx.hidd calls them for every bitmap in video memory). With BLITTER=1
 * the card has them: each one (a stub in libstart.s) puts the rectangle it
 * is about to change on the damage ring, if it is on the visible screen,
 * and then goes on to rtg.library's own software version in the matching
 * ...Default slot. On P96 that draws; on AROS it declines and p96gfx.hidd
 * draws in software. Either way the picture is the same as without.
 *
 * For now the sender only counts with it (mctest STATUS): how many of the
 * tiles it finds changed a recorded call had touched. That says how far
 * drawing calls can replace comparing, and which calls matter.
 */
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stddef.h>
#include "mcard.h"

/* libstart.s: one stub per MC_BLIT_* kind, 8 bytes each */
extern char DmgStubs[];

#define STUB_SIZE 8
#define REG_D(n) (n)
#define REG_A(n) (8 + (n))
#define NONE 0xff

/*
 * Where each function finds its destination and rectangle (register
 * numbers into the saved d0-d7/a0-a6), and its source if it copies.
 */
struct BlitDesc {
	UWORD slot;      /* offset of the function in struct BoardInfo */
	UWORD fallback;  /* ... and of rtg.library's version */
	UBYTE ri;        /* the destination RenderInfo */
	UBYTE x, y, w, h;
	UBYTE src_ri, sx, sy;  /* a copy's source, or NONE */
};

static const struct BlitDesc descs[MC_BLIT_KINDS] = {
	/* FillRect(a0 bi, a1 ri, d0 x, d1 y, d2 w, d3 h, d4 pen, d5 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, FillRect), offsetof(struct BoardInfo, FillRectDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3), NONE, NONE, NONE },
	/* InvertRect(a0 bi, a1 ri, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, InvertRect), offsetof(struct BoardInfo, InvertRectDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3), NONE, NONE, NONE },
	/* BlitRect(a0 bi, a1 ri, d0 sx, d1 sy, d2 dx, d3 dy, d4 w, d5 h, d6 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitRect), offsetof(struct BoardInfo, BlitRectDefault),
	  REG_A(1), REG_D(2), REG_D(3), REG_D(4), REG_D(5), REG_A(1), REG_D(0), REG_D(1) },
	/* BlitTemplate(a0 bi, a1 ri, a2 template, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitTemplate), offsetof(struct BoardInfo, BlitTemplateDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3), NONE, NONE, NONE },
	/* BlitPattern(a0 bi, a1 ri, a2 pattern, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitPattern), offsetof(struct BoardInfo, BlitPatternDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3), NONE, NONE, NONE },
	/* DrawLine(a0 bi, a1 ri, a2 line, d0 mask, d7 fmt): the rectangle from the line */
	{ offsetof(struct BoardInfo, DrawLine), offsetof(struct BoardInfo, DrawLineDefault),
	  REG_A(1), NONE, NONE, NONE, NONE, NONE, NONE, NONE },
	/* BlitRectNoMaskComplete(a0 bi, a1 src ri, a2 dst ri, d0 sx, d1 sy, d2 dx, d3 dy,
	   d4 w, d5 h, d6 minterm, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitRectNoMaskComplete),
	  offsetof(struct BoardInfo, BlitRectNoMaskCompleteDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5), REG_A(1), REG_D(0), REG_D(1) },
	/* BlitPlanar2Chunky(a0 bi, a1 bitmap, a2 ri, d0 sx, d1 sy, d2 dx, d3 dy, d4 w, d5 h,
	   d6 minterm, d7 mask) */
	{ offsetof(struct BoardInfo, BlitPlanar2Chunky),
	  offsetof(struct BoardInfo, BlitPlanar2ChunkyDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5), NONE, NONE, NONE },
	/* BlitPlanar2Direct(a0 bi, a1 bitmap, a2 ri, a3 colour map, d0 sx, d1 sy, d2 dx,
	   d3 dy, d4 w, d5 h, d6 minterm, d7 mask) */
	{ offsetof(struct BoardInfo, BlitPlanar2Direct),
	  offsetof(struct BoardInfo, BlitPlanar2DirectDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5), NONE, NONE, NONE },
};

#define SLOT(bi, off) (*(APTR *)((UBYTE *)(bi) + (off)))

/*
 * Where a RenderInfo's pixel 0,0 is on the visible screen, if its memory is
 * the displayed bitmap (or a window into it). FALSE for anything else:
 * offscreen bitmaps in video memory.
 */
static BOOL on_screen(struct McCard *mc, struct RenderInfo *ri, LONG *ox, LONG *oy)
{
	UBYTE *bm = mc->bm_mem;
	ULONG bpp = mc_bytesperpixel(mc->format);
	ULONG bpr, off;

	if (!ri || !bm || !bpp || !mc->display)
		return FALSE;
	bpr = mc_bytesperrow(mc->bm_width > mc->width ? mc->bm_width : mc->width, mc->format);
	if ((UBYTE *)ri->Memory < bm || ri->BytesPerRow != (WORD)bpr)
		return FALSE;
	off = (UBYTE *)ri->Memory - bm;
	if (off >= bpr * (ULONG)(mc->yoff + mc->height))
		return FALSE;
	*ox = (LONG)(off % bpr) / bpp - mc->xoff;
	*oy = (LONG)(off / bpr) - mc->yoff;
	return TRUE;
}

static WORD reg_word(ULONG *regs, UBYTE r)
{
	return (WORD)(regs[r] & 0xffff);
}

/*
 * Called by each stub, in the drawing task, before the drawing is done.
 * Returns rtg.library's version of the function, which the stub goes on to
 * with every register as it was.
 */
APTR mc_damage(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG kind,
               __reg("a1") ULONG *regs)
{
	struct McCard *mc = MC_CARD(bi);
	const struct BlitDesc *d = &descs[kind];
	struct RenderInfo *ri = (struct RenderInfo *)regs[d->ri];
	BOOL first = mc->boardnum == 0;
	LONG ox, oy, x, y, w, h;

	if (first)
		mc_status->blit_calls[kind]++;
	if (!on_screen(mc, ri, &ox, &oy))
		return SLOT(bi, d->fallback);

	if (kind == MC_BLIT_LINE) {
		struct Line *l = (struct Line *)regs[REG_A(2)];

		x = l->dX < 0 ? l->X + l->dX : l->X;
		y = l->dY < 0 ? l->Y + l->dY : l->Y;
		w = (l->dX < 0 ? -l->dX : l->dX) + 1;
		h = (l->dY < 0 ? -l->dY : l->dY) + 1;
	} else {
		x = reg_word(regs, d->x);
		y = reg_word(regs, d->y);
		w = reg_word(regs, d->w);
		h = reg_word(regs, d->h);
	}
	x += ox;
	y += oy;
	/* clip to the visible screen */
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > mc->width)
		w = mc->width - x;
	if (y + h > mc->height)
		h = mc->height - y;
	if (w <= 0 || h <= 0)
		return SLOT(bi, d->fallback);

	if (first) {
		LONG sx, sy;

		mc_status->blit_screen[kind]++;
		if (d->src_ri != NONE &&
		    on_screen(mc, (struct RenderInfo *)regs[d->src_ri], &sx, &sy))
			mc_status->screen_copies++;
	}
	Forbid();
	{
		struct McRect *r = &mc->damage[mc->damage_head % MC_DAMAGE_RING];

		r->x = x;
		r->y = y;
		r->w = w;
		r->h = h;
		r->kind = kind;
		mc->damage_head++;
	}
	Permit();
	return SLOT(bi, d->fallback);
}

/* InitCard, with BLITTER=1: the stubs in the drawing functions' slots */
void mc_blitter_install(struct McCard *mc)
{
	struct BoardInfo *bi = mc->bi;
	int k;

	for (k = 0; k < MC_BLIT_KINDS; k++)
		SLOT(bi, descs[k].slot) = DmgStubs + STUB_SIZE * k;
	bi->Flags |= BIF_BLITTER;
	if (mc->boardnum == 0)
		mc_status->blitter = 1;
}
