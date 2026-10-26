/*
 * P96Multicast - wraptest: runs the built Wrap.card under vamos, wrapping
 * Fake.card, with this program in the place of rtg.library.
 *
 *   wraptest [DRAW [PATH]]   DRAW: the DRAW= in ENV:Wrap.prefs, 0 or 1 (default)
 *                            PATH: Wrap.card's file, default LIBS:Picasso96/Wrap.card
 *
 * tests/wraptest.sh sets up LIBS:Picasso96 and ENV: for it. Checks:
 * FindCard and InitCard reach the wrapped card with bi->CardBase set to it;
 * the card's own functions are wrapped and rtg.library's fallbacks are not;
 * a call through a wrapped function reaches the card with every register as
 * the caller set it, except a6 (the card's base), and returns with the
 * card's results and the caller's a6; a function that preserves all
 * registers still does through its thunk; Wrap.card records the display
 * state and counts the calls.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include "boardinfo.h"
#include "wrap_status.h"
#include "fakecard.h"

BOOL __FindCard(__reg("a6") struct Library *, __reg("a0") struct BoardInfo *) = "\tjsr\t-30(a6)";
BOOL __InitCard(__reg("a6") struct Library *, __reg("a0") struct BoardInfo *,
                __reg("a1") char **) = "\tjsr\t-36(a6)";

void call_regs(__reg("a0") APTR fn, __reg("a1") ULONG *in, __reg("a2") ULONG *out);
extern void RtgDefault(void);

#define X_ENUM(f, kind, def) SLOT_##f,
enum { WRAP_SLOTS(X_ENUM) SLOT_COUNT };

static int fails;

static void check(int ok, const char *what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		fails++;
}

static struct BoardInfo *new_board(struct Library *cardbase)
{
	struct BoardInfo *bi = AllocMem(sizeof(*bi) + 1024, MEMF_PUBLIC | MEMF_CLEAR);
	APTR *slots;
	int i;

	if (!bi) {
		printf("FAIL: no memory\n");
		exit(20);
	}
	slots = &bi->AllocCardMem;
	for (i = 0; i < WRAP_NSLOTS; i++)
		slots[i] = (APTR)RtgDefault;
	bi->CardBase = cardbase;
	bi->ExecBase = SysBase;
	return bi;
}

static void fill(ULONG *r, ULONG base)
{
	int i;

	for (i = 0; i < 15; i++)
		r[i] = base + i * 0x01010101UL;
}

/* registers first..last (0-7 d0-d7, 8-14 a0-a6) of a and b are the same */
static int same_regs(const ULONG *a, const ULONG *b, int first, int last)
{
	int i;

	for (i = first; i <= last; i++)
		if (a[i] != b[i]) {
			printf("  register %c%d: 0x%08lx, want 0x%08lx\n", i < 8 ? 'd' : 'a', i & 7,
			       a[i], b[i]);
			return 0;
		}
	return 1;
}

