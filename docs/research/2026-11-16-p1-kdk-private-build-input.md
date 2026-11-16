# KDK as a private build input: 13 of 14 units compile

**2026-11-16 · HOST_ONLY.** Object compilation only: not a link, not a kernel,
not an interface qualification, not a device result. Follows the
[ABI closure record](2026-11-16-p1-abi-closure.md). Stacked on the native
build branch at `763674e`. AI-assisted.

## Owner decisions this record acts on

1. **TrustCache and CodeSignature come from the authentic Kernel Development
   Kit**, KDK 15.6 build 24G84, as a private build input. No replacement
   interface is designed.
2. The entitlement binding of the earlier candidate header is accepted because
   it equals the KDK's.
3. The GIC profile correction is applied in the repository.

The KDK is Apple proprietary software. Nothing of it is in this repository,
this record or any log published here: only file names, sizes and SHA-256
values. Whether a given machine may hold and use it is governed by the licence
its holder accepted; the owner lists that review as pending, and this record
makes no legal determination. A third-party mirror of KDK packages was
suggested as a source; nothing was fetched from it — it is not needed (the
owner holds the exact pinned build) and the tag named was a different build.

## What changed in the repository

- `tools/m12-penang-physical/PENANG_PHYSICAL.h` no longer defines
  `HAS_GIC_V3`, `GIC_SPURIOUS_IRQ` or `GICR_PE_SIZE` (owner-authorized edit in
  the build-chain directory). The B4.2 refusals in that file are untouched.
- `tools/p1-penang-platform/kdk/p1_kdk_probe.py`: a LOCAL_ONLY adapter.
  `verify` checks a header directory against published pins; `find` locates
  headers by name in a KDK tree and prints paths and hashes; `compile` runs a
  caller-supplied plan with the verified directory on the include path and
  writes a report that holds no header content and no local paths. It refuses
  unverified, symlinked or extra files, an output directory that overlaps the
  KDK, and plan flags that would dump preprocessed text or dependency files.
- `kdk/kdk-15.6-24G84-header-pins.json`: 21 header paths with size and SHA-256.

## Result

Compiler: clang rebuilt here from LLVM `58c46bae21181fb4240cf964461bea738d96d50e`
plus the delivered patch (SHA-256 `7c9f2cb1…2dba9`). Same source as the native
build's compiler, **different binary** (`4899031a…3dee` here, `1f3a4757…fe62`
there). Tree: public XNU `43a90889` plus the delivered overlay and generated
configuration, rebuilt privately; it is a reconstruction of the native build
tree, not that tree. Gate A therefore remains the build-chain owner's re-run.

- **Baseline:** all 14 original first errors reproduce at the same file, line
  and column with this compiler.
- **With the corrected profile and the 21 verified KDK headers:**

| # | Unit | State | Defined external symbols |
| ---: | --- | --- | ---: |
| 1 | `osfmk/arm/pmap/pmap.c` | **BLOCKED**: `arm64/ppl/sart.h` not found (line 94; `arm64/ppl/uat.h` follows) | — |
| 2 | `osfmk/kern/startup.c` | compiled | 18 |
| 3 | `iokit/Kernel/IOUserClient.cpp` | compiled | 345 |
| 4 | `iokit/bsddev/IOKitBSDInit.cpp` | compiled | 41 |
| 5 | `bsd/kern/code_signing/txm.c` | compiled, empty (not a provider in this configuration) | 0 |
| 6 | `bsd/kern/kern_cs.c` | compiled | 104 |
| 7 | `bsd/kern/kern_exec.c` | compiled | 24 |
| 8 | `bsd/kern/kern_proc.c` | compiled | 233 |
| 9 | `bsd/kern/kern_trustcache.c` | compiled | 28 |
| 10 | `bsd/kern/ubc_subr.c` | compiled | 81 |
| 11 | `bsd/net/necp_client.c` | compiled | 116 |
| 12 | `bsd/vm/vm_unix.c` | compiled | 167 |
| 13 | `libkern/amfi/amfi.c` | compiled | 2 |
| 14 | `pexpert/arm/pe_fiq.c` | compiled (upstream's empty `pe_init_fiq`) | 1 |

The same 13/1 split was obtained through the published adapter.

**Entitlement binding.** `necp_client.c` compiled against the project's
candidate header and against the KDK headers gives the same object, byte for
byte. The owner-side comparison of the four macro values agrees.

**The remaining unit.** `sart.h` and `uat.h` are not among the 21 headers of
the KDK's TrustCache, CodeSignature and CoreEntitlements folders. Whether the
KDK carries them elsewhere is unknown here; the owner can check on their
machine with
`p1_kdk_probe.py find --kdk-root <KDK> sart.h uat.h`. If it does not, `pmap.c`
needs a different answer from its owner; none is proposed here.

## Projected link frontier

Computed from symbol tables, not by the linker: the 634 symbols the delivered
ld64 log lists as undefined, minus what the 13 new objects define.

- 500 of the 634 are defined by the 13 objects.
- 134 remain. All but a handful are the `pmap_*` family and related globals
  that `pmap.c` defines, plus `_penang_p1_runtime_fdt`, which the provider in
  `ppp_runtime_fdt.c` defines.
- The 13 objects add one new unresolved reference of their own,
  `_pmap_query_page_info_retries`, again from `pmap.c`, besides
  linker-synthesized symbols.

So `pmap.c` is the first real link blocker, and the next one after it is the
bridge side of the retained FDT.

## The library side

None of the 13 objects has an undefined reference to a TrustCache library
symbol. In this configuration the kernel reaches the implementation only
through the interface table that a separate component registers at run time
(`amfi_interface_register`; `TRUST_CACHE_INTERFACE_VERSION 3`), so **no KDK
library has to be linked**, and none was. The KDK's kernel support archives are
built for Apple platforms; their CPU subtype was not inspected here because
the binaries are not in this environment, and nothing should force-link them.

At run time nothing registers that table. The public source then stops at
`panic("amfi interface not available")` in the trust-cache initialisation.
That is the correct behaviour for now: no success path was synthesized and no
signature or admission check was relaxed. What provides trust-cache services
on this platform is an open design question, and `TRUST_ADMISSION` stays OPEN.

## Gates

- **A / COMPILATION_CLOSURE: 13 of 14 here, not yet a receipt.** It becomes
  one when the build-chain owner repeats it with their compiler binary and
  tree. `pmap.c` is blocked on two further non-public headers.
- **B / LINK_FRONTIER: projected only.** No linker was run on the new objects.
- **C / RUNTIME: nothing claimed.**

## Owner-side command and report

```bash
python3 -B tools/p1-penang-platform/kdk/p1_kdk_probe.py verify --kdk-headers <KDK private headers>/platform
```

```bash
python3 -B tools/p1-penang-platform/kdk/p1_kdk_probe.py compile --kdk-headers <KDK private headers>/platform --plan plan.json --compiler <pinned clang> --out <new directory>
```

`plan.json` is `{"xnu_root": "...", "units": [{"name": "...", "source": "...",
"argv": ["..."]}]}` with each unit's compiler arguments and no output option.
`report.json` (schema `penang-kdk-probe-report-1`) gives, per unit: status,
exit code, error count, the first diagnostic with paths replaced by `<KDK>`,
`<XNU>` and `<LOCAL>`, the object's SHA-256 and its defined and undefined
external symbols. It can be shared; the headers cannot.
