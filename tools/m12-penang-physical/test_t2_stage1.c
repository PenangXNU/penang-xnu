/* SPDX-License-Identifier: MIT */
#include "t2_stage1.h"

#ifdef T2_STAGE1_QEMU
static void print(const char *s)
{
    volatile uint32_t *uart = (void *)0x09000000;
    for (; *s; s++)
        *uart = (uint32_t)*s;
}
#define CHECK(x) do { if (!(x)) { print("T2 FAIL: " #x "\n"); for (;;) __asm__("wfe"); } } while (0)
static uint64_t root[2048] __attribute__((aligned(16384)));
static uint64_t pages[2048] __attribute__((aligned(16384)));
__asm__(".pushsection .text.entry,\"ax\"\n"
        ".global _start\n_start:\n"
        "ldr x0, =test_stack_end\nmov sp, x0\nbl t2_test\n1: wfe\nb 1b\n"
        ".popsection\n.pushsection .bss\n.balign 16\n.space 16384\ntest_stack_end:\n.popsection");
static int access_ok(uint64_t va, int write)
{
    uint64_t result;
    if (write)
        __asm__ volatile("at s1e1w, %0" :: "r"(va) : "memory");
    else
        __asm__ volatile("at s1e1r, %0" :: "r"(va) : "memory");
    __asm__ volatile("isb\n\tmrs %0, par_el1" : "=r"(result));
    return !(result & 1);
}
static void enable(void)
{
    root[4] = UINT64_C(0x08000000) | (1ull << 53) | (1ull << 54) | 0x405;
    root[32] = UINT64_C(0x40000000) | 0x701;
    root[33] = (uint64_t)pages | 3;
    uint64_t tcr = 28ull | (1ull << 8) | (1ull << 10) | (3ull << 12) |
        (2ull << 14) | (1ull << 23) | (2ull << 32);
    uint64_t sctlr;
    __asm__ volatile("msr mair_el1, %0\n\tmsr tcr_el1, %1\n\tmsr ttbr0_el1, %2\n\tdsb sy\n\tisb\n\tmrs %3, sctlr_el1"
                     : "+r"((uint64_t){0xff}), "+r"(tcr), "+r"((uint64_t){(uint64_t)root}), "=r"(sctlr) :: "memory");
    sctlr |= 0x1005; /* MMU, data cache and instruction cache. */
    __asm__ volatile("msr sctlr_el1, %0\n\tisb" :: "r"(sctlr) : "memory");
}
void t2_test(void)
#else
#include <assert.h>
#include <stdio.h>
#define CHECK(x) assert(x)
#define print(s) puts(s)
static uint64_t pages[2048];
int main(void)
#endif
{
    struct t2_stage1 map = { .entries = pages, .count = 3, .va_base = 0x42000000,
                            .pa_base = 0x43000000, .pa_size = 16384 };
    uint64_t va = map.va_base, pa = map.pa_base;
    CHECK(t2_map(&map, 0, pa + 16384, T2_RW_NX) != 0);
    CHECK(t2_map(&map, 0, pa + 1, T2_RW_NX) != 0);
    CHECK(t2_map(&map, 0, pa, T2_RW_NX | T2_RX) != 0);
    CHECK(t2_map(&map, 0, pa, T2_RW_NX) == 0);
    CHECK(t2_map(&map, 1, pa, T2_RW_NX) == 0);
    CHECK(t2_map(&map, 1, pa, T2_RO_NX) != 0);
    CHECK((pages[0] & (1ull << 53)) && !(pages[0] & 128));
#ifdef T2_STAGE1_QEMU
    enable();
    CHECK(access_ok(va, 1) && access_ok(va + 16384, 1));
    volatile uint32_t *code = (void *)va;
    code[0] = 0x52800540; /* mov w0, #42 */
    code[1] = 0xd65f03c0; /* ret */
#endif
    CHECK(t2_begin_transition(&map, 0, T2_RO_NX) == 0);
    CHECK(pages[0] == 0 && pages[1] == 0);
    CHECK(t2_map(&map, 2, pa, T2_RW_NX) != 0);
    t2_abort_transition(&map);
    CHECK(t2_finish_transition(&map) != 0 && pages[0] == 0 && pages[1] == 0);
#ifdef T2_STAGE1_QEMU
    CHECK(!access_ok(va, 0) && !access_ok(va + 16384, 0));
#endif
    CHECK(t2_map(&map, 0, pa, T2_RW_NX) == 0);
    CHECK(t2_map(&map, 1, pa, T2_RW_NX) == 0);
    CHECK(t2_begin_transition(&map, 0, T2_RO_NX) == 0);
    CHECK(t2_finish_transition(&map) == 0);
    CHECK(pages[1] == 0 && (pages[0] & 128) && (pages[0] & (1ull << 53)));
#ifdef T2_STAGE1_QEMU
    CHECK(access_ok(va, 0) && !access_ok(va, 1) && !access_ok(va + 16384, 1));
#endif
    CHECK(t2_begin_transition(&map, 0, T2_RX) == 0);
    CHECK(t2_finish_transition(&map) == 0);
    CHECK((pages[0] & 128) && !(pages[0] & (1ull << 53)));
    CHECK(t2_map(&map, 1, pa, T2_RW_NX) != 0);
#ifdef T2_STAGE1_QEMU
    CHECK(!access_ok(va, 1) && !access_ok(va + 16384, 0));
    CHECK(((int (*)(void))va)() == 42);
#else
    (void)va;
#endif
    CHECK(t2_begin_transition(&map, 0, T2_RW_NX) == 0);
    CHECK(t2_finish_transition(&map) == 0 && (pages[0] & (1ull << 53)));
#ifdef T2_STAGE1_QEMU
    CHECK(access_ok(va, 1));
    print("T2_STAGE1_PMAP_QEMU_VERIFIED\n");
#else
    print("T2_STAGE1_HOST_PASS");
    return 0;
#endif
}
