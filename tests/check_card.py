#!/usr/bin/env python3
"""Static checks of a built card driver (an AmigaOS hunk file).

    check_card.py build/Multicast.card
    check_card.py build/Wrap.card Wrap.card

* the first hunk is code and starts with "moveq #-1,d0; rts", so running the
  library as a program fails harmlessly;
* it has a RomTag (RTC_MATCHWORD, pointing at itself) for an auto-init
  NT_LIBRARY named "Multicast.card" (or the name given);
* the function table has Open, Close, Expunge, Null, FindCard and InitCard
  (6 entries, -1 terminated), so FindCard is at -30 and InitCard at -36;
  Multicast.card has McRecord, McDamage and McPointer after them, for
  Wrap.card (9 entries).
"""
import struct
import sys

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS = 0x3F3, 0x3E9, 0x3EA, 0x3EB
HUNK_RELOC32, HUNK_SYMBOL, HUNK_DEBUG, HUNK_END = 0x3EC, 0x3F0, 0x3F1, 0x3F2
HUNK_RELOC32SHORT = (0x3F7, 0x3FC)  # vlink -Rshort writes 0x3F7 (V37+ LoadSeg)


def load_hunks(data):
    """Return a list of (type, bytes, relocs) with relocs {offset: target hunk}."""
    pos = 0

    def u32():
        nonlocal pos
        v = struct.unpack(">I", data[pos:pos + 4])[0]
        pos += 4
        return v

    if u32() != HUNK_HEADER:
        raise ValueError("not a load file")
    while u32():  # resident library names (none)
        pass
    count, first, last = u32(), u32(), u32()
    sizes = [u32() & 0x3fffffff for _ in range(last - first + 1)]
    hunks = []
    while pos < len(data) and len(hunks) < count:
        t = u32() & 0x3fffffff
        if t in (HUNK_CODE, HUNK_DATA):
            n = u32() * 4
            body = data[pos:pos + n]
            pos += n
            hunks.append([t, body, {}])
        elif t == HUNK_BSS:
            u32()
            hunks.append([t, b"", {}])
        elif t == HUNK_RELOC32:
            while True:
                n = u32()
                if not n:
                    break
                target = u32()
                for _ in range(n):
                    hunks[-1][2][u32()] = target
        elif t in HUNK_RELOC32SHORT:
            start = pos
            while True:
                n = struct.unpack(">H", data[pos:pos + 2])[0]
                pos += 2
                if not n:
                    break
                target = struct.unpack(">H", data[pos:pos + 2])[0]
                pos += 2
                for _ in range(n):
                    hunks[-1][2][struct.unpack(">H", data[pos:pos + 2])[0]] = target
                    pos += 2
            pos = start + ((pos - start + 3) & ~3)
        elif t == HUNK_SYMBOL:
            while True:
                n = u32()
                if not n:
                    break
                pos += n * 4 + 4
        elif t == HUNK_DEBUG:
            pos += u32() * 4
        elif t == HUNK_END:
            continue
        else:
            raise ValueError("unexpected hunk type %x" % t)
    return hunks, sizes


def main(argv):
    path = argv[1] if len(argv) > 1 else "build/Multicast.card"
    want = argv[2] if len(argv) > 2 else "Multicast.card"
    with open(path, "rb") as f:
        data = f.read()
    hunks, _ = load_hunks(data)
    fails = []
    t, code, relocs = hunks[0]
    if t != HUNK_CODE:
        fails.append("first hunk is not code")
    if code[:4] != b"\x70\xff\x4e\x75":
        fails.append("does not start with moveq #-1,d0; rts")
    rt = code.find(b"\x4a\xfc")
    while rt >= 0 and struct.unpack(">I", code[rt + 2:rt + 6])[0] != rt:
        rt = code.find(b"\x4a\xfc", rt + 2)
    if rt < 0:
        fails.append("no RomTag")
    else:
        flags, version, ntype = code[rt + 10], code[rt + 11], code[rt + 12]
        name_off = struct.unpack(">I", code[rt + 14:rt + 18])[0]
        init_off = struct.unpack(">I", code[rt + 22:rt + 26])[0]
        name = code[name_off:code.index(b"\0", name_off)].decode()
        if not flags & 0x80 or ntype != 9:
            fails.append("RomTag is not an auto-init library")
        if name != want:
            fails.append("library name is %r" % name)
        functable = struct.unpack(">I", code[init_off + 4:init_off + 8])[0]
        entries = []
        p = functable
        while True:
            v = struct.unpack(">I", code[p:p + 4])[0]
            if v == 0xffffffff:
                break
            if p not in relocs:
                fails.append("function table entry at %d is not relocated" % p)
            entries.append(v)
            p += 4
        nfuncs = 9 if name == "Multicast.card" else 6
        if len(entries) != nfuncs:
            fails.append("function table has %d entries, want %d" % (len(entries), nfuncs))
        print("check_card: %s: library %s version %d, %d functions, %d bytes"
              % (path, name, version, len(entries), len(data)))
    for f in fails:
        print("FAIL:", f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
