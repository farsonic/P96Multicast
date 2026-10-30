#!/usr/bin/env python3
"""Unit tests for client/panoview.py: records and pixel formats.

    python3 tests/test_client.py
"""
import os
import struct
import sys
import tempfile
import time
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


class SpriteTests(unittest.TestCase):
    """graphics.library's sprites (SPRITES=1): SPRITEIMAGE bands under an
    id, SPRITE pointing a sprite at one."""
    COLS = bytes((200, 0, 0, 255, 255, 255, 0, 0, 255))

    def band(self, s, ident, w, h, first, rows, data):
        s.apply(pv.SPRITEIMAGE, struct.pack(">HBBHHH", ident, w, 0, h, first, rows) + self.COLS + data)

    def sprite(self, s, n, vis, x, y, ident):
        s.apply(pv.SPRITE, struct.pack(">BBhhH", n, vis, x, y, ident))

    def test_bands_make_the_image(self):
        s = pv.Screen()
        # 32 wide, 3 rows: row 0 colour 1 in the first pixel, row 1 colour 2
        # in the last, row 2 colour 3 everywhere; each row plane 0 then plane 1
        rows = [struct.pack(">II", 0x80000000, 0),
                struct.pack(">II", 0, 0x00000001),
                struct.pack(">II", 0xffffffff, 0xffffffff)]
        self.sprite(s, 2, 1, 10, 20, 5)
        self.band(s, 5, 32, 3, 0, 2, rows[0] + rows[1])
        self.assertEqual(s.shown_sprites(), [])            # not all of it yet
        serial = s.sprite_serial
        self.band(s, 5, 32, 3, 2, 1, rows[2])
        self.assertEqual(s.sprite_serial, serial + 1)
        [(n, x, y, w, h, rgba)] = s.shown_sprites()
        self.assertEqual((n, x, y, w, h), (2, 10, 20, 32, 3))
        self.assertEqual(rgba[0:4], bytes((200, 0, 0, 255)))
        self.assertEqual(rgba[4:8], bytes(4))                       # clear
        self.assertEqual(rgba[(32 + 31) * 4:(32 + 32) * 4], bytes((255, 255, 255, 255)))
        self.assertEqual(rgba[64 * 4:65 * 4], bytes((0, 0, 255, 255)))

    def test_images_kept_and_shared(self):
        # two sprites showing one image, then one of them another: no resending
        s = pv.Screen()
        self.band(s, 0, 16, 1, 0, 1, struct.pack(">HH", 0x8000, 0))
        self.band(s, 1, 16, 1, 0, 1, struct.pack(">HH", 0, 0x8000))
        self.sprite(s, 3, 1, 0, 0, 0)
        self.sprite(s, 4, 1, 16, 0, 0)
        self.assertEqual([t[0] for t in s.shown_sprites()], [4, 3])   # 3 in front
        self.sprite(s, 3, 1, 0, 0, 1)
        self.assertEqual(s.shown_sprites()[1][5][0:4], bytes((255, 255, 255, 255)))
        self.sprite(s, 4, 0, 16, 0, 0)                                 # hidden
        self.assertEqual([t[0] for t in s.shown_sprites()], [3])

    def test_lost_image_asked_for(self):
        # a shown sprite whose image never came: the viewer asks for the
        # sprites again, once LACKING has passed, then at most once a second
        sent = []
        v = pv.Viewer(pv.Screen(), send=lambda data, addr: sent.append(data))
        v.amiga, v.stream, v.expect = ("10.0.0.2", 9696), 7, 0
        self.sprite(v.screen, 2, 1, 0, 0, 3)
        types = lambda: [d[3] for d in sent]
        v.tick(now=100.0)
        self.assertNotIn(pv.WANTSPRITES, types())
        v.tick(now=100.6)
        self.assertEqual(types().count(pv.WANTSPRITES), 1)
        v.tick(now=101.0)
        self.assertEqual(types().count(pv.WANTSPRITES), 1)
        self.band(v.screen, 3, 16, 1, 0, 1, bytes(4))      # it came
        v.tick(now=103.0)
        self.assertEqual(types().count(pv.WANTSPRITES), 1)

    def test_attached_pair(self):
        # depth 4: an attached pair's 15 colours, planes 0-3 in each row
        s = pv.Screen()
        cols = bytes(range(45))
        # pixel 0: colour 1 + 4 = 5 (planes 0 and 2); pixel 1: 15 (all four)
        row = struct.pack(">HHHH", 0xc000, 0x4000, 0xc000, 0x4000)
        s.apply(pv.SPRITEIMAGE, struct.pack(">HBBHHH", 4, 16, 4, 1, 0, 1) + cols + row)
        self.sprite(s, 2, 1, 0, 0, 4)
        [(_n, _x, _y, w, h, rgba)] = s.shown_sprites()
        self.assertEqual((w, h), (16, 1))
        self.assertEqual(rgba[0:4], cols[12:15] + b"\xff")     # colour 5
        self.assertEqual(rgba[4:8], cols[42:45] + b"\xff")     # colour 15
        self.assertEqual(rgba[8:12], bytes(4))

    def test_bad_bands(self):
        s = pv.Screen()
        self.sprite(s, 3, 1, -5, 40, 7)
        # rows past the height, and data too short: ignored
        self.band(s, 7, 16, 2, 1, 2, bytes(8))
        self.band(s, 7, 16, 2, 0, 2, bytes(4))
        self.assertEqual(s.shown_sprites(), [])

    def test_drawn_over_the_screen(self):
        # the web view's picture: the screen with sprite 2 over it, clipped
        s = pv.Screen()
        mode(s, 4, 2, pv.R8G8B8, 3)
        tile(s, 0, 0, 4, 2, b"\x10" * 24)
        self.band(s, 9, 16, 1, 0, 1, struct.pack(">HH", 0xc000, 0x4000))
        self.sprite(s, 2, 1, 2, 1, 9)
        head = type("Head", (), {"screen": s, "col": 0, "row": 0})()
        w, h, rgb = pv.composite([head], sprites=True)
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(rgb[:12], b"\x10" * 12)                      # row 0: the screen
        self.assertEqual(rgb[12:18], b"\x10" * 6)                     # row 1, x 0-1
        self.assertEqual(rgb[18:24], bytes((200, 0, 0, 0, 0, 255)))   # x 2-3: the sprite
        self.assertEqual(pv.composite([head])[2], b"\x10" * 24)      # without: the pixels

    def test_web_page_sprites(self):
        # the web page places the sprites itself: where, and which picture,
        # a picture's number never reused for another
        s = pv.Screen()
        mode(s, 4, 2, pv.R8G8B8, 3)
        self.band(s, 9, 16, 1, 0, 1, struct.pack(">HH", 0xc000, 0x4000))
        self.band(s, 8, 16, 1, 0, 1, struct.pack(">HH", 0x8000, 0))
        self.sprite(s, 2, 1, 2, 1, 9)
        self.sprite(s, 5, 1, 0, 0, 8)
        heads = [type("Head", (), {"screen": s, "col": 0, "row": 0})()]
        pics = pv.SpritePictures()
        a, b = pv.web_sprites(heads, pics)
        self.assertEqual((a["id"], a["x"], a["y"], a["w"], a["h"]), ("0-5", 0, 0, 16, 1))
        self.assertEqual(b["id"], "0-2")                 # in front: last
        self.assertNotEqual(a["key"], b["key"])
        self.assertEqual(pv.web_sprites(heads, pics)[1]["key"], b["key"])
        self.assertTrue(pics.png(b["key"]).startswith(b"\x89PNG"))
        self.assertIsNone(pics.png(999))

    def test_pointer_still_the_same(self):
        s = pv.Screen()
        s.apply(pv.POINTER, struct.pack(">BBbb", 16, 1, 0, 0) + self.COLS +
                struct.pack(">HH", 0xc000, 0x4000))
        w, h, _hx, _hy, rgba = s.pointer
        self.assertEqual(rgba[0:8], bytes((200, 0, 0, 255, 0, 0, 255, 255)))


