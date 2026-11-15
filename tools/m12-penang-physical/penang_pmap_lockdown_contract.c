/* SPDX-License-Identifier: MIT */
/*
 * M12-B4.2: Freestanding C11 implementation of B42_TO_B1_CONTRACT and
 * physical PMAP/lockdown invariant checks. No MMIO, no SMC, no stubs.
 */
#include "penang_pmap_lockdown_contract.h"

static int b42_add_overflow(uint64_t a, uint64_t b, uint64_t *sum)
{
    if (b > UINT64_MAX - a) {
        return 1;
    }
    *sum = a + b;
    return 0;
}

static int b42_ranges_overlap(uint64_t a_base, uint64_t a_end,
                              uint64_t b_base, uint64_t b_end)
{
    return a_base < b_end && b_base < a_end;
}

static const char *const k_b42_status_names[B42_STATUS_COUNT] = {
    "OK_HOST_MODEL_ONLY",
    "ERR_NULL_OR_ABI",
    "ERR_EVIDENCE_NOT_PENANG_DEVICE",
    "ERR_ENTRY_EL_OR_CPU_CAP",
    "ERR_APPLE_HARDWARE_CLAIMED",
    "ERR_SILENT_STAGE1_NO_MONITOR",
    "ERR_SPECULATIVE_SMC_FORBIDDEN",
    "ERR_B1_DRAM_INVALID",
    "ERR_B1_UNRESOLVED_DYNAMIC_RESV",
    "ERR_B1_SUBPAGE_SHARED_GRANULE",
    "ERR_B1_WINDOW_ALIGNMENT_OR_SIZE",
    "ERR_B1_WINDOW_RESERVED_COLLISION",
    "ERR_B1_OWNERSHIP_UNVERIFIED",
    "ERR_DMA_SMMU_UNISOLATED",
    "ERR_RANGE_BOUNDS_OR_GRANULE",
    "ERR_WX_VIOLATION",
    "ERR_PHYSMAP_BLOCK_ALIAS_RW",
    "ERR_TLB_SYNC_MISSING",
    "ERR_UNLOCK_POLICY_VIOLATION",
    "BLOCKED_BACKEND_UNVERIFIED"
};

const char *b42_status_name(int status)
{
    if (status < 0 || status >= B42_STATUS_COUNT) {
        return "UNKNOWN";
    }
    return k_b42_status_names[status];
}

int b42_qualcomm_lockdown_backend_verified(void)
{
#if defined(B42_HOST_MODEL_SYNTHETIC_BACKEND) && !defined(__aarch64__)
    return 1;
#else
    /*
     * Physical Qualcomm SM4350 Stage-2 (QHEE hyp@80000000) / XPU (QSEE TZ) /
     * SMMU (qcom,smmu-500) lockdown ABI is OPEN/UNVERIFIED on stock penang.
     * Always fail closed in production/target builds.
     */
    return 0;
#endif
}

