/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Penang XNU P1 physical platform services: shared declarations.
 *
 * Scope: Qualcomm SM4350 (holi) / Moto G53 (penang), one boot CPU, non-secure
 * EL1 below a vendor hypervisor and TrustZone. HOST_ONLY until linked into a
 * native kernel and observed on hardware; nothing here is DEVICE_VERIFIED.
 *
 * No MMIO or physical address is compiled in. Every resource is resolved from
 * the flattened device tree that the boot chain delivered at run time, and
 * every register access goes through struct ppp_io so the same source runs
 * against a host model and against real hardware.
 */
#ifndef PENANG_P1_PLATFORM_H
#define PENANG_P1_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPP_ABI_VERSION 1u

/* System registers used by these services. The adapter performs the access
 * and the context synchronization (ISB) the architecture requires. */
enum ppp_sysreg {
    PPP_SR_MPIDR_EL1 = 0,
    PPP_SR_ICC_SRE_EL1,
    PPP_SR_ICC_PMR_EL1,
    PPP_SR_ICC_BPR1_EL1,
    PPP_SR_ICC_CTLR_EL1,
    PPP_SR_ICC_IGRPEN1_EL1,
    PPP_SR_ICC_IAR1_EL1,
    PPP_SR_ICC_EOIR1_EL1,
    PPP_SR_ICC_SGI1R_EL1,
    PPP_SR_CNTFRQ_EL0,
    PPP_SR_COUNT
};

/*
 * Register access provider. `addr` is the physical address resolved from the
 * device tree; the adapter owns the physical-to-virtual mapping and refuses
 * addresses it did not map. All callbacks are mandatory.
 */
struct ppp_io {
    void *ctx;
    uint32_t (*read32)(void *ctx, uint64_t addr);
    void (*write32)(void *ctx, uint64_t addr, uint32_t value);
    uint64_t (*read64)(void *ctx, uint64_t addr);
    void (*write64)(void *ctx, uint64_t addr, uint64_t value);
    uint64_t (*sysreg_read)(void *ctx, uint32_t reg);
    void (*sysreg_write)(void *ctx, uint32_t reg, uint64_t value);
};

/* ---- Run-time resource resolution ------------------------------------- */

enum ppp_status {
    PPP_OK = 0,
    PPP_E_ARGS = 1,
    PPP_E_FDT = 2,              /* malformed, truncated or unsupported FDT */
    PPP_E_DEPTH = 3,            /* deeper than the bounded walker accepts */
    PPP_E_GIC_ABSENT = 4,
    PPP_E_GIC_DUPLICATE = 5,
    PPP_E_GIC_LAYOUT = 6,       /* reg/regions/stride/cells outside contract */
    PPP_E_TRANSLATE = 7,        /* reg not translatable to CPU addresses */
    PPP_E_TIMER_ABSENT = 8,
    PPP_E_TIMER_DUPLICATE = 9,
    PPP_E_TIMER_LAYOUT = 10,
    PPP_E_CPUS = 11,
    PPP_STATUS_COUNT = 12
};

#define PPP_TIMER_SECURE_PHYS 0u   /* device-tree order of arm,armv8-timer */
#define PPP_TIMER_NONSEC_PHYS 1u
#define PPP_TIMER_VIRTUAL     2u   /* the timer XNU programs (CNTV_*) */
#define PPP_TIMER_HYP_PHYS    3u
#define PPP_TIMER_MAX         4u

struct ppp_ppi {
    uint32_t intid;     /* 16..31 */
    uint32_t dt_flags;  /* third interrupt cell, unmodified */
};

struct ppp_resources {
    uint32_t abi_version;
    uint32_t cpu_count;          /* enabled device_type "cpu" nodes in /cpus */
    uint64_t gicd_pa;
    uint64_t gicd_size;
    uint64_t gicr_pa;
    uint64_t gicr_size;
    uint64_t gicr_stride;        /* per-CPU redistributor frame set */
    uint32_t timer_count;        /* 3 or 4 */
    uint32_t timer_dt_hz;        /* 0 when the tree gives no clock-frequency */
    struct ppp_ppi timer[PPP_TIMER_MAX];
    uint32_t psci_method;        /* 0 absent, 1 smc, 2 hvc */
    uint32_t console_candidates; /* enabled GENI console nodes seen */
    uint64_t console_pa;         /* valid only if console_candidates == 1 */
    uint64_t console_size;
    uint32_t console_intid;      /* 0 when not an SPI specifier */
    uint32_t reserved;
};

