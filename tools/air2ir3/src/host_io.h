/* SPDX-License-Identifier: MIT */
/* host_io.h - host-only helpers shared by the air2ir3 CLI (stdio, malloc). */
#ifndef A2I_HOST_IO_H
#define A2I_HOST_IO_H

#include <stdio.h>

#include "a2i_air.h"
#include "a2i_mtlb.h"

#define A2I_MAX_INPUT (64u << 20)

a2i_allocator a2i_host_allocator(void);
/* Reads a whole file (<= 64 MiB). Returns A2I_OK or A2I_E_IO / A2I_E_RESOURCE. */
a2i_status a2i_read_file(const char *path, uint8_t **buf, uint64_t *len, a2i_diag *d);
void a2i_print_diag(FILE *f, const a2i_diag *d);
void a2i_hex(FILE *f, const uint8_t *p, size_t n);
/* JSON string (RFC 8259); ill-formed UTF-8 bytes become U+FFFD, counted in
 * *lossy when lossy is non-NULL. */
void a2i_json_str(FILE *o, const char *s, uint32_t n, uint32_t *lossy);

/* Opens FILE, parses the container, selects/validates the entry and decodes
 * its module. On success the module, mtlb and index fields are filled. */
typedef struct a2i_loaded {
   uint8_t *file;
   uint64_t file_len;
   a2i_arena arena;
   a2i_mtlb mtlb;
   uint32_t index;
   a2i_module module;
} a2i_loaded;

a2i_status a2i_load_entry(const char *path, const char *entry, bool check_policy,
                          a2i_loaded *L, a2i_diag *d);
void a2i_loaded_free(a2i_loaded *L);

/* Canonical dumps (used by the O1 oracles). */
int a2i_dump_records(FILE *out, const uint8_t *bc, uint64_t len, uint64_t file_off,
                     a2i_diag *d);
void a2i_dump_ir(FILE *out, const a2i_module *m);
void a2i_print_type(FILE *out, const a2i_module *m, uint32_t t);

#endif
