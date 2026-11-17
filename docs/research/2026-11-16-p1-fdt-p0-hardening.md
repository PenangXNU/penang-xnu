# P1 FDT P0 hardening: bind, retention and seed transport

**2026-11-16 · HOST_ONLY, plus QEMU_VERIFIED for the bind on QEMU `virt` with
the MMU on.** Work order WO-CLAUDE-P1-FDT-P0. Base
`main@b063b7cc75eb55f7dc2be8dbdde274c7ec4cd907`. Not a kernel build, not an
XNU run, not a device result. No PMAP file, compile selector, link tool or
B4.2 guard was changed. AI-assisted.

## Matrix

PROVEN = reproduced or checked here; INFERRED = derived from source reading,
not executed; OPEN = not settled.

| # | Problem (source) | Before | Now | Evidence | Still needs |
| --- | --- | --- | --- | --- | --- |
| 1 | Bind dereferences an invalid or null VA (Gemini 1) | PROVEN by inspection: the header was read at `virt_base + offset` with no mapping check | **Fixed.** No byte is read until a non-faulting translation hook confirms every 4 KiB of the tree maps readable at exactly the expected PA. VA 0, wrap-around and VAs outside the adapter's vouched range are refused | Host: holes, aliases and an unbacked VA refused without a crash, ASan/UBSan clean. QEMU: 16-KiB granule, TTBR1 window, hook = `AT S1E1R`; absent block, aliased block, missing 16-KiB page, null base all refused with **no abort**. A mutant that reads before the proof aborts (ESR `0x96000006`, translation fault L2), so the test detects the old behaviour | The kernel adapter's real hook (same instruction) — not written |
| 2 | Only the header is checked before the single-assignment publish (Gemini 2) | PROVEN by inspection | **Fixed.** `ppp_fdt_check_structure` validates header, block placement and overlaps, the reservation map (terminated, no wrap, no zero size), the token stream, names, property order and uniqueness, root-child uniqueness, depth, and that `FDT_END` is last | 22 malformed-tree classes refused; 3000-case mutation sweep; the private Moto G53 tree passes (see below) | Content integrity (digest) is not checked |
| 3 | A failed bind may consume the slot or leave partial state | Partly: the old code wrote only at the end, but had no re-entrancy guard | **Fixed.** Atomic state (unbound → binding → bound); every refusal restores "unbound"; the descriptor is written only after all checks, `bytes` last with release ordering; a nested bind is refused `BUSY`; storage written by anyone else is never overwritten | Descriptor compared byte-for-byte after every refusal; a valid bind succeeds after each refusal, on host and in QEMU | — |
| 4 | Physical retention vs `ml_static_mfree` and VM remap (Gemini 4) | The provider accepted any PA in `[physBase, topOfKernelData)`, **including the prefix XNU frees**; the bridge never retained the firmware FDT at all | **Fixed in provider and bridge.** Provider requires `[kernel_end_pa, topOfKernelData)` and no overlap with the Apple device tree or `boot_args`. Bridge copies the FDT right after `boot_args`, raises `topOfKernelData` past it, and announces it as `/chosen/penang,fdt-pa` and `penang,fdt-length` (not in `memory-map`) | Source reading below (INFERRED for runtime); host composition bridge → copy → bind → admission | **The memory-admission gate still accepts the freed prefix** (proposal P-1); XNU_MONITOR builds slide this region (OPEN for P2); runtime lifetime needs an XNU QEMU run |
| 5a | `rng-seed` 512 bytes vs a 64-byte rule (Gemini 4) | PROVEN: the bridge refused anything but 64 bytes, so the observed handoff could never pass | **Fixed (transport only).** 64..512 bytes accepted; exactly 64 forwarded unchanged; longer condensed with SHA-512; duplicates, shorter, longer and all-zero refused. Seed values zeroed in the retained copy (`rng-seed`, `kaslr-seed`) | SHA-512 equals Python `hashlib` for 265 lengths; copy is byte-exact except the zeroed values | **Entropy gate stays closed**: no quality or provenance claim |
| 5b | Size-only reservations (Gemini 4) | Bridge refuses them on the physical profile; admission returns `UNRESOLVED_RESERVATION` | Unchanged, deliberately | Bridge refusal re-tested | Policy decision (options below) |
| 5c | `boot_args` ABI | 1152 bytes | Unchanged | Static assertions and a host check of `sizeof` | — |
| 6 | Timer DT flags (incl. `0x0`, `0x5`) (Gemini 5) | `edge = flags & 3` coerced silently | Not in this PR | — | Separate PR, as the work order asks |
| 7a | Property after child (Gemini 7) | — | Rejected by the structure check and the resolver | Host differential: **the memory-admission gate accepts it** (returns 0) | Proposal P-2 |
| 7b | Window over contiguous RAM banks (Gemini 7) | — | Recorded only | Admission rejects a window spanning two adjacent banks (3, `BAD_WINDOW`) in one or two nodes. The bridge already picks windows inside one bank, so the two agree; the specification does not require it | Policy, not a defect |
| 8 | P1/P2 macro combinations (Gemini 3) | — | Recorded only (guards untouched) | `PENANG_PHYSICAL.h` rejects `MONITOR`, `XNU_MONITOR`, `PMAP_CS`, `PMAP_CS_ENABLE`, `KERNEL_INTEGRITY_WT`, `HAS_GUARDED_IO_FILTER`, `NO_MONITOR` at compile time; **not `CONFIG_PMAP_PPL`** (the compile driver asserts that separately) | Guard owner's decision |
| 9 | TrustCache v3 / AMFI (Gemini 8) | Fail-closed panic when no interface is registered | Unchanged | — | Real implementation; no stub |

