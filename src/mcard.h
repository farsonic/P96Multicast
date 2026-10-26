/*
 * P96Multicast - Multicast.card internals.
 *
 * The card has no hardware: its "video RAM" is a block of Fast RAM, and a
 * sender process (sender.c) streams what is visible on it to a UDP multicast
 * group. The BoardInfo functions (card.c) only record the display state the
 * sender needs.
 *
 * BoardInfo functions run in the context of whatever task called rtg.library,
 * with a6 undefined. Per-card state lives in struct McCard, reached through
 * bi->CardData[0]; the only globals are library bases, which are the same for
 * every card.
 */
#ifndef P96MC_MCARD_H
#define P96MC_MCARD_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dosextens.h>
#include "boardinfo.h"
#include "mc_status.h"

#define MC_CARDNAME "Multicast.card"
#define MC_VERSION_STR "1.3"
#define MC_DATE "26.10.2026"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct GfxBase *GfxBase;

/* MODES=: at most this many sizes */
#define MC_MAXMODES 24

/* settings: built-in defaults < monitor ToolTypes < ENV:Multicast.prefs
   (ENVARC: if ENV: has none yet) */
struct McConfig {
	ULONG dest;        /* IPv4 destination, host byte order (=network on 68k) */
	UWORD port;
	UBYTE ttl;
	BYTE  pri;         /* sender task priority */
	UWORD fps;         /* update passes per second */
	ULONG rate;        /* byte budget per second */
	UWORD refresh;     /* seconds to resend the whole screen for late joiners */
	ULONG vramsize;    /* bytes */
	UWORD maxwidth, maxheight;
	UWORD boardtype;
	UWORD boards;      /* boards to offer, 1-MC_MAXBOARDS */
	UWORD blitter;     /* record the drawing functions (BLITTER=1) */
	UWORD nmodes;      /* MODES=: sizes to offer, 0 for the built-in list */
	UWORD nomodes;     /* MODES=NONE: offer none, P96's settings do */
	UWORD mode_w[MC_MAXMODES], mode_h[MC_MAXMODES];
	UWORD quiet;
	char  log[80];     /* sender log file, "" for none */
};

/*
 * BOARDS=n: up to this many boards, each with its own video memory and
 * sender. Board n sends to PORT + n and is called "Multicast<n+1>" from the
 * second one on, so its modes can be told apart.
 */
#define MC_MAXBOARDS 8

/*
 * BLITTER=1: the drawing functions put the rectangles they change on the
 * visible screen here (screen coordinates), and the sender takes them off.
 */
#define MC_DAMAGE_RING 1024

struct McRect {
	WORD  x, y, w, h;
	UWORD kind;            /* MC_BLIT_* */
	UWORD pad;
};

struct McCard {
	struct BoardInfo *bi;
	UWORD  boardnum;       /* 0 for the first board */
	char   boardname[16];
	struct McConfig cfg;
	APTR   vram_alloc;     /* AllocMem() block, vram is aligned inside it */
	ULONG  vram_allocsize;

	/*
	 * Display state, written by the BoardInfo functions, read by the
	 * sender under Forbid(). Every change bumps serial.
	 */
	volatile ULONG serial;
	UBYTE *bm_mem;         /* start of the displayed bitmap (SetPanning) */
	UWORD  bm_width;       /* its width in pixels */
	WORD   xoff, yoff;     /* top left of the visible part */
	UWORD  width, height;  /* visible size (SetGC) */
	RGBFTYPE format;       /* SetDAC */
	UWORD  display;        /* SetDisplay */
	UWORD  moniswitch;     /* SetSwitch */

	volatile ULONG pal_serial;
	UBYTE  pal[256 * 3];

	UBYTE  clockindex;

	struct Process *sender;
	volatile UWORD sender_quit;

	APTR   modes;          /* the resolutions made for P96 (modes.c) */
	ULONG  modessize;

	struct McRect damage[MC_DAMAGE_RING];
	volatile ULONG damage_head;  /* rectangles put in so far */
};

#define MC_CARD(bi) ((struct McCard *)(bi)->CardData[0])

/* card.c */
ULONG mc_bytesperpixel(RGBFTYPE fmt);
ULONG mc_bytesperrow(UWORD width, RGBFTYPE fmt);
extern struct McStatus *mc_status;

/* modes.c */
void mc_modes_build(struct McCard *mc);

/* blit.c */
void mc_blitter_install(struct McCard *mc);

/* config.c */
void mc_config_defaults(struct McConfig *cfg);
void mc_config_line(struct McConfig *cfg, const char *line);
BOOL mc_config_file(struct McConfig *cfg, const char *path);
void mc_config_env(struct McConfig *cfg);

/* sender.c */
BOOL mc_sender_start(struct McCard *mc);

#endif
