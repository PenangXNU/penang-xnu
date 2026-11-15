/* SPDX-License-Identifier: MIT */
/* Pure C, freestanding: never read/write physical MMIO, configure PSCI, or acknowledge IRQ. */
#include "penang_physical_contract.h"

static uint64_t ppm_u64(const uint8_t *v)
{
    uint64_t x = 0;
    uint32_t j;
    for (j = 0; j < 8; ++j) {
        x |= ((uint64_t)v[j]) << (8u * j);
    }
    return x;
}

static uint32_t ppm_u32(const uint8_t *v)
{
    uint32_t x = 0;
    uint32_t j;
    for (j = 0; j < 4; ++j) {
        x |= ((uint32_t)v[j]) << (8u * j);
    }
    return x;
}

static int ppm_is(const struct ppm_blob *v, const char *want)
{
    uint32_t i = 0;
    if (!v || !v->bytes || v->count == 0) return 0;
    while (i < v->count && want[i] && (uint8_t)want[i] == v->bytes[i]) ++i;
    if (want[i] || i == v->count || v->bytes[i] != 0) return 0;
    for (; i < v->count; ++i) if (v->bytes[i] != 0) return 0;
    return 1;
}

static int ppm_has_compatible(const struct ppm_blob *v, const char *want)
{
    uint32_t start = 0, i = 0, k;
    int found = 0;
    if (!v || !v->bytes || !v->count || v->bytes[v->count - 1] != 0) return 0;
    while (i < v->count) {
        start = i;
        while (i < v->count && v->bytes[i]) ++i;
        if (i >= v->count) return 0;
        if (i == start) {
            /* Trailing NUL padding allowed, non-NUL garbage forbidden. */
            for (; i < v->count; ++i) if (v->bytes[i]) return 0;
            break;
        }
        k = 0;
        while (want[k] && start + k < i && (uint8_t)want[k] == v->bytes[start + k]) ++k;
        if (!want[k] && start + k == i) found = 1;
        ++i;
    }
    return found;
}

static int ppm_is_usable(const struct ppm_blob *v, uint32_t n)
{
    return v && v->bytes && v->count == n;
}

static const char *const status_names[PPM_STATUS_COUNT] = {
    "OK", "INVALID_INPUT", "IDENTITY_MISMATCH", "TGRAN16_UNQUALIFIED",
    "OPTIONS_UNSUPPORTED", "CPU_NODE_INVALID", "CPU_MPIDR_MISMATCH",
    "CPU_TIMEBASE_INVALID", "KERNEL_TIMEBASE_UNKNOWN", "SOC_LAYOUT_INVALID",
    "GIC_LAYOUT_INVALID"
};

const char *ppm_status_name(int status)
{
    return status >= 0 && status < PPM_STATUS_COUNT ? status_names[status] : "UNKNOWN";
}

int ppm_validate_identity(const struct ppm_platform_snapshot *s)
{
    if (!s || s->abi_version != PPM_ABI_VERSION) return PPM_ERR_INPUT;
    if (!ppm_has_compatible(&s->root_compatible, PPM_COMPATIBLE) ||
        !ppm_is(&s->root_model, PPM_MODEL) ||
        !ppm_is(&s->root_target, PPM_TARGET)) return PPM_ERR_IDENTITY;
    return PPM_OK;
}

int ppm_validate_platform(const struct ppm_platform_snapshot *s, struct ppm_verified *out)
{
    uint64_t soc_child, soc_base, soc_size, dist_off, dist_size, redist_off, redist_size;
    uint64_t freq;
    int identity;
    if (!s || !out) return PPM_ERR_INPUT;
    out->timer_hz = out->gicd_pa = out->gicr_pa = 0;
    out->cpu_count = 0;
    identity = ppm_validate_identity(s);
    if (identity != PPM_OK) return identity;
    if (!ppm_is_usable(&s->bridge_tgran16, 4u) ||
        ppm_u32(s->bridge_tgran16.bytes) != 1u) return PPM_ERR_TGRAN16_GATE;
    if (s->options_present != 0) return PPM_ERR_OPTIONS_UNSUPPORTED;
    if (s->cpu_nodes != 1u ||
        !ppm_is(&s->cpu0_device_type, "cpu") ||
        !ppm_is_usable(&s->cpu0_reg, 4u)) return PPM_ERR_CPU_NODE;
    if (ppm_u32(s->cpu0_reg.bytes) != 0u) return PPM_ERR_CPU_AFFINITY;
    if (!ppm_is_usable(&s->cpu0_timebase, 4u)) return PPM_ERR_CPU_TIMEBASE;
    freq = ppm_u32(s->cpu0_timebase.bytes);
    if (freq == 0u || freq != PPM_TIMER_NOMINAL_HZ) return PPM_ERR_CPU_TIMEBASE;
    if (!s->kernel_timebase_hz) return PPM_ERR_KERNEL_TIMEBASE;
    if (freq != s->kernel_timebase_hz) return PPM_ERR_KERNEL_TIMEBASE;

    if (!ppm_is_usable(&s->armio_ranges, 24u)) return PPM_ERR_SOC_LAYOUT;
    soc_child = ppm_u64(s->armio_ranges.bytes);
    soc_base = ppm_u64(s->armio_ranges.bytes + 8);
    soc_size = ppm_u64(s->armio_ranges.bytes + 16);
    if (soc_child != 0 || soc_base != PPM_SOC_BASE ||
        soc_size != PPM_SOC_BYTES) return PPM_ERR_SOC_LAYOUT;

    if (!ppm_has_compatible(&s->gic_compatible, "arm,gic-v3") ||
        !ppm_is_usable(&s->gic_reg, 32u)) return PPM_ERR_GIC_LAYOUT;
    dist_off = ppm_u64(s->gic_reg.bytes);
    dist_size = ppm_u64(s->gic_reg.bytes + 8);
    redist_off = ppm_u64(s->gic_reg.bytes + 16);
    redist_size = ppm_u64(s->gic_reg.bytes + 24);
    /* Bounds checked BEFORE adding PA: no wraparound and no MMIO access. */
    if (dist_off > soc_size || redist_off > soc_size ||
        dist_size > soc_size - dist_off ||
        redist_size > soc_size - redist_off ||
        dist_size != PPM_GICD_BYTES ||
        redist_size != PPM_GICR_BOOT_FRAME_BYTES ||
        soc_base + dist_off != PPM_GICD_BASE ||
        soc_base + redist_off != PPM_GICR_BASE ||
        (dist_off < redist_off + redist_size &&
         redist_off < dist_off + dist_size)) return PPM_ERR_GIC_LAYOUT;

    out->timer_hz = freq;
    out->gicd_pa = soc_base + dist_off;
    out->gicr_pa = soc_base + redist_off;
    out->cpu_count = 1;
    return PPM_OK;
}
