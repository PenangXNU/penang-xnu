#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fresh selected-TU ARM64 Mach-O probe; not a T6 replay or native kernel build.

Inputs and logs can be private. Never execute Makefiles, modify sources, link a
kernel, invoke QEMU or operate a device. Complete recipes, generated headers and
physical providers remain separate prerequisites. Compilation preserves B4.2.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import os

from p1_native_inputs import canonical_path, make_text, read_text, references, top_level_guard

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
GUARDS = ("osfmk/arm64/arm_vm_init.c", "osfmk/arm/pmap/pmap.c")
REQUIRED = {"CONFIG_PMAP_PPL", "CONFIG_KERNEL_INTEGRITY"}
FORBIDDEN = {
    "NO_MONITOR", "XNU_NO_TYPE_BUILTINS", "QEMU_VIRT", "ARM64_BOARD_CONFIG_QEMU_VIRT",
    "APPLEVIRTUALPLATFORM", "VMAPPLE", "CONFIG_SPTM", "MONITOR", "XNU_MONITOR",
    "PMAP_CS", "PMAP_CS_ENABLE", "PENANG_B43_VERIFIED_PHYSICAL_LOCKDOWN",
    "__arm64e__", "__ARM_FEATURE_PAUTH", "CPU_HAS_APPLE_PAC", "HAS_APPLE_PAC",
    "HAS_PARAVIRTUALIZED_PAC", "HAS_GUARDED_IO_FILTER", "APPLE_ARM64_ARCH_FAMILY",
    "KERNEL_INTEGRITY_KTRR", "KERNEL_INTEGRITY_CTRR", "KERNEL_INTEGRITY_WT",
}
BASE = ["--target=arm64-apple-darwin", "-mcpu=cortex-a76", "-ffreestanding",
        "-nostdlibinc", "-fno-builtin", "-DAPPLE", "-DKERNEL", "-DKERNEL_PRIVATE",
        "-DXNU_KERNEL_PRIVATE", "-DPRIVATE", "-D__MACHO__=1",
        "-DARM64_BOARD_CONFIG_PENANG_PHYSICAL"]
COMPONENT_FLAGS = {
    "osfmk": ["-DMACH_KERNEL_PRIVATE", "-DMACH_KERNEL"],
    "iokit": ["-DDRIVER_PRIVATE", "-DIOKIT_KERNEL_PRIVATE", "-DDRIVERKIT_PRIVATE=1",
              "-DIOMATCHDEBUG=1", "-DIOALLOCDEBUG=1", "-DPRIVATE_WIFI_ONLY=1", "-DIOASSERT=0"],
}
CXX_FLAGS = ["-std=gnu++2b", "-fsized-deallocation", "-fapple-kext", "-fno-rtti", "-fno-exceptions"]
LINK_ADDRESS = "0xfffffe0007004000"
# Pinned XNU MakeInc.def MacOSX ARM64 CFLAGS; still a fresh selected-TU probe.
NATIVE_C_FLAGS = ["-Darm64", "-DARM64", "-D__ARM64__", "-DLP64", "-DPAGE_SIZE_FIXED",
                  "-DVM_KERNEL_LINK_ADDRESS=" + LINK_ADDRESS, "-DARM_LARGE_MEMORY=1",
                  "-momit-leaf-frame-pointer", "-fno-strict-aliasing", "-D__API__=v4", "-mkernel"]
