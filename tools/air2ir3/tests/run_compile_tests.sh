#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# air2ir3 `compile` tests (AIR -> NIR -> ir3, Adreno 619 / a6xx) on the MIT
# synthetic fixtures only. Host compile + decode; nothing runs on a GPU.
#
# Env: A2I_COMPILE_BIN (full CLI from build_compile.sh), A2I_TEST_OUT,
#      LLVM_AS (default llvm-as-18), IR3_DISASM (Mesa's standalone decoder,
#      default $M9_WORK/mesa-build/src/freedreno/isa/ir3-disasm).
# Exit: 0 all pass, 1 any failure, 2 setup error.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FIX="$HERE/fixtures/synthetic"
M9_WORK="${M9_WORK:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028}"
BIN="${A2I_COMPILE_BIN:-$M9_WORK/out/air2ir3}"
DIS="${IR3_DISASM:-$M9_WORK/mesa-build/src/freedreno/isa/ir3-disasm}"
OUT="${A2I_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/a2i-compile-tests.XXXXXX")}"
LLVM_AS="${LLVM_AS:-llvm-as-18}"
CHIP=06010900
mkdir -p "$OUT"
PASS=0 FAIL=0
ok() { PASS=$((PASS + 1)); printf 'PASS %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }
check() { if eval "$2"; then ok "$1"; else bad "$1" "$3"; fi; }

# compile NAME WANT_RC STDERR_PREFIX -- ARGS...   (prefix = $OUT/NAME)
compile() {
  local name=$1 want=$2 pfx=$3
  shift 4
  rm -f "$OUT/$name".{bin,dis,nir,json}
  "$BIN" compile "$1" "$OUT/$name" "${@:2}" >"$OUT/$name.out" 2>"$OUT/$name.err"
  local rc=$?
  if grep -q -E 'runtime error:|AddressSanitizer|LeakSanitizer' "$OUT/$name.err"; then
    bad "$name" "sanitizer report in $OUT/$name.err"; return 1
  fi
  if [ "$rc" != "$want" ]; then
    bad "$name" "rc=$rc want=$want: $(head -c 240 "$OUT/$name.err")"; return 1
  fi
  if [ -n "$pfx" ] && ! grep -q "^$pfx" "$OUT/$name.err"; then
    bad "$name" "stderr lacks '$pfx': $(head -c 240 "$OUT/$name.err")"; return 1
  fi
  ok "$name"
}

json() {
  if python3 "$HERE/json_check.py" "$OUT/$1.json" "$2" 2>"$OUT/$1.jsonchk"; then ok "$1.json"
  else bad "$1.json" "$(head -c 240 "$OUT/$1.jsonchk")"; fi
}

# decoded instruction text, one per line, from either decoder's output
insns() { sed -E 's/^ *[0-9]+\[[0-9a-f]{8}_[0-9a-f]{8}\] //; s/^[[:space:]]+//' "$1" | grep -v '^$'; }

gen() { local o=$1; shift; python3 "$HERE/gen_metallib.py" "$OUT/$o.metallib" "$@" || { echo "setup: gen $o failed" >&2; exit 2; }; }

[ -x "$BIN" ] || { echo "setup: missing $BIN (run build_compile.sh)" >&2; exit 2; }
[ -x "$DIS" ] || { echo "setup: missing Mesa ir3-disasm $DIS (run build_mesa.sh)" >&2; exit 2; }
for f in vs_scale fs_tint neg_fdiv neg_texture; do
  "$LLVM_AS" "$FIX/$f.ll" -o "$OUT/$f.bc" || { echo "setup: $LLVM_AS $f.ll failed" >&2; exit 2; }
done
gen vs --bc "$OUT/vs_scale.bc" --name vs_scale
gen fs --bc "$OUT/fs_tint.bc" --name fs_tint --type 1
gen multi --bc "$OUT/vs_scale.bc" --name vs_scale --fn "$OUT/fs_tint.bc:fs_tint:1"

COMMON='j["classification"] == "FRONTEND_TO_IR3_HOST_COMPILE" and j["gpu_executed"] is False and j["target"]["chip_id"] == "0x06010900" and j["target"]["compiler_gen"] == 6 and j["target"]["gpu"] == "FD619" and j["bin"]["size"] % 8 == 0'

# ---- Marco C: synthetic vertex and fragment shaders through the real pipeline
for t in vs fs; do
  compile "c_$t" 0 "compiled " -- "$OUT/$t.metallib"
  check "c_$t.outputs" '[ -s "$OUT/c_$t.bin" ] && [ -s "$OUT/c_$t.dis" ] && [ -s "$OUT/c_$t.nir" ] && [ -s "$OUT/c_$t.json" ]' "missing output file"
  json "c_$t" "$COMMON"
  SHA=$(sha256sum "$OUT/c_$t.bin" | cut -d' ' -f1)
  SIZE=$(stat -c %s "$OUT/c_$t.bin")
  json "c_$t" "j['bin']['sha256'] == '$SHA' and j['bin']['size'] == $SIZE"
  # independent decode: Mesa's standalone ir3-disasm (separate process, chip id
  # given explicitly) must decode the file without errors and agree with .dis
  "$DIS" -c "$CHIP" "$OUT/c_$t.bin" >"$OUT/c_$t.tooldis" 2>&1
  DRC=$?
  check "c_$t.decode_clean" '[ "$DRC" -eq 0 ] && ! grep -q -i -E "error|unknown|\?\?\?" "$OUT/c_$t.tooldis"' "decoder rc=$DRC or reported errors"
  check "c_$t.decode_agrees" 'diff <(insns "$OUT/c_$t.tooldis") <(insns "$OUT/c_$t.dis") >/dev/null' "standalone decode differs from in-process decode"
  check "c_$t.decode_end" 'insns "$OUT/c_$t.tooldis" | grep -q "^end$"' "no end instruction"
  # decode -> Mesa ir3 assembler -> identical words (O3 round-trip tool)
  if [ -x "${A2I_O3_ASM:-$M9_WORK/out/a2i-o3-asm}" ]; then
    check "c_$t.asm_roundtrip" '"${A2I_O3_ASM:-$M9_WORK/out/a2i-o3-asm}" "$OUT/c_$t.bin" >"$OUT/c_$t.o3asm" 2>&1' "$(head -c 200 "$OUT/c_$t.o3asm" 2>/dev/null)"
  fi
done
json c_vs 'j["input"]["stage"] == "vertex" and j["nir"]["fp_alu_strict"] == 2 and j["nir"]["ubo_loads"] == 2 and [o["slot_name"] for o in j["bindings"]["outputs"]] == ["VARYING_SLOT_POS", "VARYING_SLOT_VAR0", "VARYING_SLOT_VAR1"] and [o["slot"] for o in j["ir3"]["outputs"]] == [0, 32, 33] and [i["slot_name"] for i in j["bindings"]["inputs"][:3]] == ["VERT_ATTRIB_GENERIC0", "VERT_ATTRIB_GENERIC1", "VERT_ATTRIB_GENERIC2"] and j["bindings"]["inputs"][3]["slot_name"] == "UBO0" and j["ir3"]["ubo_const_ranges"][0]["ubo"] == 0'
json c_fs 'j["input"]["stage"] == "fragment" and j["nir"]["fp_alu_strict"] == 4 and [o["slot_name"] for o in j["bindings"]["outputs"]] == ["FRAG_RESULT_DATA0"] and [(i["slot_name"], i.get("interpolation")) for i in j["bindings"]["inputs"][:2]] == [("VARYING_SLOT_VAR0", "perspective"), ("VARYING_SLOT_VAR1", "noperspective")] and j["ir3"]["outputs"][0]["slot"] == 4 and j["ir3"]["ubo_const_ranges"][0]["ubo"] == 1'
# the strict fmul+fadd pair stays exact in NIR; ir3 emits a6xx mad.f32, which
# Mesa documents as unfused on a6xx (ir3_compiler_nir.c, nir_op_fmad)
check c_vs.nir_exact 'grep -q "fmul .*// exact" "$OUT/c_vs.nir" && grep -q "fadd .*// exact" "$OUT/c_vs.nir"' "fp ops not exact in NIR"
check c_vs.isa_mad 'insns "$OUT/c_vs.dis" | grep -q "mad.f32"' "no mad.f32"
check c_fs.isa_bary 'test "$(insns "$OUT/c_fs.dis" | grep -c "bary.f")" -eq 2' "expected 2 bary.f (perspective + noperspective)"
check c_fs.isa_neg 'insns "$OUT/c_fs.dis" | grep -q "(neg)c0.w"' "fsub not lowered to add with (neg)"

# ---- reproducibility: a second compile is byte-identical in every output
compile c_vs_again 0 "compiled " -- "$OUT/vs.metallib"
for x in bin dis nir json; do
  check "c_vs.deterministic.$x" 'cmp -s "$OUT/c_vs.$x" "$OUT/c_vs_again.$x"' "outputs differ between runs"
done

# ---- entry selection
compile c_multi_fs 0 "compiled fs_tint (fragment)" -- "$OUT/multi.metallib" --entry fs_tint
check c_multi_fs.same_bin 'cmp -s "$OUT/c_multi_fs.bin" "$OUT/c_fs.bin"' "fs_tint compiled from a multi-entry library differs"

# ---- failures stop at their layer and leave no output files
noout() { check "$1.no_outputs" '! ls "$OUT/$1".{bin,dis,nir,json} >/dev/null 2>&1' "outputs written on failure"; }
gen n_fdiv --bc "$OUT/neg_fdiv.bc" --name neg_fdiv --type 1
compile cf_fdiv 6 "E_UNSUPPORTED_IR:" -- "$OUT/n_fdiv.metallib"; noout cf_fdiv
gen n_texture --bc "$OUT/neg_texture.bc" --name neg_texture --type 1
compile cf_texture 7 "E_UNSUPPORTED_BINDING:" -- "$OUT/n_texture.metallib"; noout cf_texture
gen m_hash --bc "$OUT/vs_scale.bc" --name vs_scale --mutate hash
compile cf_hash 3 "E_INTEGRITY:" -- "$OUT/m_hash.metallib"; noout cf_hash
gen m_magic --bc "$OUT/vs_scale.bc" --name vs_scale --mutate bad-magic
compile cf_magic 2 "E_CONTAINER:" -- "$OUT/m_magic.metallib"; noout cf_magic
gen m_bc --bc "$OUT/vs_scale.bc" --name vs_scale --mutate bc-magic
compile cf_bitstream 5 "E_BITSTREAM:" -- "$OUT/m_bc.metallib"; noout cf_bitstream
gen m_air --bc "$OUT/vs_scale.bc" --name vs_scale --air 2.6
compile cf_version 4 "E_VERSION:" -- "$OUT/m_air.metallib"; noout cf_version
"$BIN" compile "$OUT/vs.metallib" >"$OUT/cf_usage.out" 2>"$OUT/cf_usage.err"
URC=$?
check cf_usage '[ "$URC" -eq 64 ]' "missing OUT_PREFIX must be E_USAGE (rc=$URC)"

printf 'compile-tests: %d passed, %d failed (scratch %s)\n' "$PASS" "$FAIL" "$OUT"
[ "$FAIL" -eq 0 ]
