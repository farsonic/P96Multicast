/*
 * Panorama - muigui.c: PanoramaPrefs' window, in MUI, built as the family's
 * guide says (docs/mui-guide/README.md): flat, in the theme's colours (light, or
 * dark from the View menu), with its own controls and icons drawn in code.
 *
 *  +-----------+--------------------------------------------+
 *  | Desktop   |  Desktop   One screen over viewer windows  |
 *  | Network   |  [ the monitors, the Workbench beside them ]|
 *  | Display   |  Monitors      < Two, side by side >       |
 *  | Wrap.card |  ...           switches                    |
 *  | Status    |                                            |
 *  +-----------+--------------------------------------------+
 *  | Install  Open  Close                Cancel   Use   Save |
 *  +--------------------------------------------------------+
 *
 * Every command is in the menus (Project, Edit, Desktop, View), with its
 * Amiga-key shortcut; the buttons are shortcuts to the same IDs (ids.h),
 * and command() is the one place they all arrive, CMD=n too. Every string
 * the user sees goes through tr() (lang.c): the theme's text and bubbles do
 * it themselves, requesters and report lines here.
 *
 * Every control is one of the classes here, subclasses of MUI's Area:
 * the navigation pane, the picture of the monitors, a choice (< value >),
 * a switch, a button, a label and a list of rows. Text entry is MUI's
 * String. Each class keeps a shown flag (MUIM_Show/Hide) and neither draws
 * nor takes the mouse while hidden. The settings are read back from the
 * controls by collect(), as the GadTools window (gui.c, used when MUI isn't
 * there) does.
 *
 * Where it differs from the guide, and why: no splash window (it starts at
 * once) and no fonts of its own (the screen's font, which the guide's
 * fallback ends at anyway); Install shows the busy pointer but no Abort,
 * because stopping half-way would leave the system half set up, and it
 * takes a second or two; help bubbles can't be switched off (MUI's own
 * settings can).
 *
 * For newcomers to MUI: a custom class is a BOOPSI class made with
 * MUI_CreateCustomClass; its dispatcher gets every method. MUIM_AskMinMax
 * says how big it wants to be (MUI 3.8 fills MinMaxInfo in during the
 * superclass's method: read it after), MUIM_Draw draws it, MUIM_HandleEvent
 * gets the mouse clicks its event handler asks for. Setting an attribute
 * with set() passes it on to the superclass too, which is what lets other
 * objects (and the application, through MUIM_Application_ReturnID) watch
 * it with MUIM_Notify.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/timer.h>
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

#define MTAG (TAG_USER | 0x4d500000UL)
#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

/* the pages */
enum { PG_DESKTOP, PG_NETWORK, PG_DISPLAY, PG_WRAP, PG_STATUS, NPAGES };
static const char *const page_names[NPAGES] = { "Desktop", "Network", "Display", "Wrap.card",
                                                 "Status" };
static const char *const page_subtitles[NPAGES] = {
	"One screen over viewer windows",
	"Where it goes, how fast",
	"Boards, memory, screen modes",
	"Wrap another card",
	"What the card is doing"
};
static const int page_icons[NPAGES] = { IC_DESKTOP, IC_NETWORK, IC_DISPLAY, IC_WRAP, IC_STATUS };

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
static const char *const modesrc_names[] = { "P96's settings", "This list", NULL };
static const char *const wrap_names[] = { "No", "Wrap only", "Mirror", "Span", NULL };
static const char *standard_modes = "640x480,800x600,1024x768,1280x720,1280x960,1280x1024,1920x1080";

static int layout_sel, size_sel, colour_sel;

/* where the Workbench can be: next to an outer edge of a monitor, or nowhere (16 + 1) */
#define NWB 17
static int wb_sel, wb_side[NWB], wb_mon[NWB];
static char wb_text[NWB][24];
static const char *wb_labels[NWB + 1];

/* the mode list and the status lines */
#define MAXMODES 24
static char mode_text[MAXMODES][12];
static int nmodes;
#define NSTATUS 24
static char stat_text[NSTATUS][80];
static int nstat;

static char report[2048];
static Object *app, *win;

/* ================================================================ small things */

#define _between(a, x, b) ((x) >= (a) && (x) <= (b))
#define _inside(obj, x, y) (_between(_mleft(obj), (x), _mright(obj)) && \
                            _between(_mtop(obj), (y), _mbottom(obj)))

/*
 * The object's font: MUI's, or the screen's, or the system's. MUI need not
 * have given an object its font yet when it asks for its size (Easel never
 * uses _font() unchecked either); measuring with none froze the machine.
 */
static struct TextFont *ofont(Object *obj)
{
	struct Screen *scr;

	if (_font(obj))
		return _font(obj);
	scr = _screen(obj);
	if (scr && scr->RastPort.Font)
		return scr->RastPort.Font;
	return GfxBase->DefaultFont;
}

/*
 * Text's width in a font, for MUIM_AskMinMax, from the font's own tables
 * (each character's spacing and kerning, or the fixed width), not from
 * TextLength: a patched text renderer (AAText) froze MUI 3.8 there, with a
 * RastPort that had no bitmap. Bold is a pixel a character wider.
 */
static LONG measure(Object *obj, const char *s, BOOL bold)
{
	struct TextFont *tf = ofont(obj);
	const WORD *space = (const WORD *)tf->tf_CharSpace, *kern = (const WORD *)tf->tf_CharKern;
	LONG w = 0, n = 0;

	s = tr(s);
	for (; *s; s++, n++) {
		UBYTE c = *s;
		LONG i = (c >= tf->tf_LoChar && c <= tf->tf_HiChar ? c : tf->tf_HiChar + 1) -
		         tf->tf_LoChar;

		w += (space && (tf->tf_Flags & FPF_PROPORTIONAL) ? space[i] : tf->tf_XSize) +
		     (kern ? kern[i] : 0);
	}
	return w + (bold ? n : 0);
}

static ULONG xget(Object *obj, ULONG attr)
{
	ULONG v = 0;

	get(obj, attr, &v);
	return v;
}

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

/* every place, for any layout (the choice skips those a layout hasn't); then the one for side, mon */
static void wb_places(int side, int mon)
{
	static const char *names[] = { "", "Left of", "Right of", "Above", "Below" };
	int i, n = 0, sd;

	if (!wb_labels[0]) {
		for (sd = WB_LEFT; sd <= WB_BELOW; sd++)
			for (i = 0; i < 4; i++, n++) {
				wb_side[n] = sd;
				wb_mon[n] = i;
				snprintf(wb_text[n], sizeof(wb_text[n]), "%s monitor %d", names[sd], i + 1);
			}
		wb_side[n] = WB_NONE;
		strcpy(wb_text[n], "Nowhere (hotkey only)");
		for (i = 0; i < NWB; i++)
			wb_labels[i] = wb_text[i];
		wb_labels[NWB] = NULL;
	}
	if (side != WB_NONE)
		wb_fit(&side, &mon, layout_cols(), layout_rows());
	wb_sel = NWB - 1;
	for (i = 0; i < NWB; i++)
		if (wb_side[i] == side && (side == WB_NONE || wb_mon[i] == mon))
			wb_sel = i;
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

/* a line for the report a requester shows after Save, Use, Install or Open */
static void note(const char *s)
{
	int n = strlen(report);

	snprintf(report + n, sizeof(report) - n, "%s%s", n ? "\n" : "", tr(s));
}

static Object *root;    /* the window's contents */

/*
 * The whole window drawn again: MUI doesn't always repair what a requester
 * covered over the classes' own drawing, and a new theme needs it too.
 */
static void redraw_window(void)
{
	if (root && win && xget(win, MUIA_Window_Open)) {
		DoMethod(root, MUIM_Group_InitChange);
		DoMethod(root, MUIM_Group_ExitChange);
	}
}

/* a requester; returns the button, 1 the first (and 0 the last) */
static LONG request(const char *title, const char *buttons, const char *text)
{
	ULONG args[1];
	LONG r;

	mp_trace("requester: %s", title);
	args[0] = (ULONG)tr(text);
	r = MUI_RequestA(app, win, 0, (STRPTR)tr(title), (STRPTR)tr(buttons), (STRPTR)"%s", args);
	redraw_window();
	return r;
}

static void tell(const char *title, const char *text)
{
	request(title, "_OK", text);
}

static BOOL ask(const char *title, const char *text, const char *buttons)
{
	return request(title, buttons, text) == 1;
}

/* help bubbles are kept for the window's life: MUI shows the text it was given later */
static const char *keep(const char *s)
{
	char *k = AllocVec(strlen(s) + 1, MEMF_ANY);
	static APTR kept[160];
	static int nkept;

	if (!k)
		return "";
	strcpy(k, s);
	if (nkept < 160)
		kept[nkept++] = k;
	return k;
}

/* ================================================================ the classes */

/*
 * Every class's data starts with its shown flag: a hidden object (a page
 * not in front) has no place on the window, so it neither draws nor takes
 * the mouse (its old place may be under another page's controls).
 */
struct Shown {
	int shown;
};
#define SHOWN(cl, obj) (((struct Shown *)INST_DATA(cl, obj))->shown)

/* --------------------------------------------------------- the navigation pane */
#define MUIA_Nav_Active (MTAG + 1)
struct NavData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	struct MUI_EventHandlerNode ehn;
	LONG active;
};

static LONG nav_rowh(Object *obj)
{
	return ofont(obj)->tf_YSize + 14;
}

