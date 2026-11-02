/* SPDX-License-Identifier: Apache-2.0 */
#include "penang_platform.h"
static pv_result result(pv_status s, pv_code c, uint32_t pa, uint32_t granule) {
 pv_result r={(uint32_t)s,(uint32_t)c,pa,granule}; return r;
}
pv_result pv_validate_translation(const pv_snapshot *s, uint64_t tcr, const pv_request *q) {
 static const uint32_t pa_widths[]={32,36,40,42,44,48,52};
 if (!s || !q) return result(PV_FAIL,PV_NULL,0,0);
 if (q->abi_version!=PV_ABI_VERSION || q->granule!=16384 || q->pa_bits!=40 || q->va_bits!=47)
  return result(PV_FAIL,PV_ABI,0,0);
 if (s->valid!=1) return result(PV_BLOCKED,PV_FEATURE_UNKNOWN,0,0);
 if (s->current_el!=4) return result(PV_FAIL,PV_EXCEPTION_LEVEL,0,0);
 if (s->sctlr_el1&1) return result(PV_FAIL,PV_MMU_STATE,0,0);
 uint32_t granule=(uint32_t)((s->id_aa64mmfr0_el1>>20)&15);
 if (granule==0) return result(PV_FAIL,PV_GRANULE_UNSUPPORTED,0,2);
 if (granule!=1 && granule!=2) return result(PV_BLOCKED,PV_FEATURE_UNKNOWN,0,0);
 uint32_t pa=(uint32_t)(s->id_aa64mmfr0_el1&15);
 if (pa>6) return result(PV_BLOCKED,PV_FEATURE_UNKNOWN,0,1);
 uint32_t bits=pa_widths[pa];
 if (bits<q->pa_bits || ((tcr>>32)&7)!=2) return result(PV_FAIL,PV_PA_RANGE,bits,1);
 if (((tcr>>14)&3)!=2 || ((tcr>>30)&3)!=1) return result(PV_FAIL,PV_TCR_GRANULE,bits,1);
 if ((tcr&63)!=17 || ((tcr>>16)&63)!=17) return result(PV_FAIL,PV_TCR_VA,bits,1);
 /* This slice uses traditional <=48-bit stage-1 tables, never LPA2 DS. */
 if ((tcr>>59)&1) return result(PV_FAIL,PV_TCR_MODE,bits,1);
 if (((tcr>>8)&63)!=PV_QEMU_TCR_CACHE || ((tcr>>24)&63)!=PV_QEMU_TCR_CACHE)
  return result(PV_FAIL,PV_TCR_CACHE,bits,1);
 if ((tcr & ~((uint64_t)PV_QEMU_OPTIONAL_ASID16)) != (uint64_t)PV_QEMU_TCR_BASE)
  return result(PV_FAIL,PV_TCR_MODE,bits,1);
 if ((tcr&(1ull<<36)) && ((s->id_aa64mmfr0_el1>>4)&15)!=2)
  return result(PV_FAIL,PV_ASID,bits,1);
 return result(PV_PASS,PV_OK,bits,1);
}
pv_result pv_validate_memory(const pv_region *regions, size_t count, const pv_request *q) {
 if (!q || (!regions && count)) return result(PV_FAIL,PV_NULL,0,0);
 if (q->abi_version!=PV_ABI_VERSION || q->granule!=16384 || q->pa_bits!=40)
  return result(PV_FAIL,PV_ABI,0,0);
 if (!count || count>64) return result(PV_FAIL,PV_MEMORY_REQUIRED,0,0);
 size_t ram=count;
 unsigned kernel_regions=0,boot_regions=0;
 for (size_t i=0;i<count;i++) {
  const pv_region *r=&regions[i];
  if (!r->size || r->base>UINT64_MAX-r->size) return result(PV_FAIL,PV_MEMORY_OVERFLOW,0,0);
  if ((r->base|r->size)&(q->granule-1)) return result(PV_FAIL,PV_MEMORY_ALIGNMENT,0,0);
  if (r->base+r->size>(1ull<<q->pa_bits)) return result(PV_FAIL,PV_PA_RANGE,0,0);
  if (r->reserved || r->kind<1 || r->kind>4) return result(PV_FAIL,PV_MEMORY_REQUIRED,0,0);
  if (r->kind==2) kernel_regions++;
  if (r->kind==3) boot_regions++;
  if (r->kind==1) {
   if (ram!=count) return result(PV_FAIL,PV_MEMORY_OVERLAP,0,0);
   ram=i;
  }
 }
 if (ram==count || kernel_regions!=1 || boot_regions!=1) return result(PV_FAIL,PV_MEMORY_REQUIRED,0,0);
 for (size_t i=0;i<count;i++) {
  if (i==ram) continue;
  const pv_region *r=&regions[i],*m=&regions[ram];
  if (r->base<m->base || r->base+r->size>m->base+m->size)
   return result(PV_FAIL,PV_MEMORY_CONTAINMENT,0,0);
  for (size_t j=i+1;j<count;j++) {
   if (j==ram) continue;
   if (r->base<regions[j].base+regions[j].size && regions[j].base<r->base+r->size)
    return result(PV_FAIL,PV_MEMORY_OVERLAP,0,0);
  }
 }
 return result(PV_PASS,PV_OK,q->pa_bits,1);
}
