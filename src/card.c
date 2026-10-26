/*
 * P96Multicast - Multicast.card: a P96 card driver without hardware.
 *
 * FindCard() allocates the "video RAM" from Fast RAM, InitCard() describes
 * the board to rtg.library and starts the sender process, and the BoardInfo
 * functions below record the display state (mode, format, panning, palette)
 * for the sender. Interface: https://wiki.icomp.de/wiki/P96_Driver_Development
 */
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include "mcard.h"
#include "mc_status.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;

extern char LibName[];
extern char LibId[];
extern void SetMemoryMode(void);

/* boards set up so far; BOARDS= says how many to offer */
static UWORD cards_found;

/* RGB formats the card offers; all of them coexist in memory */
#define MC_FORMATS (RGBFF_CLUT | RGBFF_R5G6B5PC | RGBFF_R5G5B5PC | \
                    RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_B8G8R8 | RGBFF_R8G8B8 | \
                    RGBFF_A8R8G8B8 | RGBFF_B8G8R8A8 | RGBFF_R8G8B8A8 | RGBFF_A8B8G8R8)

/* virtual pixel clocks: 10 MHz to 200 MHz in 1 MHz steps */
#define MC_CLOCK_MIN   10000000UL
#define MC_CLOCK_STEP  1000000UL
#define MC_NCLOCKS     191

/* ---------------------------------------------------------------- library */

/* the library base is struct McBase (mc_status.h); its size is in libstart.s */
typedef char mc_libsize_check[sizeof(struct McBase) <= MC_LIBSIZE ? 1 : -1];

/* the status block in the library base, filled in for "mctest STATUS" */
struct McStatus *mc_status;

struct Library *LibInit(__reg("d0") struct McBase *base, __reg("a0") BPTR seglist,
                        __reg("a6") struct ExecBase *sysbase)
{
	SysBase = sysbase;
	base->seglist = seglist;
	mc_status = &base->status;
	mc_status->magic = MC_STATUS_MAGIC;
	base->lib.lib_Node.ln_Type = NT_LIBRARY;
	base->lib.lib_Node.ln_Name = LibName;
	base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
	base->lib.lib_Version = 1;
	base->lib.lib_Revision = 2;
	base->lib.lib_IdString = LibId;

	DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 37);
	GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
	if (!DOSBase || !GfxBase) {
		if (DOSBase)
			CloseLibrary((struct Library *)DOSBase);
		if (GfxBase)
			CloseLibrary((struct Library *)GfxBase);
		FreeMem((UBYTE *)base - base->lib.lib_NegSize,
		        base->lib.lib_NegSize + base->lib.lib_PosSize);
		return NULL;
	}
	return &base->lib;
}

struct Library *LibOpen(__reg("a6") struct McBase *base)
{
	base->lib.lib_OpenCnt++;
	base->lib.lib_Flags &= ~LIBF_DELEXP;
	return &base->lib;
}

BPTR LibClose(__reg("a6") struct McBase *base)
{
	base->lib.lib_OpenCnt--;
	return 0;
}

/*
 * The sender process runs code from this library and a card cannot be
 * removed from rtg.library, so once loaded the library stays.
 */
BPTR LibExpunge(__reg("a6") struct McBase *base)
{
	base->lib.lib_Flags |= LIBF_DELEXP;
	return 0;
}

/* ---------------------------------------------------------------- helpers */

ULONG mc_bytesperpixel(RGBFTYPE fmt)
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

/* rows are padded to a multiple of 4 bytes */
ULONG mc_bytesperrow(UWORD width, RGBFTYPE fmt)
{
	return ((ULONG)width * mc_bytesperpixel(fmt) + 3) & ~3UL;
}

static BOOL is_process(void)
{
	return FindTask(NULL)->tc_Node.ln_Type == NT_PROCESS;
}

/* ---------------------------------------------------------------- BoardInfo functions */

