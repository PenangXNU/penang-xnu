/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * drivers/penang/include/penang_bsp_contract.h
 *
 * Host-side hardware-description and platform-resource validation contract
 * for Motorola Moto G53 5G (codename: penang, SoC: Qualcomm SM4350-AC / holi).
 *
 * IMPORTANT:
 * - This is a host-testable validation foundation, NOT an operational XNU/IOKit
 *   kernel driver and NOT suitable for loading on a phone.
 * - Grounded exclusively in manifests/penang-bsp-offline.json (stock vendor_boot
 *   base FDT, SHA-256: 7919e7a72ba1244ff06c545384456a8a7779857bc54d3577da72136b48ec9e67)
 *   and manifests/penang-cpu-probe.json (read-only CPU MIDR/HWCAP probe).
 * - Contains NO speculative physical MMIO constants and performs NO real MMIO
 *   reads or writes.
 * - XNU 11417, IOKit/PlatformExpert, GMU, display, and SMMU integrations are
 *   explicitly reported as UNKNOWN until matching public interfaces are built
 *   and exercised.
 */

#ifndef PENANG_BSP_CONTRACT_H
#define PENANG_BSP_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Versioned contract ABI identifier (v1.0).
 * Callers must populate contract_version = PENANG_BSP_CONTRACT_ABI_VERSION.
 */
#define PENANG_BSP_CONTRACT_VERSION_MAJOR 1u
#define PENANG_BSP_CONTRACT_VERSION_MINOR 0u
#define PENANG_BSP_CONTRACT_ABI_VERSION \
    ((PENANG_BSP_CONTRACT_VERSION_MAJOR << 16) | PENANG_BSP_CONTRACT_VERSION_MINOR)

/*
 * Provenance constants grounded in manifests/penang-bsp-offline.json.
 */
#define PENANG_MANIFEST_SCHEMA_VERSION "0.1.0"
#define PENANG_BOARD_NAME              "Motorola Moto G53 5G"
#define PENANG_CODENAME                "penang"
#define PENANG_SOC_MODEL               "SM4350-AC"
#define PENANG_SOC_FAMILY              "holi"
#define PENANG_SOURCE_TYPE             "stock_vendor_boot_fdt_not_running_tree"
#define PENANG_STOCK_DTB_SHA256 \
    "7919e7a72ba1244ff06c545384456a8a7779857bc54d3577da72136b48ec9e67"
#define PENANG_STOCK_DTB_BYTES         323476u
#define PENANG_PARSED_CANDIDATES_SHA256 \
    "a060cd705075b4e4c7edd8cd42d7e8b783c585c638f7ffdc4f6c305794dd7881"
#define PENANG_DEVICE_TEST_STATUS      "NO_DEVICE_PROBE_PERFORMED_IN_THIS_STEP"

/*
 * Verified CPU topology constants from manifests/penang-bsp-offline.json
 * and manifests/penang-cpu-probe.json.
 */
#define PENANG_CPU_COUNT                 8u
#define PENANG_LITTLE_CORES_COUNT        6u
#define PENANG_BIG_CORES_COUNT           2u
#define PENANG_LITTLE_CORES_MASK         0x3fu
#define PENANG_CPU_CAPACITY_SILVER_DMIPS 1024u /* 0x400: CPUs 0..5 (Kryo 4xx Silver, part 0x805) */
#define PENANG_CPU_CAPACITY_GOLD_DMIPS   1740u /* 0x6cc: CPUs 6..7 (Kryo 4xx Gold, part 0x804) */
#define PENANG_MIDR_KRYO4XX_SILVER       0x51df805eu
#define PENANG_MIDR_KRYO4XX_GOLD         0x518f804eu

/*
 * Verified GICv3 constants from manifests/penang-bsp-offline.json.
 * No other physical MMIO addresses are verified in the offline base FDT manifest.
 */
#define PENANG_GIC_COMPATIBLE            "arm,gic-v3"
#define PENANG_GIC_DIST_BASE             0x0f200000ULL
#define PENANG_GIC_DIST_SIZE             0x00010000ULL
#define PENANG_GIC_REDIST_BASE           0x0f240000ULL
#define PENANG_GIC_REDIST_SIZE           0x00100000ULL
#define PENANG_GIC_REDIST_FRAME_SIZE     0x00020000ULL /* 128 KiB per GICv3 redistributor (RD_base + SGI_base) */

