#!/usr/bin/env python3
"""P96Multicast viewer: shows the screen an Amiga's Multicast.card sends.

    p96view.py                              join 239.255.96.1:9696, show a window
    p96view.py --group 127.0.0.1            UAE's NAT (slirp): the card sends to 10.0.2.2
    p96view.py --group 239.255.96.2 --port 9700
    p96view.py --span 2x1                   two parts of a SPLIT=2x1 screen, a window each
                                            (groups 239.255.96.1 and .2, ports 9696, 9697)
    p96view.py --part 2                     only part 2 (group 239.255.96.2, port 9697)
    p96view.py --snapshot out.png --duration 60   no window: save frames as PNG
    p96view.py --web                        show it in a web browser instead

The card speaks Copper (docs/COPPER.md): it sends what was drawn (fills,
copies, text) for the viewer to draw, and pixels only for the rest. The
viewer tells the card what it missed, by answering to the address the
datagrams come from. Uses only the standard library (tkinter for the
window, or a web browser when there is no tkinter); numpy makes it faster
when it is installed.
"""
import argparse
import socket
import struct
import sys
import time
import zlib

try:
    import numpy as np
except ImportError:  # pragma: no cover - optional
    np = None

MAGIC, VERSION = 0x4355, 1
OLD_MAGIC = b"P96M"   # protocol 1, Multicast.card 1.x
F_REPAIR = 1

SURFACE, PALETTE, FRAME = 0x01, 0x02, 0x03
FILL, INVERT, COPY, TEXT, PATTERN = 0x10, 0x11, 0x12, 0x13, 0x14
PIXELS, POINTER, POINTERPOS = 0x20, 0x30, 0x31
JAM2, COMPLEMENT, INVERSVID = 1, 2, 4
RAW, SOLID, RLE = 0, 1, 2
HELLO, NAK, WANT = 1, 2, 3

# P96 RGBFTYPE numbers
CLUT, R8G8B8, B8G8R8, R5G6B5PC, R5G5B5PC = 1, 2, 3, 4, 5
A8R8G8B8, A8B8G8R8, R8G8B8A8, B8G8R8A8 = 6, 7, 8, 9
R5G6B5, R5G5B5, B5G6R5PC, B5G5R5PC = 10, 11, 12, 13

FORMAT_NAMES = {
    CLUT: "CLUT", R8G8B8: "R8G8B8", B8G8R8: "B8G8R8", R5G6B5PC: "R5G6B5PC",
    R5G5B5PC: "R5G5B5PC", A8R8G8B8: "A8R8G8B8", A8B8G8R8: "A8B8G8R8",
    R8G8B8A8: "R8G8B8A8", B8G8R8A8: "B8G8R8A8", R5G6B5: "R5G6B5",
    R5G5B5: "R5G5B5", B5G6R5PC: "B5G6R5PC", B5G5R5PC: "B5G5R5PC",
}

# byte positions of R, G, B within a 3 or 4 byte pixel
BYTE_ORDER = {
    R8G8B8: (0, 1, 2), B8G8R8: (2, 1, 0),
    A8R8G8B8: (1, 2, 3), A8B8G8R8: (3, 2, 1),
    R8G8B8A8: (0, 1, 2), B8G8R8A8: (2, 1, 0),
}

INVERT_TABLE = bytes(b ^ 0xff for b in range(256))


def _expand(v, bits):
    return (v << (8 - bits)) | (v >> (2 * bits - 8))


def rgb16(fmt, v):
    """RGB of one 16 bit pixel; v is the value read big-endian from memory."""
    if fmt in (R5G6B5PC, R5G5B5PC, B5G6R5PC, B5G5R5PC):
        v = ((v & 0xff) << 8) | (v >> 8)  # PC formats are little-endian
    if fmt in (R5G6B5, R5G6B5PC):
        r, g, b = v >> 11, (v >> 5) & 63, v & 31
        return _expand(r, 5), _expand(g, 6), _expand(b, 5)
    if fmt in (R5G5B5, R5G5B5PC):
        r, g, b = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return _expand(r, 5), _expand(g, 5), _expand(b, 5)
    if fmt == B5G6R5PC:
        b, g, r = v >> 11, (v >> 5) & 63, v & 31
        return _expand(r, 5), _expand(g, 6), _expand(b, 5)
    if fmt == B5G5R5PC:
        b, g, r = (v >> 10) & 31, (v >> 5) & 31, v & 31
        return _expand(r, 5), _expand(g, 5), _expand(b, 5)
    raise ValueError(fmt)


def seq_before(a, b):
    """a comes before b, in 32 bit sequence numbers."""
    return ((b - a) & 0xffffffff) - 1 < 0x7fffffff


