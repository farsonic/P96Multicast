# Protocol 2: the Multicast.card display protocol

Status: design. Protocol 1 ([PROTOCOL.md](PROTOCOL.md)) is what the card
sends today. This document is the plan for its successor, written from
measurements of what actually changes on an Amiga screen and where the
Amiga's CPU goes.

## Why a new protocol

Protocol 1 makes the Amiga do all the work. It finds changes by comparing
the whole screen with a shadow copy every pass, sends every change as
pixels, and resends the whole screen slowly in case someone missed
something. In the span tests (AROS on an emulated 68040), five boards
comparing at 25 passes a second left the network starved. Comparing reads
as much memory as copying, so changing what gets copied freed no CPU.

Protocol 2 is built on three rules:

1. **The Amiga only spends CPU on what changed.** Changes come from the
   drawing calls, not from scanning the screen.
2. **Send what was drawn, not the pixels it made**, whenever the client can
   draw it: a filled rectangle, a copy, a line of text. The client has the
   CPU to spare.
3. **Nothing is sent twice unless it was lost.** Clients say what they
   missed, instead of the Amiga resending everything just in case.

## What the measurements say

`emu/measure_test.sh`: Multicast.card with `BLITTER=1` on AROS m68k
(p96gfx.hidd), mctest `WORKLOAD` at 640x480 (see the results section at the
end for the full table). Every call rtg.library makes to the card's drawing
functions records the rectangle it changes, and the sender checks which
changed tiles those calls explain.

* **About 82-95% of all changed tiles were explained** by drawing calls the
  card heard about (the range comes from how the counting is done; see the
  table at the end).
* **Copies dominate:** `BlitRectNoMaskComplete` (window moves, scrolling,
  bitmap to screen) explained about half of all changed tiles, and in
  scrolling 96-98% of them, with just 30 calls.
* **Fills come next** (FillRect, about a quarter), then lines, text and
  patterns. Text has many calls but covers few tiles.
* **What the calls miss:** blits from planar bitmaps, `WritePixelArray`
  and `WriteChunkyPixels`, and a few window-refresh paths. AROS does these
  with the CPU straight into video memory. Opening a new screen in the same
  mode is also missed: it is drawn before it is shown, then swapped in by
  `SetPanning`.

So most changes can travel as small drawing commands. The rest need a
safety net, which should cost little when nothing happens.

## Overview

```
 rtg.library ──calls──▶ Multicast.card ──▶ frame buffer (Fast RAM)
                         │   ▲
                         │   └── background scan (a stripe per pass) ──┐
                         ▼                                              │
              orders + shadow update ─────────▶ encoder ◀── pixels ────┘
                                                   │
                                         datagrams (seq-numbered)
                                                   │
                          multicast ───────────────┼──────────▶ clients
                          unicast ◀── HELLO/NAK/ACK/INPUT ─────┘
```

* The **shadow** is the Amiga's copy of what clients have. Every order that
  is sent is also applied to the shadow, by the same reference renderer the
  clients use.
* The **background scan** compares the frame buffer with the shadow, a
  stripe at a time. It finds what no call reported, and anything where the
  reference renderer and rtg.library disagree, and sends it as pixels. This
  keeps the clients correct even if an order is drawn slightly differently
  on the client than on the Amiga: the scan corrects it.

## Transport

* **Data:** UDP, multicast (or unicast) to `DEST:PORT`, as today. Datagrams
  are at most 1472 bytes, to fit a 1500-byte Ethernet MTU without
  fragmenting. Each datagram holds as many records as fit.
* **Control:** clients send small unicast datagrams to the Amiga's control
  port (`PORT - 1`, default 9695). Without a control channel (a
  listen-only client, a firewall), clients still work, from the slow
  keyframe cycle.
* All numbers are big-endian (the Amiga's order).

### Datagram header (12 bytes)

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 2 | magic | `0x5032`, "P2" |
| 2 | 1 | version | 2 |
| 3 | 1 | flags | bit 0: a repair (resent); bit 1: part of a keyframe |
| 4 | 4 | stream | random when the card starts; a change means start over |
| 8 | 4 | seq | datagram number, +1 for each new datagram |

The records follow, back to back.

### Records

Each record has a 3-byte header: `type` (1) and `length` of the body (2).
Unknown types are skipped by their length, so the protocol can grow.

Coordinates are u16. A **pixel** is in the surface's format (1, 2, 3 or 4
bytes, P96 `RGBFTYPE`, as in protocol 1). The client converts.