int b42_validate_b1_handoff(const struct b42_b1_handoff_snapshot *b1)
{
    uint32_t i;
    uint32_t j;
    uint64_t win_end = 0;
    int contained_in_dram = 0;

    if (!b1 || b1->abi_version != B42_CONTRACT_ABI_VERSION) {
        return B42_ERR_NULL_OR_ABI;
    }
    if (b1->evidence_origin != B42_EVIDENCE_PENANG_DEVICE) {
        return B42_ERR_EVIDENCE_NOT_PENANG_DEVICE;
    }
    if (b1->hardware_pac_claimed != 0) {
        return B42_ERR_APPLE_HARDWARE_CLAIMED;
    }
    if (b1->entry_el != 1u ||
        b1->tgran16_supported != 1u ||
        b1->icache_line_bytes != B42_EXPECTED_CACHE_LINE_BYTES ||
        b1->dcache_line_bytes != B42_EXPECTED_CACHE_LINE_BYTES) {
        return B42_ERR_ENTRY_EL_OR_CPU_CAP;
    }
    if (b1->unresolved_dynamic_count != 0u) {
        return B42_ERR_B1_UNRESOLVED_DYNAMIC_RESV;
    }
    if (b1->dram_count == 0u || b1->dram_count > B42_MAX_RANGES ||
        b1->reserved_count > B42_MAX_RANGES) {
        return B42_ERR_B1_DRAM_INVALID;
    }

    for (i = 0; i < b1->dram_count; ++i) {
        uint64_t end_i = 0;
        const struct b42_phys_region *d = &b1->dram[i];
        if (d->size == 0u ||
            (d->base & B42_PAGE_16K_MASK) != 0u ||
            (d->size & B42_PAGE_16K_MASK) != 0u ||
            b42_add_overflow(d->base, d->size, &end_i)) {
            return B42_ERR_B1_DRAM_INVALID;
        }
        if (d->owner == B42_OWNER_UNKNOWN) {
            return B42_ERR_B1_OWNERSHIP_UNVERIFIED;
        }
        for (j = i + 1u; j < b1->dram_count; ++j) {
            uint64_t end_j = 0;
            if (b42_add_overflow(b1->dram[j].base, b1->dram[j].size, &end_j) ||
                b42_ranges_overlap(d->base, end_i, b1->dram[j].base, end_j)) {
                return B42_ERR_B1_DRAM_INVALID;
            }
        }
    }

    if ((b1->kernel_phys_base & B42_L2_BLOCK_32M_MASK) != 0u ||
        (b1->kernel_mem_size & B42_L2_BLOCK_32M_MASK) != 0u ||
        b1->kernel_mem_size < B42_MIN_KERNEL_WINDOW_BYTES ||
        b42_add_overflow(b1->kernel_phys_base, b1->kernel_mem_size, &win_end)) {
        return B42_ERR_B1_WINDOW_ALIGNMENT_OR_SIZE;
    }
    if ((b1->kernel_lowest_pa & B42_PAGE_16K_MASK) != 0u ||
        (b1->top_of_kernel_data_pa & B42_PAGE_16K_MASK) != 0u ||
        b1->kernel_lowest_pa < b1->kernel_phys_base + B42_KERNEL_MACHO_MIN_OFFSET ||
        b1->top_of_kernel_data_pa <= b1->kernel_lowest_pa ||
        b1->top_of_kernel_data_pa > win_end) {
        return B42_ERR_B1_WINDOW_ALIGNMENT_OR_SIZE;
    }
    if (b1->kernel_window_grant == B42_GRANT_UNVERIFIED ||
        b1->kernel_window_grant == B42_GRANT_NO_HLOS) {
        return B42_ERR_B1_OWNERSHIP_UNVERIFIED;
    }

    for (i = 0; i < b1->dram_count; ++i) {
        uint64_t d_end = b1->dram[i].base + b1->dram[i].size;
        if (b1->dram[i].owner == B42_OWNER_HLOS_XNU &&
            b1->kernel_phys_base >= b1->dram[i].base &&
            win_end <= d_end) {
            contained_in_dram = 1;
            break;
        }
    }
    if (!contained_in_dram) {
        return B42_ERR_B1_WINDOW_ALIGNMENT_OR_SIZE;
    }

    for (i = 0; i < b1->reserved_count; ++i) {
        uint64_t r_end = 0;
        const struct b42_phys_region *r = &b1->reserved[i];
        if (r->size == 0u || b42_add_overflow(r->base, r->size, &r_end)) {
            return B42_ERR_B1_DRAM_INVALID;
        }
        if (r->owner == B42_OWNER_UNKNOWN) {
            return B42_ERR_B1_OWNERSHIP_UNVERIFIED;
        }
        if (r->coalesced_16k == 0u ||
            (r->base & B42_PAGE_16K_MASK) != 0u ||
            (r->size & B42_PAGE_16K_MASK) != 0u) {
            return B42_ERR_B1_SUBPAGE_SHARED_GRANULE;
        }
        /*
         * Critical HZ-B42-01 / HZ-B42-02 check:
         * arm_vm_prot_finalize calls ml_static_mfree on [physBase, segLOWEST),
         * and start.s maps the entire [physBase, physBase + memSize) with 32 MiB
         * cacheable L2 blocks. Zero overlap with ANY reserved region is allowed.
         */
        if (b42_ranges_overlap(b1->kernel_phys_base, win_end, r->base, r_end)) {
            return B42_ERR_B1_WINDOW_RESERVED_COLLISION;
        }
        if (r->dma_writable != 0u &&
            b42_ranges_overlap(b1->kernel_phys_base, win_end, r->base, r_end)) {
            return B42_ERR_DMA_SMMU_UNISOLATED;
        }
    }

    if (b1->smmu_global_policy != B42_DMA_SMMU_FAULT_UNMAPPED) {
        return B42_ERR_DMA_SMMU_UNISOLATED;
    }

    return B42_OK_HOST_MODEL_ONLY;
}

