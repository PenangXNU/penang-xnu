# M12 isolated Stage-1 and B4.2 security contract

The six MIT-licensed C source/header/test files here preserve an isolated 16-KiB Stage-1 model and a fail-closed PMAP physical security contract.

- t2_stage1.c / .h and test_t2_stage1.c: one-vCPU managed L3 descriptor model, page permission transitions, TLBI, I-cache/D-cache sequencing and negative tests. A compile-time guard rejects the physical Penang profile.
- penang_pmap_lockdown_contract.c / .h and host_test_pmap_lockdown_contract.c: explicit ownership, grant, W^X, alias and DMA requirements. The production path cannot promote to device without a verified backend.

The original T2 Stage-1 tests ran in isolated QEMU/host scope, **not** inside the complete physical XNU PMAP. No Qualcomm monitor or SMMU backend is provided. Stage-2 grants and PMAP_PHYSICAL remain BLOCKED.

The source blobs are identical to the pinned local M12 integration inputs before M12-T4 and carry their original MIT SPDX. This import does not replace the separate native-XNU build or the corresponding original evidence.

[Technical boundaries](../../docs/research/qualcomm-memory-security.md) · [M12 status](../../docs/research/m12-kernel-and-handoff.md)

## Native input inventory (P1, host only)

`p1_native_inputs.py` inspects the retained generated OSFMK/IOKit Makefiles,
OSFMK configuration, board header and two source trees. It derives missing
source files from disk, rejects noncanonical paths and escaping symlinks, hashes
the inspected bytes, and reuses the original B4.2 assessor and P1 entry checker.
Comments cannot supply missing references or an active physical refusal. The
explicit physical refusal must be a top-level conditional `#error` block.

```bash
python3 -B tools/m12-penang-physical/p1_native_inputs.py \
  --xnu-original-root "$XNU_ORIGINAL" \
  --b42-guarded-root "$XNU_B42_GUARDED" \
  --osfmk-make "$OSFMK_MAKEFILE" --iokit-make "$IOKIT_MAKEFILE" \
  --osfmk-config "$OSFMK_CONFIG" \
  --board-header tools/m12-penang-physical/PENANG_PHYSICAL.h \
  --out .build/p1-native-inputs.json
```

Receipt creation is exclusive. Exit 2 means missing/invalid prerequisites;
exit 1 means supplied inputs were rejected; exit 0 means only
`INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD`. All outcomes preserve
`PMAP_PHYSICAL=BLOCKED`, compile/link/device/userland execution `NOT_RUN`.

Literal canonical `$(SOURCE_DIR)/…` sources are inventoried separately from
the exact generated C/S/P rule templates and genassym prerequisite. Rule A/B
families and canonical directories must match; unknown macro syntax, extra
recipe text, empty references, traversal and backslash escapes are rejected.
Make line continuations are normalized before classification. Other Make
variables, conditionals and computed source lists are not evaluated.
This is a **lexical reference inventory**, not Make evaluation or a compiler
flag interpreter. It never executes Makefiles, patches sources or synthesizes
missing configuration. Hashes record inspected bytes, not independently admitted
provenance. Source counts are not proof of a complete build or link closure.
Selector overrides with assigned values are conservatively rejected; supported
inputs retain bare `CONFIG_PMAP_PPL` and `CONFIG_KERNEL_INTEGRITY` options.

The cloud check against public XNU 11417.140.69 commit
`43a90889846e00bfb5cf1d255cdc0a701a1e05a4` inventoried original `start.s` and
`arm_init.c`, then stopped at missing retained generated OSFMK/IOKit Makefiles,
OSFMK configuration and the B4.2 guarded tree. These are existing research
inputs to recover, not permission to reconstruct a weaker physical profile.
No configured native compilation was attempted. Tests use synthetic sources;
they do not establish that unavailable historical recipes pass this parser.

The subsequent owner-supplied recipe/overlay replay passed the input audit:
303 OSFMK and 83 IOKit literal source references; 24/13 distinct rule templates
and one separate OSFMK genassym dependency. The original multiline refusal
banner is admitted without changing its bytes. C line splicing is normalized
before checking conditional depth, so an inactive outer `#if` cannot disguise
the refusal. This remains `INPUT_AUDIT_COMPLETE_B42_COMPILE_HOLD`.

## Fresh native object compilation