| Type | Name | Body |
|---|---|---|
| 0x01 | SURFACE | id u8, width, height, format u8, flags u8 |
| 0x02 | SHOW | id u8, x offset, y offset (panning), display on u8 |
| 0x03 | PALETTE | first u8, count u16, then RGB triples |
| 0x04 | FRAME | frame u32: everything up to here belongs together; present it |
| 0x10 | FILL | id, x, y, w, h, pixel |
| 0x11 | INVERT | id, x, y, w, h, plane mask u8 |
| 0x12 | COPY | src id, sx, sy, dst id, dx, dy, w, h (plain copy only) |
| 0x13 | TEXT | id, x, y, w, h, draw mode u8, fg pixel, bg pixel, then h rows of ceil(w/8) bytes, one bit per pixel |
| 0x14 | PATTERN | id, x, y, w, h, draw mode, fg, bg, x/y offset, rows u8, then rows × u16 |
| 0x15 | LINE | id, x1, y1, x2, y2, draw mode, fg, bg, line pattern u16, pattern shift u8 |
| 0x20 | PIXELS | id, x, y, w, h, encoding u8, data |
| 0x30 | POINTER_SHAPE | width u8, height u8, hot x s8, hot y s8, format u8, data |
| 0x31 | POINTER_POS | x, y, visible u8 |

Draw modes are P96's: JAM1, JAM2, COMPLEMENT, INVERSVID. LINE carries the
endpoints and the pattern state, and the client draws it with the same
Bresenham steps as P96 (the reference renderer).

**PIXELS encodings,** in increasing order of what they cost the Amiga to
produce:

| Code | Encoding | |
|---|---|---|
| 0 | RAW | w × h pixels |
| 1 | SOLID | one pixel for the whole rectangle |
| 2 | RLE | per row: (count u8, pixel) runs |
| 3 | INDEXED | up to 16 pixels in a table, then 4 bits per pixel |

The Amiga picks the cheapest one that fits: SOLID and RLE are found while
the row is read anyway. Decoding cost is the client's.

### Surfaces

A surface is a bitmap the clients keep: the screen on display, and screens
seen before. `SURFACE` defines one, `SHOW` puts one on display (with its
scroll position). A screen switch to a surface the clients already have is
just a `SHOW` and the differences (see below). Opening a requester screen
and closing it again then costs almost nothing. Surface ids are small, and
the Amiga reuses them when bitmaps are freed. Offscreen bitmaps in video
memory could become surfaces too (`COPY` from them would then need no
pixels), in a later version.

### Pointer

With `SetSprite`, `SetSpriteImage` and `SetSpritePosition`, Multicast.card
says it has a hardware pointer. rtg.library then stops drawing the pointer
into the screen, and the card sends `POINTER_SHAPE` once and `POINTER_POS`
on every move, in a datagram of its own, at once. Clients draw it on top.
A pointer move becomes a 20-byte datagram instead of two redrawn areas.

## Reliability

* **seq:** clients apply datagrams strictly in seq order (orders depend on
  the picture before them: a COPY reads what is there). A client holds back
  later datagrams while it waits for a missing one, up to a limit.
* **NAK** (client to control port): seq ranges that are missing. The Amiga
  resends them from a history ring of recent datagrams (e.g. 256 KB),
  marked as repairs, on the multicast group, so any other client that
  missed the same ones gets them too. Clients wait a random 0-20 ms before
  sending a NAK and drop it if a repair arrives meanwhile (NAK suppression,
  as in reliable multicast).
* **KEYFRAME:** when a NAK asks for what has already left the history, or
  for a new client (HELLO), the Amiga sends the surface from its shadow as
  PIXELS, flagged as a keyframe, ending with the seq it is current at. One
  keyframe serves every client that asked during it.
* **HELLO / heartbeat** (client to control port, every few seconds, with
  the last seq applied): lets the Amiga know who is listening. With no
  client heard from, the card can skip sending entirely (`SEND=auto`).
* **Slow keyframes** (`REFRESH=`): only for listen-only clients; off by
  default when the control channel works.

## The Amiga side

Sources of changes, cheapest first:

1. **Drawing calls** (`BLITTER=1`, as measured): each becomes an order. The
   card lets rtg.library draw it (on AROS by declining, on P96 through the
   `...Default` function), applies it to the shadow with the reference
   renderer, and queues the record. An order the encoder can't express
   (masks, unusual minterms) just marks its rectangle damaged.
2. **SetPanning / SetGC:** SURFACE and SHOW. A switch to a surface with a
   shadow compares the new screen with that shadow once and sends the
   differences.
3. **Background scan:** a stripe of the frame buffer per pass against the
   shadow (e.g. 1/16 of the screen at 25 passes a second, so all of it every
   0.64 s), sent as PIXELS. When a stripe finds changes nothing reported
   (a program writing pixels itself: video, games), the scan speeds up for
   that area until it goes quiet again, so such programs still update
   smoothly.

Cost when nothing happens: one stripe per pass, 1/16 of today's comparing.
Cost of each step of a window drag: one COPY and the FILLs of the exposed
edges, under 100 bytes, instead of comparing the screen and sending the
window as pixels again.

**Batching and pacing:** records are packed into full datagrams, flushed at
FRAME or after a few milliseconds. The rate stays within `RATE`.
`ENOBUFS` from the stack means: keep the records and try again next pass.

## Clients

