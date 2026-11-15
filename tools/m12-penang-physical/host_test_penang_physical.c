/* SPDX-License-Identifier: MIT */
/* M12.2 deterministic host tests (positive + fail-closed mutations). */
#include "penang_physical_contract.h"
#include <stdio.h>
#include <string.h>

static const uint8_t kCompat[] = "penang,physical\0motorola,penang\0qcom,holi";
static const uint8_t kModel[] = PPM_MODEL;
static const uint8_t kTarget[] = PPM_TARGET;
static const uint8_t kCPU[] = "cpu";
static const uint8_t kGIC[] = "arm,gic-v3";
static const uint8_t kGate[] = {1,0,0,0};
static const uint8_t kCPUReg[] = {0,0,0,0};
static const uint8_t kFreq[] = {0,0xf8,0x24,0x01};
static uint8_t ranges[24];
static uint8_t gicreg[32];

static void put64(uint8_t *b, uint64_t val)
{
    unsigned i;
    for (i = 0; i < 8; ++i) b[i] = (uint8_t)(val >> (i * 8));
}

#define BLOB(v) { (v), (uint32_t)sizeof(v) }
static struct ppm_platform_snapshot valid_snapshot(void)
{
    struct ppm_platform_snapshot s;
    memset(&s, 0, sizeof(s));
    put64(ranges + 0, 0);
    put64(ranges + 8, PPM_SOC_BASE);
    put64(ranges + 16, PPM_SOC_BYTES);
    put64(gicreg + 0, PPM_GICD_BASE - PPM_SOC_BASE);
    put64(gicreg + 8, PPM_GICD_BYTES);
    put64(gicreg + 16, PPM_GICR_BASE - PPM_SOC_BASE);
    put64(gicreg + 24, PPM_GICR_BOOT_FRAME_BYTES);
    s.abi_version = PPM_ABI_VERSION;
    s.root_compatible.bytes = kCompat;
    s.root_compatible.count = sizeof(kCompat);
    s.root_model.bytes = kModel;
    s.root_model.count = sizeof(kModel);
    s.root_target.bytes = kTarget;
    s.root_target.count = sizeof(kTarget);
    s.bridge_tgran16.bytes = kGate;
    s.bridge_tgran16.count = sizeof(kGate);
    s.cpu_nodes = 1;
    s.cpu0_device_type.bytes = kCPU;
    s.cpu0_device_type.count = sizeof(kCPU);
    s.cpu0_reg.bytes = kCPUReg;
    s.cpu0_reg.count = sizeof(kCPUReg);
    s.cpu0_timebase.bytes = kFreq;
    s.cpu0_timebase.count = sizeof(kFreq);
    s.kernel_timebase_hz = PPM_TIMER_NOMINAL_HZ;
    s.armio_ranges.bytes = ranges;
    s.armio_ranges.count = sizeof(ranges);
    s.gic_compatible.bytes = kGIC;
    s.gic_compatible.count = sizeof(kGIC);
    s.gic_reg.bytes = gicreg;
    s.gic_reg.count = sizeof(gicreg);
    return s;
}

