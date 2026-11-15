#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Read-only inventory of retained native build inputs, never a build/boot gate.

Makefile references are inspected lexically, not evaluated or executed. This
cannot establish the selected link closure, active compiler flags or providers.
The preserved B4.2 assessor and P1 entry checker remain separate subchecks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat

from p1_native_entry_frontier import inspect as inspect_entry, PHYSICAL_GUARD, BLOCKER
from verify_b42_pmap_lockdown import assess_b42, extract_makefile_sources

MAX_INPUT = 12_000_000
INPUTS = ("osfmk_make", "iokit_make", "osfmk_config", "board_header")


def read_text(path):
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError("input must be a regular file")
    with path.open("rb") as stream:
        data = stream.read(MAX_INPUT + 1)
    if not data or len(data) > MAX_INPUT or b"\0" in data:
        raise ValueError("invalid or oversized input")
    return data.decode("utf-8"), {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def make_text(text):
    # Generated doconf references have no quoted/escaped hashes. Unsupported
    # source references are rejected below rather than partially admitted.
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def c_text(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def references(text):
    tokens = re.findall(r"\$\(SOURCE_DIR\)/([^\s\\]+)", text)
    paths = extract_makefile_sources(text)
    for token in tokens:
        parts = PurePosixPath(token).parts
        if (not re.fullmatch(r"[A-Za-z0-9_./+-]+\.(?:cpp|c|s|S)", token)
                or token.startswith("/") or any(p in (".", "..") for p in token.split("/"))
                or not parts or str(PurePosixPath(token)) != token):
            raise ValueError("unsupported source reference")
    if set(tokens) != set(paths) or not paths:
        raise ValueError("empty or partially parsed source references")
    return paths


def top_level_guard(source):
    """Require an unconditional top-level physical #if/#error/#endif block.

    Deliberately bounded: alternative guard spellings need separate review.
    This checks the explicit refusal itself, not the rest of the preprocessor.
    """
    source = c_text(source)
    pattern = (re.escape(PHYSICAL_GUARD) + r'\s*\n\s*#error\s+"[^"\n]*'
               + re.escape(BLOCKER) + r'[^"\n]*"\s*\n\s*#endif\b')
    depth = 0
    for line in source.splitlines(keepends=True):
        if depth == 0 and re.match(pattern, source):
            return True
        directive = re.match(r"\s*#\s*(if|ifdef|ifndef|endif)\b", line)
        if directive:
            depth += -1 if directive[1] == "endif" else 1
            if depth < 0:
                return False
        source = source[len(line):]
    return False


def inspect(original, guarded=None, *, osfmk_make=None, iokit_make=None,
            osfmk_config=None, board_header=None):
    result = {
        "schema": "penang-p1-native-inputs-v1", "scope": "HOST_ONLY",
        "status": "BLOCKED", "reference_semantics": "LEXICAL_NOT_MAKE_EVALUATION",
        "compile": "NOT_RUN", "native_xnu_full_link": "NOT_RUN",
        "xnu_device_entry": "NOT_RUN", "userland_penang_init": "NOT_RUN",
        "pmap_physical": "B42_BLOCKED_BACKEND_UNVERIFIED",
        "inputs": {}, "sources": {}, "blockers": [],
    }
    inputs = dict(osfmk_make=osfmk_make, iokit_make=iokit_make,
                  osfmk_config=osfmk_config, board_header=board_header)
    texts = {}
    for name, path in inputs.items():
        try:
            if path is None:
                raise FileNotFoundError
            texts[name], result["inputs"][name] = read_text(path)
        except (OSError, UnicodeError, ValueError) as exc:
            result["blockers"].append({"code": "MISSING_OR_INVALID_INPUT", "input": name,
                                       "error": type(exc).__name__})
    if guarded is None:
        result["blockers"].append({"code": "MISSING_GUARDED_TREE", "input": "b42_guarded_root"})
    # Always inventory upstream entry files, even while private recipes are
    # unavailable. Never substitute this tree for the guarded native tree.
    for name in ("osfmk/arm64/start.s", "osfmk/arm/arm_init.c"):
        try:
            path = (original / name).resolve(strict=True)
            if not path.is_relative_to(original.resolve()):
                raise ValueError("source escapes root")
            _, result["sources"][name] = read_text(path)
        except (OSError, UnicodeError, ValueError) as exc:
            result["blockers"].append({"code": "ORIGINAL_SOURCE_UNAVAILABLE", "source": name,
                                       "error": type(exc).__name__})
    if result["blockers"]:
        return result
    try:
        osfmk = make_text(texts["osfmk_make"])
        iokit = make_text(texts["iokit_make"])
        config = make_text(texts["osfmk_config"])
        board = c_text(texts["board_header"])
        paths = sorted(set(references(osfmk) + references(iokit)))
    except ValueError:
        result["status"] = "REJECTED"
        result["blockers"].append({"code": "UNSUPPORTED_SOURCE_REFERENCES"})
        return result
    result["reference_count"] = len(paths)
    missing = []
    for name in paths:
        try:
            path = (guarded / name).resolve(strict=True)
            if not path.is_relative_to(guarded.resolve()):
                raise ValueError("source escapes root")
            _, result["sources"]["guarded/" + name] = read_text(path)
        except (OSError, UnicodeError, ValueError) as exc:
            missing.append(name)
            result["blockers"].append({"code": "SOURCE_UNAVAILABLE_OR_ESCAPES_ROOT", "source": name,
                                       "error": type(exc).__name__})
    assessment = assess_b42(iokit, osfmk, config, board_header_text=board,
                            missing_public_sources=missing)
    result["b42"] = assessment
    required = ("DOCONF_NATIVE_VALID", "BOOTSTRAP_CORE_SOURCES",
                "PUBLIC_DROP_SOURCE_COMPLETENESS", "NO_QEMU_CONTAMINATION",
                "NO_SILENT_FALLBACK", "NO_FAKE_LOCKDOWN_STUBS",
                "DIAGNOSTIC_CLEAN_PUBLIC_SELECTION")
    for check in required:
        if assessment[check] != "PASS":
            result["blockers"].append({"code": "B42_INPUT_CHECK_FAILED", "check": check})
    # These are conservative textual requirements, not compiler flag evaluation.
    combined = "\n".join((osfmk, iokit, config, board))
    for macro in ("CONFIG_PMAP_PPL", "CONFIG_KERNEL_INTEGRITY"):
        if (not re.search(r"(?m)^\s*options\s+" + macro + r"\s*$", config)
                or re.search(r"(?:-U\s*" + macro + r"\b|-D\s*" + macro + r"\s*=|"
                             r"#\s*(?:define|undef)\s+" + macro + r"\b)", combined)):
            result["blockers"].append({"code": "UNSUPPORTED_SECURITY_SELECTOR", "macro": macro})
    # Apply the legacy checks to *both* components, not OSFMK flags alone.
    profile = "\n".join((osfmk, iokit, config))
    for token in ("NO_MONITOR", "XNU_NO_TYPE_BUILTINS", "QEMU_VIRT",
                  "ARM64_BOARD_CONFIG_QEMU_VIRT", "CONFIG_SPTM"):
        if (re.search(r"(?<![A-Za-z0-9_])(?:-D|-U)?" + token + r"\b", profile)
                or re.search(r"(?m)^\s*#\s*define\s+" + token + r"\b", board)):
            result["blockers"].append({"code": "FORBIDDEN_PROFILE_TOKEN", "token": token})
    guards_valid = True
    for name in ("osfmk/arm64/arm_vm_init.c", "osfmk/arm/pmap/pmap.c"):
        try:
            path = (guarded / name).resolve(strict=True)
            safe = path.is_relative_to(guarded.resolve())
            source = read_text(path)[0] if safe else ""
            valid = top_level_guard(source)
        except (OSError, UnicodeError, ValueError):
            valid = False
        if not valid:
            guards_valid = False
            result["blockers"].append({"code": "TOP_LEVEL_B42_REFUSAL_MISSING", "source": name})
    # The fixed entry-checker paths must be safe even when their references
    # were omitted from a supplied Makefile. Do not let the legacy reader
    # follow an unchecked path merely to produce a failing subcheck.
    result["entry"] = (inspect_entry(original, guarded) if not missing and guards_valid
                       else {"status": "NOT_RUN"})
    if result["entry"]["status"] != "P1_SOURCE_ENTRY_CHAIN_VERIFIED_B42_COMPILE_HOLD":
        result["blockers"].append({"code": "SOURCE_ENTRY_CHECK_FAILED"})
    result["status"] = "REJECTED" if result["blockers"] else "INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xnu-original-root", type=Path, required=True)
    parser.add_argument("--b42-guarded-root", type=Path)
    for name in INPUTS:
        parser.add_argument("--" + name.replace("_", "-"), type=Path)
    parser.add_argument("--out", type=Path, help="Exclusive receipt; no overwrite")
    args = parser.parse_args()
    result = inspect(args.xnu_original_root, args.b42_guarded_root,
                     **{name: getattr(args, name) for name in INPUTS})
    data = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        try:
            with args.out.open("x", encoding="utf-8") as stream:
                stream.write(data)
        except OSError:
            parser.exit(1, "receipt creation refused\n")
    print(data, end="")
    return {"BLOCKED": 2, "REJECTED": 1, "INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD": 0}[result["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
