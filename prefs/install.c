/*
 * P96Multicast - MulticastPrefs: install the cards and their monitor.
 *
 * P96 sets a board up for each monitor in DEVS:Monitors whose icon names it
 * (ToolType BOARDTYPE=Multicast loads LIBS:Picasso96/Multicast.card); every
 * monitor is a copy of the same small program. AROS's DEVS:Monitors/P96
 * loads the card named after the file instead. So:
 *
 *   1. Multicast.card and Wrap.card go to LIBS:Picasso96/, from FROM=, or
 *      the package's Libs/Picasso96/ beside this program or in the current
 *      directory (install_card); mctest and wrapstat from the package's C/
 *      to C:, so an old copy there doesn't answer for the new cards.
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
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"

struct Library *IconBase;
struct Library *SocketBase;

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

/*
 * The monitors Install moved to SYS:Storage/Monitors, in
 * ENVARC:P96Multicast.moved, so a later setup that doesn't need them out of
 * the way (back from MIRROR to SINGLE, say) puts them back.
 */
#define MOVED_FILE "ENVARC:P96Multicast.moved"
#define MAXMOVED 16
static char moved[MAXMOVED][32];
static int nmoved;

static void remember_moved(const char *name)
{
	int i;

	for (i = 0; i < nmoved; i++)
		if (!mp_stricmp(moved[i], name))
			return;
	if (nmoved < MAXMOVED) {
		strncpy(moved[nmoved], name, 31);
		moved[nmoved++][31] = 0;
	}
}

static void load_moved(void)
{
	BPTR fh;
	char line[40];

	nmoved = 0;
	if (!(fh = Open((STRPTR)MOVED_FILE, MODE_OLDFILE)))
		return;
	while (FGets(fh, (STRPTR)line, sizeof(line))) {
		int n = strlen(line);

		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (n)
			remember_moved(line);
	}
	Close(fh);
}

static void save_moved(void)
{
	BPTR fh;
	int i;

	if (dry)
		return;
	if (!nmoved) {
		DeleteFile((STRPTR)MOVED_FILE);
		return;
	}
	if (!(fh = Open((STRPTR)MOVED_FILE, MODE_NEWFILE)))
		return;
	for (i = 0; i < nmoved; i++) {
		FPuts(fh, (STRPTR)moved[i]);
		FPutC(fh, '\n');
	}
	Close(fh);
}

/* boards whose monitors must be out of the way for this setup */
#define MAXRETIRE 8
static char retire[MAXRETIRE][32];
static int nretire;

static void add_retire(const char *card)
{
	if (*card && nretire < MAXRETIRE)
		board_name(card, retire[nretire++], 32);
}

static BOOL to_retire(const char *board)
{
	int i;

	for (i = 0; i < nretire; i++)
		if (!mp_stricmp(retire[i], board))
			return TRUE;
	return FALSE;
}

/* what Install moved and this setup doesn't need moved: back to DEVS:Monitors */
static void restore_moved(void)
{
	int i, j;

	for (i = 0; i < nmoved; i++) {
		char from[80], to[80], b[32];

		snprintf(from, sizeof(from), "SYS:Storage/Monitors/%s", moved[i]);
		snprintf(to, sizeof(to), "DEVS:Monitors/%s", moved[i]);
		icon_board(from, b, sizeof(b));
		if (to_retire(moved[i]) || (b[0] && to_retire(b)))
			continue;
		if (exists(from) && !exists(to)) {
			if (!move_file(from, to)) {
				notef("Could not move %s back to %s", from, to);
				failed = TRUE;
				continue;
			}
			notef("Moved %s back to %s", from, to);
		}
		for (j = i; j < nmoved - 1; j++)
			strcpy(moved[j], moved[j + 1]);
		nmoved--;
		i--;
	}
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
			    mp_stricmp((char *)fib->fib_FileName, keep)) {
				strncpy(names[n], (char *)fib->fib_FileName, 31);
				names[n++][31] = 0;
			}
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
		if (move_file(from, to)) {
			notef("Moved %s to SYS:Storage/Monitors (%s)", from,
			      "would set the same card up twice");
			remember_moved(names[i]);
		}
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

/* TRUE if a and b are the same file (one copied onto the other would be lost) */
static BOOL same_file(const char *a, const char *b)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin = me->pr_WindowPtr;
	BPTR la, lb;
	BOOL same = FALSE;

	me->pr_WindowPtr = (APTR)-1;
	la = Lock((STRPTR)a, ACCESS_READ);
	lb = Lock((STRPTR)b, ACCESS_READ);
	me->pr_WindowPtr = oldwin;
	if (la && lb) {
		char na[160], nb[160];

		/* SameLock, and the full names as well, for file systems it can't tell */
		same = SameLock(la, lb) == LOCK_SAME ||
		       (NameFromLock(la, (STRPTR)na, sizeof(na)) &&
		        NameFromLock(lb, (STRPTR)nb, sizeof(nb)) && !mp_stricmp(na, nb));
	}
	if (la)
		UnLock(la);
	if (lb)
		UnLock(lb);
	return same;
}

