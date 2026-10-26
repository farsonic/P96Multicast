/*
 * P96Multicast - MulticastPrefs: install the cards and their monitor.
 *
 * P96 sets a board up for each monitor in DEVS:Monitors whose icon names it
 * (ToolType BOARDTYPE=Multicast loads LIBS:Picasso96/Multicast.card); every
 * monitor is a copy of the same small program. AROS's DEVS:Monitors/P96
 * loads the card named after the file instead. So:
 *
 *   1. Multicast.card and Wrap.card go to LIBS:Picasso96/, from next to
 *      this program (PROGDIR:, PROGDIR:Libs/Picasso96/ or the package's
 *      Libs/Picasso96/ beside it).
 *   2. The monitor: DEVS:Monitors/Wrap if Wrap.card is used (CARD= set),
 *      else DEVS:Monitors/Multicast, copied from a P96 monitor already
 *      there (one whose icon has BOARDTYPE=, or AROS's P96), with an icon
 *      saying BOARDTYPE=<its name>.
 *   3. Monitors that would set a card up a second time go to
 *      SYS:Storage/Monitors/, where Workbench keeps monitors not in use:
 *      Wrap.card sets up the card it wraps and its heads itself, and
 *      without Wrap.card its monitor must go.
 */
#include <exec/memory.h>
#include <dos/dos.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

struct Library *IconBase;

static void (*say)(const char *);
static BOOL dry;
static const char *source;   /* FROM= */
static BOOL failed;

static void notef(const char *fmt, const char *a, const char *b)
{
	char buf[160];

	snprintf(buf, sizeof(buf), fmt, a, b);
	say(buf);
}

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

static BOOL copy_file(const char *from, const char *to)
{
	BPTR in, out;
	UBYTE *buf;
	LONG n;
	BOOL ok = TRUE;

	if (dry)
		return TRUE;
	buf = AllocMem(8192, MEMF_ANY);
	if (!buf)
		return FALSE;
	in = Open((STRPTR)from, MODE_OLDFILE);
	out = in ? Open((STRPTR)to, MODE_NEWFILE) : 0;
	if (!in || !out)
		ok = FALSE;
	while (ok && (n = Read(in, buf, 8192)) > 0)
		ok = Write(out, buf, n) == n;
	if (in)
		Close(in);
	if (out)
		Close(out);
	FreeMem(buf, 8192);
	return ok;
}

static void make_dir(const char *path)
{
	BPTR lock;

	if (dry || exists(path))
		return;
	lock = CreateDir((STRPTR)path);
	if (lock)
		UnLock(lock);
}

/* a file, and its icon if any, from one directory to another */
static BOOL move_file(const char *from, const char *to)
{
	char fi[120], ti[120];

	snprintf(fi, sizeof(fi), "%s.info", from);
	snprintf(ti, sizeof(ti), "%s.info", to);
	if (dry)
		return TRUE;
	if (!Rename((STRPTR)from, (STRPTR)to)) {
		if (!copy_file(from, to) || !DeleteFile((STRPTR)from))
			return FALSE;
	}
	if (exists(fi) && !Rename((STRPTR)fi, (STRPTR)ti)) {
		if (copy_file(fi, ti))
			DeleteFile((STRPTR)fi);
	}
	return TRUE;
}

/* "uaegfx.card" or "LIBS:Picasso96/uaegfx.card" -> "uaegfx" */
static void board_name(const char *card, char *out, int len)
{
	const char *p = card, *name = card;
	int i;

	for (; *p; p++)
		if (*p == ':' || *p == '/')
			name = p + 1;
	for (i = 0; name[i] && i < len - 1; i++)
		out[i] = name[i];
	out[i] = 0;
	if (i > 5 && !mp_stricmp(out + i - 5, ".card"))
		out[i - 5] = 0;
}

