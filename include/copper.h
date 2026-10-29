/*
 * Panorama - Copper, the display protocol (see docs/COPPER.md).
 *
 * The Amiga's Copper runs a list of display instructions; this protocol
 * streams one: what was drawn (a fill, a copy, a line of text) whenever the
 * viewer can draw it itself, pixels only for the rest. Datagrams are
 * numbered, and viewers ask for what they missed.
 *
 * All numbers are big-endian. A pixel is as it is in the Amiga's video
 * memory, in the format the last SURFACE named.
 */
#ifndef P96MC_COPPER_H
#define P96MC_COPPER_H

#define CU_MAGIC    0x4355     /* "CU" */
#define CU_VERSION  1

#define CU_DEFAULT_GROUP 0xEFFF6001UL  /* 239.255.96.1 */
#define CU_DEFAULT_PORT  9696

#define CU_HDR_SIZE   12
#define CU_MAX_DGRAM  1472     /* fits a 1500 byte Ethernet MTU */

/*
 * Datagram header
 *  0  UWORD magic     CU_MAGIC
 *  2  UBYTE version   CU_VERSION
 *  3  UBYTE flags     CU_F_*
 *  4  ULONG stream    random when the card starts: a new one means start over
 *  8  ULONG seq       +1 for each new datagram
 * then records, back to back: UBYTE type, UWORD length of the body, body.
 * Unknown types are skipped by their length.
 */
#define CU_F_REPAIR   0x01     /* sent again, because a viewer asked */

/* records */
#define CU_SURFACE    0x01     /* w, h, format u8, bpp u8, display u8, pad u8, tile w, tile h */
#define CU_PALETTE    0x02     /* first u16, count u16, count * RGB */
#define CU_FRAME      0x03     /* frame u32: show what came before; then (card 3.2) time u32:
                                  ms on the Amiga's clock when the screen was looked at */
#define CU_FILL       0x10     /* x, y, w, h, pixel */
#define CU_INVERT     0x11     /* x, y, w, h: every bit of every pixel flipped */
#define CU_COPY       0x12     /* sx, sy, dx, dy, w, h: as if through a buffer */
#define CU_TEXT       0x13     /* x, y, w, h, mode u8, pad u8, bitx u16, stride u16,
                                  fg, bg, h * stride bytes */
#define CU_PATTERN    0x14     /* x, y, w, h, mode u8, rowmask u8, px u8, py u8, fg, bg,
                                  (rowmask + 1) * u16 */
#define CU_PIXELS     0x20     /* x, y, w, h, encoding u8, pad u8, data */
#define CU_POINTER    0x30     /* w u8, h u8, hotx s8, hoty s8, 3 * RGB, h rows of
                                  plane 0 then plane 1, (w + 15) / 16 words each */
#define CU_POINTERPOS 0x31     /* x s16, y s16, visible u8, pad u8: top left on the surface */
#define CU_SPRITE     0x32     /* n u8 (1-7), visible u8, x s16, y s16 (top left on the
                                  surface), image u16: sprite n shows that image there */
#define CU_SPRITEIMAGE 0x33    /* image u16 (0-63), w u8 (16, 32, 64), depth u8 (2; 4 for an
                                  attached pair), h u16, first u16, rows u16, colours 1-3
                                  (depth 2) or 1-15 (depth 4) as RGB, rows of planes 0 ...
                                  depth - 1, w / 16 words each: rows first ... of image's
                                  h, the rest in further SPRITEIMAGEs; kept until replaced */
#define CU_SPRITE_IMAGES 64

/* TEXT and PATTERN draw modes, as graphics.library's */
#define CU_JAM1       0
#define CU_JAM2       1
#define CU_COMPLEMENT 2
#define CU_INVERSVID  4

/* PIXELS encodings */
#define CU_RAW        0        /* w * h pixels */
#define CU_SOLID      1        /* one pixel for all of it */
#define CU_RLE        2        /* each row: runs of (count u8 1-255, pixel) */

/*
 * Viewer to card: unicast to the address the datagrams come from.
 *  0  UWORD magic, UBYTE version, UBYTE type
 */
#define CU_HELLO      1        /* ULONG stream, ULONG next seq wanted: I am here */
#define CU_NAK        2        /* ULONG stream, then up to 64 * (ULONG first, UWORD count) */
#define CU_WANT       3        /* ULONG stream, then up to 128 * (x, y, w, h): send these
                                  pixels again (all 0: the whole surface) */

/* SURFACE flags (the byte after display on) */
#define CU_SF_INPUT   1        /* the card takes the viewers' mouse and keys (INPUT=1) */
#define CU_SF_BEHIND  2        /* not on the Amiga's display now (another screen is in
                                  front); what was there last, still sent */

#define CU_INPUT      4        /* ULONG stream, then up to 128 * (kind u8, a u8, x u16, y u16):
                                  the viewer's mouse and keys, with INPUT=1 */
#define CU_WANTSPRITES 5       /* ULONG stream: send the pointer and every sprite and its
                                  image again (one was lost: a shown sprite's image
                                  isn't here) */

/* CU_INPUT kinds */
#define CU_IN_MOVE    1        /* the pointer to x, y on this part of the screen */
#define CU_IN_BUTTON  2        /* a: 1 left, 2 right, 3 middle; | 0x80 released */
#define CU_IN_KEY     3        /* a: Amiga raw key code; | 0x80 released */
#define CU_IN_LEAVE   4        /* the pointer has left the viewer's windows */
/*
 * Not on the wire: the ie_SubClass of the RAWMOUSE events Panorama.card
 * writes for the viewers, so commodities (PanoramaDesk) can tell them from
 * the Amiga's own mouse. Intuition doesn't look at a RAWMOUSE's SubClass.
 */
#define CU_IN_TAG     0x50

#define CU_CTRL_MAX   1024

#endif