PROFILE_MACROS = REQUIRED | {"PENANG_PHYSICAL", "ARM64_BOARD_CONFIG_PENANG_PHYSICAL",
                             "__ARM_16K_PG__", "__arm64__", "__aarch64__"}


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def config_flags(text):
    lines = re.findall(r"(?m)^export CONFIG_DEFINES =([^\n]*)$", make_text(text))
    if len(lines) != 1:
        raise ValueError("exactly one CONFIG_DEFINES assignment required")
    flags = shlex.split(lines[0])
    fixed = BASE + NATIVE_C_FLAGS + [x for values in COMPONENT_FLAGS.values() for x in values]
    reserved = {x[2:].split("=", 1)[0] for x in fixed if x.startswith("-D")}
    reserved.update(re.findall(r"(?m)^#define\s+(\w+)", (HERE / "PENANG_PHYSICAL.h").read_text()))
    seen = set()
    for flag in flags:
        match = re.fullmatch(r"-D([A-Za-z_][A-Za-z0-9_]*)(?:=([^\r\n\x00]*))?", flag)
        if not match:
            raise ValueError("CONFIG_DEFINES must contain only -D arguments")
        name, value = match.groups()
        if (name in seen or name in FORBIDDEN or name in reserved
                or name.startswith("ARM64_BOARD_CONFIG_")
                or name.startswith("APPLE") or name.startswith("__arm")
                or (name in REQUIRED and value is not None)):
            raise ValueError("duplicate, forbidden or overridden configuration macro")
        seen.add(name)
    if not REQUIRED <= seen:
        raise ValueError("physical security selectors must remain present")
    return flags


def source_path(root, name):
    if not canonical_path(name):
        raise ValueError("noncanonical source path")
    path = (root / name).resolve(strict=True)
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError("source missing or escapes source root")
    return path


def tool(name, env=None):
    found = shutil.which(str(name))
    if not found:
        raise ValueError("required compiler or LLVM tool unavailable")
    path = Path(found).resolve(strict=True)
    version = subprocess.run([str(path), "--version"], env=env, text=True, capture_output=True, timeout=30)
    if version.returncode:
        raise ValueError("tool version query failed")
    return str(path), {"path": str(path), "sha256": digest(path), "version": version.stdout.strip()}


def inspect_object(path, nm, env=None):
    if not path.is_file() or path.is_symlink():
        raise ValueError("compiler produced no regular object")
    with path.open("rb") as stream:
        header = stream.read(32)
    if len(header) != 32:
        raise ValueError("truncated Mach-O object")
    magic, cpu, subtype, kind = struct.unpack_from("<4I", header)
    if (magic, cpu, subtype, kind) != (0xFEEDFACF, 0x0100000C, 0, 1):
        raise ValueError("expected non-arm64e ARM64 MH_OBJECT")
    command = [nm, "--defined-only", "--extern-only", str(path)]
    symbols = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
    if symbols.returncode or not symbols.stdout.strip():
        raise ValueError("object has no inspectable external definitions")
    return {"sha256": digest(path), "bytes": path.stat().st_size,
            "format": "ARM64_MH_OBJECT", "defined_symbols": symbols.stdout.splitlines(),
            "symbol_command": command}


