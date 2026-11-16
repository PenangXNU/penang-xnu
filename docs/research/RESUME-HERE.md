# Research state — start here

**As of 2026-11-16 · research snapshot, not a release or boot authorization.**
**Canonical repository:** PenangXNU/penang-xnu. **Target:** Motorola Moto G53 5G (`penang`, SM4350-AC/holi), XNU 11417.140.69, prospective iPadOS 18.7.8. **Last public release projection:** v0.1.0 / P0.12, deliberately frozen. Do not edit generated `STATUS.md`, the README status block or `manifests/current-status.json` by hand.

## Three independently scoped results

| Scope | Highest verified checkpoint | Boundary |
| --- | --- | --- |
| DEVICE_VERIFIED | M11-D3A1 Stage-0 executed **in RAM** on Moto G53: EL1, 16-KiB-granule CPU support, FDT/header and reservation metadata, visible own-code marker, post-boot partition checks | Not an XNU kernel boot, no PMAP/lockdown/grants certificate |
| QEMU_VERIFIED | T72-Q5: XNU 11417 QEMU_VIRT executed an ARM64 dynamic dyld/main fixture in **EL0**, with real relocation and pointer consumption (138 events; 34/34 independent checks) | Not full launchd/iPadOS; code-signature trust admission remains OPEN; not physical |
| HOST_ONLY | B1.4-H1 memory geometry, B4.2 PMAP contracts, B4.3A-R3 Qualcomm SCM/ABL static ABI research, T6 native diagnostic objects | Mechanisms and dependencies identified, not grants, an authorized SCM service for XNU or a linked PENANG_PHYSICAL kernel |

**G53 identification was restored in U2** (Android ADB, correct `penang` device). The earlier report in which USB enumeration found only `rav` is superseded. Never confuse the two devices.

## Native source-chain continuation (2026-11-16)

The [subsequent native link audit](2026-11-16-p1-pmap-link-audit.md) repeats
the 14 original errors with the exact compiler, closes the GIC profile unit,
corrects the retained-FDT consumer type and removes a byte-identical repeated
admission object. Actual ld64 over 1,027 distinct ARM64 objects still has 632
undefined symbols. KDK consumers remain an owner-side licensed build step;
cloud handoffs contain no authentic SART/UAT implementation. The explicit
PMAP selection is a code-generation proposal, not physical DMA qualification.

