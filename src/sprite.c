/*
 * Panorama - sprite.c: graphics.library's sprites on Panorama screens.
 *
 * A program asks for a sprite as on any Amiga: GetExtSprite() (or
 * GetSprite()), AllocSpriteData() for its image, ChangeExtSprite() (or
 * ChangeSprite()) to show it, MoveSprite() to move it, FreeSprite() when
 * done. On a screen of a Picasso96 board those reach no hardware and
 * nothing shows. With SPRITES=1 (the default) the card patches
 * GetExtSpriteA (to know a sprite's width), ChangeExtSpriteA,
 * ChangeSprite, MoveSprite and FreeSprite: for a
 * ViewPort on a Panorama board it keeps the sprite's image, colours and
 * place here, and the sender sends them (SPRITE, SPRITEPOS) for the
 * viewers to draw on top of the screen, as they do the pointer. Moving a
 * sprite then costs a few bytes, and no pixel of the screen changes.
 *
 * As the hardware's: sprites 1 to 7 (0 is Intuition's pointer, which the
 * card has from rtg.library already), 16, 32 or 64 pixels wide, any
 * height, colour 0 clear and colours 1 to 3 from the screen's colour map
 * (16 + 4 * (n / 2) + 1 ... for sprite n, or where SpriteBase_Even/Odd
 * put them). Unlike the hardware's: a sprite's place is in the screen's
 * pixels, whatever its mode, and attached pairs show as two sprites of 3
 * colours each. Any other ViewPort, and sprite 0, go on to graphics.library
 * as before.
 */
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/sprite.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include "mcard.h"

struct McSprite mc_sprite[MC_SPRITES];
struct McCard *mc_cards[MC_MAXBOARDS];
volatile ULONG mc_sprite_serial;

#define LVO_GetExtSpriteA    -930
#define LVO_FreeSprite       -414
#define LVO_ChangeSprite     -420
#define LVO_MoveSprite       -426
#define LVO_ChangeExtSpriteA -1026

typedef void (*FreeSpriteFn)(__reg("d0") WORD num, __reg("a6") struct GfxBase *gb);
typedef void (*ChangeSpriteFn)(__reg("a0") struct ViewPort *vp, __reg("a1") struct SimpleSprite *ss,
                               __reg("a2") APTR data, __reg("a6") struct GfxBase *gb);
typedef void (*MoveSpriteFn)(__reg("a0") struct ViewPort *vp, __reg("a1") struct SimpleSprite *ss,
                             __reg("d0") WORD x, __reg("d1") WORD y, __reg("a6") struct GfxBase *gb);
typedef LONG (*ChangeExtFn)(__reg("a0") struct ViewPort *vp, __reg("a1") struct ExtSprite *old,
                            __reg("a2") struct ExtSprite *new, __reg("a3") struct TagItem *tags,
                            __reg("a6") struct GfxBase *gb);

typedef LONG (*GetExtFn)(__reg("a2") struct ExtSprite *sp, __reg("a1") struct TagItem *tags,
                         __reg("a6") struct GfxBase *gb);

static GetExtFn old_get_ext;
static FreeSpriteFn old_free;
/* the ExtSprites GetExtSprite() handed out, by number: their width is known */
static struct ExtSprite *ext[MC_SPRITES];
static ChangeSpriteFn old_change;
static MoveSpriteFn old_move;
static ChangeExtFn old_change_ext;

/*
 * The board vp shows on, or NULL: on display there, one of its modes, or
 * (where the modes' IDs aren't the board's, as on AROS) its screen or
 * bitmap the one the sender found on the board. Nothing that may wait: this runs inside
 * graphics.library's calls, in the program's task.
 */
static struct McCard *card_of(struct ViewPort *vp)
{
	struct ColorMap *cm;
	struct LibResolution *r;
	ULONG id;
	int i;

	if (!vp)
		return NULL;
	cm = vp->ColorMap;
	id = cm ? cm->VPModeID & 0xffff0000UL : 0;
	for (i = 0; i < MC_MAXBOARDS; i++) {
		struct McCard *mc = mc_cards[i];
		struct BoardInfo *bi;

		if (!mc)
			continue;
		bi = mc->bi;
		if (bi->ViewPort == vp || (mc->our_scr && vp == &mc->our_scr->ViewPort))
			return mc;
		if (!id || id == 0xffff0000UL)
			continue;
		for (r = (struct LibResolution *)bi->ResolutionsList.mlh_Head; r->Node.ln_Succ;
		     r = (struct LibResolution *)r->Node.ln_Succ)
			if ((r->DisplayID & 0xffff0000UL) == id)
				return mc;
	}
	if (vp->RasInfo && vp->RasInfo->BitMap)
		for (i = 0; i < MC_MAXBOARDS; i++)
			if (mc_cards[i] && vp->RasInfo->BitMap == mc_cards[i]->our_bm)
				return mc_cards[i];
	return NULL;
}

