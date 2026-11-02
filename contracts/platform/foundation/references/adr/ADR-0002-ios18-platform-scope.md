# ADR-0002 — iOS 18 platform program and Apple binary integrity
Date: 2026-10-24
Status: PROPOSED; requires feasibility gates before iOS18 becomes an executable phone target.
Updates ADR-0001 only as to product roadmap priority; preserves iOS16.5 as bring-up baseline.

## Product direction
Build a Qualcomm XNU reference platform, not a G53-only ROM. Cross-device support requires at least two SoC families with independent regression evidence. Pilot: Moto G53 (penang).
- Primary research goal: iOS 18 generation, original Apple files as immutable, verifiable inputs. iOS 18.7.8 build 22H352 is our research target, but its exact XNU kernelcache revision must be extracted and verified before ABI or boot adaptations.
- Boot baseline: iOS 16.5 QEMU and Snapdragon HackiOS as functional evidence/driver reference.
- Fallback research: iPadOS18 on older A10 hardware uses native arm64 code paths where available; do not mix incompatible kernel/framework components without ABI proof.

## Hard constraints
- No claim that untouched Apple iOS18 userland runs directly on penang without verification. Qualcomm 480+ is not demonstrated to support ARM64e PAC.
- Bergamota rewrites arm64e binaries and dyld shared cache (its README says so); it is a reference for feasibility, not a binary-preserving architecture.
- Use distinct classifications ORIGINAL, RUNTIME_ADAPTED, TRANSFORMED; record all byte differences and runtime hooks. Never label transformed content 'original'.
- Even original userland bytes do not restore Apple Secure Boot, SEP, FairPlay or Apple's update trust chain on Qualcomm.
- Do not incorporate any Apple proprietary code, personal user data or Qualcomm firmware binary into the public repository.

## First proof required
1. iOS16.5 QEMU SpringBoard fully reproduced.
2. Establish correct iOS18 version mapping BEFORE ABI work: Bergamota's XNU11215.81.4 migration guide is for iOS18.0–18.3, and indicates XNU11417 family for 18.7.x. Verify the exact XNU kernelcache of iOS18.7.8 22H352; then compare ABI and identify ARM64e-only binaries and missing ISA prerequisites.
3. Compare runtime translation/compatibility vs transformed binary approaches and document trade-offs; favor original-on-disk preservation without claiming unmodified execution.
4. Simulate/validate memory budgets for 4GB penang before any phone boot session.
5. Freeze source/kernel/firmware versions and run QEMU regression for each change.

## Kill-switch / stop conditions
- Original-on-disk requirement cannot be honored without modifying Apple's system image: flag as unsupported rather than obscuring transformation.
- If battery, charging, thermal, data preservation, or return-to-Android cannot be proven, no on-device release.
- A project-controlled update is not Apple OTA. Every OS update is a separately supported build with migration/rollback tests.
