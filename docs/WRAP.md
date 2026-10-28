# Wrap.card

`Wrap.card` is a P96 card driver that loads another card driver and sits
between it and rtg.library. rtg.library still does the graphics, and the
real driver still drives the hardware. Wrap.card sees every call between
the two. It passes them through, counts them and records what is on
display, and it can show the screen on other boards, called heads: all of
it on a second board (`MIRROR=`), or one big screen across up to four
monitors, a tile on each (`SPAN=`, below), so windows move from monitor to
monitor by being dragged. Next on the list: a viewer-drawn pointer and
driver call logging.

## How it works

rtg.library (or AROS's `p96gfx.hidd`) loads `Wrap.card` as the board's
driver and calls its `FindCard` and `InitCard`. Wrap.card:

1. **FindCard:** reads `CARD=` from `ENV:Wrap.prefs` (or `ENVARC:` early in
   the boot) and opens that card driver. A name without a path is looked
   for in `LIBS:Picasso96/`, and a card already in memory is opened by name
   rather than loaded again. It then calls the card's `FindCard` with the
   same BoardInfo, with `bi->CardBase` set to the real card, and sets
   `bi->CardBase` back to Wrap.card afterwards.
2. **InitCard:** calls the card's `InitCard`, again with `bi->CardBase` set
   to the real card only during the call. Then, for every BoardInfo
   function the card filled in, it stores the card's pointer and puts a
   thunk in the slot. Slots still holding rtg.library's software fallback
   (`FillRect == FillRectDefault` and so on) are left alone, as are the
   `...Default` slots themselves.
3. **Each call:** the thunk (`src/wrapstart.s`) saves every register and
   calls `wrap_pre` (`src/wrap.c`), which counts the call and records the
   display state (SetGC, SetPanning, SetDAC, SetDisplay, SetSwitch). It
   then restores every register and calls the card's function with a6 set
   to the card's library base. AROS calls card functions with a6 set to the
   library it opened, which here is Wrap.card. The caller gets back the
   card's results and its own a6.

Wrap.card stays out of the BoardInfo's driver-owned data: it never touches
`CardData` or `ChipData`. `bi->CardBase` points at the real card only
while its `FindCard` and `InitCard` run: P96's rtg.library calls `InitCard`
through `bi->CardBase`, so leaving the real card there (as Wrap.card 0.2
did) makes P96 call the real card's `InitCard` directly, and Wrap.card
never sets up the board (wrapstat: "InitCard called 0 times"). Its own state is in a table
of up to 4 boards, keyed by BoardInfo.

The thunks keep the rules the P96 driver interface sets. All arguments are
in registers. `SetMemoryMode`, `SetWriteMask`, `SetClearMask` and
`SetReadPlane` preserve every register, and so do their thunks. `wrap_pre`
doesn't call the OS, because some functions are called from interrupts. A
call costs one register save and restore plus a lookup in a table of 4
boards. `DRAW=0` leaves the drawing functions (blits, fills, lines,
WaitBlitter) unwrapped, for speed.

## Installing

1. Copy `Wrap.card` to `LIBS:Picasso96/`, next to the real card.
2. Make the board's monitor load Wrap.card instead of the real card:
   * **P96:** set the monitor icon's `BOARDTYPE` ToolType to `Wrap`.
   * **AROS:** copy `DEVS:Monitors/P96` to `DEVS:Monitors/Wrap`.

   Remove or disable the real card's own monitor, so the card isn't also
   set up directly.
3. Name the real card in `ENVARC:Wrap.prefs` (and `ENV:`):

   ```
   CARD=uaegfx.card
   ```

4. Reboot. `wrapstat` shows what is wrapped and how often each function has
   been called.

| Key | Default | |
|---|---|---|
| `CARD` | (none) | the card driver to wrap: a name in `LIBS:Picasso96/`, or a path |
| `DRAW` | 1 | 0 leaves the drawing functions unwrapped |
| `MIRROR` | (none) | a card driver to mirror the first wrapped board onto (one head) |
| `SPAN` | (none) | `CxR`, e.g. `2x2` or `4x1`: one big screen on C x R heads (at most 4) |
| `HEAD` | (none) | with `SPAN`: the card driver of every head |
| `HEAD1`...`HEAD4` | `HEAD` | with `SPAN`: the card driver of one head (left to right, top to bottom) |
| `MIRRORFPS` | 25 | head updates per second (1-50) |
| `HEADSHARE` | 0 | 1: heads that can show any memory (Multicast.card) show the big screen's bitmap itself, no copy |
| `KEEPDISPLAY` | 0 | 1: never switch the wrapped card's display off. P96 switches a board off when another board's screen comes to the front; for the Workbench's card beside a Multicast desktop that blanks it (FS-UAE's uaegfx goes to the empty native display). With this, it keeps showing its screen. `wrapstat` counts the "off" calls it turned into "on". MulticastPrefs' `WBMONITOR=1` sets it up |