class Screen:
    """The viewer's copy of the Amiga screen, in the card's own format.

    apply() draws one record, exactly as the card's reference renderer
    (src/render.c) does. Each tile (the card's tile grid) is known to be
    valid or not: a new viewer, or one that lost datagrams, asks for the
    invalid ones (wanted()).
    """

    def __init__(self):
        self.width = self.height = 0
        self.fmt = self.bpp = 0
        self.display = 0
        self.tw = self.th = 16
        self.tcols = self.trows = 0
        self.pixels = bytearray()
        self.arr = None
        self.valid = bytearray()
        self.palette = bytearray(256 * 3)
        self.frames = 0
        self.dirty = None            # (x0, y0, x1, y1) changed since the last take_dirty()
        self.all_dirty = True
        self.pointer = None          # (w, h, hotx, hoty, rgba bytes)
        self.pointer_pos = (0, 0, 0)
        self.pointer_serial = 0
        self.stats = {"records": {}, "bad": 0, "modes": set()}
        self._lut16 = {}

    # ---- the surface

    def complete(self):
        """True once every tile of the screen is known."""
        return bool(self.width) and all(self.valid)

    def invalidate(self):
        self.valid = bytearray(self.tcols * self.trows)

    def _surface(self, b):
        w, h, fmt, bpp, display, _pad, tw, th = struct.unpack(">HHBBBBHH", b[:12])
        self.stats["modes"].add((w, h, FORMAT_NAMES.get(fmt, str(fmt))))
        if display != self.display:
            self.all_dirty = True
        self.display = display
        if not w or not h or not bpp:
            return
        if (w, h, fmt, bpp, tw, th) != (self.width, self.height, self.fmt, self.bpp,
                                        self.tw, self.th) or not self.pixels:
            self.width, self.height, self.fmt, self.bpp = w, h, fmt, bpp
            self.tw, self.th = max(tw, 1), max(th, 1)
            self.tcols = (w + self.tw - 1) // self.tw
            self.trows = (h + self.th - 1) // self.th
            self.pixels = bytearray(w * h * bpp)
            self.arr = (np.frombuffer(self.pixels, dtype=np.uint8).reshape(h, w, bpp)
                        if np is not None else None)
            self.invalidate()
            self.all_dirty = True

    def _mark(self, x, y, w, h):
        if self.dirty is None:
            self.dirty = (x, y, x + w, y + h)
        else:
            x0, y0, x1, y1 = self.dirty
            self.dirty = (min(x0, x), min(y0, y), max(x1, x + w), max(y1, y + h))

    def take_dirty(self):
        """The rectangle changed since the last call (x, y, w, h), or None."""
        if self.all_dirty and self.width:
            self.all_dirty = False
            self.dirty = None
            return (0, 0, self.width, self.height)
        d, self.dirty = self.dirty, None
        if d is None:
            return None
        return (d[0], d[1], d[2] - d[0], d[3] - d[1])

    def _tiles(self, x, y, w, h):
        """Tiles the rectangle touches, and whether it covers each completely."""
        tw, th = self.tw, self.th
        for ty in range(y // th, (y + h - 1) // th + 1):
            for tx in range(x // tw, (x + w - 1) // tw + 1):
                full = (x <= tx * tw and y <= ty * th and
                        x + w >= min((tx + 1) * tw, self.width) and
                        y + h >= min((ty + 1) * th, self.height))
                yield ty * self.tcols + tx, full

    def _covered(self, x, y, w, h):
        for t, full in self._tiles(x, y, w, h):
            if full:
                self.valid[t] = 1

    def _all_valid(self, x, y, w, h):
        return all(self.valid[t] for t, _ in self._tiles(x, y, w, h))

    def wanted(self):
        """The invalid tiles, as rectangles of runs along each row of tiles."""
        out = []
        for ty in range(self.trows):
            row = self.valid[ty * self.tcols:(ty + 1) * self.tcols]
            tx = 0
            while tx < self.tcols:
                if row[tx]:
                    tx += 1
                    continue
                start = tx
                while tx < self.tcols and not row[tx]:
                    tx += 1
                x, y = start * self.tw, ty * self.th
                out.append((x, y, min(tx * self.tw, self.width) - x,
                            min(self.th, self.height - y)))
        return out

    # ---- records

    def _ok(self, x, y, w, h):
        if w <= 0 or h <= 0 or x + w > self.width or y + h > self.height:
            self.stats["bad"] += 1
            return False
        return True

    def apply(self, rtype, b):
        """Draw one record. Returns False if it made no sense."""
        st = self.stats["records"]
        st[rtype] = st.get(rtype, 0) + 1
        if rtype == SURFACE:
            self._surface(b)
            return True
        if rtype == PALETTE:
            first, count = struct.unpack(">HH", b[:4])
            count = min(count, 256 - first, (len(b) - 4) // 3)
            self.palette[first * 3:(first + count) * 3] = b[4:4 + count * 3]
            if self.fmt == CLUT:
                self.all_dirty = True
            return True
        if rtype == FRAME:
            self.frames += 1
            return True
        if rtype == POINTER:
            self._pointer(b)
            return True
        if rtype == POINTERPOS:
            x, y, vis = struct.unpack(">hhB", b[:5])
            self.pointer_pos = (x, y, vis)
            self.pointer_serial += 1
            return True
        if not self.width or len(b) < 8:
            return False
        bpp = self.bpp
        if rtype == COPY:
            sx, sy, dx, dy, w, h = struct.unpack(">HHHHHH", b[:12])
            if not (self._ok(sx, sy, w, h) and self._ok(dx, dy, w, h)):
                return False
            self._copy(sx, sy, dx, dy, w, h)
            # what the copy brings is as good as where it came from
            for t, full in self._tiles(dx, dy, w, h):
                tx, ty = t % self.tcols * self.tw, t // self.tcols * self.th
                x0, y0 = max(tx, dx), max(ty, dy)
                x1 = min(tx + self.tw, dx + w, self.width)
                y1 = min(ty + self.th, dy + h, self.height)
                good = self._all_valid(x0 + sx - dx, y0 + sy - dy, x1 - x0, y1 - y0)
                self.valid[t] = good if full else self.valid[t] and good
            self._mark(dx, dy, w, h)
            return True
        x, y, w, h = struct.unpack(">HHHH", b[:8])
        if not self._ok(x, y, w, h):
            return False
        if rtype == FILL:
            self._fill(x, y, w, h, bytes(b[8:8 + bpp]))
            self._covered(x, y, w, h)
        elif rtype == INVERT:
            self._invert(x, y, w, h)
        elif rtype == TEXT:
            mode, bitx, stride = b[8], *struct.unpack(">HH", b[10:14])
            fg, bg = bytes(b[14:14 + bpp]), bytes(b[14 + bpp:14 + 2 * bpp])
            bits = b[14 + 2 * bpp:]
            if len(bits) < stride * h or (bitx + w + 7) // 8 > stride:
                self.stats["bad"] += 1
                return False
            self._bits(x, y, w, h, mode, fg, bg, self._text_mask(bits, bitx, stride, w, h))
        elif rtype == PATTERN:
            mode, rowmask, px, py = b[8], b[9], b[10], b[11]
            fg, bg = bytes(b[12:12 + bpp]), bytes(b[12 + bpp:12 + 2 * bpp])
            rows = b[12 + 2 * bpp:12 + 2 * bpp + 2 * (rowmask + 1)]
            if len(rows) < 2 * (rowmask + 1):
                self.stats["bad"] += 1
                return False
            words = struct.unpack(">%dH" % (rowmask + 1), rows)
            self._bits(x, y, w, h, mode, fg, bg,
                       self._pattern_mask(words, rowmask, px, py, w, h))
        elif rtype == PIXELS:
            if not self._pixels(x, y, w, h, b[8], b[10:]):
                self.stats["bad"] += 1
                return False
            self._covered(x, y, w, h)
        else:
            return True   # unknown: skipped
        self._mark(x, y, w, h)
        return True

    def _rows(self, x, y, w, h):
        stride, bpp = self.width * self.bpp, self.bpp
        for r in range(h):
            o = (y + r) * stride + x * bpp
            yield o, o + w * bpp

    def _fill(self, x, y, w, h, px):
        if self.arr is not None:
            self.arr[y:y + h, x:x + w] = np.frombuffer(px, dtype=np.uint8)
            return
        row = px * w
        for a, z in self._rows(x, y, w, h):
            self.pixels[a:z] = row

    def _invert(self, x, y, w, h):
        if self.arr is not None:
            self.arr[y:y + h, x:x + w] ^= 0xff
            return
        for a, z in self._rows(x, y, w, h):
            self.pixels[a:z] = self.pixels[a:z].translate(INVERT_TABLE)

    def _copy(self, sx, sy, dx, dy, w, h):
        if self.arr is not None:
            self.arr[dy:dy + h, dx:dx + w] = self.arr[sy:sy + h, sx:sx + w].copy()
            return
        rows = [bytes(self.pixels[a:z]) for a, z in self._rows(sx, sy, w, h)]
        for (a, z), row in zip(self._rows(dx, dy, w, h), rows):
            self.pixels[a:z] = row

    def _text_mask(self, bits, bitx, stride, w, h):
        if np is not None:
            a = np.frombuffer(bytes(bits[:stride * h]), dtype=np.uint8).reshape(h, stride)
            return np.unpackbits(a, axis=1)[:, bitx:bitx + w].astype(bool)
        mask = []
        for r in range(h):
            row = bits[r * stride:(r + 1) * stride]
            mask.append([(row[(bitx + i) >> 3] >> (7 - ((bitx + i) & 7))) & 1
                         for i in range(w)])
        return mask

    def _pattern_mask(self, words, rowmask, px, py, w, h):
        if np is not None:
            wa = np.array(words, dtype=">u2").view(np.uint8).reshape(-1, 2)
            bits = np.unpackbits(wa, axis=1).astype(bool)          # rows x 16
            ri = (py + np.arange(h)) & rowmask
            ci = (px + np.arange(w)) & 15
            return bits[ri][:, ci]
        return [[(words[(py + r) & rowmask] >> (15 - ((px + i) & 15))) & 1 for i in range(w)]
                for r in range(h)]

    def _bits(self, x, y, w, h, mode, fg, bg, mask):
        """Draw a one bit per pixel mask in a draw mode (render.c's apply())."""
        if self.arr is not None:
            if mode & INVERSVID:
                mask = ~mask
            area = self.arr[y:y + h, x:x + w]
            if mode & COMPLEMENT:
                area[mask] ^= 0xff
                return
            area[mask] = np.frombuffer(fg, dtype=np.uint8)
            if mode & JAM2:
                area[~mask] = np.frombuffer(bg, dtype=np.uint8)
            return
        bpp, inv = self.bpp, bool(mode & INVERSVID)
        for r, (a, _z) in enumerate(self._rows(x, y, w, h)):
            for i in range(w):
                bit = bool(mask[r][i]) != inv
                o = a + i * bpp
                if mode & COMPLEMENT:
                    if bit:
                        self.pixels[o:o + bpp] = self.pixels[o:o + bpp].translate(INVERT_TABLE)
                elif bit:
                    self.pixels[o:o + bpp] = fg
                elif mode & JAM2:
                    self.pixels[o:o + bpp] = bg

    def _pixels(self, x, y, w, h, enc, data):
        bpp, row = self.bpp, w * self.bpp
        if enc == SOLID:
            if len(data) < bpp:
                return False
            self._fill(x, y, w, h, bytes(data[:bpp]))
            return True
        if enc == RLE:
            if len(data) % (1 + bpp):
                return False
            if np is not None:
                runs = np.frombuffer(bytes(data), dtype=np.uint8).reshape(-1, 1 + bpp)
                px = np.repeat(runs[:, 1:], runs[:, 0], axis=0)
                if len(px) != w * h:
                    return False
                self.arr[y:y + h, x:x + w] = px.reshape(h, w, bpp)
                return True
            out = bytearray()
            for i in range(0, len(data), 1 + bpp):
                out += bytes(data[i + 1:i + 1 + bpp]) * data[i]
            if len(out) != row * h:
                return False
            data = out
        elif enc != RAW or len(data) < row * h:
            return False
        if self.arr is not None:
            self.arr[y:y + h, x:x + w] = np.frombuffer(bytes(data[:row * h]),
                                                       dtype=np.uint8).reshape(h, w, bpp)
            return True
        for r, (a, z) in enumerate(self._rows(x, y, w, h)):
            self.pixels[a:z] = data[r * row:(r + 1) * row]
        return True

    def _pointer(self, b):
        w, h, hx, hy = struct.unpack(">BBbb", b[:4])
        words = (w + 15) // 16
        cols = [None, bytes(b[4:7]), bytes(b[7:10]), bytes(b[10:13])]
        data = b[13:13 + h * 4 * words]
        if not w or not h or len(data) < h * 4 * words:
            self.pointer = None
            return
        rgba = bytearray()
        for r in range(h):
            p0 = int.from_bytes(data[r * 4 * words:r * 4 * words + 2 * words], "big")
            p1 = int.from_bytes(data[r * 4 * words + 2 * words:(r + 1) * 4 * words], "big")
            for i in range(w):
                shift = 16 * words - 1 - i
                c = ((p0 >> shift) & 1) | (((p1 >> shift) & 1) << 1)
                rgba += cols[c] + b"\xff" if c else b"\0\0\0\0"
        self.pointer = (w, h, hx, hy, bytes(rgba))
        self.pointer_serial += 1

    # ---- colours

    def rgb(self):
        """The screen as packed 8 bit RGB, width * height * 3 bytes."""
        if not self.width:
            return b""
        return self.rgb_rect(0, 0, self.width, self.height)

    def rgb_rect(self, x, y, w, h):
        """A rectangle of the screen as packed 8 bit RGB."""
        if not self.display:
            return bytes(w * h * 3)
        if self.arr is not None:
            return self._rgb_numpy(self.arr[y:y + h, x:x + w])
        fmt, n = self.fmt, self.bpp
        px = b"".join(bytes(self.pixels[a:z]) for a, z in self._rows(x, y, w, h))
        if fmt == CLUT:
            pal = [bytes(self.palette[i * 3:i * 3 + 3]) for i in range(256)]
            return b"".join(pal[p] for p in px)
        if fmt in BYTE_ORDER:
            ri, gi, bi = BYTE_ORDER[fmt]
            out = bytearray(w * h * 3)
            out[0::3] = px[ri::n]
            out[1::3] = px[gi::n]
            out[2::3] = px[bi::n]
            return bytes(out)
        lut = self._lut16.get(fmt)
        if lut is None:
            lut = [bytes(rgb16(fmt, v)) for v in range(65536)]
            self._lut16[fmt] = lut
        vals = struct.unpack(">%dH" % (len(px) // 2), px)
        return b"".join(lut[v] for v in vals)

    def _rgb_numpy(self, a):
        fmt = self.fmt
        if fmt == CLUT:
            pal = np.frombuffer(bytes(self.palette), dtype=np.uint8).reshape(256, 3)
            return pal[a[:, :, 0]].tobytes()
        if fmt in BYTE_ORDER:
            return a[:, :, list(BYTE_ORDER[fmt])].tobytes()
        lut = self._lut16.get(("np", fmt))
        if lut is None:
            lut = np.array([rgb16(fmt, v) for v in range(65536)], dtype=np.uint8)
            self._lut16[("np", fmt)] = lut
        vals = (a[:, :, 0].astype(np.uint16) << 8) | a[:, :, 1]
        return lut[vals].tobytes()


class Viewer:
    """Datagrams in order, to a Screen; asks the card for what is missing.

    feed() takes each datagram as it arrives, tick() now and then sends what
    the card should know: HELLO, NAK for missing datagrams, WANT for tiles
    the screen does not have. send(data, addr) is how messages leave.
    """

    NAK_DELAY = 0.02     # wait this long for a datagram that is merely late
    NAK_AGAIN = 0.2
    GIVE_UP = 1.0        # then ask for the tiles instead
    HELLO_EVERY = 2.0
    HOLD = 4096          # datagrams kept while waiting for one

    def __init__(self, screen, send=None):
        self.screen = screen
        self.send = send
        self.stream = None
        self.expect = None
        self.held = {}
        self.gap_since = None
        self.last_nak = self.last_hello = self.last_want = -1e9
        self.last_pixels = 0
        self.amiga = None
        self.warned_old = False
        self.stats = {"datagrams": 0, "bytes": 0, "repairs": 0, "naks": 0, "wants": 0,
                      "gaps": 0, "dups": 0, "bad": 0}

    def feed(self, data, addr=None, now=None):
        """One datagram. Returns the record types applied, in order."""
        now = time.monotonic() if now is None else now
        st = self.stats
        st["datagrams"] += 1
        st["bytes"] += len(data)
        if len(data) < 12:
            st["bad"] += 1
            return []
        magic, version, flags, stream, seq = struct.unpack(">HBBII", data[:12])
        if magic != MAGIC or version != VERSION:
            if data[:4] == OLD_MAGIC and not self.warned_old:
                print("p96view: this is Multicast.card 1.x (protocol 1): install "
                      "Multicast.card 2 for this viewer", file=sys.stderr)
                self.warned_old = True
            st["bad"] += 1
            return []
        if addr is not None:
            self.amiga = addr
        if stream != self.stream:
            # the card started (again): everything from now on
            self.stream = stream
            self.expect = seq
            self.held = {}
            self.gap_since = None
            self.screen.invalidate()
            self.last_want = -1e9
        if flags & F_REPAIR:
            st["repairs"] += 1
        if seq != self.expect:
            if seq_before(seq, self.expect):
                st["dups"] += 1
                return []
            if len(self.held) < self.HOLD:
                self.held[seq] = data
            if self.gap_since is None:
                self.gap_since = now
            return []
        types = self._apply(data)
        self.expect = (self.expect + 1) & 0xffffffff
        while self.expect in self.held:
            types += self._apply(self.held.pop(self.expect))
            self.expect = (self.expect + 1) & 0xffffffff
        self.gap_since = now if self.held else None
        return types

    def _apply(self, data):
        types, pos, n = [], 12, len(data)
        while pos + 3 <= n:
            rtype = data[pos]
            length = (data[pos + 1] << 8) | data[pos + 2]
            body = memoryview(data)[pos + 3:pos + 3 + length]
            if len(body) < length:
                self.stats["bad"] += 1
                break
            self.screen.apply(rtype, body)
            if rtype == PIXELS:
                self.last_pixels = time.monotonic()
            types.append(rtype)
            pos += 3 + length
        return types

    def missing(self):
        """The missing datagrams, as (first, count) ranges."""
        if not self.held:
            return []
        out, s = [], self.expect
        top = max(self.held, key=lambda q: (q - self.expect) & 0xffffffff)
        while s != top and len(out) < 64:
            if s in self.held:
                s = (s + 1) & 0xffffffff
                continue
            first, count = s, 0
            while s not in self.held and s != top and count < 64:
                s = (s + 1) & 0xffffffff
                count += 1
            out.append((first, count))
        return out

    def _msg(self, mtype, payload=b""):
        if self.send and self.amiga:
            self.send(struct.pack(">HBBI", MAGIC, VERSION, mtype, self.stream or 0) + payload,
                      self.amiga)

    def tick(self, now=None):
        now = time.monotonic() if now is None else now
        if self.stream is None:
            return
        if now - self.last_hello >= self.HELLO_EVERY:
            self._msg(HELLO, struct.pack(">I", self.expect))
            self.last_hello = now
        if self.gap_since is not None:
            waited = now - self.gap_since
            if waited >= self.GIVE_UP:
                # too late for repairs: everything held, in order, and ask for the tiles
                self.stats["gaps"] += 1
                held, self.held = self.held, {}
                for seq in sorted(held, key=lambda q: (q - self.expect) & 0xffffffff):
                    if seq != self.expect:
                        # what the lost ones drew may be anywhere, before what follows
                        self.screen.invalidate()
                    self._apply(held[seq])
                    self.expect = (seq + 1) & 0xffffffff
                self.gap_since = None
                self.last_want = -1e9
            elif waited >= self.NAK_DELAY and now - self.last_nak >= self.NAK_AGAIN:
                ranges = self.missing()
                if ranges:
                    self._msg(NAK, b"".join(struct.pack(">IH", f, c) for f, c in ranges))
                    self.stats["naks"] += 1
                    self.last_nak = now
        scr = self.screen
        if scr.width and self.gap_since is None and not all(scr.valid):
            quiet = now - self.last_pixels > 0.5
            if now - self.last_want > 3.0 or (quiet and now - self.last_want > 0.5):
                if not any(scr.valid):
                    rects = [(0, 0, 0, 0)]
                else:
                    rects = scr.wanted()[:128]
                self._msg(WANT, b"".join(struct.pack(">HHHH", *r) for r in rects))
                self.stats["wants"] += 1
                self.last_want = now


def png_bytes(width, height, rgb, level=6, alpha=False):
    """Encode 8 bit RGB (or RGBA) as a PNG with the standard library only."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    row = width * (4 if alpha else 3)
    raw = b"".join(b"\x00" + rgb[y * row:(y + 1) * row] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6 if alpha else 2,
                                         0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, level))
            + chunk(b"IEND", b""))


def write_png(path, width, height, rgb):
    import os
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(png_bytes(width, height, rgb))
    os.replace(tmp, path)


def write_raw(path, screen):
    """The screen in the card's format: a 12 byte header (w, h, format, bpp), pixels."""
    import os
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(struct.pack(">IIHH", screen.width, screen.height, screen.fmt, screen.bpp))
        f.write(screen.pixels)
    os.replace(tmp, path)


def open_socket(group, port, iface):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    s.bind(("", port))
    if group and socket.inet_aton(group)[0] & 0xf0 == 0xe0:
        mreq = socket.inet_aton(group) + socket.inet_aton(iface or "0.0.0.0")
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    return s


def make_viewer(sock, screen):
    def send(data, addr):
        try:
            sock.sendto(data, addr)
        except OSError:
            pass
    return Viewer(screen, send)


def print_stats(viewer, out=sys.stderr):
    st, screen = viewer.stats, viewer.screen
    rec = screen.stats["records"]
    names = {FILL: "fill", INVERT: "invert", COPY: "copy", TEXT: "text", PATTERN: "pattern",
             PIXELS: "pixels", POINTERPOS: "pointer"}
    orders = ", ".join("%d %s" % (rec[t], n) for t, n in names.items() if rec.get(t))
    modes = ", ".join("%dx%d %s" % m for m in sorted(screen.stats["modes"])) or "none"
    print("p96view: %d datagrams, %d KB, %d frames; %s; %d repairs, %d NAKs, %d WANTs, "
          "%d gaps, %d bad; %d of %d tiles; modes: %s" % (
              st["datagrams"], st["bytes"] >> 10, screen.frames, orders or "no records",
              st["repairs"], st["naks"], st["wants"], st["gaps"],
              st["bad"] + screen.stats["bad"], sum(screen.valid), len(screen.valid), modes),
          file=out)


class Head:
    """One stream: a socket and its viewer, at a place on the wall."""

    def __init__(self, sock, viewer, col=0, row=0):
        self.sock, self.viewer, self.screen = sock, viewer, viewer.screen
        self.col, self.row = col, row


def wall(heads):
    """Where each head goes, and the size of all of them: (offsets, width, height).

    Heads are laid out in their columns and rows (Wrap.card's SPAN=), each
    column as wide as its widest head and each row as high as its highest.
    """
    cols = max(h.col for h in heads) + 1
    rows = max(h.row for h in heads) + 1
    cw = [max([h.screen.width for h in heads if h.col == c] + [0]) for c in range(cols)]
    rh = [max([h.screen.height for h in heads if h.row == r] + [0]) for r in range(rows)]
    offsets = [(sum(cw[:h.col]), sum(rh[:h.row])) for h in heads]
    return offsets, sum(cw), sum(rh)


def composite(heads):
    """All heads as one picture: (width, height, packed RGB)."""
    offsets, w, h = wall(heads)
    out = bytearray(w * h * 3)
    for hd, (ox, oy) in zip(heads, offsets):
        scr = hd.screen
        if not scr.width:
            continue
        rgb, row = scr.rgb(), scr.width * 3
        for y in range(scr.height):
            o = ((oy + y) * w + ox) * 3
            out[o:o + row] = rgb[y * row:(y + 1) * row]
    return w, h, bytes(out)


def write_raw_wall(path, heads):
    """All heads as one screen in the card's format (write_raw's), if they share it."""
    import os
    offsets, w, h = wall(heads)
    bpp = heads[0].screen.bpp
    if any(hd.screen.bpp != bpp for hd in heads):
        return
    out = bytearray(w * h * bpp)
    for hd, (ox, oy) in zip(heads, offsets):
        scr, row = hd.screen, hd.screen.width * bpp
        for y in range(scr.height):
            o = ((oy + y) * w + ox) * bpp
            out[o:o + row] = scr.pixels[y * row:(y + 1) * row]
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(struct.pack(">IIHH", w, h, heads[0].screen.fmt, bpp))
        f.write(out)
    os.replace(tmp, path)


def receive_some(sock, viewer, limit=4000):
    """Whatever has arrived, without waiting. Returns the record types applied."""
    types = []
    for _ in range(limit):
        try:
            data, addr = sock.recvfrom(65536)
        except (BlockingIOError, socket.timeout):
            break
        types += viewer.feed(data, addr)
    viewer.tick()
    return types


def run_headless(heads, args):
    """Save the latest complete picture to args.snapshot, at most once a second."""
    import select
    end = time.time() + args.duration if args.duration else None
    last_save = 0
    for h in heads:
        h.sock.setblocking(False)

    def save():
        if len(heads) == 1:
            scr = heads[0].screen
            write_png(args.snapshot, scr.width, scr.height, scr.rgb())
            if args.raw:
                write_raw(args.raw, scr)
        else:
            write_png(args.snapshot, *composite(heads))
            if args.raw:
                write_raw_wall(args.raw, heads)

    while end is None or time.time() < end:
        select.select([h.sock for h in heads], [], [], 0.05)
        framed = False
        for h in heads:
            if FRAME in receive_some(h.sock, h.viewer):
                framed = True
        if framed and all(h.screen.complete() for h in heads):
            now = time.time()
            if now - last_save >= 1:
                save()
                last_save = now
    if all(h.screen.complete() for h in heads):
        save()
    for h in heads:
        print_stats(h.viewer)


def run_window(heads, args):  # pragma: no cover - interactive
    """A window per board (--span: like a monitor each), or all in one (--one-window)."""
    import tkinter as tk

    root = tk.Tk()
    separate = len(heads) > 1 and not args.one_window
    windows = []
    for i, h in enumerate(heads):
        win = root if i == 0 or not separate else tk.Toplevel(root)
        canvas = tk.Canvas(win, bg="black", highlightthickness=0, width=640, height=480)
        canvas.pack(fill="both", expand=True)
        if i == 0 or separate:
            win.protocol("WM_DELETE_WINDOW", root.destroy)
            windows.append((win, canvas))
    per = [{"img": None, "item": None, "size": None, "ptr": None, "ptr_item": None,
            "ptr_serial": -1, "at": None, "placed": False} for _ in heads]
    state = {"wall": None}
    for h in heads:
        h.sock.setblocking(False)

    def present():
        offsets, ww, wh = wall(heads)
        if not separate and (ww, wh) != state["wall"] and ww:
            state["wall"] = (ww, wh)
            windows[0][1].configure(width=ww, height=wh)
            scr = heads[0].screen
            root.title("P96Multicast %dx%d %s%s" % (ww, wh, FORMAT_NAMES.get(scr.fmt, "?"),
                       ", %d screens" % len(heads) if len(heads) > 1 else ""))
        for i, (h, st) in enumerate(zip(heads, per)):
            screen = h.screen
            win, canvas = windows[i] if separate else windows[0]
            at = (0, 0) if separate else offsets[i]
            size = (screen.width, screen.height)
            if not screen.width:
                # say what is (not) arriving, in the title
                n = h.viewer.stats["datagrams"]
                text = ("P96Multicast screen %d (port %d): %s" % (
                    i + 1, args.port + i,
                    "nothing from the Amiga yet" if not n else
                    "%d datagrams, but no screen shown on this board yet" % n))
                if st.get("wait") != text and (separate or i == 0):
                    st["wait"] = text
                    win.title(text)
                continue
            if size != st["size"] or at != st["at"]:
                if size != st["size"]:
                    st["img"] = tk.PhotoImage(width=size[0], height=size[1])
                    screen.all_dirty = True
                    if separate:
                        canvas.configure(width=size[0], height=size[1])
                        if not st["placed"]:
                            # side by side as the boards are, like monitors
                            win.geometry("+%d+%d" % (40 + offsets[i][0] + 8 * h.col,
                                                     40 + offsets[i][1] + 30 * h.row))
                            st["placed"] = True
                if st["item"] is None:
                    st["item"] = canvas.create_image(at[0], at[1], anchor="nw", image=st["img"])
                else:
                    canvas.itemconfigure(st["item"], image=st["img"])
                    canvas.coords(st["item"], at[0], at[1])
                st["size"], st["at"] = size, at
                st["ptr_serial"] = -1
            if separate:
                text = "P96Multicast screen %d of %d (port %d) %dx%d %s%s" % (
                    i + 1, len(heads), args.port + i, size[0], size[1],
                    FORMAT_NAMES.get(screen.fmt, "?"),
                    "" if screen.display else ": display off (its screen is not shown)")
                if st.get("wait") != text:
                    st["wait"] = text
                    win.title(text)
            d = screen.take_dirty()
            if d:
                x, y, w, hh = d
                part = tk.PhotoImage(data=b"P6 %d %d 255\n" % (w, hh) +
                                     screen.rgb_rect(x, y, w, hh), format="PPM")
                st["img"].tk.call(st["img"], "copy", part, "-to", x, y)
            if screen.pointer_serial != st["ptr_serial"]:
                st["ptr_serial"] = screen.pointer_serial
                x, y, vis = screen.pointer_pos
                if screen.pointer and vis:
                    w, hh, _hx, _hy, rgba = screen.pointer
                    if st["ptr"] is None or st["ptr"][0] is not screen.pointer:
                        img = tk.PhotoImage(data=png_bytes(w, hh, rgba, alpha=True), format="PNG")
                        st["ptr"] = (screen.pointer, img)
                        if st["ptr_item"] is None:
                            st["ptr_item"] = canvas.create_image(0, 0, anchor="nw", image=img)
                        else:
                            canvas.itemconfigure(st["ptr_item"], image=img)
                    canvas.coords(st["ptr_item"], at[0] + x, at[1] + y)
                    canvas.itemconfigure(st["ptr_item"], state="normal")
                    canvas.tag_raise(st["ptr_item"])
                elif st["ptr_item"] is not None:
                    canvas.itemconfigure(st["ptr_item"], state="hidden")

    state["shown"] = 0.0

    def poll():
        # show only whole updates: when the card says one is complete (FRAME),
        # or now and then anyway
        framed = False
        for h in heads:
            if FRAME in receive_some(h.sock, h.viewer):
                framed = True
        now = time.monotonic()
        if framed or now - state["shown"] > 0.25:
            present()
            state["shown"] = now
        root.after(10, poll)

    poll()
    root.mainloop()
    for h in heads:
        print_stats(h.viewer)


WEB_PAGE = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>P96Multicast</title>
<style>
 body { margin: 0; background: #111; color: #aaa; font: 13px sans-serif; }
 #bar { padding: 4px 8px; }
 img { display: block; margin: 0 auto; image-rendering: pixelated; max-width: 100vw; }
</style></head>
<body><div id="bar">waiting for the Amiga...</div><img id="s" alt="">
<script>
const img = document.getElementById("s"), bar = document.getElementById("bar");
let last = -1;
async function tick() {
  try {
    const st = await (await fetch("status", {cache: "no-store"})).json();
    bar.textContent = st.text;
    if (st.frame !== last && st.width) {
      const b = await (await fetch("frame.png?" + st.frame, {cache: "no-store"})).blob();
      const old = img.src;
      img.src = URL.createObjectURL(b);
      if (old) URL.revokeObjectURL(old);
      last = st.frame;
    }
  } catch (e) { bar.textContent = "viewer stopped"; }
  setTimeout(tick, 50);
}
tick();
</script></body></html>
"""


def run_web(heads, args):  # pragma: no cover - interactive
    """Receive in a thread; serve the screen(s) to a browser on localhost."""
    import http.server
    import json
    import select
    import threading
    import webbrowser

    lock = threading.Lock()
    state = {"version": 0, "png": None, "png_version": -1}
    for h in heads:
        h.sock.setblocking(False)

    def receive():
        while True:
            try:
                select.select([h.sock for h in heads], [], [], 0.05)
            except OSError:
                return
            with lock:
                for h in heads:
                    types = receive_some(h.sock, h.viewer)
                    if FRAME in types or SURFACE in types or PALETTE in types:
                        state["version"] += 1

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, ctype, body):
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                self.send("text/html; charset=utf-8", WEB_PAGE)
            elif path == "/status":
                with lock:
                    scr = heads[0].screen
                    _, ww, wh = wall(heads)
                    kb = sum(h.viewer.stats["bytes"] for h in heads) >> 10
                    text = ("%dx%d %s%s, %d frames, %d KB received" % (
                        ww, wh, FORMAT_NAMES.get(scr.fmt, "?"),
                        " (%d screens)" % len(heads) if len(heads) > 1 else "",
                        scr.frames, kb)
                        if ww else "waiting for the Amiga (%d datagrams so far)"
                        % sum(h.viewer.stats["datagrams"] for h in heads))
                    body = {"frame": state["version"], "width": ww, "text": text}
                self.send("application/json", json.dumps(body).encode())
            elif path == "/frame.png":
                with lock:
                    if state["png_version"] != state["version"]:
                        w, h, rgb = composite(heads)
                        if w:
                            state["png"] = png_bytes(w, h, rgb, level=1)
                            state["png_version"] = state["version"]
                    png = state["png"]
                if png:
                    self.send("image/png", png)
                else:
                    self.send_error(404)
            else:
                self.send_error(404)

    threading.Thread(target=receive, daemon=True).start()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.web), Handler)
    url = "http://127.0.0.1:%d/" % server.server_address[1]
    print("p96view: showing the Amiga screen at %s (Ctrl-C to stop)" % url, file=sys.stderr)
    if not args.no_browser:
        webbrowser.open(url)
    server.serve_forever()


def part_group(group, n):
    """Part n's group (from 0): a multicast group counts up, like the card's DEST."""
    try:
        a = socket.inet_aton(group)
    except OSError:
        return group
    if a[0] & 0xf0 != 0xe0 or not n:
        return group
    v = struct.unpack(">I", a)[0] + n
    return socket.inet_ntoa(struct.pack(">I", v))


def parse_span(text):
    """'2x1' -> (2, 1)"""
    try:
        c, r = (int(v) for v in text.lower().split("x"))
    except ValueError:
        raise argparse.ArgumentTypeError("--span wants CxR, e.g. 2x1")
    if c < 1 or r < 1 or c * r > 8:
        raise argparse.ArgumentTypeError("--span: 1 to 8 screens")
    return c, r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--group", default="239.255.96.1",
                    help="multicast group to join (a unicast address just binds)")
    ap.add_argument("--port", type=int, default=9696)
    ap.add_argument("--span", type=parse_span, metavar="CxR",
                    help="several boards, a window each (like monitors), e.g. 2x1 for Wrap.card's "
                         "SPAN=2x1: ports PORT, PORT+1, ... left to right, top to bottom")
    ap.add_argument("--part", type=int, metavar="N",
                    help="one part of a SPLIT= screen: part N's group and port (the card's "
                         "DEST + N - 1, PORT + N - 1), e.g. on a second device")
    ap.add_argument("--one-window", action="store_true",
                    help="with --span: all boards in one window, not a window each")
    ap.add_argument("--iface", help="local address of the interface to join on")
    ap.add_argument("--snapshot", help="no window: write the screen to this PNG")
    ap.add_argument("--raw", help="with --snapshot: also the card's own pixels to this file")
    ap.add_argument("--duration", type=float, default=0,
                    help="with --snapshot: stop after this many seconds")
    ap.add_argument("--web", type=int, nargs="?", const=8096, default=None, metavar="PORT",
                    help="show the screen in a web browser (default port 8096); "
                         "used automatically when tkinter is missing")
    ap.add_argument("--no-browser", action="store_true",
                    help="with --web: do not open the browser, just print the address")
    args = ap.parse_args(argv)

    # SIGTERM stops a headless run cleanly (background jobs ignore SIGINT)
    import signal
    signal.signal(signal.SIGTERM, lambda *a: (_ for _ in ()).throw(KeyboardInterrupt()))
    cols, rows = args.span or (1, 1)
    first = args.part - 1 if args.part else 0
    heads = []
    for i in range(cols * rows):
        n = first + i
        sock = open_socket(part_group(args.group, n), args.port + n, args.iface)
        heads.append(Head(sock, make_viewer(sock, Screen()), i % cols, i // cols))
    args.port += first
    try:
        if args.snapshot:
            run_headless(heads, args)
        elif args.web is not None:
            run_web(heads, args)
        else:
            try:
                import tkinter  # noqa: F401
            except ImportError:
                print("p96view: no tkinter in this Python (on macOS with Homebrew: "
                      "brew install python-tk), using the web view", file=sys.stderr)
                args.web = 8096
                run_web(heads, args)
            else:
                run_window(heads, args)
    except KeyboardInterrupt:
        for h in heads:
            print_stats(h.viewer)
    return 0


if __name__ == "__main__":
    sys.exit(main())
