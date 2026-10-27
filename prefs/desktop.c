/*
 * P96Multicast - MulticastDesktop: one screen over several Multicast windows.
 *
 * With SPLIT=CxR, Multicast.card sends its screen in C x R parts, each to
 * a port of its own, and p96view.py --span shows each part in a window of
 * its own: one desktop over several "monitors". MulticastDesktop opens that
 * desktop, a public screen of C x R times MONITOR=, on the Multicast board,
 * and keeps it open. It is a commodity, with hotkeys (HOTKEYS=, ctrl+alt by
 * default) for the active window:
 *
 *   ctrl alt cursor keys    to the monitor left, right, above, below,
 *                           at the same place on it
 *   ctrl alt 1 ... 4        to the middle of monitor 1 ... 4
 *   ctrl alt m              the desktop in front, or the Workbench again
 *
 * Exchange can hide, disable and quit it. Quitting closes the desktop once
 * no windows are left on it.
 *
 *   MulticastDesktop                    the commodity (also from WBStartup)
 *   MulticastDesktop MOVE=RIGHT         the active window one monitor right;
 *                                       LEFT, UP, DOWN too
 *   MulticastDesktop CENTRE=2           ... to the middle of monitor 2
 *   MulticastDesktop SWAP               the desktop or the Workbench in front
 *   MulticastDesktop CLOSE              tells the running one to quit
 *   MulticastDesktop SHELL              a Shell in the middle of the desktop
 *   MulticastDesktop INFO               the desktop's size and display mode
 *   ... WAIT=10                         wait up to 10 s for the desktop to open
 *   ... WINDOW=title                    a window by its title, not the active one
 *
 * The settings are MulticastPrefs': SPLIT, MONITOR, COLOURS, PUBSCREEN,
 * DEFAULTSCREEN, BEHIND and HOTKEYS in ENV:Multicast.prefs.
 */
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <graphics/displayinfo.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/commodities.h>
#include <clib/alib_protos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

#define MD_VERSION "1.0"
#define PORTNAME "MulticastDesktop"

static const char version[] = "$VER: MulticastDesktop " MD_VERSION " (" MP_DATE ")";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *CxBase;

static const char template[] = "MOVE/K,CENTRE=CENTER/K/N,SWAP/S,CLOSE/S,WINDOW/K,SHELL/S,WAIT/K/N,INFO/S";
enum { A_MOVE, A_CENTRE, A_SWAP, A_CLOSE, A_WINDOW, A_SHELL, A_WAIT, A_INFO, A_COUNT };

/* the desktop: C x R monitors of MW x MH */
static UWORD cols = 1, rows = 1, mw = 800, mh = 600, depth = 16;
static char pubname[MAXPUBSCREENNAME + 1] = "MULTICAST";
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
}

static BOOL setting_on(const char *key)
{
	return prefs_value(find_setting(key))[0] == '1';
}

/* ---------------------------------------------------------------- messages */

static void say(const char *text)
{
	/* from Workbench, or run with its output nowhere to be seen: a requester */
	if ((from_wb || !Output() || !IsInteractive(Output())) && IntuitionBase) {
		struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"MulticastDesktop",
		                         (UBYTE *)text, (UBYTE *)"Ok" };

		EasyRequestArgs(NULL, &es, NULL, NULL);
	} else {
		printf("MulticastDesktop: %s\n", text);
	}
}

/* a line for the Shell, when there is one to see it */
static void tell(const char *text)
{
	if (!from_wb && Output() && IsInteractive(Output()))
		printf("MulticastDesktop: %s\n", text);
}

/* "1600x600, mode 50de1100 Multicast:1600x600 16bit PC" */
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

/* a Multicast mode of this size with at least this depth */
static ULONG find_mode(UWORD w, UWORD h, UWORD d)
{
	ULONG id = INVALID_ID;
	struct NameInfo ni;
	struct DimensionInfo di;

	while ((id = NextDisplayInfo(id)) != INVALID_ID) {
		char *m;

		if (!GetDisplayInfoData(NULL, (UBYTE *)&ni, sizeof(ni), DTAG_NAME, id) ||
		    !(m = strstr((char *)ni.Name, "Multicast")))
			continue;
		m += 9;
		/* the first board: not Multicast2 ... */
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
		if (!strstr(buf, "Multicast"))
			say("The public screen MULTICAST is open already, but not on the Multicast "
			    "board: close it (or the program that opened it) and start MulticastDesktop "
			    "again.");
		return scr;
	}

	id = find_mode(w, h, depth);
	if (id == INVALID_ID) {
		snprintf(buf, sizeof(buf),
		         "No Multicast mode %ux%u with %u bit.\n"
		         "MulticastPrefs' Desktop page sets the modes up (then reboot).", w, h, depth);
		say(buf);
		return NULL;
	}
	scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth,
	                     SA_PubName, (ULONG)pubname, SA_Title, (ULONG)"Multicast desktop",
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

/* ---------------------------------------------------------------- the commodity */

#define HK_LEFT   1
#define HK_RIGHT  2
#define HK_UP     3
#define HK_DOWN   4
#define HK_SWAP   5
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

/* close the desktop, if it is ours and nobody is on it; FALSE: windows are left */
static BOOL close_desktop(struct Screen *scr, BOOL ours, const char *olddefault)
{
	if (!ours) {
		UnlockPubScreen(NULL, scr);
		return TRUE;
	}
	if (!(PubScreenStatus(scr, PSNF_PRIVATE) & 1)) {
		PubScreenStatus(scr, 0);
		ScreenToFront(scr);
		say("Close the windows on the desktop first.");
		return FALSE;
	}
	if (olddefault)
		SetDefaultPubScreen(olddefault[0] ? (UBYTE *)olddefault : NULL);
	CloseScreen(scr);
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
	nb.nb_Name = (STRPTR)"MulticastDesktop";
	nb.nb_Title = (STRPTR)"MulticastDesktop " MD_VERSION;
	nb.nb_Descr = (STRPTR)"One screen over Multicast windows";
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
		say("some hotkeys could not be made (HOTKEYS= in MulticastPrefs).");
	for (i = 1; i <= cols * rows && i <= 4; i++) {
		char key[2] = { (char)('0' + i), 0 };

		add_hotkey(broker, port, quals, key, HK_MON + i);
	}

	scr = open_desktop(&ours);
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

			ReplyMsg((struct Message *)msg);
			if (type == CXM_IEVENT) {
				struct Window *w = target(scr, NULL);

				if (id == HK_SWAP)
					swap(scr);
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
			printf("MulticastDesktop: is not running.\n");
		return port ? RETURN_OK : RETURN_WARN;
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

		snprintf(cmd, sizeof(cmd), "NewShell \"CON:%d/%d/%d/%d/Multicast Shell/CLOSE/SCREEN%s\"",
		         (scr->Width - w) / 2, (scr->Height - h) / 2, w, h, pubname);
		if (SystemTags((STRPTR)cmd, SYS_Input, nil, SYS_Output, NULL, TAG_END) == -1)
			say("cannot start the Shell.");
		if (nil)
			Close(nil);
	} else if (args[A_INFO]) {
		char buf[160];

		describe(scr, buf, sizeof(buf));
		printf("MulticastDesktop: %s is %s\n", pubname, buf);
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
	read_settings();
	if (!from_wb) {
		rda = ReadArgs((STRPTR)template, args, NULL);
		if (!rda) {
			PrintFault(IoErr(), (STRPTR)"MulticastDesktop");
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
