/*
 * Panorama - PanoramaPrefs: the window.
 *
 * GadTools, so it runs on AmigaOS 3.x and AROS alike. Five pages, chosen
 * with the cycle gadget at the top, each its own gadget list swapped in
 * and out: Desktop (the shared screen over several Panorama windows, with
 * a picture of the monitors), Network, Display (with the screen modes),
 * Wrap.card and Status.
 * Save writes ENVARC: and ENV:, Use only ENV:, Install also copies the
 * cards and makes the monitor (install.c). Values are read from the
 * gadgets when they are used, so a field needs no Return.
 */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <dos/dostags.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

#define NPAGES 5
#define PAGE_DESKTOP 0
#define PAGE_STATUS  4
static const char *page_names[] = { "Desktop", "Network", "Display", "Wrap.card", "Status", NULL };

/* the fields: a gadget for a setting, or for something on the page itself */
#define F_SETTING 0     /* key: a setting */
#define F_MODESRC 1     /* where the modes come from */
#define F_MODES   2     /* the mode list */
#define F_MODEADD 3     /* a size to add */
#define F_ADD     4
#define F_REMOVE  5
#define F_WRAP    6     /* how Wrap.card is used */
#define F_STATUS  7     /* the status lines */
#define F_MAP     8     /* the picture of the monitors (drawn, no gadget) */
#define F_LAYOUT  9     /* how many monitors, how arranged: SPLIT */
#define F_MONSIZE 10    /* each one's size: MONITOR */
#define F_COLOURS 11    /* COLOURS */
#define F_OPEN    12    /* open the desktop now */
#define F_CLOSE   13
#define F_WBPOS   14    /* where the Workbench is: WORKBENCH */

struct Field {
	UBYTE page;
	UBYTE what;         /* F_* */
	UWORD kind;         /* GadTools kind */
	const char *label;
	const char *key;
	UBYTE rows;         /* height in rows */
	UBYTE half;         /* 1: left half of a row, 2: right half */
};

static const struct Field fields[] = {
	{ 0, F_MAP,     0,             "",                 NULL, 12 },
	{ 0, F_LAYOUT,  CYCLE_KIND,    "Monitors",         NULL },
	{ 0, F_WBPOS,   CYCLE_KIND,    "Workbench is",     NULL },
	{ 0, F_MONSIZE, CYCLE_KIND,    "Each monitor",     NULL },
	{ 0, F_COLOURS, CYCLE_KIND,    "Colours",          NULL },
	{ 0, F_SETTING, CHECKBOX_KIND, "Programs open on it", "DEFAULTSCREEN" },
	{ 0, F_SETTING, CHECKBOX_KIND, "Open at boot",     "ATBOOT" },
	{ 0, F_SETTING, CHECKBOX_KIND, "Behind Workbench", "BEHIND" },
	{ 0, F_SETTING, CHECKBOX_KIND, "Viewers control it", "INPUT" },
	{ 0, F_SETTING, CHECKBOX_KIND, "Workbench own monitor", "WBMONITOR" },
	{ 0, F_OPEN,    BUTTON_KIND,   "Open now",         NULL, 1, 1 },
	{ 0, F_CLOSE,   BUTTON_KIND,   "Close",            NULL, 1, 2 },

	{ 1, F_SETTING, STRING_KIND,   "Send to",          "DEST" },
	{ 1, F_SETTING, INTEGER_KIND,  "UDP port",         "PORT" },
	{ 1, F_SETTING, INTEGER_KIND,  "Multicast TTL",    "TTL" },
	{ 1, F_SETTING, INTEGER_KIND,  "Updates/second",   "FPS" },
	{ 1, F_SETTING, INTEGER_KIND,  "Most KB/second",   "RATE" },
	{ 1, F_SETTING, INTEGER_KIND,  "Check all (s)",    "SCAN" },
	{ 1, F_SETTING, STRING_KIND,   "Log file",         "LOG" },
	{ 1, F_SETTING, CHECKBOX_KIND, "Send drawing",     "BLITTER" },
	{ 1, F_SETTING, CHECKBOX_KIND, "Viewer pointer",   "POINTER" },

	{ 2, F_SETTING, INTEGER_KIND,  "Boards",           "BOARDS" },
	{ 2, F_SETTING, INTEGER_KIND,  "Video MB/board",   "VRAM" },
	{ 2, F_SETTING, INTEGER_KIND,  "Largest width",    "MAXWIDTH" },
	{ 2, F_SETTING, INTEGER_KIND,  "Largest height",   "MAXHEIGHT" },
	{ 2, F_MODESRC, CYCLE_KIND,    "Screen modes",     NULL },
	{ 2, F_MODES,   LISTVIEW_KIND, "",                 NULL, 5 },
	{ 2, F_MODEADD, STRING_KIND,   "Size (WxH)",       NULL },
	{ 2, F_ADD,     BUTTON_KIND,   "Add",              NULL, 1, 1 },
	{ 2, F_REMOVE,  BUTTON_KIND,   "Remove",           NULL, 1, 2 },

	{ 3, F_WRAP,    CYCLE_KIND,    "Use Wrap.card",    NULL },
	{ 3, F_SETTING, STRING_KIND,   "Wrap card",        "CARD" },
	{ 3, F_SETTING, STRING_KIND,   "Mirror on",        "MIRROR" },
	{ 3, F_SETTING, STRING_KIND,   "Span (CxR)",       "SPAN" },
	{ 3, F_SETTING, STRING_KIND,   "Every head",       "HEAD" },
	{ 3, F_SETTING, STRING_KIND,   "Head 1",           "HEAD1" },
	{ 3, F_SETTING, STRING_KIND,   "Head 2",           "HEAD2" },
	{ 3, F_SETTING, STRING_KIND,   "Head 3",           "HEAD3" },
	{ 3, F_SETTING, STRING_KIND,   "Head 4",           "HEAD4" },
	{ 3, F_SETTING, CHECKBOX_KIND, "Share memory",     "HEADSHARE" },
	{ 3, F_SETTING, INTEGER_KIND,  "Head updates/s",   "MIRRORFPS" },

	{ 4, F_STATUS,  LISTVIEW_KIND, "",                 NULL, 14 },
};
#define NFIELDS (sizeof(fields) / sizeof(fields[0]))

