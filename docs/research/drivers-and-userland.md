# Drivers, bootstrap and userland — dependency matrix

The priorities below distinguish first kernel entry, post-entry BSD operation and a later iPadOS-compatible environment. They do not imply that driver implementations are absent from every local scratch tree; consult the reconciliation ledger first.

| Area | Current demonstrated scope | Next proof |
| --- | --- | --- |
| ABL → FDT/ADT bridge | QEMU entry plus real Stage-0 FDT metadata | Exact handoff and admissible load contract |
| VM/PMAP/lockdown | Isolated 16-KiB Stage-1 QEMU model | Physical backend with W^X, aliases and DMA isolation |
| XNU Mach-O | P0.16 real __start/_arm_init dispatched in QEMU; nine physical diagnostic objects | Full native compile/link with physical contract |
| GICv3/timer | QEMU interrupt/timer path | Physical GIC/timer driver and interrupts |
| Entropy/CCRNG | P0.15/P0.16 QEMU provider | Qualified physical origin and freshness |
| IOKit/BSD/VFS | P0.16 md0 + ROMFS mount + root vnode in QEMU | Physical PE bootstrap, matching, storage and runtime stability |
| User-process exec | launchd ENOENT; ROMFS does not support executable mmap | First independent ARM64 Mach-O user process under QEMU, then legitimate Apple userland |
| CPU SMP/PSCI | Eight cores in hardware description, QEMU single CPU qualification | Physical secondary-core bring-up and synchronization |
| USB/network/storage peripherals | Hardware inventory; no physical XNU driver qualification | Driver-specific resources, clocks, DMA and IRQ tests |
| Display/Adreno 619 | Stage-0 framebuffer and host-only AIR2IR3/Mesa research | Native GPU/display drivers, IOMMU and safe submission |
| Audio, sensors, camera, power | Hardware descriptions/research only | Independent drivers after core platform stability |

A complete graphical iPadOS environment additionally needs appropriate dynamic linker, system libraries, Apple userland files supplied by the owner, code signing, security services, filesystem and framework contracts. Do not label a successful root mount or a synthetic user process as launchd/iPadOS functional.

The first physical XNU kernel milestone does **not** require Wi-Fi, Bluetooth, GPU/Metal, camera or audio. It requires memory admission, PMAP strategy, correctly linked entry, basic diagnostic path and recovery. More drivers should not obscure these blockers.