static BOOL SetSwitch(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD state)
{
	struct McCard *mc = MC_CARD(bi);
	UWORD old = mc->moniswitch;

	mc->moniswitch = state ? 1 : 0;
	bi->MoniSwitch = mc->moniswitch;
	mc->serial++;
	return old;
}

static void SetColorArray(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD start,
                          __reg("d1") UWORD count)
{
	struct McCard *mc = MC_CARD(bi);
	UBYTE *d;
	struct CLUTEntry *s;

	if (start >= 256)
		return;
	if (count > 256 - start)
		count = 256 - start;
	d = mc->pal + start * 3;
	s = &bi->CLUT[start];
	while (count--) {
		d[0] = s->Red;
		d[1] = s->Green;
		d[2] = s->Blue;
		d += 3;
		s++;
	}
	mc->pal_serial++;
}

static void SetDAC(__reg("a0") struct BoardInfo *bi, __reg("d7") RGBFTYPE fmt)
{
	struct McCard *mc = MC_CARD(bi);

	mc->format = fmt;
	mc->serial++;
}

static void SetGC(__reg("a0") struct BoardInfo *bi, __reg("a1") struct ModeInfo *mi,
                  __reg("d0") UWORD border)
{
	struct McCard *mc = MC_CARD(bi);

	mc_status->setgc_calls++;
	bi->Border = border;
	mc->width = mi->Width;
	mc->height = mi->Height;
	mc->serial++;
}

static void SetPanning(__reg("a0") struct BoardInfo *bi, __reg("a1") UBYTE *mem,
                       __reg("d0") UWORD width, __reg("d1") WORD x, __reg("d2") WORD y,
                       __reg("d7") RGBFTYPE fmt)
{
	struct McCard *mc = MC_CARD(bi);

	mc_status->panning_calls++;
	bi->XOffset = x;
	bi->YOffset = y;
	mc->bm_mem = mem;
	mc->bm_width = width;
	mc->xoff = x;
	mc->yoff = y;
	mc->format = fmt;
	mc->serial++;
}

static ULONG CalculateBytesPerRow(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD width,
                                  __reg("d7") RGBFTYPE fmt)
{
	return mc_bytesperrow(width, fmt);
}

static APTR CalculateMemory(__reg("a0") struct BoardInfo *bi, __reg("a1") APTR mem,
                            __reg("d7") RGBFTYPE fmt)
{
	return mem;
}

static ULONG GetCompatibleFormats(__reg("a0") struct BoardInfo *bi, __reg("d7") RGBFTYPE fmt)
{
	return MC_FORMATS;
}

static BOOL SetDisplay(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD state)
{
	struct McCard *mc = MC_CARD(bi);
	UWORD old = mc->display;

	mc->display = state ? 1 : 0;
	mc->serial++;
	return old;
}

static ULONG ResolvePixelClock(__reg("a0") struct BoardInfo *bi, __reg("a1") struct ModeInfo *mi,
                               __reg("d0") ULONG clock, __reg("d7") RGBFTYPE fmt)
{
	ULONG index;

	if (clock <= MC_CLOCK_MIN)
		index = 0;
	else
		index = (clock - MC_CLOCK_MIN + MC_CLOCK_STEP / 2) / MC_CLOCK_STEP;
	if (index >= MC_NCLOCKS)
		index = MC_NCLOCKS - 1;
	mi->PixelClock = MC_CLOCK_MIN + index * MC_CLOCK_STEP;
	/* P96 may divide by these, as for a real clock synthesizer: never 0 */
	mi->Numerator = index;
	mi->Denominator = 1;
	/* there is no CRTC: double clocking and interlace mean nothing here */
	mi->Flags &= ~(GMF_DOUBLECLOCK | GMF_INTERLACE);
	return index;
}

