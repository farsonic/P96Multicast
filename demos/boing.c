/*
 * Panorama - Boing: the ball, bouncing over the whole shared desktop.
 *
 * The red and white ball, bouncing around the public screen PanoramaDesk
 * opens: over a desktop split in parts (SPLIT=2x1), from one viewer window
 * to the other.
 *
 * On a Panorama screen the ball is made of sprites, as on any Amiga:
 * graphics.library's GetExtSprite(), AllocSpriteData(), ChangeExtSprite()
 * and MoveSprite(). Panorama.card (SPRITES=1) sends them to the viewers,
 * which draw them over the screen: round, with the screen showing around
 * it, and a move costs a few bytes, no pixels. Sprites are 64 pixels wide
 * at most (16 without AGA), so the ball is two of them side by side (or
 * more: sprites 2-7), with colours 1-3 of their pair red, white and dark
 * red. (Not sprite 1: it shares the pointer's colours.)
 *
 * On any other screen (or with WINDOW), a small window without a border
 * with the ball in it, moved around: a copy within the screen, so on
 * Panorama.card a few COPY orders, not pixels.
 *
 *   Boing                       on the shared desktop (PUBSCREEN=, PANORAMA)
 *   Boing PUBSCREEN=Workbench   ... or any public screen
 *   Boing SIZE=120 SPEED=6      the ball's size, and how fast it goes
 *   Boing SECS=30               stop by itself after 30 seconds
 *   Boing WINDOW                the window, not sprites; SPRITES: sprites anyway
 *
 * Ctrl-C stops it, or clicking the ball (the window; a sprite can't be
 * clicked).
 *
 * The ball is drawn once, in 8 turns of its spin; each frame shows the
 * next one at the ball's new place.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/sprite.h>
#include <graphics/displayinfo.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: Boing 1.3 (29.10.2026)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char template[] = "PUBSCREEN/K,SIZE/K/N,SPEED/K/N,SECS/K/N,WINDOW/S,SPRITES/S";
enum { A_PUBSCREEN, A_SIZE, A_SPEED, A_SECS, A_WINDOW, A_SPRITES, A_COUNT };

#define PHASES 8          /* turns of the spin: the stripes repeat every 2 * 360 / 16 degrees */
#define LONGS  16         /* checks around */
#define LATS   8          /* checks from pole to pole */

/* asin(t) / (pi/2), t = i / 64, in 1/1024: for the longitude of a point */
static const UWORD asin_tab[65] = {
	0, 10, 20, 31, 41, 51, 61, 71, 82, 92, 102, 113, 123, 133, 144, 154,
	165, 175, 186, 196, 207, 218, 229, 240, 251, 262, 273, 284, 295, 307, 318, 330,
	341, 353, 365, 377, 389, 402, 414, 427, 440, 453, 467, 480, 494, 508, 523, 538,
	553, 568, 585, 601, 618, 636, 655, 674, 695, 716, 739, 765, 792, 824, 861, 909,
	1024
};

static LONG asin_q(LONG t)   /* t in -1024 .. 1024 -> -1024 .. 1024 */
{
	LONG a = t < 0 ? -t : t, i = a >> 4, f = a & 15, r;

	if (i >= 64)
		r = 1024;
	else
		r = asin_tab[i] + ((asin_tab[i + 1] - asin_tab[i]) * f >> 4);
	return t < 0 ? -r : r;
}

