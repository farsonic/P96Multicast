#!/usr/bin/env python3
"""Panorama viewer: shows the screen an Amiga's Panorama.card sends.

    panoview.py                              join 239.255.96.1:9696, show a window
    panoview.py --group 127.0.0.1            UAE's NAT (slirp): the card sends to 10.0.2.2
    panoview.py --group 239.255.96.2 --port 9700
    panoview.py --span 2x1                   two parts of a SPLIT=2x1 screen, a window each
                                            (groups 239.255.96.1 and .2, ports 9696, 9697)
    panoview.py --part 2                     only part 2 (group 239.255.96.2, port 9697)
    panoview.py --span 2x1 --groups 239.1.1.1,239.2.2.2
                                            each part from an address of its own
                                            (the card's DEST= and DEST2= ...)
    panoview.py --snapshot out.png --duration 60   no window: save frames as PNG
    panoview.py --web                        show it in a web browser instead

The card speaks Copper (docs/COPPER.md): it sends what was drawn (fills,
copies, text) for the viewer to draw, and pixels only for the rest. The
viewer tells the card what it missed, by answering to the address the
datagrams come from. Uses only the standard library (tkinter for the
window, or a web browser when there is no tkinter); numpy makes it faster
when it is installed.
"""
import argparse
import collections
import random
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
OLD_MAGIC = b"P96M"   # protocol 1, Panorama.card 1.x
F_REPAIR = 1

