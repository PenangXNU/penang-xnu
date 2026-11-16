/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GICv3 for the Penang P1 profile: non-secure Group 1, one boot CPU.
 *
 * Register layout and semantics: Arm IHI 0069 (GICv3/v4 architecture
 * specification). Written for a system with two security states, where
 * non-secure EL1 sees GICD_CTLR.DS == 0: Group 0 and Secure Group 1 belong to
 * the secure world and their state reads as zero and ignores writes. The
 * driver therefore
 *   - never writes Group 0 enables, ICC_*0 registers or IGRPMODR;
 *   - treats an enable or route that does not read back as "not owned" and
 *     fails closed instead of assuming delivery;
 *   - takes every interrupt, including the architectural timer, as IRQ
 *     through ICC_IAR1_EL1/ICC_EOIR1_EL1.
 * Upstream XNU's only GICv3 path (pexpert/arm/pe_fiq.c, VMAPPLE) does the
 * opposite: it enables Group 0 and takes the timer as FIQ on PPI 27. That
 * contract cannot be met from non-secure EL1 on this SoC.
 *
 * A single-security-state distributor (DS == 1, e.g. an emulator) is also
 * handled: the interrupts this driver configures are moved to Group 1.
 */
#include "ppp_platform.h"

#include "ppp_gicv3_regs.h"

#define GICD_CTLR        PPP_GICD_CTLR
#define GICD_TYPER       PPP_GICD_TYPER
#define GICD_IGROUPR     PPP_GICD_IGROUPR
#define GICD_ISENABLER   PPP_GICD_ISENABLER
#define GICD_ICENABLER   PPP_GICD_ICENABLER
#define GICD_ICPENDR     PPP_GICD_ICPENDR
#define GICD_ICACTIVER   PPP_GICD_ICACTIVER
#define GICD_IPRIORITYR  PPP_GICD_IPRIORITYR
#define GICD_ICFGR       PPP_GICD_ICFGR
#define GICD_IROUTER     PPP_GICD_IROUTER
#define GIC_PIDR2        PPP_GIC_PIDR2
#define GICD_CTLR_ENABLE_G1NS PPP_GICD_CTLR_ENABLE_G1NS
#define GICD_CTLR_ENABLE_BIT0 PPP_GICD_CTLR_ENABLE_BIT0
#define GICD_CTLR_ARE_NS      PPP_GICD_CTLR_ARE_NS
#define GICD_CTLR_DS          PPP_GICD_CTLR_DS
#define GICD_CTLR_RWP         PPP_GICD_CTLR_RWP
#define GICR_CTLR        PPP_GICR_CTLR
#define GICR_TYPER       PPP_GICR_TYPER
#define GICR_WAKER       PPP_GICR_WAKER
#define GICR_SGI_BASE    PPP_GICR_SGI_BASE
#define GICR_CTLR_RWP    PPP_GICR_CTLR_RWP
#define GICR_TYPER_VLPIS PPP_GICR_TYPER_VLPIS
#define GICR_TYPER_LAST  PPP_GICR_TYPER_LAST
#define GICR_WAKER_PROCESSORSLEEP PPP_GICR_WAKER_PROCESSORSLEEP
#define GICR_WAKER_CHILDRENASLEEP PPP_GICR_WAKER_CHILDRENASLEEP
#define ICC_SRE_SRE      PPP_ICC_SRE_SRE
#define ICC_CTLR_EOIMODE PPP_ICC_CTLR_EOIMODE

static uint32_t
rd32(struct ppp_gic *g, uint64_t a)
{
    return g->io.read32(g->io.ctx, a);
}

static void
wr32(struct ppp_gic *g, uint64_t a, uint32_t v)
{
    g->io.write32(g->io.ctx, a, v);
}

static uint64_t
sys_rd(struct ppp_gic *g, uint32_t reg)
{
    return g->io.sysreg_read(g->io.ctx, reg);
}

static void
sys_wr(struct ppp_gic *g, uint32_t reg, uint64_t v)
{
    g->io.sysreg_write(g->io.ctx, reg, v);
}

