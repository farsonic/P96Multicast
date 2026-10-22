#!/usr/bin/env python3
"""P96Multicast viewer: shows the screen an Amiga's Multicast.card sends.

    p96view.py                              join 239.255.96.1:9696, show a window
    p96view.py --group 239.255.96.2 --port 9700
    p96view.py --snapshot out.png --duration 60   no window: save frames as PNG
    p96view.py --web                        show it in a web browser instead

The protocol is described in docs/PROTOCOL.md. Uses only the standard
library (tkinter for the window, or a web browser when there is no tkinter);
numpy is used when it is installed.
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

MAGIC = 0x5039364D
MODE, PALETTE, TILE, SYNC = 1, 2, 3, 4
ENC_RAW, ENC_SOLID = 0, 1
FLAG_REFRESH = 1

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


class Screen:
    """The receiver's copy of the Amiga screen, in the card's own format."""

    def __init__(self):
        self.stream = None
        self.width = self.height = 0
        self.fmt = 0
        self.bpp = 0
        self.display = 0
        self.pixels = bytearray()
        self.palette = bytearray(256 * 3)
        self.tilesize = (0, 0)
        self.seen = set()          # tile origins received since the last mode
        self.frames = 0
        self.stats = {"packets": 0, "bytes": 0, "tiles": 0, "refresh": 0,
                      "solid": 0, "bad": 0, "modes": set()}
        self._lut16 = {}

    def complete(self):
        """True once every tile of the screen has arrived at least once."""
        tw, th = self.tilesize
        if not self.width or not tw:
            return False
        need = ((self.width + tw - 1) // tw) * ((self.height + th - 1) // th)
        return len(self.seen) >= need

    def handle(self, data):
        """Process one datagram. Returns the packet type, or None if ignored."""
        st = self.stats
        st["packets"] += 1
        st["bytes"] += len(data)
        if len(data) < 16:
            st["bad"] += 1
            return None
        magic, version, ptype, flags, stream, frame = struct.unpack(">IBBHII", data[:16])
        if magic != MAGIC or version != 1:
            st["bad"] += 1
            return None
        body = data[16:]
        if stream != self.stream:
            if ptype != MODE:
                return None  # wait for the new stream's MODE
            self.stream = stream
            self.width = 0
        if ptype == MODE:
            self._mode(body)
        elif ptype == PALETTE:
            first, count = struct.unpack(">HH", body[:4])
            count = min(count, 256 - first, (len(body) - 4) // 3)
            self.palette[first * 3:(first + count) * 3] = body[4:4 + count * 3]
        elif ptype == TILE:
            if not self.width:
                return None
            self._tile(body, flags)
        elif ptype == SYNC:
            self.frames += 1
        return ptype

    def _mode(self, b):
        w, h, fmt, bpp, display = struct.unpack(">HHBBB", b[:7])
        tw, th = struct.unpack(">HH", b[8:12])
        self.stats["modes"].add((w, h, FORMAT_NAMES.get(fmt, str(fmt))))
        self.display = display
        if (w, h, fmt) != (self.width, self.height, self.fmt) or not self.pixels:
            self.width, self.height, self.fmt, self.bpp = w, h, fmt, bpp
            self.pixels = bytearray(w * h * bpp)
            self.seen = set()
        self.tilesize = (tw, th)

    def _tile(self, b, flags):
        x, y, w, h, enc = struct.unpack(">HHHHB", b[:9])
        data = b[10:]
        bpp = self.bpp
        if x + w > self.width or y + h > self.height or not w or not h:
            self.stats["bad"] += 1
            return
        row = w * bpp
        if enc == ENC_SOLID:
            data = bytes(data[:bpp]) * (w * h)
            self.stats["solid"] += 1
        elif enc != ENC_RAW or len(data) < row * h:
            self.stats["bad"] += 1
            return
        stride = self.width * bpp
        for r in range(h):
            o = (y + r) * stride + x * bpp
            self.pixels[o:o + row] = data[r * row:(r + 1) * row]
        self.seen.add((x, y))
        self.stats["tiles"] += 1
        if flags & FLAG_REFRESH:
            self.stats["refresh"] += 1

    def rgb(self):
        """The screen as packed 8 bit RGB, width * height * 3 bytes."""
        if not self.width:
            return b""
        if not self.display:
            return bytes(self.width * self.height * 3)
        fmt, px = self.fmt, self.pixels
        if np is not None:
            return self._rgb_numpy()
        if fmt == CLUT:
            pal = [bytes(self.palette[i * 3:i * 3 + 3]) for i in range(256)]
            return b"".join(pal[p] for p in px)
        if fmt in BYTE_ORDER:
            ri, gi, bi = BYTE_ORDER[fmt]
            n = self.bpp
            out = bytearray(self.width * self.height * 3)
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

    def _rgb_numpy(self):
        fmt, w, h = self.fmt, self.width, self.height
        px = np.frombuffer(bytes(self.pixels), dtype=np.uint8)
        if fmt == CLUT:
            pal = np.frombuffer(bytes(self.palette), dtype=np.uint8).reshape(256, 3)
            return pal[px].tobytes()
        if fmt in BYTE_ORDER:
            p = px.reshape(-1, self.bpp)
            return p[:, list(BYTE_ORDER[fmt])].tobytes()
        lut = self._lut16.get(("np", fmt))
        if lut is None:
            lut = np.array([rgb16(fmt, v) for v in range(65536)], dtype=np.uint8)
            self._lut16[("np", fmt)] = lut
        vals = px.view(">u2")
        return lut[vals].reshape(h * w * 3).tobytes()


def png_bytes(width, height, rgb, level=6):
    """Encode 8 bit RGB as a PNG with the standard library only."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    row = width * 3
    raw = b"".join(b"\x00" + rgb[y * row:(y + 1) * row] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, level))
            + chunk(b"IEND", b""))


