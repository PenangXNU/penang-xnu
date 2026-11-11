/* SPDX-License-Identifier: MIT */
/*
 * a2i_bitstream.h - LLVM bitstream container reader (freestanding).
 *
 * Implements the generic layer documented in LLVM's "LLVM Bitcode File
 * Format": bitcode wrapper (0x0B17C0DE), 'BC' 0xC0DE magic, abbreviation IDs
 * (END_BLOCK, ENTER_SUBBLOCK, DEFINE_ABBREV, UNABBREV_RECORD), abbreviation
 * operand encodings (literal, fixed, VBR, array, char6, blob) and the
 * BLOCKINFO block. All reads are bounds-checked; any inconsistency is
 * E_BITSTREAM with the bit offset.
 */
#ifndef A2I_BITSTREAM_H
#define A2I_BITSTREAM_H

#include "a2i_core.h"

#define A2I_BS_MAX_DEPTH 32u
#define A2I_BS_MAX_ABBREV_OPS 256u
#define A2I_BS_MAX_RECORD_OPS (1u << 22)

enum {
   A2I_ENC_LITERAL = 0,
   A2I_ENC_FIXED = 1,
   A2I_ENC_VBR = 2,
   A2I_ENC_ARRAY = 3,
   A2I_ENC_CHAR6 = 4,
   A2I_ENC_BLOB = 5,
};

typedef struct a2i_bs_abbrev_op {
   uint8_t enc;
   uint64_t value; /* literal value or bit width */
} a2i_bs_abbrev_op;

typedef struct a2i_bs_abbrev {
   uint32_t nops;
   a2i_bs_abbrev_op *ops;
} a2i_bs_abbrev;

typedef struct a2i_bs_scope {
   uint32_t block_id;
   uint32_t abbrev_width;
   uint64_t end_bit;
   a2i_vec abbrevs; /* a2i_bs_abbrev* */
} a2i_bs_scope;

typedef struct a2i_bs_blockinfo {
   uint32_t block_id;
   a2i_vec abbrevs; /* a2i_bs_abbrev* */
} a2i_bs_blockinfo;

typedef enum { A2I_BS_RECORD, A2I_BS_SUBBLOCK, A2I_BS_END } a2i_bs_kind;

typedef struct a2i_bs_entry {
   a2i_bs_kind kind;
   uint32_t id;     /* record code or sub-block id */
   uint32_t abbrev; /* 3 = unabbreviated, >= 4 defined */
   uint32_t nops;
   const uint64_t *ops; /* valid until the next a2i_bs_next */
   const uint8_t *blob;
   uint32_t blob_len;
   bool has_blob;
   uint64_t bit_offset;
} a2i_bs_entry;

typedef struct a2i_bs {
   const uint8_t *data;
   uint64_t end_bits;
   uint64_t pos;
   uint64_t file_offset; /* byte offset of data[0] in the input file (diagnostics) */
   uint32_t wrapper_cputype;
   bool has_wrapper;
   a2i_arena *ar;
   a2i_diag *d;
   a2i_bs_scope scopes[A2I_BS_MAX_DEPTH + 1];
   uint32_t depth; /* scopes[0] is the top level */
   a2i_vec blockinfos;
   a2i_vec ops; /* uint64_t scratch */
   uint64_t records, blocks;
} a2i_bs;

/* Validates wrapper + magic and positions at the first top-level entry. */
a2i_status a2i_bs_init(a2i_bs *bs, const uint8_t *bc, uint64_t len, uint64_t file_offset,
                       a2i_arena *ar, a2i_diag *d);
/* Next entry of the current block. DEFINE_ABBREV is consumed internally. */
a2i_status a2i_bs_next(a2i_bs *bs, a2i_bs_entry *e);
/* After a SUBBLOCK entry: enter it, or skip it, or process it as BLOCKINFO. */
a2i_status a2i_bs_enter(a2i_bs *bs, uint32_t block_id);
a2i_status a2i_bs_skip(a2i_bs *bs);
a2i_status a2i_bs_read_blockinfo(a2i_bs *bs);

/* Decode a record's ops as a string of byte values (TRIPLE, DATALAYOUT...). */
char *a2i_bs_ops_string(a2i_arena *ar, const uint64_t *ops, uint32_t n, uint32_t first);

#endif
