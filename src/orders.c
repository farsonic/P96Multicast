/*
 * P96Multicast - what is drawn on the screen, for the sender.
 *
 * BLITTER=1: rtg.library hands its drawing to the card's blitter functions
 * when a card has them (on AROS, p96gfx.hidd does so for every bitmap in
 * video memory). Each one (a stub in libstart.s) puts what it is about to
 * draw on the order ring, if it draws on the visible screen, and goes on
 * to rtg.library's own software version in the matching ...Default slot.
 * On P96 that draws; on AROS it declines and p96gfx.hidd draws in
 * software. Either way the picture is the same as without.
 *
 * Fills, copies within the screen, inverted rectangles, text and patterns
 * become orders: the sender draws them into its shadow and sends them, a
 * few bytes each. Anything else that draws on the screen (lines, planar
 * blits, masked or unusual blits) is put on the ring as a rectangle the
 * sender compares and sends as pixels. The sender's background comparing
 * finds what nobody calls a function for.
 *
 * Wrap.card's heads call mc_record() through McRecord (libstart.s) with
 * the wrapped card's calls, and mc_damage_rect() through McDamage with
 * what they copied.
 *
 * POINTER=1: the hardware pointer functions keep the pointer's shape,
 * colours and position for the sender, which sends them to the viewers to
 * draw: rtg.library then never draws the pointer into the screen.
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

/* the largest template kept as an order, in bytes */
#define MAX_TEXT 16384

/*
 * Where each function finds its destination and rectangle (register
 * numbers into the saved d0-d7/a0-a6).
 */
struct BlitDesc {
	UWORD slot;      /* offset of the function in struct BoardInfo */
	UWORD fallback;  /* ... and of rtg.library's version */
	UBYTE ri;        /* the destination RenderInfo */
	UBYTE x, y, w, h;
};