/*
 * Sprite num's colours 1-3 from vp's colour map into rgb, then colours
 * 1-15 of an attached pair (FALSE: it has none). Not under Forbid():
 * GetRGB32() may wait for the colour map. One call for both in the usual
 * case (both banks at 16).
 */
static BOOL colours(struct ViewPort *vp, UWORD num, UBYTE *rgb)
{
	struct ColorMap *cm = vp->ColorMap;
	ULONG c[45], own, odd;
	int i;

	if (!cm)
		return FALSE;
	own = cm->Type >= COLORMAP_TYPE_V39 ? (num & 1 ? cm->SpriteBase_Odd : cm->SpriteBase_Even) : 16;
	odd = cm->Type >= COLORMAP_TYPE_V39 ? cm->SpriteBase_Odd : 16;
	if (odd + 16 > cm->Count || own + 16 > cm->Count)
		return FALSE;
	/* the attached pair's 15: its bank's 1-15 */
	GetRGB32(cm, odd + 1, 15, c);
	for (i = 0; i < 45; i++)
		rgb[9 + i] = c[i] >> 24;
	/* its own 3: of its pair's 4 */
	if (own != odd)
		GetRGB32(cm, own + 1, 15, c);
	for (i = 0; i < 9; i++)
		rgb[i] = c[(num >> 1) * 12 + i] >> 24;
	return TRUE;
}

/*
 * Sprite sp's colours, read once a frame at most (the programs move their
 * sprites each frame): into rgb, TRUE if read now
 */
static BOOL frame_colours(struct McSprite *sp, struct ViewPort *vp, UWORD num, UBYTE *rgb)
{
	ULONG vb = GfxBase->VBCounter;

	if (sp->mc && sp->vp == vp && sp->rgb_vb == vb)
		return FALSE;
	if (!colours(vp, num, rgb))
		return FALSE;
	sp->rgb_vb = vb;
	return TRUE;
}

/* under Forbid(): sp's colours are rgb now; TRUE if that is a change */
static BOOL set_colours(struct McSprite *sp, UBYTE *rgb)
{
	BOOL changed = FALSE;
	int i;

	for (i = 0; i < MC_SPRITE_RGB; i++)
		if (sp->rgb[i] != rgb[i]) {
			sp->rgb[i] = rgb[i];
			changed = TRUE;
		}
	return changed;
}

/* bits set in a byte */
static UBYTE bits_in(UBYTE b)
{
	UBYTE n = 0;

	for (; b; b &= b - 1)
		n++;
	return n;
}

/* the status's count of the image's pixels of colours 1, 2, 3 (for mctest) */
static void count_pixels(UWORD *data, UWORD words, UWORD h)
{
	static UBYTE table[256];
	ULONG n[3] = { 0, 0, 0 }, i;
	UWORD k;

	if (!table[255])
		for (i = 0; i < 256; i++)
			table[i] = bits_in(i);
	for (i = 0; i < h; i++)
		for (k = 0; k < words; k++) {
			UWORD p0 = data[i * 2 * words + k], p1 = data[i * 2 * words + words + k];
			UWORD c[3];
			int j;

			c[0] = p0 & ~p1;
			c[1] = ~p0 & p1;
			c[2] = p0 & p1;
			for (j = 0; j < 3; j++)
				n[j] += table[c[j] >> 8] + table[c[j] & 255];
		}
	for (i = 0; i < 3; i++)
		mc_status->sprite_pixels[i] = n[i];
}

/*
 * Sprite num on vp gets a new image: words words of each plane a row, h
 * rows after the control words at posctl (as many as a row has), each row
 * plane 0 then plane 1. FALSE: not ours. A
 * spinning ball changes images every frame: its buffer is kept when the
 * new image fits, and nothing slow happens here.
 */
