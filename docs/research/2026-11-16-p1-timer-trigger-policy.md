# P1 timer interrupt flags and the Group 1 IRQ contract

**2026-11-16 · HOST_ONLY.** Follows the FDT P0 hardening ([pull request 23](https://github.com/PenangXNU/penang-xnu/pull/23)), independent of it in code,
as the work order asks: this PR defines and enforces the timer's device-tree
flag policy and records the IRQ contract. It installs no kernel adapter and
edits no native XNU file. AI-assisted.

## The problem

`ppp_timer_admit` used to set `edge = (flags & 3) != 0`. Mixed trigger bits
such as `0x5` therefore became "edge" silently. `0x0` became "level"
silently. Reserved bits were ignored.

## Policy now enforced

Applied to the third cell of **every** `arm,armv8-timer` specifier, not only
the virtual timer's:

| Bits | Value | Result |
| --- | --- | --- |
| [3:0] | `0x4` level-high | level |
| [3:0] | `0x8` level-low | level; `polarity_low = 1`. A GICv3 has no polarity control, so it programs the same as level-high. Holi trees say `0xff08` |
| [3:0] | `0x0` none | level, the architectural default for the generic timer, reported in `trigger_defaulted = 1`: explicit, not silent |
| [3:0] | `0x1`, `0x2` edge | **refused** (`PPP_TIMER_E_TRIGGER`). The generic timer output is level-sensitive (Arm ARM, generic timer chapter); an edge configuration can lose a re-assertion |
| [3:0] | several bits (`0x3`, `0x5`, `0x6`, `0x9`, `0xc`, `0xf`) | **refused** |
| [15:8] | any | GICv2 CPU-interface mask; meaningless to a GICv3. Recorded in `cpu_mask`, never used for routing |
| [7:4], [31:16] | non-zero | **refused** (reserved) |

A refusal leaves `frequency_hz` and `virtual_intid` at zero, so nothing is
half-admitted. `edge` stays in the struct for layout compatibility and is
always 0.

Evidence: 6 accepted and 12 refused flag values on the virtual timer, plus a
bad flag on each of the other three timers; plain and GCC ASan/UBSan. Three
mutations of the policy were each detected. The QEMU tests still pass: QEMU's
`virt` tree uses a flag value the policy accepts. No device value of
`GICD_CTLR.DS` or of the timer wiring was measured. The level-low reading
of `0xff08` is the binding's meaning, not an observation.

## Group 1 NS IRQ contract for the later adapter PR

Nothing below is implemented in the kernel yet; it is the contract the
adapter PR must meet, taken from XNU 11417 (`43a90889`).

| Step | Requirement |
| --- | --- |
| Order | `ppp_resolve` on the bound tree → map distributor and redistributor region → `ppp_gic_init` with `PSTATE.I` set → `ppp_timer_admit` → `ppp_gic_configure(intid, edge = 0)` → `ppp_gic_enable` → only then unmask IRQ |
| Handler | Installed with `ml_install_interrupt_handler` before the first unmask. `sleh_irq` (non-`USE_APPLEARMSMP`) calls `cdp->interrupt_handler(target, refCon, nub, source)` |
| Dispatch | The handler calls `ppp_gic_dispatch`, which owns the **single** acknowledge (`ICC_IAR1_EL1`) and the **single** EOI (`ICC_EOIR1_EL1`, EOImode 0). INTIDs 1020–1023 are never completed. No second EOI anywhere |
| Timer | For the admitted virtual-timer INTID: `cpu_decrementer = -1; rtclock_intr(TRUE)`, as `sleh_fiq` does today. `rtclock_intr` reprograms `CNTV_*`, which drops the level before the EOI |
| Barriers | Every system-register write is followed by `ISB` (the adapter's `struct ppp_io`). MMIO writes that must complete before a dependent access (RWP polling, enable read-back) are ordered by device-memory semantics. `DSB` precedes an EOI only when memory written by the handler must be visible first |
| Rate | `CNTFRQ_EL0`, adopted and compared with the tree, never written |
| Refusal | Any refusal keeps IRQ masked and panics before unmask. `ICC_IGRPEN1_EL1 = 0` and `ICC_PMR_EL1 = 0` after a failed GIC init (already enforced). No Group 0 or FIQ fallback |
| Scope | One CPU. IOKit's root interrupt controller must route to the same dispatch, so that IOKit re-installing the CPU handler does not bypass it |
