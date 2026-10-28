/*
 * Panorama - PanoramaPrefs: what the running cards say about
 * themselves (the status blocks mctest STATUS and wrapstat read), and
 * what is installed where.
 */
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"
#include "mc_status.h"
#include "wrap_status.h"

static BOOL exists(const char *path)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin = me->pr_WindowPtr;
	BPTR lock;

	me->pr_WindowPtr = (APTR)-1;
	lock = Lock((STRPTR)path, ACCESS_READ);
	me->pr_WindowPtr = oldwin;
	if (lock)
		UnLock(lock);
	return lock != 0;
}

#define LINE(...) do { if (n < max) snprintf(lines[n++], 80, __VA_ARGS__); } while (0)

int status_lines(char lines[][80], int max)
{
	struct McBase *mc;
	struct WrapBase *wrap;
	struct McStatus ms;
	struct WrapStatus ws;
	struct WrapMirror wm;
	struct Task *sender;
	BOOL stack;
	UWORD mcver = 0, mcrev = 0, wver = 0, wrev = 0;
	char inner[WRAP_NAMELEN] = "";
	int n = 0, i;

	Forbid();
	mc = (struct McBase *)FindName(&SysBase->LibList, "Panorama.card");
	if (mc) {
		ms = mc->status;
		mcver = mc->lib.lib_Version;
		mcrev = mc->lib.lib_Revision;
	}
	wrap = (struct WrapBase *)FindName(&SysBase->LibList, "Wrap.card");
	if (wrap) {
		ws = wrap->status;
		wver = wrap->lib.lib_Version;
		wrev = wrap->lib.lib_Revision;
		if (ws.magic == WRAP_STATUS_MAGIC && ws.mirror)
			wm = *ws.mirror;
		if (ws.magic == WRAP_STATUS_MAGIC && ws.inner && ws.inner->lib_Node.ln_Name) {
			strncpy(inner, ws.inner->lib_Node.ln_Name, sizeof(inner) - 1);
			inner[sizeof(inner) - 1] = 0;
		}
	}
	sender = FindTask("Panorama.card sender");
	stack = FindName(&SysBase->LibList, "bsdsocket.library") != NULL;
	Permit();

	if (!mc) {
		LINE("Panorama.card: not running");
	} else {
		int boards = ms.magic == MC_STATUS_MAGIC ? ms.cards : 1;

		LINE("Panorama.card %u.%u: %d board(s)", mcver, mcrev, boards);
		if (ms.magic == MC_STATUS_MAGIC) {
			if (ms.error)
				LINE("  setup failed: error %u", ms.error);
			LINE("  video memory: %lu KB", ms.vramsize >> 10);
			LINE("  modes: %u from P96, %u from the card", ms.modes_before, ms.modes_added);
			LINE("  modes set %lu, screens shown %lu", ms.setgc_calls, ms.panning_calls);
			if (mcver >= 2) {
				LINE("  sent %lu KB, %u viewer(s), %lu repairs", ms.sent_kb, ms.viewers,
				     ms.repairs);
				LINE("  orders: %s, pointer: %s, screen checked every %u s",
				     ms.blitter ? "on" : "off", ms.pointer ? "drawn by viewers" : "on screen",
				     ms.sweep_secs);
			}
		}
		LINE("  sender: %s", sender ? "running" : "not running");
		LINE("  TCP/IP stack: %s", stack ? "running" : "not found");
	}
	if (!wrap) {
		LINE("Wrap.card: not running");
	} else if (ws.magic == WRAP_STATUS_MAGIC) {
		LINE("Wrap.card %u.%u: %u board(s)", wver, wrev, ws.nboards);
		LINE("  wrapping: %s", inner[0] ? inner : "nothing");
		if (ws.error)
			LINE("  error %u (wrapstat explains)", ws.error);
		if (ws.mirror && wm.nheads) {
			LINE("  %u head(s), %s", wm.nheads, wm.task ? "running" : "not running");
			for (i = 0; i < wm.nheads && i < WRAP_MAXHEADS; i++)
				LINE("  head %d: %s %ux%u", i + 1,
				     wm.head[i].bi ? wm.head[i].card : "(not set up)",
				     wm.head[i].width, wm.head[i].height);
		}
	}
	LINE("Installed:");
	LINE("  LIBS:Picasso96/Panorama.card: %s", exists("LIBS:Picasso96/Panorama.card") ? "yes" : "no");
	LINE("  LIBS:Picasso96/Wrap.card: %s", exists("LIBS:Picasso96/Wrap.card") ? "yes" : "no");
	LINE("  DEVS:Monitors/Panorama: %s", exists("DEVS:Monitors/Panorama") ? "yes" : "no");
	LINE("  DEVS:Monitors/Wrap: %s", exists("DEVS:Monitors/Wrap") ? "yes" : "no");
	LINE("  ENVARC:Panorama.prefs: %s", exists("ENVARC:Panorama.prefs") ? "yes" : "no");
	LINE("  ENVARC:Wrap.prefs: %s", exists("ENVARC:Wrap.prefs") ? "yes" : "no");
	return n;
}
