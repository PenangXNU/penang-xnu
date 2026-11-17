/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Retained run-time FDT provider (ABI 2). See ppp_runtime_fdt.h for the
 * contract. Freestanding: no libc, no allocation, no global MMIO.
 *
 * Structure rules follow the Devicetree Specification v0.4, chapter 5
 * (flattened format). Where the specification is permissive and real
 * vendor trees rely on it (property names such as ";" or ":" in a Motorola
 * tree), the checker is permissive too; where an ambiguity would let two
 * parsers disagree about the same bytes, it refuses.
 */
#include "ppp_runtime_fdt.h"
#include "p1_memory_admission.h" /* struct p1_runtime_fdt: single definition */

#if defined(__APPLE__) || defined(__MACH__)
#define PPP_C_SYMBOL(name) "_" name
#else
#define PPP_C_SYMBOL(name) name
#endif

/*
 * Storage behind the descriptor that p1_runtime_fdt_provider.h declares
 * as `extern struct p1_runtime_fdt penang_p1_runtime_fdt`. The private C
 * name is bound to that linker symbol; the object type is the same mutable
 * struct the consumer declares.
 */
struct p1_runtime_fdt ppp_runtime_fdt_storage __asm__(PPP_C_SYMBOL("penang_p1_runtime_fdt"));
struct p1_runtime_fdt ppp_runtime_fdt_storage;

enum { PPP_STATE_UNBOUND = 0, PPP_STATE_BINDING = 1, PPP_STATE_BOUND = 2 };
static uint32_t ppp_fdt_state;

#define FDT_MAGIC       0xd00dfeedu
#define FDT_BEGIN_NODE  1u
#define FDT_END_NODE    2u
#define FDT_PROP        3u
#define FDT_NOP         4u
#define FDT_END         9u
#define FDT_HEADER      40u
#define FDT_MAX_DEPTH   32u
#define FDT_MAX_RSV     128u     /* same bound as memory admission */
#define FDT_MAX_NODES   65536u
#define PPP_PROBE_STEP  0x1000u  /* probe every 4 KiB: correct for any granule */

static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t
be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static int
overlaps(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0 < b1 && b0 < a1;
}

/* NUL-terminated string at s[0..limit); returns its length or -1. */
static long
cstring(const uint8_t *s, uint32_t limit)
{
    for (uint32_t i = 0; i < limit; ++i) {
        if (s[i] == 0) {
            return (long)i;
        }
    }
    return -1;
}

static int
printable(const uint8_t *s, long n, int forbid_slash)
{
    for (long i = 0; i < n; ++i) {
        if (s[i] < 0x21 || s[i] > 0x7e || (forbid_slash && s[i] == '/')) {
            return 0;
        }
    }
    return 1;
}

static int
same_string(const uint8_t *strings, uint32_t a, uint32_t b)
{
    if (a == b) {
        return 1;
    }
    for (;;) {
        if (strings[a] != strings[b]) {
            return 0;
        }
        if (strings[a] == 0) {
            return 1;
        }
        ++a;
        ++b;
    }
}

/*
 * The properties of the current node are the FDT_PROP tokens between the
 * node's first token and `end` (properties precede children, so no child
 * token can lie in between). Refuse a name that appears twice.
 */
static int
duplicate_property(const uint8_t *s, uint32_t first, uint32_t end, const uint8_t *strings,
    uint32_t nameoff)
{
    uint32_t at = first;
    while (at < end) {
        uint32_t token = be32(s + at);
        at += 4;
        if (token == FDT_NOP) {
            continue;
        }
        /* only FDT_PROP can occur here; the walker already bounds-checked it */
        uint32_t len = be32(s + at);
        uint32_t off = be32(s + at + 4);
        if (same_string(strings, off, nameoff)) {
            return 1;
        }
        at += 8 + ((len + 3u) & ~3u);
    }
    return 0;
}

/*
 * Root children are what every consumer looks up by path (/chosen,
 * /memory@..., /reserved-memory, /cpus, /soc): a duplicate would let two
 * parsers pick different nodes, so names at that level must be unique.
 * Rescans the already validated tokens before `end`; no stack array.
 */