/*
 * PSCI constants from stock DTB /soc/psci; runtime invocation is NOT validated.
 */
#define PENANG_PSCI_COMPATIBLE           "arm,psci-1.0"
#define PENANG_PSCI_BASE_FDT_METHOD      "smc" /* stock DTB /soc/psci; NOT a runtime result */

/*
 * Address space and alignment bounds for descriptor validation.
 */
#define PENANG_MIN_PAGE_ALIGNMENT        0x00001000ULL /* 4 KiB minimum DT MMIO page alignment */
#define PENANG_XNU_16K_PAGE_ALIGNMENT    0x00004000ULL /* 16 KiB XNU page granule (unverified on penang EL1) */
#define PENANG_PHYS_ADDR_LIMIT_48BIT     (1ULL << 48)

/*
 * MMIO descriptor flags.
 * Real MMIO writes are strictly forbidden in this host-only contract.
 */
#define PENANG_MMIO_FLAG_NO_REAL_WRITES  (1u << 0)
#define PENANG_MMIO_FLAG_WRITE_ATTEMPTED (1u << 1)
#define PENANG_MMIO_FLAG_SPECULATIVE     (1u << 2)
#define PENANG_MMIO_FLAG_ALLOWED_MASK    (PENANG_MMIO_FLAG_NO_REAL_WRITES)

/*
 * Linux/Devicetree GICv3 3-cell interrupt specifier constants
 * (Documentation/devicetree/bindings/interrupt-controller/arm,gic-v3.yaml).
 */
#define PENANG_GIC_IRQ_TYPE_SPI          0u
#define PENANG_GIC_IRQ_TYPE_PPI          1u

#define PENANG_GIC_SPI_MAX_NUMBER        987u /* INTIDs 32..1019 */
#define PENANG_GIC_PPI_MAX_NUMBER        15u  /* INTIDs 16..31 */

#define PENANG_IRQ_FLAG_EDGE_RISING      0x01u
#define PENANG_IRQ_FLAG_EDGE_FALLING     0x02u
#define PENANG_IRQ_FLAG_EDGE_BOTH        0x03u /* reserved/rejected: no GICv3 both-edge trigger */
#define PENANG_IRQ_FLAG_LEVEL_HIGH       0x04u
#define PENANG_IRQ_FLAG_LEVEL_LOW        0x08u
#define PENANG_IRQ_FLAG_TRIGGER_MASK     0x0fu

/*
 * Evidence classification for hardware descriptors.
 * Zero-initialized value (0) is PENANG_EVIDENCE_UNKNOWN (fail-closed default).
 */
typedef enum penang_evidence_status {
    PENANG_EVIDENCE_UNKNOWN = 0,
    PENANG_EVIDENCE_OFFLINE_FDT_BASE = 1,
    PENANG_EVIDENCE_MEASURED_READONLY = 2,
    PENANG_EVIDENCE_ACTIVE_FDT_VERIFIED = 3
} penang_evidence_status_t;

/*
 * Subsystem integration status for XNU / IOKit / SoC blocks.
 * Zero-initialized value (0) is PENANG_INTEGRATION_UNKNOWN.
 */
typedef enum penang_integration_status {
    PENANG_INTEGRATION_UNKNOWN = 0,
    PENANG_INTEGRATION_HOST_CONTRACT_ONLY = 1
} penang_integration_status_t;

/*
 * Explicit fail-closed validation status codes.
 */
