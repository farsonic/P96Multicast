# Copper: the Panorama.card display protocol

The Amiga's Copper runs a list of display instructions. This protocol streams one:
what was drawn (a fill, a copy, a line of text) whenever the viewer can draw it
itself, pixels only for the rest. Panorama.card 2 sends it, and `client/panoview.py`
shows it. It replaces protocol 1 (Panorama.card 1.x), which compared the whole
screen with a copy every pass and sent every change as pixels.

`include/copper.h` is the definitive list of numbers. `src/render.c` is the
reference renderer: the card draws every order into its shadow with it. Viewers must
draw orders exactly the same way. `tests/test_copper.py` checks the Python viewer
against it, in every pixel size.

## Why

Measurements of everyday work on AROS (`mctest WORKLOAD`) showed that 82-95% of
everything that changes on screen comes from a drawing call that the card can hear
about (rtg.library's blitter functions). Most of those calls are:

* copies within the screen: windows moved, scrolling;
* fills;
* text;
* patterns.

Copper is built on three rules:

1. **The Amiga spends CPU only on what changed.** Changes come from the drawing
   calls, not from scanning the whole screen.
2. **Send what was drawn, not the pixels it made,** whenever the viewer can draw it.
   The viewer has CPU to spare.
3. **Nothing is sent twice unless it was lost.** Viewers say what they missed,
   instead of the Amiga resending everything just in case.

## How the card works

```
 rtg.library ──calls──▶ Panorama.card stubs ──▶ rtg.library's own drawing
                             │                       (into the frame buffer)
                             ▼
                       the order ring
                             │
     sender process, FPS times a second:
       1. orders: drawn into the shadow, and sent
       2. comparing: the frame buffer against the shadow, where there is reason
          to; whatever differs is sent as pixels
       3. what viewers asked for: lost datagrams again, or tiles from the shadow
                             │
                     numbered datagrams ──────▶ viewers
                             ◀── HELLO / NAK / WANT (to the sender's address)
```

* **The shadow** is the card's copy of what the viewers have. Each order that is
  sent is also drawn into the shadow, with the same renderer the viewers use. So
  comparing the frame buffer with the shadow finds two things:
  * what no order explained;
  * anything an order drew slightly differently from rtg.library.

  Both are sent as pixels. Viewers end up exact even where the reference renderer
  and rtg.library disagree.
* **Orders** (`src/orders.c`): with `BLITTER=1` (the default), the card offers
  rtg.library the blitter functions. Each one records what it is about to draw on
  the order ring, then goes on to rtg.library's own software version, which draws.
  * `FillRect`, `InvertRect`, `BlitRect` and plain (`BLIT_SRC`) copies in
    `BlitRectNoMaskComplete` within the screen become orders, as do `BlitTemplate`
    (text) and `BlitPattern`.
  * `DrawLine`, planar blits, masked blits and other copy operations are recorded
    only as rectangles to compare.
  * If the ring overflows (a few thousand calls in one pass), the card compares
    everything once.
* **Comparing**, where there is reason to:
  * rectangles the ring says were drawn some other way, from the next pass on
    (the drawing happens after the call is recorded);
  * the outer tiles of every order, where frames and borders are drawn around it;
  * tiles that changed lately, every pass for half a second, so CPU-drawn
    animation stays smooth;
  * the area around the mouse pointer while it moves;
  * the background scan: a slice of the screen every pass. It covers the whole
    screen every `SCAN=` seconds at best (default 2), and never takes more than
    `CPU=` percent of the time (default 20). The pace adapts every pass,
    measured with the E-clock, so a slow Amiga scans more slowly instead of
    slowing down.
* **Pacing:** at most `RATE=` KB a second leave (default 1500). Orders are
  always sent. Pixels wait while the datagrams already made would take more than
  two passes to send.
* **The pointer:** with `POINTER=1` (the default), the card has a hardware
  pointer, so rtg.library never draws it into the screen. The card sends its
  shape and position, and the viewers draw it on top. A pointer move costs a
  20-byte record instead of two redrawn areas. Under Wrap.card, a head passes on
  the wrapped card's pointer, if that card has one.
* **Timing:** with `FPS=50` (the default) a pass runs just after each vertical
  blank, as the programs that wait for it (`WaitTOF()`) have just moved their
  sprites and drawn: one update per frame they make, none of them shown twice
  or missed. Each FRAME carries its time on the Amiga, and panoview shows
  every update that far after the one before, a little behind (the network's
  jitter, learnt as it goes, 100 ms at most), however unevenly the network
  brings them, following the drift between the two clocks: motion as even
  as on the Amiga (`--no-pacing`: as they come, less delay). A viewer filling
  in (all of the screen, when it joins) takes 40% of a frame's time a pass
  at most, so the sprites and the pointer keep up meanwhile. `mctest STATUS`
  shows where a pass's time goes.
* **Sprites:** with `SPRITES=1` (the default), graphics.library's sprites work
  on Panorama screens. A program uses them as on any Amiga: `GetExtSprite()`
  (or `GetSprite()`), `AllocSpriteData()`, `ChangeExtSprite()` (or
  `ChangeSprite()`), `MoveSprite()`, `FreeSprite()`. On a Picasso96 board
  those normally show nothing; the card patches them, and for a ViewPort on
  its board keeps sprites 1-7 (0 is Intuition's pointer) and sends them: the
  viewers draw them over the screen, under the pointer, lower numbers in
  front. As the hardware shows a sprite by pointing at its image, the card
  sends each image once, under an id the viewers keep (SPRITEIMAGE), and a
  SPRITE record says where sprite n is and which image it shows: a move, or
  a change to an image already sent, is an 11-byte record (with its header)
  and changes no pixel of the screen. A spinning Boing ball's 16 images go
  once; then 40 seconds of bouncing cost about 50 KB, where sending its
  images each frame cost 1.5 MB. A viewer that joins asks for everything,
  and the card sends the images again.
  As the hardware's: 16, 32 or 64 pixels wide (as `AllocSpriteData()` makes
  them), colour 0 clear, colours 1-3 from the screen's colour map (16 +
  4 * (n / 2) + 1 ... for sprite n, or where `SpriteBase_Even`/`Odd` say).
  An odd sprite attached to the even one before it (`SPRITE_ATTACHED` in its
  control word) makes one sprite of 15 colours with it, as on the hardware:
  colours 1-15 of the odd sprite's bank (17-31 by default). Not as the
  hardware's: the place is in the screen's pixels, whatever its mode. Only the
  screen shown on the board shows its sprites. Boing draws its ball this way
  on a Panorama screen.

