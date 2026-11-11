/* SPDX-License-Identifier: MIT */
/*
 * a2i_core.h - freestanding core of air2ir3 (Penang XNU M9).
 *
 * Only <stddef.h>, <stdint.h> and <stdbool.h> are used so the core compiles
 * both on the host and as Mach-O arm64 objects (-target arm64-apple-ios18.7
 * -ffreestanding) without an SDK. All memory comes from an injected
 * allocator through a bounded arena. Every failure is reported through
 * a2i_diag with an explicit a2i_status; there is no silent fallback.
 */
#ifndef A2I_CORE_H
#define A2I_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum a2i_status {
   A2I_OK = 0,
   A2I_E_IO = 1,
   A2I_E_CONTAINER = 2,
   A2I_E_INTEGRITY = 3,
   A2I_E_VERSION = 4,
   A2I_E_BITSTREAM = 5,
   A2I_E_UNSUPPORTED_IR = 6,
   A2I_E_UNSUPPORTED_BINDING = 7,
   A2I_E_BACKEND = 8,  /* Freedreno ir3 failed to compile/encode */
   A2I_E_RESOURCE = 9,
   A2I_E_NIR = 10,     /* NIR construction/validation failed (host compile only) */
   A2I_E_USAGE = 64,
} a2i_status;

const char *a2i_status_name(a2i_status s);

typedef struct a2i_diag {
   a2i_status code;
   uint64_t offset;   /* byte offset in the file, or bit offset in the stream */
   bool offset_is_bits;
   bool has_offset;
   char msg[320];
} a2i_diag;

void a2i_diag_clear(a2i_diag *d);
/* Sets code/message (keeps the first failure if one is already recorded). */
a2i_status a2i_fail(a2i_diag *d, a2i_status code, const char *msg);
a2i_status a2i_fail_at(a2i_diag *d, a2i_status code, uint64_t off, bool bits, const char *msg);
void a2i_diag_str(a2i_diag *d, const char *s);
void a2i_diag_strn(a2i_diag *d, const char *s, size_t n);
void a2i_diag_u64(a2i_diag *d, uint64_t v);
void a2i_diag_hex(a2i_diag *d, uint64_t v);

typedef struct a2i_allocator {
   void *(*alloc)(void *ctx, size_t size); /* may return NULL */
   void (*free)(void *ctx, void *ptr);
   void *ctx;
} a2i_allocator;

typedef struct a2i_arena_chunk a2i_arena_chunk;
typedef struct a2i_arena {
   a2i_allocator al;
   a2i_arena_chunk *head;
   size_t total;
   size_t limit;
   bool exhausted;
} a2i_arena;

void a2i_arena_init(a2i_arena *a, a2i_allocator al, size_t limit);
void *a2i_arena_alloc(a2i_arena *a, size_t size); /* zeroed, 16-byte aligned */
char *a2i_arena_strndup(a2i_arena *a, const char *s, size_t n);
void a2i_arena_release(a2i_arena *a);

/* growable array living in an arena (old storage is reclaimed with the arena) */
typedef struct a2i_vec {
   void *data;
   uint32_t len, cap, elem;
} a2i_vec;
void a2i_vec_init(a2i_vec *v, uint32_t elem);
void *a2i_vec_push(a2i_arena *a, a2i_vec *v); /* zeroed slot or NULL */
#define A2I_VEC_AT(v, T, i) (((T *)(v)->data)[(i)])

void *a2i_memcpy(void *d, const void *s, size_t n);
void *a2i_memset(void *d, int c, size_t n);
int a2i_memcmp(const void *a, const void *b, size_t n);
size_t a2i_strlen(const char *s);
bool a2i_streq(const char *a, const char *b);
bool a2i_streqn(const char *a, const uint8_t *b, size_t n); /* a == b[0..n) */
bool a2i_has_prefix(const char *s, const char *prefix);

typedef struct a2i_sha256 {
   uint32_t h[8];
   uint64_t len;
   uint8_t buf[64];
   size_t n;
} a2i_sha256;
void a2i_sha256_init(a2i_sha256 *c);
void a2i_sha256_update(a2i_sha256 *c, const void *p, size_t n);
void a2i_sha256_final(a2i_sha256 *c, uint8_t out[32]);
void a2i_sha256_buf(const void *p, size_t n, uint8_t out[32]);

static inline uint16_t a2i_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t a2i_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint32_t a2i_be32(const uint8_t *p)
{
   return (uint32_t)p[3] | (uint32_t)p[2] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
}
static inline uint64_t a2i_le64(const uint8_t *p)
{
   return (uint64_t)a2i_le32(p) | (uint64_t)a2i_le32(p + 4) << 32;
}
/* overflow-safe "off + len <= total" */
static inline bool a2i_in_bounds(uint64_t off, uint64_t len, uint64_t total)
{
   return off <= total && len <= total - off;
}

#endif
