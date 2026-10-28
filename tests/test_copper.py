#!/usr/bin/env python3
"""The viewer (client/panoview.py) against the card's reference renderer.

    python3 tests/test_copper.py

Builds tests/rendertest.c with src/render.c natively (cc), has it draw
random Copper records, and checks that the viewer draws the same, in every
pixel size, with and without numpy. Also checks the viewer's handling of
datagrams: order, repairs, NAKs, WANTs.
"""
import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(TOP, "client"))
import panoview as pv  # noqa: E402

FMT = {1: pv.CLUT, 2: pv.R5G6B5, 3: pv.R8G8B8, 4: pv.A8R8G8B8}
STREAM = 0x1234


def build():
    exe = os.path.join(TOP, "build", "rendertest")
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    subprocess.check_call(["cc", "-O2", "-Wall", "-I" + os.path.join(TOP, "include"),
                           "-I" + os.path.join(TOP, "src"), "-o", exe,
                           os.path.join(HERE, "rendertest.c"), os.path.join(TOP, "src", "render.c")])
    return exe


def surface(w, h, bpp, tw=16, th=16):
    return struct.pack(">HHBBBBHH", w, h, FMT[bpp], bpp, 1, 0, tw, th)


def dgram(seq, records, flags=0, stream=STREAM):
    body = b"".join(struct.pack(">BH", t, len(b)) + b for t, b in records)
    return struct.pack(">HBBII", pv.MAGIC, pv.VERSION, flags, stream, seq) + body


