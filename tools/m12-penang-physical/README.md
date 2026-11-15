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

Only literal canonical `$(SOURCE_DIR)/…` references are inventoried; other Make
variables, conditionals and computed source lists are outside this inventory.
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
