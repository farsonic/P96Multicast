/*
 * Panorama - Panorama.card internals.
 *
 * The card has no hardware: its "video RAM" is a block of Fast RAM, and a
 * sender process (sender.c) streams what is visible on it to a UDP multicast
 * group, in the Copper protocol (include/copper.h). The BoardInfo functions
 * (card.c) record the display state the sender needs, the drawing functions
 * (orders.c) what is drawn on the screen.
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

#define MC_CARDNAME "Panorama.card"
#define MC_VERSION_STR "2.9"
#define MC_REVISION    9      /* the library's lib_Revision: keep with the string and libstart.s */
#define MC_DATE "26.10.2026"

extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct GfxBase *GfxBase;

/* MODES=: at most this many sizes */
#define MC_MAXMODES 24

/* SPLIT=CxR: at most this many parts, each sent to a port of its own */
#define MC_MAXVIEWS 4

/* settings: built-in defaults < monitor ToolTypes < ENV:Panorama.prefs
   (ENVARC: if ENV: has none yet) */
struct McConfig {
	ULONG dest;        /* IPv4 destination, host byte order (=network on 68k) */
	ULONG part_dest[MC_MAXVIEWS];  /* SPLIT=: DEST2= ... for the further parts, 0: from DEST */
	UWORD port;
	UBYTE ttl;
	BYTE  pri;         /* sender task priority */
	UWORD fps;         /* update passes per second */
	ULONG rate;        /* byte budget per second */
	UWORD refresh;     /* seconds between whole screens while no viewer talks back */
	ULONG vramsize;    /* bytes */
	UWORD maxwidth, maxheight;
	UWORD boardtype;
	UWORD boards;      /* boards to offer, 1-MC_MAXBOARDS */
	UWORD blitter;     /* send the drawing functions as orders (BLITTER=1) */
	UWORD split_cols, split_rows;  /* SPLIT=CxR: a stream for each part of the screen */
	UWORD pointer;     /* a hardware pointer, drawn by the viewers (POINTER=1) */
	UWORD scan;        /* seconds to compare the whole screen at most once */
	UWORD cpu;         /* percent of the CPU the background comparing may take */
	ULONG history;     /* bytes of sent datagrams kept for repairs */
	UWORD nmodes;      /* MODES=: sizes to offer, 0 for the built-in list */
	UWORD nomodes;     /* MODES=NONE: offer none, P96's settings do */
	UWORD mode_w[MC_MAXMODES], mode_h[MC_MAXMODES];
	UWORD input;       /* INPUT=1: the viewers' mouse and keys work the Amiga */
	ULONG input_from;  /* INPUTFROM=address: only that viewer's (0: any viewer's) */
	UWORD quiet;
	char  log[80];     /* sender log file, "" for none */
};

/*
 * BOARDS=n: up to this many boards, each with its own video memory and
 * sender. Board n sends to PORT + n and is called "Panorama<n+1>" from the
 * second one on, so its modes can be told apart.
 */
#define MC_MAXBOARDS 8


/*
 * The order ring: the drawing functions (orders.c) put what they draw on
 * the visible screen here, in screen coordinates, and the sender takes it
 * off. Records are McRec headers with a body, a multiple of 4 bytes, and
 * never wrap: a PAD record fills the end of the ring instead.
 */
#define MC_RING_SIZE 131072UL

#define MC_R_PAD     0
#define MC_R_FILL    1     /* pen */
#define MC_R_INVERT  2
#define MC_R_COPY    3     /* sx, sy */
#define MC_R_TEXT    4     /* bitx, stride, fg, bg, bits */
#define MC_R_PATTERN 5     /* rowmask, px, py, pad, fg, bg, rows */
#define MC_R_DAMAGE  6     /* drawn, but not as an order: compare it */

struct McRec {
	UWORD len;             /* with the header */
	UBYTE type;            /* MC_R_* */
	UBYTE mode;            /* TEXT, PATTERN: draw mode */
	WORD  x, y, w, h;
};

struct McCopy { struct McRec r; WORD sx, sy; };
struct McFill { struct McRec r; ULONG pen; };
struct McText { struct McRec r; UWORD bitx, stride; ULONG fg, bg; };
struct McPattern { struct McRec r; UBYTE rowmask, px, py, pad; ULONG fg, bg; };

/* the largest pointer kept: 64 x 64, two planes */
#define MC_PTR_MAXW 64
#define MC_PTR_MAXH 64

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

	/*
	 * The order ring: bytes put in so far, and taken off by the slowest
	 * sender (each part of a SPLIT= screen has its own, view_tail).
	 */
	UBYTE *ring;
	volatile ULONG ring_head, ring_tail;
	volatile ULONG view_tail[MC_MAXVIEWS];
	volatile ULONG ring_lost;    /* times it overflowed: compare everything */
	volatile UWORD ring_full;
	UWORD  nviews;               /* senders: SPLIT= columns x rows */

	/*
	 * SPLIT=: the parts keep in step. The first part's sender starts each
	 * pass: it bumps tick, fixes tick_head (how far on the ring this pass
	 * goes) and signals the others (CTRL-F), so every part shows the same
	 * moment and says so in its FRAME.
	 */
	volatile ULONG tick, tick_head;
	struct Task *view_task[MC_MAXVIEWS];
	volatile UWORD lead_gone;    /* the first part's sender has stopped */

	/* INPUT=1: the qualifiers (shift, buttons, ...) the viewers hold down */
	volatile UWORD in_qual;

	/* POINTER=1: the hardware pointer */
	volatile ULONG ptr_serial;   /* shape or colours changed */
	volatile ULONG pos_serial;   /* moved, shown or hidden */
	WORD   ptr_x, ptr_y;         /* top left, on the bitmap */
	UWORD  ptr_on;
	UBYTE  ptr_w, ptr_h;
	BYTE   ptr_hx, ptr_hy;       /* the hot spot, from the top left */
	UBYTE  ptr_rgb[9];           /* colours 1-3 */
	UWORD  ptr_data[MC_PTR_MAXH * 2 * (MC_PTR_MAXW / 16)];  /* rows: plane 0, plane 1 */
};

#define MC_CARD(bi) ((struct McCard *)(bi)->CardData[0])

/* card.c */
ULONG mc_bytesperpixel(RGBFTYPE fmt);
ULONG mc_bytesperrow(UWORD width, RGBFTYPE fmt);
extern struct McStatus *mc_status;

/* modes.c */
void mc_modes_build(struct McCard *mc);

/* orders.c */
void mc_blitter_install(struct McCard *mc);
void mc_pointer_install(struct McCard *mc);
void mc_record(struct McCard *mc, ULONG kind, ULONG *regs);
void mc_damage_rect(struct McCard *mc, LONG x, LONG y, LONG w, LONG h);
void mc_pointer_image(struct McCard *mc, struct BoardInfo *bi);
void mc_pointer_pos(struct McCard *mc, struct BoardInfo *bi);
void mc_pointer_colour(struct McCard *mc, ULONG index, ULONG r, ULONG g, ULONG b);
void mc_pointer_show(struct McCard *mc, ULONG on);

/* config.c */
void mc_config_defaults(struct McConfig *cfg);
void mc_config_line(struct McConfig *cfg, const char *line);
BOOL mc_config_file(struct McConfig *cfg, const char *path);
void mc_config_env(struct McConfig *cfg);

/* sender.c */
BOOL mc_sender_start(struct McCard *mc);

#endif
