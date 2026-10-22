/*
 * P96Multicast - mctest: opens a screen on the Multicast card and draws a
 * known test pattern, for the end-to-end test (emu/run_test.sh).
 *
 *   mctest LIST                         list display modes
 *   mctest STATUS                       is the card loaded, is its sender running
 *   mctest DEPTH=8 SECS=10              the first Multicast mode with this depth
 *   mctest WIDTH=640 HEIGHT=480 DEPTH=16  a particular size
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

static const char template[] = "LIST/S,STATUS/S,NAME/K,WIDTH/N,HEIGHT/N,DEPTH/N,SECS/N";
enum { A_LIST, A_STATUS, A_NAME, A_WIDTH, A_HEIGHT, A_DEPTH, A_SECS, A_COUNT };

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
		if (!mode_name(id, buf, sizeof(buf)) || !strstr(buf, name))
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

static void draw_rgb(struct Screen *scr, UWORD w, UWORD h)
{
	struct RastPort *rp = &scr->RastPort;
	int i, y;

	FillPixelArray(rp, 0, 0, w, h, 0x000000);
	for (i = 0; i < 8; i++)
		FillPixelArray(rp, i * w / 8, 0, (i + 1) * w / 8 - i * w / 8, h / 2, bars[i]);
	for (y = h / 2; y < h; y += 4)
		for (i = 0; i < 16; i++) {
			ULONG r = (y - h / 2) * 255 / (h / 2), g = i * 17;

			FillPixelArray(rp, i * w / 16, y, w / 16, 4, (r << 16) | (g << 8) | 0x40);
		}
	FillPixelArray(rp, 0, 0, w, 1, 0xffffff);
	FillPixelArray(rp, 0, h - 1, w, 1, 0xffffff);
	FillPixelArray(rp, 0, 0, 1, h, 0xffffff);
	FillPixelArray(rp, w - 1, 0, 1, h, 0xffffff);
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
	printf("mctest: drawn, waiting %u s\n", secs);
	Delay(secs * TICKS_PER_SECOND);
	CloseScreen(scr);
	if (CyberGfxBase)
		CloseLibrary(CyberGfxBase);
	FreeArgs(rda);
	printf("mctest: done\n");
	return RETURN_OK;
}
