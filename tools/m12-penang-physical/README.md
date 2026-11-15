# M12 isolated Stage-1 and B4.2 security contract

The six MIT-licensed C source/header/test files here preserve an isolated 16-KiB Stage-1 model and a fail-closed PMAP physical security contract.

- t2_stage1.c / .h and test_t2_stage1.c: one-vCPU managed L3 descriptor model, page permission transitions, TLBI, I-cache/D-cache sequencing and negative tests. A compile-time guard rejects the physical Penang profile.
- penang_pmap_lockdown_contract.c / .h and host_test_pmap_lockdown_contract.c: explicit ownership, grant, W^X, alias and DMA requirements. The production path cannot promote to device without a verified backend.

The original T2 Stage-1 tests ran in isolated QEMU/host scope, **not** inside the complete physical XNU PMAP. No Qualcomm monitor or SMMU backend is provided. Stage-2 grants and PMAP_PHYSICAL remain BLOCKED.

The source blobs are identical to the pinned local M12 integration inputs before M12-T4 and carry their original MIT SPDX. This import does not replace the separate native-XNU build or the corresponding original evidence.

[Technical boundaries](../../docs/research/qualcomm-memory-security.md) · [M12 status](../../docs/research/m12-kernel-and-handoff.md)

## Native input inventory (P1, host only)

`p1_native_inputs.py` inspects the retained generated OSFMK/IOKit Makefiles,
OSFMK configuration, board header and two source trees. It derives missing
source files from disk, rejects noncanonical paths and escaping symlinks, hashes
the inspected bytes, and reuses the original B4.2 assessor and P1 entry checker.
Comments cannot supply missing references or an active physical refusal. The
explicit physical refusal must be a top-level conditional `#error` block.

```bash
python3 -B tools/m12-penang-physical/p1_native_inputs.py \
  --xnu-original-root "$XNU_ORIGINAL" \
  --b42-guarded-root "$XNU_B42_GUARDED" \
  --osfmk-make "$OSFMK_MAKEFILE" --iokit-make "$IOKIT_MAKEFILE" \
  --osfmk-config "$OSFMK_CONFIG" \
  --board-header tools/m12-penang-physical/PENANG_PHYSICAL.h \
  --out .build/p1-native-inputs.json
```

Receipt creation is exclusive. Exit 2 means missing/invalid prerequisites;
exit 1 means supplied inputs were rejected; exit 0 means only
`INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD`. All outcomes preserve
`PMAP_PHYSICAL=BLOCKED`, compile/link/device/userland execution `NOT_RUN`.

Literal canonical `$(SOURCE_DIR)/…` sources are inventoried separately from
the exact generated C/S/P rule templates and genassym prerequisite. Rule A/B
families and canonical directories must match; unknown macro syntax, extra
recipe text, empty references, traversal and backslash escapes are rejected.
Make line continuations are normalized before classification. Other Make
variables, conditionals and computed source lists are not evaluated.
This is a **lexical reference inventory**, not Make evaluation or a compiler
flag interpreter. It never executes Makefiles, patches sources or synthesizes
missing configuration. Hashes record inspected bytes, not independently admitted
provenance. Source counts are not proof of a complete build or link closure.
Selector overrides with assigned values are conservatively rejected; supported
inputs retain bare `CONFIG_PMAP_PPL` and `CONFIG_KERNEL_INTEGRITY` options.

The cloud check against public XNU 11417.140.69 commit
`43a90889846e00bfb5cf1d255cdc0a701a1e05a4` inventoried original `start.s` and
`arm_init.c`, then stopped at missing retained generated OSFMK/IOKit Makefiles,
OSFMK configuration and the B4.2 guarded tree. These are existing research
inputs to recover, not permission to reconstruct a weaker physical profile.
No configured native compilation was attempted. Tests use synthetic sources;
they do not establish that unavailable historical recipes pass this parser.