`p1_cache_geometry.py` emits an exclusive patch for the pinned public
`osfmk/arm64/proc_reg.h`. Its only source delta is a physical cache-line
selector with `MMU_I_CLINE=MMU_CLINE=6`, using the D3A1 64-byte observation
already recorded in the [physical ledger](../../docs/research/2026-11-15-physical-evidence-ledger.md).
The unknown-processor refusal and other targets remain unchanged. The tool
does not apply its patch, assert monitor support or edit VM/PMAP guards.

```bash
python3 -B tools/m12-penang-physical/p1_cache_geometry.py \
  --xnu-original-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/cache.patch"
git -C "$XNU_B42_GUARDED" apply --check "$PRIVATE_OUTPUT/cache.patch"
git -C "$XNU_B42_GUARDED" apply "$PRIVATE_OUTPUT/cache.patch"
python3 -B tools/m12-penang-physical/p1_compile_probe.py \
  --source-root "$XNU_B42_GUARDED" \
  --osfmk-make "$OSFMK_MAKEFILE" --iokit-make "$IOKIT_MAKEFILE" \
  --board-header tools/m12-penang-physical/PENANG_PHYSICAL.h \
  --generated-include "$GENERATED_HEADERS" --generated-include "$EXPORTED_HEADERS" \
  --source osfmk/arm/machine_cpuid.c --source osfmk/arm/arm_init.c \
  --source osfmk/arm64/arm_vm_init.c --out "$PRIVATE_OUTPUT/fresh-probe"
```

Use a fresh private source copy with the retained overlay. Never apply these
steps to the original upstream checkout or packet. Generate configuration
headers through upstream `SETUP/config` and `assym.s` through `arm64/genassym.c`
and the original sed rule; do not fabricate headers. Generated include roots
may contain `osfmk`/`iokit` children; exported include directories are explicit.

The compile probe preserves recipe `CONFIG_DEFINES`, adds explicit MacOSX ARM64
kernel flags from the pinned public `MakeInc.def`, separates component C/C++
flags, and never executes the Makefiles. Compiler, configuration, source,
feature-header, command and log identities are recorded in the private output.
Reviewed board bytes are snapshotted, argument-injection environment variables
are removed, and actual preprocessor macros must preserve the physical target,
16-KiB selector, PPL and integrity. Source or forced-header drift rejects results.

Exit 0 means only selected nonempty ARM64 `MH_OBJECT` files with external
definitions, under `SELECTED_OBJECTS_ONLY_B42_COMPILE_HOLD`; exit 2 means
`NATIVE_BUILD_BLOCKED`; exit 1 means input/tool/output rejection. Output must
be new and outside the source tree/public checkout. A zero compiler exit for
a deliberately empty unit is not counted as a provider; a VM/PMAP unit that
unexpectedly compiles is rejected. No kernel link or runtime is attempted.

The fresh LLVM 19 run compiled 22 nonempty objects, including original XNU
`arm_init.c`, `machine_cpuid.c` and `locore.s`, plus the retained platform
contracts. Three other selected units compiled without external definitions;
five failed compilation. Remaining frontiers include the intentional B4.2
refusal in `arm_vm_init.c`, absent `arm64/tunables/tunables.s` for `start.s`,
unproduced MIG headers (`mach/vm_map.h`, `mach/clock.h`), and a SHA assembly
dialect error. This is a fresh diagnostic subset, **not an exact T6 replay**:
the supplied historical scripts refer to additional T1/T4 tools/recipes and
toolchain inputs not present in the packet. No full-link candidate was produced.


## T1 entry, real MIG and SHA256 continuation (host only)

The continuation starts at PR #15 head `db536abddd9309305c42d8a883cd46b1137eb2d8`;
`main` already contains that change via merge `a8c5983`. Original XNU remains
11417.140.69, commit `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`.
Retained private packets are verified and unpacked outside the public checkout;
the original and guarded source trees remain separate.

Three pinned emitters produce small reviewable public-source deltas. They never
apply them. Apply only to a fresh private copy after `git apply --check`:

```bash
python3 -B tools/m12-penang-physical/p1_t1_entry.py emit \
  --source-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/start-only.patch"
python3 -B tools/m12-penang-physical/p1_t1_header_dependency.py \
  --source-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/pmap-header.patch"
python3 -B tools/m12-penang-physical/p1_sha256_assembly.py patch \
  --xnu-original-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/sha256.patch"
```

