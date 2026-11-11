/* SPDX-License-Identifier: MIT */
/*
 * PENANG XNU â€” M12.1 ARM64 Boot Bridge Core
 * Freestanding C implementation (zero libc/heap, strict alignment for MMU-off AArch64).
 */
#include "bridge_contract.h"
#include "m12_physical_entropy_gate.h"

#if defined(M12_QEMU_RESERVATIONS) && (defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(PENANG_PHYSICAL))
#error Dynamic reservation planning is restricted to the QEMU bridge
#endif

#if defined(M12_HANDOFF_PROBE) && (defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(PENANG_PHYSICAL))
#error Handoff probe planner is host/QEMU only
#endif

uint64_t g_m12_handoff_args_pa = 0;
uint64_t g_m12_handoff_entry_pa = 0;

static const char g_status_names[M12_STATUS_COUNT][16] = {
    [M12_BRIDGE_OK]               = "BRIDGE_OK",
    [M12_ERR_ABI_ARGS]            = "ERR_ABI_ARGS",
    [M12_ERR_CPU_EL]              = "ERR_CPU_EL",
    [M12_ERR_CPU_MMU_CACHE]       = "ERR_CPU_MMU",
    [M12_ERR_NO_TGRAN16]          = "ERR_NO_TGRAN16",
    [M12_ERR_CNTFRQ_ZERO]         = "ERR_CNTFRQ_0",
    [M12_ERR_FDT_INVALID]         = "ERR_FDT_INV",
    [M12_ERR_FDT_COMPATIBLE]      = "ERR_FDT_COMPAT",
    [M12_ERR_MEMORY_MAP]          = "ERR_MEM_MAP",
    [M12_ERR_DYNAMIC_RESERVATION] = "ERR_DYN_RESV",
    [M12_ERR_BOOT_CPU_MPIDR]      = "ERR_BOOT_MPIDR",
    [M12_ERR_GIC_INVALID]         = "ERR_GIC_INV",
    [M12_ERR_WINDOW_SELECTION]    = "ERR_WIN_SEL",
    [M12_ERR_MACHO_INVALID]       = "ERR_MACHO_INV",
    [M12_ERR_SEGMENT_OVERLAP]     = "ERR_SEG_OVLP",
    [M12_ERR_ADT_OVERFLOW]        = "ERR_ADT_OVFL",
    [M12_ERR_BOOTARGS_INVALID]    = "ERR_BOOTARGS",
    [M12_ERR_EXCEPTION_TAKEN]     = "ERR_EXCEPTION",
    [M12_ERR_ENTROPY_UNAVAILABLE] = "ERR_NO_ENTROPY",
    [M12_ERR_DYNAMIC_UNRESOLVED]  = "ERR_RESV_UNRES",
    [M12_ERR_ENTROPY_UNTRUSTED]   = "ERR_RNG_UNTRUST",
};

const char *
m12_status_name(uint32_t status)
{
    if (status >= M12_STATUS_COUNT) {
        return "ERR_UNKNOWN";
    }
    return g_status_names[status];
}

/* Safe byte-granularity memory helpers (safe when SCTLR_EL1.M == 0) */
static void
m12_bzero(void *dst, size_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; i++) {
        p[i] = 0;
    }
}

static void
m12_memcpy(void *dst, const void *src, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static size_t
m12_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int
m12_streq(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int
m12_starts_with(const char *s, const char *prefix)
{
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static uint32_t
m12_be32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static uint64_t
m12_be64(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint64_t)m12_be32(b) << 32) | (uint64_t)m12_be32(b + 4);
}

static uint32_t
m12_le32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint64_t
m12_le64(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint64_t)m12_le32(b) | ((uint64_t)m12_le32(b + 4) << 32);
}