/* the first of each method a class gets, traced (TRACE=) */
static void trace_first(const char *cls, ULONG id)
{
	static ULONG seen[64];
	static int n;
	int i;

	if (!mp_trace_file || (id != MUIM_Setup && id != MUIM_AskMinMax && id != MUIM_Draw &&
	                       id != MUIM_Cleanup && id != OM_NEW))
		return;
	for (i = 0; i < n; i++)
		if (seen[i] == (((ULONG)cls[0] << 24) ^ id))
			return;
	if (n < 64)
		seen[n++] = ((ULONG)cls[0] << 24) ^ id;
	mp_trace("mui: %s gets %s", cls, id == MUIM_Setup ? "Setup" : id == MUIM_AskMinMax ?
	         "AskMinMax" : id == MUIM_Draw ? "Draw" : id == OM_NEW ? "New" : "Cleanup");
}

/* the first time each class has answered MUIM_AskMinMax, traced */
static void trace_sized(const char *cls, struct MUI_MinMax *mm)
{
	static const char *done[8];
	static int n;
	int i;

	if (!mp_trace_file)
		return;
	for (i = 0; i < n; i++)
		if (done[i] == cls)
			return;
	if (n < 8)
		done[n++] = cls;
	mp_trace("mui: %s sized: min %ld x %ld, max %ld x %ld", cls, (LONG)mm->MinWidth,
	         (LONG)mm->MinHeight, (LONG)mm->MaxWidth, (LONG)mm->MaxHeight);
}

static ULONG nav_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                          __reg("a1") Msg msg)
{
	struct NavData *d;

	trace_first("nav", msg->MethodID);
	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case OM_NEW:
		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		d->active = GetTagData(MUIA_Nav_Active, 0, ((struct opSet *)msg)->ops_AttrList);
		return (ULONG)obj;
	case OM_SET: {
		struct TagItem *t = FindTagItem(MUIA_Nav_Active, ((struct opSet *)msg)->ops_AttrList);

		d = INST_DATA(cl, obj);
		if (t && (LONG)t->ti_Data != d->active) {
			d->active = t->ti_Data;
			if (SHOWN(cl, obj))
				MUI_Redraw(obj, MADF_DRAWOBJECT);
		}
		break;
	}
	case OM_GET:
		if (((struct opGet *)msg)->opg_AttrID == MUIA_Nav_Active) {
			*((struct opGet *)msg)->opg_Storage = ((struct NavData *)INST_DATA(cl, obj))->active;
			return TRUE;
		}
		break;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
		d->ehn.ehn_Object = obj;
		d->ehn.ehn_Class = cl;
		DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG w = 0, i;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		for (i = 0; i < NPAGES; i++) {
			LONG l;

			l = measure(obj, page_names[i], TRUE);

			if (l > w)
				w = l;
		}
		w += 16 + 12 + 10 + 16;
		mm->MinWidth += w;
		mm->DefWidth += w;
		mm->MaxWidth += w;
		mm->MinHeight += NPAGES * nav_rowh(obj) + 16;
		mm->DefHeight += NPAGES * nav_rowh(obj) + 16;
		mm->MaxHeight = MUI_MAXMAX;
		trace_sized("nav", mm);
		return 0;
	}
	case MUIM_Draw: {
		struct RastPort *rp = _rp(obj);
		LONG i, rh = nav_rowh(obj), fh = ofont(obj)->tf_YSize;

		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
			return 0;
		d = INST_DATA(cl, obj);
		SetFont(rp, ofont(obj));
		theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_PANEL));
		for (i = 0; i < NPAGES; i++) {
			LONG y = _mtop(obj) + 8 + i * rh;
			BOOL on = i == d->active;

			if (on) {
				theme_fill(rp, _mleft(obj) + 6, y, _mright(obj) - 6, y + rh - 3, tpen(C_HOVER));
				theme_fill(rp, _mleft(obj) + 6, y + 3, _mleft(obj) + 8, y + rh - 6, tpen(C_ACCENT));
			}
			theme_icon(rp, page_icons[i], _mleft(obj) + 16, y + (rh - 3 - 16) / 2, 16,
			           on ? -1 : tpen(C_DIM));
			theme_text(rp, _mleft(obj) + 16 + 16 + 10, y + (rh - 3 - fh) / 2, page_names[i],
			           tpen(on ? C_TEXT : C_DIM), on);
		}
		return 0;
	}
	case MUIM_HandleEvent:
		if (!SHOWN(cl, obj))
			return 0;
		{
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		if (im && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN &&
		    _inside(obj, im->MouseX, im->MouseY)) {
			LONG i = (im->MouseY - _mtop(obj) - 8) / nav_rowh(obj);

			if (i >= 0 && i < NPAGES)
				set(obj, MUIA_Nav_Active, i);
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- the monitors and the Workbench */
#define MUIA_Map_Clicked (MTAG + 10)    /* (notify) the Workbench was put somewhere else */
struct MapData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	struct MUI_EventHandlerNode ehn;
	LONG clicked;
};

/* the monitors' top left and one monitor's size, with room around them for the Workbench */
static void map_geometry(Object *obj, LONG *ox, LONG *oy, LONG *cw, LONG *ch)
{
	LONG c = layout_cols(), r = layout_rows(), w = _mwidth(obj) - 8;
	LONG h = _mheight(obj) - ofont(obj)->tf_YSize - 10;
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

static void wb_box(Object *obj, int n, LONG ox, LONG oy, LONG cw, LONG ch, LONG *bx, LONG *by,
                   LONG *bw, LONG *bh)
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
	LONG c = layout_cols(), r = layout_rows(), ox, oy, cw, ch, i, fh = ofont(obj)->tf_YSize;
	const char *pv = prefs_value(find_setting("PORT"));
	UWORD mw, mh;
	int port = atoi(pv);
	char buf[80];

	SetFont(rp, ofont(obj));
	theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_BACK));
	map_geometry(obj, &ox, &oy, &cw, &ch);
	monitor_size(&mw, &mh);
	for (i = 0; i < c * r; i++) {
		LONG x = ox + (i % c) * cw, y = oy + (i / c) * ch, l;

		theme_fill(rp, x + 2, y + 2, x + cw - 3, y + ch - 3, tpen(C_MONITOR));
		theme_rect(rp, x + 2, y + 2, x + cw - 3, y + ch - 3, tpen(C_MONITOR2));
		snprintf(buf, sizeof(buf), "%d", (int)i + 1);
		l = theme_textlen(rp, buf, TRUE);
		theme_text(rp, x + (cw - l) / 2, y + ch / 2 - fh, buf, tpen(C_ONACCENT), TRUE);
		snprintf(buf, sizeof(buf), "port %d", port + (int)i);
		l = theme_textlen(rp, buf, FALSE);
		if (l < cw - 8 && ch > 3 * fh)
			theme_text(rp, x + (cw - l) / 2, y + ch / 2 + 1, buf, tpen(C_ONACCENT), FALSE);
	}
	if (wb_side[wb_sel] != WB_NONE) {
		LONG bx, by, bw, bh, l;

		wb_box(obj, wb_sel, ox, oy, cw, ch, &bx, &by, &bw, &bh);
		if (bw > 4 && bh > 4 && bx >= _mleft(obj) && by >= _mtop(obj)) {
			theme_fill(rp, bx, by, bx + bw - 1, by + bh - 1, tpen(C_WB));
			strcpy(buf, "Workbench");
			if ((l = theme_textlen(rp, buf, FALSE)) > bw - 4) {
				strcpy(buf, "WB");
				l = theme_textlen(rp, buf, FALSE);
			}
			theme_text(rp, bx + (bw - l) / 2, by + (bh - fh) / 2, buf, tpen(C_BACK), FALSE);
		}
	}
	snprintf(buf, sizeof(buf), "Desktop %d x %d%s", (int)(c * mw), (int)(r * mh),
	         c * r > 1 ? ": windows move across" : ": one Panorama screen");
	i = theme_textlen(rp, buf, FALSE);
	theme_text(rp, _mleft(obj) + (_mwidth(obj) - i) / 2, _mbottom(obj) - fh - 2, buf,
	           tpen(C_DIM), FALSE);
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
		wb_box(obj, i, ox, oy, cw, ch, &bx, &by, &bw, &bh);
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

