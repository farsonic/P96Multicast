/*
 * P96Multicast - MulticastPrefs: the window.
 *
 * GadTools, so it runs on AmigaOS 3.x and AROS alike. Four pages, chosen
 * with the cycle gadget at the top, each its own gadget list swapped in
 * and out: Network, Display (with the screen modes), Wrap.card and Status.
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
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *GadToolsBase;

#define NPAGES 4
static const char *page_names[] = { "Network", "Display", "Wrap.card", "Status", NULL };

/* the fields: a gadget for a setting, or for something on the page itself */
#define F_SETTING 0     /* key: a setting */
#define F_MODESRC 1     /* where the modes come from */
#define F_MODES   2     /* the mode list */
#define F_MODEADD 3     /* a size to add */
#define F_ADD     4
#define F_REMOVE  5
#define F_WRAP    6     /* how Wrap.card is used */
#define F_STATUS  7     /* the status lines */

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
	{ 0, F_SETTING, STRING_KIND,   "Send to",          "DEST" },
	{ 0, F_SETTING, INTEGER_KIND,  "UDP port",         "PORT" },
	{ 0, F_SETTING, INTEGER_KIND,  "Multicast TTL",    "TTL" },
	{ 0, F_SETTING, INTEGER_KIND,  "Updates/second",   "FPS" },
	{ 0, F_SETTING, INTEGER_KIND,  "Most KB/second",   "RATE" },
	{ 0, F_SETTING, INTEGER_KIND,  "Resend all (s)",   "REFRESH" },
	{ 0, F_SETTING, STRING_KIND,   "Log file",         "LOG" },
	{ 0, F_SETTING, CHECKBOX_KIND, "Record drawing",   "BLITTER" },

	{ 1, F_SETTING, INTEGER_KIND,  "Boards",           "BOARDS" },
	{ 1, F_SETTING, INTEGER_KIND,  "Video MB/board",   "VRAM" },
	{ 1, F_SETTING, INTEGER_KIND,  "Largest width",    "MAXWIDTH" },
	{ 1, F_SETTING, INTEGER_KIND,  "Largest height",   "MAXHEIGHT" },
	{ 1, F_MODESRC, CYCLE_KIND,    "Screen modes",     NULL },
	{ 1, F_MODES,   LISTVIEW_KIND, "",                 NULL, 5 },
	{ 1, F_MODEADD, STRING_KIND,   "Size (WxH)",       NULL },
	{ 1, F_ADD,     BUTTON_KIND,   "Add",              NULL, 1, 1 },
	{ 1, F_REMOVE,  BUTTON_KIND,   "Remove",           NULL, 1, 2 },

	{ 2, F_WRAP,    CYCLE_KIND,    "Use Wrap.card",    NULL },
	{ 2, F_SETTING, STRING_KIND,   "Wrap card",        "CARD" },
	{ 2, F_SETTING, STRING_KIND,   "Mirror on",        "MIRROR" },
	{ 2, F_SETTING, STRING_KIND,   "Span (CxR)",       "SPAN" },
	{ 2, F_SETTING, STRING_KIND,   "Every head",       "HEAD" },
	{ 2, F_SETTING, STRING_KIND,   "Head 1",           "HEAD1" },
	{ 2, F_SETTING, STRING_KIND,   "Head 2",           "HEAD2" },
	{ 2, F_SETTING, STRING_KIND,   "Head 3",           "HEAD3" },
	{ 2, F_SETTING, STRING_KIND,   "Head 4",           "HEAD4" },
	{ 2, F_SETTING, CHECKBOX_KIND, "Share memory",     "HEADSHARE" },
	{ 2, F_SETTING, INTEGER_KIND,  "Head updates/s",   "MIRRORFPS" },

	{ 3, F_STATUS,  LISTVIEW_KIND, "",                 NULL, 14 },
};
#define NFIELDS (sizeof(fields) / sizeof(fields[0]))

static const char *modesrc_names[] = { "Built-in list", "This list", "P96's settings", NULL };
static const char *wrap_names[] = { "No", "Wrap only", "Mirror", "Span", NULL };

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

	es.es_Title = (UBYTE *)title;
	EasyRequest(win, &es, NULL, (ULONG)text);
}