static void
m12_put_le32(void *dst, uint32_t v)
{
    uint8_t *b = (uint8_t *)dst;
    b[0] = (uint8_t)(v & 0xffu);
    b[1] = (uint8_t)((v >> 8) & 0xffu);
    b[2] = (uint8_t)((v >> 16) & 0xffu);
    b[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void
m12_put_le64(void *dst, uint64_t v)
{
    m12_put_le32(dst, (uint32_t)(v & 0xffffffffu));
    m12_put_le32((uint8_t *)dst + 4, (uint32_t)(v >> 32));
}

/* Castagnoli CRC-32C (polynomial 0x1EDC6F41, reflected 0x82F63B78) */
uint32_t
m12_crc32c(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0x82f63b78u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* --------------------------------------------------------------------------
 * FDT (0xd00dfeed) bounded read-only parser
 * -------------------------------------------------------------------------- */
#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  0x00000001u
#define FDT_END_NODE    0x00000002u
#define FDT_PROP        0x00000003u
#define FDT_NOP         0x00000004u
#define FDT_END         0x00000009u
#define FDT_MAX_TOTAL   (2u * 1024u * 1024u)

static int
m12_strlist_contains(const uint8_t *data, uint32_t len, const char *want)
{
    uint32_t i = 0;
    size_t want_len = m12_strlen(want);
    if (data == NULL || len == 0 || data[len - 1] != 0) {
        return 0;
    }
    while (i < len) {
        uint32_t start = i;
        while (i < len && data[i] != 0) {
            i++;
        }
        if (i - start == want_len) {
            size_t k = 0;
            while (k < want_len && data[start + k] == (uint8_t)want[k]) {
                k++;
            }
            if (k == want_len) {
                return 1;
            }
        }
        i++;
    }
    return 0;
}

static int
m12_add_reserved(struct m12_handoff_plan *plan, uint64_t base, uint64_t size)
{
    if (size == 0) {
        return 0;
    }
    if (base > UINT64_MAX - size ||
        base + size > UINT64_MAX - M12_PAGE_16K_MASK) {
        /* Coalescing rounds reservation ends up to 16 KiB. */
        return -1;
    }
    if (plan->reserved_count >= M12_MAX_RESERVED_RANGES) {
        return -1;
    }
    plan->reserved_ranges[plan->reserved_count].base = base;
    plan->reserved_ranges[plan->reserved_count].size = size;
    plan->reserved_count++;
    return 0;
}

/* Penang static guard reservations from stock device_tree.dtb (M12.0 Section 4.1) */
static const struct m12_phys_range g_penang_static_guards[] = {
    { 0x80000000ULL, 0x00800000ULL }, /* hyp_mem + xbl_aop + xbl_DTlog */
    { 0x80800000ULL, 0x00080000ULL }, /* aop_image + aop_cmd_db */
    { 0x80884000ULL, 0x0001c000ULL }, /* smem_region */
    { 0x80900000ULL, 0x01d00000ULL }, /* tz_Apps_region */
    { 0x85200000ULL, 0x01100000ULL }, /* cont_splash + removed_region */
    { 0x86400000ULL, 0x11500000ULL }, /* camera + pil + adsp + ipa + gpu + wlan + mpss */
    { 0x9c700000ULL, 0x03900000ULL }, /* dfps + tz_stat + tags + qseecom_ta */
    { 0xfec00000ULL, 0x00400000ULL }, /* rmtfs + lower/upper guards */
};

struct m12_dynamic_request {
    const uint8_t *ranges;
    uint64_t size;
    uint64_t alignment;
    uint32_t ranges_len;
    uint32_t node_offset;
    uint32_t no_map;
    uint32_t reusable;
    uint32_t has_alignment;
};

_Static_assert(sizeof(struct m12_handoff_plan) <= 8192, "bridge_entry.S plan storage");

struct m12_fdt_parse_state {
    struct m12_dynamic_request requests[M12_MAX_DYNAMIC_REQUESTS];
    uint32_t request_count, reserve_addr_cells, reserve_size_cells;
    uint32_t memory_only;
    uint8_t  chosen_seed[64];
    uint32_t has_chosen_seed;
    uint32_t chosen_seed_property_seen;
    uint64_t initrd_start;
    uint64_t initrd_end;
    uint32_t boot_cpu_found;
    uint32_t gic_found;
    uint32_t timer_found;
    uint32_t psci_found;
};

static uint64_t
m12_read_cells(const uint8_t *p, uint32_t cells)
{
    if (cells == 1) {
        return (uint64_t)m12_be32(p);
    }
    return m12_be64(p);
}

#define FDT_MAX_DEPTH   16u

static enum m12_bridge_status
m12_parse_fdt(const uint8_t *fdt, const struct m12_cpu_context *ctx,
              struct m12_handoff_plan *plan, struct m12_fdt_parse_state *st)
{
    uint32_t totalsize, off_struct, off_strings, off_mem_rsvmap, size_struct, size_strings, version;
    uint32_t pos, depth = 0;
    /* Path tracking for up to depth FDT_MAX_DEPTH */
    const char *node_names[FDT_MAX_DEPTH];
    uint32_t addr_cells[FDT_MAX_DEPTH];
    uint32_t size_cells[FDT_MAX_DEPTH];
    const uint8_t *node_reg_ptr[FDT_MAX_DEPTH];
    uint32_t node_reg_len[FDT_MAX_DEPTH];
    uint8_t node_is_gic[FDT_MAX_DEPTH];
    uint8_t node_is_psci[FDT_MAX_DEPTH];
    uint8_t node_psci_method[FDT_MAX_DEPTH];
    /* Per-node temporary tracking */
    uint32_t in_memory_node = 0, memory_disabled = 0, memory_first = 0;
    uint32_t in_resmem_child = 0;
    uint32_t resmem_has_reg = 0;
    struct m12_dynamic_request request = {0};
    const uint8_t *request_size = NULL, *request_alignment = NULL;
    uint32_t size_len = 0, alignment_len = 0;
    uint32_t reserve_ranges_seen = 0, fdt_end = 0;
    uint32_t resmem_disabled = 0;
    uint32_t resmem_reg_valid = 0;
    uint32_t in_cpu_child = 0;
    uint32_t cpu_is_device_cpu = 0;
    uint32_t cpu_has_reg = 0;
    uint64_t cpu_reg_mpidr = 0;

    if (fdt == NULL || ((uintptr_t)fdt & 7u) != 0) {
        return M12_ERR_ABI_ARGS;
    }
    if (m12_be32(fdt) != FDT_MAGIC) {
        return M12_ERR_FDT_INVALID;
    }
    totalsize    = m12_be32(fdt + 4);
    off_struct   = m12_be32(fdt + 8);
    off_strings  = m12_be32(fdt + 12);
    off_mem_rsvmap = m12_be32(fdt + 16);
    version      = m12_be32(fdt + 20);
    size_strings = m12_be32(fdt + 32);
    size_struct  = m12_be32(fdt + 36);

    if (totalsize < 40u || totalsize > FDT_MAX_TOTAL || version < 16u) {
        return M12_ERR_FDT_INVALID;
    }
    if (off_struct >= totalsize || size_struct > totalsize - off_struct || (off_struct & 3u) != 0) {
        return M12_ERR_FDT_INVALID;
    }
    if (off_strings >= totalsize || size_strings > totalsize - off_strings) {
        return M12_ERR_FDT_INVALID;
    }
    /* Header reserve-map entries are physical reservations independent of
     * /reserved-memory children. Must have a terminating (0,0) pair before
     * the structure block; do not guess address ownership. */
    if (off_mem_rsvmap < 40u || (off_mem_rsvmap & 7u) ||
        off_mem_rsvmap > off_struct || off_struct - off_mem_rsvmap < 16u) {
        return M12_ERR_FDT_INVALID;
    }
    {
        uint32_t rpos = off_mem_rsvmap;
        uint32_t found_end = 0;
        while (rpos <= off_struct && off_struct - rpos >= 16u) {
            uint64_t address = m12_be64(fdt + rpos);
            uint64_t bytes = m12_be64(fdt + rpos + 8u);
            rpos += 16u;
            if (address == 0u && bytes == 0u) {
                found_end = 1;
                break;
            }
            if (bytes == 0u || m12_add_reserved(plan, address, bytes) != 0) {
                return M12_ERR_MEMORY_MAP;
            }
        }
        if (!found_end) {
            return M12_ERR_FDT_INVALID;
        }
    }

    plan->fdt_pa = (uint64_t)(uintptr_t)fdt;
    plan->fdt_totalsize = totalsize;

    for (pos = 0; pos < FDT_MAX_DEPTH; pos++) {
        node_names[pos] = "";
        addr_cells[pos] = 2;
        size_cells[pos] = 2;
        node_reg_ptr[pos] = NULL;
        node_reg_len[pos] = 0;
        node_is_gic[pos] = 0;
        node_is_psci[pos] = 0;
        node_psci_method[pos] = 0;
    }

    pos = off_struct;
    while (pos + 4 <= off_struct + size_struct) {
        uint32_t token = m12_be32(fdt + pos);
        pos += 4;

        if (token == FDT_NOP) {
            continue;
        } else if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)(fdt + pos);
            uint32_t max_len = off_struct + size_struct - pos;
            uint32_t nlen = 0;
            while (nlen < max_len && name[nlen] != '\0') {
                nlen++;
            }
            if (nlen >= max_len || depth >= FDT_MAX_DEPTH) {
                return M12_ERR_FDT_INVALID;
            }
            pos += (nlen + 1u + 3u) & ~3u;

            node_names[depth] = name;
            if (depth > 0) {
                addr_cells[depth] = addr_cells[depth - 1];
                size_cells[depth] = size_cells[depth - 1];
            } else {
                addr_cells[0] = 2;
                size_cells[0] = 2;
            }
            node_reg_ptr[depth] = NULL;
            node_reg_len[depth] = 0;
            node_is_gic[depth] = 0;
            node_is_psci[depth] = 0;
            node_psci_method[depth] = 0;

            if (depth == 1 && (m12_streq(name, "memory") || m12_starts_with(name, "memory@"))) {
                in_memory_node = 1;
                memory_disabled = 0;
                memory_first = plan->ram_range_count;
            }
            if (depth == 2 && m12_streq(node_names[1], "reserved-memory")) {
                in_resmem_child = 1;
                resmem_has_reg = 0;
                m12_bzero(&request, sizeof(request));
                request.node_offset = (uint32_t)((const uint8_t *)name - fdt);
                request_size = request_alignment = NULL;
                resmem_disabled = 0;
                resmem_reg_valid = 0;
            }
            if (depth == 2 && m12_streq(node_names[1], "cpus") && m12_starts_with(name, "cpu@")) {
                in_cpu_child = 1;
                cpu_is_device_cpu = 0;
                cpu_has_reg = 0;
                cpu_reg_mpidr = 0;
            }
            depth++;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return M12_ERR_FDT_INVALID;
            }
            depth--;
            if (depth == 1 && in_memory_node) {
                in_memory_node = 0;
                if (memory_disabled) plan->ram_range_count = memory_first;
            }
            if (depth == 2 && in_resmem_child) {
                if (!resmem_disabled && !resmem_has_reg && request_size) {
#ifndef M12_QEMU_RESERVATIONS
                    if (!st->memory_only)
                        return M12_ERR_DYNAMIC_UNRESOLVED;
#endif
                    uint32_t sc = size_cells[1], ac = addr_cells[1];
                    if (!reserve_ranges_seen || sc != size_cells[0] || ac != addr_cells[0] ||
                        sc < 1 || sc > 2 || ac < 1 || ac > 2 || size_len != sc * 4 ||
                        (request_alignment && alignment_len != sc * 4) ||
                        (request.ranges && (!request.ranges_len || request.ranges_len % ((ac + sc) * 4))) ||
                        (request.no_map && request.reusable) ||
                        st->request_count == M12_MAX_DYNAMIC_REQUESTS)
                        return M12_ERR_DYNAMIC_RESERVATION;
                    request.has_alignment = request_alignment != NULL;
                    request.size = m12_read_cells(request_size, sc);
                    request.alignment = request_alignment ? m12_read_cells(request_alignment, sc) : 1;
                    if (!request.size || !request.alignment ||
                        (request.alignment & (request.alignment - 1)))
                        return M12_ERR_DYNAMIC_RESERVATION;
                    st->reserve_addr_cells = ac;
                    st->reserve_size_cells = sc;
                    st->requests[st->request_count++] = request;
                    resmem_reg_valid = 1;
                }
                if (!resmem_disabled && !resmem_reg_valid) {
                    return M12_ERR_DYNAMIC_RESERVATION;
                }
                in_resmem_child = 0;
            }
            if (depth == 2 && in_cpu_child) {
                if (cpu_is_device_cpu && cpu_has_reg) {
                    plan->cpu_count++;
                    if ((cpu_reg_mpidr & 0xff00ffffffULL) == (ctx->mpidr_el1 & 0xff00ffffffULL)) {
                        st->boot_cpu_found = 1;
                    }
                }
                in_cpu_child = 0;
            }
            if (node_is_gic[depth]) {
                uint32_t gic_ac = (depth > 0) ? addr_cells[depth - 1] : 2u;
                uint32_t gic_sc = (depth > 0) ? size_cells[depth - 1] : 2u;
                const uint8_t *rptr = node_reg_ptr[depth];
                uint32_t rlen = node_reg_len[depth];
                if (rptr == NULL || gic_ac < 1 || gic_ac > 2 || gic_sc < 1 || gic_sc > 2) {
                    return M12_ERR_GIC_INVALID;
                }
                uint32_t stride_bytes = (gic_ac + gic_sc) * 4u;
                if (stride_bytes == 0 || rlen < 2u * stride_bytes) {
                    return M12_ERR_GIC_INVALID;
                }
                plan->gicd_base = m12_read_cells(rptr, gic_ac);
                plan->gicd_size = m12_read_cells(rptr + gic_ac * 4u, gic_sc);
                plan->gicr_base = m12_read_cells(rptr + stride_bytes, gic_ac);
                plan->gicr_size = m12_read_cells(rptr + stride_bytes + gic_ac * 4u, gic_sc);
                st->gic_found = 1;
                node_is_gic[depth] = 0;
            }
            if (node_is_psci[depth]) {
                st->psci_found = 1;
                if (node_psci_method[depth] != 0) {
                    plan->psci_method = node_psci_method[depth];
                }
                node_is_psci[depth] = 0;
            }
        } else if (token == FDT_PROP) {
            if (depth == 0 || pos + 8 > off_struct + size_struct) {
                return M12_ERR_FDT_INVALID;
            }
            uint32_t plen = m12_be32(fdt + pos);
            uint32_t nameoff = m12_be32(fdt + pos + 4);
            pos += 8;
            if (plen > off_struct + size_struct - pos || nameoff >= size_strings) {
                return M12_ERR_FDT_INVALID;
            }
            const uint8_t *pval = fdt + pos;
            const char *pname = (const char *)(fdt + off_strings + nameoff);
            /* Verify pname NUL-termination within strings block */
            uint32_t max_s = size_strings - nameoff;
            uint32_t sl = 0;
            while (sl < max_s && pname[sl] != '\0') {
                sl++;
            }
            if (sl >= max_s) {
                return M12_ERR_FDT_INVALID;
            }
            pos += (plen + 3u) & ~3u;

            uint32_t cur = depth - 1;
            if (st->memory_only && cur != 0 &&
                !(in_memory_node && cur == 1) &&
                !(cur == 1 && m12_streq(node_names[1], "reserved-memory")) &&
                !(in_resmem_child && cur == 2))
                continue;
            if (m12_streq(pname, "#address-cells") && plen == 4) {
                addr_cells[cur] = m12_be32(pval);
            } else if (m12_streq(pname, "#size-cells") && plen == 4) {
                size_cells[cur] = m12_be32(pval);
            }
            if (m12_streq(pname, "reg")) {
                node_reg_ptr[cur] = pval;
                node_reg_len[cur] = plen;
            }

            /* Root compatible */
            if (cur == 0 && m12_streq(pname, "compatible")) {
                if (m12_strlist_contains(pval, plen, "qcom,holi") ||
                    m12_strlist_contains(pval, plen, "motorola,penang")) {
                    plan->platform_profile = M12_PLAT_PENANG_PHYSICAL;
                } else if (m12_strlist_contains(pval, plen, "qemu,virt") ||
                           m12_strlist_contains(pval, plen, "linux,dummy-virt")) {
                    plan->platform_profile = M12_PLAT_QEMU_VIRT;
                }
            }

            if (in_memory_node && cur == 1 && m12_streq(pname, "status"))
                memory_disabled = !m12_strlist_contains(pval, plen, "okay") &&
                    !m12_strlist_contains(pval, plen, "ok");

            /* /memory reg */
            if (in_memory_node && cur == 1 && m12_streq(pname, "reg")) {
                uint32_t ac = addr_cells[0];
                uint32_t sc = size_cells[0];
                uint32_t step = (ac + sc) * 4u;
                uint32_t off = 0;
                if (ac < 1 || ac > 2 || sc < 1 || sc > 2 || step == 0 || (plen % step) != 0) {
                    return M12_ERR_MEMORY_MAP;
                }
                while (off + step <= plen) {
                    uint64_t rbase = m12_read_cells(pval + off, ac);
                    uint64_t rsize = m12_read_cells(pval + off + ac * 4u, sc);
                    if (rsize > 0) {
                        if (rbase > UINT64_MAX - rsize || plan->ram_range_count >= M12_MAX_MEM_RANGES) {
                            return M12_ERR_MEMORY_MAP;
                        }
                        plan->ram_ranges[plan->ram_range_count].base = rbase;
                        plan->ram_ranges[plan->ram_range_count].size = rsize;
                        plan->ram_range_count++;
                    }
                    off += step;
                }
            }

            /* /reserved-memory ranges must be empty identity */
            if (cur == 1 && m12_streq(node_names[1], "reserved-memory") && m12_streq(pname, "ranges")) {
                if (plen != 0 || reserve_ranges_seen++) {
                    return M12_ERR_MEMORY_MAP;
                }
            }

            /* /reserved-memory/<child> */
            if (in_resmem_child && cur == 2) {
                if (m12_streq(pname, "size")) {
                    if (request_size) return M12_ERR_DYNAMIC_RESERVATION;
                    request_size = pval;
                    size_len = plen;
                } else if (m12_streq(pname, "alignment")) {
                    if (request_alignment) return M12_ERR_DYNAMIC_RESERVATION;
                    request_alignment = pval;
                    alignment_len = plen;
                } else if (m12_streq(pname, "alloc-ranges")) {
                    if (request.ranges) return M12_ERR_DYNAMIC_RESERVATION;
                    request.ranges = pval;
                    request.ranges_len = plen;
                } else if (m12_streq(pname, "no-map") || m12_streq(pname, "reusable")) {
                    uint32_t *flag = m12_streq(pname, "no-map") ? &request.no_map : &request.reusable;
                    if (plen || *flag) return M12_ERR_DYNAMIC_RESERVATION;
                    *flag = 1;
                }
                if (m12_streq(pname, "status") && m12_strlist_contains(pval, plen, "disabled")) {
                    resmem_disabled = 1;
                } else if (m12_streq(pname, "reg")) {
                    uint32_t ac = addr_cells[1];
                    uint32_t sc = size_cells[1];
                    uint32_t step = (ac + sc) * 4u;
                    uint32_t off = 0;
                    resmem_has_reg = 1;
                    if (ac < 1 || ac > 2 || sc < 1 || sc > 2 || step == 0 || plen == 0 || (plen % step) != 0) {
                        return M12_ERR_DYNAMIC_RESERVATION;
                    }
                    while (off + step <= plen) {
                        uint64_t rbase = m12_read_cells(pval + off, ac);
                        uint64_t rsize = m12_read_cells(pval + off + ac * 4u, sc);
                        if (rsize == 0) {
                            return M12_ERR_DYNAMIC_RESERVATION;
                        }
                        if (m12_add_reserved(plan, rbase, rsize) != 0) {
                            return M12_ERR_MEMORY_MAP;
                        }
                        resmem_reg_valid = 1;
                        off += step;
                    }
                }
            }

            if (st->memory_only) continue;

            /* /cpus/cpu@N */
            if (in_cpu_child && cur == 2) {
                if (m12_streq(pname, "device_type") && m12_strlist_contains(pval, plen, "cpu")) {
                    cpu_is_device_cpu = 1;
                } else if (m12_streq(pname, "reg")) {
                    uint32_t ac = addr_cells[1];
                    if ((ac == 1 || ac == 2) && plen == ac * 4u) {
                        cpu_reg_mpidr = m12_read_cells(pval, ac);
                        cpu_has_reg = 1;
                    }
                }
            }

            /* /chosen */
            if (cur == 1 && m12_streq(node_names[1], "chosen")) {
                if (!st->memory_only && m12_streq(pname, "rng-seed")) {
                    /* Exactly one 64-byte field is required. Longer inputs
                     * must not be silently truncated; duplicates are ambiguous. */
                    if (st->chosen_seed_property_seen || plen != 64u) {
                        return M12_ERR_ENTROPY_UNAVAILABLE;
                    }
                    st->chosen_seed_property_seen = 1u;
                    /* Only forward 64 bytes supplied by a dedicated bootloader
                     * entropy field, unchanged. Do not expand an 8-byte KASLR
                     * value or invent entropy from public CPU registers.
                     * Its actual unpredictability remains a trust-boundary
                     * requirement for physical release. */
                    uint32_t i;
                    uint8_t any_nonzero = 0;
                    for (i = 0; i < 64u; i++) {
                        st->chosen_seed[i] = pval[i];
                        any_nonzero |= pval[i];
                    }
                    st->has_chosen_seed = any_nonzero != 0;
                } else if (m12_streq(pname, "bootargs")) {
                    /* Linux /chosen/bootargs can contain Android-only flags,
                     * device identifiers and a command line >1024 bytes.
                     * It is input metadata, NEVER the XNU CommandLine. */
                } else if (m12_streq(pname, "linux,initrd-start") && (plen == 4 || plen == 8)) {
                    st->initrd_start = (plen == 8) ? m12_be64(pval) : (uint64_t)m12_be32(pval);
                } else if (m12_streq(pname, "linux,initrd-end") && (plen == 4 || plen == 8)) {
                    st->initrd_end = (plen == 8) ? m12_be64(pval) : (uint64_t)m12_be32(pval);
                }
            }

            /* GICv3 node */
            if (m12_streq(pname, "compatible") && m12_strlist_contains(pval, plen, "arm,gic-v3")) {
                node_is_gic[cur] = 1;
            }

            /* Arch timer */
            if (m12_streq(pname, "compatible") &&
                (m12_strlist_contains(pval, plen, "arm,armv8-timer") ||
                  m12_strlist_contains(pval, plen, "arm,armv7-timer"))) {
                st->timer_found = 1;
            }

            /* PSCI */
            if (m12_streq(pname, "compatible") &&
                (m12_strlist_contains(pval, plen, "arm,psci-1.0") ||
                 m12_strlist_contains(pval, plen, "arm,psci-0.2"))) {
                node_is_psci[cur] = 1;
            }
            if (m12_streq(pname, "method")) {
                if (m12_strlist_contains(pval, plen, "smc")) {
                    node_psci_method[cur] = 1;
                } else if (m12_strlist_contains(pval, plen, "hvc")) {
                    node_psci_method[cur] = 2;
                }
            }

            /* PL011 UART (optional on QEMU_VIRT) */
            if (m12_streq(pname, "compatible") && m12_strlist_contains(pval, plen, "arm,pl011")) {
                plan->uart_base = M12_QEMU_UART_BASE;
            }
        } else if (token == FDT_END) {
            if (depth != 0) {
                return M12_ERR_FDT_INVALID;
            }
            fdt_end = 1;
            break;
        } else {
            return M12_ERR_FDT_INVALID;
        }
    }

    if (!fdt_end) return M12_ERR_FDT_INVALID;
    if (st->memory_only)
        return plan->ram_range_count ? M12_BRIDGE_OK : M12_ERR_MEMORY_MAP;
    if (st->request_count && (plan->platform_profile != M12_PLAT_QEMU_VIRT || ctx->force_profile))
        return M12_ERR_DYNAMIC_UNRESOLVED;

    if (ctx->force_profile != 0) {
        plan->platform_profile = ctx->force_profile;
    }
    if (plan->platform_profile == M12_PLAT_UNKNOWN) {
        return M12_ERR_FDT_COMPATIBLE;
    }
    if (plan->ram_range_count == 0) {
        return M12_ERR_MEMORY_MAP;
    }
    if (!st->boot_cpu_found || plan->cpu_count == 0) {
        return M12_ERR_BOOT_CPU_MPIDR;
    }
    if (!st->gic_found || !st->timer_found) {
        return M12_ERR_GIC_INVALID;
    }
    if ((st->initrd_start == 0u) != (st->initrd_end == 0u) ||
        st->initrd_end < st->initrd_start) {
        return M12_ERR_MEMORY_MAP;
    }
    return M12_BRIDGE_OK;
}

