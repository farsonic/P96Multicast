/*
 * Panorama - modetest: runs the built Panorama.card under vamos, with
 * this program in the place of rtg.library, and checks the screen modes it
 * puts in bi->ResolutionsList (src/modes.c).
 *
 *   modetest PATH COUNT [WxH [PRE]]  PATH: Panorama.card's file
 *                               COUNT: the resolutions the card is to add
 *                               WxH: one that must be among them (- for none)
 *                               PRE: P96 has a 640x480 of its own already
 *
 * tests/modetest.sh sets up LIBS: and ENV: for it.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boardinfo.h"

BOOL __FindCard(__reg("a6") struct Library *, __reg("a0") struct BoardInfo *) = "\tjsr\t-30(a6)";
BOOL __InitCard(__reg("a6") struct Library *, __reg("a0") struct BoardInfo *,
                __reg("a1") char **) = "\tjsr\t-36(a6)";

static int fails;

static void check(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		fails++;
}

int main(int argc, char **argv)
{
	static const UBYTE depths[MAXMODES] = { 0, 8, 16, 24, 32 };
	struct Library *card;
	struct BoardInfo *bi;
	struct LibResolution *res, pre;
	int count = 0, want = argc > 2 ? atoi(argv[2]) : -1, found = 0, good = 1;
	UWORD fw = 0, fh = 0;

	if (argc > 3 && argv[3][0] != '-')
		sscanf(argv[3], "%hux%hu", &fw, &fh);
	card = OpenLibrary((STRPTR)(argc > 1 ? argv[1] : "LIBS:Picasso96/Panorama.card"), 0);
	check(card != NULL, "Panorama.card opens");
	if (!card)
		return 20;
	bi = AllocMem(sizeof(*bi) + 4096, MEMF_PUBLIC | MEMF_CLEAR);
	bi->CardBase = card;
	bi->ExecBase = SysBase;
	NewList((struct List *)&bi->ResolutionsList);
	if (argc > 4) {
		/* a resolution from P96's settings, there before InitCard */
		memset(&pre, 0, sizeof(pre));
		pre.Width = 640;
		pre.Height = 480;
		pre.DisplayID = 0x50031000;
		AddTail((struct List *)&bi->ResolutionsList, &pre.Node);
	}
	check(__FindCard(card, bi), "FindCard");
	/* InitCard fails here when vamos cannot start the sender; the modes come before */
	__InitCard(card, bi, NULL);

	for (res = (struct LibResolution *)bi->ResolutionsList.mlh_Head;
	     res->Node.ln_Succ; res = (struct LibResolution *)res->Node.ln_Succ) {
		int k, n = 0;

		if (res == &pre)
			continue;
		count++;
		printf("  %08lx %4ux%-4u %-22s", res->DisplayID, res->Width, res->Height, res->Name);
		for (k = CHUNKY; k < MAXMODES; k++) {
			struct ModeInfo *mi = res->Modes[k];

			if (!mi)
				continue;
			printf(" %u", mi->Depth);
			n++;
			if (mi->Width != res->Width || mi->Height != res->Height || mi->Depth != depths[k] ||
			    !mi->Active || mi->HorTotal <= mi->Width || mi->VerTotal <= mi->Height ||
			    !mi->PixelClock || bi->MaxHorResolution[k] < mi->Width ||
			    bi->MaxVerResolution[k] < mi->Height)
				good = 0;
		}
		printf("\n");
		/* IDs from the top of the range (0xc0 and up), apart from P96's own */
		if (!n || res->BoardInfo != bi || !(res->Flags & P96F_PUBLIC) ||
		    strncmp(res->Node.ln_Name, "P96-0:Panorama:", 15) ||
		    (res->DisplayID & 0xff00ffffUL) != 0x50001000UL ||
		    ((res->DisplayID >> 16) & 0xff) < 0xc0)
			good = 0;
		{
			struct LibResolution *o;

			for (o = (struct LibResolution *)res->Node.ln_Succ; o->Node.ln_Succ;
			     o = (struct LibResolution *)o->Node.ln_Succ)
				if (o->DisplayID == res->DisplayID)
					good = 0;
		}
		if (res->Width == fw && res->Height == fh)
			found = 1;
	}
	if (want >= 0) {
		char what[64];

		sprintf(what, "%d resolutions", want);
		check(count == want, what);
	}
	check(good, "names, unique high IDs, flags, a ModeInfo per depth that fits, within the limits");
	if (fw)
		check(found, "the asked-for size is there");
	printf("modetest: %s\n", fails ? "FAILED" : "passed");
	return fails ? 20 : 0;
}
