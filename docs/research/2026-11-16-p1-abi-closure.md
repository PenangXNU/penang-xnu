# P1 ABI closure: the 14 compile blockers, GICv3 and the retained FDT

**2026-11-16 · HOST_ONLY, plus one QEMU_VERIFIED result scoped to the platform
services on QEMU `virt`.** Not a kernel build, not an XNU run, not a device
result. Base: `feat/p1-native-build-codex-20261116@763674e` (native build
branch, not yet merged). The native build chain, PMAP, the B4.2 guards and the
full link stay with their owner; nothing in this change edits them. AI-assisted.

## Outcome in one paragraph

Of the 14 failing translation units, **one is closed** by a three-line change
to the private board profile (`pe_fiq.c`), **one has a candidate interface**
that needs an owner decision (`necp_client.c`), and **twelve stay BLOCKED**:
the header they need, `TrustCache/API.h`, is not public source, and neither is
the library behind it. No stand-in header was written for it. The link-time
blocker `penang_p1_runtime_fdt` now has a real provider that resolves against
the delivered `arm_vm_init` object. The GICv3 driver ran unmodified at EL1 on
QEMU in both security configurations and took the timer as a Group 1 IRQ.
`NATIVE_XNU_LINK`, `GIC_TIMER_PHYSICAL` and every other physical gate remain
OPEN.

## How the failures were reproduced

The delivery's exact compiler (LLVM `58c46bae` plus the delivered patch,
SHA-256 `7c9f2cb1…2dba9`) does not exist as a binary in this cloud host. A
rebuild from the pinned commit with that patch was started and had not
finished when this record was written. Everything below was therefore observed
with **Ubuntu clang 18.1.3 in `-fsyntax-only` mode**, on a private tree
rebuilt from public XNU `43a90889` plus the delivered overlay (23/23 overlay
hashes match) and the delivered generated configuration, using the flag sets
in `tools/m12-penang-physical/p1_compile_probe.py`. That is a different
toolchain; these are diagnostics, not Gate A receipts.

- 12 of the 14 first errors reproduce at the identical file, line and column.
- The two C++ units (`IOUserClient.cpp`, `IOKitBSDInit.cpp`) stop earlier in
  stock clang on constructs the patched compiler provides, so they are
  **not reproduced** here.

## The 14 units, before and after

| # | Unit | Original first error | After this work |
| ---: | --- | --- | --- |
| 1 | `osfmk/arm/pmap/pmap.c` | `TrustCache/API.h` | BLOCKED. Behind it: `arm64/ppl/sart.h` and `arm64/ppl/uat.h`, included unconditionally at lines 94–95 and also absent from public source |
| 2 | `osfmk/kern/startup.c` | `TrustCache/API.h` | BLOCKED; no further diagnostic once the include is satisfied in a probe |
| 3 | `iokit/Kernel/IOUserClient.cpp` | `TrustCache/API.h` | BLOCKED; frontier not probed (needs the pinned compiler) |
| 4 | `iokit/bsddev/IOKitBSDInit.cpp` | `TrustCache/API.h` | BLOCKED; frontier not probed |
| 5 | `bsd/kern/code_signing/txm.c` | `TrustCache/API.h` | BLOCKED; frontier inconclusive with stock clang |
| 6 | `bsd/kern/kern_cs.c` | `TrustCache/API.h` | BLOCKED; no further diagnostic in the probe |
| 7 | `bsd/kern/kern_exec.c` | `TrustCache/API.h` | BLOCKED; also needs the four entitlement macros of unit 11 |
| 8 | `bsd/kern/kern_proc.c` | `TrustCache/API.h` | BLOCKED; frontier inconclusive |
| 9 | `bsd/kern/kern_trustcache.c` | `TrustCache/API.h` | BLOCKED; the heaviest consumer |
| 10 | `bsd/kern/ubc_subr.c` | `TrustCache/API.h` | BLOCKED; no further diagnostic in the probe |
| 11 | `bsd/net/necp_client.c` | `CodeSignature/Entitlements.h` | CANDIDATE header published; binding needs an owner decision; confirmation with the pinned compiler outstanding |
| 12 | `bsd/vm/vm_unix.c` | `TrustCache/API.h` | BLOCKED; frontier inconclusive |
| 13 | `libkern/amfi/amfi.c` | `TrustCache/API.h` | BLOCKED; no further diagnostic in the probe |
| 14 | `pexpert/arm/pe_fiq.c` | `GICR_TYPER` undeclared | CLOSED by a profile change; compiles to an object defining `_pe_init_fiq` (stock clang; the owner must re-run) |