/* --------------------------------------------------------------------------
 * Coalesce reserved regions at 16 KiB granularity and select 32 MiB window
 * -------------------------------------------------------------------------- */
static void
m12_coalesce_reserved_16k(struct m12_handoff_plan *plan)
{
    struct m12_phys_range rounded[M12_MAX_RESERVED_RANGES];
    uint32_t n = plan->reserved_count;
    uint32_t i, j;

    for (i = 0; i < n; i++) {
        uint64_t a = plan->reserved_ranges[i].base;
        uint64_t b = a + plan->reserved_ranges[i].size;
        uint64_t pa = a & ~M12_PAGE_16K_MASK;
        uint64_t pb = (b + M12_PAGE_16K_MASK) & ~M12_PAGE_16K_MASK;
        rounded[i].base = pa;
        rounded[i].size = pb - pa;
    }

    /* Insertion sort by base */
    for (i = 1; i < n; i++) {
        struct m12_phys_range key = rounded[i];
        j = i;
        while (j > 0 && rounded[j - 1].base > key.base) {
            rounded[j] = rounded[j - 1];
            j--;
        }
        rounded[j] = key;
    }

    /* Coalesce overlapping or adjacent 16 KiB ranges */
    plan->coalesced_reserved_count = 0;
    for (i = 0; i < n; i++) {
        uint64_t a = rounded[i].base;
        uint64_t b = a + rounded[i].size;
        if (plan->coalesced_reserved_count > 0) {
            uint32_t last = plan->coalesced_reserved_count - 1;
            uint64_t prev_a = plan->coalesced_reserved[last].base;
            uint64_t prev_b = prev_a + plan->coalesced_reserved[last].size;
            if (a <= prev_b) {
                if (b > prev_b) {
                    plan->coalesced_reserved[last].size = b - prev_a;
                }
                continue;
            }
        }
        plan->coalesced_reserved[plan->coalesced_reserved_count].base = a;
        plan->coalesced_reserved[plan->coalesced_reserved_count].size = b - a;
        plan->coalesced_reserved_count++;
    }
}

/* Find the lowest fit within each allowed RAM intersection, jumping over
 * rounded exclusions. The allocation order is FDT child order. */
