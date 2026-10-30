/*
 * Panorama - dtpic: a picture through datatypes, in a window, timed.
 *
 *   dtpic FILE [PUBSCREEN=name] [SECS=n]
 *
 * Loads FILE with datatypes.library (as Multiview does), says how long the
 * loading and the layout took, opens a window of the picture's size on
 * the public screen (default: the default one) and shows it there for SECS
 * seconds (default 10) or until closed. For emu/picture_test.sh:
 * panoramapic.datatype has the viewer decode the picture.
 */
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include <stdio.h>

static const char version[] = "$VER: dtpic 1.0 (29.10.2026)";

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *DataTypesBase;

static const char template[] = "FILE/A,PUBSCREEN/K,SECS/K/N";
enum { A_FILE, A_PUBSCREEN, A_SECS, A_COUNT };

static LONG ticks(void)
{
	struct DateStamp ds;

	DateStamp(&ds);
	return (ds.ds_Days * 1440 + ds.ds_Minute) * 60 * TICKS_PER_SECOND + ds.ds_Tick;
}

int main(void)
{
	LONG args[A_COUNT] = { 0 };
	struct RDArgs *rda;
	struct Screen *scr;
	Object *o;
	struct BitMap *bm = NULL;
	struct Window *win;
	ULONG w = 0, h = 0;
	LONG t0, t1, t2, secs = 10;
	int rc = RETURN_FAIL;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
	DataTypesBase = OpenLibrary("datatypes.library", 39);
	if (!IntuitionBase || !GfxBase || !DataTypesBase) {
		printf("dtpic: needs AmigaOS 3.0 or later\n");
		goto out;
	}
	rda = ReadArgs((STRPTR)template, args, NULL);
	if (!rda) {
		PrintFault(IoErr(), (STRPTR)"dtpic");
		goto out;
	}
	if (args[A_SECS])
		secs = *(LONG *)args[A_SECS];
	scr = LockPubScreen((STRPTR)args[A_PUBSCREEN]);
	if (!scr) {
		printf("dtpic: no public screen %s\n", args[A_PUBSCREEN] ? (char *)args[A_PUBSCREEN] : "");
		FreeArgs(rda);
		goto out;
	}

	t0 = ticks();
	o = NewDTObject((APTR)args[A_FILE], DTA_GroupID, GID_PICTURE, PDTA_Remap, TRUE,
	                PDTA_Screen, (ULONG)scr, PDTA_DestMode, PMODE_V43, TAG_DONE);
	t1 = ticks();
	if (!o) {
		LONG err = IoErr();

		printf("dtpic: cannot load %s: error %ld\n", (char *)args[A_FILE], err);
		PrintFault(err, (STRPTR)"dtpic");
		UnlockPubScreen(NULL, scr);
		FreeArgs(rda);
		goto out;
	}
	DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
	t2 = ticks();
	GetDTAttrs(o, PDTA_DestBitMap, (ULONG)&bm, DTA_NominalHoriz, (ULONG)&w, DTA_NominalVert, (ULONG)&h,
	           TAG_DONE);
	if (!bm)
		GetDTAttrs(o, PDTA_BitMap, (ULONG)&bm, TAG_DONE);
	printf("dtpic: %s: %lux%lu, loaded in %ld.%02ld s, laid out in %ld.%02ld s\n",
	       (char *)args[A_FILE], w, h, (t1 - t0) / TICKS_PER_SECOND,
	       (t1 - t0) % TICKS_PER_SECOND * 2, (t2 - t1) / TICKS_PER_SECOND,
	       (t2 - t1) % TICKS_PER_SECOND * 2);
	fflush(stdout);
	if (!bm || !w || !h) {
		printf("dtpic: no bitmap\n");
		DisposeDTObject(o);
		UnlockPubScreen(NULL, scr);
		FreeArgs(rda);
		goto out;
	}
	win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, 40, WA_Top, 40,
	                     WA_InnerWidth, w, WA_InnerHeight, h, WA_Title, (ULONG)"dtpic",
	                     WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
	                     WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW,
	                     TAG_END);
	if (win) {
		LONG end = ticks() + secs * TICKS_PER_SECOND;
		BOOL done = FALSE;

		BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, w, h, 0xc0);
		rc = RETURN_OK;
		while (!done && ticks() < end) {
			struct IntuiMessage *m;

			Delay(10);
			while ((m = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
				if (m->Class == IDCMP_CLOSEWINDOW)
					done = TRUE;
				else if (m->Class == IDCMP_REFRESHWINDOW) {
					BeginRefresh(win);
					BltBitMapRastPort(bm, 0, 0, win->RPort, win->BorderLeft, win->BorderTop, w, h,
					                  0xc0);
					EndRefresh(win, TRUE);
				}
				ReplyMsg((struct Message *)m);
			}
			if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
				done = TRUE;
		}
		CloseWindow(win);
	} else {
		printf("dtpic: cannot open the window\n");
	}
	DisposeDTObject(o);
	UnlockPubScreen(NULL, scr);
	FreeArgs(rda);
out:
	if (DataTypesBase)
		CloseLibrary(DataTypesBase);
	if (GfxBase)
		CloseLibrary((struct Library *)GfxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	return rc;
}
