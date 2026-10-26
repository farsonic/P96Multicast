/*
 * P96Multicast - wrapstat: what Wrap.card wraps and what the wrapped card
 * has been asked to do.
 *
 *   wrapstat            the wrapped card, its boards, display state, calls,
 *                       and the mirror (MIRROR=)
 *   wrapstat ALL        ... also the slots that are not wrapped
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include "wrap_status.h"

#define X_NAME(f, kind, def) #f,
static const char *slot_names[] = { WRAP_SLOTS(X_NAME) };

static const char *errors[] = {
	"none", "no CARD= in ENV:Wrap.prefs", "the card named by CARD= did not open",
	"CARD= names Wrap.card itself", "too many boards", "thunk table mismatch, not wrapping",
	"InitCard for an unknown board"
};

static const char *mirror_errors[] = {
	"none", "the MIRROR= card did not open", "its FindCard found no board",
	"its InitCard failed", "no memory", "no mirror process",
	"it cannot show the wrapped board's pixel format",
	"the screen does not fit in its video memory"
};

static const char template[] = "ALL/S";

static struct WrapStatus st;
static struct WrapBoard boards[WRAP_MAXBOARDS];
static struct WrapMirror mirror;

int main(void)
{
	LONG args[1] = { 0 };
	struct RDArgs *rda;
	struct WrapBase *base;
	char innername[WRAP_NAMELEN];
	UWORD innerver = 0, innerrev = 0;
	UWORD i, j;

	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), "wrapstat");
		return RETURN_FAIL;
	}

	innername[0] = 0;
	Forbid();
	base = (struct WrapBase *)FindName(&SysBase->LibList, "Wrap.card");
	if (base && base->status.magic == WRAP_STATUS_MAGIC) {
		st = base->status;
		for (i = 0; i < WRAP_MAXBOARDS; i++)
			boards[i] = st.boards[i];
		if (st.mirror)
			mirror = *st.mirror;
		if (st.inner) {
			const char *n = st.inner->lib_Node.ln_Name;

			for (j = 0; n && n[j] && j < WRAP_NAMELEN - 1; j++)
				innername[j] = n[j];
			innername[j] = 0;
			innerver = st.inner->lib_Version;
			innerrev = st.inner->lib_Revision;
		}
	}
	Permit();

	if (!base) {
		printf("Wrap.card: not loaded\n");
		FreeArgs(rda);
		return RETURN_WARN;
	}
	printf("Wrap.card: loaded, version %u.%u, open count %u\n", base->lib.lib_Version,
	       base->lib.lib_Revision, base->lib.lib_OpenCnt);
	if (st.magic != WRAP_STATUS_MAGIC) {
		printf("  no status block\n");
		FreeArgs(rda);
		return RETURN_WARN;
	}
	printf("  CARD=%s DRAW=%u\n", st.card[0] ? st.card : "(none)", st.draw);
	if (innername[0])
		printf("  wrapping %s %u.%u\n", innername, innerver, innerrev);
	else
		printf("  wrapping nothing\n");
	printf("  FindCard called %u times (card found %u), InitCard %u times (card set up %u)\n",
	       st.findcard_calls, st.inner_found, st.initcard_calls, st.inner_init);
	printf("  last error: %s\n", st.error < sizeof(errors) / sizeof(errors[0]) ?
	       errors[st.error] : "?");

	for (i = 0; i < st.nboards && i < WRAP_MAXBOARDS; i++) {
		struct WrapBoard *b = &boards[i];
		struct WrapDisplay *d = &b->disp;

		printf("board %u: %s, %u functions wrapped\n", i,
		       b->initialised ? "set up" : "found, not set up", b->wrapped);
		printf("  display %s, switch %s, %ux%u, format %lu, bitmap at 0x%08lx width %u"
		       " offset %d,%d (%lu changes)\n",
		       d->display ? "on" : "off", d->moniswitch ? "on" : "off", d->width, d->height,
		       d->format, (ULONG)d->mem, d->bm_width, d->xoff, d->yoff, d->serial);
		for (j = 0; j < WRAP_NSLOTS; j++) {
			if (b->inner[j])
				printf("  %-30s %lu\n", slot_names[j], b->calls[j]);
			else if (args[0])
				printf("  %-30s -\n", slot_names[j]);
		}
	}
	if (mirror.card[0]) {
		printf("mirror: MIRROR=%s MIRRORFPS=%u, %s\n", mirror.card, mirror.fps,
		       mirror.task ? "running" : "not running");
		printf("  last error: %s\n", mirror.error < sizeof(mirror_errors) / sizeof(mirror_errors[0]) ?
		       mirror_errors[mirror.error] : "?");
		if (mirror.bi)
			printf("  board %s, display %s, %ux%u, format %lu, at 0x%08lx, %lu bytes per row\n",
			       mirror.bi->BoardName ? mirror.bi->BoardName : "(no name)",
			       mirror.display ? "on" : "off", mirror.width, mirror.height,
			       mirror.format, (ULONG)mirror.mem, mirror.bpr);
		printf("  modes set up %lu, frames copied %lu\n", mirror.setups, mirror.frames);
	}
	FreeArgs(rda);
	return RETURN_OK;
}
