# P1 physical platform services and userland continuation

**2026-11-16 · HOST_ONLY research record.** Not a release, a boot image, a
device result or an authorization. Read [the research entrypoint](RESUME-HERE.md)
and [the evidence ledger](2026-11-15-physical-evidence-ledger.md) first. Base:
`main@b2afe898cd15a46eda3b09178251fe7f34be0de7`. No QEMU instance and no
device command was run for this record. AI-assisted work; every claim below
names what was actually observed.

## Result in one paragraph

A new freestanding module, [`tools/p1-penang-platform`](../../tools/p1-penang-platform/README.md),
resolves the interrupt controller, timer, CPU and console resources from a
run-time FDT and implements a GICv3 driver for the position XNU would occupy
on the phone: non-secure EL1, Group 1 only, timer as IRQ. It is `IMPLEMENTED`,
`BUILDS` for ARM64 and `TESTED` on a host. Reading the pinned public XNU
source showed that the existing QEMU contract (Group 0, FIQ, PPI 27) cannot be
carried to this SoC, which makes a kernel-side IRQ timer adapter the first
missing provider. Separately, the T72 dynamic-userland host build was
reproduced in a clean cloud host and produced Mach-O images **byte-identical**
to the ones recorded for the Q5 QEMU run. All ten physical gates remain OPEN.

## Source-grounded finding: the timer cannot arrive as FIQ on this SoC

Observed in XNU 11417.140.69 at `43a90889846e00bfb5cf1d255cdc0a701a1e05a4`:

- `pexpert/arm/pe_fiq.c` (`HAS_GIC_V3`, set only by `VMAPPLE.h`) writes
  `GICR_IGROUPR0 = 0x81FFFFFF`, enables PPI 27, sets `GICD_CTLR_ENABLEGRP0`
  and `ICC_IGRPEN0_EL1`.
- `osfmk/arm64/sleh.c` `sleh_fiq` acknowledges through `ICC_IAR0_EL1` and
  completes through `ICC_EOIR0_EL1`, then calls `rtclock_intr`.
- `osfmk/arm64/machine_routines.c` programs `CNTV_*` and masks `CNTP_CTL_EL0`.

