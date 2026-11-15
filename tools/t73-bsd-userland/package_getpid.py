#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host-only T73 Mach-O/ROMFS packaging for an original BSD getpid fixture.

Adapted from the independently retained, self-authored T72 Q5 C1 emitter and
ROMFS layout. It does NOT admit the image under Apple signature policy, run
XNU, or grant a physical boot. Never changes the Q5 inputs.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

PAGE = 0x4000
BASE = 0x100000000
MAIN_IDENT = b"org.penang.t73.getpid\0"
DYLD_IDENT = b"org.penang.t72.dyld\0"
REBASE = bytes.fromhex("11 22 00 51 00")
DATA_INIT = struct.pack("<5Q", BASE + 0x4010, 0, 0x50454e414e475437, 0, 0)


class Invalid(ValueError):
    pass


def require(ok, reason):
    if not ok:
        raise Invalid(reason)


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def segment(name, addr, size, offset, length, perm):
    return struct.pack("<II16sQQQQIIII", 0x19, 72, name.encode(),
                       addr, size, offset, length, perm, perm, 0, 0)


def code_signature(image, ident, role):
    require(len(ident) <= 24, "CodeDirectory identifier too long")
    count = (len(image) + 4095) // 4096
    require(count == (9 if role == "main" else 5), "slot count")
    cd = struct.pack(
        ">9I4B4I4Q", 0xfade0c02, 112 + count * 32,
        0x20400, 2, 112, 88, 0, count, len(image),
        32, 2, 0, 12, 0, 0, 0, 0, 0, 0, PAGE, int(role == "main")
    )
    cd += ident.ljust(24, b"\0")
    cd += b"".join(
        hashlib.sha256(image[i:min(i + 4096, len(image))]).digest()
        for i in range(0, len(image), 4096)
    )
    return struct.pack(">5I", 0xfade0cc0, 20 + len(cd), 1, 0, 20) + cd + bytes(12)


def verify_signature(image, sigoff, ident, role):
    size = 0x1b0 if role == "main" else 0x130
    slots = 9 if role == "main" else 5
    require(len(image) == sigoff + size, "signed image size")
    cdsize = 112 + slots * 32
    require(struct.unpack_from(">5I", image, sigoff) ==
            (0xfade0cc0, 20 + cdsize, 1, 0, 20), "SuperBlob")
    cd = image[sigoff + 20:sigoff + 20 + cdsize]
    require(struct.unpack_from(">9I4B4I4Q", cd) ==
            (0xfade0c02, cdsize, 0x20400, 2, 112, 88, 0, slots,
             sigoff, 32, 2, 0, 12, 0, 0, 0, 0, 0, 0, PAGE,
             int(role == "main")), "CodeDirectory profile")
    require(cd[88:112] == ident.ljust(24, b"\0"), "CodeDirectory identity")
    require(image[sigoff + 20 + cdsize:] == bytes(12), "signature tail")
    for idx in range(slots):
        expected = hashlib.sha256(image[idx * 4096:min((idx + 1) * 4096, sigoff)]).digest()
        require(cd[112 + idx * 32:144 + idx * 32] == expected,
                f"CodeDirectory slot {idx}")


def emit_main(text, data):
    require(0 < len(text) <= 0x3000, "C1 TEXT budget")
    # A valid signature is not proof of *which* program was packaged.
    # Constrain the host fixture to its one original getpid/SVC instruction.
    require(text.count(bytes.fromhex("90 02 80 52")) == 1,
            "getpid syscall 20 in w16")
    require(text.count(bytes.fromhex("01 10 00 d4")) == 1,
            "exactly one svc #0x80")
    require(data == DATA_INIT, "T73 data relocation contract")
    sigoff = 0x8100
    image = bytearray(sigoff)
    commands = b"".join((
        segment("__PAGEZERO", 0, BASE, 0, 0, 0),
        segment("__TEXT", BASE, PAGE, 0, PAGE, 5),
        segment("__DATA", BASE + PAGE, PAGE, PAGE, PAGE, 3),
        segment("__LINKEDIT", BASE + PAGE * 2, PAGE, PAGE * 2, 0x2b0, 1),
        struct.pack("<6I", 0x32, 24, 1, 0xf0500, 0xf0500, 0),
        struct.pack("<III20s", 0xe, 32, 12, b"/usr/lib/dyld\0"),
        struct.pack("<IIQQ", 0x80000028, 24, 0x1000, 0x40000),
        struct.pack("<12I", 0x80000022, 48, 0x8000, 5, 0, 0, 0, 0, 0, 0, 0, 0),
        struct.pack("<4I", 0x1d, 16, sigoff, 0x1b0),
    ))
    require(len(commands) == 0x1b0, "load command geometry")
    image[:32] = struct.pack("<8I", 0xfeedfacf, 0x100000c, 0, 2, 9,
                              len(commands), 0x200005, 0)
    image[32:32 + len(commands)] = commands
    image[0x1000:0x1000 + len(text)] = text
    image[PAGE:PAGE + len(data)] = data
    image[0x8000:0x8005] = REBASE
    output = bytes(image) + code_signature(bytes(image), MAIN_IDENT, "main")
    verify_main(output)
    return output


