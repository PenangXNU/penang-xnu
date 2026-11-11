/* SPDX-License-Identifier: MIT */
/*
 * PENANG XNU — M12.1 ARM64 Boot Bridge Contract Header
 * Motorola ABL (Linux ARM64 Image + FDT) -> ARM64 Boot Bridge -> XNU 11417 (__start)
 */
#ifndef PENANG_M12_BRIDGE_CONTRACT_H
#define PENANG_M12_BRIDGE_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define M12_VIRT_BASE               0xfffffe0004000000ULL
#define M12_LINK_OFFSET_MIN         0x03000000ULL
#define M12_L2_BLOCK_SIZE           0x02000000ULL
#define M12_L2_BLOCK_MASK           (M12_L2_BLOCK_SIZE - 1ULL)
#define M12_PAGE_16K                0x4000ULL
#define M12_PAGE_16K_MASK           (M12_PAGE_16K - 1ULL)
#define M12_DT_MAX_SIZE             0x10000ULL
#define M12_BOOT_LINE_LENGTH        1024u
#define M12_BOOT_ARGS_SIZE          1152u

#define M12_PENANG_FB_BASE          0x85200000ULL
#define M12_PENANG_FB_WIDTH         720u
#define M12_PENANG_FB_HEIGHT        1600u
#define M12_PENANG_FB_STRIDE        2880u
#define M12_PENANG_FB_DEPTH         32u
#define M12_PENANG_FB_ENVELOPE      0x0005F6C0ULL

#define M12_PENANG_GICD_BASE        0x0f200000ULL
#define M12_PENANG_GICD_SIZE        0x00010000ULL
#define M12_PENANG_GICR_BASE        0x0f240000ULL
#define M12_PENANG_GICR_PE_SIZE     0x00020000ULL
#define M12_PENANG_GICR_TOTAL_SIZE  0x00100000ULL
#define M12_PENANG_SOC_IO_BASE      0x0f000000ULL
#define M12_PENANG_SOC_IO_SIZE      0x01000000ULL

#define M12_QEMU_GICD_BASE          0x08000000ULL
#define M12_QEMU_GICD_SIZE          0x00010000ULL
#define M12_QEMU_GICR_BASE          0x080a0000ULL
#define M12_QEMU_GICR_PE_SIZE       0x00020000ULL
#define M12_QEMU_UART_BASE          0x09000000ULL
#define M12_QEMU_SOC_IO_BASE        0x08000000ULL
#define M12_QEMU_SOC_IO_SIZE        0x02000000ULL

#define M12_C0_PACKET_BYTES         136u
#define M12_MAX_MEM_RANGES          8u
#define M12_MAX_RESERVED_RANGES     64u
#define M12_MAX_MACHO_SEGMENTS      24u

enum m12_bridge_status {
    M12_BRIDGE_OK                  = 0,
    M12_ERR_ABI_ARGS               = 1,
    M12_ERR_CPU_EL                 = 2,
    M12_ERR_CPU_MMU_CACHE          = 3,
    M12_ERR_NO_TGRAN16             = 4,
    M12_ERR_CNTFRQ_ZERO            = 5,
    M12_ERR_FDT_INVALID            = 6,
    M12_ERR_FDT_COMPATIBLE         = 7,
    M12_ERR_MEMORY_MAP             = 8,
    M12_ERR_DYNAMIC_RESERVATION    = 9,
    M12_ERR_BOOT_CPU_MPIDR         = 10,
    M12_ERR_GIC_INVALID            = 11,
    M12_ERR_WINDOW_SELECTION       = 12,
    M12_ERR_MACHO_INVALID          = 13,
    M12_ERR_SEGMENT_OVERLAP        = 14,
    M12_ERR_ADT_OVERFLOW           = 15,
    M12_ERR_BOOTARGS_INVALID       = 16,
    M12_ERR_EXCEPTION_TAKEN        = 17,
    M12_ERR_ENTROPY_UNAVAILABLE    = 18,
    M12_ERR_DYNAMIC_UNRESOLVED     = 19,
    M12_ERR_ENTROPY_UNTRUSTED      = 20,
    M12_STATUS_COUNT               = 21
};

enum m12_platform_profile {
    M12_PLAT_UNKNOWN         = 0,
    M12_PLAT_PENANG_PHYSICAL = 1,
    M12_PLAT_QEMU_VIRT       = 2
};

/* Exact XNU 11417.140.69 Boot_Video (pexpert/pexpert/arm64/boot.h) */
struct m12_boot_video {
    uint64_t v_baseAddr;
    uint64_t v_display;
    uint64_t v_rowBytes;
    uint64_t v_width;
    uint64_t v_height;
    uint64_t v_depth;
};

/* Exact XNU 11417.140.69 boot_args (1152 bytes) */
struct m12_boot_args {
    uint16_t Revision;
    uint16_t Version;
    uint32_t _pad0;
    uint64_t virtBase;
    uint64_t physBase;
    uint64_t memSize;
    uint64_t topOfKernelData;
    struct m12_boot_video Video;
    uint32_t machineType;
    uint32_t _pad1;
    uint64_t deviceTreeP;
    uint32_t deviceTreeLength;
    char     CommandLine[M12_BOOT_LINE_LENGTH];
    uint32_t _pad2;
    uint64_t bootFlags;
    uint64_t memSizeActual;
};