typedef enum penang_status {
    PENANG_STATUS_OK = 0,
    PENANG_STATUS_ERR_NULL_ARG = 1,
    PENANG_STATUS_ERR_CONTRACT_VERSION = 2,
    PENANG_STATUS_ERR_PROVENANCE_MISMATCH = 3,
    PENANG_STATUS_ERR_MMIO_ZERO_OR_EMPTY = 4,
    PENANG_STATUS_ERR_MMIO_OVERFLOW = 5,
    PENANG_STATUS_ERR_MMIO_ALIGNMENT = 6,
    PENANG_STATUS_ERR_MMIO_OVERLAP = 7,
    PENANG_STATUS_ERR_MMIO_SPECULATIVE_ADDR = 8,
    PENANG_STATUS_ERR_MMIO_WRITE_FORBIDDEN = 9,
    PENANG_STATUS_ERR_CPU_COUNT_INVALID = 10,
    PENANG_STATUS_ERR_CPU_MPIDR_INVALID = 11,
    PENANG_STATUS_ERR_CPU_CAPACITY_INVALID = 12,
    PENANG_STATUS_ERR_CPU_MASK_MISMATCH = 13,
    PENANG_STATUS_ERR_GIC_COMPAT_INVALID = 14,
    PENANG_STATUS_ERR_GIC_BOUNDS_INVALID = 15,
    PENANG_STATUS_ERR_IRQ_SPEC_INVALID = 16,
    PENANG_STATUS_ERR_PSCI_METHOD_ASSUMED = 17,
    PENANG_STATUS_ERR_CLOCK_DESC_INVALID = 18,
    PENANG_STATUS_ERR_DISPLAY_DESC_INVALID = 19,
    PENANG_STATUS_ERR_UNKNOWN_FEATURE_ASSERTED = 20,
    PENANG_STATUS_ERR_INTEGRATION_UNPROVEN = 21,
    PENANG_STATUS_ERR_CPU_MIDR_INVALID = 22 /* v1 ABI extension; prior values stable */
} penang_status_t;

/*
 * Memory-mapped device region descriptor (metadata only; no pointer dereference).
 */
typedef struct penang_mmio_desc {
    const char *name;
    uint64_t phys_base;
    uint64_t size;
    uint64_t min_alignment;
    uint32_t flags;
    penang_evidence_status_t evidence;
} penang_mmio_desc_t;

/*
 * Single CPU core descriptor.
 */
typedef struct penang_cpu_core_desc {
    uint32_t cpu_index;
    uint32_t mpidr_aff1;
    uint32_t capacity_dmips_mhz;
    uint32_t midr_el1;
    bool is_little_core;
} penang_cpu_core_desc_t;

/*
 * SoC CPU topology descriptor.
 */
typedef struct penang_cpu_topology_desc {
    uint32_t count;
    uint32_t little_cores_mask;
    penang_cpu_core_desc_t cores[PENANG_CPU_COUNT];
} penang_cpu_topology_desc_t;

/*
 * GICv3 interrupt controller descriptor.
 */
typedef struct penang_gic_desc {
    const char *compatible;
    penang_mmio_desc_t distributor;
    penang_mmio_desc_t redistributor;
} penang_gic_desc_t;

/*
 * GICv3 3-cell interrupt specifier descriptor.
 */
typedef struct penang_irq_desc {
    const char *label;
    uint32_t irq_type;
    uint32_t irq_number;
    uint32_t flags;
} penang_irq_desc_t;

/*
 * PSCI descriptor.
 */
typedef struct penang_psci_desc {
    const char *compatible;
    const char *method;
    penang_evidence_status_t evidence;
} penang_psci_desc_t;

/*
 * Clock resource validation descriptor.
 * Note: Concrete clock MMIO bases and frequency tables are not in
 * manifests/penang-bsp-offline.json; evidence must be PENANG_EVIDENCE_UNKNOWN.
 */
typedef struct penang_clock_desc {
    const char *controller_compat;
    const char *clock_name;
    uint32_t clock_id;
    uint64_t min_hz;
    uint64_t max_hz;
    uint64_t requested_hz;
    penang_evidence_status_t evidence;
} penang_clock_desc_t;

/*
 * Display / framebuffer resource validation descriptor.
 * Note: display_panel_timing is in manifests/penang-bsp-offline.json unknown[];
 * timing_evidence must be PENANG_EVIDENCE_UNKNOWN until active DTBO is extracted.
 */
