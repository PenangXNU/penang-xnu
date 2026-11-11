#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Cross-compiles the freestanding air2ir3 frontend (container, bitstream,
# module, AIR services) to Mach-O arm64 objects for iOS, without any SDK
# (-ffreestanding -nostdlibinc: only clang's own stddef/stdint/stdbool), and
# writes a manifest with compiler, triple, flags, object hashes, file headers
# and the undefined symbols each object needs from its runtime.
#
# This proves the frontend compiles for the target ABI. It does NOT show that
# anything runs on iPadOS, links into an IOKit driver, or reaches the Adreno.
# Host-only layers (host_io, CLI) and Mesa-dependent layers (air_to_nir,
# ir3_emit) are out of scope for this build.
#
# Env: M9_WORK (default ~/rom-work/penang/m9-air-nir-frontend-20261028),
#      A2I_OUT (default $M9_WORK/out), CLANG (default clang-18),
#      A2I_TRIPLE (default arm64-apple-ios18.7).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
M9_WORK="${M9_WORK:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028}"
OUT="${A2I_OUT:-$M9_WORK/out}/arm64"
CLANG="${CLANG:-clang-18}"
TRIPLE="${A2I_TRIPLE:-arm64-apple-ios18.7}"
SUF="${CLANG#clang}"
mkdir -p "$OUT"
FLAGS=(-target "$TRIPLE" -std=c11 -ffreestanding -nostdlibinc -O2 -g0 -fno-stack-protector
       -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion -Wpedantic)
CORE=(a2i_core a2i_mtlb a2i_bitstream a2i_module a2i_air)
OBJS=()
for f in "${CORE[@]}"; do
  "$CLANG" "${FLAGS[@]}" -c "$SRC/$f.c" -o "$OUT/$f.o"
  OBJS+=("$OUT/$f.o")
done
# static archive of the frontend (ld64.lld has no -r); the manifest computes the
# link closure: symbols still undefined after all objects = runtime dependencies
rm -f "$OUT/libair2ir3_frontend.a"
"llvm-ar$SUF" rcsD "$OUT/libair2ir3_frontend.a" "${OBJS[@]}"

python3 - "$CLANG" "$TRIPLE" "$SUF" "$OUT/manifest.json" "${OBJS[@]}" -- "${FLAGS[@]}" <<'EOF'
import hashlib, json, os, subprocess, sys
args = sys.argv[1:]
sep = args.index("--")
clang, triple, suf, manifest, objs, flags = args[0], args[1], args[2], args[3], args[4:sep], args[sep + 1:]
def run(*c):
    return subprocess.run(c, capture_output=True, text=True, check=True).stdout
def hdr(o):
    h = {}
    for line in run("llvm-readobj" + suf, "--file-header", o).splitlines():
        k, _, v = line.strip().partition(":")
        if k in ("Format", "Arch", "AddressSize", "FileType", "Magic", "CpuType", "CpuSubType"):
            h[k] = v.strip()
    return h
out = {
    "classification": "CROSS_BUILD_OBJECTS_ONLY",
    "gpu_executed": False, "ipados_executed": False,
    "compiler": run(clang, "--version").splitlines()[0],
    "target_triple": triple, "flags": flags, "sdk": None,
    "objects": [],
}
for o in objs:
    data = open(o, "rb").read()
    und = sorted(l.split()[-1] for l in run("llvm-nm" + suf, "-u", o).splitlines() if l.strip())
    defs = [l.split()[-1] for l in run("llvm-nm" + suf, "-g", "--defined-only", o).splitlines() if l.strip()]
    out["objects"].append({"file": os.path.basename(o), "size": len(data),
                           "sha256": hashlib.sha256(data).hexdigest(), "header": hdr(o),
                           "undefined": und, "defined_globals": len(defs)})
defined = set()
for o in objs:
    defined |= {l.split()[-1] for l in run("llvm-nm" + suf, "-g", "--defined-only", o).splitlines() if l.strip()}
closure = sorted({u for ob in out["objects"] for u in ob["undefined"]} - defined)
dups = sorted(s for s in defined if sum(
    s in {l.split()[-1] for l in run("llvm-nm" + suf, "-g", "--defined-only", o).splitlines() if l.strip()}
    for o in objs) > 1)
out["link_closure_undefined"] = closure
out["duplicate_definitions"] = dups
lib = os.path.join(os.path.dirname(manifest), "libair2ir3_frontend.a")
out["archive"] = {"file": os.path.basename(lib), "sha256": hashlib.sha256(open(lib, "rb").read()).hexdigest()}
json.dump(out, open(manifest, "w"), indent=1)
print("formats:", sorted({ob["header"].get("Format", "?") for ob in out["objects"]}))
print("link closure (runtime must provide):", closure, "duplicates:", dups)
EOF
echo "arm64 objects + manifest in $OUT"
