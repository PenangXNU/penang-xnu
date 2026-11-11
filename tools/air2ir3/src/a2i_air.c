/* SPDX-License-Identifier: MIT */
#include "a2i_air.h"

/* --------------------------------------------------------------- names */
static const char *const fp_binops[13] = {"fadd", "fsub", "fmul", NULL, "fdiv", NULL, "frem",
                                          NULL, NULL, NULL, NULL, NULL, NULL};
static const char *const int_binops[13] = {"add", "sub", "mul", "udiv", "sdiv", "urem", "srem",
                                           "shl", "lshr", "ashr", "and", "or", "xor"};
static const char *const casts[13] = {"trunc", "zext", "sext", "fptoui", "fptosi", "uitofp",
                                      "sitofp", "fptrunc", "fpext", "ptrtoint", "inttoptr",
                                      "bitcast", "addrspacecast"};
static const char *const fcmps[16] = {"false", "oeq", "ogt", "oge", "olt", "ole", "one", "ord",
                                      "uno", "ueq", "ugt", "uge", "ult", "ule", "une", "true"};
static const char *const icmps[10] = {"eq", "ne", "ugt", "uge", "ult", "ule", "sgt", "sge",
                                      "slt", "sle"};

const char *
a2i_cast_name(uint32_t op)
{
   return op < 13 ? casts[op] : "cast?";
}

const char *
a2i_cmp_name(uint32_t pred)
{
   if (pred < 16)
      return fcmps[pred];
   if (pred >= 32 && pred < 42)
      return icmps[pred - 32];
   return "pred?";
}

const char *
a2i_inst_mnemonic(const a2i_inst *in, bool is_fp)
{
   switch (in->code) {
   case A2I_FC_BINOP:
      if (in->op < 13) {
         const char *n = is_fp ? fp_binops[in->op] : int_binops[in->op];
         if (n)
            return n;
      }
      return "binop?";
   case A2I_FC_CAST: return a2i_cast_name(in->op);
   case A2I_FC_UNOP: return in->op == 0 ? "fneg" : "unop?";
   case A2I_FC_GEP: return "getelementptr";
   case A2I_FC_EXTRACTELT: return "extractelement";
   case A2I_FC_INSERTELT: return "insertelement";
   case A2I_FC_SHUFFLEVEC: return "shufflevector";
   case A2I_FC_CMP:
   case A2I_FC_CMP2: return in->op < 32 ? "fcmp" : "icmp";
   case A2I_FC_SELECT:
   case A2I_FC_VSELECT: return "select";
   case A2I_FC_RET: return "ret";
   case A2I_FC_BR: return "br";
   case A2I_FC_SWITCH: return "switch";
   case A2I_FC_UNREACHABLE: return "unreachable";
   case A2I_FC_PHI: return "phi";
   case A2I_FC_ALLOCA: return "alloca";
   case A2I_FC_LOAD:
   case A2I_FC_LOADATOMIC: return "load";
   case A2I_FC_STORE:
   case A2I_FC_STOREATOMIC: return "store";
   case A2I_FC_EXTRACTVAL: return "extractvalue";
   case A2I_FC_INSERTVAL: return "insertvalue";
   case A2I_FC_CALL: return "call";
   case A2I_FC_FREEZE: return "freeze";
   default: return "inst?";
   }
}

bool
a2i_type_is_fp(const a2i_module *m, uint32_t type)
{
   const a2i_type *t = a2i_type_get(m, type);
   if (t && t->kind == A2I_T_VECTOR)
      t = a2i_type_get(m, t->elem);
   return t && (t->kind == A2I_T_FLOAT || t->kind == A2I_T_HALF || t->kind == A2I_T_DOUBLE);
}

bool
a2i_type_is_float_vec(const a2i_module *m, uint32_t type, uint32_t *ncomp)
{
   const a2i_type *t = a2i_type_get(m, type);
   if (!t)
      return false;
   if (t->kind == A2I_T_FLOAT) {
      *ncomp = 1;
      return true;
   }
   if (t->kind == A2I_T_VECTOR && !t->scalable && t->count >= 2 && t->count <= 4) {
      const a2i_type *e = a2i_type_get(m, t->elem);
      if (e && e->kind == A2I_T_FLOAT) {
         *ncomp = (uint32_t)t->count;
         return true;
      }
   }
   return false;
}

/* -------------------------------------------------------------- layout */
static bool
dl_num(const char **p, uint32_t *v)
{
   const char *s = *p;
   uint64_t x = 0;
   if (*s < '0' || *s > '9')
      return false;
   while (*s >= '0' && *s <= '9') {
      x = x * 10 + (uint64_t)(*s - '0');
      if (x > UINT32_MAX)
         return false;
      s++;
   }
   *p = s;
   *v = (uint32_t)x;
   return true;
}

static uint32_t
dl_lookup(const a2i_layout *L, char kind, uint32_t bits)
{
   for (uint32_t i = 0; i < L->n; i++)
      if (L->spec[i].kind == kind && L->spec[i].bits == bits)
         return L->spec[i].abi;
   return 0;
}

