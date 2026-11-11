/* SPDX-License-Identifier: MIT */
/*
 * M12-B4.2 deterministic host test suite for B42_TO_B1_CONTRACT and
 * physical PMAP/lockdown invariants (positive model + 26 negative mutations).
 */
#include "penang_pmap_lockdown_contract.h"
#include <stdio.h>
#include <string.h>

static struct b42_b1_handoff_snapshot valid_b1(void)
{
    struct b42_b1_handoff_snapshot s;
    memset(&s, 0, sizeof(s));
    s.abi_version = B42_CONTRACT_ABI_VERSION;
    s.evidence_origin = B42_EVIDENCE_PENANG_DEVICE;
    s.entry_el = 1u;
    s.tgran16_supported = 1u;
    s.icache_line_bytes = 64u;
    s.dcache_line_bytes = 64u;
    s.hardware_pac_claimed = 0u;
    s.unresolved_dynamic_count = 0u;
    s.smmu_global_policy = B42_DMA_SMMU_FAULT_UNMAPPED;

    s.dram_count = 1u;
    s.dram[0].base = 0x80000000ULL;
    s.dram[0].size = 0x80000000ULL; /* 2 GiB bank [0x80000000, 0x100000000) */
    s.dram[0].owner = B42_OWNER_HLOS_XNU;
    s.dram[0].s2_grant = B42_GRANT_HLOS_RW_NX;
    s.dram[0].coalesced_16k = 1u;

    s.reserved_count = 4u;
    /* QHEE hyp@80000000 + XBL/AOP coalesced */
    s.reserved[0].base = 0x80000000ULL;
    s.reserved[0].size = 0x00880000ULL;
    s.reserved[0].owner = B42_OWNER_QHEE_HYP_EL2;
    s.reserved[0].s2_grant = B42_GRANT_NO_HLOS;
    s.reserved[0].coalesced_16k = 1u;
    /* QSEE TZ apps */
    s.reserved[1].base = 0x80900000ULL;
    s.reserved[1].size = 0x01d00000ULL;
    s.reserved[1].owner = B42_OWNER_QSEE_TZ_EL3;
    s.reserved[1].s2_grant = B42_GRANT_NO_HLOS;
    s.reserved[1].coalesced_16k = 1u;
    /* cont_splash display framebuffer */
    s.reserved[2].base = 0x85200000ULL;
    s.reserved[2].size = 0x01100000ULL;
    s.reserved[2].owner = B42_OWNER_CONT_SPLASH_DISPLAY;
    s.reserved[2].s2_grant = B42_GRANT_HLOS_RW_NX;
    s.reserved[2].coalesced_16k = 1u;
    /* PIL + ipa_gsi@92c10000 (40K) + gpu@92c1a000 (8K) coalesced outward to 16K */
    s.reserved[3].base = 0x86400000ULL;
    s.reserved[3].size = 0x11500000ULL;
    s.reserved[3].owner = B42_OWNER_PIL_PERIPHERAL;
    s.reserved[3].s2_grant = B42_GRANT_NO_HLOS;
    s.reserved[3].coalesced_16k = 1u;

    /* 128 MiB window [0xa0000000, 0xa8000000) disjoint from all reservations */
    s.kernel_phys_base = 0xa0000000ULL;
    s.kernel_mem_size = 0x08000000ULL;
    s.kernel_lowest_pa = 0xa3004000ULL;
    s.top_of_kernel_data_pa = 0xa4800000ULL;
    s.kernel_window_grant = B42_GRANT_HLOS_RW_NX;
    return s;
}

