/* SPDX-License-Identifier: MIT */
/*
 * M12-B4.2: Freestanding physical PMAP/lockdown & B42_TO_B1_CONTRACT validator.
 *
 * Pure C11, freestanding: ZERO MMIO accesses, ZERO SMC/HVC calls, ZERO stubs.
 * Distinguishes functional Stage-1 virtual memory from effective physical
 * kernel-memory lockdown (Stage-2/XPU + SMMU DMA + 16 KiB physmap alias control).
 * Fails closed (B42_BLOCKED_BACKEND_UNVERIFIED) in all target/device builds.
 */
#ifndef M12_PENANG_PMAP_LOCKDOWN_CONTRACT_H
#define M12_PENANG_PMAP_LOCKDOWN_CONTRACT_H

#include <stdint.h>

#if defined(B42_HOST_MODEL_SYNTHETIC_BACKEND) && defined(__aarch64__)
#error B42_HOST_MODEL_SYNTHETIC_BACKEND is strictly forbidden in AArch64/target builds
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define B42_CONTRACT_ABI_VERSION        1u
#define B42_PAGE_16K                    0x4000ULL
#define B42_PAGE_16K_MASK               (B42_PAGE_16K - 1ULL)
#define B42_L2_BLOCK_32M                0x02000000ULL
#define B42_L2_BLOCK_32M_MASK           (B42_L2_BLOCK_32M - 1ULL)
#define B42_MIN_KERNEL_WINDOW_BYTES     0x04000000ULL
#define B42_KERNEL_MACHO_MIN_OFFSET     0x03000000ULL
#define B42_EXPECTED_CACHE_LINE_BYTES   64u
#define B42_MAX_RANGES                  64u

enum b42_owner_domain {
    B42_OWNER_UNKNOWN               = 0,
    B42_OWNER_HLOS_XNU              = 1,
    B42_OWNER_QHEE_HYP_EL2          = 2,
    B42_OWNER_QSEE_TZ_EL3           = 3,
    B42_OWNER_PIL_PERIPHERAL        = 4,
    B42_OWNER_CONT_SPLASH_DISPLAY   = 5,
    B42_OWNER_BOOT_BRIDGE_RECLAIM   = 6
};

enum b42_s2_grant {
    B42_GRANT_UNVERIFIED            = 0,
    B42_GRANT_NO_HLOS               = 1,
    B42_GRANT_HLOS_RW_NX            = 2,
    B42_GRANT_HLOS_RO_NX            = 3,
    B42_GRANT_HLOS_RX_ONLY          = 4,
    B42_GRANT_HLOS_RWX_LEGACY       = 5
};

enum b42_dma_policy {
    B42_DMA_UNVERIFIED              = 0,
    B42_DMA_BYPASS_ACTIVE           = 1,
    B42_DMA_SMMU_FAULT_UNMAPPED     = 2,
    B42_DMA_READONLY_CONT_SPLASH    = 3
};

enum b42_evidence_origin {
    B42_EVIDENCE_NONE               = 0,
    B42_EVIDENCE_HOST_SYNTHETIC     = 1,
    B42_EVIDENCE_QEMU_VIRT          = 2,
    B42_EVIDENCE_APPLE_SILICON      = 3,
    B42_EVIDENCE_PENANG_DEVICE      = 4
};

enum b42_backend_kind {
    B42_BACKEND_NONE                = 0,
    B42_BACKEND_APPLE_PPL_AMCC_SPTM = 1,
    B42_BACKEND_STAGE1_NO_MONITOR   = 2,
    B42_BACKEND_SPECULATIVE_SMC     = 3,
    B42_BACKEND_QCOM_STAGE2_SMMU    = 4
};

enum b42_lockdown_class {
    B42_LOCK_STATIC_KERNEL_RX       = 1,
    B42_LOCK_STATIC_KERNEL_RO       = 2,
    B42_LOCK_IMMUTABLE_CS_DATA      = 3,
    B42_LOCK_MEDIATED_RO_ZONE       = 4
};