def write_png(path, width, height, rgb):
    import os
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(png_bytes(width, height, rgb))
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


def print_stats(screen, out=sys.stderr):
    st = screen.stats
    modes = ", ".join("%dx%d %s" % m for m in sorted(st["modes"])) or "none"
    print("p96view: %d packets, %d bytes, %d tiles (%d solid, %d refresh), %d frames, "
          "%d bad; modes: %s" % (st["packets"], st["bytes"], st["tiles"], st["solid"],
                                 st["refresh"], screen.frames, st["bad"], modes), file=out)


def run_headless(sock, screen, args):
    """Save the latest complete frame to args.snapshot, at most once a second."""
    end = time.time() + args.duration if args.duration else None
    last_save = 0
    sock.settimeout(0.5)
    while end is None or time.time() < end:
        try:
            data = sock.recv(65536)
        except socket.timeout:
            continue
        if screen.handle(data) == SYNC and screen.complete():
            now = time.time()
            if now - last_save >= 1:
                write_png(args.snapshot, screen.width, screen.height, screen.rgb())
                last_save = now
    if screen.complete():
        write_png(args.snapshot, screen.width, screen.height, screen.rgb())
    print_stats(screen)


def run_window(sock, screen, args):  # pragma: no cover - interactive
    import tkinter as tk

    root = tk.Tk()
    root.title("P96Multicast")
    label = tk.Label(root, bg="black")
    label.pack()
    state = {"img": None, "size": None, "dirty": False}
    sock.setblocking(False)

    def poll():
        for _ in range(2000):
            try:
                data = sock.recv(65536)
            except BlockingIOError:
                break
            if screen.handle(data) in (SYNC, MODE, PALETTE):
                state["dirty"] = True
        if state["dirty"] and screen.width:
            size = (screen.width, screen.height)
            ppm = b"P6 %d %d 255\n" % size + screen.rgb()
            state["img"] = tk.PhotoImage(data=ppm, format="PPM")
            label.configure(image=state["img"])
            if size != state["size"]:
                root.title("P96Multicast %dx%d %s" % (size[0], size[1],
                           FORMAT_NAMES.get(screen.fmt, "?")))
                state["size"] = size
            state["dirty"] = False
        root.after(20, poll)

    poll()
    root.mainloop()
    print_stats(screen)


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
  setTimeout(tick, 80);
}
tick();
</script></body></html>
"""


def run_web(sock, screen, args):  # pragma: no cover - interactive
    """Receive in a thread; serve the screen to a browser on localhost."""
    import http.server
    import json
    import threading
    import webbrowser

    lock = threading.Lock()
    state = {"version": 0, "png": None, "png_version": -1}

    def receive():
        sock.settimeout(0.5)
        while True:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            with lock:
                if screen.handle(data) in (SYNC, MODE, PALETTE):
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
                    st = screen.stats
                    text = ("%dx%d %s, %d frames, %d KB received" % (
                        screen.width, screen.height, FORMAT_NAMES.get(screen.fmt, "?"),
                        screen.frames, st["bytes"] >> 10)
                        if screen.width else "waiting for the Amiga (%d packets so far)"
                        % st["packets"])
                    body = {"frame": state["version"], "width": screen.width, "text": text}
                self.send("application/json", json.dumps(body).encode())
            elif path == "/frame.png":
                with lock:
                    if state["png_version"] != state["version"] and screen.width:
                        state["png"] = png_bytes(screen.width, screen.height,
                                                 screen.rgb(), level=1)
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--group", default="239.255.96.1",
                    help="multicast group to join (a unicast address just binds)")
    ap.add_argument("--port", type=int, default=9696)
    ap.add_argument("--iface", help="local address of the interface to join on")
    ap.add_argument("--snapshot", help="no window: write the screen to this PNG")
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
    sock = open_socket(args.group, args.port, args.iface)
    screen = Screen()
    try:
        if args.snapshot:
            run_headless(sock, screen, args)
        elif args.web is not None:
            run_web(sock, screen, args)
        else:
            try:
                import tkinter  # noqa: F401
            except ImportError:
                print("p96view: no tkinter in this Python (on macOS with Homebrew: "
                      "brew install python-tk), using the web view", file=sys.stderr)
                args.web = 8096
                run_web(sock, screen, args)
            else:
                run_window(sock, screen, args)
    except KeyboardInterrupt:
        print_stats(screen)
    return 0


if __name__ == "__main__":
    sys.exit(main())