static ULONG map_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                          __reg("a1") Msg msg)
{
	struct MapData *d;

	trace_first("map", msg->MethodID);

	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
		d->ehn.ehn_Object = obj;
		d->ehn.ehn_Class = cl;
		DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG fh = ofont(obj)->tf_YSize;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		mm->MinWidth += 260;
		mm->DefWidth += 380;
		mm->MaxWidth = MUI_MAXMAX;
		mm->MinHeight += 4 * fh + 10;
		mm->DefHeight += 12 * fh + 10;
		mm->MaxHeight += 16 * fh + 10;
		trace_sized("map", mm);
		return 0;
	}
	case MUIM_Draw:
		DoSuperMethodA(cl, obj, msg);
		if (((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE))
			map_draw(obj);
		return 0;
	case MUIM_HandleEvent:
		if (!SHOWN(cl, obj))
			return 0;
		{
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		if (im && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN &&
		    _inside(obj, im->MouseX, im->MouseY)) {
			if (map_click(obj, im->MouseX, im->MouseY)) {
				if (SHOWN(cl, obj))
					MUI_Redraw(obj, MADF_DRAWOBJECT);
				d = INST_DATA(cl, obj);
				set(obj, MUIA_Map_Clicked, ++d->clicked);
			}
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- a choice: < value > */
#define MUIA_Choice_Labels (MTAG + 20)   /* (init) NULL-ended */
#define MUIA_Choice_Active (MTAG + 21)   /* (isg, notify) */
#define MUIA_Choice_Valid  (MTAG + 22)   /* (init) BOOL (*)(int): the entries to offer */
struct ChoiceData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	struct MUI_EventHandlerNode ehn;
	const char *const *labels;
	LONG n, active;
	BOOL (*valid)(int);
};

static ULONG choice_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                             __reg("a1") Msg msg)
{
	struct ChoiceData *d;

	trace_first("choice", msg->MethodID);

	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case OM_NEW: {
		struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
		struct TagItem more[] = { { MUIA_CycleChain, 1 }, { TAG_MORE, (ULONG)tags } };
		struct opSet sup = { OM_NEW, more, ((struct opSet *)msg)->ops_GInfo };

		if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)&sup)))
			return 0;
		d = INST_DATA(cl, obj);
		d->labels = (const char *const *)GetTagData(MUIA_Choice_Labels, 0, tags);
		d->active = GetTagData(MUIA_Choice_Active, 0, tags);
		d->valid = (BOOL (*)(int))GetTagData(MUIA_Choice_Valid, 0, tags);
		for (d->n = 0; d->labels && d->labels[d->n]; d->n++)
			;
		return (ULONG)obj;
	}
	case OM_SET: {
		struct TagItem *t = FindTagItem(MUIA_Choice_Active, ((struct opSet *)msg)->ops_AttrList);

		d = INST_DATA(cl, obj);
		if (t && (LONG)t->ti_Data != d->active && (LONG)t->ti_Data < d->n) {
			d->active = t->ti_Data;
			if (SHOWN(cl, obj))
				MUI_Redraw(obj, MADF_DRAWUPDATE);
		}
		break;
	}
	case OM_GET:
		if (((struct opGet *)msg)->opg_AttrID == MUIA_Choice_Active) {
			*((struct opGet *)msg)->opg_Storage = ((struct ChoiceData *)INST_DATA(cl, obj))->active;
			return TRUE;
		}
		break;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
		d->ehn.ehn_Object = obj;
		d->ehn.ehn_Class = cl;
		DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG w = 0, i, fh = ofont(obj)->tf_YSize;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		d = INST_DATA(cl, obj);
		for (i = 0; i < d->n; i++) {
			LONG l = measure(obj, d->labels[i], FALSE);

			if (l > w)
				w = l;
		}
		w += 2 * (fh + 8) + 12;
		mm->MinWidth += w;
		mm->DefWidth += w;
		mm->MaxWidth = MUI_MAXMAX;
		mm->MinHeight += fh + 8;
		mm->DefHeight += fh + 8;
		mm->MaxHeight += fh + 8;
		trace_sized("choice", mm);
		return 0;
	}
	case MUIM_Draw: {
		struct RastPort *rp = _rp(obj);
		LONG fh = ofont(obj)->tf_YSize, aw = fh + 8, l;
		const char *s;

		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
			return 0;
		d = INST_DATA(cl, obj);
		SetFont(rp, ofont(obj));
		theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_FIELD));
		theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_BORDER));
		theme_icon(rp, IC_LEFT, _mleft(obj) + (aw - fh) / 2, _mtop(obj) + 4, fh, tpen(C_DIM));
		theme_icon(rp, IC_RIGHT, _mright(obj) - aw + (aw - fh) / 2, _mtop(obj) + 4, fh,
		           tpen(C_DIM));
		s = d->active < d->n ? d->labels[d->active] : "";
		l = theme_textlen(rp, s, FALSE);
		theme_text(rp, _mleft(obj) + (_mwidth(obj) - l) / 2, _mtop(obj) + 4, s, tpen(C_TEXT),
		           FALSE);
		return 0;
	}
	case MUIM_HandleEvent:
		if (!SHOWN(cl, obj))
			return 0;
		{
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		if (im && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN &&
		    _inside(obj, im->MouseX, im->MouseY)) {
			/* the left part steps back, the rest on; shift steps back too */
			BOOL back = im->MouseX < _mleft(obj) + _mwidth(obj) / 3 ||
			            (im->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT));
			LONG n, k;

			d = INST_DATA(cl, obj);
			n = d->active;
			for (k = 0; k < d->n; k++) {
				n = (n + (back ? d->n - 1 : 1)) % d->n;
				if (!d->valid || d->valid(n))
					break;
			}
			set(obj, MUIA_Choice_Active, n);
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- a switch */
#define MUIA_Switch_On (MTAG + 30)        /* (isg, notify) */
struct SwitchData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	struct MUI_EventHandlerNode ehn;
	LONG on;
};

