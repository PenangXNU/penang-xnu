/* SPDX-License-Identifier: MIT */
/*
 * ir3_emit.h - NIR -> Freedreno ir3 machine code through the pinned,
 * unmodified Mesa backend (host layer). The handle is opaque so callers do
 * not need Mesa headers.
 *
 * Target policy: Adreno 619, chip_id 0x06010900 (the id declared by the
 * target device tree, as used by the M7 BACKEND_ONLY component). Opening
 * fails with E_BACKEND unless Mesa resolves the chip to compiler generation 6
 * (a6xx); no a7xx/a8xx assumption is made anywhere.
 */
#ifndef A2I_IR3_EMIT_H
#define A2I_IR3_EMIT_H

#include <stdio.h>

#include "a2i_core.h"

#define A2I_A619_CHIP_ID 0x06010900ull
#define A2I_IR3_MAX_IO 48u

struct nir_shader;
struct nir_shader_compiler_options;
typedef struct a2i_ir3 a2i_ir3;

typedef struct a2i_ir3_in {
   uint32_t slot, regid, compmask, inloc;
   bool sysval, bary, flat, half;
} a2i_ir3_in;

typedef struct a2i_ir3_outp {
   uint32_t slot, regid;
   bool half;
} a2i_ir3_outp;

typedef struct a2i_ir3_out {
   uint8_t *bin;          /* malloc'd copy of the encoded variant */
   uint32_t size;         /* bytes */
   uint32_t sizedwords, instrs_count, constlen, instrlen;
   int max_reg, max_half_reg;
   bool has_binning;      /* VS: Mesa also produced the binning-pass variant */
   uint32_t binning_size;
   uint32_t ninputs, noutputs;
   a2i_ir3_in inputs[A2I_IR3_MAX_IO];
   a2i_ir3_outp outputs[A2I_IR3_MAX_IO];
   /* UBO byte ranges ir3 promoted to const registers: the driver must upload
    * block[start, end) to the const file at `offset` (vec4 units x 16 bytes) */
   uint32_t nubo_ranges;
   struct {
      uint32_t block, start, end, offset;
   } ubo_ranges[8];
} a2i_ir3_out;

a2i_status a2i_ir3_open(a2i_ir3 **out, uint64_t chip_id, a2i_diag *d);
const struct nir_shader_compiler_options *a2i_ir3_nir_options(const a2i_ir3 *x);
uint64_t a2i_ir3_chip_id(const a2i_ir3 *x);
unsigned a2i_ir3_gen(const a2i_ir3 *x);
const char *a2i_ir3_gpu_name(const a2i_ir3 *x);

/* Prints `nir` (as produced by the frontend) to nir_log if non-NULL, applies
 * the ir3 I/O lowering and ir3_finalize_nir, prints the finalized NIR, then
 * compiles one variant with the default key. Takes ownership of `nir`. */
a2i_status a2i_ir3_compile(a2i_ir3 *x, struct nir_shader *nir, FILE *nir_log, a2i_ir3_out *out,
                           a2i_diag *d);
/* Decodes an encoded ir3 binary with Mesa's isaspec decoder (a6xx). */
void a2i_ir3_disasm(const uint8_t *bin, uint32_t size, FILE *f);
void a2i_ir3_out_free(a2i_ir3_out *o);
void a2i_ir3_close(a2i_ir3 *x);

#endif
