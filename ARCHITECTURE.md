# Architecture and boundaries

The accepted target and binary-preservation policy come from ADR-0004 and ADR-0006 in contracts/platform/foundation/docs/adr/; the OpenCore-style principle (unmodified Apple binaries, adaptation below the OS, BYO IPSW) from [ADR-0007](docs/adr/ADR-0007-original-experience.md). The foundation directory is the preserved W1 documentary snapshot, with one public redaction of unnecessary device-condition metadata.

## Layers

1. Apple inputs — user-supplied, immutable, never in this repository.
2. XNU source — public 11417.140.69 plus small APSL patch series (from v0.2).
3. Platform layer — contracts, SoC backends (qemu-virt, qualcomm-holi, qualcomm-trinket), board support, Platform Expert and drivers.
4. Research security providers — real verification under a project research root, never presented as Apple equivalence.
5. Userland bridge — the kernel services the unmodified userland expects.
6. Validation — HOST, QEMU and DEVICE evidence with negative controls.

## What v0.1 contains

The working slice is C ABI v1 plus a Rust validator and Python/schema frontend. A request specifies 16 KiB granules, PA40 and VA47. The decoder checks supplied CPU/translation snapshots and half-open RAM/reservation intervals. Backends distinguish virtual CPU context from physical platform descriptors. The host fallback does not fabricate privileged registers.

QEMU_VIRT has a real MRS reader and a pre-MMU assembly guard, compiled here without executing privileged instructions. Holi reuses the host BSP descriptor validator. Trinket refuses runtime readiness. No backend is an operational XNU driver.

The public evidence registry binds to explicit fact projections, with original source hashes and scope. They do not turn summaries into independently reproduced runtime evidence. The core validator algorithms are those of W2/W3; v0.1 changed only license headers. A source lock is consistency checking, not authentication.

## What the larger system still needs

Interrupt controller and CPU power manager (B5), then RTC, root filesystem, entitlements and code-signing policy for user-supplied binaries, the IOKit services iPadOS expects, device drivers for the G53 and a Metal-capable Adreno driver stack. AIR translation or an ARM64 object is not proof of GPU execution. See [status](STATUS.md), [the contract](contracts/platform/CONTRACT.md) and [roadmap](ROADMAP.md).
