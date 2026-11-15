/* SPDX-License-Identifier: MIT */
#include "a2i_bitstream.h"

static a2i_status
bfail(a2i_bs *bs, const char *msg)
{
   return a2i_fail_at(bs->d, A2I_E_BITSTREAM, bs->file_offset * 8 + bs->pos, true, msg);
}

static bool
rd(a2i_bs *bs, unsigned n, uint64_t *out)
{
   if (n > 64 || bs->pos > bs->end_bits || n > bs->end_bits - bs->pos)
      return false;
   uint64_t v = 0, p = bs->pos;
   unsigned got = 0;
   while (got < n) {
      unsigned bit = (unsigned)(p & 7);
      unsigned take = 8 - bit;
      if (take > n - got)
         take = n - got;
      uint64_t chunk = ((uint64_t)bs->data[p >> 3] >> bit) & ((1u << take) - 1u);
      v |= chunk << got;
      got += take;
      p += take;
   }
   bs->pos = p;
   *out = v;
   return true;
}

static bool
rd_vbr(a2i_bs *bs, unsigned n, uint64_t *out)
{
   if (n < 2 || n > 32)
      return false;
   uint64_t hi = 1ull << (n - 1), v = 0, piece;
   unsigned shift = 0;
   for (;;) {
      if (!rd(bs, n, &piece))
         return false;
      uint64_t payload = piece & (hi - 1);
      if (payload) {
         if (shift >= 64 || (shift > 0 && (payload >> (64 - shift)) != 0))
            return false; /* value exceeds 64 bits */
         v |= payload << shift;
      }
      if (!(piece & hi))
         break;
      shift += n - 1;
      if (shift > 70)
         return false;
   }
   *out = v;
   return true;
}

static bool
align32(a2i_bs *bs)
{
   uint64_t p = (bs->pos + 31) & ~(uint64_t)31;
   if (p > bs->end_bits)
      return false;
   bs->pos = p;
   return true;
}

static uint64_t
char6(uint64_t v)
{
   if (v < 26)
      return 'a' + v;
   if (v < 52)
      return 'A' + (v - 26);
   if (v < 62)
      return '0' + (v - 52);
   return v == 62 ? '.' : '_';
}

a2i_status
a2i_bs_init(a2i_bs *bs, const uint8_t *bc, uint64_t len, uint64_t file_offset, a2i_arena *ar,
            a2i_diag *d)
{
   a2i_memset(bs, 0, sizeof(*bs));
   bs->ar = ar;
   bs->d = d;
   bs->file_offset = file_offset;
   uint64_t nbytes = len;
   const uint8_t *data = bc;
   if (len >= 4 && a2i_le32(bc) == 0x0B17C0DEu) {
      if (len < 20)
         return a2i_fail_at(d, A2I_E_BITSTREAM, file_offset, false, "bitcode wrapper truncated");
      if (a2i_le32(bc + 4) != 0)
         return a2i_fail_at(d, A2I_E_VERSION, file_offset + 4, false,
                            "unsupported bitcode wrapper version (only 0)");
      uint32_t off = a2i_le32(bc + 8), size = a2i_le32(bc + 12);
      if (off < 20 || !a2i_in_bounds(off, size, len))
         return a2i_fail_at(d, A2I_E_BITSTREAM, file_offset + 8, false,
                            "bitcode wrapper offset/size out of bounds");
      bs->has_wrapper = true;
      bs->wrapper_cputype = a2i_le32(bc + 16);
      data = bc + off;
      nbytes = size;
      bs->file_offset = file_offset + off;
   }
   if (nbytes < 4 || data[0] != 'B' || data[1] != 'C' || data[2] != 0xC0 || data[3] != 0xDE)
      return a2i_fail_at(d, A2I_E_BITSTREAM, bs->file_offset, false, "missing 'BC' 0xC0DE magic");
   if (nbytes % 4 != 0)
      return a2i_fail_at(d, A2I_E_BITSTREAM, bs->file_offset, false,
                         "bitcode size is not a multiple of 4 bytes");
   if (nbytes > ((uint64_t)1 << 32))
      return a2i_fail_at(d, A2I_E_RESOURCE, bs->file_offset, false, "bitcode too large");
   bs->data = data;
   bs->end_bits = nbytes * 8;
   bs->pos = 32;
   bs->scopes[0].block_id = UINT32_MAX;
   bs->scopes[0].abbrev_width = 2;
   bs->scopes[0].end_bit = bs->end_bits;
   a2i_vec_init(&bs->scopes[0].abbrevs, sizeof(a2i_bs_abbrev *));
   a2i_vec_init(&bs->blockinfos, sizeof(a2i_bs_blockinfo));
   a2i_vec_init(&bs->ops, sizeof(uint64_t));
   return A2I_OK;
}