static const char *modesrc_names[] = { "P96's settings", "This list", NULL };

/* "This list" with nothing in it starts with these */
static const char *standard_modes = "640x480,800x600,1024x768,1280x720,1280x960,1280x1024,1920x1080";
static const char *wrap_names[] = { "No", "Wrap only", "Mirror", "Span", NULL };

/* the Desktop page's choices, and the settings they stand for */
static const char *layout_names[] = { "One", "Two, side by side", "Two, one above the other",
                                      "Three, side by side", "Four, side by side",
                                      "Four, two by two", NULL };
static const char *layout_split[] = { "1x1", "2x1", "1x2", "3x1", "4x1", "2x2" };
static const char *size_names[] = { "640 x 480", "800 x 600", "1024 x 768", "1280 x 720",
                                    "1280 x 1024", "1920 x 1080", NULL };
static const char *size_value[] = { "640x480", "800x600", "1024x768", "1280x720",
                                    "1280x1024", "1920x1080" };
static const char *colour_names[] = { "256 (8 bit)", "65536 (16 bit)", "16 million (32 bit)",
                                      NULL };
static const char *colour_value[] = { "8", "16", "32" };
static int layout_sel, size_sel, colour_sel;

/* where the Workbench can be: next to an outer edge of a monitor, or nowhere */
#define MAXWBPOS 17
static int wb_npos, wb_sel, wb_side[MAXWBPOS], wb_mon[MAXWBPOS];
static char wb_text[MAXWBPOS][24];
static const char *wb_labels[MAXWBPOS + 1];
static WORD map_x, map_y, map_w, map_h;
static struct DrawInfo *dri;

#define ID_PAGE    1
#define ID_SAVE    2
#define ID_USE     3
#define ID_INSTALL 4
#define ID_CANCEL  5
#define ID_FIELD   100      /* + field index */

static struct Window *win;
static struct Gadget *glist[NPAGES], *base;
static struct Gadget *gad[NFIELDS];
static APTR vi;
static int page;
static WORD page_top, area_bottom, page_bottom;

/* the mode list, for the listview */
#define MAXLISTED 24
static struct List modes;
static struct Node mode_nodes[MAXLISTED];
static char mode_text[MAXLISTED][12];
static int nmodes, mode_sel = -1;

/* the status lines */
#define NSTATUS 24
static struct List stat;
static struct Node stat_nodes[NSTATUS];
static char stat_text[NSTATUS][80];

static char report[2048];

static void note(const char *s)
{
	int n = strlen(report);

	snprintf(report + n, sizeof(report) - n, "%s%s", n ? "\n" : "", s);
}

static void tell(const char *title, const char *text)
{
	struct EasyStruct es = { sizeof(struct EasyStruct), 0, NULL, "%s", "OK" };

	mp_trace("requester: %s", title);
	es.es_Title = (UBYTE *)title;
	EasyRequest(win, &es, NULL, (ULONG)text);
}

static BOOL ask(const char *title, const char *text, const char *buttons)
{
	struct EasyStruct es = { sizeof(struct EasyStruct), 0, NULL, "%s", NULL };

	mp_trace("asking: %s", title);
	es.es_Title = (UBYTE *)title;
	es.es_GadgetFormat = (UBYTE *)buttons;
	return EasyRequest(win, &es, NULL, (ULONG)text) == 1;
}

/* ---------------------------------------------------------------- the mode list */

static void modes_from(const char *v)
{
	nmodes = 0;
	while (*v && nmodes < MAXLISTED) {
		int i = 0;

		while (*v == ',' || *v == ' ')
			v++;
		while (*v && *v != ',' && *v != ' ' && i < 11)
			mode_text[nmodes][i++] = *v++;
		mode_text[nmodes][i] = 0;
		if (i)
			nmodes++;
	}
}

static void modes_to(char *out, int len)
{
	int i, n = 0;

	out[0] = 0;
	for (i = 0; i < nmodes; i++)
		n += snprintf(out + n, len - n, "%s%s", i ? "," : "", mode_text[i]);
}

static void modes_list(void)
{
	int i;

	NewList(&modes);
	for (i = 0; i < nmodes; i++) {
		mode_nodes[i].ln_Name = mode_text[i];
		AddTail(&modes, &mode_nodes[i]);
	}
}

static int field_index(UBYTE what)
{
	int i;

	for (i = 0; i < NFIELDS; i++)
		if (fields[i].what == what)
			return i;
	return -1;
}

static void modes_show(void)
{
	struct Gadget *g = gad[field_index(F_MODES)];

	GT_SetGadgetAttrs(g, win, NULL, GTLV_Labels, ~0, TAG_END);
	modes_list();
	GT_SetGadgetAttrs(g, win, NULL, GTLV_Labels, (ULONG)&modes, GTLV_Selected, ~0, TAG_END);
	mode_sel = -1;
}

/* ---------------------------------------------------------------- status */

static void status_show(BOOL attach)
{
	struct Gadget *g = gad[field_index(F_STATUS)];
	int n, i;

	if (attach)
		GT_SetGadgetAttrs(g, win, NULL, GTLV_Labels, ~0, TAG_END);
	n = status_lines(stat_text, NSTATUS);
	NewList(&stat);
	for (i = 0; i < n; i++) {
		stat_nodes[i].ln_Name = stat_text[i];
		AddTail(&stat, &stat_nodes[i]);
	}
	if (attach)
		GT_SetGadgetAttrs(g, win, NULL, GTLV_Labels, (ULONG)&stat, TAG_END);
}

/* ---------------------------------------------------------------- the gadgets */

static int wrap_mode(void)
{
	const char *card = prefs_value(find_setting("CARD"));
	const char *span = prefs_value(find_setting("SPAN"));
	const char *mirror = prefs_value(find_setting("MIRROR"));

	if (!*card)
		return 0;
	if (*span)
		return 3;
	if (*mirror)
		return 2;
	return 1;
}

static int pick(const char **values, int n, const char *v)
{
	int i;

	for (i = 0; i < n; i++)
		if (!mp_stricmp(values[i], v))
			return i;
	return -1;
}

