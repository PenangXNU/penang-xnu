#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# air2ir3 CLI regression and error-path tests. Inputs are only the MIT
# synthetic fixtures in tests/fixtures/synthetic (assembled with llvm-as-18 and
# wrapped by gen_metallib.py at test time); no Apple artefact is read.
#
# Env: A2I_BIN (CLI under test, ASan/UBSan build expected), A2I_TEST_OUT
#      (scratch dir, default mktemp), LLVM_AS (default llvm-as-18).
# Exit: 0 all pass, 1 any failure, 2 setup error.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FIX="$HERE/fixtures/synthetic"
BIN="${A2I_BIN:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028/out/air2ir3-host}"
OUT="${A2I_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/a2i-cli-tests.XXXXXX")}"
LLVM_AS="${LLVM_AS:-llvm-as-18}"
mkdir -p "$OUT"
PASS=0 FAIL=0
ok() { PASS=$((PASS + 1)); printf 'PASS %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }

# run NAME WANT_RC STDERR_PREFIX -- CMD...   (prefix "" = not checked)
run() {
  local name=$1 want=$2 pfx=$3
  shift 4
  "$@" >"$OUT/$name.out" 2>"$OUT/$name.err"
  local rc=$?
  if grep -q -E 'runtime error:|AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer' "$OUT/$name.err"; then
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

# json NAME EXPR  -- validates $OUT/NAME.out with the independent checker
json() {
  if python3 "$HERE/json_check.py" "$OUT/$1.out" "$2" 2>"$OUT/$1.json.err"; then
    ok "$1.json"
  else
    bad "$1.json" "$(head -c 240 "$OUT/$1.json.err")"
  fi
}

# out_has NAME REGEX -- stdout of NAME matches REGEX
out_has() {
  if grep -q -E "$2" "$OUT/$1.out"; then ok "$1.stdout"; else bad "$1.stdout" "no match for /$2/"; fi
}

gen() { local o=$1; shift; python3 "$HERE/gen_metallib.py" "$OUT/$o.metallib" "$@" || { echo "setup: gen $o failed" >&2; exit 2; }; }

[ -x "$BIN" ] || { echo "setup: missing CLI $BIN (run build_host.sh)" >&2; exit 2; }
for f in vs_scale fs_tint neg_fdiv neg_branch neg_kernel neg_texture neg_select_global; do
  "$LLVM_AS" "$FIX/$f.ll" -o "$OUT/$f.bc" || { echo "setup: $LLVM_AS $f.ll failed" >&2; exit 2; }
done

gen vs --bc "$OUT/vs_scale.bc" --name vs_scale
gen fs --bc "$OUT/fs_tint.bc" --name fs_tint --type 1
gen type4 --bc "$OUT/vs_scale.bc" --name vs_scale --type 4
gen type255 --bc "$OUT/vs_scale.bc" --name vs_scale --type 255
# NAME bytes: a " b \ c TAB d DEL 0xFF(invalid) U+00E9 z, then ED A0 80 (encoded
# surrogate) -> each invalid byte must become U+FFFD and set name_lossy.
gen oddname --bc "$OUT/vs_scale.bc" --name-hex 6122625c6309647fffc3a97aeda080
gen multi --bc "$OUT/vs_scale.bc" --name vs_scale --fn "$OUT/fs_tint.bc:fs_tint:1"
gen fat --bc "$OUT/vs_scale.bc" --name vs_scale --mutate fat

# ---- Marco A: inspect JSON (regression: '"type": 4 ("other")' was invalid JSON)
run inspect_vs 0 "" -- "$BIN" inspect "$OUT/vs.metallib"
json inspect_vs 'j["function_count"] == 1 and j["functions"][0]["type"] == 0 and j["functions"][0]["type_name"] == "vertex" and j["header_policy"] == "MVP_OK"'
VS_SHA=$(sha256sum "$OUT/vs_scale.bc" | cut -d' ' -f1)
json inspect_vs "j['functions'][0]['hash'] == '$VS_SHA' and j['functions'][0]['bitcode_file_offset'] > 0x58"
run inspect_type4 0 "" -- "$BIN" inspect "$OUT/type4.metallib"
json inspect_type4 'j["functions"][0]["type"] == 4 and j["functions"][0]["type_name"] == "unknown"'
run inspect_type255 0 "" -- "$BIN" inspect "$OUT/type255.metallib"
json inspect_type255 'j["functions"][0]["type"] == 255 and j["functions"][0]["type_name"] == "unknown"'
run inspect_oddname 0 "" -- "$BIN" inspect "$OUT/oddname.metallib"
json inspect_oddname 'j["functions"][0]["name"] == "a\"b\\c\td\x7f\ufffd\u00e9z\ufffd\ufffd\ufffd" and j["functions"][0]["name_lossy"] is True'
run inspect_multi 0 "" -- "$BIN" inspect "$OUT/multi.metallib"
json inspect_multi 'j["function_count"] == 2 and [f["name"] for f in j["functions"]] == ["vs_scale", "fs_tint"] and [f["index"] for f in j["functions"]] == [0, 1]'
run inspect_fat 0 "" -- "$BIN" inspect "$OUT/fat.metallib"
json inspect_fat 'j["fat_slices"] == 1 and j["function_count"] == 1'
run inspect_entry_name 0 "entry fs_tint -> index 1" -- "$BIN" inspect "$OUT/multi.metallib" --entry fs_tint
json inspect_entry_name 'j["function_count"] == 2'

# ---- Marco A: the four inspection modes on valid fixtures
run dump_records_vs 0 "" -- "$BIN" dump-records "$OUT/vs.metallib"
out_has dump_records_vs '^BLOCK 13$'
run dump_ir_vs 0 "" -- "$BIN" dump-ir "$OUT/vs.metallib" --entry vs_scale
out_has dump_ir_vs '^define <\{<4 x float>, <4 x float>, <2 x float>\}> @vs_scale\('
run dump_ir_fs_idx 0 "" -- "$BIN" dump-ir "$OUT/multi.metallib" --entry 1
out_has dump_ir_fs_idx '^entry 1 fs_tint$'
run check_vs 0 "" -- "$BIN" check "$OUT/vs.metallib"
out_has check_vs '^OK vs_scale stage=vertex insts=11 in=4 out=3$'
run check_fs 0 "" -- "$BIN" check "$OUT/fs.metallib"
out_has check_fs '^OK fs_tint stage=fragment insts=9 in=3 out=1$'
run check_fat 0 "" -- "$BIN" check "$OUT/fat.metallib"

# ---- usage and selection
run usage_noargs 64 "usage:" -- "$BIN"
run usage_badmode 64 "usage:" -- "$BIN" frobnicate "$OUT/vs.metallib"
run select_unknown 64 "E_USAGE: no function entry named 'nope'" -- "$BIN" check "$OUT/vs.metallib" --entry nope
run select_range 64 "E_USAGE: entry index out of range" -- "$BIN" dump-ir "$OUT/multi.metallib" --entry 2
run io_missing 1 "E_IO: cannot open" -- "$BIN" inspect "$OUT/does-not-exist.metallib"
# the host build (build_host.sh) has no backend; set A2I_EXPECT_STUB=0 when
# A2I_BIN is the full build from build_compile.sh (covered by run_compile_tests.sh)
if [ "${A2I_EXPECT_STUB:-1}" = 1 ]; then
  run compile_stub 64 "this build has no Mesa backend" -- "$BIN" compile "$OUT/vs.metallib" "$OUT/never"
fi

# ---- error paths: each malformed input maps to one explicit status/exit code
for m in bad-magic truncate file-size list-size dup-tag no-endt fat-no-air; do
  gen "m_$m" --bc "$OUT/vs_scale.bc" --name vs_scale --mutate "$m"
  run "container_$m" 2 "E_CONTAINER:" -- "$BIN" inspect "$OUT/m_$m.metallib"
done
for m in unknown-tag bc-oob; do   # container parses; the selected entry is rejected
  gen "m_$m" --bc "$OUT/vs_scale.bc" --name vs_scale --mutate "$m"
  run "inspect_$m" 0 "" -- "$BIN" inspect "$OUT/m_$m.metallib"
  run "entry_$m" 2 "E_CONTAINER:" -- "$BIN" check "$OUT/m_$m.metallib"
done
gen m_hash --bc "$OUT/vs_scale.bc" --name vs_scale --mutate hash
run integrity_hash 3 "E_INTEGRITY:" -- "$BIN" check "$OUT/m_hash.metallib"
run integrity_hash_inspect 3 "E_INTEGRITY:" -- "$BIN" inspect "$OUT/m_hash.metallib" --entry 0
json integrity_hash_inspect 'j["function_count"] == 1'
gen m_air26 --bc "$OUT/vs_scale.bc" --name vs_scale --air 2.6
run version_air 4 "E_VERSION: unsupported VERS AIR 2.6" -- "$BIN" check "$OUT/m_air26.metallib"
gen m_file27 --bc "$OUT/vs_scale.bc" --name vs_scale --file-ver 2.7
run version_file 4 "E_VERSION: unsupported MTLB file version 2.7" -- "$BIN" check "$OUT/m_file27.metallib"
run version_file_inspect 0 "" -- "$BIN" inspect "$OUT/m_file27.metallib"
json version_file_inspect 'j["header_policy"] == "REJECT" and j["file_version"] == "2.7"'
gen m_macos --bc "$OUT/vs_scale.bc" --name vs_scale --os-type 0x81 --os-major 15
run version_os 4 "E_VERSION: unsupported target OS" -- "$BIN" check "$OUT/m_macos.metallib"
gen m_bcmagic --bc "$OUT/vs_scale.bc" --name vs_scale --mutate bc-magic
run bitstream_magic 5 "E_BITSTREAM:" -- "$BIN" dump-records "$OUT/m_bcmagic.metallib"
run bitstream_magic_ir 5 "E_BITSTREAM:" -- "$BIN" check "$OUT/m_bcmagic.metallib"
gen m_namediff --bc "$OUT/vs_scale.bc" --name not_vs_scale
run name_mismatch 2 "E_CONTAINER: bitcode function 'vs_scale' differs from MTLB entry NAME" -- "$BIN" check "$OUT/m_namediff.metallib"
gen n_fdiv --bc "$OUT/neg_fdiv.bc" --name neg_fdiv --type 1
run unsupported_fdiv 6 "E_UNSUPPORTED_IR: instruction #0 'fdiv'" -- "$BIN" check "$OUT/n_fdiv.metallib"
gen n_branch --bc "$OUT/neg_branch.bc" --name neg_branch --type 1
run unsupported_branch 6 "E_UNSUPPORTED_IR: control flow outside MVP-AIR-0" -- "$BIN" check "$OUT/n_branch.metallib"
gen n_kernel --bc "$OUT/neg_kernel.bc" --name neg_kernel --type 2
run unsupported_kernel 7 "E_UNSUPPORTED_BINDING: entry point kind outside MVP-AIR-0: air.kernel" -- "$BIN" check "$OUT/n_kernel.metallib"
gen n_texture --bc "$OUT/neg_texture.bc" --name neg_texture --type 1
run unsupported_texture 7 "E_UNSUPPORTED_BINDING: input binding outside MVP-AIR-0: 'air.texture'" -- "$BIN" check "$OUT/n_texture.metallib"

# regression: select over a global pointer used to fail with E_BITSTREAM
gen n_selglob --bc "$OUT/neg_select_global.bc" --name neg_select_global --type 1
run selglob_dump_ir 0 "" -- "$BIN" dump-ir "$OUT/n_selglob.metallib"
out_has selglob_dump_ir '^  \[2\] bb0 select +ptr addrspace\(2\) ops\['
run selglob_check 6 "E_UNSUPPORTED_IR: instruction #1 'fcmp' outside MVP-AIR-0" -- "$BIN" check "$OUT/n_selglob.metallib"

printf 'cli-tests: %d passed, %d failed (scratch %s)\n' "$PASS" "$FAIL" "$OUT"
[ "$FAIL" -eq 0 ]
