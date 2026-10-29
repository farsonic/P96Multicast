#!/usr/bin/env python3
"""A viewer that joins late and never asks for the whole screen (no WANT):
the sprites it hears of point at images sent before it came, so it must
get them with WANTSPRITES alone. For emu/sprite_test.sh.

    sprite_join.py GROUP PORT IFACE SECONDS

Prints "sprites N wantsprites M images K": N sprites shown at the end.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "client"))
import panoview as pv  # noqa: E402


def main():
    group, port, iface, secs = sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4])
    sock = pv.open_socket(group, port, iface)
    sock.setblocking(False)
    viewer = pv.make_viewer(sock, pv.Screen())
    send = viewer._msg

    def no_want(mtype, payload=b""):
        if mtype != pv.WANT:
            send(mtype, payload)
    viewer._msg = no_want
    end = time.monotonic() + secs
    while time.monotonic() < end:
        pv.receive_some(sock, viewer)
        time.sleep(0.02)
    scr = viewer.screen
    print("sprites %d wantsprites %d images %d" % (
        len(scr.shown_sprites()), viewer.stats["want_sprites"],
        sum(1 for i in scr.sprite_images.values() if i["rgba"])))


if __name__ == "__main__":
    main()