SURFACE, PALETTE, FRAME = 0x01, 0x02, 0x03
FILL, INVERT, COPY, TEXT, PATTERN = 0x10, 0x11, 0x12, 0x13, 0x14
PIXELS, POINTER, POINTERPOS, SPRITE, SPRITEIMAGE = 0x20, 0x30, 0x31, 0x32, 0x33
JAM2, COMPLEMENT, INVERSVID = 1, 2, 4
RAW, SOLID, RLE = 0, 1, 2
HELLO, NAK, WANT, INPUT, WANTSPRITES = 1, 2, 3, 4, 5
IN_MOVE, IN_BUTTON, IN_KEY, IN_LEAVE = 1, 2, 3, 4
SF_INPUT = 1                    # SURFACE: the card takes our mouse and keys
SF_BEHIND = 2                   # SURFACE: another screen is in front on the Amiga
CU_SPRITE_IMAGES = 64           # sprite image ids the card uses
DECODE_PORT = 9700              # TCP: the Amiga asks us to decode pictures (panoramapic.datatype)

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
        self.takes_input = False     # the card works the Amiga with our mouse and keys
        self.behind = False          # not on the Amiga's display now: the last picture
        self.tw = self.th = 16
        self.tcols = self.trows = 0
        self.pixels = bytearray()
        self.arr = None
        self.valid = bytearray()
        self.palette = bytearray(256 * 3)
        self.frames = 0
        self.frame = 0               # the number of the last FRAME (SPLIT=: the same for all parts)
        self.frame_time = None       # ... its time on the Amiga, ms (Panorama.card 3.2), or None
        self.frame_arrival = None    # ... and when it came here (time.monotonic())
        self.dirty = None            # (x0, y0, x1, y1) changed since the last take_dirty()
        self.all_dirty = True
        self.pointer = None          # (w, h, hotx, hoty, rgba bytes)
        self.pointer_pos = (0, 0, 0)
        self.pointer_serial = 0
        self.sprites = {}            # n: (x, y, visible, image id)
        self.sprite_images = {}      # id: {"w", "h", "buf", "rgba" (once all of it came)}
        self.sprite_serial = 0
        self.stats = {"records": {}, "bad": 0, "modes": set()}
        self._lut16 = {}

    # ---- the surface

    def complete(self):
        """True once every tile of the screen is known."""
        return bool(self.width) and all(self.valid)

    def invalidate(self):
        self.valid = bytearray(self.tcols * self.trows)

    def _surface(self, b):
        w, h, fmt, bpp, display, flags, tw, th = struct.unpack(">HHBBBBHH", b[:12])
        self.takes_input = bool(flags & SF_INPUT)
        self.behind = bool(flags & SF_BEHIND)
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
            if len(b) >= 4:
                self.frame = struct.unpack(">I", b[:4])[0]
            self.frame_time = struct.unpack(">I", b[4:8])[0] if len(b) >= 8 else None
            return True
        if rtype == POINTER:
            self._pointer(b)
            return True
        if rtype == POINTERPOS:
            x, y, vis = struct.unpack(">hhB", b[:5])
            self.pointer_pos = (x, y, vis)
            self.pointer_serial += 1
            return True
        if rtype == SPRITE:
            if len(b) >= 8:
                n, vis, x, y, image = struct.unpack(">BBhhH", b[:8])
                self.sprites[n] = (x, y, vis, image)
                self.sprite_serial += 1
            return True
        if rtype == SPRITEIMAGE:
            self._sprite_image(b)
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
        self.pointer = (w, h, hx, hy, sprite_rgba(data, w, h, cols))
        self.pointer_serial += 1

    def _sprite_image(self, b):
        """A band of rows of a sprite image; the image is there once its last
        band is (until then, and while the id is redefined, not shown)."""
        if len(b) < 19:
            return
        ident, w, depth, h, first, rows = struct.unpack(">HBBHHH", b[:10])
        depth = 4 if depth == 4 else 2          # 4: an attached pair, 15 colours
        ncol = (1 << depth) - 1
        words = (w + 15) // 16
        head = 10 + 3 * ncol
        cols = [None] + [bytes(b[10 + 3 * i:13 + 3 * i]) for i in range(ncol)]
        data = b[head:head + rows * 2 * depth * words]
        if not w or not h or first + rows > h or len(data) < rows * 2 * depth * words:
            return
        img = self.sprite_images.get(ident)
        if first == 0 or img is None or (img["w"], img["h"]) != (w, h):
            img = self.sprite_images[ident] = {"w": w, "h": h, "buf": bytearray(w * h * 4),
                                               "rgba": None}
        img["buf"][first * w * 4:(first + rows) * w * 4] = sprite_rgba(data, w, rows, cols, depth)
        if first + rows == h:
            img["rgba"] = bytes(img["buf"])
            self.sprite_serial += 1

    def lacking_sprite(self):
        """Is a shown sprite's image not (all) here?"""
        for x, y, vis, image in self.sprites.values():
            if vis:
                img = self.sprite_images.get(image)
                if not img or not img["rgba"]:
                    return True
        return False

    def shown_sprites(self):
        """The sprites to draw, back to front (7 first: lower numbers in
        front, as the hardware's): (n, x, y, w, h, rgba)."""
        out = []
        for n in range(7, 0, -1):
            sp = self.sprites.get(n)
            if not sp or not sp[2]:
                continue
            img = self.sprite_images.get(sp[3])
            if img and img["rgba"]:
                out.append((n, sp[0], sp[1], img["w"], img["h"], img["rgba"]))
        return out

    # ---- colours

    def rgb(self):
        """The screen as packed 8 bit RGB, width * height * 3 bytes."""
        if not self.width:
            return b""
        return self.rgb_rect(0, 0, self.width, self.height)

    def rgb_rect(self, x, y, w, h):
        """A rectangle of the screen as packed 8 bit RGB."""
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
    LACKING = 0.5        # a shown sprite's image not here this long: ask for the sprites
    HOLD = 4096          # datagrams kept while waiting for one

    def __init__(self, screen, send=None):
        self.screen = screen
        self.send = send
        self.drop = 0.0              # --drop: the share of datagrams to lose
        self.pacer = None            # the window's Pacer, for print_stats
        self.stream = None
        self.expect = None
        self.held = {}
        self.gap_since = None
        self.last_nak = self.last_hello = self.last_want = self.last_want_sprites = -1e9
        self.lacking_since = None    # a shown sprite's image isn't here, since
        self.sprites_lost = False    # a gap given up on: sprite images may be lost
        self.last_pixels = 0
        self.amiga = None
        self.warned_old = False
        # in step (several parts of one screen): stop after each FRAME, until
        # release(), so a picture is never shown with half of the next update
        self.in_step = False
        self.stopped = False
        self.waiting = []
        self.stats = {"datagrams": 0, "bytes": 0, "repairs": 0, "naks": 0, "wants": 0,
                      "want_sprites": 0, "gaps": 0, "dups": 0, "bad": 0}

    def feed(self, data, addr=None, now=None):
        """One datagram. Returns the record types applied, in order."""
        now = time.monotonic() if now is None else now
        if self.stopped:
            self.waiting.append((data, addr, now))
            return []
        types = self._feed(data, addr, now)
        if self.in_step and FRAME in types:
            self.stopped = True
        return types

    def release(self):
        """Go on after a FRAME: what waited, up to the next FRAME."""
        self.stopped = False
        types = []
        while self.waiting and not self.stopped:
            data, addr, came = self.waiting.pop(0)
            types += self.feed(data, addr, came)
        return types

    def frame_waiting(self):
        """Does a whole update wait (a FRAME among what waits)?"""
        return self.next_frame() is not False

    def next_frame(self):
        """The next FRAME waiting: (its Amiga time in ms or None, when it
        came), or False if none waits."""
        for data, _addr, came in self.waiting:
            pos, n = 12, len(data)
            while pos + 3 <= n:
                length = (data[pos + 1] << 8) | data[pos + 2]
                if data[pos] == FRAME:
                    t = (struct.unpack(">I", data[pos + 7:pos + 11])[0]
                         if length >= 8 and pos + 11 <= n else None)
                    return t, came
                pos += 3 + length
        return False

    def _feed(self, data, addr=None, now=None):
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
                print("panoview: this is Panorama.card 1.x (protocol 1): install "
                      "Panorama.card 2 for this viewer", file=sys.stderr)
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
        types = self._apply(data, now)
        self.expect = (self.expect + 1) & 0xffffffff
        while self.expect in self.held:
            types += self._apply(self.held.pop(self.expect), now)
            self.expect = (self.expect + 1) & 0xffffffff
        self.gap_since = now if self.held else None
        return types

    def _apply(self, data, came=None):
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
            elif rtype == FRAME:
                self.screen.frame_arrival = came
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

    def send_input(self, events):
        """Mouse and key events, (kind, a, x, y) each, for a card with INPUT=1."""
        for i in range(0, len(events), 128):
            self._msg(INPUT, b"".join(struct.pack(">BBHH", k, a, max(x, 0), max(y, 0))
                                      for k, a, x, y in events[i:i + 128]))

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
                self.sprites_lost = True
            elif waited >= self.NAK_DELAY and now - self.last_nak >= self.NAK_AGAIN:
                ranges = self.missing()
                if ranges:
                    self._msg(NAK, b"".join(struct.pack(">IH", f, c) for f, c in ranges))
                    self.stats["naks"] += 1
                    self.last_nak = now
        scr = self.screen
        # the pointer's and sprites' shapes come once: lost, they must be asked for
        if scr.lacking_sprite():
            if self.lacking_since is None:
                self.lacking_since = now
        else:
            self.lacking_since = None
        if ((self.sprites_lost or (self.lacking_since is not None and
                                   now - self.lacking_since >= self.LACKING)) and
                self.gap_since is None and now - self.last_want_sprites >= 1.0):
            self._msg(WANTSPRITES)
            self.stats["want_sprites"] += 1
            self.last_want_sprites = now
            self.sprites_lost = False
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


