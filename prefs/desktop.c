/*
 * Panorama - PanoramaDesk: one screen over several Panorama windows.
 *
 * With SPLIT=CxR, Panorama.card sends its screen in C x R parts, each to
 * a port of its own, and panoview.py --span shows each part in a window of
 * its own: one desktop over several "monitors". PanoramaDesk opens that
 * desktop, a public screen of C x R times MONITOR=, on the Panorama board,
 * and keeps it open. It is a commodity, with hotkeys (HOTKEYS=, ctrl+alt by
 * default) for the active window:
 *
 *   ctrl alt cursor keys    to the monitor left, right, above, below,
 *                           at the same place on it
 *   ctrl alt 1 ... 4        to the middle of monitor 1 ... 4
 *   ctrl alt m              the desktop in front, or the Workbench again
 *
 * WORKBENCH= says where the Workbench is, next to the desktop: LEFT1 is
 * left of monitor 1, BELOW2 below monitor 2, and so on. Pushing the mouse
 * over that edge crosses to the Workbench: it comes to the front, the
 * pointer goes to the matching place on its edge, and the window last
 * active there is active again; over the Workbench's edge back, the same
 * for the desktop. WORKBENCH=NONE: the hotkey only.
 *
 * Exchange can hide, disable and quit it. Quitting closes the desktop once
 * no windows are left on it.
 *
 *   PanoramaDesk                    the commodity (also from WBStartup)
 *   PanoramaDesk MOVE=RIGHT         the active window one monitor right;
 *                                       LEFT, UP, DOWN too
 *   PanoramaDesk CENTRE=2           ... to the middle of monitor 2
 *   PanoramaDesk SWAP               the desktop or the Workbench in front
 *   PanoramaDesk CLOSE              tells the running one to quit
 *   PanoramaDesk SHELL              a Shell in the middle of the desktop
 *   PanoramaDesk INFO               every public screen: size, mode, owner
 *   ... WAIT=10                         wait up to 10 s for the desktop to open
 *   ... WINDOW=title                    a window by its title, not the active one
 *
 * The settings are PanoramaPrefs': SPLIT, MONITOR, COLOURS, PUBSCREEN,
 * DEFAULTSCREEN, BEHIND, WORKBENCH and HOTKEYS in ENV:Panorama.prefs.
 */
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/displayinfo.h>
#include <graphics/layers.h>
#include <devices/inputevent.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/commodities.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"
#include "copper.h"
#include "lang.h"

#define MD_VERSION "1.8"
#define PORTNAME "PanoramaDesk"

static const char version[] = "$VER: PanoramaDesk " MD_VERSION " (" MP_DATE ")";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *CxBase;
struct Library *LayersBase;

static const char template[] = "MOVE/K,CENTRE=CENTER/K/N,SWAP/S,CLOSE/S,WINDOW/K,SHELL/S,WAIT/K/N,INFO/S";
enum { A_MOVE, A_CENTRE, A_SWAP, A_CLOSE, A_WINDOW, A_SHELL, A_WAIT, A_INFO, A_COUNT };

/* the desktop: C x R monitors of MW x MH */
static UWORD cols = 1, rows = 1, mw = 800, mh = 600, depth = 16;
static char pubname[MAXPUBSCREENNAME + 1] = "PANORAMA";
static int wb_side = WB_LEFT, wb_mon;     /* WORKBENCH=: where the Workbench is */
static BOOL from_wb;

/* ---------------------------------------------------------------- settings */

static BOOL size_of(const char *v, UWORD *w, UWORD *h)
{
	UWORD a = 0, b = 0;

	while (*v >= '0' && *v <= '9')
		a = a * 10 + *v++ - '0';
	if (*v != 'x' && *v != 'X')
		return FALSE;
	v++;
	while (*v >= '0' && *v <= '9')
		b = b * 10 + *v++ - '0';
	if (!a || !b)
		return FALSE;
	*w = a;
	*h = b;
	return TRUE;
}

static void read_settings(void)
{
	const char *v;

	prefs_load();
	if (!size_of(prefs_value(find_setting("SPLIT")), &cols, &rows))
		cols = rows = 1;
	size_of(prefs_value(find_setting("MONITOR")), &mw, &mh);
	v = prefs_value(find_setting("COLOURS"));
	depth = v[0] == '8' ? 8 : v[0] == '3' || v[0] == '2' ? 24 : 16;
	v = prefs_value(find_setting("PUBSCREEN"));
	if (*v) {
		strncpy(pubname, v, MAXPUBSCREENNAME);
		pubname[MAXPUBSCREENNAME] = 0;
	}
	if (!wb_place(prefs_value(find_setting("WORKBENCH")), &wb_side, &wb_mon))
		wb_side = WB_NONE;
	wb_fit(&wb_side, &wb_mon, cols, rows);
}

static BOOL setting_on(const char *key)
{
	return prefs_value(find_setting(key))[0] == '1';
}

/* ---------------------------------------------------------------- messages */