static int layout_cols(void)
{
	return layout_split[layout_sel][0] - '0';
}

static int layout_rows(void)
{
	return layout_split[layout_sel][2] - '0';
}

/* can the Workbench be at place n with this layout: an outer edge of a monitor it has? */
static BOOL wb_fits(int n)
{
	int c = layout_cols(), r = layout_rows(), m = wb_mon[n];

	if (wb_side[n] == WB_NONE)
		return TRUE;
	if (m >= c * r)
		return FALSE;
	switch (wb_side[n]) {
	case WB_LEFT:
		return m % c == 0;
	case WB_RIGHT:
		return m % c == c - 1;
	case WB_ABOVE:
		return m / c == 0;
	default:
		return m / c == r - 1;
	}
}

/*
 * Every place the Workbench can be, for any layout: the cycle's labels
 * never change (GadTools before V42 can't change them); places a layout
 * hasn't are skipped. Then the one nearest side, mon.
 */
static void wb_positions(int side, int mon)
{
	static const char *names[] = { "", "Left of", "Right of", "Above", "Below" };
	int i, n = 0, sd;

	if (!wb_npos) {
		for (sd = WB_LEFT; sd <= WB_BELOW; sd++)
			for (i = 0; i < 4; i++, n++) {
				wb_side[n] = sd;
				wb_mon[n] = i;
				snprintf(wb_text[n], sizeof(wb_text[n]), "%s monitor %d", names[sd], i + 1);
			}
		wb_side[n] = WB_NONE;
		wb_mon[n] = 0;
		snprintf(wb_text[n], sizeof(wb_text[n]), "Nowhere (hotkey only)");
		n++;
		wb_npos = n;
		for (i = 0; i < n; i++)
			wb_labels[i] = wb_text[i];
		wb_labels[n] = NULL;
	}
	if (side != WB_NONE)
		wb_fit(&side, &mon, layout_cols(), layout_rows());
	wb_sel = wb_npos - 1;
	for (i = 0; i < wb_npos; i++)
		if (wb_side[i] == side && (side == WB_NONE || wb_mon[i] == mon))
			wb_sel = i;
}

/* the cycle went to n, from wb_sel: on to the next place this layout has */
static int wb_step(int n)
{
	int back = n == (wb_sel + wb_npos - 1) % wb_npos && n != (wb_sel + 1) % wb_npos;
	int k;

	for (k = 0; k < wb_npos && !wb_fits(n); k++)
		n = (n + (back ? wb_npos - 1 : 1)) % wb_npos;
	return n;
}

static void desktop_from_settings(void)
{
	int side, mon;

	layout_sel = pick(layout_split, 6, prefs_value(find_setting("SPLIT")));
	size_sel = pick(size_value, 6, prefs_value(find_setting("MONITOR")));
	colour_sel = pick(colour_value, 3, prefs_value(find_setting("COLOURS")));
	if (layout_sel < 0)
		layout_sel = 0;
	if (size_sel < 0)
		size_sel = 1;
	if (colour_sel < 0)
		colour_sel = 1;
	if (!wb_place(prefs_value(find_setting("WORKBENCH")), &side, &mon)) {
		side = WB_LEFT;
		mon = 0;
	}
	wb_positions(side, mon);
}

/*
 * RectFill and a bevel box, only inside the map and never upside down: a
 * fill with its corners the wrong way round can hang a graphics board.
 */
static void map_fill(struct RastPort *rp, WORD x1, WORD y1, WORD x2, WORD y2)
{
	if (x1 < map_x)
		x1 = map_x;
	if (y1 < map_y)
		y1 = map_y;
	if (x2 > map_x + map_w - 1)
		x2 = map_x + map_w - 1;
	if (y2 > map_y + map_h - 1)
		y2 = map_y + map_h - 1;
	if (x1 <= x2 && y1 <= y2)
		RectFill(rp, x1, y1, x2, y2);
}

static void map_bevel(struct RastPort *rp, WORD x, WORD y, WORD w, WORD h, BOOL recessed)
{
	if (w < 4 || h < 4 || x < map_x || y < map_y || x + w > map_x + map_w ||
	    y + h > map_y + map_h)
		return;
	DrawBevelBox(rp, x, y, w, h, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, recessed,
	             TAG_END);
}

/* the monitor size MONITOR= stands for */
static void monitor_size(UWORD *mw, UWORD *mh)
{
	const char *sz = size_value[size_sel];

	*mw = 0;
	while (*sz >= '0' && *sz <= '9')
		*mw = *mw * 10 + *sz++ - '0';
	sz++;
	*mh = 0;
	while (*sz >= '0' && *sz <= '9')
		*mh = *mh * 10 + *sz++ - '0';
}

/*
 * Where the map puts the monitors: top left, and one monitor's size; room
 * is left around them for the Workbench.
 */
static void map_geometry(WORD *ox, WORD *oy, WORD *cw, WORD *ch)
{
	UWORD c = layout_cols(), r = layout_rows(), mw, mh;
	WORD avail_h = map_h - win->RPort->TxHeight - 6;

	monitor_size(&mw, &mh);
	*cw = (LONG)(map_w - 8) * 10 / (c * 10 + 13);
	*ch = (LONG)*cw * mh / mw;
	if ((LONG)*ch * (r * 10 + 13) > avail_h * 10L) {
		*ch = (LONG)avail_h * 10 / (r * 10 + 13);
		*cw = (LONG)*ch * mw / mh;
	}
	*ox = map_x + (map_w - *cw * c) / 2;
	*oy = map_y + 2 + (avail_h - *ch * r) / 2;
}

/* the Workbench's box on the map, at place n */
static void wb_box(int n, WORD ox, WORD oy, WORD cw, WORD ch, WORD *bx, WORD *by, WORD *bw,
                   WORD *bh)
{
	int c = layout_cols(), m = wb_mon[n];
	WORD mx = ox + (m % c) * cw, my = oy + (m / c) * ch;

	*bw = cw * 6 / 10;
	*bh = ch * 6 / 10;
	switch (wb_side[n]) {
	case WB_LEFT:
		*bx = ox - 3 - *bw;
		*by = my + (ch - *bh) / 2;
		break;
	case WB_RIGHT:
		*bx = ox + c * cw + 3;
		*by = my + (ch - *bh) / 2;
		break;
	case WB_ABOVE:
		*bx = mx + (cw - *bw) / 2;
		*by = oy - 3 - *bh;
		break;
	default:
		*bx = mx + (cw - *bw) / 2;
		*by = oy + layout_rows() * ch + 3;
		break;
	}
}

