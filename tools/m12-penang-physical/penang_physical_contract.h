/* SPDX-License-Identifier: MIT */
/* M12.2: host-verifiable IOKit Penang platform contract. No MMIO operations. */
#ifndef M12_PENANG_PHYSICAL_CONTRACT_H
#define M12_PENANG_PHYSICAL_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PPM_ABI_VERSION 1u
#define PPM_COMPATIBLE "motorola,penang"
#define PPM_MODEL "PENANG_PHYSICAL"
#define PPM_TARGET "PENANG_PHYSICAL"
#define PPM_TIMER_NOMINAL_HZ 19200000ULL
#define PPM_GICD_BASE 0x0f200000ULL
#define PPM_GICD_BYTES 0x10000ULL
#define PPM_GICR_BASE 0x0f240000ULL
#define PPM_GICR_BOOT_FRAME_BYTES 0x20000ULL
#define PPM_SOC_BASE 0x0f000000ULL
#define PPM_SOC_BYTES 0x01000000ULL

enum ppm_result {
    PPM_OK = 0,
    PPM_ERR_INPUT = 1,
    PPM_ERR_IDENTITY = 2,
    PPM_ERR_TGRAN16_GATE = 3,
    PPM_ERR_OPTIONS_UNSUPPORTED = 4,
    PPM_ERR_CPU_NODE = 5,
    PPM_ERR_CPU_AFFINITY = 6,
    PPM_ERR_CPU_TIMEBASE = 7,
    PPM_ERR_KERNEL_TIMEBASE = 8,
    PPM_ERR_SOC_LAYOUT = 9,
    PPM_ERR_GIC_LAYOUT = 10,
    PPM_STATUS_COUNT = 11
};

struct ppm_blob {
    const uint8_t *bytes;
    uint32_t count;
};

struct ppm_platform_snapshot {
    uint32_t abi_version;
    struct ppm_blob root_compatible;
    struct ppm_blob root_model;
    struct ppm_blob root_target;
    struct ppm_blob bridge_tgran16; /* LE u32=1, emitted ONLY after bridge reads ID_AA64MMFR0 */
    uint32_t options_present;
    uint32_t cpu_nodes;
    struct ppm_blob cpu0_device_type;
    struct ppm_blob cpu0_reg;          /* little-endian u32 */
    struct ppm_blob cpu0_timebase;     /* little-endian u32 */
    uint64_t kernel_timebase_hz;       /* gPEClockFrequencyInfo, must be set */
    struct ppm_blob armio_ranges;      /* LE u64 child-bus, LE u64 PA, LE u64 bytes */
    struct ppm_blob gic_compatible;    /* NUL-terminated string list */
    struct ppm_blob gic_reg;           /* relative GICD/GICR, LE u64 pairs */
};

struct ppm_verified {
    uint64_t timer_hz;
    uint64_t gicd_pa;
    uint64_t gicr_pa;
    uint32_t cpu_count;
};

int ppm_validate_identity(const struct ppm_platform_snapshot *snap);
int ppm_validate_platform(const struct ppm_platform_snapshot *snap, struct ppm_verified *out);
const char *ppm_status_name(int status);

#ifdef __cplusplus
}
#endif
#endif
