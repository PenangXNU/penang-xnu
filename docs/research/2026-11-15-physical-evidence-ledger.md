# Moto G53 — physical research evidence ledger

**2026-11-15 · historical reconciliation / public-safe summary.** This is an index of the original locally retained reports and commits; it is **not** a new hardware measurement, security attestation or preflight approval. Original raw logs, stock binaries, source worktrees and their hashes remain private. Supersession below changes interpretation, not the historical files.

## Completed investigation (do not restart as a generic survey)

| Track | Source lineage / scope | Evidence already recovered | Still unproved |
| --- | --- | --- | --- |
| Android/firmware inventory (M7–M8) | Project M3 hardware plan, historical Android read-only inventory; DEVICE_VERIFIED *Android*, HOST_ONLY ROM | Twenty hardware domains/driver bindings, stock ROM member mapping and extraction provenance. A later public-free inventory recorded hundreds of Linux bindings | Porting Android drivers to XNU; active bootloader memory policy |
| Motorola bootchain (M10, M11-R2C/R3) | M11 source disassembly and `M11-R2C-EVIDENCE-ERRATA.md`; HOST_ONLY | ABL/XBL, MotoBootModule, firmware handoff, display, PMIC/PON, watchdog control paths, HYP/Stage-2 mechanisms | Runtime values/grants/owners and reliable actual watchdog state |
| Physical Stage-0 (M11-D3A1) | Original branch commit `a4c8d0ae8457fb572593eba73dd0f73680fb5714`; DEVICE_VERIFIED | Owner's single RAM-only fastboot boot; own-code screen marker; EL1, supported 16-KiB translation granule; CTR cache 64 B; valid FDT magic; 512-byte `rng-seed` metadata; 21 fixed `reg` reserved-memory nodes, eight dynamic size-only nodes; 4/4 tracked post-boot partition hashes equal | XNU entry, actual ELF/Apple OS boot, raw FDT bytes and materialized allocations, seed quality |
| FDT/DRAM (B1.2/B1.4-H1) | `research/m12-b14-h1-gpt6-20261110` @ `d6f2f1d`; HOST_ONLY | Compared vendor_boot base and independent device-tree snapshot; three declared RAM banks, 21 static and eight dynamic reservations, 232 MiB dynamic requested, 16-KiB outward geometry. H1 host 79/79 tests | Exact FDT delivered to the D3A1 handoff, dynamic physical placement, allowed HLOS window/ownership |
| PMAP and Apple monitor dependencies (B4.2) | `research/m12-b42-pmap-lockdown-argon-20261107` @ `29e6aca`; HOST_ONLY | Fail-closed R01–R06 contract, guards in native `pmap.c` and `arm_vm_init.c`, 307 OSFMK paths and missing Apple AMCC sources identified | Qualcomm-backed RO/RX/write-mediated protection, aliases, DMA/SMMU and TLB/cache lifecycle |
| SCM/QHEE (B4.3A-R1/R2/R3) | `research/m12-b43a-r3-codex-20261110` @ `8ac4cda`; HOST_ONLY | Recovered pinned `ScmDxe`/HYP data, two transport forms; 90 EFI modules, 11 GUID references, nine demonstrated method flows; conditional **ABL** direct SMC after ExitBootServices and before handoff | XNU NS-EL1 caller/ABI/service authorization, error semantics and actual Stage-2/XPU grants |
| Entropy (B3, D3A1, P0.15) | Firmware RNG research + DEVICE_VERIFIED metadata + QEMU_VERIFIED provider | Qualcomm legacy QRNG and UEFI RNG candidate paths, boot seed metadata; CCRNG/HMAC_DRBG in QEMU | Physical entropy quality, freshness and trustworthy XNU bootstrap |
| GIC/CPU (P0.12, T6) | QEMU_VERIFIED SGI1; HOST_ONLY native objects | GICv3 IRQ handler/EOI observed in QEMU. T6 built 18 diagnostic Mach-O ARM64 objects and preserved the VM/PMAP guards | Native GIC/timer IRQ on SM4350; full physical kernel link |
| U2 Android identity | U2 2026-11-15; DEVICE_VERIFIED *Android* | ADB `device` from the actual G53, correct product/slot/release family, without changing device state | Runtime FDT/privileged memory; physical XNU execution |

### Recheck conducted 2026-11-15