static a2i_status
parse_abbrev(a2i_bs *bs, a2i_bs_abbrev **out)
{
   uint64_t n;
   if (!rd_vbr(bs, 5, &n))
      return bfail(bs, "truncated DEFINE_ABBREV");
   if (n == 0 || n > A2I_BS_MAX_ABBREV_OPS)
      return bfail(bs, "DEFINE_ABBREV operand count out of range");
   a2i_bs_abbrev *ab = a2i_arena_alloc(bs->ar, sizeof(*ab));
   a2i_bs_abbrev_op *ops = a2i_arena_alloc(bs->ar, sizeof(*ops) * (size_t)n);
   if (!ab || !ops)
      return a2i_fail(bs->d, A2I_E_RESOURCE, "arena exhausted (abbrev)");
   ab->nops = (uint32_t)n;
   ab->ops = ops;
   for (uint64_t i = 0; i < n; i++) {
      uint64_t lit, v, enc;
      if (!rd(bs, 1, &lit))
         return bfail(bs, "truncated abbrev operand");
      if (lit) {
         if (!rd_vbr(bs, 8, &v))
            return bfail(bs, "truncated abbrev literal");
         ops[i].enc = A2I_ENC_LITERAL;
         ops[i].value = v;
         continue;
      }
      if (!rd(bs, 3, &enc))
         return bfail(bs, "truncated abbrev encoding");
      if (enc == A2I_ENC_FIXED || enc == A2I_ENC_VBR) {
         if (!rd_vbr(bs, 5, &v))
            return bfail(bs, "truncated abbrev width");
         if (v == 0) { /* LLVM: fixed(0)/vbr(0) are a literal zero */
            ops[i].enc = A2I_ENC_LITERAL;
            ops[i].value = 0;
            continue;
         }
         if ((enc == A2I_ENC_FIXED && v > 64) || (enc == A2I_ENC_VBR && (v < 2 || v > 32)))
            return bfail(bs, "abbrev operand width out of range");
         ops[i].enc = (uint8_t)enc;
         ops[i].value = v;
      } else if (enc == A2I_ENC_ARRAY || enc == A2I_ENC_CHAR6 || enc == A2I_ENC_BLOB) {
         ops[i].enc = (uint8_t)enc;
         ops[i].value = 0;
      } else {
         return bfail(bs, "unknown abbrev operand encoding");
      }
   }
   *out = ab;
   return A2I_OK;
}

static a2i_bs_blockinfo *
find_blockinfo(a2i_bs *bs, uint32_t id, bool create)
{
   for (uint32_t i = 0; i < bs->blockinfos.len; i++)
      if (A2I_VEC_AT(&bs->blockinfos, a2i_bs_blockinfo, i).block_id == id)
         return &A2I_VEC_AT(&bs->blockinfos, a2i_bs_blockinfo, i);
   if (!create)
      return NULL;
   a2i_bs_blockinfo *bi = a2i_vec_push(bs->ar, &bs->blockinfos);
   if (bi) {
      bi->block_id = id;
      a2i_vec_init(&bi->abbrevs, sizeof(a2i_bs_abbrev *));
   }
   return bi;
}

