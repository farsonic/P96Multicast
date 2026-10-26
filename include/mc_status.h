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

/*
 * BLITTER=1: the drawing functions rtg.library may call on the card. Each
 * records the rectangle it changes and goes on to rtg.library's software
 * version, so the counts below say how much of what changes on screen the
 * card hears about.
 */
#define MC_BLIT_FILL      0  /* FillRect */
#define MC_BLIT_INVERT    1  /* InvertRect */
#define MC_BLIT_RECT      2  /* BlitRect: copy within one bitmap */
#define MC_BLIT_TEMPLATE  3  /* BlitTemplate: text, one bit per pixel */
#define MC_BLIT_PATTERN   4  /* BlitPattern */
#define MC_BLIT_LINE      5  /* DrawLine */
#define MC_BLIT_COMPLETE  6  /* BlitRectNoMaskComplete: bitmap to bitmap */
#define MC_BLIT_P2C       7  /* BlitPlanar2Chunky */
#define MC_BLIT_P2D       8  /* BlitPlanar2Direct */
#define MC_BLIT_KINDS     9

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
	UWORD modes_before;    /* resolutions P96 had for the board at InitCard */
	UWORD modes_added;     /* ... and the card's own it added (modes.c) */

	/* BLITTER=1, the first board */
	UWORD blitter;         /* the drawing functions are recorded */
	UWORD pad;
	ULONG blit_calls[MC_BLIT_KINDS];   /* calls of each */
	ULONG blit_screen[MC_BLIT_KINDS];  /* ... drawing on the visible screen */
	ULONG screen_copies;   /* COMPLETE or RECT from the visible screen to itself */
	ULONG damage_lost;     /* the sender fell behind the damage ring */
	ULONG tiles_changed;   /* tiles the sender found changed (not a new mode) */
	ULONG tiles_explained; /* ... that a recorded call had touched */
	ULONG tiles_by[MC_BLIT_KINDS];     /* ... by the last kind that touched them */
	ULONG tiles_switch;    /* ... or by another bitmap put on display */
};

struct McBase {
	struct Library lib;
	BPTR seglist;
	struct McStatus status;
};

/* src/libstart.s allocates this many bytes for the base */
#define MC_LIBSIZE 256

#endif
