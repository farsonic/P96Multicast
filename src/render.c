/*
 * P96Multicast - Copper's reference renderer: see render.h.
 *
 * client/p96view.py must draw exactly the same; tests/rendertest.c and
 * tests/test_copper.py check that it does.
 */
#include <string.h>
#include "render.h"
#include "copper.h"

#define ROW(s, x, y) ((s)->mem + (long)(y) * (s)->bpr + (long)(x) * (s)->bpp)

void cu_pen(unsigned long pen, int bpp, unsigned char *px)
{
	int i;

	for (i = bpp - 1; i >= 0; i--) {
		px[i] = (unsigned char)pen;
		pen >>= 8;
	}
}

static void put(unsigned char *p, const unsigned char *px, int bpp)
{
	switch (bpp) {
	case 4:
		p[3] = px[3];
		/* fall through */
	case 3:
		p[2] = px[2];
		/* fall through */
	case 2:
		p[1] = px[1];
		/* fall through */
	default:
		p[0] = px[0];
	}
}

static void flip(unsigned char *p, int bpp)
{
	while (bpp--)
		*p++ ^= 0xff;
}

/* one row of n pixels: one put, then doubling copies */
static void fill_row(unsigned char *p, int n, const unsigned char *px, int bpp)
{
	unsigned long len = (unsigned long)n * bpp, done = bpp, k;

	if (bpp == 1) {
		memset(p, px[0], len);
		return;
	}
	put(p, px, bpp);
	while (done < len) {
		k = done < len - done ? done : len - done;
		memcpy(p + done, p, k);
		done += k;
	}
}

void cu_fill(struct CuSurf *s, int x, int y, int w, int h, const unsigned char *px)
{
	unsigned char *first = ROW(s, x, y), *p = first;
	unsigned long len = (unsigned long)w * s->bpp;

	if (w <= 0 || h <= 0)
		return;
	fill_row(first, w, px, s->bpp);
	while (--h) {
		p += s->bpr;
		memcpy(p, first, len);
	}
}

void cu_invert(struct CuSurf *s, int x, int y, int w, int h)
{
	unsigned char *p = ROW(s, x, y);
	unsigned long len = (unsigned long)w * s->bpp, i;

	if (w <= 0)
		return;
	for (; h > 0; h--, p += s->bpr)
		for (i = 0; i < len; i++)
			p[i] ^= 0xff;
}

void cu_copy(struct CuSurf *s, int sx, int sy, int dx, int dy, int w, int h)
{
	unsigned long len = (unsigned long)w * s->bpp;
	int r;

	if (w <= 0 || h <= 0)
		return;
	if (dy > sy) {
		for (r = h - 1; r >= 0; r--)
			memmove(ROW(s, dx, dy + r), ROW(s, sx, sy + r), len);
	} else {
		for (r = 0; r < h; r++)
			memmove(ROW(s, dx, dy + r), ROW(s, sx, sy + r), len);
	}
}

/* one pixel by its bit, in a draw mode */
static void apply(unsigned char *p, int bit, int mode, const unsigned char *fg,
                  const unsigned char *bg, int bpp)
{
	if (mode & CU_INVERSVID)
		bit = !bit;
	if (mode & CU_COMPLEMENT) {
		if (bit)
			flip(p, bpp);
	} else if (bit) {
		put(p, fg, bpp);
	} else if (mode & CU_JAM2) {
		put(p, bg, bpp);
	}
}

void cu_text(struct CuSurf *s, int x, int y, int w, int h, int mode,
             const unsigned char *bits, int bitx, int stride,
             const unsigned char *fg, const unsigned char *bg)
{
	int bpp = s->bpp, r, i;

	for (r = 0; r < h; r++) {
		unsigned char *p = ROW(s, x, y + r);
		const unsigned char *b = bits + (long)r * stride;

		for (i = 0; i < w; i++, p += bpp) {
			int n = bitx + i;

			apply(p, (b[n >> 3] >> (7 - (n & 7))) & 1, mode, fg, bg, bpp);
		}
	}
}

void cu_pattern(struct CuSurf *s, int x, int y, int w, int h, int mode,
                const unsigned char *rows, int rowmask, int px, int py,
                const unsigned char *fg, const unsigned char *bg)
{
	int bpp = s->bpp, r, i;

	for (r = 0; r < h; r++) {
		unsigned char *p = ROW(s, x, y + r);
		int k = (py + r) & rowmask;
		unsigned int word = (rows[2 * k] << 8) | rows[2 * k + 1];

		for (i = 0; i < w; i++, p += bpp)
			apply(p, (word >> (15 - ((px + i) & 15))) & 1, mode, fg, bg, bpp);
	}
}

static int same(const unsigned char *a, const unsigned char *b, int bpp)
{
	switch (bpp) {
	case 1:
		return a[0] == b[0];
	case 2:
		return a[0] == b[0] && a[1] == b[1];
	case 3:
		return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
	default:
		return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
	}
}

unsigned long cu_encode(const unsigned char *src, long bpr, int w, int h, int bpp,
                        unsigned char *out, unsigned long max, unsigned char *enc)
{
	unsigned long raw = (unsigned long)w * h * bpp, rowlen = (unsigned long)w * bpp, n = 0;
	unsigned long limit = raw < max ? raw : max;
	int r, i, solid = 1;

	/* runs, and whether there is only one colour; give up past the limit */
	for (r = 0; r < h; r++) {
		const unsigned char *p = src + (long)r * bpr;

		for (i = 0; i < w;) {
			const unsigned char *px = p + (long)i * bpp;
			int run = 1;

			while (i + run < w && run < 255 && same(px + (long)run * bpp, px, bpp))
				run++;
			if (solid && !same(px, src, bpp))
				solid = 0;
			if (n + 1 + bpp > limit) {
				n = limit + 1;   /* RLE is no smaller */
				goto not_rle;
			}
			out[n] = (unsigned char)run;
			memcpy(out + n + 1, px, bpp);
			n += 1 + bpp;
			i += run;
		}
	}
	if (solid) {
		memcpy(out, src, bpp);
		*enc = CU_SOLID;
		return bpp;
	}
	if (n < raw) {
		*enc = CU_RLE;
		return n;
	}
not_rle:
	/* a solid surface never gets here: its runs are always smaller */
	if (raw > max)
		return 0;
	for (r = 0; r < h; r++)
		memcpy(out + r * rowlen, src + (long)r * bpr, rowlen);
	*enc = CU_RAW;
	return raw;
}

int cu_differ(const unsigned char *a, const unsigned char *b, unsigned long n)
{
#ifdef __VBCC__
	/* the 68020 and up read longwords at any address */
	const unsigned long *la = (const unsigned long *)a, *lb = (const unsigned long *)b;

	for (; n >= 16; n -= 16, la += 4, lb += 4)
		if (la[0] != lb[0] || la[1] != lb[1] || la[2] != lb[2] || la[3] != lb[3])
			return 1;
	for (; n >= 4; n -= 4)
		if (*la++ != *lb++)
			return 1;
	a = (const unsigned char *)la;
	b = (const unsigned char *)lb;
	while (n--)
		if (*a++ != *b++)
			return 1;
	return 0;
#else
	return memcmp(a, b, n) != 0;
#endif
}