static void say(const char *text)
{
	text = tr(text);
	/* from Workbench, or run with its output nowhere to be seen: a requester */
	if ((from_wb || !Output() || !IsInteractive(Output())) && IntuitionBase) {
		struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"PanoramaDesk",
		                         (UBYTE *)"%s", (UBYTE *)"OK" };
		ULONG args[1];

		args[0] = (ULONG)text;
		es.es_GadgetFormat = (UBYTE *)tr("OK");
		EasyRequestArgs(NULL, &es, NULL, args);
	} else {
		printf("PanoramaDesk: %s\n", text);
	}
}

/* a line for the Shell, when there is one to see it */
static void tell(const char *text)
{
	if (!from_wb && Output() && IsInteractive(Output()))
		printf("PanoramaDesk: %s\n", text);
}

/* "1600x600, mode 50de1100 Panorama:1600x600 16bit PC" */
static void describe(struct Screen *scr, char *buf, int len)
{
	ULONG id = GetVPModeID(&scr->ViewPort);
	struct NameInfo ni;

	if (id == INVALID_ID || !GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, id))
		ni.Name[0] = 0;
	snprintf(buf, len, "%dx%d, mode %08lx %s", scr->Width, scr->Height, id,
	         ni.Name[0] ? (char *)ni.Name : "(no name)");
}

/* ---------------------------------------------------------------- the screen */

/* a Panorama mode of this size with at least this depth */
static ULONG find_mode(UWORD w, UWORD h, UWORD d)
{
	ULONG id = INVALID_ID;
	struct NameInfo ni;
	struct DimensionInfo di;

	while ((id = NextDisplayInfo(id)) != INVALID_ID) {
		char *m;

		if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, id) ||
		    !(m = strstr((char *)ni.Name, "Panorama")))
			continue;
		m += 8;
		/* the first board: not Panorama2 ... */
		if (*m >= '0' && *m <= '9')
			continue;
		if (!GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di), DTAG_DIMS, id))
			continue;
		if (di.Nominal.MaxX - di.Nominal.MinX + 1 == w &&
		    di.Nominal.MaxY - di.Nominal.MinY + 1 == h && di.MaxDepth >= d)
			return id;
	}
	return INVALID_ID;
}

static struct Screen *open_desktop(BOOL *ours)
{
	struct Screen *scr;
	ULONG id, err = 0;
	UWORD w = cols * mw, h = rows * mh;
	char buf[160];

	*ours = FALSE;
	if ((scr = LockPubScreen((STRPTR)pubname)) != NULL) {
		/* open already: use it (it stays locked while we run) */
		describe(scr, buf, sizeof(buf));
		tell("a public screen of that name is open already, using it:");
		tell(buf);
		if (!strstr(buf, "Panorama"))
			say("The public screen PANORAMA is open already, but not on the Panorama "
			    "board: close it (or the program that opened it) and start PanoramaDesk "
			    "again.");
		return scr;
	}

	id = find_mode(w, h, depth);
	if (id == INVALID_ID) {
		snprintf(buf, sizeof(buf),
		         "No Panorama mode %ux%u with %u bit.\n"
		         "PanoramaPrefs' Desktop page sets the modes up (then reboot).", w, h, depth);
		say(buf);
		return NULL;
	}
	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
	                     SA_PubName, (ULONG)pubname, SA_Title, (ULONG)"Panorama desktop",
	                     SA_LikeWorkbench, TRUE, SA_SharePens, TRUE, SA_Type, PUBLICSCREEN,
	                     SA_Behind, setting_on("BEHIND"), SA_ErrorCode, (ULONG)&err, TAG_END);
	if (!scr) {
		snprintf(buf, sizeof(buf), "Cannot open the %ux%u desktop (error %lu).", w, h, err);
		say(buf);
		return NULL;
	}
	PubScreenStatus(scr, 0);
	*ours = TRUE;
	describe(scr, buf, sizeof(buf));
	tell("the desktop is open:");
	tell(buf);
	return scr;
}

/* ---------------------------------------------------------------- the moves */

static struct Window *target(struct Screen *scr, const char *title)
{
	struct Window *w;

	if (title) {
		for (w = scr->FirstWindow; w; w = w->NextWindow)
			if (w->Title && !strcmp((char *)w->Title, title))
				return w;
		return NULL;
	}
	w = IntuitionBase->ActiveWindow;
	return w && w->WScreen == scr ? w : NULL;
}

/* one monitor over, at the same place on it; only as far as the desktop goes */
static void move_by(struct Screen *scr, struct Window *w, WORD dx, WORD dy)
{
	WORD mwid = scr->Width / cols, mhei = scr->Height / rows;
	WORD x = w->LeftEdge + dx * mwid, y = w->TopEdge + dy * mhei;

	if (x < 0 || y < 0 || x >= scr->Width || y >= scr->Height)
		return;
	if (x + w->Width > scr->Width)
		x = scr->Width - w->Width;
	if (y + w->Height > scr->Height)
		y = scr->Height - w->Height;
	MoveWindow(w, x - w->LeftEdge, y - w->TopEdge);
}

