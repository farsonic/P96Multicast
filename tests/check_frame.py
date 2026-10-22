#!/usr/bin/env python3
"""Check a frame captured by p96view.py against mctest's test pattern.

    check_frame.py frame.png [WIDTH HEIGHT]

mctest draws eight colour bars across the top half of the screen; this
samples the middle of each bar.
"""
import struct
import sys
import zlib

BARS = [0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0x000000]
TOLERANCE = 8


def read_png(path):
    """Read the 8 bit RGB, unfiltered PNGs that p96view.py writes."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            if depth != 8 or ctype != 2:
                raise ValueError("not 8 bit RGB")
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    row = w * 3
    rgb = bytearray()
    for y in range(h):
        line = raw[y * (row + 1):(y + 1) * (row + 1)]
        if line[0] != 0:
            raise ValueError("filtered PNG rows are not supported")
        rgb += line[1:]
    return w, h, bytes(rgb)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    try:
        w, h, rgb = read_png(argv[1])
    except (OSError, ValueError, zlib.error) as e:
        print("check_frame: %s: %s" % (argv[1], e))
        return 1
    want_w, want_h = (int(argv[2]), int(argv[3])) if len(argv) > 3 else (640, 480)
    ok = True
    if (w, h) != (want_w, want_h):
        print("check_frame: size %dx%d, want %dx%d" % (w, h, want_w, want_h))
        return 1
    y = h // 4
    for i, want in enumerate(BARS):
        x = (2 * i + 1) * w // 16
        o = (y * w + x) * 3
        got = rgb[o:o + 3]
        exp = ((want >> 16) & 255, (want >> 8) & 255, want & 255)
        if any(abs(a - b) > TOLERANCE for a, b in zip(got, exp)):
            print("check_frame: bar %d at %d,%d is %02x%02x%02x, want %06x"
                  % (i, x, y, got[0], got[1], got[2], want))
            ok = False
    print("check_frame: %s %dx%d %s" % (argv[1], w, h, "ok" if ok else "MISMATCH"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
