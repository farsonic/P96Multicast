/*
 * P96Multicast - Wrap.card: a P96 card driver that wraps another one.
 *
 * rtg.library loads Wrap.card as the board's driver. Wrap.card opens the
 * real driver named by CARD= in ENV:Wrap.prefs, calls its FindCard and
 * InitCard with the same BoardInfo, and then puts a thunk (wrapstart.s) in
 * front of every BoardInfo function the real driver filled in. The thunks
 * count the calls and record what is on display, and call the real
 * function with every register as rtg.library passed it. This version
 * changes nothing on the way: it is the base for mirroring, spanning and
 * other features that need to see what the driver is asked to do.
 * MIRROR= adds the first of them: mirror.c shows the first board's screen
 * on a board of another card as well.
 *
 * The real driver keeps its BoardInfo to itself: Wrap.card never touches
 * CardData or ChipData, and bi->CardBase points at the real driver, for
 * drivers that find their library base there.
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stddef.h>
#include "wrap.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

extern char LibName[];
extern char LibId[];
extern char WrapThunks[];
extern char WrapThunksEnd[];

#define THUNK_SIZE 8

/* ---------------------------------------------------------------- slots */

#define X_ENUM(f, kind, def) SLOT_##f,
enum { WRAP_SLOTS(X_ENUM) SLOT_COUNT };

#define X_KIND(f, kind, def) kind,
static const UBYTE slot_kind[] = { WRAP_SLOTS(X_KIND) };

#define X_DEF(f, kind, def) def,
static const BYTE slot_default[] = { WRAP_SLOTS(X_DEF) };

/* the list matches struct BoardInfo, and each default is its function's neighbour */
#define X_CHECK(f, kind, def) \
	typedef char off_##f[offsetof(struct BoardInfo, f) - \
	                     offsetof(struct BoardInfo, AllocCardMem) == 4 * SLOT_##f ? 1 : -1]; \
	typedef char def_##f[(def) < 0 || (def) == SLOT_##f + 1 ? 1 : -1];
WRAP_SLOTS(X_CHECK)
typedef char nslots_check[SLOT_COUNT == WRAP_NSLOTS ? 1 : -1];
typedef char libsize_check[sizeof(struct WrapBase) <= WRAP_LIBSIZE ? 1 : -1];

/* ---------------------------------------------------------------- state */

static struct WrapStatus *status;
static struct WrapBoard boards[WRAP_MAXBOARDS];
static struct WrapMirror mirror;
static struct Library *inner;

/* ---------------------------------------------------------------- library */

struct Library *LibInit(__reg("d0") struct WrapBase *base, __reg("a0") BPTR seglist,
                        __reg("a6") struct ExecBase *sysbase)
{
	SysBase = sysbase;
	base->seglist = seglist;
	status = &base->status;
	status->magic = WRAP_STATUS_MAGIC;
	status->draw = 1;
	status->boards = boards;
	status->mirror = &mirror;
	mirror.fps = 25;
	base->lib.lib_Node.ln_Type = NT_LIBRARY;
	base->lib.lib_Node.ln_Name = LibName;
	base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
	base->lib.lib_Version = 0;
	base->lib.lib_Revision = 1;
	base->lib.lib_IdString = LibId;

	DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 37);
	if (!DOSBase) {
		FreeMem((UBYTE *)base - base->lib.lib_NegSize,
		        base->lib.lib_NegSize + base->lib.lib_PosSize);
		return NULL;
	}
	return &base->lib;
}

struct Library *LibOpen(__reg("a6") struct WrapBase *base)
{
	base->lib.lib_OpenCnt++;
	base->lib.lib_Flags &= ~LIBF_DELEXP;
	return &base->lib;
}

BPTR LibClose(__reg("a6") struct WrapBase *base)
{
	base->lib.lib_OpenCnt--;
	return 0;
}

/* BoardInfo pointers lead into this library's thunks: once loaded it stays */
BPTR LibExpunge(__reg("a6") struct WrapBase *base)
{
	base->lib.lib_Flags |= LIBF_DELEXP;
	return 0;
}

/* ---------------------------------------------------------------- settings */

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static BOOL same_name(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return !*a && !*b;
}

/* key matches s up to '=' or a blank, ignoring case */
static const char *match_key(const char *s, const char *key)
{
	while (*key) {
		if (lower(*s) != *key)
			return NULL;
		s++;
		key++;
	}
	while (*s == ' ' || *s == '\t')
		s++;
	if (*s != '=')
		return NULL;
	s++;
	while (*s == ' ' || *s == '\t')
		s++;
	return s;
}