/* the board a monitor's icon names, or "" */
static void icon_board(const char *path, char *out, int len)
{
	struct DiskObject *dob;
	STRPTR v;

	out[0] = 0;
	if (!IconBase || !(dob = GetDiskObject((STRPTR)path)))
		return;
	v = FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (STRPTR)"BOARDTYPE");
	if (v) {
		strncpy(out, (char *)v, len - 1);
		out[len - 1] = 0;
	}
	FreeDiskObject(dob);
}

/*
 * The monitors in DEVS:Monitors that set up `board`: a file of that name
 * (AROS) or one whose icon says BOARDTYPE=board (P96). Each found goes to
 * SYS:Storage/Monitors unless it is `keep`.
 */
static void retire_monitors(const char *board, const char *keep)
{
	struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
	BPTR lock = Lock((STRPTR)"DEVS:Monitors", ACCESS_READ);
	char names[16][32];
	int n = 0, i;

	if (fib && lock && Examine(lock, fib)) {
		while (ExNext(lock, fib) && n < 16) {
			char path[80], b[32];
			int l = strlen((char *)fib->fib_FileName);

			if (fib->fib_DirEntryType > 0 || (l > 5 && !mp_stricmp((char *)fib->fib_FileName + l - 5, ".info")))
				continue;
			snprintf(path, sizeof(path), "DEVS:Monitors/%s", fib->fib_FileName);
			icon_board(path, b, sizeof(b));
			if ((!mp_stricmp((char *)fib->fib_FileName, board) || !mp_stricmp(b, board)) &&
			    mp_stricmp((char *)fib->fib_FileName, keep))
				strncpy(names[n++], (char *)fib->fib_FileName, 31);
		}
	}
	if (lock)
		UnLock(lock);
	if (fib)
		FreeDosObject(DOS_FIB, fib);
	for (i = 0; i < n; i++) {
		char from[80], to[80];

		snprintf(from, sizeof(from), "DEVS:Monitors/%s", names[i]);
		snprintf(to, sizeof(to), "SYS:Storage/Monitors/%s", names[i]);
		make_dir("SYS:Storage");
		make_dir("SYS:Storage/Monitors");
		if (move_file(from, to))
			notef("Moved %s to SYS:Storage/Monitors (%s)", from,
			      "would set the same card up twice");
		else {
			notef("Could not move %s%s", from, "");
			failed = TRUE;
		}
	}
}

/* a P96 monitor program to copy: AROS's P96, or any with BOARDTYPE= */
static BOOL find_template(char *out, int len)
{
	static const char *dirs[] = { "DEVS:Monitors", "SYS:Storage/Monitors" };
	int d;

	if (exists("DEVS:Monitors/P96")) {
		strncpy(out, "DEVS:Monitors/P96", len);
		return TRUE;
	}
	for (d = 0; d < 2; d++) {
		struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
		BPTR lock = Lock((STRPTR)dirs[d], ACCESS_READ);
		BOOL found = FALSE;

		if (fib && lock && Examine(lock, fib)) {
			while (!found && ExNext(lock, fib)) {
				char b[32];
				int l = strlen((char *)fib->fib_FileName);

				if (fib->fib_DirEntryType > 0 ||
				    (l > 5 && !mp_stricmp((char *)fib->fib_FileName + l - 5, ".info")))
					continue;
				snprintf(out, len, "%s/%s", dirs[d], fib->fib_FileName);
				icon_board(out, b, sizeof(b));
				found = b[0] != 0;
			}
		}
		if (lock)
			UnLock(lock);
		if (fib)
			FreeDosObject(DOS_FIB, fib);
		if (found)
			return TRUE;
	}
	return FALSE;
}

/* the monitor's icon: BOARDTYPE=name, the rest of the template's look */
static BOOL write_icon(const char *monitor, const char *template, const char *name)
{
	struct DiskObject *dob;
	STRPTR *old;
	char tt[48];
	STRPTR types[2];
	BOOL ok;

	if (!IconBase)
		return FALSE;
	if (dry)
		return TRUE;
	dob = template ? GetDiskObject((STRPTR)template) : NULL;
	if (!dob)
		dob = GetDefDiskObject(WBTOOL);
	if (!dob)
		return FALSE;
	snprintf(tt, sizeof(tt), "BOARDTYPE=%s", name);
	types[0] = (STRPTR)tt;
	types[1] = NULL;
	old = dob->do_ToolTypes;
	dob->do_ToolTypes = types;
	dob->do_Type = WBTOOL;
	ok = PutDiskObject((STRPTR)monitor, dob);
	dob->do_ToolTypes = old;
	FreeDiskObject(dob);
	return ok;
}