class RenderTests(unittest.TestCase):
    exe = None

    @classmethod
    def setUpClass(cls):
        cls.exe = build()

    def run_case(self, bpp, seed):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "out")
            subprocess.check_call([self.exe, str(bpp), str(seed), path])
            data = open(path, "rb").read()
        w, h, b = struct.unpack(">HHB", data[:5])
        screen = pv.Screen()
        screen.apply(pv.SURFACE, surface(w, h, b))
        pos, count = 5, 0
        while True:
            t, n = struct.unpack(">BH", data[pos:pos + 3])
            pos += 3
            if t == 0:
                break
            self.assertTrue(screen.apply(t, memoryview(data)[pos:pos + n]), "record %d" % count)
            pos += n
            count += 1
        self.assertEqual(screen.stats["bad"], 0)
        want = data[pos:]
        got = bytes(screen.pixels)
        if got != want:
            bad = next(i for i in range(len(want)) if got[i] != want[i]) // bpp
            self.fail("bpp %d seed %d: first difference at %d,%d" % (bpp, seed, bad % w, bad // w))
        return count

    def check_all(self):
        for bpp in (1, 2, 3, 4):
            for seed in (1, 2, 3):
                with self.subTest(bpp=bpp, seed=seed):
                    self.assertGreater(self.run_case(bpp, seed), 300)

    @unittest.skipIf(pv.np is None, "numpy not installed")
    def test_numpy(self):
        self.check_all()

    def test_python(self):
        saved, pv.np = pv.np, None
        try:
            self.check_all()
        finally:
            pv.np = saved


class ViewerTests(unittest.TestCase):
    def setUp(self):
        self.sent = []
        self.screen = pv.Screen()
        self.v = pv.Viewer(self.screen, lambda data, addr: self.sent.append((data, addr)))

    def fill(self, x, y, w, h, v):
        return (pv.FILL, struct.pack(">HHHHB", x, y, w, h, v))

    def messages(self, mtype):
        return [d for d, _ in self.sent if d[3] == mtype]

    def test_in_order_and_held(self):
        v = self.v
        v.feed(dgram(10, [(pv.SURFACE, surface(32, 16, 1))]), ("amiga", 1), now=0)
        v.feed(dgram(12, [self.fill(0, 0, 32, 16, 2)]), None, now=0)
        self.assertEqual(self.screen.pixels[0], 0)          # held: 11 is missing
        v.tick(now=0.05)
        naks = self.messages(pv.NAK)
        self.assertEqual(len(naks), 1)
        self.assertEqual(struct.unpack(">IH", naks[0][8:14]), (11, 1))
        v.feed(dgram(11, [self.fill(0, 0, 16, 16, 1)], flags=pv.F_REPAIR), None, now=0.06)
        self.assertEqual((self.screen.pixels[0], self.screen.pixels[20]), (2, 2))
        self.assertTrue(self.screen.complete())
        self.assertEqual(v.stats["repairs"], 1)
        v.feed(dgram(11, [self.fill(0, 0, 16, 16, 9)]), None, now=0.07)   # duplicate
        self.assertEqual(self.screen.pixels[0], 2)

    def test_give_up_and_want(self):
        v = self.v
        v.feed(dgram(1, [(pv.SURFACE, surface(32, 16, 1))]), ("amiga", 1), now=0)
        v.feed(dgram(2, [self.fill(0, 0, 32, 16, 2)]), None, now=0)
        self.assertTrue(self.screen.complete())
        v.feed(dgram(4, [self.fill(16, 0, 16, 16, 5)]), None, now=0)
        v.tick(now=1.5)                                      # 3 never came
        self.assertEqual(v.stats["gaps"], 1)
        self.assertEqual(self.screen.pixels[16], 5)          # 4 applied after all
        self.assertFalse(self.screen.complete())
        wants = self.messages(pv.WANT)
        self.assertEqual(len(wants), 1)
        self.assertEqual(struct.unpack(">HHHH", wants[0][8:16]), (0, 0, 16, 16))

    def test_copy_carries_validity(self):
        v = self.v
        v.feed(dgram(1, [(pv.SURFACE, surface(32, 16, 1)),
                         self.fill(0, 0, 16, 16, 7),
                         (pv.COPY, struct.pack(">HHHHHH", 0, 0, 16, 0, 16, 16))]),
               ("amiga", 1), now=0)
        self.assertTrue(self.screen.complete())
        self.assertEqual(self.screen.pixels[31], 7)
        self.screen.invalidate()
        self.screen.valid[1] = 1
        v.feed(dgram(2, [(pv.COPY, struct.pack(">HHHHHH", 0, 0, 16, 0, 16, 16))]), None, now=0)
        self.assertEqual(list(self.screen.valid), [0, 0])
        self.assertEqual(self.screen.wanted(), [(0, 0, 32, 16)])

    def test_new_stream_starts_over(self):
        v = self.v
        v.feed(dgram(1, [(pv.SURFACE, surface(16, 16, 1)), self.fill(0, 0, 16, 16, 1)]),
               ("amiga", 1), now=0)
        self.assertTrue(self.screen.complete())
        v.feed(dgram(500, [(pv.SURFACE, surface(16, 16, 1))], stream=STREAM + 1), None, now=0)
        self.assertFalse(self.screen.complete())
        self.assertEqual(v.expect, 501)

    def test_pointer(self):
        img = struct.pack(">BBbb", 2, 2, 0, 0) + bytes((255, 0, 0, 0, 255, 0, 0, 0, 255))
        # rows: plane 0 then plane 1: colours 1, 2 / 3, 0
        img += struct.pack(">HHHH", 0x8000, 0x4000, 0x8000, 0x8000)
        self.screen.apply(pv.POINTER, img)
        w, h, _hx, _hy, rgba = self.screen.pointer
        self.assertEqual((w, h), (2, 2))
        self.assertEqual(rgba, bytes((255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0)))

    def test_rgb_rect(self):
        s = self.screen
        s.apply(pv.SURFACE, surface(4, 2, 3))
        s.apply(pv.PIXELS, struct.pack(">HHHHBB", 0, 0, 4, 2, pv.RAW, 0) + bytes(range(24)))
        self.assertEqual(s.rgb_rect(1, 1, 2, 1), bytes(range(15, 21)))
        self.assertEqual(s.take_dirty(), (0, 0, 4, 2))


class LossTest(unittest.TestCase):
    """fake_sender.py loses a tenth of its datagrams; the viewer still ends up exact."""

    def test_loss(self):
        with tempfile.TemporaryDirectory() as d:
            port = str(20000 + os.getpid() % 20000)
            view = subprocess.Popen([sys.executable, os.path.join(TOP, "client", "panoview.py"),
                                     "--group", "127.0.0.1", "--port", port,
                                     "--snapshot", os.path.join(d, "v.png"),
                                     "--raw", os.path.join(d, "v.raw"), "--duration", "7"],
                                    stderr=subprocess.PIPE)
            import time
            time.sleep(0.5)
            subprocess.check_call([sys.executable, os.path.join(HERE, "fake_sender.py"),
                                   "--dest", "127.0.0.1", "--port", port, "--depth", "16",
                                   "--loss", "0.1", "--seconds", "4", "--linger", "3",
                                   "--final", os.path.join(d, "f.raw")], stdout=subprocess.DEVNULL)
            err = view.communicate()[1].decode()
            self.assertIn("600 of 600 tiles", err)
            with open(os.path.join(d, "v.raw"), "rb") as a, open(os.path.join(d, "f.raw"), "rb") as b:
                self.assertTrue(a.read() == b.read(), "the viewer's screen differs: " + err)


if __name__ == "__main__":
    unittest.main()