/* a click on the map: the Workbench to the place nearest it */
static BOOL wb_click(WORD x, WORD y)
{
	WORD ox, oy, cw, ch, bx, by, bw, bh;
	LONG best = -1, d;
	int i, pick = -1;

	if (page != PAGE_DESKTOP || x < map_x || y < map_y || x >= map_x + map_w ||
	    y >= map_y + map_h)
		return FALSE;
	map_geometry(&ox, &oy, &cw, &ch);
	if (x >= ox && x < ox + layout_cols() * cw && y >= oy && y < oy + layout_rows() * ch)
		return FALSE;   /* on the monitors */
	for (i = 0; i < wb_npos; i++) {
		if (wb_side[i] == WB_NONE || !wb_fits(i))
			continue;
		wb_box(i, ox, oy, cw, ch, &bx, &by, &bw, &bh);
		d = (LONG)(x - bx - bw / 2) * (x - bx - bw / 2) + (LONG)(y - by - bh / 2) * (y - by - bh / 2);
		if (best < 0 || d < best) {
			best = d;
			pick = i;
		}
	}
	if (pick < 0 || pick == wb_sel)
		return FALSE;
	wb_sel = pick;
	return TRUE;
}

/*
 * The Desktop page's picture: the monitors as the layout puts them, each
 * with its number, size and port, and the whole desktop's size under them.
 */
static void draw_map(void)
{
	struct RastPort *rp = win->RPort;
	UWORD c = layout_split[layout_sel][0] - '0', r = layout_split[layout_sel][2] - '0';
	UWORD mw = 640, mh = 480, i;
	WORD fh = rp->TxHeight, cw, ch, ox, oy;
	UWORD fill = dri ? dri->dri_Pens[FILLPEN] : 3, text = dri ? dri->dri_Pens[FILLTEXTPEN] : 2;
	UWORD back = dri ? dri->dri_Pens[BACKGROUNDPEN] : 0, plain = dri ? dri->dri_Pens[TEXTPEN] : 1;
	const char *pv = prefs_value(find_setting("PORT"));
	int port = 0;
	char buf[80];

	while (*pv >= '0' && *pv <= '9')
		port = port * 10 + *pv++ - '0';

	if (page != PAGE_DESKTOP)
		return;
	monitor_size(&mw, &mh);

	SetAPen(rp, back);
	mp_trace("map: background %d,%d %dx%d", map_x, map_y, map_w, map_h);
	map_fill(rp, map_x, map_y, map_x + map_w - 1, map_y + map_h - 1);
	map_geometry(&ox, &oy, &cw, &ch);
	mp_trace("map: monitors at %d,%d, each %dx%d", ox, oy, cw, ch);
	SetDrMd(rp, JAM1);

	/* the Workbench, where the mouse crosses over to it */
	if (wb_side[wb_sel] != WB_NONE) {
		WORD bx, by, bw, bh, l;
		UWORD shine = dri ? dri->dri_Pens[SHINEPEN] : 2;

		wb_box(wb_sel, ox, oy, cw, ch, &bx, &by, &bw, &bh);
		mp_trace("map: Workbench box %d,%d %dx%d", bx, by, bw, bh);
		SetAPen(rp, shine);
		map_fill(rp, bx + 2, by + 2, bx + bw - 3, by + bh - 3);
		map_bevel(rp, bx, by, bw, bh, TRUE);
		SetAPen(rp, plain);
		strcpy(buf, "Workbench");
		l = TextLength(rp, (STRPTR)buf, strlen(buf));
		if (l >= bw - 4) {
			strcpy(buf, "WB");
			l = TextLength(rp, (STRPTR)buf, strlen(buf));
		}
		Move(rp, bx + (bw - l) / 2, by + (bh - fh) / 2 + rp->TxBaseline);
		Text(rp, (STRPTR)buf, strlen(buf));
	}
	for (i = 0; i < c * r; i++) {
		WORD x = ox + (i % c) * cw, y = oy + (i / c) * ch;
		WORD l;

		SetAPen(rp, fill);
		mp_trace("map: monitor %d", (int)i + 1);
		map_fill(rp, x + 3, y + 3, x + cw - 4, y + ch - 4);
		map_bevel(rp, x + 1, y + 1, cw - 2, ch - 2, FALSE);
		SetAPen(rp, text);
		snprintf(buf, sizeof(buf), "%d", (int)i + 1);
		l = TextLength(rp, (STRPTR)buf, strlen(buf));
		Move(rp, x + (cw - l) / 2, y + ch / 2 - 2);
		Text(rp, (STRPTR)buf, strlen(buf));
		if (ch > 3 * fh + 8) {
			/* where it goes: a group of its own if DEST is multicast */
			const char *d = prefs_value(find_setting("DEST"));
			int a = 0, b = 0, c2 = 0, e = 0;

			if (sscanf(d, "%d.%d.%d.%d", &a, &b, &c2, &e) == 4 && a >= 224 && a <= 239)
				snprintf(buf, sizeof(buf), "%d.%d.%d.%d:%d", a, b, c2, e + (int)i,
				         port + (int)i);
			else
				snprintf(buf, sizeof(buf), "port %d", port + (int)i);
			l = TextLength(rp, (STRPTR)buf, strlen(buf));
			if (l >= cw - 8) {
				snprintf(buf, sizeof(buf), "port %d", port + (int)i);
				l = TextLength(rp, (STRPTR)buf, strlen(buf));
			}
			if (l < cw - 8) {
				Move(rp, x + (cw - l) / 2, y + ch / 2 - 2 + fh + 2);
				Text(rp, (STRPTR)buf, strlen(buf));
			}
		}
	}
	mp_trace("map: the text under it");
	SetAPen(rp, plain);
	snprintf(buf, sizeof(buf), "Desktop %d x %d%s", (int)c * mw, (int)r * mh,
	         c * r > 1 ? ": windows move across" : ": one Panorama screen");
	{
		WORD l = TextLength(rp, (STRPTR)buf, strlen(buf));

		Move(rp, map_x + (map_w - l) / 2, map_y + map_h - 4 - rp->TxHeight + rp->TxBaseline);
		Text(rp, (STRPTR)buf, strlen(buf));
	}
}

