/* SPDX-License-Identifier: MIT */
#include "a2i_module.h"

typedef struct parser {
   a2i_bs bs;
   a2i_module *m;
   a2i_diag *d;
   a2i_arena *ar;
   uint64_t declared_types;
   bool have_numentry;
} parser;

typedef struct rec {
   const uint64_t *ops;
   uint32_t n, i;
} rec;

static a2i_status
pfail(parser *p, a2i_status c, const char *msg)
{
   return a2i_fail_at(p->d, c, p->bs.file_offset * 8 + p->bs.pos, true, msg);
}

static a2i_status
pfail_code(parser *p, a2i_status c, const char *msg, uint64_t code)
{
   pfail(p, c, msg);
   a2i_diag_u64(p->d, code);
   return c;
}

static a2i_status
nomem(parser *p)
{
   return a2i_fail(p->d, A2I_E_RESOURCE, "arena exhausted while decoding the module");
}

/* --------------------------------------------------------------- lookups */
const a2i_type *
a2i_type_get(const a2i_module *m, uint32_t id)
{
   if (id >= m->types.len)
      return NULL;
   return &A2I_VEC_AT(&m->types, a2i_type, id);
}

const a2i_value *
a2i_value_get(const a2i_module *m, const a2i_function *f, uint32_t id)
{
   if (id < m->values.len)
      return &A2I_VEC_AT(&m->values, a2i_value, id);
   if (f && id >= m->n_module_values && id - m->n_module_values < f->locals.len)
      return &A2I_VEC_AT(&f->locals, a2i_value, id - m->n_module_values);
   return NULL;
}

uint32_t
a2i_value_type(const a2i_module *m, const a2i_function *f, uint32_t id)
{
   const a2i_value *v = a2i_value_get(m, f, id);
   return v ? v->type : A2I_NONE;
}

const a2i_const *
a2i_value_const(const a2i_module *m, const a2i_function *f, uint32_t id)
{
   const a2i_value *v = a2i_value_get(m, f, id);
   if (!v || v->kind != A2I_V_CONST || v->ref >= m->consts.len)
      return NULL;
   return &A2I_VEC_AT(&m->consts, a2i_const, v->ref);
}

const a2i_inst *
a2i_value_inst(const a2i_module *m, const a2i_function *f, uint32_t id)
{
   const a2i_value *v = a2i_value_get(m, f, id);
   if (!f || !v || v->kind != A2I_V_INST || v->ref >= f->insts.len)
      return NULL;
   return &A2I_VEC_AT(&f->insts, a2i_inst, v->ref);
}

const char *
a2i_function_name(const a2i_module *m, const a2i_function *f)
{
   if (f->global >= m->globals.len)
      return "";
   const char *n = A2I_VEC_AT(&m->globals, a2i_global, f->global).name;
   return n ? n : "";
}

const a2i_md *
a2i_md_get(const a2i_module *m, uint32_t id)
{
   if (id >= m->mds.len)
      return NULL;
   return &A2I_VEC_AT(&m->mds, a2i_md, id);
}

const a2i_named_md *
a2i_named_md_find(const a2i_module *m, const char *name)
{
   for (uint32_t i = 0; i < m->named.len; i++) {
      const a2i_named_md *n = &A2I_VEC_AT(&m->named, a2i_named_md, i);
      if (n->name && a2i_streq(n->name, name))
         return n;
   }
   return NULL;
}

static uint32_t
find_type(const a2i_module *m, uint8_t kind, uint32_t elem, uint64_t count, uint32_t bits)
{
   for (uint32_t i = 0; i < m->types.len; i++) {
      const a2i_type *t = &A2I_VEC_AT(&m->types, a2i_type, i);
      if (t->kind != kind)
         continue;
      if (kind == A2I_T_VECTOR && t->elem == elem && t->count == count && !t->scalable)
         return i;
      if (kind == A2I_T_INT && t->bits == bits)
         return i;
   }
   return A2I_NONE;
}

static uint32_t
find_ptr_type(const a2i_module *m, uint32_t addrspace, uint32_t pointee)
{
   uint32_t any = A2I_NONE;
   for (uint32_t i = 0; i < m->types.len; i++) {
      const a2i_type *t = &A2I_VEC_AT(&m->types, a2i_type, i);
      if (t->kind != A2I_T_PTR || t->addrspace != addrspace)
         continue;
      if (pointee == A2I_NONE || t->pointee == pointee || t->pointee == A2I_NONE)
         return i;
      if (any == A2I_NONE)
         any = i;
   }
   return any;
}

/* ------------------------------------------------------------- values */
static uint32_t
next_value_id(parser *p, a2i_function *f)
{
   return f ? p->m->n_module_values + f->locals.len : p->m->values.len;
}

static a2i_status
push_value(parser *p, a2i_function *f, uint8_t kind, uint32_t type, uint32_t ref)
{
   a2i_value *v = a2i_vec_push(p->ar, f ? &f->locals : &p->m->values);
   if (!v)
      return nomem(p);
   v->kind = kind;
   v->type = type;
   v->ref = ref;
   return A2I_OK;
}

static uint64_t *
copy_ops(parser *p, const uint64_t *ops, uint32_t n)
{
   uint64_t *o = a2i_arena_alloc(p->ar, sizeof(uint64_t) * (n ? n : 1));
   if (o && n)
      a2i_memcpy(o, ops, sizeof(uint64_t) * n);
   return o;
}

static const char *
ops_string(parser *p, const a2i_bs_entry *e, uint32_t first)
{
   return a2i_bs_ops_string(p->ar, e->ops, e->nops, first);
}

/* ------------------------------------------------------------- types */
static a2i_status
check_type_ref(parser *p, uint64_t id)
{
   if (p->have_numentry ? id >= p->declared_types : id > (1u << 20))
      return pfail(p, A2I_E_BITSTREAM, "type reference out of range");
   return A2I_OK;
}