So upstream's only GICv3 contract is: one security state, Group 0, virtual
timer on PPI 27, delivered as FIQ. The P0.12 QEMU_VIRT provider is built on
the same contract and its own source says so ("QEMU virt has one security
state (DS=1). Do not guess the secure GIC model").

For the phone:

- **Observed (offline tree snapshots):** `arm,gic-v3` with one redistributor
  region and a 128 KiB stride; `arm,armv8-timer` with PPIs 1, 2, 3, 0, i.e.
  INTIDs 17, 18, **19 (virtual)**, 16; `clock-frequency` 19.2 MHz; PSCI over
  SMC; eight CPU nodes. The resolver in this change returned exactly these
  values from a privately held tree snapshot. That snapshot is not the FDT of
  the Stage-0 handoff.
- **INFERRED, not measured:** with a secure monitor and a vendor hypervisor
  present, non-secure EL1 sees `GICD_CTLR.DS == 0`; Group 0 state is RAZ/WI
  and `ICC_*0_EL1` is not usable there (Arm IHI 0069). The timer PPI can then
  only be a Group 1 non-secure interrupt, taken on the IRQ vector.

The driver does not depend on the inference: it reads `DS`, never writes
Group 0, and reports an interrupt whose enable does not stick as `NOT_OWNED`.
Measuring `DS` on the device is step 1 of the test plan below.

## What was implemented and tested

| Item | State | Evidence |
| --- | --- | --- |
| FDT resource resolver | IMPLEMENTED, TESTED (HOST) | translation through `ranges`, property-order independence, 24 refusal variants, every structure word of a fixture corrupted three ways |
| GICv3 non-secure Group 1 driver | IMPLEMENTED, TESTED (HOST model) | bring-up, stale-enable clearing, timer PPI delivery and single completion, secure-owned refusal, SPI route/trigger, self-SGI, `DS == 1` variant, 13 bring-up refusals |
| Timer admission | IMPLEMENTED, TESTED (HOST) | zero and mismatching `CNTFRQ_EL0` refused |
| GENI UART polled transmit | IMPLEMENTED, TESTED (HOST model) | read-only probe, bounded transmit, self-disable after timeout |
| ARM64 freestanding objects | BUILDS | `aarch64-none-elf` and `arm64-apple-darwin`, no undefined symbols |
| Kernel adapter, QEMU run, device run | NOT_TESTED | not written / not authorized |

Commands and results on the cloud host (Ubuntu clang 18.1.3, GCC 13.3.0):

```text
python3 -B -m unittest tests/public/test_p1_penang_platform.py   -> 16 tests, OK
same suite built with GCC -fsanitize=address,undefined            -> 16 tests, OK
```

A mutation pass (17 hand-made faults in the three C files) was caught by the
suite except for one equivalent mutant (`GICR_TYPER.Last` ignored while the
walk is already bounded by the tree's region size).

The "HOST model" is a register model written for these tests from the
architecture specification. It is my reading of the specification, not
silicon and not QEMU.

## Proposed shared early-ABI contract (needs review by both agents)

Nothing here edits another owner's files. It is a proposal for the one
contract the coordination rules require before cross-directory changes.

| Topic | Proposal | Owner today |
| --- | --- | --- |
| `boot_args` | unchanged: `struct m12_boot_args`, 1152 bytes, in `tools/m12-boot-bridge/bridge_contract.h` | bridge |
| Run-time FDT | one retained, immutable mapping, described by the existing `struct p1_runtime_fdt`; the same bytes feed `p1_memory_admit` and `ppp_resolve` | native build chain / bridge |
| Memory ownership | unchanged: `p1_memory_admit` decides; this module never interprets `memory` or `reserved-memory` | native build chain |
| Interrupt controller and timer | `struct ppp_resources` + `struct ppp_io`; Group 1 non-secure only; virtual-timer INTID and frequency come from the tree and `CNTFRQ_EL0` | this module |
| Console | framebuffer marker first; GENI UART only as an optional candidate after abort recovery exists | this module / bridge |
| Entropy | unchanged fail-closed gate; see mismatch below | bridge |

Three points for that review, each backed by existing evidence:

1. **The bridge's device-tree output cannot describe the phone yet.** The
   published `bridge_core.c` emits one fixed 128 KiB redistributor frame, an
   `arm,pl011` console node and no timer interrupt. A consumer of that output
   cannot learn the virtual-timer INTID or the redistributor stride. Either the
   bridge forwards them, or the kernel adapter reads the retained FDT with
   `ppp_resolve`. The second option needs no new property invention.
2. **Entropy size mismatch.** The Stage-0 device result recorded a 512-byte
   `rng-seed`; the bridge requires exactly 64 bytes and otherwise returns
   `M12_ERR_ENTROPY_UNAVAILABLE`. As written, a physical handoff with the
   observed seed stops there before the trust question is even reached.
   Condensing 512 bytes to 64 is a security decision (which function, what
   provenance claim) and is not made here.
3. **Size-only reservations.** The eight nodes with `size` and no `reg` are,
   per the devicetree specification, requests for the operating system to
   allocate memory itself; the boot firmware assigns them no address, so a
   "run-time exact address" for them does not exist at handoff. Whether P1 may
   therefore leave them unallocated (no P1 consumer, no secure-world hand-over
   of those pools) instead of treating them as unresolved is a memory-admission
   decision for its owner. I am recording the specification reading, not
   changing `P1_MEMORY_UNRESOLVED_RESERVATION`.

## First real missing providers, in dependency order

1. **Kernel IRQ timer adapter.** A board configuration that does not define
   `HAS_GIC_V3`/`APPLEVIRTUALPLATFORM` FIQ handling, supplies `struct ppp_io`
   (mapped distributor and boot redistributor frame, `MRS`/`MSR` + `ISB`), and
   routes `sleh_irq` to `ppp_gic_dispatch`, calling `rtclock_intr` for the
   virtual-timer INTID and IOKit vectors otherwise. It touches native XNU
   files, so it needs the joint contract above. Smallest next diff: the
   adapter as a source delta plus a compile probe against the pinned tree.
2. **Retained FDT provider.** Already identified as missing in the entrypoint;
   `ppp_resolve` adds a second consumer with the same requirement.
3. **Abort recovery** before any optional MMIO probe (UART, hardware RNG).
4. **Entropy supplier decision** (point 2 above).

## Device test plan for later, separate approval

Nothing below is authorized. Each step would need the owner's explicit
permission for that specific operation, a byte-pinned payload and the same
RAM-only method and post-boot partition comparison used for M11-D3A1.

| Step | Action | Writes to hardware | Pass evidence | Rollback |
| --- | --- | --- | --- | --- |
| 1 | Stage-0 extension: read `GICD_CTLR`, `GICD_TYPER`, `GICD_PIDR2`, boot `GICR_TYPER`, `CNTFRQ_EL0`, `MPIDR_EL1`; show values with the existing screen marker | none | values photographed; `DS`, line count, affinity recorded | RAM-only boot; power cycle returns to Android; partition hashes compared |
| 2 | Same, plus read `ICC_SRE_EL1` with an installed synchronous-exception vector | none | value or a recorded trap | as step 1 |
| 3 | `ppp_gic_init`, self-SGI through `ICC_SGI1R_EL1`, one virtual-timer shot, both observed on the IRQ vector | GIC non-secure enables, `CNTV_*` | counters shown on screen | as step 1; GIC state does not persist across reset |
| 4 | Only after 1–3 and a linked kernel: first native entry | per that image's review | separate plan | separate plan |

Stop conditions: any unexpected exception class, `NOT_OWNED` on the timer PPI,
`RWP`/wake timeouts, or a marker that does not appear. No UART probe, SMC, HVC
or flash is part of this plan.

## Userland track (B)

**Reproduced (HOST_ONLY, cloud):** the owner's untracked T72 host pipeline
(`build_host.py`: two independent Clang/LLD builds, structural verification,
rebase tests plain and under ASan/UBSan) ran unmodified: 55 commands, no
nonzero exit, 83 passing rebase checks in each variant, both builds
byte-identical. The resulting images hash to
`90bf1b73b1476603a28b9c10dca079012c2e151a05316710a6e5f3def74b36e8` (main) and
`6e770d2ecf0703fab61602aabcea9fce1172deddfd066262b14dd798d124912a` (dynamic
loader) — the same values the [Q5 record](2026-11-15-q5-el0.md) lists for the
images that ran in QEMU. This settles the byte identity that earlier cloud
checks could not test. It does not re-run Q5 and proves nothing about the
runtime.

**Not done:** ROMFS image rebuild, any kernel build, any QEMU run, signature
admission. `TRUST_ADMISSION` stays OPEN: the images carry an ad-hoc signature
("integrity only" in the pipeline's own words) and Q5 ran with process
enforcement off. The uncommitted ROMFS VFS change that adds read-only `mmap`
and UBC page-in is the right mechanism for executable mapping, but it has only
the Q5 run behind it and no negative tests of its own in this packet.

**Publication status of the untracked sources.** These 20 files exist only in
the owner's working tree at research HEAD
`5b665caca568bb243e9c5def48f378744cd17bd0`. They carry no SPDX line and no
recorded license decision, and the packet they arrived in forbids committing
them directly. They are therefore **not imported** by this change. Listed for
the owner's admission decision (proposed: Apache-2.0 as project-authored
research tooling, with the ROMFS adapter keeping the MIT line of the file it
modifies):

| Source path in the research tree | Bytes | SHA-256 |
| --- | --- | --- |
| `tools/p16-root-media/src/p16_romfs_vfs.c` (modified) | 13024 | `7cb828749d5137cd757ddeca7f4ca3de8d3e4b64d0e71a0848b948e4b38cb6a2` |
| `tools/t72-dynamic-userland/audit_physical.py` | 7041 | `c498d433ed07a7339cd36f8ccb390f44ad1fcf53b4f22dd063a27b63ab392d7b` |
| `tools/t72-dynamic-userland/audit_runtime.py` | 7522 | `63958136e35a91b6ddab48a455ed7d1b33396b475f7799bfb19ae6b4160f152a` |
| `tools/t72-dynamic-userland/build_host.py` | 5619 | `bfa5c8802bf27c64750809b7a08d47521ac30aad680a6072632042e5f3e4ff32` |
| `tools/t72-dynamic-userland/build_romfs.py` | 3143 | `ad5f5c24ce78e906cefb3f582732adea95e1bb2ed650c6a71a8cd8ae6c28de9a` |
| `tools/t72-dynamic-userland/dyld.c` | 9345 | `6d65d1c3bafb1b902cf090e6854000523fac5096676eea6a042a9c76e5545782` |
| `tools/t72-dynamic-userland/dyld.h` | 1414 | `26b3c2f92ec3793bec33f6ec6076f8e72fe50f0e517fa39c01bc7631a5f105a7` |
| `tools/t72-dynamic-userland/dyld.lds` | 362 | `87b1865f3d9475a9fcf568bd6178a6d3d85389d7a8e9977f0349064a948e63f6` |
| `tools/t72-dynamic-userland/dyld_entry.S` | 751 | `ed4ac9d6ce6d210fe00a8c85bbfbd8130d7e34a0a6f52c42977df0b1c49795be` |
| `tools/t72-dynamic-userland/emit_macho.py` | 3462 | `50e69503f3425cdc35f4f470d57e0f0ae5e3863b0e0a035f2d0ebff2b08b0c1b` |
| `tools/t72-dynamic-userland/fixture.S` | 1107 | `14d822b4e67a873d292ae2bb9b21585bde0126ab3f15690b3989d0ec9ba6f7af` |
| `tools/t72-dynamic-userland/fixture.lds` | 467 | `ad3e1141609cfdfa3d951097c543364926dc8af63ccd522c297fab036401e7c4` |
| `tools/t72-dynamic-userland/inspect_device.py` | 7142 | `cf162f0c06a2beac4c233cfd1038d141870e61a258ce4c8a471481d6765aa745` |
| `tools/t72-dynamic-userland/policy_observer.py` | 3474 | `bc8b70b30a94861c0ec459b10b1c9aa7f4bf8a89fcc2b874fd57a786d1c87e60` |
| `tools/t72-dynamic-userland/prepare_transport.py` | 3713 | `b80aa10301e477b4611183daae4d765daa367ad6ad3ebd3b77bbfbc51613e039` |
| `tools/t72-dynamic-userland/qualify_romfs.py` | 3343 | `2882256cc1c101797c0a7cdbc77f63a8930e7644209e17e592bc59cefad17271` |
| `tools/t72-dynamic-userland/test_macho.py` | 3303 | `999249ef7ea6592155fae74b8fd023fee29f1a78a247b763b371836905756238` |
| `tools/t72-dynamic-userland/test_rebase.c` | 7710 | `04281e0b441e7f15764ffe1667f7a8e6514643a6c58b73b3dd90bccf1b6ba101` |
| `tools/t72-dynamic-userland/test_romfs.c` | 2548 | `1fd30bd73538da1b37161d68746e39aa8e950fc03a96784a079147435540fcdf` |
| `tools/t72-dynamic-userland/verify_macho.py` | 6496 | `dc848d85f8653f13e0e03c50a3b43b2057721ca1cad416af9243f3ec9153cf10` |

Next userland step once the owner admits those sources: import them with this
table as provenance, add negative tests for the page-in path, and prepare (not
run) a QEMU case in which process enforcement is on and an unsigned or
tampered image is rejected. A project `/sbin/penang-init` remains a research
fixture; none of this is Apple's dynamic loader or launch daemon, and nothing
here establishes compatibility with stock iPadOS applications.

## Scoped driver backlog (C)

None of these is a prerequisite for first kernel entry or early BSD, and none
has XNU driver evidence. Every row first needs the common resource services
that do not exist yet for this SoC under XNU: clock controller, regulators
over the PMIC bus, pin control, interconnect/bus voting and IOMMU.

| Area | Prerequisites beyond the common services | First proof to aim for | Main risk |
| --- | --- | --- | --- |
| SMP | PSCI `CPU_ON` over SMC, per-CPU redistributor wake, IPIs as Group 1 SGIs | second core reaches the idle loop | vendor hypervisor/monitor policy for the caller |
| Storage | host-controller and PHY bring-up, DMA through the IOMMU | read-only block device, one sector verified | DMA isolation (`DMA_ENFORCEMENT` gate) |
| USB | controller, PHY, VBUS/role handling | device-mode enumeration | PHY sequencing, power |
| Display | adopt the boot framebuffer first, then mode setting | stable console on the inherited framebuffer | panel/PHY sequencing |
| GPU | display path, GPU clocks/power, firmware loading policy, command submission, the AIR2IR3 compiler | one triangle with verified pixels | firmware provenance, memory safety of submission |
| Audio | DSP remote-processor boot, codec, shared-memory transport | one tone on one output | firmware provenance, secure-world interfaces |
| Camera | sensor power/clock, CSI receiver, ISP | one raw frame | scale; secure-world interfaces |
| Wi-Fi / Bluetooth | remote-processor or combo-chip firmware, shared-memory IPC, regulatory data | scan result / HCI version | firmware provenance, regulatory |

Firmware named in this table is never shipped by the project; how a user
would supply it is an open policy question, not an implementation detail.
