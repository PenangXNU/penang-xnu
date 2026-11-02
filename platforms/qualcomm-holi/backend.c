/* SPDX-License-Identifier: MIT */
#include "penang_platform.h"
#include "penang_bsp_contract.h"
/* Reuse the audited metadata decoder. Success is not XNU driver readiness. */
int pv_holi_validate_inventory(void) {
 penang_platform_descriptor_t descriptor;
 penang_status_t status=penang_populate_offline_baseline(&descriptor);
 if (status!=PENANG_STATUS_OK) return (int)status;
 return (int)penang_validate_platform_descriptor(&descriptor);
}