static enum m12_bridge_status
m12_place_dynamic(struct m12_handoff_plan *plan, const struct m12_fdt_parse_state *st)
{
    uint32_t original_count = plan->reserved_count;
    for (uint32_t i = 0; i < st->request_count; i++) {
        const struct m12_dynamic_request *req = &st->requests[i];
        uint64_t best = UINT64_MAX;
        uint32_t step = (st->reserve_addr_cells + st->reserve_size_cells) * 4u;
        uint64_t alignment = req->alignment < M12_PAGE_16K ? M12_PAGE_16K : req->alignment;
        if (req->size > UINT64_MAX - M12_PAGE_16K_MASK)
            goto reject;
        uint64_t size = (req->size + M12_PAGE_16K_MASK) & ~M12_PAGE_16K_MASK;
        m12_coalesce_reserved_16k(plan);
        for (uint32_t off = 0; off < (req->ranges ? req->ranges_len : step); off += step) {
            uint64_t base = 0, end = UINT64_MAX;
            if (req->ranges) {
                base = m12_read_cells(req->ranges + off, st->reserve_addr_cells);
                uint64_t length = m12_read_cells(req->ranges + off + st->reserve_addr_cells * 4,
                                                 st->reserve_size_cells);
                if (!length || base > UINT64_MAX - length)
                    goto reject;
                end = base + length;
            }
            for (uint32_t j = 0; j < plan->ram_range_count; j++) {
                const struct m12_phys_range *ram = &plan->ram_ranges[j];
                uint64_t ram_end = ram->base + ram->size;
                uint64_t cursor = base > ram->base ? base : ram->base;
                uint64_t limit = end < ram_end ? end : ram_end;
                for (uint32_t k = 0; k <= plan->coalesced_reserved_count; k++) {
                    if (cursor > UINT64_MAX - (alignment - 1)) break;
                    cursor = (cursor + alignment - 1) & ~(alignment - 1);
                    if (cursor > limit || size > limit - cursor) break;
                    const struct m12_phys_range *excluded = &plan->coalesced_reserved[k];
                    if (k == plan->coalesced_reserved_count || cursor + size <= excluded->base) {
                        if (cursor < best) best = cursor;
                        break;
                    }
                    uint64_t excluded_end = excluded->base + excluded->size;
                    if (cursor < excluded_end) cursor = excluded_end;
                }
            }
        }
        if (best == UINT64_MAX || m12_add_reserved(plan, best, size))
            goto reject;
        plan->dynamic[i] = (struct m12_dynamic_placement){
            {best, size}, req->node_offset, M12_RESERVATION_FDT_REQUEST, req->no_map,
        };
    }
    plan->dynamic_count = st->request_count;
    return M12_BRIDGE_OK;
reject:
    plan->reserved_count = original_count;
    m12_bzero(plan->dynamic, sizeof(plan->dynamic));
    m12_coalesce_reserved_16k(plan);
    return M12_ERR_DYNAMIC_RESERVATION;
}

#if !defined(__aarch64__) && !defined(__arm64__)
int
m12_bridge_plan_memory(const void *fdt, size_t bytes, struct m12_handoff_plan *plan)
{
    struct m12_cpu_context ctx = {0};
    struct m12_fdt_parse_state st = {.memory_only = 1};
    if (!plan) return M12_ERR_FDT_INVALID;
    m12_bzero(plan, sizeof(*plan));
    if (!fdt || bytes < 40 || m12_be32((const uint8_t *)fdt + 4) > bytes)
        return M12_ERR_FDT_INVALID;
    int status = m12_parse_fdt(fdt, &ctx, plan, &st);
    if (status == M12_BRIDGE_OK)
        status = m12_place_dynamic(plan, &st);
    m12_coalesce_reserved_16k(plan);
    return status;
}
#endif

#ifdef M12_HANDOFF_PROBE
/* Emit input geometry before clipping a dry run to the admission interval. */
int
m12_bridge_probe_memory(const void *fdt, size_t bytes, struct m12_handoff_plan *plan,
                        const struct m12_phys_range *admitted,
                        const struct m12_phys_range *excluded, size_t excluded_count,
                        m12_memory_observer observe, void *opaque, int *placement_status)
{
    struct m12_cpu_context ctx;
    struct m12_fdt_parse_state st;
    m12_bzero(&ctx, sizeof(ctx));
    m12_bzero(&st, sizeof(st));
    st.memory_only = 1;
    *placement_status = M12_ERR_WINDOW_SELECTION;
    m12_bzero(plan, sizeof(*plan));
    if (!fdt || bytes < 40) return M12_ERR_FDT_INVALID;
    const uint8_t *header = fdt;
    uint32_t total = m12_be32(header + 4);
    uint32_t structure = m12_be32(header + 8), strings = m12_be32(header + 12);
    uint32_t structure_size = m12_be32(header + 36), strings_size = m12_be32(header + 32);
    if (total < 40 || total > bytes || structure < 40 || strings < 40 ||
        structure > total || structure_size > total - structure ||
        strings > total || strings_size > total - strings ||
        (structure < strings + strings_size && strings < structure + structure_size))
        return M12_ERR_FDT_INVALID;
    int status = m12_parse_fdt(fdt, &ctx, plan, &st);
    if (status != M12_BRIDGE_OK) return status;
    for (uint32_t i = 0; i < plan->ram_range_count; i++) {
        struct m12_phys_range r = plan->ram_ranges[i];
        if (observe(opaque, M12_MEM_RAM, i, r.base, r.size, 0))
            return M12_ERR_MEMORY_MAP;
    }
    for (uint32_t i = 0; i < plan->reserved_count; i++) {
        struct m12_phys_range r = plan->reserved_ranges[i];
        if (observe(opaque, M12_MEM_STATIC, i, r.base, r.size, 0))
            return M12_ERR_MEMORY_MAP;
    }
    for (uint32_t i = 0; i < st.request_count; i++) {
        const struct m12_dynamic_request *req = &st.requests[i];
        uint32_t flags = req->no_map | (req->reusable << 1) | (req->has_alignment << 2);
        if (observe(opaque, M12_MEM_REQUEST, i, req->size, req->alignment, flags))
            return M12_ERR_MEMORY_MAP;
        uint32_t ac = st.reserve_addr_cells, sc = st.reserve_size_cells;
        for (uint32_t off = 0; off < req->ranges_len; off += (ac + sc) * 4) {
            uint64_t base = m12_read_cells(req->ranges + off, ac);
            uint64_t size = m12_read_cells(req->ranges + off + ac * 4, sc);
            if (!size || base > UINT64_MAX - size) return M12_ERR_DYNAMIC_RESERVATION;
            if (observe(opaque, M12_MEM_ALLOC_RANGE, i, base, size, 0))
                return M12_ERR_MEMORY_MAP;
        }
    }
    if (!admitted || !admitted->size || admitted->base > UINT64_MAX - admitted->size ||
        ((admitted->base | admitted->size) & M12_PAGE_16K_MASK))
        return M12_BRIDGE_OK;
    uint64_t end = admitted->base + admitted->size;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < plan->ram_range_count; i++) {
        struct m12_phys_range r = plan->ram_ranges[i];
        uint64_t hi = r.base + r.size;
        uint64_t lo = r.base > admitted->base ? r.base : admitted->base;
        if (hi > end) hi = end;
        if (lo < hi) plan->ram_ranges[kept++] = (struct m12_phys_range){lo, hi - lo};
    }
    plan->ram_range_count = kept;
    if (!kept) return M12_BRIDGE_OK;
    for (size_t i = 0; i < excluded_count; i++) {
        if (m12_add_reserved(plan, excluded[i].base, excluded[i].size))
            return M12_ERR_MEMORY_MAP;
    }
    *placement_status = m12_place_dynamic(plan, &st);
    if (*placement_status == M12_BRIDGE_OK) {
        for (uint32_t i = 0; i < plan->dynamic_count; i++) {
            const struct m12_dynamic_placement *p = &plan->dynamic[i];
            if (observe(opaque, M12_MEM_PLACEMENT, i, p->range.base, p->range.size, p->no_map))
                return M12_ERR_MEMORY_MAP;
        }
    }
    return M12_BRIDGE_OK;
}
#endif

static int
m12_window_overlaps_reserved(const struct m12_handoff_plan *plan, uint64_t wbase, uint64_t wsize)
{
    uint64_t wend = wbase + wsize;
    uint32_t i;
    for (i = 0; i < plan->coalesced_reserved_count; i++) {
        uint64_t ra = plan->coalesced_reserved[i].base;
        uint64_t rb = ra + plan->coalesced_reserved[i].size;
        if (wbase < rb && ra < wend) {
            return 1;
        }
    }
    return 0;
}

static enum m12_bridge_status
m12_select_window(struct m12_handoff_plan *plan, uint64_t min_top_offset)
{
    static const uint64_t candidates[] = {
        0x10000000ULL, /* 256 MiB */
        0x08000000ULL, /* 128 MiB */
        0x06000000ULL, /* 96 MiB */
        0x04000000ULL  /* 64 MiB */
    };
    uint32_t ci, ri;

    for (ci = 0; ci < sizeof(candidates) / sizeof(candidates[0]); ci++) {
        uint64_t wsize = candidates[ci];
        if (wsize < min_top_offset + 16u * M12_PAGE_16K) {
            continue;
        }
        for (ri = 0; ri < plan->ram_range_count; ri++) {
            uint64_t rbase = plan->ram_ranges[ri].base;
            uint64_t rend  = rbase + plan->ram_ranges[ri].size;
            uint64_t cand  = (rbase + M12_L2_BLOCK_MASK) & ~M12_L2_BLOCK_MASK;
            while (cand <= rend && rend - cand >= wsize) {
                if (!m12_window_overlaps_reserved(plan, cand, wsize)) {
                    plan->phys_base = cand;
                    plan->mem_size  = wsize;
                    return M12_BRIDGE_OK;
                }
                cand += M12_L2_BLOCK_SIZE;
            }
        }
    }
    return M12_ERR_WINDOW_SELECTION;
}

/* --------------------------------------------------------------------------
 * Mach-O 64-bit validator & slide-0 loader
 * -------------------------------------------------------------------------- */
#define MH_MAGIC_64     0xfeedfacfu
#define CPU_TYPE_ARM64  0x0100000cu
#define LC_SEGMENT_64   0x00000019u
#define LC_UNIXTHREAD   0x00000005u

