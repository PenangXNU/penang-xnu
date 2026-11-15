#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the full air2ir3 CLI (inspect/dump/check + compile) under ASan/UBSan,
# linked against the pinned, unmodified Mesa NIR/ir3 static libraries produced
# by build_mesa.sh. Mesa itself is not rebuilt or modified here.
#
# Layers: freestanding frontend (a2i_core/mtlb/bitstream/module/air) and host
# CLI (host_io, a2i_cli) use the strict flags of build_host.sh; only
# air_to_nir.c, ir3_emit.c and a2i_compile.c see Mesa headers.
#
# Env: M9_WORK (default ~/rom-work/penang/m9-air-nir-frontend-20261028),
#      A2I_OUT (default $M9_WORK/out), JOBS (unused: single translation units).
set -euo pipefail
PIN=5f31557bb1a51004a8bbac397af99fd6a2cbb59a
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
M9_WORK="${M9_WORK:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028}"
MESA="$M9_WORK/deps/mesa"
BUILD="$M9_WORK/mesa-build"
OUT="${A2I_OUT:-$M9_WORK/out}"
OBJ="$OUT/obj-compile"
mkdir -p "$OBJ"

[ "$(git -C "$MESA" rev-parse HEAD)" = "$PIN" ] || { echo "Mesa not at pin $PIN" >&2; exit 1; }
[ -f "$BUILD/air2ir3-libs.txt" ] || { echo "run build_mesa.sh first ($BUILD/air2ir3-libs.txt missing)" >&2; exit 1; }
mapfile -t LIBS < "$BUILD/air2ir3-libs.txt"
for l in "${LIBS[@]}"; do [ -f "$BUILD/$l" ] || { echo "missing Mesa library $l (run build_mesa.sh)" >&2; exit 1; }; done

# Mesa's own -I/-D/-std flags for an ir3 translation unit (paths relative to
# $BUILD). Mesa include dirs become -isystem so -Wextra/-Werror apply to the
# air2ir3 sources only, not to warnings inside Mesa's inline headers.
MESA_FLAGS=$(cd "$BUILD" && ninja -t commands src/freedreno/ir3/libfreedreno_ir3.a |
  python3 -c 'import shlex, sys
line = next(l for l in sys.stdin if "ir3_shader.c.o -c" in l)
out = []
for a in shlex.split(line):
    if a.startswith("-I"):
        out += ["-isystem", a[2:]]
    elif a.startswith(("-D", "-std=")):
        out.append(a)
print(shlex.join(out))')

SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)
STRICT=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion "${SAN[@]}")
MESA_TU=(-O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers
         "${SAN[@]}" -DA2I_MESA_PIN="\"$PIN\"" -I"$SRC")

OBJS=()
for f in a2i_core a2i_mtlb a2i_bitstream a2i_module a2i_air host_io a2i_cli; do
  cc "${STRICT[@]}" -c "$SRC/$f.c" -o "$OBJ/$f.o"
  OBJS+=("$OBJ/$f.o")
done
for f in air_to_nir ir3_emit a2i_compile; do
  (cd "$BUILD" && eval cc "$MESA_FLAGS" '"${MESA_TU[@]}"' -c "$SRC/$f.c" -o "$OBJ/$f.o")
  OBJS+=("$OBJ/$f.o")
done
(cd "$BUILD" && c++ "${SAN[@]}" -o "$OUT/air2ir3" "${OBJS[@]}" -Wl,--start-group "${LIBS[@]}" \
   -Wl,--end-group -lz -pthread -lm)
echo "built $OUT/air2ir3 (Mesa $PIN, ASan/UBSan)"

# test oracles that need Mesa (tests/oracles/*.c): O3 assembler round-trip
TESTS="$HERE/tests/oracles"
(cd "$BUILD" && eval cc "$MESA_FLAGS" '"${MESA_TU[@]}"' -c "$TESTS/o3_asm_roundtrip.c" -o "$OBJ/o3_asm_roundtrip.o")
(cd "$BUILD" && c++ "${SAN[@]}" -o "$OUT/a2i-o3-asm" "$OBJ/o3_asm_roundtrip.o" -Wl,--start-group \
   "${LIBS[@]}" -Wl,--end-group -lz -pthread -lm)
echo "built $OUT/a2i-o3-asm"
# O2 NIR evaluator: frontend + air_to_nir + ir3 options, without the CLI mains
(cd "$BUILD" && eval cc "$MESA_FLAGS" '"${MESA_TU[@]}"' -c "$TESTS/o2_nir_eval.c" -o "$OBJ/o2_nir_eval.o")
O2_OBJS=()
for f in a2i_core a2i_mtlb a2i_bitstream a2i_module a2i_air host_io air_to_nir ir3_emit o2_nir_eval; do
  O2_OBJS+=("$OBJ/$f.o")
done
(cd "$BUILD" && c++ "${SAN[@]}" -o "$OUT/a2i-o2-nireval" "${O2_OBJS[@]}" -Wl,--start-group \
   "${LIBS[@]}" -Wl,--end-group -lz -pthread -lm)
echo "built $OUT/a2i-o2-nireval"
