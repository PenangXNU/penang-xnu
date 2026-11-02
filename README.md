# Penang XNU — public preview

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

## Engineering state

| Track | Evidence and limit |
|---|---|
| W1 | Accepted documentary foundation; its preserved manifests are a historical snapshot. |
| W2/W3 | Host-tested contracts and ARM64 compile checks. Historical G4 closure: 17 PMAP, 37 public and 6 Rust tests passed in the local engineering tree. That entire private-input suite is not distributed here. |
| This preview | Public-only subset, revalidated from an exported source tree; see W4-PUBLICATION-GATES.json. |
| P0.9 | Cryptography integrated in a QEMU_VIRT DEV kernel, according to the pinned documentary record. Not imported or re-executed by this preview. |
| B3 | BLOCKED: no complete qualified Image4/AMFI/TrustCache provider. |
| D1–D4 | Not demonstrated: device XNU boot; original launchd through SpringBoard; measured Adreno-backed Metal composition; touch and Android return. |

Target: iPadOS 18.7.8 / 22H352 / iPad7,11 / ARM64_ALL; public XNU 11417.140.69 at 43a90889846e00bfb5cf1d255cdc0a701a1e05a4.
Primary device: Moto G53 5G, Penang, SM4350-AC/holi, Adreno 619. Sofiar/SM6125/trinket remains P1/UNKNOWN.

[ADR-0004](contracts/platform/foundation/docs/adr/ADR-0004-no-pac-userland-and-kernel-base.md) and [ADR-0006](contracts/platform/foundation/docs/adr/ADR-0006-delivery-scope-and-tracks.md) remain accepted. Apple binaries must remain byte-identical. Nothing here establishes Apple security equivalence, EL1 execution, hardware support or permission to redistribute Apple material.

## Start contributing

Read [architecture](ARCHITECTURE.md), [validation](docs/VALIDATION.md), [roadmap](ROADMAP.md), [initial issues](docs/INITIAL-ISSUES.md), [contribution guide](CONTRIBUTING.md) and [security policy](SECURITY.md).
Source selection, adaptations and authorship are in [the selection ledger](audit/selected-sources.json) and [licensing review](docs/LICENSING.md).

The proposed license for project-owned material is MIT; external dependencies retain their own licenses and notices. See [LICENSE](LICENSE), [NOTICE](NOTICE), [authors](AUTHORS.md) and [dependency ledger](audit/dependencies.json).
This is a local release candidate awaiting independent review and owner approval. PUBLICATION_BLOCKED remains in force; no remote repository has been created.