static ULONG switch_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                             __reg("a1") Msg msg)
{
	struct SwitchData *d;

	trace_first("switch", msg->MethodID);

	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case OM_NEW: {
		struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
		struct TagItem more[] = { { MUIA_CycleChain, 1 }, { TAG_MORE, (ULONG)tags } };
		struct opSet sup = { OM_NEW, more, ((struct opSet *)msg)->ops_GInfo };

		if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)&sup)))
			return 0;
		d = INST_DATA(cl, obj);
		d->on = GetTagData(MUIA_Switch_On, 0, tags) != 0;
		return (ULONG)obj;
	}
	case OM_SET: {
		struct TagItem *t = FindTagItem(MUIA_Switch_On, ((struct opSet *)msg)->ops_AttrList);

		d = INST_DATA(cl, obj);
		if (t && (t->ti_Data != 0) != d->on) {
			d->on = t->ti_Data != 0;
			if (SHOWN(cl, obj))
				MUI_Redraw(obj, MADF_DRAWUPDATE);
		}
		break;
	}
	case OM_GET:
		if (((struct opGet *)msg)->opg_AttrID == MUIA_Switch_On) {
			*((struct opGet *)msg)->opg_Storage = ((struct SwitchData *)INST_DATA(cl, obj))->on;
			return TRUE;
		}
		break;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
		d->ehn.ehn_Object = obj;
		d->ehn.ehn_Class = cl;
		DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG h = ofont(obj)->tf_YSize + 6;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		mm->MinWidth += 2 * h;
		mm->DefWidth += 2 * h;
		mm->MaxWidth += 2 * h;
		mm->MinHeight += h;
		mm->DefHeight += h;
		mm->MaxHeight += h;
		trace_sized("switch", mm);
		return 0;
	}
	case MUIM_Draw: {
		struct RastPort *rp = _rp(obj);
		LONG x0 = _mleft(obj), y0 = _mtop(obj), x1 = _mright(obj), y1 = _mbottom(obj);
		LONG r = (y1 - y0) / 2, k = (y1 - y0) - 4;

		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
			return 0;
		d = INST_DATA(cl, obj);
		theme_fill(rp, x0, y0, x1, y1, tpen(C_BACK));
		theme_round(rp, x0, y0, x1, y1, r, tpen(d->on ? C_ACCENT : C_BORDER));
		if (!d->on)
			theme_round(rp, x0 + 1, y0 + 1, x1 - 1, y1 - 1, r - 1, tpen(C_FIELD));
		/* the knob */
		theme_round(rp, d->on ? x1 - 2 - k : x0 + 2, y0 + 2, d->on ? x1 - 2 : x0 + 2 + k,
		            y1 - 2, k / 2, tpen(d->on ? C_ONACCENT : C_DIM));
		return 0;
	}
	case MUIM_HandleEvent:
		if (!SHOWN(cl, obj))
			return 0;
		{
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		if (im && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN &&
		    _inside(obj, im->MouseX, im->MouseY)) {
			d = INST_DATA(cl, obj);
			set(obj, MUIA_Switch_On, !d->on);
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- a button, and a label */
#define MUIA_Button_Label   (MTAG + 40)   /* (init) "_Save": S is its key */
#define MUIA_Button_Icon    (MTAG + 41)   /* (init) IC_ */
#define MUIA_Button_Primary (MTAG + 42)   /* (init) the accent's colour: the main action */
#define MUIA_Label_Flags    (MTAG + 43)   /* (init) LB_ */
#define LB_LEFT  1
#define LB_TITLE 2      /* a page's heading: bold */
#define LB_DIM   4      /* help under it */
#define LB_FIT   8      /* as wide as its text, no wider */
struct ButtonData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	char label[64];
	LONG key, icon;
	BOOL primary;
	ULONG flags;
};

static ULONG button_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                             __reg("a1") Msg msg)
{
	struct ButtonData *d;

	trace_first("button", msg->MethodID);

	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case OM_NEW: {
		struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;
		const char *l = (const char *)GetTagData(MUIA_Button_Label, (ULONG)"", tags);
		ULONG flags = GetTagData(MUIA_Label_Flags, ~0UL, tags);
		struct TagItem more[] = { { MUIA_InputMode, MUIV_InputMode_RelVerify },
		                          { MUIA_CycleChain, 1 }, { MUIA_ControlChar, 0 },
		                          { TAG_MORE, (ULONG)tags } };
		char lab[64];
		LONG key = -1, k = 0;
		const char *p;

		for (p = l; *p && k < 63; p++) {
			if (*p == '_' && p[1] && key < 0) {
				key = k;
				continue;
			}
			lab[k++] = *p;
		}
		lab[k] = 0;
		if (flags != ~0UL) {
			/* a label: no input */
			more[0].ti_Tag = more[1].ti_Tag = more[2].ti_Tag = TAG_IGNORE;
		} else if (key >= 0) {
			more[2].ti_Data = ToLower((ULONG)(UBYTE)lab[key]);
		} else {
			more[2].ti_Tag = TAG_IGNORE;
		}
		{
			struct opSet sup = { OM_NEW, more, ((struct opSet *)msg)->ops_GInfo };

			if (!(obj = (Object *)DoSuperMethodA(cl, obj, (Msg)&sup)))
				return 0;
		}
		d = INST_DATA(cl, obj);
		strcpy(d->label, lab);
		d->key = key;
		d->icon = GetTagData(MUIA_Button_Icon, IC_NONE, tags);
		d->primary = GetTagData(MUIA_Button_Primary, FALSE, tags);
		d->flags = flags;
		return (ULONG)obj;
	}
	case OM_SET: {
		/* dimmed or not: drawn again in its new colours */
		ULONG r = DoSuperMethodA(cl, obj, msg);

		if (FindTagItem(MUIA_Disabled, ((struct opSet *)msg)->ops_AttrList) && SHOWN(cl, obj))
			MUI_Redraw(obj, MADF_DRAWOBJECT);
		return r;
	}
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		return TRUE;
	case MUIM_Cleanup:
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG w, h, fh = ofont(obj)->tf_YSize;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		d = INST_DATA(cl, obj);
		w = measure(obj, d->label, (d->flags & LB_TITLE) != 0) + 2;
		if (d->flags == ~0UL) {
			/* a button: room round the label, and the icon */
			w += 28 + (d->icon ? fh + 6 : 0);
			h = fh + 10;
		} else {
			h = fh + 6;
		}
		mm->MinWidth += w;
		mm->DefWidth += w;
		if (d->flags == ~0UL ||
		    ((d->flags & (LB_LEFT | LB_TITLE | LB_DIM)) && !(d->flags & LB_FIT)))
			mm->MaxWidth = MUI_MAXMAX;
		else
			mm->MaxWidth += w;
		mm->MinHeight += h;
		mm->DefHeight += h;
		mm->MaxHeight += h;
		trace_sized("button", mm);
		return 0;
	}
	case MUIM_Draw: {
		struct RastPort *rp = _rp(obj);
		LONG fh = ofont(obj)->tf_YSize, tw, x, y, iw;
		ULONG sel = 0, off = 0;

		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
			return 0;
		d = INST_DATA(cl, obj);
		SetFont(rp, ofont(obj));
		if (d->flags != ~0UL) {
			/* a label */
			BOOL bold = (d->flags & LB_TITLE) != 0;

			theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_BACK));
			tw = theme_textlen(rp, d->label, bold);
			x = d->flags & (LB_LEFT | LB_TITLE | LB_DIM) ? _mleft(obj) : _mright(obj) - tw;
			theme_text(rp, x, _mtop(obj) + (_mheight(obj) - fh) / 2, d->label,
			           tpen(d->flags & LB_DIM ? C_DIM : C_TEXT), bold);
			return 0;
		}
		get(obj, MUIA_Selected, &sel);
		get(obj, MUIA_Disabled, &off);
		if (off)
			sel = 0;
		theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_PANEL));
		if (d->primary) {
			theme_round(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), 3,
			            tpen(sel ? C_ACCENTHOVER : C_ACCENT));
		} else {
			theme_round(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), 3,
			            tpen(sel ? C_ACCENT : C_BORDER));
			theme_round(rp, _mleft(obj) + 1, _mtop(obj) + 1, _mright(obj) - 1, _mbottom(obj) - 1,
			            2, tpen(sel ? C_HOVER : C_FIELD));
		}
		iw = d->icon ? fh + 6 : 0;
		tw = theme_textlen(rp, d->label, FALSE);
		x = _mleft(obj) + (_mwidth(obj) - tw - iw) / 2;
		y = _mtop(obj) + (_mheight(obj) - fh) / 2;
		if (d->icon)
			theme_icon(rp, d->icon, x, y, fh, off ? tpen(C_DISABLED) :
			           d->primary ? tpen(C_ONACCENT) : -1);
		theme_text(rp, x + iw, y, d->label,
		           tpen(off ? C_DISABLED : d->primary ? C_ONACCENT : C_TEXT), FALSE);
		/* the key's letter underlined (not in a translation: it may not be there) */
		if (d->key >= 0 && tr(d->label) == (const char *)d->label) {
			LONG ux = x + iw + TextLength(rp, (STRPTR)d->label, d->key);
			LONG uw = TextLength(rp, (STRPTR)d->label + d->key, 1);

			Move(rp, ux, y + rp->TxBaseline + 1);
			Draw(rp, ux + uw - 1, y + rp->TxBaseline + 1);
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- rows: the mode list, the status */
#define MUIA_Rows_Lines  (MTAG + 50)   /* (init) char (*)[n] rows, each width chars */
#define MUIA_Rows_Width  (MTAG + 51)   /* (init) the size of each row's array */
#define MUIA_Rows_Count  (MTAG + 52)   /* (isg) how many */
#define MUIA_Rows_Active (MTAG + 53)   /* (isg) the one chosen, -1 none; selectable if given */
#define MUIA_Rows_Visible (MTAG + 54)  /* (init) rows to make room for */
struct RowsData {
	int shown;                  /* between MUIM_Show and MUIM_Hide: drawing allowed */
	struct MUI_EventHandlerNode ehn;
	char *lines;
	LONG width, count, active, visible;
	BOOL select;
};

static ULONG rows_dispatch(__reg("a0") struct IClass *cl, __reg("a2") Object *obj,
                           __reg("a1") Msg msg)
{
	struct RowsData *d;

	trace_first("rows", msg->MethodID);

	switch (msg->MethodID) {
	case MUIM_Show:
	case MUIM_Hide:
		SHOWN(cl, obj) = msg->MethodID == MUIM_Show;
		break;
	case OM_NEW: {
		struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList;

		if (!(obj = (Object *)DoSuperMethodA(cl, obj, msg)))
			return 0;
		d = INST_DATA(cl, obj);
		d->lines = (char *)GetTagData(MUIA_Rows_Lines, 0, tags);
		d->width = GetTagData(MUIA_Rows_Width, 80, tags);
		d->count = GetTagData(MUIA_Rows_Count, 0, tags);
		d->visible = GetTagData(MUIA_Rows_Visible, 6, tags);
		d->select = FindTagItem(MUIA_Rows_Active, tags) != NULL;
		d->active = d->select ? (LONG)GetTagData(MUIA_Rows_Active, -1, tags) : -1;
		return (ULONG)obj;
	}
	case OM_SET: {
		struct TagItem *tags = ((struct opSet *)msg)->ops_AttrList, *t;

		d = INST_DATA(cl, obj);
		if ((t = FindTagItem(MUIA_Rows_Count, tags)) != NULL)
			d->count = t->ti_Data;
		if ((t = FindTagItem(MUIA_Rows_Active, tags)) != NULL)
			d->active = t->ti_Data;
		if (FindTagItem(MUIA_Rows_Count, tags) || FindTagItem(MUIA_Rows_Active, tags))
			if (SHOWN(cl, obj))
				MUI_Redraw(obj, MADF_DRAWUPDATE);
		break;
	}
	case OM_GET:
		if (((struct opGet *)msg)->opg_AttrID == MUIA_Rows_Active) {
			*((struct opGet *)msg)->opg_Storage = ((struct RowsData *)INST_DATA(cl, obj))->active;
			return TRUE;
		}
		break;
	case MUIM_Setup:
		if (!DoSuperMethodA(cl, obj, msg))
			return FALSE;
		theme_setup(obj);
		d = INST_DATA(cl, obj);
		if (d->select) {
			d->ehn.ehn_Events = IDCMP_MOUSEBUTTONS;
			d->ehn.ehn_Object = obj;
			d->ehn.ehn_Class = cl;
			DoMethod(_win(obj), MUIM_Window_AddEventHandler, (ULONG)&d->ehn);
		}
		return TRUE;
	case MUIM_Cleanup:
		d = INST_DATA(cl, obj);
		if (d->select)
			DoMethod(_win(obj), MUIM_Window_RemEventHandler, (ULONG)&d->ehn);
		theme_cleanup(obj);
		break;
	case MUIM_AskMinMax: {
		struct MUI_MinMax *mm;
		LONG rh = ofont(obj)->tf_YSize + 3;

		DoSuperMethodA(cl, obj, msg);
		/* MUI 3.8 fills MinMaxInfo in during the superclass's method: read it after */
		mm = ((struct MUIP_AskMinMax *)msg)->MinMaxInfo;
		d = INST_DATA(cl, obj);
		mm->MinWidth += 160;
		mm->DefWidth += 300;
		mm->MaxWidth = MUI_MAXMAX;
		mm->MinHeight += 3 * rh + 8;
		mm->DefHeight += d->visible * rh + 8;
		mm->MaxHeight = MUI_MAXMAX;
		trace_sized("rows", mm);
		return 0;
	}
	case MUIM_Draw: {
		struct RastPort *rp = _rp(obj);
		LONG rh = ofont(obj)->tf_YSize + 3, i, y;

		DoSuperMethodA(cl, obj, msg);
		if (!(((struct MUIP_Draw *)msg)->flags & (MADF_DRAWOBJECT | MADF_DRAWUPDATE)))
			return 0;
		d = INST_DATA(cl, obj);
		SetFont(rp, ofont(obj));
		theme_fill(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_FIELD));
		theme_rect(rp, _mleft(obj), _mtop(obj), _mright(obj), _mbottom(obj), tpen(C_BORDER));
		for (i = 0, y = _mtop(obj) + 4; i < d->count && y + rh <= _mbottom(obj); i++, y += rh) {
			const char *s = d->lines + i * d->width;

			if (i == d->active)
				theme_fill(rp, _mleft(obj) + 2, y - 1, _mright(obj) - 2, y + rh - 2,
				           tpen(C_ACCENT));
			{
				/* as much as fits */
				struct TextExtent te;
				LONG n = TextFit(rp, (STRPTR)s, strlen(s), &te, NULL, 1,
				                 _mwidth(obj) - 16, rp->TxHeight);
				char cut[100];

				if (n > 99)
					n = 99;
				memcpy(cut, s, n);
				cut[n] = 0;
				theme_text(rp, _mleft(obj) + 8, y, cut,
				           tpen(i == d->active ? C_ONACCENT : s[0] == ' ' ? C_DIM : C_TEXT),
				           FALSE);
			}
		}
		return 0;
	}
	case MUIM_HandleEvent:
		if (!SHOWN(cl, obj))
			return 0;
		{
		struct IntuiMessage *im = ((struct MUIP_HandleEvent *)msg)->imsg;

		if (im && im->Class == IDCMP_MOUSEBUTTONS && im->Code == SELECTDOWN &&
		    _inside(obj, im->MouseX, im->MouseY)) {
			LONG i = (im->MouseY - _mtop(obj) - 4) / (ofont(obj)->tf_YSize + 3);

			d = INST_DATA(cl, obj);
			set(obj, MUIA_Rows_Active, i >= 0 && i < d->count ? i : -1);
			return MUI_EventHandlerRC_Eat;
		}
		return 0;
	}
	}
	return DoSuperMethodA(cl, obj, msg);
}

