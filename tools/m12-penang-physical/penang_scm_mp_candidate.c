/* SPDX-License-Identifier: MIT */
/* HOST-ONLY Qualcomm SCM MP_ASSIGN wire serializer. NO SCM transport. */
#include "penang_scm_mp_candidate.h"

static void zero_descriptor(struct penang_scm_mp_descriptor *out)
{
    uint32_t i;
    uint8_t *p;
    if (!out) return;
    p = (uint8_t *)out;
    for (i = 0; i < (uint32_t)sizeof(*out); ++i) p[i] = 0u;
}

static uint32_t align64(uint32_t n)
{
    return (n + PENANG_SCM_MP_ALIGN - 1u) &
           ~(PENANG_SCM_MP_ALIGN - 1u);
}

static void put32(uint8_t *dst, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 4u; ++i) dst[i] = (uint8_t)(v >> (i * 8u));
}

static void put64(uint8_t *dst, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; ++i) dst[i] = (uint8_t)(v >> (i * 8u));
}

static uint32_t count_owners(uint32_t bitmap)
{
    uint32_t total = 0, i;
    for (i = 0; i < PENANG_SCM_MP_MAX_SOURCE_VMIDS; ++i) {
        if ((bitmap >> i) & 1u) ++total;
    }
    return total;
}

const char *penang_scm_mp_status_name(int code)
{
    static const char *const errors[] = {
        "INVALID", "ERR_INPUT", "ERR_RANGE", "ERR_OWNER",
        "ERR_DEST_PERMISSION", "ERR_CAPACITY"
    };
    if (code == PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY) {
        return "HOST_DESCRIPTOR_ONLY";
    }
    return code > 0 && code <= PENANG_SCM_MP_ERR_CAPACITY ?
        errors[code] : "UNKNOWN";
}

int penang_scm_mp_encode_candidate(const struct penang_scm_mp_request *req,
                                   struct penang_scm_mp_descriptor *out)
{
    uint32_t src_bytes, src_count, region_off, dest_off, required, i, n;
    uint64_t end;
    if (!out) return PENANG_SCM_MP_ERR_INPUT;
    zero_descriptor(out);
    if (!req || req->abi_version != PENANG_SCM_MP_CANDIDATE_ABI_VERSION) {
        return PENANG_SCM_MP_ERR_INPUT;
    }
    if (!req->size || (req->pa & (PENANG_SCM_MP_GRANULE - 1ull)) ||
        (req->size & (PENANG_SCM_MP_GRANULE - 1ull)) ||
        req->size > UINT64_MAX - req->pa) {
        return PENANG_SCM_MP_ERR_RANGE;
    }
    end = req->pa + req->size;
    if (end <= req->pa) return PENANG_SCM_MP_ERR_RANGE;
    if (!req->source_owner_bitmap ||
        req->destination_vmid >= PENANG_SCM_MP_MAX_SOURCE_VMIDS) {
        return PENANG_SCM_MP_ERR_OWNER;
    }
    /* A read-only or executable-but-not-writable candidate only.
       This restriction is NOT proof of enforcement by secure firmware. */
    if (req->destination_perms != PENANG_SCM_MP_PERM_READ &&
        req->destination_perms !=
            (PENANG_SCM_MP_PERM_READ | PENANG_SCM_MP_PERM_EXEC)) {
        return PENANG_SCM_MP_ERR_DEST_PERMISSION;
    }
    src_count = count_owners(req->source_owner_bitmap);
    src_bytes = src_count * 4u;
    region_off = align64(src_bytes);
    dest_off = region_off + align64(16u);
    required = dest_off + align64(24u);
    if (required > PENANG_SCM_MP_SCRATCH_CAPACITY) {
        return PENANG_SCM_MP_ERR_CAPACITY;
    }
    n = 0;
    for (i = 0; i < PENANG_SCM_MP_MAX_SOURCE_VMIDS; ++i) {
        if ((req->source_owner_bitmap >> i) & 1u) {
            put32(&out->bytes[n * 4u], i);
            ++n;
        }
    }
    put64(&out->bytes[region_off], req->pa);
    put64(&out->bytes[region_off + 8u], req->size);
    put32(&out->bytes[dest_off], req->destination_vmid);
    put32(&out->bytes[dest_off + 4u], req->destination_perms);
    /* ctx (8), ctx_size (4), unused (4): guaranteed zero by initialization. */
    out->used = required;
    out->source_offset = 0u;
    out->source_size = src_bytes;
    out->range_offset = region_off;
    out->range_size = 16u;
    out->destination_offset = dest_off;
    out->destination_size = 24u;
    out->service = PENANG_SCM_MP_SERVICE;
    out->command = PENANG_SCM_MP_ASSIGN_COMMAND;
    return PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY;
}
