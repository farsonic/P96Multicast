# The P96Multicast protocol

`Multicast.card` sends the visible screen as UDP datagrams, by default to
the multicast group **239.255.96.1, port 9696** (both settable, and the
destination may also be a unicast address). Any number of receivers can
join the group. Nothing is ever sent back to the Amiga, so a receiver that
joins late or loses packets has to catch up from what keeps arriving. The
sender makes sure it can:

* MODE (and, in CLUT modes, the whole PALETTE) is repeated once a second;
* unchanged tiles are resent slowly in the background (`REFRESH`, default
  every 5 seconds for the whole screen).

Each datagram is at most 1472 bytes, so it fits a 1500 byte Ethernet MTU
without fragmenting. All numbers are big-endian. `include/p96mc_proto.h`
has the same definitions for C.

## Header (16 bytes, every packet)

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 4 | magic | `0x5039364D`, "P96M" |
| 4 | 1 | version | 1 |
| 5 | 1 | type | 1 MODE, 2 PALETTE, 3 TILE, 4 SYNC |
| 6 | 2 | flags | bit 0 (TILE): a background refresh of an unchanged tile |
| 8 | 4 | stream | random, chosen when the card starts |
| 12 | 4 | frame | update pass number; the same in all packets of a pass |

When the stream number changes, the Amiga has rebooted: forget everything
and wait for the next MODE. Ignore packets of an unknown version.

## MODE (type 1)

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | width in pixels |
| 18 | 2 | height in pixels |
| 20 | 1 | pixel format (P96 `RGBFTYPE`, below) |
| 21 | 1 | bytes per pixel |
| 22 | 1 | display: 0 = off (show black), 1 = on |
| 23 | 1 | 0 |
| 24 | 2 | tile width (pixels) |
| 26 | 2 | tile height (pixels) |
| 28 | 2 | update passes per second |
| 30 | 2 | 0 |

A MODE with a new size or format replaces the screen; everything has to be
received again. Tiles are laid out on a grid of tile width × tile height
from the top left; tiles on the right and bottom edges are clipped.

## PALETTE (type 2)

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | first entry |
| 18 | 2 | count |
| 20 | 3 × count | red, green, blue, 8 bits each |

Only for CLUT screens. A palette change applies to pixels already received
(palette animation works).

## TILE (type 3)

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | x |
| 18 | 2 | y |
| 20 | 2 | width |
| 22 | 2 | height |
| 24 | 1 | encoding: 0 RAW, 1 SOLID |
| 25 | 1 | 0 |
| 26 | ... | pixels |

RAW: width × height pixels, row by row, no padding between rows. SOLID:
a single pixel that fills the whole rectangle. Pixels are exactly as they
are in the Amiga's video memory, in the format given by MODE.

## SYNC (type 4)

| Offset | Size | Field |
|---|---|---|
| 16 | 2 | tiles sent in this pass |
| 18 | 2 | 0 |

Ends an update pass. A receiver that wants to avoid tearing draws on SYNC.
A pass in which nothing changed sends no SYNC.

## Pixel formats

The number is P96's `RGBFTYPE`. Byte order is memory order; "PC" formats
are little-endian 16 bit words.

| Format | Bytes | Layout |
|---|---|---|
| 1 CLUT | 1 | palette index |
| 2 R8G8B8 | 3 | R, G, B |
| 3 B8G8R8 | 3 | B, G, R |
| 4 R5G6B5PC | 2 | `gggbbbbb rrrrrggg` |
| 5 R5G5B5PC | 2 | `gggbbbbb 0rrrrrgg` |
| 6 A8R8G8B8 | 4 | A, R, G, B |
| 7 A8B8G8R8 | 4 | A, B, G, R |
| 8 R8G8B8A8 | 4 | R, G, B, A |
| 9 B8G8R8A8 | 4 | B, G, R, A |
| 10 R5G6B5 | 2 | `rrrrrggg gggbbbbb` |
| 11 R5G5B5 | 2 | `0rrrrrgg gggbbbbb` |
| 12 B5G6R5PC | 2 | `gggrrrrr bbbbbggg` |
| 13 B5G5R5PC | 2 | `gggrrrrr 0bbbbbgg` |

Alpha bytes carry no meaning. To widen 5 or 6 bit channels to 8 bits,
repeat the top bits (`v << 3 | v >> 2` for 5 bits).
