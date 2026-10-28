/*
 * P96Multicast - Wrap.card: the BoardInfo function slots it can wrap, and
 * the status block in its library base, read by "wrapstat".
 */
#ifndef P96MC_WRAP_STATUS_H
#define P96MC_WRAP_STATUS_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include "boardinfo.h"

#define WRAP_STATUS_MAGIC 0x57525354UL  /* "WRST" */
#define WRAP_MAXBOARDS    4
#define WRAP_NAMELEN      32

/*
 * The BoardInfo function pointers, in order from AllocCardMem (slot 0) to
 * DeleteFeature, so slot n is at offsetof(AllocCardMem) + 4 * n.
 *
 *   X(field, kind, default)
 *
 * kind: WK_STATE for display state and setup, WK_DRAW for drawing (WRAP
 * DRAW=0 leaves those alone), WK_NONE for slots that are never wrapped:
 * rtg.library's own defaults, and the reserved ones.
 * default: the slot holding rtg.library's software fallback for this
 * function, or -1. A function still equal to its fallback is not the card's
 * and is not wrapped either.
 */
#define WK_NONE  0
#define WK_STATE 1
#define WK_DRAW  2

#define WRAP_SLOTS(X) \
	X(AllocCardMem,                  WK_STATE, -1) \
	X(FreeCardMem,                   WK_STATE, -1) \
	X(SetSwitch,                     WK_STATE, -1) \
	X(SetColorArray,                 WK_STATE, -1) \
	X(SetDAC,                        WK_STATE, -1) \
	X(SetGC,                         WK_STATE, -1) \
	X(SetPanning,                    WK_STATE, -1) \
	X(CalculateBytesPerRow,          WK_STATE, -1) \
	X(CalculateMemory,               WK_STATE, -1) \
	X(GetCompatibleFormats,          WK_STATE, -1) \
	X(SetDisplay,                    WK_STATE, -1) \
	X(ResolvePixelClock,             WK_STATE, -1) \
	X(GetPixelClock,                 WK_STATE, -1) \
	X(SetClock,                      WK_STATE, -1) \
	X(SetMemoryMode,                 WK_STATE, -1) \
	X(SetWriteMask,                  WK_STATE, -1) \
	X(SetClearMask,                  WK_STATE, -1) \
	X(SetReadPlane,                  WK_STATE, -1) \
	X(WaitVerticalSync,              WK_STATE, -1) \
	X(SetInterrupt,                  WK_STATE, -1) \
	X(WaitBlitter,                   WK_DRAW,  -1) \
	X(ScrollPlanar,                  WK_DRAW,  22) \
	X(ScrollPlanarDefault,           WK_NONE,  -1) \
	X(UpdatePlanar,                  WK_DRAW,  24) \
	X(UpdatePlanarDefault,           WK_NONE,  -1) \
	X(BlitPlanar2Chunky,             WK_DRAW,  26) \
	X(BlitPlanar2ChunkyDefault,      WK_NONE,  -1) \
	X(FillRect,                      WK_DRAW,  28) \
	X(FillRectDefault,               WK_NONE,  -1) \
	X(InvertRect,                    WK_DRAW,  30) \
	X(InvertRectDefault,             WK_NONE,  -1) \
	X(BlitRect,                      WK_DRAW,  32) \
	X(BlitRectDefault,               WK_NONE,  -1) \
	X(BlitTemplate,                  WK_DRAW,  34) \
	X(BlitTemplateDefault,           WK_NONE,  -1) \
	X(BlitPattern,                   WK_DRAW,  36) \
	X(BlitPatternDefault,            WK_NONE,  -1) \
	X(DrawLine,                      WK_DRAW,  38) \
	X(DrawLineDefault,               WK_NONE,  -1) \
	X(BlitRectNoMaskComplete,        WK_DRAW,  40) \
	X(BlitRectNoMaskCompleteDefault, WK_NONE,  -1) \
	X(BlitPlanar2Direct,             WK_DRAW,  42) \
	X(BlitPlanar2DirectDefault,      WK_NONE,  -1) \
	X(EnableSoftSprite,              WK_NONE,  -1) \
	X(Reserved0Default,              WK_NONE,  -1) \
	X(Reserved1,                     WK_NONE,  -1) \
	X(SetSplitPosition,              WK_STATE, -1) \
	X(ReInitMemory,                  WK_STATE, -1) \
	X(GetCompatibleDACFormats,       WK_STATE, -1) \
	X(CoerceMode,                    WK_STATE, -1) \
	X(Reserved3Default,              WK_NONE,  -1) \
	X(BlitRectTransparent,           WK_DRAW,  52) \
	X(BlitRectTransparentDefault,    WK_NONE,  -1) \
	X(GetVSyncState,                 WK_STATE, -1) \
	X(GetVBeamPos,                   WK_STATE, -1) \
	X(SetDPMSLevel,                  WK_STATE, -1) \
	X(ResetChip,                     WK_STATE, -1) \
	X(GetFeatureAttrs,               WK_STATE, -1) \
	X(AllocBitMap,                   WK_STATE, -1) \
	X(FreeBitMap,                    WK_STATE, -1) \
	X(GetBitMapAttr,                 WK_STATE, -1) \
	X(SetSprite,                     WK_STATE, -1) \
	X(SetSpritePosition,             WK_STATE, -1) \
	X(SetSpriteImage,                WK_STATE, -1) \
	X(SetSpriteColor,                WK_STATE, -1) \
	X(CreateFeature,                 WK_STATE, -1) \
	X(SetFeatureAttrs,               WK_STATE, -1) \
	X(DeleteFeature,                 WK_STATE, -1)

