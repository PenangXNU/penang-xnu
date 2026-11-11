# M12 isolated Stage-1 and B4.2 security contract

The six MIT-licensed C source/header/test files here preserve an isolated 16-KiB Stage-1 model and a fail-closed PMAP physical security contract.

- t2_stage1.c / .h and test_t2_stage1.c: one-vCPU managed L3 descriptor model, page permission transitions, TLBI, I-cache/D-cache sequencing and negative tests. A compile-time guard rejects the physical Penang profile.
- penang_pmap_lockdown_contract.c / .h and host_test_pmap_lockdown_contract.c: explicit ownership, grant, W^X, alias and DMA requirements. The production path cannot promote to device without a verified backend.

The original T2 Stage-1 tests ran in isolated QEMU/host scope, **not** inside the complete physical XNU PMAP. No Qualcomm monitor or SMMU backend is provided. Stage-2 grants and PMAP_PHYSICAL remain BLOCKED.

The source blobs are identical to the pinned local M12 integration inputs before M12-T4 and carry their original MIT SPDX. This import does not replace the separate native-XNU build or the corresponding original evidence.

[Technical boundaries](../../docs/research/qualcomm-memory-security.md) · [M12 status](../../docs/research/m12-kernel-and-handoff.md)