static bool
push_op(a2i_bs *bs, uint64_t v)
{
   if (bs->ops.len >= A2I_BS_MAX_RECORD_OPS)
      return false;
   uint64_t *slot = a2i_vec_push(bs->ar, &bs->ops);
   if (!slot)
      return false;
   *slot = v;
   return true;
}

static a2i_status
read_scalar(a2i_bs *bs, const a2i_bs_abbrev_op *op, uint64_t *v)
{
   switch (op->enc) {
   case A2I_ENC_LITERAL:
      *v = op->value;
      return A2I_OK;
   case A2I_ENC_FIXED:
      return rd(bs, (unsigned)op->value, v) ? A2I_OK : bfail(bs, "truncated fixed field");
   case A2I_ENC_VBR:
      return rd_vbr(bs, (unsigned)op->value, v) ? A2I_OK : bfail(bs, "bad VBR field");
   case A2I_ENC_CHAR6:
      if (!rd(bs, 6, v))
         return bfail(bs, "truncated char6 field");
      *v = char6(*v);
      return A2I_OK;
   default:
      return bfail(bs, "array/blob used as scalar operand");
   }
}

static a2i_status
read_record(a2i_bs *bs, uint32_t abbrev_id, a2i_bs_entry *e)
{
   bs->ops.len = 0;
   e->kind = A2I_BS_RECORD;
   e->abbrev = abbrev_id;
   e->has_blob = false;
   e->blob = NULL;
   e->blob_len = 0;
   uint64_t v;
   if (abbrev_id == 3) {
      uint64_t code, n;
      if (!rd_vbr(bs, 6, &code) || !rd_vbr(bs, 6, &n))
         return bfail(bs, "truncated UNABBREV_RECORD header");
      if (n > A2I_BS_MAX_RECORD_OPS || n > bs->end_bits - bs->pos)
         return bfail(bs, "UNABBREV_RECORD operand count exceeds stream");
      if (code > UINT32_MAX)
         return bfail(bs, "record code out of range");
      e->id = (uint32_t)code;
      for (uint64_t i = 0; i < n; i++) {
         if (!rd_vbr(bs, 6, &v))
            return bfail(bs, "truncated UNABBREV_RECORD operand");
         if (!push_op(bs, v))
            return a2i_fail(bs->d, A2I_E_RESOURCE, "record operand storage exhausted");
      }
   } else {
      a2i_bs_scope *sc = &bs->scopes[bs->depth];
      uint32_t idx = abbrev_id - 4;
      if (idx >= sc->abbrevs.len)
         return bfail(bs, "reference to undefined abbreviation");
      const a2i_bs_abbrev *ab = A2I_VEC_AT(&sc->abbrevs, a2i_bs_abbrev *, idx);
      const a2i_bs_abbrev_op *ops = ab->ops;
      if (ops[0].enc == A2I_ENC_ARRAY || ops[0].enc == A2I_ENC_BLOB)
         return bfail(bs, "abbreviation starts with an array or blob");
      a2i_status st = read_scalar(bs, &ops[0], &v);
      if (st)
         return st;
      if (v > UINT32_MAX)
         return bfail(bs, "record code out of range");
      e->id = (uint32_t)v;
      for (uint32_t i = 1; i < ab->nops; i++) {
         const a2i_bs_abbrev_op *op = &ops[i];
         if (op->enc == A2I_ENC_ARRAY) {
            if (i + 2 != ab->nops)
               return bfail(bs, "array operand is not second to last");
            const a2i_bs_abbrev_op *elt = &ops[++i];
            if (elt->enc != A2I_ENC_FIXED && elt->enc != A2I_ENC_VBR && elt->enc != A2I_ENC_CHAR6)
               return bfail(bs, "invalid array element encoding");
            uint64_t cnt;
            if (!rd_vbr(bs, 6, &cnt))
               return bfail(bs, "truncated array length");
            if (cnt > A2I_BS_MAX_RECORD_OPS || cnt > bs->end_bits - bs->pos)
               return bfail(bs, "array length exceeds stream");
            for (uint64_t k = 0; k < cnt; k++) {
               st = read_scalar(bs, elt, &v);
               if (st)
                  return st;
               if (!push_op(bs, v))
                  return a2i_fail(bs->d, A2I_E_RESOURCE, "record operand storage exhausted");
            }
         } else if (op->enc == A2I_ENC_BLOB) {
            if (i + 1 != ab->nops)
               return bfail(bs, "blob operand is not last");
            uint64_t cnt;
            if (!rd_vbr(bs, 6, &cnt))
               return bfail(bs, "truncated blob length");
            if (!align32(bs))
               return bfail(bs, "blob alignment beyond stream");
            if (cnt > UINT32_MAX || cnt * 8 > bs->end_bits - bs->pos)
               return bfail(bs, "blob exceeds stream");
            e->blob = bs->data + (bs->pos >> 3);
            e->blob_len = (uint32_t)cnt;
            e->has_blob = true;
            bs->pos += cnt * 8;
            if (!align32(bs))
               return bfail(bs, "blob tail alignment beyond stream");
         } else {
            st = read_scalar(bs, op, &v);
            if (st)
               return st;
            if (!push_op(bs, v))
               return a2i_fail(bs->d, A2I_E_RESOURCE, "record operand storage exhausted");
         }
      }
   }
   if (bs->pos > bs->scopes[bs->depth].end_bit)
      return bfail(bs, "record crosses the end of its block");
   e->nops = bs->ops.len;
   e->ops = bs->ops.data;
   bs->records++;
   return A2I_OK;
}

