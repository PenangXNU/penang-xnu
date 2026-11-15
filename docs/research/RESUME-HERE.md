# Research state — start here

**As of 2026-11-15 · research snapshot, not a release or boot authorization.**
**Canonical repository:** PenangXNU/penang-xnu. **Target:** Motorola Moto G53 5G (`penang`, SM4350-AC/holi), XNU 11417.140.69, prospective iPadOS 18.7.8. **Last public release projection:** v0.1.0 / P0.12, deliberately frozen. Do not edit generated `STATUS.md`, the README status block or `manifests/current-status.json` by hand.

## Three independently scoped results

| Scope | Highest verified checkpoint | Boundary |
| --- | --- | --- |
| DEVICE_VERIFIED | M11-D3A1 Stage-0 executed **in RAM** on Moto G53: EL1, 16-KiB-granule CPU support, FDT/header and reservation metadata, visible own-code marker, post-boot partition checks | Not an XNU kernel boot, no PMAP/lockdown/grants certificate |
| QEMU_VERIFIED | T72-Q5: XNU 11417 QEMU_VIRT executed an ARM64 dynamic dyld/main fixture in **EL0**, with real relocation and pointer consumption (138 events; 34/34 independent checks) | Not full launchd/iPadOS; code-signature trust admission remains OPEN; not physical |
| HOST_ONLY | B1.4-H1 memory geometry, B4.2 PMAP contracts, B4.3A-R3 Qualcomm SCM/ABL static ABI research, T6 native diagnostic objects | Mechanisms and dependencies identified, not grants, an authorized SCM service for XNU or a linked PENANG_PHYSICAL kernel |

**G53 identification was restored in U2** (Android ADB, correct `penang` device). The earlier report in which USB enumeration found only `rav` is superseded. Never confuse the two devices.

## What to read, in order

1. [2026-11-15 evidence ledger](2026-11-15-physical-evidence-ledger.md): *which physical hardware work is already done*, original source branches, corrections to old reports, and scope.
2. [T72-Q5 result](2026-11-15-q5-el0.md): latest QEMU userland functional state, with signature/admission limits.
3. [Physical boundary](qualcomm-memory-security.md), [driver dependencies](drivers-and-userland.md) and the previous [2026-11-11 reconciliation](2026-11-11-reconciliation.md): original recovered source and known architecture.
4. [WO-M12-CV1](../work-orders/WO-M12-CV1-EVIDENCE-CONVERGENCE.md): next integration task, deliberately **not** a new broad reverse-engineering survey.

## Unresolved physical gates (all OPEN for XNU-on-device)

FDT_PHYSICAL_HANDOFF; SCM_XNU_ABI; STAGE2_GRANTS; DMA_ENFORCEMENT; PMAP_PHYSICAL; NATIVE_VM_PMAP_COMPILE; NATIVE_XNU_LINK; GIC_TIMER_PHYSICAL; ENTROPY_PHYSICAL; XNU_DEVICE_ENTRY.

**PHYSICAL_BOOT_HOLD:** the physical XNU kernel has not been fully linked or qualified. The Stage-0 RAM experiment is not a substitute. No physical XNU execution, flash, reboot, fastboot boot, monitor call, firmware mutation or device operation is authorized by this document.

## Non-negotiable anti-rework and continuation contract

- Before assigning a worker, search **both** existing Git branches/worktrees and the private evidence inventory, then follow the report's source commit, file hash, test log and subsequent errata. An `OPEN` gate does **not** imply missing research.
- Reuse already imported public M9/M12 code from [2026-11-11 reconciliation](2026-11-11-reconciliation.md); do not reimplement or recopy it. Compare file-level SHA and licenses before adding more files.
- Separate `HOST_ONLY`, `QEMU_VERIFIED`, `DEVICE_VERIFIED`, `INFERRED` and `OPEN` claims. A Git commit or document is traceability, not runtime evidence.
- Never publish firmware, ROM partitions, Apple KDK/IPSW/shared caches, proprietary material, device identifiers, raw physical logs, photos, keys, local absolute paths or private evidence. Public research records name source branch/commit and abstracted artifact/check hashes; originals stay outside this repository.
- Keep original worktrees, uncommitted files and historical results. No reset/clean/force-push/whole-branch merge. New work must state its **smallest missing implementation or evidence**, existing owner, tests, negative cases and rollback.
- After each work order, update a dated research snapshot, provenance and this entrypoint through review. Do not overwrite old reports or retroactively recategorize experiments.

**Current work order:** [WO-M12-CV1 — converge evidence and providers](../work-orders/WO-M12-CV1-EVIDENCE-CONVERGENCE.md). **Portuguese summary:** [Retomada Penang XNU](../pt-BR/RETOMADA-PENANG-XNU.md).
