/* SPDX-License-Identifier: MIT */
/*
 * ir3_emit.c - NIR -> Freedreno ir3 for Adreno 619 (a6xx) with the pinned,
 * unmodified Mesa backend. The call sequence is the one already exercised by
 * the M7 BACKEND_ONLY component (I/O vars -> temporaries, ir3 I/O lowering,
 * ir3_finalize_nir, ir3_shader_from_nir, one variant with the default key).
 * Nothing here executes on a GPU.
 */
#include "ir3_emit.h"

#include <stdlib.h>
#include <string.h>

#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "freedreno/common/freedreno_dev_info.h"
#include "freedreno/isa/ir3-isa.h"
#include "util/ralloc.h"
#include "ir3.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"
#include "ir3_shader.h"

struct a2i_ir3 {
   struct fd_dev_id id; /* referenced by the compiler for its whole lifetime */
   struct ir3_compiler *c;
   bool glsl_ref;
};

a2i_status
a2i_ir3_open(a2i_ir3 **out, uint64_t chip_id, a2i_diag *d)
{
   *out = NULL;
   a2i_ir3 *x = calloc(1, sizeof(*x));
   if (!x)
      return a2i_fail(d, A2I_E_RESOURCE, "ir3: out of memory");
   glsl_type_singleton_init_or_ref();
   x->glsl_ref = true;
   x->id.gpu_id = 0;
   x->id.chip_id = chip_id;
   const struct fd_dev_info *info = fd_dev_info_raw(&x->id);
   if (!info) {
      a2i_ir3_close(x);
      a2i_fail(d, A2I_E_BACKEND, "ir3: Mesa has no device entry for chip ");
      a2i_diag_hex(d, chip_id);
      return A2I_E_BACKEND;
   }
   /* Every compile must run the backend: never serve a variant from, or write
    * one to, Mesa's on-disk shader cache (~/.cache/mesa_shader_cache). */
   if (setenv("MESA_SHADER_CACHE_DISABLE", "true", 1)) {
      a2i_ir3_close(x);
      return a2i_fail(d, A2I_E_RESOURCE, "ir3: cannot disable the Mesa shader cache");
   }
   x->c = ir3_compiler_create(NULL, &x->id, info, &(struct ir3_compiler_options){0});
   if (!x->c) {
      a2i_ir3_close(x);
      return a2i_fail(d, A2I_E_BACKEND, "ir3: ir3_compiler_create failed");
   }
   /* disk_cache_create() returns an inert object even when disabled; drop it
    * so ir3_disk_cache_retrieve/store see no cache at all. */
   disk_cache_destroy(x->c->disk_cache);
   x->c->disk_cache = NULL;
   unsigned gen = x->c->gen;
   if (gen != 6) {
      a2i_ir3_close(x);
      a2i_fail(d, A2I_E_BACKEND, "ir3: chip is not compiler generation 6 (a6xx), gen ");
      a2i_diag_u64(d, gen);
      return A2I_E_BACKEND;
   }
   *out = x;
   return A2I_OK;
}

const struct nir_shader_compiler_options *
a2i_ir3_nir_options(const a2i_ir3 *x)
{
   return ir3_get_compiler_options(x->c);
}

uint64_t
a2i_ir3_chip_id(const a2i_ir3 *x)
{
   return x->id.chip_id;
}

unsigned
a2i_ir3_gen(const a2i_ir3 *x)
{
   return x->c->gen;
}

const char *
a2i_ir3_gpu_name(const a2i_ir3 *x)
{
   const char *n = fd_dev_name(&x->id);
   return n ? n : "unknown";
}