The subsequent owner-supplied recipe/overlay replay passed the input audit:
303 OSFMK and 83 IOKit literal source references; 24/13 distinct rule templates
and one separate OSFMK genassym dependency. The original multiline refusal
banner is admitted without changing its bytes. C line splicing is normalized
before checking conditional depth, so an inactive outer `#if` cannot disguise
the refusal. This remains `INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD`.

## Fresh native object compilation

`p1_cache_geometry.py` emits an exclusive patch for the pinned public
`osfmk/arm64/proc_reg.h`. Its only source delta is a physical cache-line
selector with `MMU_I_CLINE=MMU_CLINE=6`, using the D3A1 64-byte observation
already recorded in the [physical ledger](../../docs/research/2026-11-15-physical-evidence-ledger.md).
The unknown-processor refusal and other targets remain unchanged. The tool
does not apply its patch, assert monitor support or edit VM/PMAP guards.

```bash
python3 -B tools/m12-penang-physical/p1_cache_geometry.py \
  --xnu-original-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/cache.patch"
git -C "$XNU_B42_GUARDED" apply --check "$PRIVATE_OUTPUT/cache.patch"
git -C "$XNU_B42_GUARDED" apply "$PRIVATE_OUTPUT/cache.patch"
python3 -B tools/m12-penang-physical/p1_compile_probe.py \
  --source-root "$XNU_B42_GUARDED" \
  --osfmk-make "$OSFMK_MAKEFILE" --iokit-make "$IOKIT_MAKEFILE" \
  --board-header tools/m12-penang-physical/PENANG_PHYSICAL.h \
  --generated-include "$GENERATED_HEADERS" --generated-include "$EXPORTED_HEADERS" \
  --source osfmk/arm/machine_cpuid.c --source osfmk/arm/arm_init.c \
  --source osfmk/arm64/arm_vm_init.c --out "$PRIVATE_OUTPUT/fresh-probe"
```

Use a fresh private source copy with the retained overlay. Never apply these
steps to the original upstream checkout or packet. Generate configuration
headers through upstream `SETUP/config` and `assym.s` through `arm64/genassym.c`
and the original sed rule; do not fabricate headers. Generated include roots
may contain `osfmk`/`iokit` children; exported include directories are explicit.

The compile probe preserves recipe `CONFIG_DEFINES`, adds explicit MacOSX ARM64
kernel flags from the pinned public `MakeInc.def`, separates component C/C++
flags, and never executes the Makefiles. Compiler, configuration, source,
feature-header, command and log identities are recorded in the private output.
Reviewed board bytes are snapshotted, argument-injection environment variables
are removed, and actual preprocessor macros must preserve the physical target,
16-KiB selector, PPL and integrity. Source or forced-header drift rejects results.

Exit 0 means only selected nonempty ARM64 `MH_OBJECT` files with external
definitions, under `SELECTED_OBJECTS_ONLY_B42_COMPILE_HOLD`; exit 2 means
`NATIVE_BUILD_BLOCKED`; exit 1 means input/tool/output rejection. Output must
be new and outside the source tree/public checkout. A zero compiler exit for
a deliberately empty unit is not counted as a provider; a VM/PMAP unit that
unexpectedly compiles is rejected. No kernel link or runtime is attempted.

The fresh LLVM 19 run compiled 22 nonempty objects, including original XNU
`arm_init.c`, `machine_cpuid.c` and `locore.s`, plus the retained platform
contracts. Three other selected units compiled without external definitions;
five failed compilation. Remaining frontiers include the intentional B4.2
refusal in `arm_vm_init.c`, absent `arm64/tunables/tunables.s` for `start.s`,
unproduced MIG headers (`mach/vm_map.h`, `mach/clock.h`), and a SHA assembly
dialect error. This is a fresh diagnostic subset, **not an exact T6 replay**:
the supplied historical scripts refer to additional T1/T4 tools/recipes and
toolchain inputs not present in the packet. No full-link candidate was produced.