"Probe" means a private, shape-only header used to see what lies behind the
missing include. It was never published, linked or counted as a pass.

## G1 — `TrustCache/API.h`: not recoverable from public source

Evidence that this is a Kernel Development Kit interface, not an omission:

- Neither `TrustCache/` nor `CodeSignature/` exists in the pinned checkout;
  the only references are the six `#include` lines.
- Upstream's own build adds the KDK's `Kernel.framework/PrivateHeaders` to the
  include path (`makedefs/MakeInc.def`, `INCFLAGS_SDK += -I$(KDKROOT)/$(KPINCDIR)`)
  and force-loads `$(KDKROOT)/System/Library/KernelSupport/lib<machine>.os.<config>.a`
  into the kernel link. The header and the implementation both come from there.

The project does not use the KDK, so the interface stays a gate. What a
conforming interface would have to supply, taken from compiler diagnostics on
the eleven C consumers and from the public sources that use it:

| Kind | Identifiers | Why a guess is not acceptable |
| --- | --- | --- |
| Result type | `TCReturn_t` with members `component`, `error`, `uniqueError` | returned by value across the AMFI interface table |
| Result codes | `kTCReturnSuccess`, `kTCReturnError`, `kTCReturnNotFound`, `kTCReturnDuplicate` | compared against values produced by the library |
| Objects with layout | `TrustCache_t` (allocated by XNU with `kalloc_type`), `TrustCacheRuntime_t` and `TrustCacheMutableRuntime_t` (XNU defines storage for them), `TrustCacheQueryToken_t` | size and layout are shared with the library that fills them |
| Scalar types | `TCType_t`, `TCQueryType_t`, `TCCapabilities_t` | passed through the interface table |
| Type constants | `kTCTypeInvalid`, `…LTRS`, `…DTRS`, `…Engineering`, `…Legacy`, `…Static`, `…Cryptex1BootOS`, `…Cryptex1BootApp`, `…Total` | index a library table and are stored in trust caches |
| Query constants | `kTCQueryTypeAll`, `…Static`, `…Loadable`, `…Total`; `kTCCapabilityNone` | same |
| Sizes | `kTCEntryHashSize`, `kUUIDSize` | array bounds in function signatures |
| Library data | `TCTypeConfig[type].entitlementValue` | defined by the library |
| Library function | `trustCacheInitializeRuntime(…)` | called directly; no public definition |
| Interface table | `amfi->TrustCache.{version, load, loadModule, query, checkRuntimeForUUID, getCapabilities, queryGetTCType, queryGetFlags, queryGetHashType}` | signatures are public in `libkern/libkern/amfi/amfi.h` (`TRUST_CACHE_INTERFACE_VERSION 3`); the implementation is registered by a component that is not public |

So a header alone would not close these units' purpose: the kernel would still
need the library and the registering component at link and run time.

Options, for the owner to choose; none is taken here:

1. Keep the gate. Twelve units stay out of the archive.
2. Use headers and the support library from a lawfully obtained KDK as a
   private build input. This contradicts the standing no-KDK rule and needs
   the legal review the project already lists as open.
3. Design a project-owned trust-cache interface and implementation with real
   verification semantics. It would compile and link, would not be
   layout-compatible with Apple's binary components, and is a security design
   in its own right that needs its own review. It is the only option
   compatible with the current rules, and it is not a small one.

## G2 — `CodeSignature/Entitlements.h`: names known, values ambiguous

The two public consumers take exactly four macros from this header, each used
as a `const char *` entitlement key: `kCSWebBrowserHostEntitlement`,
`kCSWebBrowserGPUEntitlement`, `kCSWebBrowserNetworkEntitlement`,
`kCSWebBrowserWebContentEntitlement` (`necp_client.c:10718`,
`kern_exec.c:1398–1401`).

Public XNU's own tests (`tests/entitlements/hardened-binary-entitlements-{1,2}.plist`)
list, under a comment naming each mask, **two to four keys per macro** — for
example `com.apple.private.web-browser-engine.host` and
`com.apple.developer.web-browser-engine.host`. They do not say which single
key the macro holds. The value is therefore not recoverable.