/* --------------------------------------------------------- the classes made and freed */
static struct MUI_CustomClass *nav_cl, *map_cl, *choice_cl, *switch_cl, *button_cl, *rows_cl;

static BOOL classes_make(void)
{
	nav_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct NavData),
	                               (APTR)nav_dispatch);
	map_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct MapData),
	                               (APTR)map_dispatch);
	choice_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct ChoiceData),
	                                  (APTR)choice_dispatch);
	switch_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct SwitchData),
	                                  (APTR)switch_dispatch);
	button_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct ButtonData),
	                                  (APTR)button_dispatch);
	rows_cl = MUI_CreateCustomClass(NULL, MUIC_Area, NULL, sizeof(struct RowsData),
	                                (APTR)rows_dispatch);
	return nav_cl && map_cl && choice_cl && switch_cl && button_cl && rows_cl;
}

static void classes_free(void)
{
	struct MUI_CustomClass **c[] = { &nav_cl, &map_cl, &choice_cl, &switch_cl, &button_cl,
	                                 &rows_cl };
	int i;

	for (i = 0; i < 6; i++)
		if (*c[i]) {
			MUI_DeleteCustomClass(*c[i]);
			*c[i] = NULL;
		}
}

static Object *MpButton(const char *label, int icon, BOOL primary)
{
	return NewObject(button_cl->mcc_Class, NULL, MUIA_Button_Label, (ULONG)label,
	                 MUIA_Button_Icon, icon, MUIA_Button_Primary, primary, TAG_DONE);
}

static Object *MpLabel(const char *label, ULONG flags)
{
	return NewObject(button_cl->mcc_Class, NULL, MUIA_Button_Label, (ULONG)label,
	                 MUIA_Label_Flags, flags, TAG_DONE);
}

static Object *MpChoice(const char *const *labels, LONG active, BOOL (*valid)(int), const char *help)
{
	return NewObject(choice_cl->mcc_Class, NULL, MUIA_Choice_Labels, (ULONG)labels,
	                 MUIA_Choice_Active, active, MUIA_Choice_Valid, (ULONG)valid,
	                 MUIA_ShortHelp, (ULONG)help, TAG_DONE);
}

/* ================================================================ the fields */

enum { K_TEXT, K_NUM, K_BOOL };
struct Field {
	UBYTE page;
	UBYTE kind;
	const char *key;
	const char *label;
	Object *obj;
};

static struct Field fields[] = {
	{ PG_DESKTOP, K_BOOL, "DEFAULTSCREEN", "Programs open on it" },
	{ PG_DESKTOP, K_BOOL, "ATBOOT", "Open at boot" },
	{ PG_DESKTOP, K_BOOL, "BEHIND", "Open behind Workbench" },
	{ PG_DESKTOP, K_BOOL, "INPUT", "Viewers control it" },
	{ PG_DESKTOP, K_BOOL, "WBMONITOR", "Workbench keeps display" },
	{ PG_DESKTOP, K_BOOL, "P96SCREENCX", "P96ScreenCx tool types" },

	{ PG_NETWORK, K_TEXT, "DEST", "Send to" },
	{ PG_NETWORK, K_TEXT, "DEST2", "Monitor 2 to" },
	{ PG_NETWORK, K_TEXT, "DEST3", "Monitor 3 to" },
	{ PG_NETWORK, K_TEXT, "DEST4", "Monitor 4 to" },
	{ PG_NETWORK, K_NUM, "PORT", "UDP port" },
	{ PG_NETWORK, K_NUM, "TTL", "Multicast hops" },
	{ PG_NETWORK, K_NUM, "FPS", "Updates a second" },
	{ PG_NETWORK, K_NUM, "RATE", "Most KB a second" },
	{ PG_NETWORK, K_NUM, "SCAN", "Check it all in (s)" },
	{ PG_NETWORK, K_TEXT, "INPUTFROM", "Input only from" },
	{ PG_NETWORK, K_TEXT, "LOG", "Log file" },
	{ PG_NETWORK, K_BOOL, "BLITTER", "Send the drawing" },
	{ PG_NETWORK, K_BOOL, "POINTER", "Viewers draw the pointer" },

	{ PG_DISPLAY, K_NUM, "BOARDS", "Boards" },
	{ PG_DISPLAY, K_NUM, "VRAM", "Video MB a board" },
	{ PG_DISPLAY, K_NUM, "MAXWIDTH", "Widest mode" },
	{ PG_DISPLAY, K_NUM, "MAXHEIGHT", "Highest mode" },

	{ PG_WRAP, K_TEXT, "CARD", "Card to wrap" },
	{ PG_WRAP, K_TEXT, "MIRROR", "Mirror on" },
	{ PG_WRAP, K_TEXT, "SPAN", "Span (CxR)" },
	{ PG_WRAP, K_TEXT, "HEAD", "Every head" },
	{ PG_WRAP, K_TEXT, "HEAD1", "Head 1" },
	{ PG_WRAP, K_TEXT, "HEAD2", "Head 2" },
	{ PG_WRAP, K_NUM, "MIRRORFPS", "Head updates a second" },
	{ PG_WRAP, K_BOOL, "HEADSHARE", "Heads share its memory" },
	{ PG_WRAP, K_BOOL, "KEEPDISPLAY", "Keep its display on" },
};
#define NFIELDS (sizeof(fields) / sizeof(fields[0]))

static Object *nav, *pages, *map, *ch_layout, *ch_size, *ch_colours, *ch_wb, *ch_modesrc,
              *ch_wrap, *modes_rows, *mode_entry, *status_rows;

static Object *field_obj(struct Field *f)
{
	const struct Setting *s = find_setting(f->key);
	const char *v = s ? prefs_value(s) : "";
	const char *help = keep(theme_bubble(f->label, s ? s->help : ""));

	if (f->kind == K_BOOL)
		f->obj = NewObject(switch_cl->mcc_Class, NULL, MUIA_Switch_On, v[0] == '1',
		                   MUIA_ShortHelp, (ULONG)help, TAG_DONE);
	else
		f->obj = StringObject, StringFrame, MUIA_String_Contents, (ULONG)v,
		         MUIA_String_MaxLen, 80, MUIA_CycleChain, 1, MUIA_ShortHelp, (ULONG)help,
		         (ULONG)(f->kind == K_NUM ? MUIA_String_Accept : TAG_IGNORE),
		         (ULONG)"-0123456789", End;
	return f->obj;
}

/* a page's text fields, label and field, one or two (pairs) to a row */
static Object *form(int page, int pairs)
{
	Object *g = ColGroup(2 * pairs), MUIA_Group_VertSpacing, 3, MUIA_Group_HorizSpacing, 10,
	            End;
	unsigned i, n = 0;

	for (i = 0; i < NFIELDS; i++)
		if (fields[i].page == page && fields[i].kind != K_BOOL) {
			DoMethod(g, OM_ADDMEMBER, (ULONG)MpLabel(fields[i].label, 0));
			DoMethod(g, OM_ADDMEMBER, (ULONG)field_obj(&fields[i]));
			n++;
		}
	for (; n % pairs; n++) {
		DoMethod(g, OM_ADDMEMBER, (ULONG)HSpace(0));
		DoMethod(g, OM_ADDMEMBER, (ULONG)HSpace(0));
	}
	return g;
}

/* a page's switches, two to a row: switch, then its label */
static Object *switches(int page)
{
	Object *g = ColGroup(4), MUIA_Group_VertSpacing, 3, MUIA_Group_HorizSpacing, 8, End;
	unsigned i, n = 0;

	for (i = 0; i < NFIELDS; i++)
		if (fields[i].page == page && fields[i].kind == K_BOOL) {
			DoMethod(g, OM_ADDMEMBER, (ULONG)field_obj(&fields[i]));
			DoMethod(g, OM_ADDMEMBER, (ULONG)MpLabel(fields[i].label, LB_LEFT));
			n++;
		}
	if (n & 1) {
		DoMethod(g, OM_ADDMEMBER, (ULONG)HSpace(0));
		DoMethod(g, OM_ADDMEMBER, (ULONG)HSpace(0));
	}
	return g;
}

static Object *heading(int page)
{
	/* one line: the title, and what the page is for */
	return HGroup, MUIA_Group_Spacing, 12,
		Child, MpLabel(page_names[page], LB_TITLE | LB_FIT),
		Child, MpLabel(page_subtitles[page], LB_DIM),
	End;
}