static int modesrc(void)
{
	const char *v = prefs_get(find_setting("MODES"));

	return v && *v && mp_stricmp(v, "none") ? 1 : 0;
}

static BOOL make_gadgets(struct Screen *scr)
{
	struct NewGadget ng;
	struct TextAttr *ta = scr->Font;
	struct RastPort rp;
	struct TextFont *tf = OpenFont(ta);
	struct Gadget *g, *btail;
	WORD fh = ta->ta_YSize, row = fh + 7, lw = 0, fw, left, top, i, y[NPAGES], width;

	if (!tf)
		return FALSE;
	InitRastPort(&rp);
	SetFont(&rp, tf);
	for (i = 0; i < NFIELDS; i++) {
		WORD l = TextLength(&rp, (STRPTR)fields[i].label, strlen(fields[i].label));

		if (l > lw)
			lw = l;
	}
	fw = TextLength(&rp, (STRPTR)"00000000000000000000000000000000000", 35);
	CloseFont(tf);

	left = scr->WBorLeft + 12 + lw + 8;
	top = scr->WBorTop + fh + 1 + 6;
	width = left + fw + 12 + scr->WBorRight;
	memset(&ng, 0, sizeof(ng));
	ng.ng_TextAttr = ta;
	ng.ng_VisualInfo = vi;

	/* the page cycle and the buttons */
	g = CreateContext(&base);
	ng.ng_LeftEdge = left;
	ng.ng_TopEdge = top;
	ng.ng_Width = fw;
	ng.ng_Height = fh + 4;
	ng.ng_GadgetText = (UBYTE *)"Page";
	ng.ng_GadgetID = ID_PAGE;
	ng.ng_Flags = PLACETEXT_LEFT;
	g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)page_names,
	                 GTCY_Active, page, TAG_END);
	btail = g;          /* the buttons follow it, in the list that stays */
	page_top = top + row + 4;

	for (i = 0; i < NPAGES; i++) {
		y[i] = page_top + 4;
		CreateContext(&glist[i]);
	}
	/* each page's fields */
	{
		struct Gadget *last[NPAGES];

		for (i = 0; i < NPAGES; i++) {
			last[i] = glist[i];
			while (last[i]->NextGadget)
				last[i] = last[i]->NextGadget;
		}
		for (i = 0; i < NFIELDS; i++) {
			const struct Field *f = &fields[i];
			const struct Setting *s = f->key ? find_setting(f->key) : NULL;
			const char *v = s ? prefs_value(s) : "";
			WORD rows = f->rows ? f->rows : 1, half = (fw - 8) / 2;
			LONG n = 0;
			const char *p = v;

			ng.ng_LeftEdge = left + (f->half == 2 ? half + 8 : 0);
			ng.ng_TopEdge = y[f->page];
			ng.ng_Width = f->half ? half : fw;
			ng.ng_Height = f->kind == LISTVIEW_KIND ? rows * (fh + 1) + 4 : fh + 4;
			ng.ng_GadgetText = (UBYTE *)f->label;
			ng.ng_GadgetID = ID_FIELD + i;
			ng.ng_Flags = f->kind == BUTTON_KIND ? PLACETEXT_IN : PLACETEXT_LEFT;
			if (f->page == PAGE_STATUS) {
				ng.ng_LeftEdge = scr->WBorLeft + 10;
				ng.ng_Width = width - scr->WBorLeft - scr->WBorRight - 20;
			}
			if (f->half != 1)
				y[f->page] += f->kind == LISTVIEW_KIND ? ng.ng_Height + 4 : row;

			if (s && (s->type == T_NUM || s->type == T_BOOL)) {
				BOOL neg = *p == '-';

				if (neg)
					p++;
				while (*p >= '0' && *p <= '9')
					n = n * 10 + *p++ - '0';
				if (neg)
					n = -n;
			}
			switch (f->what) {
			case F_SETTING:
				if (f->kind == STRING_KIND)
					g = CreateGadget(STRING_KIND, last[f->page], &ng, GTST_String, (ULONG)v,
					                 GTST_MaxChars, 79, TAG_END);
				else if (f->kind == INTEGER_KIND)
					g = CreateGadget(INTEGER_KIND, last[f->page], &ng, GTIN_Number, n,
					                 GTIN_MaxChars, 8, TAG_END);
				else
					g = CreateGadget(CHECKBOX_KIND, last[f->page], &ng, GTCB_Checked, n != 0,
					                 TAG_END);
				break;
			case F_MODESRC:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)modesrc_names, GTCY_Active, modesrc(), TAG_END);
				break;
			case F_MODES:
				ng.ng_GadgetText = NULL;
				g = CreateGadget(LISTVIEW_KIND, last[f->page], &ng, GTLV_Labels, (ULONG)&modes,
				                 GTLV_ShowSelected, NULL, TAG_END);
				break;
			case F_MODEADD:
				g = CreateGadget(STRING_KIND, last[f->page], &ng, GTST_String, (ULONG)"",
				                 GTST_MaxChars, 11, TAG_END);
				break;
			case F_ADD:
			case F_REMOVE:
				g = CreateGadget(BUTTON_KIND, last[f->page], &ng, TAG_END);
				break;
			case F_WRAP:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)wrap_names, GTCY_Active, wrap_mode(), TAG_END);
				break;
			case F_MAP:
				/* drawn, not a gadget: the whole width, this many rows */
				map_x = scr->WBorLeft + 10;
				map_w = width - scr->WBorLeft - scr->WBorRight - 20;
				map_y = ng.ng_TopEdge;
				map_h = rows * (fh + 1) + 4;
				y[f->page] += map_h + 6 - row;
				gad[i] = NULL;
				continue;
			case F_LAYOUT:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)layout_names, GTCY_Active, layout_sel, TAG_END);
				break;
			case F_WBPOS:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)wb_labels, GTCY_Active, wb_sel, TAG_END);
				break;
			case F_MONSIZE:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)size_names, GTCY_Active, size_sel, TAG_END);
				break;
			case F_COLOURS:
				g = CreateGadget(CYCLE_KIND, last[f->page], &ng, GTCY_Labels,
				                 (ULONG)colour_names, GTCY_Active, colour_sel, TAG_END);
				break;
			case F_OPEN:
			case F_CLOSE:
				g = CreateGadget(BUTTON_KIND, last[f->page], &ng, TAG_END);
				break;
			case F_STATUS:
				ng.ng_GadgetText = NULL;
				g = CreateGadget(LISTVIEW_KIND, last[f->page], &ng, GTLV_Labels, (ULONG)&stat,
				                 GTLV_ReadOnly, TRUE, TAG_END);
				break;
			}
			if (!g)
				return FALSE;
			gad[i] = g;
			last[f->page] = g;
		}
	}
	page_bottom = page_top;
	for (i = 0; i < NPAGES; i++)
		if (y[i] > page_bottom)
			page_bottom = y[i];
	area_bottom = page_bottom;

	/* the buttons along the bottom */
	{
		static const char *labels[] = { "Save", "Use", "Install", "Cancel" };
		WORD bw = (width - scr->WBorLeft - scr->WBorRight - 20 - 3 * 8) / 4;

		for (i = 0; i < 4; i++) {
			ng.ng_LeftEdge = scr->WBorLeft + 10 + i * (bw + 8);
			ng.ng_TopEdge = page_bottom + 6;
			ng.ng_Width = bw;
			ng.ng_Height = fh + 6;
			ng.ng_GadgetText = (UBYTE *)labels[i];
			ng.ng_GadgetID = ID_SAVE + i;
			ng.ng_Flags = PLACETEXT_IN;
			btail = CreateGadget(BUTTON_KIND, btail, &ng, TAG_END);
		}
		if (!btail)
			return FALSE;
		page_bottom += 6 + fh + 6 + 6 + scr->WBorBottom;
	}
	return TRUE;
}