* Apply records in seq order to their surfaces. Present at FRAME.
* Draw the pointer themselves.
* **Viewports:** a client shows the whole surface or a rectangle of it
  (`--view 640,0,640,480`, `--head 2x2:2`). A desktop larger than one
  monitor (Wrap.card's span, without the heads) goes out once, and each
  monitor's client shows its part. The Amiga's cost doesn't grow with the
  number of monitors.
* Scale, record, and keep any number of viewers on one multicast group.

## Local heads: the same records, on real cards

Wrap.card's heads (real graphics cards, `SPAN=`) are clients too, inside
the Amiga. A FILL becomes the head's `FillRect`, a COPY the head's
`BlitRect` within its own video memory, TEXT the head's `BlitTemplate`, each
clipped and shifted to the head's tile. Only PIXELS (and copies that cross
from one head to another) move pixels over Zorro, and only the changed
ones. The heads then need no comparing at all.

## Input (later)

`INPUT` records from a client to the control port (mouse moves, buttons,
raw key codes) make the client a remote desktop. They are injected with an
input handler. This needs a shared secret (`KEY=`) and should stay on the
LAN.

## Order of work

1. **Records and transport:** header, records, PIXELS (RAW, SOLID, RLE)
   from the existing tile comparison, FRAME, SHOW, PALETTE; the client
   decodes protocol 2. Same CPU cost as today, smaller on the wire.
2. **Control channel:** seq, history ring, NAK, HELLO, keyframes from the
   shadow; slow keyframes only for listen-only clients.
3. **Orders:** FILL, COPY, TEXT, INVERT, PATTERN, LINE from the drawing
   calls, with the reference renderer in C shared by card and client, and
   the background scan in stripes. This is the big CPU saving.
4. **Pointer:** hardware sprite emulation, POINTER_SHAPE/POS.
5. **Surfaces:** screens kept by the clients, switches as SHOW.
6. **Viewports** in the client: one desktop on several monitors.
7. **Local heads** replaying orders on real cards' blitters.
8. **Input.**

Protocol 1 stays available (`PROTO=1`) for existing receivers.

## For Riptide

Nothing in Riptide limits this today: `sendto` runs in the caller's task,
and UDP copies and checksums in one pass. What would help a sender like
this one:

* **Pacing:** a socket that `WaitSelect` reports writable when the SANA-II
  write queue has room, instead of `ENOBUFS` at `S2_MAX_WRITES` with
  nothing to wait for.
* **Batching:** several datagrams in one call (like `sendmmsg`), so the
  per-call cost (library call, stack lock, route cache) is paid once per
  batch. Worth measuring first.
* **Per-socket checksums:** turning off the UDP checksum for one socket
  (like Linux's `SO_NO_CHECK`), for LAN-only video, instead of the global
  `net.inet.udp.checksum` sysctl.

## Measurements

`emu/measure_test.sh`, AROS m68k nightly 2026-10-23 in FS-UAE (A4000/040),
Multicast.card with `BLITTER=1`, mctest `WORKLOAD` on a 640x480 screen. A
tile is 64 bytes by 16 rows. A call "explains" a changed tile if it touched
that tile shortly before the sender found it changed. Two runs used two
lengths for "shortly":

| Step | 8 bit, 8 passes | 16 bit, 8 passes | 8 bit, 2 s | 16 bit, 2 s |
|---|---|---|---|---|
| open screen | 316 tiles, 0%* | (new mode) | 298, 100%* | (new mode) |
| console: Dir, List | 460, 91% | | 461, 100% | 482, 87% |
| open window | 244, 60% | 244, 100% | 244, 100% | 244, 100% |
| move, size, depth | 1454, 97% | 1435, 94% | 1345, 98% | 1354, 99% |
| drawing | 256, 86% | 256, 100% | 256, 100% | 256, 100% |
| scrolling | 1221, 100% | 1333, 100% | 1358, 100% | 1278, 100% |
| images | 110, 50% | 110, 50% | 110, 100%† | 110, 100%† |
| close window | 400, 100% | 400, 100% | 400, 89% | 400, 64% |
| close console | 459, 40% | 459, 100% | 459, 54% | 459, 100% |
| **total** | **4920, 82%** | **4994, 91%** | **4931, 94%** | **4583, 95%** |

\* Opening a screen in the mode already shown: drawn before it is shown,
then swapped in by SetPanning. The second pair of runs counts that switch
as an explanation (a SHOW in protocol 2), the first doesn't.

† The images step draws over areas drawn less than 2 s before, so the
longer window credits calls for the CPU-drawn images too. With the shorter
window, the planar blits and `WriteChunkyPixels`/`WritePixelArray` (half
of the step) are rightly not explained.

The short window undercounts (a tile the sender looks at late is missed)
and the long one overcounts, so the truth lies between them: **about 82% to
95% of changed tiles come with a drawing call** on AROS. By tiles, the calls
that matter are `BlitRectNoMaskComplete` (about half: moves, scrolling,
bitmap to screen; 330 copies within the screen in all), `FillRect` (about a
quarter), then `DrawLine`, `BlitTemplate`, `BlitPattern` and `InvertRect`.
The damage ring (1024 rectangles) overflowed once in each run, in the
console step's burst of about 2,500 fills, which costs a little accuracy.

Not measured yet: real P96 (rtg.library). Its mix of calls may differ, and
it is the next thing to measure, with the same tools.