static a2i_status
parse_types(parser *p)
{
   a2i_status st = a2i_bs_enter(&p->bs, 17);
   if (st)
      return st;
   const char *pending = NULL;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         break;
      if (e.kind == A2I_BS_SUBBLOCK) {
         if ((st = a2i_bs_skip(&p->bs)))
            return st;
         continue;
      }
      a2i_type t;
      a2i_memset(&t, 0, sizeof(t));
      t.pointee = A2I_NONE;
      t.elem = A2I_NONE;
      t.ret = A2I_NONE;
      uint32_t first_member = 0;
      switch (e.id) {
      case 1: /* NUMENTRY */
         if (e.nops < 1 || e.ops[0] > (1u << 20))
            return pfail(p, A2I_E_BITSTREAM, "bad TYPE NUMENTRY");
         p->declared_types = e.ops[0];
         p->have_numentry = true;
         continue;
      case 2: t.kind = A2I_T_VOID; break;
      case 3: t.kind = A2I_T_FLOAT; break;
      case 4: t.kind = A2I_T_DOUBLE; break;
      case 5: t.kind = A2I_T_LABEL; break;
      case 10: t.kind = A2I_T_HALF; break;
      case 16: t.kind = A2I_T_METADATA; break;
      case 22: t.kind = A2I_T_TOKEN; break;
      case 13: case 14: case 15: case 17: case 23: case 24: case 26:
         t.kind = A2I_T_OTHER;
         break;
      case 6: /* OPAQUE (named) */
         t.kind = A2I_T_OPAQUE;
         t.named = true;
         t.name = pending;
         pending = NULL;
         break;
      case 7:
         if (e.nops < 1 || e.ops[0] == 0 || e.ops[0] > (1u << 23))
            return pfail(p, A2I_E_BITSTREAM, "bad INTEGER type width");
         t.kind = A2I_T_INT;
         t.bits = (uint32_t)e.ops[0];
         break;
      case 8: /* POINTER [pointee, addrspace] */
         if (e.nops < 1 || (st = check_type_ref(p, e.ops[0])))
            return st ? st : pfail(p, A2I_E_BITSTREAM, "bad POINTER type");
         t.kind = A2I_T_PTR;
         t.pointee = (uint32_t)e.ops[0];
         t.addrspace = e.nops > 1 ? (uint32_t)e.ops[1] : 0;
         break;
      case 25: /* OPAQUE_POINTER [addrspace] */
         if (e.nops < 1 || e.ops[0] > UINT32_MAX)
            return pfail(p, A2I_E_BITSTREAM, "bad OPAQUE_POINTER type");
         t.kind = A2I_T_PTR;
         t.addrspace = (uint32_t)e.ops[0];
         break;
      case 9:  /* FUNCTION_OLD [vararg, attrid, retty, params...] */
      case 21: /* FUNCTION [vararg, retty, params...] */
         first_member = e.id == 9 ? 3 : 2;
         if (e.nops < first_member)
            return pfail(p, A2I_E_BITSTREAM, "bad FUNCTION type");
         t.kind = A2I_T_FUNCTION;
         t.vararg = e.ops[0] != 0;
         t.ret = (uint32_t)e.ops[first_member - 1];
         if ((st = check_type_ref(p, t.ret)))
            return st;
         break;
      case 11: /* ARRAY [n, elt] */
      case 12: /* VECTOR [n, elt, scalable] */
         if (e.nops < 2 || (st = check_type_ref(p, e.ops[1])))
            return st ? st : pfail(p, A2I_E_BITSTREAM, "bad ARRAY/VECTOR type");
         t.kind = e.id == 11 ? A2I_T_ARRAY : A2I_T_VECTOR;
         t.count = e.ops[0];
         t.elem = (uint32_t)e.ops[1];
         t.scalable = e.id == 12 && e.nops > 2 && e.ops[2];
         if (e.id == 12 && (t.count == 0 || t.count > 1024))
            return pfail(p, A2I_E_BITSTREAM, "VECTOR element count out of range");
         break;
      case 18: /* STRUCT_ANON [packed, elts...] */
      case 20: /* STRUCT_NAMED [packed, elts...] */
         if (e.nops < 1)
            return pfail(p, A2I_E_BITSTREAM, "bad STRUCT type");
         t.kind = A2I_T_STRUCT;
         t.packed = e.ops[0] != 0;
         first_member = 1;
         if (e.id == 20) {
            t.named = true;
            t.name = pending;
            pending = NULL;
         }
         break;
      case 19: /* STRUCT_NAME */
         pending = ops_string(p, &e, 0);
         if (!pending)
            return pfail(p, A2I_E_BITSTREAM, "bad STRUCT_NAME");
         continue;
      default:
         return pfail_code(p, A2I_E_UNSUPPORTED_IR, "unknown TYPE record code ", e.id);
      }
      if (first_member) {
         t.nmembers = e.nops - first_member;
         t.members = a2i_arena_alloc(p->ar, sizeof(uint32_t) * (t.nmembers ? t.nmembers : 1));
         if (!t.members)
            return nomem(p);
         for (uint32_t i = 0; i < t.nmembers; i++) {
            if ((st = check_type_ref(p, e.ops[first_member + i])))
               return st;
            t.members[i] = (uint32_t)e.ops[first_member + i];
         }
      }
      a2i_type *slot = a2i_vec_push(p->ar, &p->m->types);
      if (!slot)
         return nomem(p);
      *slot = t;
   }
   if (p->have_numentry && p->m->types.len != p->declared_types)
      return pfail(p, A2I_E_BITSTREAM, "TYPE table size differs from NUMENTRY");
   for (uint32_t i = 0; i < p->m->types.len; i++) {
      const a2i_type *t = &A2I_VEC_AT(&p->m->types, a2i_type, i);
      uint32_t n = p->m->types.len;
      if ((t->elem != A2I_NONE && t->elem >= n) || (t->ret != A2I_NONE && t->ret >= n) ||
          (t->pointee != A2I_NONE && t->pointee >= n))
         return pfail(p, A2I_E_BITSTREAM, "dangling type reference");
      for (uint32_t k = 0; k < t->nmembers; k++)
         if (t->members[k] >= n)
            return pfail(p, A2I_E_BITSTREAM, "dangling member type reference");
   }
   return A2I_OK;
}

