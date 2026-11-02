# Architecture and boundaries

The accepted target and binary-preservation policy come from ADR-0004 and ADR-0006 in contracts/platform/foundation/docs/adr/. The foundation directory is the preserved W1 documentary snapshot, with one public redaction of unnecessary device-condition metadata.

The working preview slice is C ABI v1 plus a Rust validator and Python/schema frontend. A request specifies 16 KiB granules, PA40 and VA47. The decoder checks supplied CPU/translation snapshots and half-open RAM/reservation intervals. Backends distinguish virtual CPU context from physical platform descriptors. The host fallback does not fabricate privileged registers.

QEMU_VIRT has a real MRS reader and a pre-MMU assembly guard, compiled here without executing privileged instructions. Holi reuses the existing host BSP descriptor validator. Trinket refuses runtime readiness. No backend is an operational XNU driver.

The public evidence registry binds to explicit fact projections, with original source hashes and scope. These replacements remove dependence on the private engineering tree; they do not turn summaries into independently reproduced runtime evidence. The core validator algorithms are byte-identical to W2/W3. A source lock is consistency checking, not authentication.

The larger system still requires loader/boot ABI, PlatformExpert, BSP drivers, complete policy-bearing security providers, Mach/IOKit compatibility and original userland services. P0.9 documents crypto integration only; B3 remains blocked. AIR translation or an ARM64 object is not proof of GPU execution. See [status](STATUS.md), [the contract](contracts/platform/CONTRACT.md) and [roadmap](ROADMAP.md).
