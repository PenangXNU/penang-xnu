#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate ARM64 Mach-O link inputs and reject duplicate strong providers.

An explicit repeated source identity is coalesced only when both source and
complete object hashes agree. Unrelated sources with conflicting definitions
are errors, never silently dropped. Empty units remain classified separately.
This is object selection, not a substitute for executing the real linker.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from p1_native_inputs import canonical_path
from p1_t1_entry import read, write_exclusive


def sha(data):
    return hashlib.sha256(data).hexdigest()


def inspect(path, nm):
    path = Path(path)
    for part in [path, *path.parents]:
        if part.is_symlink():
            raise ValueError("symlinked link input")
    data = read(path)
    if len(data) < 32 or struct.unpack_from("<4I", data) != (0xfeedfacf, 0x100000c, 0, 1):
        raise ValueError("non-arm64e ARM64 MH_OBJECT required")
    result = subprocess.run([str(nm), "-m", "--extern-only", str(path)],
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise ValueError("object symbol inspection failed")
    strong, weak, undefined = [], [], []
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        symbol = line.split()[-1]
        if "(undefined)" in line:
            undefined.append(symbol)
        elif "external" in line:
            (weak if "weak " in line or "(common)" in line else strong).append(symbol)
    return {"object":str(path), "object_sha256":sha(data), "bytes":len(data),
            "strong":sorted(strong), "weak_or_common":sorted(weak), "undefined":sorted(undefined),
            "classification":"COMPILE_ONLY" if strong or weak else "COMPILED_EMPTY_NONPROVIDER"}


def select(rows, nm):
    if not isinstance(rows, list) or not rows:
        raise ValueError("nonempty link-input rows required")
    selected, aliases, identities, providers = [], [], {}, {}
    for row in rows:
        if not isinstance(row,dict) or not isinstance(row.get("object"),str) or not row["object"]:
            raise ValueError("link input row requires an object path")
        identity, source_hash = row.get("source_identity"), row.get("source_sha256")
        if not isinstance(identity, str) or not canonical_path(identity) or not isinstance(source_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", source_hash):
            raise ValueError("canonical source identity and SHA-256 required")
        item = {**inspect(row["object"], nm), "source_identity":identity, "source_sha256":source_hash}
        if identity in identities:
            previous = identities[identity]
            if any(item[k] != previous[k] for k in ("source_sha256", "object_sha256")):
                raise ValueError("conflicting repeated source identity: " + identity)
            aliases.append({"source_identity":identity, "object":item["object"], "selected":previous["object"],
                            "source_sha256":source_hash, "object_sha256":item["object_sha256"]})
            continue
        for symbol in item["strong"]:
            if symbol in providers:
                raise ValueError("duplicate strong provider: " + symbol)
            providers[symbol] = item["object"]
        identities[identity] = item
        selected.append(item)
    return {"scope":"COMPILE_ONLY_LINK_INPUT_AUDIT", "selected":selected, "exact_source_aliases":aliases,
            "nonempty_objects":sum(x["classification"]=="COMPILE_ONLY" for x in selected),
            "empty_nonproviders":sum(x["classification"]=="COMPILED_EMPTY_NONPROVIDER" for x in selected),
            "link":"NOT_RUN", "device_entry":"NOT_RUN"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = select(json.loads(read(args.plan))["units"], args.nm)
        write_exclusive(args.out, (json.dumps(report, indent=2)+"\n").encode(), (args.plan,))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, "link input selection refused: " + str(error) + "\n")
    print(json.dumps({"selected":len(report["selected"]), "aliases":len(report["exact_source_aliases"])}))


if __name__=="__main__":
    main()
