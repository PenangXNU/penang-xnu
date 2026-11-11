/* SPDX-License-Identifier: MIT */
#include "a2i_mtlb.h"

static bool
tag_is(const uint8_t *t, const char *s)
{
   return t[0] == (uint8_t)s[0] && t[1] == (uint8_t)s[1] && t[2] == (uint8_t)s[2] &&
          t[3] == (uint8_t)s[3];
}

static a2i_status
fail_off(a2i_diag *d, a2i_status c, uint64_t off, const char *msg)
{
   return a2i_fail_at(d, c, off, false, msg);
}

static a2i_status
dup_tag(a2i_diag *d, uint64_t off, const char *tag)
{
   fail_off(d, A2I_E_CONTAINER, off, "duplicate tag ");
   a2i_diag_str(d, tag);
   return A2I_E_CONTAINER;
}

static a2i_status
parse_entry_tags(const uint8_t *p, uint64_t q, uint64_t end, uint64_t base,
                 a2i_mtlb_function *f, a2i_diag *d)
{
   for (;;) {
      if (!a2i_in_bounds(q, 4, end))
         return fail_off(d, A2I_E_CONTAINER, base + q, "function entry not terminated by ENDT");
      const uint8_t *t = p + q;
      if (tag_is(t, "ENDT")) {
         if (q + 4 != end)
            return fail_off(d, A2I_E_CONTAINER, base + q, "ENDT before end of function entry");
         return A2I_OK;
      }
      if (!a2i_in_bounds(q, 6, end))
         return fail_off(d, A2I_E_CONTAINER, base + q, "truncated tag header");
      uint16_t l = a2i_le16(t + 4);
      if (!a2i_in_bounds(q + 6, l, end))
         return fail_off(d, A2I_E_CONTAINER, base + q, "tag payload out of entry bounds");
      const uint8_t *pl = t + 6;
      if (tag_is(t, "NAME")) {
         if (f->has_name)
            return dup_tag(d, base + q, "NAME");
         if (l < 2 || pl[l - 1] != 0)
            return fail_off(d, A2I_E_CONTAINER, base + q, "NAME not NUL-terminated or empty");
         for (uint16_t i = 0; i + 1 < l; i++)
            if (pl[i] == 0)
               return fail_off(d, A2I_E_CONTAINER, base + q, "NAME contains interior NUL");
         f->has_name = true;
         f->name = (const char *)pl;
         f->name_len = (uint32_t)(l - 1);
      } else if (tag_is(t, "TYPE")) {
         if (f->has_type)
            return dup_tag(d, base + q, "TYPE");
         if (l != 1)
            return fail_off(d, A2I_E_CONTAINER, base + q, "TYPE payload must be 1 byte");
         f->has_type = true;
         f->type = pl[0];
      } else if (tag_is(t, "HASH")) {
         if (f->has_hash)
            return dup_tag(d, base + q, "HASH");
         if (l != 32)
            return fail_off(d, A2I_E_CONTAINER, base + q, "HASH payload must be 32 bytes");
         f->has_hash = true;
         a2i_memcpy(f->hash, pl, 32);
      } else if (tag_is(t, "OFFT")) {
         if (f->has_offt)
            return dup_tag(d, base + q, "OFFT");
         if (l != 24)
            return fail_off(d, A2I_E_CONTAINER, base + q, "OFFT payload must be 24 bytes");
         f->has_offt = true;
         f->off_public_md = a2i_le64(pl);
         f->off_private_md = a2i_le64(pl + 8);
         f->off_bitcode = a2i_le64(pl + 16);
      } else if (tag_is(t, "VERS")) {
         if (f->has_vers)
            return dup_tag(d, base + q, "VERS");
         if (l != 8)
            return fail_off(d, A2I_E_CONTAINER, base + q, "VERS payload must be 8 bytes");
         f->has_vers = true;
         f->air_major = a2i_le16(pl);
         f->air_minor = a2i_le16(pl + 2);
         f->lang_major = a2i_le16(pl + 4);
         f->lang_minor = a2i_le16(pl + 6);
      } else if (tag_is(t, "MDSZ")) {
         if (f->has_mdsz)
            return dup_tag(d, base + q, "MDSZ");
         if (l != 8)
            return fail_off(d, A2I_E_CONTAINER, base + q, "MDSZ payload must be 8 bytes");
         f->has_mdsz = true;
         f->mdsz = a2i_le64(pl);
      } else if (tag_is(t, "RFLT")) {
         if (f->has_rflt)
            return dup_tag(d, base + q, "RFLT");
         if (l != 8)
            return fail_off(d, A2I_E_CONTAINER, base + q, "RFLT payload must be 8 bytes");
         f->has_rflt = true;
         f->rflt = a2i_le64(pl);
      } else {
         if (f->unknown_tags++ == 0) {
            for (int i = 0; i < 4; i++)
               f->first_unknown_tag[i] = (t[i] >= 0x20 && t[i] < 0x7f) ? (char)t[i] : '?';
            f->first_unknown_tag[4] = '\0';
         }
      }
      q += 6u + l;
   }
}

