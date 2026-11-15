#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Builds the air2ir3 host CLI WITHOUT the Mesa backend (inspect/dump/check),
# under ASan+UBSan. The 'compile' mode is a weak stub in this build.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src"
OUT="${A2I_OUT:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028/out}"
mkdir -p "$OUT"
CORE=(a2i_core.c a2i_mtlb.c a2i_bitstream.c a2i_module.c a2i_air.c host_io.c a2i_cli.c)
FLAGS=(-std=c11 -O1 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion
       -fsanitize=address,undefined -fno-sanitize-recover=all)
cc "${FLAGS[@]}" "${CORE[@]/#/$SRC/}" -o "$OUT/air2ir3-host"
echo "built $OUT/air2ir3-host"