The settings are read when FindCard runs, so only from `ENV:`/`ENVARC:`.
The monitor's ToolTypes are passed on to the real card's InitCard untouched.

## Heads: mirror and span

Heads are boards of other card drivers that show the first wrapped board's
screen. Wrap.card (in its InitCard, for the first board it wraps) opens
each head's card driver and sets up one of its boards on a BoardInfo of
its own. rtg.library never hears of these boards: no screen opens on them,
nothing draws on them, and they offer no modes. A process, "Wrap.card
heads", then follows what the thunks record about the wrapped board,
MIRRORFPS times a second:

* a new mode: the same on each head, the way rtg.library sets one
  (ResolvePixelClock, SetClock, SetGC with a copy of the wrapped board's
  ModeInfo, SetDAC, CalculateBytesPerRow, SetPanning onto the start of the
  head's video memory);
* a palette change (SetColorArray): the wrapped board's CLUT on each head;
* display on or off (SetDisplay, SetSwitch): the same on the heads;
* and every pass, it copies what changed in each head's part of the screen
  into the head's video memory (below).

The pixels are copied as they are, so a head's card has to offer the
wrapped board's pixel format (wrapstat says so if not).

### MIRROR: the same screen twice

`MIRROR=card` is one head showing what the wrapped board shows (its
visible part, where it is scrolled to). It can be a second board of the
same driver. With Multicast.card, `BOARDS=2` in `Multicast.prefs` gives a
second board that streams to PORT+1:

```
# ENVARC:Wrap.prefs                # ENVARC:Multicast.prefs
CARD=Multicast.card                BOARDS=2
MIRROR=Multicast.card
```

Or, to stream a real board's screen to the network:

```
CARD=uaegfx.card
MIRROR=Multicast.card
```

### SPAN: one screen on up to four monitors

`SPAN=CxR` makes C x R heads (at most 4), left to right and top to bottom,
each showing its tile of one big screen. It is one Intuition screen, so
every program works unchanged: windows are dragged from monitor to monitor,
open anywhere on it, and the pointer goes everywhere. The big screen is
either:

* **a screen larger than its display mode** (works with any mode list, AROS
  included): for 2x2 heads of 640x480, open a 1280x960 screen in a 640x480
  mode. Each head shows one mode-sized tile of the screen's bitmap, wherever
  the wrapped board itself is scrolled to. Wrap.card uses this layout when
  the bitmap is at least C times the mode's width;
* **one big mode**, e.g. a 2560x1024 mode made in P96's mode editor for 2x1
  heads: the mode is split evenly between the heads, each getting its tile's
  size with the big mode's refresh rate.

The wrapped board is best a virtual one: Multicast.card's first board
holds the big screen in Fast RAM (it needs no monitor of its own, though it
streams too), and the heads are the real monitors:

```
# ENVARC:Wrap.prefs
CARD=Multicast.card
SPAN=2x1
HEAD1=PicassoIV.card
HEAD2=ZZ9000.card
```

Or all four on the network, as `emu/span_test.sh` runs it (BOARDS=5 in
`Multicast.prefs`: board 1 is the big screen, boards 2 to 5 the heads,
ports 9697 to 9700):

```
CARD=Multicast.card
SPAN=2x2
HEAD=Multicast.card
```

A card with a hardware sprite draws the pointer only on its own display,
so the wrapped board should be one without (like Multicast.card): rtg.library
then draws the pointer into the screen, and it shows on whichever head it
is over.

### What it costs

Nothing tells a card which pixels changed: rtg.library and programs write
into the bitmap directly. So every pass compares each head's tile with a
shadow of it in Fast RAM, 64 bytes at a time, and writes only the chunks
that differ, to the head and the shadow. The head's video memory (slow to
read over Zorro) is only written, and only where something changed: in the
span test each head took about 1 MB over a minute instead of 600 KB per
pass. Without room for a shadow, a head compares with its own video memory.