static a2i_status
parse_ext_header(a2i_mtlb *m, const uint8_t *p, uint64_t q, uint64_t end, a2i_diag *d)
{
   for (;;) {
      if (!a2i_in_bounds(q, 4, end))
         return fail_off(d, A2I_E_CONTAINER, m->slice_offset + q, "extended header not terminated");
      const uint8_t *t = p + q;
      if (tag_is(t, "ENDT"))
         return A2I_OK; /* trailing padding up to the next section is allowed */
      if (!a2i_in_bounds(q, 6, end))
         return fail_off(d, A2I_E_CONTAINER, m->slice_offset + q, "truncated extended tag");
      uint16_t l = a2i_le16(t + 4);
      if (!a2i_in_bounds(q + 6, l, end))
         return fail_off(d, A2I_E_CONTAINER, m->slice_offset + q, "extended tag out of bounds");
      if (tag_is(t, "UUID") && l == 16) {
         m->has_uuid = true;
         a2i_memcpy(m->uuid, t + 6, 16);
      } else if (tag_is(t, "RLST") && l == 16) {
         m->has_rlst = true;
         m->rlst_off = a2i_le64(t + 6);
         m->rlst_size = a2i_le64(t + 14);
      } else {
         m->ext_unknown_tags++;
      }
      q += 6u + l;
   }
}

