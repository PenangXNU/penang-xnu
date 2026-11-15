# Penang XNU

**Experimental XNU bring-up for ARM64 platforms.** The first hardware target is the Motorola Moto G53 5G (Qualcomm SM4350-AC/holi, Adreno 619); QEMU `virt` is the reproducible kernel development environment.

The project studies a platform adaptation below the operating system — boot handoff, CPU/MMU contracts, platform drivers and documented XNU patches — while preserving Apple-supplied userland binaries unchanged. See [ADR-0007](docs/adr/ADR-0007-original-experience.md). An unmodified, user-supplied iPadOS userland is a **future target**, not a current capability. No Apple firmware, IPSW, kernel binary or proprietary ROM is distributed here.

> **Research only.** This repository is not a bootable iPadOS build, ROM, installer or physical-device flashing package. It is independent of Apple, Qualcomm and Motorola.

## Research status — 15 November 2026

These are independently scoped engineering results, **not** the v0.1.0 release status:

| Track | Demonstrated | Remaining boundary |
| --- | --- | --- |
| XNU / QEMU | **QEMU_VERIFIED:** T72-Q5 executed a project-built dynamic Mach-O dyld/main fixture in ARM64 EL0, performed relocation and consumed the relocated pointer (138 trace events; 34 independent audit checks). | Not Apple's dyld/launchd, full BSD services or complete iPadOS. Code-signature admission equivalence remains **OPEN**. |
| BSD userland | **HOST_ONLY:** T73 `getpid` fixture assembled and linked for ARM64 with source-checked XNU syscall ABI, negative/layout tests and CI. | `getpid` has **not** been executed by XNU in QEMU. |
| Moto G53 | **DEVICE_VERIFIED:** M11-D3A1 ran the project's temporary Stage-0 in RAM and observed EL1/FDT/16-KiB-granule support. | Stage-0 is **not XNU**; XNU has not booted on the handset. |
| Physical memory protections | **HOST_ONLY:** B4.2 PMAP contracts, error paths, source-selection guards and negative tests are implemented. Stock SCM/QHEE and FDT geometry were studied. | Post-handoff NS-EL1 SCM authorization, Stage-2/XPU grants, DMA/SMMU isolation, a qualified PMAP backend and complete physical XNU link remain **OPEN**. |

Technical evidence: [research entrypoint](docs/research/RESUME-HERE.md), [Q5 EL0](docs/research/2026-11-15-q5-el0.md), [physical evidence ledger](docs/research/2026-11-15-physical-evidence-ledger.md). A historical research result must not be promoted from HOST_ONLY or QEMU_VERIFIED to DEVICE_VERIFIED without a corresponding device test.

## Repository contents

- **Platform contracts:** CPU/MMU and memory-reservation C ABI, descriptors for `qemu-virt` and Qualcomm platforms, a fail-closed Rust/C validator with Python frontend.
- **Native research primitives:** selected original M12 boot/memory/PMAP contract and guard sources in [the M12 PMAP contract](tools/m12-penang-physical/penang_pmap_lockdown_contract.c). They do not constitute a physical Qualcomm protection provider.
- **BSD fixture:** [the T73 fixture](tools/t73-bsd-userland/fixture_getpid.S) contains source and host-only linker layout for a subsequent syscall experiment; it is not a bootable Mach-O distribution.
- **Graphics research:** [M9 AIR-to-IR3 translation](tools/air2ir3/README.md), with host-only compilation and validation.
- **Evidence and decisions:** curated, publishable results in [docs/research](docs/research/README.md), architecture in [ARCHITECTURE.md](ARCHITECTURE.md), public inputs and provenance under `audit/` and `evidence/`.

Proprietary firmware, raw device traces, Apple software, private worktrees and temporary work orders remain **outside this repository**. The work is tracked through [GitHub Issues](https://github.com/PenangXNU/penang-xnu/issues) and reviewed PRs.

## Run the public checks

On Ubuntu 24.04 (including WSL) with Python 3.12, Rust 1.99.1, Clang 18 and standard Git tools:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r requirements.txt
cargo fetch --locked --manifest-path tools/penang-validate/Cargo.toml
python3 -B scripts/validate_public.py
```

The suite covers public contract, build, synthetic, adversarial and content-policy checks. **It does not boot XNU**, run QEMU or contact a physical phone. See [validation scope](docs/VALIDATION.md).

A synthetic platform example:

```bash
python3 -B tools/penang-validate/validate.py --platform qemu-virt --observation examples/qemu-host-synthetic.json --contract-only
```

Expected classification is `HOST_CONTRACT`, not physical readiness. Unknown capabilities remain blocked.

## Next engineering steps

Qualify the **first real BSD syscall return** in a separate QEMU experiment with an independently admitted Mach-O/ROMFS fixture, and converge existing Qualcomm/PMAP evidence into a legitimate native build. Neither result authorizes a physical XNU boot. Current tracked work: [physical integration](https://github.com/PenangXNU/penang-xnu/issues/4) and [source recovery](https://github.com/PenangXNU/penang-xnu/issues/5).

## Contributing, security and licensing

Begin with [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md) for reproducibility, provenance and device-safety constraints. Contributions require DCO sign-off (`git commit -s`). Security disclosure: [SECURITY.md](SECURITY.md). [Resumo em português](docs/pt-BR/README.md).

Project-owned code and documentation: [Apache-2.0](LICENSE). Apple XNU-derived modifications, if published, must retain applicable APSL-2.0 obligations. See [NOTICE](NOTICE), [licensing](docs/LICENSING.md) and [dependency inventory](audit/dependencies.json).

---

## v0.1.0 release snapshot — 2 November 2026

The following block is **generated from the frozen release inputs**, not the current research state. It is retained for reproducibility; later QEMU and hardware findings are summarized above.

<!-- CURRENT_STATUS:BEGIN -->
## Current state — v0.1.0

| Track | Evidence and limit |
|---|---|
| Kernel (QEMU_VIRT, research) | XNU 11417.140.69 DEV: B3 research trust, B4 IOKit Platform Expert and P0.12 GICv3/SGI1 path verified in QEMU TCG with one CPU. The kernel entered bsd_init but did not finish. |
| Next blockers | B6_IOPMGR (CPU bootstrap) and B7_PTHREAD_PROVIDER (pthread_init guard), independent; no userland. |
| Apple compatibility | Not APPLE_IPADOS_SECURITY_COMPATIBLE: own research trust root, no Apple entitlements, SEP or userland yet. |
| Moto G53 (penang) | Bootloader accepts RAM boot (OBS-D1, Android image). XNU on the device: NOT_RUN. |
| D1–D4 (ADR-0006) | NOT_DEMONSTRATED. M9 graphics compiler is host only; Sofiar remains P1/UNKNOWN. |

Details: [STATUS.md](STATUS.md), [P0.12](docs/P0.12-STATUS.md), [P0.11](docs/P0.11-STATUS.md), [device observability](docs/DEVICE-OBSERVABILITY.md), [P0.10](docs/P0.10-STATUS.md).
These are documentary references to pinned research records (P0.12 f889c03, P0.11 bed47e6, OBS-D1 6838eaa), not runtime observations made by this release.

Target: iPadOS 18.7.8 / 22H352 / iPad7,11 / ARM64_ALL (bring your own IPSW); XNU 11417.140.69 at 43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
Penang: SM4350-AC/holi, Adreno 619. Accepted [ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md), [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md) and [ADR-0007](docs/adr/ADR-0007-original-experience.md).
<!-- CURRENT_STATUS:END -->