static ULONG isqrt(ULONG n)
{
	ULONG r = 0, b = 1UL << 30;

	while (b > n)
		b >>= 2;
	while (b) {
		if (n >= r + b) {
			n -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

/*
 * Pen of pixel (x, y) of the ball in turn `phase`: 0 outside, 1 red, 2 white.
 * The ball leans 1/4 of a radian-ish: a fixed rotation of the coordinates.
 */
static int ball_pen(int x, int y, int r, int phase)
{
	/* lean: x' = x cos a + y sin a, y' = -x sin a + y cos a, a ~ 17 degrees */
	LONG c = 979, s = 297;   /* *1024 */
	LONG xr = (x * c + y * s) / 1024, yr = (-x * s + y * c) / 1024;
	LONG rr = (LONG)r * r, half, lon, lat;

	if ((LONG)x * x + (LONG)y * y > rr)
		return 0;
	if (yr > r)
		yr = r;
	if (yr < -r)
		yr = -r;
	/* latitude from yr, longitude from xr across the row's half width */
	lat = asin_q(yr * 1024 / r);
	half = isqrt(rr - (LONG)yr * yr);
	lon = half ? asin_q(xr * 1024 / half) : 0;
	{
		/*
		 * The front half shows LONGS / 2 checks across (2048 units), so a
		 * check is 4096 / LONGS; a turn of the spin moves them by 1/PHASES
		 * of two checks.
		 */
		LONG check = 4096 / LONGS;
		LONG lo = (lon + 1024 + phase * 2 * check / PHASES) / check;
		LONG la = (lat + 1024) / (2048 / LATS);

		return ((lo + la) & 1) ? 1 : 2;
	}
}

/* the ball, turn phase, into rp at x0, y0 with pens pen[1] (red) and pen[2] (white) */
static void draw_ball(struct RastPort *rp, int x0, int y0, int size, int phase, LONG *pen)
{
	int px, py, r = size / 2;

	for (py = 0; py < size; py++) {
		int run = -1, last = -1;

		/* runs of one pen at a time: far fewer calls than pixels */
		for (px = 0; px <= size; px++) {
			int p = px < size ? ball_pen(px - r, py - r, r - 1, phase) : -1;

			if (p != last) {
				if (last > 0) {
					SetAPen(rp, pen[last]);
					RectFill(rp, x0 + run, y0 + py, x0 + px - 1, y0 + py);
				}
				last = p;
				run = px;
			}
		}
	}
}

struct Ball {
	struct Screen *scr;
	LONG size, x, y, vx, vy, g;
};

/* the next place: gravity down, the walls and the floor of the whole screen */
static void bounce(struct Ball *b)
{
	struct Screen *scr = b->scr;
	LONG nx, ny, top = scr->BarHeight + 1;
	LONG high = (LONG)isqrt(2 * b->g * (scr->Height - b->size - scr->BarHeight - 8));

	b->vy += b->g;
	nx = b->x + b->vx;
	ny = b->y + b->vy;
	if (nx < 0 || nx + b->size > scr->Width) {
		b->vx = -b->vx;
		nx = b->x + b->vx;
	}
	if (ny + b->size > scr->Height) {
		ny = scr->Height - b->size;
		b->vy = -b->vy;
		/* always bounce as high again: from the floor to near the top */
		if (b->vy > -high)
			b->vy = -high;
	}
	if (ny < top) {
		ny = top;
		b->vy = 0;
	}
	b->x = nx;
	b->y = ny;
}

/* SECS=: by the clock, however fast the frames go */
static BOOL time_up(struct DateStamp *start, LONG secs)
{
	struct DateStamp now;

	if (!secs)
		return FALSE;
	DateStamp(&now);
	return (now.ds_Days - start->ds_Days) * 86400 + (now.ds_Minute - start->ds_Minute) * 60 +
	       (now.ds_Tick - start->ds_Tick) / TICKS_PER_SECOND >= secs;
}

/* is scr on a Panorama board (its mode's name says so)? */
static BOOL panorama_screen(struct Screen *scr)
{
	struct NameInfo ni;
	ULONG id = GetVPModeID(&scr->ViewPort);

	if (id == INVALID_ID || !GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, id))
		return FALSE;
	return strncmp((char *)ni.Name, "Panorama", 8) == 0;
}

/* ---------------------------------------------------------------- the ball as sprites */

/*
 * Sprites 2-7: 0 is the pointer, and 1 has the pointer's colours (a pair
 * shares its colours), which the ball would change
 */
#define FIRST    2
#define MAXTILES 6

/*
 * The ball as sprites side by side, each tw wide. RETURN_OK when stopped,
 * -1 if it can't be (too few sprites, their colours taken): the window then.
 */
static int sprite_boing(struct Ball *b, LONG secs)
{
	static const ULONG rgb[3][3] = {
		{ 0xeeeeeeee, 0x11111111, 0x11111111 },     /* 1 red */
		{ 0xffffffff, 0xffffffff, 0xffffffff },     /* 2 white */
		{ 0x77777777, 0x00000000, 0x00000000 }      /* 3 dark red */
	};
	struct Screen *scr = b->scr;
	struct ViewPort *vp = &scr->ViewPort;
	struct ColorMap *cm = vp->ColorMap;
	struct BitMap *bm[PHASES] = { 0 };
	struct ExtSprite *spr[PHASES][MAXTILES] = { { 0 } }, *cur[MAXTILES];
	LONG num[MAXTILES], pens[2 * MAXTILES * 3], npens = 0, tw = 0, tiles, i, t, c;
	LONG pen[3] = { 0, 1, 2 };
	struct RastPort rp;
	struct DateStamp start;
	BOOL clut = GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH) <= 8;
	int rc = -1, phase = 0;

	if (GfxBase->LibNode.lib_Version < 39)
		return -1;
	for (t = 0; t < MAXTILES; t++)
		num[t] = -1;

	/* as wide as the sprites can be: 64 with AGA, 16 without */
	{
		static const LONG widths[3] = { 64, 32, 16 };
		struct BitMap *probe = AllocBitMap(64, 1, 2, BMF_CLEAR, NULL);

		for (i = 0; probe && i < 3 && !tw; i++) {
			struct ExtSprite *e = AllocSpriteData(probe, SPRITEA_Width, widths[i], TAG_END);

			if (e) {
				tw = widths[i];
				FreeSpriteData(e);
			}
		}
		if (probe)
			FreeBitMap(probe);
	}
	if (!tw)
		return -1;
	tiles = (b->size + tw - 1) / tw;
	if (tiles > MAXTILES) {
		tiles = MAXTILES;
		b->size = tiles * tw;
	}

	/* the turns of the ball in two bitplanes: 0 clear, 1 red, 2 white */
	printf("Boing: drawing the ball (%ld sprites of %ld)...\n", tiles, tw);
	InitRastPort(&rp);
	for (i = 0; i < PHASES; i++) {
		bm[i] = AllocBitMap(tiles * tw, b->size, 2, BMF_CLEAR, NULL);
		if (!bm[i])
			goto out;
		rp.BitMap = bm[i];
		draw_ball(&rp, 0, 0, b->size, i, pen);
		for (t = 0; t < tiles; t++) {
			/* this sprite's columns of it */
			struct BitMap part = *bm[i];

			part.Planes[0] += t * tw / 8;
			part.Planes[1] += t * tw / 8;
			spr[i][t] = AllocSpriteData(&part, SPRITEA_Width, tw, TAG_END);
			if (!spr[i][t])
				goto out;
		}
	}

	for (t = 0; t < tiles; t++) {
		num[t] = GetExtSprite(spr[0][t], GSTAG_SPRITE_NUM, FIRST + t, TAG_END);
		if (num[t] < 0) {
			printf("Boing: not enough sprites free\n");
			goto out;
		}
		cur[t] = spr[0][t];
	}
	/*
	 * Colours 1-3 of each sprite pair's: on a screen of 256 colours or
	 * fewer those are the screen's pens too, so only if nobody has them
	 */
	for (t = 0; t < tiles; t++) {
		ULONG base = (cm && cm->Type >= COLORMAP_TYPE_V39 ?
		              (num[t] & 1 ? cm->SpriteBase_Odd : cm->SpriteBase_Even) : 16) +
		             (num[t] >> 1) * 4 + 1;

		for (c = 0; c < 3; c++) {
			if (clut) {
				LONG k;

				for (k = 0; k < npens && pens[k] != (LONG)(base + c); k++)
					;
				if (k < npens)
					continue;
				if (ObtainPen(cm, base + c, rgb[c][0], rgb[c][1], rgb[c][2], PEN_EXCLUSIVE) == -1) {
					printf("Boing: the sprites' colours are the screen's, in use\n");
					goto out;
				}
				pens[npens++] = base + c;
			} else {
				SetRGB32(vp, base + c, rgb[c][0], rgb[c][1], rgb[c][2]);
			}
		}
	}

	printf("Boing: bouncing over %dx%d as sprites; Ctrl-C to stop\n", scr->Width, scr->Height);
	fflush(stdout);
	rc = RETURN_OK;
	DateStamp(&start);
	for (;;) {
		if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
			break;
		if (time_up(&start, secs))
			break;
		bounce(b);
		phase = (phase + (b->vx > 0 ? 1 : PHASES - 1)) % PHASES;
		for (t = 0; t < tiles; t++) {
			if (cur[t] != spr[phase][t]) {
				ChangeExtSprite(vp, cur[t], spr[phase][t], TAG_END);
				cur[t] = spr[phase][t];
			}
			MoveSprite(vp, &cur[t]->es_SimpleSprite, b->x + t * tw, b->y);
		}
		WaitTOF();
	}

out:
	for (t = 0; t < MAXTILES; t++)
		if (num[t] >= 0)
			FreeSprite(num[t]);
	for (i = 0; i < npens; i++)
		ReleasePen(cm, pens[i]);
	for (i = 0; i < PHASES; i++) {
		for (t = 0; t < MAXTILES; t++)
			if (spr[i][t])
				FreeSpriteData(spr[i][t]);
		if (bm[i])
			FreeBitMap(bm[i]);
	}
	if (rc < 0 && !bm[PHASES - 1])
		printf("Boing: not enough memory for the sprites\n");
	return rc;
}

/* ---------------------------------------------------------------- the ball in a window */

static int window_boing(struct Ball *b, LONG secs)
{
	struct Screen *scr = b->scr;
	struct Window *win = NULL;
	struct BitMap *frames[PHASES] = { 0 };
	struct RastPort rp;
	struct DateStamp start;
	LONG pen[3], size = b->size;
	BOOL got_pens;
	int i, rc = RETURN_FAIL, phase = 0;

	/* red and white, and the screen's background around the ball */
	pen[0] = 0;
	pen[1] = ObtainBestPen(scr->ViewPort.ColorMap, 0xeeeeeeee, 0x11111111, 0x11111111,
	                       OBP_Precision, PRECISION_IMAGE, TAG_END);
	pen[2] = ObtainBestPen(scr->ViewPort.ColorMap, 0xffffffff, 0xffffffff, 0xffffffff,
	                       OBP_Precision, PRECISION_IMAGE, TAG_END);
	got_pens = pen[1] >= 0 && pen[2] >= 0;
	if (!got_pens) {
		pen[1] = 2;
		pen[2] = 1;
	}

	/* the turns of the ball, drawn once, in bitmaps like the screen's */
	printf("Boing: drawing the ball...\n");
	InitRastPort(&rp);
	for (i = 0; i < PHASES; i++) {
		frames[i] = AllocBitMap(size, size, scr->RastPort.BitMap->Depth, BMF_CLEAR,
		                        scr->RastPort.BitMap);
		if (!frames[i]) {
			printf("Boing: not enough memory\n");
			goto out;
		}
		rp.BitMap = frames[i];
		SetAPen(&rp, pen[0]);
		RectFill(&rp, 0, 0, size - 1, size - 1);
		draw_ball(&rp, 0, 0, size, i, pen);
	}

	win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, b->x, WA_Top, b->y,
	                     WA_Width, size, WA_Height, size, WA_Borderless, TRUE,
	                     WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
	                     WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_CHANGEWINDOW, WA_Title, (ULONG)"Boing",
	                     TAG_END);
	if (!win) {
		printf("Boing: cannot open the window\n");
		goto out;
	}
	printf("Boing: bouncing over %dx%d; click the ball or Ctrl-C to stop\n", scr->Width,
	       scr->Height);
	fflush(stdout);
	rc = RETURN_OK;
	DateStamp(&start);

	for (;;) {
		struct IntuiMessage *m;
		BOOL stop = FALSE;

		if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
			break;
		while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
			if (m->Class == IDCMP_MOUSEBUTTONS && m->Code == SELECTDOWN)
				stop = TRUE;
			ReplyMsg((struct Message *)m);
		}
		if (stop || time_up(&start, secs))
			break;
		bounce(b);
		/*
		 * Intuition moves the window later, not at once: wait until it has
		 * (IDCMP_CHANGEWINDOW), so moves don't pile up and the next one is
		 * worked out from where the window really is. Then draw the next
		 * turn of the ball, at its new place.
		 */
		MoveWindow(win, b->x - win->LeftEdge, b->y - win->TopEdge);
		{
			int tries;
			BOOL moved = FALSE;

			for (tries = 0; tries < 10 && !moved; tries++) {
				WaitTOF();
				while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
					if (m->Class == IDCMP_CHANGEWINDOW)
						moved = TRUE;
					else if (m->Class == IDCMP_MOUSEBUTTONS && m->Code == SELECTDOWN)
						stop = TRUE;
					ReplyMsg((struct Message *)m);
				}
			}
		}
		b->x = win->LeftEdge;
		b->y = win->TopEdge;
		phase = (phase + (b->vx > 0 ? 1 : PHASES - 1)) % PHASES;
		BltBitMapRastPort(frames[phase], 0, 0, win->RPort, 0, 0, size, size, 0xc0);
		WaitTOF();
		if (stop)
			break;
	}

