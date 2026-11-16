/* SPDX-License-Identifier: Apache-2.0 */
/*
 * QEMU `virt` harness: runs the unmodified P1 platform services at EL1
 * against the emulator's GICv3 and architectural timer, with the resources
 * taken from the DTB the emulator generated. It proves delivery on an
 * emulated GIC, not on SM4350 silicon, and is not an XNU run.
 */
#include "../ppp_platform.h"

#define SYS_WRITE0 0x04
#define SYS_EXIT   0x18

static struct ppp_gic gic;
static struct ppp_timer timer;
static volatile uint32_t sgi_seen, timer_seen, other_seen, last_intid;

static uint64_t
semihost(uint64_t op, uint64_t arg)
{
    register uint64_t x0 __asm__("x0") = op;
    register uint64_t x1 __asm__("x1") = arg;
    __asm__ volatile ("hlt #0xf000" : "+r"(x0) : "r"(x1) : "memory");
    return x0;
}

void
harness_exit(uint64_t code)
{
    uint64_t block[2] = { 0x20026, code }; /* ADP_Stopped_ApplicationExit */
    semihost(SYS_EXIT, (uint64_t)block);
    for (;;) {
    }
}

static void
put(const char *s)
{
    semihost(SYS_WRITE0, (uint64_t)s);
}

static void
put_value(const char *key, uint64_t v)
{
    char buf[19] = "0x";
    for (int i = 0; i < 16; ++i) {
        buf[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 0xf];
    }
    buf[18] = 0;
    put("PPP_QEMU ");
    put(key);
    put("=");
    put(buf);
    put("\n");
}

void
harness_unexpected(uint64_t kind, uint64_t esr, uint64_t elr)
{
    put_value("unexpected_vector", kind);
    put_value("esr", esr);
    put_value("elr", elr);
    harness_exit(3);
}

/* ---- struct ppp_io on real (emulated) hardware; MMU off, identity ---- */
static uint32_t r32(void *c, uint64_t a) { (void)c; return *(volatile uint32_t *)a; }
static void w32(void *c, uint64_t a, uint32_t v) { (void)c; *(volatile uint32_t *)a = v; }
static uint64_t r64(void *c, uint64_t a) { (void)c; return *(volatile uint64_t *)a; }
static void w64(void *c, uint64_t a, uint64_t v) { (void)c; *(volatile uint64_t *)a = v; }

#define MRS(name) ({ uint64_t v_; __asm__ volatile ("mrs %0, " name : "=r"(v_)); v_; })
#define MSR(name, v) do { uint64_t v_ = (v); __asm__ volatile ("msr " name ", %0\n\tisb" : : "r"(v_)); } while (0)

static uint64_t
sysreg_read(void *c, uint32_t reg)
{
    (void)c;
    switch (reg) {
    case PPP_SR_MPIDR_EL1: return MRS("mpidr_el1");
    case PPP_SR_ICC_SRE_EL1: return MRS("S3_0_C12_C12_5");
    case PPP_SR_ICC_PMR_EL1: return MRS("S3_0_C4_C6_0");
    case PPP_SR_ICC_BPR1_EL1: return MRS("S3_0_C12_C12_3");
    case PPP_SR_ICC_CTLR_EL1: return MRS("S3_0_C12_C12_4");
    case PPP_SR_ICC_IGRPEN1_EL1: return MRS("S3_0_C12_C12_7");
    case PPP_SR_ICC_IAR1_EL1: return MRS("S3_0_C12_C12_0");
    case PPP_SR_CNTFRQ_EL0: return MRS("cntfrq_el0");
    default: harness_unexpected(100, reg, 0); return 0;
    }
}

static void
sysreg_write(void *c, uint32_t reg, uint64_t v)
{
    (void)c;
    switch (reg) {
    case PPP_SR_ICC_SRE_EL1: MSR("S3_0_C12_C12_5", v); break;
    case PPP_SR_ICC_PMR_EL1: MSR("S3_0_C4_C6_0", v); break;
    case PPP_SR_ICC_BPR1_EL1: MSR("S3_0_C12_C12_3", v); break;
    case PPP_SR_ICC_CTLR_EL1: MSR("S3_0_C12_C12_4", v); break;
    case PPP_SR_ICC_IGRPEN1_EL1: MSR("S3_0_C12_C12_7", v); break;
    case PPP_SR_ICC_EOIR1_EL1: MSR("S3_0_C12_C12_1", v); break;
    case PPP_SR_ICC_SGI1R_EL1: MSR("S3_0_C12_C11_5", v); break;
    default: harness_unexpected(101, reg, v);
    }
}

static const struct ppp_io io = { 0, r32, w32, r64, w64, sysreg_read, sysreg_write };

