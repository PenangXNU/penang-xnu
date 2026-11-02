# Penang XNU — public preview RC2

An experimental, community-facing ARM64 platform-contract and validation project. This source preview contains working C/Rust/Python code, synthetic tests and the accepted documentary foundation. It is not a bootable iPadOS distribution.

## What works here

- C ABI v1 for CPU/MMU/granule and RAM/reservation checks.
- Rust/C validator with a Python frontend, JSON schemas and source admission locks.
- Distinct QEMU_VIRT, Qualcomm holi/Penang and trinket/Sofiar descriptors.
- Fail-closed negative tests: unknown capabilities stay unknown; synthetic observations never qualify hardware.
- Own pre-MMU assembly guard, available for cross-compilation only. No XNU source or kernel is shipped.

## Run the public tests

Tested on Ubuntu 24.04/WSL, x86_64, Python 3.12.3, Rust/Cargo 1.99.1 and clang 18.1.3. Other hosts/toolchains have not been qualified. Install those tools using your normal package sources, then:

    python3 -m venv .venv
    . .venv/bin/activate
    python3 -m pip install -r requirements.txt
    cargo fetch --locked --manifest-path tools/penang-validate/Cargo.toml
    python3 -B scripts/validate_public.py

Dependency acquisition is explicit. The validation script uses Cargo offline and locked and does not download inputs, boot an emulator or access a device. It needs no ROM, firmware, IPSW, KDK, XNU checkout or private evidence. First dependency installation/build may take more than five minutes.

Inspect the synthetic example after validation:

    python3 -B tools/penang-validate/validate.py --platform qemu-virt --observation examples/qemu-host-synthetic.json --contract-only

Expected: PASS in HOST_CONTRACT scope, runtime_enabled=false, runtime_status=NOT_RUN.
Without the example observation, all three platform profiles intentionally return BLOCKED (exit 2).

<!-- CURRENT_STATUS:BEGIN -->
## Engineering state — fixed RC2 snapshot

| Track | Evidence and limit |
|---|---|
| W1 / W2/W3 | Accepted documentary foundation; host tests and ARM64 compilation. Their preserved snapshots remain historical. |
| P0.9 | Unified DEV-kernel cryptography, QEMU regression and scoped security tests documented. |
| P0.10 | B3_RESEARCH_PROVIDER_RUNTIME_VERIFIED under a project research root with authenticated PNRA-R1; positive acceptance and unauthorized-signer rejection documented in QEMU DEV. |
| Apple compatibility | Not APPLE_IPADOS_SECURITY_COMPATIBLE. Research Image4 interface v7, restricted operations; no Apple entitlement, SEP or complete userland support. |
| New blocker | B4_IOKIT_PLATFORM_DRIVER: IOKit matching reached; a valid Platform Expert was not initialized. |
| Hardware / D1–D4 | No physical G53 XNU boot, physical GPU or SpringBoard demonstrated. M9 is host only; Sofiar remains P1/UNKNOWN. |

Panic: Unable to find driver for this platform: "qemu,virt".

This RC is fixed to P0.10 afa7ef6acfb89bf5ae2f3d0c67e7a775096f39b0; later B4 progress cannot update it automatically.
These are documentary references to earlier QEMU results, not new RC2 runtime observations.
Monocypher and security limitations: [P0.10 scope](docs/P0.10-STATUS.md) and [component review](docs/MONOCYPHER-REVIEW.md).

Target: iPadOS 18.7.8 / 22H352 / iPad7,11 / ARM64_ALL; XNU 11417.140.69 at 43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
Penang: SM4350-AC/holi, Adreno 619. Accepted [ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md) and [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md) and Apple binary preservation remain unchanged.

PUBLICATION_BLOCKED; [human decisions](PUBLICATION-DECISIONS.md) remain pending.
<!-- CURRENT_STATUS:END -->

## Start contributing

Read [architecture](ARCHITECTURE.md), [validation](docs/VALIDATION.md), [roadmap](ROADMAP.md), [initial issues](docs/INITIAL-ISSUES.md), [contribution guide](CONTRIBUTING.md) and [security policy](SECURITY.md).
Current evidence is derived from evidence/current-status-sources.json by scripts/current_status.py. W4 records remain historical. RC2 gates and decisions are in W4-RC2-GATES.json and PUBLICATION-DECISIONS.md.

Source selection, adaptations and authorship are in [the selection ledger](audit/selected-sources.json) and [licensing review](docs/LICENSING.md).

The proposed license for project-owned material is MIT; external dependencies retain their own licenses and notices. See [LICENSE](LICENSE), [NOTICE](NOTICE), [authors](AUTHORS.md) and [dependency ledger](audit/dependencies.json).
This fixed RC2 is a local candidate awaiting the decisions in PUBLICATION-DECISIONS.md; self-review does not constitute independent approval. PUBLICATION_BLOCKED remains in force; no remote repository has been created.
