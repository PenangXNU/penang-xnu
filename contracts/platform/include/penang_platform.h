/* SPDX-License-Identifier: MIT */
/* v1: metadata validation, not an MMU/TLB or security provider. */
#ifndef PENANG_PLATFORM_H
#define PENANG_PLATFORM_H
#include <stdint.h>
#include "penang_translation_v1.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PV_ABI_VERSION 0x00010000u
typedef enum { PV_PASS=0, PV_FAIL=1, PV_BLOCKED=2, PV_NOT_RUN=3 } pv_status;
typedef enum {
 PV_OK=0, PV_NULL=1, PV_ABI=2, PV_GRANULE_UNSUPPORTED=3,
 PV_FEATURE_UNKNOWN=4, PV_TCR_GRANULE=5, PV_PA_RANGE=6,
 PV_TCR_VA=7, PV_TCR_MODE=8, PV_EXCEPTION_LEVEL=9, PV_MMU_STATE=10,
 PV_CPU_IDENTITY=11, PV_MEMORY_OVERFLOW=12, PV_MEMORY_CONTAINMENT=13,
 PV_MEMORY_OVERLAP=14, PV_MEMORY_ALIGNMENT=15, PV_MEMORY_REQUIRED=16,
 PV_TCR_CACHE=17, PV_ASID=18, PV_SUBJECT=19
} pv_code;
typedef struct {
 uint32_t valid, subject; /* 1=virtual context, 2=physical context; not CPUID-derived */
 uint64_t current_el, midr_el1, id_aa64mmfr0_el1, sctlr_el1;
 uint64_t observed_tcr_el1, cntfrq_el0;
} pv_snapshot;
typedef struct { uint32_t abi_version, granule, pa_bits, va_bits; } pv_request;
typedef struct { uint64_t base, size; uint32_t kind, reserved; } pv_region;
typedef struct { uint32_t status, code, pa_bits, granule_state; } pv_result;
/* Host and kernel callers share the decoder; no writes to architectural state. */
pv_result pv_validate_translation(const pv_snapshot *, uint64_t desired_tcr, const pv_request *);
pv_result pv_validate_memory(const pv_region *, size_t count, const pv_request *);
pv_result pv_qemu_validate(const pv_snapshot *, uint64_t desired_tcr, const pv_request *);
/* Privileged EL1 caller only. Host build returns NOT_RUN, never fabricated registers. */
pv_status pv_qemu_read_el1(pv_snapshot *);
int pv_holi_validate_inventory(void);
pv_status pv_trinket_require_runtime(void);
#ifdef __cplusplus
}
#endif
#endif