/* the middle of monitor n (1 ...), or its top left if it doesn't fit */
static void centre(struct Screen *scr, struct Window *w, UWORD n)
{
	WORD mwid = scr->Width / cols, mhei = scr->Height / rows;
	WORD x, y;

	if (n < 1 || n > cols * rows)
		return;
	n--;
	x = (n % cols) * mwid + (w->Width < mwid ? (mwid - w->Width) / 2 : 0);
	y = (n / cols) * mhei + (w->Height < mhei ? (mhei - w->Height) / 2 : 0);
	if (x + w->Width > scr->Width)
		x = scr->Width - w->Width;
	if (y + w->Height > scr->Height)
		y = scr->Height - w->Height;
	MoveWindow(w, x - w->LeftEdge, y - w->TopEdge);
}

static void swap(struct Screen *scr)
{
	if (IntuitionBase->FirstScreen == scr) {
		ScreenToBack(scr);
		WBenchToFront();
	} else {
		ScreenToFront(scr);
	}
}

/* ---------------------------------------------------------------- crossing over */

/* the window last active on the desktop, and on the Workbench */
static struct Window *last_desk, *last_wb;
static int pushes;

/*
 * Whose the desktop in front is. The Amiga's own display never shows it
 * (the viewers do), but shows the Workbench beside it: the Amiga's own
 * mouse (an emulator's, in its window) works the Workbench, unless it
 * crossed over to the desktop itself (here, with P96ScreenCx, or the
 * hotkey). The viewers' mouse (Panorama.card's events) works the desktop.
 */
enum { OWN_VIEWERS, OWN_LOCAL };
static int desk_owner = OWN_VIEWERS;
static struct Screen *seen_front;
static int own_moves;             /* NEWPOINTERPOS events of our own (cross()) still to come */

/*
 * A window on scr made active: w if it is still there, else one at x, y,
 * else one that isn't a backdrop, else the first. From Intuition's list of
 * the screen's windows, under its lock: not from the layers, some of which
 * (the title bar's, menus') belong to no window.
 */
static void activate_on(struct Screen *scr, struct Window *w, WORD x, WORD y)
{
	struct Window *x1, *at = NULL, *front = NULL, *found = NULL;
	ULONG lock = LockIBase(0);

	for (x1 = scr->FirstWindow; x1; x1 = x1->NextWindow) {
		if (x1 == w)
			found = w;
		if (x1->Flags & WFLG_BACKDROP)
			continue;
		if (!front)
			front = x1;
		if (!at && x >= x1->LeftEdge && x < x1->LeftEdge + x1->Width &&
		    y >= x1->TopEdge && y < x1->TopEdge + x1->Height)
			at = x1;
	}
	if (!found)
		found = at ? at : front ? front : scr->FirstWindow;
	UnlockIBase(lock);
	if (found)
		ActivateWindow(found);
}

/*
 * The keys go where the mouse went: when the screen in front changes
 * (crossing over here or with P96ScreenCx, a viewer's mouse, a depth
 * gadget) to the desktop or the Workbench, and the active window is on
 * another screen, the window last active on that one is active again (else
 * the one under the pointer, else the frontmost). Not while a button is
 * held: that is a drag, or a menu.
 */
static void follow_focus(struct Screen *desk, UWORD qual)
{
	static struct Screen *last_front;
	struct Screen *front = IntuitionBase->FirstScreen;
	struct Window *act = IntuitionBase->ActiveWindow;
	BOOL wb;

	if (act && act->WScreen == desk)
		last_desk = act;
	else if (act && (act->WScreen->Flags & SCREENTYPE) == WBENCHSCREEN)
		last_wb = act;
	if (front == last_front ||
	    (qual & (IEQUALIFIER_LEFTBUTTON | IEQUALIFIER_RBUTTON | IEQUALIFIER_MIDBUTTON)))
		return;
	last_front = front;
	if (!front || (act && act->WScreen == front))
		return;
	wb = (front->Flags & SCREENTYPE) == WBENCHSCREEN;
	if (front == desk || wb)
		activate_on(front, wb ? last_wb : last_desk, front->MouseX, front->MouseY);
}

/*
 * To the Workbench (to_wb) or the desktop: in front, the pointer at frac
 * (0-65535) along the edge they share, and a window there active again.
 */