a2i_status
a2i_ir3_compile(a2i_ir3 *x, nir_shader *nir, FILE *nir_log, a2i_ir3_out *out, a2i_diag *d)
{
   memset(out, 0, sizeof(*out));
   if (nir_log) {
      fprintf(nir_log, "; ---- NIR from air_to_nir (before ir3 lowering)\n");
      nir_print_shader(nir, nir_log);
   }
   nir_assign_io_var_locations(nir, nir_var_shader_in);
   nir_assign_io_var_locations(nir, nir_var_shader_out);
   ir3_nir_lower_io_vars_to_temporaries(nir);
   ir3_nir_lower_io(nir);
   ir3_finalize_nir(x->c, &(struct ir3_shader_nir_options){0}, nir);
   if (nir_log) {
      fprintf(nir_log, "; ---- NIR after ir3_finalize_nir\n");
      nir_print_shader(nir, nir_log);
   }

   struct ir3_shader *sh = ir3_shader_from_nir(x->c, nir, &(struct ir3_shader_options){0});
   if (!sh)
      return a2i_fail(d, A2I_E_BACKEND, "ir3: ir3_shader_from_nir failed");
   struct ir3_shader_key key;
   memset(&key, 0, sizeof(key));
   struct ir3_shader_variant *v = ir3_shader_create_variant(sh, &key, false);
   /* ir3_shader_create_variant() returns a ralloc root owned by the caller */
   if (!v || !v->bin || v->info.size == 0 || v->info.size % 8) {
      ralloc_free(v);
      ir3_shader_destroy(sh);
      return a2i_fail(d, A2I_E_BACKEND, "ir3: backend produced no encoded variant");
   }
   out->bin = malloc(v->info.size);
   if (!out->bin) {
      ralloc_free(v);
      ir3_shader_destroy(sh);
      return a2i_fail(d, A2I_E_RESOURCE, "ir3: out of memory");
   }
   memcpy(out->bin, v->bin, v->info.size);
   out->size = v->info.size;
   out->sizedwords = v->info.sizedwords;
   out->instrs_count = v->info.instrs_count;
   out->max_reg = v->info.max_reg;
   out->max_half_reg = v->info.max_half_reg;
   out->constlen = v->constlen;
   out->instrlen = v->instrlen;
   out->has_binning = v->binning != NULL;
   out->binning_size = v->binning ? v->binning->info.size : 0;
   out->ninputs = MIN2(v->inputs_count, A2I_IR3_MAX_IO);
   for (uint32_t i = 0; i < out->ninputs; i++) {
      out->inputs[i] = (a2i_ir3_in){
         .slot = v->inputs[i].slot, .regid = v->inputs[i].regid,
         .compmask = v->inputs[i].compmask, .inloc = v->inputs[i].inloc,
         .sysval = v->inputs[i].sysval, .bary = v->inputs[i].bary,
         .flat = v->inputs[i].flat, .half = v->inputs[i].half,
      };
   }
   out->noutputs = MIN2(v->outputs_count, A2I_IR3_MAX_IO);
   for (uint32_t i = 0; i < out->noutputs; i++)
      out->outputs[i] = (a2i_ir3_outp){.slot = v->outputs[i].slot,
                                       .regid = v->outputs[i].regid,
                                       .half = v->outputs[i].half};
   const struct ir3_ubo_analysis_state *us = &ir3_const_state(v)->ubo_state;
   for (uint32_t i = 0; i < us->num_enabled && out->nubo_ranges < 8; i++) {
      if (us->range[i].ubo.bindless || us->range[i].ubo.global) {
         ralloc_free(v);
         ir3_shader_destroy(sh);
         a2i_ir3_out_free(out);
         return a2i_fail(d, A2I_E_BACKEND, "ir3: unexpected bindless/global UBO range");
      }
      out->ubo_ranges[out->nubo_ranges].block = us->range[i].ubo.block;
      out->ubo_ranges[out->nubo_ranges].start = us->range[i].start;
      out->ubo_ranges[out->nubo_ranges].end = us->range[i].end;
      out->ubo_ranges[out->nubo_ranges].offset = us->range[i].offset;
      out->nubo_ranges++;
   }
   ralloc_free(v);
   ir3_shader_destroy(sh);
   return A2I_OK;
}

void
a2i_ir3_disasm(const uint8_t *bin, uint32_t size, FILE *f)
{
   /* gpu_id 600 selects the a6xx encodings, as in the M7 verified decode. */
   ir3_isa_disasm((void *)bin, (int)size, f,
                  &(struct isa_decode_options){.gpu_id = 600, .show_errors = true});
}

void
a2i_ir3_out_free(a2i_ir3_out *o)
{
   free(o->bin);
   o->bin = NULL;
}

void
a2i_ir3_close(a2i_ir3 *x)
{
   if (!x)
      return;
   if (x->c)
      ir3_compiler_destroy(x->c);
   if (x->glsl_ref)
      glsl_type_singleton_decref();
   free(x);
}