def verify_main(data):
    require(len(data) == 0x82b0, "main file size")
    header = struct.unpack_from("<8I", data, 0)
    require(header == (0xfeedfacf, 0x100000c, 0, 2, 9, 0x1b0, 0x200005, 0),
            "Mach-O ARM64 main header")
    off = 32
    commands = []
    for _ in range(9):
        cmd, n = struct.unpack_from("<II", data, off)
        require(n >= 8 and n % 8 == 0 and off + n <= 32 + 0x1b0, "command bounds")
        commands.append((cmd, off, n))
        off += n
    require(off == 32 + 0x1b0, "command count / sizes")
    expected = [
        ("__PAGEZERO", 0, BASE, 0, 0, 0),
        ("__TEXT", BASE, PAGE, 0, PAGE, 5),
        ("__DATA", BASE + PAGE, PAGE, PAGE, PAGE, 3),
        ("__LINKEDIT", BASE + 2 * PAGE, PAGE, 2 * PAGE, 0x2b0, 1),
    ]
    for idx, (name, addr, size, fileoff, length, perm) in enumerate(expected):
        cmd, at, n = commands[idx]
        require((cmd, n) == (0x19, 72), "segment command")
        actual = struct.unpack_from("<16sQQQQIIII", data, at + 8)
        require(actual == (name.encode().ljust(16, b"\0"), addr, size, fileoff,
                           length, perm, perm, 0, 0), "W^X/segment " + name)
        require(not (actual[5] & 2 and actual[5] & 4), "writable executable segment")
    require(commands[4][0] == 0x32 and commands[4][2] == 24, "LC_BUILD_VERSION")
    require(commands[5][0] == 0xe and commands[5][2] == 32, "LC_LOAD_DYLINKER")
    require(data[commands[5][1] + 12:commands[5][1] + 32] ==
            b"/usr/lib/dyld\0".ljust(20, b"\0"), "dyld path")
    require(commands[6][0] == 0x80000028 and
            struct.unpack_from("<QQ", data, commands[6][1] + 8) ==
            (0x1000, 0x40000), "LC_MAIN entry")
    require(commands[7][0] == 0x80000022 and
            struct.unpack_from("<10I", data, commands[7][1] + 8) ==
            (0x8000, 5, 0, 0, 0, 0, 0, 0, 0, 0), "LC_DYLD_INFO_ONLY")
    require(commands[8][0] == 0x1d and
            struct.unpack_from("<II", data, commands[8][1] + 8) ==
            (0x8100, 0x1b0), "LC_CODE_SIGNATURE")
    require(data[0x1000:0x4000].count(bytes.fromhex("90 02 80 52")) == 1,
            "getpid register ABI in signed TEXT")
    require(data[0x1000:0x4000].count(bytes.fromhex("01 10 00 d4")) == 1,
            "one SVC in signed TEXT")
    require(data[0x8000:0x8005] == REBASE, "real rebase stream")
    require(data[0x8005:0x8100] == bytes(0xfb), "LINKEDIT padding")
    require(data[0x4000:0x4028] == DATA_INIT and
            data[0x4028:0x8000] == bytes(0x3fd8), "T73 DATA / padding")
    verify_signature(data, 0x8100, MAIN_IDENT, "main")
    return {"kind": "T73 getpid project fixture", "sha256": sha(data),
            "trust_admission": "OPEN", "guest_execution": "NOT_RUN",
            "codedirectory": "ad-hoc integrity only"}


def verify_q5_dyld(data):
    # Historical project-built dyld, never Apple's dyld. No trust admission claim.
    require(len(data) == 0x4230, "Q5 dyld size")
    require(struct.unpack_from("<8I", data, 0) ==
            (0xfeedfacf, 0x100000c, 0, 7, 6, 0x1f8, 1, 0),
            "Q5 dyld header")
    off = 32
    codes = []
    for _ in range(6):
        code, size = struct.unpack_from("<II", data, off)
        require(size >= 8 and size % 8 == 0 and off + size <= 0x218,
                f"Q5 dyld command bounds code={code:#x} off={off:#x} size={size:#x}")
        codes.append((code, off, size))
        off += size
    require(off == 0x218, "Q5 dyld command sum")
    for idx, (name, addr, size, fileoff, length, perm) in enumerate((
        ("__TEXT", 0, PAGE, 0, PAGE, 5),
        ("__LINKEDIT", PAGE, PAGE, PAGE, 0x230, 1),
    )):
        code, at, n = codes[idx]
        require((code, n) == (0x19, 72), "Q5 dyld segment")
        require(struct.unpack_from("<16sQQQQIIII", data, at + 8) ==
                (name.encode().ljust(16, b"\0"), addr, size, fileoff,
                 length, perm, perm, 0, 0), "Q5 dyld W^X")
    require(codes[5][0] == 0x1d and
            struct.unpack_from("<II", data, codes[5][1] + 8) ==
            (0x4100, 0x130), "Q5 dyld LC_CODE_SIGNATURE")
    verify_signature(data, 0x4100, DYLD_IDENT, "dyld")
    return {"sha256": sha(data), "identity": "Q5 project-built dyld",
            "trust_admission": "OPEN"}


