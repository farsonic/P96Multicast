/*
 * Panorama - muigui.c: PanoramaPrefs' MUI window, built as RiptidePrefs'
 * (Riptide's src/amiga/prefs/mui_main.c and mui_classes.c), so the two
 * editors look and work alike: the page list on the left (under
 * "Settings" and "Advanced"), the page's head (icon, name, what it is
 * about) and its settings on the right, a note under them, the Save, Use,
 * Install and Cancel buttons and a status line below. Settings are MUI's
 * string fields, check boxes and cycle gadgets with labels of the theme;
 * messages come in small windows of the theme. Every command is in the
 * menus and goes through command(); keyboard: the underlined letters, P
 * and Shift-P (or the cursor keys) for the pages, Esc to cancel. Short
 * screens (under 400 lines) get the compact layout.
 *
 * Panorama's own parts, drawn the same way: the picture of the monitors on
 * the Desktop page (click beside them to put the Workbench there), the
 * screen mode list on the Display page, the Open and Close desktop
 * buttons, and the Status page.
 *
 * MUI 3.8 fills MUIM_AskMinMax's MinMaxInfo in during the superclass's
 * method: every class reads it after DoSuperMethodA (reading it first
 * crashed real Amigas, PanoramaPrefs 2.x). The family's guide is
 * docs/mui-guide/README.md.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <libraries/gadtools.h>
#include <libraries/mui.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>
#include "mcprefs.h"
#include "theme.h"
#include "lang.h"
#include "ids.h"

struct Library *MUIMasterBase, *UtilityBase;
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase *GfxBase;

#define TITLE "Panorama Preferences"
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif
#define MTAG (TAG_USER | 0x4d500000UL)
#define DISPATCHER(n) static ULONG n(__reg("a0") struct IClass *cl, __reg("a2") Object *obj, \
                                     __reg("a1") Msg msg)

#define RAW_P      0x19
#define RAW_UP     0x4C
#define RAW_DOWN   0x4D
#define RAW_ESC    0x45
#define RAW_RETURN 0x44
#define SHIFTS     (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)

/* ================================================================ the pages */

enum { PG_DESKTOP, PG_NETWORK, PG_DISPLAY, PG_WRAP, PG_STATUS, NPAGES };
#define FIRST_ADVANCED PG_WRAP      /* pages from here on are "Advanced" */

static const struct {
	const char *title;
	const char *note;       /* one line under the settings */
	const char *help;       /* what the page is about (head, bubble) */
} pages_text[NPAGES] = {
	{ "Desktop", "The desktop's size and colours need a reboot",
	  "One Panorama screen shown in viewer windows on other computers, like monitors side by side, "
	  "with the Workbench next to them." },
	{ "Network", "Empty: the built-in value",
	  "Where Panorama.card sends the screen, how often and how fast, and whose mouse and keys it "
	  "takes." },
	{ "Display", "Boards, memory and modes need a reboot",
	  "How many Panorama boards there are, their video memory, and the screen modes they offer." },
	{ "Wrap.card", "Wrap.card changes need a reboot",
	  "Wrap.card around another card: the Workbench's display kept on beside the desktop, the same "
	  "screen on a second board, or one screen over several." },
	{ "Status", "Brought up to date every two seconds",
	  "What Panorama.card and Wrap.card are doing now, and what is installed." },
};

/* the Desktop page's choices, and the settings they stand for */
static const char *const layout_names[] = { "One", "Two, side by side", "Two, one above the other",
                                            "Three, side by side", "Four, side by side",
                                            "Four, two by two", NULL };
static const char *const layout_split[] = { "1x1", "2x1", "1x2", "3x1", "4x1", "2x2" };
static const char *const size_names[] = { "640 x 480", "800 x 600", "1024 x 768", "1280 x 720",
                                          "1280 x 1024", "1920 x 1080", NULL };
static const char *const size_value[] = { "640x480", "800x600", "1024x768", "1280x720",
                                          "1280x1024", "1920x1080" };
static const char *const colour_names[] = { "256 colours (8 bit)", "65536 colours (16 bit)",
                                            "16 million colours (32 bit)", NULL };
static const char *const colour_value[] = { "8", "16", "32" };
static const char *const modesrc_names[] = { "P96's settings", "The list below", NULL };
static const char *const wrap_names[] = { "No", "Wrap only", "Mirror", "Span", NULL };
static const char *standard_modes = "640x480,800x600,1024x768,1280x720,1280x960,1280x1024,1920x1080";

static int layout_sel, size_sel, colour_sel;

/* where the Workbench can be: next to an outer edge of a monitor, or nowhere (16 + 1) */
#define NWB 17
static int wb_sel, wb_side[NWB], wb_mon[NWB];
static char wb_text[NWB][24];
/* the places this layout has, as the cycle gadget shows them */
static int wb_valid_idx[NWB], nwb_valid;
static const char *wb_cy[NWB + 1];

/* the mode list and the status lines */
#define MAXMODES 24
static char mode_text[MAXMODES][12];
static int nmodes;
#define NSTATUS 24
static char stat_text[NSTATUS][80];

/* the settings shown as fields: K_TEXT and K_NUM strings, K_BOOL check boxes */
enum { K_TEXT, K_NUM, K_BOOL };
struct Field {
	UBYTE page;
	UBYTE kind;
	const char *key;
	const char *label;      /* '_' marks the keyboard shortcut */
	Object *obj, *lab;
};

static struct Field fields[] = {
	{ PG_DESKTOP, K_BOOL, "DEFAULTSCREEN", "Programs open on it" },
	{ PG_DESKTOP, K_BOOL, "ATBOOT", "Open at boot" },
	{ PG_DESKTOP, K_BOOL, "BEHIND", "Behind the Workbench" },
	{ PG_DESKTOP, K_BOOL, "INPUT", "Viewers control it" },
	{ PG_DESKTOP, K_BOOL, "WBMONITOR", "Workbench stays shown" },
	{ PG_DESKTOP, K_BOOL, "P96SCREENCX", "P96ScreenCx tool types" },

	{ PG_NETWORK, K_TEXT, "DEST", "Send _to" },
	{ PG_NETWORK, K_TEXT, "DEST2", "Monitor _2 to" },
	{ PG_NETWORK, K_TEXT, "DEST3", "Monitor _3 to" },
	{ PG_NETWORK, K_TEXT, "DEST4", "Monitor _4 to" },
	{ PG_NETWORK, K_NUM, "PORT", "UDP _port" },
	{ PG_NETWORK, K_NUM, "TTL", "Multicast _hops" },
	{ PG_NETWORK, K_NUM, "FPS", "Updates a s_econd" },
	{ PG_NETWORK, K_NUM, "RATE", "Most _KB a second" },
	{ PG_NETWORK, K_NUM, "SCAN", "Check all in (s)" },
	{ PG_NETWORK, K_TEXT, "INPUTFROM", "Input only _from" },
	{ PG_NETWORK, K_TEXT, "LOG", "Log _file" },
	{ PG_NETWORK, K_BOOL, "BLITTER", "Send the drawing" },
	{ PG_NETWORK, K_BOOL, "POINTER", "Viewers draw the pointer" },

	{ PG_DISPLAY, K_NUM, "BOARDS", "_Boards" },
	{ PG_DISPLAY, K_NUM, "VRAM", "Video _MB a board" },
	{ PG_DISPLAY, K_NUM, "MAXWIDTH", "Widest _mode" },
	{ PG_DISPLAY, K_NUM, "MAXHEIGHT", "_Highest mode" },

	{ PG_WRAP, K_TEXT, "CARD", "Card to _wrap" },
	{ PG_WRAP, K_TEXT, "MIRROR", "_Mirror on" },
	{ PG_WRAP, K_TEXT, "SPAN", "S_pan (CxR)" },
	{ PG_WRAP, K_TEXT, "HEAD", "_Every head" },
	{ PG_WRAP, K_TEXT, "HEAD1", "Head _1" },
	{ PG_WRAP, K_TEXT, "HEAD2", "Head _2" },
	{ PG_WRAP, K_NUM, "MIRRORFPS", "Head _updates a second" },
	{ PG_WRAP, K_BOOL, "HEADSHARE", "Heads share its memory" },
	{ PG_WRAP, K_BOOL, "KEEPDISPLAY", "Keep its display on" },
};
#define NFIELDS (sizeof(fields) / sizeof(fields[0]))

/* ================================================================ state */

static Object *app, *win, *strip, *root, *nav, *head, *pages, *status, *bar;
static Object *btn[4], *b_open, *b_close;
static Object *map, *cy_layout, *cy_size, *cy_colours, *cy_wb, *wb_cell, *cy_modesrc, *cy_wrap;
static Object *mode_list, *mode_line, *mode_btn[3], *stat_list;
static int page;
static char status_text[160];
static const char *pubname;
static char report[2048];

static const char *const btn_label[4] = { "_Save", "_Use", "_Install", "_Cancel" };
static const ULONG btn_id[4] = { ID_SAVE, ID_USE, ID_INSTALL, ID_CANCEL };
static const char *const btn_help[4] = {
	"Saves the settings for good and uses them now. Some only take effect after a reboot.",
	"Uses the settings until the Amiga is switched off, without saving them for good.",
	"Installs Panorama.card, Wrap.card, the tools and the monitors for these settings, "
	"saves them, and offers to reboot.",
	"Closes the window. Changes that were not saved or used are forgotten.",
};

/* every object with a help bubble, so Help Bubbles can take them away and back */
#define MAXHELP 96
static Object *help_obj[MAXHELP];
static char *help_text[MAXHELP];
static int nhelp;

/* the value of an attribute */
static ULONG xget(Object *obj, ULONG attr)
{
	ULONG v = 0;

	get(obj, attr, &v);
	return v;
}

/* ================================================================ small things */

static int layout_cols(void)
{
	return layout_split[layout_sel][0] - '0';
}

static int layout_rows(void)
{
	return layout_split[layout_sel][2] - '0';
}

static void monitor_size(UWORD *mw, UWORD *mh)
{
	const char *sz = size_value[size_sel];

	*mw = *mh = 0;
	while (*sz >= '0' && *sz <= '9')
		*mw = *mw * 10 + *sz++ - '0';
	sz++;
	while (*sz >= '0' && *sz <= '9')
		*mh = *mh * 10 + *sz++ - '0';
}

/* the index of v in values, or -1 */
static int pick(const char *const *values, int n, const char *v)
{
	int i;

	for (i = 0; i < n; i++)
		if (!mp_stricmp(values[i], v))
			return i;
	return -1;
}

/* can the Workbench be at place n with this layout: an outer edge of one of its monitors? */
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

/* every place, for any layout; then the one for side, mon (moved to fit the layout) */
static void wb_places(int side, int mon)
{
	static const char *names[] = { "", "Left of", "Right of", "Above", "Below" };
	int i, n = 0, sd;

	if (!wb_text[0][0]) {
		for (sd = WB_LEFT; sd <= WB_BELOW; sd++)
			for (i = 0; i < 4; i++, n++) {
				wb_side[n] = sd;
				wb_mon[n] = i;
				snprintf(wb_text[n], sizeof(wb_text[n]), "%s monitor %d", tr(names[sd]), i + 1);
			}
		wb_side[n] = WB_NONE;
		strcpy(wb_text[n], tr("Nowhere (hotkey only)"));
	}
	if (side != WB_NONE)
		wb_fit(&side, &mon, layout_cols(), layout_rows());
	wb_sel = NWB - 1;
	for (i = 0; i < NWB; i++)
		if (wb_side[i] == side && (side == WB_NONE || wb_mon[i] == mon))
			wb_sel = i;
}

/* the places this layout has, for the cycle gadget; the chosen one's position in them */
static int wb_cycle_entries(void)
{
	int i, at = 0;

	nwb_valid = 0;
	for (i = 0; i < NWB; i++)
		if (wb_fits(i)) {
			if (i == wb_sel)
				at = nwb_valid;
			wb_valid_idx[nwb_valid] = i;
			wb_cy[nwb_valid++] = wb_text[i];
		}
	wb_cy[nwb_valid] = NULL;
	return at;
}