static int
earlier_root_child(const uint8_t *s, uint32_t end, uint32_t name)
{
    uint32_t at = 0, depth = 0;
    while (at < end) {
        uint32_t token = be32(s + at);
        at += 4;
        if (token == FDT_BEGIN_NODE) {
            uint32_t a = at, b = name;
            if (depth == 1) {
                while (s[a] == s[b] && s[a] != 0) {
                    ++a;
                    ++b;
                }
                if (s[a] == s[b]) {
                    return 1;
                }
            }
            while (s[at] != 0) {
                ++at;
            }
            at = (at + 1u + 3u) & ~3u;
            ++depth;
        } else if (token == FDT_PROP) {
            at += 8u + ((be32(s + at) + 3u) & ~3u);
        } else if (token == FDT_END_NODE) {
            --depth;
        }
    }
    return 0;
}

int
ppp_fdt_check_structure(const uint8_t *f, size_t length)
{
    uint32_t total, o_struct, o_strings, o_rsv, version, last_comp, n_strings, n_struct;
    uint32_t rsv_end;
    const uint8_t *s, *strings;
    uint32_t at = 0, depth = 0, nodes = 0, root_done = 0;
    /* Properties precede children, so a property can only belong to the
     * most recently opened node: one scalar marks where its properties start. */
    uint32_t first_prop = 0;
    uint32_t has_child = 0; /* bit d: the open node at depth d has a child */

    if (f == NULL || length < FDT_HEADER || length > PPP_FDT_MAX_BYTES) {
        return PPP_FDT_E_HEADER;
    }
    total = be32(f + 4);
    o_struct = be32(f + 8);
    o_strings = be32(f + 12);
    o_rsv = be32(f + 16);
    version = be32(f + 20);
    last_comp = be32(f + 24);
    n_strings = be32(f + 32);
    n_struct = be32(f + 36);
    if (be32(f) != FDT_MAGIC || version != 17 || last_comp < 16 || last_comp > 17) {
        return PPP_FDT_E_HEADER;
    }
    if (total != length) {
        return PPP_FDT_E_LENGTH;
    }
    /* Block placement: inside the tree, after the header, aligned, disjoint. */
    if (o_rsv < FDT_HEADER || (o_rsv & 7u) || o_rsv > total ||
        o_struct < FDT_HEADER || (o_struct & 3u) || (n_struct & 3u) || o_struct > total ||
        n_struct > total - o_struct || n_struct < 16u ||
        o_strings < FDT_HEADER || o_strings > total || n_strings > total - o_strings ||
        overlaps(o_struct, (uint64_t)o_struct + n_struct, o_strings, (uint64_t)o_strings + n_strings)) {
        return PPP_FDT_E_STRUCTURE;
    }
    /* Memory reservation map: 16-byte entries ending in (0, 0). */
    for (uint32_t i = 0;; ++i) {
        uint64_t base, size;
        uint64_t entry = (uint64_t)o_rsv + 16u * i;
        if (i > FDT_MAX_RSV || entry + 16u > total) {
            return PPP_FDT_E_STRUCTURE;
        }
        base = be64(f + entry);
        size = be64(f + entry + 8);
        if (base == 0 && size == 0) {
            rsv_end = (uint32_t)(entry + 16u);
            break;
        }
        if (size == 0 || base + size < base) {
            return PPP_FDT_E_STRUCTURE;
        }
    }
    if (overlaps(o_rsv, rsv_end, o_struct, (uint64_t)o_struct + n_struct) ||
        overlaps(o_rsv, rsv_end, o_strings, (uint64_t)o_strings + n_strings)) {
        return PPP_FDT_E_STRUCTURE;
    }

    s = f + o_struct;
    strings = f + o_strings;
    while (at < n_struct) {
        uint32_t token = be32(s + at);
        at += 4;
        if (token == FDT_NOP) {
            continue;
        }
        if (token == FDT_BEGIN_NODE) {
            long n = cstring(s + at, n_struct - at);
            uint32_t next;
            if (root_done || n < 0 || ++nodes > FDT_MAX_NODES) {
                return PPP_FDT_E_STRUCTURE;
            }
            if (depth == 0) {
                if (n != 0 || nodes != 1) {
                    return PPP_FDT_E_STRUCTURE; /* exactly one root, empty name */
                }
            } else if (n == 0 || !printable(s + at, n, 1)) {
                return PPP_FDT_E_STRUCTURE;
            } else {
                has_child |= 1u << (depth - 1);
                if (depth == 1 && earlier_root_child(s, at - 4u, at)) {
                    return PPP_FDT_E_STRUCTURE;
                }
            }
            if (depth == FDT_MAX_DEPTH) {
                return PPP_FDT_E_STRUCTURE;
            }
            next = (at + (uint32_t)n + 1u + 3u) & ~3u;
            if (next > n_struct) {
                return PPP_FDT_E_STRUCTURE;
            }
            at = next;
            first_prop = at;
            has_child &= ~(1u << depth);
            ++depth;
        } else if (token == FDT_PROP) {
            uint32_t len, nameoff, next;
            long n;
            if (depth == 0 || (has_child & (1u << (depth - 1))) || n_struct - at < 8u) {
                return PPP_FDT_E_STRUCTURE; /* properties precede children */
            }
            len = be32(s + at);
            nameoff = be32(s + at + 4);
            if (len > n_struct - at - 8u || nameoff >= n_strings) {
                return PPP_FDT_E_STRUCTURE;
            }
            n = cstring(strings + nameoff, n_strings - nameoff);
            if (n <= 0 || !printable(strings + nameoff, n, 0)) {
                return PPP_FDT_E_STRUCTURE;
            }
            next = at + 8u + ((len + 3u) & ~3u);
            /* Alignment padding is not required to be zero: the Motorola
             * boot loader edits values in place and leaves stale bytes
             * there. No parser interprets padding, so this is not an
             * ambiguity; it is only ignored. */
            if (next > n_struct ||
                duplicate_property(s, first_prop, at - 4u, strings, nameoff)) {
                return PPP_FDT_E_STRUCTURE;
            }
            at = next;
        } else if (token == FDT_END_NODE) {
            if (depth == 0) {
                return PPP_FDT_E_STRUCTURE;
            }
            if (--depth == 0) {
                root_done = 1;
            }
        } else if (token == FDT_END) {
            /* The last token, exactly at the end of the block, after the root. */
            return (root_done && depth == 0 && at == n_struct) ? PPP_FDT_OK : PPP_FDT_E_STRUCTURE;
        } else {
            return PPP_FDT_E_STRUCTURE;
        }
    }
    return PPP_FDT_E_STRUCTURE;
}