/* --------------------------------------------------------- constants */
static a2i_status
parse_constants(parser *p, a2i_function *f)
{
   a2i_status st = a2i_bs_enter(&p->bs, 11);
   if (st)
      return st;
   uint32_t cur = A2I_NONE;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         return A2I_OK;
      if (e.kind == A2I_BS_SUBBLOCK) {
         if ((st = a2i_bs_skip(&p->bs)))
            return st;
         continue;
      }
      if (e.id == 1) { /* SETTYPE */
         if (e.nops < 1 || e.ops[0] >= p->m->types.len)
            return pfail(p, A2I_E_BITSTREAM, "bad CONSTANTS SETTYPE");
         cur = (uint32_t)e.ops[0];
         continue;
      }
      if (cur == A2I_NONE)
         return pfail(p, A2I_E_BITSTREAM, "constant before SETTYPE");
      if (e.id < 2 || e.id > 33)
         return pfail_code(p, A2I_E_UNSUPPORTED_IR, "unknown CONSTANTS record code ", e.id);
      a2i_const *c = a2i_vec_push(p->ar, &p->m->consts);
      if (!c)
         return nomem(p);
      uint32_t cidx = p->m->consts.len - 1;
      c->type = cur;
      c->code = e.id;
      switch (e.id) {
      case 2: c->kind = A2I_C_NULL; break;
      case 3: c->kind = A2I_C_UNDEF; break;
      case 26: c->kind = A2I_C_POISON; break;
      case 4: {
         if (e.nops < 1)
            return pfail(p, A2I_E_BITSTREAM, "INTEGER constant without value");
         uint64_t v = e.ops[0];
         c->kind = A2I_C_INT;
         if (v & 1)
            c->ival = v == 1 ? INT64_MIN : -(int64_t)(v >> 1);
         else
            c->ival = (int64_t)(v >> 1);
         break;
      }
      case 6:
         if (e.nops < 1)
            return pfail(p, A2I_E_BITSTREAM, "FLOAT constant without value");
         c->kind = A2I_C_FLOAT;
         c->bits = e.ops[0];
         break;
      case 7:
      case 22:
         c->kind = e.id == 7 ? A2I_C_AGG : A2I_C_DATA;
         c->n = e.nops;
         c->elems = copy_ops(p, e.ops, e.nops);
         if (!c->elems)
            return nomem(p);
         break;
      case 11:
         if (e.nops < 3 || e.ops[2] > UINT32_MAX)
            return pfail(p, A2I_E_BITSTREAM, "bad CE_CAST constant");
         c->kind = A2I_C_CE_CAST;
         c->cast_opc = (uint32_t)e.ops[0];
         c->cast_operand = (uint32_t)e.ops[2];
         break;
      default:
         c->kind = A2I_C_OTHER;
         c->n = e.nops;
         c->elems = copy_ops(p, e.ops, e.nops);
         if (!c->elems)
            return nomem(p);
         break;
      }
      if ((st = push_value(p, f, A2I_V_CONST, cur, cidx)))
         return st;
   }
}

/* ---------------------------------------------------------- metadata */
static a2i_status
push_md(parser *p, const a2i_md *src)
{
   a2i_md *md = a2i_vec_push(p->ar, &p->m->mds);
   if (!md)
      return nomem(p);
   *md = *src;
   return A2I_OK;
}

static a2i_status
parse_md_strings(parser *p, const a2i_bs_entry *e)
{
   if (e->nops < 2 || !e->has_blob)
      return pfail(p, A2I_E_BITSTREAM, "bad METADATA_STRINGS");
   uint64_t count = e->ops[0], offsz = e->ops[1];
   if (count > (1u << 20) || offsz > e->blob_len)
      return pfail(p, A2I_E_BITSTREAM, "METADATA_STRINGS counts exceed blob");
   const uint8_t *lens = e->blob;
   const uint8_t *chars = e->blob + offsz;
   uint64_t chars_len = e->blob_len - offsz, cpos = 0, bit = 0, end = offsz * 8;
   for (uint64_t s = 0; s < count; s++) {
      uint64_t v = 0;
      unsigned shift = 0;
      for (;;) {
         if (bit + 6 > end)
            return pfail(p, A2I_E_BITSTREAM, "METADATA_STRINGS length table truncated");
         uint64_t piece = 0;
         for (unsigned k = 0; k < 6; k++, bit++)
            piece |= (uint64_t)((lens[bit >> 3] >> (bit & 7)) & 1u) << k;
         v |= (piece & 31u) << shift;
         if (!(piece & 32u))
            break;
         shift += 5;
         if (shift > 35)
            return pfail(p, A2I_E_BITSTREAM, "METADATA_STRINGS length overflow");
      }
      if (v > chars_len - cpos)
         return pfail(p, A2I_E_BITSTREAM, "METADATA_STRINGS string exceeds blob");
      a2i_md md;
      a2i_memset(&md, 0, sizeof(md));
      md.kind = A2I_MD_STRING;
      md.code = 35;
      md.str = chars + cpos;
      md.str_len = (uint32_t)v;
      cpos += v;
      a2i_status st = push_md(p, &md);
      if (st)
         return st;
   }
   return A2I_OK;
}

static bool
md_code_defines(uint32_t code)
{
   return code == 7 || code == 8 || code == 9 || (code >= 12 && code <= 34) || code == 37 ||
          (code >= 40 && code <= 47);
}

