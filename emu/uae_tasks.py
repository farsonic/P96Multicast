#!/usr/bin/env python3
"""The tasks of a stopped Amiga, through the UAE debugger: emu/boing_cross_test.sh.

    uae_tasks.py FIFO LOG

FIFO is FS-UAE's stdin (the debugger active), LOG its stdout. Walks
ExecBase's ThisTask, TaskReady and TaskWait with the debugger's m command
and prints each task's name, saved SP and PC, then has the debugger show
the stack and the code at the PC of the tasks that matter.
"""
import re
import sys
import time

fifo = open(sys.argv[1], "w", buffering=1)
log = open(sys.argv[2], "r", errors="replace")
log.seek(0, 2)


def cmd(c, wait=0.4):
    fifo.write(c + "\n")
    fifo.flush()
    time.sleep(wait)
    return log.read()


def longs(addr, n):
    out = cmd("m %08x %d" % (addr, (n + 3) // 4))
    words = []
    for line in out.splitlines():
        m = re.match(r"^>?([0-9A-Fa-f]{8}) ((?:[0-9A-Fa-f]{4} ?)+)", line.strip())
        if m:
            words += m.group(2).split()
    vals = []
    for i in range(0, len(words) - 1, 2):
        vals.append(int(words[i] + words[i + 1], 16))
    return vals[:n]


def string(addr):
    out = cmd("m %08x 2" % addr)
    for line in out.splitlines():
        m = re.search(r"([0-9A-Fa-f]{8}) ((?:[0-9A-Fa-f]{4} ?)+)\s+(.*)$", line.strip())
        if m:
            return m.group(3)[:30]
    return "?"


cmd("")
execbase = longs(4, 1)[0]
print("ExecBase %08x" % execbase)
names = {}
for title, start, walk in (("Current", execbase + 276, False), ("Ready", execbase + 406, True),
                           ("Waiting", execbase + 420, True)):
    print(title)
    node = longs(start, 1)[0]
    while node:
        if walk and not longs(node, 1)[0]:
            break
        name = string(longs(node + 10, 1)[0])
        sp = longs(node + 54, 1)[0]
        sigwait = longs(node + 22, 1)[0]
        pc = longs(sp + 74, 1)[0] if sp else 0
        print("  %08x %-28s sp %08x pc %08x waits %08x" % (node, name, sp, pc, sigwait))
        names[node] = (name, sp, pc)
        if not walk:
            break
        node = longs(node, 1)[0]
for node, (name, sp, pc) in names.items():
    if re.search(r"PanoramaDesk|mctest|Boing|input|Panorama|Wrap|Workbench", name) and sp:
        print("---- %s stack" % name)
        print(cmd("m %08x 24" % sp, 0.8))
        print("---- %s code" % name)
        print(cmd("d %08x 12" % (pc - 24), 0.8))