static int
wait_clear(struct ppp_gic *g, uint64_t addr, uint32_t bits)
{
    for (uint32_t i = 0; i < g->poll_limit; ++i) {
        if ((rd32(g, addr) & bits) == 0) {
            return 0;
        }
    }
    return -1;
}

static int
is_v3(struct ppp_gic *g, uint64_t frame)
{
    uint32_t arch = rd32(g, frame + GIC_PIDR2) & 0xf0u;
    return arch == 0x30u || arch == 0x40u;
}

/* GICR_TYPER[63:32] is Aff3.Aff2.Aff1.Aff0; MPIDR keeps Aff3 at [39:32]. */
static uint32_t
affinity(uint64_t mpidr)
{
    return (uint32_t)(mpidr & 0xffffffu) | (uint32_t)((mpidr >> 32) & 0xffu) << 24;
}

static int
on_boot_cpu(struct ppp_gic *g)
{
    return affinity(sys_rd(g, PPP_SR_MPIDR_EL1)) == affinity(g->boot_mpidr);
}

/* SGIs and PPIs live in the redistributor's SGI frame, SPIs in GICD. */
static uint64_t
bank(struct ppp_gic *g, uint32_t intid)
{
    return intid < 32 ? g->gicr + GICR_SGI_BASE : g->gicd;
}

static int
wait_bank(struct ppp_gic *g, uint32_t intid)
{
    return intid < 32 ? wait_clear(g, g->gicr + GICR_CTLR, GICR_CTLR_RWP)
                      : wait_clear(g, g->gicd + GICD_CTLR, GICD_CTLR_RWP);
}

static int
usable(struct ppp_gic *g, uint32_t intid)
{
    if (g == NULL || !g->ready) {
        return PPP_GIC_E_STATE;
    }
    if (intid >= g->intid_count) {
        return PPP_GIC_E_INTID;
    }
    return on_boot_cpu(g) ? PPP_GIC_OK : PPP_GIC_E_WRONG_CPU;
}

static int
init_distributor(struct ppp_gic *g)
{
    uint32_t ctlr;
    if (!is_v3(g, g->gicd)) {
        return PPP_GIC_E_ARCH;
    }
    ctlr = rd32(g, g->gicd + GICD_CTLR);
    g->single_security = (ctlr & GICD_CTLR_DS) != 0;
    if (!(ctlr & GICD_CTLR_ARE_NS)) {
        /* Changing ARE while a group is enabled is UNPREDICTABLE. */
        if (ctlr & (GICD_CTLR_ENABLE_BIT0 | GICD_CTLR_ENABLE_G1NS)) {
            return PPP_GIC_E_ROUTING;
        }
        wr32(g, g->gicd + GICD_CTLR, ctlr | GICD_CTLR_ARE_NS);
        if (wait_clear(g, g->gicd + GICD_CTLR, GICD_CTLR_RWP)) {
            return PPP_GIC_E_RWP_TIMEOUT;
        }
        if (!(rd32(g, g->gicd + GICD_CTLR) & GICD_CTLR_ARE_NS)) {
            return PPP_GIC_E_ROUTING;
        }
    }
    g->intid_count = ((rd32(g, g->gicd + GICD_TYPER) & 0x1fu) + 1u) * 32u;
    if (g->intid_count > 1020u) {
        g->intid_count = 1020u;
    }
    /* Start with no SPI consumer. Writes to secure-owned bits are ignored. */
    for (uint32_t word = 1; word < (g->intid_count + 31u) / 32u; ++word) {
        wr32(g, g->gicd + GICD_ICENABLER + 4u * word, UINT32_MAX);
        wr32(g, g->gicd + GICD_ICACTIVER + 4u * word, UINT32_MAX);
    }
    return wait_clear(g, g->gicd + GICD_CTLR, GICD_CTLR_RWP) ? PPP_GIC_E_RWP_TIMEOUT : PPP_GIC_OK;
}

