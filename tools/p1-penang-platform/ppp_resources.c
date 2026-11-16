/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Run-time resolution of the P1 platform resources from a flattened device
 * tree (devicetree specification v0.4, structure version 17).
 *
 * One bounded, allocation-free pass. Properties of a node are collected
 * until its first child or its end and only then interpreted, so property
 * order inside a node never matters. `reg` values are translated through
 * every ancestor `ranges`; a bus without `ranges` is not CPU-addressable and
 * is refused instead of being assumed identity-mapped.
 */
#include "ppp_platform.h"

#define FDT_MAGIC      0xd00dfeedu
#define FDT_BEGIN_NODE 1u
#define FDT_END_NODE   2u
#define FDT_PROP       3u
#define FDT_NOP        4u
#define FDT_END        9u
#define PPP_MAX_DEPTH  16u
#define GIC_FRAME_64K  0x10000ull

struct blob {
    const uint8_t *p;
    uint32_t len;
    uint32_t present;
};

struct level {
    const char *name;
    uint32_t addr_cells;   /* cells of this node's children */
    uint32_t size_cells;
    uint32_t intr_cells;
    uint32_t evaluated;
    struct blob ranges, compatible, reg, interrupts, interrupts_ext, status;
    struct blob device_type, method, regions, stride, frequency;
};

struct walk {
    struct level lv[PPP_MAX_DEPTH];
    struct ppp_resources *out;
    uint32_t gic_seen;
    uint32_t gic_intr_cells;
    uint32_t timer_seen;
    uint32_t cpu_nodes;
};

static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t
cells(const uint8_t *p, uint32_t count)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < count; ++i) {
        v = v << 32 | be32(p + 4u * i);
    }
    return v;
}

static int
str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/* Exact member of a well-formed NUL-terminated string list. */
static int
list_has(const struct blob *b, const char *want)
{
    uint32_t at = 0;
    if (!b->present || b->len == 0 || b->p[b->len - 1] != 0) {
        return 0;
    }
    while (at < b->len) {
        const char *s = (const char *)b->p + at;
        uint32_t n = 0;
        while (s[n]) {
            ++n;
        }
        if (str_eq(s, want)) {
            return 1;
        }
        at += n + 1;
    }
    return 0;
}

static int
node_enabled(const struct level *l)
{
    return !l->status.present || list_has(&l->status, "okay") || list_has(&l->status, "ok");
}

/* Translate an address in the space of the node at `depth` to the root. */
static int
translate(const struct walk *w, uint32_t depth, uint64_t addr, uint64_t size, uint64_t *out)
{
    if (size == 0 || addr + size < addr) {
        return PPP_E_TRANSLATE;
    }
    for (uint32_t d = depth; d >= 1; --d) {
        const struct level *bus = &w->lv[d];
        const struct level *up = &w->lv[d - 1];
        uint32_t entry = (bus->addr_cells + up->addr_cells + bus->size_cells) * 4u;
        uint32_t at;
        /* Only one- and two-cell (32/64-bit) buses are in this contract. */
        if (!bus->ranges.present || bus->addr_cells - 1u > 1u || up->addr_cells - 1u > 1u ||
            bus->size_cells - 1u > 1u) {
            return PPP_E_TRANSLATE;
        }
        if (bus->ranges.len == 0) {
            continue; /* empty ranges: identity */
        }
        if (bus->ranges.len % entry != 0) {
            return PPP_E_TRANSLATE;
        }
        for (at = 0; at < bus->ranges.len; at += entry) {
            const uint8_t *e = bus->ranges.p + at;
            uint64_t child = cells(e, bus->addr_cells);
            uint64_t parent = cells(e + 4u * bus->addr_cells, up->addr_cells);
            uint64_t span = cells(e + 4u * (bus->addr_cells + up->addr_cells), bus->size_cells);
            if (addr >= child && span >= size && addr - child <= span - size &&
                parent + (addr - child) >= parent &&
                parent + (addr - child) + size >= parent) {
                addr = parent + (addr - child);
                break;
            }
        }
        if (at >= bus->ranges.len) {
            return PPP_E_TRANSLATE;
        }
    }
    *out = addr;
    return PPP_OK;
}