a2i_status
a2i_layout_parse(const char *dl, a2i_layout *L, a2i_diag *d)
{
   a2i_memset(L, 0, sizeof(*L));
   L->ptr_bytes = 8;
   L->ptr_abi = 8;
   if (!dl || !*dl)
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "module has no datalayout");
   const char *s = dl;
   while (*s) {
      const char *comp = s;
      char k = *s;
      if (k == 'e' || k == 'E') {
         L->big_endian = k == 'E';
         s++;
      } else if (k == 'p') {
         uint32_t as = 0, size, abi;
         s++;
         if (*s >= '0' && *s <= '9' && !dl_num(&s, &as))
            goto bad;
         if (*s++ != ':' || !dl_num(&s, &size) || *s++ != ':' || !dl_num(&s, &abi))
            goto bad;
         if (as == 0) {
            if (size % 8 || abi % 8 || !size || !abi)
               goto bad;
            L->ptr_bytes = size / 8;
            L->ptr_abi = abi / 8;
         }
      } else if (k == 'i' || k == 'f' || k == 'v' || k == 'a') {
         uint32_t size = 0, abi;
         s++;
         if (k != 'a' && !dl_num(&s, &size))
            goto bad;
         if (*s++ != ':' || !dl_num(&s, &abi))
            goto bad;
         if (abi % 8 || (k != 'a' && abi == 0))
            goto bad;
         if (L->n >= 64)
            return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "datalayout has too many specs");
         L->spec[L->n].kind = k;
         L->spec[L->n].bits = size;
         L->spec[L->n].abi = abi / 8;
         L->n++;
      }
      while (*s && *s != '-')
         s++; /* skip preferred alignment and unrelated components */
      if (*s == '-')
         s++;
      if (s == comp)
         goto bad;
   }
   return A2I_OK;
bad:
   a2i_fail(d, A2I_E_UNSUPPORTED_IR, "malformed datalayout near '");
   a2i_diag_strn(d, s, 16);
   a2i_diag_str(d, "'");
   return A2I_E_UNSUPPORTED_IR;
}

static uint64_t
align_to(uint64_t v, uint64_t a)
{
   return a ? (v + a - 1) / a * a : v;
}

static a2i_status
scalar_bits(const a2i_module *m, uint32_t type, uint32_t *bits, char *kind)
{
   const a2i_type *t = a2i_type_get(m, type);
   if (!t)
      return A2I_E_UNSUPPORTED_IR;
   switch (t->kind) {
   case A2I_T_INT: *bits = t->bits; *kind = 'i'; return A2I_OK;
   case A2I_T_HALF: *bits = 16; *kind = 'f'; return A2I_OK;
   case A2I_T_FLOAT: *bits = 32; *kind = 'f'; return A2I_OK;
   case A2I_T_DOUBLE: *bits = 64; *kind = 'f'; return A2I_OK;
   default: return A2I_E_UNSUPPORTED_IR;
   }
}

static a2i_status
type_layout(const a2i_module *m, const a2i_layout *L, uint32_t type, uint64_t *size,
            uint64_t *align, a2i_diag *d, unsigned depth)
{
   const a2i_type *t = a2i_type_get(m, type);
   if (!t || depth > 32)
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "type layout: invalid or too deeply nested type");
   uint32_t bits;
   char k;
   switch (t->kind) {
   case A2I_T_INT:
   case A2I_T_HALF:
   case A2I_T_FLOAT:
   case A2I_T_DOUBLE: {
      scalar_bits(m, type, &bits, &k);
      uint64_t bytes = (bits + 7) / 8, abi = dl_lookup(L, k, bits);
      if (!abi && k == 'f')
         abi = bytes; /* LLVM defaults f16:16 f32:32 f64:64 */
      if (!abi)
         return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "datalayout lacks an integer alignment");
      *align = abi;
      *size = align_to(bytes, abi);
      return A2I_OK;
   }
   case A2I_T_PTR:
      *size = L->ptr_bytes;
      *align = L->ptr_abi;
      return A2I_OK;
   case A2I_T_VECTOR: {
      if (t->scalable || scalar_bits(m, t->elem, &bits, &k) != A2I_OK)
         return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "vector layout: unsupported element");
      uint64_t total = t->count * bits, store = (total + 7) / 8;
      uint64_t abi = total <= UINT32_MAX ? dl_lookup(L, 'v', (uint32_t)total) : 0;
      if (!abi) {
         abi = 1;
         while (abi < store)
            abi <<= 1;
      }
      *align = abi;
      *size = align_to(store, abi);
      return A2I_OK;
   }
   case A2I_T_ARRAY: {
      uint64_t es, ea;
      a2i_status st = type_layout(m, L, t->elem, &es, &ea, d, depth + 1);
      if (st)
         return st;
      if (es && t->count > UINT64_MAX / es)
         return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "array layout overflow");
      *size = es * t->count;
      *align = ea;
      return A2I_OK;
   }
   case A2I_T_STRUCT: {
      uint64_t off = 0, maxal = 1;
      for (uint32_t i = 0; i < t->nmembers; i++) {
         uint64_t ms, ma;
         a2i_status st = type_layout(m, L, t->members[i], &ms, &ma, d, depth + 1);
         if (st)
            return st;
         if (t->packed)
            ma = 1;
         off = align_to(off, ma) + ms;
         if (ma > maxal)
            maxal = ma;
      }
      if (!t->packed) {
         uint64_t agg = dl_lookup(L, 'a', 0);
         if (agg > maxal)
            maxal = agg;
      }
      *align = maxal;
      *size = align_to(off, maxal);
      return A2I_OK;
   }
   default:
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "type has no data layout in MVP-AIR-0");
   }
}

