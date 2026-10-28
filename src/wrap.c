/*
 * Panorama - Wrap.card: a P96 card driver that wraps another one.
 *
 * rtg.library loads Wrap.card as the board's driver. Wrap.card opens the
 * real driver named by CARD= in ENV:Wrap.prefs, calls its FindCard and
 * InitCard with the same BoardInfo, and then puts a thunk (wrapstart.s) in
 * front of every BoardInfo function the real driver filled in. The thunks
 * count the calls and record what is on display, and call the real
 * function with every register as rtg.library passed it. This version
 * changes nothing on the way: it is the base for mirroring, spanning and
 * other features that need to see what the driver is asked to do.
 * MIRROR= and SPAN= add the first of them: mirror.c shows the first
 * board's screen on boards of other cards too, all of it or a tile each.
 *
 * The real driver keeps its BoardInfo to itself: Wrap.card never touches
 * CardData or ChipData. bi->CardBase points at the real driver while its
 * FindCard and InitCard run, for drivers that find their library base there,
 * and at Wrap.card otherwise: P96 calls InitCard through bi->CardBase, so
 * leaving the real driver there would skip Wrap.card's InitCard. The thunks
 * call the real functions with a6 = the real driver.
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

/* MIRROR=, SPAN=, HEAD= and HEAD1= ... as read; heads_config() sorts them out */
static char mirror_card[WRAP_NAMELEN];
static char head_card[WRAP_NAMELEN];
static char head_cards[WRAP_MAXHEADS][WRAP_NAMELEN];
static UWORD span_cols, span_rows;

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
	base->lib.lib_Revision = 5;
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

static LONG parse_num(const char **s)
{
	LONG n = 0;

	while (**s >= '0' && **s <= '9')
		n = n * 10 + (*(*s)++ - '0');
	return n;
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
	} else if ((v = match_key(line, "keepdisplay")) != NULL) {
		status->keepdisplay = *v == '1';
	} else if ((v = match_key(line, "mirror")) != NULL) {
		copy_name(mirror_card, v);
	} else if ((v = match_key(line, "mirrorfps")) != NULL) {
		LONG n = parse_num(&v);

		if (n >= 1 && n <= 50)
			mirror.fps = n;
	} else if ((v = match_key(line, "span")) != NULL) {
		/* SPAN=2x2, columns x rows */
		LONG c = parse_num(&v), r;

		if (*v == 'x' || *v == 'X') {
			v++;
			r = parse_num(&v);
			if (c >= 1 && r >= 1 && c * r <= WRAP_MAXHEADS) {
				span_cols = c;
				span_rows = r;
			}
		}
	} else if ((v = match_key(line, "headshare")) != NULL) {
		mirror.share = *v == '1';
	} else if ((v = match_key(line, "head")) != NULL) {
		copy_name(head_card, v);
	} else {
		static const char *keys[WRAP_MAXHEADS] = { "head1", "head2", "head3", "head4" };
		int i;

		for (i = 0; i < WRAP_MAXHEADS; i++)
			if ((v = match_key(line, keys[i])) != NULL)
				copy_name(head_cards[i], v);
	}
}

/*
 * The heads from the settings: SPAN= with HEAD= (or HEAD1= ...) for each,
 * else MIRROR= as one head.
 */