def decode_picture(data, maxw=0, maxh=0):
    """A picture file's bytes (JPEG, PNG, GIF, BMP, ...) as (width, height,
    RGB bytes), decoded here for the Amiga; transparency over white; turned
    as its EXIF says; scaled down to fit maxw x maxh if given (a camera's
    photo is more than an Amiga wants to hold). None if it can't be."""
    try:
        from PIL import Image, ImageOps
    except ImportError:
        return None
    import io
    try:
        im = Image.open(io.BytesIO(data))
        im.load()
        im = ImageOps.exif_transpose(im)
        if im.mode in ("RGBA", "LA") or (im.mode == "P" and "transparency" in im.info):
            im = im.convert("RGBA")
            bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
            bg.alpha_composite(im)
            im = bg
        im = im.convert("RGB")
        if maxw and maxh and (im.size[0] > maxw or im.size[1] > maxh):
            im.thumbnail((maxw, maxh), Image.LANCZOS)
    except Exception:
        return None
    w, h = im.size
    if not w or not h or w > 16384 or h > 16384:
        return None
    return w, h, im.tobytes()


def decode_server(port, log=sys.stderr):  # pragma: no cover - a thread with sockets
    """Decode pictures for the Amiga (panoramapic.datatype): a TCP request
    "CUDC", version u8, codec u8, flags u8, pad u8, length u32, and from
    version 2 the largest size wanted (width u16, height u16: scaled down to
    fit), then the file; the answer "CUDR", status u8 (0 done, 1 not a
    picture we can decode, 2 too big), format u8 (0 RGB, 3 bytes a pixel),
    pad u16, width u16, height u16, then the rows. One picture a connection."""
    import socket as so
    import threading

    def serve(conn, addr):
        try:
            conn.settimeout(30)
            head = b""
            while len(head) < 12:
                part = conn.recv(12 - len(head))
                if not part:
                    return
                head += part
            if head[:4] != b"CUDC":
                return
            length = struct.unpack(">I", head[8:12])[0]
            maxw = maxh = 0
            if head[4] >= 2:
                more = b""
                while len(more) < 4:
                    part = conn.recv(4 - len(more))
                    if not part:
                        return
                    more += part
                maxw, maxh = struct.unpack(">HH", more)
            if length > 64 << 20:
                conn.sendall(b"CUDR" + struct.pack(">BBHHH", 2, 0, 0, 0, 0))
                return
            buf = bytearray()
            while len(buf) < length:
                part = conn.recv(min(1 << 16, length - len(buf)))
                if not part:
                    return
                buf += part
            t0 = time.monotonic()
            got = decode_picture(bytes(buf), maxw, maxh)
            if not got:
                conn.sendall(b"CUDR" + struct.pack(">BBHHH", 1, 0, 0, 0, 0))
                print("panoview: %s sent %d bytes that aren't a picture I can decode" %
                      (addr[0], length), file=log)
                return
            w, h, rgb = got
            conn.sendall(b"CUDR" + struct.pack(">BBHHH", 0, 0, 0, w, h) + rgb)
            print("panoview: decoded a %dx%d picture (%d KB) for %s in %.2f s%s" %
                  (w, h, length >> 10, addr[0], time.monotonic() - t0,
                   " (scaled to fit %dx%d)" % (maxw, maxh) if maxw and (w == maxw or h == maxh)
                   else ""), file=log)
        except (OSError, struct.error):
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def run():
        s = so.socket(so.AF_INET, so.SOCK_STREAM)
        s.setsockopt(so.SOL_SOCKET, so.SO_REUSEADDR, 1)
        try:
            s.bind(("", port))
            s.listen(4)
        except OSError as e:
            print("panoview: no picture decoding for the Amiga: port %d: %s" % (port, e), file=log)
            return
        while True:
            try:
                conn, addr = s.accept()
            except OSError:
                return
            threading.Thread(target=serve, args=(conn, addr), daemon=True).start()

    threading.Thread(target=run, daemon=True).start()


def sprite_rgba(data, w, rows, cols, depth=2):
    """Amiga sprite rows (planes 0 ... depth - 1, (w + 15) / 16 words each:
    2, or 4 for an attached pair) as RGBA: colour 0 clear, the others from
    cols."""
    words = (w + 15) // 16
    pb = 2 * words                      # bytes of a plane's row
    out = bytearray()
    for r in range(rows):
        row = r * depth * pb
        planes = [int.from_bytes(data[row + k * pb:row + (k + 1) * pb], "big")
                  for k in range(depth)]
        for i in range(w):
            shift = 16 * words - 1 - i
            c = 0
            for k in range(depth):
                c |= ((planes[k] >> shift) & 1) << k
            out += cols[c] + b"\xff" if c else b"\0\0\0\0"
    return bytes(out)


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
             PIXELS: "pixels", POINTERPOS: "pointer", SPRITE: "sprite", SPRITEIMAGE: "sprite image"}
    orders = ", ".join("%d %s" % (rec[t], n) for t, n in names.items() if rec.get(t))
    modes = ", ".join("%dx%d %s" % m for m in sorted(screen.stats["modes"])) or "none"
    print("panoview: %d datagrams, %d KB, %d frames; %s; %d repairs, %d NAKs, %d WANTs, "
          "%d WANTSPRITES, %d gaps, %d bad; %d of %d tiles; modes: %s" % (
              st["datagrams"], st["bytes"] >> 10, screen.frames, orders or "no records",
              st["repairs"], st["naks"], st["wants"], st["want_sprites"], st["gaps"],
              st["bad"] + screen.stats["bad"], sum(screen.valid), len(screen.valid), modes),
          file=out)
    timing = viewer.pacer.report() if viewer.pacer else None
    if timing:
        print("panoview: updates came %.1f ms off the Amiga's timing on average, and were "
              "shown %.1f ms off" % timing, file=out)


