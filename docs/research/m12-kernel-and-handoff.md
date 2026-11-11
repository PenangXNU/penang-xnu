# P0.16 / M11 / M12 — kernel and handoff status, 2026-11-11

These are scoped conclusions from pinned local reports and canonical state validation, not a public CI replay. No physical XNU kernel execution is claimed.

| Checkpoint | Observation | Provenance |
| --- | --- | --- |
| P0.16 (aad24381a428bae74db0e4a1ef6675a3d181d1ae) | QEMU_VIRT XNU mounted an actual md0 ROMFS via original VFS, installed root vnode, then attempted /sbin/launchd and received ENOENT (2) | QEMU_VERIFIED |
| M11-D3A1 | Moto G53 executed an original Stage-0 probe at EL1, observed TGran16 and 64-byte I/D cache lines, FDT header and metadata, then returned to Android | DEVICE_VERIFIED for Stage-0 only |
| M12-T0 | QEMU Mach-O bridge dispatched original P0.16 __start and _arm_init instructions | QEMU_VERIFIED, synthetic handoff |
| M12-T1 | Nine diagnostic ARM64 Mach-O relocatable objects compiled, with PMAP/VM guards retained | HOST_ONLY, no native full link |
| M12-T2 | Eight pinned DTB dynamic reservation requests placed deterministically in host model; isolated one-vCPU 16-KiB Stage-1 transitions exercised in QEMU | HOST_ONLY / QEMU_VERIFIED |
| M12-T3 | Probe counted three RAM banks, 21 static and eight dynamic reservations; eight host sanitized scenarios and six QEMU scenarios passed | QEMU_VERIFIED, physical candidate HOLD |

The T3 geometry hash is a5a59e923097fb998887e848474475118b0072f7a7c5a5062a220782b23cf8cd. Local canonical validation at the T3 checkpoint passed 23 gate descriptions, 18 evidence sources and nine rejected mutations. This verifies ledger integrity, not passage of every gate.

## Independent blockers

- FDT_PHYSICAL_HANDOFF: BLOCKED. The live handoff identity, ownership, final reservation placement and permitted XNU window remain unproven.
- NATIVE_VM_PMAP_COMPILE: BLOCKED. Missing qualified provider for physical PMAP/lockdown and private support interfaces.
- NATIVE_XNU_LINK: NOT_RUN. No complete G53-native executable.
- ENTROPY_PHYSICAL: BLOCKED. A measured 512-byte seed-property length does not prove origin/freshness/entropy.
- GIC_TIMER_PHYSICAL: BLOCKED. QEMU driver validation does not qualify G53 IRQ and timer operation.
- XNU_DEVICE_ENTRY: NOT_RUN. Historical Stage-0 execution must not be relabelled as XNU execution.
- QEMU_LAUNCHD: BLOCKED. P0.16 ROMFS deliberately does not support executable mapping; the absence of launchd is not a mere path typo.

## Next experiments, without duplication

The separately active M12-T4 work is to qualify a bounded Stage-0 diagnostic image for the exact ABL/slot and provide physical recovery gates, not to flash or execute XNU automatically. An independent QEMU user-process bring-up may study Mach-O exec/mmap before Apple userland integration. Both depend on reusing existing P0.16 and M12 contracts.

Read [Qualcomm security boundary](qualcomm-memory-security.md) before treating a Stage-1 map as secure lockdown and [driver/userland matrix](drivers-and-userland.md) before expanding peripherals.