a2i_status
a2i_type_layout(const a2i_module *m, const a2i_layout *L, uint32_t type, uint64_t *alloc_size,
                uint64_t *abi_align, a2i_diag *d)
{
   return type_layout(m, L, type, alloc_size, abi_align, d, 0);
}

static a2i_status
field_offset(const a2i_module *m, const a2i_layout *L, const a2i_type *t, uint32_t field,
             uint64_t *off, a2i_diag *d)
{
   uint64_t o = 0;
   for (uint32_t i = 0; i <= field; i++) {
      uint64_t ms, ma;
      a2i_status st = a2i_type_layout(m, L, t->members[i], &ms, &ma, d);
      if (st)
         return st;
      if (t->packed)
         ma = 1;
      o = align_to(o, ma);
      if (i == field)
         break;
      o += ms;
   }
   *off = o;
   return A2I_OK;
}

bool
a2i_const_int(const a2i_module *m, const a2i_function *f, uint32_t id, int64_t *v)
{
   const a2i_const *c = a2i_value_const(m, f, id);
   if (!c)
      return false;
   const a2i_type *t = a2i_type_get(m, c->type);
   if (!t || t->kind != A2I_T_INT)
      return false;
   if (c->kind == A2I_C_INT) {
      *v = c->ival;
      return true;
   }
   if (c->kind == A2I_C_NULL) {
      *v = 0;
      return true;
   }
   return false;
}

a2i_status
a2i_gep_offset(const a2i_module *m, const a2i_function *f, const a2i_layout *L,
               const a2i_inst *gep, int64_t *offset, a2i_diag *d)
{
   int64_t acc = 0, v, term;
   uint32_t cur = gep->xtype;
   uint64_t es, ea;
   a2i_status st;
   if (gep->nops < 2) {
      *offset = 0;
      return A2I_OK;
   }
   if (!a2i_const_int(m, f, gep->ops[1], &v))
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr with a non-constant index");
   if ((st = a2i_type_layout(m, L, cur, &es, &ea, d)))
      return st;
   if (es > INT64_MAX || __builtin_mul_overflow(v, (int64_t)es, &term) ||
       __builtin_add_overflow(acc, term, &acc))
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr offset overflow");
   for (uint32_t k = 2; k < gep->nops; k++) {
      const a2i_type *t = a2i_type_get(m, cur);
      if (!a2i_const_int(m, f, gep->ops[k], &v))
         return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr with a non-constant index");
      if (t && t->kind == A2I_T_STRUCT) {
         uint64_t fo;
         if (v < 0 || (uint64_t)v >= t->nmembers)
            return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr struct index out of range");
         if ((st = field_offset(m, L, t, (uint32_t)v, &fo, d)))
            return st;
         if (fo > INT64_MAX || __builtin_add_overflow(acc, (int64_t)fo, &acc))
            return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr offset overflow");
         cur = t->members[v];
      } else if (t && (t->kind == A2I_T_ARRAY || t->kind == A2I_T_VECTOR)) {
         if ((st = a2i_type_layout(m, L, t->elem, &es, &ea, d)))
            return st;
         if (es > INT64_MAX || __builtin_mul_overflow(v, (int64_t)es, &term) ||
             __builtin_add_overflow(acc, term, &acc))
            return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr offset overflow");
         cur = t->elem;
      } else {
         return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "getelementptr indexes into a scalar");
      }
   }
   *offset = acc;
   return A2I_OK;
}

/* ------------------------------------------------------------ interface */
static const a2i_md *
mdop(const a2i_module *m, const a2i_md *node, uint32_t k)
{
   if (!node || node->kind != A2I_MD_NODE || k >= node->nops || node->ops[k] == 0)
      return NULL;
   return a2i_md_get(m, node->ops[k] - 1);
}

static bool
md_is(const a2i_md *x, const char *s)
{
   return x && x->kind == A2I_MD_STRING && a2i_streqn(s, x->str, x->str_len);
}

static bool
md_str(const a2i_md *x, a2i_bstr *out)
{
   if (!x || x->kind != A2I_MD_STRING)
      return false;
   out->s = x->str;
   out->n = x->str_len;
   return true;
}

static bool
md_int(const a2i_module *m, const a2i_md *x, int64_t *v)
{
   if (!x || x->kind != A2I_MD_VALUE)
      return false;
   return a2i_const_int(m, NULL, x->value, v);
}

