/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PENANG_P1_RUNTIME_FDT_PROVIDER_H
#define PENANG_P1_RUNTIME_FDT_PROVIDER_H
#include "p1_memory_admission.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The provider writes this descriptor exactly once on the boot CPU. Its
 * declared object type must agree with that mutable storage, including under
 * optimization/LTO. Consumers borrow a const view; the retained tree bytes
 * themselves are immutable. Binding must complete before the first view is
 * admitted and before any concurrent reader starts. This declaration supplies
 * no storage, fallback FDT or binding operation. Missing storage stays a link
 * failure; an unbound descriptor stays an admission failure. */
extern struct p1_runtime_fdt penang_p1_runtime_fdt;

static inline const struct p1_runtime_fdt *
p1_runtime_fdt_view(void)
{
    return &penang_p1_runtime_fdt;
}

#ifdef __cplusplus
}
#endif
#endif
