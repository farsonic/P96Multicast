/*
 * Panorama - checks struct BoardInfo against the published P96 offsets.
 * Built with vbcc and run under vamos by "make check".
 */
#include <stdio.h>
#include <stddef.h>
#include "boardinfo.h"

static int fails;

#define CHECK(field, want) check(#field, offsetof(struct BoardInfo, field), want)

static void check(const char *name, unsigned long got, unsigned long want)
{
	if (got != want) {
		printf("FAIL %s: offset %lu, want %lu\n", name, got, want);
		fails++;
	}
}

int main(void)
{
	CHECK(RegisterBase, 0);
	CHECK(MemoryBase, 4);
	CHECK(MemorySize, 12);
	CHECK(BoardName, 16);
	CHECK(CardBase, 52);
	CHECK(ExecBase, 60);
	CHECK(UtilBase, 64);
	CHECK(BoardType, 170);
	CHECK(PaletteChipType, 174);
	CHECK(GraphicsControllerType, 178);
	CHECK(MoniSwitch, 182);
	CHECK(BitsPerCannon, 184);
	CHECK(Flags, 186);
	CHECK(SoftSpriteFlags, 190);
	CHECK(RGBFormats, 200);
	CHECK(MaxHorValue, 202);
	CHECK(MaxVerResolution, 232);
	CHECK(PixelClockCount, 254);
	CHECK(AllocCardMem, 274);
	CHECK(SetSwitch, 282);
	CHECK(SetColorArray, 286);
	CHECK(SetDAC, 290);
	CHECK(SetGC, 294);
	CHECK(SetPanning, 298);
	CHECK(CalculateBytesPerRow, 302);
	CHECK(CalculateMemory, 306);
	CHECK(GetCompatibleFormats, 310);
	CHECK(SetDisplay, 314);
	CHECK(ResolvePixelClock, 318);
	CHECK(GetPixelClock, 322);
	CHECK(SetClock, 326);
	CHECK(SetMemoryMode, 330);
	CHECK(WaitVerticalSync, 346);
	CHECK(SetInterrupt, 350);
	CHECK(WaitBlitter, 354);
	CHECK(BlitPlanar2DirectDefault, 442);
	CHECK(SetDPMSLevel, 494);
	CHECK(ResetChip, 498);
	CHECK(SetSprite, 518);
	CHECK(DeleteFeature, 542);
	CHECK(ModeInfo, 558);
	CHECK(RGBFormat, 562);
	CHECK(XOffset, 566);
	CHECK(YOffset, 568);
	CHECK(Border, 572);
	CHECK(CLUT, 578);
	CHECK(MouseX, 1382);
	CHECK(ChipData, 1418);
	CHECK(CardData, 1482);
	CHECK(MemorySpaceBase, 1546);
	CHECK(MemorySpaceSize, 1550);
	check("ModeInfo.Width", offsetof(struct ModeInfo, Width), 18);
	check("ModeInfo.Height", offsetof(struct ModeInfo, Height), 20);
	check("ModeInfo.Flags", offsetof(struct ModeInfo, Flags), 23);
	check("ModeInfo.Numerator", offsetof(struct ModeInfo, Numerator), 42);
	check("ModeInfo.PixelClock", offsetof(struct ModeInfo, PixelClock), 44);
	/* LibResolution: the offsets in UAE's picasso96_win.h */
	check("LibResolution.P96ID", offsetof(struct LibResolution, P96ID), 14);
	check("LibResolution.Name", offsetof(struct LibResolution, Name), 20);
	check("LibResolution.DisplayID", offsetof(struct LibResolution, DisplayID), 42);
	check("LibResolution.Width", offsetof(struct LibResolution, Width), 46);
	check("LibResolution.Flags", offsetof(struct LibResolution, Flags), 50);
	check("LibResolution.Modes", offsetof(struct LibResolution, Modes), 52);
	check("LibResolution.BoardInfo", offsetof(struct LibResolution, BoardInfo), 72);
	check("sizeof LibResolution", sizeof(struct LibResolution), 80);
	check("sizeof ModeInfo", sizeof(struct ModeInfo), 48);
	printf(fails ? "offsets: %d failures\n" : "offsets: ok\n", fails);
	return fails ? 20 : 0;
}
