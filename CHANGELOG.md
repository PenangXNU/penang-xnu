# Changelog

This file tracks reviewed **public source and technical-context changes**. It is not a substitute for the generated v0.1.0 release [STATUS.md](STATUS.md), original test receipts, Git history or GitHub PR review. Research evidence is classified by execution environment.

## Unreleased — native bootstrap research (2026-11-15)

### Documentation

- Added a public-safe [native XNU bootstrap continuity record](docs/research/2026-11-15-native-bootstrap-cloud-continuity.md) to make current source lineage, build dependencies and the distinction between kernel entry and `/sbin/penang-init` discoverable to GitHub coding agents.
- Linked the continuity record from the research entrypoint and the AI contributor instructions. Private hardware evidence, local operating handoffs and vendor images remain external to the public repository.

### Existing reviewed lineage (not introduced by this documentation change)

- [PR #11](https://github.com/PenangXNU/penang-xnu/pull/11) merged: fail-closed, host-only Qualcomm SCM memory-ownership **candidate** descriptor and adversarial tests. It does not make a physical SCM provider or pass the B4.2 PMAP lockdown gate.
- [PR #12](https://github.com/PenangXNU/penang-xnu/pull/12) was open when this documentation was authored: a source-only check of `_start` → `start_first_cpu` → `arm_init` → `arm_vm_init`, with the original B4.2 guards required. The check does not compile or link a complete physical kernel.
- The historical M11-D3A1 Stage-0 proof on the Moto G53 remains **DEVICE_VERIFIED for owner-written Stage-0 only**; T72-Q5 is **QEMU_VERIFIED for the project's dynamic EL0 fixture**. Neither demonstrates physical XNU or `/sbin/penang-init` on the phone.

## v0.1.0 — frozen public research projection (2026-11-02)

The original v0.1.0 documentary and validator release remains represented by its generated status and release metadata. This changelog does **not** rewrite those generated records or retroactively promote subsequent research to the v0.1.0 release.

See [Research: resume here](docs/research/RESUME-HERE.md), [Evidence admission](docs/EVIDENCE-ADMISSION.md) and [Roadmap](ROADMAP.md) for scope and current limitations.
