/* SPDX-License-Identifier: MIT */
/*
 * air_to_nir.c - MVP-AIR-0 -> Mesa NIR (see air_to_nir.h for the policy).
 * Host layer: depends on the freestanding frontend (a2i_*.h) and on the
 * pinned Mesa NIR headers/libraries only; no ir3 knowledge lives here.
 */
#include "air_to_nir.h"

#include <stdio.h>
#include <string.h>

#include "compiler/glsl_types.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/ralloc.h"

/* LLVM bitcode FastMathFlags encoding (bitc::FastMathMap). */
enum {
   LLVM_FMF_UNSAFE = 1u << 0,
   LLVM_FMF_NNAN = 1u << 1,
   LLVM_FMF_NINF = 1u << 2,
   LLVM_FMF_NSZ = 1u << 3,
   LLVM_FMF_ARCP = 1u << 4,
   LLVM_FMF_CONTRACT = 1u << 5,
   LLVM_FMF_AFN = 1u << 6,
   LLVM_FMF_REASSOC = 1u << 7,
};

#define MAX_VAR_SLOTS 32u

typedef struct ptr_val {
   bool valid;
   uint32_t binding; /* index into io->in (an A2I_IN_BUFFER binding) */
   int64_t offset;   /* bytes from the start of the bound buffer */
} ptr_val;

typedef struct ctx {
   const a2i_module *m;
   const a2i_function *f;
   const a2i_iface *io;
   const a2i_layout *L;
   a2i_nir_info *info;
   a2i_diag *d;
   nir_builder b;
   void *tmp;           /* ralloc context for the tables below */
   uint32_t nlocals;
   nir_def **val;       /* per function-local value: translated SSA def */
   nir_def ***agg;      /* per local value of the returned struct type: members */
   ptr_val *ptr;        /* per local value: constant-buffer pointer */
   nir_variable *out_var[A2I_MAX_BINDINGS];
} ctx;

static a2i_status
bad(ctx *c, a2i_status s, const char *msg)
{
   return a2i_fail(c->d, s, msg);
}

static a2i_status
bad_inst(ctx *c, uint32_t k, a2i_status s, const char *msg)
{
   a2i_fail(c->d, s, "air_to_nir: instruction #");
   a2i_diag_u64(c->d, k);
   a2i_diag_str(c->d, ": ");
   a2i_diag_str(c->d, msg);
   return s;
}

static uint32_t
ncomp_of(const ctx *c, uint32_t type)
{
   uint32_t n;
   return type != A2I_NONE && a2i_type_is_float_vec(c->m, type, &n) ? n : 0;
}

/* function-local index of a value id, or UINT32_MAX for module-level values */
static uint32_t
local_index(const ctx *c, uint32_t id)
{
   if (id < c->m->n_module_values || id - c->m->n_module_values >= c->nlocals)
      return UINT32_MAX;
   return id - c->m->n_module_values;
}

static uint32_t
local_of_arg(const ctx *c, uint32_t argno)
{
   for (uint32_t i = 0; i < c->f->locals.len; i++) {
      const a2i_value *v = &A2I_VEC_AT(&c->f->locals, a2i_value, i);
      if (v->kind == A2I_V_ARG && v->ref == argno)
         return i;
   }
   return UINT32_MAX;
}

static const struct glsl_type *
float_type(uint32_t n)
{
   return n == 1 ? glsl_float_type() : glsl_vector_type(GLSL_TYPE_FLOAT, n);
}

/* Fast-math flags of one LLVM fp instruction -> NIR fp math control. No flag
 * means IEEE-strict LLVM semantics, which is the most restrictive NIR mode. */
