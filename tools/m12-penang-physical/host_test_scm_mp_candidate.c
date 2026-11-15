/* SPDX-License-Identifier: MIT */
/* Negative-heavy HOST_ONLY SCM MP_ASSIGN encoding tests. Never emits SMC. */
#include "penang_scm_mp_candidate.h"
#include "penang_pmap_lockdown_contract.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checked, negatives;
#define CHECK(expr) do {     ++checked;     if (!(expr)) {         fprintf(stderr, "SCM_MP_TEST_FAIL line=%u expr=%s\n",                 (unsigned)__LINE__, #expr);         return 1;     } } while (0)

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t result = 0;
    unsigned i;
    for (i = 0; i < 8u; ++i) result |= (uint64_t)p[i] << (i * 8u);
    return result;
}
static int all_zero(const struct penang_scm_mp_descriptor *out)
{
    const uint8_t *p = (const uint8_t *)out;
    unsigned i;
    for (i = 0; i < sizeof(*out); ++i) if (p[i]) return 0;
    return 1;
}

static struct penang_scm_mp_request valid(void)
{
    struct penang_scm_mp_request r = {
        PENANG_SCM_MP_CANDIDATE_ABI_VERSION,
        UINT64_C(0xa3004000), UINT64_C(0x800000),
        (1u << 1) | (1u << 3),
        1u, PENANG_SCM_MP_PERM_READ | PENANG_SCM_MP_PERM_EXEC
    };
    return r;
}
static int rejected(const struct penang_scm_mp_request *r, int expected)
{
    struct penang_scm_mp_descriptor out;
    int rc;
    memset(&out, 0xa5, sizeof(out));
    rc = penang_scm_mp_encode_candidate(r, &out);
    ++negatives;
    CHECK(rc == expected);
    CHECK(all_zero(&out));
    return 0;
}

int main(void)
{
    struct penang_scm_mp_request r;
    struct penang_scm_mp_descriptor out;
    unsigned i;
    int rc;

    /* Existing B4.2 production lockdown provider remains explicitly blocked. */
    CHECK(b42_qualcomm_lockdown_backend_verified() == 0);
    CHECK(_Alignof(struct penang_scm_mp_descriptor) >= PENANG_SCM_MP_ALIGN);
    /* Qualcomm SCM permission encoding is R=4/W=2/X=1, not R=1/X=4. */
    CHECK(PENANG_SCM_MP_PERM_READ == 4u);
    CHECK(PENANG_SCM_MP_PERM_WRITE == 2u);
    CHECK(PENANG_SCM_MP_PERM_EXEC == 1u);
    CHECK(strcmp(penang_scm_mp_status_name(PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY),
                 "HOST_DESCRIPTOR_ONLY") == 0);
    CHECK(strcmp(penang_scm_mp_status_name(0), "UNKNOWN") == 0);
    CHECK(strcmp(penang_scm_mp_status_name(-1), "UNKNOWN") == 0);
    CHECK(strcmp(penang_scm_mp_status_name(99), "UNKNOWN") == 0);

    r = valid();
    memset(&out, 0xa5, sizeof(out));
    rc = penang_scm_mp_encode_candidate(&r, &out);
    CHECK(rc == PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY);
    CHECK(((uintptr_t)out.bytes & (PENANG_SCM_MP_ALIGN - 1u)) == 0u);
    CHECK(out.used == 192u && out.source_size == 8u);
    CHECK(out.range_offset == 64u && out.range_size == 16u);
    CHECK(out.destination_offset == 128u && out.destination_size == 24u);
    CHECK(out.service == 0x0cu && out.command == 0x16u);
    CHECK(get32(&out.bytes[0]) == 1u);
    CHECK(get32(&out.bytes[4]) == 3u);
    CHECK(get64(&out.bytes[64]) == r.pa);
    CHECK(get64(&out.bytes[72]) == r.size);
    CHECK(get32(&out.bytes[128]) == 1u);
    CHECK(get32(&out.bytes[132]) == 5u);
    for (i = 8u; i < 64u; ++i) CHECK(out.bytes[i] == 0u);
    for (i = 80u; i < 128u; ++i) CHECK(out.bytes[i] == 0u);
    for (i = 136u; i < out.used; ++i) CHECK(out.bytes[i] == 0u);
    for (i = out.used; i < sizeof(out.bytes); ++i) CHECK(out.bytes[i] == 0u);

    r = valid(); r.source_owner_bitmap = UINT32_MAX; r.destination_vmid = 31u;
    CHECK(penang_scm_mp_encode_candidate(&r, &out) ==
          PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY);
    CHECK(out.source_size == 128u && out.range_offset == 128u);
    CHECK(out.destination_offset == 192u && out.used == 256u);
    CHECK(get32(&out.bytes[124]) == 31u);
    CHECK(get64(&out.bytes[128]) == r.pa);
    CHECK(get32(&out.bytes[192]) == 31u);

    r = valid(); r.destination_perms = PENANG_SCM_MP_PERM_READ;
    CHECK(penang_scm_mp_encode_candidate(&r, &out) ==
          PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY);
    CHECK(get32(&out.bytes[132]) == PENANG_SCM_MP_PERM_READ);

    r = valid(); r.abi_version = 0u;
    if (rejected(&r, PENANG_SCM_MP_ERR_INPUT)) return 1;
    r = valid(); r.pa++;
    if (rejected(&r, PENANG_SCM_MP_ERR_RANGE)) return 1;
    r = valid(); r.size--;
    if (rejected(&r, PENANG_SCM_MP_ERR_RANGE)) return 1;
    r = valid(); r.size = 0u;
    if (rejected(&r, PENANG_SCM_MP_ERR_RANGE)) return 1;
    r = valid(); r.pa = UINT64_MAX - UINT64_C(0x3fff); r.size = 0x4000u;
    if (rejected(&r, PENANG_SCM_MP_ERR_RANGE)) return 1;
    r = valid(); r.source_owner_bitmap = 0u;
    if (rejected(&r, PENANG_SCM_MP_ERR_OWNER)) return 1;
    r = valid(); r.destination_vmid = 32u;
    if (rejected(&r, PENANG_SCM_MP_ERR_OWNER)) return 1;
    r = valid(); r.destination_perms = PENANG_SCM_MP_PERM_WRITE;
    if (rejected(&r, PENANG_SCM_MP_ERR_DEST_PERMISSION)) return 1;
    r = valid(); r.destination_perms = PENANG_SCM_MP_PERM_EXEC;
    if (rejected(&r, PENANG_SCM_MP_ERR_DEST_PERMISSION)) return 1;
    r = valid(); r.destination_perms = PENANG_SCM_MP_PERM_READ | PENANG_SCM_MP_PERM_WRITE;
    if (rejected(&r, PENANG_SCM_MP_ERR_DEST_PERMISSION)) return 1;
    r = valid(); r.destination_perms = 7u;
    if (rejected(&r, PENANG_SCM_MP_ERR_DEST_PERMISSION)) return 1;
    r = valid(); r.destination_perms = 0u;
    if (rejected(&r, PENANG_SCM_MP_ERR_DEST_PERMISSION)) return 1;
    if (rejected(NULL, PENANG_SCM_MP_ERR_INPUT)) return 1;
    CHECK(penang_scm_mp_encode_candidate(&r, NULL) == PENANG_SCM_MP_ERR_INPUT);
    CHECK(b42_qualcomm_lockdown_backend_verified() == 0);

    printf("PENANG_SCM_MP_HOST_CANDIDATE_PASS checks=%u negatives=%u physical_authority=BLOCKED no_smc=1\n",
           checked, negatives);
    return 0;
}
