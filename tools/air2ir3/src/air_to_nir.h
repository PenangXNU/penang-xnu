/* SPDX-License-Identifier: MIT */
/*
 * air_to_nir.h - MVP-AIR-0 module -> Mesa NIR (host layer, links Mesa libnir).
 *
 * The input must already have passed a2i_iface_extract() and
 * a2i_subset_check(); the translator still fails closed (E_UNSUPPORTED_IR /
 * E_UNSUPPORTED_BINDING) on anything outside the subset, and reports NIR
 * construction problems as E_NIR. Backend lowering (ir3 I/O, finalize) is not
 * done here: the result is generic NIR with I/O variables and load_ubo.
 *
 * Binding policy (reported to the caller, see a2i_nir_io):
 *   vertex_input  location L          -> VERT_ATTRIB_GENERIC0 + L
 *   buffer        location L (const)  -> load_ubo, UBO index L, byte offsets
 *                                        from the DataLayout (constant GEPs)
 *   fragment_input #k (declaration)   -> VARYING_SLOT_VAR0 + k, center,
 *                                        smooth / noperspective
 *   position                          -> VARYING_SLOT_POS
 *   vertex_output #k (declaration)    -> VARYING_SLOT_VAR0 + k
 *   render_target R                   -> FRAG_RESULT_DATA0 + R
 * Varyings are linked by declaration order, not by the AIR link name; a
 * pipeline that pairs stages must match the reported link names itself.
 *
 * Floating point: each fadd/fsub/fmul/fneg carries the LLVM fast-math flags
 * of its record; absent flags map to strict NIR (no contraction or
 * reassociation, signed zero / Inf / NaN preserved).
 */
#ifndef A2I_AIR_TO_NIR_H
#define A2I_AIR_TO_NIR_H

#include "a2i_air.h"

struct nir_shader;
struct nir_shader_compiler_options;

typedef struct a2i_nir_io {
   uint8_t kind;      /* A2I_IN_* (inputs) or A2I_OUT_* (outputs) */
   uint32_t argno;    /* inputs: AIR argument number */
   uint32_t location; /* vertex attribute / buffer index / render target */
   uint32_t slot;     /* gl_vert_attrib / gl_varying_slot / gl_frag_result; UBO index for buffers */
   uint32_t ncomp;
   uint8_t interp;    /* A2I_INTERP_* (fragment inputs) */
   bool unused;
   a2i_bstr user;     /* AIR link name of vertex_output / fragment_input */
   char slot_name[40]; /* Mesa name of `slot` ("UBO" for buffers) */
} a2i_nir_io;

typedef struct a2i_nir_info {
   uint32_t nin, nout;
   a2i_nir_io in[A2I_MAX_BINDINGS];
   a2i_nir_io out[A2I_MAX_BINDINGS];
   uint32_t num_ubos;
   uint32_t ubo_loads;
   uint32_t alu_strict;  /* fp ALU ops without fast-math flags (exact NIR) */
   uint32_t alu_relaxed; /* fp ALU ops with at least one fast-math flag */
} a2i_nir_info;

/* On success *out is a new nir_shader (ralloc root; the caller owns it). */
a2i_status a2i_air_to_nir(const a2i_module *m, const a2i_iface *io, const a2i_layout *L,
                          const char *name, const struct nir_shader_compiler_options *opts,
                          struct nir_shader **out, a2i_nir_info *info, a2i_diag *d);

#endif