static a2i_status
parse_metadata(parser *p)
{
   a2i_status st = a2i_bs_enter(&p->bs, 15);
   if (st)
      return st;
   const char *pending = NULL;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         break;
      if (e.kind == A2I_BS_SUBBLOCK) {
         if ((st = a2i_bs_skip(&p->bs)))
            return st;
         continue;
      }
      a2i_md md;
      a2i_memset(&md, 0, sizeof(md));
      md.code = e.id;
      switch (e.id) {
      case 35:
         if ((st = parse_md_strings(p, &e)))
            return st;
         continue;
      case 1: { /* STRING_OLD */
         const char *s = ops_string(p, &e, 0);
         if (!s)
            return pfail(p, A2I_E_BITSTREAM, "bad METADATA_STRING_OLD");
         md.kind = A2I_MD_STRING;
         md.str = (const uint8_t *)s;
         md.str_len = e.nops;
         break;
      }
      case 2: /* VALUE [ty, val] */
         if (e.nops != 2 || e.ops[0] >= p->m->types.len || e.ops[1] > UINT32_MAX)
            return pfail(p, A2I_E_BITSTREAM, "bad METADATA_VALUE");
         md.kind = A2I_MD_VALUE;
         md.type = (uint32_t)e.ops[0];
         md.value = (uint32_t)e.ops[1];
         break;
      case 3: /* NODE */
      case 5: /* DISTINCT_NODE */
         md.kind = A2I_MD_NODE;
         md.distinct = e.id == 5;
         md.nops = e.nops;
         md.ops = a2i_arena_alloc(p->ar, sizeof(uint32_t) * (e.nops ? e.nops : 1));
         if (!md.ops)
            return nomem(p);
         for (uint32_t i = 0; i < e.nops; i++) {
            if (e.ops[i] > (1u << 24))
               return pfail(p, A2I_E_BITSTREAM, "metadata node operand out of range");
            md.ops[i] = (uint32_t)e.ops[i];
         }
         break;
      case 4: /* NAME */
         pending = ops_string(p, &e, 0);
         if (!pending)
            return pfail(p, A2I_E_BITSTREAM, "bad METADATA_NAME");
         continue;
      case 10: { /* NAMED_NODE */
         if (!pending)
            return pfail(p, A2I_E_BITSTREAM, "NAMED_NODE without NAME");
         a2i_named_md *nm = a2i_vec_push(p->ar, &p->m->named);
         if (!nm)
            return nomem(p);
         nm->name = pending;
         pending = NULL;
         nm->nops = e.nops;
         nm->ops = a2i_arena_alloc(p->ar, sizeof(uint32_t) * (e.nops ? e.nops : 1));
         if (!nm->ops)
            return nomem(p);
         for (uint32_t i = 0; i < e.nops; i++) {
            if (e.ops[i] > (1u << 24))
               return pfail(p, A2I_E_BITSTREAM, "named metadata operand out of range");
            nm->ops[i] = (uint32_t)e.ops[i];
         }
         continue;
      }
      case 6: case 11: case 36: case 38: case 39:
         continue; /* KIND, ATTACHMENT, GLOBAL_DECL_ATTACHMENT, INDEX_OFFSET, INDEX */
      default:
         if (!md_code_defines(e.id))
            return pfail_code(p, A2I_E_UNSUPPORTED_IR, "unknown METADATA record code ", e.id);
         md.kind = A2I_MD_OTHER;
         break;
      }
      if ((st = push_md(p, &md)))
         return st;
   }
   uint32_t n = p->m->mds.len;
   for (uint32_t i = 0; i < n; i++) {
      const a2i_md *md = &A2I_VEC_AT(&p->m->mds, a2i_md, i);
      for (uint32_t k = 0; md->kind == A2I_MD_NODE && k < md->nops; k++)
         if (md->ops[k] > n)
            return pfail(p, A2I_E_BITSTREAM, "metadata node references undefined node");
   }
   for (uint32_t i = 0; i < p->m->named.len; i++) {
      const a2i_named_md *nm = &A2I_VEC_AT(&p->m->named, a2i_named_md, i);
      for (uint32_t k = 0; k < nm->nops; k++)
         if (nm->ops[k] >= n)
            return pfail(p, A2I_E_BITSTREAM, "named metadata references undefined node");
   }
   return A2I_OK;
}

/* ------------------------------------------------------ instructions */
static a2i_status
rec_need(parser *p, rec *r, uint32_t k)
{
   if (r->i > r->n || k > r->n - r->i)
      return pfail(p, A2I_E_BITSTREAM, "instruction record too short");
   return A2I_OK;
}

/* relative value + type (type follows only for forward references) */
static a2i_status
vtp(parser *p, a2i_function *f, rec *r, uint32_t *vid, uint32_t *ty)
{
   a2i_status st = rec_need(p, r, 1);
   if (st)
      return st;
   uint32_t next = next_value_id(p, f);
   uint32_t v = next - (uint32_t)r->ops[r->i++];
   *vid = v;
   if (v < next) {
      *ty = a2i_value_type(p->m, f, v);
   } else {
      if ((st = rec_need(p, r, 1)))
         return st;
      if (r->ops[r->i] >= p->m->types.len)
         return pfail(p, A2I_E_BITSTREAM, "forward reference with invalid type");
      *ty = (uint32_t)r->ops[r->i++];
   }
   return A2I_OK;
}

static a2i_status
val(parser *p, a2i_function *f, rec *r, uint32_t *vid)
{
   a2i_status st = rec_need(p, r, 1);
   if (st)
      return st;
   *vid = next_value_id(p, f) - (uint32_t)r->ops[r->i++];
   return A2I_OK;
}

static a2i_status
raw(parser *p, rec *r, uint64_t *v)
{
   a2i_status st = rec_need(p, r, 1);
   if (!st)
      *v = r->ops[r->i++];
   return st;
}

static const a2i_type *
tget(parser *p, uint32_t id)
{
   return a2i_type_get(p->m, id);
}

static a2i_status
need_type(parser *p, uint32_t id, const char *what)
{
   if (id == A2I_NONE || id >= p->m->types.len)
      return pfail(p, A2I_E_BITSTREAM, what);
   return A2I_OK;
}

#define MAX_INST_OPS 4096u