/* a card name, up to the first blank */
static void copy_name(char *to, const char *v)
{
	int i;

	for (i = 0; v[i] && v[i] != ' ' && v[i] != '\t' && i < WRAP_NAMELEN - 1; i++)
		to[i] = v[i];
	to[i] = 0;
}

static void config_line(const char *line)
{
	const char *v;

	while (*line == ' ' || *line == '\t')
		line++;
	if ((v = match_key(line, "card")) != NULL) {
		copy_name(status->card, v);
	} else if ((v = match_key(line, "draw")) != NULL) {
		status->draw = *v != '0';
	} else if ((v = match_key(line, "mirror")) != NULL) {
		copy_name(mirror.card, v);
	} else if ((v = match_key(line, "mirrorfps")) != NULL) {
		LONG n = 0;

		while (*v >= '0' && *v <= '9')
			n = n * 10 + (*v++ - '0');
		if (n >= 1 && n <= 50)
			mirror.fps = n;
	}
}

/* Needs a process. A missing ENV: must fail quietly, as in config.c. */
static BOOL config_file(const char *path)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	APTR oldwin;
	BPTR fh;
	char *buf, *p, *line;
	LONG len;

	oldwin = me->pr_WindowPtr;
	me->pr_WindowPtr = (APTR)-1;
	fh = Open((STRPTR)path, MODE_OLDFILE);
	me->pr_WindowPtr = oldwin;
	if (!fh)
		return FALSE;
	buf = AllocMem(1024, MEMF_PUBLIC);
	if (!buf) {
		Close(fh);
		return FALSE;
	}
	len = Read(fh, buf, 1023);
	Close(fh);
	if (len < 0)
		len = 0;
	buf[len] = 0;

	for (line = p = buf; ; p++) {
		if (*p == '\n' || *p == '\r' || *p == 0) {
			char end = *p;

			*p = 0;
			if (*line != '#')
				config_line(line);
			if (!end)
				break;
			line = p + 1;
		}
	}
	FreeMem(buf, 1024);
	return TRUE;
}

/* ---------------------------------------------------------------- the wrapped card */

ULONG wrap_bytesperpixel(ULONG fmt)
{
	switch (fmt) {
	case RGBFB_CLUT:
		return 1;
	case RGBFB_R5G6B5PC: case RGBFB_R5G5B5PC: case RGBFB_R5G6B5:
	case RGBFB_R5G5B5: case RGBFB_B5G6R5PC: case RGBFB_B5G5R5PC:
		return 2;
	case RGBFB_R8G8B8: case RGBFB_B8G8R8:
		return 3;
	case RGBFB_A8R8G8B8: case RGBFB_A8B8G8R8: case RGBFB_R8G8B8A8:
	case RGBFB_B8G8R8A8:
		return 4;
	}
	return 0;
}

/*
 * CARD=uaegfx.card opens LIBS:Picasso96/uaegfx.card; a name with a path
 * is taken as it is. A card already in memory is opened by its name, so
 * it is not loaded a second time. Not Wrap.card itself.
 */
struct Library *wrap_open_card(const char *card)
{
	char path[16 + WRAP_NAMELEN];
	const char *name = (const char *)FilePart((STRPTR)card);
	struct Node *found;
	int i, j;

	if (!card[0] || same_name(name, LibName))
		return NULL;
	Forbid();
	found = FindName(&SysBase->LibList, (STRPTR)name);
	Permit();
	if (found)
		return OpenLibrary((STRPTR)name, 0);
	for (i = 0; card[i] && card[i] != ':'; i++)
		;
	if (card[i] == ':')
		return OpenLibrary((STRPTR)card, 0);
	for (i = 0; "LIBS:Picasso96/"[i]; i++)
		path[i] = "LIBS:Picasso96/"[i];
	for (j = 0; card[j]; j++)
		path[i++] = card[j];
	path[i] = 0;
	return OpenLibrary((STRPTR)path, 0);
}

static BOOL open_inner(void)
{
	if (FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS) {
		if (!config_file("ENV:Wrap.prefs"))
			config_file("ENVARC:Wrap.prefs");
	}
	if (!status->card[0]) {
		status->error = WRAP_ERR_NOCARD;
		return FALSE;
	}
	if (same_name((const char *)FilePart((STRPTR)status->card), LibName)) {
		status->error = WRAP_ERR_SELF;
		return FALSE;
	}
	inner = wrap_open_card(status->card);
	if (!inner) {
		status->error = WRAP_ERR_OPEN;
		return FALSE;
	}
	status->inner = inner;
	status->error = WRAP_ERR_NONE;
	return TRUE;
}

static void close_inner(void)
{
	if (inner)
		CloseLibrary(inner);
	inner = NULL;
	status->inner = NULL;
}

static struct WrapBoard *find_board(struct BoardInfo *bi)
{
	UWORD i;