static enum m12_bridge_status
m12_parse_macho(const uint8_t *macho, size_t macho_len, struct m12_handoff_plan *plan,
                uint64_t *vm_lo_out, uint64_t *vm_hi_out)
{
    uint32_t magic, cputype, filetype, ncmds, sizeofcmds;
    uint32_t off, end, i, j;
    uint64_t vm_lo = UINT64_MAX, vm_hi = 0;
    int has_entry = 0;

    if (macho == NULL || macho_len < 32u) {
        return M12_ERR_MACHO_INVALID;
    }
    magic      = m12_le32(macho + 0);
    cputype    = m12_le32(macho + 4);
    filetype   = m12_le32(macho + 12);
    ncmds      = m12_le32(macho + 16);
    sizeofcmds = m12_le32(macho + 20);

    if (magic != MH_MAGIC_64 || cputype != CPU_TYPE_ARM64 || (filetype != 2u && filetype != 12u)) {
        return M12_ERR_MACHO_INVALID;
    }
    if (sizeofcmds > macho_len - 32u || sizeofcmds > UINT32_MAX - 32u ||
        ncmds == 0 || ncmds > 256u) {
        return M12_ERR_MACHO_INVALID;
    }

    off = 32u;
    end = 32u + sizeofcmds;
    plan->segment_count = 0;

    for (i = 0; i < ncmds; i++) {
        if (end - off < 8u) {
            return M12_ERR_MACHO_INVALID;
        }
        uint32_t cmd = m12_le32(macho + off);
        uint32_t cmdsize = m12_le32(macho + off + 4);
        if (cmdsize < 8u || (cmdsize & 7u) != 0 || cmdsize > end - off) {
            return M12_ERR_MACHO_INVALID;
        }
        if (cmd == LC_SEGMENT_64) {
            if (cmdsize < 72u || (cmdsize - 72u) % 80u != 0 ||
                m12_le32(macho + off + 64u) != (cmdsize - 72u) / 80u) {
                return M12_ERR_MACHO_INVALID;
            }
            uint64_t vmaddr   = m12_le64(macho + off + 24);
            uint64_t vmsize   = m12_le64(macho + off + 32);
            uint64_t fileoff  = m12_le64(macho + off + 40);
            uint64_t filesize = m12_le64(macho + off + 48);
            uint32_t maxprot  = m12_le32(macho + off + 56);
            uint32_t initprot = m12_le32(macho + off + 60);

            if (vmsize > 0) {
                if (vmaddr < M12_VIRT_BASE + M12_LINK_OFFSET_MIN || vmaddr > UINT64_MAX - vmsize) {
                    return M12_ERR_MACHO_INVALID;
                }
                if (filesize > vmsize || fileoff > macho_len || filesize > macho_len - fileoff) {
                    return M12_ERR_MACHO_INVALID;
                }
                if (plan->segment_count >= M12_MAX_MACHO_SEGMENTS) {
                    return M12_ERR_MACHO_INVALID;
                }
                struct m12_macho_seg *seg = &plan->segments[plan->segment_count++];
                m12_memcpy(seg->name, macho + off + 8, 16);
                seg->vmaddr   = vmaddr;
                seg->vmsize   = vmsize;
                seg->fileoff  = fileoff;
                seg->filesize = filesize;
                seg->maxprot  = maxprot;
                seg->initprot = initprot;

                if (vmaddr < vm_lo) {
                    vm_lo = vmaddr;
                }
                if (vmaddr + vmsize > vm_hi) {
                    vm_hi = vmaddr + vmsize;
                }
            }
        } else if (cmd == LC_UNIXTHREAD) {
            /* ARM_THREAD_STATE64: flavor=6, count=68 (272 bytes); x0..x28, fp, lr, sp, pc (index 32) */
            if (has_entry || cmdsize != 16u + 68u * 4u) {
                return M12_ERR_MACHO_INVALID;
            }
            uint32_t flavor = m12_le32(macho + off + 8);
            uint32_t count  = m12_le32(macho + off + 12);
            if (flavor != 6u || count != 68u) {
                return M12_ERR_MACHO_INVALID;
            }
            plan->entry_va = m12_le64(macho + off + 16u + 32u * 8u);
            has_entry = 1;
        }
        off += cmdsize;
    }

    if (off != end || plan->segment_count == 0 || !has_entry || vm_lo >= vm_hi ||
        (plan->entry_va & 3u) != 0) {
        return M12_ERR_MACHO_INVALID;
    }

    /* The entry instruction must be backed by bytes in an executable segment. */
    int entry_in_exec = 0;
    for (i = 0; i < plan->segment_count; i++) {
        uint64_t a0 = plan->segments[i].vmaddr;
        uint64_t a1 = a0 + plan->segments[i].vmsize;
        uint64_t file_size = plan->segments[i].filesize;
        if (plan->entry_va >= a0 && file_size >= 4u &&
            plan->entry_va - a0 <= file_size - 4u && (plan->segments[i].initprot & 4u)) {
            entry_in_exec = 1;
        }
        for (j = i + 1; j < plan->segment_count; j++) {
            uint64_t b0 = plan->segments[j].vmaddr;
            uint64_t b1 = b0 + plan->segments[j].vmsize;
            if (a0 < b1 && b0 < a1) {
                return M12_ERR_SEGMENT_OVERLAP;
            }
        }
    }
    if (!entry_in_exec) {
        return M12_ERR_MACHO_INVALID;
    }

    *vm_lo_out = vm_lo;
    *vm_hi_out = vm_hi;
    return M12_BRIDGE_OK;
}

/* --------------------------------------------------------------------------
 * Apple Device Tree (ADT) serializer
 * -------------------------------------------------------------------------- */
struct m12_adt_writer {
    uint8_t *base;
    uint32_t cap;
    uint32_t pos;
    uint32_t overflow;
    uint32_t hdr_stack[8];
    uint32_t nprops_stack[8];
    uint32_t nchildren_stack[8];
    uint32_t depth;
};

static void
adt_begin_node(struct m12_adt_writer *w, const char *name)
{
    if (w->overflow || w->depth >= 8 || w->pos + 8u > w->cap) {
        w->overflow = 1;
        return;
    }
    if (w->depth > 0) {
        w->nchildren_stack[w->depth - 1]++;
    }
    w->hdr_stack[w->depth] = w->pos;
    w->nprops_stack[w->depth] = 0;
    w->nchildren_stack[w->depth] = 0;
    w->pos += 8u;
    w->depth++;
    /* Every Apple DT node starts with its "name" property */
    size_t nlen = m12_strlen(name) + 1u;
    if (w->pos + 36u + ((nlen + 3u) & ~3u) > w->cap) {
        w->overflow = 1;
        return;
    }
    m12_bzero(w->base + w->pos, 32u);
    m12_memcpy(w->base + w->pos, "name", 4u);
    m12_put_le32(w->base + w->pos + 32u, (uint32_t)nlen);
    w->pos += 36u;
    m12_bzero(w->base + w->pos, (nlen + 3u) & ~3u);
    m12_memcpy(w->base + w->pos, name, nlen);
    w->pos += (uint32_t)((nlen + 3u) & ~3u);
    w->nprops_stack[w->depth - 1]++;
}

static void
adt_prop(struct m12_adt_writer *w, const char *key, const void *val, uint32_t len)
{
    size_t klen = m12_strlen(key);
    uint32_t aligned = (len + 3u) & ~3u;
    if (w->overflow || w->depth == 0 || klen >= 32u || w->pos + 36u + aligned > w->cap) {
        w->overflow = 1;
        return;
    }
    m12_bzero(w->base + w->pos, 32u);
    m12_memcpy(w->base + w->pos, key, klen);
    m12_put_le32(w->base + w->pos + 32u, len);
    w->pos += 36u;
    m12_bzero(w->base + w->pos, aligned);
    if (len > 0 && val != NULL) {
        m12_memcpy(w->base + w->pos, val, len);
    }
    w->pos += aligned;
    w->nprops_stack[w->depth - 1]++;
}

static void
adt_prop_str(struct m12_adt_writer *w, const char *key, const char *str)
{
    adt_prop(w, key, str, (uint32_t)(m12_strlen(str) + 1u));
}

static void
adt_prop_u32(struct m12_adt_writer *w, const char *key, uint32_t v)
{
    uint8_t buf[4];
    m12_put_le32(buf, v);
    adt_prop(w, key, buf, 4u);
}

static void
adt_prop_u64(struct m12_adt_writer *w, const char *key, uint64_t v)
{
    uint8_t buf[8];
    m12_put_le64(buf, v);
    adt_prop(w, key, buf, 8u);
}

static void
adt_end_node(struct m12_adt_writer *w)
{
    if (w->overflow || w->depth == 0) {
        w->overflow = 1;
        return;
    }
    w->depth--;
    uint32_t hdr = w->hdr_stack[w->depth];
    m12_put_le32(w->base + hdr, w->nprops_stack[w->depth]);
    m12_put_le32(w->base + hdr + 4u, w->nchildren_stack[w->depth]);
}

