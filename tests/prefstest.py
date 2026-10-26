#!/usr/bin/env python3
"""Run the built MulticastPrefs under vamos and check what it writes.

    tests/prefstest.py [VAMOS]          (make check)

Covers the command line: SET (into ENV: and ENVARC:, keeping comments and
unknown lines, reusing a commented-out line, refusing bad values), USE,
unsetting, BOARDS following Wrap.card's heads, SHOW, and INSTALL (cards
copied, monitor made from AROS's P96 monitor, the card's own monitor moved
to SYS:Storage/Monitors when Wrap.card takes over). vamos has no
icon.library, so the monitor icons are left to the emulator test.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VAMOS = (sys.argv[1] if len(sys.argv) > 1 else "vamos -C 68020").split()
DIR = os.path.join(ROOT, "build", "prefstest.d")
fails = 0


def check(ok, what):
    global fails
    print("%s: %s" % ("ok" if ok else "FAIL", what))
    if not ok:
        fails += 1


def setup():
    shutil.rmtree(DIR, ignore_errors=True)
    for d in ("env", "envarc", "libs", "devs/Monitors", "sys"):
        os.makedirs(os.path.join(DIR, d))


def run(*args, cwd=None):
    cmd = VAMOS + (["--cwd", cwd] if cwd else []) + ["-V", "p96:" + ROOT,
                   "-a", "ENV:p96:build/prefstest.d/env",
                   "-a", "ENVARC:p96:build/prefstest.d/envarc",
                   "-a", "LIBS:p96:build/prefstest.d/libs",
                   "-a", "DEVS:p96:build/prefstest.d/devs",
                   "-a", "SYS:p96:build/prefstest.d/sys",
                   "build/MulticastPrefs"] + list(args)
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    out = "\n".join(l for l in p.stdout.splitlines())
    return p.returncode, out


def read(where, name):
    path = os.path.join(DIR, where, name)
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return f.read().splitlines()


def write(where, name, text):
    with open(os.path.join(DIR, where, name), "w") as f:
        f.write(text)


def main():
    setup()
    rc, out = run("SET", "DEST=239.1.2.3", "FPS=10", "MODES=640x480,1280x960")
    check(rc == 0, "SET runs")
    for where in ("env", "envarc"):
        lines = read(where, "Multicast.prefs") or []
        check(lines == ["DEST=239.1.2.3", "FPS=10", "MODES=640x480,1280x960"],
              "SET writes %s: %r" % (where.upper() + ":", lines))
    check(read("env", "Wrap.prefs") is None, "Wrap.prefs not written when untouched")

    # an existing file: comments, unknown lines and order survive
    write("env", "Multicast.prefs", "# mine\nFOO=bar\n#FPS=25\nPORT=9000\n")
    rc, out = run("SET", "FPS=12", "PORT=9001", "USE")
    lines = read("env", "Multicast.prefs")
    check(lines == ["# mine", "FOO=bar", "FPS=12", "PORT=9001"],
          "comments and unknown lines kept, #FPS= reused: %r" % lines)
    check(read("envarc", "Multicast.prefs") != lines, "USE leaves ENVARC: alone")

    rc, out = run("SET", "FPS=99")
    check(rc != 0 and "FPS must be a number from 1 to 50" in out and "FPS FPS" not in out,
          "FPS=99 refused: %r" % out)
    check(read("env", "Multicast.prefs") == lines, "... and nothing written")
    for bad in ("DEST=1.2.3", "MODES=640x", "SPAN=3x2", "NOSUCH=1", "LOG=a b"):
        rc, out = run("SET", bad)
        check(rc != 0, "%s refused (%s)" % (bad, out.strip()))

    # Wrap.card with Multicast heads: BOARDS follows
    rc, out = run("SET", "CARD=Multicast.card", "SPAN=2x2", "HEAD=Multicast.card")
    wrap = read("env", "Wrap.prefs")
    check(wrap == ["CARD=Multicast.card", "SPAN=2x2", "HEAD=Multicast.card"],
          "Wrap.prefs: %r" % wrap)
    check("BOARDS=5" in (read("env", "Multicast.prefs") or []) and "BOARDS=5" in out,
          "BOARDS=5 for the big screen and four Multicast heads")
    rc, out = run("SET", "HEAD2=uaegfx.card")
    check("BOARDS=5" in (read("env", "Multicast.prefs") or []),
          "a real-card head leaves BOARDS alone (4 would do, 5 is still enough)")

    rc, out = run("SET", "SPAN=", "MIRROR=Multicast.card")
    wrap = read("env", "Wrap.prefs")
    check("#SPAN=2x2" in wrap and "MIRROR=Multicast.card" in wrap,
          "SPAN= unsets (commented out): %r" % wrap)

    rc, out = run("SHOW")
    check("CARD       Multicast.card" in out and "TTL        1" in out and "(default)" in out,
          "SHOW lists values and defaults")

    # INSTALL: an AROS-like system with Devs:Monitors/P96 and the card's own monitor
    write("devs/Monitors", "P96", "monitor program")
    write("devs/Monitors", "Multicast", "monitor program")
    rc, out = run("INSTALL", "DRY", "FROM=p96:build")
    check("Would do:" in out and not os.path.exists(os.path.join(DIR, "libs", "Picasso96")),
          "INSTALL DRY changes nothing")
    rc, out = run("INSTALL", "FROM=p96:build")
    print("    " + out.replace("\n", "\n    "))
    libs = os.path.join(DIR, "libs", "Picasso96")
    check(os.path.exists(os.path.join(libs, "Multicast.card")) and
          os.path.exists(os.path.join(libs, "Wrap.card")), "cards copied to LIBS:Picasso96")
    with open(os.path.join(ROOT, "build", "Multicast.card"), "rb") as a, \
            open(os.path.join(libs, "Multicast.card"), "rb") as b:
        check(a.read() == b.read(), "... intact")
    mon = os.path.join(DIR, "devs", "Monitors")
    check(os.path.exists(os.path.join(mon, "Wrap")), "DEVS:Monitors/Wrap made (CARD= is set)")
    check(not os.path.exists(os.path.join(mon, "Multicast")) and
          os.path.exists(os.path.join(DIR, "sys", "Storage", "Monitors", "Multicast")),
          "DEVS:Monitors/Multicast moved to SYS:Storage/Monitors (Wrap.card sets it up)")
    check(read("envarc", "Wrap.prefs") == read("env", "Wrap.prefs"), "INSTALL saves to ENVARC:")

    # from a shell that gives the program no PROGDIR: of its own (bash):
    # the package's Libs/Picasso96 in the current directory
    shutil.rmtree(libs)
    pkg = os.path.join(DIR, "pkg", "Libs", "Picasso96")
    os.makedirs(pkg)
    for c in ("Multicast.card", "Wrap.card"):
        shutil.copy(os.path.join(ROOT, "build", c), pkg)
    rc, out = run("INSTALL", cwd="p96:build/prefstest.d/pkg")
    check("Copied Libs/Picasso96/Multicast.card to LIBS:Picasso96/Multicast.card" in out,
          "the cards found in the current directory's Libs/Picasso96: %r" % out.splitlines()[:2])
    # a copy onto itself is no copy: FROM= the installed cards
    size = os.path.getsize(os.path.join(libs, "Multicast.card"))
    rc, out = run("INSTALL", "FROM=LIBS:Picasso96")
    check("already in LIBS:Picasso96" in out and "Could not copy" not in out and
          os.path.getsize(os.path.join(libs, "Multicast.card")) == size,
          "FROM= the installed cards: left alone, not copied onto themselves")

    print("prefstest: %s" % ("FAILED" if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