def probe(source_root, osfmk_make, iokit_make, board_header, sources, out,
          include_dirs=(), clang="clang", clangxx="clang++", llvm_nm="llvm-nm"):
    root = source_root.resolve(strict=True)
    output = out.resolve()
    if output.is_relative_to(root) or output.is_relative_to(REPO):
        raise ValueError("output must be outside source tree and public repository")
    if output.exists():
        raise ValueError("output directory already exists")
    board, board_info = read_text(board_header)
    if board.encode() != (HERE / "PENANG_PHYSICAL.h").read_bytes():
        raise ValueError("board header differs from reviewed public physical contract")
    for name in GUARDS:
        if not top_level_guard(read_text(source_path(root, name))[0]):
            raise ValueError("B4.2 physical compile refusal missing")
    recipes, configs, selected = {}, {}, {}
    for component, path in (("osfmk", osfmk_make), ("iokit", iokit_make)):
        text, recipes[component] = read_text(path)
        configs[component] = config_flags(text)
        selected[component] = set(references(make_text(text)))
    chosen = []
    if not sources or len(set(sources)) != len(sources):
        raise ValueError("nonempty distinct source selection required")
    for name in sources:
        component = name.split("/", 1)[0]
        if component not in selected or name not in selected[component]:
            raise ValueError("source is not a literal reference in its component recipe")
        path = source_path(root, name)
        if path.suffix not in (".c", ".cpp", ".s", ".S"):
            raise ValueError("unsupported source language")
        chosen.append((name, component, path))
    includes = [Path(p).resolve(strict=True) for p in include_dirs]
    if any(not p.is_dir() for p in includes):
        raise ValueError("generated include path must be a directory")
    env = os.environ.copy()
    removed = []
    for key in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "CCC_OVERRIDE_OPTIONS", "CL", "_CL_"):
        if key in env:
            removed.append(key)
            del env[key]
    cc, cc_info = tool(clang, env)
    cxx, cxx_info = tool(clangxx, env)
    nm, nm_info = tool(llvm_nm, env)
    # Exclusive output creation occurs only after all paths/configuration pass.
    output.mkdir(parents=True, exist_ok=False)
    board_snapshot = output / "PENANG_PHYSICAL.h"
    board_snapshot.write_bytes(board.encode())
    report = {"schema": "penang-p1-fresh-compile-probe-v1", "scope": "HOST_ONLY",
              "recipe": "FRESH_EXPLICIT_SELECTED_TU_NOT_T6_REPLAY", "status": "COMPILE_HOLD",
              "native_xnu_full_link": "NOT_RUN", "xnu_device_entry": "NOT_RUN",
              "userland_penang_init": "NOT_RUN", "pmap_physical": "B42_BLOCKED_BACKEND_UNVERIFIED",
              "source_root": str(root), "board_header": board_info, "recipes": recipes,
              "board_header_snapshot": str(board_snapshot),
              "native_c_flags": NATIVE_C_FLAGS, "macosx_arm64_link_address": LINK_ADDRESS,
              "config_defines": configs, "generated_include_dirs": list(map(str, includes)),
              "tools": {"clang": cc_info, "clangxx": cxx_info, "llvm_nm": nm_info},
              "ignored_environment_variables": removed, "units": [], "compiled_objects": 0}
    for index, (name, component, path) in enumerate(chosen):
        obj = output / f"{index:03d}-{path.stem}.o"
        language = "assembler-with-cpp" if path.suffix in (".s", ".S") else "c++" if path.suffix == ".cpp" else "c"
        generated = [p / component if (p / component / "meta_features.h").is_file() else p for p in includes]
        include_args = []
        for directory in [*generated, *(root / d for d in
                (".", "osfmk", "iokit", "pexpert", "bsd", "libkern", "osfmk/libsa", "EXTERNAL_HEADERS"))]:
            include_args.extend(("-I", str(directory)))
        feature = next((p / "meta_features.h" for p in generated if (p / "meta_features.h").is_file()), None)
        if generated and feature is None:
            raise ValueError("generated meta_features.h unavailable")
        feature_args = ["-include", str(feature.resolve())] if feature else []
        component_flags = COMPONENT_FLAGS[component] if language != "assembler-with-cpp" else []
        # MakeInc.def SFLAGS_ARM64 inherits CFLAGS_ARM64 as well; assym and
        # assembly must use the same native address-space/layout definitions.
        language_flags = (NATIVE_C_FLAGS + CXX_FLAGS + ["-fblocks"] if language == "c++" else
                          NATIVE_C_FLAGS + ["-fblocks"] if language == "c" else
                          NATIVE_C_FLAGS + ["-D__ASSEMBLER__", "-DASSEMBLER"])
        argv = [cxx if language == "c++" else cc, *BASE, "-x", language,
                "-include", str(board_snapshot), *configs[component],
                *component_flags, *language_flags, *feature_args, *include_args,
                "-c", str(path), "-o", str(obj)]
        row = {"source": name, "source_sha256": digest(path), "command": argv,
               "status": "BLOCKED", "log": f"{index:03d}-{path.stem}.log"}
        if feature:
            row["feature_header"] = {"path": str(feature.resolve()), "sha256": digest(feature)}
        try:
            if (digest(board_header) != board_info["sha256"]
                    or digest(board_snapshot) != board_info["sha256"]):
                raise ValueError("BOARD_HEADER_CHANGED_DURING_PROBE")
            # Check the actual forced includes before compiling the TU: feature
            # headers may not erase physical identity or enable another profile.
            macro_command = argv[:-4] + ["-E", "-dM", "-"]
            state = subprocess.run(macro_command, input="", cwd=root, env=env,
                                   text=True, capture_output=True, timeout=30)
            macros = dict(re.findall(r"(?m)^#define ([A-Za-z_][A-Za-z0-9_]*)[ \t]*(.*)$", state.stdout))
            row["macro_state_command"] = macro_command
            row["macro_state_sha256"] = hashlib.sha256(state.stdout.encode()).hexdigest()
            row["macro_state_returncode"] = state.returncode
            invalid = sorted(macro for macro in PROFILE_MACROS if macros.get(macro) != "1")
            invalid += sorted(FORBIDDEN & macros.keys())
            if state.returncode or invalid:
                row["blocker"] = "PREINCLUDE_PROFILE_REJECTED"
                row["invalid_macros"] = invalid
                row["returncode"] = None
                log = state.stdout + state.stderr
                (output / row["log"]).write_text(log)
                row["log_sha256"] = digest(output / row["log"])
                report["units"].append(row)
                continue
            run = subprocess.run(argv, cwd=root, env=env, text=True, capture_output=True, timeout=90)
            row["returncode"] = run.returncode
            log = run.stdout + run.stderr
            if run.returncode == 0 and name in GUARDS:
                row["blocker"] = "B42_GUARD_COMPILED_UNEXPECTEDLY"
            elif run.returncode == 0:
                try:
                    row["object"] = inspect_object(obj, nm, env)
                    row["status"] = "COMPILED_OBJECT_ONLY"
                except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
                    row["blocker"] = str(exc)
            else:
                row["blocker"] = ("B42_COMPILE_REFUSAL" if "B4.2 physical PMAP/Stage-2/SMMU lockdown backend is BLOCKED" in log
                                  else "COMPILER_FAILURE")
        except (OSError, ValueError, subprocess.TimeoutExpired) as exc:
            row["blocker"] = str(exc) if isinstance(exc, ValueError) else type(exc).__name__
            log = str(exc)
        (output / row["log"]).write_text(log)
        row["log_sha256"] = digest(output / row["log"])
        if digest(path) != row["source_sha256"]:
            row["status"], row["blocker"] = "BLOCKED", "SOURCE_CHANGED_DURING_PROBE"
        if (digest(board_header) != board_info["sha256"] or digest(board_snapshot) != board_info["sha256"]):
            row["status"], row["blocker"] = "BLOCKED", "BOARD_HEADER_CHANGED_DURING_PROBE"
        if feature and digest(feature) != row["feature_header"]["sha256"]:
            row["status"], row["blocker"] = "BLOCKED", "FEATURE_HEADER_CHANGED_DURING_PROBE"
        report["units"].append(row)
    report["blocked_units"] = sum(r["status"] == "BLOCKED" for r in report["units"])
    report["compiled_objects"] = sum(r["status"] == "COMPILED_OBJECT_ONLY" for r in report["units"])
    report["status"] = "SELECTED_OBJECTS_ONLY_B42_COMPILE_HOLD" if not report["blocked_units"] else "NATIVE_BUILD_BLOCKED"
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("source-root", "osfmk-make", "iokit-make", "board-header", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--source", action="append", required=True)
    p.add_argument("--generated-include", type=Path, action="append", default=[])
    p.add_argument("--clang", default="clang")
    p.add_argument("--clangxx", default="clang++")
    p.add_argument("--llvm-nm", default="llvm-nm")
    a = p.parse_args()
    try:
        report = probe(a.source_root, a.osfmk_make, a.iokit_make, a.board_header,
                       a.source, a.out, a.generated_include, a.clang, a.clangxx, a.llvm_nm)
    except (OSError, ValueError) as exc:
        p.exit(1, f"compile probe refused: {exc}\n")
    print(json.dumps({k: report[k] for k in ("status", "scope", "compiled_objects", "blocked_units",
                                           "native_xnu_full_link", "xnu_device_entry")}))
    return 2 if report["blocked_units"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