class Head:
    """One stream: a socket and its viewer, at a place on the wall."""

    def __init__(self, sock, viewer, col=0, row=0, port=0):
        self.sock, self.viewer, self.screen = sock, viewer, viewer.screen
        self.col, self.row = col, row
        self.port = port


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


def composite(heads, sprites=False):
    """All heads as one picture: (width, height, packed RGB). sprites: with
    the Amiga's sprites (SPRITES=1) drawn over it, as the web view shows it;
    without, the screens' own pixels."""
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
        if sprites:
            draw_sprites(scr, out, w, h, ox, oy)
    return w, h, bytes(out)


def draw_sprites(scr, out, w, h, ox, oy):
    """scr's shown sprites into out (packed RGB, w x h) with scr at ox, oy:
    sprite 7 first, so lower numbers end up in front, as the hardware's."""
    for _n, sx, sy, sw, sh, rgba in scr.shown_sprites():
        x0, y0 = ox + sx, oy + sy
        for y in range(sh):
            ty = y0 + y
            if ty < oy or ty >= min(h, oy + scr.height):
                continue
            for x in range(sw):
                tx = x0 + x
                i = (y * sw + x) * 4
                if rgba[i + 3] and ox <= tx < min(w, ox + scr.width):
                    o = (ty * w + tx) * 3
                    out[o:o + 3] = rgba[i:i + 3]


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
        if viewer.drop and random.random() < viewer.drop:
            continue   # --drop: lost on the way, for testing
        types += viewer.feed(data, addr)
    viewer.tick()
    return types


def _keymap():
    """Tk key names to Amiga raw key codes (a US keyboard)."""
    m = {}
    rows = [("`1234567890-=\\", 0x00), ("qwertyuiop[]", 0x10), ("asdfghjkl;'", 0x20),
            ("zxcvbnm,./", 0x31)]
    names = {"`": "grave", "-": "minus", "=": "equal", "\\": "backslash", "[": "bracketleft",
             "]": "bracketright", ";": "semicolon", "'": "apostrophe", ",": "comma",
             ".": "period", "/": "slash"}
    for keys, first in rows:
        for i, ch in enumerate(keys):
            m[names.get(ch, ch)] = first + i
    shifted = {"asciitilde": "grave", "exclam": "1", "at": "2", "numbersign": "3",
               "dollar": "4", "percent": "5", "asciicircum": "6", "ampersand": "7",
               "asterisk": "8", "parenleft": "9", "parenright": "0", "underscore": "minus",
               "plus": "equal", "bar": "backslash", "braceleft": "bracketleft",
               "braceright": "bracketright", "colon": "semicolon", "quotedbl": "apostrophe",
               "quoteright": "apostrophe", "less": "comma", "greater": "period",
               "question": "slash"}
    for k, v in shifted.items():
        m[k] = m[v]
    for ch in "abcdefghijklmnopqrstuvwxyz":
        m[ch.upper()] = m[ch]
    m.update({
        "space": 0x40, "BackSpace": 0x41, "Tab": 0x42, "ISO_Left_Tab": 0x42, "KP_Enter": 0x43,
        "Return": 0x44, "Escape": 0x45, "Delete": 0x46, "Up": 0x4C, "Down": 0x4D,
        "Right": 0x4E, "Left": 0x4F, "Help": 0x5F, "F11": 0x5F, "Insert": 0x5F,
        "Shift_L": 0x60, "Shift_R": 0x61, "Caps_Lock": 0x62, "Control_L": 0x63,
        "Control_R": 0x63, "Alt_L": 0x64, "Option_L": 0x64, "Alt_R": 0x65, "Option_R": 0x65,
        "Meta_L": 0x66, "Super_L": 0x66, "Command": 0x66, "Meta_R": 0x67, "Super_R": 0x67,
        "KP_0": 0x0F, "KP_1": 0x1D, "KP_2": 0x1E, "KP_3": 0x1F, "KP_4": 0x2D, "KP_5": 0x2E,
        "KP_6": 0x2F, "KP_7": 0x3D, "KP_8": 0x3E, "KP_9": 0x3F, "KP_Decimal": 0x3C,
        "KP_Subtract": 0x4A, "KP_Add": 0x5E, "KP_Divide": 0x5C, "KP_Multiply": 0x5D,
    })
    for i in range(10):
        m["F%d" % (i + 1)] = 0x50 + i
    return m


KEYMAP = _keymap()
WHEEL_UP, WHEEL_DOWN = 0x7A, 0x7B   # NewMouse wheel keys