/* function index referenced by a METADATA_VALUE (direct or through bitcasts) */
static uint32_t
md_function(const a2i_module *m, const a2i_md *x)
{
   if (!x || x->kind != A2I_MD_VALUE)
      return A2I_NONE;
   uint32_t id = x->value;
   for (int depth = 0; depth < 4; depth++) {
      const a2i_value *v = a2i_value_get(m, NULL, id);
      if (!v)
         return A2I_NONE;
      if (v->kind == A2I_V_FUNCTION) {
         const a2i_global *g = &A2I_VEC_AT(&m->globals, a2i_global, v->ref);
         return g->func;
      }
      if (v->kind != A2I_V_CONST)
         return A2I_NONE;
      const a2i_const *c = &A2I_VEC_AT(&m->consts, a2i_const, v->ref);
      if (c->kind != A2I_C_CE_CAST || (c->cast_opc != 11 && c->cast_opc != 12))
         return A2I_NONE;
      id = c->cast_operand;
   }
   return A2I_NONE;
}

static a2i_status
bind_fail(a2i_diag *d, const char *msg, const a2i_md *tok)
{
   a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, msg);
   if (tok && tok->kind == A2I_MD_STRING) {
      a2i_diag_str(d, " '");
      a2i_diag_strn(d, (const char *)tok->str, tok->str_len);
      a2i_diag_str(d, "'");
   }
   return A2I_E_UNSUPPORTED_BINDING;
}

static a2i_status
parse_out(const a2i_module *m, uint8_t stage, const a2i_md *node, a2i_out_binding *o,
          a2i_diag *d)
{
   const a2i_md *t0 = mdop(m, node, 0);
   uint32_t k;
   int64_t v, v2;
   if (stage == A2I_STAGE_VERTEX && md_is(t0, "air.position")) {
      o->kind = A2I_OUT_POSITION;
      k = 1;
   } else if (stage == A2I_STAGE_VERTEX && md_is(t0, "air.vertex_output")) {
      o->kind = A2I_OUT_VERTEX_OUTPUT;
      if (!md_str(mdop(m, node, 1), &o->user))
         return bind_fail(d, "vertex_output without link name", NULL);
      k = 2;
   } else if (stage == A2I_STAGE_FRAGMENT && md_is(t0, "air.render_target")) {
      if (!md_int(m, mdop(m, node, 1), &v) || !md_int(m, mdop(m, node, 2), &v2) || v < 0 ||
          v > 7 || v2 != 0)
         return bind_fail(d, "render_target index outside MVP-AIR-0 (0..7, index 0)", NULL);
      o->kind = A2I_OUT_RENDER_TARGET;
      o->rt_index = (uint32_t)v;
      k = 3;
   } else {
      return bind_fail(d, "output binding outside MVP-AIR-0:", t0);
   }
   while (k < node->nops) {
      const a2i_md *key = mdop(m, node, k);
      if (md_is(key, "air.arg_type_name") && md_str(mdop(m, node, k + 1), &o->type_name))
         k += 2;
      else if (md_is(key, "air.arg_name") && md_str(mdop(m, node, k + 1), &o->arg_name))
         k += 2;
      else
         return bind_fail(d, "output attribute outside MVP-AIR-0:", key);
   }
   return A2I_OK;
}