/* Tuple `index` of this node's reg, translated to the root address space. */
static int
reg_tuple(const struct walk *w, uint32_t depth, uint32_t index, uint64_t *base, uint64_t *size)
{
    const struct level *l = &w->lv[depth];
    const struct level *parent = &w->lv[depth - 1];
    uint32_t step = (parent->addr_cells + parent->size_cells) * 4u;
    const uint8_t *e;
    if (parent->addr_cells - 1u > 1u || parent->size_cells - 1u > 1u) {
        return PPP_E_TRANSLATE;
    }
    if (!l->reg.present || l->reg.len % step != 0 || (index + 1u) * step > l->reg.len) {
        return PPP_E_TRANSLATE;
    }
    e = l->reg.p + index * step;
    *size = cells(e + 4u * parent->addr_cells, parent->size_cells);
    return translate(w, depth - 1, cells(e, parent->addr_cells), *size, base);
}

static int
eval_gic(struct walk *w, uint32_t depth)
{
    const struct level *l = &w->lv[depth];
    struct ppp_resources *r = w->out;
    uint64_t stride = 2 * GIC_FRAME_64K;
    if (w->gic_seen++) {
        return PPP_E_GIC_DUPLICATE;
    }
    /* One contiguous redistributor region; anything else needs new review. */
    if (l->regions.present && (l->regions.len != 4 || be32(l->regions.p) != 1)) {
        return PPP_E_GIC_LAYOUT;
    }
    if (l->stride.present) {
        if (l->stride.len != 4 && l->stride.len != 8) {
            return PPP_E_GIC_LAYOUT;
        }
        stride = cells(l->stride.p, l->stride.len / 4u);
    }
    if (l->intr_cells != 3) {
        return PPP_E_GIC_LAYOUT;
    }
    if (reg_tuple(w, depth, 0, &r->gicd_pa, &r->gicd_size) != PPP_OK ||
        reg_tuple(w, depth, 1, &r->gicr_pa, &r->gicr_size) != PPP_OK) {
        return PPP_E_TRANSLATE;
    }
    if (r->gicd_size != GIC_FRAME_64K || stride < 2 * GIC_FRAME_64K ||
        stride % GIC_FRAME_64K != 0 || r->gicr_size < stride || r->gicr_size % stride != 0 ||
        (r->gicd_pa | r->gicr_pa) % GIC_FRAME_64K != 0 ||
        (r->gicd_pa < r->gicr_pa + r->gicr_size && r->gicr_pa < r->gicd_pa + r->gicd_size)) {
        return PPP_E_GIC_LAYOUT;
    }
    r->gicr_stride = stride;
    w->gic_intr_cells = l->intr_cells;
    return PPP_OK;
}

static int
eval_timer(struct walk *w, uint32_t depth)
{
    const struct level *l = &w->lv[depth];
    struct ppp_resources *r = w->out;
    uint32_t count;
    if (w->timer_seen++) {
        return PPP_E_TIMER_DUPLICATE;
    }
    /* Three-cell GIC specifiers only; interrupts-extended is not accepted. */
    if (l->interrupts_ext.present || !l->interrupts.present ||
        (l->interrupts.len != 36 && l->interrupts.len != 48)) {
        return PPP_E_TIMER_LAYOUT;
    }
    count = l->interrupts.len / 12u;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *e = l->interrupts.p + 12u * i;
        if (be32(e) != 1 || be32(e + 4) > 15) { /* must be a PPI */
            return PPP_E_TIMER_LAYOUT;
        }
        r->timer[i].intid = 16u + be32(e + 4);
        r->timer[i].dt_flags = be32(e + 8);
        for (uint32_t j = 0; j < i; ++j) {
            if (r->timer[j].intid == r->timer[i].intid) {
                return PPP_E_TIMER_LAYOUT;
            }
        }
    }
    if (l->frequency.present) {
        if (l->frequency.len != 4 || be32(l->frequency.p) == 0) {
            return PPP_E_TIMER_LAYOUT;
        }
        r->timer_dt_hz = be32(l->frequency.p);
    }
    r->timer_count = count;
    return PPP_OK;
}

static void
eval_console(struct walk *w, uint32_t depth)
{
    const struct level *l = &w->lv[depth];
    struct ppp_resources *r = w->out;
    uint64_t base, size;
    /* Optional diagnostic device: an unusable node is not a candidate. */
    if (reg_tuple(w, depth, 0, &base, &size) != PPP_OK || size < 0x1000) {
        return;
    }
    if (r->console_candidates++ == 0) {
        r->console_pa = base;
        r->console_size = size;
        r->console_intid = 0;
        if (l->interrupts.present && l->interrupts.len >= 12 && be32(l->interrupts.p) == 0 &&
            be32(l->interrupts.p + 4) < 988) {
            r->console_intid = 32u + be32(l->interrupts.p + 4);
        }
    }
}

