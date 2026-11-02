/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * drivers/penang/src/penang_bsp_validate.c
 *
 * Host-only validation implementation for Motorola Moto G53 5G (penang,
 * SM4350-AC / holi) platform descriptors and resource bounds.
 *
 * Grounded in:
 * - manifests/penang-bsp-offline.json (vendor_boot base FDT SHA-256:
 *   7919e7a72ba1244ff06c545384456a8a7779857bc54d3577da72136b48ec9e67)
 * - manifests/penang-cpu-probe.json (read-only CPU MIDR/HWCAP measurement)
 *
 * No physical memory accesses, register reads, or register writes occur here.
 */

#include "penang_bsp_contract.h"

#include <string.h>

static bool is_nonempty_str(const char *s)
{
    return (s != NULL) && (s[0] != '\0');
}

static bool str_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static bool is_power_of_two_u64(uint64_t v)
{
    return (v != 0ULL) && ((v & (v - 1ULL)) == 0ULL);
}

static bool is_verified_offline_fdt_mmio(uint64_t phys_base, uint64_t size)
{
    if (phys_base == PENANG_GIC_DIST_BASE && size == PENANG_GIC_DIST_SIZE) {
        return true;
    }
    if (phys_base == PENANG_GIC_REDIST_BASE && size == PENANG_GIC_REDIST_SIZE) {
        return true;
    }
    return false;
}

static bool ranges_overlap_u64(uint64_t base_a, uint64_t size_a,
                               uint64_t base_b, uint64_t size_b)
{
    /* Reject undecidable invalid geometry conservatively if reused by new callers. */
    if (size_a == 0ULL || size_b == 0ULL) {
        return false;
    }
    if (UINT64_MAX - base_a < size_a || UINT64_MAX - base_b < size_b) {
        return true; /* Fail closed: invalid wraparound must never imply no overlap. */
    }
    uint64_t end_a = base_a + size_a;
    uint64_t end_b = base_b + size_b;
    uint64_t max_base = (base_a > base_b) ? base_a : base_b;
    uint64_t min_end = (end_a < end_b) ? end_a : end_b;
    return max_base < min_end;
}

const char *penang_status_str(penang_status_t status)
{
    switch (status) {
    case PENANG_STATUS_OK:
        return "PENANG_STATUS_OK";
    case PENANG_STATUS_ERR_NULL_ARG:
        return "PENANG_STATUS_ERR_NULL_ARG";
    case PENANG_STATUS_ERR_CONTRACT_VERSION:
        return "PENANG_STATUS_ERR_CONTRACT_VERSION";
    case PENANG_STATUS_ERR_PROVENANCE_MISMATCH:
        return "PENANG_STATUS_ERR_PROVENANCE_MISMATCH";
    case PENANG_STATUS_ERR_MMIO_ZERO_OR_EMPTY:
        return "PENANG_STATUS_ERR_MMIO_ZERO_OR_EMPTY";
    case PENANG_STATUS_ERR_MMIO_OVERFLOW:
        return "PENANG_STATUS_ERR_MMIO_OVERFLOW";
    case PENANG_STATUS_ERR_MMIO_ALIGNMENT:
        return "PENANG_STATUS_ERR_MMIO_ALIGNMENT";
    case PENANG_STATUS_ERR_MMIO_OVERLAP:
        return "PENANG_STATUS_ERR_MMIO_OVERLAP";
    case PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR:
        return "PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR";
    case PENANG_STATUS_ERR_MMIO_WRITE_FORBIDDEN:
        return "PENANG_STATUS_ERR_MMIO_WRITE_FORBIDDEN";
    case PENANG_STATUS_ERR_CPU_COUNT_INVALID:
        return "PENANG_STATUS_ERR_CPU_COUNT_INVALID";
    case PENANG_STATUS_ERR_CPU_MPIDR_INVALID:
        return "PENANG_STATUS_ERR_CPU_MPIDR_INVALID";
    case PENANG_STATUS_ERR_CPU_MIDR_INVALID:
        return "PENANG_STATUS_ERR_CPU_MIDR_INVALID";
    case PENANG_STATUS_ERR_CPU_CAPACITY_INVALID:
        return "PENANG_STATUS_ERR_CPU_CAPACITY_INVALID";
    case PENANG_STATUS_ERR_CPU_MASK_MISMATCH:
        return "PENANG_STATUS_ERR_CPU_MASK_MISMATCH";
    case PENANG_STATUS_ERR_GIC_COMPAT_INVALID:
        return "PENANG_STATUS_ERR_GIC_COMPAT_INVALID";
    case PENANG_STATUS_ERR_GIC_BOUNDS_INVALID:
        return "PENANG_STATUS_ERR_GIC_BOUNDS_INVALID";
    case PENANG_STATUS_ERR_IRQ_SPEC_INVALID:
        return "PENANG_STATUS_ERR_IRQ_SPEC_INVALID";
    case PENANG_STATUS_ERR_PSCI_METHOD_ASSUMED:
        return "PENANG_STATUS_ERR_PSCI_METHOD_ASSUMED";
    case PENANG_STATUS_ERR_CLOCK_DESC_INVALID:
        return "PENANG_STATUS_ERR_CLOCK_DESC_INVALID";
    case PENANG_STATUS_ERR_DISPLAY_DESC_INVALID:
        return "PENANG_STATUS_ERR_DISPLAY_DESC_INVALID";
    case PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED:
        return "PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED";
    case PENANG_STATUS_ERR_INTEGRATION_UNPROVEN:
        return "PENANG_STATUS_ERR_INTEGRATION_UNPROVEN";
    default:
        return "PENANG_STATUS_ERR_UNRECOGNIZED";
    }
}