class InStepTests(unittest.TestCase):
    """SPLIT=: the parts shown together, and never behind."""

    def datagram(self, seq, frame):
        return struct.pack(">HBBII", pv.MAGIC, pv.VERSION, 0, 1, seq) + \
            bytes((pv.FRAME,)) + struct.pack(">HI", 4, frame)

    def test_behind_catches_up(self):
        import socket
        heads, out = [], socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for i in range(2):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("127.0.0.1", 0))
            sock.setblocking(False)
            heads.append(pv.Head(sock, pv.Viewer(pv.Screen()), i, 0))
        step = pv.InStep(heads)
        # five whole updates came while one was being shown
        for f in range(5):
            for h in heads:
                out.sendto(self.datagram(f, f), h.sock.getsockname())
        time.sleep(0.1)
        self.assertTrue(step.receive())
        # shown: the newest, 4, not 0
        self.assertEqual([h.screen.frame for h in heads], [4, 4])
        out.close()
        for h in heads:
            h.sock.close()


class PacerTests(unittest.TestCase):
    """Updates shown as evenly as the Amiga made them (FRAME's time)."""

    def test_bursts_shown_evenly(self):
        # every 20 ms on the Amiga; here they come in bursts of three, 10 ms late
        p = pv.Pacer()
        dues = []
        for k in range(300):
            t_ms = 1000 + 20 * k
            came = 50.0 + (20 * (k - k % 3) + 10 * (k % 3 == 2)) / 1000.0
            dues.append(p.due(t_ms, came))
        steps = [b - a for a, b in zip(dues[150:], dues[151:])]
        # within half a millisecond, where arrivals were up to 40 ms apart
        self.assertTrue(all(abs(x - 0.020) < 0.0005 for x in steps), steps[:6])
        # and never before it came: the margin covers the bursts
        for k in range(150, 300):
            came = 50.0 + (20 * (k - k % 3) + 10 * (k % 3 == 2)) / 1000.0
            self.assertGreaterEqual(dues[k] + 0.001, came)

    def test_drifting_clocks(self):
        # the Amiga's clock 1% slow against ours, little jitter: the margin
        # stays small, as the drift is followed, not taken for jitter
        p = pv.Pacer()
        for k in range(600):
            t_ms = 20 * k
            came = 7.0 + 1.01 * t_ms / 1000.0 + (0.002 if k % 7 == 0 else 0)
            due = p.due(t_ms, came)
        self.assertLess(due - came, 0.01)
        self.assertGreaterEqual(due - came, 0)

    def test_clock_starting_again(self):
        p = pv.Pacer()
        p.due(5000, 10.0)
        self.assertAlmostEqual(p.due(20, 10.02), 10.02 + p.MIN_MARGIN, places=6)

    def test_report(self):
        p = pv.Pacer()
        p.shown(0, 1.000, 1.050)
        p.shown(20, 1.030, 1.070)        # came 10 ms off, shown on time
        came, shown = p.report()
        self.assertAlmostEqual(came, 10.0, places=6)
        self.assertAlmostEqual(shown, 0.0, places=6)