### Under Wrap.card

When Panorama.card 2 is a head that shows the wrapped board's bitmap itself
(`MIRROR=Panorama.card`, `HEADSHARE=1`), Wrap.card passes it the wrapped card's
drawing calls and pointer, through two library functions of Panorama.card:
`McRecord` (-42) and `McPointer` (-54). Mirroring uaegfx.card or a real graphics
card then sends orders as well. Heads that get a copy instead tell Panorama.card
what was copied (`McDamage`, -48), so it compares only that. `wrapstat` shows
"hears the wrapped board's drawing" for such heads.

## Transport

* UDP to `DEST:PORT`, as before: a multicast group, or one machine. Board *n*
  sends to `PORT + n`.
* Datagrams are at most 1472 bytes, to fit a 1500-byte Ethernet frame. Each one
  holds as many records as fit.
* Viewers send messages back to the address and port the datagrams come from.
  This works through NAT (UAE's slirp) and needs no port of its own.
* All numbers are big-endian.

### Datagram header (12 bytes)

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 2 | magic | `0x4355`, "CU" |
| 2 | 1 | version | 1 |
| 3 | 1 | flags | bit 0: a repair, sent again because a viewer asked |
| 4 | 4 | stream | random when the card starts: a new one means start over |
| 8 | 4 | seq | +1 for each new datagram |

Records follow, back to back. Each has a 3-byte header: `type` (1 byte) and the
`length` of its body (2 bytes). Viewers skip unknown types by their length.

### Records

Coordinates are u16 pixels on the surface (the visible screen). A **pixel** is
1-4 bytes, as it is in the card's video memory, in the P96 `RGBFTYPE` that
`SURFACE` names.

| Type | Name | Body |
|---|---|---|
| 0x01 | SURFACE | w, h, format u8, bytes per pixel u8, display on u8, flags u8 (bit 0: takes INPUT), tile w, tile h |
| 0x02 | PALETTE | first u16, count u16, count × RGB |
| 0x03 | FRAME | frame u32: show what came before; then (Panorama.card 3.2) time u32: when the Amiga looked at the screen for it, in ms on its own clock. A viewer may show each update that far after the one before, rather than when it happens to arrive (panoview does): motion as even as on the Amiga, whatever the network's jitter. Older viewers read only the frame |
| 0x10 | FILL | x, y, w, h, pixel |
| 0x11 | INVERT | x, y, w, h: every bit of every pixel flipped |
| 0x12 | COPY | sx, sy, dx, dy, w, h: as if through a buffer (overlap is fine) |
| 0x13 | TEXT | x, y, w, h, mode u8, pad u8, bitx u16, stride u16, fg pixel, bg pixel, then h rows of stride bytes; pixel i of a row is bit bitx + i, MSB first |
| 0x14 | PATTERN | x, y, w, h, mode u8, rowmask u8, px u8, py u8, fg, bg, then rowmask + 1 big-endian words; pixel (i, j) is bit 15 - ((px + i) & 15) of word (py + j) & rowmask |
| 0x20 | PIXELS | x, y, w, h, encoding u8, pad u8, data |
| 0x30 | POINTER | w u8, h u8, hot x s8, hot y s8, colours 1-3 as RGB, then h rows: plane 0 then plane 1, (w + 15) / 16 words each, MSB leftmost |
| 0x31 | POINTERPOS | x s16, y s16 (top left), visible u8, pad u8 |
| 0x32 | SPRITE | n u8 (1-7), visible u8, x s16, y s16 (top left), image u16: sprite n shows that image there |
| 0x33 | SPRITEIMAGE | image u16 (0-63), w u8 (16, 32 or 64), depth u8 (2, or 4 for an attached pair), h u16, first u16, rows u16, colours 1-3 (depth 2) or 1-15 (depth 4) as RGB, then rows rows: planes 0 ... depth - 1, w / 16 words each, MSB leftmost (an attached pair: the even sprite's planes, then the odd one's). Rows first ... first + rows - 1 of the image: it comes in bands, each fitting a datagram, and is whole when the band ending at row h has come. A viewer keeps each image until the same id comes again |

The draw modes of TEXT and PATTERN are graphics.library's: JAM1 0, JAM2 1,
COMPLEMENT 2, INVERSVID 4.
* INVERSVID flips the bit first.
* COMPLEMENT then inverts every pixel whose bit is set.
* Otherwise a set bit draws fg, and a clear bit draws bg in JAM2 and nothing in
  JAM1.

PIXELS encodings:

| Code | Encoding | Data |
|---|---|---|
| 0 | RAW | w × h pixels |
| 1 | SOLID | one pixel, for all of it |
| 2 | RLE | each row: runs of (count u8, 1-255; pixel). Runs never cross rows. |

The card sends pixels a tile at a time (64 bytes × 16 rows), so they always fit a
datagram. It picks SOLID if it can, else RLE if that is smaller than RAW. Solid
tiles side by side in the same colour become one record.

SURFACE goes out at least four times a second, even when nothing changes. It is
the heartbeat: a viewer learns the card's address from it, and notices a lost
last datagram soon.

## Viewer to card

Messages are unicast to the address of the datagrams. Their header (8 bytes) is
the magic, the version, a type and the stream.

| Type | Name | Body |
|---|---|---|
| 1 | HELLO | next seq wanted u32; sent every 2 s |
| 2 | NAK | up to 64 × (first seq u32, count u16): datagrams to send again |
| 3 | WANT | up to 128 × (x, y, w, h): send these pixels from the shadow; one all-zero rectangle means everything, with SURFACE, PALETTE and the pointer |
| 4 | INPUT | up to 128 × (kind u8, a u8, x u16, y u16): the viewer's mouse and keys, with `INPUT=1` |
| 5 | WANTSPRITES | send the pointer and every sprite, with its image, again: a shown sprite's image is missing here (lost with datagrams given up on) |

INPUT kinds:

| Kind | Name | |
|---|---|---|
| 1 | MOVE | the pointer to x, y on this part of the screen |
| 2 | BUTTON | a: 1 left, 2 right, 3 middle; + 0x80 when released |
| 3 | KEY | a: an Amiga raw key code; + 0x80 when released (0x7A/0x7B: the wheel, as NewMouse) |
| 4 | LEAVE | the pointer has left all of the viewer's windows: buttons still held are released, and the screen that was in front before the viewer's mouse brought the card's (the Workbench, say) is in front again |

With `INPUT=1` the card sets bit 0 of the byte after "display on" in SURFACE,
so viewers know their mouse and keys are taken. It writes them to input.device
as the Amiga's own: the pointer goes to the screen shown on the board (which
comes to the front), at the part's position plus the part's place on the
screen. Any part's sender takes them, so every window of a split screen works
the same desktop, from the same device or different ones. `INPUTFROM=` limits
input to one address. Anyone else who can reach the card can type on the Amiga,
so INPUT is off unless set (`SETUP=DESKTOP` and PanoramaPrefs' Open set it).
The card reads `INPUT=` and `INPUTFROM=` from ENV:Panorama.prefs again
when the file changes, and tells the viewers in its next SURFACE.

## Reliability

* Viewers apply datagrams strictly in seq order, because orders depend on the
  picture before them: a COPY reads what is there. A later datagram waits while
  an earlier one is missing.
* After 20 ms, the viewer sends a NAK for what is missing, and again every
  200 ms. The card answers from its history of the last `HISTORY=` KB of
  datagrams (default 512). Repairs go first, ahead of the send budget, and go to
  the group, so other viewers that missed the same datagram get it too.
* If a datagram has not come after 1 s, the viewer applies what it holds and
  marks everything as unknown from that point. It then asks for what it does not
  know (WANT).
* Viewers keep track of which tiles they know:
  * PIXELS and FILL covering a tile make it known.
  * A COPY makes its destination as known as its source.
  * Anything else leaves a tile as it was.

  A new viewer knows nothing, so it asks for everything. A viewer that lost
  datagrams asks only for the tiles it no longer knows. One answer serves every
  viewer that asked at the same time.
* While no viewer has talked for 10 s, the card sends the whole screen every
  `REFRESH=` seconds (default 10, 0 = never). This is for listen-only viewers
  behind a firewall.

## Settings (Panorama.prefs)

| Key | Default | |
|---|---|---|
| `BLITTER` | 1 | send the drawing as orders (0: compare and send pixels only) |
| `POINTER` | 1 | a hardware pointer that the viewers draw (0: rtg.library draws it into the screen) |
| `SPRITES` | 1 | graphics.library's sprites 1-7 on the board's screens, drawn by the viewers (0: not patched, they show nothing) |
| `FPS` | 50 | passes per second; 50 or more: a pass after each vertical blank of the display (`WaitTOF()`), in step with the programs that wait for it, as many as the display has (50 PAL, 60 NTSC) |
| `RATE` | 1500 | most KB per second |
| `SCAN` | 2 | seconds for the background scan to cover the screen, at the fastest |
| `CPU` | 20 | most percent of the time the background scan may take |
| `HISTORY` | 512 | KB of sent datagrams kept for repairs |
| `REFRESH` | 10 | seconds between whole screens while no viewer talks back (0 = never) |
| `SPLIT` | 1x1 | the screen in C×R parts, each a stream of its own on PORT, PORT+1, ... and, with a multicast DEST, in a group of its own, DEST, DEST+1, ... (a sender each, with its own shadow, history and ring position) |
| `DEST2`-`DEST4` | | where parts 2-4 go instead of the next group (`panoview.py --groups`) |
| `INPUT` | 0 | 1: the viewers' mouse and keys work the Amiga |
| `INPUTFROM` | | with `INPUT=1`: only from this address |

## What it costs

`mctest WORKLOAD DUMP=file` reports what each step of everyday work sent, then
dumps the screen. `emu/copper_test.sh` runs it on AROS in FS-UAE and checks that
the viewer's picture equals the dump, pixel for pixel. Results for a 640×480
screen are in the test's output. Window moves and scrolling go out as COPY
orders, and console text as TEXT and FILL orders. Pixels are needed only for
images and for what rtg.library draws with the CPU (lines, planar blits).

## Later

* **Surfaces:** screens the viewers keep, so switching to a screen they have seen
  costs a SHOW instead of the whole screen.
* **Offscreen bitmaps** in video memory as surfaces, so blits from them are
  copies too.
* **Viewports** in the viewer: one desktop spread over several monitors.

## With Riptide

Riptide 0.17 added what this sender asked for:

* **Batches:** with riptide.library 3 running, each sender sends up to 16
  datagrams per `RiptideSendMMsg()` call and reads viewer messages with
  `RiptideRecvMMsg()` (`include/riptide_mmsg.h`). With other stacks it uses
  `sendto()` and `recvfrom()`. `mctest STATUS` says which.
* **Pacing:** a non-blocking UDP socket says `EWOULDBLOCK` while the
  interface is full, instead of dropping the datagram. The card counts that
  as a wait, not an error, and sends the rest in the next pass.
* **No checksums** (`SO_RIPTIDE_NO_CHECK`) is not used yet.