static BOOL take_image(struct ViewPort *vp, UWORD num, UWORD words, UWORD h, UWORD *posctl)
{
	UWORD *data = posctl ? posctl + 2 * words : NULL;
	/* an odd sprite attached to the even one before it: one of 15 colours */
	UWORD attached = posctl && (num & 1) && (posctl[words] & SPRITE_ATTACHED);
	struct McCard *mc;
	struct McSprite *sp;
	UWORD *fresh = NULL, *old = NULL;
	UBYTE rgb[MC_SPRITE_RGB];
	BOOL have_rgb;
	ULONG n;

	if (num == 0 || num >= MC_SPRITES)
		return FALSE;
	sp = &mc_sprite[num];
	mc = sp->mc && sp->vp == vp ? sp->mc : card_of(vp);
	if (!mc)
		return FALSE;
	if (words != 1 && words != 2 && words != 4)
		words = 1;
	if (h > MC_SPRITE_MAXH)
		h = MC_SPRITE_MAXH;
	n = (ULONG)h * 2 * words;
	if (!data)
		n = 0;
	have_rgb = frame_colours(sp, vp, num, rgb);
	if (n > sp->cap) {
		fresh = AllocVec(n * 2, MEMF_PUBLIC);
		if (!fresh)
			n = 0;
	}
	Forbid();
	if (fresh) {
		old = sp->data;
		sp->data = fresh;
		sp->cap = n;
	}
	if (n) {
		/*
		 * Copied, and its fingerprint taken on the way (a long at a time):
		 * the sender knows an image the viewers have by it, without going
		 * over it again each pass
		 */
		ULONG *from = (ULONG *)data, *to = (ULONG *)sp->data, h = 0x9e3779b9UL, k;

		for (k = n / 2; k; k--) {
			ULONG v = *from++;

			*to++ = v;
			h = ((h << 5) | (h >> 27)) ^ v;
		}
		sp->hash = h;
	}
	sp->mc = mc;
	sp->vp = vp;
	sp->w = n ? words * 16 : 0;
	sp->h = n ? h : 0;
	sp->attached = attached;
	if (have_rgb)
		set_colours(sp, rgb);
	sp->serial++;
	sp->pos_serial++;
	mc_sprite_serial++;
	Permit();
	if (old)
		FreeVec(old);
	if (mc->boardnum == 0) {
		mc_status->sprite_changes++;
		/* now and then: counting takes time */
		if ((mc_status->sprite_changes & 63) == 1 && n) {
			mc_status->sprite_num = num;
			mc_status->sprite_w = words * 16;
			mc_status->sprite_h = h;
			count_pixels(data, words, h);
		}
	}
	return TRUE;
}

static LONG new_ChangeExtSpriteA(__reg("a0") struct ViewPort *vp, __reg("a1") struct ExtSprite *old,
                                 __reg("a2") struct ExtSprite *new, __reg("a3") struct TagItem *tags,
                                 __reg("a6") struct GfxBase *gb)
{
	/* the sprite engine old has is the one that changes */
	UWORD num = old ? old->es_SimpleSprite.num : new ? new->es_SimpleSprite.num : 0;

	mc_status->sprite_calls++;
	mc_status->sprite_lastnum = num;
	if (new && new->es_SimpleSprite.posctldata) {
		UWORD words = new->es_wordwidth ? new->es_wordwidth : 1;

		if (take_image(vp, num, words, new->es_SimpleSprite.height,
		               new->es_SimpleSprite.posctldata)) {
			/* new takes over old's number and place, as graphics.library's does */
			new->es_SimpleSprite.num = num;
			if (old && old != new) {
				new->es_SimpleSprite.x = old->es_SimpleSprite.x;
				new->es_SimpleSprite.y = old->es_SimpleSprite.y;
				if (ext[num] == old)
					ext[num] = new;
			}
			return 1;
		}
	}
	return old_change_ext(vp, old, new, tags, gb);
}

static LONG new_GetExtSpriteA(__reg("a2") struct ExtSprite *sp, __reg("a1") struct TagItem *tags,
                              __reg("a6") struct GfxBase *gb)
{
	LONG num = old_get_ext(sp, tags, gb);
	struct TagItem *t;

	if (num > 0 && num < MC_SPRITES)
		ext[num] = sp;
	/* GSTAG_ATTACHED: the pair's second, numbered by graphics.library */
	for (t = tags; num >= 0 && t && t->ti_Tag != TAG_DONE; t++) {
		if (t->ti_Tag == TAG_MORE) {
			t = (struct TagItem *)t->ti_Data - 1;
			continue;
		}
		if (t->ti_Tag == TAG_IGNORE || t->ti_Tag == TAG_SKIP)
			continue;
		if (t->ti_Tag == GSTAG_ATTACHED && t->ti_Data) {
			struct ExtSprite *two = (struct ExtSprite *)t->ti_Data;

			if (two->es_SimpleSprite.num > 0 && two->es_SimpleSprite.num < MC_SPRITES)
				ext[two->es_SimpleSprite.num] = two;
		}
	}
	return num;
}