static uint32_t
fp_ctrl(uint64_t fmf)
{
   if (fmf & LLVM_FMF_UNSAFE)
      return nir_fp_fast_math;
   uint32_t ctl = 0;
   if (!(fmf & LLVM_FMF_NSZ))
      ctl |= nir_fp_preserve_signed_zero;
   if (!(fmf & LLVM_FMF_NINF))
      ctl |= nir_fp_preserve_inf;
   if (!(fmf & LLVM_FMF_NNAN))
      ctl |= nir_fp_preserve_nan;
   if (!(fmf & LLVM_FMF_CONTRACT))
      ctl |= nir_fp_no_contract;
   if (!(fmf & LLVM_FMF_REASSOC))
      ctl |= nir_fp_no_reassoc;
   if (ctl & (nir_fp_no_contract | nir_fp_no_reassoc))
      ctl |= nir_fp_no_transform; /* required by NIR when either is set */
   return ctl;
}

/* ------------------------------------------------------------ constants */
/* One scalar element of a float constant: raw IEEE bits, or undefined. */
static a2i_status
const_scalar_bits(ctx *c, const a2i_const *k, uint32_t *bits, bool *undef)
{
   *undef = false;
   switch (k->kind) {
   case A2I_C_FLOAT:
      if (!a2i_type_get(c->m, k->type) || a2i_type_get(c->m, k->type)->kind != A2I_T_FLOAT)
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: non-f32 float constant");
      *bits = (uint32_t)k->bits;
      return A2I_OK;
   case A2I_C_NULL:
      *bits = 0;
      return A2I_OK;
   case A2I_C_UNDEF:
   case A2I_C_POISON:
      *undef = true;
      return A2I_OK;
   default:
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: constant element is not a float");
   }
}

/* Bit-exact float constant (keeps -0.0 and NaN payloads). */
static a2i_status
const_def(ctx *c, const a2i_const *k, uint32_t type, nir_def **out)
{
   uint32_t n = ncomp_of(c, type);
   if (!n)
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: constant is not float/float2/3/4");
   uint32_t bits[4] = {0};
   bool undef[4] = {false};
   a2i_status st;
   switch (k->kind) {
   case A2I_C_FLOAT:
   case A2I_C_NULL:
   case A2I_C_UNDEF:
   case A2I_C_POISON:
      if (k->kind == A2I_C_FLOAT && n != 1)
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: scalar constant of vector type");
      for (uint32_t i = 0; i < n; i++)
         if ((st = const_scalar_bits(c, k, &bits[i], &undef[i])))
            return st;
      break;
   case A2I_C_DATA:
      if (k->n != n)
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: data vector length mismatch");
      for (uint32_t i = 0; i < n; i++) {
         if (k->elems[i] > UINT32_MAX)
            return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: data element wider than f32");
         bits[i] = (uint32_t)k->elems[i];
      }
      break;
   case A2I_C_AGG:
      if (k->n != n)
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate vector length mismatch");
      for (uint32_t i = 0; i < n; i++) {
         const a2i_const *e = k->elems[i] <= UINT32_MAX
                                 ? a2i_value_const(c->m, c->f, (uint32_t)k->elems[i])
                                 : NULL;
         if (!e)
            return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate element is not a constant");
         if ((st = const_scalar_bits(c, e, &bits[i], &undef[i])))
            return st;
      }
      break;
   default:
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: constant kind outside MVP-AIR-0");
   }
   bool any_undef = false, all_undef = true;
   for (uint32_t i = 0; i < n; i++) {
      any_undef |= undef[i];
      all_undef &= undef[i];
   }
   if (all_undef) {
      *out = nir_undef(&c->b, n, 32);
   } else if (!any_undef) {
      nir_const_value v[4];
      memset(v, 0, sizeof(v));
      for (uint32_t i = 0; i < n; i++)
         v[i].u32 = bits[i];
      *out = nir_build_imm(&c->b, n, 32, v);
   } else {
      nir_def *ch[4];
      for (uint32_t i = 0; i < n; i++)
         ch[i] = undef[i] ? nir_undef(&c->b, 1, 32) : nir_imm_int(&c->b, (int32_t)bits[i]);
      *out = nir_vec(&c->b, ch, n);
   }
   return A2I_OK;
}

