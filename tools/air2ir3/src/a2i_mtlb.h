/* SPDX-License-Identifier: MIT */
/*
 * a2i_mtlb.h - .metallib container reader (layout established from the three
 * iPadOS 22H352 Cryptex1,SystemOS metallibs, see docs in tools/air2ir3).
 *
 * Optional FAT wrapper (0xCAFEBABE, big-endian) whose AIR64 slice
 * (cputype 0x01000017) carries an 'MTLB' image:
 *   0x00 'MTLB' | u16 0x04 | u16 file_ver_major | u16 file_ver_minor
 *   u8 lib_type | u8 target_os | u16 os_major | u16 os_minor | u64 file_size
 *   4 x (u64 offset, u64 size): function list, public md, private md, bitcode
 * Function list: u32 count, then per entry u32 entry_size (includes itself)
 * followed by tags "TAG" u16 len payload ... terminated by "ENDT".
 */
#ifndef A2I_MTLB_H
#define A2I_MTLB_H

#include "a2i_core.h"

#define A2I_MTLB_HEADER_SIZE 0x58u
#define A2I_MTLB_MAX_FUNCTIONS 4096u
#define A2I_MTLB_MAX_FAT 16u
#define A2I_CPU_TYPE_AIR64 0x01000017u

enum {
   A2I_SEC_FUNCTION_LIST = 0,
   A2I_SEC_PUBLIC_MD = 1,
   A2I_SEC_PRIVATE_MD = 2,
   A2I_SEC_BITCODE = 3,
};

typedef struct a2i_fat_slice {
   uint32_t cputype, cpusubtype, offset, size, align;
   bool is_air64_mtlb;
} a2i_fat_slice;

typedef struct a2i_mtlb_function {
   uint32_t index;
   uint64_t entry_offset; /* relative to the MTLB slice */
   uint32_t entry_size;
   const char *name;      /* points into the input buffer, NUL-terminated */
   uint32_t name_len;
   bool has_name, has_type, has_hash, has_offt, has_vers, has_mdsz, has_rflt;
   uint8_t type;
   uint8_t hash[32];
   uint64_t off_public_md, off_private_md, off_bitcode;
   uint16_t air_major, air_minor, lang_major, lang_minor;
   uint64_t mdsz;
   uint64_t rflt;
   uint32_t unknown_tags;
   char first_unknown_tag[5];
   bool bitcode_in_bounds;
   const uint8_t *bitcode; /* valid when bitcode_in_bounds */
   uint64_t bitcode_file_offset;
} a2i_mtlb_function;

typedef struct a2i_mtlb {
   const uint8_t *file;
   uint64_t file_len;
   uint32_t fat_count;
   a2i_fat_slice fat[A2I_MTLB_MAX_FAT];
   uint64_t slice_offset, slice_size;
   uint16_t hdr_u16_04, file_ver_major, file_ver_minor;
   uint8_t lib_type, target_os;
   uint16_t os_major, os_minor;
   uint64_t file_size;
   uint64_t sec_off[4], sec_size[4];
   uint32_t function_count;
   a2i_mtlb_function *functions;
   bool has_uuid, has_rlst;
   uint8_t uuid[16];
   uint64_t rlst_off, rlst_size;
   uint32_t ext_unknown_tags;
   uint64_t ext_header_size;
} a2i_mtlb;

/* Structural parse (bounds, tags, sizes). Version policy is separate. */
a2i_status a2i_mtlb_parse(const uint8_t *buf, uint64_t len, a2i_arena *ar, a2i_mtlb *m,
                          a2i_diag *d);
/* Header policy accepted by MVP-AIR-0: MTLB 2.8, u16@4 == 1, iOS (0x82), OS 18. */
a2i_status a2i_mtlb_check_header_policy(const a2i_mtlb *m, a2i_diag *d);
/* Selection: exact NAME match first; otherwise a decimal index. */
a2i_status a2i_mtlb_select(const a2i_mtlb *m, const char *sel, uint32_t *index, a2i_diag *d);
/* Selected-entry checks: required tags, no unknown tags, bitcode bounds,
 * HASH == SHA-256(bitcode) (E_INTEGRITY), VERS == AIR 2.7 / MSL 3.2 (E_VERSION). */
a2i_status a2i_mtlb_check_entry(const a2i_mtlb *m, uint32_t index, a2i_diag *d);

#endif