static int
init_redistributor(struct ppp_gic *g, const struct ppp_resources *res)
{
    uint32_t self = affinity(g->boot_mpidr);
    uint64_t frame = 0;
    uint32_t waker;
    for (uint64_t off = 0; off + res->gicr_stride <= res->gicr_size; off += res->gicr_stride) {
        uint64_t rd = res->gicr_pa + off;
        uint64_t typer;
        if (!is_v3(g, rd)) {
            return PPP_GIC_E_ARCH;
        }
        typer = g->io.read64(g->io.ctx, rd + GICR_TYPER);
        /* GICv4 VLPI frames double the per-CPU size; the tree must say so. */
        if ((typer & GICR_TYPER_VLPIS) && res->gicr_stride < 0x40000u) {
            return PPP_GIC_E_REDIST;
        }
        if ((uint32_t)(typer >> 32) == self) {
            frame = rd;
            break;
        }
        if (typer & GICR_TYPER_LAST) {
            break;
        }
    }
    if (frame == 0) {
        return PPP_GIC_E_REDIST;
    }
    g->gicr = frame;
    waker = rd32(g, frame + GICR_WAKER);
    if (waker & GICR_WAKER_PROCESSORSLEEP) {
        wr32(g, frame + GICR_WAKER, waker & ~GICR_WAKER_PROCESSORSLEEP);
    }
    if (wait_clear(g, frame + GICR_WAKER, GICR_WAKER_CHILDRENASLEEP)) {
        return PPP_GIC_E_WAKE_TIMEOUT;
    }
    /* No SGI/PPI consumer yet. */
    wr32(g, frame + GICR_SGI_BASE + GICD_ICENABLER, UINT32_MAX);
    wr32(g, frame + GICR_SGI_BASE + GICD_ICACTIVER, UINT32_MAX);
    return wait_clear(g, frame + GICR_CTLR, GICR_CTLR_RWP) ? PPP_GIC_E_RWP_TIMEOUT : PPP_GIC_OK;
}

static int
init_cpu_interface(struct ppp_gic *g)
{
    uint64_t v = sys_rd(g, PPP_SR_ICC_SRE_EL1);
    if (!(v & ICC_SRE_SRE)) {
        sys_wr(g, PPP_SR_ICC_SRE_EL1, v | ICC_SRE_SRE);
        if (!(sys_rd(g, PPP_SR_ICC_SRE_EL1) & ICC_SRE_SRE)) {
            return PPP_GIC_E_SRE; /* higher exception level did not grant it */
        }
    }
    sys_wr(g, PPP_SR_ICC_PMR_EL1, 0xff);
    if (sys_rd(g, PPP_SR_ICC_PMR_EL1) <= PPP_GIC_PRIORITY) {
        return PPP_GIC_E_CPUIF; /* our priority would be masked */
    }
    sys_wr(g, PPP_SR_ICC_BPR1_EL1, 0);
    /* ppp_gic_eoi relies on priority drop also deactivating. */
    v = sys_rd(g, PPP_SR_ICC_CTLR_EL1);
    if (v & ICC_CTLR_EOIMODE) {
        sys_wr(g, PPP_SR_ICC_CTLR_EL1, v & ~(uint64_t)ICC_CTLR_EOIMODE);
        if (sys_rd(g, PPP_SR_ICC_CTLR_EL1) & ICC_CTLR_EOIMODE) {
            return PPP_GIC_E_CPUIF;
        }
    }
    sys_wr(g, PPP_SR_ICC_IGRPEN1_EL1, 1);
    return (sys_rd(g, PPP_SR_ICC_IGRPEN1_EL1) & 1u) ? PPP_GIC_OK : PPP_GIC_E_CPUIF;
}

