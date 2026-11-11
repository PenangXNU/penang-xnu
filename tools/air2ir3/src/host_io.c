/* SPDX-License-Identifier: MIT */
#include "host_io.h"

#include <stdlib.h>

static void *host_alloc(void *c, size_t n) { (void)c; return malloc(n); }
static void host_free(void *c, void *p) { (void)c; free(p); }

a2i_allocator
a2i_host_allocator(void)
{
   a2i_allocator a = {host_alloc, host_free, NULL};
   return a;
}

a2i_status
a2i_read_file(const char *path, uint8_t **buf, uint64_t *len, a2i_diag *d)
{
   *buf = NULL;
   *len = 0;
   FILE *f = fopen(path, "rb");
   if (!f) {
      a2i_fail(d, A2I_E_IO, "cannot open ");
      a2i_diag_str(d, path);
      return A2I_E_IO;
   }
   if (fseek(f, 0, SEEK_END)) {
      fclose(f);
      return a2i_fail(d, A2I_E_IO, "seek failed");
   }
   long n = ftell(f);
   if (n < 0 || (unsigned long)n > A2I_MAX_INPUT || fseek(f, 0, SEEK_SET)) {
      fclose(f);
      return a2i_fail(d, A2I_E_RESOURCE, "file too large or unseekable");
   }
   uint8_t *b = malloc((size_t)n ? (size_t)n : 1);
   if (!b) {
      fclose(f);
      return a2i_fail(d, A2I_E_RESOURCE, "out of memory reading file");
   }
   if (fread(b, 1, (size_t)n, f) != (size_t)n) {
      free(b);
      fclose(f);
      return a2i_fail(d, A2I_E_IO, "short read");
   }
   fclose(f);
   *buf = b;
   *len = (uint64_t)n;
   return A2I_OK;
}

void
a2i_print_diag(FILE *f, const a2i_diag *d)
{
   fprintf(f, "%s: %s", a2i_status_name(d->code), d->msg[0] ? d->msg : "(no message)");
   if (d->has_offset)
      fprintf(f, " [at %s %llu]", d->offset_is_bits ? "bit" : "byte",
              (unsigned long long)d->offset);
   fputc('\n', f);
}

/* Length of a well-formed UTF-8 sequence at s[i..n) (RFC 3629: no overlongs,
 * no surrogates, <= U+10FFFF), or 0 if the bytes there are not one. */
static uint32_t
utf8_seq_len(const unsigned char *s, uint32_t i, uint32_t n)
{
   unsigned char c = s[i];
   uint32_t len;
   unsigned char lo = 0x80, hi = 0xBF;
   if (c >= 0xC2 && c <= 0xDF)
      len = 2;
   else if (c >= 0xE0 && c <= 0xEF) {
      len = 3;
      if (c == 0xE0)
         lo = 0xA0;
      else if (c == 0xED)
         hi = 0x9F;
   } else if (c >= 0xF0 && c <= 0xF4) {
      len = 4;
      if (c == 0xF0)
         lo = 0x90;
      else if (c == 0xF4)
         hi = 0x8F;
   } else
      return 0;
   if (len > n - i)
      return 0;
   if (s[i + 1] < lo || s[i + 1] > hi)
      return 0;
   for (uint32_t k = 2; k < len; k++)
      if (s[i + k] < 0x80 || s[i + k] > 0xBF)
         return 0;
   return len;
}

/* JSON string per RFC 8259. Bytes that are not well-formed UTF-8 are emitted
 * as U+FFFD and counted in *lossy so the caller can flag the field. */
void
a2i_json_str(FILE *o, const char *s, uint32_t n, uint32_t *lossy)
{
   const unsigned char *u = (const unsigned char *)s;
   fputc('"', o);
   for (uint32_t i = 0; i < n;) {
      unsigned char c = u[i];
      if (c == '"' || c == '\\') {
         fputc('\\', o);
         fputc(c, o);
         i++;
      } else if (c < 0x20 || c == 0x7F) {
         fprintf(o, "\\u%04x", c);
         i++;
      } else if (c < 0x80) {
         fputc(c, o);
         i++;
      } else {
         uint32_t len = utf8_seq_len(u, i, n);
         if (len) {
            fwrite(u + i, 1, len, o);
            i += len;
         } else {
            fputs("\\ufffd", o);
            if (lossy)
               (*lossy)++;
            i++;
         }
      }
   }
   fputc('"', o);
}