/* Every check that needs no memory access. */
static int
check_window(const struct ppp_fdt_handoff *h, const struct ppp_fdt_mapping *m, uint64_t *va_out)
{
    const uint64_t mask = PPP_FDT_PAGE - 1u;
    uint64_t fdt_end, va;

    if (h->mem_size == 0 || ((h->phys_base | h->virt_base | h->mem_size | h->kernel_end_pa |
        h->top_of_kernel_data) & mask) != 0 || h->virt_base == 0 ||
        h->phys_base + h->mem_size < h->phys_base || h->virt_base + h->mem_size < h->virt_base ||
        h->kernel_end_pa <= h->phys_base || h->top_of_kernel_data <= h->kernel_end_pa ||
        h->top_of_kernel_data > h->phys_base + h->mem_size) {
        return PPP_FDT_E_WINDOW;
    }
    /* The adapter vouches only for [va_min, va_max); the whole kernel window
     * must fall inside it, which also rules out VA 0 and wrap-around. */
    if (m->va_min >= m->va_max || h->virt_base < m->va_min ||
        h->virt_base + h->mem_size > m->va_max) {
        return PPP_FDT_E_WINDOW;
    }
    if (h->fdt_pa == 0 || (h->fdt_pa & 7u) != 0) {
        return PPP_FDT_E_ALIGNMENT;
    }
    if (h->fdt_length < FDT_HEADER || h->fdt_length > PPP_FDT_MAX_BYTES ||
        h->fdt_pa < h->kernel_end_pa || h->fdt_pa >= h->top_of_kernel_data ||
        h->fdt_length > h->top_of_kernel_data - h->fdt_pa) {
        return PPP_FDT_E_NOT_RETAINED;
    }
    fdt_end = h->fdt_pa + h->fdt_length;
    if (h->boot_args_pa == 0 || h->boot_args_pa + PPP_BOOT_ARGS_BYTES < h->boot_args_pa ||
        h->adt_pa + h->adt_length < h->adt_pa) {
        return PPP_FDT_E_ARGS;
    }
    if (overlaps(h->fdt_pa, fdt_end, h->boot_args_pa, h->boot_args_pa + PPP_BOOT_ARGS_BYTES) ||
        (h->adt_length != 0 && overlaps(h->fdt_pa, fdt_end, h->adt_pa, h->adt_pa + h->adt_length))) {
        return PPP_FDT_E_OVERLAP;
    }
    va = h->virt_base + (h->fdt_pa - h->phys_base);
    if (va != (uint64_t)(uintptr_t)va) {
        return PPP_FDT_E_WINDOW;
    }
    *va_out = va;
    return PPP_FDT_OK;
}