static enum m12_bridge_status
m12_build_adt(uint8_t *buf, uint32_t cap, const struct m12_cpu_context *ctx,
              const struct m12_fdt_parse_state *st, struct m12_handoff_plan *plan)
{
    struct m12_adt_writer w;
    uint8_t seed[64];
    uint8_t ranges[24];
    uint8_t gic_reg[32];

    m12_bzero(&w, sizeof(w));
    w.base = buf;
    w.cap  = cap;

    if (!st->has_chosen_seed) {
        return M12_ERR_ENTROPY_UNAVAILABLE;
    }
    m12_memcpy(seed, st->chosen_seed, sizeof(seed));

    adt_begin_node(&w, "device-tree");
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL) {
        static const char compat_penang[] = "penang,physical\0motorola,penang\0qcom,holi";
        adt_prop_str(&w, "model", "PENANG_PHYSICAL");
        adt_prop(&w, "compatible", compat_penang, sizeof(compat_penang));
        adt_prop_str(&w, "target-type", "PENANG_PHYSICAL");
    } else {
        adt_prop_str(&w, "model", "QEMU_VIRT");
        adt_prop_str(&w, "compatible", "qemu,virt");
        adt_prop_str(&w, "target-type", "QEMU_VIRT");
    }
    adt_prop_u32(&w, "#address-cells", 2u);
    adt_prop_u32(&w, "#size-cells", 2u);
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL) {
        /* This is a run-local result from bridge CPU feature validation,
         * NOT independent evidence of the Motorola's actual capabilities. */
        uint32_t tgran16 = (uint32_t)((ctx->id_aa64mmfr0_el1 >> 20) & 0xfu);
        if (tgran16 != 1u && tgran16 != 2u) {
            return M12_ERR_NO_TGRAN16;
        }
        adt_prop_u32(&w, "penang,bridge-tgran16", 1u);
    }

    /* /chosen */
    adt_begin_node(&w, "chosen");
    adt_prop(&w, "random-seed", seed, 64u);
    /* ADT owns its own copy; do not retain a second seed on the stack. */
    m12_bzero(seed, sizeof(seed));
    adt_prop_str(&w, "penang,loader-entropy-source", "fdt-rng-seed-64");
    adt_prop_u64(&w, "dram-base", plan->phys_base);
    adt_prop_u64(&w, "dram-size", plan->mem_size);
    adt_prop_u32(&w, "debug-enabled", 1u);
    adt_prop_str(&w, "firmware-version", "penang-m12-arm64-boot-bridge");
    adt_begin_node(&w, "memory-map");
    if (plan->ramdisk_size != 0) {
        uint8_t rd_prop[16];
        m12_put_le64(rd_prop, plan->ramdisk_pa);
        m12_put_le64(rd_prop + 8, plan->ramdisk_size);
        adt_prop(&w, "RAMDisk", rd_prop, 16u);
    }
    adt_end_node(&w); /* memory-map */
    adt_end_node(&w); /* chosen */

    /* /defaults */
    adt_begin_node(&w, "defaults");
    adt_prop_u32(&w, "serial-device", plan->uart_base != 0 ? 1u : 0u);
    adt_end_node(&w); /* defaults */

    /* /cpus/cpu0 */
    adt_begin_node(&w, "cpus");
    adt_begin_node(&w, "cpu0");
    adt_prop_str(&w, "device_type", "cpu");
    adt_prop_str(&w, "state", "running");
    adt_prop_u32(&w, "reg", (uint32_t)(plan->boot_mpidr & 0xffffffffu));
    adt_prop_u32(&w, "cpu-id", 0u);
    adt_prop_u32(&w, "timebase-frequency", (uint32_t)ctx->cntfrq_el0);
    adt_prop_u32(&w, "fixed-frequency", (uint32_t)ctx->cntfrq_el0);
    adt_prop_u32(&w, "bus-frequency", (uint32_t)ctx->cntfrq_el0);
    /* Do NOT invent a Penang CPU frequency. CNTFRQ is a timer rate,
     * not a measured core frequency. QEMU virtual fixture retains its value. */
    if (plan->platform_profile == M12_PLAT_QEMU_VIRT) {
        adt_prop_u64(&w, "clock-frequency", 1000000000ULL);
    }
    adt_end_node(&w); /* cpu0 */
    adt_end_node(&w); /* cpus */

    /* /arm-io */
    adt_begin_node(&w, "arm-io");
    adt_prop_str(&w, "device_type",
                 plan->platform_profile == M12_PLAT_PENANG_PHYSICAL ? "penang-io" : "qemu-virt");
    m12_put_le64(ranges + 0, 0ULL);
    m12_put_le64(ranges + 8, plan->soc_io_base);
    m12_put_le64(ranges + 16, plan->soc_io_size);
    adt_prop(&w, "ranges", ranges, sizeof(ranges));
    adt_prop_u32(&w, "#address-cells", 2u);
    adt_prop_u32(&w, "#size-cells", 2u);

    /* /arm-io/gic */
    adt_begin_node(&w, "gic");
    adt_prop_str(&w, "device_type", "interrupt-controller");
    adt_prop_str(&w, "compatible", "arm,gic-v3");
    adt_prop_str(&w, "interrupt-controller", "master");
    m12_put_le64(gic_reg + 0,  plan->gicd_base - plan->soc_io_base);
    m12_put_le64(gic_reg + 8,  plan->gicd_size);
    m12_put_le64(gic_reg + 16, plan->gicr_base - plan->soc_io_base);
    /* Single boot core redistributor frame (0x20000) for initial XNU UP bring-up */
    m12_put_le64(gic_reg + 24, M12_PENANG_GICR_PE_SIZE);
    adt_prop(&w, "reg", gic_reg, sizeof(gic_reg));
    adt_end_node(&w); /* gic */

    if (plan->uart_base != 0) {
        uint8_t ureg[16];
        adt_begin_node(&w, "uart0");
        adt_prop_str(&w, "device_type", "uart");
        adt_prop_str(&w, "compatible", "arm,pl011");
        m12_put_le64(ureg + 0, plan->uart_base - plan->soc_io_base);
        m12_put_le64(ureg + 8, 0x1000ULL);
        adt_prop(&w, "reg", ureg, sizeof(ureg));
        adt_prop_u32(&w, "AAPL,phandle", 1u);
        adt_end_node(&w); /* uart0 */
    }

    adt_end_node(&w); /* arm-io */
    adt_end_node(&w); /* device-tree */

    if (w.overflow || w.depth != 0 || w.pos < 8u || w.pos > cap) {
        return M12_ERR_ADT_OVERFLOW;
    }
    plan->dt_len = w.pos;
    return M12_BRIDGE_OK;
}

/* --------------------------------------------------------------------------
 * Minimal 8x8 ASCII bitmap font & C0 + Text Framebuffer Renderer
 * Strictly confined to [fb_base, fb_base + 0x5F6C0) (390,848 bytes)
 * -------------------------------------------------------------------------- */
static void
m12_glyph_rows(char c, uint8_t rows[8])
{
    /* Compact 8x8 bitmap table for uppercase, digits, punctuation */
    switch (c) {
    case '0': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x6e;rows[3]=0x76;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case '1': rows[0]=0x18;rows[1]=0x38;rows[2]=0x18;rows[3]=0x18;rows[4]=0x18;rows[5]=0x18;rows[6]=0x7e;rows[7]=0x00; break;
    case '2': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x06;rows[3]=0x1c;rows[4]=0x30;rows[5]=0x66;rows[6]=0x7e;rows[7]=0x00; break;
    case '3': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x06;rows[3]=0x1c;rows[4]=0x06;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case '4': rows[0]=0x0c;rows[1]=0x1c;rows[2]=0x3c;rows[3]=0x6c;rows[4]=0x7e;rows[5]=0x0c;rows[6]=0x0c;rows[7]=0x00; break;
    case '5': rows[0]=0x7e;rows[1]=0x60;rows[2]=0x7c;rows[3]=0x06;rows[4]=0x06;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case '6': rows[0]=0x1c;rows[1]=0x30;rows[2]=0x60;rows[3]=0x7c;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case '7': rows[0]=0x7e;rows[1]=0x66;rows[2]=0x06;rows[3]=0x0c;rows[4]=0x18;rows[5]=0x18;rows[6]=0x18;rows[7]=0x00; break;
    case '8': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x3c;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case '9': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x3e;rows[4]=0x06;rows[5]=0x0c;rows[6]=0x38;rows[7]=0x00; break;
    case 'A': rows[0]=0x18;rows[1]=0x3c;rows[2]=0x66;rows[3]=0x66;rows[4]=0x7e;rows[5]=0x66;rows[6]=0x66;rows[7]=0x00; break;
    case 'B': rows[0]=0x7c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x7c;rows[4]=0x66;rows[5]=0x66;rows[6]=0x7c;rows[7]=0x00; break;
    case 'C': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x60;rows[3]=0x60;rows[4]=0x60;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case 'D': rows[0]=0x78;rows[1]=0x6c;rows[2]=0x66;rows[3]=0x66;rows[4]=0x66;rows[5]=0x6c;rows[6]=0x78;rows[7]=0x00; break;
    case 'E': rows[0]=0x7e;rows[1]=0x60;rows[2]=0x60;rows[3]=0x78;rows[4]=0x60;rows[5]=0x60;rows[6]=0x7e;rows[7]=0x00; break;
    case 'F': rows[0]=0x7e;rows[1]=0x60;rows[2]=0x60;rows[3]=0x78;rows[4]=0x60;rows[5]=0x60;rows[6]=0x60;rows[7]=0x00; break;
    case 'G': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x60;rows[3]=0x6e;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3e;rows[7]=0x00; break;
    case 'H': rows[0]=0x66;rows[1]=0x66;rows[2]=0x66;rows[3]=0x7e;rows[4]=0x66;rows[5]=0x66;rows[6]=0x66;rows[7]=0x00; break;
    case 'I': rows[0]=0x3c;rows[1]=0x18;rows[2]=0x18;rows[3]=0x18;rows[4]=0x18;rows[5]=0x18;rows[6]=0x3c;rows[7]=0x00; break;
    case 'J': rows[0]=0x1e;rows[1]=0x0c;rows[2]=0x0c;rows[3]=0x0c;rows[4]=0x0c;rows[5]=0x6c;rows[6]=0x38;rows[7]=0x00; break;
    case 'K': rows[0]=0x66;rows[1]=0x6c;rows[2]=0x78;rows[3]=0x70;rows[4]=0x78;rows[5]=0x6c;rows[6]=0x66;rows[7]=0x00; break;
    case 'L': rows[0]=0x60;rows[1]=0x60;rows[2]=0x60;rows[3]=0x60;rows[4]=0x60;rows[5]=0x60;rows[6]=0x7e;rows[7]=0x00; break;
    case 'M': rows[0]=0x63;rows[1]=0x77;rows[2]=0x7f;rows[3]=0x6b;rows[4]=0x63;rows[5]=0x63;rows[6]=0x63;rows[7]=0x00; break;
    case 'N': rows[0]=0x66;rows[1]=0x76;rows[2]=0x7e;rows[3]=0x7e;rows[4]=0x6e;rows[5]=0x66;rows[6]=0x66;rows[7]=0x00; break;
    case 'O': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x66;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case 'P': rows[0]=0x7c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x7c;rows[4]=0x60;rows[5]=0x60;rows[6]=0x60;rows[7]=0x00; break;
    case 'Q': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x66;rows[4]=0x6a;rows[5]=0x6c;rows[6]=0x36;rows[7]=0x00; break;
    case 'R': rows[0]=0x7c;rows[1]=0x66;rows[2]=0x66;rows[3]=0x7c;rows[4]=0x6c;rows[5]=0x66;rows[6]=0x66;rows[7]=0x00; break;
    case 'S': rows[0]=0x3c;rows[1]=0x66;rows[2]=0x60;rows[3]=0x3c;rows[4]=0x06;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case 'T': rows[0]=0x7e;rows[1]=0x18;rows[2]=0x18;rows[3]=0x18;rows[4]=0x18;rows[5]=0x18;rows[6]=0x18;rows[7]=0x00; break;
    case 'U': rows[0]=0x66;rows[1]=0x66;rows[2]=0x66;rows[3]=0x66;rows[4]=0x66;rows[5]=0x66;rows[6]=0x3c;rows[7]=0x00; break;
    case 'V': rows[0]=0x66;rows[1]=0x66;rows[2]=0x66;rows[3]=0x66;rows[4]=0x66;rows[5]=0x3c;rows[6]=0x18;rows[7]=0x00; break;
    case 'W': rows[0]=0x63;rows[1]=0x63;rows[2]=0x63;rows[3]=0x6b;rows[4]=0x7f;rows[5]=0x77;rows[6]=0x63;rows[7]=0x00; break;
    case 'X': rows[0]=0x66;rows[1]=0x66;rows[2]=0x3c;rows[3]=0x18;rows[4]=0x3c;rows[5]=0x66;rows[6]=0x66;rows[7]=0x00; break;
    case 'Y': rows[0]=0x66;rows[1]=0x66;rows[2]=0x66;rows[3]=0x3c;rows[4]=0x18;rows[5]=0x18;rows[6]=0x18;rows[7]=0x00; break;
    case 'Z': rows[0]=0x7e;rows[1]=0x06;rows[2]=0x0c;rows[3]=0x18;rows[4]=0x30;rows[5]=0x60;rows[6]=0x7e;rows[7]=0x00; break;
    case ':': rows[0]=0x00;rows[1]=0x18;rows[2]=0x18;rows[3]=0x00;rows[4]=0x18;rows[5]=0x18;rows[6]=0x00;rows[7]=0x00; break;
    case '_': rows[0]=0x00;rows[1]=0x00;rows[2]=0x00;rows[3]=0x00;rows[4]=0x00;rows[5]=0x00;rows[6]=0x7e;rows[7]=0x00; break;
    case '-': rows[0]=0x00;rows[1]=0x00;rows[2]=0x00;rows[3]=0x7e;rows[4]=0x00;rows[5]=0x00;rows[6]=0x00;rows[7]=0x00; break;
    default:  rows[0]=0x00;rows[1]=0x00;rows[2]=0x00;rows[3]=0x00;rows[4]=0x00;rows[5]=0x00;rows[6]=0x00;rows[7]=0x00; break;
    }
}

