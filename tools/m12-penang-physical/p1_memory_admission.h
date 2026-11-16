/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PENANG_P1_MEMORY_ADMISSION_H
#define PENANG_P1_MEMORY_ADMISSION_H
#include <stddef.h>
#include <stdint.h>

enum p1_memory_status {
    P1_MEMORY_GEOMETRY_OK = 0, /* Geometry only; never a P2 or runtime certificate. */
    P1_MEMORY_BAD_FDT = 1,
    P1_MEMORY_UNRESOLVED_RESERVATION = 2,
    P1_MEMORY_BAD_WINDOW = 3,
    P1_MEMORY_RESERVED_OVERLAP = 4,
    P1_MEMORY_FDT_NOT_RETAINED = 5
};

/* Readable, immutable runtime FDT mapping supplied by the real boot bridge.
 * A snapshot/dry-run placement cannot substitute for that provider. */
struct p1_runtime_fdt {
    const void *bytes;
    size_t length;
    uint64_t physical_address;
};

/* Validate the contiguous window consumed by original arm_vm_init, before any
 * table mutation. Narrow domain: FDT v17, 2/2 cells, 16-KiB ARM64 window,
 * all reservations materialized, FDT retained in the bootstrap prefix.
 * No allocator, page-table writes, MMIO, monitor or synthetic grants. */
int p1_memory_admit(const struct p1_runtime_fdt *fdt, uint64_t base,
                    uint64_t size, uint64_t top_of_kernel_data);
#endif