int
ppp_gic_init(struct ppp_gic *gic, const struct ppp_resources *res,
    const struct ppp_io *io, uint32_t poll_limit)
{
    int status;
    int sre_granted = 0;
    uint32_t ctlr;
    if (gic == NULL || res == NULL || io == NULL || io->read32 == NULL || io->write32 == NULL ||
        io->read64 == NULL || io->write64 == NULL || io->sysreg_read == NULL ||
        io->sysreg_write == NULL || res->abi_version != PPP_ABI_VERSION ||
        res->gicd_pa == 0 || res->gicr_pa == 0 || res->gicr_stride == 0 ||
        res->gicr_size < res->gicr_stride) {
        return PPP_GIC_E_ARGS;
    }
    if (gic->ready) {
        return PPP_GIC_E_STATE;
    }
    gic->io = *io;
    gic->gicd = res->gicd_pa;
    gic->gicr = 0;
    gic->poll_limit = poll_limit ? poll_limit : PPP_GIC_POLL_DEFAULT;
    gic->boot_mpidr = sys_rd(gic, PPP_SR_MPIDR_EL1);
    gic->ack_count = gic->eoi_count = gic->spurious_count = 0;

    status = init_distributor(gic);
    if (status == PPP_GIC_OK) {
        status = init_redistributor(gic, res);
    }
    if (status == PPP_GIC_OK) {
        sre_granted = 1;
        status = init_cpu_interface(gic);
        if (status == PPP_GIC_E_SRE) {
            sre_granted = 0; /* ICC_* access would trap: touch nothing more */
        }
    }
    if (status == PPP_GIC_OK) {
        ctlr = rd32(gic, gic->gicd + GICD_CTLR);
        wr32(gic, gic->gicd + GICD_CTLR, ctlr | GICD_CTLR_ENABLE_G1NS);
        if (wait_clear(gic, gic->gicd + GICD_CTLR, GICD_CTLR_RWP)) {
            status = PPP_GIC_E_RWP_TIMEOUT;
        } else if (!(rd32(gic, gic->gicd + GICD_CTLR) & GICD_CTLR_ENABLE_G1NS)) {
            status = PPP_GIC_E_DIST_ENABLE;
        }
    }
    if (status != PPP_GIC_OK) {
        /* Fail closed: no group enable and a priority mask that admits
         * nothing on this CPU, so a later unmask of PSTATE.I cannot deliver
         * through a half-configured controller. Distributor forwarding that
         * firmware had already enabled is left as found. */
        if (sre_granted) {
            sys_wr(gic, PPP_SR_ICC_IGRPEN1_EL1, 0);
            sys_wr(gic, PPP_SR_ICC_PMR_EL1, 0);
        }
        gic->gicr = 0;
        gic->intid_count = 0;
        return status;
    }
    gic->ready = 1;
    return PPP_GIC_OK;
}

int
ppp_gic_disable(struct ppp_gic *gic, uint32_t intid)
{
    int status = usable(gic, intid);
    if (status != PPP_GIC_OK) {
        return status;
    }
    wr32(gic, bank(gic, intid) + GICD_ICENABLER + 4u * (intid / 32u), 1u << (intid % 32u));
    return wait_bank(gic, intid) ? PPP_GIC_E_RWP_TIMEOUT : PPP_GIC_OK;
}

int
ppp_gic_configure(struct ppp_gic *gic, uint32_t intid, int edge)
{
    uint64_t base;
    uint32_t bit = 1u << (intid % 32u);
    uint32_t word = 4u * (intid / 32u);
    uint32_t shift = 8u * (intid % 4u);
    uint64_t reg;
    uint32_t v;
    int status = ppp_gic_disable(gic, intid);
    if (status != PPP_GIC_OK) {
        return status;
    }
    base = bank(gic, intid);
    if (gic->single_security) {
        reg = base + GICD_IGROUPR + word;
        wr32(gic, reg, rd32(gic, reg) | bit);
    }
    reg = base + GICD_IPRIORITYR + (intid & ~3u);
    wr32(gic, reg, (rd32(gic, reg) & ~(0xffu << shift)) | (PPP_GIC_PRIORITY << shift));
    if (intid >= 16) { /* SGIs have no trigger configuration */
        uint32_t cfg_bit = 2u << (2u * (intid % 16u));
        reg = base + GICD_ICFGR + 4u * (intid / 16u);
        v = rd32(gic, reg);
        v = edge ? (v | cfg_bit) : (v & ~cfg_bit);
        wr32(gic, reg, v);
        /* A PPI's trigger may be fixed by the implementation; an SPI's must
         * take the value, otherwise it is not ours to configure. */
        if (intid >= 32 && ((rd32(gic, reg) & cfg_bit) != 0) != (edge != 0)) {
            return PPP_GIC_E_NOT_OWNED;
        }
    }
    if (intid >= 32) {
        /* One target, IRM == 0, the boot CPU's full affinity. */
        uint64_t route = gic->boot_mpidr & 0xff00ffffffull;
        reg = gic->gicd + GICD_IROUTER + 8ull * intid;
        gic->io.write64(gic->io.ctx, reg, route);
        if (gic->io.read64(gic->io.ctx, reg) != route) {
            return PPP_GIC_E_NOT_OWNED;
        }
    }
    wr32(gic, base + GICD_ICPENDR + word, bit); /* drop a stale edge */
    return PPP_GIC_OK;
}

