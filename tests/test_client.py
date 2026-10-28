#!/usr/bin/env python3
"""Unit tests for client/panoview.py: records and pixel formats.

    python3 tests/test_client.py
"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "client"))
import panoview as pv  # noqa: E402

def mode(s, w, h, fmt, bpp, display=1, tw=16, th=16):
    s.apply(pv.SURFACE, struct.pack(">HHBBBBHH", w, h, fmt, bpp, display, 0, tw, th))


def tile(s, x, y, w, h, data, enc=pv.RAW):
    return s.apply(pv.PIXELS, struct.pack(">HHHHBB", x, y, w, h, enc, 0) + data)


def palette(s, first, rgb):
    s.apply(pv.PALETTE, struct.pack(">HH", first, len(rgb) // 3) + rgb)


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
                mode(s, len(COLOURS), 1, fmt, bpp)
                data = b"".join(encode(fmt, *c) for c in COLOURS)
                self.assertTrue(tile(s, 0, 0, len(COLOURS), 1, data))
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
        mode(s, 4, 2, pv.CLUT, 1)
        palette(s, 0, bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255)))
        tile(s, 0, 0, 4, 2, bytes((0, 1, 2, 3, 3, 2, 1, 0)))
        want = bytes((0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255,
                      0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0))
        self.assertEqual(s.rgb(), want)
        # palette changes apply to pixels already received
        palette(s, 1, bytes((9, 9, 9)))
        self.assertEqual(s.rgb()[3:6], bytes((9, 9, 9)))


class GroupTests(unittest.TestCase):
    def test_parts_count_up_or_have_their_own(self):
        self.assertEqual(pv.head_group("239.255.96.1", None, 1), "239.255.96.2")
        self.assertEqual(pv.head_group("10.0.2.2", None, 1), "10.0.2.2")
        own = ["239.1.1.1", "", "239.3.3.3"]
        self.assertEqual(pv.head_group("239.255.96.1", own, 0), "239.1.1.1")
        self.assertEqual(pv.head_group("239.255.96.1", own, 1), "239.255.96.2")
        self.assertEqual(pv.head_group("239.255.96.1", own, 2), "239.3.3.3")
        self.assertEqual(pv.head_group("239.255.96.1", own, 3), "239.255.96.4")


class RecordTests(unittest.TestCase):
    def test_solid_and_tiles(self):
        s = pv.Screen()
        mode(s, 20, 20, pv.CLUT, 1, tw=16, th=16)
        palette(s, 0, bytes((0, 0, 0, 10, 20, 30)))
        tile(s, 0, 0, 16, 16, b"\x01", enc=pv.SOLID)
        self.assertFalse(s.complete())
        tile(s, 16, 0, 4, 16, b"\x01" * 64)
        tile(s, 0, 16, 16, 4, b"\x01" * 64)
        tile(s, 16, 16, 4, 4, bytes((4, 1)) * 4, enc=pv.RLE)
        self.assertTrue(s.complete())
        self.assertEqual(s.rgb(), bytes((10, 20, 30)) * 400)

    def test_ignored(self):
        s = pv.Screen()
        # pixels before the first SURFACE are dropped
        self.assertFalse(tile(s, 0, 0, 1, 1, b"\0"))
        mode(s, 8, 8, pv.CLUT, 1)
        # out of bounds, short data, a bad RLE
        self.assertFalse(tile(s, 4, 4, 8, 8, b"\0" * 64))
        self.assertFalse(tile(s, 0, 0, 8, 8, b"\0" * 63))
        self.assertFalse(tile(s, 0, 0, 2, 1, bytes((3, 0)), enc=pv.RLE))
        self.assertEqual(s.stats["bad"], 3)

    def test_display_off_keeps_the_picture(self):
        # another screen in front on the Amiga: the last picture stays, not black
        s = pv.Screen()
        mode(s, 2, 1, pv.R8G8B8, 3)
        tile(s, 0, 0, 2, 1, b"\xff" * 6)
        s.apply(pv.SURFACE, struct.pack(">HHBBBBHH", 2, 1, pv.R8G8B8, 3, 1, pv.SF_BEHIND, 16, 16))
        self.assertTrue(s.behind)
        self.assertEqual(s.rgb(), b"\xff" * 6)
        mode(s, 2, 1, pv.R8G8B8, 3, display=0)
        self.assertEqual(s.rgb(), b"\xff" * 6)

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
