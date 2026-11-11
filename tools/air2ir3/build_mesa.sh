#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Isolated, out-of-tree build of the pinned Mesa ir3/NIR static libraries for
# air2ir3 (Penang XNU M9). Never writes into the M7 job: the source is a
# `git clone --shared` of the M7 checkout at the pin, the wrap package cache is
# copied after SHA-256 verification, and the build directory is M9-owned.
#
# Env: M9_WORK (default ~/rom-work/penang/m9-air-nir-frontend-20261028)
#      M7_WORK (default ~/rom-work/penang/m7-frontier-metal-repair-20261028)
set -euo pipefail
PIN=5f31557bb1a51004a8bbac397af99fd6a2cbb59a
M9_WORK="${M9_WORK:-$HOME/rom-work/penang/m9-air-nir-frontend-20261028}"
M7_WORK="${M7_WORK:-$HOME/rom-work/penang/m7-frontier-metal-repair-20261028}"
SRC="$M9_WORK/deps/mesa"
BUILD="$M9_WORK/mesa-build"
VENV="$M7_WORK/tools-venv"            # meson 1.12.1 + mako, executed read-only
export PYTHONDONTWRITEBYTECODE=1       # do not write .pyc into the M7 venv
export PATH="$VENV/bin:$PATH"

if [ ! -d "$SRC/.git" ]; then
  git clone --quiet --shared --no-checkout "$M7_WORK/deps/mesa" "$SRC"
  git -C "$SRC" checkout --quiet "$PIN"
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$PIN" ] || { echo "Mesa not at pin $PIN" >&2; exit 1; }
[ -z "$(git -C "$SRC" status --porcelain --untracked-files=no)" ] || { echo "Mesa tree modified" >&2; exit 1; }

mkdir -p "$SRC/subprojects/packagecache"
while read -r sum rel; do
  f="$M7_WORK/$rel"; dst="$SRC/subprojects/packagecache/$(basename "$rel")"
  [ -f "$dst" ] || cp "$f" "$dst"
  echo "$sum  $dst" | sha256sum -c --quiet - || { echo "packagecache hash mismatch: $rel" >&2; exit 1; }
done < "$M7_WORK/analysis/mesa-subproject-packagecache.sha256"

if [ ! -f "$BUILD/build.ninja" ]; then
  meson setup "$BUILD" "$SRC" --wrap-mode=nodownload -Dbuildtype=debugoptimized \
    -Dgallium-drivers=softpipe -Dvulkan-drivers= -Dplatforms= -Dglx=disabled \
    -Degl=disabled -Dgbm=disabled -Dopengl=true -Dgles1=disabled -Dgles2=disabled \
    -Dllvm=disabled -Dtools=freedreno -Dzstd=disabled -Dvalgrind=disabled \
    -Dlibunwind=disabled -Dexpat=disabled -Dwerror=false
fi
LIBS=(src/freedreno/ir3/libfreedreno_ir3.a src/freedreno/isa/libir3encode.a
      src/util/libmesa_util.a src/util/libmesa_util_clflush.a src/util/libmesa_util_clflushopt.a
      src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a
      src/compiler/nir/libnir.a src/compiler/libcompiler.a
      src/freedreno/common/libfreedreno_common.a src/freedreno/isa/libir3decode.a
      src/compiler/isaspec/libisaspec.a)
ninja -C "$BUILD" -j"${JOBS:-2}" "${LIBS[@]}" src/freedreno/ir3/ir3_disasm src/freedreno/isa/ir3-disasm
printf '%s\n' "${LIBS[@]}" > "$BUILD/air2ir3-libs.txt"
echo "mesa-build ready: $BUILD (pin $PIN)"