void
a2i_hex(FILE *f, const uint8_t *p, size_t n)
{
   for (size_t i = 0; i < n; i++)
      fprintf(f, "%02x", p[i]);
}

a2i_status
a2i_load_entry(const char *path, const char *entry, bool check_policy, a2i_loaded *L, a2i_diag *d)
{
   a2i_memset(L, 0, sizeof(*L));
   a2i_status st = a2i_read_file(path, &L->file, &L->file_len, d);
   if (st)
      return st;
   a2i_arena_init(&L->arena, a2i_host_allocator(), 512u << 20);
   if ((st = a2i_mtlb_parse(L->file, L->file_len, &L->arena, &L->mtlb, d)))
      return st;
   if (check_policy && (st = a2i_mtlb_check_header_policy(&L->mtlb, d)))
      return st;
   const char *sel = entry ? entry : "0";
   if ((st = a2i_mtlb_select(&L->mtlb, sel, &L->index, d)))
      return st;
   if ((st = a2i_mtlb_check_entry(&L->mtlb, L->index, d)))
      return st;
   const a2i_mtlb_function *fn = &L->mtlb.functions[L->index];
   return a2i_module_parse(fn->bitcode, fn->mdsz, fn->bitcode_file_offset, &L->arena, &L->module, d);
}

void
a2i_loaded_free(a2i_loaded *L)
{
   a2i_arena_release(&L->arena);
   free(L->file);
   L->file = NULL;
}

void
a2i_print_type(FILE *out, const a2i_module *m, uint32_t id)
{
   const a2i_type *t = a2i_type_get(m, id);
   if (!t) {
      fprintf(out, "?");
      return;
   }
   switch (t->kind) {
   case A2I_T_VOID: fprintf(out, "void"); break;
   case A2I_T_HALF: fprintf(out, "half"); break;
   case A2I_T_FLOAT: fprintf(out, "float"); break;
   case A2I_T_DOUBLE: fprintf(out, "double"); break;
   case A2I_T_LABEL: fprintf(out, "label"); break;
   case A2I_T_METADATA: fprintf(out, "metadata"); break;
   case A2I_T_INT: fprintf(out, "i%u", t->bits); break;
   case A2I_T_PTR:
      if (t->addrspace)
         fprintf(out, "ptr addrspace(%u)", t->addrspace);
      else
         fprintf(out, "ptr");
      break;
   case A2I_T_VECTOR:
      fprintf(out, "<%llu x ", (unsigned long long)t->count);
      a2i_print_type(out, m, t->elem);
      fprintf(out, ">");
      break;
   case A2I_T_ARRAY:
      fprintf(out, "[%llu x ", (unsigned long long)t->count);
      a2i_print_type(out, m, t->elem);
      fprintf(out, "]");
      break;
   case A2I_T_STRUCT:
      fprintf(out, "%s{", t->packed ? "<" : "");
      for (uint32_t i = 0; i < t->nmembers; i++) {
         if (i)
            fprintf(out, ", ");
         a2i_print_type(out, m, t->members[i]);
      }
      fprintf(out, "}%s", t->packed ? ">" : "");
      break;
   case A2I_T_FUNCTION:
      a2i_print_type(out, m, t->ret);
      fprintf(out, " (");
      for (uint32_t i = 0; i < t->nmembers; i++) {
         if (i)
            fprintf(out, ", ");
         a2i_print_type(out, m, t->members[i]);
      }
      fprintf(out, "%s)", t->vararg ? ", ..." : "");
      break;
   case A2I_T_OPAQUE: fprintf(out, "opaque"); break;
   default: fprintf(out, "ty%u", id); break;
   }
}

