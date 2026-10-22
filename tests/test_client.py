#!/usr/bin/env python3
"""Unit tests for client/p96view.py: packet handling and pixel formats.

    python3 tests/test_client.py
"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "client"))
import p96view as pv  # noqa: E402

STREAM = 0x12345678


def hdr(ptype, flags=0, frame=0):
    return struct.pack(">IBBHII", pv.MAGIC, 1, ptype, flags, STREAM, frame)


def mode(w, h, fmt, bpp, display=1, tw=16, th=16):
    return hdr(pv.MODE) + struct.pack(">HHBBBBHHHH", w, h, fmt, bpp, display, 0, tw, th, 25, 0)


def tile(x, y, w, h, data, enc=pv.ENC_RAW, flags=0):
    return hdr(pv.TILE, flags) + struct.pack(">HHHHBB", x, y, w, h, enc, 0) + data


def palette(first, rgb):
    return hdr(pv.PALETTE) + struct.pack(">HH", first, len(rgb) // 3) + rgb


def encode(fmt, r, g, b):
    """One pixel in the Amiga's memory layout."""
    if fmt == pv.R8G8B8:
        return bytes((r, g, b))
    if fmt == pv.B8G8R8:
        return bytes((b, g, r))
    if fmt == pv.A8R8G8B8:
        return bytes((0, r, g, b))
    if fmt == pv.A8B8G8R8:
        return bytes((0, b, g, r))
    if fmt == pv.R8G8B8A8:
        return bytes((r, g, b, 0))
    if fmt == pv.B8G8R8A8:
        return bytes((b, g, r, 0))
    if fmt in (pv.R5G6B5, pv.R5G6B5PC):
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    elif fmt in (pv.R5G5B5, pv.R5G5B5PC):
        v = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    elif fmt == pv.B5G6R5PC:
        v = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3)
    elif fmt == pv.B5G5R5PC:
        v = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
    else:
        raise ValueError(fmt)
    if fmt in (pv.R5G6B5PC, pv.R5G5B5PC, pv.B5G6R5PC, pv.B5G5R5PC):
        return struct.pack("<H", v)
    return struct.pack(">H", v)


COLOURS = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255), (0, 0, 0), (255, 255, 0)]
BPP = {pv.R8G8B8: 3, pv.B8G8R8: 3, pv.A8R8G8B8: 4, pv.A8B8G8R8: 4, pv.R8G8B8A8: 4,
       pv.B8G8R8A8: 4, pv.R5G6B5: 2, pv.R5G6B5PC: 2, pv.R5G5B5: 2, pv.R5G5B5PC: 2,
       pv.B5G6R5PC: 2, pv.B5G5R5PC: 2}


class FormatTests(unittest.TestCase):
    def check_formats(self):
        for fmt, bpp in BPP.items():
            with self.subTest(fmt=pv.FORMAT_NAMES[fmt]):
                s = pv.Screen()
                s.handle(mode(len(COLOURS), 1, fmt, bpp))
                data = b"".join(encode(fmt, *c) for c in COLOURS)
                self.assertEqual(s.handle(tile(0, 0, len(COLOURS), 1, data)), pv.TILE)
                self.assertTrue(s.complete())
                self.assertEqual(s.rgb(), b"".join(bytes(c) for c in COLOURS))

    def test_formats_python(self):
        saved, pv.np = pv.np, None
        try:
            self.check_formats()
        finally:
            pv.np = saved

    @unittest.skipIf(pv.np is None, "numpy not installed")
    def test_formats_numpy(self):
        self.check_formats()

    def test_clut(self):
        s = pv.Screen()
        s.handle(mode(4, 2, pv.CLUT, 1))
        s.handle(palette(0, bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255))))
        s.handle(tile(0, 0, 4, 2, bytes((0, 1, 2, 3, 3, 2, 1, 0))))
        want = bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                      0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0))
        self.assertEqual(s.rgb(), want)
        # palette changes apply to pixels already received
        s.handle(palette(1, bytes((9, 9, 9))))
        self.assertEqual(s.rgb()[3:6], bytes((9, 9, 9)))


class PacketTests(unittest.TestCase):
    def test_solid_and_tiles(self):
        s = pv.Screen()
        s.handle(mode(20, 20, pv.CLUT, 1, tw=16, th=16))
        s.handle(palette(0, bytes((0, 0, 0, 10, 20, 30))))
        s.handle(tile(0, 0, 16, 16, b"\x01", enc=pv.ENC_SOLID))
        self.assertFalse(s.complete())
        s.handle(tile(16, 0, 4, 16, b"\x01" * 64))
        s.handle(tile(0, 16, 16, 4, b"\x01" * 64, flags=pv.FLAG_REFRESH))
        s.handle(tile(16, 16, 4, 4, b"\x01", enc=pv.ENC_SOLID))
        self.assertTrue(s.complete())
        self.assertEqual(s.rgb(), bytes((10, 20, 30)) * 400)
        self.assertEqual(s.stats["solid"], 2)
        self.assertEqual(s.stats["refresh"], 1)

    def test_ignored(self):
        s = pv.Screen()
        self.assertIsNone(s.handle(b"junk"))
        self.assertIsNone(s.handle(b"\0" * 40))
        # tiles before the first MODE of a stream are dropped
        self.assertIsNone(s.handle(tile(0, 0, 1, 1, b"\0")))
        s.handle(mode(8, 8, pv.CLUT, 1))
        # out of bounds
        s.handle(tile(4, 4, 8, 8, b"\0" * 64))
        self.assertEqual(s.stats["bad"], 3)

    def test_new_stream_resets(self):
        s = pv.Screen()
        s.handle(mode(8, 8, pv.CLUT, 1))
        s.handle(tile(0, 0, 8, 8, b"\x05" * 64))
        other = bytearray(mode(8, 8, pv.CLUT, 1))
        other[8:12] = struct.pack(">I", STREAM + 1)
        s.handle(bytes(other))
        self.assertEqual(s.stream, STREAM + 1)
        self.assertFalse(s.complete())

    def test_display_off_is_black(self):
        s = pv.Screen()
        s.handle(mode(2, 1, pv.R8G8B8, 3, display=0))
        s.handle(tile(0, 0, 2, 1, b"\xff" * 6))
        self.assertEqual(s.rgb(), b"\0" * 6)

    def test_png_roundtrip(self):
        sys.path.insert(0, os.path.dirname(__file__))
        import check_frame
        rgb = bytes((i * 7) & 255 for i in range(32 * 48 * 3))
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "x.png")
            pv.write_png(path, 32, 48, rgb)
            w, h, got = check_frame.read_png(path)
        self.assertEqual((w, h, got), (32, 48, rgb))


if __name__ == "__main__":
    unittest.main()