The comparing is not free either: it reads as much as a copy does. With
`HEADSHARE=1`, heads that can show any memory (Multicast.card's boards)
skip it: each one is panned onto its part of the big screen's bitmap
(SetPanning with the bitmap's width and the tile's offset), and nothing is
compared or copied. Real cards can only show their own video memory, so
for them it stays off.

Multicast.card 2 heads don't compare their whole screen either: with
HEADSHARE, Wrap.card passes the wrapped board's drawing calls on to them
(McRecord), so they send orders and compare only what the calls don't
explain; heads that get a copy hear what was copied (McDamage). See
docs/COPPER.md.

## Known limits

* **AROS gives a card driver one board.** `p96gfx.hidd` calls a `.card`
  library's FindCard once, while rtg.library calls it until it fails. So
  `BOARDS=2` in Multicast.card should give two independent boards on P96
  (not tested yet), but on AROS only Wrap.card's heads reach the others.

* **AROS also calls the wrapped card directly.** AROS's `p96gfx.hidd`
  calls `FindCard` on every `.card` library in memory, so after Wrap.card
  loads the real card, AROS asks that card for a board too. The card has to
  say no. Multicast.card does, because it has one board. Drivers for real
  hardware normally do too, once they have claimed their Zorro board.
* **Real P96 (rtg.library).** rtg.library calls `InitCard` through
  `bi->CardBase` (fixed in 0.3, see above). Still open: whether it calls
  any slot with a BoardInfo that isn't in a0.
* **Stays loaded.** BoardInfo pointers lead into Wrap.card's code, so it
  never expunges (like Multicast.card).

## Testing

* `make check` runs `tests/wraptest.c` under vamos. Standing in for
  rtg.library, it loads the built `Wrap.card`, which wraps `Fake.card` (a
  test driver in `tests/fakecard.c` whose functions record the registers
  they get). It checks the register handling exactly, which slots get
  wrapped, the call counts and the recorded display state, with `DRAW=1`
  and `DRAW=0`.
* `make emu-wrap` (`emu/wrap_test.sh`) boots AROS m68k in FS-UAE with
  Wrap.card wrapping Multicast.card, opens and draws 8, 16 and 32 bit
  screens with mctest, and checks `wrapstat`: the board is set up through
  Wrap.card and SetGC and SetPanning went through it. It needs no network.
* `make emu-span` (`emu/span_test.sh`) is a 2x2 span over the network:
  Multicast.card's first board holds a 1280x960 screen in a 640x480 mode,
  and its boards 2 to 5 are the heads. mctest `SPAN=2x2` draws the test
  pattern in each head's tile and moves an orange window from head 1 to
  head 4. At each step, every head must show its pattern, and only the head
  the window is on must show the window.
* `make emu-mirror` (`emu/mirror_test.sh`) is the mirror over the network,
  like `make emu-test` (Riptide, FSUAE_PCAP, root). Wrap.card wraps
  Multicast.card's first board and mirrors it onto the second. mctest
  draws on the first in 8, 16 and 32 bit, and the receivers on ports 9696
  and 9697 must both see the test pattern and the same picture.
