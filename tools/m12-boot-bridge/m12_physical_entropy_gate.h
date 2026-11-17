/* SPDX-License-Identifier: MIT
 * M12.2-B3 physical entropy admission boundary.
 *
 * The DT /chosen/rng-seed is mutable loader data. Having 64 nonzero bytes
 * does NOT authenticate the source, hardware provenance, freshness or
 * health of any RNG. The stock Motorola ABL provider is not yet qualified.
 *
 * Physical builds therefore fail CLOSED until a separately reviewed
 * supplier contract is integrated. This header intentionally provides no
 * runtime "trust me" flag and no weak symbol that silently opts in.
 */
#ifndef M12_PHYSICAL_ENTROPY_GATE_H
#define M12_PHYSICAL_ENTROPY_GATE_H

/* Host synthetic testing must be opted into explicitly, and MUST NOT compile
 * for ARM64 or in device configurations. This is not a cryptographic source.
 */
#if defined(M12_HOST_SYNTHETIC_ENTROPY) && (defined(__aarch64__) || defined(__arm64__) || defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(M12_PENANG_PHYSICAL_DEVICE_BUILD))
#error M12_HOST_SYNTHETIC_ENTROPY is forbidden in target/device builds
#endif

static inline int
m12_physical_entropy_source_qualified(void)
{
#if defined(M12_HOST_SYNTHETIC_ENTROPY)
    return 1; /* explicitly labeled host-only fixture; NEVER DEVICE_VERIFIED */
#else
    return 0; /* until physical provider identity and trust are established */
#endif
}
#endif /* M12_PHYSICAL_ENTROPY_GATE_H */
