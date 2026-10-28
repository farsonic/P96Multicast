/*
 * P96Multicast - theme.c: see theme.h. The palette is Easel's dark one;
 * the icons are MulticastPrefs' own.
 */
#include <string.h>
#include <stdio.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <libraries/mui.h>
#include <proto/muimaster.h>
#include "theme.h"

static const ULONG rgb[C_NPENS] = {
	0x262626,   /* BACK */
	0x1E1E1E,   /* PANEL */
	0x323232,   /* FIELD */
	0x4A4A4A,   /* BORDER */
	0x3A3A3A,   /* HOVER */
	0xDCDCDC,   /* TEXT */
	0x9A9A9A,   /* DIM */
	0x5E5E5E,   /* DISABLED */
	0xD2D2D2,   /* ICON */
	0x1473E6,   /* ACCENT */
	0x4AA0F0,   /* ACCENTHOVER */
	0xFFFFFF,   /* ONACCENT */
	0x2D5F9E,   /* MONITOR */
	0x3B78C2,   /* MONITOR2 */
	0xE6E6E6,   /* WB */
	0xEE6A6A, 0x5BC08A, 0xF0A050
};
static LONG pens[C_NPENS];
static struct ColorMap *pencm;
static int users;
static Object *setup_objs[64];
static int nsetup;

/*
 * ObtainBestPen shares a pen that has the colour already, or takes a free
 * one, or settles for the nearest; each goes back with ReleasePen. The
 * colour map wants 32 bits a gun: each 8-bit value repeated four times.
 */
void theme_setup(Object *obj)
{
	int i;

	for (i = 0; i < nsetup; i++)
		if (setup_objs[i] == obj)
			return;
	if (nsetup < 64)
		setup_objs[nsetup++] = obj;
	if (users++)
		return;
	pencm = _screen(obj)->ViewPort.ColorMap;
	for (i = 0; i < C_NPENS; i++) {
		ULONG c = rgb[i];

		pens[i] = ObtainBestPen(pencm, ((c >> 16) & 255) * 0x01010101UL,
		                        ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL,
		                        OBP_Precision, PRECISION_GUI, TAG_DONE);
	}
}

/* MUI may send Cleanup without Setup, or Setup twice: each object counts once */
void theme_cleanup(Object *obj)
{
	int i, k = -1;

	for (i = 0; i < nsetup; i++)
		if (setup_objs[i] == obj)
			k = i;
	if (k < 0)
		return;
	setup_objs[k] = setup_objs[--nsetup];
	if (!users || --users)
		return;
	for (i = 0; i < C_NPENS; i++)
		ReleasePen(pencm, pens[i]);
}

LONG tpen(int which)
{
	return users && which >= 0 && which < C_NPENS ? pens[which] : 1;
}

static const char *spec(ULONG c)
{
	static char buf[2][40];
	static int n;
	char *s = buf[n ^= 1];

	sprintf(s, "2:%08lx,%08lx,%08lx", ((c >> 16) & 255) * 0x01010101UL,
	        ((c >> 8) & 255) * 0x01010101UL, (c & 255) * 0x01010101UL);
	return s;
}

const char *theme_back(void)
{
	return spec(rgb[C_BACK]);
}

const char *theme_panel_back(void)
{
	return spec(rgb[C_PANEL]);
}