static a2i_status
get_value(ctx *c, uint32_t id, nir_def **out)
{
   const a2i_value *v = a2i_value_get(c->m, c->f, id);
   if (!v)
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: operand id out of range");
   uint32_t li = local_index(c, id);
   if (li != UINT32_MAX && c->val[li]) {
      *out = c->val[li];
      return A2I_OK;
   }
   switch (v->kind) {
   case A2I_V_CONST: {
      a2i_status st = const_def(c, &A2I_VEC_AT(&c->m->consts, a2i_const, v->ref), v->type, out);
      if (!st && li != UINT32_MAX)
         c->val[li] = *out;
      return st;
   }
   case A2I_V_ARG:
      return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: argument used as a value is not a stage input");
   case A2I_V_INST:
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: use of a value before its definition");
   default:
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: global/function value outside MVP-AIR-0");
   }
}

/* Members of a value of the returned struct type (NULL member = undefined). */
static a2i_status
get_agg(ctx *c, uint32_t id, nir_def ***members)
{
   const a2i_type *rt = a2i_type_get(c->m, c->io->ret_type);
   const a2i_value *v = a2i_value_get(c->m, c->f, id);
   if (!v || !rt || rt->kind != A2I_T_STRUCT || v->type != c->io->ret_type)
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate is not the returned struct");
   uint32_t li = local_index(c, id);
   if (v->kind == A2I_V_INST) {
      if (li == UINT32_MAX || !c->agg[li])
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate used before definition");
      *members = c->agg[li];
      return A2I_OK;
   }
   if (v->kind != A2I_V_CONST)
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate operand kind");
   const a2i_const *k = &A2I_VEC_AT(&c->m->consts, a2i_const, v->ref);
   nir_def **mem = rzalloc_array(c->tmp, nir_def *, rt->nmembers);
   if (!mem)
      return bad(c, A2I_E_NIR, "air_to_nir: out of memory");
   if (k->kind == A2I_C_NULL) {
      for (uint32_t i = 0; i < rt->nmembers; i++) {
         uint32_t n = ncomp_of(c, rt->members[i]);
         if (!n)
            return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: struct member is not float/float2/3/4");
         mem[i] = nir_imm_zero(&c->b, n, 32);
      }
   } else if (k->kind != A2I_C_UNDEF && k->kind != A2I_C_POISON) {
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: aggregate constant must be zero/undef/poison");
   }
   *members = mem;
   return A2I_OK;
}

/* Constant shufflevector mask element: index, or -1 for undef/poison. */
static a2i_status
mask_elem(ctx *c, uint32_t mask_id, uint32_t i, int64_t *idx)
{
   const a2i_const *k = a2i_value_const(c->m, c->f, mask_id);
   if (!k)
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: shufflevector mask is not constant");
   switch (k->kind) {
   case A2I_C_NULL: *idx = 0; return A2I_OK;
   case A2I_C_UNDEF:
   case A2I_C_POISON: *idx = -1; return A2I_OK;
   case A2I_C_DATA:
      if (i >= k->n || k->elems[i] > INT32_MAX)
         return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: shufflevector mask element");
      *idx = (int64_t)k->elems[i];
      return A2I_OK;
   case A2I_C_AGG: {
      const a2i_const *e = i < k->n && k->elems[i] <= UINT32_MAX
                              ? a2i_value_const(c->m, c->f, (uint32_t)k->elems[i])
                              : NULL;
      if (e && (e->kind == A2I_C_UNDEF || e->kind == A2I_C_POISON)) {
         *idx = -1;
         return A2I_OK;
      }
      if (e && e->kind == A2I_C_NULL) {
         *idx = 0;
         return A2I_OK;
      }
      if (e && e->kind == A2I_C_INT && e->ival >= 0) {
         *idx = e->ival;
         return A2I_OK;
      }
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: shufflevector mask element");
   }
   default:
      return bad(c, A2I_E_UNSUPPORTED_IR, "air_to_nir: shufflevector mask kind");
   }
}

/* -------------------------------------------------------------- bindings */
static a2i_status
copy_name(char *dst, size_t cap, a2i_bstr s, const char *fallback)
{
   size_t n = s.n < cap - 1 ? s.n : cap - 1;
   if (!s.s || !s.n) {
      snprintf(dst, cap, "%s", fallback);
      return A2I_OK;
   }
   memcpy(dst, s.s, n);
   dst[n] = '\0';
   return A2I_OK;
}

