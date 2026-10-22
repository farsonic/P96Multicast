#!/usr/bin/env python3
"""Pretend to be Multicast.card: send mctest's colour bars, for testing viewers.

    fake_sender.py                         to 239.255.96.1:9696, 8 bit, for 30 s
    fake_sender.py --dest 127.0.0.1 --depth 32 --seconds 0   forever, to localhost

Sends MODE, PALETTE (8 bit) and the whole screen as tiles once a second,
like the card's background refresh, with a moving square so a viewer shows
something changing.
"""
import argparse
import random
import socket
import struct
import time

MAGIC = 0x5039364D
BARS = [0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000]
CLUT, R5G6B5PC, B8G8R8A8 = 1, 4, 9


def pixel(fmt, rgb):
    r, g, b = rgb >> 16, (rgb >> 8) & 255, rgb & 255
    if fmt == B8G8R8A8:
        return bytes((b, g, r, 0))
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return struct.pack("<H", v)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dest", default="239.255.96.1")
    ap.add_argument("--port", type=int, default=9696)
    ap.add_argument("--depth", type=int, choices=(8, 16, 32), default=8)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--height", type=int, default=480)
    ap.add_argument("--seconds", type=float, default=30, help="0 = until Ctrl-C")
    args = ap.parse_args()

    fmt, bpp = {8: (CLUT, 1), 16: (R5G6B5PC, 2), 32: (B8G8R8A8, 4)}[args.depth]
    w, h = args.width, args.height
    tw, th = 64 // bpp, 16
    stream = random.getrandbits(32)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    to = (args.dest, args.port)

    def send(ptype, body, frame):
        s.sendto(struct.pack(">IBBHII", MAGIC, 1, ptype, 0, stream, frame) + body, to)

    def colour(x, y, t):
        sq = (t * 8) % (w - 64)
        if sq <= x < sq + 64 and h // 2 + 32 <= y < h // 2 + 96:
            return 7 if fmt == CLUT else 0xffffff
        if y < h // 2:
            i = x * 8 // w
            return i if fmt == CLUT else BARS[i]
        return 8 + (x * 16 // w) * 15 if fmt == CLUT else ((x * 255 // w) << 8) | 0x40

    pal = b"".join(struct.pack("BBB", c >> 16, (c >> 8) & 255, c & 255) for c in BARS)
    pal += b"".join(bytes((i, i, i)) for i in range(8, 256))
    end = time.time() + args.seconds if args.seconds else None
    frame = 0
    while end is None or time.time() < end:
        send(1, struct.pack(">HHBBBBHHHH", w, h, fmt, bpp, 1, 0, tw, th, 25, 0), frame)
        if fmt == CLUT:
            send(2, struct.pack(">HH", 0, 256) + pal, frame)
        tiles = 0
        for ty in range(0, h, th):
            for tx in range(0, w, tw):
                cw, ch = min(tw, w - tx), min(th, h - ty)
                rows = []
                for y in range(ty, ty + ch):
                    if fmt == CLUT:
                        rows.append(bytes(colour(x, y, frame) for x in range(tx, tx + cw)))
                    else:
                        rows.append(b"".join(pixel(fmt, colour(x, y, frame))
                                             for x in range(tx, tx + cw)))
                send(3, struct.pack(">HHHHBB", tx, ty, cw, ch, 0, 0) + b"".join(rows), frame)
                tiles += 1
        send(4, struct.pack(">HH", tiles, 0), frame)
        frame += 1
        time.sleep(1)
    print("fake_sender: sent %d frames to %s:%d" % (frame, args.dest, args.port))


if __name__ == "__main__":
    main()
