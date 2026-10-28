/*
 * Panorama - Fake.card for tests/wraptest.c (see fakecard.h).
 *
 * InitCard fills in SetGC, SetPanning, SetDisplay and FillRect with
 * FakeRecord and SetMemoryMode with FakeKeep, sets BlitRect to rtg.library's
 * BlitRectDefault (a card without its own BlitRect), and leaves the rest as
 * rtg.library set them.
 */
#include <exec/execbase.h>
#include <proto/exec.h>
#include "boardinfo.h"
#include "fakecard.h"

extern void FakeRecord(void);
extern void FakeKeep(void);
extern ULONG FakeRecordRegs[];
extern ULONG FakeKeepRegs[];
extern ULONG FakeRecordCalls;
extern ULONG FakeKeepCalls;

typedef char libsize_check[sizeof(struct FakeBase) <= FAKE_LIBSIZE ? 1 : -1];

static struct FakeBase *fake;

struct Library *LibInit(__reg("d0") struct FakeBase *base, __reg("a0") BPTR seglist,
                        __reg("a6") struct ExecBase *sysbase)
{
	fake = base;
	base->seglist = seglist;
	base->lib.lib_Node.ln_Type = NT_LIBRARY;
	base->lib.lib_Node.ln_Name = "Fake.card";
	base->lib.lib_Version = 1;
	base->record = FakeRecordRegs;
	base->keep = FakeKeepRegs;
	base->record_fn = (APTR)FakeRecord;
	base->keep_fn = (APTR)FakeKeep;
	base->record_calls = &FakeRecordCalls;
	base->keep_calls = &FakeKeepCalls;
	return &base->lib;
}

/* one board, and only when rtg.library says the card is this library */
BOOL FindCard(__reg("a0") struct BoardInfo *bi)
{
	fake->findcard_calls++;
	if (fake->findcard_calls > 1 || bi->CardBase != &fake->lib)
		return FALSE;
	bi->CardData[0] = FAKE_CARDDATA;
	return TRUE;
}

BOOL InitCard(__reg("a0") struct BoardInfo *bi, __reg("a1") char **tooltypes)
{
	fake->initcard_calls++;
	if (bi->CardData[0] != FAKE_CARDDATA || bi->CardBase != &fake->lib)
		return FALSE;
	bi->BoardName = "Fake";
	bi->SetGC = (APTR)FakeRecord;
	bi->SetPanning = (APTR)FakeRecord;
	bi->SetDisplay = (APTR)FakeRecord;
	bi->FillRect = (APTR)FakeRecord;
	bi->SetMemoryMode = (APTR)FakeKeep;
	bi->BlitRect = bi->BlitRectDefault;
	return TRUE;
}
