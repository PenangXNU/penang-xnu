#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Pinned start.s-only board delta and structural ARM64 entry-object audit.

HOST_ONLY: neither compiler equivalence, physical CPU/errata qualification nor
a linked XNU kernel follows from these checks. No device or QEMU operations.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import stat
import struct

SOURCE = "osfmk/arm64/start.s"
SOURCE_SHA = "c822541b8c76a2759352960b5f6b3eda26e354ea4b9df5a0823fb6b37f37be72"
RESULT_SHA = "81bf313523423cebba9856258387542e65af71cdf4749cb16f84ddf888ab9550"
P1_RESULT_SHA = "6425f16b2054be8e38227c40ae2923ab3dc169876e0770c2bc6bbaca9148d41f"
# The diagnostic profile must not borrow the protected PENANG_PHYSICAL marker.
# Both original tunables sites are suppressed only for a complete explicit P1
# code-generation selection; other inputs retain the original Apple path.
P1_OPTINS = ("PENANG_P1_DIAGNOSTIC", "ARM64_BOARD_CONFIG_PENANG_P1_DIAGNOSTIC",
             "PENANG_P1_SANDBOX_COMPILE", "PENANG_P1_IOMMU_CODEGEN_ONLY", "NO_MONITOR")
P1_TUNABLES_GUARD = ("#if !defined(PENANG_PHYSICAL) && !(" +
                     " && ".join("defined(" + name + ") && (" + name + " == 1)" for name in P1_OPTINS) + ")\n")
REPO = Path(__file__).resolve().parents[2]
EDITS = (
    "#include <arm64/tunables/tunables.s>\n",
    "\t// Read MIDR before start of per-SoC tunables\n\tmrs x12, MIDR_EL1\n\n"
    "\tAPPLY_TUNABLES x12, x13, x14\n",
)
# Exact A64 encodings for the reviewed board's original common-start sequence.
OPERATIONS = {"TCR_EL1": 0xD5182040, "TTBR1_EL1": 0xD5182020,
              "MAIR_EL1": 0xD518A200, "VBAR_EL1": 0xD518C000,
              "SCTLR_EL1": 0xD5181000, "TLBI_VMALLE1": 0xD508871F,
              "DSB_ISH": 0xD5033B9F, "ISB": 0xD5033FDF,
              "IC_IALLU": 0xD508751F, "RET": 0xD65F03C0}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode), "regular nonsymlink input required")
    with path.open("rb") as stream:
        data = stream.read(16_000_001)
    require(0 < len(data) <= 16_000_000, "input size outside bounded profile")
    return data


def source_delta(root, p1_diagnostic=False):
    root = root.resolve(strict=True)
    path = (root / SOURCE).resolve(strict=True)
    require(path.is_relative_to(root), "source escapes original root")
    original = read(path)
    require(sha(original) == SOURCE_SHA, "original XNU start.s SHA mismatch")
    text = original.decode("utf-8")
    updated = text
    guard = P1_TUNABLES_GUARD if p1_diagnostic else "#if !defined(PENANG_PHYSICAL)\n"
    result_sha = P1_RESULT_SHA if p1_diagnostic else RESULT_SHA
    for anchor in EDITS:
        require(updated.count(anchor) == 1, "missing or ambiguous source anchor")
        updated = updated.replace(anchor, guard + anchor + "#endif\n", 1)
    require(sha(updated.encode()) == result_sha, "reviewed T1 source result mismatch")
    patch = "".join(difflib.unified_diff(text.splitlines(True), updated.splitlines(True),
                                       fromfile="a/" + SOURCE, tofile="b/" + SOURCE)).encode()
    return patch, {"scope": "HOST_ONLY", "source": SOURCE, "original_sha256": SOURCE_SHA,
                   "result_sha256": result_sha, "patch_sha256": sha(patch),
                   "profile": "P1_DIAGNOSTIC_CODEGEN_ONLY" if p1_diagnostic else "PENANG_PHYSICAL",
                   "changed_files": [SOURCE], "physical_cpu_errata": "NOT_VERIFIED",
                   "native_xnu_full_link": "NOT_RUN", "xnu_device_entry": "NOT_RUN"}