typedef struct penang_display_desc {
    const char *panel_compat;
    uint32_t width_px;
    uint32_t height_px;
    uint32_t refresh_hz;
    uint32_t bits_per_pixel;
    uint32_t stride_bytes;
    uint64_t framebuffer_size;
    penang_evidence_status_t timing_evidence;
} penang_display_desc_t;

/*
 * Explicit UNKNOWN feature gates corresponding to manifests/penang-bsp-offline.json
 * "unknown" array. All fields MUST be false in the offline baseline.
 */
typedef struct penang_unknown_gates {
    bool active_dt_overlays_verified;
    bool el2_accessibility_verified;
    bool feat_nv2_verified;
    bool pac_verified;
    bool page_granule_16k_verified;
    bool gpu_firmware_verified;
    bool display_panel_timing_verified;
    bool ram_available_to_loader_verified;
} penang_unknown_gates_t;

/*
 * Top-level Penang BSP platform descriptor.
 */
typedef struct penang_platform_descriptor {
    uint32_t contract_version;
    const char *board;
    const char *codename;
    const char *source_type;
    const char *dtb_sha256;
    uint32_t dtb_bytes;
    const char *parsed_candidates_sha256;
    bool not_a_hardware_register_read;
    const char *device_test_status;
    penang_cpu_topology_desc_t cpu;
    penang_gic_desc_t gic;
    penang_psci_desc_t psci;
    penang_unknown_gates_t unknown_gates;
    const penang_mmio_desc_t *extra_mmio_regions;
    size_t extra_mmio_count;
} penang_platform_descriptor_t;

/*
 * Subsystem integration report for XNU 11417, IOKit/PlatformExpert, GMU,
 * Display, and SMMU. All target kernel integrations remain UNKNOWN.
 */
typedef struct penang_integration_report {
    penang_integration_status_t overall_status;
    penang_integration_status_t xnu_11417_platform_expert;
    penang_integration_status_t iokit_service_matching;
    penang_integration_status_t adreno_gmu_gpu;
    penang_integration_status_t display_dpu_mdp;
    penang_integration_status_t smmu_dma_isolation;
    const char *target_xnu_tag;
    const char *target_xnu_commit;
    const char *target_apple_build;
    const char *reference_xnu_tag;
    const char *reference_xnu_commit;
    const char *summary_reason;
} penang_integration_report_t;

/*
 * Human-readable diagnostic helpers.
 */
const char *penang_status_str(penang_status_t status);
const char *penang_evidence_status_str(penang_evidence_status_t evidence);
const char *penang_integration_status_str(penang_integration_status_t status);

/*
 * Populate a platform descriptor with the exact verified offline baseline
 * from manifests/penang-bsp-offline.json.
 */
penang_status_t penang_populate_offline_baseline(penang_platform_descriptor_t *out_desc);

/*
 * Component and whole-descriptor fail-closed validation entry points.
 */
penang_status_t penang_validate_mmio_region(const penang_mmio_desc_t *region);
penang_status_t penang_validate_mmio_set(const penang_mmio_desc_t *regions, size_t count);
penang_status_t penang_validate_cpu_topology(const penang_cpu_topology_desc_t *cpu);
penang_status_t penang_validate_gic_descriptor(const penang_gic_desc_t *gic, uint32_t cpu_count);
penang_status_t penang_validate_irq_specifier(const penang_irq_desc_t *irq);
penang_status_t penang_validate_psci_descriptor(const penang_psci_desc_t *psci);
penang_status_t penang_validate_clock_descriptor(const penang_clock_desc_t *clk);
penang_status_t penang_validate_display_descriptor(const penang_display_desc_t *disp);
penang_status_t penang_validate_unknown_gates(const penang_unknown_gates_t *gates);
penang_status_t penang_validate_platform_descriptor(const penang_platform_descriptor_t *desc);

/*
 * Integration status query and fail-closed readiness gate.
 * penang_require_xnu_iokit_ready() always returns PENANG_STATUS_ERR_INTEGRATION_UNPROVEN
 * until real XNU 11417 / IOKit interfaces are built and verified.
 */
penang_status_t penang_query_integration_report(penang_integration_report_t *out_report);
penang_status_t penang_require_xnu_iokit_ready(const penang_integration_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_BSP_CONTRACT_H */
