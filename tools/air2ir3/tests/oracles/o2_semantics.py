#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""O2 semantic oracle: LLVM execution vs NIR evaluation of the same AIR entry.

  o2_semantics.py --nireval A2I-O2-NIREVAL --out DIR [--runs N]

For each MIT synthetic fixture and N deterministic input sets drawn from
IEEE-754 edge values (+-0, +-inf, NaN, denormal, max, 1+ulp, ...):
  LLVM  the fixture function is executed by the LLVM 18 interpreter
        (lli-18 -force-interpreter, module DataLayout kept) through a
        generated harness that prints every output component's bits
  NIR   a2i-o2-nireval evaluates the NIR emitted by air_to_nir on the same
        inputs with Mesa's NIR constant evaluator
Outputs must be bit-identical; two NaNs compare equal (LLVM leaves the NaN
payload unspecified). The fixtures carry no fast-math flags, so signed zeros
and infinities are compared exactly. This is host evaluation only: it says
nothing about ir3 or the GPU. Exit 0 = all equal, 1 = mismatch, 2 = setup.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
FIX = os.path.join(TESTS, "fixtures", "synthetic")
FIXTURES = [("vs_scale", 0), ("fs_tint", 1), ("o2_ops", 1)]
EDGE = [0x00000000, 0x80000000, 0x3FC00000, 0xC0100000, 0x7F800000, 0xFF800000, 0x7FC00000,
        0x00000001, 0x7F7FFFFF, 0x3F800001, 0x40490FDB, 0xBF800000, 0x00800000, 0x807FFFFF]
BUF_DWORDS = 16


def is_nan(w):
    return (w & 0x7F800000) == 0x7F800000 and (w & 0x007FFFFF) != 0