The entry delta scopes the Apple tunables include and its call together, leaving
the original common bootstrap instructions. It does not provide Cortex CPU
errata qualification. The header delta moves only `TrustCache/API.h` into its
existing PPL-monitor block: active PPL still requires the real API, and direct
AMFI/trust-cache clients still fail without it. The retained startup include
change is not applied. No trust initialization, signature policy or B4.2 guard
is removed. The SHA delta changes seven register spellings in four legacy
macros to their existing standard A64 equivalents; it changes no algorithm,
register number or instruction ordering. The real kernel SHA TU uses its
`BUILDKERNEL=1` vector-register preservation mode.

Build `migcom` from Apple's public `bootstrap_cmds` tag `bootstrap_cmds-133.140.1`,
commit `619db33806b11b58fa14078bc7a811958312e52d`. The Linux host build uses
unchanged generator sources and public host Mach headers from `cctools-port`
commit `ef969083aecf77dee1814d6961a06b88af23edef`. In a fresh private build directory:

```bash
bison -y -d -o y.tab.c "$MIG_SOURCE/migcom.tproj/parser.y"
flex -o lex.yy.c "$MIG_SOURCE/migcom.tproj/lexxer.l"
set --
for unit in error global header mig routine server statement string type user utils; do
  set -- "$@" "$MIG_SOURCE/migcom.tproj/$unit.c"
done
clang -D_GNU_SOURCE -DHAVE_BCMP -DHAVE_BZERO -DHAVE_BCOPY -DHAVE_INDEX -DHAVE_RINDEX \
  -include sys/types.h '-D__unused=__attribute__((unused))' \
  '-DMIG_VERSION="mig-133.140.1 (619db33806b11b58fa14078bc7a811958312e52d)"' \
  -I "$MIG_SOURCE/migcom.tproj" -I . -I "$HOST_HEADERS/cctools/include" \
  -I "$HOST_HEADERS/cctools/include/foreign" "$@" y.tab.c lex.yy.c -o migcom
sha256sum migcom
```

These eleven C files match the generator's original Xcode source phase.
`HAVE_*` describes real glibc facilities. Record source/tool hashes, the exact
command and independent host-versus-ARM64 **user-wire** layout checks before
admitting the binary. A version string alone is insufficient provenance.
The host's 24-byte wire header must not be equated to the kernel's 32-byte
native IPC header. No foreign host headers enter target compilation.

```bash
python3 -B tools/m12-penang-physical/p1_mig_headers.py \
  --xnu-root "$XNU_ORIGINAL" --osfmk-make "$OSFMK_MAKEFILE" \
  --migcom "$PRIVATE_MIG_BUILD/migcom" --migcom-sha256 "$MIGCOM_SHA256" \
  --out "$PRIVATE_OUTPUT/mig-user"
# Generate a kernel-server ABI explicitly in a separate root:
python3 -B tools/m12-penang-physical/p1_mig_headers.py \
  --xnu-root "$XNU_ORIGINAL" --osfmk-make "$OSFMK_MAKEFILE" \
  --migcom "$PRIVATE_MIG_BUILD/migcom" --migcom-sha256 "$MIGCOM_SHA256" \
  --kernel-server mach_host_server --out "$PRIVATE_OUTPUT/mig-server"
```

Default interfaces are `clock` and `vm_map`; repeat `--interface` for admitted
Makefile selections. An explicit `--kernel-server` uses `KERNEL_SERVER=1` and
`-mach_msg2`; ordinary exported server headers use their separate Make rule.
Kernel-user headers use `KERNEL_USER=1` and `-maxonstack 1024`. The wrapper also
supports the pinned `device/device_server` and UserNotification request/reply
rules through `--subsystem`. Retained `CONFIG_DEFINES`, `PRIVATE`, native target
and Mach flags are recorded. Every `.defs` preprocessing dependency must match
the pinned Git tree. Routine IDs and skipped slots are independently checked;
real `clock`/`vm_map` consumers compile in C and C++ with selected prototype and
layout assertions. This does not qualify every interface or execute Mach IPC.

The compile probe now restores upstream per-file corecrypto include paths and
`VM_UNSAFE_TYPES=1` for direct `osfmk/vm/*.c` units. Despite its name, that flag
**enables** semantic wrapper types and their assertions. Global overrides and
forced headers that erase required per-TU selectors are rejected. Include roots
may have component children even when only another root supplies meta-features.
They keep kernel-server headers out of the IOKit exported-header selection.

```bash
python3 -B tools/m12-penang-physical/p1_t1_entry.py audit \
  --object "$START_OBJECT" --reference-object "$RETAINED_START_OBJECT" \
  --out "$PRIVATE_OUTPUT/entry-audit.json"
python3 -B tools/m12-penang-physical/p1_sha256_assembly.py audit \
  --object "$SHA256_OBJECT" --out "$PRIVATE_OUTPUT/sha256-audit.json"
```