The [native tools](../../tools/m12-penang-physical/README.md#native-compiler-and-generated-providers)
now distinguish doconf-selected generated sources from the larger MIG generation
set. They cover BSD syscall/version generation, Skywalk/firehose exports, MIG
138 daemon clients, explicit MacOSX 15.6 metadata and bounded real compiler
type-trait checks. Preserve the original optional-profiling and VM semantic-type
rules when reconstructing commands. This is compilation/link research; no
physical XNU boot or `/sbin/penang-init` is established.

Continue at the reproduced native interface/provider failures, including
`TrustCache/API.h`, `CodeSignature/Entitlements.h` and the physical GIC provider.
P1 sandbox compilation remains separate from P2 backend qualification and from
approval of effective security-policy changes. Keep build objects, private
packets, operational scripts and full logs in the private handoff, outside Git.

## What to read, in order

1. [2026-11-15 evidence ledger](2026-11-15-physical-evidence-ledger.md): *which physical hardware work is already done*, original source branches, corrections to old reports, and scope.
2. [T72-Q5 result](2026-11-15-q5-el0.md): latest QEMU userland functional state, with signature/admission limits.
3. [Physical boundary](qualcomm-memory-security.md), [driver dependencies](drivers-and-userland.md) and the previous [2026-11-11 reconciliation](2026-11-11-reconciliation.md): original recovered source and known architecture.
4. [Native XNU bootstrap continuity](2026-11-15-native-bootstrap-cloud-continuity.md): current physical entry source chain, B4.2 compiler boundary and the precise meaning of `/sbin/penang-init`; PR #11 merged; PR #12 later merged into its feature base but not into `main@65078ff`; PR #13 proposes the P1 verifier and this continuity documentation together.
5. [Physical integration issue](https://github.com/PenangXNU/penang-xnu/issues/4): qualify the smallest missing native provider/link delta; do not repeat hardware reverse engineering.

## Unresolved physical gates (all OPEN for XNU-on-device)

FDT_PHYSICAL_HANDOFF; SCM_XNU_ABI; STAGE2_GRANTS; DMA_ENFORCEMENT; PMAP_PHYSICAL; NATIVE_VM_PMAP_COMPILE; NATIVE_XNU_LINK; GIC_TIMER_PHYSICAL; ENTROPY_PHYSICAL; XNU_DEVICE_ENTRY.

**PHYSICAL_BOOT_HOLD:** the physical XNU kernel has not been fully linked or qualified. The Stage-0 RAM experiment is not a substitute. No physical XNU execution, flash, reboot, fastboot boot, monitor call, firmware mutation or device operation is authorized by this document.

## Non-negotiable anti-rework and continuation contract

- Before assigning a worker, search **both** existing Git branches/worktrees and the private evidence inventory, then follow the report's source commit, file hash, test log and subsequent errata. An `OPEN` gate does **not** imply missing research.
- Reuse already imported public M9/M12 code from [2026-11-11 reconciliation](2026-11-11-reconciliation.md); do not reimplement or recopy it. Compare file-level SHA and licenses before adding more files.
- Separate `HOST_ONLY`, `QEMU_VERIFIED`, `DEVICE_VERIFIED`, `INFERRED` and `OPEN` claims. A Git commit or document is traceability, not runtime evidence.
- Never publish firmware, ROM partitions, Apple KDK/IPSW/shared caches, proprietary material, device identifiers, raw physical logs, photos, keys, local absolute paths or private evidence. Public research records name source branch/commit and abstracted artifact/check hashes; originals stay outside this repository.
- Keep original worktrees, uncommitted files and historical results. No reset/clean/force-push/whole-branch merge. New work must state its **smallest missing implementation or evidence**, existing owner, tests, negative cases and rollback.
- After each reviewed technical milestone, update provenance and this entrypoint, not a new internal work-order file. Do not overwrite old reports or retroactively recategorize experiments.

**Next implementation:** native-provider qualification via [issue #4](https://github.com/PenangXNU/penang-xnu/issues/4), and a separate QEMU test of the existing HOST_ONLY BSD `getpid` fixture. **Portuguese summary:** [Retomada Penang XNU](../pt-BR/RETOMADA-PENANG-XNU.md).

**Cloud host increment (2026-11-15):** the [P1 native input inventory](../../tools/m12-penang-physical/README.md#native-input-inventory-p1-host-only)
now reports missing retained build inputs, filesystem/source-reference failures
and explicit B4.2 refusal checks without executing Makefiles or compiling XNU.
The original public XNU source is available; native generated recipes and the
guarded research tree remain unavailable in cloud. The T73 packager also checks
previously omitted dyld identity/thread/build load commands against the public
structural fixture profile, including re-signed malformed inputs. Historical Q5
byte identity and runtime remain untested by these host checks; no physical gate
or native link has advanced. Recover the existing recipes/tree before attempting
the first configured native compile; do not manufacture substitutes.

**Subsequent native-build increment (2026-11-15):** owner-provided private
recipes/overlay now replay through the corrected lexical classifier (303 OSFMK,
83 IOKit sources) with B4.2 compile HOLD intact. A pinned public-source
cache-geometry delta and [fresh native compile probe](../../tools/m12-penang-physical/README.md#fresh-native-object-compilation)
produced 22 nonempty ARM64 Mach-O objects, including original `arm_init.c`,
`machine_cpuid.c` and `locore.s`. Upstream configuration/assym generators were
used; guards were not edited. Three empty objects were not counted as providers,
and five TUs failed compilation. VM intentionally reaches the B4.2 refusal;
`start.s` still lacks its tunables include, PMAP/Platform Expert require generated
MIG headers, and SHA assembly has a compiler-dialect error. This is a fresh
diagnostic subset, not the retained T6 replay, full kernel link or physical boot.
The next source-recovery target is the omitted T1 dependency/toolchain recipe;
do not invent tunables, MIG ABI, monitor or successful lockdown providers.


**T1/MIG/SHA continuation (2026-11-16, HOST_ONLY):** PR #15 is merged in
`main@a8c5983`; work continues on a separate branch from `db536ab`. The
[reviewed source-delta/MIG tools](../../tools/m12-penang-physical/README.md#t1-entry-real-mig-and-sha256-continuation-host-only)
recover the entry include/call pair, generate real interfaces with pinned Apple
MIG, and correct SHA256 assembler register syntax. All 386 recipe TUs were
compiled with the corrected native flags: 203 nonempty ARM64 Mach-O objects,
23 empty/non-provider units and 160 compiler failures. Seventy-four genuine
MIG headers were generated; selected C/C++ ABI consumers and all 80 static SHA
crypto instruction encodings were checked. No security stub or guard removal
was used. Original startup trust calls and VM/PMAP guard bytes are unchanged.

The next dependency is genuine XNU compiler builtins for typed allocations;
DriverKit/TrustCache APIs and the physical PMU/core assumptions also remain.
The protected P2 PMAP profile still requires its separately admitted backend.
A separate P1 functional profile has independent Stage-1/handoff requirements;
see the subsequent correction below.
The delivered object set is usable as linker input only: `NATIVE_XNU_LINK`,
`XNU_DEVICE_ENTRY` and `/sbin/penang-init` remain unobserved. Do not classify it
as `P1_NATIVE_LINK_CANDIDATE_UNTESTED` or a bootable/qualified image. Private
recipes, logs and objects stay outside the public repository.


**P1/P2 correction and implementation (2026-11-16, HOST_ONLY):** PR #16 is now
merged at `main@b877aa9`; the new isolated branch starts there. P1 diagnostic
Stage-1 functionality is independent of P2 monitor/Stage-2/DMA qualification.
The protected board and guards remain unchanged. New [memory admission and build
dependency tools](../../tools/m12-penang-physical/README.md#p1-functional-memory-admission-and-build-dependencies)
generate original version/syscall headers, fix optional/unused header dependencies,
and reject unsafe FDT/window geometry before VM setup. The same protected
386-TU replay now produces 209 nonempty ARM64 objects. An isolated private P1
prototype compiles original `arm_vm_init`, `cpu_bootstrap` and `machine_startup`
with the real admission call; its effective guard/profile patch awaits separate
security review and is not integrated into the protected build.

The exact remaining PMAP compile blocker is `TrustCache/API.h`, used also by
non-monitor trust lookups. The real runtime FDT provider is still absent; eight
size-only reservations in the offline evidence cannot be assigned guessed
addresses. State remains `P1_STAGE1_BUILD_HOLD`; builtin compiler and DriverKit
provider gaps persist. No full link, device entry, P2 qualification or execution
of `/sbin/penang-init` follows from this source progress.
