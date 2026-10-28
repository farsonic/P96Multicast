/*
 * Panorama - Fake.card, a card driver for tests/wraptest.c: it finds
 * one board, fills in a few BoardInfo functions that record the registers
 * they were called with, and leaves the rest to rtg.library.
 */
#ifndef P96MC_FAKECARD_H
#define P96MC_FAKECARD_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>

#define FAKE_CARDDATA 0xFACECA4DUL   /* in CardData[0]: Wrap.card must leave it */
#define FAKE_RESULT   0x600DF00DUL   /* d0 returned by FakeRecord */

struct FakeBase {
	struct Library lib;
	BPTR  seglist;
	ULONG *record;      /* d0-d7/a0-a6 of the last FakeRecord call */
	ULONG *keep;        /* ... and of the last FakeKeep call */
	APTR  record_fn;    /* FakeRecord: returns FAKE_RESULT in d0 */
	APTR  keep_fn;      /* FakeKeep: preserves every register */
	UWORD findcard_calls;
	UWORD initcard_calls;
	ULONG *record_calls;
	ULONG *keep_calls;
};

#define FAKE_LIBSIZE 80

#endif
