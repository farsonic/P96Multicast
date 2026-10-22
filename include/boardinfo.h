/*
 * P96Multicast - the P96 BoardInfo structure, as far as a card driver uses it.
 *
 * Written from the interface description at
 * https://wiki.icomp.de/wiki/P96_Driver_Development and the published field
 * offsets (AROS p96gfx.hidd, WinUAE picasso96.h). Only the members up to
 * MemorySpaceSize are declared: everything after them is "dynamic data" of
 * rtg.library, and its layout differs between P96 and AROS's p96gfx.hidd,
 * so a card driver must not write there.
 *
 * Offsets are checked by tests/offsets.c.
 */
#ifndef P96MC_BOARDINFO_H
#define P96MC_BOARDINFO_H

#include <exec/types.h>
#include <exec/interrupts.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <graphics/gfx.h>

/* ---- RGB formats (bit numbers, RGBFB_*, and masks, RGBFF_*) ---- */
typedef ULONG RGBFTYPE;

#define RGBFB_NONE      0   /* planar */
#define RGBFB_CLUT      1   /* 8 bit chunky, palette */
#define RGBFB_R8G8B8    2
#define RGBFB_B8G8R8    3
#define RGBFB_R5G6B5PC  4   /* gggbbbbb rrrrrggg */
#define RGBFB_R5G5B5PC  5   /* gggbbbbb 0rrrrrgg */
#define RGBFB_A8R8G8B8  6
#define RGBFB_A8B8G8R8  7
#define RGBFB_R8G8B8A8  8
#define RGBFB_B8G8R8A8  9
#define RGBFB_R5G6B5    10  /* rrrrrggg gggbbbbb */
#define RGBFB_R5G5B5    11  /* 0rrrrrgg gggbbbbb */
#define RGBFB_B5G6R5PC  12
#define RGBFB_B5G5R5PC  13
#define RGBFB_MaxFormats 16

#define RGBFF(fmt) (1UL << (fmt))
#define RGBFF_CLUT      RGBFF(RGBFB_CLUT)
#define RGBFF_R8G8B8    RGBFF(RGBFB_R8G8B8)
#define RGBFF_B8G8R8    RGBFF(RGBFB_B8G8R8)
#define RGBFF_R5G6B5PC  RGBFF(RGBFB_R5G6B5PC)
#define RGBFF_R5G5B5PC  RGBFF(RGBFB_R5G5B5PC)
#define RGBFF_A8R8G8B8  RGBFF(RGBFB_A8R8G8B8)
#define RGBFF_A8B8G8R8  RGBFF(RGBFB_A8B8G8R8)
#define RGBFF_R8G8B8A8  RGBFF(RGBFB_R8G8B8A8)
#define RGBFF_B8G8R8A8  RGBFF(RGBFB_B8G8R8A8)
#define RGBFF_R5G6B5    RGBFF(RGBFB_R5G6B5)
#define RGBFF_R5G5B5    RGBFF(RGBFB_R5G5B5)
#define RGBFF_B5G6R5PC  RGBFF(RGBFB_B5G6R5PC)
#define RGBFF_B5G5R5PC  RGBFF(RGBFB_B5G5R5PC)

/* mode types, the index of the per-mode arrays (not an RGB format) */
#define PLANAR    0
#define CHUNKY    1
#define HICOLOR   2
#define TRUECOLOR 3
#define TRUEALPHA 4
#define MAXMODES  5

/* ---- BoardInfo->Flags ---- */
#define BIB_HARDWARESPRITE     0
#define BIB_NOMEMORYMODEMIX    1
#define BIB_NEEDSALIGNMENT     2
#define BIB_CACHEMODECHANGE    3
#define BIB_VBLANKINTERRUPT    4
#define BIB_HASSPRITEBUFFER    5
#define BIB_VGASCREENSPLIT     6
#define BIB_DBLSCANDBLSPRITEY  8
#define BIB_ILACEHALFSPRITEY   9
#define BIB_ILACEDBLROWOFFSET  10
#define BIB_INTERNALMODESONLY  11
#define BIB_FLICKERFIXER       12
#define BIB_VIDEOCAPTURE       13
#define BIB_VIDEOWINDOW        14
#define BIB_BLITTER            15
#define BIB_HIRESSPRITE        16
#define BIB_BIGSPRITE          17
#define BIB_BORDEROVERRIDE     18
#define BIB_BORDERBLANK        19
#define BIB_INDISPLAYCHAIN     20
#define BIB_QUIET              21
#define BIB_NOMASKBLITS        22
#define BIB_NOP2CBLITS         23
#define BIB_NOBLITTER          24
#define BIB_SYSTEM2SCREENBLITS 25
#define BIB_GRANTDIRECTACCESS  26
#define BIB_PALETTESWITCH      27
#define BIB_DACSWITCH          28
#define BIB_OVERCLOCK          31

#define BIF_INDISPLAYCHAIN    (1UL << BIB_INDISPLAYCHAIN)
#define BIF_QUIET             (1UL << BIB_QUIET)
#define BIF_GRANTDIRECTACCESS (1UL << BIB_GRANTDIRECTACCESS)

/* ---- board, palette chip and graphics controller types ---- */
#define BT_NoBoard     0
#define BT_PicassoII   6
#define BT_uaegfx      14
#define PCT_Unknown    0
#define GCT_Unknown    0

/* ---- ModeInfo ---- */
struct ModeInfo {
	struct Node Node;
	WORD  OpenCount;
	BOOL  Active;
	UWORD Width;
	UWORD Height;
	UBYTE Depth;
	UBYTE Flags;
	UWORD HorTotal;
	UWORD HorBlankSize;
	UWORD HorSyncStart;
	UWORD HorSyncSize;
	UBYTE HorSyncSkew;
	UBYTE HorEnableSkew;
	UWORD VerTotal;
	UWORD VerBlankSize;
	UWORD VerSyncStart;
	UWORD VerSyncSize;
	UBYTE Numerator;    /* also: Clock */
	UBYTE Denominator;  /* also: ClockDivide */
	ULONG PixelClock;
};

