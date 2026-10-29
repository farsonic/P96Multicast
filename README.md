# Panorama

*Your Amiga's screen, on every screen.* (Called P96Multicast before 3.1:
PanoramaPrefs' Install takes over its settings, `Multicast.prefs`, and
moves the old card, monitor and commodity out of the way.)

A P96 (Picasso96) graphics card driver for AmigaOS whose "graphics card"
is the network. `Panorama.card` gives rtg.library a board with its video
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
boards ("heads"): `MIRROR=Panorama.card` streams a real board's screen to
the network too, and `SPAN=2x2` shows one big screen across four monitors,
a tile on each, so windows are dragged from monitor to monitor. A
Panorama.card head hears what is drawn on the wrapped board, so mirroring
uaegfx.card or a real card streams orders too. See [docs/WRAP.md](docs/WRAP.md).

## Installing

The easy way is **PanoramaPrefs** (`prefs/`), in the package's top
drawer: a prefs editor for everything below, without P96's own tools. Its
window is MUI (3.8 or later; AROS's Zune too), drawn in the dark, flat look
of Easel (`prefs/muigui.c`, `prefs/theme.c`): pages for the desktop, the
network, the display, Wrap.card and the status, a picture of the monitors
where a click places the Workbench, and a help bubble on every control.
Without MUI it opens a plain GadTools window (`prefs/gui.c`). Its
Install button copies the cards to `LIBS:Picasso96`, makes the monitor and
its icon from a P96 monitor already there, moves monitors that would set a
card up twice to `SYS:Storage/Monitors`, and saves the settings. The card
can offer its own screen modes (`MODES=`), so P96 needs no Picasso96Mode
setup either if you give it a mode list. From a Shell: `PanoramaPrefs SHOW`, `SET KEY=VALUE ...`,
`INSTALL [SETUP=SINGLE|TWO|MIRROR|SPAN] [DRY]`, `STATUS`. `INSTALL` sets up
everything for the next boot: the cards, `mctest`/`wrapstat` in `C:`,
PanoramaPrefs in `SYS:Prefs`, the monitor, screen modes if Picasso96Mode
has none for Panorama, `DEST=10.0.2.2` on UAE's NAT network, and with
`SETUP=` one or two Panorama screens, the display card mirrored, or its
screen split over two (SPAN) or four (SPAN2x2) Panorama boards as one
desktop (it finds the card); switching setups puts back the monitors it moved aside.

By hand:

1. Copy `Panorama.card` to `LIBS:Picasso96/`.
2. P96: copy a monitor file in `DEVS:Monitors` and set its `BOARDTYPE`
   ToolType to `Panorama`. AROS: copy `DEVS:Monitors/P96` to
   `DEVS:Monitors/Panorama` (the monitor driver opens the card named after
   itself).
3. Optionally put settings in `ENVARC:Panorama.prefs` (below).
4. Start your TCP/IP stack as usual. The card starts sending as soon as it
   finds the stack.

## Settings

`KEY=VALUE`, one per line, in `ENV:Panorama.prefs` (read at boot and again
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
| `INPUT` | 0 | 1: the mouse and keys in the viewer windows work the Amiga, on the card's screen (`INPUTFROM=` an address: only from there). Off unless set, since anyone who can reach the card could type on the Amiga; `SETUP=DESKTOP` and PanoramaPrefs' Open set it. The card takes it (and `INPUTFROM=`) from ENV: while it runs, no reboot |
| `SPLIT` | 1x1 | the screen in CxR parts (at most 4), each sent to a port of its own (PORT, PORT+1, ...) and, with a multicast DEST, a group of its own (DEST, DEST+1, ...): with `panoview.py --span CxR`, a window for each, like monitors, and windows dragged from one to the other; `panoview.py --part N` shows one part, e.g. on another device. `SETUP=DESKTOP` sets it up |
| `DEST2` ... `DEST4` | | where parts 2-4 go instead (PanoramaPrefs: Network, "Monitor 2 to" ...); the viewer then takes them with `--groups DEST,DEST2,...` |
| `VRAM` | 8 | video memory in MB (read at boot only) |
| `MAXWIDTH`, `MAXHEIGHT` | 1920, 1200 | largest screen mode offered |
| `BOARDTYPE` | 6 | P96 board type reported to rtg.library (6 = PicassoII) |
| `PRI` | -1 | sender task priority |
| `LOG` | (none) | log file for the sender, e.g. `T:Panorama.log` |
| `QUIET` | 0 | set BIF_QUIET |
| `MODES` | (none) | screen modes the card adds for P96: `640x480,800x600,...`, so no Picasso96Mode setup is needed. Without it (or `NONE`) the modes are P96's own, from Picasso96Mode. With both, remove the board's modes in Picasso96Mode, or sizes show twice. AROS uses its own fixed list |
| `BLITTER` | 1 | send the drawing as orders (0: compare the screen and send pixels only) |
| `BOARDS` | 1 | boards to offer (1-8); board n sends to PORT+n and is named `Panorama<n+1>` from the second on. AROS takes one board per card driver, so there the others are only for Wrap.card's heads. `VRAM` applies to each |

## The shared desktop

`SPLIT=2x1` sends the screen in two halves, each to a port of its own, and
`panoview.py --span 2x1` shows each in its own window: one desktop over two
"monitors", windows dragged from one to the other. PanoramaPrefs' Desktop
page sets it up with a picture of the monitors (layout, size, colours,
open at boot). `PanoramaDesk` (prefs/desktop.c) opens the desktop as a
public screen and is a commodity with hotkeys: ctrl alt + cursor keys
moves the active window to the next monitor, ctrl alt 1-4 centres it on
one, ctrl alt m swaps the desktop and the Workbench, ctrl alt s moves the
active window's program to the other screen (commodities, which most MUI
programs are: Exchange's Hide and Show with the other screen the default
public screen; closing the desktop moves its programs to the Workbench the
same way). `WORKBENCH=` (LEFT1,
BELOW2, ...; the Desktop page's picture, click to move it) says where the
Workbench sits next to the desktop: pushing the mouse over that edge
brings the Workbench to the front with the pointer on its matching edge and
its last active window active again, and back the same way. `Boing`
(demos/boing.c) bounces the ball over all of it, as graphics.library
sprites: Panorama.card (`SPRITES=1`) takes a program's sprites 1-7 on its
screens and the viewers draw them on top, so the ball is round and moves
without a pixel of the screen being sent.

## Watching

```
python3 client/panoview.py                       # a window (tkinter)
python3 client/panoview.py --group 127.0.0.1     # UAE's NAT (slirp): the card sends to 10.0.2.2
python3 client/panoview.py --iface 192.168.1.10  # join on this interface
python3 client/panoview.py --span 2x1            # Wrap.card's SPAN=2x1: a window per board
python3 client/panoview.py --snapshot amiga.png --duration 30
```

numpy makes the viewer much faster, but is optional. The viewer answers
the card (lost datagrams, the picture when it starts), so a firewall must
let UDP from the Amiga's address in and back.

## Building

vbcc, vasm, vlink and the NDK 3.2; see [docs/BUILD.md](docs/BUILD.md).

```
make            # build/Panorama.card, build/Wrap.card, mctest, wrapstat
make check      # BoardInfo offsets, library header checks, Wrap.card under vamos,
                # the viewer against the card's renderer, packet loss
make emu-test   # AROS + Riptide in FS-UAE, end to end
make emu-wrap   # AROS in FS-UAE: Wrap.card wrapping Panorama.card
make emu-mirror # ... mirroring it to a second Panorama board, over the network
make emu-span   # ... one 1280x960 screen on four Panorama boards, a window moved between them
make emu-copper # everyday work on AROS; the viewer must show exactly the card's screen
make emu-prefs  # PanoramaPrefs: install on a clean AROS, its window, a second boot
make emu-os3    # PanoramaPrefs' MUI window on AmigaOS 3.1.4 and MUI 3.8 (your ROM and OS)
make emu-p96    # the desktop install with Picasso96 and uaegfx: the mouse across and back
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
| `prefs/` | PanoramaPrefs: settings and installation, GadTools window and Shell |
| `src/wrapstart.s`, `src/wrap.c`, `src/mirror.c` | Wrap.card: library header and thunks, the wrapper, the mirror |
| `include/wrap_status.h` | Wrap.card's slot table and status block |
| `include/boardinfo.h` | the P96 BoardInfo structure |
| `include/copper.h` | the wire protocol |
| `client/panoview.py` | viewer / receiver |
| `tests/` | offset test, hunk checks, viewer tests (`test_client.py`, `test_copper.py` with `rendertest.c`, `fake_sender.py`), `mctest` and `wrapstat` (Amiga), `wraptest` with `Fake.card` (vamos) |
| `emu/run_test.sh`, `emu/copper_test.sh` | end-to-end tests on FS-UAE |
| `emu/wrap_test.sh`, `emu/mirror_test.sh`, `emu/span_test.sh` | Wrap.card on FS-UAE, its mirror and span over the network |
