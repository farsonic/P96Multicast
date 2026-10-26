/*
 * P96Multicast - the card's own screen modes.
 *
 * P96 normally takes a board's resolutions from its settings, made with
 * Picasso96Mode. A card may also put its own in bi->ResolutionsList in
 * InitCard, as UAE's uaegfx.card does, and then needs no settings at all.
 * Multicast.card does that, with the sizes in MODES= or a built-in list,
 * each in every depth class whose screen fits in the video memory. A frame
 * buffer has no monitor, so the timings only have to be plausible.
 *
 * P96 also adds the modes its settings have for the board, and refuses two
 * resolutions with the same DisplayID ("at least one DisplayID already
 * exists"). So the card never adds a size or a DisplayID that is already in
 * the list, and without MODES= it adds none at all if the list already has
 * some when InitCard runs: the user's Picasso96Mode settings win. MODES=NONE
 * leaves the list to P96's settings in any case. How many were there before
 * and how many the card added is in the status block (mctest STATUS).
 *
 * AROS's p96gfx.hidd makes its own list from a fixed table after InitCard,
 * so there these are not used.
 */
#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include "mcard.h"

/* P96's numbers for the usual sizes: the table in UAE's picasso96_win.cpp.
   Other sizes count down from 0x51. */
static const struct { UWORD w, h; UBYTE id; } known[] = {
	{  320,  200,   0 }, {  320,  240,   1 }, {  640,  400,   2 }, {  640,  480,   3 },
	{  800,  600,   4 }, { 1024,  768,   5 }, { 1152,  864,   6 }, { 1280, 1024,   7 },
	{ 1600, 1280,   8 }, {  320,  256,   9 }, {  640,  512,  10 }, {  704,  480, 129 },
	{  704,  576, 130 }, {  720,  480, 131 }, {  720,  576, 132 }, {  768,  483, 133 },
	{  768,  576, 134 }, {  800,  480, 135 }, {  848,  480, 136 }, {  854,  480, 137 },
	{  948,  576, 138 }, { 1024,  576, 139 }, { 1152,  768, 140 }, { 1152,  864, 141 },
	{ 1280,  720, 142 }, { 1280,  768, 143 }, { 1280,  800, 144 }, { 1280,  854, 145 },
	{ 1280,  960, 146 }, { 1366,  768, 147 }, { 1440,  900, 148 }, { 1440,  960, 149 },
	{ 1600, 1200, 150 }, { 1680, 1050, 151 }, { 1920, 1080, 152 }, { 1920, 1200, 153 },
	{ 2048, 1152, 154 }, { 2048, 1536, 155 }, { 2560, 1600, 156 }, { 2560, 2048, 157 },
	{  400,  300, 158 }, {  512,  384, 159 }, {  640,  432, 160 }, { 1360,  768, 161 },
	{ 1360, 1024, 162 }, { 1400, 1050, 163 }, { 1792, 1344, 164 }, { 1800, 1440, 165 },
	{ 1856, 1392, 166 }, { 1920, 1440, 167 }, {  480,  360, 168 }, {  640,  350, 169 },
	{ 1600,  900, 170 }, {  960,  600, 171 }, { 1088,  612, 172 }, { 1152,  648, 173 },
	{ 1776, 1000, 174 }, { 2560, 1440, 175 }, { 2560, 1920, 176 }, { 3440, 1440, 177 },
	{ 3840, 1600, 178 }, { 3840, 2160, 179 }, { 5120, 1440, 180 }, { 5120, 2160, 181 },
	{ 1280,  600, 182 }, { 3840, 1080, 183 }, { 2560, 1080, 184 }, { 4096, 2160, 185 },
	{ 5120, 2880, 186 }, { 1176,  664, 187 }, { 1440, 1080, 188 }, { 1600, 1000, 189 },
	{ 1600, 1024, 190 }, { 2048, 1280, 191 }, { 6144, 3456, 192 }, { 7680, 4320, 193 },
};

/* without MODES=: these, up to MAXWIDTH x MAXHEIGHT */
static const UWORD defaults[][2] = {
	{ 320, 240 }, { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 720 },
	{ 1280, 960 }, { 1280, 1024 }, { 1440, 900 }, { 1600, 1200 }, { 1920, 1080 },
	{ 1920, 1200 },
};

#define HOR_BLANK 8
#define VER_BLANK 8

static ULONG display_id(UWORD w, UWORD h, UWORD board, UWORD *unknown)
{
	int i;

	for (i = 0; i < sizeof(known) / sizeof(known[0]); i++)
		if (known[i].w == w && known[i].h == h)
			return (0x50001000UL | ((ULONG)known[i].id << 16)) + board * 0x100;
	(*unknown)++;
	return 0x51001000UL - (ULONG)*unknown * 0x10000 + board * 0x100;
}

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
	UWORD n = 0, unknown = 0, i, before = 0, added = 0;
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
	if (cfg->nomodes || (!cfg->nmodes && before))
		return;
	if (cfg->nmodes) {
		for (i = 0; i < cfg->nmodes; i++) {
			sizes[n][0] = cfg->mode_w[i];
			sizes[n][1] = cfg->mode_h[i];
			n++;
		}
	} else {
		for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]) && n < MC_MAXMODES; i++) {
			if (defaults[i][0] > cfg->maxwidth || defaults[i][1] > cfg->maxheight)
				continue;
			sizes[n][0] = defaults[i][0];
			sizes[n][1] = defaults[i][1];
			n++;
		}
	}
	if (!n)
		return;

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
		res->DisplayID = display_id(w, h, mc->boardnum, &unknown);
		if (taken(bi, w, h, res->DisplayID))
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