static a2i_status
decode_inst(parser *p, a2i_function *f, const a2i_bs_entry *e, uint32_t *cur_bb)
{
   rec r = {e->ops, e->nops, 0};
   a2i_inst in;
   a2i_memset(&in, 0, sizeof(in));
   in.code = e->id;
   in.type = A2I_NONE;
   in.value_id = A2I_NONE;
   in.xtype = A2I_NONE;
   in.bb = *cur_bb;
   uint32_t ops[MAX_INST_OPS];
   uint64_t idx[MAX_INST_OPS];
   uint32_t nops = 0, nidx = 0;
   bool defines = false, terminator = false;
   uint32_t a, b, ty, ty2;
   uint64_t x;
   a2i_status st = A2I_OK;
   const a2i_type *t;

   if (e->nops > MAX_INST_OPS)
      return pfail(p, A2I_E_RESOURCE, "instruction has too many operands");
   if (*cur_bb >= f->nblocks)
      return pfail(p, A2I_E_BITSTREAM, "instruction after the last declared basic block");

   switch (e->id) {
   case A2I_FC_BINOP:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = val(p, f, &r, &b)) || (st = raw(p, &r, &x)))
         return st;
      in.op = (uint32_t)x;
      if (r.i < r.n)
         in.flags = r.ops[r.i++];
      ops[nops++] = a;
      ops[nops++] = b;
      in.type = ty;
      defines = true;
      break;
   case A2I_FC_CAST:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = raw(p, &r, &x)))
         return st;
      in.type = (uint32_t)x;
      if ((st = raw(p, &r, &x)))
         return st;
      in.op = (uint32_t)x;
      if (r.i < r.n)
         in.flags = r.ops[r.i++];
      ops[nops++] = a;
      defines = true;
      break;
   case A2I_FC_UNOP:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = raw(p, &r, &x)))
         return st;
      in.op = (uint32_t)x;
      if (r.i < r.n)
         in.flags = r.ops[r.i++];
      ops[nops++] = a;
      in.type = ty;
      defines = true;
      break;
   case A2I_FC_GEP:
      if ((st = rec_need(p, &r, 2)))
         return st;
      in.flags = r.ops[r.i++];
      in.xtype = (uint32_t)r.ops[r.i++];
      if ((st = need_type(p, in.xtype, "GEP source element type invalid")))
         return st;
      while (r.i < r.n) {
         if ((st = vtp(p, f, &r, &a, &ty)))
            return st;
         if (nops == 0)
            in.type = ty;
         ops[nops++] = a;
      }
      if (nops == 0)
         return pfail(p, A2I_E_BITSTREAM, "GEP without base operand");
      if (in.type == A2I_NONE)
         in.type = find_ptr_type(p->m, 0, A2I_NONE);
      defines = true;
      break;
   case A2I_FC_EXTRACTELT:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = vtp(p, f, &r, &b, &ty2)))
         return st;
      t = tget(p, ty);
      if (!t || t->kind != A2I_T_VECTOR)
         return pfail(p, A2I_E_BITSTREAM, "extractelement on non-vector");
      ops[nops++] = a;
      ops[nops++] = b;
      in.type = t->elem;
      defines = true;
      break;
   case A2I_FC_INSERTELT:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = val(p, f, &r, &b)) ||
          (st = vtp(p, f, &r, &ops[2], &ty2)))
         return st;
      ops[0] = a;
      ops[1] = b;
      nops = 3;
      in.type = ty;
      defines = true;
      break;
   case A2I_FC_SHUFFLEVEC: {
      uint32_t mask;
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = val(p, f, &r, &b)) ||
          (st = vtp(p, f, &r, &mask, &ty2)))
         return st;
      const a2i_type *t1 = tget(p, ty), *tm = tget(p, ty2);
      if (!t1 || !tm || t1->kind != A2I_T_VECTOR || tm->kind != A2I_T_VECTOR)
         return pfail(p, A2I_E_BITSTREAM, "shufflevector operand types invalid");
      in.type = find_type(p->m, A2I_T_VECTOR, t1->elem, tm->count, 0);
      if (in.type == A2I_NONE)
         return pfail(p, A2I_E_BITSTREAM, "shufflevector result type absent from type table");
      ops[nops++] = a;
      ops[nops++] = b;
      ops[nops++] = mask;
      defines = true;
      break;
   }
   case A2I_FC_CMP:
   case A2I_FC_CMP2: {
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = val(p, f, &r, &b)) || (st = raw(p, &r, &x)))
         return st;
      in.op = (uint32_t)x;
      if (r.i < r.n)
         in.flags = r.ops[r.i++];
      ops[nops++] = a;
      ops[nops++] = b;
      uint32_t i1 = find_type(p->m, A2I_T_INT, 0, 0, 1);
      t = tget(p, ty);
      in.type = (t && t->kind == A2I_T_VECTOR) ? find_type(p->m, A2I_T_VECTOR, i1, t->count, 0) : i1;
      if (in.type == A2I_NONE)
         return pfail(p, A2I_E_BITSTREAM, "compare result type absent from type table");
      defines = true;
      break;
   }
   case A2I_FC_SELECT:
   case A2I_FC_VSELECT:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = val(p, f, &r, &b)))
         return st;
      ops[nops++] = a;
      ops[nops++] = b;
      if (e->id == A2I_FC_VSELECT) {
         if ((st = vtp(p, f, &r, &ops[2], &ty2)))
            return st;
         if (r.i < r.n)
            in.flags = r.ops[r.i++];
      } else if ((st = val(p, f, &r, &ops[2]))) {
         return st;
      }
      nops = 3;
      in.type = ty;
      defines = true;
      break;
   case A2I_FC_RET:
      if (r.n > 0) {
         if ((st = vtp(p, f, &r, &a, &ty)))
            return st;
         ops[nops++] = a;
      }
      terminator = true;
      break;
   case A2I_FC_BR:
      if (r.n != 1 && r.n != 3)
         return pfail(p, A2I_E_BITSTREAM, "malformed br");
      idx[nidx++] = r.ops[r.i++];
      if (r.n == 3) {
         idx[nidx++] = r.ops[r.i++];
         if ((st = val(p, f, &r, &a)))
            return st;
         ops[nops++] = a;
      }
      terminator = true;
      break;
   case A2I_FC_SWITCH:
      if ((st = rec_need(p, &r, 3)))
         return st;
      if ((r.ops[0] >> 16) == 0x4B5)
         return pfail(p, A2I_E_UNSUPPORTED_IR, "switch with case ranges is unsupported");
      in.xtype = (uint32_t)r.ops[r.i++];
      if ((st = val(p, f, &r, &a)))
         return st;
      ops[nops++] = a;
      idx[nidx++] = r.ops[r.i++];
      if ((r.n - r.i) % 2)
         return pfail(p, A2I_E_BITSTREAM, "switch case list is odd");
      while (r.i < r.n) {
         if (r.ops[r.i] > UINT32_MAX)
            return pfail(p, A2I_E_BITSTREAM, "switch case value id out of range");
         ops[nops++] = (uint32_t)r.ops[r.i++]; /* absolute value id */
         idx[nidx++] = r.ops[r.i++];
      }
      terminator = true;
      break;
   case A2I_FC_UNREACHABLE:
      terminator = true;
      break;
   case A2I_FC_PHI: {
      if ((st = rec_need(p, &r, 1)))
         return st;
      in.type = (uint32_t)r.ops[r.i++];
      if ((st = need_type(p, in.type, "phi type invalid")))
         return st;
      uint32_t pairs = (r.n - 1) / 2;
      uint32_t next = next_value_id(p, f);
      for (uint32_t k = 0; k < pairs; k++) {
         uint64_t sv = r.ops[r.i++];
         int64_t delta = (sv & 1) ? -(int64_t)(sv >> 1) : (int64_t)(sv >> 1);
         ops[nops++] = (uint32_t)((int64_t)next - delta);
         idx[nidx++] = r.ops[r.i++];
      }
      if (r.i < r.n)
         in.flags = r.ops[r.i++];
      defines = true;
      break;
   }
   case A2I_FC_ALLOCA:
      if ((st = rec_need(p, &r, 4)))
         return st;
      in.xtype = (uint32_t)r.ops[0];
      if ((st = need_type(p, in.xtype, "alloca type invalid")))
         return st;
      if (r.ops[2] > UINT32_MAX)
         return pfail(p, A2I_E_BITSTREAM, "alloca size value id out of range");
      ops[nops++] = (uint32_t)r.ops[2];
      in.align = r.ops[3];
      r.i = r.n;
      in.type = find_ptr_type(p->m, 0, in.xtype);
      if (in.type == A2I_NONE)
         return pfail(p, A2I_E_BITSTREAM, "alloca result pointer type absent");
      defines = true;
      break;
   case A2I_FC_LOAD:
   case A2I_FC_LOADATOMIC: {
      if ((st = vtp(p, f, &r, &a, &ty)))
         return st;
      uint32_t tail = e->id == A2I_FC_LOAD ? 3 : 5;
      if (r.n - r.i != tail)
         return pfail(p, A2I_E_UNSUPPORTED_IR, "load record without explicit result type");
      in.xtype = (uint32_t)r.ops[r.i++];
      if ((st = need_type(p, in.xtype, "load type invalid")))
         return st;
      in.align = r.ops[r.i++];
      in.is_volatile = r.ops[r.i++] != 0;
      r.i = r.n;
      ops[nops++] = a;
      in.type = in.xtype;
      defines = true;
      break;
   }
   case A2I_FC_STORE:
   case A2I_FC_STOREATOMIC:
      if ((st = vtp(p, f, &r, &a, &ty)) || (st = vtp(p, f, &r, &b, &ty2)) ||
          (st = rec_need(p, &r, 2)))
         return st;
      in.align = r.ops[r.i++];
      in.is_volatile = r.ops[r.i++] != 0;
      r.i = r.n;
      ops[nops++] = a;
      ops[nops++] = b;
      break;
   case A2I_FC_EXTRACTVAL:
   case A2I_FC_INSERTVAL: {
      if ((st = vtp(p, f, &r, &a, &ty)))
         return st;
      ops[nops++] = a;
      if (e->id == A2I_FC_INSERTVAL) {
         if ((st = vtp(p, f, &r, &b, &ty2)))
            return st;
         ops[nops++] = b;
      }
      uint32_t cur = ty;
      if (r.i >= r.n)
         return pfail(p, A2I_E_BITSTREAM, "aggregate access without indices");
      while (r.i < r.n) {
         uint64_t k = r.ops[r.i++];
         idx[nidx++] = k;
         t = tget(p, cur);
         if (!t)
            return pfail(p, A2I_E_BITSTREAM, "aggregate operand type invalid");
         if (t->kind == A2I_T_STRUCT && k < t->nmembers)
            cur = t->members[k];
         else if (t->kind == A2I_T_ARRAY && k < t->count)
            cur = t->elem;
         else
            return pfail(p, A2I_E_BITSTREAM, "aggregate index out of range");
      }
      in.type = e->id == A2I_FC_EXTRACTVAL ? cur : ty;
      defines = true;
      break;
   }
   case A2I_FC_CALL: {
      uint64_t cc;
      if ((st = rec_need(p, &r, 2)))
         return st;
      r.i++; /* paramattrs */
      cc = r.ops[r.i++];
      in.op = (uint32_t)cc;
      if (cc & (1u << 17)) {
         if ((st = raw(p, &r, &in.flags)))
            return st;
      }
      if (!(cc & (1u << 15)))
         return pfail(p, A2I_E_UNSUPPORTED_IR, "call record without explicit function type");
      if ((st = raw(p, &r, &x)))
         return st;
      in.xtype = (uint32_t)x;
      const a2i_type *ft = tget(p, in.xtype);
      if (!ft || ft->kind != A2I_T_FUNCTION)
         return pfail(p, A2I_E_BITSTREAM, "call function type invalid");
      if ((st = vtp(p, f, &r, &a, &ty)))
         return st;
      ops[nops++] = a;
      for (uint32_t k = 0; k < ft->nmembers; k++) {
         const a2i_type *pt = tget(p, ft->members[k]);
         if (pt && pt->kind == A2I_T_LABEL)
            return pfail(p, A2I_E_UNSUPPORTED_IR, "call with label argument");
         if ((st = val(p, f, &r, &b)))
            return st;
         ops[nops++] = b;
      }
      while (r.i < r.n) {
         if (!ft->vararg)
            return pfail(p, A2I_E_BITSTREAM, "extra call arguments for non-variadic callee");
         if ((st = vtp(p, f, &r, &b, &ty2)))
            return st;
         ops[nops++] = b;
      }
      const a2i_type *rt = tget(p, ft->ret);
      if (rt && rt->kind != A2I_T_VOID) {
         in.type = ft->ret;
         defines = true;
      }
      break;
   }
   case A2I_FC_FREEZE:
      if ((st = vtp(p, f, &r, &a, &ty)))
         return st;
      ops[nops++] = a;
      in.type = ty;
      defines = true;
      break;
   default:
      return pfail_code(p, A2I_E_UNSUPPORTED_IR, "unsupported instruction record code ", e->id);
   }

   if (defines && (in.type == A2I_NONE || in.type >= p->m->types.len))
      return pfail(p, A2I_E_BITSTREAM, "instruction result type unresolved");
   in.nops = nops;
   in.nidx = nidx;
   in.ops = a2i_arena_alloc(p->ar, sizeof(uint32_t) * (nops ? nops : 1));
   in.idx = a2i_arena_alloc(p->ar, sizeof(uint64_t) * (nidx ? nidx : 1));
   if (!in.ops || !in.idx)
      return nomem(p);
   a2i_memcpy(in.ops, ops, sizeof(uint32_t) * nops);
   a2i_memcpy(in.idx, idx, sizeof(uint64_t) * nidx);
   if (defines)
      in.value_id = next_value_id(p, f);
   a2i_inst *slot = a2i_vec_push(p->ar, &f->insts);
   if (!slot)
      return nomem(p);
   *slot = in;
   if (defines && (st = push_value(p, f, A2I_V_INST, in.type, f->insts.len - 1)))
      return st;
   if (terminator)
      (*cur_bb)++;
   return A2I_OK;
}