static int
evaluate(struct walk *w, uint32_t depth)
{
    struct level *l = &w->lv[depth];
    int status = PPP_OK;
    if (l->evaluated) {
        return PPP_OK;
    }
    l->evaluated = 1;
    if (depth == 0 || !node_enabled(l)) {
        return PPP_OK;
    }
    if (depth == 2 && str_eq(w->lv[1].name, "cpus") && list_has(&l->device_type, "cpu")) {
        ++w->cpu_nodes;
    }
    if (list_has(&l->compatible, "arm,gic-v3")) {
        status = eval_gic(w, depth);
    } else if (list_has(&l->compatible, "arm,armv8-timer")) {
        status = eval_timer(w, depth);
    } else if (list_has(&l->compatible, "arm,psci-1.0") ||
        list_has(&l->compatible, "arm,psci-0.2")) {
        w->out->psci_method = list_has(&l->method, "smc") ? 1u :
            list_has(&l->method, "hvc") ? 2u : 0u;
    } else if (list_has(&l->compatible, "qcom,msm-geni-console") ||
        list_has(&l->compatible, "qcom,geni-debug-uart")) {
        eval_console(w, depth);
    }
    return status;
}

static int
cell_count(const uint8_t *value, uint32_t len, uint32_t max, uint32_t *out)
{
    if (len != 4 || be32(value) > max) {
        return PPP_E_FDT;
    }
    *out = be32(value);
    return PPP_OK;
}

static int
property(struct level *l, const char *name, const uint8_t *value, uint32_t len)
{
    static const struct {
        const char *name;
        size_t field;
    } map[] = {
        { "ranges", offsetof(struct level, ranges) },
        { "compatible", offsetof(struct level, compatible) },
        { "reg", offsetof(struct level, reg) },
        { "interrupts", offsetof(struct level, interrupts) },
        { "interrupts-extended", offsetof(struct level, interrupts_ext) },
        { "status", offsetof(struct level, status) },
        { "device_type", offsetof(struct level, device_type) },
        { "method", offsetof(struct level, method) },
        { "#redistributor-regions", offsetof(struct level, regions) },
        { "redistributor-stride", offsetof(struct level, stride) },
        { "clock-frequency", offsetof(struct level, frequency) },
    };
    if (str_eq(name, "#address-cells")) {
        return cell_count(value, len, 4, &l->addr_cells);
    }
    if (str_eq(name, "#size-cells")) {
        return cell_count(value, len, 4, &l->size_cells);
    }
    if (str_eq(name, "#interrupt-cells")) {
        return cell_count(value, len, 8, &l->intr_cells);
    }
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (str_eq(name, map[i].name)) {
            struct blob *b = (struct blob *)(void *)((uint8_t *)l + map[i].field);
            if (b->present) {
                return PPP_E_FDT; /* a duplicated property is ambiguous */
            }
            b->p = value;
            b->len = len;
            b->present = 1;
            break;
        }
    }
    return PPP_OK;
}

static void
zero(void *p, size_t n)
{
    volatile uint8_t *b = p;
    while (n--) {
        *b++ = 0;
    }
}