static void
on_irq(void *ctx, uint32_t intid)
{
    (void)ctx;
    last_intid = intid;
    if (intid == 1) {
        ++sgi_seen;
    } else if (intid == timer.virtual_intid) {
        /* Level-triggered: silence the source before completion, as the
         * kernel's timer path does by reprogramming the deadline. */
        MSR("cntv_ctl_el0", 2 /* IMASK */);
        ++timer_seen;
    } else {
        ++other_seen;
    }
}

void
harness_irq(void)
{
    ppp_gic_dispatch(&gic, on_irq, 0);
}

static int
wait_for(volatile uint32_t *counter, uint32_t want)
{
    for (uint32_t i = 0; i < 50000000u; ++i) {
        if (*counter >= want) {
            return 0;
        }
    }
    return -1;
}

static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void
harness_main(const uint8_t *fdt)
{
    struct ppp_resources res;
    int status;

    put_value("current_el", MRS("CurrentEL") >> 2);
    status = ppp_resolve(fdt, be32(fdt + 4), &res);
    put_value("resolve_status", (uint64_t)status);
    if (status != PPP_OK) {
        harness_exit(10);
    }
    put_value("gicd", res.gicd_pa);
    put_value("gicr", res.gicr_pa);
    put_value("gicr_stride", res.gicr_stride);
    put_value("cpus", res.cpu_count);
    put_value("virtual_timer_intid", res.timer[PPP_TIMER_VIRTUAL].intid);

    status = ppp_gic_init(&gic, &res, &io, 0);
    put_value("gic_init_status", (uint64_t)status);
    if (status != PPP_GIC_OK) {
        harness_exit(11);
    }
    put_value("gicd_ctlr_ds", gic.single_security);
    put_value("intid_count", gic.intid_count);
    put_value("boot_redistributor", gic.gicr);

    status = ppp_timer_admit(&res, &io, &timer);
    put_value("timer_admit_status", (uint64_t)status);
    put_value("timer_hz", timer.frequency_hz);
    if (status != PPP_TIMER_OK) {
        harness_exit(12);
    }

    /* 1. Nothing is enabled yet: unmasking IRQ must deliver nothing. */
    __asm__ volatile ("msr daifclr, #2\n\tisb");
    for (volatile uint32_t i = 0; i < 100000; ++i) {
    }
    put_value("irqs_before_enable", sgi_seen + timer_seen + other_seen);

    /* 2. Hardware-delivered SGI through ICC_SGI1R_EL1, taken on the IRQ vector. */
    if (ppp_gic_configure(&gic, 1, 0) != PPP_GIC_OK || ppp_gic_enable(&gic, 1) != PPP_GIC_OK ||
        ppp_gic_self_sgi(&gic, 1) != PPP_GIC_OK || wait_for(&sgi_seen, 1)) {
        put_value("sgi_failed_last_intid", last_intid);
        harness_exit(13);
    }
    put_value("sgi_delivered", sgi_seen);

    /* 3. Virtual timer PPI named by the tree, as a Group 1 IRQ. */
    if (ppp_gic_configure(&gic, timer.virtual_intid, (int)timer.edge) != PPP_GIC_OK ||
        ppp_gic_enable(&gic, timer.virtual_intid) != PPP_GIC_OK) {
        harness_exit(14);
    }
    MSR("cntv_tval_el0", timer.frequency_hz / 100); /* 10 ms */
    MSR("cntv_ctl_el0", 1);                         /* ENABLE, not masked */
    if (wait_for(&timer_seen, 1)) {
        put_value("timer_failed_last_intid", last_intid);
        harness_exit(15);
    }
    put_value("timer_delivered", timer_seen);

    /* 4. A disabled interrupt stays silent even when its source asserts. */
    if (ppp_gic_disable(&gic, timer.virtual_intid) != PPP_GIC_OK) {
        harness_exit(16);
    }
    MSR("cntv_tval_el0", 1);
    MSR("cntv_ctl_el0", 1);
    for (volatile uint32_t i = 0; i < 2000000; ++i) {
    }
    put_value("timer_after_disable", timer_seen);
    put_value("istatus_while_disabled", (MRS("cntv_ctl_el0") >> 2) & 1);
    MSR("cntv_ctl_el0", 2);

    put_value("acks", gic.ack_count);
    put_value("eois", gic.eoi_count);
    put_value("unexpected_intids", other_seen);
    status = sgi_seen == 1 && timer_seen == 1 && other_seen == 0 && gic.ack_count == 2 &&
        gic.eoi_count == 2;
    put(status ? "PPP_QEMU result=PASS\n" : "PPP_QEMU result=FAIL\n");
    harness_exit(status ? 0 : 1);
}