static void heads_config(void)
{
	UWORD i, n;

	if (span_cols * span_rows > 1) {
		mirror.cols = span_cols;
		mirror.rows = span_rows;
		n = span_cols * span_rows;
		for (i = 0; i < n; i++)
			copy_name(mirror.head[i].card, head_cards[i][0] ? head_cards[i] : head_card);
	} else if (mirror_card[0]) {
		mirror.cols = mirror.rows = 1;
		n = 1;
		copy_name(mirror.head[0].card, mirror_card);
	} else {
		n = 0;
	}
	mirror.nheads = n;
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
		heads_config();
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

/* MC_BLIT_* (mc_status.h) of a drawing slot, or -1 */
static LONG blit_kind(ULONG slot)
{
	switch (slot) {
	case SLOT_FillRect:               return 0;
	case SLOT_InvertRect:             return 1;
	case SLOT_BlitRect:               return 2;
	case SLOT_BlitTemplate:           return 3;
	case SLOT_BlitPattern:            return 4;
	case SLOT_DrawLine:               return 5;
	case SLOT_BlitRectNoMaskComplete: return 6;
	case SLOT_BlitPlanar2Chunky:      return 7;
	case SLOT_BlitPlanar2Direct:      return 8;
	}
	return -1;
}

/*
 * A drawing call or a pointer change of the wrapped board, to each head
 * that shows the board's bitmap itself and can send it as an order
 * (WrapHead.orders): McRecord and McPointer of Panorama.card 2.
 */
static void pass_on(ULONG slot, ULONG *regs)
{
	LONG kind = blit_kind(slot), what = -1;
	UWORD i;

	switch (slot) {
	case SLOT_SetSprite:         what = 0; break;
	case SLOT_SetSpritePosition: what = 1; break;
	case SLOT_SetSpriteImage:    what = 2; break;
	case SLOT_SetSpriteColor:    what = 3; break;
	}
	if (kind < 0 && what < 0)
		return;
	for (i = 0; i < mirror.nheads; i++) {
		struct WrapHead *h = &mirror.head[i];

		if (!h->orders || !h->bi)
			continue;
		if (kind >= 0)
			((McRecordFn)WRAP_LVO(h->lib, WRAP_LVO_MCRECORD))(h->bi, kind, regs, h->lib);
		else
			((McRecordFn)WRAP_LVO(h->lib, WRAP_LVO_MCPOINTER))(h->bi, what, regs, h->lib);
	}
}

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

	/*
	 * KEEPDISPLAY=1: the wrapped card is a monitor of its own (the
	 * Workbench's, beside a Panorama desktop): P96 switching it off when
	 * another board's screen comes to the front would blank it, so it is
	 * told "on" instead, and keeps showing its screen.
	 */
	if (status->keepdisplay && (slot == SLOT_SetSwitch || slot == SLOT_SetDisplay) &&
	    !(regs[0] & 0xffff)) {
		regs[0] |= 1;
		status->kept_on++;
	}

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

	/* the first board's drawing, to heads that send it on (Panorama.card 2) */
	if (b == &boards[0] && mirror.nheads)
		pass_on(slot, regs);

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
	/*
	 * With heads, one board: rtg.library asks until FindCard says no, and
	 * further boards of the card may be meant for the heads (CARD= and
	 * MIRROR= or HEAD= both Panorama.card).
	 */
	if (status->nboards && mirror.nheads)
		return FALSE;

	bi->CardBase = inner;
	if (!InnerFindCard(bi, inner)) {
		bi->CardBase = oldbase;
		if (!status->nboards)
			close_inner();
		return FALSE;
	}
	bi->CardBase = oldbase;
	status->inner_found++;
	b = &boards[status->nboards++];
	b->bi = bi;
	return TRUE;
}

BOOL InitCard(__reg("a0") struct BoardInfo *bi, __reg("a1") char **tooltypes)
{
	struct Library *oldbase = bi->CardBase;
	struct WrapBoard *b = find_board(bi);
	BOOL ok;

	status->initcard_calls++;
	if (!b) {
		status->error = WRAP_ERR_UNKNOWN;
		return FALSE;
	}
	bi->CardBase = inner;
	ok = InnerInitCard(bi, tooltypes, inner);
	bi->CardBase = oldbase;
	if (!ok) {
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
	/*
	 * KEEPDISPLAY: the Workbench's board beside the Panorama desktop is a
	 * monitor of its own, whatever the card says (UAE's uaegfx can say it
	 * is in the display chain). P96ScreenCx moves the mouse only between
	 * boards out of the chain: in it, the board counts as "Native", and the
	 * desktop's RIGHT_OF=Wrap would lead nowhere.
	 */
	if (status->keepdisplay)
		bi->Flags &= ~BIF_INDISPLAYCHAIN;

	/* the card works without the thunks, so a broken table only stops the wrapping */
	if (WrapThunksEnd - WrapThunks != THUNK_SIZE * WRAP_NSLOTS) {
		status->error = WRAP_ERR_THUNKS;
		return TRUE;
	}
	install_thunks(b);
	b->initialised = 1;
	if (b == &boards[0] && mirror.nheads && !mirror.task && !mirror.head[0].lib)
		wrap_mirror_start(b, inner, &mirror);
	return TRUE;
}
