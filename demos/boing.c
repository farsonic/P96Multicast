/*
 * P96Multicast - Boing: the ball, bouncing over the whole shared desktop.
 *
 * A small window without a border, with the red and white ball in it,
 * moved around the public screen MulticastDesktop opens: over a desktop
 * split in parts (SPLIT=2x1), it bounces from one viewer window to the
 * other. Moving a window is a copy within the screen, so on Multicast.card
 * each step goes out as a few COPY orders, not as pixels.
 *
 *   Boing                       on the shared desktop (PUBSCREEN=, MULTICAST)
 *   Boing PUBSCREEN=Workbench   ... or any public screen
 *   Boing SIZE=120 SPEED=6      the ball's size, and how fast it goes
 *   Boing SECS=30               stop by itself after 30 seconds
 *
 * Click the ball, or Ctrl-C, to stop it.
 *
 * The ball is drawn once, in 8 turns of its spin, into bitmaps like the
 * screen's; each frame blits the next one into the window and moves it.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

static const char version[] = "$VER: Boing 1.0 (26.10.2026)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

static const char template[] = "PUBSCREEN/K,SIZE/K/N,SPEED/K/N,SECS/K/N";
enum { A_PUBSCREEN, A_SIZE, A_SPEED, A_SECS, A_COUNT };

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

int main(void)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	const char *name = "MULTICAST";
	struct Screen *scr;
	struct Window *win = NULL;
	struct BitMap *frames[PHASES] = { 0 };
	struct RastPort rp;
	LONG size = 100, speed = 5, pen[3], x, y, vx, vy, g = 1, secs = 0;
	struct DateStamp start, now;
	BOOL got_pens = FALSE;
	int i, rc = RETURN_FAIL, phase = 0;

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

	scr = LockPubScreen((STRPTR)name);
	if (!scr && !args[A_PUBSCREEN])
		scr = LockPubScreen(NULL);
	if (!scr) {
		printf("Boing: no public screen %s (MulticastDesktop opens it)\n", name);
		FreeArgs(rda);
		goto out;
	}

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

	/* the turns of the ball, drawn once */
	printf("Boing: drawing the ball...\n");
	InitRastPort(&rp);
	for (i = 0; i < PHASES; i++) {
		int px, py, r = size / 2;

		frames[i] = AllocBitMap(size, size, scr->RastPort.BitMap->Depth, BMF_CLEAR,
		                        scr->RastPort.BitMap);
		if (!frames[i]) {
			printf("Boing: not enough memory\n");
			goto unlock;
		}
		rp.BitMap = frames[i];
		SetAPen(&rp, pen[0]);
		RectFill(&rp, 0, 0, size - 1, size - 1);
		for (py = 0; py < size; py++) {
			int run = -1, last = -1;

			/* runs of one pen at a time: far fewer calls than pixels */
			for (px = 0; px <= size; px++) {
				int p = px < size ? ball_pen(px - r, py - r, r - 1, i) : -1;

				if (p != last) {
					if (last > 0) {
						SetAPen(&rp, pen[last]);
						RectFill(&rp, run, py, px - 1, py);
					}
					last = p;
					run = px;
				}
			}
		}
	}

	x = 20;
	y = scr->BarHeight + 4;
	vx = speed;
	vy = 0;
	win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, x, WA_Top, y,
	                     WA_Width, size, WA_Height, size, WA_Borderless, TRUE,
	                     WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE,
	                     WA_IDCMP, IDCMP_MOUSEBUTTONS, WA_Title, (ULONG)"Boing", TAG_END);
	if (!win) {
		printf("Boing: cannot open the window\n");
		goto unlock;
	}
	printf("Boing: bouncing over %dx%d; click the ball or Ctrl-C to stop\n", scr->Width,
	       scr->Height);
	fflush(stdout);
	rc = RETURN_OK;
	DateStamp(&start);

	for (;;) {
		struct IntuiMessage *m;
		BOOL stop = FALSE;
		LONG nx, ny;

		if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
			break;
		while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
			if (m->Class == IDCMP_MOUSEBUTTONS && m->Code == SELECTDOWN)
				stop = TRUE;
			ReplyMsg((struct Message *)m);
		}
		if (stop)
			break;
		/* SECS=: by the clock, however fast the frames go */
		if (secs) {
			DateStamp(&now);
			if ((now.ds_Days - start.ds_Days) * 86400 + (now.ds_Minute - start.ds_Minute) * 60 +
			    (now.ds_Tick - start.ds_Tick) / TICKS_PER_SECOND >= secs)
				break;
		}

		/* a bounce: gravity down, the walls and the floor of the whole desktop */
		vy += g;
		nx = x + vx;
		ny = y + vy;
		if (nx < 0 || nx + size > scr->Width) {
			vx = -vx;
			nx = x + vx;
		}
		if (ny + size > scr->Height) {
			ny = scr->Height - size;
			vy = -vy;
			/* always bounce as high again: from the floor to near the top */
			if (vy > -(LONG)isqrt(2 * g * (scr->Height - size - scr->BarHeight - 8)))
				vy = -(LONG)isqrt(2 * g * (scr->Height - size - scr->BarHeight - 8));
		}
		if (ny < scr->BarHeight + 1) {
			ny = scr->BarHeight + 1;
			vy = 0;
		}
		phase = (phase + (vx > 0 ? 1 : PHASES - 1)) % PHASES;
		BltBitMapRastPort(frames[phase], 0, 0, win->RPort, 0, 0, size, size, 0xc0);
		MoveWindow(win, nx - win->LeftEdge, ny - win->TopEdge);
		x = nx;
		y = ny;
		WaitTOF();
		WaitTOF();
	}

unlock:
	if (win)
		CloseWindow(win);
	for (i = 0; i < PHASES; i++)
		if (frames[i])
			FreeBitMap(frames[i]);
	if (got_pens) {
		ReleasePen(scr->ViewPort.ColorMap, pen[1]);
		ReleasePen(scr->ViewPort.ColorMap, pen[2]);
	}
	UnlockPubScreen(NULL, scr);
	FreeArgs(rda);
out:
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