a2i_status
a2i_mtlb_parse(const uint8_t *buf, uint64_t len, a2i_arena *ar, a2i_mtlb *m, a2i_diag *d)
{
   a2i_memset(m, 0, sizeof(*m));
   m->file = buf;
   m->file_len = len;
   if (len < 8)
      return fail_off(d, A2I_E_CONTAINER, 0, "file too small for any metallib header");

   uint64_t base = 0, size = len;
   if (a2i_be32(buf) == 0xCAFEBABEu) {
      uint32_t n = a2i_be32(buf + 4);
      if (n == 0 || n > A2I_MTLB_MAX_FAT)
         return fail_off(d, A2I_E_CONTAINER, 4, "FAT slice count out of range");
      if (!a2i_in_bounds(8, (uint64_t)20 * n, len))
         return fail_off(d, A2I_E_CONTAINER, 8, "FAT arch table truncated");
      int air = -1;
      m->fat_count = n;
      for (uint32_t i = 0; i < n; i++) {
         const uint8_t *e = buf + 8 + 20 * i;
         a2i_fat_slice *s = &m->fat[i];
         s->cputype = a2i_be32(e);
         s->cpusubtype = a2i_be32(e + 4);
         s->offset = a2i_be32(e + 8);
         s->size = a2i_be32(e + 12);
         s->align = a2i_be32(e + 16);
         if (!a2i_in_bounds(s->offset, s->size, len))
            return fail_off(d, A2I_E_CONTAINER, 8 + 20 * i, "FAT slice out of file bounds");
         s->is_air64_mtlb = s->cputype == A2I_CPU_TYPE_AIR64 && s->size >= 4 &&
                            a2i_memcmp(buf + s->offset, "MTLB", 4) == 0;
         if (s->is_air64_mtlb) {
            if (air >= 0)
               return fail_off(d, A2I_E_CONTAINER, 8 + 20 * i, "more than one AIR64 slice");
            air = (int)i;
         }
      }
      if (air < 0)
         return fail_off(d, A2I_E_CONTAINER, 0, "FAT container has no AIR64 MTLB slice");
      base = m->fat[air].offset;
      size = m->fat[air].size;
   } else if (a2i_memcmp(buf, "MTLB", 4) != 0) {
      return fail_off(d, A2I_E_CONTAINER, 0, "bad magic (neither MTLB nor FAT)");
   }
   m->slice_offset = base;
   m->slice_size = size;
   if (size < A2I_MTLB_HEADER_SIZE)
      return fail_off(d, A2I_E_CONTAINER, base, "MTLB header truncated");

   const uint8_t *p = buf + base;
   m->hdr_u16_04 = a2i_le16(p + 4);
   m->file_ver_major = a2i_le16(p + 6);
   m->file_ver_minor = a2i_le16(p + 8);
   m->lib_type = p[0x0a];
   m->target_os = p[0x0b];
   m->os_major = a2i_le16(p + 0x0c);
   m->os_minor = a2i_le16(p + 0x0e);
   m->file_size = a2i_le64(p + 0x10);
   if (m->file_size != size)
      return fail_off(d, A2I_E_CONTAINER, base + 0x10, "header file_size differs from slice size");
   for (int s = 0; s < 4; s++) {
      m->sec_off[s] = a2i_le64(p + 0x18 + 16 * s);
      m->sec_size[s] = a2i_le64(p + 0x20 + 16 * s);
      if (!a2i_in_bounds(m->sec_off[s], m->sec_size[s], size) ||
          m->sec_off[s] < A2I_MTLB_HEADER_SIZE)
         return fail_off(d, A2I_E_CONTAINER, base + 0x18 + 16 * s, "section out of bounds");
   }

   uint64_t fl = m->sec_off[A2I_SEC_FUNCTION_LIST];
   uint64_t fl_size = m->sec_size[A2I_SEC_FUNCTION_LIST];
   if (!a2i_in_bounds(fl, 4 + fl_size, size))
      return fail_off(d, A2I_E_CONTAINER, base + fl, "function list exceeds file");
   m->function_count = a2i_le32(p + fl);
   if (m->function_count > A2I_MTLB_MAX_FUNCTIONS)
      return fail_off(d, A2I_E_CONTAINER, base + fl, "function count above limit");
   if (m->function_count) {
      m->functions = a2i_arena_alloc(ar, sizeof(a2i_mtlb_function) * m->function_count);
      if (!m->functions)
         return a2i_fail(d, A2I_E_RESOURCE, "arena exhausted (function table)");
   }
   uint64_t q = fl + 4, end = fl + 4 + fl_size;
   for (uint32_t i = 0; i < m->function_count; i++) {
      a2i_mtlb_function *f = &m->functions[i];
      f->index = i;
      if (!a2i_in_bounds(q, 4, end))
         return fail_off(d, A2I_E_CONTAINER, base + q, "function entry beyond function list");
      uint32_t esz = a2i_le32(p + q);
      if (esz < 8 || !a2i_in_bounds(q, esz, end))
         return fail_off(d, A2I_E_CONTAINER, base + q, "function entry size invalid");
      f->entry_offset = q;
      f->entry_size = esz;
      a2i_status st = parse_entry_tags(p, q + 4, q + esz, base, f, d);
      if (st != A2I_OK)
         return st;
      q += esz;
   }
   if (q != end)
      return fail_off(d, A2I_E_CONTAINER, base + q, "function list size mismatch");

   uint64_t pub = m->sec_off[A2I_SEC_PUBLIC_MD];
   if (pub > end) {
      m->ext_header_size = pub - end;
      a2i_status st = parse_ext_header(m, p, end, pub, d);
      if (st != A2I_OK)
         return st;
   }

   uint64_t bc = m->sec_off[A2I_SEC_BITCODE], bc_size = m->sec_size[A2I_SEC_BITCODE];
   for (uint32_t i = 0; i < m->function_count; i++) {
      a2i_mtlb_function *f = &m->functions[i];
      if (f->has_offt && f->has_mdsz && a2i_in_bounds(f->off_bitcode, f->mdsz, bc_size)) {
         f->bitcode_in_bounds = true;
         f->bitcode = p + bc + f->off_bitcode;
         f->bitcode_file_offset = base + bc + f->off_bitcode;
      }
   }
   return A2I_OK;
}

