# Research reconciliation — 2026-11-11

**Disposition:** source recovery on an isolated public review branch, NOT a new release, a device boot qualification or an independent security review.

## Canonical lines of work

| Track | Original local line | Highest directly reviewed checkpoint | Provenance |
| --- | --- | --- | --- |
| Public v0.1 | PenangXNU/penang-xnu main | P0.12 documentary status | PUBLIC_SOURCE |
| AIR2IR3 M9 | research/m9-air-nir-frontend, commit 2c21032a0f61867880dff055a884a2e492c7f130 | A–D host frontend, NIR and ir3 checks | HOST_ONLY, private-corpus evidence referenced but not bundled |
| QEMU kernel P0.16 | integration/p0-16-bsd-root-media-20261104, commit aad24381a428bae74db0e4a1ef6675a3d181d1ae | Original BSD root vnode installed; launchd exec ENOENT | QEMU_VERIFIED |
| Device M11-D3A1 | commit a4c8d0ae8457fb572593eba73dd0f73680fb5714 | Stage-0 EL1, TGran16, FDT metadata and Android return | DEVICE_VERIFIED, Stage-0 only |
| M12 integration | integration/m12-convergence-c0-c1-20261110, checked HEAD 59726684744df751496979ba72907eb69ceb0715 | T0 through T3 handoffs, 23-gate canonical verifier | HOST/QEMU, physical XNU NOT_RUN |
| PMAP B4.2 | research/m12-b42-pmap-lockdown-argon-20261107, commit 29e6aca5dc8e5e057692c45adab1b6fd048b8b19 | Fail-closed physical security contract | HOST_ONLY; PMAP_PHYSICAL BLOCKED |
| Qualcomm R3 | research/m12-b43a-r3-codex-20261110, commit 8ac4cdab7dc3b83a0e4330ec6021670d4ad79257 | Static ABL post-EBS transport analysis | HOST_ONLY; no NS-EL1 service authorization proven |

Other preserved Windows research trees cover M10 hardware preflight, B1 memory, B3 entropy, R1/R2/R3 firmware ABI, C0/C1 diagnostics and earlier P0.5–P0.15. WSL scratch under rom-work/penang contains separate builds, compiler/toolchain outputs, QEMU traces and the M4–M9 GPU research corpus. These are **evidence or build inputs, not automatically a publishable source tree**.

## Recovered into this GitHub branch

1. Forty-one Git-tracked source, build and synthetic-test files from the original M9 AIR2IR3 implementation. Every imported Git blob ID was compared to the local M9 Git object; the source files were not rewritten.
2. A per-file SHA-256 and license register in audit/public-files.json and audit/file-licenses.json.
3. Public-safe descriptions of P0.16, M11-D3A1 and M12-T0–T3, including the reasons physical XNU is not ready.
4. Nine byte-identical MIT source/header/test files from the M12 geometry bridge, isolated Stage-1 model and fail-closed PMAP contract, with public scope READMEs. They are **not** the complete physical loader, PMAP backend or T3 device candidate.

The original M9 history was intentionally not imported because it was interleaved with local research operations and private artifacts. The current public files are copies of the pinned originals, not claims of a clean-room reproduction of Apple's implementation.

## Explicitly not imported

- Apple metallibs, IPSWs, kernelcache, dyld cache, KDK, firmware and vendor images.
- Qualcomm/Motorola boot partitions, DTBs, extracted privileged images or raw reverse-engineering dumps.
- Raw photographs, serial/device identifiers, partition dumps, private logs, secrets, personal paths and toolchain artifacts.
- Native XNU patches or private SDK integrations that have not passed source/license and device-security admission.
- M12-T4 in-progress files, to avoid racing the active Codex worktree.

## Source candidates requiring separate publication admission

- Remaining bridge entry/loader/ADT and T3 instrumentation modules (a selected M12 memory/Stage-1/PMAP source subset is now published and should not be rebuilt).
- The P0.16 research ROMFS, loader integration and XNU modifications (APSL/third-party license review required).
- M4–M8 graphics research tools, separating original code from Mesa/LLVM/vendor inputs.

These are candidates, NOT items declared absent or tasks to implement from scratch. Review original code and hashes before either importing or commissioning new development.

## Anti-rework policy for new work orders

- First inspect the canonical local CURRENT_GATES.json and its verifier, then source reports and SHA-256 manifests; do not treat an agent summary as a new gate.
- Distinguish HOST_ONLY, QEMU_VERIFIED and DEVICE_VERIFIED at every claim.
- Search all existing worktrees and rom-work before allocating a new implementation task.
- Promote only a small, reviewed source delta from the appropriate original branch; do not merge experimental histories wholesale.
- Preserve original evidence, worktrees and source SHA-256; no reset/clean/force-push.
- Physical flash, boot, reboot, SMC/HVC or MMIO work requires a separately scoped authorization and exact-image preflight.
- The public main branch continues on its v0.1 release status until an independent review and explicit merge decision; this branch records research, not a release.

See [M12 status](m12-kernel-and-handoff.md), [M9](m9-air2ir3.md), [Windows/WSL source inventory](local-worktree-inventory.md) and [driver/userland gates](drivers-and-userland.md).

**Post-T3 update:** the local M12-T4 branch advanced to `bf169b42` with `T4_PHYSICAL_PREFLIGHT_HOLD`; it recorded 20 host and 10 QEMU cases, but no physically approved read envelope and no Android boot image. The historical T3 import and the T4 source state are deliberately kept separate.
