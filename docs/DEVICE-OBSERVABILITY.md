# Device observability — Moto G53 (penang)

Gates follow ADR-0005 (observability without writing partitions). To avoid confusion with the ADR-0006 delivery criteria D1–D4, its operational gates are referred to here as **OBS-0 … OBS-6**.

| Gate | Meaning | State |
|---|---|---|
| OBS-0 | Read-only inventory over adb/fastboot | Done (2026-10-28) |
| OBS-1 (ADR-0005 "D1") | `fastboot boot` of the device's own boot image, RAM only | **Done (2026-11-02)** |
| OBS-2 … OBS-6 | Own payloads: display markers, USB log, return path, synthetic core, XNU first marker | Not run; each needs a separate owner approval |

## OBS-1 result (pinned record 6838eaab4fd5ceefa9b9fd3f372500f2d393f969)

- The image was a byte-identical copy of the currently flashed `boot_a`, which carries the owner's root patch. It is **not** a factory image.
- The Motorola bootloader accepted `fastboot boot`; Android booted with kernel arguments identical to a normal boot.
- Partitions and the active slot were unchanged afterwards (verified by read-only hashing on the device).
- Android pstore records written before the reboot survived the reboot → fastboot → RAM-boot path, which makes the ramoops region a strong candidate for a cross-boot log channel. Whether content written by project code survives is not yet tested.
- USB did not enumerate until the screen was unlocked by the owner.

## Still unknown

- 16 KiB translation granule support (TGRAN16) on these Kryo cores.
- Whether the watchdog is armed when the bootloader hands off.

Both need project code running at EL1 (OBS-2/OBS-3). XNU has not run on the device.

## Published data policy

No boot image, raw device tree, pstore content, serial number or partition hash is published. Raw records stay in the owner's private evidence store.
