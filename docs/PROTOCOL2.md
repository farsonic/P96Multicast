# Protocol 2 (proposal)

Protocol 1 ([PROTOCOL.md](PROTOCOL.md)) makes the Amiga do all the work: it
finds out what changed by comparing the whole screen with a shadow copy
every pass, sends changed tiles as pixels, and resends everything slowly in
the background in case a receiver missed something. The receivers only
paint what arrives. This proposal moves work to where the CPU is: the
Amiga reports what it drew, and the clients do the rest.

## What the measurements say

From the span tests (`emu/span_test.sh`, AROS on an emulated 68040,
Riptide, five Multicast boards):

* Comparing costs as much as copying. Change-only copying cut what Wrap.card
  writes to each head from 600 KB per pass to about 1 MB per minute, and
  freed no CPU, because finding the changes reads both buffers.
* With the heads' comparisons gone (`HEADSHARE=1`), five senders comparing a
  640x480 screen 25 times a second still starve the network: each receiver
  got about 40 KB/s, short of a full screen in 11 seconds. At 10 passes a
  second they keep up.
* Riptide was not the limit. `sendto` runs in the caller's task, and UDP
  copies and checksums the payload in one pass.

So the cost is in *finding* changes and in resending things nobody lost.
Protocol 2 addresses both.

## 1. Changes from the drawing calls, not from comparing

Multicast.card offers rtg.library a "blitter": `FillRect`, `BlitRect`
(screen to screen: window drags, scrolling), `InvertRect`, `BlitTemplate`
(text), `BlitPattern`, `BlitRectNoMaskComplete`, `DrawLine`. Each draws into
the frame buffer itself (in Fast RAM, with the CPU, as rtg.library would
have) and also emits an **order** that describes the operation. rtg.library
sends most drawing through these, so the sender learns the changed
rectangles, and often the operation itself, for free.

Writes that don't go through the card (programs writing into a locked
bitmap, `WritePixelArray` and other CPU paths) are caught by a slow
background comparison: a stripe of the screen per pass, the whole screen
about once a second, instead of all of it every pass. Each order is also
applied to the shadow, so the background comparison finds only what the
orders missed.

Which drawing goes through the card on AROS and on P96 has to be measured
first (Wrap.card's call counts will show it). Start with the four that
matter most: FillRect, BlitRect, InvertRect and BlitTemplate. Leave the
rest to rtg.library's software paths, where the background comparison
catches them.

## 2. Orders: let the client draw

| Packet | Body | Instead of |
|---|---|---|
| FILL | x, y, w, h, pixel | the area's tiles |
| COPY | sx, sy, dx, dy, w, h | the whole destination; a window drag or a scroll becomes about 20 bytes plus the newly exposed area |
| INVERT | x, y, w, h | the area's tiles |
| TEXT | x, y, w, h, fg, bg, draw mode, 1 bit per pixel mask | text as pixels (8x per pixel at 8 bit, 32x at 32 bit) |
| TILE | as in protocol 1, plus optional RLE | |
| POINTER_SHAPE | w, h, hotspot, image | |
| POINTER_POS | x, y, visible | redrawing the pointer into the picture on every mouse move |

Every packet carries a sequence number. Orders depend on what came before
(COPY reads the client's current picture), so a client applies them in
order and must not skip one.

**Pointer.** Multicast.card claims a hardware sprite (`SetSprite`,
`SetSpriteImage`, `SetSpritePosition`). rtg.library then stops drawing the
pointer into the frame buffer, and the clients draw it themselves. Mouse
movement no longer damages the picture, and in a span the right monitor
shows it.

## 3. A back channel instead of blind refresh

Clients send small unicast UDP messages to the Amiga (one port, e.g. 9695):

* **HELLO:** a new client. The Amiga answers with a keyframe: the screen as
  TILEs from the shadow, which is exactly what the clients should have.
* **NAK:** sequence numbers that went missing. The Amiga resends them from a
  history ring (e.g. the last 256 KB of packets). If they are gone from the
  history, it sends a keyframe of the affected area instead.

The periodic background refresh (`REFRESH=`), which today costs CPU and
bandwidth all the time, is only needed for listen-only receivers and can be
much slower. Many receivers on one multicast group still cost the Amiga
the same as one, apart from NAKs, which clients can delay at random and
suppress when another client has already asked (as in reliable multicast).

## 4. Clients as monitors

The Amiga sends **one** stream of the whole desktop, and each viewer shows
its own part of it (`p96view.py --view 640,0,640,480`, or `--head 2x2:2`).
For network monitors this replaces Wrap.card's heads: one board and one
comparison, however many monitors. A 2x2 span becomes one Multicast board
with a 1280x960 desktop and four viewers. Optionally, tiles can be split
over several multicast groups by region, so a viewer only receives its
quarter.

Wrap.card's heads stay for real graphics cards, which can't be fed from
the network.

Clients can also scale, record, and decode compressed tiles, because they
have the CPU to spare.

## 5. Later: input

A back channel can carry mouse and keyboard events from the viewer to the
Amiga too (injected with an input handler). That makes the viewer a remote
desktop, not just a monitor. It needs at least a shared secret, and should
stay on the LAN.

## Order of work

1. Viewports in the client, and a sender option to stream the whole desktop
   (a screen larger than its mode); 4 network monitors from one board.
2. Sequence numbers, HELLO/NAK, keyframes from the shadow; slower REFRESH.
3. Orders: FillRect, BlitRect, InvertRect and BlitTemplate in
   Multicast.card, applied to the shadow too, and the background comparison
   in stripes. Measure the call mix with Wrap.card first.
4. Client-drawn pointer: hardware sprite emulation in Multicast.card.
5. Input.

Protocol 1 receivers ignore packets with version 2, so a sender can offer
both (e.g. version 1 tiles on the current port) while clients move over.

## For Riptide

Nothing in Riptide limits this today. Things that would help a sender like
this one:

* **Pacing:** a way to wait until the interface can take another datagram
  (a socket reported writable by `WaitSelect` when the SANA-II write queue
  has room). Today `sendto` returns `ENOBUFS` at `S2_MAX_WRITES`, and the
  only option is to retry blindly later.
* **Batching:** sending several datagrams in one call (like `sendmmsg`).
  Orders are small packets, and the per-call cost (library call, stack lock,
  route cache) would be paid once per batch. Worth measuring before
  building.
* **Per-socket checksums:** turning off the UDP checksum for one socket
  (like Linux's `SO_NO_CHECK`), for LAN-only video, instead of the global
  `net.inet.udp.checksum` sysctl.
* Already there and enough: `sendmsg` with iovecs, IGMP joins, multicast
  loopback only when a local socket joined, and the send path inline in the
  caller's task.
