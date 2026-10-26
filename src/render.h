/*
 * P96Multicast - Copper's reference renderer (render.c).
 *
 * The card draws every order it sends into its shadow, the copy of what
 * the viewers have, with these functions. Viewers draw them the same way
 * (client/p96view.py), so the shadow stays what they show, and comparing
 * the screen with the shadow finds whatever the orders got wrong. Plain C,
 * built for the Amiga and, for tests, natively.
 *
 * Rectangles are already clipped to the surface.
 */
#ifndef P96MC_RENDER_H
#define P96MC_RENDER_H

struct CuSurf {
	unsigned char *mem;
	long bpr;
	int w, h, bpp;
};

/* a pixel as bpp bytes, from a pen as P96 passes it: the low bytes, big-endian */
void cu_pen(unsigned long pen, int bpp, unsigned char *px);

void cu_fill(struct CuSurf *s, int x, int y, int w, int h, const unsigned char *px);
void cu_invert(struct CuSurf *s, int x, int y, int w, int h);
void cu_copy(struct CuSurf *s, int sx, int sy, int dx, int dy, int w, int h);

/* bits: h rows of stride bytes, pixel i of a row is bit bitx + i, MSB first */
void cu_text(struct CuSurf *s, int x, int y, int w, int h, int mode,
             const unsigned char *bits, int bitx, int stride,
             const unsigned char *fg, const unsigned char *bg);

/* rows: rowmask + 1 big-endian words; pixel (i, j) is bit 15 - ((px + i) & 15)
   of row (py + j) & rowmask */
void cu_pattern(struct CuSurf *s, int x, int y, int w, int h, int mode,
                const unsigned char *rows, int rowmask, int px, int py,
                const unsigned char *fg, const unsigned char *bg);

/*
 * w x h pixels at src (bpr bytes per row) as a PIXELS body's encoding and
 * data: SOLID, else RLE if it is smaller, else RAW. Returns the data's size,
 * or 0 if it would be more than max bytes.
 */
unsigned long cu_encode(const unsigned char *src, long bpr, int w, int h, int bpp,
                        unsigned char *out, unsigned long max, unsigned char *enc);

/* TRUE if n bytes at a and b differ */
int cu_differ(const unsigned char *a, const unsigned char *b, unsigned long n);

#endif