class Pacer:
    """
    When to show each update: as far after the one before as it was on the
    Amiga (FRAME's time, Panorama.card 3.2), not when the network happens to
    bring it. The delay of each (arrival - Amiga time) drifts as the two
    clocks run apart (an emulator's more than a real Amiga's crystal): a
    line through the last WINDOW seconds' delays follows that, and updates
    are shown on it, plus enough for nearly all of the jitter around it
    (the 95th percentile, MAX_MARGIN at most). Late ones show at once. Also
    keeps how even the timing came and was shown.
    """
    WINDOW = 5.0
    MIN_MARGIN = 0.004
    MAX_MARGIN = 0.1

    def __init__(self):
        self.samples = collections.deque()      # (arrival, Amiga time, arrival - Amiga time)
        self.last = None                        # (Amiga time, arrival, shown) of the last shown
        self.came_err, self.shown_err = [], []  # |interval here - interval there|, s
        self.log = None                         # --timing: a file for each update's times

    def due(self, t_ms, came, learn=True):
        """When to show the update of Amiga time t_ms that came at came."""
        t = t_ms / 1000.0
        d = came - t
        if learn:
            if self.samples and abs(d - self.samples[-1][2]) > 1:
                # the Amiga's clock started again (or wrapped): from now on
                self.samples.clear()
                self.last = None
            self.samples.append((came, t, d))
            while self.samples[0][0] < came - self.WINDOW:
                self.samples.popleft()
        if not self.samples:
            return came
        # the drift: a line through the delays against the Amiga's time
        # (least squares; its times have no jitter, the delays do)
        n = len(self.samples)
        slope = 0.0
        if n >= 20:
            mx = sum(u for _, u, _ in self.samples) / n
            my = sum(x for _, _, x in self.samples) / n
            sxx = sum((u - mx) ** 2 for _, u, _ in self.samples)
            if sxx > 0:
                slope = sum((u - mx) * (x - my) for _, u, x in self.samples) / sxx
                slope = max(-0.05, min(slope, 0.05))
        # each delay as it would be at time t, on the line
        rest = sorted(x + slope * (t - u) for _, u, x in self.samples)
        base = rest[0]
        late = rest[int(0.95 * (n - 1))] - base
        margin = min(max(late, self.MIN_MARGIN), self.MAX_MARGIN)
        return t + base + margin

    def shown(self, t_ms, came, now):
        if t_ms is None:
            return
        if self.log:
            self.log.write("%d %.4f %.4f\n" % (t_ms, came, now))
        if self.last is not None and t_ms >= self.last[0]:
            dt = (t_ms - self.last[0]) / 1000.0
            self.came_err.append(abs((came - self.last[1]) - dt))
            self.shown_err.append(abs((now - self.last[2]) - dt))
            del self.came_err[:-2000], self.shown_err[:-2000]
        self.last = (t_ms, came, now)

    def report(self):
        """'... ms': how unevenly updates came, and were shown (mean error)."""
        if not self.shown_err:
            return None
        return (1000 * sum(self.came_err) / len(self.came_err),
                1000 * sum(self.shown_err) / len(self.shown_err))


