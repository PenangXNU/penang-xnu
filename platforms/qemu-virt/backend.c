/* SPDX-License-Identifier: MIT */
#include "penang_platform.h"
pv_result pv_qemu_validate(const pv_snapshot *s, uint64_t tcr, const pv_request *q) {
 if (!s) { pv_result r={PV_FAIL,PV_NULL,0,0}; return r; }
 if (s->subject!=1) { pv_result r={PV_FAIL,PV_SUBJECT,0,0}; return r; }
 if (s->valid==1 && (s->midr_el1&0xff0ffff0ull)!=0x410fd0b0ull) {
  pv_result r={PV_FAIL,PV_CPU_IDENTITY,0,0}; return r;
 }
 return pv_validate_translation(s,tcr,q);
}
pv_status pv_qemu_read_el1(pv_snapshot *s) {
 if (!s) return PV_FAIL;
 *s=(pv_snapshot){0};
#if defined(__aarch64__) || defined(__arm64__)
 /* Called only by the selected QEMU_VIRT kernel backend at EL1, never from EL0.
  * Context is selected by PlatformExpert, not inferred from MIDR. */
 __asm__ volatile("mrs %0, CurrentEL" : "=r"(s->current_el));
 if (s->current_el!=4) return PV_BLOCKED;
 __asm__ volatile("mrs %0, MIDR_EL1" : "=r"(s->midr_el1));
 __asm__ volatile("mrs %0, ID_AA64MMFR0_EL1" : "=r"(s->id_aa64mmfr0_el1));
 __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(s->sctlr_el1));
 __asm__ volatile("mrs %0, TCR_EL1" : "=r"(s->observed_tcr_el1));
 __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(s->cntfrq_el0));
 s->subject=1; s->valid=1; return PV_PASS;
#else
 return PV_NOT_RUN;
#endif
}