enum b42_status {
    B42_OK_HOST_MODEL_ONLY              = 0,
    B42_ERR_NULL_OR_ABI                 = 1,
    B42_ERR_EVIDENCE_NOT_PENANG_DEVICE  = 2,
    B42_ERR_ENTRY_EL_OR_CPU_CAP         = 3,
    B42_ERR_APPLE_HARDWARE_CLAIMED      = 4,
    B42_ERR_SILENT_STAGE1_NO_MONITOR    = 5,
    B42_ERR_SPECULATIVE_SMC_FORBIDDEN   = 6,
    B42_ERR_B1_DRAM_INVALID             = 7,
    B42_ERR_B1_UNRESOLVED_DYNAMIC_RESV  = 8,
    B42_ERR_B1_SUBPAGE_SHARED_GRANULE   = 9,
    B42_ERR_B1_WINDOW_ALIGNMENT_OR_SIZE = 10,
    B42_ERR_B1_WINDOW_RESERVED_COLLISION = 11,
    B42_ERR_B1_OWNERSHIP_UNVERIFIED     = 12,
    B42_ERR_DMA_SMMU_UNISOLATED         = 13,
    B42_ERR_RANGE_BOUNDS_OR_GRANULE     = 14,
    B42_ERR_WX_VIOLATION                = 15,
    B42_ERR_PHYSMAP_BLOCK_ALIAS_RW      = 16,
    B42_ERR_TLB_SYNC_MISSING            = 17,
    B42_ERR_UNLOCK_POLICY_VIOLATION     = 18,
    B42_BLOCKED_BACKEND_UNVERIFIED      = 19,
    B42_STATUS_COUNT                    = 20
};

struct b42_phys_region {
    uint64_t base;
    uint64_t size;
    uint32_t owner;       /* enum b42_owner_domain */
    uint32_t s2_grant;    /* enum b42_s2_grant */
    uint32_t dma_writable;
    uint32_t coalesced_16k;
};

struct b42_b1_handoff_snapshot {
    uint32_t abi_version;
    uint32_t evidence_origin;         /* enum b42_evidence_origin */
    uint32_t entry_el;                /* observed CurrentEL: must be 1 on penang */
    uint32_t tgran16_supported;       /* 1 = verified 16 KiB support */
    uint32_t icache_line_bytes;       /* 64 from CTR_EL0=0x84448004 */
    uint32_t dcache_line_bytes;       /* 64 from CTR_EL0=0x84448004 */
    uint32_t hardware_pac_claimed;    /* must be 0 on Kryo 460 */
    uint32_t unresolved_dynamic_count;/* must be 0 */
    uint32_t smmu_global_policy;      /* enum b42_dma_policy */
    uint32_t dram_count;
    struct b42_phys_region dram[B42_MAX_RANGES];
    uint32_t reserved_count;
    struct b42_phys_region reserved[B42_MAX_RANGES];
    uint64_t kernel_phys_base;        /* 32 MiB aligned */
    uint64_t kernel_mem_size;         /* 32 MiB aligned, >= 64 MiB */
    uint64_t kernel_lowest_pa;        /* _mh_execute_header PA (>= phys_base + 0x03000000) */
    uint64_t top_of_kernel_data_pa;   /* 16 KiB aligned, <= phys_base + mem_size */
    uint32_t kernel_window_grant;     /* enum b42_s2_grant */
};

struct b42_lockdown_request {
    uint32_t lockdown_class;          /* enum b42_lockdown_class */
    uint32_t backend_kind;            /* enum b42_backend_kind */
    uint64_t kva;
    uint64_t pa;
    uint64_t size;
    uint32_t req_write;
    uint32_t req_exec;
    uint32_t physmap_page_granular_16k; /* 1 = 32 MiB block split to 16 KiB PTEs */
    uint32_t physmap_aperture_rw_open;  /* 1 = EL1 phystokv remains RW (e.g. !XNU_MONITOR) */
    uint32_t stage1_aliases_revoked;    /* 1 = all PVH RW aliases demoted */
    uint32_t tlb_broadcast_synced;      /* 1 = TLBI IS + DSB ISH completed under lock */
    uint32_t kva_to_pa_rechecked;       /* 1 = post-lock kvtophys recheck matched pa */
    uint32_t dma_stream_excluded;       /* 1 = SMMU denies device writes to [pa, pa+size) */
    uint32_t already_locked;
    uint32_t is_unlock_operation;
    uint32_t prior_lockdown_class;
};

int b42_qualcomm_lockdown_backend_verified(void);
int b42_validate_b1_handoff(const struct b42_b1_handoff_snapshot *b1);
int b42_validate_lockdown_range(const struct b42_b1_handoff_snapshot *b1,
                                const struct b42_lockdown_request *req);
int b42_evaluate_lockdown_gate(const struct b42_b1_handoff_snapshot *b1,
                               const struct b42_lockdown_request *req,
                               int *out_can_promote_to_device);
const char *b42_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* M12_PENANG_PMAP_LOCKDOWN_CONTRACT_H */