	for (i = 0; i < status->nboards; i++)
		if (boards[i].bi == bi)
			return &boards[i];
	return NULL;
}

/* ---------------------------------------------------------------- thunks */

/*
 * Called by every thunk before the card's function, in the caller's context:
 * any task, or an interrupt. It must not call the OS. regs holds d0-d7 and
 * a0-a6 as the caller passed them; a change there reaches the card.
 */
APTR wrap_pre(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG slot,
              __reg("a1") ULONG *regs)
{
	struct WrapBoard *b = find_board(bi);
	struct WrapDisplay *d;
	APTR fn;

	if (!b)
		b = &boards[0];   /* cannot happen: thunks go only into known boards */
	fn = slot < WRAP_NSLOTS ? b->inner[slot] : NULL;
	if (!fn)
		return (APTR)LibNull;
	b->calls[slot]++;
	d = &b->disp;

	switch (slot) {
	case SLOT_SetSwitch:
		d->moniswitch = regs[0] & 0xffff ? 1 : 0;
		d->serial++;
		break;
	case SLOT_SetDAC:
		d->format = regs[7];
		d->serial++;
		break;
	case SLOT_SetGC: {
		struct ModeInfo *mi = (struct ModeInfo *)regs[9];

		if (mi) {
			d->mode = *mi;
			d->width = mi->Width;
			d->height = mi->Height;
		}
		d->serial++;
		break;
	}
	case SLOT_SetColorArray:
		d->pal_serial++;
		break;
	case SLOT_SetPanning:
		d->mem = (UBYTE *)regs[9];
		d->bm_width = regs[0];
		d->xoff = regs[1];
		d->yoff = regs[2];
		d->format = regs[7];
		d->serial++;
		break;
	case SLOT_SetDisplay:
		d->display = regs[0] & 0xffff ? 1 : 0;
		d->serial++;
		break;
	}

	regs[14] = (ULONG)inner;   /* a6 for the card's function */
	return fn;
}

static void install_thunks(struct WrapBoard *b)
{
	APTR *slots = &b->bi->AllocCardMem;
	UWORD i;

	for (i = 0; i < WRAP_NSLOTS; i++) {
		APTR fn = slots[i];

		if (slot_kind[i] == WK_NONE || !fn)
			continue;
		if (slot_kind[i] == WK_DRAW && !status->draw)
			continue;
		/* rtg.library's own fallback, not the card's */
		if (slot_default[i] >= 0 && fn == slots[slot_default[i]])
			continue;
		if ((char *)fn >= WrapThunks && (char *)fn < WrapThunksEnd)
			continue;
		b->inner[i] = fn;
		slots[i] = WrapThunks + THUNK_SIZE * i;
		b->wrapped++;
	}
}

/* ---------------------------------------------------------------- FindCard / InitCard */

BOOL FindCard(__reg("a0") struct BoardInfo *bi)
{
	struct Library *oldbase = bi->CardBase;
	struct WrapBoard *b;

	status->findcard_calls++;
	if (!inner && !open_inner())
		return FALSE;
	if (status->nboards >= WRAP_MAXBOARDS) {
		status->error = WRAP_ERR_BOARDS;
		return FALSE;
	}

	bi->CardBase = inner;
	if (!InnerFindCard(bi, inner)) {
		bi->CardBase = oldbase;
		if (!status->nboards)
			close_inner();
		return FALSE;
	}
	status->inner_found++;
	b = &boards[status->nboards++];
	b->bi = bi;
	return TRUE;
}

BOOL InitCard(__reg("a0") struct BoardInfo *bi, __reg("a1") char **tooltypes)
{
	struct WrapBoard *b = find_board(bi);

	status->initcard_calls++;
	if (!b) {
		status->error = WRAP_ERR_UNKNOWN;
		return FALSE;
	}
	bi->CardBase = inner;
	if (!InnerInitCard(bi, tooltypes, inner)) {
		/* rtg.library drops this BoardInfo: forget it */
		struct WrapBoard *last = &boards[--status->nboards];
		UBYTE *p = (UBYTE *)last;
		ULONG n;

		*b = *last;
		for (n = 0; n < sizeof(*last); n++)
			p[n] = 0;
		return FALSE;
	}
	status->inner_init++;

	/* the card works without the thunks, so a broken table only stops the wrapping */
	if (WrapThunksEnd - WrapThunks != THUNK_SIZE * WRAP_NSLOTS) {
		status->error = WRAP_ERR_THUNKS;
		return TRUE;
	}
	install_thunks(b);
	b->initialised = 1;
	if (b == &boards[0] && mirror.card[0] && !mirror.bi)
		wrap_mirror_start(b, inner, &mirror);
	return TRUE;
}
