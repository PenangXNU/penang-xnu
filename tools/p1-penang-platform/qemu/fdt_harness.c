/* SPDX-License-Identifier: Apache-2.0 */
/*
 * QEMU `virt` harness for the retained-FDT provider with the MMU on.
 *
 * Builds 16-KiB-granule page tables the way XNU's bootstrap does in shape:
 * an identity map through TTBR0 for this harness and a kernel window
 * through TTBR1 at XNU's virtual base. The test places a tree in guest RAM
 * at the retained-FDT slot, then binds it with a translation hook that is
 * the address-translation instruction (AT S1E1R and PAR_EL1), never a load.
 * Scenarios remove or alias part of the window first: a read through an
 * unproven mapping would take a synchronous abort, which this harness
 * reports as a failure. Research fixture only; not an XNU run.
 */
#include "../ppp_runtime_fdt.h"
#include "../ppp_platform.h"
#include "p1_memory_admission.h"

#define SYS_WRITE0 0x04
#define SYS_EXIT   0x18

#define VIRT_BASE  0xfffffe0004000000ull
#define PHYS_BASE  0x48000000ull
#define MEM_SIZE   0x04000000ull           /* two 32-MiB L2 blocks */
#define KERNEL_END (PHYS_BASE + 0x03100000ull)
#define ADT_PA     KERNEL_END
#define ARGS_PA    (ADT_PA + 0x10000ull)
#define SLOT_PA    (ARGS_PA + 0x4000ull)
#define SCENARIO_PA 0x4fff0000ull          /* written by the QEMU loader */
#define LENGTH_PA   0x4fff0008ull
#define TTBR1_MIN  0xffff800000000000ull   /* T1SZ = 17 */

extern struct p1_runtime_fdt penang_p1_runtime_fdt;

static uint64_t l1_lo[2048] __attribute__((aligned(16384)));
static uint64_t l2_lo[2048] __attribute__((aligned(16384)));
static uint64_t l1_hi[2048] __attribute__((aligned(16384)));
static uint64_t l2_hi[2048] __attribute__((aligned(16384)));
static uint64_t l3_hi[2048] __attribute__((aligned(16384)));

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
    uint64_t block[2] = { 0x20026, code };
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
    put("PPP_FDT ");
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

void
harness_irq(void)
{
    harness_unexpected(5, 0, 0);
}

#define MRS(name) ({ uint64_t v_; __asm__ volatile ("mrs %0, " name : "=r"(v_)); v_; })
#define MSR(name, v) do { uint64_t v_ = (v); __asm__ volatile ("msr " name ", %0" : : "r"(v_)); } while (0)

#define DESC_TABLE  3ull
#define DESC_BLOCK  1ull
#define DESC_PAGE   3ull
#define ATTR_NORMAL (1ull << 2)      /* MAIR index 1 */
#define AF_SH       ((1ull << 10) | (3ull << 8))
#define BLOCK_32M   0x02000000ull

static void
tlb_sync(void)
{
    __asm__ volatile ("dsb ishst\n\ttlbi vmalle1\n\tdsb ish\n\tisb" ::: "memory");
}

static void
map_window(int scenario)
{
    unsigned l2 = (unsigned)((VIRT_BASE >> 25) & 0x7ff);
    l2_hi[l2] = PHYS_BASE | ATTR_NORMAL | AF_SH | DESC_BLOCK;
    switch (scenario) {
    case 1: /* second block absent */
        l2_hi[l2 + 1] = 0;
        break;
    case 2: /* second block aliases other memory */
        l2_hi[l2 + 1] = (PHYS_BASE + 4 * BLOCK_32M) | ATTR_NORMAL | AF_SH | DESC_BLOCK;
        break;
    case 3: /* second block by 16-KiB pages, one page of the tree missing */
        for (unsigned i = 0; i < 2048; ++i) {
            uint64_t pa = PHYS_BASE + BLOCK_32M + (uint64_t)i * 0x4000ull;
            l3_hi[i] = pa | ATTR_NORMAL | AF_SH | DESC_PAGE;
        }
        l3_hi[((SLOT_PA - PHYS_BASE - BLOCK_32M) >> 14) + 1] = 0;
        l2_hi[l2 + 1] = (uint64_t)l3_hi | DESC_TABLE;
        break;
    default:
        l2_hi[l2 + 1] = (PHYS_BASE + BLOCK_32M) | ATTR_NORMAL | AF_SH | DESC_BLOCK;
    }
    tlb_sync();
}