int
ppp_gic_enable(struct ppp_gic *gic, uint32_t intid)
{
    uint64_t reg;
    uint32_t bit = 1u << (intid % 32u);
    int status = usable(gic, intid);
    if (status != PPP_GIC_OK) {
        return status;
    }
    reg = bank(gic, intid) + GICD_ISENABLER + 4u * (intid / 32u);
    wr32(gic, reg, bit);
    return (rd32(gic, reg) & bit) ? PPP_GIC_OK : PPP_GIC_E_NOT_OWNED;
}

uint32_t
ppp_gic_ack(struct ppp_gic *gic)
{
    uint32_t intid;
    if (gic == NULL || !gic->ready) {
        return PPP_GIC_SPURIOUS;
    }
    intid = (uint32_t)sys_rd(gic, PPP_SR_ICC_IAR1_EL1) & 0xffffffu;
    if (intid >= 1020u) { /* 1020..1023 are special and never completed */
        ++gic->spurious_count;
        return PPP_GIC_SPURIOUS;
    }
    ++gic->ack_count;
    return intid;
}

void
ppp_gic_eoi(struct ppp_gic *gic, uint32_t intid)
{
    if (gic == NULL || !gic->ready || intid >= 1020u) {
        return;
    }
    sys_wr(gic, PPP_SR_ICC_EOIR1_EL1, intid);
    ++gic->eoi_count;
}

int
ppp_gic_dispatch(struct ppp_gic *gic, ppp_irq_handler handler, void *ctx)
{
    uint32_t intid = ppp_gic_ack(gic);
    if (intid == PPP_GIC_SPURIOUS) {
        return 0;
    }
    if (handler != NULL) {
        handler(ctx, intid);
    }
    ppp_gic_eoi(gic, intid);
    return 1;
}

int
ppp_gic_self_sgi(struct ppp_gic *gic, uint32_t intid)
{
    uint64_t mpidr;
    int status = usable(gic, intid);
    if (status != PPP_GIC_OK) {
        return status;
    }
    mpidr = sys_rd(gic, PPP_SR_MPIDR_EL1);
    /* Target list covers Aff0 0..15; range selectors are outside P1. */
    if (intid >= 16 || (mpidr & 0xffu) >= 16) {
        return PPP_GIC_E_INTID;
    }
    if (!(rd32(gic, gic->gicr + GICR_SGI_BASE + GICD_ISENABLER) & (1u << intid))) {
        return PPP_GIC_E_CONFIG;
    }
    /* Explicit target (IRM == 0): IRM == 1 would mean "all but self". */
    sys_wr(gic, PPP_SR_ICC_SGI1R_EL1,
        ((mpidr >> 32) & 0xffu) << 48 | ((mpidr >> 16) & 0xffu) << 32 |
        (uint64_t)intid << 24 | ((mpidr >> 8) & 0xffu) << 16 | 1ull << (mpidr & 0xfu));
    return PPP_GIC_OK;
}

const char *
ppp_gic_status_name(int status)
{
    static const char *const names[PPP_GIC_STATUS_COUNT] = {
        "OK", "ARGS", "ARCH", "ROUTING", "RWP_TIMEOUT", "REDIST", "WAKE_TIMEOUT", "SRE",
        "CPUIF", "DIST_ENABLE", "STATE", "INTID", "WRONG_CPU", "NOT_OWNED", "CONFIG",
    };
    return status >= 0 && status < PPP_GIC_STATUS_COUNT ? names[status] : "UNKNOWN";
}