static a2i_status
parse_function_body(parser *p)
{
   a2i_module *m = p->m;
   if (m->bodies_seen == 0)
      m->n_module_values = m->values.len;
   a2i_function *f = NULL;
   uint32_t k = 0;
   for (uint32_t i = 0; i < m->functions.len; i++) {
      a2i_function *c = &A2I_VEC_AT(&m->functions, a2i_function, i);
      if (!c->is_proto && k++ == m->bodies_seen) {
         f = c;
         break;
      }
   }
   if (!f)
      return pfail(p, A2I_E_BITSTREAM, "FUNCTION_BLOCK without a matching definition");
   m->bodies_seen++;
   f->has_body = true;
   const a2i_type *ft = a2i_type_get(m, f->type);
   f->nargs = ft->nmembers;
   a2i_vec_init(&f->locals, sizeof(a2i_value));
   a2i_vec_init(&f->insts, sizeof(a2i_inst));
   a2i_status st;
   for (uint32_t i = 0; i < f->nargs; i++)
      if ((st = push_value(p, f, A2I_V_ARG, ft->members[i], i)))
         return st;
   if ((st = a2i_bs_enter(&p->bs, 12)))
      return st;
   uint32_t cur_bb = 0;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         break;
      if (e.kind == A2I_BS_SUBBLOCK) {
         st = e.id == 11 ? parse_constants(p, f) : a2i_bs_skip(&p->bs);
         if (st)
            return st;
         continue;
      }
      if (e.id == A2I_FC_DECLAREBLOCKS) {
         if (e.nops < 1 || e.ops[0] == 0 || e.ops[0] > (1u << 20) || f->nblocks)
            return pfail(p, A2I_E_BITSTREAM, "bad DECLAREBLOCKS");
         f->nblocks = (uint32_t)e.ops[0];
         continue;
      }
      if (e.id == A2I_FC_DEBUG_LOC || e.id == A2I_FC_DEBUG_LOC_AGAIN ||
          e.id == A2I_FC_OPERAND_BUNDLE || e.id == A2I_FC_BLOCKADDR_USERS ||
          (e.id >= 61 && e.id <= 65))
         continue;
      if (!f->nblocks)
         return pfail(p, A2I_E_BITSTREAM, "instruction before DECLAREBLOCKS");
      if ((st = decode_inst(p, f, &e, &cur_bb)))
         return st;
   }
   if (cur_bb != f->nblocks)
      return pfail(p, A2I_E_BITSTREAM, "function terminator count differs from DECLAREBLOCKS");
   uint32_t total = m->n_module_values + f->locals.len;
   for (uint32_t i = 0; i < f->insts.len; i++) {
      const a2i_inst *in = &A2I_VEC_AT(&f->insts, a2i_inst, i);
      for (uint32_t k2 = 0; k2 < in->nops; k2++)
         if (in->ops[k2] >= total)
            return pfail(p, A2I_E_BITSTREAM, "operand references an undefined value");
      bool bbrefs = in->code == A2I_FC_BR || in->code == A2I_FC_SWITCH || in->code == A2I_FC_PHI;
      for (uint32_t k2 = 0; bbrefs && k2 < in->nidx; k2++)
         if (in->idx[k2] >= f->nblocks)
            return pfail(p, A2I_E_BITSTREAM, "branch to an undefined basic block");
   }
   return A2I_OK;
}

