#!/usr/bin/env python3
"""Pretend to be Multicast.card: mctest's colour bars in Copper, for testing viewers.

    fake_sender.py                         to 239.255.96.1:9696, 8 bit, for 30 s
    fake_sender.py --dest 127.0.0.1 --depth 32 --seconds 0   forever, to localhost
    fake_sender.py --loss 0.2 --final out.raw    lose a fifth; the last screen to a file

Sends the screen as pixels, then moves a square across it with COPY and
FILL orders and writes text with TEXT, like the card does. Answers the
viewer's NAKs from a history of datagrams and its WANTs from the screen.
--loss drops that share of datagrams (repairs too), to test the viewer's
recovery; --final writes the screen at the end in p96view.py's --raw format.
"""
import argparse
import os
import random
import select
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "client"))
import p96view as pv  # noqa: E402

BARS = [0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000]
MAX = 1472


def pixel(fmt, rgb):
    r, g, b = rgb >> 16, (rgb >> 8) & 255, rgb & 255
    if fmt == pv.B8G8R8A8:
        return bytes((b, g, r, 0))
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


class Card:
    def __init__(self, sock, to, w, h, fmt, bpp, loss):
        self.sock, self.to, self.loss = sock, to, loss
        self.w, self.h, self.fmt, self.bpp = w, h, fmt, bpp
        self.tw, self.th = 64 // bpp, 16
        self.stream = random.getrandbits(32)
        self.seq = 0
        self.hist = {}
        self.cur = []
        self.curlen = 12
        # the screen, as the card's shadow: drawn with p96view's own renderer
        self.screen = pv.Screen()
        self.screen.apply(pv.SURFACE, self.surface())
        self.sent = self.lost = 0

    def surface(self):
        return struct.pack(">HHBBBBHH", self.w, self.h, self.fmt, self.bpp, 1, 0, self.tw, self.th)

    def rec(self, rtype, body, draw=True):
        if self.curlen + 3 + len(body) > MAX:
            self.flush()
        self.cur.append(struct.pack(">BH", rtype, len(body)) + body)
        self.curlen += 3 + len(body)
        if draw:
            self.screen.apply(rtype, body)

    def flush(self):
        if not self.cur:
            return
        d = struct.pack(">HBBII", pv.MAGIC, pv.VERSION, 0, self.stream, self.seq) + b"".join(self.cur)
        self.hist[self.seq] = d
        self.hist.pop(self.seq - 4096, None)
        self.seq += 1
        self.cur, self.curlen = [], 12
        self.out(d)

    def out(self, d):
        if random.random() < self.loss:
            self.lost += 1
            return
        self.sock.sendto(d, self.to)
        self.sent += 1

    def tile(self, x, y):
        w, h, bpp = min(self.tw, self.w - x), min(self.th, self.h - y), self.bpp
        rows = [bytes(self.screen.pixels[a:z]) for a, z in self.screen._rows(x, y, w, h)]
        self.rec(pv.PIXELS, struct.pack(">HHHHBB", x, y, w, h, pv.RAW, 0) + b"".join(rows),
                 draw=False)

    def rects(self, x, y, w, h):
        for ty in range(y // self.th * self.th, y + h, self.th):
            for tx in range(x // self.tw * self.tw, x + w, self.tw):
                self.tile(tx, ty)

    def control(self):
        while select.select([self.sock], [], [], 0)[0]:
            m, addr = self.sock.recvfrom(2048)
            magic, ver, mtype, stream = struct.unpack(">HBBI", m[:8])
            if magic != pv.MAGIC or stream != self.stream:
                continue
            if mtype == pv.NAK:
                for i in range(8, len(m) - 5, 6):
                    first, count = struct.unpack(">IH", m[i:i + 6])
                    for q in range(first, first + count):
                        if q in self.hist:
                            d = bytearray(self.hist[q])
                            d[3] |= pv.F_REPAIR
                            self.out(bytes(d))
            elif mtype == pv.WANT:
                for i in range(8, len(m) - 7, 8):
                    x, y, w, h = struct.unpack(">HHHH", m[i:i + 8])
                    if not w:
                        self.rec(pv.SURFACE, self.surface())
                        if self.fmt == pv.CLUT:
                            self.rec(pv.PALETTE, self.palette)
                        x, y, w, h = 0, 0, self.w, self.h
                    self.rects(x, y, w, h)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dest", default="239.255.96.1")
    ap.add_argument("--port", type=int, default=9696)
    ap.add_argument("--depth", type=int, choices=(8, 16, 32), default=8)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--seconds", type=float, default=30, help="0 = until Ctrl-C")
    ap.add_argument("--loss", type=float, default=0, help="share of datagrams to drop")
    ap.add_argument("--linger", type=float, default=1.5,
                    help="seconds to go on answering at the end")
    ap.add_argument("--final", help="write the last screen here (p96view.py --raw format)")
    args = ap.parse_args()

    fmt, bpp = {8: (pv.CLUT, 1), 16: (pv.R5G6B5PC, 2), 32: (pv.B8G8R8A8, 4)}[args.depth]
    w, h = args.width, args.height
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    card = Card(s, (args.dest, args.port), w, h, fmt, bpp, args.loss)

    def px(c):
        return bytes((c,)) if fmt == pv.CLUT else pixel(fmt, c)

    def colour(i):
        return i if fmt == pv.CLUT else BARS[i]

    card.palette = struct.pack(">HH", 0, 256) + b"".join(
        struct.pack("BBB", c >> 16, (c >> 8) & 255, c & 255) for c in BARS) + b"".join(
        bytes((i, i, i)) for i in range(8, 256))
    card.rec(pv.SURFACE, card.surface())
    if fmt == pv.CLUT:
        card.rec(pv.PALETTE, card.palette)
    # the bars, drawn as fills, then sent as pixels as a card would from its shadow
    for i in range(8):
        card.screen.apply(pv.FILL, struct.pack(">HHHH", i * w // 8, 0, (i + 1) * w // 8 - i * w // 8,
                                               h // 2) + px(colour(i)))
    card.screen.apply(pv.FILL, struct.pack(">HHHH", 0, h // 2, w, h - h // 2) + px(colour(7)))
    card.rects(0, 0, w, h)
    card.rec(pv.FRAME, struct.pack(">I", 0))
    card.flush()

    end = time.time() + args.seconds if args.seconds else None
    frame, x, sq = 0, 0, 48
    text = bytes(random.getrandbits(8) for _ in range(8 * 16))
    while end is None or time.time() < end:
        # the square moves right: a copy, and the edge it leaves filled
        y = h // 2 + 32
        if x + sq + 8 >= w:
            card.rec(pv.FILL, struct.pack(">HHHH", x, y, sq, sq) + px(colour(7)))
            x = 0
            card.rec(pv.FILL, struct.pack(">HHHH", x, y, sq, sq) + px(colour(1)))
        else:
            card.rec(pv.COPY, struct.pack(">HHHHHH", x, y, x + 8, y, sq, sq))
            card.rec(pv.FILL, struct.pack(">HHHH", x, y, 8, sq) + px(colour(7)))
            x += 8
        # a line of "text" under it
        card.rec(pv.TEXT, struct.pack(">HHHHBBHH", (frame * 37) % (w - 128), h - 40, 128, 8,
                                      frame & 1, 0, 0, 16) + px(colour(frame % 7)) + px(colour(7))
                 + text[:8 * 16])
        frame += 1
        card.rec(pv.FRAME, struct.pack(">I", frame))
        card.flush()
        t = time.time() + 0.04
        while time.time() < t:
            select.select([s], [], [], max(0, t - time.time()))
            card.control()
            card.flush()
    # a moment for the last repairs
    t = time.time() + args.linger
    while time.time() < t:
        select.select([s], [], [], 0.05)
        card.control()
        card.flush()
        card.rec(pv.SURFACE, card.surface(), draw=False)
        card.flush()
    if args.final:
        pv.write_raw(args.final, card.screen)
    print("fake_sender: %d frames, %d datagrams sent, %d lost on purpose, to %s:%d"
          % (frame, card.sent, card.lost, args.dest, args.port))


if __name__ == "__main__":
    main()
