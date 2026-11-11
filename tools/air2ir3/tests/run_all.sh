#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Rebuilds air2ir3 incrementally and runs every M9 check in order:
#   host CLI (ASan/UBSan) + CLI tests        Marco A
#   O1 structural oracle (only if the private corpus is present)   Marco B
#   full build + compile tests               Marco C
#   O2 LLVM-vs-NIR semantics, O3 ir3 backend, ARM64 cross-build    Marco D
# Scratch output goes to $A2I_RUN_DIR (default mktemp). Corpus reports stay
# under $M9_WORK/analysis. Nothing touches the phone or a GPU.
# Exit: number of failed stages (0 = all passed).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
M9_WORK="${M9_WORK:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028}"
RUN="${A2I_RUN_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/a2i-run.XXXXXX")}"
OUT="${A2I_OUT:-$M9_WORK/out}"
CORPUS="${A2I_CORPUS:-$M9_WORK/apple-inputs}"
export M9_WORK A2I_OUT="$OUT"
mkdir -p "$RUN"
FAILED=0
stage() {
  local name=$1
  shift
  if "$@" >"$RUN/$name.log" 2>&1; then
    printf 'STAGE %-14s PASS  %s\n' "$name" "$(tail -1 "$RUN/$name.log")"
  else
    FAILED=$((FAILED + 1))
    printf 'STAGE %-14s FAIL  (see %s)\n' "$name" "$RUN/$name.log"
  fi
}
stage build_host bash "$ROOT/build_host.sh"
stage cli_tests env A2I_BIN="$OUT/air2ir3-host" A2I_TEST_OUT="$RUN/cli" bash "$HERE/run_cli_tests.sh"
if [ -f "$CORPUS/SHA256SUMS" ] && [ -d "$M9_WORK/analysis/bc" ]; then
  stage o1 python3 "$HERE/oracles/o1_structural.py" --cli "$OUT/air2ir3-host" --corpus "$CORPUS" \
    --bcdir "$M9_WORK/analysis/bc" --out "$M9_WORK/analysis/o1-$(date +%Y%m%d)" \
    --candidates "$M9_WORK/analysis/census.json"
else
  printf 'STAGE %-14s SKIP  (private corpus not present)\n' o1
fi
stage build_compile bash "$ROOT/build_compile.sh"
stage cli_tests_full env A2I_BIN="$OUT/air2ir3" A2I_EXPECT_STUB=0 A2I_TEST_OUT="$RUN/cli-full" \
  bash "$HERE/run_cli_tests.sh"
stage compile_tests env A2I_COMPILE_BIN="$OUT/air2ir3" A2I_TEST_OUT="$RUN/compile" \
  bash "$HERE/run_compile_tests.sh"
stage o2 python3 "$HERE/oracles/o2_semantics.py" --nireval "$OUT/a2i-o2-nireval" --out "$RUN/o2"
stage o3 python3 "$HERE/oracles/o3_backend.py" --compile "$OUT/air2ir3" --asm "$OUT/a2i-o3-asm" \
  --out "$RUN/o3"
stage arm64 bash "$ROOT/build_arm64.sh"
echo "run_all: $FAILED failed stage(s); logs in $RUN"
exit "$FAILED"