static a2i_status
parse_in(const a2i_module *m, uint8_t stage, const a2i_md *node, a2i_in_binding *b,
         a2i_diag *d)
{
   int64_t v, v2;
   if (!md_int(m, mdop(m, node, 0), &v) || v < 0 || v >= A2I_MAX_BINDINGS)
      return bind_fail(d, "input binding without a valid argument number", NULL);
   b->argno = (uint32_t)v;
   const a2i_md *kind = mdop(m, node, 1);
   uint32_t k = 2;
   bool has_loc = false, read = false, center = false, persp = false, has_as = false;
   if (stage == A2I_STAGE_VERTEX && md_is(kind, "air.vertex_input")) {
      b->kind = A2I_IN_VERTEX_INPUT;
   } else if (md_is(kind, "air.buffer")) {
      b->kind = A2I_IN_BUFFER;
   } else if (stage == A2I_STAGE_FRAGMENT && md_is(kind, "air.fragment_input")) {
      b->kind = A2I_IN_FRAGMENT_INPUT;
      if (!md_str(mdop(m, node, 2), &b->user))
         return bind_fail(d, "fragment_input without link name", NULL);
      k = 3;
   } else {
      return bind_fail(d, "input binding outside MVP-AIR-0:", kind);
   }
   while (k < node->nops) {
      const a2i_md *key = mdop(m, node, k);
      if (md_is(key, "air.arg_type_name") && md_str(mdop(m, node, k + 1), &b->type_name)) {
         k += 2;
      } else if (md_is(key, "air.arg_name") && md_str(mdop(m, node, k + 1), &b->arg_name)) {
         k += 2;
      } else if (md_is(key, "air.arg_unused")) {
         b->unused = true;
         k += 1;
      } else if (b->kind != A2I_IN_FRAGMENT_INPUT && md_is(key, "air.location_index") &&
                 md_int(m, mdop(m, node, k + 1), &v) && md_int(m, mdop(m, node, k + 2), &v2)) {
         if (v < 0 || v > 30 || v2 != 1)
            return bind_fail(d, "location_index outside MVP-AIR-0 (0..30, count 1)", NULL);
         b->location = (uint32_t)v;
         has_loc = true;
         k += 3;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.read")) {
         read = true;
         k += 1;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.address_space") &&
                 md_int(m, mdop(m, node, k + 1), &v)) {
         b->addrspace = (uint32_t)v;
         has_as = true;
         k += 2;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.buffer_size") &&
                 md_int(m, mdop(m, node, k + 1), &v) && v > 0) {
         b->has_buffer_size = true;
         b->buffer_size = (uint64_t)v;
         k += 2;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.arg_type_size") &&
                 md_int(m, mdop(m, node, k + 1), &v) && v >= 0) {
         b->arg_type_size = (uint64_t)v;
         k += 2;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.arg_type_align_size") &&
                 md_int(m, mdop(m, node, k + 1), &v) && v >= 0) {
         b->arg_type_align = (uint64_t)v;
         k += 2;
      } else if (b->kind == A2I_IN_BUFFER && md_is(key, "air.struct_type_info") &&
                 mdop(m, node, k + 1) && mdop(m, node, k + 1)->kind == A2I_MD_NODE) {
         k += 2; /* reflection-only metadata */
      } else if (b->kind == A2I_IN_FRAGMENT_INPUT && md_is(key, "air.center")) {
         center = true;
         k += 1;
      } else if (b->kind == A2I_IN_FRAGMENT_INPUT && md_is(key, "air.perspective") && !persp) {
         persp = true;
         b->interp = A2I_INTERP_PERSPECTIVE;
         k += 1;
      } else if (b->kind == A2I_IN_FRAGMENT_INPUT && md_is(key, "air.no_perspective") && !persp) {
         persp = true;
         b->interp = A2I_INTERP_NO_PERSPECTIVE;
         k += 1;
      } else {
         return bind_fail(d, "input attribute outside MVP-AIR-0:", key);
      }
   }
   if (b->kind != A2I_IN_FRAGMENT_INPUT && !has_loc)
      return bind_fail(d, "input binding without location_index", kind);
   if (b->kind == A2I_IN_BUFFER && (!read || !has_as || b->addrspace != 2))
      return bind_fail(d, "buffer must be read-only constant address space 2", kind);
   if (b->kind == A2I_IN_FRAGMENT_INPUT && (!center || !persp))
      return bind_fail(d, "fragment_input needs center sampling and (no_)perspective", kind);
   return A2I_OK;
}