The entry audit checks a nonempty ARM64 Mach-O object, its unconditional first
CPU branch, real relocations to `arm_init` and native MMU/cache/TLB instructions.
The historical and current 9,116-byte text sections differ only in 1,276 words
of zero-versus-NOP padding in two 4-KiB alignment gaps; all remaining bytes and
36 relocations match. Byte identity of complete historical objects is not claimed.
The SHA audit independently matches all 80 statically emitted SHA256 crypto
instructions against Arm encodings, ordering and operands. Runtime KATs, CPU
errata, full link, physical entry and `/sbin/penang-init` remain untested.


The final corrected MacOSX run selected all 386 literal recipe TUs and compiled
203 nonempty ARM64 Mach-O objects (199 OSFMK, four IOKit). Another 23 compiled
without external definitions and are excluded from provider counts; 160 failed
compilation. This is selected-TU compilation, not a complete XNU build: other
components, generated MIG implementation C files and required platform libraries
have not closed. No full kernel link or boot image is produced.

The generated dependency set is 50 user/exported Mach headers, 21 explicit
kernel-server Mach headers and three device/UserNotification headers. C/C++ ABI
consumers cover the two specified interfaces. Fresh `start.s`, `arm_init.c`,
`clock.c`, `vm_init.c` and `vm_fault.c` objects are present. The real SHA object
SHA-256 is `3f15f824d1bf4604d404b64a0e5c53c970e08cb01066bc1cad469b9ddd5f2727`;
entry object SHA-256 is `b87057f7ef0eef12c10b5682faa045e9f1c5dd6f53dbb253a1ab4268fc6fd52a`.
Objects, generated headers, logs and their manifests remain private.

Remaining first diagnostics include the LLVM 19 compiler's missing genuine
`__builtin_xnu_type_summary`, `__builtin_xnu_types_compatible` and
`__builtin_xnu_type_signature` support; missing DriverKit/TrustCache APIs;
Cortex PMU/Apple core-type assumptions; and build metadata headers. `vm_map.c`
now passes its generated-header and semantic-wrapper frontiers and reaches the
compiler builtins. The next compiler prerequisite is a source-admitted toolchain
implementing these builtins, preserving typed-allocation checks and metadata.
An empty macro, constant-success substitute, `XNU_NO_TYPE_BUILTINS`, disabling
TrustCache, or invented PMU constants is not an acceptable continuation.
Even after those dependencies close, a real admitted physical lockdown backend
is still required: both original B4.2 guard files remain byte-identical.


## P1 functional memory admission and build dependencies

P1 diagnostic Stage-1 functionality and P2 qualified physical integrity are
independent milestones. A monitor/Stage-2/DMA backend is not a universal P1
build prerequisite. P1 still needs real Stage-1 memory management, correct
handoff/reservations, permissions, exception/GIC/timer providers and error
semantics. The protected `PENANG_PHYSICAL.h` and B4.2 refusals remain unchanged.
The prior blanket PMAP-backend wording applies to that protected profile;
it must not be used to redefine every diagnostic Stage-1 build as a P2 claim.

`p1_generated_headers.py` generates `libkern/version.h` and `sys/syscall.h`
through pinned XNU `newvers.pl`, `unifdef` and `makesyscalls.sh`. It never
executes Makefiles or imports SDK declarations. Version metadata is explicit:
`24.6.0d1` in the recorded diagnostic run is a custom development build value,
not a recovered stock release identifier or ABI qualification. Inputs must
match XNU commit `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`; the caller admits
the unifdef binary SHA and retains its external source/package provenance.
`SEED_DEFINES` is explicitly empty in this supported generation profile.

```bash
python3 -B tools/m12-penang-physical/p1_generated_headers.py \
  --xnu-root "$XNU_ORIGINAL" --unifdef "$UNIFDEF" \
  --unifdef-sha256 "$UNIFDEF_SHA256" --darwin-version 24.6.0d1 \
  --out "$PRIVATE_OUTPUT/upstream-generated"
python3 -B tools/m12-penang-physical/p1_optional_headers.py \
  --source-root "$XNU_ORIGINAL" --out "$PRIVATE_OUTPUT/optional-headers.patch"
```