static void cross(struct Screen *desk, BOOL to_wb, ULONG frac)
{
	static struct IEPointerPixel pp;
	static struct InputEvent ie;
	struct Screen *wb = LockPubScreen((STRPTR)"Workbench"), *to;
	struct Window *act = IntuitionBase->ActiveWindow, *w;
	WORD mwid = desk->Width / cols, mhei = desk->Height / rows;
	WORD mx0 = (wb_mon % cols) * mwid, my0 = (wb_mon / cols) * mhei;
	WORD x, y;

	if (!wb)
		return;
	to = to_wb ? wb : desk;
	if (act && act->WScreen == (to_wb ? desk : wb)) {
		if (to_wb)
			last_desk = act;
		else
			last_wb = act;
	}
	if (to_wb) {
		x = wb_side == WB_LEFT ? wb->Width - 3 : wb_side == WB_RIGHT ? 2 :
		    (WORD)((frac * wb->Width) >> 16);
		y = wb_side == WB_ABOVE ? wb->Height - 3 : wb_side == WB_BELOW ? 2 :
		    (WORD)((frac * wb->Height) >> 16);
	} else {
		x = wb_side == WB_LEFT ? 2 : wb_side == WB_RIGHT ? desk->Width - 3 :
		    mx0 + (WORD)((frac * mwid) >> 16);
		y = wb_side == WB_ABOVE ? 2 : wb_side == WB_BELOW ? desk->Height - 3 :
		    my0 + (WORD)((frac * mhei) >> 16);
	}
	ScreenToFront(to);

	/* the pointer across, as an event, as a tablet would */
	pp.iepp_Screen = to;
	pp.iepp_Position.X = x;
	pp.iepp_Position.Y = y;
	memset(&ie, 0, sizeof(ie));
	ie.ie_Class = IECLASS_NEWPOINTERPOS;
	ie.ie_SubClass = IESUBCLASS_PIXEL;
	ie.ie_Code = IECODE_NOBUTTON;
	ie.ie_EventAddress = (APTR)&pp;
	own_moves++;
	if (!to_wb)
		desk_owner = OWN_LOCAL;
	AddIEvents(&ie);

	/* the window last active there, else the one under the pointer, else the frontmost */
	w = to_wb ? last_wb : last_desk;
	activate_on(to, w, x, y);
	UnlockPubScreen(NULL, wb);
}

/*
 * P96ScreenCx (Aminet util/cdity/P96ScreenCx) moves the mouse between the
 * boards itself, from the monitors' tool types PanoramaPrefs writes: if it
 * runs, it crosses over first (this does if it hasn't by the fourth push).
 * A commodity of that name can't be made twice, which tells.
 */
static BOOL p96screencx_runs(void)
{
	struct NewBroker nb;
	CxObj *b;
	LONG err = 0;

	memset(&nb, 0, sizeof(nb));
	nb.nb_Version = NB_VERSION;
	nb.nb_Name = (STRPTR)"P96ScreenCx";
	nb.nb_Title = (STRPTR)"P96ScreenCx";
	nb.nb_Descr = (STRPTR)"(PanoramaDesk checking)";
	nb.nb_Unique = NBU_UNIQUE;
	b = CxBroker(&nb, &err);
	if (b) {
		DeleteCxObj(b);
		return FALSE;
	}
	return err == CBERR_DUP;
}

/*
 * A mouse move (dx, dy): pushing over the edge the desktop shares with the
 * Workbench, twice in a row, crosses over.
 */
static void edge(struct Screen *desk, WORD dx, WORD dy)
{
	struct Screen *front = IntuitionBase->FirstScreen;
	WORD mwid = desk->Width / cols, mhei = desk->Height / rows;
	WORD mx0 = (wb_mon % cols) * mwid, my0 = (wb_mon / cols) * mhei;
	WORD mx, my;
	BOOL over = FALSE, to_wb;
	ULONG frac = 0;

	if (wb_side == WB_NONE || !front || (!dx && !dy))
		return;
	mx = front->MouseX;
	my = front->MouseY;
	if (front == desk) {
		to_wb = TRUE;
		if (wb_side == WB_LEFT || wb_side == WB_RIGHT) {
			over = (wb_side == WB_LEFT ? mx <= 0 && dx < 0 : mx >= desk->Width - 1 && dx > 0) &&
			       my >= my0 && my < my0 + mhei;
			frac = ((ULONG)(my - my0) << 16) / mhei;
		} else {
			over = (wb_side == WB_ABOVE ? my <= 0 && dy < 0 : my >= desk->Height - 1 && dy > 0) &&
			       mx >= mx0 && mx < mx0 + mwid;
			frac = ((ULONG)(mx - mx0) << 16) / mwid;
		}
	} else if ((front->Flags & SCREENTYPE) == WBENCHSCREEN) {
		to_wb = FALSE;
		if (wb_side == WB_LEFT || wb_side == WB_RIGHT) {
			over = wb_side == WB_LEFT ? mx >= front->Width - 1 && dx > 0 : mx <= 0 && dx < 0;
			frac = my >= 0 ? ((ULONG)my << 16) / front->Height : 0;
		} else {
			over = wb_side == WB_ABOVE ? my >= front->Height - 1 && dy > 0 : my <= 0 && dy < 0;
			frac = mx >= 0 ? ((ULONG)mx << 16) / front->Width : 0;
		}
	}
	if (!over) {
		pushes = 0;
		return;
	}
	if (++pushes < 2)
		return;
	/* P96ScreenCx's to do, if it runs; if it hasn't by the fourth push, ours */
	if (pushes < 4 && p96screencx_runs())
		return;
	pushes = 0;
	if (frac > 65535)
		frac = 65535;
	cross(desk, to_wb, frac);
}

/*
 * A mouse event, copied to us: from the viewers (Panorama.card's) or the
 * Amiga's own? The Amiga's own moving while the desktop is in front, and
 * the desktop not its own: the Workbench to the front, and its window
 * active, before a click lands on the desktop nobody here sees.
 */