static void modes_from(const char *v)
{
	nmodes = 0;
	while (*v && nmodes < MAXMODES) {
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

/* a line for the report a message shows after Save, Use, Install or Open */
static void note(const char *s)
{
	int n = strlen(report);

	snprintf(report + n, sizeof(report) - n, "%s%s", n ? "\n" : "", tr(s));
}

/* a bubble text kept with its object, and the object noted for Help Bubbles */
static void with_help(Object *obj, const char *name, const char *body)
{
	char buf[400], *p;

	if (!obj || nhelp >= MAXHELP)
		return;
	theme_bubble_to(buf, sizeof(buf), name, body);
	if (!(p = AllocVec(strlen(buf) + 1, MEMF_ANY)))
		return;
	strcpy(p, buf);
	help_obj[nhelp] = obj;
	help_text[nhelp++] = p;
	set(obj, MUIA_ShortHelp, (ULONG)p);
}

/* a label's text without its '_' */
static void plain(const char *s, char *out, int len)
{
	int n = 0;

	for (s = tr(s); *s && n < len - 1; s++)
		if (*s != '_')
			out[n++] = *s;
	out[n] = 0;
}

/* a label's shortcut key, lower case, 0: none */
static char key_of(const char *s)
{
	const char *u = strchr(tr(s), '_');

	return u && u[1] ? (char)(u[1] >= 'A' && u[1] <= 'Z' ? u[1] + 32 : u[1]) : 0;
}

/* ================================================================ the classes */

/* the tags of our own classes, as RiptidePrefs' (a range of our own) */
#define MUIA_Nav_Active     (MTAG + 0x101)  /* (init, set, get, notify) the page shown */
#define MUIA_Head_Page      (MTAG + 0x201)  /* (init, set) the page described */
#define MUIA_QButton_Label  (MTAG + 0x301)  /* (init) "_Save" */
#define MUIA_QButton_Key    (MTAG + 0x302)  /* (init) a raw key code (Return, Esc) that presses it too */
#define MUIA_QButton_ID     (MTAG + 0x303)  /* (init) the return ID that key gives */
#define MUIA_QLabel_Text    (MTAG + 0x401)  /* (init, set) the text (not copied) */
#define MUIA_QLabel_Kind    (MTAG + 0x402)  /* (init) QL_* */
#define MUIA_Status_Text    (MTAG + 0x501)  /* (set) the text (not copied) */
#define MUIA_Map_Clicked    (MTAG + 0x701)  /* (notify) the Workbench was put somewhere else */

enum { QL_LABEL, QL_NOTE, QL_TITLE, QL_TEXT };   /* QL_TEXT: several lines (\n) */

static struct MUI_CustomClass *nav_mcc, *head_mcc, *qbutton_mcc, *qlabel_mcc, *status_mcc, *line_mcc,
                              *map_mcc;

/* the tag's value in an OM_NEW/OM_SET message, or def */
static ULONG tagval(Msg msg, ULONG tag, ULONG def)
{
	return GetTagData(tag, def, ((struct opSet *)msg)->ops_AttrList);
}

static BOOL has_tag(Msg msg, ULONG tag)
{
	return FindTagItem(tag, ((struct opSet *)msg)->ops_AttrList) != NULL;
}

/* a text's width in the theme's font (a rastport of our own: no bitmap needed) */
static LONG measure(const char *s, int key)
{
	struct RastPort rp;

	InitRastPort(&rp);
	return key ? theme_keytextlen(&rp, s) : theme_textlen(&rp, s);
}

/* the rows of a MUI_MinMax, added after the superclass has filled it in (MUI 3.8) */
static void minmax(Msg msg, LONG minw, LONG defw, LONG maxw, LONG minh, LONG defh, LONG maxh)
{
	struct MUI_MinMax *m = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;

	m->MinWidth += (WORD)minw;
	m->DefWidth += (WORD)defw;
	m->MaxWidth += (WORD)maxw;
	m->MinHeight += (WORD)minh;
	m->DefHeight += (WORD)defh;
	m->MaxHeight += (WORD)maxh;
}

/* the pointer in an object */
static int inside(Object *obj, struct IntuiMessage *im)
{
	return im->MouseX >= _mleft(obj) && im->MouseX <= _mright(obj) && im->MouseY >= _mtop(obj) &&
	       im->MouseY <= _mbottom(obj);
}

/* add or remove an object's event handler (between MUIM_Setup and Cleanup) */
static void handler(Object *obj, struct IClass *cl, struct MUI_EventHandlerNode *ehn, ULONG events, int add)
{
	if (add) {
		ehn->ehn_Priority = 0;
		ehn->ehn_Flags = 0;
		ehn->ehn_Object = obj;
		ehn->ehn_Class = cl;
		ehn->ehn_Events = events;
		DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)ehn);
	} else {
		DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)ehn);
	}
}

/* ---------------------------------------------------------------- nav: the page list */
struct nav_data {
	struct MUI_EventHandlerNode ehn;
	int active, hot, shown;
};

/* compact (short screens): no headings, no icons, rows just over the font */
static int nav_rowh(void)
{
	return theme_fh() + (theme_compact() ? 2 : 10);
}

static int nav_headh(void)
{
	return theme_compact() ? 0 : theme_fh() + 8;
}

static int nav_isz(void)
{
	return theme_compact() ? 0 : nav_rowh() - 2 < 16 ? nav_rowh() - 2 : 16;
}

/* the top of row i (page i) below the object's top */
static LONG nav_rowy(int i)
{
	return nav_headh() * (i >= FIRST_ADVANCED ? 2 : 1) + (LONG)i * nav_rowh();
}

/* the page under y, or -1 */
static int nav_hit(Object *obj, LONG y)
{
	int i;

	for (i = 0; i < NPAGES; i++) {
		LONG t = _mtop(obj) + nav_rowy(i);

		if (y >= t && y < t + nav_rowh())
			return i;
	}
	return -1;
}

static void nav_draw(struct nav_data *d, Object *obj)
{
	struct RastPort *rp = _rp(obj);
	LONG x = _mleft(obj), r = _mright(obj), fh = theme_fh();
	int i, isz = nav_isz(), gap = isz ? isz + 8 : 0;

	theme_fill(rp, x, _mtop(obj), r - 1, _mbottom(obj), tpen(C_PANEL));
	theme_line(rp, r, _mtop(obj), r, _mbottom(obj), tpen(C_BORDER));
	if (nav_headh()) {
		theme_text(rp, x + 10, _mtop(obj) + (nav_headh() - fh) / 2 + 1, "Settings", tpen(C_DIM), 1);
		theme_text(rp, x + 10, _mtop(obj) + nav_rowy(FIRST_ADVANCED) - nav_headh() + (nav_headh() - fh) / 2 + 1,
		           "Advanced", tpen(C_DIM), 1);
	}
	for (i = 0; i < NPAGES; i++) {
		LONG t = _mtop(obj) + nav_rowy(i), b = t + nav_rowh() - 1;

		if (i == d->active) {
			theme_fill(rp, x, t, r - 1, b, tpen(C_PRESSED));
			theme_fill(rp, x, t, x + 2, b, tpen(C_ACCENT));
		} else if (i == d->hot) {
			theme_fill(rp, x, t, r - 1, b, tpen(C_HOVER));
		}
		if (isz)
			theme_icon(rp, i, x + 10, t + (nav_rowh() - isz) / 2, isz, 0);
		theme_text(rp, x + 10 + gap, t + (nav_rowh() - fh) / 2, pages_text[i].title, tpen(C_TEXT),
		           i == d->active);
	}
}

/* change the page, through OM_SET so notifications go out */
static void nav_go(Object *obj, int pg)
{
	SetAttrs(obj, MUIA_Nav_Active, (ULONG)pg, TAG_DONE);
}

DISPATCHER(nav_dispatch)
{
	struct nav_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		d->active = (int)tagval(msg, MUIA_Nav_Active, 0);
		d->hot = -1;
		return (ULONG)obj;
	case OM_SET:
		d = INST_DATA(cl, obj);
		if (has_tag(msg, MUIA_Nav_Active)) {
			int p = (int)tagval(msg, MUIA_Nav_Active, 0);

			if (p >= 0 && p < NPAGES && p != d->active) {
				d->active = p;
				if (d->shown)
					MUI_Redraw(obj, MADF_DRAWOBJECT);
			}
		}
		break;
	case OM_GET:
		if (((struct opGet *)msg)->opg_AttrID == MUIA_Nav_Active) {
			d = INST_DATA(cl, obj);
			*((struct opGet *)msg)->opg_Storage = (ULONG)d->active;
			return TRUE;
		}
		break;
	case MUIM_AskMinMax: {
		LONG w = 0, ww;
		int i;

		DoSuperMethodA(cl, obj, msg);
		for (i = 0; i < NPAGES; i++)
			if ((ww = measure(pages_text[i].title, 0) + 1) > w)
				w = ww;
		w += 10 + (nav_isz() ? nav_isz() + 8 : 0) + 14;
		ww = nav_rowy(NPAGES) + 4;
		minmax(msg, w, w, w, ww, ww, MUI_MAXMAX);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY, 1);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, 0, 0);
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct nav_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct nav_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			nav_draw(INST_DATA(cl, obj), obj);
		return 0;
	case MUIM_HandleEvent: {
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
		int i;

		d = INST_DATA(cl, obj);
		if (!im || !d->shown)
			return 0;
		if (im->Class == IDCMP_MOUSEMOVE) {
			i = inside(obj, im) ? nav_hit(obj, im->MouseY) : -1;
			if (i != d->hot) {
				d->hot = i;
				MUI_Redraw(obj, MADF_DRAWOBJECT);
			}
			return 0;
		}
		if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && inside(obj, im)) {
			i = nav_hit(obj, im->MouseY);
			if (i >= 0)
				nav_go(obj, i);
			return MUI_EventHandlerRC_Eat;
		}
		if (im->Class == IDCMP_RAWKEY && !(im->Code & IECODE_UP_PREFIX)) {
			int back = (im->Qualifier & SHIFTS) != 0;

			if (im->Code == RAW_P) {
				nav_go(obj, (d->active + (back ? NPAGES - 1 : 1)) % NPAGES);
				return MUI_EventHandlerRC_Eat;
			}
			if (im->Code == RAW_ESC) {
				/* Esc: Cancel, as the close gadget */
				DoMethod(_app(obj), MUIM_Application_ReturnID, ID_CANCEL);
				return MUI_EventHandlerRC_Eat;
			}
			if ((im->Code == RAW_UP && d->active > 0) || (im->Code == RAW_DOWN && d->active < NPAGES - 1)) {
				nav_go(obj, d->active + (im->Code == RAW_UP ? -1 : 1));
				return MUI_EventHandlerRC_Eat;
			}
		}
		return 0;
	}
	case MUIM_CreateShortHelp: {
		struct MUIP_CreateShortHelp *m = (struct MUIP_CreateShortHelp *)msg;
		int i = nav_hit(obj, m->my);

		if (i < 0)
			return 0;
		return (ULONG)theme_bubble(pages_text[i].title, pages_text[i].help);
	}
	case MUIM_DeleteShortHelp:
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- head: the page's icon, name, description */
struct head_data {
	int page, shown;
};

#define HEAD_LINES 3

/* compact: the page's name on one line, no icon or description */
static LONG head_h(void)
{
	if (theme_compact())
		return theme_fh() + 7;
	return 8 + theme_fh() + 4 + HEAD_LINES * (theme_fh() + 1) + 8;
}

static LONG head_isz(void)
{
	return theme_fh() * 2 + 8 > 32 ? theme_fh() * 2 + 8 : 32;
}

