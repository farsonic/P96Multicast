/*
 * P96Multicast - status block in the Multicast.card library base, for
 * "mctest STATUS": what rtg.library asked of the card and why setup failed,
 * readable without a log file or a debugger.
 */
#ifndef P96MC_STATUS_H
#define P96MC_STATUS_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>

#define MC_STATUS_MAGIC 0x4D435354UL  /* "MCST" */

/* McStatus.error: why the last FindCard/InitCard failed */
#define MC_ERR_NONE      0
#define MC_ERR_NOCONTEXT 1  /* no memory for the card's context */
#define MC_ERR_NOVRAM    2  /* not even 1 MB of Fast RAM for video memory */
#define MC_ERR_NOSENDER  3  /* could not create the sender process */

struct McStatus {
	ULONG magic;
	UWORD findcard_calls;
	UWORD initcard_calls;
	UWORD cards;           /* boards set up (0 or 1) */
	UWORD error;           /* MC_ERR_* */
	ULONG fast_largest;    /* largest free Fast RAM block when FindCard ran */
	ULONG vramsize;        /* video memory taken, bytes */
	ULONG setgc_calls;     /* SetGC: rtg.library set up a display mode */
	ULONG panning_calls;   /* SetPanning: a bitmap was put on display */
};

struct McBase {
	struct Library lib;
	BPTR seglist;
	struct McStatus status;
};

/* src/libstart.s allocates this many bytes for the base */
#define MC_LIBSIZE 80

#endif
