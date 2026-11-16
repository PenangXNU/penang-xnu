# P1 PMAP selection, retained-FDT ABI and real link audit

**2026-11-16 · COMPILE_ONLY / HOST_ONLY.** Stacked on PR #20 at
`46efb70ba9a5494868cf458b59a4f8fba5f66169`, containing PR #19 at
`763674e6291fb6be4d75ea5cf026b62dfcf7ef8a`. No merge, device operation or
QEMU run. AI-assisted implementation and verification.

## Actual result and reproducibility

The real ld64-956.6 invocation over **1,027 distinct ARM64 Mach-O objects**
(987 with external definitions, 40 empty nonproviders) fails with **632
undefined symbols**. `_penang_p1_runtime_fdt` and `_pe_init_fiq` now resolve,
each through exactly one provider. No duplicate strong definitions remain
after the source-selection correction below. This is an actual linker
result, not the earlier 134-symbol projection or a linked kernel candidate.

The first linker diagnostic is `_IOBSDGetPlatformUUID`, referenced by
`gethostuuid`. `IOKitBSDInit.cpp`, which supplies that function, remains
blocked by the authentic TrustCache API build dependency. PMAP definitions
also remain absent. Declarations cannot substitute for either object.

Original XNU: `43a90889846e00bfb5cf1d255cdc0a701a1e05a4` (11417.140.69).
Compiler: LLVM `58c46bae21181fb4240cf964461bea738d96d50e` plus the retained
type-trait patch; actual Clang binary SHA-256
`1f3a4757e27ac5f7f36afd45fca08df6a25b5f371e28a51f01b69798c8fdfe62`.
Actual ld64 binary SHA-256
`3d4c5436f774cb44b74ecfbab94990f1af4342358445a3fae48e56bad841ff7f`.
Target: ARM64-all, Cortex-A76 instruction selection, MacOSX deployment 15.6,
16-KiB private P1 sandbox. This does not qualify physical CPU errata.

All 14 original failed commands were repeated with that compiler and retained
generated inputs; all original first errors reproduced. PR #20's three-macro
GIC profile correction then compiles original `pe_fiq.c` to an ARM64
MH_OBJECT defining `_pe_init_fiq`. Thirteen units remain blocked: twelve on
`TrustCache/API.h`, one on `CodeSignature/Entitlements.h`.

The supplied archive's 21 header sizes/hashes match the metadata-only pins.
Proprietary headers were not deployed to a compiler include root or used in
this cloud replay: the supplied work order restricts their deployment to the
licensed owner-side environment. The two subsequent cloud handoffs contain
**no `sart.h` or `uat.h`**; their README explicitly excludes historical empty
bootstrap substitutes. Absence from a subset does not prove absence from the
complete owner-held KDK.

The owner-side 13/14 result is therefore **not independently reproduced
here**. The public Entitlements candidate remains off the include path. The
existing LOCAL_ONLY KDK adapter is reused. No Apple support archive is linked.
Commands, logs, effective flags, generated-source receipts and an owner-side
plan remain in the private delivery. No physical entry or trust admission is
qualified by these results.

## Narrow SART/UAT code-generation proposal

Public non-SPTM `pmap.c` has two relevant includes and two calls,
`uat_bootstrap()` and `sart_bootstrap()`, immediately after
`pmap_data_bootstrap()`. No return value establishes successful bootstrap.
No other direct consumer exists in that implementation. The separate SPTM
PMAP's UAT path remains untouched. Missing implementations prevent inference
of their DT parsing, allocations or DMA effects.

`p1_pmap_platform.py` emits, but never applies, a hash-pinned four-site delta.
The default path retains both original headers and calls. The separate
Penang compilation experiment requires the diagnostic board, sandbox opt-in,
`NO_MONITOR` and `PENANG_P1_IOMMU_CODEGEN_ONLY=1`. Protected P2, PPL, SPTM,
Apple PAC, partial selectors and other platform combinations are refused.
No replacement bootstrap function or DMA guarantee is supplied. B4.2 guard
text, TrustCache consumers, admission and Stage-1 algorithms are unchanged.
The existing private P1 guard/profile proposal still needs its own review.

