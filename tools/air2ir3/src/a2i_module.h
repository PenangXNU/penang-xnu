/* SPDX-License-Identifier: MIT */
/*
 * a2i_module.h - LLVM IR module model decoded from AIR bitcode (freestanding).
 *
 * Covers the block/record vocabulary observed in the 22H352 corpus
 * (metalfe-32023.622): IDENTIFICATION, MODULE (version 2, relative value
 * ids, STRTAB names), TYPE_BLOCK_NEW (typed and opaque pointers), CONSTANTS,
 * METADATA (STRINGS/VALUE/NODE/NAME/NAMED_NODE), FUNCTION_BLOCK. Types of
 * loads and GEP source elements are taken from the explicit record fields,
 * never from pointer pointee types (opaque-pointer safe).
 */
#ifndef A2I_MODULE_H
#define A2I_MODULE_H

#include "a2i_bitstream.h"

#define A2I_NONE UINT32_MAX

typedef enum a2i_type_kind {
   A2I_T_VOID, A2I_T_HALF, A2I_T_FLOAT, A2I_T_DOUBLE, A2I_T_LABEL, A2I_T_METADATA,
   A2I_T_INT, A2I_T_PTR, A2I_T_VECTOR, A2I_T_ARRAY, A2I_T_STRUCT, A2I_T_FUNCTION,
   A2I_T_OPAQUE, A2I_T_TOKEN, A2I_T_OTHER,
} a2i_type_kind;

typedef struct a2i_type {
   uint8_t kind;
   bool packed, vararg, named, scalable;
   uint32_t bits;      /* INT */
   uint32_t addrspace; /* PTR */
   uint32_t pointee;   /* PTR (typed pointer records) or A2I_NONE */
   uint64_t count;     /* VECTOR / ARRAY */
   uint32_t elem;      /* VECTOR / ARRAY */
   uint32_t ret;       /* FUNCTION */
   uint32_t nmembers;  /* STRUCT members / FUNCTION params */
   uint32_t *members;
   const char *name;   /* named STRUCT / OPAQUE */
} a2i_type;

enum { A2I_V_GLOBALVAR, A2I_V_FUNCTION, A2I_V_ALIAS, A2I_V_CONST, A2I_V_ARG, A2I_V_INST };

typedef struct a2i_value {
   uint8_t kind;
   uint32_t type; /* globals: their pointer type, A2I_NONE if absent from the type table */
   uint32_t ref;  /* index into globals / consts / function insts / arg number */
} a2i_value;

enum { A2I_C_NULL, A2I_C_UNDEF, A2I_C_POISON, A2I_C_INT, A2I_C_FLOAT, A2I_C_AGG, A2I_C_DATA,
       A2I_C_CE_CAST, A2I_C_OTHER };

typedef struct a2i_const {
   uint8_t kind;
   uint32_t type;
   uint32_t code;     /* CST_CODE_* */
   int64_t ival;      /* INT, sign-extended */
   uint64_t bits;     /* FLOAT raw bits */
   uint32_t n;
   uint64_t *elems;   /* AGG: absolute value ids; DATA: raw element values */
   uint32_t cast_opc, cast_operand; /* CE_CAST */
} a2i_const;

typedef struct a2i_global {
   bool is_function;
   uint32_t value_id;
   uint64_t strtab_off, strtab_size;
   const char *name; /* resolved from STRTAB, NUL-terminated copy */
   uint32_t value_type; /* GLOBALVAR content type / FUNCTION function type */
   uint32_t func;       /* index in functions if is_function */
} a2i_global;

/* FUNC_CODE_* record codes used by the reader */
enum {
   A2I_FC_DECLAREBLOCKS = 1, A2I_FC_BINOP = 2, A2I_FC_CAST = 3, A2I_FC_GEP_OLD = 4,
   A2I_FC_SELECT = 5, A2I_FC_EXTRACTELT = 6, A2I_FC_INSERTELT = 7, A2I_FC_SHUFFLEVEC = 8,
   A2I_FC_CMP = 9, A2I_FC_RET = 10, A2I_FC_BR = 11, A2I_FC_SWITCH = 12, A2I_FC_INVOKE = 13,
   A2I_FC_UNREACHABLE = 15, A2I_FC_PHI = 16, A2I_FC_ALLOCA = 19, A2I_FC_LOAD = 20,
   A2I_FC_VAARG = 23, A2I_FC_STORE_OLD = 24, A2I_FC_EXTRACTVAL = 26, A2I_FC_INSERTVAL = 27,
   A2I_FC_CMP2 = 28, A2I_FC_VSELECT = 29, A2I_FC_INBOUNDS_GEP_OLD = 30,
   A2I_FC_DEBUG_LOC_AGAIN = 33, A2I_FC_CALL = 34, A2I_FC_DEBUG_LOC = 35, A2I_FC_FENCE = 36,
   A2I_FC_LOADATOMIC = 41, A2I_FC_GEP = 43, A2I_FC_STORE = 44, A2I_FC_STOREATOMIC = 45,
   A2I_FC_OPERAND_BUNDLE = 55, A2I_FC_UNOP = 56, A2I_FC_FREEZE = 58,
   A2I_FC_BLOCKADDR_USERS = 60,
};

