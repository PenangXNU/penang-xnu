#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""O1 structural oracle: air2ir3 frontend vs LLVM 18 over a local AIR corpus.

  o1_structural.py --cli AIR2IR3 --corpus DIR --bcdir DIR --out DIR

For every MTLB function entry of every DIR/*.metallib:
  container  HASH tag == SHA-256 of the bytes at bitcode_file_offset/mdsz read
             here independently, and == SHA-256 of the previously extracted
             module in --bcdir (located by hash, never copied)
  records    `air2ir3 dump-records` == `llvm-bcanalyzer-18 -dump` (block tree,
             record codes, operand counts, first 24 operands, blob presence)
  ir         `air2ir3 dump-ir` == `llvm-dis-18` text: defined functions, basic
             block counts, opcode sequence, direct callees, named metadata,
             type-table size
  entry kind MTLB TYPE vs the !air.<kind> named metadata listing the entry
  subset     `air2ir3 check` status (first MVP-AIR-0 violation) plus the full
             set of non-MVP features seen in the LLVM text

An LLVM tool failure is recorded as oracle_unavailable, never as a frontend
defect. The corpus is read-only; the report (which names corpus functions)
is written only under --out. Exit: 0 = no structural mismatch, 1 = mismatch,
2 = setup error.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import subprocess
import sys

BLOCK_IDS = {
    "BLOCKINFO_BLOCK": 0, "MODULE_BLOCK": 8, "PARAMATTR_BLOCK": 9, "PARAMATTR_GROUP_BLOCK_ID": 10,
    "CONSTANTS_BLOCK": 11, "FUNCTION_BLOCK": 12, "IDENTIFICATION_BLOCK_ID": 13,
    "VALUE_SYMTAB": 14, "METADATA_BLOCK": 15, "METADATA_ATTACHMENT": 16, "TYPE_BLOCK_ID": 17,
    "USELIST_BLOCK": 18, "MODULE_STRTAB_BLOCK": 19, "GLOBALVAL_SUMMARY_BLOCK": 20,
    "OPERAND_BUNDLE_TAGS_BLOCK": 21, "METADATA_KIND_BLOCK": 22, "STRTAB_BLOCK": 23,
    "FULL_LTO_GLOBALVAL_SUMMARY_BLOCK": 24, "SYMTAB_BLOCK": 25, "SYNC_SCOPE_NAMES_BLOCK": 26,
}
# Records whose name carries no codeid in llvm-bcanalyzer output.
NAMELESS_CODES = {"BLOB": 1}
MVP_OPS = {"ret", "fadd", "fsub", "fmul", "fneg", "shufflevector", "extractelement",
           "insertelement", "insertvalue", "extractvalue", "load", "getelementptr"}
ENTRY_MD = re.compile(r"^air\.(vertex|fragment|kernel|visible|extern|intersection|mesh|object"
                      r"|stitchable|tile)$")
OPLIM = 24


def run(cmd, timeout=300):
    p = subprocess.run(cmd, capture_output=True, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


# ---------------------------------------------------------------- records
def bca_events(text):
    ev, depth = [], 0
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith("Summary of") or s.startswith("Block ID #"):
            break
        if not s.startswith("<") or s.startswith("<BITCODE_WRAPPER_HEADER"):
            continue
        if s.startswith("</"):
            depth -= 1
            continue
        m = re.match(r"<([A-Za-z_0-9]+)([^>]*?)(/?)>(.*)$", s)
        if not m:
            raise ValueError("unparsed bcanalyzer line: %r" % s[:80])
        name, attrs, selfclose, rest = m.groups()
        if "NumWords=" in attrs or "BlockID=" in attrs or name == "BLOCKINFO_BLOCK":
            bid = re.search(r"BlockID=(\d+)", attrs)
            if bid:
                bid = int(bid.group(1))
            elif name in BLOCK_IDS:
                bid = BLOCK_IDS[name]
            elif name.startswith("UnknownBlock"):
                bid = int(name[len("UnknownBlock"):])
            else:
                raise ValueError("unknown block name %s" % name)
            ev.append(("B", depth, bid))
            if not selfclose:
                depth += 1
            continue
        code = re.search(r"codeid=(\d+)", attrs)
        if code:
            code = int(code.group(1))
        elif name.startswith("UnknownCode"):
            code = int(name[len("UnknownCode"):])
        elif name in NAMELESS_CODES:
            code = NAMELESS_CODES[name]
        else:
            raise ValueError("record without code: %s" % name)
        ops = [int(v) for _, v in sorted(((int(k), v) for k, v in
                                           re.findall(r"\bop(\d+)=(-?\d+)", attrs)))]
        # METADATA_STRINGS prints its blob as "num-strings = N {...}" instead
        blob = "blob data" in rest or re.search(r"\bnum-strings = \d+", rest) is not None
        bl = re.search(r"unprintable, (\d+) bytes", rest)
        ev.append(("R", depth, code, len(ops), tuple(ops[:OPLIM]), blob,
                   int(bl.group(1)) if bl else None))
    return ev


def ours_events(text):
    ev = []
    for raw in text.splitlines():
        ind = (len(raw) - len(raw.lstrip(" "))) // 2
        s = raw.strip()
        if s.startswith("BLOCK "):
            ev.append(("B", ind, int(s.split()[1])))
            continue
        m = re.match(r"REC (\d+) n=(\d+)((?: \d+)*)( \.\.\.)?(?: blob=(\d+))?$", s)
        if not m:
            raise ValueError("unparsed dump-records line: %r" % s[:80])
        ops = tuple(int(x) for x in m.group(3).split())
        ev.append(("R", ind, int(m.group(1)), int(m.group(2)), ops, m.group(5) is not None,
                   int(m.group(5)) if m.group(5) else None))
    return ev


def compare_records(ours, ref):
    if len(ours) != len(ref):
        n = min(len(ours), len(ref))
    else:
        n = len(ours)
    for i in range(n):
        a, b = ours[i], ref[i]
        if a[0] != b[0] or a[:5] != b[:5] or (a[0] == "R" and a[5] != b[5]) or \
           (a[0] == "R" and b[6] is not None and a[6] != b[6]):
            return {"index": i, "ours": repr(a)[:200], "llvm": repr(b)[:200]}
    if len(ours) != len(ref):
        return {"index": n, "ours_len": len(ours), "llvm_len": len(ref)}
    return None


# --------------------------------------------------------------------- IR
INST = re.compile(r"^\s*(?:%\S+ = )?(?:tail |notail |musttail )?([a-z][a-z0-9]*)\b")
CALLEE = re.compile(r"\bcall [^@]*@(\"[^\"]+\"|[\w.$]+)\(")
LABEL = re.compile(r"^(?:\d+|[\w.$-]+|\"[^\"]+\"):(?:\s|$)")


def ll_ir(text):
    funcs, named, cur, in_switch = {}, [], None, False
    for line in text.splitlines():
        if cur is None:
            m = re.match(r"^define\b.*?@(\"[^\"]+\"|[\w.$]+)\(", line)
            if m:
                cur = {"name": m.group(1).strip('"'), "blocks": 0, "ops": [], "callees": [],
                       "first": True}
                continue
            m = re.match(r"^!([A-Za-z_.$-][A-Za-z0-9_.$-]*) = !\{(.*)\}\s*$", line)
            if m:
                inner = m.group(2).strip()
                named.append((m.group(1), 0 if not inner else len(inner.split(","))))
            continue
        s = line.strip()
        if s == "}":
            if cur["first"]:
                cur["blocks"] = 0
            del cur["first"]
            funcs[cur["name"]] = cur
            cur = None
            continue
        if in_switch:
            if s == "]":
                in_switch = False
            continue
        if not s or s.startswith(";"):
            continue
        if LABEL.match(s):
            cur["blocks"] += 1
            cur["first"] = False
            continue
        if cur["first"]:
            cur["blocks"] += 1      # unlabeled entry block
            cur["first"] = False
        m = INST.match(s)
        if not m:
            raise ValueError("unparsed instruction: %r" % s[:80])
        op = m.group(1)
        cur["ops"].append(op)
        if op == "call":
            c = CALLEE.search(s)
            cur["callees"].append(c.group(1).strip('"') if c else "indirect")
        if op == "switch" and s.endswith("["):
            in_switch = True
    return funcs, named


def ours_ir(text):
    funcs, named, types, cur = {}, [], None, None
    for line in text.splitlines():
        m = re.match(r"^define .* @(\S+?)\(.*\) blocks=(\d+)$", line)
        if m:
            cur = {"name": m.group(1), "blocks": int(m.group(2)), "ops": [], "callees": []}
            funcs[cur["name"]] = cur
            continue
        m = re.match(r"^  \[\d+\] bb\d+ (\S+)", line)
        if m:
            cur["ops"].append(m.group(1))
            if m.group(1) == "call":
                c = re.search(r" callee=(?:@(\S+)|indirect)$", line)
                cur["callees"].append(c.group(1) if c and c.group(1) else "indirect")
            continue
        m = re.match(r"^named !(\S+) ops=(\d+)$", line)
        if m:
            named.append((m.group(1), int(m.group(2))))
            continue
        m = re.match(r"^types (\d+)$", line)
        if m:
            types = int(m.group(1))
    return funcs, named, types


TYPED_PTR = re.compile(r"(?<=\.)p(\d+)(?:i\d+|f\d+|v\d+[if]\d+|s_[A-Za-z0-9_]+)(?=\.|$)")


def upgrade_intrinsic(name):
    """LLVM >= 17 reads typed-pointer bitcode and renames overloaded intrinsics
    to opaque-pointer mangling (llvm.memcpy.p0i8.p2i8.i64 -> llvm.memcpy.p0.p2.i64).
    The bitcode STRTAB keeps the original name, which is what air2ir3 reports."""
    return TYPED_PTR.sub(r"p\1", name) if name.startswith("llvm.") else name


def compare_ir(of, lf, upgrades):
    for fn in of.values():
        new = [upgrade_intrinsic(c) for c in fn["callees"]]
        upgrades.update(o for o, n in zip(fn["callees"], new) if o != n)
        fn["callees"] = new
    if set(of) != set(lf):
        return {"defined_only_ours": sorted(set(of) - set(lf))[:5],
                "defined_only_llvm": sorted(set(lf) - set(of))[:5]}
    for name in sorted(of):
        a, b = of[name], lf[name]
        for k in ("blocks", "ops", "callees"):
            if a[k] != b[k]:
                if isinstance(a[k], list):
                    i = next((i for i, (x, y) in enumerate(zip(a[k], b[k])) if x != y),
                             min(len(a[k]), len(b[k])))
                    return {"function": name, "field": k, "at": i, "ours": a[k][i:i + 3],
                            "llvm": b[k][i:i + 3], "len": [len(a[k]), len(b[k])]}
                return {"function": name, "field": k, "ours": a[k], "llvm": b[k]}
    return None


def norm_reason(err):
    first = err.strip().splitlines()[0] if err.strip() else ""
    first = re.sub(r" \[at (?:bit|byte) \d+\]$", "", first)
    first = re.sub(r"instruction #\d+", "instruction #N", first)
    first = re.sub(r"found \d+", "found N", first)
    first = re.sub(r"basic blocks = \d+", "basic blocks = N", first)
    return first


# ------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cli", required=True)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--bcdir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--candidates", help="census JSON with mvp_candidates")
    ap.add_argument("--llvm-suffix", default="-18")
    a = ap.parse_args()
    dis, bca = "llvm-dis" + a.llvm_suffix, "llvm-bcanalyzer" + a.llvm_suffix
    os.makedirs(a.out, exist_ok=True)
    tools = {}
    for t in (dis, bca):
        rc, out, _ = run([t, "--version"])
        if rc:
            print("setup: %s unavailable" % t, file=sys.stderr)
            return 2
        tools[t] = " ".join(out.split())[:120]
    by_hash = {}
    for fn in sorted(os.listdir(a.bcdir)):
        if fn.endswith(".bc"):
            p = os.path.join(a.bcdir, fn)
            by_hash.setdefault(hashlib.sha256(open(p, "rb").read()).hexdigest(), p)

    entries, mismatches, upgrades, controls = [], 0, collections.Counter(), {}
    libs = sorted(f for f in os.listdir(a.corpus) if f.endswith(".metallib"))
    for lib in libs:
        path = os.path.join(a.corpus, lib)
        blob = open(path, "rb").read()
        rc, out, err = run([a.cli, "inspect", path])
        if rc:
            entries.append({"lib": lib, "inspect_error": norm_reason(err)})
            mismatches += 1
            continue
        ins = json.loads(out)
        for f in ins["functions"]:
            e = {"lib": lib, "index": f["index"], "name": f["name"], "type": f["type"],
                 "type_name": f["type_name"], "hash": f["hash"], "mdsz": f["mdsz"]}
            off, n = f["bitcode_file_offset"], f["mdsz"]
            e["hash_matches_slice"] = (off is not None and
                                       hashlib.sha256(blob[off:off + n]).hexdigest() == f["hash"])
            bc = by_hash.get(f["hash"])
            e["extracted_bc"] = os.path.basename(bc) if bc else None
            sel = str(f["index"])
            # records
            rc1, o1, e1 = run([a.cli, "dump-records", path, "--entry", sel])
            if bc:
                rc2, o2, e2 = run([bca, "-dump", "-non-symbolic", bc])
            else:
                rc2, o2, e2 = -1, "", "no extracted module with this hash"
            if rc1:
                e["records"] = "frontend_error: " + norm_reason(e1)
            elif rc2:
                e["records"] = "oracle_unavailable: " + norm_reason(e2)
            else:
                try:
                    oe, re_ = ours_events(o1), bca_events(o2)
                    d = compare_records(oe, re_)
                    e["records"] = "match" if d is None else {"mismatch": d}
                    if d is None and "records" not in controls:
                        # negative controls: the comparator must see a dropped
                        # record and a perturbed operand
                        k = next(i for i, x in enumerate(oe) if x[0] == "R" and x[4])
                        bumped = list(oe)
                        bumped[k] = oe[k][:4] + ((oe[k][4][0] + 1,) + oe[k][4][1:],) + oe[k][5:]
                        controls["records"] = (compare_records(oe[:-1], re_) is not None and
                                               compare_records(bumped, re_) is not None)
                except ValueError as ex:
                    e["records"] = "oracle_parse_error: %s" % ex
            # IR
            rc1, o1, e1 = run([a.cli, "dump-ir", path, "--entry", sel])
            if bc:
                rc2, o2, e2 = run([dis, "-o", "-", bc])
            else:
                rc2, o2, e2 = -1, "", "no extracted module with this hash"
            lfun = lnamed = None
            if not rc2:
                try:
                    lfun, lnamed = ll_ir(o2)
                except ValueError as ex:
                    rc2, e2 = -2, "oracle_parse_error: %s" % ex
            if rc1:
                e["ir"] = "frontend_error: " + norm_reason(e1)
            elif rc2:
                e["ir"] = "oracle_unavailable: " + norm_reason(e2)
            else:
                try:
                    ofun, onamed, otypes = ours_ir(o1)
                    if "ir" not in controls and ofun:
                        # negative controls: a dropped instruction and a renamed
                        # opcode must both be reported
                        n0 = sorted(ofun)[0]
                        drop = {k: dict(v) for k, v in ofun.items()}
                        drop[n0]["ops"] = drop[n0]["ops"][:-1]
                        ren = {k: dict(v) for k, v in ofun.items()}
                        ren[n0]["ops"] = ["frem"] + ren[n0]["ops"][1:]
                        controls["ir"] = (compare_ir(drop, lfun, collections.Counter()) is not None
                                          and compare_ir(ren, lfun, collections.Counter())
                                          is not None)
                    d = compare_ir(ofun, lfun, upgrades)
                    if d is None and onamed != lnamed:
                        d = {"named_md_ours": onamed[:8], "named_md_llvm": lnamed[:8]}
                    e["ir"] = "match" if d is None else {"mismatch": d}
                    e["defined_functions"] = len(ofun)
                    e["types"] = otypes
                except ValueError as ex:
                    e["ir"] = "oracle_parse_error: %s" % ex
            if lnamed is not None:
                e["entry_md"] = sorted({nm for nm, _ in lnamed if ENTRY_MD.match(nm)})
                feats = collections.Counter()
                for fn in lfun.values():
                    feats.update("op:" + o for o in fn["ops"] if o not in MVP_OPS)
                    feats.update("call:" + c for c in fn["callees"])
                    if fn["blocks"] > 1:
                        feats["control_flow"] += 1
                if len(lfun) != 1:
                    feats["defined_functions!=1"] += 1
                e["non_mvp_features"] = dict(sorted(feats.items()))
            # subset gate
            rc3, _, e3 = run([a.cli, "check", path, "--entry", sel])
            e["check_rc"] = rc3
            e["check"] = "OK" if rc3 == 0 else norm_reason(e3)
            for k in ("records", "ir"):
                if isinstance(e.get(k), dict) or str(e.get(k, "")).startswith("frontend_error"):
                    mismatches += 1
            if not e["hash_matches_slice"] or not bc:
                mismatches += 1
            entries.append(e)

    ok = [e for e in entries if "index" in e]
    summary = {
        "tools": tools,
        "metallibs": len(libs),
        "function_entries": len(ok),
        "by_type": dict(sorted(collections.Counter(str(e["type"]) for e in ok).items())),
        "hash_slice_ok": sum(e["hash_matches_slice"] for e in ok),
        "extracted_bc_by_hash": sum(bool(e["extracted_bc"]) for e in ok),
        "records_match": sum(e.get("records") == "match" for e in ok),
        "ir_match": sum(e.get("ir") == "match" for e in ok),
        "oracle_unavailable": sum(str(e.get(k, "")).startswith("oracle_")
                                  for e in ok for k in ("records", "ir")),
        "type_vs_entry_md": {str(t): dict(collections.Counter(",".join(e.get("entry_md", []))
                                                              or "(none)" for e in ok
                                                              if e["type"] == t))
                             for t in sorted({e["type"] for e in ok})},
        "check_ok": sorted("%s:%s" % (e["lib"].split(".")[0], e["name"])
                           for e in ok if e["check_rc"] == 0),
        "check_classes": dict(collections.Counter(e["check"] for e in ok
                                                  if e["check_rc"]).most_common()),
        "feature_classes": dict(collections.Counter(
            k for e in ok for k in e.get("non_mvp_features", {})).most_common(40)),
        "llvm_intrinsic_renames_normalized": dict(upgrades),
        "mismatch_samples": [{"lib": e["lib"], "index": e["index"], "records": e.get("records"),
                              "ir": e.get("ir")} for e in ok
                             if e.get("records") != "match" or e.get("ir") != "match"][:6],
        "structural_mismatches": mismatches,
    }
    if a.candidates:
        cands = json.load(open(a.candidates))["mvp_candidates"]
        summary["census_candidates"] = {
            c: next(("%s (rc %d)" % (e["check"], e["check_rc"]) for e in ok if e["name"] == c),
                    "not found") for c in cands}
    summary["oracle_negative_controls"] = controls
    controls_ok = controls.get("records") is True and controls.get("ir") is True
    if mismatches:
        summary["verdict"] = "O1_MISMATCH"
    elif summary["oracle_unavailable"] or not controls_ok:
        summary["verdict"] = "O1_PARTIAL"
    else:
        summary["verdict"] = "O1_VERIFIED"
    with open(os.path.join(a.out, "o1-report.json"), "w") as fo:
        json.dump({"summary": summary, "entries": entries}, fo, indent=1)
    print(json.dumps(summary, indent=1))
    print("o1: %s - %d entries, records %d, ir %d, hash %d, mismatches %d (%s)" %
          (summary["verdict"], len(ok), summary["records_match"], summary["ir_match"],
           summary["hash_slice_ok"], mismatches, a.out))
    return 0 if mismatches == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