static struct b42_lockdown_request valid_req(void)
{
    struct b42_lockdown_request r;
    memset(&r, 0, sizeof(r));
    r.lockdown_class = B42_LOCK_STATIC_KERNEL_RX;
    r.backend_kind = B42_BACKEND_QCOM_STAGE2_SMMU;
    r.kva = 0xfffffe0007004000ULL;
    r.pa = 0xa3004000ULL;
    r.size = 0x00800000ULL;
    r.req_write = 0u;
    r.req_exec = 1u;
    r.physmap_page_granular_16k = 1u;
    r.physmap_aperture_rw_open = 0u;
    r.stage1_aliases_revoked = 1u;
    r.tlb_broadcast_synced = 1u;
    r.kva_to_pa_rechecked = 1u;
    r.dma_stream_excluded = 1u;
    r.already_locked = 0u;
    r.is_unlock_operation = 0u;
    r.prior_lockdown_class = 0u;
    return r;
}

static unsigned g_total = 0;
static unsigned g_negatives = 0;

static int check_case(const char *name,
                      const struct b42_b1_handoff_snapshot *b1,
                      const struct b42_lockdown_request *req,
                      int expected_rc,
                      int is_negative)
{
    int can_promote = 99;
    int rc = b42_evaluate_lockdown_gate(b1, req, &can_promote);
    ++g_total;
    if (is_negative) {
        ++g_negatives;
    }
    if (rc != expected_rc || can_promote != 0) {
        fprintf(stderr, "FAIL %s: got rc=%d (%s) promote=%d, expected rc=%d (%s) promote=0\n",
                name, rc, b42_status_name(rc), can_promote,
                expected_rc, b42_status_name(expected_rc));
        return 1;
    }
    return 0;
}