a2i_status
a2i_iface_extract(const a2i_module *m, a2i_iface *io, a2i_diag *d)
{
   a2i_memset(io, 0, sizeof(*io));
   uint32_t defs = 0, fi = A2I_NONE;
   for (uint32_t i = 0; i < m->functions.len; i++)
      if (A2I_VEC_AT(&m->functions, a2i_function, i).has_body) {
         defs++;
         fi = i;
      }
   if (defs != 1) {
      a2i_fail(d, A2I_E_UNSUPPORTED_IR, "MVP-AIR-0 needs exactly one defined function; found ");
      a2i_diag_u64(d, defs);
      return A2I_E_UNSUPPORTED_IR;
   }
   io->func = fi;
   static const char *const stages[] = {"air.vertex", "air.fragment"};
   static const char *const other[] = {"air.kernel", "air.visible", "air.mesh", "air.object",
                                       "air.intersection"};
   const a2i_md *entry = NULL;
   for (uint32_t s = 0; s < 2; s++) {
      const a2i_named_md *nm = a2i_named_md_find(m, stages[s]);
      for (uint32_t k = 0; nm && k < nm->nops; k++) {
         const a2i_md *node = a2i_md_get(m, nm->ops[k]);
         if (md_function(m, mdop(m, node, 0)) == fi) {
            if (entry)
               return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "function listed as more than one entry point");
            entry = node;
            io->stage = (uint8_t)s;
         }
      }
   }
   if (!entry) {
      for (uint32_t s = 0; s < sizeof(other) / sizeof(other[0]); s++) {
         const a2i_named_md *nm = a2i_named_md_find(m, other[s]);
         for (uint32_t k = 0; nm && k < nm->nops; k++)
            if (md_function(m, mdop(m, a2i_md_get(m, nm->ops[k]), 0)) == fi) {
               a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "entry point kind outside MVP-AIR-0: ");
               a2i_diag_str(d, other[s]);
               return A2I_E_UNSUPPORTED_BINDING;
            }
      }
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING,
                      "function is not listed in !air.vertex or !air.fragment");
   }
   if (entry->nops != 3)
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "entry-point node does not have 3 operands");
   const a2i_md *outs = mdop(m, entry, 1), *ins = mdop(m, entry, 2);
   if (!outs || !ins || outs->kind != A2I_MD_NODE || ins->kind != A2I_MD_NODE)
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "entry-point node lacks output/input lists");
   if (outs->nops > A2I_MAX_BINDINGS || ins->nops > A2I_MAX_BINDINGS)
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "too many bindings");

   const a2i_function *f = &A2I_VEC_AT(&m->functions, a2i_function, fi);
   const a2i_type *ft = a2i_type_get(m, f->type);
   const a2i_type *rt = a2i_type_get(m, ft->ret);
   io->ret_type = ft->ret;
   io->ret_is_struct = rt && rt->kind == A2I_T_STRUCT;
   a2i_status st;
   io->nout = outs->nops;
   for (uint32_t i = 0; i < outs->nops; i++) {
      const a2i_md *node = mdop(m, outs, i);
      if (!node || node->kind != A2I_MD_NODE)
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "malformed output binding");
      if ((st = parse_out(m, io->stage, node, &io->out[i], d)))
         return st;
   }
   io->nin = ins->nops;
   for (uint32_t i = 0; i < ins->nops; i++) {
      const a2i_md *node = mdop(m, ins, i);
      if (!node || node->kind != A2I_MD_NODE)
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "malformed input binding");
      if ((st = parse_in(m, io->stage, node, &io->in[i], d)))
         return st;
   }

   /* outputs vs. return type */
   if (io->ret_is_struct) {
      if (rt->nmembers != io->nout)
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "output count differs from return struct");
      for (uint32_t i = 0; i < io->nout; i++)
         io->out[i].type = rt->members[i];
   } else {
      if (!rt || rt->kind == A2I_T_VOID || io->nout != 1)
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "non-struct return needs exactly one output");
      io->out[0].type = ft->ret;
   }
   uint32_t positions = 0, ncomp;
   for (uint32_t i = 0; i < io->nout; i++) {
      if (!a2i_type_is_float_vec(m, io->out[i].type, &ncomp))
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "output type is not float/float2/3/4");
      if (io->out[i].kind == A2I_OUT_POSITION) {
         positions++;
         if (ncomp != 4)
            return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "position output must be float4");
      }
      for (uint32_t j = 0; j < i; j++) {
         if (io->out[i].kind == A2I_OUT_RENDER_TARGET && io->out[j].kind == A2I_OUT_RENDER_TARGET &&
             io->out[i].rt_index == io->out[j].rt_index)
            return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "duplicate render_target index");
         if (io->out[i].kind == A2I_OUT_VERTEX_OUTPUT && io->out[j].kind == A2I_OUT_VERTEX_OUTPUT &&
             io->out[i].user.n == io->out[j].user.n &&
             a2i_memcmp(io->out[i].user.s, io->out[j].user.s, io->out[i].user.n) == 0)
            return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "duplicate vertex_output name");
      }
   }
   if (io->stage == A2I_STAGE_VERTEX && positions != 1)
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "vertex function needs exactly one position");

   /* inputs vs. parameters */
   if (ft->nmembers != io->nin)
      return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "input binding count differs from parameters");
   for (uint32_t i = 0; i < io->nin; i++) {
      a2i_in_binding *b = &io->in[i];
      if (b->argno >= ft->nmembers)
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "input binding argument out of range");
      for (uint32_t j = 0; j < i; j++)
         if (io->in[j].argno == b->argno)
            return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "argument bound twice");
      b->type = ft->members[b->argno];
      const a2i_type *at = a2i_type_get(m, b->type);
      if (b->kind == A2I_IN_BUFFER) {
         if (!at || at->kind != A2I_T_PTR || at->addrspace != 2)
            return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "buffer argument is not ptr addrspace(2)");
      } else if (!a2i_type_is_float_vec(m, b->type, &ncomp)) {
         return a2i_fail(d, A2I_E_UNSUPPORTED_BINDING, "stage input is not float/float2/3/4");
      }
   }
   return A2I_OK;
}

/* --------------------------------------------------------------- subset */
static bool
float_operand_ok(const a2i_module *m, const a2i_function *f, uint32_t id, uint32_t want_type)
{
   const a2i_value *v = a2i_value_get(m, f, id);
   if (!v || v->type != want_type)
      return false;
   if (v->kind == A2I_V_INST || v->kind == A2I_V_ARG)
      return true;
   if (v->kind != A2I_V_CONST)
      return false;
   const a2i_const *c = &A2I_VEC_AT(&m->consts, a2i_const, v->ref);
   switch (c->kind) {
   case A2I_C_FLOAT:
   case A2I_C_NULL:
   case A2I_C_UNDEF:
   case A2I_C_POISON:
   case A2I_C_DATA:
      return true;
   case A2I_C_AGG:
      for (uint32_t i = 0; i < c->n; i++) {
         if (c->elems[i] > UINT32_MAX)
            return false;
         const a2i_const *e = a2i_value_const(m, f, (uint32_t)c->elems[i]);
         if (!e || (e->kind != A2I_C_FLOAT && e->kind != A2I_C_UNDEF && e->kind != A2I_C_POISON &&
                    e->kind != A2I_C_NULL))
            return false;
      }
      return true;
   default:
      return false;
   }
}

