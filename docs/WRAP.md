# Wrap.card

`Wrap.card` is a P96 card driver that loads another card driver and sits
between it and rtg.library. rtg.library still does the graphics, and the
real driver still drives the hardware. Wrap.card sees every call between
the two. Version 0.1 only passes them through, counts them and records what
is on display. It is the base for features that need to watch or redirect a
real board: mirroring a screen to the network (Multicast's sender), showing
one screen on two boards, spanning a screen across monitors, a software or
viewer-drawn pointer, and driver call logging.

## How it works

rtg.library (or AROS's `p96gfx.hidd`) loads `Wrap.card` as the board's
driver and calls its `FindCard` and `InitCard`. Wrap.card:

1. **FindCard:** reads `CARD=` from `ENV:Wrap.prefs` (or `ENVARC:` early in
   the boot) and opens that card driver. A name without a path is looked
   for in `LIBS:Picasso96/`, and a card already in memory is opened by name
   rather than loaded again. It then calls the card's `FindCard` with the
   same BoardInfo, with `bi->CardBase` set to the real card.
2. **InitCard:** calls the card's `InitCard`. Then, for every BoardInfo
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
`CardData` or `ChipData`, and `bi->CardBase` points at the real card for
drivers that find their library base there. Its own state is in a table
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

The settings are read when FindCard runs, so only from `ENV:`/`ENVARC:`.
The monitor's ToolTypes are passed on to the real card's InitCard untouched.

## Known limits

* **AROS also calls the wrapped card directly.** AROS's `p96gfx.hidd`
  calls `FindCard` on every `.card` library in memory, so after Wrap.card
  loads the real card, AROS asks that card for a board too. The card has to
  say no. Multicast.card does, because it has one board. Drivers for real
  hardware normally do too, once they have claimed their Zorro board.
* **Not yet tried on real P96 (rtg.library).** It has been tested on AROS
  only. The open questions for P96 are whether rtg.library reads
  `bi->CardBase` after InitCard, and whether it calls any slot with a
  BoardInfo that isn't in a0.
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
