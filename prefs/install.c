/*
 * Panorama - PanoramaPrefs: install the cards and their monitor.
 *
 * P96 sets a board up for each monitor in DEVS:Monitors whose icon names it
 * (ToolType BOARDTYPE=Panorama loads LIBS:Picasso96/Panorama.card); every
 * monitor is a copy of the same small program. AROS's DEVS:Monitors/P96
 * loads the card named after the file instead. So:
 *
 *   1. Panorama.card and Wrap.card go to LIBS:Picasso96/, from FROM=, or
 *      the package's Libs/Picasso96/ beside this program or in the current
 *      directory (install_card); mctest and wrapstat from the package's C/
 *      to C:, so an old copy there doesn't answer for the new cards.
 *   2. The monitor: DEVS:Monitors/Wrap if Wrap.card is used (CARD= set),
 *      else DEVS:Monitors/Panorama, copied from a P96 monitor already
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
#include <dos/dostags.h>
#include <libraries/commodities.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/commodities.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>
#include "mcprefs.h"
#include "lang.h"

struct Library *IconBase;
struct Library *SocketBase;
struct Library *CxBase;

static void (*say)(const char *);
static BOOL dry;
static const char *source;   /* FROM= */
static BOOL failed;

/* a line of the report: the format translated as a whole, then filled in */
static void notef(const char *fmt, const char *a, const char *b)
{
	char buf[160];

	snprintf(buf, sizeof(buf), tr(fmt), a, b);
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
 * ENVARC:Panorama.moved, so a later setup that doesn't need them out of
 * the way (back from MIRROR to SINGLE, say) puts them back.
 */
#define MOVED_FILE "ENVARC:Panorama.moved"
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
	snprintf(tt, sizeof(tt), "BOARDTYPE=%s", name ? name : "");
	types[0] = name ? (STRPTR)tt : NULL;
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

/*
 * panoramapic.datatype (pictures decoded by the viewer) to
 * SYS:Classes/DataTypes, and its descriptors JPEG and PNG to
 * DEVS:DataTypes, unless a descriptor of that name is there already (a
 * JPEG datatype of the user's own stays). picture.datatype V43 (Picasso96
 * installs one; AmigaOS 3.2 has V47) is what it needs: said if missing.
 */
static void install_datatype(void)
{
	static const char *const names[] = { "JPEG", "PNG", NULL };
	static const char *const from[] = { "PROGDIR:", "" };
	char src[100], dst[100];
	int i, k, n = 0;

	for (i = 0; i < 2; i++) {
		snprintf(src, sizeof(src), "%sClasses/DataTypes/panoramapic.datatype", from[i]);
		if (exists(src))
			break;
	}
	if (i == 2) {
		notef("panoramapic.datatype: not found in the package, pictures stay as they are%s%s", "", "");
		return;
	}
	if (!copy_file(src, "SYS:Classes/DataTypes/panoramapic.datatype")) {
		notef("Could not copy %s to SYS:Classes/DataTypes/%s", src, "");
		failed = TRUE;
		return;
	}
	notef("Copied %s to SYS:Classes/DataTypes/%s", src, "");
	for (k = 0; names[k]; k++) {
		snprintf(dst, sizeof(dst), "DEVS:DataTypes/%s", names[k]);
		if (exists(dst)) {
			notef("DEVS:DataTypes/%s is there already: left as it is (remove it for Panorama's)%s",
			      names[k], "");
			continue;
		}
		snprintf(src, sizeof(src), "%sDevs/DataTypes/%s", from[i], names[k]);
		if (copy_file(src, dst))
			n++;
		else {
			notef("Could not copy %s to %s", src, dst);
			failed = TRUE;
		}
	}
	if (n)
		notef("DEVS:DataTypes: JPEG and PNG pictures are decoded by the viewer (panoview) "
		      "from the next boot%s%s", "", "");
	{
		struct Library *pic = OpenLibrary((STRPTR)"datatypes/picture.datatype", 43);

		if (pic)
			CloseLibrary(pic);
		else
			notef("  picture.datatype V43 is missing: install Picasso96's (Classes/DataTypes) "
			      "for them to show%s%s", "", "");
	}
}

/*
 * ATBOOT=1: PanoramaDesk in SYS:WBStartup, with an icon, so the shared
 * desktop opens at every boot; ATBOOT=0 takes it out again.
 */
static void boot_desktop(void)
{
	static const char *wb = "SYS:WBStartup/PanoramaDesk";
	BOOL on = prefs_value(find_setting("ATBOOT"))[0] == '1';

	if (!exists("SYS:WBStartup"))
		return;
	if (!on) {
		if (exists(wb)) {
			if (!dry) {
				DeleteFile((STRPTR)wb);
				DeleteFile((STRPTR)"SYS:WBStartup/PanoramaDesk.info");
			}
			notef("Took %s out: the desktop opens when you start it%s", wb, "");
		}
		return;
	}
	if (!exists("C:PanoramaDesk")) {
		notef("C:PanoramaDesk is missing: %s not made%s", wb, "");
		return;
	}
	if (!copy_file("C:PanoramaDesk", wb)) {
		notef("Could not copy C:PanoramaDesk to %s%s", wb, "");
		failed = TRUE;
		return;
	}
	if (!write_icon(wb, NULL, NULL))
		notef("Could not make %s.info%s", wb, "");
	notef("Copied C:PanoramaDesk to %s: the desktop opens at every boot%s", wb, "");
}

/*
 * Before 3.1 this was P96Multicast: Multicast.card, MulticastPrefs and
 * MulticastDesktop. Their monitor goes to SYS:Storage/Monitors (P96 would
 * still load the old card for it), the old programs go, so the old
 * commodity doesn't start at boot. Multicast.prefs stays, as it was: the
 * settings were read from it and are saved as Panorama.prefs.
 */
static void old_names(void)
{
	static const char *const gone[] = {
		"SYS:WBStartup/MulticastDesktop", "SYS:WBStartup/MulticastDesktop.info",
		"SYS:Prefs/MulticastPrefs", "SYS:Prefs/MulticastPrefs.info",
		"C:MulticastDesktop", "C:MulticastPrefs", "LIBS:Picasso96/Multicast.card", NULL
	};
	int i;

	if (exists("DEVS:Monitors/Multicast")) {
		make_dir("SYS:Storage");
		make_dir("SYS:Storage/Monitors");
		if (move_file("DEVS:Monitors/Multicast", "SYS:Storage/Monitors/Multicast"))
			notef("Moved the old DEVS:Monitors/Multicast to SYS:Storage/Monitors%s", "", "");
	}
	for (i = 0; gone[i]; i++)
		if (exists(gone[i])) {
			if (!dry)
				DeleteFile((STRPTR)gone[i]);
			notef("Deleted the old %s%s (now Panorama)", gone[i], "");
		}
	if (!dry && exists("ENVARC:P96Multicast.moved") && !exists(MOVED_FILE))
		Rename((STRPTR)"ENVARC:P96Multicast.moved", (STRPTR)MOVED_FILE);
}

/* a script from the package's Scripts/ to C:, marked as a script, so its name runs it */
static void install_script(const char *file)
{
	static const char *from[] = { "PROGDIR:Scripts/", "Scripts/" };
	char src[100], dst[100];
	int i;

	snprintf(dst, sizeof(dst), "C:%s", file);
	for (i = 0; i < 2; i++) {
		snprintf(src, sizeof(src), "%s%s", from[i], file);
		if (exists(src) && !same_file(src, dst))
			break;
	}
	if (i == 2)
		return;
	if (copy_file(src, dst)) {
		if (!dry)
			SetProtection((STRPTR)dst, FIBF_SCRIPT);
		notef("Copied %s to %s", src, dst);
	} else {
		notef("Could not copy %s to %s", src, dst);
	}
}

/* PanoramaPrefs itself to SYS:Prefs, with its icon, so it is at hand */
static void install_program(void)
{
	static const char *from[] = { "PROGDIR:PanoramaPrefs", "PanoramaPrefs" };
	char ic[60], dic[60];
	int i;

	if (!exists("SYS:Prefs"))
		return;
	for (i = 0; i < 2; i++)
		if (exists(from[i]) && !same_file(from[i], "SYS:Prefs/PanoramaPrefs"))
			break;
	if (i == 2)
		return;
	if (!copy_file(from[i], "SYS:Prefs/PanoramaPrefs")) {
		notef("Could not copy %s to %s", from[i], "SYS:Prefs/PanoramaPrefs");
		return;
	}
	notef("Copied %s to %s", from[i], "SYS:Prefs/PanoramaPrefs");
	snprintf(ic, sizeof(ic), "%s.info", from[i]);
	snprintf(dic, sizeof(dic), "SYS:Prefs/PanoramaPrefs.info");
	if (exists(ic))
		copy_file(ic, dic);
}

/*
 * The display card to mirror: a monitor in DEVS:Monitors (or one Install
 * moved to SYS:Storage/Monitors) whose card, other than Panorama.card and
 * Wrap.card, is in LIBS:Picasso96. Its board is the icon's BOARDTYPE, or
 * the monitor's name (AROS).
 */
static BOOL find_display_card_at(char *card, int len, char *monpath, int mlen, char *board,
                                 int blen);

static BOOL find_display_card(char *card, int len)
{
	return find_display_card_at(card, len, NULL, 0, NULL, 0);
}

static BOOL find_display_card_at(char *card, int len, char *monpath, int mlen, char *board,
                                 int blen)
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
				if (!mp_stricmp(b, "Panorama") || !mp_stricmp(b, "Wrap"))
					continue;
				snprintf(lib, sizeof(lib), "LIBS:Picasso96/%s.card", b);
				if (exists(lib)) {
					snprintf(card, len, "%s.card", b);
					if (monpath)
						snprintf(monpath, mlen, "%s", path);
					if (board)
						snprintf(board, blen, "%s", b);
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

static BOOL is_multicast_card(const char *card)
{
	return !mp_stricmp(card, "Panorama.card");
}

/* SETUP=SINGLE, TWO, MIRROR, SPAN or DESKTOP: the settings for it */
static BOOL apply_setup(const char *setup)
{
	char card[40], nb[8];

	if (!mp_stricmp(setup, "single") || !mp_stricmp(setup, "two")) {
		set("CARD", "");
		set("MIRROR", "");
		set("SPAN", "");
		set("BOARDS", mp_stricmp(setup, "two") ? "1" : "2");
		set("SPLIT", "");
		say(mp_stricmp(setup, "two") ? "Setup: one Panorama screen" :
		    "Setup: two Panorama screens (ports 9696 and 9697)");
		return TRUE;
	}
	if (!mp_stricmp(setup, "mirror")) {
		const char *c = prefs_value(find_setting("CARD"));

		if (*c && mp_stricmp(c, "Panorama.card"))
			snprintf(card, sizeof(card), "%s", c);
		else if (!find_display_card(card, sizeof(card))) {
			say("Setup MIRROR: no display card found (a monitor in DEVS:Monitors whose"
			    " card is in LIBS:Picasso96); give CARD= yourself.");
			return FALSE;
		}
		set("CARD", card);
		set("MIRROR", "Panorama.card");
		set("SPAN", "");
		set("HEADSHARE", "1");
		set("MIRRORFPS", "10");
		set("BOARDS", "1");
		set("SPLIT", "");
		notef("Setup: the %s screen mirrored to a Panorama board%s", card,
		      " (port 9696)");
		return TRUE;
	}
	if ((setup[0] | 32) == 's' && (setup[1] | 32) == 'p' && (setup[2] | 32) == 'a' &&
	    (setup[3] | 32) == 'n') {
		/* SPAN, or SPAN2x1, SPAN2x2 ...: the display card's screen over Panorama boards */
		const char *c = prefs_value(find_setting("CARD"));
		const char *size = setup[4] ? setup + 4 : "2x1";
		const char *why = prefs_check(find_setting("SPAN"), size);
		LONG cols = size[0] - '0', rows = size[2] - '0';

		if (why) {
			notef("Setup SPAN%s: %s", setup + 4, why);
			return FALSE;
		}
		if (*c && mp_stricmp(c, "Panorama.card"))
			snprintf(card, sizeof(card), "%s", c);
		else if (!find_display_card(card, sizeof(card))) {
			say("Setup SPAN: no display card found (a monitor in DEVS:Monitors whose"
			    " card is in LIBS:Picasso96); give CARD= yourself.");
			return FALSE;
		}
		set("CARD", card);
		set("MIRROR", "");
		set("SPAN", size);
		set("HEAD", "Panorama.card");
		set("HEAD1", "");
		set("HEAD2", "");
		set("HEAD3", "");
		set("HEAD4", "");
		set("HEADSHARE", "1");
		set("MIRRORFPS", "10");
		snprintf(nb, sizeof(nb), "%ld", cols * rows);
		set("BOARDS", nb);
		set("SPLIT", "");
		notef("Setup: the %s screen over %s Panorama boards, a part on each,", card, size);
		say(cols * rows == 2 ? "       one desktop (ports 9696 and 9697)" :
		    "       one desktop (ports from 9696 on)");
		return TRUE;
	}
	if ((setup[0] | 32) == 'd' && (setup[1] | 32) == 'e' && (setup[2] | 32) == 's' &&
	    (setup[3] | 32) == 'k' && (setup[4] | 32) == 't' && (setup[5] | 32) == 'o' &&
	    (setup[6] | 32) == 'p') {
		/*
		 * DESKTOP, DESKTOP2x2: one Panorama board, its screen split in
		 * parts, each sent to a port of its own (SPLIT=) for a window each.
		 * No Wrap.card: the board keeps its own modes, twice as wide.
		 */
		const char *size = setup[7] ? setup + 7 : "2x1";
		const char *why = prefs_check(find_setting("SPLIT"), size);

		if (why) {
			notef("Setup DESKTOP%s: %s", setup + 7, why);
			return FALSE;
		}
		set("CARD", "");
		set("MIRROR", "");
		set("SPAN", "");
		set("BOARDS", "1");
		set("SPLIT", size);
		set("VRAM", "16");
		set("MODES", size[0] == '2' && size[2] == '1' ?
		    "1280x480,1600x600,2048x768,2560x1024,640x480,800x600" :
		    size[0] == '2' && size[2] == '2' ?
		    "1280x960,1600x1200,2048x1536,640x480,800x600" :
		    "640x480,800x600,1024x768,1280x1024");
		set("INPUT", "1");
		set("WBMONITOR", "1");
		set("P96SCREENCX", "1");
		notef("Setup: one Panorama screen split %s, a window for each part%s", size,
		      ", one desktop; the Workbench stays where it is");
		say("       the viewers' mouse and keys work it (INPUT=1; INPUTFROM= limits who)");
		return TRUE;
	}
	say("SETUP must be SINGLE, TWO, MIRROR, SPAN (SPAN2x2) or DESKTOP (DESKTOP2x2)");
	return FALSE;
}

/*
 * Screen modes: P96's own, if its settings (Picasso96Mode) have any for
 * Panorama, else the card's (MODES=): a board without modes can't be used.
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

			for (i = 0; i + 8 <= len && !p96; i++)
				p96 = !memcmp(buf + i, "Panorama", 8);
			keep = len < 7 ? len : 7;
			memmove(buf, buf + len - keep, keep);
		}
		if (buf)
			FreeMem(buf, 4096);
		Close(fh);
	}
	/* under Wrap.card the board is a "Wrap" board: Picasso96Mode's Panorama modes don't count */
	if (p96 && !*prefs_value(find_setting("CARD"))) {
		say("Screen modes: Picasso96Mode has some for Panorama, so those (MODES= would "
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
	const char *split = prefs_value(find_setting("SPLIT"));
	BOOL multicast = (dest[0] == '2' && dest[1] == '2') || (dest[0] == '2' && dest[1] == '3');
	char line[160];
	struct Library *mc;

	say("After the reboot:");
	BOOL keep = prefs_value(find_setting("KEEPDISPLAY"))[0] == '1' && !*mirror && !*span;

	if ((!*card || keep) && *split && strcmp(split, "1x1")) {
		if (keep)
			notef("  the Workbench (%s, through Wrap.card) stays shown beside the desktop%s",
			      card, "");
		if (prefs_value(find_setting("P96SCREENCX"))[0] == '1')
			say("  the mouse crosses to the Workbench with P96ScreenCx (Aminet util/cdity)");
		if (prefs_value(find_setting("ATBOOT"))[0] != '1') {
			say("  open the desktop: DesktopOpen (or Execute C:DesktopOpen, or");
			say("  double-click Scripts/DesktopOpen): it opens a Shell in the middle too");
		} else {
			say("  the desktop opens by itself (SYS:WBStartup/PanoramaDesk)");
		}
		say("  hotkeys: ctrl alt + cursor keys: the active window to the next monitor;");
		say("  ctrl alt 1-4: to the middle of that monitor; ctrl alt m: desktop or Workbench");
		say("  Boing bounces a ball over all of it");
		snprintf(line, sizeof(line), "  on your computer: python3 panoview.py --span %s%s",
		         split, multicast ? "" : " --group 127.0.0.1");
		say(line);
		say("  a window for each part; use the mouse and keys in them (INPUT=1),");
		say("  and drag windows from one to the other");
		goto running;
	}
	if (*card && *span)
		notef("  %s's screen spans %s heads", card, span);
	else if (*card && *mirror)
		notef("  %s's screen also goes out through %s", card, mirror);
	else
		notef("  %s Panorama board(s), each its own screen%s", boards, "");
	if (*card && *span)
		snprintf(line, sizeof(line), "  on your computer: python3 panoview.py --span %s%s", span,
		         multicast ? "" : " --group 127.0.0.1");
	else
		snprintf(line, sizeof(line), "  on your computer: python3 panoview.py%s",
		         multicast ? "" : " --group 127.0.0.1");
	say(line);
	if (*card && *span)
		say("  drag windows from one part to the other: it is one screen");
	if (!*card && boards[0] > '1')
		say("  (the second board: add --port 9697)");
running:
	Forbid();
	mc = (struct Library *)FindName(&SysBase->LibList, "Panorama.card");
	if (mc)
		snprintf(line, sizeof(line), "  Panorama.card %u.%u is running now; the installed one starts then",
		         mc->lib_Version, mc->lib_Revision);
	Permit();
	if (mc)
		say(line);
}

/*
 * The new settings and files take effect at the next boot: give the disks a
 * moment to finish writing, then reboot.
 */
void mp_reboot(void)
{
	Delay(2 * TICKS_PER_SECOND);
	ColdReboot();
}

/*
 * The monitor's icon with its tool types changed: each "KEY=VALUE" of set
 * replaces the KEY's line (or is added), each KEY of drop goes. The others
 * stay as they were.
 */
static BOOL set_tooltypes(const char *monitor, const char **set, int nset, const char **drop,
                          int ndrop)
{
	struct DiskObject *dob;
	STRPTR *old, types[40];
	int n = 0, i, j;
	BOOL ok;

	if (!IconBase || !(dob = GetDiskObject((STRPTR)monitor)))
		return FALSE;
	old = dob->do_ToolTypes;
	for (i = 0; old && old[i] && n < 30; i++) {
		const char *t = (const char *)old[i];
		BOOL keep = TRUE;

		for (j = 0; j < nset + ndrop && keep; j++) {
			const char *k = j < nset ? set[j] : drop[j - nset];
			int l = 0;

			while (k[l] && k[l] != '=')
				l++;
			if (!strncmp(t, k, l) && (t[l] == '=' || t[l] == 0))
				keep = FALSE;
		}
		if (keep)
			types[n++] = old[i];
	}
	for (j = 0; j < nset && n < 39; j++)
		types[n++] = (STRPTR)set[j];
	types[n] = NULL;
	ok = TRUE;
	if (!dry) {
		dob->do_ToolTypes = types;
		ok = PutDiskObject((STRPTR)monitor, dob);
		dob->do_ToolTypes = old;
	}
	FreeDiskObject(dob);
	return ok;
}

/*
 * The monitors' places for P96ScreenCx (Aminet util/cdity/P96ScreenCx),
 * which moves the mouse between boards as between monitors side by side:
 * the Panorama monitor is a monitor of its own (DISPLAYCHAIN=NO) next to
 * the Workbench's board, on the side WORKBENCH= says. WBMONITOR=1: the
 * Workbench's board is one of its own too, so it keeps its screen shown
 * while the desktop is in front, instead of going blank.
 */
void place_monitors(void (*note)(const char *))
{
	static const char *drop[] = { "LEFT_OF", "RIGHT_OF", "ABOVE", "BELOW" };
	static const char *opposite[] = { "", "RIGHT_OF", "LEFT_OF", "BELOW", "ABOVE" };
	static char place[48];
	const char *set[2];
	char card[40], wbmon[80], wbboard[32];
	int side = WB_NONE, mon = 0, nset = 0;
	BOOL wbmonitor = prefs_value(find_setting("WBMONITOR"))[0] == '1';
	BOOL cx = prefs_value(find_setting("P96SCREENCX"))[0] == '1';
	BOOL opened = FALSE;

	say = note;
	if (!IconBase) {
		IconBase = OpenLibrary("icon.library", 37);
		opened = TRUE;
	}
	if (!IconBase || !exists("DEVS:Monitors/Panorama"))
		goto out;
	if (*prefs_value(find_setting("CARD"))) {
		/* Wrap.card: only when it wraps the Workbench's card beside the desktop */
		const char *m = prefs_get(find_setting("MIRROR")), *sp = prefs_get(find_setting("SPAN"));

		if ((m && *m) || (sp && *sp) || !exists("DEVS:Monitors/Wrap"))
			goto out;
		strcpy(wbmon, "DEVS:Monitors/Wrap");
		strcpy(wbboard, "Wrap");
	} else if (!find_display_card_at(card, sizeof(card), wbmon, sizeof(wbmon), wbboard,
	                                 sizeof(wbboard))) {
		wbmon[0] = wbboard[0] = 0;
	}
	if (wbmon[0]) {
		/* its own monitor for P96ScreenCx (and the Workbench kept shown with Wrap.card) */
		static const char *own[] = { "DISPLAYCHAIN=NO" };

		if (wbmonitor && set_tooltypes(wbmon, own, 1, NULL, 0))
			notef("%s: DISPLAYCHAIN=NO (a monitor of its own)%s", wbmon, "");
	}
	wb_place(prefs_value(find_setting("WORKBENCH")), &side, &mon);
	set[nset++] = "DISPLAYCHAIN=NO";
	if (cx && side != WB_NONE) {
		snprintf(place, sizeof(place), "%s=%s", opposite[side],
		         wbmonitor && wbboard[0] ? wbboard : "Native");
		set[nset++] = place;
	}
	if (set_tooltypes("DEVS:Monitors/Panorama", set, nset, drop, 4))
		notef("DEVS:Monitors/Panorama: %s%s", nset > 1 ? place : "DISPLAYCHAIN=NO",
		      nset > 1 ? " (for P96ScreenCx: the mouse crosses to the Workbench)" : "");
out:
	if (opened && IconBase) {
		CloseLibrary(IconBase);
		IconBase = NULL;
	}
}

/*
 * WBMONITOR=1 with a desktop (SPLIT=, no MIRROR or SPAN): Wrap.card wraps
 * the Workbench's card, only to keep its display on (KEEPDISPLAY=1), so the
 * Workbench stays shown while the desktop's screen is in front; the
 * Panorama board is set up beside it. WBMONITOR=0 undoes that. TRUE: the
 * Workbench's card is wrapped so.
 */
static BOOL wb_wrap(void (*note)(const char *))
{
	const char *split = prefs_get(find_setting("SPLIT"));
	const char *mirror = prefs_get(find_setting("MIRROR"));
	const char *span = prefs_get(find_setting("SPAN"));
	BOOL plain = (!mirror || !*mirror) && (!span || !*span);
	char card[40];

	if (!plain)
		return FALSE;
	if (prefs_value(find_setting("WBMONITOR"))[0] != '1' || !split || !*split) {
		if (prefs_value(find_setting("KEEPDISPLAY"))[0] == '1') {
			set("CARD", "");
			set("KEEPDISPLAY", "0");
			note("Wrap.card no longer wraps the Workbench's card (WBMONITOR=0)");
		}
		return FALSE;
	}
	if (!find_display_card(card, sizeof(card))) {
		note("WBMONITOR=1: no display card found for the Workbench, left as it is");
		return FALSE;
	}
	set("CARD", card);
	set("KEEPDISPLAY", "1");
	set("DRAW", "0");
	set("BOARDS", "1");
	notef("WBMONITOR=1: Wrap.card wraps %s and keeps its display on, so the Workbench%s",
	      card, " stays shown beside the desktop");
	return TRUE;
}

/* DEVS:Monitors/name, made from a P96 monitor there if missing, its icon BOARDTYPE=name */
static void make_monitor(const char *monitor_name)
{
	char monitor[80], template[80], icon[90], board[32];

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
}

BOOL install(void (*note)(const char *), BOOL dry_run, const char *from, const char *setup)
{
	const char *card, *monitor_name;
	BOOL wrap, wbwrap;
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
	wbwrap = wb_wrap(note);
	prefs_fix(note);
	card = prefs_value(find_setting("CARD"));
	wrap = card[0] != 0;
	monitor_name = wrap ? "Wrap" : "Panorama";

	old_names();
	install_card("Panorama.card");
	install_card("Wrap.card");
	install_tool("mctest");
	install_tool("wrapstat");
	install_tool("PanoramaDesk");
	install_tool("Boing");
	install_tool("dtpic");
	install_datatype();
	install_script("DesktopOpen");
	install_script("DesktopShell");
	install_script("DesktopBoing");
	install_script("DesktopClose");
	boot_desktop();
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
		/* only wrapping the Workbench's card: no heads, whatever HEAD= says */
		for (i = 0; i < 6 && !wbwrap; i++)
			add_retire(prefs_value(find_setting(keys[i])));
	} else {
		add_retire("Wrap");
	}
	restore_moved();

	make_monitor(monitor_name);
	if (wbwrap)
		make_monitor("Panorama");

	for (i = 0; i < nretire; i++)
		retire_monitors(retire[i], monitor_name);
	if (!wrap || wbwrap)
		place_monitors(note);
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

/* ---------------------------------------------------------------- Open */

/* is a commodity of that name running? (one can't be made twice) */
static BOOL commodity_runs(const char *name)
{
	struct NewBroker nb;
	CxObj *b;
	LONG err = 0;

	CxBase = OpenLibrary("commodities.library", 37);
	if (!CxBase)
		return FALSE;
	memset(&nb, 0, sizeof(nb));
	nb.nb_Version = NB_VERSION;
	nb.nb_Name = (STRPTR)name;
	nb.nb_Title = (STRPTR)name;
	nb.nb_Descr = (STRPTR)"(PanoramaPrefs checking)";
	nb.nb_Unique = NBU_UNIQUE;
	b = CxBroker(&nb, &err);
	if (b)
		DeleteCxObj(b);
	CloseLibrary(CxBase);
	CxBase = NULL;
	return !b && err == CBERR_DUP;
}

/*
 * Before Open starts PanoramaDesk: the viewers' mouse and keys on (the
 * card takes INPUT= while it runs), and P96ScreenCx started if it is
 * installed and not running. Without it, PanoramaDesk crosses to the
 * Workbench itself.
 */
void desktop_ready(void (*note)(const char *))
{
	static const char *const where[] = {
		"SYS:WBStartup/P96ScreenCx", "SYS:Tools/Commodities/P96ScreenCx",
		"SYS:Utilities/P96ScreenCx", "C:P96ScreenCx", NULL
	};
	const struct Setting *in = find_setting("INPUT");
	char cmd[80];
	int i;

	if (prefs_value(in)[0] != '1') {
		prefs_set(in, "1");
		note("INPUT=1: the mouse and keys in the viewer windows work the desktop");
	}
	if (prefs_value(find_setting("P96SCREENCX"))[0] != '1' || commodity_runs("P96ScreenCx"))
		return;
	for (i = 0; where[i]; i++) {
		BPTR l = Lock((STRPTR)where[i], ACCESS_READ);

		if (!l)
			continue;
		UnLock(l);
		snprintf(cmd, sizeof(cmd), "\"%s\"", where[i]);
		mp_trace("starting %s", where[i]);
		{
			BPTR nin = Open((STRPTR)"NIL:", MODE_OLDFILE);
			BPTR nout = Open((STRPTR)"NIL:", MODE_NEWFILE);

			if (SystemTags((STRPTR)cmd, SYS_Input, nin, SYS_Output, nout, SYS_Asynch, TRUE,
			               TAG_END) == -1) {
				if (nin)
					Close(nin);
				if (nout)
					Close(nout);
			}
		}
		return;
	}
}