static void
m12_format_hex(char *dst, uint64_t val, unsigned nibbles)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned i;
    for (i = 0; i < nibbles; i++) {
        unsigned shift = (nibbles - 1u - i) * 4u;
        dst[i] = hex[(val >> shift) & 0xfu];
    }
}

static void
m12_render_diagnostics(const struct m12_cpu_context *ctx, struct m12_handoff_plan *plan)
{
    uint8_t *pkt = plan->c0_packet;
    char lines[8][26];
    uint32_t r, c;

    m12_bzero(pkt, M12_C0_PACKET_BYTES);
    /* Magic "C0Bridge" (distinct from Stage-0 "C0Penang" and XNU kernel "C0XnuM12") */
    m12_memcpy(pkt + 0, "C0Bridge", 8u);

    uint32_t tgran16 = (uint32_t)((ctx->id_aa64mmfr0_el1 >> 20) & 0xfu);
    uint32_t fdt_ok  = (plan->status != M12_ERR_ABI_ARGS && plan->status != M12_ERR_FDT_INVALID) ? 1u : 0u;
    uint32_t vflags  = 0x02u | /* version 2 (M12 Bridge) */
                       (ctx->fb_enabled ? (1u << 4) : 0u) |
                       ((plan->status == M12_ERR_EXCEPTION_TAKEN) ? (1u << 5) : 0u) |
                       (fdt_ok ? (1u << 6) : 0u) |
                       (((tgran16 == 1u || tgran16 == 2u) ? 1u : 0u) << 7) |
                       ((plan->status & 0xffu) << 8) |
                       ((plan->platform_profile & 0xfu) << 16);

    m12_put_le32(pkt + 8,   vflags);
    m12_put_le32(pkt + 12,  plan->fdt_totalsize);
    m12_put_le64(pkt + 16,  ctx->x0);
    m12_put_le64(pkt + 24,  plan->phys_base);
    m12_put_le64(pkt + 32,  plan->mem_size);
    m12_put_le64(pkt + 40,  plan->args_pa);
    m12_put_le64(pkt + 48,  plan->dt_pa);
    m12_put_le64(pkt + 56,  plan->entry_pa);
    m12_put_le64(pkt + 64,  ctx->active_el_raw);
    m12_put_le64(pkt + 72,  ctx->sctlr_el1);
    m12_put_le64(pkt + 80,  ctx->daif);
    m12_put_le64(pkt + 88,  ctx->id_aa64mmfr0_el1);
    m12_put_le64(pkt + 96,  ctx->mpidr_el1);
    m12_put_le64(pkt + 104, ctx->cntfrq_el0);
    m12_put_le64(pkt + 112, plan->top_of_kernel_data);
    m12_put_le64(pkt + 120, ctx->esr_el1);
    m12_put_le32(pkt + 128, 0xa55a3cc3u);
    uint32_t crc = m12_crc32c(pkt, 132u);
    m12_put_le32(pkt + 132, crc);

    if (!ctx->fb_enabled || plan->fb_base_pa == 0) {
        return;
    }

    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)plan->fb_base_pa;
    /* 1. Render 64x17 8x8 C0 barcode at x=0..511, y=0..135 */
    uint32_t bit_idx;
    for (bit_idx = 0; bit_idx < M12_C0_PACKET_BYTES * 8u; bit_idx++) {
        uint32_t byte_i = bit_idx >> 3;
        uint32_t bit_in = 7u - (bit_idx & 7u);
        uint32_t bit_v  = (pkt[byte_i] >> bit_in) & 1u;
        uint32_t pixel  = bit_v ? 0xffffffffu : 0xff000000u;
        uint32_t cell_x = (bit_idx & 63u) * 8u;
        uint32_t cell_y = (bit_idx >> 6) * 8u;
        uint32_t dy, dx;
        for (dy = 0; dy < 8u; dy++) {
            uint32_t off_words = (cell_y + dy) * (M12_PENANG_FB_STRIDE / 4u) + cell_x;
            for (dx = 0; dx < 8u; dx++) {
                fb[off_words + dx] = pixel;
            }
        }
    }

    /* 2. Format 8x26 text lines (Rows 0..3: Bridge telemetry; Rows 4..7: XNU slot) */
    for (r = 0; r < 8; r++) {
        for (c = 0; c < 26; c++) {
            lines[r][c] = ' ';
        }
    }
    /* Row 0: "PENANG BRIDGE M12" */
    m12_memcpy(lines[0], "PENANG BRIDGE M12", 17u);
    /* Row 1: "ST:<status_name>" */
    m12_memcpy(lines[1], "ST:", 3u);
    const char *sname = m12_status_name(plan->status);
    size_t slen = m12_strlen(sname);
    if (slen > 23u) slen = 23u;
    m12_memcpy(lines[1] + 3, sname, slen);
    /* Row 2: "PB:<8hex> SZ:<8hex>" */
    m12_memcpy(lines[2], "PB:", 3u);
    m12_format_hex(lines[2] + 3, plan->phys_base, 8u);
    m12_memcpy(lines[2] + 12, "SZ:", 3u);
    m12_format_hex(lines[2] + 15, plan->mem_size, 8u);
    /* Row 3: "BA:<8hex> DT:<8hex>" */
    m12_memcpy(lines[3], "BA:", 3u);
    m12_format_hex(lines[3] + 3, plan->args_pa, 8u);
    m12_memcpy(lines[3] + 12, "DT:", 3u);
    m12_format_hex(lines[3] + 15, plan->dt_pa, 8u);
    /* Row 4..7: XNU Kernel Checkpoint Slot (initialized to WAITING by Bridge) */
    m12_memcpy(lines[4], "XNU:WAITING_ENTRY", 17u);
    m12_memcpy(lines[5], "EP:", 3u);
    m12_format_hex(lines[5] + 3, plan->entry_pa, 16u);
    m12_memcpy(lines[6], "MMU:OFF PE:NONE", 15u);
    m12_memcpy(lines[7], "CRC:", 4u);
    m12_format_hex(lines[7] + 4, crc, 8u);

    /* Render 8x26 text cells at x=512..719, y=0..127 (each char is 8x16 pixels) */
    for (r = 0; r < 8u; r++) {
        for (c = 0; c < 26u; c++) {
            uint8_t grows[8];
            uint32_t gr, dy, dx;
            m12_glyph_rows(lines[r][c], grows);
            for (gr = 0; gr < 8u; gr++) {
                uint8_t bits = grows[gr];
                for (dy = 0; dy < 2u; dy++) {
                    uint32_t y = r * 16u + gr * 2u + dy;
                    uint32_t base_w = y * (M12_PENANG_FB_STRIDE / 4u) + 512u + c * 8u;
                    for (dx = 0; dx < 8u; dx++) {
                        uint32_t fg = (r < 4u) ? 0xffffffffu : 0xffccccccu;
                        uint32_t px = ((bits >> (7u - dx)) & 1u) ? fg : 0xff000000u;
                        fb[base_w + dx] = px;
                    }
                }
            }
        }
    }
}

void
m12_bridge_record_exception(const struct m12_cpu_context *ctx, struct m12_handoff_plan *plan,
                            uint64_t esr, uint64_t far, uint64_t elr)
{
    struct m12_cpu_context exc_ctx;
    m12_memcpy(&exc_ctx, ctx, sizeof(exc_ctx));
    exc_ctx.esr_el1 = esr;
    exc_ctx.far_el1 = far;
    exc_ctx.elr_el1 = elr;
    plan->status = M12_ERR_EXCEPTION_TAKEN;
    m12_render_diagnostics(&exc_ctx, plan);
}

/* --------------------------------------------------------------------------
 * Main Boot Bridge Pipeline: Motorola ABL Context -> XNU 11417 Handoff
 * -------------------------------------------------------------------------- */
