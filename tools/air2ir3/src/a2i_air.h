/* SPDX-License-Identifier: MIT */
/*
 * a2i_air.h - AIR-level services over the decoded module (freestanding):
 *   - DataLayout parsing, type alloc size / ABI alignment, constant GEP offsets
 *   - entry-point interface from !air.vertex / !air.fragment metadata
 *   - MVP-AIR-0 subset validation (first violation -> explicit status)
 *   - LLVM mnemonics for diagnostics and canonical dumps
 */
#ifndef A2I_AIR_H
#define A2I_AIR_H

#include "a2i_module.h"

/* ------------------------------------------------------------- layout */
typedef struct a2i_layout_spec {
   char kind;      /* 'i', 'f', 'v', 'a' */
   uint32_t bits;  /* size in bits (0 for 'a') */
   uint32_t abi;   /* ABI alignment in bytes */
} a2i_layout_spec;

typedef struct a2i_layout {
   bool big_endian;
   uint32_t ptr_bytes, ptr_abi;
   uint32_t n;
   a2i_layout_spec spec[64];
} a2i_layout;

a2i_status a2i_layout_parse(const char *dl, a2i_layout *L, a2i_diag *d);
a2i_status a2i_type_layout(const a2i_module *m, const a2i_layout *L, uint32_t type,
                           uint64_t *alloc_size, uint64_t *abi_align, a2i_diag *d);
/* Constant byte offset of a GEP (all indices must be constant integers). */
a2i_status a2i_gep_offset(const a2i_module *m, const a2i_function *f, const a2i_layout *L,
                          const a2i_inst *gep, int64_t *offset, a2i_diag *d);
/* Constant integer value of a value id (INT or NULL integer). */
bool a2i_const_int(const a2i_module *m, const a2i_function *f, uint32_t id, int64_t *v);

/* ---------------------------------------------------------- interface */
#define A2I_MAX_BINDINGS 32u

enum { A2I_STAGE_VERTEX = 0, A2I_STAGE_FRAGMENT = 1 };
enum { A2I_OUT_POSITION, A2I_OUT_VERTEX_OUTPUT, A2I_OUT_RENDER_TARGET };
enum { A2I_IN_VERTEX_INPUT, A2I_IN_BUFFER, A2I_IN_FRAGMENT_INPUT };
enum { A2I_INTERP_PERSPECTIVE = 0, A2I_INTERP_NO_PERSPECTIVE = 1 };

typedef struct a2i_bstr {
   const uint8_t *s;
   uint32_t n;
} a2i_bstr;

typedef struct a2i_out_binding {
   uint8_t kind;
   uint32_t rt_index;   /* RENDER_TARGET */
   a2i_bstr user;       /* VERTEX_OUTPUT link name, e.g. generated(5colorDv4_f) */
   a2i_bstr type_name, arg_name;
   uint32_t type;       /* LLVM type of the returned field */
} a2i_out_binding;

typedef struct a2i_in_binding {
   uint8_t kind;
   uint32_t argno;
   uint32_t location;   /* vertex attribute or buffer index */
   uint32_t addrspace;
   bool has_buffer_size;
   uint64_t buffer_size, arg_type_size, arg_type_align;
   a2i_bstr user, type_name, arg_name;
   uint8_t interp;
   bool unused;
   uint32_t type;       /* LLVM type of the argument */
} a2i_in_binding;

typedef struct a2i_iface {
   uint8_t stage;
   uint32_t func;       /* index into module functions */
   uint32_t ret_type;
   bool ret_is_struct;
   uint32_t nout, nin;
   a2i_out_binding out[A2I_MAX_BINDINGS];
   a2i_in_binding in[A2I_MAX_BINDINGS];
} a2i_iface;

/* Locates the single defined function and its stage/interface. */
a2i_status a2i_iface_extract(const a2i_module *m, a2i_iface *io, a2i_diag *d);

/* ------------------------------------------------------------- subset */
typedef struct a2i_subset_report {
   uint32_t insts;
   uint32_t first_bad_inst; /* A2I_NONE when clean */
} a2i_subset_report;

/* MVP-AIR-0: also checks the bitcode function name against the MTLB NAME. */
a2i_status a2i_subset_check(const a2i_module *m, const a2i_iface *io, const a2i_layout *L,
                            const char *mtlb_name, uint32_t mtlb_name_len,
                            a2i_subset_report *rep, a2i_diag *d);

/* ------------------------------------------------------------- names */
const char *a2i_inst_mnemonic(const a2i_inst *in, bool is_fp);
const char *a2i_cast_name(uint32_t op);
const char *a2i_cmp_name(uint32_t pred);
bool a2i_type_is_fp(const a2i_module *m, uint32_t type); /* scalar or vector element */
bool a2i_type_is_float_vec(const a2i_module *m, uint32_t type, uint32_t *ncomp);

#endif