#define WRAP_NSLOTS 68

/* what the wrapped card was last told to show, written by the thunks */
struct WrapDisplay {
	ULONG serial;          /* bumped on every change below but the palette */
	ULONG pal_serial;      /* SetColorArray: bi->CLUT changed */
	struct ModeInfo mode;  /* SetGC: a copy of the mode */
	UBYTE *mem;            /* SetPanning: start of the displayed bitmap */
	UWORD bm_width;        /* its width in pixels */
	WORD  xoff, yoff;
	UWORD width, height;   /* SetGC */
	ULONG format;          /* SetDAC / SetPanning, an RGBFB_* */
	UWORD display;         /* SetDisplay */
	UWORD moniswitch;      /* SetSwitch */
};

struct WrapBoard {
	struct BoardInfo *bi;
	UWORD initialised;     /* InitCard succeeded, thunks installed */
	UWORD wrapped;         /* number of slots that go through a thunk */
	APTR  inner[WRAP_NSLOTS];  /* the card's function, NULL if not wrapped */
	ULONG calls[WRAP_NSLOTS];
	struct WrapDisplay disp;
};

/*
 * Heads: boards of other card drivers that show the first wrapped board's
 * screen. Wrap.card sets each one up on a BoardInfo of its own, which
 * rtg.library never sees, and a process copies the screen onto them,
 * following its mode, palette and display state.
 *
 * MIRROR=card: one head, showing what the wrapped board shows.
 * SPAN=CxR, HEAD=card (HEAD1= ... HEAD4= for each one): C x R heads, left
 * to right and top to bottom, each showing its tile of one big screen.
 * HEADSHARE=1: heads that can show any memory show the wrapped board's
 * bitmap itself instead of a copy.
 *
 * Multicast.card 2 heads hear what is drawn: the thunks pass the wrapped
 * board's drawing calls (and pointer) on to a shared head (McRecord,
 * McPointer), and the copying says what it copied to the others
 * (McDamage), so the head sends orders and compares only what changed.
 */
#define WRAP_MAXHEADS 4

#define WRAP_MERR_NONE    0
#define WRAP_MERR_OPEN    1  /* the head's card did not open */
#define WRAP_MERR_NOBOARD 2  /* its FindCard found no board */
#define WRAP_MERR_INIT    3  /* its InitCard failed */
#define WRAP_MERR_NOMEM   4
#define WRAP_MERR_NOTASK  5  /* no process to copy the screen */
#define WRAP_MERR_FORMAT  6  /* it cannot show the wrapped board's pixel format */
#define WRAP_MERR_TOOBIG  7  /* its tile does not fit in its video memory */

struct WrapHead {
	char  card[WRAP_NAMELEN];  /* its card driver */
	UWORD error;           /* WRAP_MERR_*, for the last attempt */
	UWORD display;         /* its display is on */
	UWORD shared;          /* it shows the wrapped board's bitmap (HEADSHARE) */
	UWORD orders;          /* it hears the wrapped board's drawing (Multicast.card 2) */
	struct Library *lib;   /* its card driver, open */
	struct BoardInfo *bi;  /* its board, once set up */
	ULONG setups;          /* modes set up on it */
	ULONG frames;          /* passes that compared its tile */
	ULONG changed;         /* ... and found something to copy */
	ULONG copied_kb;       /* KB copied to it */
	UWORD x, y;            /* its tile in the wrapped board's screen */
	UWORD width, height;   /* and its mode */
	ULONG format;
	UBYTE *mem;            /* where it shows it */
	ULONG bpr;             /* its bytes per row */
};

struct WrapMirror {
	UWORD fps;             /* MIRRORFPS= */
	UWORD cols, rows;      /* SPAN=, 1x1 for MIRROR= */
	UWORD nheads;          /* cols * rows, 0 for none */
	UWORD share;           /* HEADSHARE= */
	struct Process *task;  /* the process copying the screen */
	ULONG passes;
	struct WrapHead head[WRAP_MAXHEADS];
};

/* WrapStatus.error */
#define WRAP_ERR_NONE     0
#define WRAP_ERR_NOCARD   1  /* no CARD= in ENV:Wrap.prefs */
#define WRAP_ERR_OPEN     2  /* the card named by CARD= did not open */
#define WRAP_ERR_SELF     3  /* CARD= names Wrap.card itself */
#define WRAP_ERR_BOARDS   4  /* more than WRAP_MAXBOARDS boards */
#define WRAP_ERR_THUNKS   5  /* the thunk table is not what wrap.c expects */
#define WRAP_ERR_UNKNOWN  6  /* InitCard for a board FindCard never saw */

struct WrapStatus {
	ULONG magic;
	UWORD error;           /* WRAP_ERR_* */
	UWORD draw;            /* DRAW= setting */
	char  card[WRAP_NAMELEN];  /* CARD= */
	struct Library *inner; /* the wrapped card, while open */
	UWORD findcard_calls;
	UWORD initcard_calls;
	UWORD inner_found;     /* inner FindCard said yes */
	UWORD inner_init;      /* inner InitCard said yes */
	UWORD nboards;
	UWORD keepdisplay;     /* KEEPDISPLAY=1: never switch the wrapped card's display off */
	struct WrapBoard *boards;  /* WRAP_MAXBOARDS of them */
	struct WrapMirror *mirror;
	ULONG kept_on;         /* ... "off" calls (SetSwitch, SetDisplay) passed on as "on" */
};

struct WrapBase {
	struct Library lib;
	BPTR seglist;
	struct WrapStatus status;
};

/* src/wrapstart.s allocates this many bytes for the base */
#define WRAP_LIBSIZE 128

#endif
