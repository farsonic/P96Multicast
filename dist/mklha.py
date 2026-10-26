#!/usr/bin/env python3
"""Write an LhA archive of a directory tree, stored without compression.

    dist/mklha.py OUT.lha DIR [NAME]

Used by dist/mkpackage.sh when no lha that can create archives is around
(lhasa only extracts). Every file goes in as method -lh0- (stored) with a
level 2 header: the path in a directory extension (0xff between parts) and
the name in a filename extension, CRC-16 (the ARC polynomial, as LhA uses),
OS id 'A'. Directories are given as entries of their own (-lhd-), so empty
ones survive. LhA and lhasa read level 2 headers; stored files cost no
unpacking time on a 68000.

NAME is the top directory's name in the archive (default: DIR's own).
"""
import os
import struct
import sys
import time


def crc16(data, crc=0):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def ext(kind, payload):
    return bytes([kind]) + payload


def header(name, dirparts, data, mtime, is_dir):
    exts = [ext(0x01, name.encode("latin-1"))]
    if dirparts:
        exts.append(ext(0x02, b"\xff".join(p.encode("latin-1") for p in dirparts) + b"\xff"))
    method = b"-lhd-" if is_dir else b"-lh0-"
    size = 0 if is_dir else len(data)
    # each extension is preceded by its size (2 bytes, counting itself);
    # the last "next size" is 0
    body = b""
    sizes = [len(e) + 2 for e in exts]
    for i, e in enumerate(exts):
        nxt = sizes[i + 1] if i + 1 < len(exts) else 0
        body += e + struct.pack("<H", nxt)
    total = 26 + len(body)
    head = struct.pack("<H5sIIIBBHBH", total, method, size, size, int(mtime), 0x20, 2,
                       crc16(data) if not is_dir else 0, ord("A"), sizes[0])
    assert len(head) == 26
    return head + body


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    out, top = argv[1], argv[2].rstrip("/")
    name = argv[3] if len(argv) > 3 else os.path.basename(top)
    entries = []
    for root, dirs, files in os.walk(top):
        dirs.sort()
        rel = os.path.relpath(root, top)
        parts = [name] + ([] if rel == "." else rel.split(os.sep))
        entries.append((parts[:-1], parts[-1], root, True))
        for f in sorted(files):
            entries.append((parts, f, os.path.join(root, f), False))
    with open(out, "wb") as w:
        for dirparts, fname, path, is_dir in entries:
            st = os.stat(path)
            if is_dir:
                # a directory entry: no name, the whole path as the directory
                exts_dir = dirparts + [fname]
                w.write(header("", exts_dir, b"", st.st_mtime, True))
                continue
            with open(path, "rb") as f:
                data = f.read()
            w.write(header(fname, dirparts, data, st.st_mtime, False))
            w.write(data)
        w.write(b"\0")
    print("mklha: %s: %d entries" % (out, len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