static void
mmu_on(void)
{
    /* Identity map of 1 GiB of RAM at 0x40000000 for the harness itself. */
    for (unsigned i = 32; i < 64; ++i) {
        l2_lo[i] = ((uint64_t)i * BLOCK_32M) | ATTR_NORMAL | AF_SH | DESC_BLOCK;
    }
    l1_lo[0] = (uint64_t)l2_lo | DESC_TABLE;
    l1_hi[(VIRT_BASE >> 36) & 0x7ff] = (uint64_t)l2_hi | DESC_TABLE;
    map_window(0);
    MSR("mair_el1", 0xff00ull);                 /* attr0 device, attr1 normal WB */
    MSR("tcr_el1", 17ull | (1ull << 8) | (1ull << 10) | (3ull << 12) | (2ull << 14) |
        (17ull << 16) | (1ull << 24) | (1ull << 26) | (3ull << 28) | (1ull << 30) | (2ull << 32));
    MSR("ttbr0_el1", (uint64_t)l1_lo);
    MSR("ttbr1_el1", (uint64_t)l1_hi);
    tlb_sync();
    MSR("sctlr_el1", MRS("sctlr_el1") | 1u | (1u << 2) | (1u << 12));
    __asm__ volatile ("isb");
}

/* The adapter's translation hook: AT S1E1R never faults. */
static int
at_translate(void *ctx, uint64_t va, uint64_t *pa)
{
    uint64_t par;
    (void)ctx;
    __asm__ volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r"(par) : "r"(va) : "memory");
    if (par & 1u) {
        return 1;
    }
    *pa = (par & 0x0000fffffffff000ull) | (va & 0xfffu);
    return 0;
}

static int
descriptor_is_zero(void)
{
    const volatile uint8_t *d = (const volatile uint8_t *)&penang_p1_runtime_fdt;
    for (unsigned i = 0; i < sizeof(penang_p1_runtime_fdt); ++i) {
        if (d[i]) {
            return 0;
        }
    }
    return 1;
}

void
harness_main(const uint8_t *qemu_dtb)
{
    uint64_t scenario = *(volatile uint64_t *)SCENARIO_PA;
    uint64_t length = *(volatile uint64_t *)LENGTH_PA;
    uint64_t top = SLOT_PA + ((length + 0x3fffu) & ~0x3fffull);
    struct ppp_fdt_handoff h = {
        PPP_FDT_ABI_VERSION, 0, SLOT_PA, length, PHYS_BASE, VIRT_BASE, MEM_SIZE, KERNEL_END, top,
        ADT_PA, 0x10000, ARGS_PA,
    };
    struct ppp_fdt_mapping m = { 0, at_translate, TTBR1_MIN, 0 };
    struct ppp_resources res;
    int status, retry;
    (void)qemu_dtb;

    m.va_max = 0; /* the whole TTBR1 half: [TTBR1_MIN, 2^64) would wrap; use the window end */
    m.va_max = VIRT_BASE + MEM_SIZE;
    put_value("scenario", scenario);
    put_value("current_el", MRS("CurrentEL") >> 2);
    mmu_on();
    put_value("mmu", MRS("sctlr_el1") & 1u);
    map_window((int)scenario);

    if (scenario == 4) {
        h.virt_base = 0; /* null virtual base */
    }
    status = ppp_runtime_fdt_bind(&h, &m);
    put_value("bind_status", (uint64_t)status);
    put_value("descriptor_untouched", descriptor_is_zero());
    if (status == PPP_FDT_OK) {
        put_value("descriptor_va", (uint64_t)penang_p1_runtime_fdt.bytes);
        put_value("descriptor_pa", penang_p1_runtime_fdt.physical_address);
        put_value("admit_status", (uint64_t)p1_memory_admit(&penang_p1_runtime_fdt, PHYS_BASE, MEM_SIZE, top));
        put_value("resolve_status", (uint64_t)ppp_resolve(penang_p1_runtime_fdt.bytes,
            penang_p1_runtime_fdt.length, &res));
        put_value("virtual_timer_intid", res.timer[PPP_TIMER_VIRTUAL].intid);
        put_value("gicd", res.gicd_pa);
        put("PPP_FDT result=PASS\n");
        harness_exit(0);
    }
    /* Restore the correct mapping: the refusal must not have consumed the
     * single assignment. */
    h.virt_base = VIRT_BASE;
    map_window(0);
    retry = ppp_runtime_fdt_bind(&h, &m);
    put_value("retry_status", (uint64_t)retry);
    put_value("second_bind", (uint64_t)ppp_runtime_fdt_bind(&h, &m));
    put(retry == PPP_FDT_OK ? "PPP_FDT result=PASS\n" : "PPP_FDT result=FAIL\n");
    harness_exit(retry == PPP_FDT_OK ? 0 : 1);
}