LONG theme_textlen(struct RastPort *rp, const char *s, BOOL bold)
{
	LONG w;

	SetSoftStyle(rp, bold ? FSF_BOLD : 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
	w = TextLength(rp, (STRPTR)s, strlen(s));
	SetSoftStyle(rp, 0, FSF_BOLD);
	return w;
}

void theme_text(struct RastPort *rp, LONG x, LONG y, const char *s, LONG pen, BOOL bold)
{
	SetSoftStyle(rp, bold ? FSF_BOLD : 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
	SetAPen(rp, pen);
	SetDrMd(rp, JAM1);
	Move(rp, x, y + rp->TxBaseline);
	Text(rp, (STRPTR)s, strlen(s));
	SetSoftStyle(rp, 0, FSF_BOLD);
}

void theme_rect(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
	if (x1 < x0 || y1 < y0)
		return;
	SetAPen(rp, pen);
	Move(rp, x0, y0);
	Draw(rp, x1, y0);
	Draw(rp, x1, y1);
	Draw(rp, x0, y1);
	Draw(rp, x0, y0);
}

void theme_fill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG pen)
{
	if (x1 < x0 || y1 < y0)
		return;
	SetAPen(rp, pen);
	RectFill(rp, x0, y0, x1, y1);
}

void theme_round(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1, LONG r, LONG pen)
{
	LONG i;

	if (x1 - x0 < 2 * r || y1 - y0 < 2 * r)
		r = 0;
	SetAPen(rp, pen);
	for (i = 0; i < r; i++) {
		/* row i of a corner: how far the quarter circle of radius r reaches in */
		LONG dy = r - i, k = 0;

		while ((k + 1) * (k + 1) <= r * r - dy * dy)
			k++;
		RectFill(rp, x0 + r - k, y0 + i, x1 - r + k, y0 + i);
		RectFill(rp, x0 + r - k, y1 - i, x1 - r + k, y1 - i);
	}
	if (y1 - r >= y0 + r)
		RectFill(rp, x0, y0 + r, x1, y1 - r);
}

/*
 * Icons on a 16 x 16 grid scaled to the size asked for. Commands: cX colour
 * (i icon, a accent, w the Workbench white, g green, r red, d dim),
 * L x0 y0 x1 y1 line, R x0 y0 x1 y1 outline, F x0 y0 x1 y1 filled,
 * O cx cy r circle, E cx cy r filled circle, P n x0 y0 ... polyline.
 */
static const char *const icons[IC_N] = {
	"",
	/* DESKTOP: two monitors side by side */
	"ci R 0 3 7 10 R 8 3 15 10 ca F 1 4 6 9 F 9 4 14 9 ci L 3 12 4 12 L 11 12 12 12 L 2 13 5 13 L 10 13 13 13",
	/* NETWORK: a sender and three viewers */
	"ci R 6 1 9 4 L 7 4 7 8 L 2 8 13 8 L 2 8 2 11 L 7 8 7 11 L 13 8 13 11 ca F 1 11 3 14 F 6 11 8 14 F 12 11 14 14",
	/* DISPLAY: a screen with resolutions */
	"ci R 1 2 14 12 L 6 14 9 14 L 7 12 7 14 ca R 3 4 12 10 R 5 6 10 8",
	/* WRAP: a card wrapped */
	"ci R 3 4 12 11 L 5 13 5 11 L 7 13 7 11 L 9 13 9 11 ca R 1 2 14 13 L 1 2 4 2",
	/* STATUS: a pulse */
	"ci R 1 2 14 13 ca P 6 2 8 5 8 6 4 8 11 10 7 13 7",
	/* SAVE: a disk */
	"ci R 2 2 13 13 F 5 2 10 5 R 4 9 11 13",
	/* USE: a tick */
	"ci P 3 3 8 6 11 13 4",
	/* INSTALL: an arrow into a tray */
	"ci L 8 1 8 9 P 3 5 6 8 9 11 6 P 4 2 10 2 13 13 13 13 10",
	/* OPEN: a monitor with a plus */
	"ci R 1 2 14 11 L 6 13 9 13 ca L 8 4 8 9 L 5 6 11 6",
	/* CLOSE: a monitor with a cross */
	"ci R 1 2 14 11 L 6 13 9 13 cr L 5 4 10 9 L 10 4 5 9",
	/* ADD */
	"ci L 8 3 8 13 L 3 8 13 8",
	/* REMOVE */
	"ci L 3 8 13 8",
	/* LEFT: a chevron */
	"ci P 3 10 3 6 8 10 13",
	/* RIGHT */
	"ci P 3 6 3 10 8 6 13",
	/* REFRESH: a circular arrow */
	"ci O 8 8 5 cd F 10 2 15 6 ci P 3 10 3 13 4 13 7",
};

static LONG num(const char **p)
{
	LONG v = 0;
	int neg = 0;

	while (**p == ' ')
		(*p)++;
	if (**p == '-') {
		neg = 1;
		(*p)++;
	}
	while (**p >= '0' && **p <= '9')
		v = v * 10 + *(*p)++ - '0';
	return neg ? -v : v;
}

/* pen >= 0: every part in it; else each in its own colour */
void theme_icon(struct RastPort *rp, int icon, LONG x, LONG y, LONG size, LONG pen)
{
	const char *p;
	LONG a, b, e, f;

#define SC(v) ((v) * size / 16)
	if (icon <= IC_NONE || icon >= IC_N)
		return;
	SetDrMd(rp, JAM1);
	SetAPen(rp, pen >= 0 ? pen : tpen(C_ICON));
	for (p = icons[icon]; *p;) {
		char c = *p++;

		switch (c) {
		case 'c': {
			char k = *p ? *p++ : 'i';
			int t = k == 'a' ? C_ACCENT : k == 'w' ? C_WB : k == 'g' ? C_GREEN :
			        k == 'r' ? C_RED : k == 'd' ? C_DIM : C_ICON;

			SetAPen(rp, pen >= 0 ? pen : tpen(t));
			break;
		}
		case 'L':
			a = num(&p); b = num(&p); e = num(&p); f = num(&p);
			Move(rp, x + SC(a), y + SC(b));
			Draw(rp, x + SC(e), y + SC(f));
			break;
		case 'R':
			a = num(&p); b = num(&p); e = num(&p); f = num(&p);
			Move(rp, x + SC(a), y + SC(b));
			Draw(rp, x + SC(e), y + SC(b));
			Draw(rp, x + SC(e), y + SC(f));
			Draw(rp, x + SC(a), y + SC(f));
			Draw(rp, x + SC(a), y + SC(b));
			break;
		case 'F':
			a = num(&p); b = num(&p); e = num(&p); f = num(&p);
			RectFill(rp, x + SC(a), y + SC(b), x + SC(e), y + SC(f));
			break;
		case 'O':
			a = num(&p); b = num(&p); e = num(&p);
			DrawEllipse(rp, x + SC(a), y + SC(b), SC(e) > 0 ? SC(e) : 1, SC(e) > 0 ? SC(e) : 1);
			break;
		case 'E': {
			LONG r, dy;

			a = num(&p); b = num(&p); e = num(&p);
			r = SC(e) > 0 ? SC(e) : 1;
			for (dy = -r; dy <= r; dy++) {
				LONG dx = 0;

				while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r)
					dx++;
				RectFill(rp, x + SC(a) - dx, y + SC(b) + dy, x + SC(a) + dx, y + SC(b) + dy);
			}
			break;
		}
		case 'P': {
			LONG n = num(&p), i;

			for (i = 0; i < n; i++) {
				a = num(&p);
				b = num(&p);
				if (i)
					Draw(rp, x + SC(a), y + SC(b));
				else
					Move(rp, x + SC(a), y + SC(b));
			}
			break;
		}
		default:
			break;
		}
	}
#undef SC
}

/* the name in bold (MUI's ESC b), then the text wrapped at about 46 characters */
const char *theme_bubble(const char *name, const char *body)
{
	static char out[500];
	char *o;
	int col = 0;
	size_t room;

	snprintf(out, sizeof(out), "\33b%s\33n", name);
	if (!body || !*body)
		return out;
	o = out + strlen(out);
	room = sizeof(out) - (o - out) - 2;
	*o++ = '\n';
	room--;
	while (*body && room > 1) {
		const char *sp = body;
		int wl;

		while (*sp && *sp != ' ' && *sp != '\n')
			sp++;
		wl = sp - body;
		if (col && col + 1 + wl > 46) {
			*o++ = '\n';
			room--;
			col = 0;
		} else if (col) {
			*o++ = ' ';
			room--;
			col++;
		}
		while (body < sp && room > 1) {
			*o++ = *body++;
			room--;
			col++;
		}
		if (*body == '\n') {
			*o++ = '\n';
			room--;
			col = 0;
			body++;
		}
		while (*body == ' ')
			body++;
	}
	*o = 0;
	return out;
}