/* draw s in lines of at most w pixels from x,y; at most n lines */
static void wrap_text(struct RastPort *rp, LONG x, LONG y, LONG w, const char *s, int n, LONG pen)
{
	char line[120];
	int len = 0;

	s = tr(s);
	while (*s && n > 0) {
		const char *e = s;
		int k = len;

		while (*e == ' ')
			e++;
		if (k)
			line[k++] = ' ';
		while (*e && *e != ' ' && k < (int)sizeof(line) - 1)
			line[k++] = *e++;
		line[k] = 0;
		if (len && TextLength(rp, (STRPTR)line, (ULONG)k) > w) {
			line[len] = 0;
			theme_text(rp, x, y, line, pen, 0);
			y += theme_fh() + 1;
			n--;
			len = 0;
			continue;
		}
		len = k;
		s = e;
	}
	if (len && n > 0)
		theme_text(rp, x, y, line, pen, 0);
}

static void head_draw(struct head_data *d, Object *obj)
{
	struct RastPort *rp = _rp(obj);
	LONG x = _mleft(obj), y = _mtop(obj), isz = head_isz(), tx = x + 12 + isz + 12;

	theme_fill(rp, x, y, _mright(obj), _mbottom(obj) - 1, tpen(C_HEAD));
	theme_line(rp, x, _mbottom(obj), _mright(obj), _mbottom(obj), tpen(C_BORDER));
	if (theme_compact()) {
		theme_text(rp, x + 12, y + 3, pages_text[d->page].title, tpen(C_TEXT), 1);
		return;
	}
	theme_icon(rp, d->page, x + 12, y + (_mheight(obj) - isz) / 2, isz, 0);
	theme_text(rp, tx, y + 8, pages_text[d->page].title, tpen(C_TEXT), 1);
	if (theme_font())
		SetFont(rp, theme_font());
	wrap_text(rp, tx, y + 8 + theme_fh() + 4, _mright(obj) - tx - 8, pages_text[d->page].help, HEAD_LINES,
	          tpen(C_DIM));
}