class PacedOldCardTests(unittest.TestCase):
    def test_no_times_shows_newest(self):
        import socket
        out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        sock.setblocking(False)
        head = pv.Head(sock, pv.Viewer(pv.Screen()))
        step = pv.InStep([head], pacing=True)
        for f in range(4):
            # an older card's FRAME: the number only
            out.sendto(struct.pack(">HBBII", pv.MAGIC, pv.VERSION, 0, 1, f) +
                       bytes((pv.FRAME,)) + struct.pack(">HI", 4, f), sock.getsockname())
        time.sleep(0.1)
        self.assertTrue(step.receive())
        self.assertEqual(head.screen.frame, 3)
        out.close()
        sock.close()


class DecodeTests(unittest.TestCase):
    """Pictures decoded for the Amiga (panoramapic.datatype)."""

    def test_decode_and_scale(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("no Pillow")
        import io
        im = Image.new("RGB", (300, 200), (10, 200, 30))
        png = io.BytesIO()
        im.save(png, "PNG")
        w, h, rgb = pv.decode_picture(png.getvalue())
        self.assertEqual((w, h, rgb[:3]), (300, 200, bytes((10, 200, 30))))
        w, h, rgb = pv.decode_picture(png.getvalue(), 100, 100)
        self.assertEqual((w, h), (100, 67))                 # scaled to fit, shape kept
        self.assertEqual(len(rgb), 100 * 67 * 3)
        self.assertIsNone(pv.decode_picture(b"not a picture at all"))


if __name__ == "__main__":
    unittest.main()