/*
 * Where the package's cards are: FROM=, then Libs/Picasso96 beside the
 * program or in the current directory (a shell like bash may not give the
 * program a PROGDIR: of its own), then the program's or the current
 * directory itself. Never LIBS:Picasso96 itself: that is where they go.
 */
static void install_card(const char *file)
{
	static const char *from[] = { NULL, "PROGDIR:Libs/Picasso96/", "Libs/Picasso96/",
	                              "PROGDIR:", "" };
	char src[160], dst[100];
	int i;

	snprintf(dst, sizeof(dst), "LIBS:Picasso96/%s", file);
	for (i = source ? 0 : 1; i < 5; i++) {
		const char *d = i ? from[i] : source;
		int l = strlen(d);

		snprintf(src, sizeof(src), "%s%s%s", d,
		         l && d[l - 1] != ':' && d[l - 1] != '/' ? "/" : "", file);
		if (exists(src) && !same_file(src, dst))
			break;
	}
	if (i == 5) {
		if (exists(dst))
			notef("%s is already in LIBS:Picasso96 (no other copy found%s)", file,
			      source ? "" : "; FROM= says where");
		else {
			notef("%s not found: give FROM=<the package's Libs/Picasso96>%s", file, "");
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

/* the package's tools to C: (a tool not found is only mentioned) */
static void install_tool(const char *file)
{
	static const char *from[] = { "PROGDIR:C/", "C/" };
	char src[100], dst[100];
	int i;

	snprintf(dst, sizeof(dst), "C:%s", file);
	for (i = 0; i < 2; i++) {
		snprintf(src, sizeof(src), "%s%s", from[i], file);
		if (exists(src) && !same_file(src, dst))
			break;
	}
	if (i == 2) {
		notef("%s: not found in the package's C/, C:%s left as it is", file, file);
		return;
	}
	if (copy_file(src, dst))
		notef("Copied %s to %s", src, dst);
	else {
		notef("Could not copy %s to %s", src, dst);
		failed = TRUE;
	}
}

/* MulticastPrefs itself to SYS:Prefs, with its icon, so it is at hand */
static void install_program(void)
{
	static const char *from[] = { "PROGDIR:MulticastPrefs", "MulticastPrefs" };
	char ic[60], dic[60];
	int i;

	if (!exists("SYS:Prefs"))
		return;
	for (i = 0; i < 2; i++)
		if (exists(from[i]) && !same_file(from[i], "SYS:Prefs/MulticastPrefs"))
			break;
	if (i == 2)
		return;
	if (!copy_file(from[i], "SYS:Prefs/MulticastPrefs")) {
		notef("Could not copy %s to %s", from[i], "SYS:Prefs/MulticastPrefs");
		return;
	}
	notef("Copied %s to %s", from[i], "SYS:Prefs/MulticastPrefs");
	snprintf(ic, sizeof(ic), "%s.info", from[i]);
	snprintf(dic, sizeof(dic), "SYS:Prefs/MulticastPrefs.info");
	if (exists(ic))
		copy_file(ic, dic);
}

/*
 * The display card to mirror: a monitor in DEVS:Monitors (or one Install
 * moved to SYS:Storage/Monitors) whose card, other than Multicast.card and
 * Wrap.card, is in LIBS:Picasso96. Its board is the icon's BOARDTYPE, or
 * the monitor's name (AROS).
 */
static BOOL find_display_card(char *card, int len)
{
	static const char *dirs[] = { "DEVS:Monitors", "SYS:Storage/Monitors" };
	int d;

	for (d = 0; d < 2; d++) {
		struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
		BPTR lock = Lock((STRPTR)dirs[d], ACCESS_READ);
		BOOL found = FALSE;

		if (fib && lock && Examine(lock, fib)) {
			while (!found && ExNext(lock, fib)) {
				char path[80], b[32], lib[80];
				int l = strlen((char *)fib->fib_FileName);

				if (fib->fib_DirEntryType > 0 ||
				    (l > 5 && !mp_stricmp((char *)fib->fib_FileName + l - 5, ".info")))
					continue;
				snprintf(path, sizeof(path), "%s/%s", dirs[d], fib->fib_FileName);
				icon_board(path, b, sizeof(b));
				if (!b[0])
					board_name((char *)fib->fib_FileName, b, sizeof(b));
				if (!mp_stricmp(b, "Multicast") || !mp_stricmp(b, "Wrap"))
					continue;
				snprintf(lib, sizeof(lib), "LIBS:Picasso96/%s.card", b);
				if (exists(lib)) {
					snprintf(card, len, "%s.card", b);
					found = TRUE;
				}
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

static void set(const char *key, const char *value)
{
	prefs_set(find_setting(key), value);
}

/* SETUP=SINGLE, TWO, MIRROR or SPAN: the settings for it */
static BOOL apply_setup(const char *setup)
{
	char card[40], nb[8];

	if (!mp_stricmp(setup, "single") || !mp_stricmp(setup, "two")) {
		set("CARD", "");
		set("MIRROR", "");
		set("SPAN", "");
		set("BOARDS", mp_stricmp(setup, "two") ? "1" : "2");
		say(mp_stricmp(setup, "two") ? "Setup: one Multicast screen" :
		    "Setup: two Multicast screens (ports 9696 and 9697)");
		return TRUE;
	}
	if (!mp_stricmp(setup, "mirror")) {
		const char *c = prefs_value(find_setting("CARD"));

		if (*c && mp_stricmp(c, "Multicast.card"))
			snprintf(card, sizeof(card), "%s", c);
		else if (!find_display_card(card, sizeof(card))) {
			say("Setup MIRROR: no display card found (a monitor in DEVS:Monitors whose"
			    " card is in LIBS:Picasso96); give CARD= yourself.");
			return FALSE;
		}
		set("CARD", card);
		set("MIRROR", "Multicast.card");
		set("SPAN", "");
		set("HEADSHARE", "1");
		set("MIRRORFPS", "10");
		set("BOARDS", "1");
		notef("Setup: the %s screen mirrored to a Multicast board%s", card,
		      " (port 9696)");
		return TRUE;
	}
	if ((setup[0] | 32) == 's' && (setup[1] | 32) == 'p' && (setup[2] | 32) == 'a' &&
	    (setup[3] | 32) == 'n') {
		/* SPAN, or SPAN2x1, SPAN2x2 ...: the display card's screen over Multicast boards */
		const char *c = prefs_value(find_setting("CARD"));
		const char *size = setup[4] ? setup + 4 : "2x1";
		const char *why = prefs_check(find_setting("SPAN"), size);
		LONG cols = size[0] - '0', rows = size[2] - '0';

		if (why) {
			notef("Setup SPAN%s: %s", setup + 4, why);
			return FALSE;
		}
		if (*c && mp_stricmp(c, "Multicast.card"))
			snprintf(card, sizeof(card), "%s", c);
		else if (!find_display_card(card, sizeof(card))) {
			say("Setup SPAN: no display card found (a monitor in DEVS:Monitors whose"
			    " card is in LIBS:Picasso96); give CARD= yourself.");
			return FALSE;
		}
		set("CARD", card);
		set("MIRROR", "");
		set("SPAN", size);
		set("HEAD", "Multicast.card");
		set("HEAD1", "");
		set("HEAD2", "");
		set("HEAD3", "");
		set("HEAD4", "");
		set("HEADSHARE", "1");
		set("MIRRORFPS", "10");
		snprintf(nb, sizeof(nb), "%ld", cols * rows);
		set("BOARDS", nb);
		notef("Setup: the %s screen over %s Multicast boards, a part on each,", card, size);
		say(cols * rows == 2 ? "       one desktop (ports 9696 and 9697)" :
		    "       one desktop (ports from 9696 on)");
		return TRUE;
	}
	say("SETUP must be SINGLE, TWO, MIRROR or SPAN (SPAN2x1, SPAN2x2, ...)");
	return FALSE;
}

/*
 * Screen modes: P96's own, if its settings (Picasso96Mode) have any for
 * Multicast, else the card's (MODES=): a board without modes can't be used.
 */
static void auto_modes(void)
{
	static const char *standard = "640x480,800x600,1024x768,1280x720,1280x960,1280x1024,1920x1080";
	const char *m = prefs_get(find_setting("MODES"));
	BOOL p96 = FALSE;
	BPTR fh;

	if (m && *m)
		return;
	if ((fh = Open((STRPTR)"DEVS:Picasso96Settings", MODE_OLDFILE)) != 0) {
		UBYTE *buf = AllocMem(4096, MEMF_ANY);
		LONG n, keep = 0;

		/* the board's name in the settings, across 4 KB blocks */
		while (buf && !p96 && (n = Read(fh, buf + keep, 4096 - keep)) > 0) {
			LONG i, len = keep + n;

			for (i = 0; i + 9 <= len && !p96; i++)
				p96 = !memcmp(buf + i, "Multicast", 9);
			keep = len < 8 ? len : 8;
			memmove(buf, buf + len - keep, keep);
		}
		if (buf)
			FreeMem(buf, 4096);
		Close(fh);
	}
	if (p96) {
		say("Screen modes: Picasso96Mode has some for Multicast, so those (MODES= would "
		    "add the card's own).");
		return;
	}
	set("MODES", standard);
	notef("Screen modes: the card's own, MODES=%s%s", standard, "");
}

/* Where to send, if not set: UAE's NAT network passes no multicast */
static void auto_dest(void)
{
	const char *d = prefs_get(find_setting("DEST"));
	ULONG id = 0;

	if (d && *d)
		return;
	Forbid();
	if (FindName(&SysBase->LibList, "bsdsocket.library"))
		SocketBase = OpenLibrary("bsdsocket.library", 3);
	Permit();
	if (SocketBase) {
		id = gethostid();
		CloseLibrary(SocketBase);
		SocketBase = NULL;
	}
	if ((id & 0xffffff00UL) == 0x0a000200UL) {
		set("DEST", "10.0.2.2");
		say("Network: UAE's NAT (10.0.2.x): sending to the host, DEST=10.0.2.2");
	}
}

static void summary(void)
{
	const char *card = prefs_value(find_setting("CARD"));
	const char *mirror = prefs_value(find_setting("MIRROR"));
	const char *span = prefs_value(find_setting("SPAN"));
	const char *dest = prefs_value(find_setting("DEST"));
	const char *boards = prefs_value(find_setting("BOARDS"));
	BOOL multicast = (dest[0] == '2' && dest[1] == '2') || (dest[0] == '2' && dest[1] == '3');
	char line[160];
	struct Library *mc;

	say("After the reboot:");
	if (*card && *span)
		notef("  %s's screen spans %s heads", card, span);
	else if (*card && *mirror)
		notef("  %s's screen also goes out through %s", card, mirror);
	else
		notef("  %s Multicast board(s), each its own screen%s", boards, "");
	if (*card && *span)
		snprintf(line, sizeof(line), "  on your computer: python3 p96view.py --span %s%s", span,
		         multicast ? "" : " --group 127.0.0.1");
	else
		snprintf(line, sizeof(line), "  on your computer: python3 p96view.py%s",
		         multicast ? "" : " --group 127.0.0.1");
	say(line);
	if (*card && *span)
		say("  drag windows from one part to the other: it is one screen");
	if (!*card && boards[0] > '1')
		say("  (the second board: add --port 9697)");
	Forbid();
	mc = (struct Library *)FindName(&SysBase->LibList, "Multicast.card");
	if (mc)
		snprintf(line, sizeof(line), "  Multicast.card %u.%u is running now; the installed one starts then",
		         mc->lib_Version, mc->lib_Revision);
	Permit();
	if (mc)
		say(line);
}

BOOL install(void (*note)(const char *), BOOL dry_run, const char *from, const char *setup)
{
	const char *card, *monitor_name;
	BOOL wrap;
	char monitor[80], template[80], icon[90], board[32];
	int i;

	say = note;
	dry = dry_run;
	source = from;
	failed = FALSE;
	IconBase = OpenLibrary("icon.library", 37);
	if (dry)
		say("Would do:");

	if (setup && !apply_setup(setup)) {
		failed = TRUE;
		goto out;
	}
	auto_modes();
	auto_dest();
	prefs_fix(note);
	card = prefs_value(find_setting("CARD"));
	wrap = card[0] != 0;
	monitor_name = wrap ? "Wrap" : "Multicast";

	install_card("Multicast.card");
	install_card("Wrap.card");
	install_tool("mctest");
	install_tool("wrapstat");
	install_program();

	/*
	 * Nothing else may set up what this monitor's card sets up: with
	 * Wrap.card, the card it wraps and the heads; without, Wrap.card. What
	 * an earlier setup moved away and this one doesn't need moved comes back
	 * first (this setup's own monitor among them, perhaps).
	 */
	load_moved();
	nretire = 0;
	if (wrap) {
		static const char *keys[] = { "MIRROR", "HEAD", "HEAD1", "HEAD2", "HEAD3", "HEAD4" };

		add_retire(card);
		for (i = 0; i < 6; i++)
			add_retire(prefs_value(find_setting(keys[i])));
	} else {
		add_retire("Wrap");
	}
	restore_moved();

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

	for (i = 0; i < nretire; i++)
		retire_monitors(retire[i], monitor_name);
	save_moved();
	if (!failed)
		summary();
out:
	say(failed ? "Not everything could be done." : "Reboot to start.");
	if (IconBase)
		CloseLibrary(IconBase);
	IconBase = NULL;
	return !failed;
}
