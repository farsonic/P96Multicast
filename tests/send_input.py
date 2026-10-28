#!/usr/bin/env python3
"""Work the Amiga from here, as a viewer would (Copper INPUT, INPUT=1).

    send_input.py --group G --port P [--iface IP] STEP...

Listens on the part's group and port until the card is heard from (its
address and stream), then sends each STEP:

    move:X,Y    the pointer to X,Y on this part of the screen
    click:B     button B (1 left, 2 right, 3 middle) pressed and released
    type:TEXT   the keys for TEXT (a US keyboard; \\n is Return)
    wait:S      S seconds

Then prints where the card says its pointer is (POINTERPOS), as
"pointer X Y", for a second.
"""
import argparse
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "client"))
import panoview  # noqa: E402

SHIFTED = "~!@#$%^&*()_+|{}:\"<>?ABCDEFGHIJKLMNOPQRSTUVWXYZ"
NAMES = {" ": "space", "\n": "Return", "`": "grave", "-": "minus", "=": "equal",
         "\\": "backslash", "[": "bracketleft", "]": "bracketright", ";": "semicolon",
         "'": "apostrophe", ",": "comma", ".": "period", "/": "slash", "~": "asciitilde",
         "!": "exclam", "@": "at", "#": "numbersign", "$": "dollar", "%": "percent",
         "^": "asciicircum", "&": "ampersand", "*": "asterisk", "(": "parenleft",
         ")": "parenright", "_": "underscore", "+": "plus", "|": "bar", "{": "braceleft",
         "}": "braceright", ":": "colon", '"': "quotedbl", "<": "less", ">": "greater",
         "?": "question"}


def keys_for(text):
    ev = []
    for ch in text:
        code = panoview.KEYMAP[NAMES.get(ch, ch)]
        shift = ch in SHIFTED
        if shift:
            ev.append((panoview.IN_KEY, 0x60, 0, 0))
        ev += [(panoview.IN_KEY, code, 0, 0), (panoview.IN_KEY, code | 0x80, 0, 0)]
        if shift:
            ev.append((panoview.IN_KEY, 0x60 | 0x80, 0, 0))
    return ev


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--group", required=True)
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--iface")
    ap.add_argument("steps", nargs="+")
    args = ap.parse_args()

    sock = panoview.open_socket(args.group, args.port, args.iface)
    sock.settimeout(10)
    amiga = stream = None
    while amiga is None:
        data, addr = sock.recvfrom(65536)
        if len(data) >= 12 and struct.unpack(">H", data[:2])[0] == panoview.MAGIC:
            amiga, stream = addr, struct.unpack(">I", data[4:8])[0]
    print("card at %s:%d" % amiga)

    def send(events):
        for i in range(0, len(events), 100):
            sock.sendto(struct.pack(">HBBI", panoview.MAGIC, panoview.VERSION, panoview.INPUT,
                                    stream) +
                        b"".join(struct.pack(">BBHH", *e) for e in events[i:i + 100]), amiga)
            time.sleep(0.05)

    for step in args.steps:
        kind, _, arg = step.partition(":")
        if kind == "move":
            x, y = (int(v) for v in arg.split(","))
            send([(panoview.IN_MOVE, 0, x, y)])
        elif kind == "click":
            b = int(arg)
            send([(panoview.IN_BUTTON, b, 0, 0)])
            time.sleep(0.1)
            send([(panoview.IN_BUTTON, b | 0x80, 0, 0)])
        elif kind == "type":
            send(keys_for(arg.replace("\\n", "\n")))
        elif kind == "wait":
            time.sleep(float(arg))
        else:
            sys.exit("send_input: what is %r?" % step)
        time.sleep(0.2)

    # where the card has its pointer now
    end = time.time() + 1.5
    sock.settimeout(0.2)
    last = None
    while time.time() < end:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            continue
        pos, n = 12, len(data)
        while pos + 3 <= n:
            rtype, length = data[pos], (data[pos + 1] << 8) | data[pos + 2]
            if rtype == panoview.POINTERPOS and length >= 6:
                last = struct.unpack(">hhB", data[pos + 3:pos + 8])
            pos += 3 + length
    if last:
        print("pointer %d %d%s" % (last[0], last[1], "" if last[2] else " (hidden)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