static ULONG GetPixelClock(__reg("a0") struct BoardInfo *bi, __reg("a1") struct ModeInfo *mi,
                           __reg("d0") ULONG index, __reg("d7") RGBFTYPE fmt)
{
	if (index >= MC_NCLOCKS)
		index = MC_NCLOCKS - 1;
	return MC_CLOCK_MIN + index * MC_CLOCK_STEP;
}

static void SetClock(__reg("a0") struct BoardInfo *bi)
{
	struct McCard *mc = MC_CARD(bi);

	if (bi->ModeInfo)
		mc->clockindex = bi->ModeInfo->Numerator;
}

/*
 * No retrace to wait for. The native display's vertical blank keeps
 * callers paced the way they expect, and works with the display off.
 */
static void WaitVerticalSync(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD end)
{
	if (!end)
		WaitTOF();
}

static BOOL GetVSyncState(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD expected)
{
	return expected;
}

static void SetInterrupt(__reg("a0") struct BoardInfo *bi, __reg("d0") UWORD state)
{
}

static void WaitBlitter(__reg("a0") struct BoardInfo *bi)
{
}

static void SetDPMSLevel(__reg("a0") struct BoardInfo *bi, __reg("d0") ULONG level)
{
}

/* ---------------------------------------------------------------- FindCard / InitCard */

/*
 * Video memory: the configured size if it fits in Fast RAM with at least
 * 512 KB to spare, otherwise half of it, and so on down to 1 MB.
 */
static BOOL alloc_vram(struct McCard *mc)
{
	ULONG size = mc->cfg.vramsize;
	ULONG largest = AvailMem(MEMF_FAST | MEMF_LARGEST);

	mc_status->fast_largest = largest;
	for (; size >= 1024UL * 1024; size >>= 1) {
		if (largest < size + 4096 + 512UL * 1024)
			continue;
		mc->vram_allocsize = size + 4096;
		mc->vram_alloc = AllocMem(mc->vram_allocsize, MEMF_FAST | MEMF_PUBLIC);
		if (mc->vram_alloc) {
			mc->cfg.vramsize = size;
			mc_status->vramsize = size;
			return TRUE;
		}
	}
	return FALSE;
}

BOOL FindCard(__reg("a0") struct BoardInfo *bi)
{
	struct McCard *mc;
	UBYTE *vram;

	mc_status->findcard_calls++;
	/* rtg.library asks again until FindCard fails */
	if (cards_found >= MC_MAXBOARDS)
		return FALSE;

	mc = AllocMem(sizeof(*mc), MEMF_PUBLIC | MEMF_CLEAR);
	if (!mc) {
		mc_status->error = MC_ERR_NOCONTEXT;
		return FALSE;
	}
	mc->bi = bi;
	mc->boardnum = cards_found;
	mc_config_defaults(&mc->cfg);
	if (is_process())
		mc_config_env(&mc->cfg);
	if (cards_found >= mc->cfg.boards) {
		FreeMem(mc, sizeof(*mc));
		return FALSE;
	}

	if (!alloc_vram(mc)) {
		mc_status->error = MC_ERR_NOVRAM;
		FreeMem(mc, sizeof(*mc));
		return FALSE;
	}
	/* page aligned, so the MMU libraries can map it cleanly */
	vram = (UBYTE *)(((ULONG)mc->vram_alloc + 4095) & ~4095UL);

	bi->CardData[0] = (ULONG)mc;
	bi->RegisterBase = NULL;
	bi->MemoryIOBase = NULL;
	bi->MemoryBase = vram;
	bi->MemorySize = mc->cfg.vramsize;
	bi->MemorySpaceBase = vram;
	bi->MemorySpaceSize = mc->cfg.vramsize;
	cards_found++;
	mc_status->cards = cards_found;
	return TRUE;
}

