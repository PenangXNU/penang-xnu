// SPDX-License-Identifier: MIT
#include "penang_platform.h"
int main() {
    pv_snapshot s{1,1,4,0x410fd0b0,0x100022,0,0,62500000};
    pv_request q{PV_ABI_VERSION,16384,40,47};
    if (pv_qemu_validate(&s,0x226511a511,&q).status!=PV_PASS) return 1;
    s.valid=0;
    if (pv_qemu_validate(&s,0x226511a511,&q).status!=PV_BLOCKED) return 2;
    pv_region regions[]={{0x40000000,0x100000,1,0},{0x40000000,0x4000,2,0},{0x40004000,0x4000,3,0}};
    if (pv_validate_memory(regions,3,&q).status!=PV_PASS) return 3;
    if (pv_validate_memory(regions,2,&q).code!=PV_MEMORY_REQUIRED) return 4;
    q.abi_version=0;
    if (pv_validate_memory(regions,3,&q).code!=PV_ABI) return 5;
    return 0;
}