const char *penang_evidence_status_str(penang_evidence_status_t evidence)
{
    switch (evidence) {
    case PENANG_EVIDENCE_UNKNOWN:
        return "UNKNOWN";
    case PENANG_EVIDENCE_OFFLINE_FDT_BASE:
        return "OFFLINE_FDT_BASE";
    case PENANG_EVIDENCE_MEASURED_READONLY:
        return "MEASURED_READONLY";
    case PENANG_EVIDENCE_ACTIVE_FDT_VERIFIED:
        return "ACTIVE_FDT_VERIFIED";
    default:
        return "INVALID_EVIDENCE";
    }
}

const char *penang_integration_status_str(penang_integration_status_t status)
{
    switch (status) {
    case PENANG_INTEGRATION_UNKNOWN:
        return "UNKNOWN";
    case PENANG_INTEGRATION_HOST_CONTRACT_ONLY:
        return "HOST_CONTRACT_ONLY";
    default:
        return "INVALID_INTEGRATION_STATUS";
    }
}

penang_status_t penang_populate_offline_baseline(penang_platform_descriptor_t *out_desc)
{
    uint32_t i;

    if (out_desc == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    memset(out_desc, 0, sizeof(*out_desc));

    out_desc->contract_version = PENANG_BSP_CONTRACT_ABI_VERSION;
    out_desc->board = PENANG_BOARD_NAME;
    out_desc->codename = PENANG_CODENAME;
    out_desc->source_type = PENANG_SOURCE_TYPE;
    out_desc->dtb_sha256 = PENANG_STOCK_DTB_SHA256;
    out_desc->dtb_bytes = PENANG_STOCK_DTB_BYTES;
    out_desc->parsed_candidates_sha256 = PENANG_PARSED_CANDIDATES_SHA256;
    out_desc->not_a_hardware_register_read = true;
    out_desc->device_test_status = PENANG_DEVICE_TEST_STATUS;

    out_desc->cpu.count = PENANG_CPU_COUNT;
    out_desc->cpu.little_cores_mask = PENANG_LITTLE_CORES_MASK;
    for (i = 0; i < PENANG_CPU_COUNT; ++i) {
        bool is_little = (i < PENANG_LITTLE_CORES_COUNT);
        out_desc->cpu.cores[i].cpu_index = i;
        out_desc->cpu.cores[i].mpidr_aff1 = i;
        out_desc->cpu.cores[i].capacity_dmips_mhz = is_little
            ? PENANG_CPU_CAPACITY_SILVER_DMIPS
            : PENANG_CPU_CAPACITY_GOLD_DMIPS;
        out_desc->cpu.cores[i].midr_el1 = is_little
            ? PENANG_MIDR_KRYO4XX_SILVER
            : PENANG_MIDR_KRYO4XX_GOLD;
        out_desc->cpu.cores[i].is_little_core = is_little;
    }

    out_desc->gic.compatible = PENANG_GIC_COMPATIBLE;
    out_desc->gic.distributor.name = "gic-distributor";
    out_desc->gic.distributor.phys_base = PENANG_GIC_DIST_BASE;
    out_desc->gic.distributor.size = PENANG_GIC_DIST_SIZE;
    out_desc->gic.distributor.min_alignment = PENANG_MIN_PAGE_ALIGNMENT;
    out_desc->gic.distributor.flags = PENANG_MMIO_FLAG_NO_REAL_WRITES;
    out_desc->gic.distributor.evidence = PENANG_EVIDENCE_OFFLINE_FDT_BASE;

    out_desc->gic.redistributor.name = "gic-redistributor";
    out_desc->gic.redistributor.phys_base = PENANG_GIC_REDIST_BASE;
    out_desc->gic.redistributor.size = PENANG_GIC_REDIST_SIZE;
    out_desc->gic.redistributor.min_alignment = PENANG_MIN_PAGE_ALIGNMENT;
    out_desc->gic.redistributor.flags = PENANG_MMIO_FLAG_NO_REAL_WRITES;
    out_desc->gic.redistributor.evidence = PENANG_EVIDENCE_OFFLINE_FDT_BASE;

    out_desc->psci.compatible = PENANG_PSCI_COMPATIBLE;
    out_desc->psci.method = PENANG_PSCI_BASE_FDT_METHOD;
    out_desc->psci.evidence = PENANG_EVIDENCE_OFFLINE_FDT_BASE;

    /* All unknown features in manifests/penang-bsp-offline.json remain false (unverified). */
    out_desc->unknown_gates.active_dt_overlays_verified = false;
    out_desc->unknown_gates.el2_accessibility_verified = false;
    out_desc->unknown_gates.feat_nv2_verified = false;
    out_desc->unknown_gates.pac_verified = false;
    out_desc->unknown_gates.page_granule_16k_verified = false;
    out_desc->unknown_gates.gpu_firmware_verified = false;
    out_desc->unknown_gates.display_panel_timing_verified = false;
    out_desc->unknown_gates.ram_available_to_loader_verified = false;

    out_desc->extra_mmio_regions = NULL;
    out_desc->extra_mmio_count = 0u;

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_mmio_region(const penang_mmio_desc_t *region)
{
    if (region == NULL || !is_nonempty_str(region->name)) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if ((region->flags & PENANG_MMIO_FLAG_WRITE_ATTEMPTED) != 0u) {
        return PENANG_STATUS_ERR_MMIO_WRITE_FORBIDDEN;
    }
    if ((region->flags & PENANG_MMIO_FLAG_SPECULATIVE) != 0u) {
        return PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR;
    }
    if ((region->flags & PENANG_MMIO_FLAG_NO_REAL_WRITES) == 0u ||
        (region->flags & ~PENANG_MMIO_FLAG_ALLOWED_MASK) != 0u) {
        return PENANG_STATUS_ERR_MMIO_WRITE_FORBIDDEN;
    }

    if (region->phys_base == 0ULL || region->size == 0ULL) {
        return PENANG_STATUS_ERR_MMIO_ZERO_OR_EMPTY;
    }

    if (UINT64_MAX - region->phys_base < region->size) {
        return PENANG_STATUS_ERR_MMIO_OVERFLOW;
    }

    if ((region->phys_base + region->size) > PENANG_PHYS_ADDR_LIMIT_48BIT) {
        return PENANG_STATUS_ERR_MMIO_OVERFLOW;
    }

    if (region->min_alignment < PENANG_MIN_PAGE_ALIGNMENT ||
        !is_power_of_two_u64(region->min_alignment)) {
        return PENANG_STATUS_ERR_MMIO_ALIGNMENT;
    }

    if ((region->phys_base & (region->min_alignment - 1ULL)) != 0ULL ||
        (region->size & (PENANG_MIN_PAGE_ALIGNMENT - 1ULL)) != 0ULL) {
        return PENANG_STATUS_ERR_MMIO_ALIGNMENT;
    }

    switch (region->evidence) {
    case PENANG_EVIDENCE_OFFLINE_FDT_BASE:
        if (!is_verified_offline_fdt_mmio(region->phys_base, region->size)) {
            return PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR;
        }
        break;
    case PENANG_EVIDENCE_ACTIVE_FDT_VERIFIED:
        /* Active DT overlays on penang are UNKNOWN; claiming active FDT verification fails closed. */
        return PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED;
    case PENANG_EVIDENCE_UNKNOWN:
    case PENANG_EVIDENCE_MEASURED_READONLY:
        /* Non-FDT or unverified MMIO regions are rejected as speculative in offline BSP contract. */
        return PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR;
    default:
        return PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_mmio_set(const penang_mmio_desc_t *regions, size_t count)
{
    size_t i;
    size_t j;

    if (count == 0u) {
        return PENANG_STATUS_OK;
    }
    if (regions == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    for (i = 0; i < count; ++i) {
        penang_status_t st = penang_validate_mmio_region(&regions[i]);
        if (st != PENANG_STATUS_OK) {
            return st;
        }
    }

    for (i = 0; i < count; ++i) {
        for (j = i + 1u; j < count; ++j) {
            if (ranges_overlap_u64(regions[i].phys_base, regions[i].size,
                                   regions[j].phys_base, regions[j].size)) {
                return PENANG_STATUS_ERR_MMIO_OVERLAP;
            }
        }
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_cpu_topology(const penang_cpu_topology_desc_t *cpu)
{
    uint32_t i;
    uint32_t seen_mpidr_mask = 0u;
    uint32_t computed_little_mask = 0u;

    if (cpu == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (cpu->count != PENANG_CPU_COUNT) {
        return PENANG_STATUS_ERR_CPU_COUNT_INVALID;
    }

    if (cpu->little_cores_mask != PENANG_LITTLE_CORES_MASK) {
        return PENANG_STATUS_ERR_CPU_MASK_MISMATCH;
    }

    for (i = 0; i < PENANG_CPU_COUNT; ++i) {
        const penang_cpu_core_desc_t *core = &cpu->cores[i];
        bool expected_little = (i < PENANG_LITTLE_CORES_COUNT);
        uint32_t expected_cap = expected_little
            ? PENANG_CPU_CAPACITY_SILVER_DMIPS
            : PENANG_CPU_CAPACITY_GOLD_DMIPS;
        uint32_t expected_midr = expected_little
            ? PENANG_MIDR_KRYO4XX_SILVER
            : PENANG_MIDR_KRYO4XX_GOLD;

        if (core->cpu_index != i || core->mpidr_aff1 != i ||
            core->mpidr_aff1 >= PENANG_CPU_COUNT) {
            return PENANG_STATUS_ERR_CPU_MPIDR_INVALID;
        }

        if ((seen_mpidr_mask & (1u << core->mpidr_aff1)) != 0u) {
            return PENANG_STATUS_ERR_CPU_MPIDR_INVALID;
        }
        seen_mpidr_mask |= (1u << core->mpidr_aff1);

        if (core->capacity_dmips_mhz != expected_cap) {
            return PENANG_STATUS_ERR_CPU_CAPACITY_INVALID;
        }

        if (core->midr_el1 != expected_midr) {
            return PENANG_STATUS_ERR_CPU_MIDR_INVALID;
        }

        if (core->is_little_core != expected_little) {
            return PENANG_STATUS_ERR_CPU_MASK_MISMATCH;
        }

        if (core->is_little_core) {
            computed_little_mask |= (1u << i);
        }
    }

    if (computed_little_mask != cpu->little_cores_mask) {
        return PENANG_STATUS_ERR_CPU_MASK_MISMATCH;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_gic_descriptor(const penang_gic_desc_t *gic, uint32_t cpu_count)
{
    penang_status_t st;
    uint64_t required_redist_size;

    if (gic == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (!str_eq(gic->compatible, PENANG_GIC_COMPATIBLE)) {
        return PENANG_STATUS_ERR_GIC_COMPAT_INVALID;
    }

    if (cpu_count == 0u || cpu_count > PENANG_CPU_COUNT) {
        return PENANG_STATUS_ERR_CPU_COUNT_INVALID;
    }

    /* Check GICv3 distributor and redistributor exact bounds before generic MMIO check
     * so out-of-bounds redistributor size reports PENANG_STATUS_ERR_GIC_BOUNDS_INVALID
     * when base matches. */
    required_redist_size = (uint64_t)cpu_count * PENANG_GIC_REDIST_FRAME_SIZE;
    if (gic->distributor.phys_base == PENANG_GIC_DIST_BASE &&
        gic->distributor.size != PENANG_GIC_DIST_SIZE &&
        gic->distributor.size != 0ULL) {
        return PENANG_STATUS_ERR_GIC_BOUNDS_INVALID;
    }
    if (gic->redistributor.phys_base == PENANG_GIC_REDIST_BASE &&
        gic->redistributor.size != PENANG_GIC_REDIST_SIZE &&
        gic->redistributor.size != 0ULL) {
        return PENANG_STATUS_ERR_GIC_BOUNDS_INVALID;
    }

    st = penang_validate_mmio_region(&gic->distributor);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    st = penang_validate_mmio_region(&gic->redistributor);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    if (gic->distributor.phys_base != PENANG_GIC_DIST_BASE ||
        gic->distributor.size != PENANG_GIC_DIST_SIZE) {
        return PENANG_STATUS_ERR_GIC_BOUNDS_INVALID;
    }

    if (gic->redistributor.phys_base != PENANG_GIC_REDIST_BASE ||
        gic->redistributor.size != PENANG_GIC_REDIST_SIZE ||
        gic->redistributor.size < required_redist_size) {
        return PENANG_STATUS_ERR_GIC_BOUNDS_INVALID;
    }

    if (ranges_overlap_u64(gic->distributor.phys_base, gic->distributor.size,
                           gic->redistributor.phys_base, gic->redistributor.size)) {
        return PENANG_STATUS_ERR_MMIO_OVERLAP;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_irq_specifier(const penang_irq_desc_t *irq)
{
    uint32_t trigger;

    if (irq == NULL || !is_nonempty_str(irq->label)) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    trigger = irq->flags & PENANG_IRQ_FLAG_TRIGGER_MASK;
    if (trigger == 0u || (irq->flags & ~PENANG_IRQ_FLAG_TRIGGER_MASK) != 0u) {
        return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
    }

    if (irq->irq_type == PENANG_GIC_IRQ_TYPE_SPI) {
        if (irq->irq_number > PENANG_GIC_SPI_MAX_NUMBER) {
            return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
        }
        /* GICv3 SPIs only support level-high or edge-rising trigger types. */
        if (trigger != PENANG_IRQ_FLAG_LEVEL_HIGH &&
            trigger != PENANG_IRQ_FLAG_EDGE_RISING) {
            return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
        }
        return PENANG_STATUS_OK;
    }

    if (irq->irq_type == PENANG_GIC_IRQ_TYPE_PPI) {
        if (irq->irq_number > PENANG_GIC_PPI_MAX_NUMBER) {
            return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
        }
        /* The stock arch timer has LEVEL_LOW (0xff08 including legacy
         * affinity bits). This API validates normalized 4-bit trigger flags;
         * decoding PPI affinity from the running FDT is NOT implemented. */
        if (trigger != PENANG_IRQ_FLAG_EDGE_RISING &&
            trigger != PENANG_IRQ_FLAG_LEVEL_HIGH &&
            trigger != PENANG_IRQ_FLAG_LEVEL_LOW) {
            return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
        }
        return PENANG_STATUS_OK;
    }

    return PENANG_STATUS_ERR_IRQ_SPEC_INVALID;
}

penang_status_t penang_validate_psci_descriptor(const penang_psci_desc_t *psci)
{
    if (psci == NULL || !is_nonempty_str(psci->compatible) || !is_nonempty_str(psci->method)) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (!str_eq(psci->compatible, PENANG_PSCI_COMPATIBLE)) {
        return PENANG_STATUS_ERR_PROVENANCE_MISMATCH;
    }

    if (psci->evidence != PENANG_EVIDENCE_OFFLINE_FDT_BASE) {
        return PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED;
    }

    /* 'smc' is known in the base tree, not yet proven on the running phone. */
    if (!str_eq(psci->method, PENANG_PSCI_BASE_FDT_METHOD)) {
        return PENANG_STATUS_ERR_PSCI_METHOD_ASSUMED;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_clock_descriptor(const penang_clock_desc_t *clk)
{
    if (clk == NULL ||
        !is_nonempty_str(clk->controller_compat) ||
        !is_nonempty_str(clk->clock_name)) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (clk->min_hz == 0ULL ||
        clk->max_hz < clk->min_hz ||
        clk->requested_hz < clk->min_hz ||
        clk->requested_hz > clk->max_hz) {
        return PENANG_STATUS_ERR_CLOCK_DESC_INVALID;
    }

    /*
     * manifests/penang-bsp-offline.json does not extract GCC/RPMh clock frequency
     * tables or clock MMIO bases. Any clock descriptor claiming verified offline
     * or active FDT evidence on penang is rejected fail-closed.
     */
    if (clk->evidence != PENANG_EVIDENCE_UNKNOWN) {
        return PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_display_descriptor(const penang_display_desc_t *disp)
{
    uint64_t bytes_per_pixel;
    uint64_t min_stride;
    uint64_t min_fb_size;

    if (disp == NULL || !is_nonempty_str(disp->panel_compat)) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (disp->width_px == 0u || disp->height_px == 0u ||
        disp->refresh_hz < 30u || disp->refresh_hz > 240u) {
        return PENANG_STATUS_ERR_DISPLAY_DESC_INVALID;
    }

    if (disp->bits_per_pixel != 16u &&
        disp->bits_per_pixel != 24u &&
        disp->bits_per_pixel != 32u) {
        return PENANG_STATUS_ERR_DISPLAY_DESC_INVALID;
    }

    bytes_per_pixel = (uint64_t)(disp->bits_per_pixel / 8u);
    min_stride = (uint64_t)disp->width_px * bytes_per_pixel;
    if ((uint64_t)disp->stride_bytes < min_stride) {
        return PENANG_STATUS_ERR_DISPLAY_DESC_INVALID;
    }

    if (UINT64_MAX / (uint64_t)disp->height_px < (uint64_t)disp->stride_bytes) {
        return PENANG_STATUS_ERR_DISPLAY_DESC_INVALID;
    }

    min_fb_size = (uint64_t)disp->stride_bytes * (uint64_t)disp->height_px;
    if (disp->framebuffer_size < min_fb_size ||
        (disp->framebuffer_size & (PENANG_MIN_PAGE_ALIGNMENT - 1ULL)) != 0ULL) {
        return PENANG_STATUS_ERR_DISPLAY_DESC_INVALID;
    }

    /*
     * display_panel_timing is explicitly listed in manifests/penang-bsp-offline.json
     * "unknown" array. Claiming non-UNKNOWN timing evidence fails closed.
     */
    if (disp->timing_evidence != PENANG_EVIDENCE_UNKNOWN) {
        return PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_unknown_gates(const penang_unknown_gates_t *gates)
{
    if (gates == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (gates->active_dt_overlays_verified ||
        gates->el2_accessibility_verified ||
        gates->feat_nv2_verified ||
        gates->pac_verified ||
        gates->page_granule_16k_verified ||
        gates->gpu_firmware_verified ||
        gates->display_panel_timing_verified ||
        gates->ram_available_to_loader_verified) {
        return PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED;
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_validate_platform_descriptor(const penang_platform_descriptor_t *desc)
{
    penang_status_t st;
    size_t i;

    if (desc == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (desc->contract_version != PENANG_BSP_CONTRACT_ABI_VERSION) {
        return PENANG_STATUS_ERR_CONTRACT_VERSION;
    }

    if (!str_eq(desc->board, PENANG_BOARD_NAME) ||
        !str_eq(desc->codename, PENANG_CODENAME) ||
        !str_eq(desc->source_type, PENANG_SOURCE_TYPE) ||
        !str_eq(desc->dtb_sha256, PENANG_STOCK_DTB_SHA256) ||
        desc->dtb_bytes != PENANG_STOCK_DTB_BYTES ||
        !str_eq(desc->parsed_candidates_sha256, PENANG_PARSED_CANDIDATES_SHA256) ||
        !desc->not_a_hardware_register_read ||
        !str_eq(desc->device_test_status, PENANG_DEVICE_TEST_STATUS)) {
        return PENANG_STATUS_ERR_PROVENANCE_MISMATCH;
    }

    st = penang_validate_cpu_topology(&desc->cpu);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    st = penang_validate_gic_descriptor(&desc->gic, desc->cpu.count);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    st = penang_validate_psci_descriptor(&desc->psci);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    st = penang_validate_unknown_gates(&desc->unknown_gates);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    if (desc->extra_mmio_count > 0u && desc->extra_mmio_regions == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    st = penang_validate_mmio_set(desc->extra_mmio_regions, desc->extra_mmio_count);
    if (st != PENANG_STATUS_OK) {
        return st;
    }

    for (i = 0; i < desc->extra_mmio_count; ++i) {
        const penang_mmio_desc_t *ext = &desc->extra_mmio_regions[i];
        if (ranges_overlap_u64(ext->phys_base, ext->size,
                               desc->gic.distributor.phys_base,
                               desc->gic.distributor.size) ||
            ranges_overlap_u64(ext->phys_base, ext->size,
                               desc->gic.redistributor.phys_base,
                               desc->gic.redistributor.size)) {
            return PENANG_STATUS_ERR_MMIO_OVERLAP;
        }
    }

    return PENANG_STATUS_OK;
}

penang_status_t penang_query_integration_report(penang_integration_report_t *out_report)
{
    if (out_report == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    out_report->overall_status = PENANG_INTEGRATION_UNKNOWN;
    out_report->xnu_11417_platform_expert = PENANG_INTEGRATION_UNKNOWN;
    out_report->iokit_service_matching = PENANG_INTEGRATION_UNKNOWN;
    out_report->adreno_gmu_gpu = PENANG_INTEGRATION_UNKNOWN;
    out_report->display_dpu_mdp = PENANG_INTEGRATION_UNKNOWN;
    out_report->smmu_dma_isolation = PENANG_INTEGRATION_UNKNOWN;

    out_report->target_xnu_tag = "xnu-11417.140.69";
    out_report->target_xnu_commit = "43a90889846e00bfb5cf1d255cdc0a701a1e05a4";
    out_report->target_apple_build = "22H352 (iPadOS/iOS 18.7.8 xnu-11417.140.69.706.9)";
    out_report->reference_xnu_tag = "xnu-8796.121.2";
    out_report->reference_xnu_commit = "c54f35ca767986246321eb901baf8f5ff7923f6a";
    out_report->summary_reason =
        "UNKNOWN: Host-only BSP descriptor contract only; XNU 11417 ARMv8.2 machine config, "
        "IOKit PlatformExpert/GICv3/PSCI glue, Adreno 619 GMU/ZAP, MDSS/DPU display timing, "
        "SMMUv2 DMA isolation, and 16 KiB translation granule are unbuilt or unverified.";

    return PENANG_STATUS_OK;
}

penang_status_t penang_require_xnu_iokit_ready(const penang_integration_report_t *report)
{
    if (report == NULL) {
        return PENANG_STATUS_ERR_NULL_ARG;
    }

    if (report->overall_status != PENANG_INTEGRATION_UNKNOWN ||
        report->xnu_11417_platform_expert != PENANG_INTEGRATION_UNKNOWN ||
        report->iokit_service_matching != PENANG_INTEGRATION_UNKNOWN ||
        report->adreno_gmu_gpu != PENANG_INTEGRATION_UNKNOWN ||
        report->display_dpu_mdp != PENANG_INTEGRATION_UNKNOWN ||
        report->smmu_dma_isolation != PENANG_INTEGRATION_UNKNOWN) {
        /* Emulated or unproven kernel integration must never claim readiness. */
        return PENANG_STATUS_ERR_INTEGRATION_UNPROVEN;
    }

    return PENANG_STATUS_ERR_INTEGRATION_UNPROVEN;
}