int
m12_bridge_execute(const struct m12_cpu_context *ctx, struct m12_handoff_plan *plan, int commit_memory)
{
    struct m12_fdt_parse_state st;
    uint64_t vm_lo = 0, vm_hi = 0;
    enum m12_bridge_status status;
    uint32_t i;

    if (ctx == NULL || plan == NULL) {
        return M12_ERR_ABI_ARGS;
    }
    m12_bzero(plan, sizeof(*plan));
    m12_bzero(&st, sizeof(st));
    plan->virt_base  = M12_VIRT_BASE;
    plan->fb_base_pa = ctx->fb_enabled ? M12_PENANG_FB_BASE : 0ULL;
    plan->boot_mpidr = ctx->mpidr_el1 & 0xff00ffffffULL;

    /* 1. Validate Linux ARM64 entry ABI: x0 != 0, 8-byte aligned; x1 == x2 == x3 == 0 */
    if (ctx->x0 == 0 || (ctx->x0 & 7ULL) != 0 || ctx->x1 != 0 || ctx->x2 != 0 || ctx->x3 != 0) {
        plan->status = M12_ERR_ABI_ARGS;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 2. Validate CPU Execution State: active EL == EL1 (0x4), MMU/C/I off, TGran16 supported, CNTFRQ != 0 */
    if (((ctx->active_el_raw >> 2) & 3ULL) != 1ULL) {
        plan->status = M12_ERR_CPU_EL;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    if ((ctx->sctlr_el1 & 0x1005ULL) != 0ULL) {
        plan->status = M12_ERR_CPU_MMU_CACHE;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    uint32_t tgran16 = (uint32_t)((ctx->id_aa64mmfr0_el1 >> 20) & 0xfu);
    if (tgran16 != 1u && tgran16 != 2u) {
        plan->status = M12_ERR_NO_TGRAN16;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    if (ctx->cntfrq_el0 == 0 || ctx->cntfrq_el0 > 0xffffffffULL) {
        plan->status = M12_ERR_CNTFRQ_ZERO;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 3. Parse and validate incoming FDT */
    status = m12_parse_fdt((const uint8_t *)(uintptr_t)ctx->x0, ctx, plan, &st);
    if (status != M12_BRIDGE_OK) {
        plan->status = status;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 3b. A weak reconstructed seed must never be presented to XNU as RNG.
     * Physical release still requires independent trust in the FDT provider. */
    if (!st.has_chosen_seed) {
        m12_bzero(st.chosen_seed, sizeof(st.chosen_seed));
        plan->status = M12_ERR_ENTROPY_UNAVAILABLE;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    /* No DT string or seed length proves RNG origin. The physical provider
     * remains unqualified. A synthetic host fixture is test-only and cannot
     * compile in a target-device build. QEMU retains its P0.6 trust model. */
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL &&
        !m12_physical_entropy_source_qualified()) {
        m12_bzero(st.chosen_seed, sizeof(st.chosen_seed));
        plan->status = M12_ERR_ENTROPY_UNTRUSTED;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 4. Validate GICv3 physical layout */
    if (plan->gicd_size != 0x10000ULL || plan->gicr_size < M12_PENANG_GICR_PE_SIZE ||
        ((plan->gicd_base | plan->gicr_base | plan->gicd_size | plan->gicr_size) & 0xffffULL) != 0 ||
        (plan->gicd_base < plan->gicr_base + plan->gicr_size &&
         plan->gicr_base < plan->gicd_base + plan->gicd_size)) {
        plan->status = M12_ERR_GIC_INVALID;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL) {
        plan->soc_io_base = M12_PENANG_SOC_IO_BASE;
        plan->soc_io_size = M12_PENANG_SOC_IO_SIZE;
    } else {
        plan->soc_io_base = M12_QEMU_SOC_IO_BASE;
        plan->soc_io_size = M12_QEMU_SOC_IO_SIZE;
    }
    if (plan->gicd_base < plan->soc_io_base ||
        plan->gicd_base + plan->gicd_size > plan->soc_io_base + plan->soc_io_size ||
        plan->gicr_base < plan->soc_io_base ||
        plan->gicr_base + plan->gicr_size > plan->soc_io_base + plan->soc_io_size) {
        plan->status = M12_ERR_GIC_INVALID;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 5. Reserve Bridge footprint, FDT footprint, initrd (if any), and Penang static guards */
    if (ctx->bridge_end_pa > ctx->bridge_start_pa) {
        if (m12_add_reserved(plan, ctx->bridge_start_pa, ctx->bridge_end_pa - ctx->bridge_start_pa) != 0) {
            plan->status = M12_ERR_MEMORY_MAP;
            m12_render_diagnostics(ctx, plan);
            return (int)plan->status;
        }
    }
    if (m12_add_reserved(plan, plan->fdt_pa, plan->fdt_totalsize) != 0) {
        plan->status = M12_ERR_MEMORY_MAP;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    if (st.initrd_end > st.initrd_start) {
        if (m12_add_reserved(plan, st.initrd_start, st.initrd_end - st.initrd_start) != 0) {
            plan->status = M12_ERR_MEMORY_MAP;
            m12_render_diagnostics(ctx, plan);
            return (int)plan->status;
        }
    }
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL) {
        for (i = 0; i < sizeof(g_penang_static_guards) / sizeof(g_penang_static_guards[0]); i++) {
            if (m12_add_reserved(plan, g_penang_static_guards[i].base, g_penang_static_guards[i].size) != 0) {
                plan->status = M12_ERR_MEMORY_MAP;
                m12_render_diagnostics(ctx, plan);
                return (int)plan->status;
            }
        }
    }
    status = m12_place_dynamic(plan, &st);
    if (status != M12_BRIDGE_OK) {
        plan->status = status;
        m12_render_diagnostics(ctx, plan);
        return (int)status;
    }
    m12_coalesce_reserved_16k(plan);

    /* 6. Parse and validate Mach-O 64 XNU kernel payload */
    if (ctx->payload_end_pa <= ctx->payload_start_pa) {
        plan->status = M12_ERR_MACHO_INVALID;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }
    const uint8_t *macho = (const uint8_t *)(uintptr_t)ctx->payload_start_pa;
    size_t macho_len = (size_t)(ctx->payload_end_pa - ctx->payload_start_pa);
    status = m12_parse_macho(macho, macho_len, plan, &vm_lo, &vm_hi);
    if (status != M12_BRIDGE_OK) {
        plan->status = status;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    uint64_t kern_hi_rel = ((vm_hi - M12_VIRT_BASE) + M12_PAGE_16K_MASK) & ~M12_PAGE_16K_MASK;
    uint64_t rd_size_aligned = 0;
    /* An Android initrd is NOT an Apple/XNU RAMDisk. Reserve the incoming
     * source memory, but never copy or publish it to the physical ADT.
     * Retain the QEMU-only test path unchanged. */
    if (plan->platform_profile == M12_PLAT_QEMU_VIRT &&
        st.initrd_end > st.initrd_start) {
        if (st.initrd_end - st.initrd_start > UINT64_MAX - M12_PAGE_16K_MASK) {
            plan->status = M12_ERR_MEMORY_MAP;
            m12_render_diagnostics(ctx, plan);
            return (int)plan->status;
        }
        rd_size_aligned = ((st.initrd_end - st.initrd_start) + M12_PAGE_16K_MASK) & ~M12_PAGE_16K_MASK;
    }
    uint64_t min_top_offset = kern_hi_rel + rd_size_aligned + M12_DT_MAX_SIZE + M12_PAGE_16K;

    /* 7. Select 32 MiB-aligned unreserved physical window for XNU */
    status = m12_select_window(plan, min_top_offset);
    if (status != M12_BRIDGE_OK) {
        plan->status = status;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    plan->kernel_lo_pa = plan->phys_base + (vm_lo - M12_VIRT_BASE);
    plan->kernel_hi_pa = plan->phys_base + kern_hi_rel;
    plan->entry_pa     = plan->phys_base + (plan->entry_va - M12_VIRT_BASE);
    if (rd_size_aligned > 0) {
        plan->ramdisk_pa   = plan->kernel_hi_pa;
        plan->ramdisk_size = st.initrd_end - st.initrd_start;
        plan->dt_pa        = plan->kernel_hi_pa + rd_size_aligned;
    } else {
        plan->ramdisk_pa   = 0;
        plan->ramdisk_size = 0;
        plan->dt_pa        = plan->kernel_hi_pa;
    }
    plan->args_pa            = plan->dt_pa + M12_DT_MAX_SIZE;
    plan->top_of_kernel_data = plan->args_pa + M12_PAGE_16K;

    if ((plan->dt_pa & M12_PAGE_16K_MASK) != 0 ||
        (plan->args_pa & M12_PAGE_16K_MASK) != 0 ||
        plan->top_of_kernel_data > plan->phys_base + plan->mem_size) {
        plan->status = M12_ERR_BOOTARGS_INVALID;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 8. Copy segments (slide = 0) and build ADT + boot_args */
    if (commit_memory) {
        for (i = 0; i < plan->segment_count; i++) {
            const struct m12_macho_seg *seg = &plan->segments[i];
            uint64_t seg_pa = plan->phys_base + (seg->vmaddr - M12_VIRT_BASE);
            m12_bzero((void *)(uintptr_t)seg_pa, (size_t)seg->vmsize);
            if (seg->filesize > 0) {
                m12_memcpy((void *)(uintptr_t)seg_pa, macho + seg->fileoff, (size_t)seg->filesize);
            }
        }
        if (plan->ramdisk_size > 0) {
            m12_bzero((void *)(uintptr_t)plan->ramdisk_pa, (size_t)rd_size_aligned);
            m12_memcpy((void *)(uintptr_t)plan->ramdisk_pa,
                       (const void *)(uintptr_t)st.initrd_start, (size_t)plan->ramdisk_size);
        }
        m12_bzero((void *)(uintptr_t)plan->dt_pa, M12_DT_MAX_SIZE);
        status = m12_build_adt((uint8_t *)(uintptr_t)plan->dt_pa, (uint32_t)M12_DT_MAX_SIZE, ctx, &st, plan);
    } else {
        static uint8_t dryrun_adt[M12_DT_MAX_SIZE];
        m12_bzero(dryrun_adt, sizeof(dryrun_adt));
        status = m12_build_adt(dryrun_adt, (uint32_t)sizeof(dryrun_adt), ctx, &st, plan);
    }
    /* The ADT owns a separate seed copy or the build failed.
     * Clear the parser's temporary stack seed on both paths. */
    m12_bzero(st.chosen_seed, sizeof(st.chosen_seed));
    if (status != M12_BRIDGE_OK) {
        plan->status = status;
        m12_render_diagnostics(ctx, plan);
        return (int)plan->status;
    }

    /* 9. Populate XNU 11417 boot_args (1152 bytes) */
    struct m12_boot_args *ba = &plan->boot_args_copy;
    m12_bzero(ba, sizeof(*ba));
    ba->Revision           = 2u;
    ba->Version            = 2u;
    ba->virtBase           = M12_VIRT_BASE;
    ba->physBase           = plan->phys_base;
    ba->memSize            = plan->mem_size;
    ba->topOfKernelData    = plan->top_of_kernel_data;
    if (ctx->fb_enabled) {
        ba->Video.v_baseAddr = M12_PENANG_FB_BASE;
        ba->Video.v_display  = 1ULL;
        ba->Video.v_rowBytes = M12_PENANG_FB_STRIDE;
        ba->Video.v_width    = M12_PENANG_FB_WIDTH;
        ba->Video.v_height   = M12_PENANG_FB_HEIGHT;
        ba->Video.v_depth    = M12_PENANG_FB_DEPTH;
    }
    ba->machineType        = 0u;
    ba->deviceTreeP        = plan->dt_pa - plan->phys_base + M12_VIRT_BASE;
    ba->deviceTreeLength   = plan->dt_len;
    if (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL) {
        /* Deliberate XNU-only diagnostic policy. No unsupported serial
         * driver, Android keys or vendor boot flags are introduced. */
        static const char penang_cmd[] = "cpus=1 -v -noprogress";
        m12_memcpy(ba->CommandLine, penang_cmd, sizeof(penang_cmd));
    } else {
        /* Preserve original QEMU_VIRT bootstrap policy. */
        static const char qemu_cmd[] = "debug=0x8 serial=3 cpus=1 -v -noprogress";
        m12_memcpy(ba->CommandLine, qemu_cmd, sizeof(qemu_cmd));
    }
    ba->bootFlags          = 0ULL;
    /* Apple boot.h labels memSizeActual as actual physical memory. That
     * total is NOT established by the selected 256-MiB XNU window.
     * XNU arm_vm_init.c falls back to memSize when this field is zero. */
    ba->memSizeActual      = (plan->platform_profile == M12_PLAT_PENANG_PHYSICAL)
                             ? 0ULL : plan->mem_size;

    if (commit_memory) {
        m12_bzero((void *)(uintptr_t)plan->args_pa, M12_PAGE_16K);
        m12_memcpy((void *)(uintptr_t)plan->args_pa, ba, sizeof(*ba));
    }

    plan->status = M12_BRIDGE_OK;
    g_m12_handoff_args_pa  = plan->args_pa;
    g_m12_handoff_entry_pa = plan->entry_pa;
    m12_render_diagnostics(ctx, plan);
    return (int)plan->status;
}