int main(void)
{
    struct b42_b1_handoff_snapshot b1;
    struct b42_lockdown_request req;
    int expected_valid_gate = b42_qualcomm_lockdown_backend_verified()
        ? B42_OK_HOST_MODEL_ONLY
        : B42_BLOCKED_BACKEND_UNVERIFIED;
    int fail = 0;

    /* 1. Baseline valid structure: still BLOCKED unless synthetic host model flag set; never promotes */
    b1 = valid_b1();
    req = valid_req();
    fail |= check_case("baseline_gate_decision", &b1, &req, expected_valid_gate,
                       expected_valid_gate != B42_OK_HOST_MODEL_ONLY);

    /* 2. Null / ABI version mismatch */
    fail |= check_case("null_b1", NULL, &req, B42_ERR_NULL_OR_ABI, 1);
    b1 = valid_b1(); b1.abi_version = 99u;
    fail |= check_case("bad_abi", &b1, &req, B42_ERR_NULL_OR_ABI, 1);

    /* 3. QEMU_VIRT / Host / Apple evidence rejected as physical Penang proof */
    b1 = valid_b1(); b1.evidence_origin = B42_EVIDENCE_QEMU_VIRT;
    fail |= check_case("qemu_virt_evidence_rejected", &b1, &req, B42_ERR_EVIDENCE_NOT_PENANG_DEVICE, 1);
    b1 = valid_b1(); b1.evidence_origin = B42_EVIDENCE_HOST_SYNTHETIC;
    fail |= check_case("host_synthetic_evidence_rejected", &b1, &req, B42_ERR_EVIDENCE_NOT_PENANG_DEVICE, 1);

    /* 4. Entry EL / TGran16 / CTR_EL0 / PAC checks */
    b1 = valid_b1(); b1.entry_el = 2u; /* M11-D3a-1 observed EL1, not EL2 */
    fail |= check_case("wrong_entry_el2_claim", &b1, &req, B42_ERR_ENTRY_EL_OR_CPU_CAP, 1);
    b1 = valid_b1(); b1.tgran16_supported = 0u;
    fail |= check_case("missing_tgran16", &b1, &req, B42_ERR_ENTRY_EL_OR_CPU_CAP, 1);
    b1 = valid_b1(); b1.dcache_line_bytes = 128u;
    fail |= check_case("wrong_cache_line_128b", &b1, &req, B42_ERR_ENTRY_EL_OR_CPU_CAP, 1);
    b1 = valid_b1(); b1.hardware_pac_claimed = 1u;
    fail |= check_case("apple_pac_claimed_on_kryo460", &b1, &req, B42_ERR_APPLE_HARDWARE_CLAIMED, 1);

    /* 5. Forbidden backends: Apple PPL/AMCC/SPTM, silent NO_MONITOR Stage-1, speculative SMC */
    b1 = valid_b1(); req = valid_req(); req.backend_kind = B42_BACKEND_APPLE_PPL_AMCC_SPTM;
    fail |= check_case("apple_ppl_backend_rejected", &b1, &req, B42_ERR_APPLE_HARDWARE_CLAIMED, 1);
    req = valid_req(); req.backend_kind = B42_BACKEND_STAGE1_NO_MONITOR;
    fail |= check_case("silent_no_monitor_fallback_rejected", &b1, &req, B42_ERR_SILENT_STAGE1_NO_MONITOR, 1);
    req = valid_req(); req.backend_kind = B42_BACKEND_SPECULATIVE_SMC;
    fail |= check_case("speculative_smc_rejected", &b1, &req, B42_ERR_SPECULATIVE_SMC_FORBIDDEN, 1);
    req = valid_req();

    /* 6. B1 handoff hazards: zero DRAM, unresolved dynamic nodes, sub-16K uncoalesced carve-out */
    b1 = valid_b1(); b1.dram[0].size = 0u;
    fail |= check_case("base_dtb_zero_dram_rejected", &b1, &req, B42_ERR_B1_DRAM_INVALID, 1);
    b1 = valid_b1(); b1.unresolved_dynamic_count = 8u;
    fail |= check_case("unresolved_8_dynamic_resv_rejected", &b1, &req, B42_ERR_B1_UNRESOLVED_DYNAMIC_RESV, 1);
    b1 = valid_b1();
    b1.reserved[3].base = 0x92c1a000ULL; /* gpu_region 8 KiB unaligned to 16 KiB! */
    b1.reserved[3].size = 0x00002000ULL;
    b1.reserved[3].coalesced_16k = 1u;
    fail |= check_case("subpage_8k_gpu_uncoalesced_rejected", &b1, &req, B42_ERR_B1_SUBPAGE_SHARED_GRANULE, 1);

    /* 7. Kernel 32 MiB window alignment & ml_static_mfree [physBase, segLOWEST) collision */
    b1 = valid_b1(); b1.kernel_phys_base = 0xa1000000ULL; /* 16 MiB, not 32 MiB aligned */
    fail |= check_case("window_not_32m_aligned", &b1, &req, B42_ERR_B1_WINDOW_ALIGNMENT_OR_SIZE, 1);
    b1 = valid_b1();
    /* Place a dynamic reservation at 0xa0100000 inside [physBase, segLOWEST_PA) freed by ml_static_mfree! */
    b1.reserved[4].base = 0xa0100000ULL;
    b1.reserved[4].size = 0x00100000ULL;
    b1.reserved[4].owner = B42_OWNER_PIL_PERIPHERAL;
    b1.reserved[4].s2_grant = B42_GRANT_NO_HLOS;
    b1.reserved[4].coalesced_16k = 1u;
    b1.reserved_count = 5u;
    fail |= check_case("ml_static_mfree_prefix_collision", &b1, &req, B42_ERR_B1_WINDOW_RESERVED_COLLISION, 1);

    /* 8. Unverified ownership & SMMU bypass DMA hazards */
    b1 = valid_b1(); b1.kernel_window_grant = B42_GRANT_UNVERIFIED;
    fail |= check_case("unverified_window_grant", &b1, &req, B42_ERR_B1_OWNERSHIP_UNVERIFIED, 1);
    b1 = valid_b1(); b1.smmu_global_policy = B42_DMA_BYPASS_ACTIVE;
    fail |= check_case("smmu_bypass_active_rejected", &b1, &req, B42_ERR_DMA_SMMU_UNISOLATED, 1);

    /* 9. Range alignment (4 KiB vs 16 KiB), uint64 overflow, W^X violations */
    b1 = valid_b1(); req = valid_req(); req.kva += 0x1000ULL; /* 4 KiB user page, not 16 KiB kernel page */
    fail |= check_case("4k_unaligned_kva_rejected", &b1, &req, B42_ERR_RANGE_BOUNDS_OR_GRANULE, 1);
    req = valid_req(); req.size = UINT64_MAX - req.pa + B42_PAGE_16K;
    fail |= check_case("pa_size_overflow_rejected", &b1, &req, B42_ERR_RANGE_BOUNDS_OR_GRANULE, 1);
    req = valid_req(); req.req_write = 1u; req.req_exec = 1u;
    fail |= check_case("wx_simultaneous_rejected", &b1, &req, B42_ERR_WX_VIOLATION, 1);
    req = valid_req(); req.lockdown_class = B42_LOCK_IMMUTABLE_CS_DATA; req.req_exec = 1u;
    fail |= check_case("exec_on_cs_data_rejected", &b1, &req, B42_ERR_WX_VIOLATION, 1);

    /* 10. Physmap 32 MiB block alias, RW phystokv aperture (!XNU_MONITOR zalloc_ro), TLB/ABA, DMA */
    req = valid_req(); req.physmap_page_granular_16k = 0u;
    fail |= check_case("physmap_32m_block_unsplit_rejected", &b1, &req, B42_ERR_PHYSMAP_BLOCK_ALIAS_RW, 1);
    req = valid_req(); req.lockdown_class = B42_LOCK_MEDIATED_RO_ZONE; req.req_exec = 0u; req.physmap_aperture_rw_open = 1u;
    fail |= check_case("open_rw_phystokv_ro_zone_rejected", &b1, &req, B42_ERR_PHYSMAP_BLOCK_ALIAS_RW, 1);
    req = valid_req(); req.tlb_broadcast_synced = 0u;
    fail |= check_case("missing_tlb_broadcast_sync_rejected", &b1, &req, B42_ERR_TLB_SYNC_MISSING, 1);
    req = valid_req(); req.kva_to_pa_rechecked = 0u;
    fail |= check_case("missing_aba_kvtophys_recheck_rejected", &b1, &req, B42_ERR_TLB_SYNC_MISSING, 1);
    req = valid_req(); req.dma_stream_excluded = 0u;
    fail |= check_case("per_range_dma_unexcluded_rejected", &b1, &req, B42_ERR_DMA_SMMU_UNISOLATED, 1);

    /* 11. Unlock / rollback / double-lock / mismatch policies */
    req = valid_req(); req.already_locked = 1u;
    fail |= check_case("double_lock_rejected", &b1, &req, B42_ERR_UNLOCK_POLICY_VIOLATION, 1);
    req = valid_req(); req.is_unlock_operation = 1u; req.already_locked = 1u;
    req.prior_lockdown_class = B42_LOCK_STATIC_KERNEL_RX;
    fail |= check_case("static_kernel_rx_unlock_rollback_rejected", &b1, &req, B42_ERR_UNLOCK_POLICY_VIOLATION, 1);
    req = valid_req(); req.lockdown_class = B42_LOCK_IMMUTABLE_CS_DATA; req.req_exec = 0u;
    req.is_unlock_operation = 1u; req.already_locked = 0u;
    fail |= check_case("unlock_without_prior_lock_rejected", &b1, &req, B42_ERR_UNLOCK_POLICY_VIOLATION, 1);
    req = valid_req(); req.lockdown_class = B42_LOCK_IMMUTABLE_CS_DATA; req.req_exec = 0u;
    req.is_unlock_operation = 1u; req.already_locked = 1u; req.prior_lockdown_class = B42_LOCK_MEDIATED_RO_ZONE;
    fail |= check_case("unlock_reason_mismatch_rejected", &b1, &req, B42_ERR_UNLOCK_POLICY_VIOLATION, 1);

    if (fail) {
        return 1;
    }
    printf("B42_PMAP_LOCKDOWN_CONTRACT_TESTS_PASS total=%u negatives=%u backend_verified=%d\n",
           g_total, g_negatives, b42_qualcomm_lockdown_backend_verified());
    return 0;
}