class InStep:
    """
    The parts of one screen (SPLIT=) shown together. The card's parts keep
    in step and number their FRAMEs alike; each head stops at a FRAME, and
    the picture is shown when every head has stopped at the same one, or a
    moment later (a part with nothing new sends no FRAME). With pacing
    (the window), each update is shown at its time (Pacer), the newest due
    when several are; one screen stops at its FRAMEs for that too.
    """
    WAIT = 0.025

    def __init__(self, heads, pacing=False):
        self.heads = heads
        self.since = None
        self.pacer = Pacer() if pacing else None
        self.due = None                         # (frame, when) of the update waiting
        for h in heads:
            h.viewer.in_step = len(heads) > 1 or pacing

    def receive(self):
        """Take what arrived. True when a picture is ready to show."""
        framed = False
        for h in self.heads:
            if FRAME in receive_some(h.sock, h.viewer):
                framed = True
        if len(self.heads) == 1 and not self.pacer:
            return framed
        stopped = [h for h in self.heads if h.viewer.stopped]
        if not stopped:
            return False
        now = time.monotonic()
        if self.since is None:
            self.since = now
        if len(self.heads) > 1:
            # a part behind the others catches up first
            top = max(h.screen.frame for h in stopped)
            for h in stopped:
                while h.screen.frame != top and h.viewer.stopped and h.viewer.frame_waiting():
                    h.viewer.release()
            stopped = [h for h in self.heads if h.viewer.stopped]
            frames = set(h.screen.frame for h in stopped)
            if len(stopped) < len(self.heads) or len(frames) > 1:
                if now - self.since < self.WAIT:
                    return False
                # late: catch up on what is waiting, to the last whole update
                for h in stopped:
                    while h.viewer.stopped and h.viewer.frame_waiting():
                        h.viewer.release()
                return True
        first = self.heads[0].screen
        if self.pacer and first.frame_time is not None and first.frame_arrival is not None:
            # its time: wait for it; past it, and the next due too: on to that
            while True:
                if self.due is None or self.due[0] != (first.frame, first.frame_time):
                    self.due = ((first.frame, first.frame_time),
                                self.pacer.due(first.frame_time, first.frame_arrival))
                if now < self.due[1]:
                    return False
                nxt = [h.viewer.next_frame() for h in self.heads]
                if not all(n and n[0] is not None for n in nxt) or \
                        self.pacer.due(nxt[0][0], nxt[0][1], learn=False) > now:
                    return True
                for h in self.heads:
                    h.viewer.release()
                if len(set(h.screen.frame for h in self.heads)) > 1:
                    return True
        # behind (updates come faster than they are shown: a moving sprite
        # makes one each pass): on to the newest every part has whole, and
        # show that one, as a display shows the latest picture, never a late one
        while all(h.viewer.stopped and h.viewer.frame_waiting() for h in self.heads):
            for h in self.heads:
                h.viewer.release()
            if len(set(h.screen.frame for h in self.heads)) > 1:
                break
        return True

    def shown(self):
        """After showing the picture: on to the next."""
        self.since = None
        if self.pacer:
            first = self.heads[0].screen
            self.pacer.shown(first.frame_time, first.frame_arrival, time.monotonic())
        for h in self.heads:
            if h.viewer.stopped:
                h.viewer.release()


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

    step = InStep(heads)
    while end is None or time.time() < end:
        select.select([h.sock for h in heads], [], [], 0.01)
        if step.receive():
            if all(h.screen.complete() for h in heads):
                now = time.time()
                if now - last_save >= 1:
                    save()
                    last_save = now
            step.shown()
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
            "ptr_serial": -1, "at": None, "placed": False, "spr": {}, "spr_serial": -1}
           for _ in heads]
    sprite_images = {}   # RGBA bytes: PhotoImage (a spinning ball has only a few)
    state = {"wall": None}
    for h in heads:
        h.sock.setblocking(False)

    def present():
        offsets, ww, wh = wall(heads)
        if not separate and (ww, wh) != state["wall"] and ww:
            state["wall"] = (ww, wh)
            windows[0][1].configure(width=ww, height=wh)
            scr = heads[0].screen
            root.title("Panorama %dx%d %s%s" % (ww, wh, FORMAT_NAMES.get(scr.fmt, "?"),
                       ", %d screens" % len(heads) if len(heads) > 1 else ""))
        for i, (h, st) in enumerate(zip(heads, per)):
            screen = h.screen
            win, canvas = windows[i] if separate else windows[0]
            at = (0, 0) if separate else offsets[i]
            size = (screen.width, screen.height)
            if not screen.width:
                # say what is (not) arriving, in the title
                n = h.viewer.stats["datagrams"]
                text = ("Panorama screen %d (port %d): %s" % (
                    i + 1, h.port,
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
                st["spr_serial"] = -1
            if separate:
                text = "Panorama %s (port %d) %dx%d %s%s" % (
                    "screen %d of %d" % (i + 1, len(heads)), h.port, size[0], size[1], FORMAT_NAMES.get(screen.fmt, "?"),
                    " (behind: another screen is in front on the Amiga)" if screen.behind else
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
            # The Amiga's pointer: where the Amiga says it is, or, while our
            # own pointer is in this window working the Amiga, where ours is:
            # the Amiga's follows ours anyway, a round trip later, and this way
            # it is under the hand at once (the Amiga's shape, our place)
            local = state.get("local")
            local = local if local and local[0] == i and screen.takes_input and \
                not args.no_input else None
            if screen.pointer_serial != st["ptr_serial"] or st.get("ptr_local") != local:
                st["ptr_serial"] = screen.pointer_serial
                st["ptr_local"] = local
                x, y, vis = screen.pointer_pos
                if screen.pointer and local:
                    x, y = local[1] - screen.pointer[2], local[2] - screen.pointer[3]
                    vis = 1
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
            if screen.sprite_serial != st["spr_serial"]:
                # graphics.library's sprites (SPRITES=1): lower numbers in
                # front, as the hardware's; the pointer over all of them
                st["spr_serial"] = screen.sprite_serial
                shown = set()
                for n, sx, sy, sw, sh, rgba in screen.shown_sprites():
                    shown.add(n)
                    item = st["spr"].get(n)
                    img = sprite_images.get(rgba)
                    if img is None:
                        if len(sprite_images) > 2 * CU_SPRITE_IMAGES:
                            sprite_images.clear()
                        img = tk.PhotoImage(data=png_bytes(sw, sh, rgba, level=1, alpha=True),
                                            format="PNG")
                        sprite_images[rgba] = img
                    if item is None:
                        item = st["spr"][n] = canvas.create_image(0, 0, anchor="nw", image=img)
                    else:
                        canvas.itemconfigure(item, image=img, state="normal")
                    canvas.coords(item, at[0] + sx, at[1] + sy)
                    canvas.tag_raise(item)
                for n, item in st["spr"].items():
                    if n not in shown:
                        canvas.itemconfigure(item, state="hidden")
                if st["ptr_item"] is not None:
                    canvas.tag_raise(st["ptr_item"])

    state["shown"] = 0.0
    step = InStep(heads, pacing=not args.no_pacing)
    heads[0].viewer.pacer = step.pacer
    if step.pacer and args.timing:
        step.pacer.log = open(args.timing, "w", buffering=1)

    # INPUT=1: the mouse and keys in these windows work the Amiga
    pending = [[] for _ in heads]
    aqua = root.tk.call("tk", "windowingsystem") == "aqua"
    buttons = {1: 1, 2: 3 if aqua else 2, 3: 2 if aqua else 3}
    held = set()
    state["keys"] = 0
    state["cursor"] = [None] * len(heads)

    def head_at(widget, x, y):
        """The head under the pointer, and the position on its part."""
        for i, (win, canvas) in enumerate(windows):
            if canvas is widget:
                break
        else:
            return None, 0, 0
        if separate:
            return i, x, y
        for j, st in enumerate(per):
            if st["at"] is None:
                continue
            ax, ay = st["at"]
            if ax <= x < ax + st["size"][0] and ay <= y < ay + st["size"][1]:
                return j, x - ax, y - ay
        return None, 0, 0

    def add(i, kind, a, x=0, y=0):
        if i is None or args.no_input or not heads[i].screen.takes_input:
            return
        ev = pending[i]
        if kind == IN_MOVE and ev and ev[-1][0] == IN_MOVE:
            ev[-1] = (kind, a, x, y)
        else:
            ev.append((kind, a, x, y))

    def on_motion(e):
        i, x, y = head_at(e.widget, e.x, e.y)
        if i is not None:
            state["keys"] = i
            state["local"] = (i, x, y)
        add(i, IN_MOVE, 0, x, y)

    def on_button(e, up):
        i, x, y = head_at(e.widget, e.x, e.y)
        if e.num in (4, 5):
            if not up:
                code = WHEEL_UP if e.num == 4 else WHEEL_DOWN
                add(i, IN_KEY, code)
                add(i, IN_KEY, code | 0x80)
            return
        b = buttons.get(e.num)
        if b is None:
            return
        if not up:
            e.widget.focus_set()
        add(i, IN_MOVE, 0, x, y)
        add(i, IN_BUTTON, b | (0x80 if up else 0))

    def on_wheel(e):
        i, x, y = head_at(e.widget, e.x, e.y)
        code = WHEEL_UP if e.delta > 0 else WHEEL_DOWN
        add(i, IN_KEY, code)
        add(i, IN_KEY, code | 0x80)

    def on_key(e, up):
        code = KEYMAP.get(e.keysym)
        if code is None:
            return
        if up:
            if code not in held:
                return
            held.discard(code)
        else:
            held.add(code)
        add(state["keys"], IN_KEY, code | (0x80 if up else 0))
        return "break"

    canvases = [canvas for _, canvas in windows]
    state["inside"] = False
    state["pointer"] = None
    state["local"] = None            # (head, x, y): our pointer, while it is in a window

    def track_pointer():
        """Where the computer's pointer is, asked for, not waited for: a
        window that isn't the active one (on a Mac, while the emulator is)
        gets no motion events, and the Amiga's pointer should follow anyway.
        The keys stay with whatever program has them: the emulator sends
        them to the Amiga too, and they go to the window made active there.
        Out of every window here: LEAVE, the Workbench to the front."""
        if args.no_input or not any(h.screen.takes_input for h in heads):
            return
        try:
            px, py = root.winfo_pointerxy()
            w = root.winfo_containing(px, py)
        except (tk.TclError, KeyError):
            return
        if (px, py) == state["pointer"]:
            return
        state["pointer"] = (px, py)
        if w in canvases:
            state["inside"] = True
            i, x, y = head_at(w, px - w.winfo_rootx(), py - w.winfo_rooty())
            if i is not None:
                state["keys"] = i
                state["local"] = (i, x, y)
            add(i, IN_MOVE, 0, x, y)
        elif state["inside"]:
            state["inside"] = False
            state["local"] = None
            for i, h in enumerate(heads):
                if h.screen.takes_input:
                    pending[i].append((IN_LEAVE, 0, 0, 0))
                    break

    for win, canvas in windows:
        canvas.bind("<Motion>", on_motion)
        canvas.bind("<ButtonPress>", lambda e: on_button(e, False))
        canvas.bind("<ButtonRelease>", lambda e: on_button(e, True))
        canvas.bind("<MouseWheel>", on_wheel)
        win.bind("<KeyPress>", lambda e: on_key(e, False))
        win.bind("<KeyRelease>", lambda e: on_key(e, True))

    def flush_input():
        for i, h in enumerate(heads):
            if pending[i]:
                h.viewer.send_input(pending[i])
                pending[i] = []
            # the Amiga's pointer is the one to see, while the card takes ours
            want = "none" if h.screen.takes_input and not args.no_input else ""
            if state["cursor"][i] != want and (separate or i == 0):
                try:
                    windows[i if separate else 0][1].configure(cursor=want)
                except tk.TclError:
                    want = ""
                state["cursor"][i] = want

    def poll():
        # show only whole updates: when the card says one is complete (FRAME),
        # all parts of the screen at the same moment, or now and then anyway
        now = time.monotonic()
        if now - state.get("tracked", 0) > 0.03:
            state["tracked"] = now
            track_pointer()
        flush_input()
        ready = step.receive()
        now = time.monotonic()
        moved = state.get("local") != state.get("local_shown")
        if ready or moved or now - state["shown"] > 0.25:
            state["local_shown"] = state.get("local")
            present()
            state["shown"] = now
            if ready:
                step.shown()
        root.after(5, poll)

    poll()
    root.mainloop()
    for h in heads:
        print_stats(h.viewer)


WEB_PAGE = b"""<!doctype html>
<html><head><meta charset="utf-8"><title>Panorama</title>
<style>
 body { margin: 0; background: #111; color: #aaa; font: 13px sans-serif; }
 #bar { padding: 4px 8px; }
 #wrap { position: relative; margin: 0 auto; width: fit-content; }
 #s { display: block; image-rendering: pixelated; max-width: 100vw; }
 .spr { position: absolute; image-rendering: pixelated; pointer-events: none; }
</style></head>
<body><div id="bar">waiting for the Amiga...</div>
<div id="wrap"><img id="s" alt=""></div>
<script>
// The screen's pixels as one picture, fetched when they change; the Amiga's
// sprites (SPRITES=1) as pictures of their own over it, moved as they move,
// as the hardware shows them: a moving sprite costs no new screen picture.
const img = document.getElementById("s"), bar = document.getElementById("bar");
const wrap = document.getElementById("wrap"), sprites = {};
let last = -1;
function place(st) {
  const scale = st.width ? img.clientWidth / st.width : 1, seen = {};
  st.sprites.forEach((sp, z) => {
    let e = sprites[sp.id];
    if (!e) {
      e = sprites[sp.id] = document.createElement("img");
      e.className = "spr";
      wrap.appendChild(e);
    }
    const src = "sprite/" + sp.key + ".png";
    if (e.getAttribute("src") !== src) e.setAttribute("src", src);
    e.style.left = (sp.x * scale) + "px";
    e.style.top = (sp.y * scale) + "px";
    e.style.width = (sp.w * scale) + "px";
    e.style.zIndex = 1 + z;
    e.style.display = "block";
    seen[sp.id] = 1;
  });
  for (const id in sprites) if (!seen[id]) sprites[id].style.display = "none";
}
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
    place(st);
  } catch (e) { bar.textContent = "viewer stopped"; }
  setTimeout(tick, 20);
}
tick();
</script></body></html>
"""


class SpritePictures:
    """The web page's sprite pictures: each RGBA image a number, for its
    URL, that is never used for another (a browser may keep pictures)."""

    def __init__(self):
        self.keys, self.pics, self.next = {}, {}, 1

    def key(self, w, h, rgba):
        k = self.keys.get(rgba)
        if k is None:
            if len(self.keys) > 4 * CU_SPRITE_IMAGES:
                self.keys.clear()
                self.pics.clear()
            k = self.keys[rgba] = self.next
            self.pics[k] = (w, h, rgba)
            self.next += 1
        return k

    def png(self, k):
        """The picture of number k, as PNG, or None."""
        if k not in self.pics:
            return None
        w, h, rgba = self.pics[k]
        return png_bytes(w, h, rgba, level=1, alpha=True)


def web_sprites(heads, pictures):
    """The shown sprites of all heads, back to front, for the web page:
    {"id", "key" (its picture's number in pictures), "x", "y", "w", "h"}."""
    offsets, _w, _h = wall(heads)
    out = []
    for i, (hd, (ox, oy)) in enumerate(zip(heads, offsets)):
        for n, x, y, w, h, rgba in hd.screen.shown_sprites():
            out.append({"id": "%d-%d" % (i, n), "key": pictures.key(w, h, rgba),
                        "x": ox + x, "y": oy + y, "w": w, "h": h})
    return out


def run_web(heads, args):  # pragma: no cover - interactive
    """Receive in a thread; serve the screen(s) to a browser on localhost."""
    import http.server
    import json
    import select
    import threading
    import webbrowser

    lock = threading.Lock()
    state = {"version": 0, "png": None, "png_version": -1}
    pictures = SpritePictures()
    # what changes the screen's pixels: then a new picture of it
    drawing = {FRAME, SURFACE, PALETTE, FILL, INVERT, COPY, TEXT, PATTERN, PIXELS}
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
                    if drawing.intersection(types) - {FRAME}:
                        state["drawn"] = True
                    if FRAME in types and state.pop("drawn", False) or SURFACE in types:
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
                    body = {"frame": state["version"], "width": ww, "text": text,
                            "sprites": web_sprites(heads, pictures)}
                self.send("application/json", json.dumps(body).encode())
            elif path.startswith("/sprite/") and path.endswith(".png"):
                try:
                    k = int(path[len("/sprite/"):-len(".png")])
                except ValueError:
                    k = None
                with lock:
                    png = pictures.png(k)
                if png:
                    self.send("image/png", png)
                else:
                    self.send_error(404)
            elif path == "/frame.png" and "sprites=1" in self.path:
                # the picture with the sprites in it (a browser draws them itself)
                with lock:
                    w, h, rgb = composite(heads, sprites=True)
                if w:
                    self.send("image/png", png_bytes(w, h, rgb, level=1))
                else:
                    self.send_error(404)
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
    print("panoview: showing the Amiga screen at %s (Ctrl-C to stop)" % url, file=sys.stderr)
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


def head_group(group, groups, n):
    """Part n's address (from 0): its own from --groups, else counted up from --group."""
    if groups and n < len(groups) and groups[n]:
        return groups[n]
    return part_group(group, n)


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
    ap.add_argument("--groups", type=lambda s: [g.strip() for g in s.split(",")],
                    metavar="A,B,...",
                    help="each part's own group or address, in order (the card's DEST=, "
                         "DEST2=, ...); a part left out or empty counts up from --group")
    ap.add_argument("--span", type=parse_span, metavar="CxR",
                    help="several boards, a window each (like monitors), e.g. 2x1 for Wrap.card's "
                         "SPAN=2x1: ports PORT, PORT+1, ... left to right, top to bottom")
    ap.add_argument("--part", type=int, metavar="N",
                    help="one part of a SPLIT= screen: part N's group and port (the card's "
                         "DEST + N - 1, PORT + N - 1), e.g. on a second device")
    ap.add_argument("--one-window", action="store_true",
                    help="with --span: all boards in one window, not a window each")
    ap.add_argument("--decode-port", type=int, default=DECODE_PORT, metavar="PORT",
                    help="decode pictures (JPEG, PNG, ...) for the Amiga's panoramapic.datatype "
                         "on this TCP port (default %d; 0: don't)" % DECODE_PORT)
    ap.add_argument("--no-pacing", action="store_true",
                    help="show each update as it arrives, not at its time on the Amiga "
                         "(less delay, less even motion)")
    ap.add_argument("--timing", metavar="FILE",
                    help="testing: each update's Amiga time (ms), arrival and showing (s) to FILE")
    ap.add_argument("--drop", type=float, default=0.0, metavar="PERCENT",
                    help="testing: lose this percent of the datagrams, as a bad network would")
    ap.add_argument("--no-input", action="store_true",
                    help="don't send the mouse and keys to the Amiga (the card does that only "
                         "with INPUT=1 in Panorama.prefs)")
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
        sock = open_socket(head_group(args.group, args.groups, n), args.port + n, args.iface)
        heads.append(Head(sock, make_viewer(sock, Screen()), i % cols, i // cols,
                          port=args.port + n))
        heads[-1].viewer.drop = max(0.0, min(args.drop, 90.0)) / 100
    args.port += first
    if args.decode_port:
        decode_server(args.decode_port)
    try:
        if args.snapshot:
            run_headless(heads, args)
        elif args.web is not None:
            run_web(heads, args)
        else:
            try:
                import tkinter  # noqa: F401
            except ImportError:
                print("panoview: no tkinter in this Python (on macOS with Homebrew: "
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
