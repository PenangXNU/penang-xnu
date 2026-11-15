/* SPDX-License-Identifier: MIT */
#include "t2_stage1.h"

#if defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(PENANG_PHYSICAL)
#error T2 Stage-1 model is restricted to isolated tests
#endif

#define PA_MASK UINT64_C(0x0000ffffffffc000)
#define L3_PAGE UINT64_C(3)
#define AF (UINT64_C(1) << 10)
#define SH_INNER (UINT64_C(3) << 8)
#define AP_RO (UINT64_C(1) << 7)
#define PXN (UINT64_C(1) << 53)
#define UXN (UINT64_C(1) << 54)

static int access_valid(enum t2_access access)
{
    return access == T2_RW_NX || access == T2_RO_NX || access == T2_RX;
}

static int pa_admitted(const struct t2_stage1 *map, uint64_t pa)
{
    return (pa & PA_MASK) == pa && !(map->pa_base & (T2_PAGE_SIZE - 1)) &&
        !(map->pa_size & (T2_PAGE_SIZE - 1)) &&
        map->pa_size <= UINT64_MAX - map->pa_base && map->pa_size >= T2_PAGE_SIZE &&
        pa >= map->pa_base && pa - map->pa_base <= map->pa_size - T2_PAGE_SIZE;
}

static uint64_t descriptor(uint64_t pa, enum t2_access access)
{
    return pa | L3_PAGE | AF | SH_INNER | UXN |
        (access == T2_RW_NX ? 0 : AP_RO) | (access == T2_RX ? 0 : PXN);
}

static void sync_translation(void)
{
#ifdef T2_STAGE1_QEMU
    __asm__ volatile("dsb ishst\n\ttlbi vmalle1is\n\tdsb ish\n\tisb" ::: "memory");
#endif
}

static void sync_code(uint64_t va)
{
#ifdef T2_STAGE1_QEMU
    uint64_t ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    uint64_t line = UINT64_C(4) << ((ctr >> 16) & 15u);
    for (uint64_t off = 0; off < T2_PAGE_SIZE; off += line)
        __asm__ volatile("dc cvau, %0" :: "r"(va + off) : "memory");
    __asm__ volatile("dsb ish\n\tic ialluis\n\tdsb ish\n\tisb" ::: "memory");
#else
    (void)va;
#endif
}

int t2_map(struct t2_stage1 *map, uint32_t slot, uint64_t pa, enum t2_access access)
{
    if (map->pending || slot >= map->count || map->entries[slot] ||
        !access_valid(access) || !pa_admitted(map, pa))
        return -1;
    /* Establish mappings NX. Publishing RX requires the transition path. */
    if (access == T2_RX)
        return -1;
    if (access == T2_RW_NX) {
        for (uint32_t i = 0; i < map->count; i++) {
            uint64_t pte = map->entries[i];
            if ((pte & L3_PAGE) == L3_PAGE && (pte & PA_MASK) == pa && !(pte & PXN))
                return -1;
        }
    }
    map->entries[slot] = descriptor(pa, access);
    sync_translation();
    return 0;
}

int t2_begin_transition(struct t2_stage1 *map, uint32_t slot, enum t2_access access)
{
    if (map->pending || slot >= map->count || !access_valid(access) ||
        (map->entries[slot] & L3_PAGE) != L3_PAGE ||
        (map->va_base & (T2_PAGE_SIZE - 1)) ||
        map->va_base > UINT64_MAX - (uint64_t)map->count * T2_PAGE_SIZE)
        return -1;
    uint64_t pa = map->entries[slot] & PA_MASK;
    if (!pa_admitted(map, pa))
        return -1;
    if (access == T2_RX)
        sync_code(map->va_base + (uint64_t)slot * T2_PAGE_SIZE);
    map->pending = 1;
    map->pending_pa = pa;
    map->pending_slot = slot;
    map->pending_access = access;
    for (uint32_t i = 0; i < map->count; i++) {
        uint64_t pte = map->entries[i];
        if ((pte & L3_PAGE) == L3_PAGE && (pte & PA_MASK) == pa)
            map->entries[i] = 0;
    }
    sync_translation();
    return 0;
}

int t2_finish_transition(struct t2_stage1 *map)
{
    if (!map->pending || map->pending_slot >= map->count ||
        map->entries[map->pending_slot] || !pa_admitted(map, map->pending_pa) ||
        !access_valid(map->pending_access))
        return -1;
    for (uint32_t i = 0; i < map->count; i++) {
        uint64_t pte = map->entries[i];
        if ((pte & L3_PAGE) == L3_PAGE && (pte & PA_MASK) == map->pending_pa)
            return -1;
    }
    map->entries[map->pending_slot] = descriptor(map->pending_pa, map->pending_access);
    sync_translation();
    map->pending = 0;
    return 0;
}

void t2_abort_transition(struct t2_stage1 *map)
{
    /* Break already invalidated every alias. Leave the page unmapped. */
    map->pending = 0;
}
