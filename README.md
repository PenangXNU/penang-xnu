# Penang XNU

Research toward running Apple's public XNU kernel, and eventually an unmodified iPadOS userland supplied by the user, on non-Apple ARM64 hardware. The first device is the Motorola Moto G53 5G (`penang`, Qualcomm SM4350/holi, Adreno 619); QEMU `virt` is the bring-up and regression environment.

The design follows the OpenCore idea ([ADR-0007](docs/adr/ADR-0007-original-experience.md)): Apple binaries stay byte-for-byte original and every adaptation lives below the operating system, in the boot loader, platform contracts, XNU platform drivers and documented XNU patches. **Nothing from Apple is in this repository**: you bring your own IPSW.

> Status: early research. This is not a bootable iPadOS, ROM or installer, and it is not affiliated with or endorsed by Apple, Qualcomm or Motorola.

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

## Research progress since v0.1.0 (2026-11-04)

This is a **post-release documentary update**, not a modification of the frozen v0.1.0 evidence projection above. The following results are reported by isolated, locally committed research worktrees; their private code, raw traces and proprietary inputs are not distributed here.

| Research milestone | Scoped result |
|---|---|
| P0.13 — pthread provider (`90a3f09`) | Research pthread admission and original BSD bootstrap path progressed in single-CPU QEMU. |
| P0.14 — AES-CBC (`f72dcfc`) | Real AES-CBC provider exercised in host/QEMU; original `tcp_init` returned. |
| P0.15 — CCRNG (`8a75e40`) | `CCRNG_RUNTIME_VERIFIED`: existing HMAC_DRBG serviced two original NECP requests of 32 bytes; `necp_init` returned in QEMU ARM64/TCG. G0–G12 passed within this research scope. |
| M10.2 — hardware preflight (`743a977`) | Host suite passed twice; 21/21 M10 tests and 23/23 QEMU Stage-0 scenarios passed; **physical execution remains on HOLD**. |

**Next blocker (QEMU):** `B10_BSD_ROOT_MEDIA_WAIT`. `IOFindBSDRoot` was observed waiting for `IOMedia / Apple_HFS` in a 120.1-second run. A 56.93-second observation interval does **not** prove a permanent deadlock. The original P0.10 panic-oriented checker remains FAIL on its expected-panic criterion; a separate P0.15 progression checker passed. `B6_IOPMGR` remains preserved but unimplemented.

**Device boundary:** the Motorola Moto G53 has not run our Stage-0 or XNU. The framebuffer ownership/scanout, independent recovery after a hang, and Motorola ABL acceptance of the minimal v3 image are unqualified. There is no demonstrated root filesystem, userland, SMP, Apple-equivalent security, SpringBoard or Metal on the target.

These observations are **not v0.1.0 release gates** and do not authorize physical boot, flashing, or any proprietary redistribution. The complete P0.15 and M10.2 evidence stays local pending separate v0.2 content, licensing and security review.

## What this repository contains (v0.1)

- Platform contracts: C ABI v1 for CPU/MMU/granule and RAM/reservation checks, with QEMU_VIRT, Qualcomm holi (Penang) and trinket (Sofiar) descriptors.
- A Rust/C validator with a Python frontend and JSON schemas; fail-closed negative tests (unknown capabilities stay unknown; synthetic observations never qualify hardware).
- A pre-MMU ARM64 assembly guard (cross-compiled only).
- The project's decision records, evidence policy and a status that is generated from pinned evidence.

The kernel-side research code (security providers, Platform Expert, XNU patches, QEMU harness) is planned for v0.2 after its own license and content audit.

## Run the public tests

Tested on Ubuntu 24.04 (also under WSL), x86_64, Python 3.12, Rust/Cargo 1.99.1 and clang 18. The same suite runs in GitHub Actions.

    python3 -m venv .venv
    . .venv/bin/activate
    python3 -m pip install -r requirements.txt
    cargo fetch --locked --manifest-path tools/penang-validate/Cargo.toml
    python3 -B scripts/validate_public.py

The suite needs no ROM, firmware, IPSW, KDK, XNU checkout, phone or private evidence. Example:

    python3 -B tools/penang-validate/validate.py --platform qemu-virt --observation examples/qemu-host-synthetic.json --contract-only

Expected: PASS in HOST_CONTRACT scope, runtime_enabled=false, runtime_status=NOT_RUN. Without the example observation all three platform profiles intentionally return BLOCKED (exit 2).

## Contributing

Most useful work needs neither a phone nor Apple files. Start with [CONTRIBUTING.md](CONTRIBUTING.md), the [initial issues](docs/INITIAL-ISSUES.md) and the [roadmap](ROADMAP.md). AI coding agents should read [AGENTS.md](AGENTS.md) first. Commits need a DCO sign-off (`git commit -s`).

Security reports: see [SECURITY.md](SECURITY.md). Resumo em português: [docs/pt-BR/README.md](docs/pt-BR/README.md).

## License

Project-owned code and documentation: [Apache-2.0](LICENSE). Modifications of Apple's XNU sources (from v0.2) remain under APSL-2.0, as that license requires. Third-party notices: [NOTICE](NOTICE), [docs/LICENSING.md](docs/LICENSING.md) and [audit/dependencies.json](audit/dependencies.json). Apple, iPadOS and other names identify research targets only; their software is not included.
