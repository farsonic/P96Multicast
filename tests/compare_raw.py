#!/usr/bin/env python3
"""Compare a screen dump (mctest DUMP=) with a viewer's (panoview.py --raw).

    compare_raw.py card.raw viewer.raw

Both: a 12 byte header (u32 width, u32 height, u16 format, u16 bytes per
pixel), then the pixels as the card has them. The format is not compared
(mctest does not know P96's number for it). Prints where they differ.
"""
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12:
        raise ValueError("%s: too short" % path)
    w, h, _fmt, bpp = struct.unpack(">IIHH", data[:12])
    return w, h, bpp, data[12:12 + w * h * bpp]


def main():
    try:
        a, b = load(sys.argv[1]), load(sys.argv[2])
    except (OSError, ValueError) as e:
        print("compare_raw: %s" % e)
        return 1
    if a[:3] != b[:3]:
        print("compare_raw: sizes differ: %dx%d %d bytes vs %dx%d %d bytes" % (a[:3] + b[:3]))
        return 1
    w, h, bpp, pa = a
    pb = b[3]
    if pa == pb:
        return 0
    bad = [i // bpp for i in range(min(len(pa), len(pb))) if pa[i] != pb[i]]
    pts = sorted(set((p % w, p // w) for p in bad))
    xs = [x for x, _ in pts]
    ys = [y for _, y in pts]
    print("compare_raw: %d pixels differ, within x %d-%d, y %d-%d; first at %d,%d"
          % (len(pts), min(xs), max(xs), min(ys), max(ys), pts[0][0], pts[0][1]))
    return 1


if __name__ == "__main__":
    sys.exit(main())
