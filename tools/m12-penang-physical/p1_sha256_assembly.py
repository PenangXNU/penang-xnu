#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Emit a pinned SHA256 register-dialect patch; audit A64 crypto encodings.

The patch changes seven register spellings in four legacy macros. Register
numbers, operation order, constants, control flow and algorithm stay unchanged.
No vendor source is distributed here. Generated patches and objects stay local.
This is an instruction-encoding check, not ARM execution or a SHA256 runtime KAT.
"""
from __future__ import annotations

import argparse
from collections import Counter
import difflib
import hashlib
import json
from pathlib import Path
import re
import struct

REPO = Path(__file__).resolve().parents[2]
HEADER = "osfmk/arm64/corecrypto/arm64_isa_compatibility.h"
SOURCE = "osfmk/arm64/corecrypto/sha256_compress_arm64.s"
PINS = {
    HEADER: "77bb3477fb6a8063b458dadb890817e88a7052ef69adbcd159a1fa650f0ec6f9",
    SOURCE: "a7394460ecc7390f0142a6c68e782c761afbed81cc003023faf5db347b30023d",
}
# Arm A64 SHA256 instruction encodings (DDI 0487, Advanced SIMD cryptography).
# Three-register forms place Rm at [20:16], Rn at [9:5], Rd at [4:0].
# SHA256SU0 has only Rn/Rd. These constants are independent of LLVM output.
ENCODINGS = {"sha256h": (0x5e004000, 3), "sha256h2": (0x5e005000, 3),
             "sha256su0": (0x5e282800, 2), "sha256su1": (0x5e006000, 3)}
OPERANDS = {"SHA256SU0": ("v$0.4s", "v$1.4s"),
            "SHA256SU1": ("v$0.4s", "v$1.4s", "v$2.4s"),
            "SHA256H": ("q$0", "q$1", "v$2.4s"),
            "SHA256H2": ("q$0", "q$1", "v$2.4s")}


def rewrite_legacy(text):
    """Change only the legacy spelling; require its existing standard twin."""
    replacements = 0
    for name, operands in OPERANDS.items():
        pattern = re.compile(r"(?m)^([ \t]*\.macro[ \t]+" + name
                             + r"[ \t]*\n)([^\n]*)(\n[ \t]*\.endm[ \t]*$)")
        matches = list(pattern.finditer(text))
        standard = name.lower() + " " + ", ".join(operands)
        legacy = name.lower() + " " + ", ".join("q$" + str(i) for i in range(len(operands)))
        normalize = lambda body: " ".join(body.replace(",", ", ").split())
        if (len(matches) != 2 or
                sorted(normalize(m[2]) for m in matches) != sorted((standard, legacy))):
            raise ValueError("SHA256 macro source drift")
        for match in matches:
            if normalize(match[2]) == legacy:
                old = match[2]
                new = re.sub(r"q\$(\d)", lambda m: operands[int(m[1])], old)
                replacements += sum(a != "q$" + str(i) for i, a in enumerate(operands))
                text = text[:match.start(2)] + new + text[match.end(2):]
                break
    if replacements != 7:
        raise ValueError("unexpected register spelling delta")
    return text


def render(root):
    root = root.resolve(strict=True)
    texts = {}
    for name, expected in PINS.items():
        path = (root / name).resolve(strict=True)
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError("source escapes pinned root")
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError("pinned SHA256 source SHA mismatch")
        texts[name] = raw.decode("utf-8")
    original = texts[HEADER]
    return "".join(difflib.unified_diff(original.splitlines(keepends=True),
                                      rewrite_legacy(original).splitlines(keepends=True),
                                      fromfile="a/" + HEADER, tofile="b/" + HEADER))


def encode(operation, *registers):
    base, count = ENCODINGS[operation]
    if len(registers) != count or any(type(r) is not int or not 0 <= r < 32 for r in registers):
        raise ValueError("invalid A64 SHA256 register tuple")
    word = base | registers[0] | registers[1] << 5
    return word | (registers[2] << 16 if count == 3 else 0)


def expected_program():
    # The pinned compression routine has four statically emitted round groups
    # plus two groups of four hash pairs. Loops execute some groups repeatedly;
    # this is the static instruction sequence, not an inferred runtime count.
    rounds = []
    for i in range(4):
        rounds.extend((encode("sha256su0", i, (i + 1) % 4),
                       encode("sha256h", 18, 19, 4 + i),
                       encode("sha256su1", i, (i + 2) % 4, (i + 3) % 4),
                       encode("sha256h2", 19, 20, 4 + i)))
    pairs = [word for k in range(4, 8) for word in
             (encode("sha256h", 18, 19, k), encode("sha256h2", 19, 20, k))]
    return rounds + pairs + rounds * 3 + pairs


def audit_text(text):
    if not text or len(text) % 4:
        raise ValueError("unaligned or empty A64 text")
    words, operations = [], []
    for (word,) in struct.iter_unpack("<I", text):
        for operation, (base, count) in ENCODINGS.items():
            variable = 0x3ff | (0x1f0000 if count == 3 else 0)
            if word & (~variable & 0xffffffff) == base:
                words.append(word)
                operations.append(operation)
                break
    if words != expected_program():
        raise ValueError("SHA256 instruction encoding/order/register mismatch")
    return {"scope": "HOST_ONLY_SHA256_INSTRUCTION_ENCODINGS",
            "status": "PINNED_SHA256_CRYPTO_SEQUENCE_VERIFIED",
            "crypto_instructions": len(words), "operations": dict(Counter(operations)),
            "text_sha256": hashlib.sha256(text).hexdigest(),
            "arm_execution": "NOT_RUN", "sha256_runtime_kat": "NOT_RUN",
            "native_xnu_full_link": "NOT_RUN", "xnu_device_entry": "NOT_RUN"}


def macho_text(raw):
    if len(raw) < 32:
        raise ValueError("truncated Mach-O")
    magic, cpu, subtype, kind, count, commands, _, _ = struct.unpack_from("<8I", raw)
    if (magic, cpu, subtype, kind) != (0xfeedfacf, 0x100000c, 0, 1):
        raise ValueError("expected non-arm64e ARM64 MH_OBJECT")
    end = 32 + commands
    if not 0 < count <= 1024 or end > len(raw):
        raise ValueError("Mach-O command bounds")
    position, found = 32, []
    for _ in range(count):
        if position + 8 > end:
            raise ValueError("Mach-O command header bounds")
        code, size = struct.unpack_from("<II", raw, position)
        if size < 8 or size % 8 or position + size > end:
            raise ValueError("Mach-O command bounds")
        if code == 0x19:
            if size < 72:
                raise ValueError("Mach-O segment bounds")
            sections = struct.unpack_from("<I", raw, position + 64)[0]
            if size != 72 + sections * 80:
                raise ValueError("Mach-O section table bounds")
            for index in range(sections):
                at = position + 72 + index * 80
                section, segment, _, length, offset = struct.unpack_from("<16s16sQQI", raw, at)
                if section.rstrip(b"\0") == b"__text" and segment.rstrip(b"\0") == b"__TEXT":
                    if offset < end or offset + length > len(raw):
                        raise ValueError("Mach-O text bounds")
                    found.append(raw[offset:offset + length])
        position += size
    if position != end or len(found) != 1:
        raise ValueError("expected exactly one Mach-O text section")
    return found[0]


def audit_object(path):
    raw = path.read_bytes()
    return {**audit_text(macho_text(raw)), "object_sha256": hashlib.sha256(raw).hexdigest(),
            "object_bytes": len(raw), "expected_source_pins": PINS,
            "object_source_provenance": "REQUIRES_SEPARATE_COMPILER_RECEIPT"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    patch = sub.add_parser("patch")
    patch.add_argument("--xnu-original-root", type=Path, required=True)
    audit = sub.add_parser("audit")
    audit.add_argument("--object", type=Path, required=True)
    for command in (patch, audit):
        command.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        data = (render(args.xnu_original_root) if args.mode == "patch" else
                json.dumps(audit_object(args.object), indent=2) + "\n")
        target = args.out.resolve()
        if (target.is_relative_to(REPO) or
                (args.mode == "patch" and target.is_relative_to(args.xnu_original_root.resolve()))):
            raise ValueError("output must remain outside sources and public repository")
        with target.open("x", encoding="utf-8") as output:
            output.write(data)
    except (OSError, UnicodeError, ValueError, struct.error) as error:
        parser.exit(1, type(error).__name__ + ": SHA256 patch/audit refused\n")
    print("HOST_ONLY_SHA256_" + args.mode.upper() + " sha256=" + hashlib.sha256(data.encode()).hexdigest())


if __name__ == "__main__":
    main()