static unsigned tests;
static int verify(struct ppm_platform_snapshot *s, int expected, const char *name)
{
    struct ppm_verified out;
    int rc = ppm_validate_platform(s, &out);
    tests++;
    if (rc != expected) {
        fprintf(stderr, "FAIL %s: expected %s got %s\n",
                name, ppm_status_name(expected), ppm_status_name(rc));
        return 1;
    }
    if (expected == PPM_OK &&
        (out.cpu_count != 1 || out.timer_hz != PPM_TIMER_NOMINAL_HZ ||
         out.gicd_pa != PPM_GICD_BASE || out.gicr_pa != PPM_GICR_BASE)) {
        fprintf(stderr, "FAIL %s: verified output mismatch\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    struct ppm_platform_snapshot s;
    const uint8_t wrong[] = "QEMU_VIRT";
    const uint8_t wrongcpu[] = {1,0,0,0};
    const uint8_t gatebad[] = {0,0,0,0};
    const uint8_t freqbad[] = {0,0,0,0};
    const uint8_t badlist[] = {'m','o','t','o','r','o','l','a',',','p','e','n','a','n','g'};
    const uint8_t badtail[] = "motorola,penang\0\0unexpected";
    int errors = 0;

    s = valid_snapshot(); errors += verify(&s, PPM_OK, "valid");
    s = valid_snapshot(); s.root_model.bytes = wrong; s.root_model.count = sizeof(wrong);
    errors += verify(&s, PPM_ERR_IDENTITY, "model mismatch");
    s = valid_snapshot(); s.root_target.bytes = wrong; s.root_target.count = sizeof(wrong);
    errors += verify(&s, PPM_ERR_IDENTITY, "target mismatch");
    s = valid_snapshot(); s.root_compatible.bytes = badlist; s.root_compatible.count = sizeof(badlist);
    errors += verify(&s, PPM_ERR_IDENTITY, "malformed compatible");
    s = valid_snapshot(); s.root_compatible.bytes = badtail; s.root_compatible.count = sizeof(badtail);
    errors += verify(&s, PPM_ERR_IDENTITY, "compatible malformed trailing data");
    s = valid_snapshot(); s.bridge_tgran16.bytes = gatebad;
    errors += verify(&s, PPM_ERR_TGRAN16_GATE, "unknown tgran");
    s = valid_snapshot(); s.bridge_tgran16.bytes = NULL;
    errors += verify(&s, PPM_ERR_TGRAN16_GATE, "missing tgran");
    s = valid_snapshot(); s.options_present = 1;
    errors += verify(&s, PPM_ERR_OPTIONS_UNSUPPORTED, "options");
    s = valid_snapshot(); s.cpu_nodes = 8;
    errors += verify(&s, PPM_ERR_CPU_NODE, "multi-cpu unsupported");
    s = valid_snapshot(); s.cpu0_reg.bytes = wrongcpu;
    errors += verify(&s, PPM_ERR_CPU_AFFINITY, "mpidr mismatch");
    s = valid_snapshot(); s.cpu0_timebase.bytes = freqbad;
    errors += verify(&s, PPM_ERR_CPU_TIMEBASE, "no timer");
    s = valid_snapshot(); s.kernel_timebase_hz = 0;
    errors += verify(&s, PPM_ERR_KERNEL_TIMEBASE, "no kernel timebase");
    s = valid_snapshot(); s.kernel_timebase_hz = 1000000;
    errors += verify(&s, PPM_ERR_KERNEL_TIMEBASE, "kernel timer mismatch");
    s = valid_snapshot(); put64(ranges + 8, 0x09000000u);
    errors += verify(&s, PPM_ERR_SOC_LAYOUT, "soc layout mismatch");
    s = valid_snapshot(); put64(gicreg + 0, 0x01000000);
    errors += verify(&s, PPM_ERR_GIC_LAYOUT, "gic oob");
    s = valid_snapshot(); put64(gicreg + 24, 0x00010000);
    errors += verify(&s, PPM_ERR_GIC_LAYOUT, "gic redistributor size");
    s = valid_snapshot(); s.gic_compatible.bytes = wrong; s.gic_compatible.count = sizeof(wrong);
    errors += verify(&s, PPM_ERR_GIC_LAYOUT, "gic incompatible");
    s = valid_snapshot(); s.abi_version = 0;
    errors += verify(&s, PPM_ERR_INPUT, "ABI version mismatch");
    s = valid_snapshot(); s.cpu0_device_type.bytes = wrong;
    errors += verify(&s, PPM_ERR_CPU_NODE, "cpu device_type mismatch");

    if (errors) return 1;
    printf("M12_2_PHYSICAL_PLATFORM_CONTRACT_HOST_PASS total=%u negatives=%u no_MMIO=1\n",
           tests, tests - 1);
    return 0;
}