static BOOL wb_valid(int n)
{
	return wb_fits(n);
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

static BOOL set_or_say(const struct Setting *s, const char *v)
{
	const char *why;
	char buf[160];

	if (!strcmp(v, prefs_value(s)))
		return TRUE;
	if ((why = prefs_set(s, v)) == NULL)
		return TRUE;
	snprintf(buf, sizeof(buf), "%s %s", s->key, why);
	tell("PanoramaPrefs", buf);
	return FALSE;
}

/* every control into the settings; FALSE if a value won't do */
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
		mp_trace("collect: %s", f->key);
		if (f->kind == K_BOOL) {
			snprintf(buf, sizeof(buf), "%d", xget(f->obj, MUIA_Switch_On) ? 1 : 0);
		} else {
			strncpy(buf, (char *)xget(f->obj, MUIA_String_Contents), sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = 0;
		}
		if (!set_or_say(s, buf))
			return FALSE;
	}
	mp_trace("collect: the Desktop page");
	layout_sel = xget(ch_layout, MUIA_Choice_Active);
	size_sel = xget(ch_size, MUIA_Choice_Active);
	colour_sel = xget(ch_colours, MUIA_Choice_Active);
	prefs_set(find_setting("SPLIT"), layout_sel ? layout_split[layout_sel] : "");
	prefs_set(find_setting("MONITOR"), size_value[size_sel]);
	prefs_set(find_setting("COLOURS"), colour_value[colour_sel]);
	wb_name(buf, sizeof(buf), wb_side[wb_sel], wb_mon[wb_sel]);
	prefs_set(find_setting("WORKBENCH"), buf);

	wrap = xget(ch_wrap, MUIA_Choice_Active);
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
			tell("PanoramaPrefs", "Card to wrap: which card should Wrap.card wrap?\n"
			     "(e.g. uaegfx.card, or Panorama.card)");
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
	src = xget(ch_modesrc, MUIA_Choice_Active);
	if (src == 0) {
		prefs_set(find_setting("MODES"), "");
	} else {
		modes_to(buf, sizeof(buf));
		if (!nmodes) {
			tell("PanoramaPrefs", "Screen modes: the list is empty.\n"
			     "Add sizes, or choose P96's settings.");
			return FALSE;
		}
		if (!set_or_say(find_setting("MODES"), buf))
			return FALSE;
	}
	return TRUE;
}

/* ================================================================ the actions */

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

static void status_show(void)
{
	nstat = status_lines(stat_text, NSTATUS);
	set(status_rows, MUIA_Rows_Count, nstat);
}

static void modes_show(void)
{
	set(modes_rows, MUIA_Rows_Count, nmodes);
	set(modes_rows, MUIA_Rows_Active, -1);
}

static void add_mode(void)
{
	char *s = (char *)xget(mode_entry, MUIA_String_Contents);

	if (!s || !*s)
		return;
	if (prefs_check(find_setting("MODES"), s) || !mp_stricmp(s, "none")) {
		tell("PanoramaPrefs", "A size is width x height, e.g. 1280x960");
		return;
	}
	if (nmodes >= MAXMODES)
		return;
	strncpy(mode_text[nmodes], s, 11);
	mode_text[nmodes][11] = 0;
	nmodes++;
	modes_show();
	set(mode_entry, MUIA_String_Contents, (ULONG)"");
	set(ch_modesrc, MUIA_Choice_Active, 1);
}

static void remove_mode(void)
{
	LONG k = xget(modes_rows, MUIA_Rows_Active), i;

	if (k < 0 || k >= nmodes)
		return;
	for (i = k; i < nmodes - 1; i++)
		strcpy(mode_text[i], mode_text[i + 1]);
	nmodes--;
	modes_show();
}

/* Save or Use; TRUE: done, the window may close */
static BOOL save(BOOL envarc)
{
	mp_trace("save/use: collect");
	if (!collect())
		return FALSE;
	report[0] = 0;
	prefs_fix(note);
	mp_trace("save/use: write");
	if (!prefs_write(envarc)) {
		tell("PanoramaPrefs", "Could not write the settings to ENV: or ENVARC:");
		return FALSE;
	}
	place_monitors(note);
	mp_trace("save/use: written");
	if (report[0]) {
		/* changes that the card only takes at boot: offer it */
		if (strstr(report, "reboot") && envarc) {
			if (ask("PanoramaPrefs", report, "_Reboot now|_Later"))
				mp_reboot();
		} else {
			tell("PanoramaPrefs", report);
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
	if (!ask("PanoramaPrefs: Install", report, "_Install|_Cancel"))
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
		if (ask("PanoramaPrefs: Install", report, "_Reboot now|_Later"))
			mp_reboot();
	} else {
		tell("PanoramaPrefs: Install", report);
	}
	status_show();
}

static void open_desktop(void)
{
	if (!collect())
		return;
	report[0] = 0;
	desktop_ready(note);
	{
		unsigned i;

		/* the switches as desktop_ready() left them (INPUT=) */
		for (i = 0; i < NFIELDS; i++)
			if (fields[i].kind == K_BOOL && fields[i].obj)
				set(fields[i].obj, MUIA_Switch_On,
				    prefs_value(find_setting(fields[i].key))[0] == '1');
	}
	prefs_fix(note);
	if (!prefs_write(FALSE)) {
		tell("PanoramaPrefs", "Could not write the settings to ENV:");
		return;
	}
	if (report[0])
		tell("PanoramaPrefs: Desktop", report);
	run_desktop("C:PanoramaDesk", TRUE);
}

/* ================================================================ the window */

/*
 * The menus: every command is here, the buttons are shortcuts to the same
 * IDs (ids.h). Labels go through tr() when the strip is made.
 */
#define M(title)          { NM_TITLE, (STRPTR)(title), 0, 0, 0, 0 }
#define I(label, key, id) { NM_ITEM, (STRPTR)(label), (STRPTR)(key), 0, 0, (APTR)(id) }
#define T(label, key, id) { NM_ITEM, (STRPTR)(label), (STRPTR)(key), CHECKIT | MENUTOGGLE, 0, \
                            (APTR)(id) }
#define BAR               { NM_ITEM, NM_BARLABEL, 0, 0, 0, 0 }
static struct NewMenu menus[] = {
	M("Project"),
	I("Save", "S", ID_SAVE), I("Use", "U", ID_USE), BAR,
	I("Install...", "I", ID_INSTALL), BAR,
	I("About...", "?", ID_ABOUT), BAR,
	I("Quit", "Q", ID_QUIT),
	M("Edit"),
	I("Reset To Defaults", "D", ID_DEFAULTS), I("Last Saved", "L", ID_LASTSAVED),
	I("Restore", "R", ID_RESTORE),
	M("Desktop"),
	I("Open Desktop", "O", ID_OPEN), I("Close Desktop", "K", ID_CLOSE), BAR,
	I("Refresh Status", "F", ID_REFRESH),
	M("View"),
	I("Desktop", "1", ID_PAGE0), I("Network", "2", ID_PAGE1), I("Display", "3", ID_PAGE2),
	I("Wrap.card", "4", ID_PAGE3), I("Status", "5", ID_PAGE4), BAR,
	T("Dark Theme", "T", ID_DARK),
	{ NM_END, 0, 0, 0, 0, 0 }
};
#undef M
#undef I
#undef T
#undef BAR
#define NMENUS (sizeof(menus) / sizeof(menus[0]))

static Object *strip, *bar, *b_close, *b_refresh;

/* the menu strip, its labels translated, the theme's item ticked to match */
static Object *make_menus(void)
{
	static struct NewMenu m[NMENUS];
	unsigned i;

	for (i = 0; i < NMENUS; i++) {
		m[i] = menus[i];
		if (m[i].nm_Label && m[i].nm_Label != NM_BARLABEL)
			m[i].nm_Label = (STRPTR)tr((const char *)m[i].nm_Label);
		if ((ULONG)m[i].nm_UserData == ID_DARK && theme_dark())
			m[i].nm_Flags |= CHECKED;
	}
	return MUI_MakeObject(MUIO_MenustripNM, (ULONG)m, 0);
}