The second tool emits only pinned include hygiene: `startup.c` and `cpu.c`
include the KPC header only when the existing `CONFIG_CPU_COUNTERS` option is
active; `sleh.c` and `machine_routines_common.c` drop an unused Apple-core helper
header. No CPU identity/counter values, checks, function bodies, trust calls,
source flags or security guards are replaced. Active profiling still requires
the real KPC dependency. The isolated P1 experiment regenerates configuration
with upstream SETUP/config after removing genuine optional profiling options,
rather than inventing `CORE_NCTRS`. Its source selection and headers are
recorded separately from the protected 386-TU replay.

`p1_memory_admission.c/.h` implements a read-only FDT/window admission gate for
the real `arm_vm_init` path. This is new validation code, not the historical T2
page-table model and not a replacement PMAP. Its supported input domain is:

- a readable immutable FDT v17 mapping, at most 2 MiB, with root/reserved-memory
  address/size cell widths 2/2 and identity reserved-memory ranges;
- a 16-KiB-aligned contiguous ARM64 managed window wholly inside one declared
  RAM bank, without overlapping banks, holes or static reservations;
- every enabled reserved-memory request materialized through a real `reg`;
  size-only requests are refused, never assigned dry-run addresses;
- a retained FDT physical extent inside the bootstrap prefix ending at
  `topOfKernelData`, before the VM allocator's available memory.

Reservations from both the FDT reserve map and reserved-memory nodes exclude
whole outward-rounded 16-KiB pages. Duplicate/invalid reg/status/cell properties,
overflow, malformed bounds and unsupported geometry are rejected. The input
blob is never changed. A zero result means only `P1_MEMORY_GEOMETRY_OK` for
those bytes and arguments. The 48-bit parser ceiling is not a measured CPU PA-width certificate.
The boot bridge must still supply the actual runtime mapping/identity, CPU
address-width/coherency checks and permission to use the window; a supplied offline
snapshot cannot certify runtime ownership or replace that provider.

The module compiles to ARM64 Mach-O and passes host adversarial tests, including
600 bounded mutations. It refuses the protected board even if P1 macros are
also supplied. **It is not selected automatically by a production target.**
The private sandbox proposal adds an explicit P1 selector, original non-monitor
Stage-1 source selection and a call to this gate before `arm_vm_init` mutates VM
state. It requires a real `penang_p1_runtime_fdt` provider at link time; none is
fabricated. The guard/profile integration patch remains outside GitHub pending
separate security review. No protected target inherits the diagnostic selector.

| Path or risk | Implementation/evidence | Remaining boundary |
| --- | --- | --- |
| `start.s` → `arm_init` → `arm_vm_init` | Original source compiled; entry audit and real gate/handoff relocations | Host compilation, not observed physical control flow |
| Continuous physical aperture and reservations | Gate rejects holes, overlap, unresolved requests and lost FDT | Real runtime FDT/window provider is absent |
| Mapping permissions, attributes, TLBI/cache and locks | Original `arm_vm_init`/PMAP implementation retained | PMAP still fails on its genuine TrustCache API dependency; full lifecycle is not qualified |
| P1/P2 selection | Explicit sandbox selector; protected/partial/mixed selections reject | Effective guard changes require separate approval before integration |
| Monitor/grants/DMA/TrustCache | No successful replacement or invented provider | P2 remains blocked; dependent operations cannot claim success |

The final protected replay of the same 386 TUs produced **209** nonempty ARM64
Mach-O objects, six more than 203. The P1 sandbox selected 371 genuine recipe
TUs and produced 207 nonempty objects, including original `arm_vm_init.c`,
`cpu.c` and `machine_routines.c` plus the admission module. It adds ten provider
TUs relative to the historical set while six optional profiling provider TUs
leave the configuration; these are different sets and must not be added together.
The 74 existing MIG headers were reused only after all preprocessed definition
inputs proved byte-identical under the diagnostic configuration.

**`P1_STAGE1_BUILD_HOLD`:** `pmap.c` and `startup.c` still require the real
`TrustCache/API.h`. PMAP has TrustCache lookups outside the monitor-only path,
so an unconditional removal or empty API is not a legitimate fix. Genuine XNU
compiler builtins and generated DriverKit interfaces also remain dependencies.
The supplied offline FDT is refused for unresolved reservations; no physical
placement is inferred. A public experimental compiler implementation was
inspected, but neither its semantic compatibility nor an admitted binary was
established: `BUILTIN_COMPILER_HOLD`. There is no full linked kernel or boot
image. No device operation, SCM call, QEMU run or environment publication occurred.