static a2i_status
declare_io(ctx *c)
{
   const a2i_iface *io = c->io;
   a2i_nir_info *info = c->info;
   uint32_t nvar_in = 0, nvar_out = 0;
   char name[64];
   info->nin = io->nin;
   info->nout = io->nout;
   for (uint32_t i = 0; i < io->nin; i++) {
      const a2i_in_binding *bd = &io->in[i];
      a2i_nir_io *r = &info->in[i];
      r->kind = bd->kind;
      r->argno = bd->argno;
      r->location = bd->location;
      r->interp = bd->interp;
      r->unused = bd->unused;
      r->user = bd->user;
      uint32_t li = local_of_arg(c, bd->argno);
      if (li == UINT32_MAX)
         return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: bound argument has no value id");
      for (uint32_t j = 0; j < i; j++)
         if (io->in[j].kind == bd->kind && bd->kind != A2I_IN_FRAGMENT_INPUT &&
             io->in[j].location == bd->location)
            return bad(c, A2I_E_UNSUPPORTED_BINDING,
                       "air_to_nir: two arguments bound to the same location");
      if (bd->kind == A2I_IN_BUFFER) {
         r->slot = bd->location; /* UBO index */
         r->ncomp = 0;
         snprintf(r->slot_name, sizeof(r->slot_name), "UBO%u", bd->location);
         c->ptr[li] = (ptr_val){.valid = true, .binding = i, .offset = 0};
         continue;
      }
      r->ncomp = ncomp_of(c, bd->type);
      if (!r->ncomp)
         return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: stage input is not float/float2/3/4");
      copy_name(name, sizeof(name), bd->arg_name, "in");
      nir_variable *var = nir_variable_create(c->b.shader, nir_var_shader_in,
                                              float_type(r->ncomp), name);
      if (!var)
         return bad(c, A2I_E_NIR, "air_to_nir: cannot create input variable");
      if (bd->kind == A2I_IN_VERTEX_INPUT) {
         r->slot = VERT_ATTRIB_GENERIC0 + bd->location;
      } else {
         if (nvar_in >= MAX_VAR_SLOTS)
            return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: more than 32 fragment inputs");
         r->slot = VARYING_SLOT_VAR0 + nvar_in++;
         var->data.interpolation = bd->interp == A2I_INTERP_NO_PERSPECTIVE
                                      ? INTERP_MODE_NOPERSPECTIVE
                                      : INTERP_MODE_SMOOTH;
      }
      var->data.location = (int)r->slot;
      snprintf(r->slot_name, sizeof(r->slot_name), "%s",
               bd->kind == A2I_IN_VERTEX_INPUT
                  ? gl_vert_attrib_name((gl_vert_attrib)r->slot)
                  : gl_varying_slot_name_for_stage((gl_varying_slot)r->slot, c->b.shader->info.stage));
      c->val[li] = nir_load_var(&c->b, var);
   }
   for (uint32_t i = 0; i < io->nout; i++) {
      const a2i_out_binding *bd = &io->out[i];
      a2i_nir_io *r = &info->out[i];
      r->kind = bd->kind;
      r->location = bd->rt_index;
      r->user = bd->user;
      r->ncomp = ncomp_of(c, bd->type);
      if (!r->ncomp)
         return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: output is not float/float2/3/4");
      switch (bd->kind) {
      case A2I_OUT_POSITION: r->slot = VARYING_SLOT_POS; break;
      case A2I_OUT_RENDER_TARGET: r->slot = FRAG_RESULT_DATA0 + bd->rt_index; break;
      default:
         if (nvar_out >= MAX_VAR_SLOTS)
            return bad(c, A2I_E_UNSUPPORTED_BINDING, "air_to_nir: more than 32 vertex outputs");
         r->slot = VARYING_SLOT_VAR0 + nvar_out++;
         break;
      }
      copy_name(name, sizeof(name), bd->arg_name, "out");
      nir_variable *var = nir_variable_create(c->b.shader, nir_var_shader_out,
                                              float_type(r->ncomp), name);
      if (!var)
         return bad(c, A2I_E_NIR, "air_to_nir: cannot create output variable");
      var->data.location = (int)r->slot;
      snprintf(r->slot_name, sizeof(r->slot_name), "%s",
               bd->kind == A2I_OUT_RENDER_TARGET
                  ? gl_frag_result_name((gl_frag_result)r->slot)
                  : gl_varying_slot_name_for_stage((gl_varying_slot)r->slot, c->b.shader->info.stage));
      c->out_var[i] = var;
   }
   return A2I_OK;
}

