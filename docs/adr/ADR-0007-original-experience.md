# ADR-0007 — Original Apple experience, OpenCore-style adaptation, bring your own IPSW

Date: 2026-11-02 · Status: **ACCEPTED** (project owner) · Complements ADR-0004 (target, Apple binary preservation) and ADR-0006 (delivery D1–D4).

## Context

The end goal is an iPadOS experience on non-Apple ARM64 hardware (first target: Moto G53 5G, `penang`) that is as close as possible to the original. The owner takes inspiration from OpenCore, which boots unmodified macOS on non-Apple PCs by adapting the platform below the operating system instead of changing the operating system itself.

## Decision

1. **Apple binaries run unmodified.** The iPadOS userland, frameworks and the dyld shared cache come from the user's own IPSW and are executed byte for byte. No patching, re-signing or binary rewriting of Apple user-space code is part of the design.
2. **Adaptation lives below the OS.** Every difference between the target hardware and an Apple device is handled in layers this project owns: the boot loader, the platform contracts, the XNU platform layer (Platform Expert, interrupt controller, timers, power, storage and display drivers), research security providers and documented XNU patches. Each adaptation is explicit, versioned and testable, in the spirit of OpenCore's declarative configuration.
3. **Nothing original from Apple is in this repository (BYO IPSW).** No IPSW, kernelcache, dyld shared cache, firmware, KDK or extracted Apple binary is committed, mirrored or downloaded by project tooling. Users supply their own legally obtained copy; tools take local paths and keep outputs local.
4. **Fidelity is measured, not claimed.** "Close to original" is assessed with observable criteria (unmodified binary hashes, boot sequence, services, UI and graphics behaviour) under the evidence classes already in use. Gaps are reported, not hidden.
5. **Security honesty.** Where Apple hardware security (SEP, Image4 personalization, PPL/SPTM) cannot exist on the target, the substitute is a documented research mechanism with its own trust root. It is never presented as Apple security equivalence.

## Consequences

- Kernel and platform work grows (drivers and services that iPadOS expects must exist below the OS), while the OS image stays pristine.
- Changes that would require modifying Apple binaries are out of scope unless a later ADR explicitly revisits this decision.
- XNU modifications remain APSL-2.0 patches; project-owned code is Apache-2.0.
- Legal review is still required before any tooling automates the use of Apple software on non-Apple hardware; this ADR does not grant any right to use or redistribute Apple software.
