/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Provider of the retained run-time FDT for the P1 native kernel.
 *
 * The native VM bootstrap reads the descriptor `penang_p1_runtime_fdt`
 * (declared by tools/m12-penang-physical/p1_runtime_fdt_provider.h) and
 * passes it to p1_memory_admit() before it touches any translation table.
 * This module defines that object. It starts zeroed, so admission refuses
 * it, and is published exactly once by ppp_runtime_fdt_bind(). The same
 * object then feeds ppp_resolve(): one tree, one source of truth, never a
 * synthesized one.
 *
 * Bind contract (ABI 2):
 *   - It runs on the boot CPU with interrupts masked, after the bootstrap
 *     kernel mapping is live and before arm_vm_init. It is not reentrant;
 *     a nested or concurrent call is refused, not serialized.
 *   - No byte at the computed virtual address is read until the caller's
 *     translation hook has confirmed, page by page, that every page of the
 *     tree is mapped readable at exactly the expected physical address.
 *   - The physical copy must lie in the part of the kernel's static region
 *     that XNU keeps for its whole lifetime: [kernel_end_pa,
 *     top_of_kernel_data), the "device tree, RAM disk, boot_args" range
 *     that arm_vm_physmap_init maps RWNX and that no ml_static_mfree path
 *     frees. The prefix [phys_base, segLOWEST) is freed by
 *     arm_vm_prot_finalize and is refused. The copy must not overlap the
 *     Apple device tree or boot_args, which share that range.
 *   - The whole tree is validated structurally (header, block offsets and
 *     overlaps, memory reservation map, token stream, names, property
 *     order and uniqueness) before anything is published.
 *   - Any refusal leaves the descriptor byte-for-byte unchanged and the
 *     single assignment unconsumed.
 *
 * Bind checks placement, mapping and structure. It does not judge memory
 * ownership (that is p1_memory_admit), authenticity, or integrity of the
 * content against a digest.
 */
#ifndef PENANG_P1_RUNTIME_FDT_H
#define PENANG_P1_RUNTIME_FDT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPP_FDT_ABI_VERSION 2u

enum ppp_fdt_status {
    PPP_FDT_OK = 0,
    PPP_FDT_E_ARGS = 1,
    PPP_FDT_E_ALREADY_BOUND = 2, /* single assignment already consumed */
    PPP_FDT_E_WINDOW = 3,        /* kernel window or VA contract malformed */
    PPP_FDT_E_ALIGNMENT = 4,     /* a DTB is 8-byte aligned */
    PPP_FDT_E_NOT_RETAINED = 5,  /* outside [kernel_end_pa, top_of_kernel_data) */
    PPP_FDT_E_HEADER = 6,        /* not a version-17 flattened device tree */
    PPP_FDT_E_LENGTH = 7,        /* handed-over length != header totalsize */
    PPP_FDT_E_BUSY = 8,          /* a bind is already in progress */
    PPP_FDT_E_UNMAPPED = 9,      /* translation hook refused a page, or PA differs */
    PPP_FDT_E_OVERLAP = 10,      /* overlaps the Apple device tree or boot_args */
    PPP_FDT_E_STRUCTURE = 11,    /* blocks, reservation map or token stream invalid */
    PPP_FDT_STATUS_COUNT = 12
};

#define PPP_FDT_MAX_BYTES (2u * 1024u * 1024u) /* same bound as admission */
#define PPP_FDT_PAGE      0x4000u              /* 16 KiB kernel page */
#define PPP_BOOT_ARGS_BYTES 1152u              /* XNU 11417 boot_args, unchanged */

/* What the kernel adapter assembles from boot_args, the Apple device tree
 * and its own image. All addresses are physical except virt_base. */
struct ppp_fdt_handoff {
    uint32_t abi_version;        /* PPP_FDT_ABI_VERSION */
    uint32_t reserved0;          /* must be zero */
    uint64_t fdt_pa;
    uint64_t fdt_length;         /* exact byte length of the retained copy */
    uint64_t phys_base;          /* boot_args.physBase */
    uint64_t virt_base;          /* boot_args.virtBase: VA of phys_base */
    uint64_t mem_size;           /* boot_args.memSize */
    uint64_t kernel_end_pa;      /* PA of end_kern: round_page(last kernel segment end) */
    uint64_t top_of_kernel_data; /* boot_args.topOfKernelData */
    uint64_t adt_pa;             /* boot_args.deviceTreeP as a physical address */
    uint64_t adt_length;         /* boot_args.deviceTreeLength */
    uint64_t boot_args_pa;       /* physical address of boot_args itself */
};

/*
 * Mapping evidence supplied by the adapter. `translate` must not fault: on
 * hardware it is an address-translation instruction (AT S1E1R and PAR_EL1),
 * not a load. It returns 0 and the physical address when `va` is mapped
 * readable at EL1, nonzero otherwise. [va_min, va_max) bounds the virtual
 * addresses the adapter vouches for (the kernel half of the address space
 * on hardware). All fields are mandatory.
 */
struct ppp_fdt_mapping {
    void *ctx;
    int (*translate)(void *ctx, uint64_t va, uint64_t *pa);
    uint64_t va_min;
    uint64_t va_max;
};

int ppp_runtime_fdt_bind(const struct ppp_fdt_handoff *handoff, const struct ppp_fdt_mapping *mapping);

/* Structural validation of a complete tree held in readable memory.
 * Exposed for the bridge (which validates its retained copy) and tests. */
int ppp_fdt_check_structure(const uint8_t *fdt, size_t length);

const char *ppp_fdt_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_P1_RUNTIME_FDT_H */
