/* SPDX-License-Identifier: Apache-2.0 */
#include "ppp_runtime_fdt.h"
#include "p1_memory_admission.h" /* struct p1_runtime_fdt: single definition */

#if defined(__APPLE__) || defined(__MACH__)
#define PPP_C_SYMBOL(name) "_" name
#else
#define PPP_C_SYMBOL(name) name
#endif

/*
 * Consumers see this object as `const struct p1_runtime_fdt
 * penang_p1_runtime_fdt`. It cannot be a link-time constant because the
 * address is only known at run time, so the storage is defined here under a
 * private C name bound to the same linker symbol. No translation unit ever
 * holds two conflicting declarations of one identifier.
 */
struct p1_runtime_fdt ppp_runtime_fdt_storage __asm__(PPP_C_SYMBOL("penang_p1_runtime_fdt"));
struct p1_runtime_fdt ppp_runtime_fdt_storage;

static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int
ppp_runtime_fdt_bind(const struct ppp_fdt_handoff *h)
{
    const uint8_t *bytes;
    uint64_t va;
    if (h == NULL) {
        return PPP_FDT_E_ARGS;
    }
    if (ppp_runtime_fdt_storage.bytes != NULL || ppp_runtime_fdt_storage.length != 0) {
        return PPP_FDT_E_ALREADY_BOUND;
    }
    if (h->top_of_kernel_data <= h->phys_base ||
        h->virt_base + (h->top_of_kernel_data - h->phys_base) < h->virt_base) {
        return PPP_FDT_E_WINDOW;
    }
    if (h->fdt_pa == 0 || (h->fdt_pa & 7u) != 0) {
        return PPP_FDT_E_ALIGNMENT;
    }
    if (h->fdt_length < 40 || h->fdt_length > PPP_FDT_MAX_BYTES || h->fdt_pa < h->phys_base ||
        h->fdt_pa >= h->top_of_kernel_data ||
        h->fdt_length > h->top_of_kernel_data - h->fdt_pa) {
        return PPP_FDT_E_NOT_RETAINED;
    }
    va = h->virt_base + (h->fdt_pa - h->phys_base);
    if (va != (uint64_t)(uintptr_t)va) {
        return PPP_FDT_E_WINDOW;
    }
    bytes = (const uint8_t *)(uintptr_t)va;
    /* magic, version 17, last compatible version <= 17 */
    if (be32(bytes) != 0xd00dfeedu || be32(bytes + 20) != 17 || be32(bytes + 24) > 17) {
        return PPP_FDT_E_HEADER;
    }
    /* A longer hand-over would leave unvalidated bytes inside the object; a
     * shorter one would let the tree describe memory outside it. */
    if (be32(bytes + 4) != h->fdt_length) {
        return PPP_FDT_E_LENGTH;
    }
    ppp_runtime_fdt_storage.physical_address = h->fdt_pa;
    ppp_runtime_fdt_storage.length = (size_t)h->fdt_length;
    ppp_runtime_fdt_storage.bytes = bytes; /* written last: marks it bound */
    return PPP_FDT_OK;
}

const char *
ppp_fdt_status_name(int status)
{
    static const char *const names[PPP_FDT_STATUS_COUNT] = {
        "OK", "ARGS", "ALREADY_BOUND", "WINDOW", "ALIGNMENT", "NOT_RETAINED", "HEADER", "LENGTH",
    };
    return status >= 0 && status < PPP_FDT_STATUS_COUNT ? names[status] : "UNKNOWN";
}