def audit(data):
    """Bounded MH_OBJECT parser; structural presence is not execution proof."""
    require(32 <= len(data) <= 16_000_000, "Mach-O size")
    magic, cpu, subtype, kind, ncmds, command_bytes, _, _ = struct.unpack_from("<8I", data)
    require((magic, cpu, subtype, kind) == (0xFEEDFACF, 0x0100000C, 0, 1), "non-arm64e ARM64 MH_OBJECT required")
    end = 32 + command_bytes
    require(0 < ncmds <= 128 and end <= len(data), "load command bounds")
    at, ordinal, text, symtab = 32, 0, None, None
    for _ in range(ncmds):
        require(at + 8 <= end, "truncated load command")
        command, size = struct.unpack_from("<II", data, at)
        require(size >= 8 and size % 8 == 0 and at + size <= end, "load command extent")
        if command == 0x19:
            require(size >= 72, "segment extent")
            count = struct.unpack_from("<I", data, at + 64)[0]
            require(count <= 256 and size == 72 + 80 * count, "section table extent")
            for index in range(count):
                ordinal += 1
                row = struct.unpack_from("<16s16sQQ8I", data, at + 72 + 80 * index)
                sect, seg, address, length, offset, align, reloc, nreloc, flags, _, _, _ = row
                if (sect.rstrip(b"\0"), seg.rstrip(b"\0")) == (b"__text", b"__TEXT"):
                    require(text is None and length >= 4 and length % 4 == 0 and address % 4 == 0,
                            "unique nonempty instruction section required")
                    require(offset >= end and offset + length <= len(data) and align <= 31,
                            "text section bounds")
                    require(flags & 0xFF == 0 and nreloc <= len(data) // 8
                            and (not nreloc or reloc >= end) and reloc + 8 * nreloc <= len(data),
                            "text relocation bounds")
                    text = dict(address=address, size=length, offset=offset, reloc=reloc,
                                nreloc=nreloc, ordinal=ordinal)
        elif command == 2:
            require(size == 24 and symtab is None, "symbol table command")
            symtab = struct.unpack_from("<4I", data, at + 8)
        at += size
    require(at == end and text is not None and symtab is not None, "text and complete symbol table required")
    symoff, nsyms, stroff, strsize = symtab
    require(symoff >= end and 0 < nsyms <= len(data) // 16 and symoff + 16 * nsyms <= len(data)
            and stroff >= end and strsize > 0 and stroff + strsize <= len(data), "symbol/string table bounds")
    symbols, indexed = {}, []
    for index in range(nsyms):
        string, typ, section, _, value = struct.unpack_from("<IBBHQ", data, symoff + 16 * index)
        require(string < strsize, "symbol name index")
        stop = data.find(b"\0", stroff + string, stroff + strsize)
        require(stop >= 0, "unterminated symbol name")
        name = data[stroff + string:stop].decode("utf-8")
        indexed.append(name)
        if not typ & 0xE0:
            require(name not in symbols, "duplicate symbol name")
            symbols[name] = {"value": value, "type": typ, "section": section}
    for name in ("__start", "_start_first_cpu"):
        symbol = symbols.get(name, {})
        require(symbol.get("type", 0) & 0xF == 0xF and symbol.get("section") == text["ordinal"],
                "real external entry symbols required")
        require(text["address"] <= symbol["value"] <= text["address"] + text["size"] - 4
                and symbol["value"] % 4 == 0, "entry symbol outside text")
    body = data[text["offset"]:text["offset"] + text["size"]]
    word = lambda offset: struct.unpack_from("<I", body, offset)[0]
    entry = symbols["__start"]["value"]
    first = symbols["_start_first_cpu"]["value"]
    ins = word(entry - text["address"])
    immediate = ins & 0x03FFFFFF
    if immediate & 0x02000000:
        immediate -= 0x04000000
    require(ins & 0xFC000000 == 0x14000000 and entry != first and entry + 4 * immediate == first,
            "entry must unconditionally branch to first CPU entry")
    relocs = []
    for index in range(text["nreloc"]):
        address, bits = struct.unpack_from("<iI", data, text["reloc"] + 8 * index)
        require(0 <= address <= len(body) - 4 and address % 4 == 0, "relocation address")
        symbol_index, external = bits & 0xFFFFFF, (bits >> 27) & 1
        require(not external or symbol_index < len(indexed), "relocation symbol index")
        require(address != entry - text["address"], "entry branch must be resolved locally")
        if external:
            relocs.append({"offset": address, "symbol": indexed[symbol_index], "pcrel": (bits >> 24) & 1,
                           "length": (bits >> 25) & 3, "kind": bits >> 28})
    pairs = []
    for reloc in relocs:
        offset = reloc["offset"]
        if (reloc["symbol"], reloc["kind"], reloc["pcrel"], reloc["length"]) != ("_arm_init", 3, 1, 2):
            continue
        other = dict(offset=offset + 4, symbol="_arm_init", pcrel=0, length=2, kind=4)
        if other in relocs and offset + 8 <= len(body):
            adrp, add = word(offset), word(offset + 4)
            if (adrp & 0x9F00001F == 0x9000001E and add & 0xFFC003FF == 0x910003DE):
                pairs.append(offset)
    require(len(pairs) == 1 and first - text["address"] <= pairs[0], "paired arm_init address materialization into LR required")
    words = list(struct.unpack("<" + "I" * (len(body) // 4), body))
    checks = {name: any((w & 0xFFFFFFE0 == encoding if name.endswith("_EL1") else w == encoding)
                        for w in words) for name, encoding in OPERATIONS.items()}
    require(all(checks.values()), "missing native MMU/exception/cache/TLB instructions")
    return {"schema": "penang-p1-entry-object-v1", "scope": "HOST_ONLY",
            "status": "STRUCTURAL_ENTRY_OBJECT_ONLY", "sha256": sha(data), "bytes": len(data),
            "text": {"bytes": len(body), "sha256": sha(body)},
            "symbols": {name: symbols[name] for name in ("__start", "_start_first_cpu")},
            "relocations": relocs, "arm_init_page_pair_offset": pairs[0], "instructions": checks,
            "physical_cpu_errata": "NOT_VERIFIED", "native_xnu_full_link": "NOT_RUN",
            "xnu_device_entry": "NOT_RUN"}


def write_exclusive(path, data, protected=()):
    target = path.resolve()
    require(not target.is_relative_to(REPO) and all(not target.is_relative_to(p.resolve()) for p in protected),
            "output must remain outside original sources and public repository")
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open("xb") as stream:
        stream.write(data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="mode", required=True)
    emit = sub.add_parser("emit")
    emit.add_argument("--source-root", type=Path, required=True)
    emit.add_argument("--out", type=Path, required=True)
    emit.add_argument("--p1-diagnostic", action="store_true",
                      help="emit the separate P1 selection without a protected-board alias")
    check = sub.add_parser("audit")
    check.add_argument("--object", type=Path, required=True)
    check.add_argument("--reference-object", type=Path)
    check.add_argument("--out", type=Path)
    a = p.parse_args()
    try:
        if a.mode == "emit":
            patch, report = source_delta(a.source_root, a.p1_diagnostic)
            write_exclusive(a.out, patch, (a.source_root,))
        else:
            report = audit(read(a.object))
            if a.reference_object:
                reference = audit(read(a.reference_object))
                report["reference"] = {"sha256": reference["sha256"], "text_sha256": reference["text"]["sha256"],
                                       "identical_text": report["text"] == reference["text"],
                                       "identical_relocations": report["relocations"] == reference["relocations"]}
            if a.out:
                write_exclusive(a.out, (json.dumps(report, indent=2) + "\n").encode())
    except (OSError, UnicodeError, ValueError, struct.error) as exc:
        p.exit(1, f"entry preflight rejected: {exc}\n")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