def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<{(":
            depth += 1
        elif ch in ">})":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def ncomp(ty):
    m = re.fullmatch(r"<(\d+) x float>", ty)
    if m:
        return int(m.group(1))
    if ty == "float":
        return 1
    raise ValueError("unsupported O2 type %r" % ty)


def parse_fixture(text):
    m = re.search(r"^define (.+?) @(\w+)\((.*)\) \{$", text, re.M)
    ret, name, params = m.group(1), m.group(2), split_top(m.group(3))
    args = [p.rsplit(" ", 1)[0] for p in params]
    members = split_top(ret[2:-2]) if ret.startswith("<{") else [ret]
    locs = {int(a): int(l) for a, l in re.findall(
        r'!\{i32 (\d+), !"air\.buffer", !"air\.location_index", i32 (\d+)', text)}
    return name, ret, args, members, locs


def words_for(run, argno, n):
    return [EDGE[(run * 7 + argno * 3 + c * 5) % len(EDGE)] for c in range(n)]


def buf_for(run, loc):
    return [EDGE[(run * 5 + loc + i * 3) % len(EDGE)] for i in range(BUF_DWORDS)]


def harness(text, name, ret, args, members, locs, run):
    body = re.sub(r'^target triple = ".*"$', 'target triple = "x86_64-unknown-linux-gnu"', text,
                  flags=re.M)
    g, call_args, prints = [], [], []
    for i, ty in enumerate(args):
        if ty.startswith("ptr addrspace(2)"):
            w = buf_for(run, locs[i])
            g.append("@a2i.buf%d = addrspace(2) global [%d x i32] [%s], align 16" %
                     (i, BUF_DWORDS, ", ".join("i32 %d" % x for x in w)))
            call_args.append("ptr addrspace(2) @a2i.buf%d" % i)
        else:
            n = ncomp(ty)
            w = words_for(run, i, n)
            if n == 1:
                call_args.append("float bitcast (i32 %d to float)" % w[0])
            else:
                call_args.append("%s bitcast (<%d x i32> <%s> to %s)" %
                                 (ty, n, ", ".join("i32 %d" % x for x in w), ty))
    prints.append("  %%r = call %s @%s(%s)" % (ret, name, ", ".join(call_args)))
    for k, mty in enumerate(members):
        src = "%r"
        if ret.startswith("<{"):
            prints.append("  %%m%d = extractvalue %s %%r, %d" % (k, ret, k))
            src = "%%m%d" % k
        for j in range(ncomp(mty)):
            if mty == "float":
                prints.append("  %%b%d_%d = bitcast float %s to i32" % (k, j, src))
            else:
                prints.append("  %%e%d_%d = extractelement %s %s, i32 %d" % (k, j, mty, src, j))
                prints.append("  %%b%d_%d = bitcast float %%e%d_%d to i32" % (k, j, k, j))
            prints.append("  call i32 (ptr, ...) @printf(ptr @a2i.fmt, i32 %d, i32 %d, i32 %%b%d_%d)"
                          % (k, j, k, j))
    return "\n".join([
        body, "\n".join(g),
        '@a2i.fmt = private unnamed_addr constant [12 x i8] c"%u %u %08x\\0A\\00"',
        "declare i32 @printf(ptr, ...)",
        "define i32 @main() {", "\n".join(prints), "  ret i32 0", "}", ""])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--nireval", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--runs", type=int, default=14)
    ap.add_argument("--llvm-suffix", default="-18")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    las, lli = "llvm-as" + a.llvm_suffix, "lli" + a.llvm_suffix
    total = exact = nan_eq = 0
    mism, report = [], {}
    for fx, ftype in FIXTURES:
        text = open(os.path.join(FIX, fx + ".ll")).read()
        name, ret, args, members, locs = parse_fixture(text)
        bc = os.path.join(a.out, fx + ".bc")
        lib = os.path.join(a.out, fx + ".metallib")
        subprocess.run([las, os.path.join(FIX, fx + ".ll"), "-o", bc], check=True)
        subprocess.run([sys.executable, os.path.join(TESTS, "gen_metallib.py"), lib, "--bc", bc,
                        "--name", name, "--type", str(ftype)], check=True)
        report[fx] = {"runs": a.runs, "outputs": len(members)}
        for run in range(a.runs):
            spec = os.path.join(a.out, "%s.%02d.spec" % (fx, run))
            with open(spec, "w") as f:
                for i, ty in enumerate(args):
                    if ty.startswith("ptr addrspace(2)"):
                        f.write("buf %d %s\n" % (locs[i], " ".join("%08x" % w for w in buf_for(run, locs[i]))))
                    else:
                        f.write("in %d %s\n" % (i, " ".join("%08x" % w for w in words_for(run, i, ncomp(ty)))))
            p = subprocess.run([a.nireval, lib, spec], capture_output=True, text=True)
            if p.returncode or "Sanitizer" in p.stderr:
                print("o2: NIR evaluation failed for %s run %d: %s" % (fx, run, p.stderr[:300]))
                return 1
            nir = {}
            for line in p.stdout.split("\n"):
                t = line.split()
                if t and t[0] == "out":
                    nir[int(t[1])] = [int(x, 16) for x in t[2:]]
            hll = os.path.join(a.out, "%s.%02d.harness.ll" % (fx, run))
            with open(hll, "w") as f:
                f.write(harness(text, name, ret, args, members, locs, run))
            q = subprocess.run([lli, "-force-interpreter", hll], capture_output=True, text=True)
            if q.returncode:
                print("o2: LLVM interpreter failed for %s run %d: %s" % (fx, run, q.stderr[:300]))
                return 1
            llvm = {}
            for line in q.stdout.split("\n"):
                t = line.split()
                if len(t) == 3:
                    llvm.setdefault(int(t[0]), []).append(int(t[2], 16))
            for k, mty in enumerate(members):
                lv, nv = llvm.get(k, []), nir.get(k, [])
                if len(lv) != ncomp(mty) or len(nv) != ncomp(mty):
                    mism.append({"fixture": fx, "run": run, "out": k, "llvm": lv, "nir": nv})
                    continue
                for j, (x, y) in enumerate(zip(lv, nv)):
                    total += 1
                    if x == y:
                        exact += 1
                    elif is_nan(x) and is_nan(y):
                        nan_eq += 1
                    else:
                        mism.append({"fixture": fx, "run": run, "out": k, "comp": j,
                                     "llvm": "%08x" % x, "nir": "%08x" % y})
    # NIR-only expectation where the LLVM interpreter cannot run the fixture:
    # an unwritten member of a zeroinitializer struct reaches its output as +0
    zbc, zlib = os.path.join(a.out, "o2_zero_member.bc"), os.path.join(a.out, "o2_zero_member.metallib")
    subprocess.run([las, os.path.join(FIX, "o2_zero_member.ll"), "-o", zbc], check=True)
    subprocess.run([sys.executable, os.path.join(TESTS, "gen_metallib.py"), zlib, "--bc", zbc,
                    "--name", "o2_zero_member", "--type", "1"], check=True)
    zspec = os.path.join(a.out, "o2_zero_member.spec")
    with open(zspec, "w") as f:
        f.write("in 0 80000000 3fc00000 7fc00000 ff800000\n")
    p = subprocess.run([a.nireval, zlib, zspec], capture_output=True, text=True)
    zero_ok = p.returncode == 0 and p.stdout.split("\n")[:2] == [
        "out 0 80000000 3fc00000 7fc00000 ff800000", "out 1 00000000 00000000"]
    if not zero_ok:
        mism.append({"fixture": "o2_zero_member", "nir": p.stdout.strip(), "stderr": p.stderr[:200]})
    # negative control: NIR evaluated on run 1's inputs must disagree with the
    # LLVM result of run 0 for the first fixture (the comparison is not vacuous)
    fx0 = FIXTURES[0][0]
    lib0 = os.path.join(a.out, fx0 + ".metallib")
    p = subprocess.run([a.nireval, lib0, os.path.join(a.out, "%s.01.spec" % fx0)],
                       capture_output=True, text=True)
    q = subprocess.run([lli, "-force-interpreter", os.path.join(a.out, "%s.00.harness.ll" % fx0)],
                       capture_output=True, text=True)
    nir1 = [int(x, 16) for line in p.stdout.split("\n") if line.startswith("out ")
            for x in line.split()[2:]]
    llvm0 = [int(line.split()[2], 16) for line in q.stdout.split("\n") if len(line.split()) == 3]
    control_ok = p.returncode == 0 and q.returncode == 0 and len(nir1) == len(llvm0) and \
        any(x != y and not (is_nan(x) and is_nan(y)) for x, y in zip(nir1, llvm0))
    nan_seen = sum(1 for f in os.listdir(a.out) if f.endswith(".spec"))
    summary = {"tools": {"llvm": lli + " -force-interpreter", "nir": "Mesa nir_opt_constant_folding"},
               "fixtures": report, "components_compared": total, "bit_exact": exact,
               "nan_vs_nan": nan_eq, "mismatches": mism[:20], "mismatch_count": len(mism),
               "input_sets": nan_seen, "negative_control_detected": control_ok,
               "nir_only_zero_member": zero_ok}
    with open(os.path.join(a.out, "o2-report.json"), "w") as f:
        json.dump(summary, f, indent=1)
    print("o2: %d components, %d bit-exact, %d NaN=NaN, %d mismatches, negative control %s (%s)" %
          (total, exact, nan_eq, len(mism), "detected" if control_ok else "NOT detected", a.out))
    for m in mism[:5]:
        print("  mismatch", m)
    return 0 if not mism and total and control_ok else 1


if __name__ == "__main__":
    sys.exit(main())