static void whose_mouse(struct Screen *desk, struct InputEvent *ie)
{
	struct Screen *front = IntuitionBase->FirstScreen, *wb;
	BOOL local, moving;

	if (ie->ie_Class == IECLASS_NEWPOINTERPOS) {
		if (own_moves > 0) {
			own_moves--;
			seen_front = front;
			return;
		}
		local = ie->ie_SubClass != IESUBCLASS_PIXEL;
		moving = TRUE;
	} else {
		local = ie->ie_SubClass != CU_IN_TAG;
		moving = ie->ie_X || ie->ie_Y ||
		         (ie->ie_Code != IECODE_NOBUTTON && !(ie->ie_Code & IECODE_UP_PREFIX));
	}
	if (front != seen_front) {
		/* the front changed: by whose mouse? */
		seen_front = front;
		if (front == desk)
			desk_owner = local ? OWN_LOCAL : OWN_VIEWERS;
	} else if (!local) {
		desk_owner = OWN_VIEWERS;
	}
	if (!local || !moving || front != desk || desk_owner == OWN_LOCAL)
		return;
	wb = LockPubScreen((STRPTR)"Workbench");
	if (!wb)
		return;
	ScreenToFront(wb);
	seen_front = wb;
	activate_on(wb, last_wb, wb->MouseX, wb->MouseY);
	UnlockPubScreen(NULL, wb);
}

/* ---------------------------------------------------------------- the commodity */

#define HK_LEFT   1
#define HK_RIGHT  2
#define HK_UP     3
#define HK_DOWN   4
#define HK_SWAP   5
#define HK_EDGE   6     /* a mouse move: over an edge? the keys after the screen? */
#define HK_PTR    7     /* the pointer put somewhere (a viewer's mouse): the keys after it */
#define HK_MON    10    /* + n: monitor n */

static BOOL add_hotkey(CxObj *broker, struct MsgPort *port, const char *quals, const char *key,
                       LONG id)
{
	char desc[64];
	CxObj *h;
	int i;

	snprintf(desc, sizeof(desc), "%s %s", quals, key);
	for (i = 0; desc[i]; i++)
		if (desc[i] == '+')
			desc[i] = ' ';
	h = HotKey((STRPTR)desc, port, id);
	if (!h)
		return FALSE;
	AttachCxObj(broker, h);
	return CxObjError(h) == 0;
}

/*
 * ---------------------------------------------------------------- moving programs to the Workbench
 *
 * A window cannot change screens, but a commodity closes its windows on
 * Exchange's Hide and opens them again on Show: on the default public
 * screen, which is the Workbench again by then. Every MUI program with
 * MUIA_Application_UseCommodities (the default) does this. Exchange's own
 * calls of commodities.library, private but in its SFD since V36:
 */
struct BrokerCopy {
	struct Node bc_Node;
	char bc_Name[CBD_NAMELEN];
	char bc_Title[CBD_TITLELEN];
	char bc_Descr[CBD_DESCRLEN];
	struct Task *bc_Task;
	struct MsgPort *bc_Port;
	UWORD bc_Dummy;
	ULONG bc_Flags;
};
static LONG __CopyBrokerList(__reg("a6") struct Library *, __reg("a0") struct List *) =
	"\tjsr\t-186(a6)";
static void __FreeBrokerList(__reg("a6") struct Library *, __reg("a0") struct List *) =
	"\tjsr\t-192(a6)";
static LONG __BrokerCommand(__reg("a6") struct Library *, __reg("a0") STRPTR name,
                            __reg("d0") LONG id) = "\tjsr\t-198(a6)";

#define MAXMOVE 16

/* the programs (tasks) with windows on scr, but not this one: how many */
static int owners(struct Screen *scr, struct Task **task, int max)
{
	struct Task *me = FindTask(NULL);
	struct Window *w;
	ULONG lock = LockIBase(0);
	int n = 0, i;

	for (w = scr->FirstWindow; w; w = w->NextWindow) {
		struct Task *t = w->UserPort ? w->UserPort->mp_SigTask : NULL;

		if (!t || t == me)
			continue;
		for (i = 0; i < n && task[i] != t; i++)
			;
		if (i == n && n < max)
			task[n++] = t;
	}
	UnlockIBase(lock);
	return n;
}

/* does t have a window on scr? */
static BOOL has_window(struct Screen *scr, struct Task *t)
{
	struct Window *w;
	ULONG lock = LockIBase(0);
	BOOL yes = FALSE;

	for (w = scr->FirstWindow; w && !yes; w = w->NextWindow)
		yes = w->UserPort && w->UserPort->mp_SigTask == t;
	UnlockIBase(lock);
	return yes;
}

/*
 * Every program on scr that is a commodity: Hide, Show again on the
 * Workbench. The default public screen must be the Workbench already.
 * Leaves scr private if it can be. How many were moved.
 */