a2i_status
a2i_bs_next(a2i_bs *bs, a2i_bs_entry *e)
{
   for (;;) {
      a2i_bs_scope *sc = &bs->scopes[bs->depth];
      e->bit_offset = bs->pos;
      e->nops = 0;
      e->has_blob = false;
      if (bs->depth == 0 && bs->pos >= bs->end_bits) {
         e->kind = A2I_BS_END;
         return A2I_OK;
      }
      uint64_t id;
      if (!rd(bs, sc->abbrev_width, &id))
         return bfail(bs, "truncated abbreviation id");
      if (id == 0) { /* END_BLOCK */
         if (bs->depth == 0)
            return bfail(bs, "END_BLOCK at top level");
         if (!align32(bs) || bs->pos != sc->end_bit)
            return bfail(bs, "END_BLOCK does not match the declared block length");
         bs->depth--;
         e->kind = A2I_BS_END;
         e->id = sc->block_id;
         return A2I_OK;
      }
      if (id == 1) { /* ENTER_SUBBLOCK */
         uint64_t bid;
         if (!rd_vbr(bs, 8, &bid) || bid > UINT32_MAX)
            return bfail(bs, "bad ENTER_SUBBLOCK block id");
         e->kind = A2I_BS_SUBBLOCK;
         e->id = (uint32_t)bid;
         return A2I_OK;
      }
      if (bs->depth == 0)
         return bfail(bs, "only sub-blocks are allowed at top level");
      if (id == 2) { /* DEFINE_ABBREV */
         a2i_bs_abbrev *ab;
         a2i_status st = parse_abbrev(bs, &ab);
         if (st)
            return st;
         a2i_bs_abbrev **slot = a2i_vec_push(bs->ar, &sc->abbrevs);
         if (!slot)
            return a2i_fail(bs->d, A2I_E_RESOURCE, "arena exhausted (abbrev list)");
         *slot = ab;
         continue;
      }
      return read_record(bs, (uint32_t)id, e);
   }
}

static a2i_status
block_header(a2i_bs *bs, uint32_t *width, uint64_t *end)
{
   uint64_t w, words;
   if (!rd_vbr(bs, 4, &w) || w == 0 || w > 32)
      return bfail(bs, "bad sub-block abbreviation width");
   if (!align32(bs) || !rd(bs, 32, &words))
      return bfail(bs, "truncated sub-block header");
   uint64_t e = bs->pos + words * 32;
   if (e < bs->pos || e > bs->scopes[bs->depth].end_bit)
      return bfail(bs, "sub-block length exceeds enclosing block");
   *width = (uint32_t)w;
   *end = e;
   return A2I_OK;
}