DISPATCHER(head_dispatch)
{
	struct head_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		d->page = (int)tagval(msg, MUIA_Head_Page, 0);
		return (ULONG)obj;
	case OM_SET:
		d = INST_DATA(cl, obj);
		if (has_tag(msg, MUIA_Head_Page)) {
			d->page = (int)tagval(msg, MUIA_Head_Page, 0);
			if (d->shown)
				MUI_Redraw(obj, MADF_DRAWOBJECT);
		}
		break;
	case MUIM_AskMinMax: {
		LONG h = head_h(), w = theme_fh() * 30;

		DoSuperMethodA(cl, obj, msg);
		minmax(msg, w, w, MUI_MAXMAX, h, h, h);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		return TRUE;
	case MUIM_Cleanup:
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct head_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct head_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			head_draw(INST_DATA(cl, obj), obj);
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- qbutton: a flat button
 * (Area's MUIV_InputMode_RelVerify does the clicking and the key; we draw
 * and light it under the pointer) */
struct qbutton_data {
	struct MUI_EventHandlerNode ehn;
	const char *label;
	int hot, shown;
	UWORD key;              /* raw key that presses it too, 0: none */
	ULONG id;               /* ... giving this return ID */
};

static void qbutton_draw(struct qbutton_data *d, Object *obj)
{
	struct RastPort *rp = _rp(obj);
	LONG x0 = _mleft(obj), y0 = _mtop(obj), x1 = _mright(obj), y1 = _mbottom(obj);
	int sel = xget(obj, MUIA_Selected) != 0, dis = xget(obj, MUIA_Disabled) != 0;
	LONG face = sel ? tpen(C_PRESSED) : d->hot && !dis ? tpen(C_HOVER) : tpen(C_BUTTON);
	LONG w = theme_keytextlen(rp, d->label);

	theme_fill(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, face);
	theme_rect(rp, x0, y0, x1, y1, sel ? tpen(C_ACCENT) : tpen(C_BORDER));
	theme_keytext(rp, x0 + (x1 - x0 + 1 - w) / 2, y0 + (y1 - y0 + 1 - theme_fh()) / 2, d->label,
	              dis ? tpen(C_DISABLED) : tpen(C_TEXT));
}

DISPATCHER(qbutton_dispatch)
{
	struct qbutton_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		d->label = (const char *)tagval(msg, MUIA_QButton_Label, (ULONG)"");
		d->key = (UWORD)tagval(msg, MUIA_QButton_Key, 0);
		d->id = tagval(msg, MUIA_QButton_ID, 0);
		return (ULONG)obj;
	case OM_SET:
		d = INST_DATA(cl, obj);
		if ((has_tag(msg, MUIA_Selected) || has_tag(msg, MUIA_Disabled)) && d->shown) {
			ULONG r = DoSuperMethodA(cl, obj, msg);

			MUI_Redraw(obj, MADF_DRAWOBJECT);
			return r;
		}
		break;
	case MUIM_AskMinMax: {
		LONG w = measure(((struct qbutton_data *)INST_DATA(cl, obj))->label, 1) + 24, h = theme_fh() + 8;

		DoSuperMethodA(cl, obj, msg);
		minmax(msg, w, w, MUI_MAXMAX, h, h, h);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, IDCMP_MOUSEMOVE | (d->key ? IDCMP_RAWKEY : 0), 1);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, 0, 0);
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct qbutton_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct qbutton_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			qbutton_draw(INST_DATA(cl, obj), obj);
		return 0;
	case MUIM_HandleEvent: {
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;
		int in;

		d = INST_DATA(cl, obj);
		if (!im || !d->shown)
			return 0;
		if (im->Class == IDCMP_RAWKEY && d->key && im->Code == d->key) {
			DoMethod(_app(obj), MUIM_Application_ReturnID, d->id);
			return MUI_EventHandlerRC_Eat;
		}
		if (im->Class != IDCMP_MOUSEMOVE)
			return 0;
		in = inside(obj, im);
		if (in != d->hot) {
			d->hot = in;
			MUI_Redraw(obj, MADF_DRAWOBJECT);
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- qlabel: a label, a note, a title or a text */
struct qlabel_data {
	const char *text;
	int kind, shown;
};

/* the lines of a QL_TEXT: their number, and the widest */
static int text_lines(struct RastPort *rp, const char *s, LONG *w)
{
	int n = 1;
	char line[120];

	*w = 0;
	for (;;) {
		int k = 0;
		LONG lw;

		while (*s && *s != '\n' && k < (int)sizeof(line) - 1)
			line[k++] = *s++;
		line[k] = 0;
		if ((lw = theme_textlen(rp, line)) > *w)
			*w = lw;
		if (*s != '\n')
			return n;
		s++;
		n++;
	}
}

static void qlabel_draw(struct qlabel_data *d, Object *obj)
{
	struct RastPort *rp = _rp(obj);
	LONG y = _mtop(obj) + (_mheight(obj) - theme_fh()) / 2;
	int dis = xget(obj, MUIA_Disabled) != 0;

	if (d->kind == QL_TEXT) {
		const char *s = tr(d->text);
		char line[120];

		y = _mtop(obj);
		while (*s) {
			int k = 0;

			while (*s && *s != '\n' && k < (int)sizeof(line) - 1)
				line[k++] = *s++;
			line[k] = 0;
			theme_text(rp, _mleft(obj), y, line, tpen(C_TEXT), 0);
			y += theme_fh() + 1;
			if (*s == '\n')
				s++;
		}
		return;
	}
	if (d->kind == QL_LABEL) {
		LONG w = theme_keytextlen(rp, d->text);

		theme_keytext(rp, _mright(obj) - w, y, d->text, dis ? tpen(C_DISABLED) : tpen(C_TEXT));
	} else {
		theme_text(rp, _mleft(obj), y, d->text, d->kind == QL_NOTE ? tpen(C_DIM) : tpen(C_TEXT),
		           d->kind == QL_TITLE);
	}
}

DISPATCHER(qlabel_dispatch)
{
	struct qlabel_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		d->text = (const char *)tagval(msg, MUIA_QLabel_Text, (ULONG)"");
		d->kind = (int)tagval(msg, MUIA_QLabel_Kind, QL_LABEL);
		return (ULONG)obj;
	case OM_SET:
		d = INST_DATA(cl, obj);
		if (has_tag(msg, MUIA_QLabel_Text) || has_tag(msg, MUIA_Disabled)) {
			ULONG r;

			if (has_tag(msg, MUIA_QLabel_Text))
				d->text = (const char *)tagval(msg, MUIA_QLabel_Text, (ULONG)"");
			r = DoSuperMethodA(cl, obj, msg);
			if (d->shown)
				MUI_Redraw(obj, MADF_DRAWOBJECT);
			return r;
		}
		break;
	case MUIM_AskMinMax: {
		LONG w, h = theme_fh() + 2;

		d = INST_DATA(cl, obj);
		w = measure(d->text, d->kind == QL_LABEL) + 2;
		if (d->kind == QL_TEXT) {
			struct RastPort rp;

			InitRastPort(&rp);
			h = text_lines(&rp, tr(d->text), &w) * (theme_fh() + 1);
			w += 2;
			DoSuperMethodA(cl, obj, msg);
			minmax(msg, w, w, w, h, h, h);
			return 0;
		}
		DoSuperMethodA(cl, obj, msg);
		minmax(msg, d->kind == QL_NOTE ? 0 : w, w, MUI_MAXMAX, h, h, d->kind == QL_LABEL ? MUI_MAXMAX : h);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		return TRUE;
	case MUIM_Cleanup:
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct qlabel_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct qlabel_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			qlabel_draw(INST_DATA(cl, obj), obj);
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- status: the line at the bottom */
struct status_data {
	const char *text;
	int shown;
};

DISPATCHER(status_dispatch)
{
	struct status_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		d->text = "";
		return (ULONG)obj;
	case OM_SET:
		d = INST_DATA(cl, obj);
		if (has_tag(msg, MUIA_Status_Text)) {
			d->text = (const char *)tagval(msg, MUIA_Status_Text, (ULONG)"");
			if (d->shown)
				MUI_Redraw(obj, MADF_DRAWOBJECT);
		}
		break;
	case MUIM_AskMinMax: {
		LONG h = theme_fh() + 6;

		DoSuperMethodA(cl, obj, msg);
		minmax(msg, 0, 100, MUI_MAXMAX, h, h, h);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		return TRUE;
	case MUIM_Cleanup:
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct status_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct status_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)) {
			struct RastPort *rp = _rp(obj);

			d = INST_DATA(cl, obj);
			theme_fill(rp, _mleft(obj), _mtop(obj) + 1, _mright(obj), _mbottom(obj), tpen(C_PANEL));
			theme_line(rp, _mleft(obj), _mtop(obj), _mright(obj), _mtop(obj), tpen(C_BORDER));
			theme_text(rp, _mleft(obj) + 8, _mtop(obj) + 3, d->text, tpen(C_DIM), 0);
		}
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- line: a separator */
DISPATCHER(line_dispatch)
{
	switch (msg->MethodID) {
	case MUIM_AskMinMax:
		DoSuperMethodA(cl, obj, msg);
		minmax(msg, 0, 0, MUI_MAXMAX, 1, 1, 1);
		return 0;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		return TRUE;
	case MUIM_Cleanup:
		theme_cleanup(obj);
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			theme_fill(_rp(obj), _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_BORDER));
		return 0;
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- map: the monitors and the Workbench */
struct map_data {
	struct MUI_EventHandlerNode ehn;
	int shown;
	LONG clicked;
};

/* the monitors' top left and one monitor's size, with room around them for the Workbench */
static void map_geometry(Object *obj, LONG *ox, LONG *oy, LONG *cw, LONG *ch)
{
	LONG c = layout_cols(), r = layout_rows(), w = _mwidth(obj) - 8;
	LONG h = _mheight(obj) - theme_fh() - 10;
	UWORD mw, mh;

	monitor_size(&mw, &mh);
	*cw = w * 10 / (c * 10 + 13);
	*ch = *cw * mh / mw;
	if (*ch * (r * 10 + 13) > h * 10) {
		*ch = h * 10 / (r * 10 + 13);
		*cw = *ch * mw / mh;
	}
	*ox = _mleft(obj) + (_mwidth(obj) - *cw * c) / 2;
	*oy = _mtop(obj) + 2 + (h - *ch * r) / 2;
}

/* where place n's Workbench is drawn */
static void wb_box(int n, LONG ox, LONG oy, LONG cw, LONG ch, LONG *bx, LONG *by, LONG *bw, LONG *bh)
{
	LONG c = layout_cols(), m = wb_mon[n];
	LONG mx = ox + (m % c) * cw, my = oy + (m / c) * ch;

	*bw = cw * 6 / 10;
	*bh = ch * 6 / 10;
	switch (wb_side[n]) {
	case WB_LEFT:
		*bx = ox - 4 - *bw;
		*by = my + (ch - *bh) / 2;
		break;
	case WB_RIGHT:
		*bx = ox + c * cw + 4;
		*by = my + (ch - *bh) / 2;
		break;
	case WB_ABOVE:
		*bx = mx + (cw - *bw) / 2;
		*by = oy - 4 - *bh;
		break;
	default:
		*bx = mx + (cw - *bw) / 2;
		*by = oy + layout_rows() * ch + 4;
		break;
	}
}

static void map_draw(Object *obj)
{
	struct RastPort *rp = _rp(obj);
	LONG c = layout_cols(), r = layout_rows(), ox, oy, cw, ch, i, fh = theme_fh();
	int port = atoi(prefs_value(find_setting("PORT")));
	UWORD mw, mh;
	char buf[80];

	map_geometry(obj, &ox, &oy, &cw, &ch);
	monitor_size(&mw, &mh);
	for (i = 0; i < c * r; i++) {
		LONG x = ox + (i % c) * cw, y = oy + (i / c) * ch, l;

		theme_fill(rp, x + 2, y + 2, x + cw - 3, y + ch - 3, tpen(C_ACCENT));
		theme_rect(rp, x + 2, y + 2, x + cw - 3, y + ch - 3, tpen(C_BORDER));
		snprintf(buf, sizeof(buf), "%d", (int)i + 1);
		l = theme_textlen(rp, buf);
		if (ch > 2 * fh)
			theme_text(rp, x + (cw - l) / 2, y + ch / 2 - fh, buf, tpen(C_WHITE), 1);
		snprintf(buf, sizeof(buf), "%s %d", tr("port"), port + (int)i);
		l = theme_textlen(rp, buf);
		if (l < cw - 8 && ch > 3 * fh)
			theme_text(rp, x + (cw - l) / 2, y + ch / 2 + 1, buf, tpen(C_WHITE), 0);
	}
	if (wb_side[wb_sel] != WB_NONE) {
		LONG bx, by, bw, bh, l;

		wb_box(wb_sel, ox, oy, cw, ch, &bx, &by, &bw, &bh);
		if (bw > 4 && bh > 4 && bx >= _mleft(obj) && by >= _mtop(obj)) {
			theme_fill(rp, bx, by, bx + bw - 1, by + bh - 1, tpen(C_WHITE));
			theme_rect(rp, bx, by, bx + bw - 1, by + bh - 1, tpen(C_BORDER));
			strcpy(buf, tr("Workbench"));
			if ((l = theme_textlen(rp, buf)) > bw - 4) {
				strcpy(buf, "WB");
				l = theme_textlen(rp, buf);
			}
			if (bh > fh)
				theme_text(rp, bx + (bw - l) / 2, by + (bh - fh) / 2, buf, tpen(C_TEXT), 0);
		}
	}
	snprintf(buf, sizeof(buf), "%s %d x %d%s", tr("Desktop"), (int)(c * mw), (int)(r * mh),
	         tr(c * r > 1 ? ": windows move across" : ": one Panorama screen"));
	i = theme_textlen(rp, buf);
	theme_text(rp, _mleft(obj) + (_mwidth(obj) - i) / 2, _mbottom(obj) - fh - 2, buf, tpen(C_DIM), 0);
}

/* a click around the monitors: the Workbench to the nearest place there */
static BOOL map_click(Object *obj, LONG x, LONG y)
{
	LONG ox, oy, cw, ch, bx, by, bw, bh, best = -1, d;
	int i, choice = -1;

	map_geometry(obj, &ox, &oy, &cw, &ch);
	if (x >= ox && x < ox + layout_cols() * cw && y >= oy && y < oy + layout_rows() * ch)
		return FALSE;
	for (i = 0; i < NWB; i++) {
		if (wb_side[i] == WB_NONE || !wb_fits(i))
			continue;
		wb_box(i, ox, oy, cw, ch, &bx, &by, &bw, &bh);
		d = (x - bx - bw / 2) * (x - bx - bw / 2) + (y - by - bh / 2) * (y - by - bh / 2);
		if (best < 0 || d < best) {
			best = d;
			choice = i;
		}
	}
	if (choice < 0 || choice == wb_sel)
		return FALSE;
	wb_sel = choice;
	return TRUE;
}

DISPATCHER(map_dispatch)
{
	struct map_data *d;

	switch (msg->MethodID) {
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		memset(d, 0, sizeof(*d));
		return (ULONG)obj;
	case MUIM_AskMinMax: {
		LONG fh = theme_fh();

		DoSuperMethodA(cl, obj, msg);
		minmax(msg, 200, 380, MUI_MAXMAX, (theme_compact() ? 4 : 7) * fh + 10, 10 * fh + 10, 16 * fh + 10);
		return 0;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, IDCMP_MOUSEBUTTONS, 1);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		handler(obj, cl, &d->ehn, 0, 0);
		theme_cleanup(obj);
		break;
	case MUIM_Show:
		((struct map_data *)INST_DATA(cl, obj))->shown = 1;
		break;
	case MUIM_Hide:
		((struct map_data *)INST_DATA(cl, obj))->shown = 0;
		break;
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			map_draw(obj);
		return 0;
	case MUIM_HandleEvent: {
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		d = INST_DATA(cl, obj);
		if (!im || !d->shown)
			return 0;
		if (im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN && inside(obj, im)) {
			if (map_click(obj, im->MouseX, im->MouseY)) {
				MUI_Redraw(obj, MADF_DRAWOBJECT);
				SetAttrs(obj, MUIA_Map_Clicked, ++d->clicked, TAG_DONE);
			}
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* ---------------------------------------------------------------- making and freeing the classes */
static struct MUI_CustomClass *mk(int size, APTR disp)
{
	return MUI_CreateCustomClass(NULL, (STRPTR)MUIC_Area, NULL, size, disp);
}

static BOOL classes_make(void)
{
	nav_mcc = mk(sizeof(struct nav_data), (APTR)nav_dispatch);
	head_mcc = mk(sizeof(struct head_data), (APTR)head_dispatch);
	qbutton_mcc = mk(sizeof(struct qbutton_data), (APTR)qbutton_dispatch);
	qlabel_mcc = mk(sizeof(struct qlabel_data), (APTR)qlabel_dispatch);
	status_mcc = mk(sizeof(struct status_data), (APTR)status_dispatch);
	line_mcc = mk(4, (APTR)line_dispatch);
	map_mcc = mk(sizeof(struct map_data), (APTR)map_dispatch);
	return nav_mcc && head_mcc && qbutton_mcc && qlabel_mcc && status_mcc && line_mcc && map_mcc;
}

static void classes_free(void)
{
	struct MUI_CustomClass **c[] = { &nav_mcc, &head_mcc, &qbutton_mcc, &qlabel_mcc, &status_mcc,
	                                 &line_mcc, &map_mcc };
	unsigned i;

	for (i = 0; i < sizeof(c) / sizeof(c[0]); i++)
		if (*c[i]) {
			MUI_DeleteCustomClass(*c[i]);
			*c[i] = NULL;
		}
}

/* ================================================================ building the window */

static Object *rect(void)
{
	return RectangleObject, End;
}

static Object *label(const char *text, int kind)
{
	return NewObject(qlabel_mcc->mcc_Class, NULL, MUIA_QLabel_Text, (ULONG)text, MUIA_QLabel_Kind,
	                 (ULONG)kind, TAG_DONE);
}

static Object *button(const char *text)
{
	return NewObject(qbutton_mcc->mcc_Class, NULL, MUIA_QButton_Label, (ULONG)text, MUIA_InputMode,
	                 MUIV_InputMode_RelVerify, MUIA_ControlChar, (ULONG)key_of(text), MUIA_CycleChain, 1,
	                 TAG_DONE);
}

/* a check box that keeps its size (MUIO_Checkmark stretches with its row) */
static Object *checkbox(const char *lab, BOOL on)
{
	return ImageObject, MUIA_Frame, MUIV_Frame_ImageButton, MUIA_InputMode, MUIV_InputMode_Toggle,
	       MUIA_Image_Spec, MUII_CheckMark, MUIA_Image_FreeVert, FALSE, MUIA_Background, MUII_ButtonBack,
	       MUIA_ShowSelState, FALSE, MUIA_Selected, on, MUIA_ControlChar, (ULONG)key_of(lab),
	       MUIA_CycleChain, 1, End;
}

/* a cycle gadget with translated entries (kept in out) */
static Object *cycle(const char *lab, const char *const *names, const char **out, int active)
{
	int k;

	for (k = 0; names[k]; k++)
		out[k] = tr(names[k]);
	out[k] = NULL;
	return CycleObject, MUIA_Cycle_Entries, (ULONG)out, MUIA_Cycle_Active, active,
	       MUIA_ControlChar, (ULONG)key_of(lab), MUIA_CycleChain, 1, MUIA_Font, MUIV_Font_Button, End;
}

/* a row's cell that keeps the gadget at its own width */
static Object *keep_size(Object *g)
{
	return HGroup, MUIA_Group_Spacing, 0, Child, g, Child, rect(), End;
}

/* a field's gadget from its setting */
static Object *field_gadget(struct Field *f)
{
	const struct Setting *s = find_setting(f->key);
	const char *v = s ? prefs_value(s) : "";

	if (f->kind == K_BOOL)
		f->obj = checkbox(f->label, v[0] == '1');
	else
		f->obj = StringObject, StringFrame, MUIA_String_Contents, (ULONG)v, MUIA_String_MaxLen, 80,
		         MUIA_CycleChain, 1, MUIA_ControlChar, (ULONG)key_of(f->label),
		         (ULONG)(f->kind == K_NUM ? MUIA_String_Accept : TAG_IGNORE), (ULONG)"-0123456789", End;
	f->lab = label(f->label, QL_LABEL);
	if (f->obj && f->lab) {
		char name[48];

		plain(f->label, name, sizeof(name));
		with_help(f->obj, name, s ? s->help : "");
		with_help(f->lab, name, s ? s->help : "");
	}
	return f->obj;
}

/* a page's fields of kind (text or not), pairs label + gadget to a row */
static Object *field_rows(int pg, BOOL bools, int pairs)
{
	Object *g = ColGroup(2 * pairs), MUIA_Group_HorizSpacing, 8,
	            MUIA_Group_VertSpacing, theme_compact() ? 2 : 4, End;
	unsigned i, n = 0;

	if (!g)
		return NULL;
	for (i = 0; i < NFIELDS; i++) {
		struct Field *f = &fields[i];

		if (f->page != pg || (f->kind == K_BOOL) != bools)
			continue;
		if (!field_gadget(f) || !f->lab)
			return NULL;
		DoMethod(g, OM_ADDMEMBER, (ULONG)f->lab);
		DoMethod(g, OM_ADDMEMBER, (ULONG)(bools ? keep_size(f->obj) : f->obj));
		n++;
	}
	for (; n % pairs; n++) {
		DoMethod(g, OM_ADDMEMBER, (ULONG)rect());
		DoMethod(g, OM_ADDMEMBER, (ULONG)rect());
	}
	return g;
}

/* a label and a gadget, for a two-column group */
static void row(Object *g, const char *lab, Object *gadget, const char *help)
{
	Object *l = label(lab, QL_LABEL);
	char name[48];

	plain(lab, name, sizeof(name));
	with_help(l, name, help);
	with_help(gadget, name, help);
	DoMethod(g, OM_ADDMEMBER, (ULONG)l);
	DoMethod(g, OM_ADDMEMBER, (ULONG)gadget);
}

/* the Workbench's cycle gadget, with the places this layout has */
static Object *make_wb_cycle(void)
{
	return CycleObject, MUIA_Cycle_Entries, (ULONG)wb_cy, MUIA_Cycle_Active, wb_cycle_entries(),
	       MUIA_ControlChar, 'w', MUIA_CycleChain, 1, MUIA_Font, MUIV_Font_Button, End;
}

static const char *cy_layout_t[8], *cy_size_t[8], *cy_colours_t[4], *cy_modesrc_t[3], *cy_wrap_t[5];

static Object *desktop_page(void)
{
	Object *cols = ColGroup(2), MUIA_Group_HorizSpacing, 8, MUIA_Group_VertSpacing, theme_compact() ? 2 : 4,
	               End;
	Object *bools, *btns;

	/* the picture takes the room the page has to spare */
	map = NewObject(map_mcc->mcc_Class, NULL, MUIA_VertWeight, 1000, TAG_DONE);
	cy_layout = cycle("Mo_nitors", layout_names, cy_layout_t, layout_sel);
	cy_size = cycle("Each mon_itor", size_names, cy_size_t, size_sel);
	cy_colours = cycle("C_olours", colour_names, cy_colours_t, colour_sel);
	cy_wb = make_wb_cycle();
	wb_cell = HGroup, MUIA_Group_Spacing, 0, Child, cy_wb, End;
	bools = field_rows(PG_DESKTOP, TRUE, 2);
	b_open = button("Open _desktop");
	b_close = button("C_lose desktop");
	if (!cols || !map || !cy_layout || !cy_size || !cy_colours || !cy_wb || !wb_cell || !bools || !b_open ||
	    !b_close)
		return NULL;
	with_help(map, "Monitors", "The viewer windows the desktop is shown in, and where the Workbench sits "
	          "beside them. Click next to a monitor to put the Workbench there.");
	row(cols, "Mo_nitors", cy_layout, "How many viewer windows the desktop is split into, and how they sit: "
	    "each is a part of one screen, sent to a port (and multicast group) of its own.");
	row(cols, "Each mon_itor", cy_size, "The size of each part. The desktop is that times the monitors.");
	row(cols, "C_olours", cy_colours, "The desktop's depth. 16 and 32 bit need cybergraphics.library.");
	row(cols, "_Workbench is", wb_cell, "Where the Workbench sits next to the desktop: the mouse crosses "
	    "over at that edge.");
	with_help(cy_wb, "Workbench is", "Where the Workbench sits next to the desktop: the mouse crosses over "
	          "at that edge.");
	btns = HGroup, MUIA_Group_SameSize, TRUE, MUIA_Group_Spacing, 8, Child, b_open, Child, b_close, End;
	with_help(b_open, "Open desktop", "Uses the settings, then opens the desktop (PanoramaDesk): the viewers' "
	          "mouse and keys on, and P96ScreenCx started when it is installed.");
	with_help(b_close, "Close desktop", "Closes the desktop, once no windows are left on it.");
	return VGroup, MUIA_Group_VertSpacing, theme_compact() ? 2 : 4,
	       Child, map, Child, cols, Child, bools, Child, btns, End;
}

static Object *network_page(void)
{
	Object *texts = field_rows(PG_NETWORK, FALSE, 2), *bools = field_rows(PG_NETWORK, TRUE, 2);

	if (!texts || !bools)
		return NULL;
	return VGroup, MUIA_Group_VertSpacing, theme_compact() ? 2 : 6, Child, texts, Child, bools, End;
}

/* the screen mode list and its line, as RiptidePrefs' list pages */
static Object *display_page(void)
{
	static const char *const names[3] = { "A_dd", "_Replace", "D_elete" };
	static const char *const helps[3] = {
		"Adds the size in the line above to the list.",
		"Puts the size in the line above in place of the one chosen in the list.",
		"Removes the size chosen in the list.",
	};
	Object *nums = field_rows(PG_DISPLAY, FALSE, 2), *src, *lv, *line, *btns;
	int k;

	cy_modesrc = cycle("_Screen modes", modesrc_names, cy_modesrc_t,
	                   nmodes && prefs_get(find_setting("MODES")) ? 1 : 0);
	mode_list = ListObject, MUIA_Frame, MUIV_Frame_InputList, MUIA_List_ConstructHook,
	            MUIV_List_ConstructHook_String, MUIA_List_DestructHook, MUIV_List_DestructHook_String, End;
	lv = ListviewObject, MUIA_Listview_List, (ULONG)mode_list, MUIA_CycleChain, 1, End;
	mode_line = StringObject, StringFrame, MUIA_String_MaxLen, 12, MUIA_CycleChain, 1,
	            MUIA_String_Accept, (ULONG)"0123456789xX", MUIA_ControlChar, 'z', End;
	src = ColGroup(2), MUIA_Group_HorizSpacing, 8, End;
	line = ColGroup(2), MUIA_Group_HorizSpacing, 8, End;
	btns = HGroup, MUIA_Group_SameSize, TRUE, MUIA_Group_Spacing, 8, End;
	if (!nums || !cy_modesrc || !mode_list || !lv || !mode_line || !src || !line || !btns)
		return NULL;
	row(src, "_Screen modes", cy_modesrc, "P96's settings (Picasso96Mode), or the sizes in the list, "
	    "each in every depth.");
	row(line, "Si_ze (WxH)", mode_line, "A size to add, or to put in place of the one chosen: width x "
	    "height, e.g. 1280x960.");
	with_help(lv, "Screen modes", "The sizes Panorama.card offers, each in every depth, when the list is "
	          "chosen above. Click one to change or delete it.");
	for (k = 0; k < 3; k++) {
		char name[16];

		if (!(mode_btn[k] = button(names[k])))
			return NULL;
		plain(names[k], name, sizeof(name));
		with_help(mode_btn[k], name, helps[k]);
		DoMethod(btns, OM_ADDMEMBER, (ULONG)mode_btn[k]);
	}
	return VGroup, MUIA_Group_VertSpacing, theme_compact() ? 2 : 4,
	       Child, nums, Child, src, Child, lv, Child, line, Child, btns, End;
}

static int wrap_mode(void)
{
	if (!*prefs_value(find_setting("CARD")))
		return 0;
	if (*prefs_value(find_setting("SPAN")))
		return 3;
	if (*prefs_value(find_setting("MIRROR")))
		return 2;
	return 1;
}

static Object *wrap_page(void)
{
	Object *top = ColGroup(2), MUIA_Group_HorizSpacing, 8, End;
	Object *texts = field_rows(PG_WRAP, FALSE, 1), *bools = field_rows(PG_WRAP, TRUE, 2);

	cy_wrap = cycle("Use Wrap._card", wrap_names, cy_wrap_t, wrap_mode());
	if (!top || !texts || !bools || !cy_wrap)
		return NULL;
	row(top, "Use Wrap._card", cy_wrap, "Wrap only: just wrap the card (Keep its display on works then). "
	    "Mirror: the same screen on another card. Span: one screen over 2-4 heads.");
	return VGroup, MUIA_Group_VertSpacing, theme_compact() ? 2 : 4, Child, top, Child, texts, Child, bools,
	       End;
}

static Object *status_page(void)
{
	Object *lv;

	stat_list = ListObject, ReadListFrame, MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
	            MUIA_List_DestructHook, MUIV_List_DestructHook_String, End;
	lv = ListviewObject, MUIA_Listview_List, (ULONG)stat_list, MUIA_Listview_Input, FALSE, End;
	if (lv)
		with_help(lv, "Status", "What the running cards say, and what is installed.");
	return lv;
}

/* a page: its settings, room, and the note under them */
static Object *build_page(int p)
{
	Object *body, *note_obj;
	ULONG in = theme_compact() ? 4 : 10;

	switch (p) {
	case PG_DESKTOP: body = desktop_page(); break;
	case PG_NETWORK: body = network_page(); break;
	case PG_DISPLAY: body = display_page(); break;
	case PG_WRAP: body = wrap_page(); break;
	default: body = status_page(); break;
	}
	note_obj = label(pages_text[p].note, QL_NOTE);
	if (!body || !note_obj)
		return NULL;
	if (p == PG_DISPLAY || p == PG_STATUS)
		return VGroup, MUIA_InnerLeft, 12, MUIA_InnerRight, 12, MUIA_InnerTop, in, MUIA_InnerBottom, in - 2,
		       MUIA_Group_VertSpacing, theme_compact() ? 2 : 4, Child, body, Child, note_obj, End;
	return VGroup, MUIA_InnerLeft, 12, MUIA_InnerRight, 12, MUIA_InnerTop, in, MUIA_InnerBottom, in - 2,
	       MUIA_Group_VertSpacing, theme_compact() ? 2 : 4, Child, body, Child, rect(), Child, note_obj, End;
}

/* the menus: every command, with its Amiga-key shortcut */
static struct NewMenu menus[] = {
	{ NM_TITLE, (STRPTR)"Project", 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Save", (STRPTR)"S", 0, 0, (APTR)ID_SAVE },
	{ NM_ITEM, (STRPTR)"Use", (STRPTR)"U", 0, 0, (APTR)ID_USE },
	{ NM_ITEM, (STRPTR)"Install...", (STRPTR)"I", 0, 0, (APTR)ID_INSTALL },
	{ NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"About...", (STRPTR)"?", 0, 0, (APTR)ID_ABOUT },
	{ NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, (APTR)ID_QUIT },
	{ NM_TITLE, (STRPTR)"Edit", 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Reset to Defaults", (STRPTR)"D", 0, 0, (APTR)ID_DEFAULTS },
	{ NM_ITEM, (STRPTR)"Last Saved", (STRPTR)"L", 0, 0, (APTR)ID_LASTSAVED },
	{ NM_ITEM, (STRPTR)"Restore", (STRPTR)"R", 0, 0, (APTR)ID_RESTORE },
	{ NM_TITLE, (STRPTR)"Desktop", 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Open Desktop", (STRPTR)"O", 0, 0, (APTR)ID_OPEN },
	{ NM_ITEM, (STRPTR)"Close Desktop", (STRPTR)"K", 0, 0, (APTR)ID_CLOSE },
	{ NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Refresh Status", (STRPTR)"F", 0, 0, (APTR)ID_REFRESH },
	{ NM_TITLE, (STRPTR)"Settings", 0, 0, 0, 0 },
	{ NM_ITEM, (STRPTR)"Dark Theme", (STRPTR)"T", CHECKIT | MENUTOGGLE, 0, (APTR)ID_DARK },
	{ NM_ITEM, (STRPTR)"Help Bubbles", (STRPTR)"H", CHECKIT | MENUTOGGLE, 0, (APTR)ID_TIPS },
	{ NM_END, 0, 0, 0, 0, 0 },
};

static Object *build_menus(void)
{
	static struct NewMenu m[sizeof(menus) / sizeof(menus[0])];
	unsigned i;

	for (i = 0; i < sizeof(menus) / sizeof(menus[0]); i++) {
		m[i] = menus[i];
		if (m[i].nm_Label && m[i].nm_Label != NM_BARLABEL)
			m[i].nm_Label = (STRPTR)tr((const char *)m[i].nm_Label);
		if ((ULONG)m[i].nm_UserData == ID_DARK)
			m[i].nm_Flags = (UWORD)(CHECKIT | MENUTOGGLE | (theme_dark() ? CHECKED : 0));
		if ((ULONG)m[i].nm_UserData == ID_TIPS)
			m[i].nm_Flags = (UWORD)(CHECKIT | MENUTOGGLE | (theme_tips() ? CHECKED : 0));
	}
	return MUI_MakeObject(MUIO_MenustripNM, (ULONG)m, 0);
}

#define RET(o, attr, val, id) DoMethod(o, MUIM_Notify, attr, val, (ULONG)app, 2, \
                                       MUIM_Application_ReturnID, id)

static BOOL build_window(const char *pubscreen)
{
	Object *right, *top;
	int i;

	nav = NewObject(nav_mcc->mcc_Class, NULL, MUIA_Nav_Active, (ULONG)page, MUIA_ShortHelp, (ULONG)" ",
	                TAG_DONE);
	head = NewObject(head_mcc->mcc_Class, NULL, MUIA_Head_Page, (ULONG)page, TAG_DONE);
	pages = VGroup, MUIA_Group_PageMode, TRUE, End;
	status = NewObject(status_mcc->mcc_Class, NULL, TAG_DONE);
	bar = HGroup, MUIA_Group_SameSize, TRUE, MUIA_Group_Spacing, 8, MUIA_InnerLeft, 8, MUIA_InnerRight, 8,
	      MUIA_InnerTop, theme_compact() ? 3 : 6, MUIA_InnerBottom, theme_compact() ? 3 : 6,
	      MUIA_Background, (ULONG)theme_spec(C_PANEL), End;
	if (!nav || !head || !pages || !status || !bar) {
		mp_trace("mui: objects: nav %lx head %lx pages %lx status %lx bar %lx", (ULONG)nav, (ULONG)head,
		         (ULONG)pages, (ULONG)status, (ULONG)bar);
		return FALSE;
	}
	for (i = 0; i < NPAGES; i++) {
		Object *pg = build_page(i);

		if (!pg) {
			mp_trace("mui: page %d not made", i);
			return FALSE;
		}
		DoMethod(pages, OM_ADDMEMBER, (ULONG)pg);
	}
	for (i = 0; i < 4; i++) {
		char name[16];

		if (!(btn[i] = button(btn_label[i])))
			return FALSE;
		plain(btn_label[i], name, sizeof(name));
		with_help(btn[i], name, btn_help[i]);
		DoMethod(bar, OM_ADDMEMBER, (ULONG)btn[i]);
	}
	set(pages, MUIA_Group_ActivePage, (ULONG)page);
	right = VGroup, MUIA_Group_Spacing, 0, Child, head, Child, pages, End;
	top = right ? HGroup, MUIA_Group_Spacing, 0, Child, nav, Child, right, End : NULL;
	root = top ? VGroup, MUIA_Group_Spacing, 0, MUIA_InnerLeft, 0, MUIA_InnerRight, 0, MUIA_InnerTop, 0,
	       MUIA_InnerBottom, 0, MUIA_Background, (ULONG)theme_spec(C_WINDOW), Child, top,
	       Child, NewObject(line_mcc->mcc_Class, NULL, TAG_DONE), Child, bar, Child, status, End : NULL;
	strip = build_menus();
	mp_trace("mui: root %s, menus %s", root ? "made" : "NOT made", strip ? "made" : "NOT made");
	if (!root || !strip)
		return FALSE;
	win = WindowObject, MUIA_Window_Title, (ULONG)tr(TITLE), MUIA_Window_ID, MAKE_ID('P', 'N', 'P', 'R'),
	      MUIA_Window_Menustrip, (ULONG)strip, MUIA_Window_RootObject, (ULONG)root,
	      (ULONG)(pubscreen ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pubscreen, End;
	if (!win)
		return FALSE;
	DoMethod(app, OM_ADDMEMBER, (ULONG)win);
	RET(win, MUIA_Window_CloseRequest, TRUE, ID_CANCEL);
	for (i = 0; i < 4; i++)
		RET(btn[i], MUIA_Pressed, FALSE, btn_id[i]);
	RET(b_open, MUIA_Pressed, FALSE, ID_OPEN);
	RET(b_close, MUIA_Pressed, FALSE, ID_CLOSE);
	RET(mode_btn[0], MUIA_Pressed, FALSE, ID_MODEADD);
	RET(mode_btn[1], MUIA_Pressed, FALSE, ID_MODEREPL);
	RET(mode_btn[2], MUIA_Pressed, FALSE, ID_MODEDEL);
	RET(mode_list, MUIA_List_Active, MUIV_EveryTime, ID_MODESEL);
	RET(cy_layout, MUIA_Cycle_Active, MUIV_EveryTime, ID_LAYOUT);
	RET(cy_size, MUIA_Cycle_Active, MUIV_EveryTime, ID_SIZE);
	RET(cy_wb, MUIA_Cycle_Active, MUIV_EveryTime, ID_WB);
	RET(map, MUIA_Map_Clicked, MUIV_EveryTime, ID_MAP);
	DoMethod(nav, MUIM_Notify, MUIA_Nav_Active, MUIV_EveryTime, (ULONG)pages, 3, MUIM_Set,
	         MUIA_Group_ActivePage, MUIV_TriggerValue);
	DoMethod(nav, MUIM_Notify, MUIA_Nav_Active, MUIV_EveryTime, (ULONG)head, 3, MUIM_Set, MUIA_Head_Page,
	         MUIV_TriggerValue);
	RET(nav, MUIA_Nav_Active, MUIV_EveryTime, ID_PAGE);
	return TRUE;
}

/* ================================================================ messages */

/* the main window again after something covered it (MUI does not always
 * repair custom-drawn areas uncovered by a closing window) */
static void redraw_window(void)
{
	if (root && win && xget(win, MUIA_Window_Open)) {
		DoMethod(root, MUIM_Group_InitChange);
		DoMethod(root, MUIM_Group_ExitChange);
	}
}

/*
 * A message in a small window of the theme, as RiptidePrefs': the text and
 * buttons from "_OK" or "_Reboot now|_Later"; Return presses the first
 * button, Esc (and the close gadget) the last. Returns as EasyRequest()
 * does: 1, 2 ... from the left, 0 for the last button.
 */
static LONG request(const char *text, const char *buttons)
{
	static char dlgspec[32];
	char labels[4][32];
	const char *b = tr(buttons);
	Object *dlg, *dlgroot, *txt, *brow, *bt;
	ULONG sigs = 0;
	LONG r = 0;
	int n = 0, i;

	if (!app || !win)
		return 0;
	/* "_OK|_Later": each button's first letter is its key */
	while (*b && n < 4) {
		int k = 0;

		labels[n][k++] = '_';
		while (*b && *b != '|' && k < (int)sizeof(labels[n]) - 1) {
			if (*b != '_')
				labels[n][k++] = *b;
			b++;
		}
		labels[n][k] = 0;
		n++;
		if (*b == '|')
			b++;
	}
	strcpy(dlgspec, theme_spec(C_WINDOW));
	txt = label(tr(text), QL_TEXT);
	brow = HGroup, MUIA_Group_Spacing, 8, Child, rect(), End;
	if (!txt || !brow || n == 0)
		return 0;
	for (i = 0; i < n; i++) {
		/* Return presses the first button, Esc the last (one button: both) */
		ULONG key = i == n - 1 && n > 1 ? RAW_ESC : i == 0 ? RAW_RETURN : 0;

		bt = NewObject(qbutton_mcc->mcc_Class, NULL, MUIA_QButton_Label, (ULONG)labels[i], MUIA_InputMode,
		               MUIV_InputMode_RelVerify, MUIA_ControlChar, (ULONG)(labels[i][1] | 0x20),
		               MUIA_CycleChain, 1, MUIA_QButton_Key, key, MUIA_QButton_ID, ID_DLGBTN + i,
		               MUIA_HorizWeight, 0, TAG_DONE);
		if (!bt)
			return 0;
		DoMethod(brow, OM_ADDMEMBER, (ULONG)bt);
		RET(bt, MUIA_Pressed, FALSE, ID_DLGBTN + i);
	}
	dlgroot = VGroup, MUIA_Group_Spacing, 10, MUIA_InnerLeft, 14, MUIA_InnerRight, 14, MUIA_InnerTop, 12,
	          MUIA_InnerBottom, 10, MUIA_Background, (ULONG)dlgspec, Child, txt, Child, brow, End;
	dlg = dlgroot ? WindowObject, MUIA_Window_Title, (ULONG)tr(TITLE), MUIA_Window_RootObject, (ULONG)dlgroot,
	      MUIA_Window_RefWindow, (ULONG)win, MUIA_Window_LeftEdge, MUIV_Window_LeftEdge_Centered,
	      MUIA_Window_TopEdge, MUIV_Window_TopEdge_Centered, MUIA_Window_Activate, TRUE,
	      (ULONG)(pubname ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pubname, End : NULL;
	if (!dlg)
		return 0;
	DoMethod(app, OM_ADDMEMBER, (ULONG)dlg);
	RET(dlg, MUIA_Window_CloseRequest, TRUE, ID_DLGBTN + n - 1);
	set(win, MUIA_Window_Sleep, TRUE);
	set(dlg, MUIA_Window_Open, TRUE);
	mp_trace("message: %s", text);
	for (;;) {
		LONG id = (LONG)DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);

		if (id >= ID_DLGBTN && id < ID_DLGBTN + n) {
			r = id == ID_DLGBTN + n - 1 ? 0 : id - ID_DLGBTN + 1;
			break;
		}
		if (!xget(dlg, MUIA_Window_Open))
			break;
		if (sigs)
			sigs = Wait(sigs);
	}
	mp_trace("message answered %ld", r);
	set(dlg, MUIA_Window_Open, FALSE);
	DoMethod(app, OM_REMMEMBER, (ULONG)dlg);
	MUI_DisposeObject(dlg);
	set(win, MUIA_Window_Sleep, FALSE);
	redraw_window();
	return r;
}

static void tell(const char *text)
{
	request(text, "_OK");
}

/* TRUE for the first button */
static BOOL ask(const char *text, const char *buttons)
{
	return request(text, buttons) == 1;
}

/* ================================================================ settings in and out */

static void from_settings(void)
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
	wb_places(side, mon);
	{
		const char *v = prefs_get(find_setting("MODES"));

		modes_from(v && mp_stricmp(v, "none") ? v : "");
	}
}

static BOOL set_or_say(const struct Setting *s, const char *v)
{
	const char *why;
	char buf[160];

	if (!strcmp(v, prefs_value(s)))
		return TRUE;
	if ((why = prefs_set(s, v)) == NULL)
		return TRUE;
	snprintf(buf, sizeof(buf), "%s %s", s->key, tr(why));
	tell(buf);
	return FALSE;
}

/* every gadget into the settings; FALSE if a value won't do */
static BOOL collect(void)
{
	char buf[256];
	unsigned i;
	int wrap, src;

	for (i = 0; i < NFIELDS; i++) {
		struct Field *f = &fields[i];
		const struct Setting *s = find_setting(f->key);

		if (!s || !f->obj)
			continue;
		if (f->kind == K_BOOL) {
			snprintf(buf, sizeof(buf), "%d", xget(f->obj, MUIA_Selected) ? 1 : 0);
		} else {
			strncpy(buf, (char *)xget(f->obj, MUIA_String_Contents), sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
		}
		if (!set_or_say(s, buf)) {
			set(nav, MUIA_Nav_Active, f->page);
			set(win, MUIA_Window_ActiveObject, (ULONG)f->obj);
			return FALSE;
		}
	}
	layout_sel = xget(cy_layout, MUIA_Cycle_Active);
	size_sel = xget(cy_size, MUIA_Cycle_Active);
	colour_sel = xget(cy_colours, MUIA_Cycle_Active);
	prefs_set(find_setting("SPLIT"), layout_sel ? layout_split[layout_sel] : "");
	prefs_set(find_setting("MONITOR"), size_value[size_sel]);
	prefs_set(find_setting("COLOURS"), colour_value[colour_sel]);
	wb_name(buf, sizeof(buf), wb_side[wb_sel], wb_mon[wb_sel]);
	prefs_set(find_setting("WORKBENCH"), buf);

	wrap = xget(cy_wrap, MUIA_Cycle_Active);
	if (layout_sel && wrap >= 2) {
		/* a desktop over several windows is one plain Panorama board */
		wrap = 0;
		note("Wrap.card's mirror or span off: the desktop is one Panorama board, split in parts.");
	}
	if (wrap == 0) {
		prefs_set(find_setting("CARD"), "");
		prefs_set(find_setting("MIRROR"), "");
		prefs_set(find_setting("SPAN"), "");
	} else {
		if (!*prefs_value(find_setting("CARD"))) {
			set(nav, MUIA_Nav_Active, PG_WRAP);
			tell("Card to wrap: which card should Wrap.card wrap?\n(e.g. uaegfx.card, or Panorama.card)");
			return FALSE;
		}
		if (wrap != 2)
			prefs_set(find_setting("MIRROR"), "");
		if (wrap != 3)
			prefs_set(find_setting("SPAN"), "");
		if (wrap == 2 && !*prefs_value(find_setting("MIRROR"))) {
			set(nav, MUIA_Nav_Active, PG_WRAP);
			tell("Mirror on: which card should show the same screen?");
			return FALSE;
		}
		if (wrap == 3 && !*prefs_value(find_setting("SPAN"))) {
			set(nav, MUIA_Nav_Active, PG_WRAP);
			tell("Span: how many heads, columns x rows (e.g. 2x2)?");
			return FALSE;
		}
	}
	src = xget(cy_modesrc, MUIA_Cycle_Active);
	if (src == 0) {
		prefs_set(find_setting("MODES"), "");
	} else {
		modes_to(buf, sizeof(buf));
		if (!nmodes) {
			set(nav, MUIA_Nav_Active, PG_DISPLAY);
			tell("Screen modes: the list is empty.\nAdd sizes, or choose P96's settings.");
			return FALSE;
		}
		if (!set_or_say(find_setting("MODES"), buf))
			return FALSE;
	}
	return TRUE;
}

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

/* the Status page's lines from the running cards */
static void status_show(void)
{
	int n = status_lines(stat_text, NSTATUS), i;

	if (!stat_list)
		return;
	set(stat_list, MUIA_List_Quiet, TRUE);
	DoMethod(stat_list, MUIM_List_Clear);
	for (i = 0; i < n; i++)
		DoMethod(stat_list, MUIM_List_InsertSingle, (ULONG)stat_text[i], MUIV_List_Insert_Bottom);
	set(stat_list, MUIA_List_Quiet, FALSE);
}

static void modes_show(void)
{
	int i;

	set(mode_list, MUIA_List_Quiet, TRUE);
	DoMethod(mode_list, MUIM_List_Clear);
	for (i = 0; i < nmodes; i++)
		DoMethod(mode_list, MUIM_List_InsertSingle, (ULONG)mode_text[i], MUIV_List_Insert_Bottom);
	set(mode_list, MUIA_List_Quiet, FALSE);
}

/* Add, Replace or Delete on the mode list; sel: the list's choice */
static void mode_cmd(ULONG id)
{
	LONG sel = (LONG)xget(mode_list, MUIA_List_Active), i;
	char *s = (char *)xget(mode_line, MUIA_String_Contents);

	if (id == ID_MODESEL) {
		if (sel >= 0 && sel < nmodes)
			nnset(mode_line, MUIA_String_Contents, (ULONG)mode_text[sel]);
		return;
	}
	if (id != ID_MODEDEL) {
		if (!s || !*s)
			return;
		if (prefs_check(find_setting("MODES"), s) || !mp_stricmp(s, "none")) {
			tell("A size is width x height, e.g. 1280x960");
			set(win, MUIA_Window_ActiveObject, (ULONG)mode_line);
			return;
		}
	}
	if (id == ID_MODEADD) {
		if (nmodes >= MAXMODES)
			return;
		sel = nmodes++;
	} else if (sel < 0 || sel >= nmodes) {
		return;
	}
	if (id == ID_MODEDEL) {
		for (i = sel; i < nmodes - 1; i++)
			strcpy(mode_text[i], mode_text[i + 1]);
		nmodes--;
		sel = -1;
		nnset(mode_line, MUIA_String_Contents, (ULONG)"");
	} else {
		strncpy(mode_text[sel], s, 11);
		mode_text[sel][11] = 0;
	}
	modes_show();
	nnset(mode_list, MUIA_List_Active, sel >= 0 ? (ULONG)sel : (ULONG)MUIV_List_Active_Off);
	set(cy_modesrc, MUIA_Cycle_Active, 1);
}

/* Save or Use; TRUE: done, the window may close */
static BOOL save(BOOL envarc)
{
	if (!collect())
		return FALSE;
	report[0] = 0;
	prefs_fix(note);
	if (!prefs_write(envarc)) {
		tell("Could not write the settings to ENV: or ENVARC:");
		return FALSE;
	}
	place_monitors(note);
	if (report[0]) {
		/* changes that the card only takes at boot: offer it */
		if (strstr(report, "reboot") && envarc) {
			if (ask(report, "_Reboot now|_Later"))
				mp_reboot();
		} else {
			tell(report);
		}
	}
	return TRUE;
}

static void do_install(void)
{
	BOOL ok;

	if (!collect())
		return;
	report[0] = 0;
	prefs_fix(note);
	install(note, TRUE, NULL, NULL);
	if (!ask(report, "_Install|_Cancel"))
		return;
	report[0] = 0;
	set(app, MUIA_Application_Sleep, TRUE);
	ok = install(note, FALSE, NULL, NULL);
	if (!prefs_write(TRUE)) {
		note("Could not write the settings to ENV: or ENVARC:");
		ok = FALSE;
	} else {
		note("Settings saved.");
	}
	set(app, MUIA_Application_Sleep, FALSE);
	if (ok) {
		note("\nReboot now?");
		if (ask(report, "_Reboot now|_Later"))
			mp_reboot();
	} else {
		tell(report);
	}
	status_show();
}

static void open_desktop(void)
{
	unsigned i;

	if (!collect())
		return;
	report[0] = 0;
	desktop_ready(note);
	/* the check boxes as desktop_ready() left them (INPUT=) */
	for (i = 0; i < NFIELDS; i++)
		if (fields[i].kind == K_BOOL && fields[i].obj)
			nnset(fields[i].obj, MUIA_Selected, prefs_value(find_setting(fields[i].key))[0] == '1');
	prefs_fix(note);
	if (!prefs_write(FALSE)) {
		tell("Could not write the settings to ENV:");
		return;
	}
	if (report[0])
		tell(report);
	run_desktop("C:PanoramaDesk", TRUE);
}

/* ================================================================ commands */

/* the Workbench's cycle gadget again, with the places a new layout has */
static void wb_rebuild(void)
{
	Object *old = cy_wb, *n = make_wb_cycle();

	if (!n)
		return;
	DoMethod(wb_cell, MUIM_Group_InitChange);
	DoMethod(wb_cell, OM_REMMEMBER, (ULONG)old);
	DoMethod(wb_cell, OM_ADDMEMBER, (ULONG)n);
	DoMethod(wb_cell, MUIM_Group_ExitChange);
	MUI_DisposeObject(old);
	cy_wb = n;
	RET(cy_wb, MUIA_Cycle_Active, MUIV_EveryTime, ID_WB);
}

static void map_redraw(void)
{
	if (map && xget(nav, MUIA_Nav_Active) == PG_DESKTOP)
		MUI_Redraw(map, MADF_DRAWOBJECT);
}

/* every gadget from the settings (after Reset to Defaults, Last Saved, Restore) */
static void refresh_all(void)
{
	unsigned i;

	from_settings();
	for (i = 0; i < NFIELDS; i++) {
		struct Field *f = &fields[i];
		const struct Setting *s = find_setting(f->key);
		const char *v = s ? prefs_value(s) : "";

		if (!f->obj)
			continue;
		if (f->kind == K_BOOL)
			nnset(f->obj, MUIA_Selected, v[0] == '1');
		else
			nnset(f->obj, MUIA_String_Contents, (ULONG)v);
	}
	nnset(cy_layout, MUIA_Cycle_Active, layout_sel);
	nnset(cy_size, MUIA_Cycle_Active, size_sel);
	nnset(cy_colours, MUIA_Cycle_Active, colour_sel);
	nnset(cy_modesrc, MUIA_Cycle_Active, nmodes && prefs_get(find_setting("MODES")) ? 1 : 0);
	nnset(cy_wrap, MUIA_Cycle_Active, wrap_mode());
	wb_rebuild();
	modes_show();
	map_redraw();
}

/* is PanoramaDesk running (its port there)? */
static BOOL desk_running(void)
{
	struct MsgPort *p;

	Forbid();
	p = FindPort((STRPTR)"PanoramaDesk");
	Permit();
	return p != NULL;
}

/* is Panorama.card loaded? */
static BOOL card_running(void)
{
	struct Node *n;

	Forbid();
	n = FindName(&SysBase->LibList, (STRPTR)"Panorama.card");
	Permit();
	return n != NULL;
}

static void show_status(void)
{
	snprintf(status_text, sizeof(status_text), "%s%s",
	         tr(card_running() ? "Panorama.card is running." : "Panorama.card is not running."),
	         desk_running() ? tr("  The desktop is open.") : "");
	set(status, MUIA_Status_Text, (ULONG)status_text);
}

/*
 * Whether a command can run now: one answer for the menus, the buttons and
 * command() itself (CMD= and the Amiga-key shortcuts don't go through the
 * menus), so they never disagree.
 */
static BOOL command_enabled(ULONG id)
{
	switch (id) {
	case ID_CLOSE:
		return desk_running();
	case ID_REFRESH:
		return page == PG_STATUS;
	}
	return TRUE;
}

/* the menu items and buttons that depend on the state */
static void update_menus(void)
{
	static const ULONG ids[] = { ID_CLOSE, ID_REFRESH };
	unsigned i;

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		Object *item = (Object *)DoMethod(strip, MUIM_FindUData, ids[i]);
		BOOL on = command_enabled(ids[i]);

		if (item && (xget(item, MUIA_Menuitem_Enabled) != 0) != on)
			set(item, MUIA_Menuitem_Enabled, on);
	}
	if (b_close && (xget(b_close, MUIA_Disabled) != 0) == command_enabled(ID_CLOSE))
		set(b_close, MUIA_Disabled, !command_enabled(ID_CLOSE));
}

static void menu_check(ULONG id, int on)
{
	Object *item = (Object *)DoMethod(strip, MUIM_FindUData, id);

	if (item)
		nnset(item, MUIA_Menuitem_Checked, (ULONG)on);
}

/* the editor's own preferences: ENV:PanoramaPrefs.prefs (and ENVARC:), "dark=1", "bubbles=0" */
static int eprefs_dark = -1, eprefs_tips = -1;

static void eprefs_load(void)
{
	static const char *const where[] = { "ENV:PanoramaPrefs.prefs", "ENVARC:PanoramaPrefs.prefs" };
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin = me->pr_WindowPtr;
	char line[40];
	BPTR fh = 0;
	int i;

	me->pr_WindowPtr = (APTR)-1;
	for (i = 0; i < 2 && !fh; i++)
		fh = Open((STRPTR)where[i], MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return;
	while (FGets(fh, (STRPTR)line, sizeof(line))) {
		if (!strncmp(line, "dark=", 5))
			eprefs_dark = line[5] == '1';
		else if (!strncmp(line, "bubbles=", 8))
			eprefs_tips = line[8] == '1';
	}
	Close(fh);
}

static void eprefs_save(void)
{
	static const char *const where[] = { "ENV:PanoramaPrefs.prefs", "ENVARC:PanoramaPrefs.prefs" };
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin = me->pr_WindowPtr;
	char text[40];
	int i;

	snprintf(text, sizeof(text), "dark=%d\nbubbles=%d\n", theme_dark(), theme_tips());
	me->pr_WindowPtr = (APTR)-1;
	for (i = 0; i < 2; i++) {
		BPTR fh = Open((STRPTR)where[i], MODE_NEWFILE);

		if (fh) {
			FPuts(fh, (STRPTR)text);
			Close(fh);
		}
	}
	me->pr_WindowPtr = oldwin;
}

/* the window again after the theme changed: pens are taken anew */
static void reopen(void)
{
	set(win, MUIA_Window_Open, FALSE);
	set(root, MUIA_Background, (ULONG)theme_spec(C_WINDOW));
	set(bar, MUIA_Background, (ULONG)theme_spec(C_PANEL));
	set(win, MUIA_Window_Open, TRUE);
}

static void set_tips(int on)
{
	int i;

	theme_set_tips(on);
	for (i = 0; i < nhelp; i++)
		set(help_obj[i], MUIA_ShortHelp, on ? (ULONG)help_text[i] : 0);
}

static void about(void)
{
	char text[400];

	snprintf(text, sizeof(text), "%s %s (%s)\n\n%s", "PanoramaPrefs", MP_VERSION, MP_DATE,
	         tr("Preferences for Panorama.card, Wrap.card and the Panorama desktop:\n"
	            "one Amiga screen in viewer windows on other computers.\n"
	            "This window uses MUI; without MUI, PanoramaPrefs\n"
	            "opens a GadTools window with the same settings."));
	tell(text);
}

/* run a command; TRUE when the editor should close */
static BOOL command(ULONG id)
{
	if (!command_enabled(id))
		return FALSE;
	if (id >= ID_MODESEL && id <= ID_MODEDEL) {
		mode_cmd(id);
		return FALSE;
	}
	switch (id) {
	case MUIV_Application_ReturnID_Quit:
	case ID_CANCEL:
	case ID_QUIT:
		return TRUE;
	case ID_PAGE:
		page = (int)xget(nav, MUIA_Nav_Active);
		if (page == PG_STATUS)
			status_show();
		return FALSE;
	case ID_SAVE:
	case ID_USE:
		return save(id == ID_SAVE);
	case ID_INSTALL:
		do_install();
		return FALSE;
	case ID_ABOUT:
		about();
		return FALSE;
	case ID_DEFAULTS:
		prefs_defaults();
		refresh_all();
		mp_trace("defaults: SPLIT %s, gadgets set", prefs_value(find_setting("SPLIT")));
		return FALSE;
	case ID_LASTSAVED:
		prefs_load_from("ENVARC:");
		refresh_all();
		return FALSE;
	case ID_RESTORE:
		prefs_load();
		refresh_all();
		return FALSE;
	case ID_OPEN:
		open_desktop();
		return FALSE;
	case ID_CLOSE:
		run_desktop("C:PanoramaDesk CLOSE", FALSE);
		return FALSE;
	case ID_REFRESH:
		status_show();
		return FALSE;
	case ID_DARK:
		theme_set_dark(!theme_dark());
		eprefs_save();
		reopen();
		menu_check(ID_DARK, theme_dark());
		return FALSE;
	case ID_TIPS:
		set_tips(!theme_tips());
		eprefs_save();
		menu_check(ID_TIPS, theme_tips());
		return FALSE;
	case ID_LAYOUT: {
		int side = wb_side[wb_sel], mon = wb_mon[wb_sel];

		layout_sel = xget(cy_layout, MUIA_Cycle_Active);
		wb_places(side, mon);
		wb_rebuild();
		map_redraw();
		return FALSE;
	}
	case ID_SIZE:
		size_sel = xget(cy_size, MUIA_Cycle_Active);
		map_redraw();
		return FALSE;
	case ID_WB: {
		LONG a = xget(cy_wb, MUIA_Cycle_Active);

		if (a >= 0 && a < nwb_valid)
			wb_sel = wb_valid_idx[a];
		map_redraw();
		return FALSE;
	}
	case ID_MAP: {
		/* the picture moved the Workbench: the cycle gadget follows */
		int k;

		for (k = 0; k < nwb_valid; k++)
			if (wb_valid_idx[k] == wb_sel)
				nnset(cy_wb, MUIA_Cycle_Active, k);
		return FALSE;
	}
	}
	return FALSE;
}

/* ================================================================ the loop */

static void free_all(void)
{
	int i;

	if (app)
		MUI_DisposeObject(app);
	app = win = strip = root = nav = head = pages = status = bar = NULL;
	map = cy_wb = wb_cell = mode_list = stat_list = b_open = b_close = NULL;
	classes_free();
	for (i = 0; i < nhelp; i++)
		FreeVec(help_text[i]);
	nhelp = 0;
	for (i = 0; i < (int)NFIELDS; i++)
		fields[i].obj = fields[i].lab = NULL;
}

static int window_loop(int start, int timeout, const char *pubscreen, ULONG cmd, int dark);

/* (bigstack.s) fn(arg) on the stack ss describes */
long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg);

struct GuiArgs {
	int start, timeout, dark;
	const char *pubscreen;
	ULONG cmd;
};

static long gui_on_stack(void *a)
{
	struct GuiArgs *g = a;

	return window_loop(g->start, g->timeout, g->pubscreen, g->cmd, g->dark);
}

/*
 * PanoramaPrefs' window in MUI; -1 if MUI (muimaster.library 19) isn't
 * there, for the GadTools one instead. MUI wants a big stack: one of 64 KB
 * is swapped in when the Shell gave less than 32 KB. cmd: a command (ids.h)
 * run once the window is open (CMD=, for tests), 0 none; dark: THEME=, -1
 * as last chosen.
 */
int mui_gui(int start, int timeout, const char *pubscreen, ULONG cmd, int dark)
{
	struct Task *me = FindTask(NULL);
	ULONG have = (ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower;
	struct GuiArgs g;
	struct StackSwapStruct ss;
	APTR stack;
	long rc;

	g.start = start;
	g.timeout = timeout;
	g.pubscreen = pubscreen;
	g.cmd = cmd;
	g.dark = dark;
	mp_trace("mui: stack %lu bytes", have);
	if (have >= 32768 || !(stack = AllocVec(65536, MEMF_ANY)))
		return window_loop(start, timeout, pubscreen, cmd, dark);
	ss.stk_Lower = stack;
	ss.stk_Upper = (ULONG)stack + 65536;
	ss.stk_Pointer = (APTR)ss.stk_Upper;
	rc = run_on_stack(&ss, gui_on_stack, &g);
	FreeVec(stack);
	return rc;
}

static int window_loop(int start, int timeout, const char *pubscreen, ULONG cmd, int dark)
{
	struct MsgPort *tport = NULL;
	struct timerequest *treq = NULL;
	ULONG sigs = 0, ticks = 0;
	BOOL done = FALSE, timer = FALSE;
	int rc = 0;

	MUIMasterBase = OpenLibrary(MUIMASTER_NAME, 19);
	mp_trace("mui: muimaster.library %ld.%ld", MUIMasterBase ? (LONG)MUIMasterBase->lib_Version : 0L,
	         MUIMasterBase ? (LONG)MUIMasterBase->lib_Revision : 0L);
	if (!MUIMasterBase)
		return -1;
	if (!IntuitionBase)
		IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	if (!GfxBase)
		GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	if (!UtilityBase)
		UtilityBase = OpenLibrary("utility.library", 37);
	lang_init();
	{
		struct Screen *s = LockPubScreen((STRPTR)pubscreen);

		if (s) {
			mp_trace("mui: screen %ldx%ld, font %ld", (LONG)s->Width, (LONG)s->Height,
			         (LONG)s->Font->ta_YSize);
			theme_set_compact(s->Height < 400);
			UnlockPubScreen(NULL, s);
		}
	}
	eprefs_load();
	theme_set_dark(dark >= 0 ? dark : eprefs_dark > 0);
	theme_set_tips(eprefs_tips != 0);
	page = start >= 0 && start < NPAGES ? start : 0;
	pubname = pubscreen;
	from_settings();
	if (!classes_make()) {
		printf("%s\n", tr("PanoramaPrefs: MUI couldn't make its classes"));
		rc = -1;
		goto out;
	}
	app = ApplicationObject,
		MUIA_Application_Title, (ULONG)"PanoramaPrefs",
		MUIA_Application_Version, (ULONG)"$VER: PanoramaPrefs " MP_VERSION " (" MP_DATE ")",
		MUIA_Application_Description, (ULONG)tr("Preferences for Panorama"),
		MUIA_Application_Base, (ULONG)"PANORAMAPREFS",
		MUIA_Application_UseCommodities, FALSE,
		MUIA_Application_UseRexx, FALSE,
	End;
	if (!app || !build_window(pubscreen)) {
		printf("%s\n", tr("PanoramaPrefs: the window could not be made"));
		rc = RETURN_FAIL;
		goto out;
	}
	if (!theme_tips())
		set_tips(0);
	modes_show();
	status_show();
	set(win, MUIA_Window_Open, TRUE);
	mp_trace("mui: window %s (MUI_Error %ld), page %ld, %s theme",
	         xget(win, MUIA_Window_Open) ? "open" : "NOT open", MUI_Error(), (LONG)page,
	         theme_dark() ? "dark" : "light");
	if (!xget(win, MUIA_Window_Open)) {
		printf("%s\n", tr("PanoramaPrefs: the window could not be opened"));
		rc = RETURN_FAIL;
		goto out;
	}
	show_status();
	update_menus();

	/* a tick every half second: TIMEOUT=, the status line, menus and Status page kept up to date */
	if ((tport = CreateMsgPort()) != NULL &&
	    (treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq))) != NULL &&
	    OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0) == 0) {
		timer = TRUE;
		treq->tr_node.io_Command = TR_ADDREQUEST;
		treq->tr_time.tv_secs = 0;
		treq->tr_time.tv_micro = 500000;
		SendIO((struct IORequest *)treq);
	}
	if (cmd) {
		mp_trace("CMD=%lu", cmd);
		done = command(cmd);
		update_menus();
	}
	while (!done) {
		ULONG id = DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);

		if (id) {
			mp_trace("command %lu", id);
			done = command(id);
			update_menus();
		}
		if (done || !sigs)
			continue;
		sigs = Wait(sigs | SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_F | (timer ? 1UL << tport->mp_SigBit : 0));
		if (sigs & SIGBREAKF_CTRL_C)
			done = TRUE;
		if (sigs & SIGBREAKF_CTRL_F) {
			/* PanoramaPrefs started again: this window to the front instead */
			struct Screen *s = (struct Screen *)xget(win, MUIA_Window_Screen);

			mp_trace("started again: to the front");
			if (s)
				ScreenToFront(s);
			DoMethod(win, MUIM_Window_ToFront);
			set(win, MUIA_Window_Activate, TRUE);
		}
		if (timer && CheckIO((struct IORequest *)treq)) {
			WaitIO((struct IORequest *)treq);
			ticks++;
			if (timeout && ticks >= (ULONG)timeout * 2)
				done = TRUE;
			if (ticks % 4 == 0) {
				if (page == PG_STATUS)
					status_show();
				show_status();
				update_menus();
			}
			treq->tr_time.tv_secs = 0;
			treq->tr_time.tv_micro = 500000;
			SendIO((struct IORequest *)treq);
		}
	}
	set(win, MUIA_Window_Open, FALSE);
out:
	if (timer) {
		if (!CheckIO((struct IORequest *)treq))
			AbortIO((struct IORequest *)treq);
		WaitIO((struct IORequest *)treq);
		CloseDevice((struct IORequest *)treq);
	}
	if (treq)
		DeleteIORequest((struct IORequest *)treq);
	if (tport)
		DeleteMsgPort(tport);
	free_all();
	CloseLibrary(MUIMasterBase);
	MUIMasterBase = NULL;
	return rc;
}