/* the Workbench cycle, after the place changed (its labels stay) */
static void wb_show(void)
{
	int i = field_index(F_WBPOS);

	if (i >= 0 && gad[i])
		GT_SetGadgetAttrs(gad[i], win, NULL, GTCY_Active, wb_sel, TAG_END);
}

static void show_page(int n)
{
	if (glist[page])
		RemoveGList(win, glist[page], -1);
	SetAPen(win->RPort, 0);
	RectFill(win->RPort, win->BorderLeft, page_top, win->Width - win->BorderRight - 1,
	         area_bottom);
	page = n;
	AddGList(win, glist[page], -1, -1, NULL);
	RefreshGList(glist[page], win, NULL, -1);
	GT_RefreshWindow(win, NULL);
	if (page == PAGE_STATUS)
		status_show(TRUE);
	draw_map();
}

/* ---------------------------------------------------------------- reading the gadgets */

static BOOL set_or_say(const struct Setting *s, const char *v)
{
	const char *why;

	if (!strcmp(v, prefs_value(s)))
		return TRUE;
	why = prefs_set(s, v);
	if (why) {
		char buf[160];

		snprintf(buf, sizeof(buf), "%s %s", s->key, why);
		tell("PanoramaPrefs", buf);
		return FALSE;
	}
	return TRUE;
}