#define GMF_DOUBLECLOCK  (1 << 0)
#define GMF_INTERLACE    (1 << 1)
#define GMF_DOUBLESCAN   (1 << 2)

struct CLUTEntry {
	UBYTE Red;
	UBYTE Green;
	UBYTE Blue;
};

struct RenderInfo {
	APTR     Memory;
	WORD     BytesPerRow;
	WORD     pad;
	RGBFTYPE RGBFormat;
};

/* ---- BoardInfo ---- */
struct BoardInfo {
	UBYTE *RegisterBase;
	UBYTE *MemoryBase;
	UBYTE *MemoryIOBase;
	ULONG  MemorySize;
	char  *BoardName;
	char   VBIName[32];
	struct Library  *CardBase;
	struct Library  *ChipBase;
	struct ExecBase *ExecBase;
	struct Library  *UtilBase;
	struct Interrupt HardInterrupt;
	struct Interrupt SoftInterrupt;
	struct SignalSemaphore BoardLock;
	struct MinList ResolutionsList;
	ULONG  BoardType;
	ULONG  PaletteChipType;
	ULONG  GraphicsControllerType;
	UWORD  MoniSwitch;
	UWORD  BitsPerCannon;
	ULONG  Flags;
	UWORD  SoftSpriteFlags;
	UWORD  ChipFlags;
	ULONG  CardFlags;
	UWORD  BoardNum;
	UWORD  RGBFormats;
	UWORD  MaxHorValue[MAXMODES];
	UWORD  MaxVerValue[MAXMODES];
	UWORD  MaxHorResolution[MAXMODES];
	UWORD  MaxVerResolution[MAXMODES];
	ULONG  MaxMemorySize;
	ULONG  MaxChunkSize;
	ULONG  MemoryClock;
	ULONG  PixelClockCount[MAXMODES];

	/* functions, called with register arguments (see the wiki) */
	APTR AllocCardMem;
	APTR FreeCardMem;
	APTR SetSwitch;
	APTR SetColorArray;
	APTR SetDAC;
	APTR SetGC;
	APTR SetPanning;
	APTR CalculateBytesPerRow;
	APTR CalculateMemory;
	APTR GetCompatibleFormats;
	APTR SetDisplay;
	APTR ResolvePixelClock;
	APTR GetPixelClock;
	APTR SetClock;
	APTR SetMemoryMode;
	APTR SetWriteMask;
	APTR SetClearMask;
	APTR SetReadPlane;
	APTR WaitVerticalSync;
	APTR SetInterrupt;
	APTR WaitBlitter;
	APTR ScrollPlanar;
	APTR ScrollPlanarDefault;
	APTR UpdatePlanar;
	APTR UpdatePlanarDefault;
	APTR BlitPlanar2Chunky;
	APTR BlitPlanar2ChunkyDefault;
	APTR FillRect;
	APTR FillRectDefault;
	APTR InvertRect;
	APTR InvertRectDefault;
	APTR BlitRect;
	APTR BlitRectDefault;
	APTR BlitTemplate;
	APTR BlitTemplateDefault;
	APTR BlitPattern;
	APTR BlitPatternDefault;
	APTR DrawLine;
	APTR DrawLineDefault;
	APTR BlitRectNoMaskComplete;
	APTR BlitRectNoMaskCompleteDefault;
	APTR BlitPlanar2Direct;
	APTR BlitPlanar2DirectDefault;
	APTR EnableSoftSprite;
	APTR Reserved0Default;
	APTR Reserved1;
	APTR SetSplitPosition;
	APTR ReInitMemory;
	APTR GetCompatibleDACFormats;
	APTR CoerceMode;
	APTR Reserved3Default;
	APTR BlitRectTransparent;
	APTR BlitRectTransparentDefault;
	APTR GetVSyncState;
	APTR GetVBeamPos;
	APTR SetDPMSLevel;
	APTR ResetChip;
	APTR GetFeatureAttrs;
	APTR AllocBitMap;
	APTR FreeBitMap;
	APTR GetBitMapAttr;
	APTR SetSprite;
	APTR SetSpritePosition;
	APTR SetSpriteImage;
	APTR SetSpriteColor;
	APTR CreateFeature;
	APTR SetFeatureAttrs;
	APTR DeleteFeature;

	struct MinList SpecialFeatures;
	struct ModeInfo *ModeInfo;
	RGBFTYPE RGBFormat;
	WORD   XOffset;
	WORD   YOffset;
	UBYTE  Depth;
	UBYTE  ClearMask;
	BOOL   Border;
	ULONG  Mask;
	struct CLUTEntry CLUT[256];
	struct ViewPort *ViewPort;
	struct BitMap   *VisibleBitMap;
	APTR   BitMapExtra;
	struct MinList BitMapList;
	struct MinList MemList;
	WORD   MouseX;
	WORD   MouseY;
	UBYTE  MouseWidth;
	UBYTE  MouseHeight;
	UBYTE  MouseXOffset;
	UBYTE  MouseYOffset;
	UWORD *MouseImage;
	UBYTE *MousePens;
	struct Rectangle MouseRect;
	UBYTE *MouseChunky;
	UWORD *MouseRendered;
	UBYTE *MouseSaveBuffer;
	ULONG  ChipData[16];
	ULONG  CardData[16];
	APTR   MemorySpaceBase;
	ULONG  MemorySpaceSize;
	/* rtg.library private data follows; never touch it */
};

#endif
