#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Builds synthetic .metallib containers for air2ir3 tests.

The layout is the one implemented by src/a2i_mtlb.c (MTLB 2.8 header, tagged
function list, extended header, four sections). Every byte comes from the
arguments; nothing is copied from Apple binaries. Mutations produce the
malformed inputs used by the error-path tests.

  gen_metallib.py OUT --bc FILE --name NAME [options] [--fn ...]

Repeat --fn "BC:NAME[:TYPE]" to add more entries after the first one.
"""
import argparse
import hashlib
import struct
import sys

HEADER = 0x58
MUTATIONS = (
    "none", "bad-magic", "truncate", "file-size", "list-size", "hash", "bc-magic",
    "unknown-tag", "dup-tag", "no-endt", "bc-oob", "fat", "fat-no-air",
)


def tag(t, payload):
    assert len(t) == 4 and len(payload) < 0x10000
    return t + struct.pack("<H", len(payload)) + payload


def entry(name_bytes, ftype, digest, offt, vers, mdsz, extra=b"", no_endt=False):
    body = tag(b"NAME", name_bytes + b"\0")
    body += tag(b"TYPE", bytes([ftype]))
    body += tag(b"HASH", digest)
    body += tag(b"MDSZ", struct.pack("<Q", mdsz))
    body += tag(b"OFFT", struct.pack("<QQQ", *offt))
    body += tag(b"VERS", struct.pack("<HHHH", *vers))
    body += extra
    if not no_endt:
        body += b"ENDT"
    return struct.pack("<I", 4 + len(body)) + body


def build(fns, a):
    """fns: list of (bitcode bytes, name bytes, type)."""
    vers = tuple(int(x) for x in a.air.split(".")) + tuple(int(x) for x in a.msl.split("."))
    fmaj, fmin = (int(x) for x in a.file_ver.split("."))
    pub = b"".join(struct.pack("<I", 8) + b"ENDT" for _ in fns)
    prv = pub
    bcsec, entries = b"", []
    for i, (bc, name, ftype) in enumerate(fns):
        if i == 0 and a.mutate == "bc-magic":
            bc = b"XC" + bc[2:]          # breaks 'BC' 0xC0DE; HASH recomputed below
        digest = hashlib.sha256(bc).digest()
        if i == 0 and a.mutate == "hash":
            digest = bytes([digest[0] ^ 0xFF]) + digest[1:]
        mdsz = len(bc) + (4096 if i == 0 and a.mutate == "bc-oob" else 0)
        extra = b""
        if i == 0 and a.mutate == "unknown-tag":
            extra = tag(b"ZZZZ", b"\x01\x02")
        if i == 0 and a.mutate == "dup-tag":
            extra = tag(b"TYPE", bytes([ftype]))
        entries.append(entry(name, ftype, digest, (8 * i, 8 * i, len(bcsec)), vers, mdsz,
                             extra, no_endt=(i == 0 and a.mutate == "no-endt")))
        bcsec += bc
    flist = b"".join(entries)
    fl_size = len(flist) + (4 if a.mutate == "list-size" else 0)
    ext = b"ENDT"
    off_fl = HEADER
    off_pub = off_fl + 4 + len(flist) + len(ext)
    off_prv = off_pub + len(pub)
    off_bc = off_prv + len(prv)
    total = off_bc + len(bcsec)
    hdr = b"MTLB" + struct.pack("<HHHBBHH", 1, fmaj, fmin, 0, a.os_type, a.os_major, a.os_minor)
    hdr += struct.pack("<Q", total + (16 if a.mutate == "file-size" else 0))
    for off, size in ((off_fl, fl_size), (off_pub, len(pub)), (off_prv, len(prv)),
                      (off_bc, len(bcsec))):
        hdr += struct.pack("<QQ", off, size)
    assert len(hdr) == HEADER
    img = hdr + struct.pack("<I", len(fns)) + flist + ext + pub + prv + bcsec
    assert len(img) == total
    if a.mutate == "bad-magic":
        img = b"MTLX" + img[4:]
    if a.mutate == "truncate":
        img = img[:0x40]
    if a.mutate in ("fat", "fat-no-air"):
        cputype = 0x01000017 if a.mutate == "fat" else 0x0100000C
        off = 0x1000
        fat = struct.pack(">II", 0xCAFEBABE, 1) + struct.pack(">IIIII", cputype, 0, off,
                                                               len(img), 12)
        img = fat + b"\0" * (off - len(fat)) + img
    return img


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("out")
    p.add_argument("--bc", required=True, help="LLVM bitcode of the first entry")
    p.add_argument("--name", help="first entry NAME (UTF-8)")
    p.add_argument("--name-hex", help="first entry NAME as raw hex bytes (no NUL)")
    p.add_argument("--type", type=int, default=0, help="TYPE tag byte (default 0)")
    p.add_argument("--fn", action="append", default=[], help="extra entry BC:NAME[:TYPE]")
    p.add_argument("--air", default="2.7")
    p.add_argument("--msl", default="3.2")
    p.add_argument("--file-ver", default="2.8")
    p.add_argument("--os-type", type=lambda s: int(s, 0), default=0x82)
    p.add_argument("--os-major", type=int, default=18)
    p.add_argument("--os-minor", type=int, default=0)
    p.add_argument("--mutate", choices=MUTATIONS, default="none")
    a = p.parse_args()
    if (a.name is None) == (a.name_hex is None):
        p.error("exactly one of --name / --name-hex is required")
    name = a.name.encode() if a.name is not None else bytes.fromhex(a.name_hex)
    if not name or b"\0" in name:
        p.error("NAME must be non-empty and contain no NUL")
    fns = [(open(a.bc, "rb").read(), name, a.type)]
    for spec in a.fn:
        parts = spec.split(":")
        if len(parts) not in (2, 3):
            p.error("--fn takes BC:NAME[:TYPE]")
        fns.append((open(parts[0], "rb").read(), parts[1].encode(),
                    int(parts[2]) if len(parts) == 3 else 0))
    with open(a.out, "wb") as f:
        f.write(build(fns, a))
    return 0


if __name__ == "__main__":
    sys.exit(main())
