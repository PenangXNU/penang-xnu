#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate real XNU ARM64 MIG headers with an explicitly admitted generator.

No placeholder declarations, Make execution, Mach RPC or kernel/device run.
The caller admits a host-built migcom digest; its version string alone is not
source provenance. Keep generator build/host wire-layout evidence separately.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

from p1_compile_probe import config_flags

XNU_COMMIT = "43a90889846e00bfb5cf1d255cdc0a701a1e05a4"
MIG_COMMIT = "619db33806b11b58fa14078bc7a811958312e52d"
MIG_VERSION = "mig-133.140.1 (" + MIG_COMMIT + ")"
DEF_PINS = {"clock": "7c97eebd4acbf12ad982b60a0f9b383feddf917f305c765f5d609a74a0dcf22e",
            "vm_map": "26c00af574aa05dfa5d1b42332305a260f8e3b4e8d255646b0e6edecfd610d39"}
REPO = Path(__file__).resolve().parents[2]
CONSUMER = r'''#include <stddef.h>
#include <mach/clock.h>
#include <mach/vm_map.h>
#ifdef __cplusplus
#define P1_TYPE_SAME(A, B) static_assert(__is_same(A, B), "MIG ABI prototype mismatch")
#define P1_ASSERT(E) static_assert(E, "MIG ABI layout mismatch")
#else
#define P1_TYPE_SAME(A, B) _Static_assert(__builtin_types_compatible_p(A, B), "MIG ABI prototype mismatch")
#define P1_ASSERT(E) _Static_assert(E, "MIG ABI layout mismatch")
#endif
typedef kern_return_t (*p1_clock_signature)(clock_serv_t, mach_timespec_t *);
typedef kern_return_t (*p1_allocate_signature)(vm_map_t, vm_address_t *, vm_size_t, int);
P1_TYPE_SAME(__typeof__(&clock_get_time), p1_clock_signature);
P1_TYPE_SAME(__typeof__(&vm_allocate), p1_allocate_signature);
P1_ASSERT(sizeof(vm_address_t) == 8);
P1_ASSERT(sizeof(vm_size_t) == 8);
P1_ASSERT(sizeof(NDR_record_t) == 8);
P1_ASSERT(sizeof(mach_msg_header_t) == 32);
P1_ASSERT(sizeof(mach_timespec_t) == 8);
P1_ASSERT(sizeof(__Request__clock_get_time_t) == sizeof(mach_msg_header_t));
P1_ASSERT(offsetof(__Reply__clock_get_time_t, cur_time) == sizeof(mach_msg_header_t) + sizeof(NDR_record_t) + sizeof(kern_return_t));
int p1_mig_header_consumer(void) { return clock_MSG_COUNT + vm_map_MSG_COUNT; }
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def read(path):
    require(path.is_file() and path.stat().st_size <= 16_000_000, "bounded regular input required")
    return path.read_bytes()


def file_sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', " ", text, flags=re.S)


def protocol(text):
    text = clean(text)
    subs = re.findall(r"\bsubsystem\s+(?:(?:KernelServer|KernelUser)\s+)?(\w+)\s+(\d+)\s*;", text, flags=re.I)
    require(len(subs) == 1, "exactly one MIG subsystem required")
    name, base = subs[0]
    cursor = int(base)
    mapping = {}
    for item in re.finditer(r"\b(?:routine|simpleroutine)\s+(\w+)\s*\(|\bskip\s*;", text, flags=re.I):
        if item[1]:
            require(item[1] not in mapping, "duplicate MIG routine")
            mapping[item[1]] = cursor
        cursor += 1
    require(mapping and cursor - int(base) <= 4096, "empty or oversized MIG interface")
    return {"name": name, "base": int(base), "count": cursor - int(base), "routines": mapping}


def audit_header(preprocessed, header):
    expected = protocol(preprocessed)
    header = re.sub(r'("(?:\\.|[^"\\])*")|/\*.*?\*/|//[^\n]*',
                    lambda match: match[1] if match[1] is not None else " ", header, flags=re.S)
    mapping = re.findall(r'\{\s*"(\w+)"\s*,\s*(\d+)\s*\}', header)
    counts = re.findall(r"(?m)^#define\s+" + re.escape(expected["name"]) + r"_MSG_COUNT\s+(\d+)\s*$", header)
    require(len(counts) == 1 and int(counts[0]) == expected["count"], "MIG message count mismatch")
    require(len(mapping) == len(expected["routines"])
            and {n: int(v) for n, v in mapping} == expected["routines"], "MIG routine/ID map mismatch")
    require("#include <mach/message.h>" in header and "__Request__" in header,
            "generated request ABI missing")
    return expected


def header_names(makefile, variable, suffix=".h"):
    lines = makefile.splitlines()
    start = next(i for i, line in enumerate(lines) if line.startswith(variable + " ="))
    names = []
    for line in lines[start:]:
        value = line.split("=", 1)[1] if not names and "=" in line else line
        names.extend(value.replace("\\", " ").split())
        if not line.endswith("\\"):
            break
    require(names and all(re.fullmatch(r"\w+" + re.escape(suffix), n) for n in names), "unsupported MIG header selection")
    return {n[:-len(suffix)] for n in names}


def kernel_user_names(makefile):
    return (header_names(makefile, "MIG_KUHDRS")
            | header_names(makefile, "MIG_KUSRC", "_user.c"))


def selection(makefile, name, kernel_server=False, subsystem="mach"):
    """Follow pinned exported-server or build kernel-user Makefile rules.

    Kernel-server interfaces have different CPP/ABI flags and require explicit
    selection, never inference from an overlapping exported server basename.
    """
    extra = {
        "device": {"device_server": ("device", "kernel-server", False)},
        "UserNotification": {"UNDRequest": ("UNDRequest", "kernel-user", True),
                             "UNDReplyServer": ("UNDReply", "kernel-server", False)},
    }
    if subsystem != "mach":
        selected = extra.get(subsystem, {}).get(name)
        require(selected is not None and kernel_server == (selected[1] == "kernel-server"),
                "unadmitted subsystem interface or mode")
        return selected
    if kernel_server:
        require(name in header_names(makefile, "MIG_KSHDRS"), "unadmitted kernel server header")
        return name[:-7], "kernel-server", False
    if name.endswith("_server"):
        require(name in header_names(makefile, "MIG_USHDRS"), "unadmitted exported server header")
        return name[:-7], "exported-server", False
    ku = name in kernel_user_names(makefile)
    require(ku or name in header_names(makefile, "MIG_UUHDRS"), "unadmitted user header")
    return name, "kernel-user" if ku else "exported-user", ku


def environment():
    result = os.environ.copy()
    for key in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH",
                "SDKROOT", "MACOSX_DEPLOYMENT_TARGET", "CCC_OVERRIDE_OPTIONS", "CL", "_CL_"):
        result.pop(key, None)
    return result


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)


def dependencies(root, preprocessed):
    result = {}
    for name in re.findall(r'(?m)^#\s+\d+\s+"([^"\n]+)"', preprocessed):
        if name.startswith("<"):
            continue
        path = Path(name).resolve(strict=True)
        require(path.is_relative_to(root), "preprocessor input escapes pinned XNU root")
        rel = path.relative_to(root).as_posix()
        raw = read(path)
        require(raw == git(root, "show", "HEAD:" + rel), "XNU dependency differs from pinned commit")
        result[rel] = sha(raw)
    require(result, "preprocessor supplied no source provenance")
    return result


def compile_consumers(root, headers, output, compiler):
    """Compile header-only kernel ABI consumers; no IPC call is executed."""
    source = output / "abi-consumer.c"
    source.write_text(CONSUMER)
    env = environment()
    results = []
    for language in ("c", "c++"):
        obj = output / ("abi-consumer-" + language + ".o")
        command = [str(compiler), "--target=arm64-apple-darwin", "-ffreestanding",
                   "-nostdlibinc", "-fblocks", "-DKERNEL", "-DKERNEL_PRIVATE", "-DPRIVATE",
                   "-I", str(headers)]
        for name in ("osfmk", "bsd", "libkern", "osfmk/libsa", "EXTERNAL_HEADERS"):
            command += ["-I", str(root / name)]
        command += ["-x", language, "-c", str(source), "-o", str(obj)]
        run = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
        log = output / ("abi-consumer-" + language + ".log")
        log.write_text(run.stdout + run.stderr)
        require(run.returncode == 0, "target " + language + " MIG ABI consumer failed")
        data = read(obj)
        require(len(data) >= 32 and struct.unpack_from("<4I", data) == (0xFEEDFACF, 0x0100000C, 0, 1),
                "MIG ABI consumer is not ARM64 MH_OBJECT")
        results.append({"language": language, "command": command, "object_sha256": sha(data),
                        "log_sha256": sha(read(log)), "returncode": run.returncode})
    return results


def preprocessor_command(compiler, root, defines, source, mode):
    cpp = [str(compiler), "--target=arm64-apple-darwin", "-E", "-x", "c", "-nostdinc",
           "-D__MACH30__", "-DAPPLE", "-DKERNEL", "-DKERNEL_PRIVATE", "-DXNU_KERNEL_PRIVATE",
           "-DPRIVATE", "-D__MACHO__=1", "-Dvolatile=__volatile", "-DXNU_KERN_EVENT_DATA_IS_VLA",
           "-DMACH_KERNEL_PRIVATE", "-DXNU_TARGET_OS_OSX", "-DXNU_PLATFORM_MacOSX", *defines]
    if mode == "kernel-user":
        cpp += ["-DKERNEL_USER=1"]
    if mode == "kernel-server":
        cpp += ["-DKERNEL_SERVER=1"]
    for directory in ("osfmk", "bsd", "EXTERNAL_HEADERS"):
        cpp += ["-I", str(root / directory)]
    return cpp + [str(source)]


def generate(root, migcom, admitted_sha, interfaces, out, clang="clang", osfmk_make=None, kernel_servers=(), subsystem="mach"):
    root, generator = root.resolve(strict=True), migcom.resolve(strict=True)
    require(re.fullmatch(r"[0-9a-f]{64}", admitted_sha) is not None, "explicit generator SHA-256 required")
    require(generator.is_file() and file_sha(generator) == admitted_sha, "generator SHA mismatch")
    require(git(root, "rev-parse", "HEAD").decode().strip() == XNU_COMMIT, "wrong XNU revision")
    require(interfaces and len(set(interfaces)) == len(interfaces)
            and all(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", n) for n in interfaces), "invalid interface selection")
    output = out.resolve()
    require(not output.exists() and not output.is_relative_to(root)
            and not output.is_relative_to(REPO), "fresh private output outside source/repository required")
    env = environment()
    version = subprocess.run([str(generator), "-version"], capture_output=True, text=True, env=env, timeout=30)
    require(version.returncode == 0 and version.stdout.strip() == MIG_VERSION, "unreviewed MIG version")
    compiler = Path(shutil.which(str(clang)) or "").resolve(strict=True)
    require(compiler.is_file(), "Clang unavailable")
    require(subsystem in ("mach", "device", "UserNotification"), "unadmitted MIG directory")
    makepath = "osfmk/" + subsystem + "/Makefile"
    makefile = read(root / makepath)
    require(makefile == git(root, "show", "HEAD:" + makepath), "MIG Makefile drift")
    require(len(set(kernel_servers)) == len(kernel_servers) and set(kernel_servers) <= set(interfaces),
            "invalid explicit kernel server selection")
    selections = {name: selection(makefile.decode(), name, name in kernel_servers, subsystem) for name in interfaces}
    require(osfmk_make is not None, "retained OSFMK configuration required")
    config = read(osfmk_make)
    defines = config_flags(config.decode())
    output.mkdir(parents=True, exist_ok=False)
    (output / subsystem).mkdir()
    report = {"scope": "HOST_ONLY_REAL_MIG_GENERATION", "status": "BLOCKED",
              "xnu_commit": XNU_COMMIT, "generator_source_commit": MIG_COMMIT,
              "generator_version": version.stdout.strip(), "generator_sha256": admitted_sha,
              "generator_provenance": "CALLER_ADMITTED_BINARY_WITH_SEPARATE_BUILD_RECEIPT",
              "osfmk_make_sha256": sha(config), "config_defines": defines,
              "clang_sha256": file_sha(compiler), "subsystem_directory": subsystem,
              "subsystem_makefile_sha256": sha(makefile), "interfaces": [],
              "native_xnu_full_link": "NOT_RUN", "xnu_device_entry": "NOT_RUN"}
    for name in interfaces:
        stem, mode, ku = selections[name]
        source = (root / "osfmk" / subsystem / (stem + ".defs")).resolve(strict=True)
        require(source.is_relative_to(root), "interface source escapes root")
        raw = read(source)
        require(raw == git(root, "show", "HEAD:osfmk/" + subsystem + "/" + stem + ".defs"), "interface source drift")
        if stem in DEF_PINS:
            require(sha(raw) == DEF_PINS[stem], "required interface pin mismatch")
        cpp = preprocessor_command(compiler, root, defines, source, mode)
        pre = subprocess.run(cpp, env=env, capture_output=True, text=True, timeout=60)
        (output / (name + ".cpp.log")).write_text(pre.stderr)
        require(pre.returncode == 0, "MIG target preprocessing failed")
        (output / (name + ".preprocessed.defs")).write_text(pre.stdout)
        deps = dependencies(root, pre.stdout)
        header = output / subsystem / (name + ".h")
        command = [str(generator), "-novouchers", "-header", str(header), "-user", "/dev/null",
                   "-server", "/dev/null", "-sheader", "/dev/null"]
        if mode in ("exported-server", "kernel-server"):
            command[3] = "/dev/null"
            command[-1] = str(header)
        if ku:
            command += ["-maxonstack", "1024"]
        if mode == "kernel-server":
            command += ["-mach_msg2"]
        run = subprocess.run(command, input=pre.stdout, env=env, cwd=output,
                             capture_output=True, text=True, timeout=60)
        (output / (name + ".mig.log")).write_text(run.stdout + run.stderr)
        require(run.returncode == 0, "MIG generation failed")
        data = read(header)
        wire = audit_header(pre.stdout, data.decode())
        require(file_sha(generator) == admitted_sha, "generator changed during execution")
        report["interfaces"].append({"interface": name, "mode": mode, "kernel_user": ku,
            "source_sha256": sha(raw), "preprocessed_sha256": sha(pre.stdout.encode()),
            "header_sha256": sha(data), "dependencies": deps, "protocol": wire,
            "preprocessor_command": cpp, "generator_command": command})
    if subsystem == "mach" and {"clock", "vm_map"} <= set(interfaces):
        report["abi_consumers"] = compile_consumers(root, output, output, compiler)
    report["status"] = "GENERATED_HEADERS_ONLY_NOT_IPC_RUNTIME"
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("xnu-root", "migcom", "osfmk-make", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--migcom-sha256", required=True)
    p.add_argument("--interface", action="append")
    p.add_argument("--subsystem", choices=("mach", "device", "UserNotification"), default="mach")
    p.add_argument("--clang", default="clang")
    p.add_argument("--kernel-server", action="append", default=[],
                   help="explicit KSHDRS output basename including _server; use a separate output root")
    a = p.parse_args()
    try:
        names = (a.interface or ([] if a.kernel_server else ["clock", "vm_map"])) + a.kernel_server
        r = generate(a.xnu_root, a.migcom, a.migcom_sha256, names, a.out, a.clang, a.osfmk_make, a.kernel_server, a.subsystem)
    except (OSError, ValueError, StopIteration, subprocess.SubprocessError) as error:
        p.exit(1, type(error).__name__ + ": MIG generation refused; retain private diagnostics\n")
    print(r["status"], "interfaces=" + str(len(r["interfaces"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
