/*
 * P96Multicast - the wire protocol (see docs/PROTOCOL.md).
 *
 * Every datagram starts with a 16 byte header. All numbers are big-endian.
 * Pixel data is sent as it is in the Amiga's video memory, in the RGB format
 * named by the last MODE packet; the receiver converts it.
 */
#ifndef P96MC_PROTO_H
#define P96MC_PROTO_H

#define MC_MAGIC    0x5039364DUL   /* "P96M" */
#define MC_VERSION  1

#define MC_DEFAULT_GROUP 0xEFFF6001UL  /* 239.255.96.1 */
#define MC_DEFAULT_PORT  9696

/* packet types */
#define MC_MODE     1  /* screen geometry and format */
#define MC_PALETTE  2  /* CLUT entries */
#define MC_TILE     3  /* a rectangle of pixels */
#define MC_SYNC     4  /* end of one update pass: present the frame */

/* tile encodings */
#define MC_ENC_RAW   0  /* w * h pixels, rows packed without padding */
#define MC_ENC_SOLID 1  /* one pixel, fills the whole rectangle */

/* header flags */
#define MC_FLAG_REFRESH 0x0001  /* TILE: unchanged tile resent for late joiners */

#define MC_HDR_SIZE  16
#define MC_MAX_DGRAM 1472       /* fits a 1500 byte Ethernet MTU */

/*
 *  0  ULONG magic      MC_MAGIC
 *  4  UBYTE version    MC_VERSION
 *  5  UBYTE type       MC_*
 *  6  UWORD flags
 *  8  ULONG stream     random number chosen when the card starts
 * 12  ULONG frame      update pass number, the same for all packets of a pass
 *
 * MODE (after the header)
 * 16  UWORD width, height    visible size in pixels
 * 20  UBYTE format           P96 RGBFTYPE (RGBFB_*)
 * 21  UBYTE bytesperpixel
 * 22  UBYTE display          0 = display off (blank), 1 = on
 * 23  UBYTE pad
 * 24  UWORD tilew, tileh     nominal tile size (informational)
 * 28  UWORD fps              update passes per second
 * 30  UWORD pad
 *
 * PALETTE
 * 16  UWORD first, count
 * 20  UBYTE rgb[count][3]    8 bits per gun
 *
 * TILE
 * 16  UWORD x, y, w, h
 * 24  UBYTE encoding         MC_ENC_*
 * 25  UBYTE pad
 * 26  UBYTE data[]           RAW: w*h*bpp bytes; SOLID: bpp bytes
 *
 * SYNC
 * 16  UWORD tiles            tiles sent in this pass
 * 18  UWORD pad
 */

#endif