`tools/p1-penang-platform/xnu-interfaces/CodeSignature/Entitlements.h` is a
project-authored **candidate**: it binds each name to the key Apple documents
publicly for third-party browser engines (`…developer.web-browser-engine.host`,
`.rendering`, `.networking`, `.webcontent`). A test pins every value to the
evidenced set. This is a policy choice among candidates, it may differ from
Apple's binding, and it grants nothing by itself: entitlement validation and
`TRUST_ADMISSION` are untouched and OPEN. It is not on any include path until
the owner accepts it.

## G3 — GICv3: why `pe_fiq.c` failed and what closes it

The private P1 profile defines `HAS_GIC_V3`, `GIC_SPURIOUS_IRQ` and
`GICR_PE_SIZE`. `HAS_GIC_V3` has exactly one consumer in the tree: it turns on
the body of `pe_fiq.c`, which is the VMAPPLE path — Group 0, PPI 27, FIQ — and
needs the register macros of `VMAPPLE.h`. Supplying those macros would make
the file compile and would then enable Group 0 from non-secure EL1 on the
phone, which is the contract shown to be wrong for this SoC in the
[platform services record](2026-11-16-p1-physical-platform-services.md).

**Fix: remove those three defines from the profile.** Upstream `pe_fiq.c` then
compiles to its own `#else` branch, an empty `pe_init_fiq()`. Observed with
stock clang: 16 errors with the delivered profile, 0 without the three lines,
and the object defines `_pe_init_fiq` — a symbol the delivered link log lists
as undefined. `GIC_SPURIOUS_IRQ` and `GICR_PE_SIZE` have no other consumer
outside `APPLEVIRTUALPLATFORM` code. The profile is private and belongs to the
build-chain owner; this is a proposal for them to apply and re-run.

Published alongside: `ppp_gicv3_regs.h`, the architectural register map from
Arm IHI 0069 with no board selection, so that no Penang source ever needs
`VMAPPLE.h` for an offset. It records that `VMAPPLE.h` names bit 0 of
`ICC_CTLR_EL1` "EOIMODE" while the architecture puts EOImode at bit 1.

### QEMU result for the driver

Authorized by the owner for this instance. QEMU 8.2.2, `virt`, Cortex-A76,
TCG. A bare-metal EL1 harness (`tools/p1-penang-platform/qemu/`) links the
unmodified `ppp_resources.c`, `ppp_gicv3.c` and `ppp_timer_uart.c`, takes the
DTB the emulator passes, and reports through semihosting.

| Machine | CPUs | `GICD_CTLR.DS` seen at EL1 | Result |
| --- | ---: | ---: | --- |
| `virt,gic-version=3` | 1, 4 | 1 | PASS |
| `virt,gic-version=3,secure=on` | 1, 4 | 0 | PASS |
| `virt,gic-version=2` | 1 | — | refused at resolution, controller never written |

In each passing run: no interrupt before anything is enabled; a self-SGI sent
through `ICC_SGI1R_EL1` taken once on the IRQ vector; the virtual-timer PPI
named by the emulator's tree (INTID 27 there, 19 on holi-shaped trees) taken
once as a Group 1 IRQ; after disabling it at the controller the timer source
asserts (`ISTATUS == 1`) and nothing is delivered; two acknowledges, two
completions, FIQ vector never entered. `secure=on` is the case that matters:
non-secure EL1 below an EL3 that owns Group 0.

Scope: **QEMU_VERIFIED for these three files on QEMU's GICv3 model.** It is
not XNU, not the SM4350, and says nothing about what the phone's firmware
leaves configured. `GICD_CTLR.DS` on the device is still unmeasured.

### Adapter ABI proposed to the build-chain owner (not implemented)

No upstream file needs an edit for the first IRQ. Hooks that already exist:

