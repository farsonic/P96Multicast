/*
 * P96Multicast - the card's own screen modes.
 *
 * P96 normally takes a board's resolutions from its settings, made with
 * Picasso96Mode. A card may also put its own in bi->ResolutionsList in
 * InitCard, as UAE's uaegfx.card does, and then needs no settings at all.
 * Multicast.card does that, with the sizes in MODES=, each in every depth
 * class whose screen fits in the video memory. A frame
 * buffer has no monitor, so the timings only have to be plausible.
 *
 * P96 also adds the modes its settings have for the board, and refuses two
 * resolutions with the same DisplayID ("at least one DisplayID already
 * exists"), seemingly after InitCard, where the card can't see them. So the
 * card adds modes only when MODES= asks for them (without it, or with
 * MODES=NONE, P96's settings from Picasso96Mode are the modes, as before),
 * gives them DisplayIDs P96 doesn't use for its own (display_id), and skips
 * a size already in the list. How many were there at InitCard and how many
 * the card added is in the status block (mctest STATUS).
 *
 * AROS's p96gfx.hidd makes its own list from a fixed table after InitCard,
 * so there these are not used.
 */
#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "mcard.h"

/*
 * Multicast.card 1.3 numbered from 0xff down, and Picasso96Mode saves the
 * modes it is shown, so those IDs may be in the settings now: 2.0 starts
 * lower. Neither range is one P96 gives a mode of its own.
 */
#define MC_ID_TOP 0xdf

/*
 * DisplayIDs from high in P96's range down: index MC_ID_TOP, one less, ...
 * for the first board, 24 lower for each further one. Picasso96Mode's settings
 * and UAE's table number from the bottom (up to 0xb9), so the card's modes
 * never take an ID P96 gives a mode of its own, whenever P96 adds those.
 */
static ULONG display_id(UWORD board, UWORD i)
{
	return 0x50001000UL | ((ULONG)(MC_ID_TOP - board * MC_MAXMODES - i) << 16);
}

/*
 * The DisplayIDs Picasso96Mode's settings already have (0x50xx1000, as the
 * card's are): Picasso96Mode saves whatever modes it was shown, the card's
 * own among them, and P96 refuses a board whose IDs clash with them. The
 * file's layout isn't documented, so every place that looks like such an ID
 * counts. Needs a process; without one, or without the file, none.
 */
static void ids_in_settings(UBYTE used[256])
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin;
	BPTR fh;
	UBYTE *buf;
	LONG n, keep = 0, i;

	if (me->pr_Task.tc_Node.ln_Type != NT_PROCESS) {
		mc_status->settings_read = MC_SET_NOPROCESS;
		return;
	}
	oldwin = me->pr_WindowPtr;
	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)"DEVS:Picasso96Settings", MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh) {
		mc_status->settings_read = MC_SET_NOFILE;
		return;
	}
	mc_status->settings_read = MC_SET_READ;
	buf = AllocMem(4096, MEMF_ANY);
	while (buf && (n = Read(fh, buf + keep, 4096 - keep)) > 0) {
		n += keep;
		for (i = 0; i + 4 <= n; i++)
			if (buf[i] == 0x50 && buf[i + 2] == 0x10 && buf[i + 3] == 0x00 &&
			    !used[buf[i + 1]]) {
				used[buf[i + 1]] = 1;
				mc_status->settings_ids++;
			}
		keep = n < 3 ? n : 3;
		CopyMem(buf + n - keep, buf, keep);
	}
	if (buf)
		FreeMem(buf, 4096);
	Close(fh);
}

#define HOR_BLANK 8
#define VER_BLANK 8

static void put_decimal(char **p, UWORD n)
{
	char buf[6];
	int i = 0;

	do
		buf[i++] = '0' + n % 10;
	while ((n /= 10) && i < 5);
	while (i)
		*(*p)++ = buf[--i];
}

static void fill_mode(struct ModeInfo *mi, UWORD w, UWORD h, UBYTE depth)
{
	ULONG clock = (ULONG)(w + HOR_BLANK) * (h + VER_BLANK) * 60;

	mi->Active = 1;
	mi->Width = w;
	mi->Height = h;
	mi->Depth = depth;
	mi->HorTotal = w + HOR_BLANK;
	mi->HorBlankSize = HOR_BLANK;
	mi->HorSyncStart = 2;
	mi->HorSyncSize = 2;
	mi->VerTotal = h + VER_BLANK;
	mi->VerBlankSize = VER_BLANK;
	mi->VerSyncStart = 2;
	mi->VerSyncSize = 2;
	mi->PixelClock = clock;
	/* rtg.library asks ResolvePixelClock (card.c) for these before use */
	mi->Numerator = 0;
	mi->Denominator = 1;
}

