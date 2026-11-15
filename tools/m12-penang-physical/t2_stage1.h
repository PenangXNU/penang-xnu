/* SPDX-License-Identifier: MIT */
#ifndef T2_STAGE1_H
#define T2_STAGE1_H
#include <stdint.h>

#define T2_PAGE_SIZE UINT64_C(16384)
enum t2_access { T2_RW_NX = 1, T2_RO_NX = 2, T2_RX = 4 };

struct t2_stage1 {
    volatile uint64_t *entries; /* Contiguous L3 slots, one per 16 KiB VA. */
    uint32_t count;
    uint64_t va_base, pa_base, pa_size;
    uint64_t pending_pa;
    uint32_t pending_slot;
    enum t2_access pending_access;
    uint32_t pending;
};

int t2_map(struct t2_stage1 *map, uint32_t slot, uint64_t pa, enum t2_access access);
int t2_begin_transition(struct t2_stage1 *map, uint32_t slot, enum t2_access access);
int t2_finish_transition(struct t2_stage1 *map);
void t2_abort_transition(struct t2_stage1 *map);
#endif