| Step | Existing upstream hook | Proposed Penang behaviour |
| --- | --- | --- |
| Resources | `arm_init` → `PE_init_platform(FALSE)` (device tree readable) | `ppp_resolve()` on the retained FDT; refuse to continue on any status but OK |
| MMIO | after `arm_vm_init` | map the distributor and the whole redistributor region read from the tree with `ml_io_map`; `struct ppp_io` translates the resolved physical addresses and refuses any other |
| Controller init | `PE_init_cpu()` → `pe_init_fiq()` | a Penang translation unit defines `pe_init_fiq()` in place of `pexpert/arm/pe_fiq.c` (file selection, owner's call) and runs `ppp_gic_init` with IRQ masked |
| Vector | `sleh_irq` → `cdp->interrupt_handler` (the non-`USE_APPLEARMSMP` path) | `ml_install_interrupt_handler()` installs a handler that calls `ppp_gic_dispatch` |
| Timer | `rtclock_intr()` | for the tree's virtual-timer INTID: `cpu_decrementer = -1; rtclock_intr(TRUE)`, exactly what `sleh_fiq` does today; priority `0xa0`, `ICC_PMR_EL1 = 0xff`, EOImode 0, completion after the handler returns |
| Rate | `timebase-frequency` | `ppp_timer_admit`: `CNTFRQ_EL0` adopted, compared with the tree, never written |
| Failure | — | any refusal leaves `ICC_IGRPEN1_EL1 = 0` and `ICC_PMR_EL1 = 0` and panics before interrupts are unmasked |

Open points for the review: IOKit later re-installs the CPU interrupt handler
through its own controller, so the root interrupt controller class must route
to the same dispatch; one CPU only; no FIQ source exists on this path.

## G4 — `penang_p1_runtime_fdt`

Observed, not inferred: the overlay's `arm_vm_init.c` declares
`extern const struct p1_runtime_fdt penang_p1_runtime_fdt;` and passes its
address to `p1_memory_admit()` before any table is written; the delivered
object `033-arm_vm_init.o` has `_penang_p1_runtime_fdt` undefined. It is a
**data object** of the type defined in `p1_memory_admission.h`.

A link-time constant cannot hold an address known only at run time, so
`ppp_runtime_fdt.c` defines the storage under a private C name bound to that
linker symbol. It starts zeroed — admission refuses it — and is filled once by
`ppp_runtime_fdt_bind()`.

| Property | Contract |
| --- | --- |
| Source of truth | one retained copy; `p1_memory_admit` and `ppp_resolve` read the same bytes |
| Hand-over | physical address, exact byte length, `physBase`, `virtBase`, `topOfKernelData` |
| Placement | wholly inside `[physBase, topOfKernelData)`: loaded with the kernel, mapped at `virtBase` through VM bring-up, never handed to the page allocator |
| Format | 8-byte aligned, magic, version 17, last-compatible ≤ 17, big-endian header, `totalsize` equal to the handed-over length, at most 2 MiB |
| Lifetime | single assignment before `arm_vm_init`; a second bind is refused; a failed bind publishes nothing and does not consume the assignment |
| Memory semantics | not judged here; size-only reservations still make `p1_memory_admit` return `UNRESOLVED_RESERVATION` |

Evidence: with the delivered `033-arm_vm_init.o` and `1026-p1_memory_admission.o`,
an LLD 18 link leaves `_penang_p1_runtime_fdt` undefined without the provider
object and defined with it. LLD is not the delivery's ld64-956; Gate B is the
owner's re-run.

Still missing, and deliberately not invented:

- **The bridge side.** Nothing yet copies the firmware's FDT into the prefix or
  conveys its address and length. `boot_args` has no field for it; the
  proposal is a property under `/chosen` of the bridge-built tree, read in
  `arm_init` between `PE_init_platform(FALSE)` and `arm_vm_init`. That is one
  call in a native file and an addition to the bridge output, both for the
  joint contract.
- **Integrity.** The provider validates structure and placement, not content.
  A digest computed by the bridge and checked at bind time is the natural
  extension; which digest and who vouches for it is undecided.
- **Immutability.** Enforced only by placement and by never writing; no page
  protection is claimed.

## Gates

- **A / COMPILATION_CLOSURE: not met.** 1 closed pending the owner's re-run,
  1 candidate, 12 blocked on a non-public interface. No false pass.
- **B / LINK_FRONTIER: with the owner.** New definitions offered:
  `_penang_p1_runtime_fdt`, and `_pe_init_fiq` once the profile change is
  applied. The full ld64 re-run and the complete undefined list are theirs.
- **C / RUNTIME: nothing claimed.** No kernel ran anywhere; `/sbin/penang-init`
  was not executed; B4.2 guards untouched and still fail-closed.

## What the build-chain owner is asked to do

1. Drop `HAS_GIC_V3`, `GIC_SPURIOUS_IRQ` and `GICR_PE_SIZE` from the P1
   profile, recompile `pe_fiq.c` with the pinned compiler, re-run the link.
2. Review the adapter hooks and the FDT hand-over above before either side
   edits `arm_init`, the file lists or the bridge output.
3. Note the second `pmap.c` dependency (`arm64/ppl/sart.h`, `uat.h`).
4. Add `ppp_runtime_fdt.c` to a trial link once (2) is agreed.
