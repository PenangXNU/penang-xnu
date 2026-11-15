# WO-M12-CV1 — converge existing evidence and native providers

**Track:** PENANG_PHYSICAL · **Date:** 2026-11-15 · **Authority:** planned work order, not authority to boot, flash or modify devices.

## Mission

Stop rediscovering known hardware. Reconcile M11-R2C/R3/D3A1, B1.4-H1, B4.2, B4.3A-R3, T6, U2/P2 and T72-Q5 with the existing M9/M12 public source already imported in the [2026-11-11 branch](../research/2026-11-11-reconciliation.md). Produce one traceable map of the **smallest missing code and evidence** needed for a legitimate native XNU build. Preserve security guards and historical test truth.

## Required inputs and baselines

1. [Research entrypoint](../research/RESUME-HERE.md), [physical evidence ledger](../research/2026-11-15-physical-evidence-ledger.md), [Q5](../research/2026-11-15-q5-el0.md).
2. Published public source in `reconcile/m9-air2ir3-m12-20261111` (import base `67345cedf843c94b7b1fe38384a1036c747c2988`); do **not** re-import the existing M9 files or selected M12 bridge/PMAP contract.
3. Original **local** code/evidence in independent research branches: `research/m12-b14-h1-gpt6-20261110` (B1.4-H1), `research/m12-b42-pmap-lockdown-argon-20261107` (B4.2), `research/m12-b43a-r3-codex-20261110` (R3), T6 diagnostic source and U2/P2 audit.
4. Source contracts: `B42_TO_B1_CONTRACT.json` R01–R06; current physical gates; original `pmap.c` and `arm_vm_init.c` compiler receipts; original upstream XNU target pin and release profile. Local research manifests are retained off GitHub.

## Outputs (one reviewable source delta)

- **Dependency ledger:** each gate mapped to source branch, commit, artifact SHA, owning TU/provider, existing implementation, effective authorization evidence, missing ABI or code, negative test and type of next validation.
- **Source-resolution table:** the T6 18-object inventory and its 764 unresolved imports must be checked against *all* known provider branches and build selections. Report only symbols truly absent from the **selected intended target**; do not equate import counts with number of missing functions.
- **Native path plan:** explicit order for verified memory ownership and DMA protections → physical PMAP backend → native compilation/link → GIC/timer/entropy → independently reproducible physical image → observability/recovery check. Preserve correct error handling and fail-closed guards; do not implement placeholder success.
- **Publication migration plan:** separate original project-owned or reviewable MIT code, APSL XNU modifications and prohibited firmware/Apple/private evidence. For public code imports use file-level provenance, license, content SHA-256, synthetic tests and explicit review.
- **Independent handoff:** state the one next **integrable** technical unit. If any proof requires a physical test, define its exact question and preflight, without executing it in this work order.

## Gates and exit conditions

- `CV1_EXISTING_WORK_REUSED`: published/imported files enumerated; no duplicate implementation.
- `CV1_ORIGINALS_PRESERVED`: original worktrees, dirty/untracked files and external evidence not touched.
- `CV1_PROVENANCE_RECONCILED`: no source claim without matching local bytes, branch/commit, SHA and dated test receipt.
- `CV1_SECURITY_GUARDS_PRESERVED`: B4.2 guards, AMFI/TrustCache policy and PMAP constraints unchanged.
- `CV1_BUILD_DELTA_IDENTIFIED`: one small actionable native code unit, plus explicit physical-only evidence blockers.
- `CV1_DEVICE_ACTIONS=0` and `PHYSICAL_BOOT_HOLD`: until a separate exact-image approval based on independently qualified native build and recovery.

**Do not** run QEMU again for an already verified Q5, run a new general bootchain/DTB survey, fetch privileged firmware anew, invoke speculative SCM calls, use NoMonitor/QEMU backend for the phone, flash, boot, reboot, change slots, force-push or merge unrelated histories. CI/release status may be proposed only after publication review. A future physical trial must have target identity, exact image+SHA, known effects, verified recovery/observability, independent review, one-shot command and owner approval.

**Success criterion:** a reviewer can continue from the published repo and local evidence **without repeating the investigation**. A green host checklist alone is not physical approval.
