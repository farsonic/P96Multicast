/*
 * Panorama - Copper's reference renderer against the viewer's.
 *
 *   cc -Iinclude -Isrc -o build/rendertest tests/rendertest.c src/render.c
 *   build/rendertest BPP SEED OUT
 *
 * Draws random orders and pixels with src/render.c, built natively, and
 * writes them as Copper records to OUT, followed by the surface it ended
 * with. tests/test_copper.py has client/panoview.py draw the same records
 * and compares.
 *
 * OUT: u16 w, u16 h, u8 bpp, then records (u8 type, u16 length, body), a
 * record of type 0 and length 0, then the surface, w * h * bpp bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "copper.h"
#include "render.h"

#define W 97
#define H 61

static unsigned char body[4096];
static FILE *out;
static unsigned long rng;

static unsigned rnd(unsigned n)
{
	rng = rng * 1103515245UL + 12345UL;
	return (unsigned)((rng >> 16) & 0x7fff) % n;
}

static void put16(unsigned char *p, unsigned v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static void record(int type, unsigned len)
{
	unsigned char h[3];

	h[0] = type;
	put16(h + 1, len);
	fwrite(h, 1, 3, out);
	fwrite(body, 1, len, out);
}

static void rect(int *x, int *y, int *w, int *h)
{
	*w = 1 + rnd(W);
	*h = 1 + rnd(H);
	*x = rnd(W - *w + 1);
	*y = rnd(H - *h + 1);
}

static void pixel(unsigned char *px, int bpp)
{
	int i;

	for (i = 0; i < bpp; i++)
		px[i] = rnd(4) ? rnd(256) : 0xff;
}

int main(int argc, char **argv)
{
	struct CuSurf s;
	unsigned char hdr[5], *src;
	int bpp, op, i, n;

	if (argc != 4)
		return 2;
	bpp = atoi(argv[1]);
	rng = strtoul(argv[2], NULL, 0);
	out = fopen(argv[3], "wb");
	if (!out || bpp < 1 || bpp > 4)
		return 2;
	s.w = W;
	s.h = H;
	s.bpp = bpp;
	s.bpr = (long)W * bpp;
	s.mem = calloc(W * H, bpp);
	src = malloc(W * H * bpp);
	put16(hdr, W);
	put16(hdr + 2, H);
	hdr[4] = bpp;
	fwrite(hdr, 1, 5, out);

	for (op = 0; op < 400; op++) {
		int x, y, w, h, kind = rnd(6);
		unsigned char fg[4], bg[4];

		rect(&x, &y, &w, &h);
		put16(body, x);
		put16(body + 2, y);
		put16(body + 4, w);
		put16(body + 6, h);
		switch (kind) {
		case 0:
			pixel(fg, bpp);
			cu_fill(&s, x, y, w, h, fg);
			memcpy(body + 8, fg, bpp);
			record(CU_FILL, 8 + bpp);
			break;
		case 1:
			if (rnd(3)) {
				cu_invert(&s, x, y, w, h);
				record(CU_INVERT, 8);
				break;
			}
			/* fall through: a copy instead */
		case 2: {
			int sx = rnd(W - w + 1), sy = rnd(H - h + 1);

			cu_copy(&s, sx, sy, x, y, w, h);
			put16(body, sx);
			put16(body + 2, sy);
			put16(body + 4, x);
			put16(body + 6, y);
			put16(body + 8, w);
			put16(body + 10, h);
			record(CU_COPY, 12);
			break;
		}
		case 3: {
			int mode = rnd(8), bitx = rnd(16), stride = (bitx + w + 7) / 8;
			unsigned char *bits = body + 14 + 2 * bpp;

			if (h * stride + 14 + 2 * bpp > (int)sizeof(body))
				h = (sizeof(body) - 14 - 2 * bpp) / stride;
			put16(body + 6, h);
			pixel(fg, bpp);
			pixel(bg, bpp);
			for (i = 0; i < h * stride; i++)
				bits[i] = rnd(256);
			cu_text(&s, x, y, w, h, mode, bits, bitx, stride, fg, bg);
			body[8] = mode;
			body[9] = 0;
			put16(body + 10, bitx);
			put16(body + 12, stride);
			memcpy(body + 14, fg, bpp);
			memcpy(body + 14 + bpp, bg, bpp);
			record(CU_TEXT, 14 + 2 * bpp + h * stride);
			break;
		}
		case 4: {
			int mode = rnd(8), rowmask = (1 << rnd(5)) - 1, px = rnd(16), py = rnd(rowmask + 1);
			unsigned char *rows = body + 12 + 2 * bpp;

			pixel(fg, bpp);
			pixel(bg, bpp);
			for (i = 0; i < 2 * (rowmask + 1); i++)
				rows[i] = rnd(256);
			cu_pattern(&s, x, y, w, h, mode, rows, rowmask, px, py, fg, bg);
			body[8] = mode;
			body[9] = rowmask;
			body[10] = px;
			body[11] = py;
			memcpy(body + 12, fg, bpp);
			memcpy(body + 12 + bpp, bg, bpp);
			record(CU_PATTERN, 12 + 2 * bpp + 2 * (rowmask + 1));
			break;
		}
		default: {
			/* pixels: noise, runs or one colour, from a source picture */
			int style = rnd(3), r;
			unsigned char enc;
			unsigned long len;

			if (w * h * bpp > 2048)
				h = 2048 / (w * bpp);
			put16(body + 4, w);
			put16(body + 6, h);
			pixel(fg, bpp);
			for (i = 0; i < W * H; i++) {
				if (style == 0 || (style == 1 && rnd(5) == 0))
					pixel(fg, bpp);
				memcpy(src + i * bpp, fg, bpp);
			}
			len = cu_encode(src, (long)W * bpp, w, h, bpp, body + 10, sizeof(body) - 10, &enc);
			if (!len)
				break;
			body[8] = enc;
			body[9] = 0;
			for (r = 0; r < h; r++)
				memcpy(s.mem + (y + r) * s.bpr + x * bpp, src + (long)r * W * bpp, w * bpp);
			record(CU_PIXELS, 10 + len);
			break;
		}
		}
	}
	body[0] = 0;
	record(0, 0);
	n = fwrite(s.mem, bpp, W * H, out);
	fclose(out);
	return n == W * H ? 0 : 1;
}
