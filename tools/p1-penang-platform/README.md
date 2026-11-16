# P1 physical platform services (Penang, SM4350)

**Scope: HOST_ONLY, plus QEMU_VERIFIED for the GICv3/timer files on QEMU
`virt`.** Freestanding C11 for the first native kernel entry on the Moto G53
(`penang`): run-time resource resolution, a GICv3 driver for the non-secure
world, architectural-timer admission, the retained run-time FDT provider and a
polled debug-UART candidate. It has been compiled for ARM64, exercised against
self-authored device trees and a register model on a host, and the interrupt
path has run bare-metal at EL1 under QEMU. It has **not** been linked into a
kernel or run on the phone. `GIC_TIMER_PHYSICAL` stays OPEN.

Background and the cross-agent contract proposal:
[P1 physical platform services record](../../docs/research/2026-11-16-p1-physical-platform-services.md)
and [P1 ABI closure record](../../docs/research/2026-11-16-p1-abi-closure.md).

## Why this is not the QEMU provider with new addresses

The P0.11/P0.12 QEMU_VIRT providers and upstream XNU's own GICv3 path
(`pexpert/arm/pe_fiq.c`, used by the VMAPPLE board) assume a single security
state: they enable **Group 0**, move PPIs 25–30 into it and take the timer as
**FIQ on PPI 27** through `ICC_IAR0_EL1`. XNU on this phone would run at
non-secure EL1 under a vendor hypervisor and TrustZone. In that position
Group 0 belongs to the secure world, so:

| | QEMU_VIRT / upstream VMAPPLE path | This module |
| --- | --- | --- |
| Security state assumed | one (`GICD_CTLR.DS == 1`) | two (`DS == 0`); `DS == 1` also handled |
| Timer exception | FIQ, Group 0 | IRQ, Group 1 non-secure |
| Timer INTID | 27, compiled in | virtual-timer PPI read from the tree (19 on holi-shaped trees) |
| Group / `IGRPMODR` writes | yes | never when `DS == 0` |
| Addresses | compiled in | resolved from the run-time FDT through `ranges` |
| Interrupt not owned by this world | not considered | enable/route read back; `NOT_OWNED`, fail closed |

Whether the phone really presents `DS == 0` to EL1 has not been measured; the
driver reads the bit and records it instead of assuming.

## Files

| File | Content |
| --- | --- |
| `ppp_platform.h` | Public interface, `struct ppp_io` register-access provider, status codes |
| `ppp_resources.c` | Bounded FDT v17 walker: GIC, timer PPIs, CPU count, PSCI conduit, console candidate |
| `ppp_gicv3.c` | Distributor/redistributor/CPU-interface bring-up, configure/enable, ack/EOI/dispatch, self-SGI |
| `ppp_timer_uart.c` | `CNTFRQ_EL0` admission against the tree; GENI serial-engine polled transmit |
| `ppp_gicv3_regs.h` | Architectural GICv3 register map (Arm IHI 0069); selects no board, so nothing needs XNU's `VMAPPLE.h` for an offset |
| `ppp_runtime_fdt.{h,c}` | Defines the `penang_p1_runtime_fdt` object the native VM bootstrap links against; zeroed until a single validated bind |
| `kdk/` | LOCAL_ONLY adapter for the owner-held Apple KDK private headers (`verify`, `find`, `compile`) and their hash-only pins; contains no Apple content |
| `qemu/` | Bare-metal EL1 harness that runs the unmodified driver on QEMU `virt`; a test fixture, not a boot path |
| `xnu-interfaces/CodeSignature/Entitlements.h` | **Candidate**, not on any include path: the four entitlement-key macros two XNU files need, bound to publicly evidenced keys pending an owner decision |

No file contains a physical address, libc dependency, allocation or global
MMIO access. The kernel adapter supplies `struct ppp_io` (mapping, barriers,
system-register instructions); that adapter does not exist yet and is the
first missing provider.

## Behaviour that is deliberately fail-closed

- A bus without `ranges`, an untranslatable `reg`, duplicated properties, more
  than one GIC or timer, `interrupts-extended`, a redistributor region too
  small for the described CPUs: resolution fails and returns zeroed resources.
- Affinity routing off with a group already enabled, a stuck `RWP`, a
  redistributor that does not wake, a system-register interface not granted by
  the higher exception level, an `EOImode` that cannot be cleared: bring-up
  fails and the controller stays not ready.
- An enable bit or SPI route that does not read back is reported as
  `NOT_OWNED`; delivery is never assumed.
- A failed bring-up leaves `ICC_IGRPEN1_EL1 = 0` and `ICC_PMR_EL1 = 0`, so the
  CPU cannot take an interrupt through a half-configured controller.
- The FDT provider publishes nothing unless the copy is 8-byte aligned, lies
  wholly in the retained prefix, is a version-17 tree and its `totalsize`
  equals the handed-over length; it binds once and never rebinds.
- `CNTFRQ_EL0 == 0` or a mismatch with the tree's `clock-frequency` is refused;
  no frequency is invented or written.
- Zero or several console candidates select none. The UART probe is read-only
  and refuses an engine that is not UART firmware or has its clock disabled.
  After one transmit timeout the port is disabled.

## Known limits

- One CPU. Every operation checks it runs on the boot CPU. No SMP, no LPIs,
  no ITS, no power management, no receive path.
- The UART is a *candidate*: the stock boot chain passes `console=null`, the
  pins may not be reachable on a retail unit, and reading an unclocked engine
  can raise an external abort. It must not be probed on hardware before a
  synchronous-abort recovery path exists. The framebuffer marker remains the
  only DEVICE_VERIFIED diagnostic channel.
- The register model in the host tests encodes one reading of Arm IHI 0069.
  The QEMU run checks that reading against an independent GICv3 model in both
  security configurations; neither is the SM4350.
- Nothing calls `ppp_runtime_fdt_bind()` yet: the bridge does not retain the
  firmware tree or convey its address, and the provider checks structure and
  placement, not content integrity.

## Reproduce

```bash
python3 -B -m unittest tests/public/test_p1_penang_platform.py -v
```

Opt-in emulator run (needs `qemu-system-aarch64`, `ld.lld`, `llvm-objcopy`;
skipped unless explicitly allowed, because starting an emulator requires the
owner's permission in this project):

```bash
PENANG_ALLOW_QEMU=1 python3 -B -m unittest tests/public/test_p1_penang_platform_qemu.py -v
```

Optional sanitizer run (the host needs GCC's libasan):

```bash
PPP_TEST_HOST_CC=gcc PPP_TEST_CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" LD_PRELOAD="$(gcc -print-file-name=libasan.so)" ASAN_OPTIONS=detect_leaks=0 python3 -B -m unittest tests/public/test_p1_penang_platform.py
```

Register interfaces were taken from public specifications and public source:
Arm IHI 0069 (GICv3), the devicetree specification v0.4, XNU 11417.140.69
(`43a90889846e00bfb5cf1d255cdc0a701a1e05a4`) for the consumer contract, and
Linux v6.6 `include/linux/soc/qcom/geni-se.h` and
`drivers/tty/serial/qcom_geni_serial.c` for GENI register offsets. No source
was copied from any of them. License: Apache-2.0.
