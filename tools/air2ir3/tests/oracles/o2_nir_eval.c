/* SPDX-License-Identifier: MIT */
/*
 * o2_nir_eval.c - O2 oracle, NIR side: evaluates the NIR that air_to_nir
 * produces for one MVP-AIR-0 entry on concrete inputs, using Mesa's own NIR
 * constant evaluator (nir_opt_constant_folding over nir_eval_const_opcode),
 * so the result reflects NIR semantics, not air2ir3 arithmetic.
 *
 *   a2i-o2-nireval FILE.metallib SPEC [--entry NAME|IDX]
 *
 * SPEC (text, one item per line, words are hex IEEE-754 bit patterns):
 *   in ARGNO W0 [W1 W2 W3]   value of a stage-input argument
 *   buf LOCATION W0 W1 ...   dwords of the constant buffer bound at LOCATION
 * Output, one line per output binding, in return order:
 *   out INDEX W0 [W1 W2 W3]  or  out INDEX unevaluated
 * Exit: 0 evaluated, 1 evaluation failure, 2 setup/usage, else the a2i status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "air_to_nir.h"
#include "host_io.h"
#include "ir3_emit.h"

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

#define MAXW 1024

typedef struct spec {
   bool have_in[A2I_MAX_BINDINGS];
   uint32_t in[A2I_MAX_BINDINGS][4];
   uint32_t nin[A2I_MAX_BINDINGS];
   bool have_buf[A2I_MAX_BINDINGS];
   uint32_t buf[A2I_MAX_BINDINGS][MAXW];
   uint32_t nbuf[A2I_MAX_BINDINGS];
} spec;

static int
read_spec(const char *path, spec *s)
{
   FILE *f = fopen(path, "r");
   if (!f)
      return -1;
   char line[16384];
   while (fgets(line, sizeof(line), f)) {
      char kind[8];
      unsigned idx;
      int pos;
      if (sscanf(line, " %7s %u%n", kind, &idx, &pos) != 2 || idx >= A2I_MAX_BINDINGS)
         continue;
      bool isin = !strcmp(kind, "in");
      if (!isin && strcmp(kind, "buf"))
         continue;
      uint32_t *dst = isin ? s->in[idx] : s->buf[idx];
      uint32_t cap = isin ? 4 : MAXW, n = 0;
      char *p = line + pos;
      unsigned w;
      int used;
      while (n < cap && sscanf(p, " %x%n", &w, &used) == 1) {
         dst[n++] = w;
         p += used;
      }
      if (isin) {
         s->have_in[idx] = true;
         s->nin[idx] = n;
      } else {
         s->have_buf[idx] = true;
         s->nbuf[idx] = n;
      }
   }
   fclose(f);
   return 0;
}

static nir_def *
imm_words(nir_builder *b, const uint32_t *w, unsigned n)
{
   nir_const_value v[4];
   memset(v, 0, sizeof(v));
   for (unsigned i = 0; i < n; i++)
      v[i].u32 = w[i];
   return nir_build_imm(b, n, 32, v);
}

/* Replaces stage-input loads and constant-offset UBO loads by immediates. */
static int
substitute(nir_shader *s, const a2i_nir_info *ni, const spec *sp)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(s);
   nir_builder b = nir_builder_create(impl);
   int bad = 0;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
         b.cursor = nir_before_instr(instr);
         nir_def *rep = NULL;
         if (in->intrinsic == nir_intrinsic_load_deref) {
            nir_variable *var = nir_intrinsic_get_var(in, 0);
            if (!var || var->data.mode != nir_var_shader_in)
               continue;
            for (uint32_t i = 0; i < ni->nin; i++) {
               const a2i_nir_io *io = &ni->in[i];
               if (io->kind == A2I_IN_BUFFER || (int)io->slot != var->data.location)
                  continue;
               if (!sp->have_in[io->argno] || sp->nin[io->argno] != in->def.num_components) {
                  fprintf(stderr, "spec lacks a %u-word value for input argument %u\n",
                          in->def.num_components, io->argno);
                  bad = 1;
                  break;
               }
               rep = imm_words(&b, sp->in[io->argno], in->def.num_components);
            }
         } else if (in->intrinsic == nir_intrinsic_load_ubo) {
            if (!nir_src_is_const(in->src[0]) || !nir_src_is_const(in->src[1])) {
               fprintf(stderr, "non-constant load_ubo\n");
               bad = 1;
               continue;
            }
            uint32_t ubo = nir_src_as_uint(in->src[0]), off = nir_src_as_uint(in->src[1]);
            unsigned n = in->def.num_components;
            if (ubo >= A2I_MAX_BINDINGS || !sp->have_buf[ubo] || off % 4 ||
                off / 4 + n > sp->nbuf[ubo]) {
               fprintf(stderr, "spec buffer %u lacks dwords [%u, %u)\n", ubo, off / 4, off / 4 + n);
               bad = 1;
               continue;
            }
            rep = imm_words(&b, &sp->buf[ubo][off / 4], n);
         }
         if (rep) {
            nir_def_rewrite_uses(&in->def, rep);
            nir_instr_remove(instr);
         }
      }
   }
   return bad;
}