a2i_status
a2i_bs_enter(a2i_bs *bs, uint32_t block_id)
{
   uint32_t w;
   uint64_t end;
   a2i_status st = block_header(bs, &w, &end);
   if (st)
      return st;
   if (bs->depth >= A2I_BS_MAX_DEPTH)
      return bfail(bs, "block nesting too deep");
   a2i_bs_scope *sc = &bs->scopes[++bs->depth];
   sc->block_id = block_id;
   sc->abbrev_width = w;
   sc->end_bit = end;
   a2i_vec_init(&sc->abbrevs, sizeof(a2i_bs_abbrev *));
   a2i_bs_blockinfo *bi = find_blockinfo(bs, block_id, false);
   if (bi) {
      for (uint32_t i = 0; i < bi->abbrevs.len; i++) {
         a2i_bs_abbrev **slot = a2i_vec_push(bs->ar, &sc->abbrevs);
         if (!slot)
            return a2i_fail(bs->d, A2I_E_RESOURCE, "arena exhausted (scope abbrevs)");
         *slot = A2I_VEC_AT(&bi->abbrevs, a2i_bs_abbrev *, i);
      }
   }
   bs->blocks++;
   return A2I_OK;
}

a2i_status
a2i_bs_skip(a2i_bs *bs)
{
   uint32_t w;
   uint64_t end;
   a2i_status st = block_header(bs, &w, &end);
   if (st)
      return st;
   bs->pos = end;
   bs->blocks++;
   return A2I_OK;
}

a2i_status
a2i_bs_read_blockinfo(a2i_bs *bs)
{
   a2i_status st = a2i_bs_enter(bs, 0);
   if (st)
      return st;
   a2i_bs_blockinfo *cur = NULL;
   uint32_t my_depth = bs->depth;
   for (;;) {
      a2i_bs_scope *sc = &bs->scopes[bs->depth];
      uint64_t id;
      if (!rd(bs, sc->abbrev_width, &id))
         return bfail(bs, "truncated BLOCKINFO entry");
      if (id == 0) {
         if (!align32(bs) || bs->pos != sc->end_bit)
            return bfail(bs, "BLOCKINFO END_BLOCK length mismatch");
         bs->depth = my_depth - 1;
         return A2I_OK;
      }
      if (id == 1)
         return bfail(bs, "sub-block inside BLOCKINFO");
      if (id == 2) {
         if (!cur)
            return bfail(bs, "DEFINE_ABBREV in BLOCKINFO before SETBID");
         a2i_bs_abbrev *ab;
         st = parse_abbrev(bs, &ab);
         if (st)
            return st;
         a2i_bs_abbrev **slot = a2i_vec_push(bs->ar, &cur->abbrevs);
         if (!slot)
            return a2i_fail(bs->d, A2I_E_RESOURCE, "arena exhausted (blockinfo)");
         *slot = ab;
         continue;
      }
      a2i_bs_entry e;
      st = read_record(bs, (uint32_t)id, &e);
      if (st)
         return st;
      if (e.id == 1) { /* SETBID */
         if (e.nops < 1 || e.ops[0] > UINT32_MAX)
            return bfail(bs, "malformed SETBID");
         cur = find_blockinfo(bs, (uint32_t)e.ops[0], true);
         if (!cur)
            return a2i_fail(bs->d, A2I_E_RESOURCE, "arena exhausted (blockinfo)");
      }
      /* BLOCKNAME (2) / SETRECORDNAME (3) carry no semantics for us */
   }
}

char *
a2i_bs_ops_string(a2i_arena *ar, const uint64_t *ops, uint32_t n, uint32_t first)
{
   if (first > n)
      return NULL;
   char *s = a2i_arena_alloc(ar, (size_t)(n - first) + 1);
   if (!s)
      return NULL;
   for (uint32_t i = first; i < n; i++) {
      if (ops[i] > 255)
         return NULL;
      s[i - first] = (char)ops[i];
   }
   s[n - first] = '\0';
   return s;
}