/* Resolve the interrupt controller, architectural timer, CPU count, PSCI
 * conduit and the debug-console candidate from a run-time FDT. Read-only;
 * never guesses a missing value. `out` is zeroed on any failure. */
int ppp_resolve(const void *fdt, size_t length, struct ppp_resources *out);
const char *ppp_status_name(int status);

/* ---- GICv3, non-secure Group 1 only ----------------------------------- */

enum ppp_gic_status {
    PPP_GIC_OK = 0,
    PPP_GIC_E_ARGS = 1,
    PPP_GIC_E_ARCH = 2,          /* not a GICv3/v4 distributor/redistributor */
    PPP_GIC_E_ROUTING = 3,       /* affinity routing unavailable or unsafe */
    PPP_GIC_E_RWP_TIMEOUT = 4,
    PPP_GIC_E_REDIST = 5,        /* no frame for this CPU inside the region */
    PPP_GIC_E_WAKE_TIMEOUT = 6,
    PPP_GIC_E_SRE = 7,           /* system-register interface not granted */
    PPP_GIC_E_CPUIF = 8,
    PPP_GIC_E_DIST_ENABLE = 9,
    PPP_GIC_E_STATE = 10,        /* not initialized, or already initialized */
    PPP_GIC_E_INTID = 11,
    PPP_GIC_E_WRONG_CPU = 12,
    PPP_GIC_E_NOT_OWNED = 13,    /* interrupt is not writable from this world */
    PPP_GIC_E_CONFIG = 14,
    PPP_GIC_STATUS_COUNT = 15
};

#define PPP_GIC_SPURIOUS      1023u
#define PPP_GIC_PRIORITY      0xa0u
#define PPP_GIC_POLL_DEFAULT  1000000u

struct ppp_gic {
    struct ppp_io io;
    uint64_t gicd;
    uint64_t gicr;            /* RD_base of the boot CPU's frame */
    uint64_t boot_mpidr;
    uint32_t intid_count;     /* SGI+PPI+SPI, capped at 1020 */
    uint32_t single_security; /* GICD_CTLR.DS as observed */
    uint32_t poll_limit;
    uint32_t ready;
    uint64_t ack_count;
    uint64_t eoi_count;
    uint64_t spurious_count;
};

/*
 * Bring up the distributor view, the calling CPU's redistributor and its CPU
 * interface for Group 1 non-secure interrupts. Precondition: IRQ masked
 * (PSTATE.I set) on the single boot CPU. Group 0 and the secure world's
 * configuration are never written. On failure nothing is left enabled by
 * this function and `gic->ready` stays 0.
 */
int ppp_gic_init(struct ppp_gic *gic, const struct ppp_resources *res,
                 const struct ppp_io *io, uint32_t poll_limit);
/* Disable, then set priority/trigger/route. `edge` is honoured for SPIs and
 * requested for PPIs (whose trigger may be fixed by the implementation). */
int ppp_gic_configure(struct ppp_gic *gic, uint32_t intid, int edge);
/* Fails with PPP_GIC_E_NOT_OWNED if the enable bit does not stick, which is
 * how a secure-owned (RAZ/WI) interrupt appears from non-secure EL1. */
int ppp_gic_enable(struct ppp_gic *gic, uint32_t intid);
int ppp_gic_disable(struct ppp_gic *gic, uint32_t intid);
/* Acknowledge one Group 1 interrupt; PPP_GIC_SPURIOUS if none or not ready. */
uint32_t ppp_gic_ack(struct ppp_gic *gic);
/* Priority drop and deactivation (EOImode == 0). Special INTIDs are ignored. */
void ppp_gic_eoi(struct ppp_gic *gic, uint32_t intid);
/* Hardware delivery self-check: SGI to the calling CPU through ICC_SGI1R. */
int ppp_gic_self_sgi(struct ppp_gic *gic, uint32_t intid);