Compiler tests verify original unresolved hardware calls in the default
path, absence of fake providers in the experiment, and negative profile
selection. Recompiling actual `pmap.c` still stops at `TrustCache/API.h`.
Its next post-API diagnostic is **not observed here**.

## Type-correct retained-FDT contract

The provider has mutable descriptor storage under `_penang_p1_runtime_fdt`;
the old consumer declared that object itself `const`. Different C names
sharing an assembler symbol do not make incompatible object types valid.
`p1_runtime_fdt_provider.h` declares mutable storage and exposes
`p1_runtime_fdt_view()`, a pointer to const. Tree bytes remain const.
`p1_runtime_fdt_consumer.py` emits only the include/declaration and call-site
correction in the existing private `arm_vm_init.c` proposal. No guard changes.

The corrected actual XNU unit and unchanged PR #20 provider compile with the
pinned Clang and resolve in ld64. Host C/C++ type checks, optimized execution
and ThinLTO verify the contract: missing storage fails link, zero storage
fails admission, failed bind publishes nothing, successful bind is visible,
and a second bind is refused. Fixtures are self-authored, not physical FDTs.

Bridge/initialization integration must satisfy all of these requirements:

1. Retain the **actual** handoff FDT with dynamic reservations materialized.
   Transport its physical address and exact length through a separately
   reviewed handoff; do not resize Apple's 1,152-byte `boot_args` or synthesize
   the tree from an offline snapshot.
2. Bind once on the boot CPU with IRQ masked, **after** bootstrap KVA mapping
   becomes valid and **before** `arm_vm_init`. High `virt_base` cannot be
   dereferenced while MMU is off.
3. Keep all bytes inside mapped `[physBase, topOfKernelData)`. VA/PA offset,
   readable lifetime and immutability must survive VM remapping and resource
   resolution; a disappearing identity map or scratch buffer is invalid.
4. Any refusal stops before table mutation. Bind verifies placement/header,
   not authenticity, ownership, grants or DMA isolation.

No real initialization path calls the binder yet. Symbol resolution does not
prove binding. Claude's provider/bridge implementation files are unchanged.

## Duplicate selection and remaining runtime boundary

The previous selection compiled memory admission both as a doconf unit and
as a standalone extra. Both source hashes and complete object hashes match;
object SHA-256 is
`c8fdb483ca6b50406bae8fb89eea90266bd452b96e16a605d746278714ea8ffb`.
The new selection retains the doconf identity once. `p1_link_inputs.py` checks
ARM64-all MH_OBJECTs, symbols and empty units, and coalesces explicit source
aliases only when **both** hashes agree. Conflicting aliases or duplicate
strong providers fail; weak definitions follow linker coalescing rules.

The future IRQ adapter must install XNU's `ml_install_interrupt_handler`
contract before enabling interrupts. `sleh_irq` invokes the per-CPU handler
with target, refCon, nub and source. The adapter must dispatch Group 1 NS,
admit the tree's timer INTID and `CNTFRQ_EL0`, service/rearm `CNTV_*`, and
invoke `rtclock_intr` in the required XNU context. `ppp_gic_dispatch` owns
the single EOI; no second EOI or completion of a spurious INTID. Refusal
keeps IRQ masked; no Group 0/FIQ fallback. No adapter is installed here.

TrustCache headers authenticate an interface, not its runtime implementation.
XNU still requires genuine AMFI registration (`amfi_interface_register`,
TrustCache interface version 3) and panics with `amfi interface not available`
when absent. No permissive lookup, registration stub or signature bypass is
introduced. P1 diagnostics and P2 qualification remain separate.

Next: execute the prepared 14-unit plan with authenticated headers on the
licensed host, retain the next actual PMAP diagnostic/object, and replay ld64
with those objects. Current status: `NATIVE_LINK_HOLD`, not physical boot.