static a2i_status
store_output(ctx *c, uint32_t i, nir_def *v)
{
   if (!v)
      return A2I_OK; /* undefined member: the output is left unwritten */
   if (v->num_components != c->info->out[i].ncomp || v->bit_size != 32)
      return bad(c, A2I_E_NIR, "air_to_nir: output value shape differs from its binding");
   nir_store_var(&c->b, c->out_var[i], v, nir_component_mask(v->num_components));
   return A2I_OK;
}

/* ------------------------------------------------------------ main loop */
static a2i_status
emit_inst(ctx *c, uint32_t k, const a2i_inst *in)
{
   const a2i_module *m = c->m;
   const a2i_function *f = c->f;
   uint32_t li = in->value_id != A2I_NONE ? local_index(c, in->value_id) : UINT32_MAX;
   nir_def *x = NULL, *y = NULL, *r = NULL;
   a2i_status st;
   int64_t iv;

   if (in->bb != 0)
      return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "control flow");
   if (in->value_id != A2I_NONE && li == UINT32_MAX)
      return bad_inst(c, k, A2I_E_NIR, "result value id out of range");

   switch (in->code) {
   case A2I_FC_BINOP:
   case A2I_FC_UNOP: {
      if (in->nops < (in->code == A2I_FC_BINOP ? 2u : 1u))
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "missing operand");
      if ((st = get_value(c, in->ops[0], &x)) ||
          (in->code == A2I_FC_BINOP && (st = get_value(c, in->ops[1], &y))))
         return st;
      uint32_t saved = c->b.fp_math_ctrl;
      c->b.fp_math_ctrl = fp_ctrl(in->flags);
      if (in->code == A2I_FC_UNOP && in->op == 0)
         r = nir_fneg(&c->b, x);
      else if (in->code == A2I_FC_BINOP && in->op == 0)
         r = nir_fadd(&c->b, x, y);
      else if (in->code == A2I_FC_BINOP && in->op == 1)
         r = nir_fsub(&c->b, x, y);
      else if (in->code == A2I_FC_BINOP && in->op == 2)
         r = nir_fmul(&c->b, x, y);
      c->b.fp_math_ctrl = saved;
      if (!r)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "fp opcode outside MVP-AIR-0");
      if (in->flags)
         c->info->alu_relaxed++;
      else
         c->info->alu_strict++;
      break;
   }
   case A2I_FC_SHUFFLEVEC: {
      uint32_t na = ncomp_of(c, a2i_value_type(m, f, in->ops[0]));
      uint32_t nr = ncomp_of(c, in->type);
      if (in->nops != 3 || na < 2 || nr < 2)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "shufflevector shape");
      if ((st = get_value(c, in->ops[0], &x)) || (st = get_value(c, in->ops[1], &y)))
         return st;
      nir_def *ch[4];
      for (uint32_t i = 0; i < nr; i++) {
         if ((st = mask_elem(c, in->ops[2], i, &iv)))
            return st;
         if (iv < 0)
            ch[i] = nir_undef(&c->b, 1, 32);
         else if (iv < (int64_t)na)
            ch[i] = nir_channel(&c->b, x, (unsigned)iv);
         else if (iv < 2 * (int64_t)na)
            ch[i] = nir_channel(&c->b, y, (unsigned)(iv - na));
         else
            return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "shufflevector mask out of range");
      }
      r = nir_vec(&c->b, ch, nr);
      break;
   }
   case A2I_FC_EXTRACTELT: {
      uint32_t na = ncomp_of(c, a2i_value_type(m, f, in->ops[0]));
      if (in->nops != 2 || !a2i_const_int(m, f, in->ops[1], &iv) || iv < 0 || iv >= (int64_t)na)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "extractelement index");
      if ((st = get_value(c, in->ops[0], &x)))
         return st;
      r = nir_channel(&c->b, x, (unsigned)iv);
      break;
   }
   case A2I_FC_INSERTELT: {
      uint32_t na = ncomp_of(c, in->type);
      if (in->nops != 3 || !a2i_const_int(m, f, in->ops[2], &iv) || iv < 0 || iv >= (int64_t)na)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "insertelement index");
      if ((st = get_value(c, in->ops[0], &x)) || (st = get_value(c, in->ops[1], &y)))
         return st;
      if (y->num_components != 1)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "insertelement scalar");
      r = nir_vector_insert_imm(&c->b, x, y, (unsigned)iv);
      break;
   }
   case A2I_FC_EXTRACTVAL:
   case A2I_FC_INSERTVAL: {
      nir_def **mem;
      const a2i_type *rt = a2i_type_get(m, c->io->ret_type);
      if (in->nidx != 1 || !rt || rt->kind != A2I_T_STRUCT || in->idx[0] >= rt->nmembers)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "aggregate index");
      if ((st = get_agg(c, in->ops[0], &mem)))
         return st;
      uint32_t fi = (uint32_t)in->idx[0];
      if (in->code == A2I_FC_EXTRACTVAL) {
         r = mem[fi] ? mem[fi] : nir_undef(&c->b, ncomp_of(c, rt->members[fi]), 32);
         break;
      }
      nir_def **nm = ralloc_array(c->tmp, nir_def *, rt->nmembers);
      if (!nm)
         return bad(c, A2I_E_NIR, "air_to_nir: out of memory");
      memcpy(nm, mem, sizeof(nir_def *) * rt->nmembers);
      if ((st = get_value(c, in->ops[1], &nm[fi])))
         return st;
      if (nm[fi]->num_components != ncomp_of(c, rt->members[fi]))
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "inserted field shape");
      c->agg[li] = nm;
      return A2I_OK;
   }
   case A2I_FC_GEP: {
      uint32_t bl = local_index(c, in->ops[0]);
      int64_t off;
      if (bl == UINT32_MAX || !c->ptr[bl].valid)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_BINDING, "getelementptr base is not a constant buffer");
      if ((st = a2i_gep_offset(m, f, c->L, in, &off, c->d)))
         return st;
      ptr_val p = c->ptr[bl];
      if (__builtin_add_overflow(p.offset, off, &p.offset) || p.offset < 0)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "constant-buffer offset out of range");
      c->ptr[li] = p;
      return A2I_OK;
   }
   case A2I_FC_LOAD: {
      uint32_t pl = local_index(c, in->ops[0]);
      uint32_t n = ncomp_of(c, in->type);
      if (in->is_volatile || !n || pl == UINT32_MAX || !c->ptr[pl].valid)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_BINDING, "load is not a float read of a constant buffer");
      ptr_val p = c->ptr[pl];
      const a2i_in_binding *bd = &c->io->in[p.binding];
      uint64_t bytes = 4ull * n;
      if (p.offset > INT32_MAX - (int64_t)bytes || p.offset % 4)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "constant-buffer load offset not dword aligned or too large");
      if (bd->has_buffer_size && (uint64_t)p.offset + bytes > bd->buffer_size)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_BINDING, "constant-buffer load beyond air.buffer_size");
      /* the bound buffer base is aligned to its declared type alignment */
      uint32_t base_align = 4;
      if (bd->arg_type_align >= 4 && bd->arg_type_align <= 256 &&
          !(bd->arg_type_align & (bd->arg_type_align - 1)))
         base_align = (uint32_t)bd->arg_type_align;
      r = nir_load_ubo(&c->b, n, 32, nir_imm_int(&c->b, (int)bd->location),
                       nir_imm_int(&c->b, (int)p.offset), .align_mul = base_align,
                       .align_offset = (uint32_t)p.offset % base_align, .range_base = 0,
                       .range = ~0u);
      if (bd->location + 1 > c->info->num_ubos)
         c->info->num_ubos = bd->location + 1;
      c->info->ubo_loads++;
      break;
   }
   case A2I_FC_RET: {
      if (in->nops != 1)
         return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "ret without value");
      if (c->io->ret_is_struct) {
         nir_def **mem;
         if ((st = get_agg(c, in->ops[0], &mem)))
            return st;
         for (uint32_t i = 0; i < c->io->nout; i++)
            if ((st = store_output(c, i, mem[i])))
               return st;
      } else {
         if ((st = get_value(c, in->ops[0], &x)) || (st = store_output(c, 0, x)))
            return st;
      }
      return A2I_OK;
   }
   default:
      return bad_inst(c, k, A2I_E_UNSUPPORTED_IR, "opcode outside MVP-AIR-0");
   }
   if (li == UINT32_MAX)
      return bad_inst(c, k, A2I_E_NIR, "value-producing instruction without result id");
   if (ncomp_of(c, in->type) != r->num_components || r->bit_size != 32)
      return bad_inst(c, k, A2I_E_NIR, "translated value shape differs from the LLVM type");
   c->val[li] = r;
   return A2I_OK;
}

