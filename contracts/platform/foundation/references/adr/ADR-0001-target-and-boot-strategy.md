# ADR-0001: iOS 16.5 + HackiOS / QEMU-first, penang-specific port
Status: ACCEPTED FOR HOST RESEARCH; device boot NOT authorized by this decision. Product roadmap priority amended by ADR-0002 (iOS18 research goal; iOS16.5 remains QEMU bring-up baseline).
Date: 2026-10-24.

## Context
- Device: Motorola Moto G53 5G / penang (Snapdragon 480+, Adreno 619, physical 4GiB variant).
- MemTotal from Android kernel: 3,548,204kB, not a 6GiB or 8GiB variant.
- HackiOS iOS16.5 targets redwood (Snapdragon 778G) and qemu-virt; current on-device RAM image approximately 2.5GB.
- ios-topaz iOS16.7.16 is valuable for older Qualcomm/Adreno 610, but writes to Android userdata and is rejected as our baseline.

## Decision
1. Choose iOS 16.5 build 20F66, HackiOS XNU 8796.121.2 and the matching userland as reference.
2. Prioritize the unmodified qemu-virt target on Linux first. Make no phone-specific modifications in the upstream clone.
3. Use Android 14 stock Motorola images (own device firmware) to describe a new penang BSP; preserve actual device-tree data, not speculative MMIO addresses.
4. Start with bootloader logs and XNU early text without cellular/Wi-Fi/GPU. GPU acceleration, SpringBoard, sound and other peripherals are later.
5. Verify the active Device Tree, GIC and physical addressing, PSCI calls, DMA/coherency, page granules, display allocation, thermal/power and USB.
6. No flashing and no modification of userdata/metadata. An alternate storage strategy or image-size reduction is required if the stock RAM image cannot fit safely.
7. Use images from legal sources only. Do not redistribute Apple-proprietary userland, device private keys or Motorola firmware.

## Known unknowns
- Bootloader capability to boot unsigned custom images temporarily (fastboot boot) on exact G53 firmware.
- Whether all CPU clusters support 16KiB translation granules required for this iOS kernel.
- RAM actually available to the loader after Android is no longer resident.
- Whether the active FDT agrees with the stock vendor_boot + overlay set.
- Suitable working UEFI (Mu-Silicium) port for Qualcomm holi/SM4350; redwood UEFI cannot be assumed compatible.
- Real input/display/gpu subsystem details and licensing status of reused code.

## Acceptance metrics
- QEMU logs show XNU kernel and nontrivial userland boots reproducibly.
- Penang platform profile derived from verified device tree / firmware; no TODO critical memory/MMIO fields.
- A loader session with the device must display logs on screen/USB and return safely to Android before further stages.
- No changes to verified Audio Evolution backups or the running Android installation.
