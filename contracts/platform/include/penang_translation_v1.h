/* SPDX-License-Identifier: Apache-2.0
 * Source pin: XNU 11417.140.69 / 43a90889846e00bfb5cf1d255cdc0a701a1e05a4
 * TCR_EL1_BOOT evaluated with the P0.7 QEMU_VIRT board. No protection is changed.
 */
#ifndef PENANG_TRANSLATION_V1_H
#define PENANG_TRANSLATION_V1_H
#define PV_QEMU_TCR_BASE 0x226511a511
#define PV_QEMU_OPTIONAL_ASID16 0x1000000000
#define PV_QEMU_TCR_CACHE 0x25
#endif