_Static_assert(sizeof(struct m12_boot_args) == M12_BOOT_ARGS_SIZE, "boot_args must be 1152 bytes");
_Static_assert(offsetof(struct m12_boot_args, virtBase) == 8, "virtBase offset");
_Static_assert(offsetof(struct m12_boot_args, physBase) == 16, "physBase offset");
_Static_assert(offsetof(struct m12_boot_args, memSize) == 24, "memSize offset");
_Static_assert(offsetof(struct m12_boot_args, topOfKernelData) == 32, "topOfKernelData offset");
_Static_assert(offsetof(struct m12_boot_args, Video) == 40, "Video offset");
_Static_assert(offsetof(struct m12_boot_args, machineType) == 88, "machineType offset");
_Static_assert(offsetof(struct m12_boot_args, deviceTreeP) == 96, "deviceTreeP offset");
_Static_assert(offsetof(struct m12_boot_args, deviceTreeLength) == 104, "deviceTreeLength offset");
_Static_assert(offsetof(struct m12_boot_args, CommandLine) == 108, "CommandLine offset");
_Static_assert(offsetof(struct m12_boot_args, bootFlags) == 1136, "bootFlags offset");
_Static_assert(offsetof(struct m12_boot_args, memSizeActual) == 1144, "memSizeActual offset");

struct m12_phys_range {
    uint64_t base;
    uint64_t size;
};

struct m12_macho_seg {
    char     name[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
};

struct m12_cpu_context {
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t entry_pc;
    uint64_t current_el_raw;      /* Entry CurrentEL (before any EL2->EL1 drop) */
    uint64_t active_el_raw;       /* CurrentEL after bridge EL setup (must be 0x4 = EL1) */
    uint64_t sctlr_el1;
    uint64_t daif;
    uint64_t id_aa64mmfr0_el1;
    uint64_t mpidr_el1;
    uint64_t midr_el1;
    uint64_t cntfrq_el0;
    uint64_t vbar_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
    uint64_t elr_el1;
    uint64_t bridge_start_pa;
    uint64_t bridge_end_pa;
    uint64_t payload_start_pa;
    uint64_t payload_end_pa;
    uint32_t fb_enabled;
    uint32_t force_profile;       /* 0 = auto from FDT, 1 = PENANG_PHYSICAL, 2 = QEMU_VIRT */
};

#define M12_MAX_DYNAMIC_REQUESTS 16u
#define M12_RESERVATION_FDT_REQUEST 1u

struct m12_dynamic_placement {
    struct m12_phys_range range;
    uint32_t node_name_offset;
    uint32_t owner; /* FDT request only. No verified hardware owner. */
    uint32_t no_map;
};

struct m12_handoff_plan {
    uint32_t status;
    uint32_t platform_profile;
    uint64_t fdt_pa;
    uint32_t fdt_totalsize;
    uint32_t cpu_count;
    uint64_t boot_mpidr;
    uint64_t soc_io_base;
    uint64_t soc_io_size;
    uint64_t gicd_base;
    uint64_t gicd_size;
    uint64_t gicr_base;
    uint64_t gicr_size;
    uint64_t uart_base;
    uint32_t psci_method;         /* 1 = smc, 2 = hvc */
    uint32_t ram_range_count;
    struct m12_phys_range ram_ranges[M12_MAX_MEM_RANGES];
    uint32_t reserved_count;
    struct m12_phys_range reserved_ranges[M12_MAX_RESERVED_RANGES];
    uint32_t coalesced_reserved_count;
    struct m12_phys_range coalesced_reserved[M12_MAX_RESERVED_RANGES];
    uint32_t segment_count;
    struct m12_macho_seg segments[M12_MAX_MACHO_SEGMENTS];
    uint64_t virt_base;
    uint64_t phys_base;
    uint64_t mem_size;
    uint64_t kernel_lo_pa;
    uint64_t kernel_hi_pa;
    uint64_t ramdisk_pa;
    uint64_t ramdisk_size;
    uint64_t dt_pa;
    uint32_t dt_len;
    uint32_t _pad0;
    uint64_t args_pa;
    uint64_t top_of_kernel_data;
    uint64_t entry_va;
    uint64_t entry_pa;
    uint64_t fb_base_pa;
    struct m12_boot_args boot_args_copy;
    uint8_t  c0_packet[M12_C0_PACKET_BYTES];
    uint32_t dynamic_count;
    struct m12_dynamic_placement dynamic[M12_MAX_DYNAMIC_REQUESTS];
};

#if !defined(__aarch64__) && !defined(__arm64__)
int m12_bridge_plan_memory(const void *fdt, size_t bytes, struct m12_handoff_plan *plan);
#endif

#ifdef M12_HANDOFF_PROBE
enum m12_memory_record_kind {
    M12_MEM_RAM = 1, M12_MEM_STATIC, M12_MEM_REQUEST, M12_MEM_ALLOC_RANGE,
    M12_MEM_PLACEMENT
};
typedef int (*m12_memory_observer)(void *, uint32_t, uint32_t, uint64_t, uint64_t, uint32_t);
int m12_bridge_probe_memory(const void *fdt, size_t bytes, struct m12_handoff_plan *plan,
                            const struct m12_phys_range *admitted,
                            const struct m12_phys_range *excluded, size_t excluded_count,
                            m12_memory_observer observe, void *opaque, int *placement_status);
#endif

extern uint64_t g_m12_handoff_args_pa;
extern uint64_t g_m12_handoff_entry_pa;

const char *m12_status_name(uint32_t status);
uint32_t m12_crc32c(const uint8_t *buf, size_t len);
int m12_bridge_execute(const struct m12_cpu_context *ctx, struct m12_handoff_plan *plan, int commit_memory);
void m12_bridge_record_exception(const struct m12_cpu_context *ctx, struct m12_handoff_plan *plan,
                                 uint64_t esr, uint64_t far, uint64_t elr);

#ifdef __cplusplus
}
#endif

#endif /* PENANG_M12_BRIDGE_CONTRACT_H */