typedef struct a2i_inst {
   uint32_t code;     /* A2I_FC_* */
   uint32_t op;       /* binop/cast/unop opcode, cmp predicate */
   uint64_t flags;    /* fast-math / inbounds / exact flags as recorded */
   uint32_t type;     /* result type, A2I_NONE if the record defines no value */
   uint32_t value_id; /* absolute value id defined, or A2I_NONE */
   uint32_t xtype;    /* GEP source element / LOAD / ALLOCA type, CALL function type */
   uint32_t nops;
   uint32_t *ops;     /* absolute value ids; for BR/SWITCH/PHI see bbs */
   uint32_t nidx;
   uint64_t *idx;     /* EXTRACTVAL/INSERTVAL indices, PHI/BR/SWITCH block ids */
   uint64_t align;    /* LOAD/STORE/ALLOCA raw alignment field */
   bool is_volatile;
   uint32_t bb;
} a2i_inst;

typedef struct a2i_function {
   uint32_t global;   /* index in globals */
   uint32_t type;     /* FUNCTION type */
   bool is_proto;
   uint64_t linkage, cc;
   bool has_body;
   uint32_t nblocks;
   uint32_t nargs;
   a2i_vec insts;     /* a2i_inst */
   a2i_vec locals;    /* a2i_value, ids n_module_values + i */
} a2i_function;

enum { A2I_MD_STRING, A2I_MD_VALUE, A2I_MD_NODE, A2I_MD_OTHER };

typedef struct a2i_md {
   uint8_t kind;
   bool distinct;
   uint32_t code;
   const uint8_t *str; /* STRING (not NUL-terminated) */
   uint32_t str_len;
   uint32_t type, value; /* VALUE */
   uint32_t nops;
   uint32_t *ops;        /* NODE: md id + 1, 0 = null */
} a2i_md;

typedef struct a2i_named_md {
   const char *name;
   uint32_t nops;
   uint32_t *ops; /* md ids */
} a2i_named_md;

typedef struct a2i_module {
   a2i_arena *ar;
   const char *producer;
   bool has_epoch;
   uint64_t epoch;
   uint64_t version;
   const char *triple, *datalayout, *source_filename;
   a2i_vec types;     /* a2i_type */
   a2i_vec values;    /* module-level a2i_value */
   a2i_vec consts;    /* a2i_const (module and function scope) */
   a2i_vec globals;   /* a2i_global */
   a2i_vec functions; /* a2i_function */
   a2i_vec mds;       /* a2i_md */
   a2i_vec named;     /* a2i_named_md */
   const uint8_t *strtab;
   uint32_t strtab_len;
   uint32_t n_module_values;
   uint32_t bodies_seen;
   uint64_t record_count, block_count;
} a2i_module;

a2i_status a2i_module_parse(const uint8_t *bc, uint64_t len, uint64_t file_offset,
                            a2i_arena *ar, a2i_module *m, a2i_diag *d);

const a2i_type *a2i_type_get(const a2i_module *m, uint32_t id);
/* value lookup (module values, then the given function's locals) */
const a2i_value *a2i_value_get(const a2i_module *m, const a2i_function *f, uint32_t id);
uint32_t a2i_value_type(const a2i_module *m, const a2i_function *f, uint32_t id);
const a2i_const *a2i_value_const(const a2i_module *m, const a2i_function *f, uint32_t id);
const a2i_inst *a2i_value_inst(const a2i_module *m, const a2i_function *f, uint32_t id);
const char *a2i_function_name(const a2i_module *m, const a2i_function *f);
const a2i_md *a2i_md_get(const a2i_module *m, uint32_t id);
const a2i_named_md *a2i_named_md_find(const a2i_module *m, const char *name);

#endif