/* ------------------------------------------------------------ module */
static a2i_status
add_global(parser *p, const a2i_bs_entry *e, uint8_t vkind)
{
   if (p->m->bodies_seen)
      return pfail(p, A2I_E_BITSTREAM, "global value declared after function bodies");
   if (p->m->version != 2)
      return pfail(p, A2I_E_VERSION, "global record before MODULE VERSION 2");
   if (e->nops < 3)
      return pfail(p, A2I_E_BITSTREAM, "global record too short");
   a2i_global *g = a2i_vec_push(p->ar, &p->m->globals);
   if (!g)
      return nomem(p);
   g->value_id = p->m->values.len;
   g->strtab_off = e->ops[0];
   g->strtab_size = e->ops[1];
   g->value_type = (uint32_t)e->ops[2];
   g->func = A2I_NONE;
   if (vkind == A2I_V_FUNCTION) {
      if (e->nops < 5)
         return pfail(p, A2I_E_BITSTREAM, "FUNCTION record too short");
      const a2i_type *t = a2i_type_get(p->m, g->value_type);
      if (t && t->kind == A2I_T_PTR && t->pointee != A2I_NONE) {
         g->value_type = t->pointee;
         t = a2i_type_get(p->m, g->value_type);
      }
      if (!t || t->kind != A2I_T_FUNCTION)
         return pfail(p, A2I_E_BITSTREAM, "FUNCTION record type is not a function type");
      a2i_function *fn = a2i_vec_push(p->ar, &p->m->functions);
      if (!fn)
         return nomem(p);
      fn->global = p->m->globals.len - 1;
      fn->type = g->value_type;
      fn->cc = e->ops[3];
      fn->is_proto = e->ops[4] != 0;
      fn->linkage = e->nops > 5 ? e->ops[5] : 0;
      g->is_function = true;
      g->func = p->m->functions.len - 1;
   }
   /* The value of a global is a pointer. Resolve its pointer type so that
    * instructions taking their result type from an operand (select, freeze,
    * GEP base) work when that operand is a global:
    *   GLOBALVAR [.., type, isconst, ..]: isconst bit 1 = explicit value type,
    *     then addrspace = isconst >> 2; otherwise 'type' is the pointer type.
    *   FUNCTION  [.., addrspace @18 ..] (absent in short records -> 0).
    * If the module's type table has no such pointer type the value stays
    * untyped (A2I_NONE) and any consumer needing it fails explicitly. */
   uint32_t vt = A2I_NONE;
   const a2i_type *rt = a2i_type_get(p->m, (uint32_t)e->ops[2]);
   if (vkind == A2I_V_GLOBALVAR && e->nops > 3 && (e->ops[3] & 2))
      vt = find_ptr_type(p->m, (uint32_t)(e->ops[3] >> 2), A2I_NONE);
   else if (vkind == A2I_V_GLOBALVAR && rt && rt->kind == A2I_T_PTR)
      vt = (uint32_t)e->ops[2];
   else if (vkind == A2I_V_FUNCTION)
      vt = find_ptr_type(p->m, e->nops > 18 ? (uint32_t)e->ops[18] : 0, A2I_NONE);
   return push_value(p, NULL, vkind, vt, p->m->globals.len - 1);
}