def checksum(blob):
    require(len(blob) % 4 == 0, "ROMFS checksum alignment")
    return sum(struct.unpack(">" + str(len(blob) // 4) + "I", blob)) & 0xffffffff


def align(n, multiple=16):
    return (n + multiple - 1) & ~(multiple - 1)


def build_romfs(main, dyld):
    verify_main(main)
    verify_q5_dyld(dyld)
    nodes = []
    def new(name, kind, content=b"", executable=False):
        node = {"name": name, "kind": kind, "data": content,
                "executable": executable, "next": None, "spec": None}
        nodes.append(node)
        return node
    root = new("root", 1)
    def directory(parent, children):
        dot = new(".", 0); dot["spec"] = parent
        dotdot = new("..", 0); dotdot["spec"] = parent.get("parent", parent)
        children_nodes = [dot, dotdot]
        for name, kind, content in children:
            child = new(name, kind, content, kind == 2)
            child["parent"] = parent
            children_nodes.append(child)
        for left, right in zip(children_nodes, children_nodes[1:]):
            left["next"] = right
        parent["spec"] = children_nodes[0]
        return {item["name"]: item for item in children_nodes}
    dirs = directory(root, [("dev", 1, b""), ("sbin", 1, b""),
                            ("usr", 1, b"")])
    directory(dirs["dev"], [])
    sbin = directory(dirs["sbin"], [("p16-marker", 2, b"Penang host research media\n"),
                                    ("penang-dyn-init", 2, main), ("launchd", 0, b"")])
    sbin["p16-marker"]["executable"] = False
    sbin["launchd"]["spec"] = sbin["penang-dyn-init"]
    usr = directory(dirs["usr"], [("lib", 1, b"")])
    directory(usr["lib"], [("dyld", 2, dyld)])
    cursor = 32
    for node in nodes:
        node["off"] = cursor
        node["dataoff"] = align(cursor + 17 + len(node["name"]))
        cursor = align(node["dataoff"] + len(node["data"]))
    logical = max(1024, cursor)
    output = bytearray(align(logical, 16384))
    output[:8] = b"-rom1fs-"
    struct.pack_into(">I", output, 8, logical)
    output[16:28] = b"PenangT73U0\0"
    for node in nodes:
        off = node["off"]
        following = node["next"]["off"] if node["next"] else 0
        spec = node["spec"]["off"] if node["spec"] else 0
        struct.pack_into(">4I", output, off,
                         following | node["kind"] | (8 if node["executable"] else 0),
                         spec, len(node["data"]), 0)
        label = node["name"].encode() + b"\0"
        output[off + 16:off + 16 + len(label)] = label
        output[node["dataoff"]:node["dataoff"] + len(node["data"])] = node["data"]
        struct.pack_into(">I", output, off + 12,
                         (-checksum(output[off:node["dataoff"]])) & 0xffffffff)
    struct.pack_into(">I", output, 12, (-checksum(output[:512])) & 0xffffffff)
    image = bytes(output)
    require(image.count(main) == 1 and image.count(dyld) == 1,
            "ROMFS byte identity of both executables")
    require(checksum(image[:512]) == 0, "ROMFS superblock checksum")
    require(struct.unpack_from(">I", image, sbin["launchd"]["off"] + 4)[0] ==
            sbin["penang-dyn-init"]["off"], "launchd hardlink to project fixture")
    return image, {"schema": "penang-t73-u0-romfs-v1",
                   "scope": "HOST_ONLY", "guest_execution": "NOT_RUN",
                   "trust_admission": "OPEN",
                   "sha256": sha(image), "nodes": len(nodes),
                   "root_logical_bytes": logical,
                   "paths": ["/sbin/penang-dyn-init", "/sbin/launchd",
                             "/usr/lib/dyld"], "main_sha256": sha(main),
                   "dyld_sha256": sha(dyld)}


def write_package(carrier_text, carrier_data, dyld, destination):
    require(not destination.exists(), "destination already exists")
    main = emit_main(carrier_text, carrier_data)
    romfs, receipt = build_romfs(main, dyld)
    destination.mkdir(parents=True, exist_ok=False)
    (destination / "penang-dyn-init.macho").write_bytes(main)
    (destination / "dynamic-userland.romfs").write_bytes(romfs)
    (destination / "T73-HOST-MANIFEST.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--carrier-text", type=Path, required=True)
    parser.add_argument("--carrier-data", type=Path, required=True)
    parser.add_argument("--q5-dyld-macho", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(write_package(args.carrier_text.read_bytes(),
                                   args.carrier_data.read_bytes(),
                                   args.q5_dyld_macho.read_bytes(),
                                   args.out), indent=2))