out:
	if (win)
		CloseWindow(win);
	for (i = 0; i < PHASES; i++)
		if (frames[i])
			FreeBitMap(frames[i]);
	if (got_pens) {
		ReleasePen(scr->ViewPort.ColorMap, pen[1]);
		ReleasePen(scr->ViewPort.ColorMap, pen[2]);
	}
	return rc;
}

int main(void)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	const char *name = "PANORAMA";
	struct Ball b;
	LONG size = 100, speed = 5, secs = 0;
	int rc = RETURN_FAIL;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
	if (!IntuitionBase || !GfxBase) {
		printf("Boing: needs AmigaOS 3.0 or later\n");
		goto out;
	}
	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), (STRPTR)"Boing");
		goto out;
	}
	if (args[A_PUBSCREEN])
		name = (const char *)args[A_PUBSCREEN];
	if (args[A_SIZE])
		size = *(LONG *)args[A_SIZE];
	if (args[A_SPEED])
		speed = *(LONG *)args[A_SPEED];
	if (args[A_SECS])
		secs = *(LONG *)args[A_SECS];
	if (size < 16)
		size = 16;
	if (size > 400)
		size = 400;
	if (speed < 1)
		speed = 1;

	b.scr = LockPubScreen((STRPTR)name);
	if (!b.scr && !args[A_PUBSCREEN])
		b.scr = LockPubScreen(NULL);
	if (!b.scr) {
		printf("Boing: no public screen %s (PanoramaDesk opens it)\n", name);
		FreeArgs(rda);
		goto out;
	}
	b.size = size;
	b.x = 20;
	b.y = b.scr->BarHeight + 4;
	b.vx = speed;
	b.vy = 0;
	b.g = 1;

	rc = -1;
	if (!args[A_WINDOW] && (args[A_SPRITES] || panorama_screen(b.scr)))
		rc = sprite_boing(&b, secs);
	if (rc < 0) {
		b.size = size;
		rc = window_boing(&b, secs);
	}
	UnlockPubScreen(NULL, b.scr);
	FreeArgs(rda);
out:
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