static a2i_status
parse_module_block(parser *p)
{
   a2i_status st = a2i_bs_enter(&p->bs, 8);
   if (st)
      return st;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         return A2I_OK;
      if (e.kind == A2I_BS_SUBBLOCK) {
         switch (e.id) {
         case 0: st = a2i_bs_read_blockinfo(&p->bs); break;
         case 17: st = parse_types(p); break;
         case 11:
            if (p->m->bodies_seen)
               return pfail(p, A2I_E_BITSTREAM, "module constants after function bodies");
            st = parse_constants(p, NULL);
            break;
         case 15: st = parse_metadata(p); break;
         case 12: st = parse_function_body(p); break;
         default: st = a2i_bs_skip(&p->bs); break;
         }
         if (st)
            return st;
         continue;
      }
      switch (e.id) {
      case 1:
         if (e.nops < 1)
            return pfail(p, A2I_E_BITSTREAM, "MODULE VERSION without value");
         p->m->version = e.ops[0];
         if (e.ops[0] != 2)
            return pfail_code(p, A2I_E_VERSION, "unsupported MODULE VERSION (need 2): ", e.ops[0]);
         break;
      case 2:
      case 3:
      case 16: {
         const char *s = ops_string(p, &e, 0);
         if (!s)
            return pfail(p, A2I_E_BITSTREAM, "malformed module string record");
         if (e.id == 2)
            p->m->triple = s;
         else if (e.id == 3)
            p->m->datalayout = s;
         else
            p->m->source_filename = s;
         break;
      }
      case 7: st = add_global(p, &e, A2I_V_GLOBALVAR); break;
      case 8: st = add_global(p, &e, A2I_V_FUNCTION); break;
      case 14:
      case 15: st = add_global(p, &e, A2I_V_ALIAS); break;
      default: break; /* VSTOFFSET, SECTIONNAME, GCNAME, COMDAT, HASH: no value */
      }
      if (st)
         return st;
   }
}

static a2i_status
parse_identification(parser *p)
{
   a2i_status st = a2i_bs_enter(&p->bs, 13);
   if (st)
      return st;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         return A2I_OK;
      if (e.kind == A2I_BS_SUBBLOCK) {
         if ((st = a2i_bs_skip(&p->bs)))
            return st;
         continue;
      }
      if (e.id == 1) {
         p->m->producer = ops_string(p, &e, 0);
         if (!p->m->producer)
            return pfail(p, A2I_E_BITSTREAM, "malformed IDENTIFICATION string");
      } else if (e.id == 2) {
         if (e.nops < 1)
            return pfail(p, A2I_E_BITSTREAM, "IDENTIFICATION EPOCH without value");
         p->m->has_epoch = true;
         p->m->epoch = e.ops[0];
         if (e.ops[0] != 0)
            return pfail_code(p, A2I_E_VERSION, "unsupported bitcode epoch: ", e.ops[0]);
      }
   }
}

static a2i_status
parse_strtab(parser *p)
{
   a2i_status st = a2i_bs_enter(&p->bs, 23);
   if (st)
      return st;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p->bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         return A2I_OK;
      if (e.kind == A2I_BS_SUBBLOCK) {
         if ((st = a2i_bs_skip(&p->bs)))
            return st;
         continue;
      }
      if (e.id == 1) {
         if (!e.has_blob)
            return pfail(p, A2I_E_BITSTREAM, "STRTAB without blob");
         p->m->strtab = e.blob;
         p->m->strtab_len = e.blob_len;
      }
   }
}

a2i_status
a2i_module_parse(const uint8_t *bc, uint64_t len, uint64_t file_offset, a2i_arena *ar,
                 a2i_module *m, a2i_diag *d)
{
   a2i_memset(m, 0, sizeof(*m));
   m->ar = ar;
   a2i_vec_init(&m->types, sizeof(a2i_type));
   a2i_vec_init(&m->values, sizeof(a2i_value));
   a2i_vec_init(&m->consts, sizeof(a2i_const));
   a2i_vec_init(&m->globals, sizeof(a2i_global));
   a2i_vec_init(&m->functions, sizeof(a2i_function));
   a2i_vec_init(&m->mds, sizeof(a2i_md));
   a2i_vec_init(&m->named, sizeof(a2i_named_md));
   parser p;
   a2i_memset(&p, 0, sizeof(p));
   p.m = m;
   p.d = d;
   p.ar = ar;
   a2i_status st = a2i_bs_init(&p.bs, bc, len, file_offset, ar, d);
   if (st)
      return st;
   bool seen_module = false, seen_ident = false;
   a2i_bs_entry e;
   for (;;) {
      if ((st = a2i_bs_next(&p.bs, &e)))
         return st;
      if (e.kind == A2I_BS_END)
         break;
      switch (e.id) {
      case 13:
         st = parse_identification(&p);
         seen_ident = true;
         break;
      case 8:
         if (seen_module)
            return pfail(&p, A2I_E_UNSUPPORTED_IR, "more than one MODULE_BLOCK");
         st = parse_module_block(&p);
         seen_module = true;
         break;
      case 23: st = parse_strtab(&p); break;
      case 0: st = a2i_bs_read_blockinfo(&p.bs); break;
      default: st = a2i_bs_skip(&p.bs); break;
      }
      if (st)
         return st;
   }
   if (!seen_module)
      return a2i_fail(d, A2I_E_BITSTREAM, "bitcode has no MODULE_BLOCK");
   if (!seen_ident || !m->has_epoch)
      return a2i_fail(d, A2I_E_VERSION, "bitcode lacks IDENTIFICATION epoch");
   uint32_t defs = 0;
   for (uint32_t i = 0; i < m->functions.len; i++)
      defs += !A2I_VEC_AT(&m->functions, a2i_function, i).is_proto;
   if (defs != m->bodies_seen)
      return a2i_fail(d, A2I_E_BITSTREAM, "function definitions without bodies");
   if (m->bodies_seen == 0)
      m->n_module_values = m->values.len;
   for (uint32_t i = 0; i < m->globals.len; i++) {
      a2i_global *g = &A2I_VEC_AT(&m->globals, a2i_global, i);
      if (!m->strtab || !a2i_in_bounds(g->strtab_off, g->strtab_size, m->strtab_len))
         return a2i_fail(d, A2I_E_BITSTREAM, "global name outside STRTAB");
      g->name = a2i_arena_strndup(ar, (const char *)m->strtab + g->strtab_off,
                                  (size_t)g->strtab_size);
      if (!g->name)
         return a2i_fail(d, A2I_E_RESOURCE, "arena exhausted (names)");
   }
   m->record_count = p.bs.records;
   m->block_count = p.bs.blocks;
   return A2I_OK;
}