/* true if id is a buffer argument (addrspace 2) or a GEP chain rooted on one */
static bool
buffer_pointer(const a2i_module *m, const a2i_function *f, const a2i_iface *io, uint32_t id,
               unsigned depth)
{
   const a2i_value *v = a2i_value_get(m, f, id);
   if (!v || depth > 16)
      return false;
   if (v->kind == A2I_V_ARG) {
      for (uint32_t i = 0; i < io->nin; i++)
         if (io->in[i].argno == v->ref && io->in[i].kind == A2I_IN_BUFFER)
            return true;
      return false;
   }
   const a2i_inst *in = a2i_value_inst(m, f, id);
   return in && in->code == A2I_FC_GEP && in->nops >= 1 &&
          buffer_pointer(m, f, io, in->ops[0], depth + 1);
}

static bool
mask_ok(const a2i_module *m, const a2i_function *f, uint32_t id, uint64_t limit)
{
   const a2i_const *c = a2i_value_const(m, f, id);
   if (!c)
      return false;
   if (c->kind == A2I_C_NULL || c->kind == A2I_C_UNDEF || c->kind == A2I_C_POISON)
      return true;
   if (c->kind == A2I_C_DATA) {
      for (uint32_t i = 0; i < c->n; i++)
         if (c->elems[i] >= limit)
            return false;
      return true;
   }
   if (c->kind == A2I_C_AGG) {
      for (uint32_t i = 0; i < c->n; i++) {
         if (c->elems[i] > UINT32_MAX)
            return false;
         const a2i_const *e = a2i_value_const(m, f, (uint32_t)c->elems[i]);
         if (!e)
            return false;
         if (e->kind == A2I_C_UNDEF || e->kind == A2I_C_POISON)
            continue;
         if (e->kind == A2I_C_NULL)
            continue;
         if (e->kind != A2I_C_INT || e->ival < 0 || (uint64_t)e->ival >= limit)
            return false;
      }
      return true;
   }
   return false;
}

static a2i_status
subset_bad(const a2i_module *m, const a2i_function *f, const a2i_inst *in, uint32_t k,
           const char *why, a2i_diag *d)
{
   a2i_fail(d, A2I_E_UNSUPPORTED_IR, "instruction #");
   a2i_diag_u64(d, k);
   a2i_diag_str(d, " '");
   a2i_diag_str(d, a2i_inst_mnemonic(in, in->nops && a2i_type_is_fp(m, a2i_value_type(m, f, in->ops[0]))));
   if (in->code == A2I_FC_CALL && in->nops) {
      const a2i_value *cv = a2i_value_get(m, f, in->ops[0]);
      if (cv && cv->kind == A2I_V_FUNCTION) {
         a2i_diag_str(d, " @");
         a2i_diag_str(d, A2I_VEC_AT(&m->globals, a2i_global, cv->ref).name);
      }
   }
   a2i_diag_str(d, "' outside MVP-AIR-0: ");
   a2i_diag_str(d, why);
   return A2I_E_UNSUPPORTED_IR;
}