typedef void (*ppp_irq_handler)(void *ctx, uint32_t intid);
/* Acknowledge, run `handler`, complete. Returns 1 if an interrupt was
 * handled, 0 if spurious. This is the IRQ-vector entry the kernel adapter
 * calls; the architectural timer arrives here, not on FIQ. */
int ppp_gic_dispatch(struct ppp_gic *gic, ppp_irq_handler handler, void *ctx);
const char *ppp_gic_status_name(int status);

/* ---- Architectural timer admission ------------------------------------ */

enum ppp_timer_status {
    PPP_TIMER_OK = 0,
    PPP_TIMER_E_ARGS = 1,
    PPP_TIMER_E_NO_VIRTUAL = 2,
    PPP_TIMER_E_FREQUENCY_ZERO = 3,   /* CNTFRQ_EL0 unprogrammed */
    PPP_TIMER_E_FREQUENCY_MISMATCH = 4,
    PPP_TIMER_E_TRIGGER = 5           /* a timer's DT flags are not a level trigger */
};

/*
 * Timer interrupt flags (third cell of each arm,armv8-timer specifier):
 *   bits [3:0]  trigger: 0x4 level-high and 0x8 level-low are accepted as
 *               level; 0x0 (none) takes the architectural default, level,
 *               and is reported in `trigger_defaulted`; edge (0x1, 0x2) and
 *               any combination of trigger bits (0x3, 0x5, 0xc, ...) are
 *               refused. The generic timer output is level-sensitive
 *               (Arm ARM D11), and a GICv3 has no polarity control, so
 *               level-low is recorded in `polarity_low` but programs the
 *               same as level-high.
 *   bits [15:8] GICv2 CPU-interface mask, meaningless to a GICv3: recorded
 *               in `cpu_mask`, never used for routing.
 *   bits [7:4], [31:16] reserved: must be zero.
 * Every timer specifier in the node is checked, not only the virtual one.
 */
#define PPP_DT_IRQ_TRIGGER   0x0000000fu
#define PPP_DT_IRQ_CPU_MASK  0x0000ff00u
#define PPP_DT_IRQ_RESERVED  0xffff00f0u

struct ppp_timer {
    uint64_t frequency_hz;   /* CNTFRQ_EL0 as read on the CPU */
    uint32_t virtual_intid;
    uint32_t edge;              /* always 0: the timer is admitted only as level */
    uint32_t polarity_low;      /* tree said level-low (0x8); no GICv3 effect */
    uint32_t trigger_defaulted; /* tree said 0x0; architectural level used */
    uint32_t cpu_mask;          /* GICv2 mask bits [15:8], informational */
};

/* The frequency is whatever firmware programmed; it is compared with the
 * tree when the tree states one and is never invented or written. */
int ppp_timer_admit(const struct ppp_resources *res, const struct ppp_io *io,
                    struct ppp_timer *out);

/* ---- Qualcomm GENI debug UART, polled transmit only -------------------- */

enum ppp_uart_status {
    PPP_UART_OK = 0,
    PPP_UART_E_ARGS = 1,
    PPP_UART_E_NOT_UART = 2,      /* serial engine firmware is not UART */
    PPP_UART_E_UNCLOCKED = 3,     /* engine clock not left enabled */
    PPP_UART_E_TIMEOUT = 4,
    PPP_UART_E_STATE = 5
};

struct ppp_uart {
    struct ppp_io io;
    uint64_t base;
    uint32_t poll_limit;
    uint32_t ready;
    uint64_t sent;
    uint64_t dropped;
};

/* Read-only identification. It does not program clocks, pins, baud rate or
 * packing: it only adopts an engine the boot chain already configured. */
int ppp_uart_probe(struct ppp_uart *uart, const struct ppp_resources *res,
                   const struct ppp_io *io, uint32_t poll_limit);
/* Bounded. After a timeout the port is marked not ready so a dead console
 * cannot stall the boot path. */
int ppp_uart_putc(struct ppp_uart *uart, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_P1_PLATFORM_H */