## Real tree check (private, local only)

The structure check, the resolver and the admission gate were run on the
owner-held Moto G53 device-tree snapshot (479,269 bytes, SHA-256
`6b09ae62…048a`) without publishing it. Structure: OK. Resolver: OK.
Admission: `UNRESOLVED_RESERVATION`, as expected while the eight size-only
pools stay unplaced.

Two findings changed the checker:

- **Non-zero alignment padding.** 18 properties (in `/chosen`,
  `/chosen/mmi,storage` and `/chosen/mmi,bl_sigs`) have stale bytes after the
  value: the boot loader rewrites values in place. The specification says
  padding is zero, but no parser reads it, so the checker ignores padding
  instead of refusing the real tree.
- **Property names outside the specification's character set** (`;`, `:`, and
  digits only, in `mmi,bl_sigs`). Names are checked for printable ASCII, not
  the specification's set.

Depth 8 and 2,724 nodes, both inside every limit in use.

## Retention: what XNU 11417 frees and keeps

From the pinned source (`43a90889`), non-`XNU_MONITOR` configuration (P1):

| Range | Fate | Source |
| --- | --- | --- |
| `[physBase, segLOWEST)` | **freed** to the VM in `arm_vm_prot_finalize` | `osfmk/arm64/arm_vm_init.c:1665` |
| PRELINK gap, `__BOOTDATA` | freed | `arm_vm_init.c:1673`, `:1682` |
| `__KLD`, `__KLDDATA`, kext linkedit | freed | `libkern/c++/OSKext.cpp:1234–1266`, `:7593–7768` |
| `[end_kern, topOfKernelData)` | **kept**, mapped RWNX: "Device Tree (if not locked down), RAM Disk (if present), bootArgs" | `arm_vm_init.c:1629` |
| `[topOfKernelData, …)` | general allocator (`first_avail = topOfKernelData`) | `arm_vm_init.c:1974` |
| Named `/chosen/memory-map` entries | freed when a consumer asks by name (`DeviceTree`, kernel segments, `preoslog`) | `iokit/Kernel/IODeviceTreeSupport.cpp:257`, `:360–416` |

Consequences, all applied:

- The retained FDT must sit in `[end_kern, topOfKernelData)` and nowhere in
  the prefix. `end_kern` is `round_page(last kernel segment end)`; the bridge's
  `kernel_hi_pa` is the same value.
- It must not be a `memory-map` entry. The bridge publishes it through two
  dedicated `/chosen` properties.
- Its VA stays `virtBase + (pa − physBase)` through VM bring-up because that
  range keeps its static mapping. **INFERRED**; an XNU run is needed to make it
  PROVEN.
- With `XNU_MONITOR`, `arm_vm_init.c:1597–1599` slides this range into the
  physmap and calls `PE_slide_devicetree`. The descriptor would then hold a
  stale VA. P2 needs a re-slide hook or a re-bind; OPEN.

Names in use, kept apart: `plan->dt_pa` and `boot_args.deviceTreeP` are the
**Apple** device tree the bridge builds (64 KiB, after the kernel);
`plan->retained_fdt_pa` is the **firmware** FDT copy; `plan->fdt_pa` is where
firmware left the original, which the bridge keeps reserved outside the
kernel window.

New bridge layout inside the window: kernel … `[ramdisk]` · Apple device tree
(64 KiB) · `boot_args` (16 KiB) · retained FDT (rounded to 16 KiB) ·
`topOfKernelData`. Window selection now includes the retained slot.

## Seed transport

XNU reads `SEED_SIZE` = 64 bytes from `/chosen/random-seed`
(`osfmk/prng/prng_random.c:51,119`) and zeroes only the bytes it copies
(`pexpert/gen/pe_gen.c`). Publishing a 512-byte property would have left 448
seed bytes readable in the Apple device tree for the whole boot. The bridge
therefore always publishes exactly 64 bytes:

| Field length | Result | `penang,loader-entropy-source` |
| --- | --- | --- |
| 64 | forwarded unchanged | `fdt-rng-seed-64` |
| 65–512 | SHA-512 of the whole field | `fdt-rng-seed-sha512` |
| < 64, > 512, all zero, duplicated, absent | refused (`ERR_NO_ENTROPY`) | — |

The physical entropy gate (`m12_physical_entropy_gate.h`) still returns
"unqualified", so a physical handoff still stops at `ERR_RNG_UNTRUST`. That
header was referenced by `bridge_core.c` but missing from the public tree; it
is imported unchanged (MIT, source SHA-256 `ec31a561…3020`, tracked at research
commit `acb364dc`).

## Proposals for the memory-admission owner (not applied)

- **P-1 — prefix.** `p1_memory_admit` refuses an FDT outside
  `[base, top]` but accepts one in `[base, segLOWEST)`, which XNU frees. Either
  admission takes `kernel_end` as a fifth input, or it relies on the
  provider's stricter check and says so in its contract.
- **P-2 — property after child.** Admission accepts a tree with a property
  after a child node; the provider refuses it at bind. Harmless today (bind
  runs first), but the two gates should agree.
- **P-3 — size-only reservations.** The eight Moto G53 pools (CMA,
  `qseecom`, `secure_display`, `memshare`, …) are requests for the operating
  system to allocate; firmware gives them no address. Options: (a) the bridge
  places each one inside its `alloc-ranges` and writes the placement into the
  retained copy as `reg` — a derived tree, which needs agreement; (b) P1 treats
  reusable pools without a P1 consumer as not required, while pools marked
  `no-map` stay refused. Nothing is decided here; both gates keep refusing.

## Interface changes in this PR

- `struct ppp_fdt_handoff` is now ABI 2: `abi_version`, `mem_size`,
  `kernel_end_pa`, the Apple device tree range and the `boot_args` address are
  required. `ppp_runtime_fdt_bind()` takes a second argument, the mapping
  evidence. Both are provider-owned; no caller exists yet.
- `struct m12_handoff_plan` gains `retained_fdt_pa`, `retained_fdt_length`,
  `seed_kind` and `retained_seeds_scrubbed` (bridge-internal, not XNU ABI).
- The Apple device tree gains `/chosen/penang,fdt-pa` and
  `/chosen/penang,fdt-length`. Reading them in `arm_init` between
  `PE_init_platform(FALSE)` and `arm_vm_init` is a native call site and stays
  a proposal for joint review.
- `struct p1_runtime_fdt` and `p1_runtime_fdt_provider.h` are unchanged.

## Commands and results

```bash
python3 -B -m unittest tests/public/test_p1_fdt_retention.py -v
```

```bash
PPP_TEST_HOST_CC=gcc PPP_TEST_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" LD_PRELOAD="$(gcc -print-file-name=libasan.so)" ASAN_OPTIONS=detect_leaks=0 python3 -B -m unittest tests/public/test_p1_fdt_retention.py
```

```bash
PENANG_ALLOW_QEMU=1 python3 -B -m unittest tests/public/test_p1_penang_platform_qemu.py -v
```

Results: 17/17 host tests plain and under GCC ASan/UBSan; 4/4 QEMU tests
(QEMU 8.2.2, `virt`, Cortex-A76, TCG, 1 GiB); full public validation in the
pull request. Host compiler Ubuntu clang 18.1.3; GCC 13.3.0.

## Hashes

| File | SHA-256 |
| --- | --- |
| `tools/p1-penang-platform/ppp_runtime_fdt.c` | `051542b1316c50f77d6f97effcdbf0aefc96958afac43fe7708e35252167baf4` |
| `tools/p1-penang-platform/ppp_runtime_fdt.h` | `87882a0dc09349c1bb57628578c47a2e036d771eea2b0bf57fb78359649ca3ed` |
| `tools/m12-boot-bridge/m12_retained_fdt.c` | `099a031aaedf1a780d4fd7875e2c3721afbb4c67c48a40ec164adc095da62acd` |
| `tools/m12-boot-bridge/bridge_contract.h` | `3f6f6e6b90381d9ea6517b717a9b4a76f80e4443fdc681b18064b0935dd64e4b` |
| `tools/m12-boot-bridge/bridge_core.c` | `48d92df60a7b78cbbe67471d145da466f6494b98afbe1988bca0db23bdeebb8a` |
| `tools/m12-boot-bridge/m12_physical_entropy_gate.h` | `ec31a561c351585222ed4a4737e73fac4e9f99a60ca7231b214a17a5a6003020` (unchanged import) |

Previous versions on `main@b063b7c`: `ppp_runtime_fdt.c`
`23b79582…301f`, `bridge_core.c` `bf84771b…cdc6`.
