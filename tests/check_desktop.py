#!/usr/bin/env python3
"""Check emu/desktop_test.sh's pictures of the shared desktop.

    check_desktop.py OUTDIR

OUTDIR/at-*.png are what the viewer showed of both monitors (two 320x480
halves side by side) at each step: the desktop open, Boing bouncing
(boing1 ... boing10), and a 200x120 window at 20,40 (left), moved one
monitor right (right), and to the middle of monitor 1 (centre).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_frame import read_png  # noqa: E402

HALF = 320


def load(out, name):
    path = os.path.join(out, "at-%s.png" % name)
    if not os.path.exists(path):
        return None
    w, h, rgb = read_png(path)
    return w, h, rgb


def red_per_half(pic):
    w, h, rgb = pic
    left = right = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            o = (y * w + x) * 3
            r, g, b = rgb[o], rgb[o + 1], rgb[o + 2]
            if r > 170 and g < 90 and b < 90:
                if x < HALF:
                    left += 1
                else:
                    right += 1
    return left, right


def changed_box(a, b):
    """The bounding box of the pixels that differ (below the title bar)."""
    w, h, ra = a
    rb = b[2]
    xs, ys = [], []
    for y in range(20, h):
        for x in range(w):
            o = (y * w + x) * 3
            if ra[o:o + 3] != rb[o:o + 3]:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def main():
    out = sys.argv[1]
    fails = 0

    def check(ok, what):
        nonlocal fails
        print("%s: %s" % ("PASS" if ok else "FAIL", what))
        if not ok:
            fails += 1

    base = load(out, "open")
    check(base is not None and base[0] == 2 * HALF, "the desktop: two monitors, %s" %
          ("%dx%d" % base[:2] if base else "no picture"))
    if base is None:
        return 1

    seen = []
    for i in range(1, 11):
        pic = load(out, "boing%d" % i)
        if pic:
            seen.append(red_per_half(pic))
    print("Boing, red pixels per monitor at each step: %s" % seen)
    # one ball, not a trail of them: the red pixels fit in one ball's square (60)
    spans = []
    for i in range(1, 11):
        pic = load(out, "boing%d" % i)
        if not pic:
            continue
        w, h, rgb = pic
        xs, ys = [], []
        for y in range(0, h, 2):
            for x in range(0, w, 2):
                o = (y * w + x) * 3
                if rgb[o] > 170 and rgb[o + 1] < 90 and rgb[o + 2] < 90:
                    xs.append(x)
                    ys.append(y)
        if xs:
            spans.append((max(xs) - min(xs), max(ys) - min(ys)))
    print("Boing, extent of the red at each step: %s" % spans)
    check(spans and all(a <= 66 and b <= 66 for a, b in spans), "one ball, no trail")
    check(any(l > 50 for l, r in seen), "the ball on monitor 1")
    check(any(r > 50 for l, r in seen), "the ball on monitor 2")

    boxes = {}
    for step in ("left", "right", "centre"):
        pic = load(out, step)
        boxes[step] = changed_box(base, pic) if pic else None
        print("window, %s: %s" % (step, boxes[step]))
    lb, rb, cb = boxes["left"], boxes["right"], boxes["centre"]
    check(lb is not None and lb[0] < 40 and lb[2] < HALF, "the window on monitor 1")
    check(rb is not None and lb is not None and abs(rb[0] - (lb[0] + HALF)) <= 4 and
          abs(rb[1] - lb[1]) <= 4, "MOVE=RIGHT: the same place on monitor 2")
    check(cb is not None and cb[2] < HALF and abs((cb[0] + cb[2]) // 2 - HALF // 2) <= 6,
          "CENTRE=1: the middle of monitor 1")
    # SHELL: a Shell in the middle of the whole desktop (after the window closed)
    pic = load(out, "shell")
    sb = changed_box(base, pic) if pic else None
    print("Shell: %s" % (sb,))
    check(sb is not None and abs((sb[0] + sb[2]) // 2 - HALF) <= 6 and
          abs((sb[1] + sb[3]) // 2 - 240) <= 12, "SHELL: a Shell in the middle of the desktop")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
