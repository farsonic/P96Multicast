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
 * records what it draws, as an order for the viewers or as a rectangle to
 * compare, and goes on to rtg.library's software version.
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
/* not BoardInfo functions: Wrap.card's heads (McDamage) */
#define MC_BLIT_DAMAGE    9

/* McStatus.settings_read */
#define MC_SET_NOTASKED  0  /* no MODES=: not needed */
#define MC_SET_NOPROCESS 1  /* InitCard ran in a task, which can't read files */
#define MC_SET_NOFILE    2  /* no DEVS:Picasso96Settings */
#define MC_SET_READ      3

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
	UWORD settings_read;   /* MC_SET_*: could InitCard read DEVS:Picasso96Settings */
	UWORD settings_ids;    /* ... and the 0x50xx1000 DisplayIDs it found there */
	ULONG first_id;        /* the first DisplayID the card gave a mode */

	/* the first board: what it hears about and what it sends (Copper) */
	UWORD blitter;         /* drawing calls are sent as orders */
	UWORD pointer;         /* the hardware pointer is in use */
	ULONG blit_calls[MC_BLIT_KINDS];   /* calls of each */
	ULONG blit_screen[MC_BLIT_KINDS];  /* ... drawing on the visible screen */
	ULONG orders[MC_BLIT_KINDS];       /* ... sent as orders, not compared */
	ULONG ring_lost;       /* the order ring overflowed */
	ULONG pixel_tiles;     /* tiles sent as pixels */
	ULONG pixel_kb;        /* ... their size */
	ULONG sent_kb;         /* everything sent */
	ULONG datagrams;
	ULONG repairs;         /* datagrams sent again for a viewer */
	ULONG wanted;          /* tiles sent again for a viewer */
	ULONG heard;           /* messages from viewers */
	UWORD viewers;         /* viewers heard from in the last 10 s */
	UWORD sweep_secs;      /* seconds the background comparing takes for the screen */
	ULONG send_errors;
	ULONG pointer_moves;
};

struct McBase {
	struct Library lib;
	BPTR seglist;
	struct McStatus status;
};

/* src/libstart.s allocates this many bytes for the base */
#define MC_LIBSIZE 512

#endif
