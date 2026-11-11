# Local source and evidence inventory — 2026-11-11

**Scope:** read-only inspection of the engineering trees on Windows and WSL Ubuntu 24.04, followed by a curated GitHub-source reconciliation. This inventory describes identified sources, not evidence that every directory was individually retested or that all material is distributable. No original directory or Git branch has been deleted.

## Canonical checkpoints and how to reuse them

| Workstream | Original source lineage | Current checkpoint | Reuse before authoring |
| --- | --- | --- | --- |
| M0–M3 BSP, FDT, UEFI and synthetic QEMU | WSL `penang-xnu-lab`, `penang-xnu-integration`, `penang-xnu-m3`, `penang-xnu-m3-m9` | Historical host-only BSP and synthetic entry/PSCI tests | Inspect `tools/penang-fdt-audit`, `tools/uefi-cpu-probe`, `tools/m3-qemu-entry`, `tools/m3-qemu-smp`; do not call them a real XNU boot |
| M4–M8 Adreno/Metal | WSL `rom-work/penang/m4-*` through `m8-*`, `apple-gpu-study` | Mesa ir3/Adreno research, source pins and test notes | Compare original prototypes against M9 AIR2IR3 before commissioning a new compiler; do not vendor Apple input or third-party code |
| M9 AIR2IR3 | WSL `research/m9-air-nir-frontend`, commit `2c21032` | A–D host work (original 41 files published) | Extend `tools/air2ir3`; no second frontend/parser |
| P0.5–P0.12 | Windows integration branches and historical WSL `p05-*` through `p12-*` | QEMU kernel entry, cryptography, AMFI research and GIC | Use pinned handoffs; do not rerun earlier builds merely to rediscover closed gates |
| P0.13–P0.16 | Windows `integration/p0-13-*` through `integration/p0-16-*`; WSL `p13-*` through `p16-*` | `90a3f09`, `f72dcfc`, `8a75e40`, `aad2438` | Preserve original QEMU runtime and ROMFS proofs; XNU-dependent source needs separate APSL/licensing admission |
| M10 and M11 D3A1 | Windows M10 preflight and M11 device evidence directories | Physically observed Stage-0, **not** XNU | Do not repeat the historical device trial or infer runtime approval for a different image |
| M12 B1/B3/B4 and R1–R3 | Windows isolated research branches and manifests | Memory, entropy, PMAP and Qualcomm firmware ABI contracts | Reuse pinned contracts, not raw firmware or speculative SCM access |
| M12 T0–T3 | Windows `integration/m12-convergence-c0-c1-20261110`, commit `5972668` | Host/QEMU memory bridge and Stage-1; 23-gate verification | Reuse selected published C contracts, original T3 evidence and gates |
| M12 T4 | Same integration branch, commit `bf169b4` | `T4_PHYSICAL_PREFLIGHT_HOLD`; 20 host and 10 QEMU cases | Preserve new read-bound checks and native inventory; do not build a device image without an entry-memory contract |

The canonical local M12 gate file is `CURRENT_GATES.json` with `tools/m12-integration/check_current_gates.py`. At the T3 checkpoint, 23 gates and 18 source records passed **ledger integrity checks**, not 23 physical readiness gates. Later changes must be evaluated against the active HEAD and its evidence manifests.

## Layout surveyed

- Windows Git reported **24 linked worktrees**, including multiple P0 integrations, M10, the M12 integration and independent B1/B3/B4/R1–R3 investigations. They are deliberate isolated research branches, not 24 independently bootable kernels.
- Windows also contains separate dated evidence roots for D3A1, M12 B1/B4, R1–R3, T0–T4 and earlier P0 checkpoints. These roots contain historical receipts and are **not** import candidates as directories.
- WSL contains separate `penang-xnu-build`, `penang-xnu-delegations`, `penang-xnu-integration`, `penang-xnu-lab`, `penang-xnu-m3`, `penang-xnu-m3-m9` and `penang-xnu-workers` checkouts, in addition to the build variant `penang-xnu-build-p01d`.
- `rom-work/penang` contains at least 80 named experiment/build directories, image parts, dependency trees and historical test results. The largest inspected entries included `p06-20261031` (~11.0 GB), `p01e-source-*` (~7.1 GB), `apple-gpu-study` (~5.5 GB), `super.raw` (~4.8 GB), `chunks` (~4.8 GB), `p16-20261104` (~3.0 GB), and `m9-air-nir-frontend-*` (~1.0 GB). These are approximate decimal byte counts from a top-level disk inventory, **not a duplicate or deletion analysis**.
- The WSL M9 research tree stores private Apple metallibs and a pinned Mesa build in `rom-work`; **neither belongs in public Git history**. Its 41 original project-source files were recovered from the Git-tracked `tools/air2ir3` subtree rather than copied from outputs.

## Published versus not published

**Published on the isolated reconciliation branch:** 41 original M9 AIR2IR3 source/test files; nine selected M12 geometry bridge, isolated Stage-1 and fail-closed PMAP source/test files; research status documents; per-file SHA-256 and license receipts. The material is explicitly HOST_ONLY or QEMU_VERIFIED unless a narrower device Stage-0 fact is stated.

**Already published in v0.1:** the selected platform contract, BSP validator, build scripts, public tests and project governance. Do not duplicate those into a second `platform` package.

**Requires separate admission:** M3 synthetic entry/UEFI tools, M4–M8 graphics prototypes, P0.9–P0.16 XNU/kernel-dependent patches, later M12 bridge/ADT/loader code and T4 changes. Their existence is established; their public redistributability and clean standalone tests have not been comprehensively reviewed. Where source headers are unlicensed or link to third-party components, do not infer MIT or Apache-2.0 permission from project proximity.

**Must stay out of public Git:** Apple binaries/inputs, KDK, vendor ROM and firmware, kernelcache, proprietary GPU binaries, private machine paths, device identifiers, boot-partition data, raw traces, agent sessions, secret material, large build outputs and unpacked third-party dependencies.

## Anti-duplication procedure

1. Search `CURRENT_GATES.json`, the latest M12 handoff and source-specific reports; locate the original Git commit and SHA-256 evidence before reopening a gate.
2. Locate code in a source worktree, not a similarly named `rom-work` compiled output. Check whether its exact Git blob already exists in the public branch.
3. Distinguish a full implementation, a host test, a QEMU-only adapter, a physical Stage-0 observation and a purely static reverse-engineering claim.
4. Import only a reviewed file-level delta, preserving authorship, license and digest; record exclusions and externally provided dependencies. Do not merge unpublished historical object databases wholesale.
5. Require a fresh native integration test before claiming new boot progress. Never promote `PMAP_PHYSICAL` or `XNU_DEVICE_ENTRY` from unrelated host/QEMU measurements.
6. Do not delete or reorganize Windows worktrees or `rom-work` merely because a path resembles a duplicate. Storage cleanup requires separate byte/hardlink/inode and provenance checks.

This register is deliberately a **location map**, not a disposal manifest or an instruction to repeat completed work.