a2i_status
a2i_mtlb_check_header_policy(const a2i_mtlb *m, a2i_diag *d)
{
   if (m->hdr_u16_04 != 1 || m->file_ver_major != 2 || m->file_ver_minor != 8) {
      fail_off(d, A2I_E_VERSION, m->slice_offset + 4, "unsupported MTLB file version ");
      a2i_diag_u64(d, m->file_ver_major);
      a2i_diag_str(d, ".");
      a2i_diag_u64(d, m->file_ver_minor);
      a2i_diag_str(d, " (u16@4=");
      a2i_diag_u64(d, m->hdr_u16_04);
      a2i_diag_str(d, "); MVP-AIR-0 accepts 2.8 with u16@4=1");
      return A2I_E_VERSION;
   }
   if (m->target_os != 0x82 || m->os_major != 18) {
      fail_off(d, A2I_E_VERSION, m->slice_offset + 0x0b, "unsupported target OS ");
      a2i_diag_hex(d, m->target_os);
      a2i_diag_str(d, " ");
      a2i_diag_u64(d, m->os_major);
      a2i_diag_str(d, "; MVP-AIR-0 accepts iOS (0x82) 18.x");
      return A2I_E_VERSION;
   }
   return A2I_OK;
}

a2i_status
a2i_mtlb_select(const a2i_mtlb *m, const char *sel, uint32_t *index, a2i_diag *d)
{
   size_t sl = a2i_strlen(sel);
   for (uint32_t i = 0; i < m->function_count; i++) {
      const a2i_mtlb_function *f = &m->functions[i];
      if (f->has_name && f->name_len == sl && a2i_memcmp(f->name, sel, sl) == 0) {
         *index = i;
         return A2I_OK;
      }
   }
   if (sl == 0 || sl > 9) {
      a2i_fail(d, A2I_E_USAGE, "no function entry named '");
      a2i_diag_str(d, sel);
      a2i_diag_str(d, "'");
      return A2I_E_USAGE;
   }
   uint32_t v = 0;
   for (size_t i = 0; i < sl; i++) {
      if (sel[i] < '0' || sel[i] > '9') {
         a2i_fail(d, A2I_E_USAGE, "no function entry named '");
         a2i_diag_str(d, sel);
         a2i_diag_str(d, "'");
         return A2I_E_USAGE;
      }
      v = v * 10 + (uint32_t)(sel[i] - '0');
   }
   if (v >= m->function_count) {
      a2i_fail(d, A2I_E_USAGE, "entry index out of range: ");
      a2i_diag_u64(d, v);
      return A2I_E_USAGE;
   }
   *index = v;
   return A2I_OK;
}

a2i_status
a2i_mtlb_check_entry(const a2i_mtlb *m, uint32_t index, a2i_diag *d)
{
   if (index >= m->function_count)
      return a2i_fail(d, A2I_E_USAGE, "entry index out of range");
   const a2i_mtlb_function *f = &m->functions[index];
   uint64_t eoff = m->slice_offset + f->entry_offset;
   if (!f->has_name || !f->has_type || !f->has_hash || !f->has_offt || !f->has_vers ||
       !f->has_mdsz)
      return fail_off(d, A2I_E_CONTAINER, eoff,
                      "selected entry lacks a required tag (NAME/TYPE/HASH/OFFT/VERS/MDSZ)");
   if (f->unknown_tags) {
      fail_off(d, A2I_E_CONTAINER, eoff, "selected entry has unknown tag ");
      a2i_diag_str(d, f->first_unknown_tag);
      return A2I_E_CONTAINER;
   }
   if (!f->bitcode_in_bounds)
      return fail_off(d, A2I_E_CONTAINER, eoff, "OFFT/MDSZ place the bitcode outside its section");
   uint8_t h[32];
   a2i_sha256_buf(f->bitcode, (size_t)f->mdsz, h);
   if (a2i_memcmp(h, f->hash, 32) != 0)
      return fail_off(d, A2I_E_INTEGRITY, f->bitcode_file_offset,
                      "HASH tag does not match SHA-256 of the function bitcode");
   if (f->air_major != 2 || f->air_minor != 7 || f->lang_major != 3 || f->lang_minor != 2) {
      fail_off(d, A2I_E_VERSION, eoff, "unsupported VERS AIR ");
      a2i_diag_u64(d, f->air_major);
      a2i_diag_str(d, ".");
      a2i_diag_u64(d, f->air_minor);
      a2i_diag_str(d, " / MSL ");
      a2i_diag_u64(d, f->lang_major);
      a2i_diag_str(d, ".");
      a2i_diag_u64(d, f->lang_minor);
      a2i_diag_str(d, "; MVP-AIR-0 accepts AIR 2.7 / MSL 3.2");
      return A2I_E_VERSION;
   }
   return A2I_OK;
}