a2i_status
a2i_air_to_nir(const a2i_module *m, const a2i_iface *io, const a2i_layout *L, const char *name,
               const struct nir_shader_compiler_options *opts, struct nir_shader **out,
               a2i_nir_info *info, a2i_diag *d)
{
   *out = NULL;
   memset(info, 0, sizeof(*info));
   if (io->func >= m->functions.len)
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "air_to_nir: no entry function");
   ctx c;
   memset(&c, 0, sizeof(c));
   c.m = m;
   c.f = &A2I_VEC_AT(&m->functions, a2i_function, io->func);
   c.io = io;
   c.L = L;
   c.info = info;
   c.d = d;
   if (c.f->nblocks != 1)
      return a2i_fail(d, A2I_E_UNSUPPORTED_IR, "air_to_nir: control flow outside MVP-AIR-0");
   c.nlocals = c.f->locals.len;
   c.tmp = ralloc_context(NULL);
   if (!c.tmp)
      return a2i_fail(d, A2I_E_NIR, "air_to_nir: out of memory");
   c.val = rzalloc_array(c.tmp, nir_def *, c.nlocals + 1);
   c.agg = rzalloc_array(c.tmp, nir_def **, c.nlocals + 1);
   c.ptr = rzalloc_array(c.tmp, ptr_val, c.nlocals + 1);
   if (!c.val || !c.agg || !c.ptr) {
      ralloc_free(c.tmp);
      return a2i_fail(d, A2I_E_NIR, "air_to_nir: out of memory");
   }
   mesa_shader_stage stage =
      io->stage == A2I_STAGE_VERTEX ? MESA_SHADER_VERTEX : MESA_SHADER_FRAGMENT;
   c.b = nir_builder_init_simple_shader(stage, opts, "%s", name);
   if (!c.b.shader) {
      ralloc_free(c.tmp);
      return a2i_fail(d, A2I_E_NIR, "air_to_nir: cannot create nir_shader");
   }
   a2i_status st = declare_io(&c);
   for (uint32_t k = 0; !st && k < c.f->insts.len; k++)
      st = emit_inst(&c, k, &A2I_VEC_AT(&c.f->insts, a2i_inst, k));
   if (!st) {
      const a2i_inst *last = c.f->insts.len
                                ? &A2I_VEC_AT(&c.f->insts, a2i_inst, c.f->insts.len - 1)
                                : NULL;
      if (!last || last->code != A2I_FC_RET)
         st = a2i_fail(d, A2I_E_UNSUPPORTED_IR, "air_to_nir: entry block does not end in ret");
   }
   ralloc_free(c.tmp);
   if (st) {
      ralloc_free(c.b.shader);
      return st;
   }
   c.b.shader->info.num_ubos = (uint8_t)info->num_ubos;
   *out = c.b.shader;
   return A2I_OK;
}
