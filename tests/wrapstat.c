/*
 * Panorama - wrapstat: what Wrap.card wraps and what the wrapped card
 * has been asked to do.
 *
 *   wrapstat            the wrapped card, its boards, display state, calls,
 *                       and the heads (MIRROR=, SPAN=)
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
	"none", "its card did not open", "its FindCard found no board",
	"its InitCard failed", "no memory", "no process to copy the screen",
	"it cannot show the wrapped board's pixel format",
	"its tile does not fit in its video memory"
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
	printf("  CARD=%s DRAW=%u KEEPDISPLAY=%u\n", st.card[0] ? st.card : "(none)", st.draw,
	       st.keepdisplay);
	if (st.keepdisplay)
		printf("  display kept on: %lu \"off\" calls passed on as \"on\"\n", st.kept_on);
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
	if (mirror.nheads) {
		if (mirror.nheads > 1)
			printf("heads: SPAN=%ux%u", mirror.cols, mirror.rows);
		else
			printf("heads: MIRROR=%s", mirror.head[0].card);
		printf(" MIRRORFPS=%u HEADSHARE=%u, %s, %lu passes\n", mirror.fps, mirror.share,
		       mirror.task ? "running" : "not running", mirror.passes);
	}
	for (i = 0; i < mirror.nheads && i < WRAP_MAXHEADS; i++) {
		struct WrapHead *h = &mirror.head[i];

		printf("head %d: %s, %s", i + 1, h->card, h->bi ? "set up" : "not set up");
		if (h->error) {
			const char *e = "?";

			if (h->error < sizeof(mirror_errors) / sizeof(mirror_errors[0]))
				e = mirror_errors[h->error];
			printf(", %s", e);
		}
		printf("\n");
		if (h->bi) {
			printf("  board %s, display %s, tile at %u,%u, %ux%u, format %lu, at 0x%08lx,"
			       " %lu bytes per row, %lu modes\n",
			       h->bi->BoardName ? h->bi->BoardName : "(no name)",
			       h->display ? "on" : "off", h->x, h->y, h->width, h->height,
			       h->format, (ULONG)h->mem, h->bpr, h->setups);
			if (h->shared)
				printf("  shows the wrapped board's bitmap itself (HEADSHARE)\n");
			if (h->orders)
				printf("  hears the wrapped board's drawing, sends it as orders\n");
			else
				printf("  %lu passes, %lu with changes, %lu KB copied\n", h->frames,
				       h->changed, h->copied_kb);
		}
	}
	FreeArgs(rda);
	return RETURN_OK;
}