/* every gadget into the settings; FALSE if a value won't do */
static BOOL collect(void)
{
	char buf[256];
	int i, wrap = 0, src = 0;

	for (i = 0; i < NFIELDS; i++) {
		const struct Field *f = &fields[i];
		const struct Setting *s = f->key ? find_setting(f->key) : NULL;
		ULONG v = 0;
		STRPTR str = NULL;

		if (!gad[i])
			continue;
		mp_trace("collect: field %d (%s)", i, f->label);
		if (f->what == F_WRAP) {
			GT_GetGadgetAttrs(gad[i], win, NULL, GTCY_Active, (ULONG)&v, TAG_END);
			wrap = v;
		} else if (f->what == F_MODESRC) {
			GT_GetGadgetAttrs(gad[i], win, NULL, GTCY_Active, (ULONG)&v, TAG_END);
			src = v;
		}
		if (!s)
			continue;
		if (f->kind == STRING_KIND) {
			GT_GetGadgetAttrs(gad[i], win, NULL, GTST_String, (ULONG)&str, TAG_END);
			strncpy(buf, str ? (char *)str : "", sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
		} else if (f->kind == INTEGER_KIND) {
			GT_GetGadgetAttrs(gad[i], win, NULL, GTIN_Number, (ULONG)&v, TAG_END);
			snprintf(buf, sizeof(buf), "%ld", (LONG)v);
		} else {
			GT_GetGadgetAttrs(gad[i], win, NULL, GTCB_Checked, (ULONG)&v, TAG_END);
			snprintf(buf, sizeof(buf), "%d", v ? 1 : 0);
		}
		if (!set_or_say(s, buf))
			return FALSE;
	}

	/* the Desktop page */
	mp_trace("collect: the Desktop page");
	prefs_set(find_setting("SPLIT"), layout_sel ? layout_split[layout_sel] : "");
	prefs_set(find_setting("MONITOR"), size_value[size_sel]);
	prefs_set(find_setting("COLOURS"), colour_value[colour_sel]);
	wb_name(buf, sizeof(buf), wb_side[wb_sel], wb_mon[wb_sel]);
	prefs_set(find_setting("WORKBENCH"), buf);
	if (layout_sel && wrap >= 2) {
		/* a desktop over several windows is one plain Panorama board */
		wrap = 0;
		note("Wrap.card off: the desktop is one Panorama board, split in parts.");
	}

	/* the cycles say which of the fields count */
	if (wrap == 0) {
		prefs_set(find_setting("CARD"), "");
		prefs_set(find_setting("MIRROR"), "");
		prefs_set(find_setting("SPAN"), "");
	} else {
		if (!*prefs_value(find_setting("CARD"))) {
			tell("PanoramaPrefs", "Wrap card: which card should Wrap.card wrap?\n(e.g. uaegfx.card, or Panorama.card)");
			return FALSE;
		}
		if (wrap != 2)
			prefs_set(find_setting("MIRROR"), "");
		if (wrap != 3)
			prefs_set(find_setting("SPAN"), "");
		if (wrap == 2 && !*prefs_value(find_setting("MIRROR"))) {
			tell("PanoramaPrefs", "Mirror on: which card should show the same screen?");
			return FALSE;
		}
		if (wrap == 3 && !*prefs_value(find_setting("SPAN"))) {
			tell("PanoramaPrefs", "Span: how many heads, columns x rows (e.g. 2x2)?");
			return FALSE;
		}
	}
	if (src == 0) {
		prefs_set(find_setting("MODES"), "");
	} else {
		modes_to(buf, sizeof(buf));
		if (!nmodes) {
			tell("PanoramaPrefs", "Screen modes: the list is empty.\nAdd sizes, or choose P96's settings.");
			return FALSE;
		}
		if (!set_or_say(find_setting("MODES"), buf))
			return FALSE;
	}
	return TRUE;
}

static void add_mode(void)
{
	int i = field_index(F_MODEADD);
	STRPTR str = NULL;
	const char *why;

	GT_GetGadgetAttrs(gad[i], win, NULL, GTST_String, (ULONG)&str, TAG_END);
	if (!str || !*str)
		return;
	why = prefs_check(find_setting("MODES"), (char *)str);
	if (why || !mp_stricmp((char *)str, "none")) {
		tell("PanoramaPrefs", "A size is width x height, e.g. 1280x960");
		return;
	}
	if (nmodes >= MAXLISTED)
		return;
	strncpy(mode_text[nmodes], (char *)str, 11);
	mode_text[nmodes][11] = 0;
	nmodes++;
	modes_show();
	GT_SetGadgetAttrs(gad[i], win, NULL, GTST_String, (ULONG)"", TAG_END);
	GT_SetGadgetAttrs(gad[field_index(F_MODESRC)], win, NULL, GTCY_Active, 1, TAG_END);
}

static void remove_mode(void)
{
	int i;

	if (mode_sel < 0 || mode_sel >= nmodes)
		return;
	for (i = mode_sel; i < nmodes - 1; i++)
		strcpy(mode_text[i], mode_text[i + 1]);
	nmodes--;
	modes_show();
}

/* ---------------------------------------------------------------- the desktop */

static struct Screen *dscr;   /* where dri came from */

/* a command, in the background, or waited for */
static void run_desktop(const char *cmd, BOOL async)
{
	BPTR in = Open((STRPTR)"NIL:", MODE_OLDFILE), out = Open((STRPTR)"NIL:", MODE_NEWFILE);

	if (SystemTags((STRPTR)cmd, SYS_Input, in, SYS_Output, out, SYS_Asynch, async,
	               TAG_END) == -1 || !async) {
		if (in)
			Close(in);
		if (out)
			Close(out);
	}
}

/* the settings as they are in the window (Use), and PanoramaDesk started */
static void open_desktop(void)
{
	int i;

	if (!collect())
		return;
	report[0] = 0;
	desktop_ready(note);
	for (i = 0; i < NFIELDS; i++)
		/* the check boxes as desktop_ready() left them (INPUT=) */
		if (gad[i] && fields[i].kind == CHECKBOX_KIND && fields[i].key)
			GT_SetGadgetAttrs(gad[i], win, NULL, GTCB_Checked,
			                  prefs_value(find_setting(fields[i].key))[0] == '1', TAG_END);
	prefs_fix(note);
	if (!prefs_write(FALSE)) {
		tell("PanoramaPrefs", "Could not write the settings to ENV:");
		return;
	}
	if (report[0]) {
		tell("PanoramaPrefs: Desktop", report);
		report[0] = 0;
	}
	run_desktop("C:PanoramaDesk", TRUE);
}

/* ---------------------------------------------------------------- the loop */

int gui(int start_page, int timeout, const char *pubscreen)
{
	struct Screen *scr;
	struct IntuiMessage *msg;
	BOOL done = FALSE;
	int rc = RETURN_OK, ticks = 0, i;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	GadToolsBase = OpenLibrary("gadtools.library", 37);
	if (!IntuitionBase || !GfxBase || !GadToolsBase) {
		printf("PanoramaPrefs: needs intuition, graphics and gadtools.library V37\n");
		rc = RETURN_FAIL;
		goto out;
	}
	page = start_page >= 0 && start_page < NPAGES ? start_page : 0;
	{
		const char *v = prefs_get(find_setting("MODES"));

		modes_from(v && mp_stricmp(v, "none") ? v : "");
		modes_list();
	}
	status_lines(stat_text, 0);
	NewList(&stat);
	desktop_from_settings();

	scr = LockPubScreen((STRPTR)pubscreen);
	if (scr)
		dri = GetScreenDrawInfo(scr);
	if (!scr || !(vi = GetVisualInfo(scr, TAG_END)) || !make_gadgets(scr)) {
		printf("PanoramaPrefs: cannot make the window's gadgets\n");
		rc = RETURN_FAIL;
		if (scr)
			UnlockPubScreen(NULL, scr);
		goto out;
	}
	{
		WORD w = 0;
		struct Gadget *g;

		for (i = 0; i < NPAGES; i++)
			for (g = glist[i]; g; g = g->NextGadget)
				if (g->LeftEdge + g->Width > w)
					w = g->LeftEdge + g->Width;
		win = OpenWindowTags(NULL, WA_Title, (ULONG)"PanoramaPrefs " MP_VERSION,
		                     WA_InnerWidth, w + 12 - scr->WBorLeft,
		                     WA_Height, page_bottom, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
		                     WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (ULONG)scr,
		                     WA_Left, scr->Width > 40 ? 20 : 0, WA_Top, scr->BarHeight + 2,
		                     WA_Gadgets, (ULONG)base, WA_AutoAdjust, TRUE,
		                     WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_GADGETUP |
		                               IDCMP_GADGETDOWN | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS |
		                               IDCMP_MOUSEBUTTONS |
		                               LISTVIEWIDCMP | CYCLEIDCMP,
		                     TAG_END);
	}
	dscr = scr;   /* dri is freed with the window */
	UnlockPubScreen(NULL, scr);
	if (!win) {
		printf("PanoramaPrefs: cannot open the window\n");
		rc = RETURN_FAIL;
		goto out;
	}
	AddGList(win, glist[page], -1, -1, NULL);
	RefreshGList(glist[page], win, NULL, -1);
	GT_RefreshWindow(win, NULL);
	if (page == PAGE_STATUS)
		status_show(TRUE);
	draw_map();

	while (!done) {
		ULONG sigs = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_F);

		if (sigs & SIGBREAKF_CTRL_F) {
			/* PanoramaPrefs started again: this window to the front instead */
			ScreenToFront(win->WScreen);
			WindowToFront(win);
			ActivateWindow(win);
		}
		while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
			ULONG class = msg->Class;
			UWORD code = msg->Code;
			WORD msg_x = msg->MouseX, msg_y = msg->MouseY;
			struct Gadget *g = (struct Gadget *)msg->IAddress;

			GT_ReplyIMsg(msg);
			if (class != IDCMP_INTUITICKS && class != IDCMP_MOUSEMOVE)
				mp_trace("event %08lx code %d gadget %d", class, (int)code,
				         class == IDCMP_GADGETUP || class == IDCMP_GADGETDOWN ?
				         (int)g->GadgetID : -1);
			switch (class) {
			case IDCMP_CLOSEWINDOW:
				done = TRUE;
				break;
			case IDCMP_MOUSEBUTTONS:
				if (code == SELECTDOWN && wb_click(msg_x, msg_y)) {
					wb_show();
					draw_map();
				}
				break;
			case IDCMP_REFRESHWINDOW:
				GT_BeginRefresh(win);
				GT_EndRefresh(win, TRUE);
				draw_map();
				break;
			case IDCMP_INTUITICKS:
				ticks++;
				if (page == PAGE_STATUS && ticks % 20 == 0)
					status_show(TRUE);
				if (timeout && ticks >= timeout * 10)
					done = TRUE;
				break;
			case IDCMP_GADGETUP:
				switch (g->GadgetID) {
				case ID_PAGE:
					show_page(code);
					break;
				case ID_SAVE:
				case ID_USE:
					mp_trace("save/use: collect");
					if (!collect())
						break;
					report[0] = 0;
					prefs_fix(note);
					mp_trace("save/use: write");
					if (!prefs_write(g->GadgetID == ID_SAVE)) {
						tell("PanoramaPrefs", "Could not write the settings to ENV: or ENVARC:");
						break;
					}
					mp_trace("save/use: written, %d bytes of report", (int)strlen(report));
					place_monitors(note);
					if (report[0]) {
						/* changes that the card only takes at boot: offer it */
						if (strstr(report, "reboot") && g->GadgetID == ID_SAVE) {
							if (ask("PanoramaPrefs", report, "Reboot now|Later"))
								mp_reboot();
						} else {
							tell("PanoramaPrefs", report);
						}
					}
					done = TRUE;
					break;
				case ID_INSTALL:
					if (!collect())
						break;
					report[0] = 0;
					prefs_fix(note);
					install(note, TRUE, NULL, NULL);
					if (!ask("PanoramaPrefs: Install", report, "Install|Cancel"))
						break;
					report[0] = 0;
					{
						BOOL ok = install(note, FALSE, NULL, NULL);

						if (!prefs_write(TRUE)) {
							note("Could not write the settings to ENV: or ENVARC:");
							ok = FALSE;
						} else {
							note("Settings saved.");
						}
						/* it all starts at the next boot: offer one now */
						if (ok) {
							note("\nReboot now?");
							if (ask("PanoramaPrefs: Install", report, "Reboot now|Later"))
								mp_reboot();
						} else {
							tell("PanoramaPrefs: Install", report);
						}
					}
					if (page == PAGE_STATUS)
						status_show(TRUE);
					break;
				case ID_CANCEL:
					done = TRUE;
					break;
				default:
					i = g->GadgetID - ID_FIELD;
					if (i >= 0 && i < NFIELDS) {
						if (fields[i].what == F_LAYOUT) {
							int sd = wb_side[wb_sel], mn = wb_mon[wb_sel];

							mp_trace("layout %d", (int)code);
							layout_sel = code;
							wb_positions(sd, mn);
							mp_trace("layout: Workbench place %d", wb_sel);
							wb_show();
							mp_trace("layout: cycle set, drawing");
							draw_map();
							mp_trace("layout: done");
						} else if (fields[i].what == F_WBPOS) {
							int n = wb_step(code);

							wb_sel = n;
							if (n != (int)code)
								wb_show();
							draw_map();
						} else if (fields[i].what == F_MONSIZE) {
							size_sel = code;
							draw_map();
						} else if (fields[i].what == F_COLOURS) {
							colour_sel = code;
						} else if (fields[i].what == F_OPEN) {
							open_desktop();
						} else if (fields[i].what == F_CLOSE) {
							run_desktop("C:PanoramaDesk CLOSE", FALSE);
						} else if (fields[i].what == F_MODESRC && code == 1 && !nmodes) {
							modes_from(standard_modes);
							modes_show();
						} else if (fields[i].what == F_ADD)
							add_mode();
						else if (fields[i].what == F_REMOVE)
							remove_mode();
						else if (fields[i].what == F_MODES)
							mode_sel = code;
					}
					break;
				}
				break;
			}
		}
	}
	RemoveGList(win, glist[page], -1);
	CloseWindow(win);
	win = NULL;
	if (dri)
		FreeScreenDrawInfo(dscr, dri);
	dri = NULL;
out:
	for (i = 0; i < NPAGES; i++)
		if (glist[i])
			FreeGadgets(glist[i]);
	if (base)
		FreeGadgets(base);
	if (vi)
		FreeVisualInfo(vi);
	if (GadToolsBase)
		CloseLibrary(GadToolsBase);
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
