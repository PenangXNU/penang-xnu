# Roadmap

Stages are ordered by dependency; each has a gate with evidence. No dates are promised. The delivery criteria D1–D4 come from ADR-0006; the guiding principle (unmodified Apple binaries, adaptation below the OS, BYO IPSW) from [ADR-0007](docs/adr/ADR-0007-original-experience.md).

## Done (research records, see [STATUS.md](STATUS.md))

- XNU 11417.140.69 DEV build; QEMU_VIRT entry (P0.5); kernel PRNG (P0.6); unified crypto (P0.8/P0.9).
- Research security provider with its own trust root (P0.10, B3); research Platform Expert (P0.11, B4).
- Moto G53: read-only inventory (OBS-0) and RAM boot of the device's own image (OBS-1).
- Public contracts, validator and generated status (this repository, v0.1).

## A — Kernel in QEMU up to a first process

1. **B5:** GICv3 interrupt-controller driver as the child of `PassthruInterruptController`, then a minimal `IOPMGR` (processor registration, `ml_set_max_cpus`) → `bsd_init`.
2. RTC, an explicit NVRAM decision, ramdisk root and `mountroot`.
3. A first own `init` signed by the research root: exec, Mach IPC, syscalls.
4. SMP through PSCI.
5. v0.2: publish the kernel research code (providers, Platform Expert, APSL patches, QEMU harness) after its own audit.

## B — iPadOS userland in QEMU, headless (BYO IPSW)

Each item needs an ADR before code: trust policy for the user's own Apple binaries; entitlements (OSEntitlements/CoreEntitlements); root filesystem and cryptex (APFS); data protection without SEP; the IOKit user-client interfaces the userland expects (IOSurface, display, HID, keystore). Gate: original `launchd` and essential daemons stable.

## C — Moto G53 hardware

Own payload through `fastboot boot` measuring TGRAN16, watchdog and ramoops (OBS-2/OBS-3); holi backend (GICv3 shared with QEMU, timer, PSCI on 8 heterogeneous cores, watchdog, splash framebuffer, USB logging, UFS, touch, PMIC/thermal). Gate: XNU on the device with a safe return to Android (ADR-0006 D1).

## D — Graphics and the full experience

Metal on Adreno 619 (driver, command submission, composition); then SpringBoard (D2), Metal with measured GPU evidence (D3) and touch with safe return to Android (D4). This is the longest stage.

## Organization

- Independent review of v0.1, then public visibility.
- Sofiar (second Motorola) as a headless generality check (P1).
- Legal review before any tooling automates use of Apple software on non-Apple hardware.