/* a resolution of this size or with this DisplayID is already in the list */
static BOOL taken(struct BoardInfo *bi, UWORD w, UWORD h, ULONG id)
{
	struct LibResolution *r;

	for (r = (struct LibResolution *)bi->ResolutionsList.mlh_Head; r->Node.ln_Succ;
	     r = (struct LibResolution *)r->Node.ln_Succ)
		if ((r->Width == w && r->Height == h) || r->DisplayID == id)
			return TRUE;
	return FALSE;
}

void mc_modes_build(struct McCard *mc)
{
	struct BoardInfo *bi = mc->bi;
	struct McConfig *cfg = &mc->cfg;
	static const UBYTE depths[MAXMODES] = { 0, 8, 16, 24, 32 };
	UWORD sizes[MC_MAXMODES][2];
	UWORD n = 0, i, before = 0, added = 0, next = 0;
	UBYTE used[256];
	struct Node *nd;
	UBYTE *p;

	/* what P96 put in the list before InitCard (an empty list may be unset) */
	if (bi->ResolutionsList.mlh_Head && bi->ResolutionsList.mlh_TailPred)
		for (nd = (struct Node *)bi->ResolutionsList.mlh_Head; nd->ln_Succ; nd = nd->ln_Succ)
			before++;
	else
		NewList((struct List *)&bi->ResolutionsList);
	if (mc->boardnum == 0)
		mc_status->modes_before = before;
	if (cfg->nomodes || !cfg->nmodes)
		return;
	for (i = 0; i < cfg->nmodes; i++) {
		sizes[n][0] = cfg->mode_w[i];
		sizes[n][1] = cfg->mode_h[i];
		n++;
	}
	if (!n)
		return;
	for (i = 0; i < 256; i++)
		used[i] = 0;
	ids_in_settings(used);

	mc->modessize = n * (sizeof(struct LibResolution) + (MAXMODES - 1) * sizeof(struct ModeInfo));
	mc->modes = AllocMem(mc->modessize, MEMF_PUBLIC | MEMF_CLEAR);
	if (!mc->modes)
		return;
	p = mc->modes;
	for (i = 0; i < n; i++) {
		struct LibResolution *res = (struct LibResolution *)p;
		UWORD w = sizes[i][0], h = sizes[i][1];
		char *name = res->Name;
		const char *b = mc->boardname;
		int k, any = 0;

		p += sizeof(*res);
		for (k = CHUNKY; k < MAXMODES; k++) {
			struct ModeInfo *mi = (struct ModeInfo *)p;

			p += sizeof(*mi);
			if ((ULONG)w * h * (depths[k] / 8) > cfg->vramsize)
				continue;
			fill_mode(mi, w, h, depths[k]);
			res->Modes[k] = mi;
			any = 1;
		}
		if (!any)
			continue;
		/* the board's next DisplayID that neither the list nor the settings have */
		do
			res->DisplayID = display_id(mc->boardnum, next++);
		while (next < 0x80 && (used[(res->DisplayID >> 16) & 0xff] ||
		                       taken(bi, 0, 0, res->DisplayID)));
		if (taken(bi, w, h, 0xffffffffUL))
			continue;
		/* "Multicast:640x480" */
		while (*b && name < res->Name + MAXRESOLUTIONNAMELENGTH - 11)
			*name++ = *b++;
		*name++ = ':';
		put_decimal(&name, w);
		*name++ = 'x';
		put_decimal(&name, h);
		*name = 0;
		CopyMem("P96-0:", res->P96ID, 6);
		res->Node.ln_Name = res->P96ID;
		res->Width = w;
		res->Height = h;
		res->Flags = P96F_PUBLIC;
		res->BoardInfo = bi;
		AddTail((struct List *)&bi->ResolutionsList, &res->Node);
		if (!added && mc->boardnum == 0)
			mc_status->first_id = res->DisplayID;
		added++;
		if (mc->boardnum == 0)
			mc_status->modes_added = added;
		/* a MODES= size beyond MAXWIDTH x MAXHEIGHT raises the board's limits */
		for (k = CHUNKY; k < MAXMODES; k++) {
			if (!res->Modes[k])
				continue;
			if (bi->MaxHorResolution[k] < w)
				bi->MaxHorResolution[k] = w;
			if (bi->MaxVerResolution[k] < h)
				bi->MaxVerResolution[k] = h;
		}
	}
}
