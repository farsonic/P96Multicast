#!/usr/bin/env python3
"""A DataTypes descriptor (DEVS:DataTypes/NAME) for panoramapic.datatype.

    mkdtype.py OUT NAME ID PRIORITY MASK...

    mkdtype.py DEVS/DataTypes/JPEG JPEG JPEG 0 ff d8 ff
    mkdtype.py DEVS/DataTypes/PNG PNG "PNG " 0 89 50 4e 47

A FORM DTYP as datatypes.library reads them: FVER, NAME, and DTHD with a
struct DataTypeHeader whose string and mask pointers are offsets into the
chunk. The mask is a binary one (DTF_BINARY): the file's first bytes must
be these (-1 for any). The base name "panoramapic" is the class library,
SYS:Classes/DataTypes/panoramapic.datatype, which has the viewer decode the
picture.
"""
import struct
import sys

GID_PICTURE = b"pict"
BASENAME = b"panoramapic"
PATTERN = b"#?"
DTF_BINARY = 0


def chunk(tag, body):
    return tag + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def main():
    out, name, ident, prio = sys.argv[1], sys.argv[2].encode(), sys.argv[3].encode(), int(sys.argv[4])
    mask = [int(v, 16) for v in sys.argv[5:]]
    if len(ident) != 4:
        sys.exit("the ID must be 4 characters")
    # the header, then the mask words, then the strings
    off_mask = 32
    off_name = off_mask + 2 * len(mask)
    off_base = off_name + len(name) + 1
    off_pat = off_base + len(BASENAME) + 1
    head = struct.pack(">IIIIIIhhHH", off_name, off_base, off_pat, off_mask,
                       struct.unpack(">I", GID_PICTURE)[0], struct.unpack(">I", ident)[0],
                       len(mask), 0, DTF_BINARY, prio)
    body = head + b"".join(struct.pack(">h", v) for v in mask) + name + b"\0" + BASENAME + b"\0" + \
        PATTERN + b"\0"
    form = chunk(b"FVER", b"$VER: " + name + b" 1.0 (29.10.2026)\0") + chunk(b"NAME", name + b"\0") + \
        chunk(b"DTHD", body)
    with open(out, "wb") as f:
        f.write(b"FORM" + struct.pack(">I", 4 + len(form)) + b"DTYP" + form)


if __name__ == "__main__":
    main()