/* Prove the mapping before the first load from it. */
static int
check_mapping(const struct ppp_fdt_handoff *h, const struct ppp_fdt_mapping *m, uint64_t va)
{
    uint64_t last = va + h->fdt_length - 1u;
    uint64_t probe = va & ~(uint64_t)(PPP_PROBE_STEP - 1u);
    for (;;) {
        uint64_t at = probe < va ? va : probe;
        uint64_t pa = 0;
        if (m->translate(m->ctx, at, &pa) != 0 || pa != h->phys_base + (at - h->virt_base)) {
            return PPP_FDT_E_UNMAPPED;
        }
        if (last - probe < PPP_PROBE_STEP) {
            break;
        }
        probe += PPP_PROBE_STEP;
    }
    {
        uint64_t pa = 0;
        if (m->translate(m->ctx, last, &pa) != 0 || pa != h->phys_base + (last - h->virt_base)) {
            return PPP_FDT_E_UNMAPPED;
        }
    }
    return PPP_FDT_OK;
}

static int
validate(const struct ppp_fdt_handoff *h, const struct ppp_fdt_mapping *m, const uint8_t **out)
{
    uint64_t va = 0;
    const uint8_t *bytes;
    int status;

    if (h->abi_version != PPP_FDT_ABI_VERSION || h->reserved0 != 0) {
        return PPP_FDT_E_ARGS;
    }
    if ((status = check_window(h, m, &va)) != PPP_FDT_OK ||
        (status = check_mapping(h, m, va)) != PPP_FDT_OK) {
        return status;
    }
    bytes = (const uint8_t *)(uintptr_t)va;
    /* Header identity first, so a non-tree is reported as such. */
    if (be32(bytes) != FDT_MAGIC || be32(bytes + 20) != 17 || be32(bytes + 24) > 17) {
        return PPP_FDT_E_HEADER;
    }
    if (be32(bytes + 4) != h->fdt_length) {
        return PPP_FDT_E_LENGTH;
    }
    if ((status = ppp_fdt_check_structure(bytes, (size_t)h->fdt_length)) != PPP_FDT_OK) {
        return status;
    }
    *out = bytes;
    return PPP_FDT_OK;
}

int
ppp_runtime_fdt_bind(const struct ppp_fdt_handoff *h, const struct ppp_fdt_mapping *m)
{
    uint32_t expected = PPP_STATE_UNBOUND;
    const uint8_t *bytes = 0;
    int status;

    if (h == NULL || m == NULL || m->translate == NULL) {
        return PPP_FDT_E_ARGS;
    }
    if (!__atomic_compare_exchange_n(&ppp_fdt_state, &expected, PPP_STATE_BINDING, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return expected == PPP_STATE_BOUND ? PPP_FDT_E_ALREADY_BOUND : PPP_FDT_E_BUSY;
    }
    /* Storage written by anything but this function is never overwritten. */
    if (ppp_runtime_fdt_storage.bytes != 0 || ppp_runtime_fdt_storage.length != 0 ||
        ppp_runtime_fdt_storage.physical_address != 0) {
        __atomic_store_n(&ppp_fdt_state, PPP_STATE_UNBOUND, __ATOMIC_RELEASE);
        return PPP_FDT_E_ALREADY_BOUND;
    }
    status = validate(h, m, &bytes);
    if (status != PPP_FDT_OK) {
        /* Nothing was written; the single assignment stays available. */
        __atomic_store_n(&ppp_fdt_state, PPP_STATE_UNBOUND, __ATOMIC_RELEASE);
        return status;
    }
    ppp_runtime_fdt_storage.physical_address = h->fdt_pa;
    ppp_runtime_fdt_storage.length = (size_t)h->fdt_length;
    /* Release: a reader that observes `bytes` also observes the rest. */
    __atomic_store_n(&ppp_runtime_fdt_storage.bytes, (const void *)bytes, __ATOMIC_RELEASE);
    __atomic_store_n(&ppp_fdt_state, PPP_STATE_BOUND, __ATOMIC_RELEASE);
    return PPP_FDT_OK;
}

const char *
ppp_fdt_status_name(int status)
{
    static const char *const names[PPP_FDT_STATUS_COUNT] = {
        "OK", "ARGS", "ALREADY_BOUND", "WINDOW", "ALIGNMENT", "NOT_RETAINED", "HEADER",
        "LENGTH", "BUSY", "UNMAPPED", "OVERLAP", "STRUCTURE",
    };
    return status >= 0 && status < PPP_FDT_STATUS_COUNT ? names[status] : "UNKNOWN";
}
