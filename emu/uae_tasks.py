#!/usr/bin/env python3
"""The tasks of a stopped Amiga, through the UAE debugger: emu/boing_cross_test.sh.

    uae_tasks.py FIFO LOG

FIFO is FS-UAE's stdin (the debugger active), LOG its stdout. With the
debugger's m command it walks ExecBase's library list (each library's jump
table: where its functions begin), then ThisTask, TaskReady and TaskWait:
each task's name (a process: its command), the signals it waits for, and
the return addresses on its stack, named after the library function they
are in. A task waiting on SIGF_SINGLE is in ObtainSemaphore() or a
WaitTOF().
"""
import re
import sys
import time

fifo = open(sys.argv[1], "w", buffering=1)
log = open(sys.argv[2], "r", errors="replace")
log.seek(0, 2)
SIGF_SINGLE = 0x10


def cmd(c, wait=0.3):
    fifo.write(c + "\n")
    fifo.flush()
    time.sleep(wait)
    return log.read()


def dump(addr, nwords):
    """nwords 16-bit words at addr, as the m command shows them (8 a line)"""
    lines = (nwords + 7) // 8
    out = cmd("m %08x %d" % (addr, lines), 0.15)
    words = []
    for _ in range(40):
        for line in out.splitlines():
            m = re.match(r"^>*([0-9A-Fa-f]{8}) ((?:[0-9A-Fa-f]{4} ?){1,8})", line.strip())
            if m:
                words += m.group(2).split()
        if len(words) >= nwords:
            break
        time.sleep(0.1)
        out = log.read()
    return [int(w, 16) for w in words][:nwords]


def longs(addr, n):
    w = dump(addr, 2 * n)
    return [(w[i] << 16) | w[i + 1] for i in range(0, len(w) - 1, 2)]


def long1(addr):
    v = longs(addr, 1)
    return v[0] if v else 0


def string(addr, bstr=False):
    if not addr:
        return "?"
    w = dump(addr, 16)
    b = b"".join(bytes((x >> 8, x & 255)) for x in w)
    if bstr:
        b = b[1:1 + b[0]] if b else b""
    else:
        b = b.split(b"\0")[0]
    return b.decode("latin1", "replace")[:32]


cmd("")
execbase = long1(4)
print("ExecBase %08x" % execbase)

# the libraries and their function entries
funcs = []   # (entry, library, lvo)
libs = []
for listhead in (execbase + 378, execbase + 350):   # LibList, DeviceList
    node = long1(listhead)
    while node and long1(node):
        name = string(long1(node + 10))
        negsize = dump(node + 16, 1)[0]
        libs.append((node, name, negsize))
        n = negsize // 6
        for k in range(1, min(n, 200) + 1):
            w = dump(node - 6 * k, 3)
            if len(w) == 3 and w[0] == 0x4EF9:
                entry = (w[1] << 16) | w[2]
                if entry >= 0x1000:
                    funcs.append((entry, name, -6 * k))
        node = long1(node)
funcs.sort()
print("libraries: " + ", ".join("%s@%08x" % (n, a) for a, n, s in libs))


def where(addr):
    best = None
    for entry, name, lvo in funcs:
        if entry <= addr < entry + 0x4000:
            best = (entry, name, lvo)
        if entry > addr:
            break
    if best and addr - best[0] < 0x1000:
        return "%s %d+%x" % (best[1], best[2], addr - best[0])
    if 0xf80000 <= addr < 0x1000000:
        return "rom"
    return ""


def task_name(node):
    name = string(long1(node + 10))
    if dump(node + 8, 1)[0] == 13:   # NT_PROCESS: its command
        cli = long1(node + 172) << 2
        if cli:
            cmdname = string(long1(cli + 16) << 2, bstr=True)
            if cmdname:
                name += " [" + cmdname + "]"
    return name


tasks = []
for title, start, walk in (("Current", execbase + 276, False), ("Ready", execbase + 406, True),
                           ("Waiting", execbase + 420, True)):
    print(title)
    node = long1(start)
    while node:
        if walk and not long1(node):
            break
        name = task_name(node)
        sp = long1(node + 54)
        sigwait = long1(node + 22)
        print("  %08x %-40s sp %08x waits %08x" % (node, name, sp, sigwait))
        tasks.append((node, name, sp, sigwait))
        if not walk:
            break
        node = long1(node)

for node, name, sp, sigwait in tasks:
    if not sp:
        continue
    if not (sigwait & SIGF_SINGLE or re.search(r"PanoramaDesk|mctest|Boing|input|Panorama|Wrap|Workbench|Picasso", name)):
        continue
    print("---- %s: return addresses on its stack" % name)
    stack = longs(sp, 160)
    print("  raw: " + " ".join("%08x" % v for v in stack[:48]))
    for v in stack:
        w = where(v)
        if w:
            print("  %08x  %s" % (v, w))