Original **local** SHA256SUMS were rehashed without changing source artifacts: B4.3A-R3 **12/12**, B1.4-H1 **13/13**, T72-Q5 **42/42**. These checks attest to retained bytes, **not** to independent replication of all original tests. References include R3 manifest SHA-256 `e82ba5d96869d8e8cd64cc20668caf98ed3da507eb99bfec1f804b687ee3dfbb`, H1 manifest `ea32111691ff7fcbe7748c71326e563d4715f33085797fc6639485d129b1750c` and Q5 manifest `04d0b516f0fd6aa3cf3d7aa13aeb7346b80af5504497c7573e75914d461e8505`.

## Supersession and correction registry

| Historical wording | Correct later interpretation |
| --- | --- |
| M11-R2 claimed physical readiness and unconditional watchdog/display behavior | **M11-R2C errata and M11-R3 supersede those claims.** Static control paths do not establish selected backend, grants, timing or guaranteed recovery |
| No usable RAM geometry from vendor_boot baseline | B1.4-H1 found a distinct tree snapshot with three RAM banks; this does **not** make it the exact Stage-0 handoff tree |
| SCM hardware not investigated | R1–R3 mapped substantial firmware code and conditional post-EBS ABL SMC; the **XNU caller's authorized interface** remains unproved |
| Only a `rav` USB endpoint was seen | U2 subsequently identified the Moto G53 as `penang`; the `rav` Moto G8 is a separate device |
| M12 had not reached any ARM64 userland | T72-Q5 **QEMU_VERIFIED** EL0 fixture, dyld rebasing and pointer consumption; see [Q5](2026-11-15-q5-el0.md) |
| Physical PMAP `BLOCKED` means nothing has been done | B4.2 contains explicit, tested safety contracts; missing part is a real admitted backend, not an absent analysis |

## Original sources to inspect by name, not blindly replicate

- `M11-D3A1-DEVICE-RESULT-20261106.md` and its original owner-supplied picture/log receipts (private); M11-R2C errata; M11-R3 targeted findings.
- `M12-B1.4-H1-HANDOFF-20261110.md`, `M12-B4.2-PMAP-HANDOFF.md`, `M12-B4.3A-R3-HANDOFF.md` and `M12-B4.3A-R3-GATES.json`.
- `P0.12-HANDOFF.md`, `P0.16-HANDOFF.md`, T6 `AUDIT.json`, U2/P2 physical blocker matrix, T72-Q5 result and independent audit.
- Existing public imports and public-source hashes: [2026-11-11 reconciliation](2026-11-11-reconciliation.md), [M12 status](m12-kernel-and-handoff.md), [Qualcomm trust boundary](qualcomm-memory-security.md).

## Ten physical gates at latest P2 (none passed for native XNU)

| Gate | Research completed | Exact missing qualifying evidence |
| --- | --- | --- |
| FDT_PHYSICAL_HANDOFF | DT snapshots and reservation geometry | Runtime-exact FDT, dynamic placements and HLOS window |
| SCM_XNU_ABI | EFI/ABL SCM transport and callers | Authorized post-handoff NS-EL1 call/return contract for intended memory protection |
| STAGE2_GRANTS | QHEE writers and Stage-2 conditional geometry | Selected live owner/translation/grant for XNU memory |
| DMA_ENFORCEMENT | SMMU topology/DT declarations | Effective device streams, DMA write isolation and alias coverage |
| PMAP_PHYSICAL | B4.2 fail-closed contracts | Real protected transition and mediated-writes implementation with error handling |
| NATIVE_VM_PMAP_COMPILE | Source selection, guards and diagnostics | Complete native `pmap.c`/`arm_vm_init.c` with admitted providers |
| NATIVE_XNU_LINK | T6 diagnostic Mach-O objects | Full linked executable for PENANG_PHYSICAL with entry/layout audited |
| GIC_TIMER_PHYSICAL | GIC QEMU runtime and platform DT research | Native physical IRQ/timer delivery |
| ENTROPY_PHYSICAL | Firmware candidates + 512-byte Stage-0 metadata | Qualified fresh physical seed to the XNU consumer |
| XNU_DEVICE_ENTRY | Stage-0 one-shot + U2 Android identity | Native target, recovery, observability and independently authorized physical trial |

**No claim of a bootable XNU image, Apple security equivalence or authorized experiment follows from this ledger.** Prefer the minimal [CV1 convergence work order](../work-orders/WO-M12-CV1-EVIDENCE-CONVERGENCE.md) to further general hardware scouting.