static BOOL ask(const char *title, const char *text, const char *buttons)
{
	struct EasyStruct es = { sizeof(struct EasyStruct), 0, NULL, "%s", NULL };

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

static int modesrc(void)
{
	const char *v = prefs_get(find_setting("MODES"));

	if (!v || !*v)
		return 0;
	return mp_stricmp(v, "none") ? 1 : 2;
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
			if (f->page == 3) {
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
	if (page == 3)
		status_show(TRUE);
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
		tell("MulticastPrefs", buf);
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

	/* the cycles say which of the fields count */
	if (wrap == 0) {
		prefs_set(find_setting("CARD"), "");
		prefs_set(find_setting("MIRROR"), "");
		prefs_set(find_setting("SPAN"), "");
	} else {
		if (!*prefs_value(find_setting("CARD"))) {
			tell("MulticastPrefs", "Wrap card: which card should Wrap.card wrap?\n(e.g. uaegfx.card, or Multicast.card)");
			return FALSE;
		}
		if (wrap != 2)
			prefs_set(find_setting("MIRROR"), "");
		if (wrap != 3)
			prefs_set(find_setting("SPAN"), "");
		if (wrap == 2 && !*prefs_value(find_setting("MIRROR"))) {
			tell("MulticastPrefs", "Mirror on: which card should show the same screen?");
			return FALSE;
		}
		if (wrap == 3 && !*prefs_value(find_setting("SPAN"))) {
			tell("MulticastPrefs", "Span: how many heads, columns x rows (e.g. 2x2)?");
			return FALSE;
		}
	}
	if (src == 0) {
		prefs_set(find_setting("MODES"), "");
	} else if (src == 2) {
		prefs_set(find_setting("MODES"), "NONE");
	} else {
		modes_to(buf, sizeof(buf));
		if (!nmodes) {
			tell("MulticastPrefs", "Screen modes: the list is empty.\nAdd sizes, or choose the built-in list.");
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
		tell("MulticastPrefs", "A size is width x height, e.g. 1280x960");
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
		printf("MulticastPrefs: needs intuition, graphics and gadtools.library V37\n");
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

	scr = LockPubScreen((STRPTR)pubscreen);
	if (!scr || !(vi = GetVisualInfo(scr, TAG_END)) || !make_gadgets(scr)) {
		printf("MulticastPrefs: cannot make the window's gadgets\n");
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
		win = OpenWindowTags(NULL, WA_Title, (ULONG)"MulticastPrefs " MP_VERSION,
		                     WA_InnerWidth, w + 12 - scr->WBorLeft,
		                     WA_Height, page_bottom, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
		                     WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_PubScreen, (ULONG)scr,
		                     WA_Left, scr->Width > 40 ? 20 : 0, WA_Top, scr->BarHeight + 2,
		                     WA_Gadgets, (ULONG)base, WA_AutoAdjust, TRUE,
		                     WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_GADGETUP |
		                               IDCMP_GADGETDOWN | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS |
		                               LISTVIEWIDCMP | CYCLEIDCMP,
		                     TAG_END);
	}
	UnlockPubScreen(NULL, scr);
	if (!win) {
		printf("MulticastPrefs: cannot open the window\n");
		rc = RETURN_FAIL;
		goto out;
	}
	AddGList(win, glist[page], -1, -1, NULL);
	RefreshGList(glist[page], win, NULL, -1);
	GT_RefreshWindow(win, NULL);
	if (page == 3)
		status_show(TRUE);

	while (!done) {
		WaitPort(win->UserPort);
		while ((msg = GT_GetIMsg(win->UserPort)) != NULL) {
			ULONG class = msg->Class;
			UWORD code = msg->Code;
			struct Gadget *g = (struct Gadget *)msg->IAddress;

			GT_ReplyIMsg(msg);
			switch (class) {
			case IDCMP_CLOSEWINDOW:
				done = TRUE;
				break;
			case IDCMP_REFRESHWINDOW:
				GT_BeginRefresh(win);
				GT_EndRefresh(win, TRUE);
				break;
			case IDCMP_INTUITICKS:
				ticks++;
				if (page == 3 && ticks % 20 == 0)
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
					if (!collect())
						break;
					report[0] = 0;
					prefs_fix(note);
					if (!prefs_write(g->GadgetID == ID_SAVE)) {
						tell("MulticastPrefs", "Could not write the settings to ENV: or ENVARC:");
						break;
					}
					if (report[0])
						tell("MulticastPrefs", report);
					done = TRUE;
					break;
				case ID_INSTALL:
					if (!collect())
						break;
					report[0] = 0;
					prefs_fix(note);
					install(note, TRUE, NULL);
					if (!ask("MulticastPrefs: Install", report, "Install|Cancel"))
						break;
					report[0] = 0;
					install(note, FALSE, NULL);
					if (!prefs_write(TRUE))
						note("Could not write the settings to ENV: or ENVARC:");
					else
						note("Settings saved.");
					tell("MulticastPrefs: Install", report);
					if (page == 3)
						status_show(TRUE);
					break;
				case ID_CANCEL:
					done = TRUE;
					break;
				default:
					i = g->GadgetID - ID_FIELD;
					if (i >= 0 && i < NFIELDS) {
						if (fields[i].what == F_ADD)
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
