/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Provider of the retained run-time FDT for the P1 native kernel.
 *
 * The native VM bootstrap declares
 *     extern const struct p1_runtime_fdt penang_p1_runtime_fdt;
 * and passes it to p1_memory_admit() before it touches any translation
 * table. This module defines that object. It starts zeroed, so admission
 * refuses it, and is filled exactly once by ppp_runtime_fdt_bind() from the
 * values the boot bridge handed over. The same object then feeds
 * ppp_resolve(): one tree, one source of truth, never a synthesized one.
 *
 * struct p1_runtime_fdt is owned by the memory-admission interface
 * (p1_memory_admission.h); it is used here unchanged.
 */
#ifndef PENANG_P1_RUNTIME_FDT_H
#define PENANG_P1_RUNTIME_FDT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct p1_runtime_fdt;

enum ppp_fdt_status {
    PPP_FDT_OK = 0,
    PPP_FDT_E_ARGS = 1,
    PPP_FDT_E_ALREADY_BOUND = 2, /* the provider is single-assignment */
    PPP_FDT_E_WINDOW = 3,        /* kernel window itself is malformed */
    PPP_FDT_E_ALIGNMENT = 4,     /* a DTB is 8-byte aligned */
    PPP_FDT_E_NOT_RETAINED = 5,  /* not wholly inside the retained prefix */
    PPP_FDT_E_HEADER = 6,        /* not a version-17 flattened device tree */
    PPP_FDT_E_LENGTH = 7,        /* handed-over length != header totalsize */
    PPP_FDT_STATUS_COUNT = 8
};

#define PPP_FDT_MAX_BYTES (2u * 1024u * 1024u) /* same bound as admission */

/* What the bridge must hand over. All addresses are physical except
 * virt_base, the address at which phys_base is mapped when bind runs. */
struct ppp_fdt_handoff {
    uint64_t fdt_pa;
    uint64_t fdt_length;         /* exact byte length of the retained copy */
    uint64_t phys_base;          /* boot_args.physBase */
    uint64_t virt_base;          /* boot_args.virtBase */
    uint64_t top_of_kernel_data; /* boot_args.topOfKernelData, physical */
};

/*
 * Validate and publish the retained tree. Runs on the boot CPU, before
 * arm_vm_init, with interrupts masked; it is not reentrant. The copy must
 * lie wholly in [phys_base, top_of_kernel_data), the bootstrap prefix that
 * the bridge loaded and that stays mapped at virt_base through VM bring-up,
 * so that the published pointer never dangles. On any failure nothing is
 * published and admission keeps refusing.
 */
int ppp_runtime_fdt_bind(const struct ppp_fdt_handoff *handoff);
const char *ppp_fdt_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_P1_RUNTIME_FDT_H */
