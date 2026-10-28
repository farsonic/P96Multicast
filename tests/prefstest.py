#!/usr/bin/env python3
"""Run the built PanoramaPrefs under vamos and check what it writes.

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
    for d in ("env", "envarc", "libs", "devs/Monitors", "sys", "c"):
        os.makedirs(os.path.join(DIR, d))


def run(*args, cwd=None):
    cmd = VAMOS + (["--cwd", cwd] if cwd else []) + ["-V", "p96:" + ROOT,
                   "-a", "ENV:p96:build/prefstest.d/env",
                   "-a", "ENVARC:p96:build/prefstest.d/envarc",
                   "-a", "LIBS:p96:build/prefstest.d/libs",
                   "-a", "DEVS:p96:build/prefstest.d/devs",
                   "-a", "SYS:p96:build/prefstest.d/sys",
                   "-a", "C:p96:build/prefstest.d/c",
                   "build/PanoramaPrefs"] + list(args)
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
        lines = read(where, "Panorama.prefs") or []
        check(lines == ["DEST=239.1.2.3", "FPS=10", "MODES=640x480,1280x960"],
              "SET writes %s: %r" % (where.upper() + ":", lines))
    check(read("env", "Wrap.prefs") is None, "Wrap.prefs not written when untouched")

    # an existing file: comments, unknown lines and order survive
    write("env", "Panorama.prefs", "# mine\nFOO=bar\n#FPS=25\nPORT=9000\n")
    rc, out = run("SET", "FPS=12", "PORT=9001", "USE")
    lines = read("env", "Panorama.prefs")
    check(lines == ["# mine", "FOO=bar", "FPS=12", "PORT=9001"],
          "comments and unknown lines kept, #FPS= reused: %r" % lines)
    check(read("envarc", "Panorama.prefs") != lines, "USE leaves ENVARC: alone")

    rc, out = run("SET", "FPS=99")
    check(rc != 0 and "FPS must be a number from 1 to 50" in out and "FPS FPS" not in out,
          "FPS=99 refused: %r" % out)
    check(read("env", "Panorama.prefs") == lines, "... and nothing written")
    for bad in ("DEST=1.2.3", "MODES=640x", "SPAN=3x2", "NOSUCH=1", "LOG=a b"):
        rc, out = run("SET", bad)
        check(rc != 0, "%s refused (%s)" % (bad, out.strip()))

    # Wrap.card with Panorama heads: BOARDS follows
    rc, out = run("SET", "CARD=Panorama.card", "SPAN=2x2", "HEAD=Panorama.card")
    wrap = read("env", "Wrap.prefs")
    check(wrap == ["CARD=Panorama.card", "SPAN=2x2", "HEAD=Panorama.card"],
          "Wrap.prefs: %r" % wrap)
    check("BOARDS=5" in (read("env", "Panorama.prefs") or []) and "BOARDS=5" in out,
          "BOARDS=5 for the big screen and four Panorama heads")
    rc, out = run("SET", "HEAD2=uaegfx.card")
    check("BOARDS=5" in (read("env", "Panorama.prefs") or []),
          "a real-card head leaves BOARDS alone (4 would do, 5 is still enough)")

    rc, out = run("SET", "SPAN=", "MIRROR=Panorama.card")
    wrap = read("env", "Wrap.prefs")
    check("#SPAN=2x2" in wrap and "MIRROR=Panorama.card" in wrap,
          "SPAN= unsets (commented out): %r" % wrap)

    rc, out = run("SHOW")
    check("CARD       Panorama.card" in out and "TTL        1" in out and "(default)" in out,
          "SHOW lists values and defaults")

    # INSTALL: an AROS-like system with Devs:Monitors/P96 and the card's own monitor
    write("devs/Monitors", "P96", "monitor program")
    write("devs/Monitors", "Panorama", "monitor program")
    rc, out = run("INSTALL", "DRY", "FROM=p96:build")
    check("Would do:" in out and not os.path.exists(os.path.join(DIR, "libs", "Picasso96")),
          "INSTALL DRY changes nothing")
    rc, out = run("INSTALL", "FROM=p96:build")
    print("    " + out.replace("\n", "\n    "))
    libs = os.path.join(DIR, "libs", "Picasso96")
    check(os.path.exists(os.path.join(libs, "Panorama.card")) and
          os.path.exists(os.path.join(libs, "Wrap.card")), "cards copied to LIBS:Picasso96")
    with open(os.path.join(ROOT, "build", "Panorama.card"), "rb") as a, \
            open(os.path.join(libs, "Panorama.card"), "rb") as b:
        check(a.read() == b.read(), "... intact")
    mon = os.path.join(DIR, "devs", "Monitors")
    check(os.path.exists(os.path.join(mon, "Wrap")), "DEVS:Monitors/Wrap made (CARD= is set)")
    check(not os.path.exists(os.path.join(mon, "Panorama")) and
          os.path.exists(os.path.join(DIR, "sys", "Storage", "Monitors", "Panorama")),
          "DEVS:Monitors/Panorama moved to SYS:Storage/Monitors (Wrap.card sets it up)")
    check(read("envarc", "Wrap.prefs") == read("env", "Wrap.prefs"), "INSTALL saves to ENVARC:")

    # from a shell that gives the program no PROGDIR: of its own (bash):
    # the package's Libs/Picasso96 in the current directory
    shutil.rmtree(libs)
    pkg = os.path.join(DIR, "pkg", "Libs", "Picasso96")
    os.makedirs(pkg)
    for c in ("Panorama.card", "Wrap.card"):
        shutil.copy(os.path.join(ROOT, "build", c), pkg)
    os.makedirs(os.path.join(DIR, "pkg", "C"))
    for t in ("mctest", "wrapstat"):
        shutil.copy(os.path.join(ROOT, "build", t), os.path.join(DIR, "pkg", "C"))
    write("c", "mctest", "an old mctest")
    rc, out = run("INSTALL", cwd="p96:build/prefstest.d/pkg")
    check("Copied Libs/Picasso96/Panorama.card to LIBS:Picasso96/Panorama.card" in out,
          "the cards found in the current directory's Libs/Picasso96: %r" % out.splitlines()[:2])
    with open(os.path.join(ROOT, "build", "mctest"), "rb") as a, \
            open(os.path.join(DIR, "c", "mctest"), "rb") as b:
        check(a.read() == b.read() and os.path.exists(os.path.join(DIR, "c", "wrapstat")),
              "mctest and wrapstat copied to C:, the old mctest replaced")
    # a copy onto itself is no copy: FROM= the installed cards
    size = os.path.getsize(os.path.join(libs, "Panorama.card"))
    rc, out = run("INSTALL", "FROM=LIBS:Picasso96")
    check("already in LIBS:Picasso96" in out and "Could not copy" not in out and
          os.path.getsize(os.path.join(libs, "Panorama.card")) == size,
          "FROM= the installed cards: left alone, not copied onto themselves")

    # SETUP=: a fresh system with P96's uaegfx, installing from the package
    setup()
    os.makedirs(os.path.join(DIR, "libs", "Picasso96"))
    os.makedirs(os.path.join(DIR, "sys", "Prefs"))
    write("libs/Picasso96", "uaegfx.card", "uaegfx")
    write("devs/Monitors", "uaegfx", "monitor program")
    write("devs/Monitors", "P96", "monitor program")
    pkgdir = os.path.join(DIR, "pkg")
    os.makedirs(os.path.join(pkgdir, "Libs", "Picasso96"))
    os.makedirs(os.path.join(pkgdir, "C"))
    for c in ("Panorama.card", "Wrap.card"):
        shutil.copy(os.path.join(ROOT, "build", c), os.path.join(pkgdir, "Libs", "Picasso96"))
    for t in ("mctest", "wrapstat"):
        shutil.copy(os.path.join(ROOT, "build", t), os.path.join(pkgdir, "C"))
    shutil.copy(os.path.join(ROOT, "build", "PanoramaPrefs"), pkgdir)
    mon = os.path.join(DIR, "devs", "Monitors")
    store = os.path.join(DIR, "sys", "Storage", "Monitors")

    rc, out = run("INSTALL", "SETUP=MIRROR", cwd="p96:build/prefstest.d/pkg")
    print("    " + out.replace("\n", "\n    "))
    wrap = read("envarc", "Wrap.prefs") or []
    check(rc == 0 and "CARD=uaegfx.card" in wrap and "MIRROR=Panorama.card" in wrap and
          "HEADSHARE=1" in wrap, "SETUP=MIRROR: uaegfx.card found and mirrored: %r" % wrap)
    check(os.path.exists(os.path.join(mon, "Wrap")) and
          not os.path.exists(os.path.join(mon, "uaegfx")) and
          os.path.exists(os.path.join(store, "uaegfx")),
          "... DEVS:Monitors/Wrap made, uaegfx's monitor moved aside")
    check(read("envarc", "Panorama.moved") == ["uaegfx"], "... and remembered")
    check(any(l.startswith("MODES=640x480") for l in read("envarc", "Panorama.prefs") or []),
          "no Picasso96Settings: the card's own MODES= set")
    check(os.path.exists(os.path.join(DIR, "sys", "Prefs", "PanoramaPrefs")),
          "PanoramaPrefs copied to SYS:Prefs")
    check("uaegfx.card's screen also goes out through Panorama.card" in out and
          "panoview.py" in out, "the summary says what happens after the reboot")

    rc, out = run("INSTALL", "SETUP=SINGLE", cwd="p96:build/prefstest.d/pkg")
    print("    " + out.replace("\n", "\n    "))
    check(rc == 0 and os.path.exists(os.path.join(mon, "uaegfx")) and
          os.path.exists(os.path.join(mon, "Panorama")) and
          not os.path.exists(os.path.join(mon, "Wrap")),
          "SETUP=SINGLE: uaegfx's monitor back, Panorama's made, Wrap's moved aside")
    check(not any(l.startswith("CARD=") for l in read("envarc", "Wrap.prefs") or []),
          "... Wrap.card off")
    rc, out = run("INSTALL", "SETUP=MIRROR", cwd="p96:build/prefstest.d/pkg")
    check(rc == 0 and os.path.exists(os.path.join(mon, "Wrap")) and
          not os.path.exists(os.path.join(mon, "uaegfx")) and
          not os.path.exists(os.path.join(mon, "Panorama")),
          "SETUP=MIRROR again: Wrap's monitor back, uaegfx's and Panorama's aside")
    rc, out = run("INSTALL", "SETUP=SPAN", cwd="p96:build/prefstest.d/pkg")
    print("    " + out.replace("\n", "\n    "))
    wrap = read("envarc", "Wrap.prefs") or []
    mc = read("envarc", "Panorama.prefs") or []
    check(rc == 0 and "CARD=uaegfx.card" in wrap and "SPAN=2x1" in wrap and
          "HEAD=Panorama.card" in wrap and "HEADSHARE=1" in wrap and
          not any(l.startswith("MIRROR=") and l != "MIRROR=" for l in wrap) and
          "BOARDS=2" in mc and os.path.exists(os.path.join(mon, "Wrap")),
          "SETUP=SPAN: uaegfx's screen over two Panorama boards: %r %r" % (wrap, mc))
    check("--span 2x1" in out, "... and the viewer told to show both")
    rc, out = run("INSTALL", "SETUP=DESKTOP", cwd="p96:build/prefstest.d/pkg")
    print("    " + out.replace("\n", "\n    "))
    wrap = read("envarc", "Wrap.prefs") or []
    mc = read("envarc", "Panorama.prefs") or []
    check(rc == 0 and "CARD=uaegfx.card" in wrap and "KEEPDISPLAY=1" in wrap and
          "SPLIT=2x1" in mc and "WBMONITOR=1" in mc and
          "INPUT=1" in mc and
          "BOARDS=1" in mc and any(l.startswith("MODES=1280x480,1600x600") for l in mc) and
          os.path.exists(os.path.join(mon, "Panorama")) and os.path.exists(os.path.join(mon, "Wrap"))
          and not os.path.exists(os.path.join(mon, "uaegfx")),
          "SETUP=DESKTOP: one Panorama screen split in two, beside uaegfx kept on by Wrap.card: "
          "%r %r"
          % (wrap, mc))
    check("DesktopOpen" in out and "--span 2x1" in out and
          any(l.startswith("MODES=1600x600") or ",1600x600" in l for l in mc),
          "... the desktop's mode, and how to open the desktop and watch it: %r" % out)
    rc, out = run("INSTALL", "SETUP=SINGLE", "DRY", cwd="p96:build/prefstest.d/pkg")
    rc, out = run("INSTALL", "SETUP=SPAN2x2", "DRY", cwd="p96:build/prefstest.d/pkg")
    check(rc == 0 and "2x2" in out, "SETUP=SPAN2x2: %r" % out)
    rc, out = run("INSTALL", "SETUP=SPAN3x3", "DRY", cwd="p96:build/prefstest.d/pkg")
    check(rc != 0, "SETUP=SPAN3x3 refused (at most 4 heads)")

    # Picasso96Mode has Panorama modes: the card adds none
    setup()
    write("devs", "Picasso96Settings", "\x00\x01junk Panorama\x00more")
    rc, out = run("INSTALL", "DRY", "FROM=p96:build")
    check("Picasso96Mode has some for Panorama" in out, "Picasso96Settings with Panorama: no MODES=")
    rc, out = run("SET", "CARD=Panorama.card", "MIRROR=Panorama.card")
    rc, out = run("INSTALL", "DRY", "FROM=p96:build")
    check("the card's own, MODES=" in out,
          "... but under Wrap.card (a Wrap board) the card's own modes: %r" % out)
    rc, out = run("INSTALL", "SETUP=NONSENSE", "FROM=p96:build")
    check(rc != 0 and "SETUP must be" in out, "SETUP=NONSENSE refused")

    # from before the rename: Multicast.prefs read, saved as Panorama.prefs; the
    # old monitor, card and programs out of the way
    setup()
    os.makedirs(os.path.join(DIR, "libs", "Picasso96"))
    os.makedirs(os.path.join(DIR, "sys", "WBStartup"))
    write("envarc", "Multicast.prefs", "DEST=239.9.9.9\nPUBSCREEN=MULTICAST\nLOG=T:Multicast.log\n")
    write("env", "Wrap.prefs", "CARD=uaegfx.card\nHEAD=Multicast.card\n")
    write("devs/Monitors", "Multicast", "monitor program")
    write("libs/Picasso96", "Multicast.card", "old card")
    write("sys/WBStartup", "MulticastDesktop", "old commodity")
    write("c", "MulticastDesktop", "old commodity")
    rc, out = run("SET", "FPS=11", "USE")
    lines = read("env", "Panorama.prefs") or []
    check("DEST=239.9.9.9" in lines and "PUBSCREEN=PANORAMA" in lines and
          "LOG=T:Panorama.log" in lines and "FPS=11" in lines,
          "Multicast.prefs read, saved as Panorama.prefs with the new names: %r" % lines)
    check("HEAD=Panorama.card" in (read("env", "Wrap.prefs") or []),
          "Wrap.prefs: HEAD=Multicast.card is Panorama.card now")
    rc, out = run("INSTALL", "FROM=p96:build")
    check(not os.path.exists(os.path.join(DIR, "devs", "Monitors", "Multicast")) and
          os.path.exists(os.path.join(DIR, "sys", "Storage", "Monitors", "Multicast")),
          "the old Multicast monitor moved to SYS:Storage/Monitors")
    check(not any(os.path.exists(os.path.join(DIR, *f)) for f in
                  (("libs", "Picasso96", "Multicast.card"), ("sys", "WBStartup", "MulticastDesktop"),
                   ("c", "MulticastDesktop"))),
          "the old card and commodity deleted: %r" % out)

    print("prefstest: %s" % ("FAILED" if fails else "passed"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