static int move_to_wb(struct Screen *scr)
{
	struct Task *task[MAXMOVE];
	char name[MAXMOVE][CBD_NAMELEN];
	struct List bl;
	struct BrokerCopy *bc;
	int n, i, moved = 0, wait;
	BOOL hidden[MAXMOVE];

	if (CxBase->lib_Version < 37)
		return 0;
	n = owners(scr, task, MAXMOVE);
	if (!n)
		return 0;
	NewList(&bl);
	__CopyBrokerList(CxBase, &bl);
	for (i = 0; i < n; i++) {
		name[i][0] = 0;
		for (bc = (struct BrokerCopy *)bl.lh_Head; bc->bc_Node.ln_Succ;
		     bc = (struct BrokerCopy *)bc->bc_Node.ln_Succ)
			if (bc->bc_Task == task[i]) {
				memcpy(name[i], bc->bc_Name, CBD_NAMELEN - 1);
				name[i][CBD_NAMELEN - 1] = 0;
				break;
			}
	}
	__FreeBrokerList(CxBase, &bl);

	/*
	 * Hide all, wait (up to 10 s) until their windows have gone, then make
	 * the desktop private, then Show: MUI opens its windows again on the
	 * screen it still holds, while it holds it. (Only now can it be
	 * private: MUI lets go of the screen a moment after its windows have
	 * gone.) And not Show while a Hide is still waiting: the two undo
	 * each other. One still there after 10 s is left (if it hides later,
	 * its icon is on the Workbench).
	 * (What BrokerCommand() returns for done is not documented: not checked.)
	 */
	for (i = 0; i < n; i++) {
		hidden[i] = name[i][0] != 0;
		if (hidden[i])
			__BrokerCommand(CxBase, (STRPTR)name[i], CXCMD_DISAPPEAR);
	}
	for (wait = 0; wait < 10 * TICKS_PER_SECOND / 5; wait++) {
		BOOL left = FALSE;

		for (i = 0; i < n; i++)
			if (hidden[i] && has_window(scr, task[i]))
				left = TRUE;
		if (!left)
			break;
		Delay(5);
	}
	/* their locks on it go a moment after their windows */
	for (wait = 0; wait < 3 * TICKS_PER_SECOND / 5; wait++) {
		if (PubScreenStatus(scr, PSNF_PRIVATE) & 1)
			break;
		Delay(5);
	}
	for (i = 0; i < n; i++)
		if (hidden[i] && !has_window(scr, task[i])) {
			__BrokerCommand(CxBase, (STRPTR)name[i], CXCMD_APPEAR);
			moved++;
		}
	return moved;
}

/* "Close these first: Shell, Ed." for the windows still on scr */
static void what_is_left(struct Screen *scr, char *buf, int len)
{
	struct Task *task[MAXMOVE];
	int n = owners(scr, task, MAXMOVE), i, at;

	snprintf(buf, len, "%s", tr("Close the windows on the desktop first."));
	at = strlen(buf);
	for (i = 0; i < n && at < len - 40; i++) {
		const char *nm = task[i]->tc_Node.ln_Name;

		/* a Shell's window belongs to its console, called "CON" or so */
		snprintf(buf + at, len - at, "%s%.28s", i ? ", " : "\n", nm && nm[0] ? nm : "?");
		at = strlen(buf);
	}
}

/*
 * Close the desktop, if it is ours. Programs that can (commodities) are
 * moved to the Workbench first; FALSE: windows are left. Not by the
 * screen's visitors: MUI unlocks the screen once its windows are open.
 */
static BOOL close_desktop(struct Screen *scr, BOOL ours, const char *olddefault)
{
	if (!ours) {
		UnlockPubScreen(NULL, scr);
		return TRUE;
	}
	if (olddefault)
		SetDefaultPubScreen(olddefault[0] ? (UBYTE *)olddefault : NULL);
	/* nothing new opens on it, by its name either (unless it has visitors) */
	PubScreenStatus(scr, PSNF_PRIVATE);
	if (scr->FirstWindow && move_to_wb(scr) > 0) {
		/* the last of them may take a moment to go */
		int t;

		for (t = 0; t < 10 && scr->FirstWindow; t++)
			Delay(10);
	}
	if (scr->FirstWindow || !CloseScreen(scr)) {
		char buf[200];

		PubScreenStatus(scr, 0);
		if (olddefault)
			SetDefaultPubScreen((UBYTE *)pubname);
		ScreenToFront(scr);
		what_is_left(scr, buf, sizeof(buf));
		say(buf);
		return FALSE;
	}
	return TRUE;
}

