/*
 * Panorama - cxlog: the mouse events going through commodities, printed,
 * to see how an emulator (or anything) moves the Amiga's pointer.
 *
 *   cxlog [SECS=10] [MAX=40]
 *
 * Prints class, subclass, code, qualifier, x, y of each RAWMOUSE,
 * NEWPOINTERPOS, POINTERPOS and TABLET event, then the screen in front, the
 * pointer on it and the active window: what a click did.
 */
#include <exec/ports.h>
#include <dos/dos.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/commodities.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <stdio.h>
#include <string.h>

struct Library *CxBase;
struct IntuitionBase *IntuitionBase;

static const char template[] = "SECS/N,MAX/N";

int main(void)
{
	static const UBYTE classes[] = {
		IECLASS_RAWMOUSE, IECLASS_NEWPOINTERPOS, IECLASS_POINTERPOS, 0x13 /* NEWTABLET? */
	};
	LONG args[2] = { 0, 0 };
	struct RDArgs *rda = ReadArgs((STRPTR)template, args, NULL);
	LONG secs = args[0] ? *(LONG *)args[0] : 10, max = args[1] ? *(LONG *)args[1] : 40, n = 0;
	struct MsgPort *port;
	struct NewBroker nb;
	CxObj *broker;
	ULONG end;
	struct DateStamp ds;
	int i;

	IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
	CxBase = OpenLibrary("commodities.library", 37);
	port = CreateMsgPort();
	if (!CxBase || !port)
		return RETURN_FAIL;
	memset(&nb, 0, sizeof(nb));
	nb.nb_Version = NB_VERSION;
	nb.nb_Name = (STRPTR)"cxlog";
	nb.nb_Title = (STRPTR)"cxlog";
	nb.nb_Descr = (STRPTR)"mouse events printed";
	nb.nb_Port = port;
	broker = CxBroker(&nb, NULL);
	if (!broker)
		return RETURN_FAIL;
	for (i = 0; i < 4; i++) {
		static struct InputXpression ix[4];
		CxObj *f = CxFilter(NULL);

		ix[i].ix_Version = IX_VERSION;
		ix[i].ix_Class = classes[i];
		SetFilterIX(f, &ix[i]);
		AttachCxObj(f, CxSender(port, i));
		AttachCxObj(broker, f);
	}
	ActivateCxObj(broker, 1);
	DateStamp(&ds);
	end = ds.ds_Minute * 60 + ds.ds_Tick / 50 + secs;
	while (n < max) {
		CxMsg *m;

		DateStamp(&ds);
		if ((ULONG)(ds.ds_Minute * 60 + ds.ds_Tick / 50) >= end)
			break;
		Delay(2);
		while ((m = (CxMsg *)GetMsg(port)) != NULL) {
			struct InputEvent *ie = (struct InputEvent *)CxMsgData(m);

			if (n++ < max) {
				struct Screen *scr = IntuitionBase ? IntuitionBase->FirstScreen : NULL;
				struct Window *w = IntuitionBase ? IntuitionBase->ActiveWindow : NULL;

				printf("class %02x sub %02x code %04x qual %04x x %d y %d addr %08lx",
				       ie->ie_Class, ie->ie_SubClass, ie->ie_Code, ie->ie_Qualifier,
				       ie->ie_X, ie->ie_Y, (ULONG)ie->ie_EventAddress);
				if (scr)
					printf("  front \"%s\" at %d,%d active \"%s\"",
					       scr->Title ? (char *)scr->Title : "?", scr->MouseX, scr->MouseY,
					       w && w->Title ? (char *)w->Title : "-");
				printf("\n");
			}
			ReplyMsg((struct Message *)m);
		}
	}
	DeleteCxObjAll(broker);
	{
		struct Message *m;

		while ((m = GetMsg(port)) != NULL)
			ReplyMsg(m);
	}
	DeleteMsgPort(port);
	CloseLibrary(CxBase);
	if (IntuitionBase)
		CloseLibrary((struct Library *)IntuitionBase);
	if (rda)
		FreeArgs(rda);
	return RETURN_OK;
}