static Object *make_window(int start, const char *pubscreen)
{
	Object *b_save, *b_use, *b_install, *b_cancel, *b_open, *b_add, *b_remove, *w;
	static const char *lw_help, *ls_help, *lc_help, *lb_help, *lm_help, *lwr_help;

	lw_help = keep(theme_bubble("Monitors", "How many viewer windows the desktop is split "
	                            "into, and how they sit: each is a part of one screen, sent "
	                            "to a port (and multicast group) of its own."));
	ls_help = keep(theme_bubble("Each monitor", "The size of each part. The desktop is that "
	                            "times the monitors."));
	lc_help = keep(theme_bubble("Colours", "The desktop's depth. 16 and 32 bit need "
	                            "cybergraphics.library."));
	lb_help = keep(theme_bubble("Workbench is", "Where the Workbench sits next to the desktop: "
	                            "the mouse crosses over at that edge. Click around the "
	                            "monitors in the picture to place it, too."));
	lm_help = keep(theme_bubble("Screen modes", "P96's settings (Picasso96Mode), or the sizes "
	                            "in the list, each in every depth."));
	lwr_help = keep(theme_bubble("Use Wrap.card", "Wrap only: just wrap the card (Keep its "
	                             "display on works then). Mirror: the same screen on another "
	                             "card. Span: one screen over 2-4 heads."));

	mp_trace("mui: making the controls");
	nav = NewObject(nav_cl->mcc_Class, NULL, MUIA_Nav_Active, start, MUIA_Background,
	                (ULONG)theme_panel_back(), TAG_DONE);
	map = NewObject(map_cl->mcc_Class, NULL, MUIA_ShortHelp, (ULONG)lb_help, TAG_DONE);
	ch_layout = MpChoice(layout_names, layout_sel, NULL, lw_help);
	ch_size = MpChoice(size_names, size_sel, NULL, ls_help);
	ch_colours = MpChoice(colour_names, colour_sel, NULL, lc_help);
	ch_wb = MpChoice(wb_labels, wb_sel, wb_valid, lb_help);
	ch_modesrc = MpChoice(modesrc_names, nmodes && prefs_get(find_setting("MODES")) ? 1 : 0,
	                    NULL, lm_help);
	ch_wrap = MpChoice(wrap_names, wrap_mode(), NULL, lwr_help);
	modes_rows = NewObject(rows_cl->mcc_Class, NULL, MUIA_Rows_Lines, (ULONG)mode_text,
	                       MUIA_Rows_Width, sizeof(mode_text[0]), MUIA_Rows_Count, nmodes,
	                       MUIA_Rows_Active, -1, MUIA_Rows_Visible, 5, TAG_DONE);
	nstat = status_lines(stat_text, NSTATUS);
	status_rows = NewObject(rows_cl->mcc_Class, NULL, MUIA_Rows_Lines, (ULONG)stat_text,
	                        MUIA_Rows_Width, sizeof(stat_text[0]), MUIA_Rows_Count, nstat,
	                        MUIA_Rows_Visible, 14, TAG_DONE);
	mode_entry = StringObject, StringFrame, MUIA_String_MaxLen, 12, MUIA_CycleChain, 1,
	             MUIA_String_Accept, (ULONG)"0123456789xX", End;

	mp_trace("mui: controls %s; making the pages", nav && map && ch_layout && ch_size &&
	         ch_colours && ch_wb && ch_modesrc && ch_wrap && modes_rows && status_rows &&
	         mode_entry ? "made" : "NOT all made");
	pages = VGroup, MUIA_Group_PageMode, TRUE, MUIA_Group_ActivePage, start,
		/* Desktop */
		Child, VGroup, MUIA_Group_VertSpacing, 3,
			Child, heading(PG_DESKTOP),
			Child, map,
			Child, ColGroup(2), MUIA_Group_VertSpacing, 3, MUIA_Group_HorizSpacing, 10,
				Child, MpLabel("Monitors", 0), Child, ch_layout,
				Child, MpLabel("Each monitor", 0), Child, ch_size,
				Child, MpLabel("Colours", 0), Child, ch_colours,
				Child, MpLabel("Workbench is", 0), Child, ch_wb,
			End,
			Child, switches(PG_DESKTOP),
		End,
		/* Network */
		Child, VGroup, MUIA_Group_VertSpacing, 3,
			Child, heading(PG_NETWORK),
			Child, form(PG_NETWORK, 2),
			Child, VSpace(2),
			Child, switches(PG_NETWORK),
			Child, VSpace(0),
		End,
		/* Display */
		Child, VGroup, MUIA_Group_VertSpacing, 3,
			Child, heading(PG_DISPLAY),
			Child, form(PG_DISPLAY, 1),
			Child, ColGroup(2), MUIA_Group_HorizSpacing, 10,
				Child, MpLabel("Screen modes", 0), Child, ch_modesrc,
			End,
			Child, modes_rows,
			Child, HGroup,
				Child, MpLabel("Size (WxH)", 0),
				Child, mode_entry,
				Child, b_add = MpButton("_Add", IC_ADD, FALSE),
				Child, b_remove = MpButton("_Remove", IC_REMOVE, FALSE),
			End,
		End,
		/* Wrap.card */
		Child, VGroup, MUIA_Group_VertSpacing, 3,
			Child, heading(PG_WRAP),
			Child, ColGroup(2), MUIA_Group_HorizSpacing, 10,
				Child, MpLabel("Use Wrap.card", 0), Child, ch_wrap,
			End,
			Child, form(PG_WRAP, 1),
			Child, VSpace(2),
			Child, switches(PG_WRAP),
			Child, VSpace(0),
		End,
		/* Status */
		Child, VGroup, MUIA_Group_VertSpacing, 3,
			Child, heading(PG_STATUS),
			Child, status_rows,
			Child, HGroup,
				Child, HSpace(0),
				Child, b_refresh = MpButton("Re_fresh", IC_REFRESH, FALSE),
			End,
		End,
	End;

	mp_trace("mui: pages %s; making the window", pages ? "made" : "NOT made");
	strip = make_menus();
	mp_trace("mui: menus %s", strip ? "made" : "NOT made");
	w = WindowObject,
		MUIA_Window_Title, (ULONG)"PanoramaPrefs " MP_VERSION,
		MUIA_Window_ID, MAKE_ID('M', 'C', 'P', 'R'),
		MUIA_Window_Menustrip, (ULONG)strip,
		(ULONG)(pubscreen ? MUIA_Window_PublicScreen : TAG_IGNORE), (ULONG)pubscreen,
		WindowContents, root = VGroup, MUIA_Background, (ULONG)theme_back(), MUIA_InnerLeft, 0,
		                MUIA_InnerRight, 0, MUIA_InnerTop, 0, MUIA_InnerBottom, 0,
		                MUIA_Group_Spacing, 0,
			Child, HGroup, MUIA_Group_Spacing, 0,
				Child, nav,
				Child, VGroup, MUIA_InnerLeft, 12, MUIA_InnerRight, 12, MUIA_InnerTop, 6,
				               MUIA_InnerBottom, 6,
					Child, pages,
				End,
			End,
			Child, bar = HGroup, MUIA_Background, (ULONG)theme_panel_back(), MUIA_InnerLeft, 8,
			               MUIA_InnerRight, 8, MUIA_InnerTop, 5, MUIA_InnerBottom, 5,
				Child, b_install = MpButton("_Install", IC_INSTALL, FALSE),
				Child, b_open = MpButton("_Open", IC_OPEN, FALSE),
				Child, b_close = MpButton("C_lose", IC_CLOSE, FALSE),
				Child, HSpace(0),
				Child, b_cancel = MpButton("_Cancel", IC_NONE, FALSE),
				Child, b_use = MpButton("_Use", IC_USE, FALSE),
				Child, b_save = MpButton("_Save", IC_SAVE, TRUE),
			End,
		End,
	End;
	if (!w)
		return NULL;

#define RET(o, attr, val, id) DoMethod(o, MUIM_Notify, attr, val, (ULONG)app, 2, \
                                       MUIM_Application_ReturnID, id)
	RET(w, MUIA_Window_CloseRequest, TRUE, ID_QUIT);
	RET(b_save, MUIA_Pressed, FALSE, ID_SAVE);
	RET(b_use, MUIA_Pressed, FALSE, ID_USE);
	RET(b_install, MUIA_Pressed, FALSE, ID_INSTALL);
	RET(b_cancel, MUIA_Pressed, FALSE, ID_QUIT);
	RET(b_open, MUIA_Pressed, FALSE, ID_OPEN);
	RET(b_close, MUIA_Pressed, FALSE, ID_CLOSE);
	RET(b_add, MUIA_Pressed, FALSE, ID_ADDMODE);
	RET(b_remove, MUIA_Pressed, FALSE, ID_REMOVEMODE);
	RET(b_refresh, MUIA_Pressed, FALSE, ID_REFRESH);
	RET(ch_layout, MUIA_Choice_Active, MUIV_EveryTime, ID_LAYOUT);
	RET(ch_size, MUIA_Choice_Active, MUIV_EveryTime, ID_SIZE);
	RET(ch_wb, MUIA_Choice_Active, MUIV_EveryTime, ID_WB);
	RET(map, MUIA_Map_Clicked, MUIV_EveryTime, ID_MAP);
	RET(nav, MUIA_Nav_Active, MUIV_EveryTime, ID_PAGE);
	DoMethod(nav, MUIM_Notify, MUIA_Nav_Active, MUIV_EveryTime, (ULONG)pages, 3, MUIM_Set,
	         MUIA_Group_ActivePage, MUIV_TriggerValue);
#undef RET
	return w;
}

/* ================================================================ commands */

/* the picture of the monitors drawn again, if its page is the one shown */
static void map_redraw(void)
{
	if (map && SHOWN(map_cl->mcc_Class, map))
		MUI_Redraw(map, MADF_DRAWOBJECT);
}

/* every control set from the settings (after Reset To Defaults, Last Saved, Restore) */
static void refresh_controls(void)
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
			nnset(f->obj, MUIA_Switch_On, v[0] == '1');
		else
			nnset(f->obj, MUIA_String_Contents, (ULONG)v);
	}
	nnset(ch_layout, MUIA_Choice_Active, layout_sel);
	nnset(ch_size, MUIA_Choice_Active, size_sel);
	nnset(ch_colours, MUIA_Choice_Active, colour_sel);
	nnset(ch_wb, MUIA_Choice_Active, wb_sel);
	nnset(ch_modesrc, MUIA_Choice_Active, nmodes && prefs_get(find_setting("MODES")) ? 1 : 0);
	nnset(ch_wrap, MUIA_Choice_Active, wrap_mode());
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

/*
 * Whether a command can run now: one answer for the menus, the buttons and
 * command() itself (CMD= and the Amiga-key shortcuts don't go through the
 * menus), so they never disagree.
 */
static BOOL command_enabled(ULONG id)
{
	LONG page = nav ? (LONG)xget(nav, MUIA_Nav_Active) : 0;

	switch (id) {
	case ID_CLOSE:
		return desk_running();
	case ID_REFRESH:
		return page == PG_STATUS;
	case ID_PAGE0: case ID_PAGE1: case ID_PAGE2: case ID_PAGE3: case ID_PAGE4:
		return (LONG)(id - ID_PAGE0) != page;
	}
	return TRUE;
}

/* a menu item on or off, if it isn't already */
static void menu_enable(ULONG id, BOOL on)
{
	Object *item = strip ? (Object *)DoMethod(strip, MUIM_FindUData, id) : NULL;

	if (item && (xget(item, MUIA_Menuitem_Enabled) != 0) != on)
		set(item, MUIA_Menuitem_Enabled, on);
}