static int commodity(void)
{
	struct MsgPort *port;
	struct NewBroker nb;
	CxObj *broker;
	struct Screen *scr;
	BOOL ours, done = FALSE, isdefault = FALSE;
	char olddefault[MAXPUBSCREENNAME + 1];
	const char *quals = prefs_value(find_setting("HOTKEYS"));
	LONG err = 0;
	int i;

	Forbid();
	port = FindPort((STRPTR)PORTNAME);
	Permit();
	if (port) {
		/* running already: its desktop to the front, and that's all */
		struct Screen *scr = LockPubScreen((STRPTR)pubname);

		if (scr) {
			ScreenToFront(scr);
			UnlockPubScreen(NULL, scr);
		}
		return RETURN_OK;
	}
	CxBase = OpenLibrary("commodities.library", 37);
	if (!CxBase) {
		say("needs commodities.library");
		return RETURN_FAIL;
	}
	port = CreateMsgPort();
	if (!port) {
		CloseLibrary(CxBase);
		return RETURN_FAIL;
	}
	port->mp_Node.ln_Name = (char *)PORTNAME;
	port->mp_Node.ln_Pri = 0;
	AddPort(port);

	memset(&nb, 0, sizeof(nb));
	nb.nb_Version = NB_VERSION;
	nb.nb_Name = (STRPTR)"PanoramaDesk";
	nb.nb_Title = (STRPTR)"PanoramaDesk " MD_VERSION;
	nb.nb_Descr = (STRPTR)"One screen over Panorama windows";
	nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
	nb.nb_Port = port;
	broker = CxBroker(&nb, &err);
	if (!broker) {
		say(err == CBERR_DUP ? "is running already." : "cannot make its commodity.");
		RemPort(port);
		DeleteMsgPort(port);
		CloseLibrary(CxBase);
		return RETURN_FAIL;
	}
	if (!add_hotkey(broker, port, quals, "cursor_left", HK_LEFT) ||
	    !add_hotkey(broker, port, quals, "cursor_right", HK_RIGHT) ||
	    !add_hotkey(broker, port, quals, "cursor_up", HK_UP) ||
	    !add_hotkey(broker, port, quals, "cursor_down", HK_DOWN) ||
	    !add_hotkey(broker, port, quals, "m", HK_SWAP))
		say("some hotkeys could not be made (HOTKEYS= in PanoramaPrefs).");
	for (i = 1; i <= cols * rows && i <= 4; i++) {
		char key[2] = { (char)('0' + i), 0 };

		add_hotkey(broker, port, quals, key, HK_MON + i);
	}
	{
		/*
		 * Every mouse move, copied to us: is it over the Workbench's edge,
		 * has the screen in front changed? And the pointer put somewhere,
		 * as Panorama.card does for the viewers' mouse.
		 */
		static struct InputXpression ix = {
			IX_VERSION, IECLASS_RAWMOUSE, 0, 0, 0, 0, 0
		};
		static struct InputXpression ixp = {
			IX_VERSION, IECLASS_NEWPOINTERPOS, 0, 0, 0, 0, 0
		};
		CxObj *f = CxFilter(NULL), *fp = CxFilter(NULL);

		if (f) {
			SetFilterIX(f, &ix);
			AttachCxObj(f, CxSender(port, HK_EDGE));
			AttachCxObj(broker, f);
		}
		if (fp) {
			SetFilterIX(fp, &ixp);
			AttachCxObj(fp, CxSender(port, HK_PTR));
			AttachCxObj(broker, fp);
		}
		LayersBase = OpenLibrary("layers.library", 37);
	}

	scr = open_desktop(&ours);
	seen_front = IntuitionBase->FirstScreen;
	if (!scr) {
		DeleteCxObjAll(broker);
		RemPort(port);
		DeleteMsgPort(port);
		CloseLibrary(CxBase);
		return RETURN_FAIL;
	}
	if (ours && setting_on("DEFAULTSCREEN")) {
		GetDefaultPubScreen((UBYTE *)olddefault);
		SetDefaultPubScreen((UBYTE *)pubname);
		isdefault = TRUE;
	}
	ActivateCxObj(broker, 1);

	while (!done) {
		ULONG sig = Wait((1UL << port->mp_SigBit) | SIGBREAKF_CTRL_C);
		CxMsg *msg;

		if (sig & SIGBREAKF_CTRL_C)
			done = close_desktop(scr, ours, isdefault ? olddefault : NULL);
		while ((msg = (CxMsg *)GetMsg(port)) != NULL) {
			ULONG type = CxMsgType(msg);
			LONG id = CxMsgID(msg);

			if (type == CXM_IEVENT && (id == HK_EDGE || id == HK_PTR)) {
				struct InputEvent ie = *(struct InputEvent *)CxMsgData(msg);
				WORD dx = ie.ie_X, dy = ie.ie_Y;
				UWORD qual = ie.ie_Qualifier;

				ReplyMsg((struct Message *)msg);
				if (LayersBase) {
					whose_mouse(scr, &ie);
					if (id == HK_EDGE)
						edge(scr, dx, dy);
					follow_focus(scr, qual);
				}
				continue;
			}
			ReplyMsg((struct Message *)msg);
			if (type == CXM_IEVENT) {
				struct Window *w = target(scr, NULL);

				if (id == HK_SWAP) {
					swap(scr);
					desk_owner = OWN_LOCAL;
				}
				else if (!w)
					;
				else if (id == HK_LEFT)
					move_by(scr, w, -1, 0);
				else if (id == HK_RIGHT)
					move_by(scr, w, 1, 0);
				else if (id == HK_UP)
					move_by(scr, w, 0, -1);
				else if (id == HK_DOWN)
					move_by(scr, w, 0, 1);
				else if (id > HK_MON)
					centre(scr, w, id - HK_MON);
			} else if (type == CXM_COMMAND) {
				switch (id) {
				case CXCMD_DISABLE:
					ActivateCxObj(broker, 0);
					break;
				case CXCMD_ENABLE:
					ActivateCxObj(broker, 1);
					break;
				case CXCMD_UNIQUE:
				case CXCMD_APPEAR:
					ScreenToFront(scr);
					break;
				case CXCMD_KILL:
					if (!done)
						done = close_desktop(scr, ours, isdefault ? olddefault : NULL);
					break;
				}
			}
		}
	}
	DeleteCxObjAll(broker);
	{
		struct Message *m;

		while ((m = GetMsg(port)) != NULL)
			ReplyMsg(m);
	}
	RemPort(port);
	DeleteMsgPort(port);
	if (LayersBase)
		CloseLibrary(LayersBase);
	CloseLibrary(CxBase);
	return RETURN_OK;
}