BOOL InitCard(__reg("a0") struct BoardInfo *bi, __reg("a1") char **tooltypes)
{
	struct McCard *mc = MC_CARD(bi);
	int i;

	mc_status->initcard_calls++;
	/* AROS's p96gfx.hidd passes no ToolTypes */
	if (tooltypes) {
		for (; *tooltypes; tooltypes++)
			mc_config_line(&mc->cfg, *tooltypes);
		if (is_process())
			mc_config_env(&mc->cfg);
	}

	if (mc->boardnum) {
		/* "Multicast2" ..., up to MC_MAXBOARDS */
		CopyMem("Multicast", mc->boardname, 9);
		mc->boardname[9] = '1' + mc->boardnum;
		mc->boardname[10] = 0;
	} else {
		CopyMem("Multicast", mc->boardname, 10);
	}
	bi->BoardName = mc->boardname;
	bi->BoardType = mc->cfg.boardtype;
	bi->PaletteChipType = PCT_Unknown;
	bi->GraphicsControllerType = GCT_Unknown;
	bi->BitsPerCannon = 8;
	/* P96 may weigh pixel clocks against memory bandwidth: plenty of it */
	bi->MemoryClock = 200000000UL;
	/*
	 * Bits 0-15 describe the hardware: start from none, like UAE's uaegfx
	 * does, so nothing preset by rtg.library claims a sprite or blitter.
	 * Bits 16-31 are user settings and stay. No video switch either: the
	 * native display keeps its own monitor.
	 */
	bi->Flags = (bi->Flags & 0xffff0000UL & ~BIF_INDISPLAYCHAIN) | BIF_GRANTDIRECTACCESS;
	if (mc->cfg.quiet)
		bi->Flags |= BIF_QUIET;
	bi->RGBFormats = MC_FORMATS;
	bi->SoftSpriteFlags = MC_FORMATS;   /* no hardware sprite in any format */

	for (i = 0; i < MAXMODES; i++) {
		BOOL ok = i != PLANAR;

		bi->MaxHorValue[i] = ok ? 4095 : 0;
		bi->MaxVerValue[i] = ok ? 4095 : 0;
		bi->MaxHorResolution[i] = ok ? mc->cfg.maxwidth : 0;
		bi->MaxVerResolution[i] = ok ? mc->cfg.maxheight : 0;
		bi->PixelClockCount[i] = ok ? MC_NCLOCKS : 0;
	}

	bi->SetSwitch = (APTR)SetSwitch;
	bi->SetColorArray = (APTR)SetColorArray;
	bi->SetDAC = (APTR)SetDAC;
	bi->SetGC = (APTR)SetGC;
	bi->SetPanning = (APTR)SetPanning;
	bi->CalculateBytesPerRow = (APTR)CalculateBytesPerRow;
	bi->CalculateMemory = (APTR)CalculateMemory;
	bi->GetCompatibleFormats = (APTR)GetCompatibleFormats;
	bi->SetDisplay = (APTR)SetDisplay;
	bi->ResolvePixelClock = (APTR)ResolvePixelClock;
	bi->GetPixelClock = (APTR)GetPixelClock;
	bi->SetClock = (APTR)SetClock;
	bi->SetMemoryMode = (APTR)SetMemoryMode;
	/* planar is not offered; these must preserve all registers anyway */
	bi->SetWriteMask = (APTR)SetMemoryMode;
	bi->SetClearMask = (APTR)SetMemoryMode;
	bi->SetReadPlane = (APTR)SetMemoryMode;
	bi->WaitVerticalSync = (APTR)WaitVerticalSync;
	bi->GetVSyncState = (APTR)GetVSyncState;
	bi->SetInterrupt = (APTR)SetInterrupt;
	bi->WaitBlitter = (APTR)WaitBlitter;
	bi->SetDPMSLevel = (APTR)SetDPMSLevel;

	mc->moniswitch = 1;
	mc->display = 1;
	mc->format = RGBFB_CLUT;
	if (mc->cfg.blitter)
		mc_blitter_install(mc);

	if (!mc_sender_start(mc)) {
		mc_status->error = MC_ERR_NOSENDER;
		return FALSE;
	}
	return TRUE;
}