static void install_card(const char *file)
{
	static const char *from[] = { NULL, "PROGDIR:/Libs/Picasso96/", "PROGDIR:Libs/Picasso96/",
	                              "PROGDIR:" };
	char src[160], dst[100];
	int i;

	snprintf(dst, sizeof(dst), "LIBS:Picasso96/%s", file);
	for (i = source ? 0 : 1; i < 4; i++) {
		const char *d = i ? from[i] : source;
		int l = strlen(d);

		snprintf(src, sizeof(src), "%s%s%s", d,
		         l && d[l - 1] != ':' && d[l - 1] != '/' ? "/" : "", file);
		if (exists(src))
			break;
	}
	if (i == 4) {
		if (exists(dst))
			notef("%s is already in LIBS:Picasso96 (no newer copy next to %s)", file,
			      "MulticastPrefs");
		else {
			notef("%s not found next to MulticastPrefs%s", file, "");
			failed = TRUE;
		}
		return;
	}
	make_dir("LIBS:Picasso96");
	if (copy_file(src, dst))
		notef("Copied %s to %s", src, dst);
	else {
		notef("Could not copy %s to %s", src, dst);
		failed = TRUE;
	}
}

BOOL install(void (*note)(const char *), BOOL dry_run, const char *from)
{
	const char *card = prefs_value(find_setting("CARD"));
	BOOL wrap = card[0] != 0;
	const char *monitor_name = wrap ? "Wrap" : "Multicast";
	char monitor[80], template[80], icon[90], board[32];
	int i;

	say = note;
	dry = dry_run;
	source = from;
	failed = FALSE;
	IconBase = OpenLibrary("icon.library", 37);
	if (dry)
		say("Would do:");

	install_card("Multicast.card");
	install_card("Wrap.card");

	snprintf(monitor, sizeof(monitor), "DEVS:Monitors/%s", monitor_name);
	snprintf(icon, sizeof(icon), "%s.info", monitor);
	if (!exists(monitor)) {
		if (!find_template(template, sizeof(template))) {
			say("No P96 monitor in DEVS:Monitors to copy: is P96 installed?");
			failed = TRUE;
		} else if (copy_file(template, monitor)) {
			notef("Made %s from %s", monitor, template);
			if (!write_icon(monitor, template, monitor_name))
				notef("Could not write %s%s", icon, "");
			else
				notef("Its icon says BOARDTYPE=%s%s", monitor_name, "");
		} else {
			notef("Could not make %s%s", monitor, "");
			failed = TRUE;
		}
	} else {
		icon_board(monitor, board, sizeof(board));
		if (mp_stricmp(board, monitor_name)) {
			if (write_icon(monitor, monitor, monitor_name))
				notef("%s: icon set to BOARDTYPE=%s", monitor, monitor_name);
		} else {
			notef("%s is there%s", monitor, "");
		}
	}

	/* nothing else may set up what this monitor's card sets up */
	if (wrap) {
		board_name(card, board, sizeof(board));
		retire_monitors(board, monitor_name);
		for (i = 0; i < 6; i++) {
			static const char *keys[] = { "MIRROR", "HEAD", "HEAD1", "HEAD2", "HEAD3", "HEAD4" };
			const char *h = prefs_value(find_setting(keys[i]));

			if (*h) {
				board_name(h, board, sizeof(board));
				retire_monitors(board, monitor_name);
			}
		}
	} else {
		retire_monitors("Wrap", monitor_name);
	}
	say(failed ? "Not everything could be done." : "Reboot to start the card.");
	if (IconBase)
		CloseLibrary(IconBase);
	IconBase = NULL;
	return !failed;
}