int b42_validate_lockdown_range(const struct b42_b1_handoff_snapshot *b1,
                                const struct b42_lockdown_request *req)
{
    uint64_t kva_end = 0;
    uint64_t pa_end = 0;
    uint64_t win_end;
    int rc = b42_validate_b1_handoff(b1);
    if (rc != B42_OK_HOST_MODEL_ONLY) {
        return rc;
    }
    if (!req) {
        return B42_ERR_NULL_OR_ABI;
    }

    if (req->backend_kind == B42_BACKEND_APPLE_PPL_AMCC_SPTM) {
        return B42_ERR_APPLE_HARDWARE_CLAIMED;
    }
    if (req->backend_kind == B42_BACKEND_STAGE1_NO_MONITOR) {
        return B42_ERR_SILENT_STAGE1_NO_MONITOR;
    }
    if (req->backend_kind == B42_BACKEND_SPECULATIVE_SMC) {
        return B42_ERR_SPECULATIVE_SMC_FORBIDDEN;
    }
    if (req->backend_kind != B42_BACKEND_QCOM_STAGE2_SMMU) {
        return B42_ERR_NULL_OR_ABI;
    }

    if (req->lockdown_class < B42_LOCK_STATIC_KERNEL_RX ||
        req->lockdown_class > B42_LOCK_MEDIATED_RO_ZONE) {
        return B42_ERR_NULL_OR_ABI;
    }

    if (req->size == 0u ||
        (req->kva & B42_PAGE_16K_MASK) != 0u ||
        (req->pa & B42_PAGE_16K_MASK) != 0u ||
        (req->size & B42_PAGE_16K_MASK) != 0u ||
        b42_add_overflow(req->kva, req->size, &kva_end) ||
        b42_add_overflow(req->pa, req->size, &pa_end)) {
        return B42_ERR_RANGE_BOUNDS_OR_GRANULE;
    }

    win_end = b1->kernel_phys_base + b1->kernel_mem_size;
    if (req->pa < b1->kernel_phys_base || pa_end > win_end) {
        return B42_ERR_RANGE_BOUNDS_OR_GRANULE;
    }

    if (req->req_write != 0u && req->req_exec != 0u) {
        return B42_ERR_WX_VIOLATION;
    }
    if (req->req_write != 0u) {
        /* A locked-down kernel/CS/RO-zone range cannot grant EL1 write access. */
        return B42_ERR_WX_VIOLATION;
    }
    if (req->req_exec != 0u && req->lockdown_class != B42_LOCK_STATIC_KERNEL_RX) {
        return B42_ERR_WX_VIOLATION;
    }

    if (req->is_unlock_operation != 0u) {
        /*
         * Static kernel RX/RO latches are one-way until cold SoC reset
         * (preventing runtime unlock/rollback). Dynamic CS/RO-zone unlock
         * requires an active lock with the exact matching lockdown_class.
         */
        if (req->lockdown_class == B42_LOCK_STATIC_KERNEL_RX ||
            req->lockdown_class == B42_LOCK_STATIC_KERNEL_RO) {
            return B42_ERR_UNLOCK_POLICY_VIOLATION;
        }
        if (req->already_locked == 0u ||
            req->prior_lockdown_class != req->lockdown_class) {
            return B42_ERR_UNLOCK_POLICY_VIOLATION;
        }
    } else {
        if (req->already_locked != 0u) {
            return B42_ERR_UNLOCK_POLICY_VIOLATION;
        }
    }

    if (req->physmap_page_granular_16k == 0u || req->physmap_aperture_rw_open != 0u) {
        return B42_ERR_PHYSMAP_BLOCK_ALIAS_RW;
    }
    if (req->stage1_aliases_revoked == 0u) {
        return B42_ERR_PHYSMAP_BLOCK_ALIAS_RW;
    }
    if (req->tlb_broadcast_synced == 0u || req->kva_to_pa_rechecked == 0u) {
        return B42_ERR_TLB_SYNC_MISSING;
    }
    if (req->dma_stream_excluded == 0u) {
        return B42_ERR_DMA_SMMU_UNISOLATED;
    }

    return B42_OK_HOST_MODEL_ONLY;
}

int b42_evaluate_lockdown_gate(const struct b42_b1_handoff_snapshot *b1,
                               const struct b42_lockdown_request *req,
                               int *out_can_promote_to_device)
{
    int rc;
    if (out_can_promote_to_device) {
        *out_can_promote_to_device = 0;
    }
    rc = b42_validate_lockdown_range(b1, req);
    if (rc != B42_OK_HOST_MODEL_ONLY) {
        return rc;
    }
    if (!b42_qualcomm_lockdown_backend_verified()) {
        return B42_BLOCKED_BACKEND_UNVERIFIED;
    }
    /*
     * Even under B42_HOST_MODEL_SYNTHETIC_BACKEND (host-only unit tests),
     * device promotion remains strictly false.
     */
    return B42_OK_HOST_MODEL_ONLY;
}
