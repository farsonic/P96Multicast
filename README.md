# P96Multicast

A P96 (Picasso96) graphics card driver for AmigaOS whose "graphics card"
is the network. `Multicast.card` gives rtg.library a board with its video
memory in Fast RAM, and a background process streams the screen as UDP to a
multicast group (or one machine), in **Copper**, a protocol of drawing
orders: what was drawn (fills, copies, text, patterns) goes out as a few
bytes for the viewer to draw, and pixels only for the rest. Any number of
machines on the LAN can watch the Amiga's screen with the viewer in
`client/`. See [docs/COPPER.md](docs/COPPER.md).

Status: works in 8, 15/16, 24 and 32 bit modes up to 1920x1200 (settable),
tested under AROS m68k's P96 layer in FS-UAE with the
[Riptide](https://github.com/farsonic/Riptide) TCP/IP stack, and on real
P96 (3.x) in FS-UAE. rtg.library still draws everything into the frame
buffer with the CPU; the card hears about it through the blitter functions
it offers, and the viewers draw the mouse pointer.

## How it works

* `FindCard` allocates the video memory (8 MB by default) from Fast RAM.
* `InitCard` describes the board to rtg.library: RGB formats, maximum
  resolutions, a table of virtual pixel clocks, and the BoardInfo
  functions, and starts the sender process.
* The BoardInfo functions (`SetGC`, `SetDAC`, `SetPanning`,
  `SetColorArray`, `SetDisplay`, ...) only record what is on screen.
* The blitter functions (`FillRect`, `BlitRectNoMaskComplete`,
  `BlitTemplate`, ...) put what they draw on an order ring, and let
  rtg.library's own software versions draw it.
* The sender, FPS times a second, draws the orders into a shadow (the
  viewers' picture) and sends them; compares the screen with the shadow
  only where there is reason to (what the orders don't cover, what changed
  lately, a slice of the rest, within a CPU budget) and sends what differs
  as pixels; and answers the viewers, who say what they missed. Datagrams
  are numbered and kept for repairs.
* The sender waits until a TCP/IP stack is running (it never loads
  bsdsocket.library from disk itself), so the card can start at boot
  before the network.

The interface follows the P96 driver documentation at
<https://wiki.icomp.de/wiki/P96_Driver_Development>. The BoardInfo layout
in `include/boardinfo.h` was written from the published offsets (AROS
p96gfx.hidd, WinUAE) and is checked by `tests/offsets.c`.

## Wrap.card

`Wrap.card` is a second driver in this repository: a card driver that
loads a real card driver (`CARD=uaegfx.card` in `ENV:Wrap.prefs`) and sits
between it and rtg.library, putting a thunk in front of every BoardInfo
function. It passes every call through unchanged, counts them and records
what is on display (`wrapstat` shows it). It can show the screen on other
boards ("heads"): `MIRROR=Multicast.card` streams a real board's screen to
the network too, and `SPAN=2x2` shows one big screen across four monitors,
a tile on each, so windows are dragged from monitor to monitor. A
Multicast.card head hears what is drawn on the wrapped board, so mirroring
uaegfx.card or a real card streams orders too. See [docs/WRAP.md](docs/WRAP.md).

## Installing

The easy way is **MulticastPrefs** (`prefs/`), in the package's top
drawer: a prefs editor for everything below, without P96's own tools. Its
Install button copies the cards to `LIBS:Picasso96`, makes the monitor and
its icon from a P96 monitor already there, moves monitors that would set a
card up twice to `SYS:Storage/Monitors`, and saves the settings. The card
can offer its own screen modes (`MODES=`), so P96 needs no Picasso96Mode
setup either if you give it a mode list. From a Shell: `MulticastPrefs SHOW`, `SET KEY=VALUE ...`,
`INSTALL [SETUP=SINGLE|TWO|MIRROR|SPAN] [DRY]`, `STATUS`. `INSTALL` sets up
everything for the next boot: the cards, `mctest`/`wrapstat` in `C:`,
MulticastPrefs in `SYS:Prefs`, the monitor, screen modes if Picasso96Mode
has none for Multicast, `DEST=10.0.2.2` on UAE's NAT network, and with
`SETUP=` one or two Multicast screens, the display card mirrored, or its
screen split over two (SPAN) or four (SPAN2x2) Multicast boards as one
desktop (it finds the card); switching setups puts back the monitors it moved aside.

By hand:

1. Copy `Multicast.card` to `LIBS:Picasso96/`.
2. P96: copy a monitor file in `DEVS:Monitors` and set its `BOARDTYPE`
   ToolType to `Multicast`. AROS: copy `DEVS:Monitors/P96` to
   `DEVS:Monitors/Multicast` (the monitor driver opens the card named after
   itself).
3. Optionally put settings in `ENVARC:Multicast.prefs` (below).
4. Start your TCP/IP stack as usual. The card starts sending as soon as it
   finds the stack.

## Settings

`KEY=VALUE`, one per line, in `ENV:Multicast.prefs` (read at boot and again
when the TCP/IP stack appears), or as ToolTypes of the monitor icon under
P96. `ENV:` wins over ToolTypes.

| Key | Default | |
|---|---|---|
| `DEST` | 239.255.96.1 | destination address, multicast or unicast |
| `PORT` | 9696 | destination UDP port |
| `TTL` | 1 | multicast TTL (1 = this LAN only) |
| `FPS` | 50 | update passes per second (1-50) |
| `RATE` | 1500 | most KB per second to send |
| `SCAN` | 2 | seconds to compare the whole screen in, at the fastest |
| `CPU` | 20 | most percent of the CPU the background comparing takes |
| `HISTORY` | 512 | KB of sent datagrams kept, to repair what viewers lost |
| `REFRESH` | 10 | while no viewer talks back: seconds between whole screens (0 = never) |
| `POINTER` | 1 | a hardware pointer, drawn by the viewers (0: rtg.library draws it) |
| `VRAM` | 8 | video memory in MB (read at boot only) |
| `MAXWIDTH`, `MAXHEIGHT` | 1920, 1200 | largest screen mode offered |
| `BOARDTYPE` | 6 | P96 board type reported to rtg.library (6 = PicassoII) |
| `PRI` | -1 | sender task priority |
| `LOG` | (none) | log file for the sender, e.g. `T:Multicast.log` |
| `QUIET` | 0 | set BIF_QUIET |
| `MODES` | (none) | screen modes the card adds for P96: `640x480,800x600,...`, so no Picasso96Mode setup is needed. Without it (or `NONE`) the modes are P96's own, from Picasso96Mode. With both, remove the board's modes in Picasso96Mode, or sizes show twice. AROS uses its own fixed list |
| `BLITTER` | 1 | send the drawing as orders (0: compare the screen and send pixels only) |
| `BOARDS` | 1 | boards to offer (1-8); board n sends to PORT+n and is named `Multicast<n+1>` from the second on. AROS takes one board per card driver, so there the others are only for Wrap.card's heads. `VRAM` applies to each |

## Watching

```
python3 client/p96view.py                       # a window (tkinter)
python3 client/p96view.py --group 127.0.0.1     # UAE's NAT (slirp): the card sends to 10.0.2.2
python3 client/p96view.py --iface 192.168.1.10  # join on this interface
python3 client/p96view.py --span 2x1            # Wrap.card's SPAN=2x1: a window per board
python3 client/p96view.py --snapshot amiga.png --duration 30
```

numpy makes the viewer much faster, but is optional. The viewer answers
the card (lost datagrams, the picture when it starts), so a firewall must
let UDP from the Amiga's address in and back.

## Building

vbcc, vasm, vlink and the NDK 3.2; see [docs/BUILD.md](docs/BUILD.md).

```
make            # build/Multicast.card, build/Wrap.card, mctest, wrapstat
make check      # BoardInfo offsets, library header checks, Wrap.card under vamos,
                # the viewer against the card's renderer, packet loss
make emu-test   # AROS + Riptide in FS-UAE, end to end
make emu-wrap   # AROS in FS-UAE: Wrap.card wrapping Multicast.card
make emu-mirror # ... mirroring it to a second Multicast board, over the network
make emu-span   # ... one 1280x960 screen on four Multicast boards, a window moved between them
make emu-copper # everyday work on AROS; the viewer must show exactly the card's screen
make emu-prefs  # MulticastPrefs: install on a clean AROS, its window, a second boot
```

## Layout

| Path | |
|---|---|
| `src/libstart.s` | library header: RomTag, function table |
| `src/card.c` | library init, FindCard, InitCard, the BoardInfo functions |
| `src/sender.c` | the sender process: Copper |
| `src/orders.c` | the drawing functions and the pointer: orders for the sender |
| `src/render.c` | Copper's reference renderer (the shadow; the viewer must match it) |
| `src/config.c` | settings |
| `src/modes.c` | the screen modes the card offers P96 |
| `prefs/` | MulticastPrefs: settings and installation, GadTools window and Shell |
| `src/wrapstart.s`, `src/wrap.c`, `src/mirror.c` | Wrap.card: library header and thunks, the wrapper, the mirror |
| `include/wrap_status.h` | Wrap.card's slot table and status block |
| `include/boardinfo.h` | the P96 BoardInfo structure |
| `include/copper.h` | the wire protocol |
| `client/p96view.py` | viewer / receiver |
| `tests/` | offset test, hunk checks, viewer tests (`test_client.py`, `test_copper.py` with `rendertest.c`, `fake_sender.py`), `mctest` and `wrapstat` (Amiga), `wraptest` with `Fake.card` (vamos) |
| `emu/run_test.sh`, `emu/copper_test.sh` | end-to-end tests on FS-UAE |
| `emu/wrap_test.sh`, `emu/mirror_test.sh`, `emu/span_test.sh` | Wrap.card on FS-UAE, its mirror and span over the network |