void
a2i_dump_ir(FILE *out, const a2i_module *m)
{
   fprintf(out, "triple %s\n", m->triple ? m->triple : "");
   fprintf(out, "datalayout %s\n", m->datalayout ? m->datalayout : "");
   fprintf(out, "types %u\n", m->types.len);
   for (uint32_t fi = 0; fi < m->functions.len; fi++) {
      const a2i_function *f = &A2I_VEC_AT(&m->functions, a2i_function, fi);
      if (!f->has_body)
         continue;
      fprintf(out, "define ");
      a2i_print_type(out, m, a2i_type_get(m, f->type)->ret);
      fprintf(out, " @%s(", a2i_function_name(m, f));
      const a2i_type *ft = a2i_type_get(m, f->type);
      for (uint32_t i = 0; i < ft->nmembers; i++) {
         if (i)
            fprintf(out, ", ");
         a2i_print_type(out, m, ft->members[i]);
      }
      fprintf(out, ") blocks=%u\n", f->nblocks);
      for (uint32_t k = 0; k < f->insts.len; k++) {
         const a2i_inst *in = &A2I_VEC_AT(&f->insts, a2i_inst, k);
         bool fp = in->nops && a2i_type_is_fp(m, a2i_value_type(m, f, in->ops[0]));
         fprintf(out, "  [%u] bb%u %-14s ", k, in->bb, a2i_inst_mnemonic(in, fp));
         if (in->type != A2I_NONE)
            a2i_print_type(out, m, in->type);
         if (in->code == A2I_FC_CMP || in->code == A2I_FC_CMP2)
            fprintf(out, " %s", a2i_cmp_name(in->op));
         fprintf(out, " ops[");
         for (uint32_t o = 0; o < in->nops; o++)
            fprintf(out, "%s%%%u", o ? "," : "", in->ops[o]);
         fprintf(out, "]");
         if (in->nidx) {
            fprintf(out, " idx[");
            for (uint32_t o = 0; o < in->nidx; o++)
               fprintf(out, "%s%llu", o ? "," : "", (unsigned long long)in->idx[o]);
            fprintf(out, "]");
         }
         if (in->code == A2I_FC_GEP || in->code == A2I_FC_LOAD || in->code == A2I_FC_CALL) {
            fprintf(out, " T=");
            a2i_print_type(out, m, in->xtype);
         }
         if (in->code == A2I_FC_CALL && in->nops) {
            const a2i_value *cv = a2i_value_get(m, f, in->ops[0]);
            if (cv && cv->kind == A2I_V_FUNCTION)
               fprintf(out, " callee=@%s", A2I_VEC_AT(&m->globals, a2i_global, cv->ref).name);
            else
               fprintf(out, " callee=indirect");
         }
         fputc('\n', out);
      }
   }
   /* named metadata, in module order (compared against llvm-dis by the O1 oracle) */
   for (uint32_t i = 0; i < m->named.len; i++) {
      const a2i_named_md *nm = &A2I_VEC_AT(&m->named, a2i_named_md, i);
      fprintf(out, "named !%s ops=%u\n", nm->name ? nm->name : "", nm->nops);
   }
}

/* ---- record-level dump mirroring llvm-bcanalyzer block/record structure ---- */
static int
dump_block(a2i_bs *bs, FILE *out, int indent)
{
   a2i_bs_entry e;
   for (;;) {
      a2i_status st = a2i_bs_next(bs, &e);
      if (st)
         return -1;
      if (e.kind == A2I_BS_END)
         return 0;
      if (e.kind == A2I_BS_SUBBLOCK) {
         fprintf(out, "%*sBLOCK %u\n", indent, "", e.id);
         if (e.id == 0) {
            if (a2i_bs_read_blockinfo(bs))
               return -1;
         } else {
            if (a2i_bs_enter(bs, e.id) || dump_block(bs, out, indent + 2))
               return -1;
         }
         continue;
      }
      fprintf(out, "%*sREC %u n=%u", indent, "", e.id, e.nops);
      uint32_t lim = e.nops < 24 ? e.nops : 24;
      for (uint32_t i = 0; i < lim; i++)
         fprintf(out, " %llu", (unsigned long long)e.ops[i]);
      if (e.nops > lim)
         fprintf(out, " ...");
      if (e.has_blob)
         fprintf(out, " blob=%u", e.blob_len);
      fputc('\n', out);
   }
}

int
a2i_dump_records(FILE *out, const uint8_t *bc, uint64_t len, uint64_t file_off, a2i_diag *d)
{
   a2i_arena ar;
   a2i_arena_init(&ar, a2i_host_allocator(), 256u << 20);
   a2i_bs bs;
   int rc = -1;
   if (a2i_bs_init(&bs, bc, len, file_off, &ar, d) == A2I_OK) {
      a2i_bs_entry e;
      rc = 0;
      for (;;) {
         if (a2i_bs_next(&bs, &e)) {
            rc = -1;
            break;
         }
         if (e.kind == A2I_BS_END)
            break;
         fprintf(out, "BLOCK %u\n", e.id);
         if (e.id == 0) {
            if (a2i_bs_read_blockinfo(&bs)) {
               rc = -1;
               break;
            }
         } else if (a2i_bs_enter(&bs, e.id) || dump_block(&bs, out, 2)) {
            rc = -1;
            break;
         }
      }
   }
   a2i_arena_release(&ar);
   return rc;
}
