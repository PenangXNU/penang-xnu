/* SPDX-License-Identifier: Apache-2.0 */
#include "penang_platform.h"
/* P1 has no admitted physical inventory or operational backend. */
pv_status pv_trinket_require_runtime(void) { return PV_BLOCKED; }