static int
walk_tree(const uint8_t *fdt, size_t length, struct walk *w)
{
    uint32_t total, off_struct, off_strings, size_strings, size_struct;
    const uint8_t *s, *strings;
    uint32_t at = 0, depth = 0, started = 0;
    int status;

    if (length < 40 || be32(fdt) != FDT_MAGIC) {
        return PPP_E_FDT;
    }
    total = be32(fdt + 4);
    off_struct = be32(fdt + 8);
    off_strings = be32(fdt + 12);
    size_strings = be32(fdt + 32);
    size_struct = be32(fdt + 36);
    if (total > length || be32(fdt + 20) < 17 || be32(fdt + 24) > 17 ||
        off_struct % 4 != 0 || size_struct % 4 != 0 || size_struct < 16 ||
        off_struct < 40 || off_struct > total || size_struct > total - off_struct ||
        off_strings < 40 || off_strings > total || size_strings > total - off_strings) {
        return PPP_E_FDT;
    }
    s = fdt + off_struct;
    strings = fdt + off_strings;

    for (;;) {
        uint32_t token;
        if (size_struct - at < 4) {
            return PPP_E_FDT;
        }
        token = be32(s + at);
        at += 4;
        if (token == FDT_NOP) {
            continue;
        }
        if (token == FDT_BEGIN_NODE) {
            uint32_t n = 0;
            struct level *l;
            while (at + n < size_struct && s[at + n] != 0) {
                ++n;
            }
            if (at + n >= size_struct) {
                return PPP_E_FDT;
            }
            if (started) {
                if ((status = evaluate(w, depth)) != PPP_OK) {
                    return status;
                }
                if (++depth >= PPP_MAX_DEPTH) {
                    return PPP_E_DEPTH;
                }
            } else if (n != 0) {
                return PPP_E_FDT; /* the root node has an empty name */
            }
            started = 1;
            l = &w->lv[depth];
            zero(l, sizeof(*l));
            l->name = (const char *)s + at;
            l->addr_cells = 2; /* specification defaults */
            l->size_cells = 1;
            at += (n + 1 + 3u) & ~3u;
            if (at > size_struct) {
                return PPP_E_FDT;
            }
        } else if (token == FDT_PROP) {
            uint32_t len, nameoff, n = 0;
            if (!started || w->lv[depth].evaluated || size_struct - at < 8) {
                return PPP_E_FDT; /* properties precede child nodes */
            }
            len = be32(s + at);
            nameoff = be32(s + at + 4);
            at += 8;
            if (len > size_struct - at || nameoff >= size_strings) {
                return PPP_E_FDT;
            }
            while (nameoff + n < size_strings && strings[nameoff + n] != 0) {
                ++n;
            }
            if (nameoff + n >= size_strings) {
                return PPP_E_FDT;
            }
            status = property(&w->lv[depth], (const char *)strings + nameoff, s + at, len);
            if (status != PPP_OK) {
                return status;
            }
            at += (len + 3u) & ~3u;
            if (at > size_struct) {
                return PPP_E_FDT;
            }
        } else if (token == FDT_END_NODE) {
            if (!started) {
                return PPP_E_FDT;
            }
            if ((status = evaluate(w, depth)) != PPP_OK) {
                return status;
            }
            if (depth == 0) {
                /* Only FDT_END (and padding NOPs) may follow the root. */
                do {
                    if (size_struct - at < 4) {
                        return PPP_E_FDT;
                    }
                    token = be32(s + at);
                    at += 4;
                } while (token == FDT_NOP);
                return token == FDT_END ? PPP_OK : PPP_E_FDT;
            }
            --depth;
        } else {
            return PPP_E_FDT;
        }
    }
}

int
ppp_resolve(const void *fdt, size_t length, struct ppp_resources *out)
{
    static struct walk w; /* single boot CPU, not reentrant by contract */
    int status;
    if (out == NULL) {
        return PPP_E_ARGS;
    }
    zero(out, sizeof(*out));
    if (fdt == NULL) {
        return PPP_E_ARGS;
    }
    zero(&w, sizeof(w));
    w.out = out;
    status = walk_tree(fdt, length, &w);
    if (status == PPP_OK && !w.gic_seen) {
        status = PPP_E_GIC_ABSENT;
    }
    if (status == PPP_OK && !w.timer_seen) {
        status = PPP_E_TIMER_ABSENT;
    }
    if (status == PPP_OK && (w.cpu_nodes == 0 ||
        out->gicr_size / out->gicr_stride < w.cpu_nodes)) {
        status = PPP_E_CPUS; /* every described CPU needs a redistributor */
    }
    if (status != PPP_OK) {
        zero(out, sizeof(*out));
        return status;
    }
    if (out->console_candidates != 1) {
        out->console_pa = 0; /* ambiguous or absent: no candidate is chosen */
        out->console_size = 0;
        out->console_intid = 0;
    }
    out->cpu_count = w.cpu_nodes;
    out->abi_version = PPP_ABI_VERSION;
    return PPP_OK;
}

const char *
ppp_status_name(int status)
{
    static const char *const names[PPP_STATUS_COUNT] = {
        "OK", "ARGS", "FDT", "DEPTH", "GIC_ABSENT", "GIC_DUPLICATE", "GIC_LAYOUT",
        "TRANSLATE", "TIMER_ABSENT", "TIMER_DUPLICATE", "TIMER_LAYOUT", "CPUS",
    };
    return status >= 0 && status < PPP_STATUS_COUNT ? names[status] : "UNKNOWN";
}
