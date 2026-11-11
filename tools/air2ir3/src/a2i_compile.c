/* SPDX-License-Identifier: MIT */
/*
 * a2i_compile.c - `air2ir3 compile FILE OUT_PREFIX [--entry NAME|IDX]`
 *
 *   container + bitstream + module   (freestanding frontend, a2i_*.c)
 *   -> MVP-AIR-0 gate                (a2i_iface_extract, a2i_subset_check)
 *   -> air_to_nir                    (Mesa NIR)
 *   -> ir3                           (Mesa Freedreno backend, Adreno 619 / a6xx)
 *
 * Outputs (written only after every stage succeeded; nothing on failure):
 *   OUT_PREFIX.bin   encoded ir3 machine code of the variant
 *   OUT_PREFIX.dis   Mesa isaspec decode of .bin
 *   OUT_PREFIX.nir   NIR before and after the ir3 lowering
 *   OUT_PREFIX.json  provenance, bindings, ir3 linkage tables, hashes
 * Exit status: the a2i_status of the first failing layer (E_IO, E_CONTAINER,
 * E_INTEGRITY, E_VERSION -> container; E_BITSTREAM -> bitstream/module;
 * E_UNSUPPORTED_IR/E_UNSUPPORTED_BINDING -> frontend; E_NIR -> NIR;
 * E_BACKEND -> ir3). A host compile never implies GPU execution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "air_to_nir.h"
#include "host_io.h"
#include "ir3_emit.h"

#ifndef A2I_MESA_PIN
#error "A2I_MESA_PIN must name the pinned Mesa commit"
#endif

int a2i_compile_main(int argc, char **argv);

typedef struct membuf {
   char *p;
   size_t n;
   FILE *f;
} membuf;

static bool
mb_open(membuf *m)
{
   m->p = NULL;
   m->n = 0;
   m->f = open_memstream(&m->p, &m->n);
   return m->f != NULL;
}

static void
mb_close(membuf *m)
{
   if (m->f)
      fclose(m->f);
   m->f = NULL;
}

static void
mb_free(membuf *m)
{
   mb_close(m);
   free(m->p);
   m->p = NULL;
}

static void
hex32(FILE *f, const uint8_t h[32])
{
   for (int i = 0; i < 32; i++)
      fprintf(f, "%02x", h[i]);
}

static const char *
in_kind(uint8_t k)
{
   return k == A2I_IN_VERTEX_INPUT ? "vertex_input" : k == A2I_IN_BUFFER ? "buffer" : "fragment_input";
}

static const char *
out_kind(uint8_t k)
{
   return k == A2I_OUT_POSITION ? "position" : k == A2I_OUT_VERTEX_OUTPUT ? "vertex_output"
                                                                         : "render_target";
}

static const char *
basename_of(const char *p)
{
   const char *s = strrchr(p, '/');
   return s ? s + 1 : p;
}

static void
write_json(FILE *j, const char *path, const a2i_loaded *L, const a2i_iface *io,
           const a2i_subset_report *rep, const a2i_nir_info *ni, const a2i_ir3 *x,
           const a2i_ir3_out *o)
{
   const a2i_mtlb_function *fn = &L->mtlb.functions[L->index];
   uint8_t h[32];
   fprintf(j, "{\n  \"tool\": \"air2ir3 compile\",\n");
   fprintf(j, "  \"classification\": \"FRONTEND_TO_IR3_HOST_COMPILE\",\n");
   fprintf(j, "  \"gpu_executed\": false,\n");
   fprintf(j, "  \"target\": {\"gpu\": ");
   a2i_json_str(j, a2i_ir3_gpu_name(x), (uint32_t)strlen(a2i_ir3_gpu_name(x)), NULL);
   fprintf(j, ", \"chip_id\": \"0x%08llx\", \"compiler_gen\": %u, \"mesa_commit\": \"%s\"},\n",
           (unsigned long long)a2i_ir3_chip_id(x), a2i_ir3_gen(x), A2I_MESA_PIN);
   fprintf(j, "  \"input\": {\"file\": ");
   a2i_json_str(j, basename_of(path), (uint32_t)strlen(basename_of(path)), NULL);
   fprintf(j, ", \"entry\": ");
   a2i_json_str(j, fn->name, fn->name_len, NULL);
   fprintf(j, ", \"index\": %u, \"stage\": \"%s\", \"air\": \"%u.%u\", \"msl\": \"%u.%u\", "
              "\"bitcode_sha256\": \"",
           L->index, io->stage == A2I_STAGE_VERTEX ? "vertex" : "fragment", fn->air_major,
           fn->air_minor, fn->lang_major, fn->lang_minor);
   hex32(j, fn->hash);
   fprintf(j, "\"},\n");
   fprintf(j, "  \"frontend\": {\"subset\": \"MVP-AIR-0\", \"llvm_instructions\": %u},\n",
           rep->insts);
   fprintf(j, "  \"nir\": {\"fp_alu_strict\": %u, \"fp_alu_relaxed\": %u, \"ubo_loads\": %u, "
              "\"num_ubos\": %u},\n",
           ni->alu_strict, ni->alu_relaxed, ni->ubo_loads, ni->num_ubos);
   fprintf(j, "  \"bindings\": {\"varying_linkage\": \"declaration_order\",\n    \"inputs\": [");
   for (uint32_t i = 0; i < ni->nin; i++) {
      const a2i_nir_io *b = &ni->in[i];
      fprintf(j, "%s\n      {\"arg\": %u, \"kind\": \"%s\", \"location\": %u, \"slot\": %u, "
                 "\"slot_name\": ",
              i ? "," : "", b->argno, in_kind(b->kind), b->location, b->slot);
      a2i_json_str(j, b->slot_name, (uint32_t)strlen(b->slot_name), NULL);
      fprintf(j, ", \"ncomp\": %u, \"unused\": %s", b->ncomp, b->unused ? "true" : "false");
      if (b->kind == A2I_IN_FRAGMENT_INPUT) {
         fprintf(j, ", \"interpolation\": \"%s\", \"link\": ",
                 b->interp == A2I_INTERP_NO_PERSPECTIVE ? "noperspective" : "perspective");
         a2i_json_str(j, (const char *)b->user.s, b->user.n, NULL);
      }
      fputc('}', j);
   }
   fprintf(j, "],\n    \"outputs\": [");
   for (uint32_t i = 0; i < ni->nout; i++) {
      const a2i_nir_io *b = &ni->out[i];
      fprintf(j, "%s\n      {\"index\": %u, \"kind\": \"%s\", \"slot\": %u, \"slot_name\": ",
              i ? "," : "", i, out_kind(b->kind), b->slot);
      a2i_json_str(j, b->slot_name, (uint32_t)strlen(b->slot_name), NULL);
      fprintf(j, ", \"ncomp\": %u", b->ncomp);
      if (b->kind == A2I_OUT_VERTEX_OUTPUT) {
         fprintf(j, ", \"link\": ");
         a2i_json_str(j, (const char *)b->user.s, b->user.n, NULL);
      }
      if (b->kind == A2I_OUT_RENDER_TARGET)
         fprintf(j, ", \"render_target\": %u", b->location);
      fputc('}', j);
   }
   fprintf(j, "]},\n");
   fprintf(j, "  \"ir3\": {\"sizedwords\": %u, \"instrs_count\": %u, \"instrlen\": %u, "
              "\"max_reg\": %d, \"max_half_reg\": %d, \"constlen\": %u, \"binning_variant\": %s, "
              "\"binning_size\": %u,\n",
           o->sizedwords, o->instrs_count, o->instrlen, o->max_reg, o->max_half_reg, o->constlen,
           o->has_binning ? "true" : "false", o->binning_size);
   fprintf(j, "    \"inputs\": [");
   for (uint32_t i = 0; i < o->ninputs; i++)
      fprintf(j, "%s{\"slot\": %u, \"regid\": %u, \"compmask\": %u, \"inloc\": %u, "
                 "\"sysval\": %s, \"bary\": %s, \"flat\": %s, \"half\": %s}",
              i ? ", " : "", o->inputs[i].slot, o->inputs[i].regid, o->inputs[i].compmask,
              o->inputs[i].inloc, o->inputs[i].sysval ? "true" : "false",
              o->inputs[i].bary ? "true" : "false", o->inputs[i].flat ? "true" : "false",
              o->inputs[i].half ? "true" : "false");
   fprintf(j, "],\n    \"outputs\": [");
   for (uint32_t i = 0; i < o->noutputs; i++)
      fprintf(j, "%s{\"slot\": %u, \"regid\": %u, \"half\": %s}", i ? ", " : "",
              o->outputs[i].slot, o->outputs[i].regid, o->outputs[i].half ? "true" : "false");
   fprintf(j, "],\n    \"ubo_const_ranges\": [");
   for (uint32_t i = 0; i < o->nubo_ranges; i++)
      fprintf(j, "%s{\"ubo\": %u, \"start\": %u, \"end\": %u, \"const_offset\": %u}",
              i ? ", " : "", o->ubo_ranges[i].block, o->ubo_ranges[i].start,
              o->ubo_ranges[i].end, o->ubo_ranges[i].offset);
   fprintf(j, "]},\n");
   a2i_sha256_buf(o->bin, o->size, h);
   fprintf(j, "  \"bin\": {\"size\": %u, \"sha256\": \"", o->size);
   hex32(j, h);
   fprintf(j, "\"}\n}\n");
}

static a2i_status
write_file(const char *prefix, const char *ext, const void *p, size_t n, a2i_diag *d)
{
   char path[4096];
   if ((size_t)snprintf(path, sizeof(path), "%s%s", prefix, ext) >= sizeof(path))
      return a2i_fail(d, A2I_E_IO, "output path too long");
   FILE *f = fopen(path, "wb");
   if (!f) {
      a2i_fail(d, A2I_E_IO, "cannot create ");
      a2i_diag_str(d, path);
      return A2I_E_IO;
   }
   bool ok = fwrite(p, 1, n, f) == n;
   ok = (fclose(f) == 0) && ok;
   if (!ok) {
      remove(path);
      a2i_fail(d, A2I_E_IO, "write failed: ");
      a2i_diag_str(d, path);
      return A2I_E_IO;
   }
   return A2I_OK;
}

static void
remove_outputs(const char *prefix)
{
   static const char *const exts[] = {".bin", ".dis", ".nir", ".json"};
   char path[4096];
   for (unsigned i = 0; i < 4; i++)
      if ((size_t)snprintf(path, sizeof(path), "%s%s", prefix, exts[i]) < sizeof(path))
         remove(path);
}

int
a2i_compile_main(int argc, char **argv)
{
   if (argc < 4 || argv[3][0] == '-') {
      fprintf(stderr, "usage: air2ir3 compile FILE OUT_PREFIX [--entry NAME|IDX]\n");
      return A2I_E_USAGE;
   }
   const char *path = argv[2], *prefix = argv[3], *entry = NULL;
   for (int i = 4; i < argc; i++) {
      if (!strcmp(argv[i], "--entry") && i + 1 < argc)
         entry = argv[++i];
      else {
         fprintf(stderr, "usage: air2ir3 compile FILE OUT_PREFIX [--entry NAME|IDX]\n");
         return A2I_E_USAGE;
      }
   }

   a2i_diag d;
   a2i_diag_clear(&d);
   a2i_loaded L;
   a2i_layout lay;
   a2i_iface io;
   a2i_subset_report rep;
   a2i_nir_info ni;
   a2i_ir3_out o;
   a2i_ir3 *x = NULL;
   struct nir_shader *nir = NULL;
   membuf dis = {0}, nirtxt = {0}, js = {0};
   memset(&o, 0, sizeof(o));

   a2i_status st = a2i_load_entry(path, entry, true, &L, &d);
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
      st = a2i_air_to_nir(&L.module, &io, &lay, fn->name, a2i_ir3_nir_options(x), &nir, &ni, &d);
   if (!st && (!mb_open(&dis) || !mb_open(&nirtxt) || !mb_open(&js)))
      st = a2i_fail(&d, A2I_E_RESOURCE, "cannot open memory streams");
   if (!st) {
      st = a2i_ir3_compile(x, nir, nirtxt.f, &o, &d); /* consumes nir */
      nir = NULL;
   }
   if (!st) {
      a2i_ir3_disasm(o.bin, o.size, dis.f);
      write_json(js.f, path, &L, &io, &rep, &ni, x, &o);
      mb_close(&dis);
      mb_close(&nirtxt);
      mb_close(&js);
      if ((st = write_file(prefix, ".bin", o.bin, o.size, &d)) ||
          (st = write_file(prefix, ".dis", dis.p, dis.n, &d)) ||
          (st = write_file(prefix, ".nir", nirtxt.p, nirtxt.n, &d)) ||
          (st = write_file(prefix, ".json", js.p, js.n, &d)))
         remove_outputs(prefix);
   }
   if (st)
      a2i_print_diag(stderr, &d);
   else
      fprintf(stderr, "compiled %s (%s) -> %s.{bin,dis,nir,json}: %u bytes, %u instrs\n",
              fn->name, io.stage == A2I_STAGE_VERTEX ? "vertex" : "fragment", prefix, o.size,
              o.instrs_count);
   mb_free(&dis);
   mb_free(&nirtxt);
   mb_free(&js);
   a2i_ir3_out_free(&o);
   a2i_ir3_close(x);
   a2i_loaded_free(&L);
   return st;
}