int main(int argc, char **argv)
{
	int draw = !(argc > 1 && argv[1][0] == '0');
	const char *path = argc > 2 ? argv[2] : "LIBS:Picasso96/Wrap.card";
	struct WrapBase *wrap;
	struct FakeBase *fake;
	struct WrapStatus *st;
	struct WrapBoard *b;
	struct BoardInfo *bi, *bi2;
	struct ModeInfo mi = { 0 };
	ULONG in[15], out[15];
	APTR fn;

	printf("wraptest: DRAW=%d\n", draw);
	wrap = (struct WrapBase *)OpenLibrary((STRPTR)path, 0);
	check(wrap != NULL, "Wrap.card opens");
	if (!wrap)
		return 20;
	st = &wrap->status;
	check(st->magic == WRAP_STATUS_MAGIC, "status block");

	bi = new_board(&wrap->lib);
	check(__FindCard(&wrap->lib, bi), "FindCard finds the wrapped card's board");
	fake = (struct FakeBase *)st->inner;
	check(fake != NULL && st->error == WRAP_ERR_NONE, "Fake.card opened");
	if (!fake)
		return 20;
	check(fake->findcard_calls == 1, "Fake.card's FindCard called once");
	check(bi->CardBase == &fake->lib, "bi->CardBase is the wrapped card");
	check(bi->CardData[0] == FAKE_CARDDATA, "CardData is the wrapped card's");

	check(__InitCard(&wrap->lib, bi, NULL), "InitCard");
	check(fake->initcard_calls == 1, "Fake.card's InitCard called once");
	check(st->nboards == 1 && st->inner_found == 1 && st->inner_init == 1, "one board");
	check(st->draw == draw, "DRAW= read from ENV:Wrap.prefs");
	b = &st->boards[0];
	check(b->bi == bi && b->initialised, "board set up");

	check(bi->SetPanning != fake->record_fn && b->inner[SLOT_SetPanning] == fake->record_fn,
	      "SetPanning wrapped");
	check(bi->SetMemoryMode != fake->keep_fn, "SetMemoryMode wrapped");
	check(bi->BlitRect == (APTR)RtgDefault && !b->inner[SLOT_BlitRect],
	      "BlitRect, still rtg.library's fallback, not wrapped");
	check(bi->BlitRectDefault == (APTR)RtgDefault && bi->FillRectDefault == (APTR)RtgDefault,
	      "fallback slots untouched");
	if (draw)
		check(bi->FillRect != fake->record_fn, "FillRect wrapped (DRAW=1)");
	else
		check(bi->FillRect == fake->record_fn, "FillRect not wrapped (DRAW=0)");
	printf("  %u functions wrapped\n", b->wrapped);

	/* SetPanning(a0 bi, a1 mem, d0 width, d1 x, d2 y, d7 format) */
	fill(in, 0xD0D0D0D0UL);
	in[0] = 640;
	in[1] = 16;
	in[2] = 32;
	in[7] = RGBFB_A8R8G8B8;
	in[8] = (ULONG)bi;
	in[9] = 0x00123400UL;
	in[14] = 0xA6A6A6A6UL;
	call_regs(bi->SetPanning, in, out);
	check(*fake->record_calls == 1, "SetPanning reached Fake.card");
	check(same_regs(fake->record, in, 0, 13), "... with the caller's d0-d7/a0-a5");
	check(fake->record[14] == (ULONG)fake, "... and a6 = the wrapped card's base");
	check(out[0] == FAKE_RESULT, "the card's d0 comes back");
	check(same_regs(out, in, 1, 14), "d1-d7/a0-a6 come back as the card left them, a6 the caller's");
	check(b->calls[SLOT_SetPanning] == 1, "SetPanning counted");
	check(b->disp.mem == (UBYTE *)0x00123400UL && b->disp.bm_width == 640 &&
	      b->disp.xoff == 16 && b->disp.yoff == 32 && b->disp.format == RGBFB_A8R8G8B8,
	      "SetPanning recorded");

	/* SetMemoryMode(a0 bi, d7 format) must preserve every register */
	fill(in, 0x10203040UL);
	in[8] = (ULONG)bi;
	in[14] = 0x5A5A5A5AUL;
	call_regs(bi->SetMemoryMode, in, out);
	check(*fake->keep_calls == 1, "SetMemoryMode reached Fake.card");
	check(same_regs(fake->keep, in, 0, 13) && fake->keep[14] == (ULONG)fake,
	      "... with the caller's registers, a6 the card's");
	check(same_regs(out, in, 0, 14), "all registers preserved through the thunk");

	/* SetGC(a0 bi, a1 modeinfo, d0 border), SetDisplay(a0 bi, d0 state) */
	mi.Width = 800;
	mi.Height = 600;
	fill(in, 0);
	in[0] = 0;
	in[8] = (ULONG)bi;
	in[9] = (ULONG)&mi;
	call_regs(bi->SetGC, in, out);
	in[0] = 1;
	call_regs(bi->SetDisplay, in, out);
	check(b->disp.width == 800 && b->disp.height == 600 && b->disp.display == 1,
	      "SetGC and SetDisplay recorded");

	fn = bi->FillRect;
	fill(in, 0x01000000UL);
	in[8] = (ULONG)bi;
	call_regs(fn, in, out);
	check(b->calls[SLOT_FillRect] == (draw ? 1 : 0), "FillRect counted only when wrapped");
	check(*fake->record_calls == 4, "every call reached Fake.card");

	bi2 = new_board(&wrap->lib);
	check(!__FindCard(&wrap->lib, bi2), "no second board");
	check(st->findcard_calls == 2 && st->nboards == 1, "still one board");

	printf("wraptest: %s\n", fails ? "FAILED" : "passed");
	return fails ? 20 : 0;
}