static void
fold(nir_shader *s)
{
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_copy_prop);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_dce);
   } while (progress);
}

int
main(int argc, char **argv)
{
   if (argc != 3 && !(argc == 5 && !strcmp(argv[3], "--entry"))) {
      fprintf(stderr, "usage: %s FILE.metallib SPEC [--entry NAME|IDX]\n", argv[0]);
      return 2;
   }
   static spec sp;
   if (read_spec(argv[2], &sp)) {
      fprintf(stderr, "cannot read %s\n", argv[2]);
      return 2;
   }
   a2i_diag d;
   a2i_diag_clear(&d);
   a2i_loaded L;
   a2i_layout lay;
   a2i_iface io;
   a2i_subset_report rep;
   a2i_nir_info ni;
   a2i_ir3 *x = NULL;
   nir_shader *s = NULL;
   a2i_status st = a2i_load_entry(argv[1], argc == 5 ? argv[4] : NULL, true, &L, &d);
   const a2i_mtlb_function *fn = st ? NULL : &L.mtlb.functions[L.index];
   if (!st)
      st = a2i_layout_parse(L.module.datalayout, &lay, &d);
   if (!st)
      st = a2i_iface_extract(&L.module, &io, &d);
   if (!st)
      st = a2i_subset_check(&L.module, &io, &lay, fn->name, fn->name_len, &rep, &d);
   if (!st)
      st = a2i_ir3_open(&x, A2I_A619_CHIP_ID, &d);
   if (!st)
      st = a2i_air_to_nir(&L.module, &io, &lay, fn->name, a2i_ir3_nir_options(x), &s, &ni, &d);
   if (st) {
      a2i_print_diag(stderr, &d);
      a2i_ir3_close(x);
      a2i_loaded_free(&L);
      return st;
   }
   int rc = substitute(s, &ni, &sp);
   if (!rc) {
      fold(s);
      nir_function_impl *impl = nir_shader_get_entrypoint(s);
      for (uint32_t i = 0; i < ni.nout; i++) {
         bool done = false;
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;
               nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
               if (in->intrinsic != nir_intrinsic_store_deref)
                  continue;
               nir_variable *var = nir_intrinsic_get_var(in, 0);
               if (!var || var->data.mode != nir_var_shader_out ||
                   var->data.location != (int)ni.out[i].slot)
                  continue;
               nir_const_value *cv = nir_src_as_const_value(in->src[1]);
               printf("out %u", i);
               if (!cv) {
                  printf(" unevaluated\n");
                  rc = 1;
               } else {
                  for (unsigned c = 0; c < in->src[1].ssa->num_components; c++)
                     printf(" %08x", cv[c].u32);
                  printf("\n");
               }
               done = true;
            }
         }
         if (!done) {
            printf("out %u unevaluated\n", i);
            rc = 1;
         }
      }
   }
   ralloc_free(s);
   a2i_ir3_close(x);
   a2i_loaded_free(&L);
   return rc;
}