/* ---------------------------------------------------------------- the command line */

static int command(LONG *args)
{
	struct Screen *scr;
	struct Window *w;
	struct MsgPort *port;

	if (args[A_CLOSE]) {
		Forbid();
		port = FindPort((STRPTR)PORTNAME);
		if (port)
			Signal(port->mp_SigTask, SIGBREAKF_CTRL_C);
		Permit();
		if (!port && Output() && IsInteractive(Output()))
			printf("PanoramaDesk: is not running.\n");
		return port ? RETURN_OK : RETURN_WARN;
	}
	if (args[A_INFO]) {
		/* every public screen: its mode, and whose it is */
		struct List *l = LockPubScreenList();
		struct PubScreenNode *p;
		char buf[160];

		for (p = (struct PubScreenNode *)l->lh_Head; p->psn_Node.ln_Succ;
		     p = (struct PubScreenNode *)p->psn_Node.ln_Succ) {
			describe(p->psn_Screen, buf, sizeof(buf));
			printf("%-16s %s, opened by %s\n", p->psn_Node.ln_Name, buf,
			       p->psn_SigTask && p->psn_SigTask->tc_Node.ln_Name ?
			       p->psn_SigTask->tc_Node.ln_Name : "(the system)");
		}
		UnlockPubScreenList();
		return RETURN_OK;
	}
	scr = LockPubScreen((STRPTR)pubname);
	if (!scr && args[A_WAIT]) {
		/* just started in the background: give it a moment */
		LONG n = *(LONG *)args[A_WAIT] * 4;

		while (!scr && n-- > 0) {
			Delay(TICKS_PER_SECOND / 4);
			scr = LockPubScreen((STRPTR)pubname);
		}
	}
	if (!scr) {
		say("the desktop is not open.");
		return RETURN_WARN;
	}
	if (args[A_SHELL]) {
		/* 600 x 200, or less on a small desktop, right in the middle */
		WORD w = scr->Width > 640 ? 600 : scr->Width - 40;
		WORD h = scr->Height > 240 ? 200 : scr->Height - 40;
		char cmd[200];
		BPTR nil = Open((STRPTR)"NIL:", MODE_OLDFILE);

		snprintf(cmd, sizeof(cmd), "NewShell \"CON:%d/%d/%d/%d/Panorama Shell/CLOSE/SCREEN%s\"",
		         (scr->Width - w) / 2, (scr->Height - h) / 2, w, h, pubname);
		if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL, TAG_END) == -1)
			say("cannot start the Shell.");
		if (nil)
			Close(nil);
	} else if (args[A_SWAP]) {
		swap(scr);
	} else {
		w = target(scr, (const char *)args[A_WINDOW]);
		if (!w) {
			say(args[A_WINDOW] ? "no window with that title on the desktop." :
			    "the active window is not on the desktop.");
			UnlockPubScreen(NULL, scr);
			return RETURN_WARN;
		}
		if (args[A_CENTRE]) {
			centre(scr, w, *(LONG *)args[A_CENTRE]);
		} else if (args[A_MOVE]) {
			const char *m = (const char *)args[A_MOVE];

			if (!mp_stricmp(m, "left"))
				move_by(scr, w, -1, 0);
			else if (!mp_stricmp(m, "right"))
				move_by(scr, w, 1, 0);
			else if (!mp_stricmp(m, "up"))
				move_by(scr, w, 0, -1);
			else if (!mp_stricmp(m, "down"))
				move_by(scr, w, 0, 1);
			else
				say("MOVE=LEFT, RIGHT, UP or DOWN");
		}
	}
	UnlockPubScreen(NULL, scr);
	return RETURN_OK;
}

int main(int argc, char **argv)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda = NULL;
	int rc;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	if (!IntuitionBase || !GfxBase)
		return RETURN_FAIL;
	from_wb = argc == 0;
	lang_init();
	read_settings();
	if (!from_wb) {
		rda = ReadArgs((STRPTR)template, args, NULL);
		if (!rda) {
			PrintFault(IoErr(), (STRPTR)"PanoramaDesk");
			rc = RETURN_FAIL;
			goto out;
		}
	}
	if (args[A_MOVE] || args[A_CENTRE] || args[A_SWAP] || args[A_CLOSE] || args[A_SHELL] ||
	    args[A_INFO])
		rc = command(args);
	else
		rc = commodity();
	if (rda)
		FreeArgs(rda);
out:
	CloseLibrary((struct Library *)GfxBase);
	CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