static void new_ChangeSprite(__reg("a0") struct ViewPort *vp, __reg("a1") struct SimpleSprite *ss,
                             __reg("a2") APTR data, __reg("a6") struct GfxBase *gb)
{
	if (ss && data && take_image(vp, ss->num, 1, ss->height, (UWORD *)data))
		return;
	old_change(vp, ss, data, gb);
}

static void new_MoveSprite(__reg("a0") struct ViewPort *vp, __reg("a1") struct SimpleSprite *ss,
                           __reg("d0") WORD x, __reg("d1") WORD y, __reg("a6") struct GfxBase *gb)
{
	struct McSprite *sp;

	mc_status->sprite_calls++;
	if (ss)
		mc_status->sprite_lastnum = ss->num;
	if (ss && ss->num > 0 && ss->num < MC_SPRITES) {
		sp = &mc_sprite[ss->num];
		/*
		 * Shown with GetExtSprite() or GetSprite() and moved, never
		 * changed: its image is the one it has
		 */
		if (!(sp->mc && sp->vp == vp) && ss->posctldata) {
			UWORD words = ext[ss->num] == (struct ExtSprite *)ss ? ext[ss->num]->es_wordwidth : 1;

			if (!words)
				words = 1;
			take_image(vp, ss->num, words, ss->height, ss->posctldata);
		}
		if (sp->mc && sp->vp == vp) {
			UBYTE rgb[MC_SPRITE_RGB];
			BOOL have_rgb = frame_colours(sp, vp, ss->num, rgb);

			Forbid();
			ss->x = x;
			ss->y = y;
			sp->x = x;
			sp->y = y;
			sp->on = 1;
			if (have_rgb && set_colours(sp, rgb))
				sp->serial++;
			sp->pos_serial++;
			mc_sprite_serial++;
			Permit();
			if (sp->mc->boardnum == 0)
				mc_status->sprite_moves++;
			return;
		}
	}
	old_move(vp, ss, x, y, gb);
}

static void new_FreeSprite(__reg("d0") WORD num, __reg("a6") struct GfxBase *gb)
{
	if (num > 0 && num < MC_SPRITES)
		ext[num] = NULL;
	if (num > 0 && num < MC_SPRITES && mc_sprite[num].mc) {
		struct McSprite *sp = &mc_sprite[num];
		UWORD *old;

		Forbid();
		old = sp->data;
		sp->data = NULL;
		sp->cap = 0;
		sp->mc = NULL;
		sp->vp = NULL;
		sp->on = 0;
		sp->w = sp->h = 0;
		sp->serial++;
		sp->pos_serial++;
		mc_sprite_serial++;
		Permit();
		if (old)
			FreeVec(old);
	}
	old_free(num, gb);
}

/* InitCard of the first board, with SPRITES=1: the patches, once */
void mc_sprites_install(void)
{
	if (old_move)
		return;
	Forbid();
	old_free = (FreeSpriteFn)SetFunction((struct Library *)GfxBase, LVO_FreeSprite,
	                                     (APTR)new_FreeSprite);
	old_change = (ChangeSpriteFn)SetFunction((struct Library *)GfxBase, LVO_ChangeSprite,
	                                         (APTR)new_ChangeSprite);
	old_move = (MoveSpriteFn)SetFunction((struct Library *)GfxBase, LVO_MoveSprite,
	                                     (APTR)new_MoveSprite);
	if (GfxBase->LibNode.lib_Version >= 39) {
		old_get_ext = (GetExtFn)SetFunction((struct Library *)GfxBase, LVO_GetExtSpriteA,
		                                    (APTR)new_GetExtSpriteA);
		old_change_ext = (ChangeExtFn)SetFunction((struct Library *)GfxBase, LVO_ChangeExtSpriteA,
		                                          (APTR)new_ChangeExtSpriteA);
	}
	Permit();
	mc_status->sprites = 1;
}