static const struct BlitDesc descs[MC_BLIT_KINDS] = {
	/* FillRect(a0 bi, a1 ri, d0 x, d1 y, d2 w, d3 h, d4 pen, d5 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, FillRect), offsetof(struct BoardInfo, FillRectDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3) },
	/* InvertRect(a0 bi, a1 ri, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, InvertRect), offsetof(struct BoardInfo, InvertRectDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3) },
	/* BlitRect(a0 bi, a1 ri, d0 sx, d1 sy, d2 dx, d3 dy, d4 w, d5 h, d6 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitRect), offsetof(struct BoardInfo, BlitRectDefault),
	  REG_A(1), REG_D(2), REG_D(3), REG_D(4), REG_D(5) },
	/* BlitTemplate(a0 bi, a1 ri, a2 template, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitTemplate), offsetof(struct BoardInfo, BlitTemplateDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3) },
	/* BlitPattern(a0 bi, a1 ri, a2 pattern, d0 x, d1 y, d2 w, d3 h, d4 mask, d7 fmt) */
	{ offsetof(struct BoardInfo, BlitPattern), offsetof(struct BoardInfo, BlitPatternDefault),
	  REG_A(1), REG_D(0), REG_D(1), REG_D(2), REG_D(3) },
	/* DrawLine(a0 bi, a1 ri, a2 line, d0 mask, d7 fmt): the rectangle from the line */
	{ offsetof(struct BoardInfo, DrawLine), offsetof(struct BoardInfo, DrawLineDefault),
	  REG_A(1), NONE, NONE, NONE, NONE },
	/* BlitRectNoMaskComplete(a0 bi, a1 src ri, a2 dst ri, d0 sx, d1 sy, d2 dx, d3 dy,
	   d4 w, d5 h, d6 opcode (BLIT_SRC = 0xc), d7 fmt) */
	{ offsetof(struct BoardInfo, BlitRectNoMaskComplete),
	  offsetof(struct BoardInfo, BlitRectNoMaskCompleteDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5) },
	/* BlitPlanar2Chunky(a0 bi, a1 bitmap, a2 ri, d0 sx, d1 sy, d2 dx, d3 dy, d4 w, d5 h,
	   d6 minterm, d7 mask) */
	{ offsetof(struct BoardInfo, BlitPlanar2Chunky),
	  offsetof(struct BoardInfo, BlitPlanar2ChunkyDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5) },
	/* BlitPlanar2Direct(a0 bi, a1 bitmap, a2 ri, a3 colour map, d0 sx, d1 sy, d2 dx,
	   d3 dy, d4 w, d5 h, d6 minterm, d7 mask) */
	{ offsetof(struct BoardInfo, BlitPlanar2Direct),
	  offsetof(struct BoardInfo, BlitPlanar2DirectDefault),
	  REG_A(2), REG_D(2), REG_D(3), REG_D(4), REG_D(5) },
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
 * Room for a record of len bytes (a multiple of 4) on the ring, under
 * Forbid(); the caller fills it in and calls put(). NULL when the ring is
 * full: the sender then compares everything.
 */
static struct McRec *reserve(struct McCard *mc, ULONG len)
{
	ULONG head = mc->ring_head;
	ULONG at = head & (MC_RING_SIZE - 1);

	if (at + len > MC_RING_SIZE) {
		/* the record would wrap: pad to the end, start at 0 */
		ULONG pad = MC_RING_SIZE - at;

		if (head + pad + len - mc->ring_tail > MC_RING_SIZE)
			goto full;
		((struct McRec *)(mc->ring + at))->len = pad;
		((struct McRec *)(mc->ring + at))->type = MC_R_PAD;
		mc->ring_head = head += pad;
		at = 0;
	}
	if (head + len - mc->ring_tail > MC_RING_SIZE)
		goto full;
	return (struct McRec *)(mc->ring + at);
full:
	if (!mc->ring_lost && mc->boardnum == 0)
		mc_status->ring_lost++;
	mc->ring_lost = 1;
	return NULL;
}

static void put(struct McCard *mc, struct McRec *r, ULONG len, UBYTE type, UBYTE mode,
                LONG x, LONG y, LONG w, LONG h)
{
	r->len = len;
	r->type = type;
	r->mode = mode;
	r->x = x;
	r->y = y;
	r->w = w;
	r->h = h;
	mc->ring_head += len;
}

/* a rectangle on the visible screen (screen coordinates) the sender compares */
void mc_damage_rect(struct McCard *mc, LONG x, LONG y, LONG w, LONG h)
{
	struct McRec *r;

	if (!mc->ring || w <= 0 || h <= 0)
		return;
	Forbid();
	if ((r = reserve(mc, sizeof(*r))) != NULL)
		put(mc, r, sizeof(*r), MC_R_DAMAGE, 0, x, y, w, h);
	Permit();
}

/*
 * A drawing call, before it draws. regs holds d0-d7/a0-a6 as rtg.library
 * passed them. Called by the stubs in the drawing task, and by Wrap.card.
 */
void mc_record(struct McCard *mc, ULONG kind, ULONG *regs)
{
	const struct BlitDesc *d = &descs[kind];
	struct RenderInfo *ri = (struct RenderInfo *)regs[d->ri];
	BOOL first = mc->boardnum == 0;
	BOOL clut = mc->format == RGBFB_CLUT;
	struct McRec *r;
	LONG ox, oy, x, y, w, h;

	if (kind >= MC_BLIT_KINDS)
		return;
	if (first)
		mc_status->blit_calls[kind]++;
	if (!mc->ring || !on_screen(mc, ri, &ox, &oy))
		return;

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
	/* nothing of it on the visible screen */
	if (w <= 0 || h <= 0 || x >= (LONG)mc->width || y >= (LONG)mc->height || x + w <= 0 ||
	    y + h <= 0)
		return;
	if (first)
		mc_status->blit_screen[kind]++;

	Forbid();
	switch (kind) {
	case MC_BLIT_FILL:
		if (clut && (regs[REG_D(5)] & 0xff) != 0xff)
			goto damage;
		if ((r = reserve(mc, sizeof(struct McFill))) != NULL) {
			((struct McFill *)r)->pen = regs[REG_D(4)];
			put(mc, r, sizeof(struct McFill), MC_R_FILL, 0, x, y, w, h);
		}
		break;

	case MC_BLIT_INVERT:
		if (clut && (regs[REG_D(4)] & 0xff) != 0xff)
			goto damage;
		if ((r = reserve(mc, sizeof(struct McRec))) != NULL)
			put(mc, r, sizeof(struct McRec), MC_R_INVERT, 0, x, y, w, h);
		break;

	case MC_BLIT_RECT:
		/* within the one RenderInfo */
		if (clut && (regs[REG_D(6)] & 0xff) != 0xff)
			goto damage;
		if ((r = reserve(mc, sizeof(struct McCopy))) != NULL) {
			((struct McCopy *)r)->sx = reg_word(regs, REG_D(0)) + ox;
			((struct McCopy *)r)->sy = reg_word(regs, REG_D(1)) + oy;
			put(mc, r, sizeof(struct McCopy), MC_R_COPY, 0, x, y, w, h);
		}
		break;

	case MC_BLIT_COMPLETE: {
		LONG sx, sy;

		/* a plain copy: P96's opcode BLIT_SRC (0xc), or the minterm 0xc0 */
		if (((regs[REG_D(6)] & 0xff) != 0x0c && (regs[REG_D(6)] & 0xff) != 0xc0) ||
		    !on_screen(mc, (struct RenderInfo *)regs[REG_A(1)], &sx, &sy))
			goto damage;
		if ((r = reserve(mc, sizeof(struct McCopy))) != NULL) {
			((struct McCopy *)r)->sx = reg_word(regs, REG_D(0)) + sx;
			((struct McCopy *)r)->sy = reg_word(regs, REG_D(1)) + sy;
			put(mc, r, sizeof(struct McCopy), MC_R_COPY, 0, x, y, w, h);
		}
		break;
	}

	case MC_BLIT_TEMPLATE: {
		struct Template *t = (struct Template *)regs[REG_A(2)];
		ULONG stride, size, len, i;
		UBYTE *from, *to;

		if (!t || (clut && (regs[REG_D(4)] & 0xff) != 0xff))
			goto damage;
		stride = (t->XOffset + w + 7) >> 3;
		size = stride * h;
		if (size > MAX_TEXT)
			goto damage;
		len = (sizeof(struct McText) + size + 3) & ~3UL;
		if ((r = reserve(mc, len)) != NULL) {
			struct McText *m = (struct McText *)r;

			m->bitx = t->XOffset;
			m->stride = stride;
			m->fg = t->FgPen;
			m->bg = t->BgPen;
			from = t->Memory;
			to = (UBYTE *)(m + 1);
			for (i = 0; i < h; i++, from += t->BytesPerRow, to += stride)
				CopyMem(from, to, stride);
			put(mc, r, len, MC_R_TEXT, t->DrawMode, x, y, w, h);
		}
		break;
	}

	case MC_BLIT_PATTERN: {
		struct Pattern *p = (struct Pattern *)regs[REG_A(2)];
		ULONG rows, len;

		if (!p || p->Size > 8 || (clut && (regs[REG_D(4)] & 0xff) != 0xff))
			goto damage;
		rows = 1UL << p->Size;
		len = (sizeof(struct McPattern) + 2 * rows + 3) & ~3UL;
		if ((r = reserve(mc, len)) != NULL) {
			struct McPattern *m = (struct McPattern *)r;

			m->rowmask = rows - 1;
			m->px = p->XOffset & 15;
			m->py = p->YOffset & (rows - 1);
			m->pad = 0;
			m->fg = p->FgPen;
			m->bg = p->BgPen;
			CopyMem(p->Memory, m + 1, 2 * rows);
			put(mc, r, len, MC_R_PATTERN, p->DrawMode, x, y, w, h);
		}
		break;
	}

	default:
	damage:
		if ((r = reserve(mc, sizeof(*r))) != NULL)
			put(mc, r, sizeof(*r), MC_R_DAMAGE, 0, x, y, w, h);
		break;
	}
	Permit();
}

/*
 * Called by each stub, in the drawing task, before the drawing is done.
 * Returns rtg.library's version of the function, which the stub goes on to
 * with every register as it was.
 */
APTR mc_damage(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG kind,
               __reg("a1") ULONG *regs)
{
	mc_record(MC_CARD(bi), kind, regs);
	return SLOT(bi, descs[kind].fallback);
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

/* ---------------------------------------------------------------- the pointer */

/*
 * The pointer as a board shows it (the card's own, or a board Wrap.card
 * wraps): its image is in Amiga sprite format, two control words and then
 * per row plane 0 and plane 1, (MouseWidth + 15) / 16 words each, and
 * MouseX, MouseY are its top left on the bitmap.
 */
void mc_pointer_image(struct McCard *mc, struct BoardInfo *bi)
{
	UWORD w = bi->MouseWidth, h = bi->MouseHeight, words, i;
	UWORD *from = bi->MouseImage;

	if (!from)
		return;
	if (w > MC_PTR_MAXW)
		w = MC_PTR_MAXW;
	if (h > MC_PTR_MAXH)
		h = MC_PTR_MAXH;
	words = (w + 15) / 16;
	from += 2;
	Forbid();
	for (i = 0; i < h * 2 * words; i++)
		mc->ptr_data[i] = from[i];
	mc->ptr_w = w;
	mc->ptr_h = h;
	mc->ptr_hx = -(BYTE)bi->MouseXOffset;
	mc->ptr_hy = -(BYTE)bi->MouseYOffset;
	mc->ptr_serial++;
	Permit();
}

void mc_pointer_pos(struct McCard *mc, struct BoardInfo *bi)
{
	mc->ptr_x = bi->MouseX;
	mc->ptr_y = bi->MouseY;
	mc->pos_serial++;
	if (mc->boardnum == 0)
		mc_status->pointer_moves++;
}

void mc_pointer_colour(struct McCard *mc, ULONG index, ULONG r, ULONG g, ULONG b)
{
	if (index > 2)
		return;
	mc->ptr_rgb[index * 3] = r;
	mc->ptr_rgb[index * 3 + 1] = g;
	mc->ptr_rgb[index * 3 + 2] = b;
	mc->ptr_serial++;
}

void mc_pointer_show(struct McCard *mc, ULONG on)
{
	mc->ptr_on = on ? 1 : 0;
	mc->pos_serial++;
	if (mc->boardnum == 0)
		mc_status->pointer = mc->ptr_on;
}

static BOOL SetSprite(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG on,
                      __reg("d7") RGBFTYPE fmt)
{
	mc_pointer_show(MC_CARD(bi), on & 0xffff);
	return TRUE;
}

static void SetSpritePosition(__reg("a0") struct BoardInfo *bi, __reg("d0") WORD x,
                              __reg("d1") WORD y, __reg("d7") RGBFTYPE fmt)
{
	mc_pointer_pos(MC_CARD(bi), bi);
}

static void SetSpriteImage(__reg("a0") struct BoardInfo *bi, __reg("d7") RGBFTYPE fmt)
{
	mc_pointer_image(MC_CARD(bi), bi);
}

static void SetSpriteColor(__reg("a0") struct BoardInfo *bi, __reg("d0") UBYTE index,
                           __reg("d1") UBYTE r, __reg("d2") UBYTE g, __reg("d3") UBYTE b,
                           __reg("d7") RGBFTYPE fmt)
{
	mc_pointer_colour(MC_CARD(bi), index, r, g, b);
}

/* InitCard, with POINTER=1: a hardware pointer in every format */
void mc_pointer_install(struct McCard *mc)
{
	struct BoardInfo *bi = mc->bi;

	bi->SetSprite = (APTR)SetSprite;
	bi->SetSpritePosition = (APTR)SetSpritePosition;
	bi->SetSpriteImage = (APTR)SetSpriteImage;
	bi->SetSpriteColor = (APTR)SetSpriteColor;
	bi->Flags |= BIF_HARDWARESPRITE;
	bi->SoftSpriteFlags = 0;
}

/* ---------------------------------------------------------------- for Wrap.card */

/*
 * Library functions for Wrap.card's heads (see WRAP_MC_* in wrap.h). bi is
 * the head's BoardInfo; regs the registers of a call to the wrapped card,
 * with its BoardInfo in a0.
 */
void McRecord(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG kind,
              __reg("a1") ULONG *regs)
{
	if (bi && MC_CARD(bi))
		mc_record(MC_CARD(bi), kind, regs);
}

void McDamage(__reg("a0") struct BoardInfo *bi, __reg("d0") LONG x, __reg("d1") LONG y,
              __reg("d2") LONG w, __reg("d3") LONG h)
{
	if (bi && MC_CARD(bi)) {
		if (MC_CARD(bi)->boardnum == 0)
			mc_status->blit_calls[MC_BLIT_DAMAGE]++;
		mc_damage_rect(MC_CARD(bi), x, y, w, h);
	}
}

/* what: 0 SetSprite, 1 SetSpritePosition, 2 SetSpriteImage, 3 SetSpriteColor */
void McPointer(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG what,
               __reg("a1") ULONG *regs)
{
	struct McCard *mc = bi ? MC_CARD(bi) : NULL;
	struct BoardInfo *from = (struct BoardInfo *)regs[REG_A(0)];

	if (!mc)
		return;
	switch (what) {
	case 0:
		mc_pointer_show(mc, regs[REG_D(0)] & 0xffff);
		break;
	case 1:
		mc_pointer_pos(mc, from);
		break;
	case 2:
		mc_pointer_image(mc, from);
		break;
	case 3:
		mc_pointer_colour(mc, regs[REG_D(0)] & 0xff, regs[REG_D(1)] & 0xff,
		                  regs[REG_D(2)] & 0xff, regs[REG_D(3)] & 0xff);
		break;
	}
}
