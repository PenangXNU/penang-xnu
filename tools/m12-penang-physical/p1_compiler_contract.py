#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check bounded ARM64 XNU type-trait semantics in real C/C++ compiler IR.

The simple signatures/summaries follow XNU 11417 kalloc.h's documented
8-byte granules. This does not qualify a compiler as Apple-equivalent, or
cover PAC, arm64e, unions, bitfields, inheritance or annotated arrays.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

from p1_generated_headers import REPO, require, sha
from p1_mig_headers import environment, NATIVE_DEPLOYMENT_TARGET

SOURCE = r'''
#if !__has_builtin(__builtin_xnu_type_signature) || !__has_builtin(__builtin_xnu_type_summary) || !__has_builtin(__builtin_xnu_types_compatible)
#error actual XNU type traits required
#endif
#if !__has_attribute(xnu_usage_semantics)
#error XNU semantic annotation required
#endif
struct mixed { void *ptr; unsigned long data; };
struct data { unsigned long a, b; };
struct ptrs { void *a, *b; };
#ifdef __cplusplus
#define CHECK(E) static_assert(E, "XNU type contract")
template<class T> struct deferred {
  static constexpr const char *signature = __builtin_xnu_type_signature(T);
  static constexpr unsigned long summary = __builtin_xnu_type_summary(T);
  static constexpr bool compatible = __builtin_xnu_types_compatible(T, T);
};
CHECK(deferred<mixed>::signature[0] == '1');
CHECK(deferred<mixed>::signature[1] == '2');
CHECK(deferred<mixed>::summary == 6 && deferred<mixed>::compatible);
extern "C" {
#else
#define CHECK(E) _Static_assert(E, "XNU type contract")
#endif
CHECK(sizeof(void *) == 8 && sizeof(unsigned long) == 8);
CHECK(__builtin_xnu_types_compatible(struct mixed, struct mixed));
CHECK(!__builtin_xnu_types_compatible(struct mixed, struct data));
CHECK(!__builtin_xnu_types_compatible(struct data, struct ptrs));
CHECK(__builtin_xnu_types_compatible(void, void));
CHECK(!__builtin_xnu_types_compatible(void, struct data));
const char *p1_signature_mixed = __builtin_xnu_type_signature(struct mixed);
const char *p1_signature_data = __builtin_xnu_type_signature(struct data);
const char *p1_signature_ptrs = __builtin_xnu_type_signature(struct ptrs);
unsigned long p1_summary_mixed = __builtin_xnu_type_summary(struct mixed);
unsigned long p1_summary_data = __builtin_xnu_type_summary(struct data);
unsigned long p1_summary_ptrs = __builtin_xnu_type_summary(struct ptrs);
CHECK(__builtin_xnu_type_summary(struct mixed) == 6);
CHECK(__builtin_xnu_type_summary(struct data) == 4);
CHECK(__builtin_xnu_type_summary(struct ptrs) == 2);
#ifdef __cplusplus
}
#endif
'''
NEGATIVES = {
    "incomplete": 'struct missing; const char *p = __builtin_xnu_type_signature(struct missing);\n',
    "vla": 'void f(int n) { typedef int V[n]; (void)__builtin_xnu_type_summary(V); }\n',
    "annotation_kind": 'typedef unsigned long T __attribute__((xnu_usage_semantics("success")));\n',
    "annotation_duplicate": 'typedef unsigned long T __attribute__((xnu_usage_semantics("data", "data")));\n',
    "annotation_record": 'struct __attribute__((xnu_usage_semantics("data"))) T { int x; };\n',
}


def audit_ir(ir):
    for name, signature, summary in (("mixed", "12", 6), ("data", "22", 4), ("ptrs", "11", 2)):
        pointer = re.search(r"(?m)^@p1_signature_" + name + r" = .*\bptr (@[\w.]+)[,\s]", ir)
        require(pointer is not None, "signature IR global missing")
        literal = re.search(r"(?m)^" + re.escape(pointer[1]) + r' = .*\[3 x i8\] c"([^"\n]*)"', ir)
        require(literal is not None and literal[1] == signature + r"\00", "wrong XNU granule signature")
        require(re.search(r"(?m)^@p1_summary_" + name + r" = .*\bi64 " + str(summary) + r"[,\s]", ir),
                "wrong XNU granule summary")


def check(compiler, admitted_sha, out):
    compiler = compiler.resolve(strict=True)
    target = out.resolve()
    require(re.fullmatch(r"[0-9a-f]{64}", admitted_sha) is not None and sha(compiler) == admitted_sha,
            "compiler binary must be explicitly hash-admitted")
    require(not target.exists() and not target.is_relative_to(REPO), "fresh private output required")
    target.mkdir(parents=True)
    env = environment()
    commands = []
    for language in ("c", "c++"):
        flags = [str(compiler), "--target=arm64-apple-darwin", "-mcpu=cortex-a76", "-ffreestanding",
                 "-mmacosx-version-min=" + NATIVE_DEPLOYMENT_TARGET,
                 "-nostdinc", "-x", language, "-std=" + ("gnu11" if language == "c" else "gnu++2b")]
        for name, source in (("contract", SOURCE), *NEGATIVES.items()):
            file = target / (language + "-" + name + ".source")
            file.write_text(source)
            ir = target / (language + "-" + name + ".ll")
            command = flags + ["-S", "-emit-llvm", str(file), "-o", str(ir)]
            result = subprocess.run(command, env=env, capture_output=True, timeout=60)
            log = target / (language + "-" + name + ".log")
            log.write_bytes(result.stdout + result.stderr)
            commands.append(dict(case=name, language=language, argv=command, returncode=result.returncode,
                                 source_sha256=sha(file), log_sha256=sha(log)))
            require((result.returncode == 0) == (name == "contract"), "compiler contract or negative case failed")
            if name == "contract":
                audit_ir(ir.read_text())
    require(sha(compiler) == admitted_sha, "compiler changed during contract check")
    report = dict(status="PASS_BOUNDED_TYPE_TRAITS_ONLY", scope="HOST_COMPILER_IR_ARM64_NO_EXECUTION",
                  compiler_sha256=admitted_sha, commands=commands,
                  oracle="XNU_11417_KALLOC_DOCUMENTED_GRANULES_AND_OWN_C_CPP_FIXTURES",
                  apple_compiler_equivalence="NOT_CLAIMED", native_xnu_full_link="NOT_RUN", device_entry="NOT_RUN")
    (target / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--compiler-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = check(args.compiler, args.compiler_sha256, args.out)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, type(error).__name__ + ": XNU compiler contract failed; inspect private diagnostics\n")
    print(result["status"])


if __name__ == "__main__":
    main()