/* the button for a command dimmed or not, if it isn't already */
static void button_enable(Object *b, ULONG id)
{
	BOOL on = command_enabled(id);

	if (b && (xget(b, MUIA_Disabled) != 0) == on)
		set(b, MUIA_Disabled, !on);
}

/* menu items and buttons that depend on the state: after every command, and now and then */
static void update_menus(void)
{
	static const ULONG ids[] = {
		ID_CLOSE, ID_REFRESH, ID_PAGE0, ID_PAGE1, ID_PAGE2, ID_PAGE3, ID_PAGE4
	};
	unsigned i;

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
		menu_enable(ids[i], command_enabled(ids[i]));
	button_enable(b_close, ID_CLOSE);
	button_enable(b_refresh, ID_REFRESH);
}

/* PROGDIR:PanoramaPrefs.prefs: the window's own preferences (the theme) */
static void own_prefs(BOOL write)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin = me->pr_WindowPtr;
	char line[40];
	BPTR fh;

	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)"PROGDIR:PanoramaPrefs.prefs", write ? MODE_NEWFILE : MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return;
	if (write) {
		FPuts(fh, (STRPTR)(theme_dark() ? "theme=dark\n" : "theme=light\n"));
	} else {
		while (FGets(fh, (STRPTR)line, sizeof(line)))
			if (!strncmp(line, "theme=", 6))
				theme_set_dark(!strncmp(line + 6, "dark", 4));
	}
	Close(fh);
}

/* light or dark: new pens, the backgrounds in the new colours, everything drawn again */
static void switch_theme(BOOL dark)
{
	theme_set_dark(dark);
	set(root, MUIA_Background, (ULONG)theme_back());
	set(nav, MUIA_Background, (ULONG)theme_panel_back());
	set(bar, MUIA_Background, (ULONG)theme_panel_back());
	redraw_window();
	own_prefs(TRUE);
}

static void about(void)
{
	char text[400];

	snprintf(text, sizeof(text), "%s %s (%s)\n\n%s\n\n%s", "PanoramaPrefs", MP_VERSION, MP_DATE,
	         tr("Settings for Panorama.card, Wrap.card and the Panorama desktop: one Amiga "
	            "screen shown in viewer windows on other computers, over the network."),
	         tr("Panorama.card's protocol is Copper (Docs/COPPER.md)."));
	tell("About PanoramaPrefs", text);
}

/*
 * Every command, from the menus, the buttons, the controls or CMD=; TRUE:
 * the window is done.
 */
static BOOL command(ULONG id)
{
	if (!command_enabled(id))
		return FALSE;
	switch (id) {
	case MUIV_Application_ReturnID_Quit:
	case ID_QUIT:
		return TRUE;
	case ID_SAVE:
	case ID_USE:
		return save(id == ID_SAVE);
	case ID_INSTALL:
		do_install();
		break;
	case ID_ABOUT:
		about();
		break;
	case ID_DEFAULTS:
		prefs_defaults();
		refresh_controls();
		mp_trace("defaults: SPLIT %s, controls set", prefs_value(find_setting("SPLIT")));
		break;
	case ID_LASTSAVED:
		prefs_load_from("ENVARC:");
		refresh_controls();
		break;
	case ID_RESTORE:
		prefs_load();
		refresh_controls();
		break;
	case ID_OPEN:
		open_desktop();
		break;
	case ID_CLOSE:
		run_desktop("C:PanoramaDesk CLOSE", FALSE);
		break;
	case ID_PAGE0: case ID_PAGE1: case ID_PAGE2: case ID_PAGE3: case ID_PAGE4:
		set(nav, MUIA_Nav_Active, id - ID_PAGE0);
		break;
	case ID_DARK:
		switch_theme(!theme_dark());
		break;
	case ID_LAYOUT: {
		int side = wb_side[wb_sel], mon = wb_mon[wb_sel];

		layout_sel = xget(ch_layout, MUIA_Choice_Active);
		wb_places(side, mon);
		set(ch_wb, MUIA_Choice_Active, wb_sel);
		map_redraw();
		break;
	}
	case ID_SIZE:
		size_sel = xget(ch_size, MUIA_Choice_Active);
		map_redraw();
		break;
	case ID_WB:
		wb_sel = xget(ch_wb, MUIA_Choice_Active);
		map_redraw();
		break;
	case ID_MAP:
		/* the picture moved the Workbench: the choice follows (no notify loop: same value) */
		set(ch_wb, MUIA_Choice_Active, wb_sel);
		break;
	case ID_ADDMODE:
		add_mode();
		break;
	case ID_REMOVEMODE:
		remove_mode();
		break;
	case ID_PAGE:
	case ID_REFRESH:
		mp_trace("page %ld", (LONG)xget(nav, MUIA_Nav_Active));
		if (xget(nav, MUIA_Nav_Active) == PG_STATUS)
			status_show();
		break;
	}
	return FALSE;
}

/* ================================================================ the loop */

static int window_loop(int start, int timeout, const char *pubscreen, ULONG cmd);

/* (bigstack.s) fn(arg) on the stack ss describes */
long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg);

struct GuiArgs {
	int start, timeout;
	const char *pubscreen;
	ULONG cmd;
};

static long gui_on_stack(void *a)
{
	struct GuiArgs *g = a;

	return window_loop(g->start, g->timeout, g->pubscreen, g->cmd);
}

/*
 * PanoramaPrefs' window in MUI; -1 if MUI (muimaster.library 19) isn't
 * there, for the GadTools one instead. MUI wants a big stack: one of 64 KB
 * is swapped in when the Shell gave less than 32 KB. cmd: a command (ids.h)
 * run once the window is open (CMD=, for tests), 0 none.
 */
int mui_gui(int start, int timeout, const char *pubscreen, ULONG cmd)
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
	mp_trace("mui: stack %lu bytes", have);
	if (have >= 32768 || !(stack = AllocVec(65536, MEMF_ANY)))
		return window_loop(start, timeout, pubscreen, cmd);
	ss.stk_Lower = stack;
	ss.stk_Upper = (ULONG)stack + 65536;
	ss.stk_Pointer = (APTR)ss.stk_Upper;
	rc = run_on_stack(&ss, gui_on_stack, &g);
	FreeVec(stack);
	return rc;
}

static int window_loop(int start, int timeout, const char *pubscreen, ULONG cmd)
{
	struct MsgPort *tport = NULL;
	struct timerequest *treq = NULL;
	ULONG sigs = 0, ticks = 0;
	BOOL done = FALSE, timer = FALSE;
	int rc = 0;

	MUIMasterBase = OpenLibrary(MUIMASTER_NAME, 19);
	mp_trace("mui: muimaster.library %ld.%ld (%s)", MUIMasterBase ? (LONG)MUIMasterBase->lib_Version : 0L,
	         MUIMasterBase ? (LONG)MUIMasterBase->lib_Revision : 0L,
	         MUIMasterBase && MUIMasterBase->lib_IdString ? (char *)MUIMasterBase->lib_IdString : "-");
	if (!MUIMasterBase)
		return -1;
	if (!IntuitionBase)
		IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	if (!GfxBase)
		GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	if (!UtilityBase)
		UtilityBase = OpenLibrary("utility.library", 37);
	if (start < 0 || start >= NPAGES)
		start = 0;
	lang_init();
	own_prefs(FALSE);
	from_settings();
	mp_trace("mui: settings read");
	if (!classes_make()) {
		printf("%s\n", tr("PanoramaPrefs: MUI couldn't make its classes"));
		rc = -1;
		goto out;
	}
	app = ApplicationObject,
		MUIA_Application_Title, (ULONG)"PanoramaPrefs",
		MUIA_Application_Version, (ULONG)"$VER: PanoramaPrefs " MP_VERSION " (" MP_DATE ")",
		MUIA_Application_Description, (ULONG)tr("Settings for Panorama.card"),
		MUIA_Application_Base, (ULONG)"PANORAMAPREFS",
	End;
	mp_trace("mui: classes made, application %s", app ? "made" : "NOT made");
	if (!app || !(win = make_window(start, pubscreen))) {
		printf("%s\n", tr("PanoramaPrefs: the window could not be made"));
		rc = RETURN_FAIL;
		goto out;
	}
	mp_trace("mui: window made");
	DoMethod(app, OM_ADDMEMBER, (ULONG)win);
	set(win, MUIA_Window_Open, TRUE);
	mp_trace("mui: window %s (MUI_Error %ld)", xget(win, MUIA_Window_Open) ? "open" : "NOT open",
	         MUI_Error());
	if (!xget(win, MUIA_Window_Open)) {
		printf("%s\n", tr("PanoramaPrefs: the window could not be opened"));
		rc = RETURN_FAIL;
		goto out;
	}
	update_menus();

	/* a tick every half second: TIMEOUT=, the menus and the Status page kept up to date */
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
			mp_trace("id %lu", id);
			done = command(id);
			update_menus();
		}
		if (done || !sigs)
			continue;
		sigs = Wait(sigs | SIGBREAKF_CTRL_C | (timer ? 1UL << tport->mp_SigBit : 0));
		if (sigs & SIGBREAKF_CTRL_C)
			done = TRUE;
		if (timer && CheckIO((struct IORequest *)treq)) {
			WaitIO((struct IORequest *)treq);
			ticks++;
			if (timeout && ticks >= (ULONG)timeout * 2)
				done = TRUE;
			if (ticks % 4 == 0) {
				if (xget(nav, MUIA_Nav_Active) == PG_STATUS)
					status_show();
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
	if (app)
		MUI_DisposeObject(app);
	app = win = root = strip = bar = b_close = b_refresh = NULL;
	nav = map = NULL;
	classes_free();
	CloseLibrary(MUIMasterBase);
	MUIMasterBase = NULL;
	return rc;
}
