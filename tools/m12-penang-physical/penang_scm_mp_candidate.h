/* SPDX-License-Identifier: MIT */
/*
 * Qualcomm SCM memory ownership assignment: HOST-ONLY descriptor encoder.
 *
 * This is NOT an authorized XNU NS-EL1 SCM provider and cannot establish
 * Stage-2/XPU lockdown, DMA/SMMU isolation, or kernel code integrity.
 * No SMC/HVC, MMIO, cache maintenance, or physical buffer address is exposed.
 *
 * The wire shapes are derived from the already pinned M12-C2-P0 candidate
 * and the published Linux qcom_scm_assign_mem ABI (SVC MP, CMD ASSIGN).
 * The selected stock revision's authorization semantics remain UNKNOWN.
 */
#ifndef PENANG_SCM_MP_CANDIDATE_H
#define PENANG_SCM_MP_CANDIDATE_H

#include <stdint.h>

/* C ABI also importable by the existing C++ PlatformExpert build. */
#if defined(__cplusplus)
#define PENANG_SCM_MP_ALIGNAS(n) alignas(n)
extern "C" {
#else
#define PENANG_SCM_MP_ALIGNAS(n) _Alignas(n)
#endif

#if defined(PENANG_SCM_MP_ENABLE_PHYSICAL_BACKEND)
#error Physical SCM dispatch is not qualified for Penang XNU
#endif

#define PENANG_SCM_MP_CANDIDATE_ABI_VERSION 1u
#define PENANG_SCM_MP_SERVICE 0x0cu
#define PENANG_SCM_MP_ASSIGN_COMMAND 0x16u
#define PENANG_SCM_MP_GRANULE 0x4000ull
#define PENANG_SCM_MP_ALIGN 64u
#define PENANG_SCM_MP_MAX_SOURCE_VMIDS 32u
#define PENANG_SCM_MP_SCRATCH_CAPACITY 256u
/* Qualcomm SCM VM permission bit meanings (not ARM page-table AP bits). */
#define PENANG_SCM_MP_PERM_READ 4u
#define PENANG_SCM_MP_PERM_WRITE 2u
#define PENANG_SCM_MP_PERM_EXEC 1u

enum penang_scm_mp_status {
    /* Nonzero sentinel: never mistake an encoded buffer for SMC success. */
    PENANG_SCM_MP_HOST_DESCRIPTOR_ONLY = 32,
    PENANG_SCM_MP_ERR_INPUT = 1,
    PENANG_SCM_MP_ERR_RANGE = 2,
    PENANG_SCM_MP_ERR_OWNER = 3,
    PENANG_SCM_MP_ERR_DEST_PERMISSION = 4,
    PENANG_SCM_MP_ERR_CAPACITY = 5
};

struct penang_scm_mp_request {
    uint32_t abi_version;
    uint64_t pa;
    uint64_t size;
    uint32_t source_owner_bitmap; /* This candidate deliberately uses 32 VMIDs. */
    uint32_t destination_vmid;   /* Firmware-specific VMID assignment is OPEN. */
    uint32_t destination_perms;  /* HOST structural intent, NOT enforced access. */
};

struct penang_scm_mp_descriptor {
    PENANG_SCM_MP_ALIGNAS(PENANG_SCM_MP_ALIGN)
        uint8_t bytes[PENANG_SCM_MP_SCRATCH_CAPACITY];
    uint32_t used;
    uint32_t source_offset;
    uint32_t source_size;
    uint32_t range_offset;
    uint32_t range_size;
    uint32_t destination_offset;
    uint32_t destination_size;
    uint32_t service;
    uint32_t command;
};

int penang_scm_mp_encode_candidate(const struct penang_scm_mp_request *request,
                                   struct penang_scm_mp_descriptor *out);
const char *penang_scm_mp_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_SCM_MP_CANDIDATE_H */