a2i_status
a2i_subset_check(const a2i_module *m, const a2i_iface *io, const a2i_layout *L,
                 const char *mtlb_name, uint32_t mtlb_name_len, a2i_subset_report *rep,
                 a2i_diag *d)
{
   const a2i_function *f = &A2I_VEC_AT(&m->functions, a2i_function, io->func);
   rep->insts = f->insts.len;
   rep->first_bad_inst = A2I_NONE;
   const char *fname = a2i_function_name(m, f);
   if (mtlb_name && (a2i_strlen(fname) != mtlb_name_len ||
                     a2i_memcmp(fname, mtlb_name, mtlb_name_len) != 0)) {
      a2i_fail(d, A2I_E_CONTAINER, "bitcode function '");
      a2i_diag_str(d, fname);
      a2i_diag_str(d, "' differs from MTLB entry NAME");
      return A2I_E_CONTAINER;
   }
   if (!m->triple || !a2i_has_prefix(m->triple, "air64-apple-"))
      return a2i_fail(d, A2I_E_VERSION, "target triple is not air64-apple-*");
   if (f->nblocks != 1) {
      a2i_fail(d, A2I_E_UNSUPPORTED_IR, "control flow outside MVP-AIR-0: basic blocks = ");
      a2i_diag_u64(d, f->nblocks);
      return A2I_E_UNSUPPORTED_IR;
   }
   uint32_t nc, nc2;
   int64_t iv;
   for (uint32_t k = 0; k < f->insts.len; k++) {
      const a2i_inst *in = &A2I_VEC_AT(&f->insts, a2i_inst, k);
      const char *why = NULL;
      switch (in->code) {
      case A2I_FC_BINOP:
         if (in->op > 2 || !a2i_type_is_float_vec(m, in->type, &nc))
            why = "only fadd/fsub/fmul on float vectors";
         else if (!float_operand_ok(m, f, in->ops[0], in->type) ||
                  !float_operand_ok(m, f, in->ops[1], in->type))
            why = "operand is not a float value or float constant";
         break;
      case A2I_FC_UNOP:
         if (in->op != 0 || !a2i_type_is_float_vec(m, in->type, &nc) ||
             !float_operand_ok(m, f, in->ops[0], in->type))
            why = "only fneg on float vectors";
         break;
      case A2I_FC_SHUFFLEVEC: {
         uint32_t t0 = a2i_value_type(m, f, in->ops[0]);
         if (!a2i_type_is_float_vec(m, in->type, &nc) || !a2i_type_is_float_vec(m, t0, &nc2) ||
             nc2 < 2 || !float_operand_ok(m, f, in->ops[0], t0) ||
             !float_operand_ok(m, f, in->ops[1], t0))
            why = "shufflevector operands must be float vectors (<= 4)";
         else if (!mask_ok(m, f, in->ops[2], 2u * nc2))
            why = "shufflevector mask must be a constant in range";
         break;
      }
      case A2I_FC_EXTRACTELT: {
         uint32_t t0 = a2i_value_type(m, f, in->ops[0]);
         if (!a2i_type_is_float_vec(m, t0, &nc) || nc < 2 ||
             !float_operand_ok(m, f, in->ops[0], t0))
            why = "extractelement source must be a float vector";
         else if (!a2i_const_int(m, f, in->ops[1], &iv) || iv < 0 || (uint64_t)iv >= nc)
            why = "extractelement index must be a constant in range";
         break;
      }
      case A2I_FC_INSERTELT:
         if (!a2i_type_is_float_vec(m, in->type, &nc) || nc < 2 ||
             !float_operand_ok(m, f, in->ops[0], in->type))
            why = "insertelement target must be a float vector";
         else if (!a2i_const_int(m, f, in->ops[2], &iv) || iv < 0 || (uint64_t)iv >= nc)
            why = "insertelement index must be a constant in range";
         else if (!float_operand_ok(m, f, in->ops[1], a2i_type_get(m, in->type)->elem))
            why = "inserted element must be a float";
         break;
      case A2I_FC_EXTRACTVAL:
      case A2I_FC_INSERTVAL: {
         uint32_t at = a2i_value_type(m, f, in->ops[0]);
         if (!io->ret_is_struct || at != io->ret_type || in->nidx != 1)
            why = "aggregate access only on the returned struct, single index";
         else if (in->code == A2I_FC_INSERTVAL) {
            const a2i_value *av = a2i_value_get(m, f, in->ops[0]);
            const a2i_type *rt = a2i_type_get(m, io->ret_type);
            if (av->kind == A2I_V_CONST) {
               const a2i_const *c = &A2I_VEC_AT(&m->consts, a2i_const, av->ref);
               if (c->kind != A2I_C_UNDEF && c->kind != A2I_C_POISON && c->kind != A2I_C_NULL)
                  why = "insertvalue base must be undef/poison/zeroinitializer or a value";
            } else if (av->kind != A2I_V_INST) {
               why = "insertvalue base must be undef/poison/zeroinitializer or a value";
            }
            if (!why && !float_operand_ok(m, f, in->ops[1], rt->members[in->idx[0]]))
               why = "inserted field must be a float value";
         }
         break;
      }
      case A2I_FC_GEP: {
         int64_t off;
         a2i_diag tmp;
         a2i_diag_clear(&tmp);
         if (!buffer_pointer(m, f, io, in->ops[0], 0))
            why = "getelementptr base must be a constant-buffer argument";
         else if (a2i_gep_offset(m, f, L, in, &off, &tmp) != A2I_OK)
            why = tmp.msg;
         else if (off < 0)
            why = "negative constant-buffer offset";
         break;
      }
      case A2I_FC_LOAD:
         if (!a2i_type_is_float_vec(m, in->type, &nc))
            why = "load type must be float/float2/3/4";
         else if (in->is_volatile)
            why = "volatile load";
         else if (!buffer_pointer(m, f, io, in->ops[0], 0))
            why = "load must read a constant-buffer argument";
         break;
      case A2I_FC_RET:
         if (in->nops != 1 || a2i_value_type(m, f, in->ops[0]) != io->ret_type)
            why = "ret must return the entry value";
         else if (!io->ret_is_struct && !float_operand_ok(m, f, in->ops[0], io->ret_type))
            why = "returned value must be float";
         else if (io->ret_is_struct) {
            const a2i_value *rv = a2i_value_get(m, f, in->ops[0]);
            if (rv->kind == A2I_V_CONST) {
               const a2i_const *c = &A2I_VEC_AT(&m->consts, a2i_const, rv->ref);
               if (c->kind != A2I_C_NULL && c->kind != A2I_C_UNDEF && c->kind != A2I_C_POISON)
                  why = "returned aggregate constant must be zero/undef/poison";
            } else if (rv->kind != A2I_V_INST) {
               why = "returned aggregate must be built by insertvalue";
            }
         }
         break;
      default:
         why = "opcode not in MVP-AIR-0";
         break;
      }
      if (why) {
         rep->first_bad_inst = k;
         return subset_bad(m, f, in, k, why, d);
      }
   }
   return A2I_OK;
}
