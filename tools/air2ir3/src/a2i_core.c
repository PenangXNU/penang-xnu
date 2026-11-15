/* SPDX-License-Identifier: MIT */
#include "a2i_core.h"

const char *
a2i_status_name(a2i_status s)
{
   switch (s) {
   case A2I_OK: return "OK";
   case A2I_E_IO: return "E_IO";
   case A2I_E_CONTAINER: return "E_CONTAINER";
   case A2I_E_INTEGRITY: return "E_INTEGRITY";
   case A2I_E_VERSION: return "E_VERSION";
   case A2I_E_BITSTREAM: return "E_BITSTREAM";
   case A2I_E_UNSUPPORTED_IR: return "E_UNSUPPORTED_IR";
   case A2I_E_UNSUPPORTED_BINDING: return "E_UNSUPPORTED_BINDING";
   case A2I_E_BACKEND: return "E_BACKEND";
   case A2I_E_RESOURCE: return "E_RESOURCE";
   case A2I_E_NIR: return "E_NIR";
   case A2I_E_USAGE: return "E_USAGE";
   }
   return "E_UNKNOWN";
}

/* ---------------------------------------------------------------- memory */
void *
a2i_memcpy(void *d, const void *s, size_t n)
{
   uint8_t *o = d;
   const uint8_t *i = s;
   while (n--)
      *o++ = *i++;
   return d;
}

void *
a2i_memset(void *d, int c, size_t n)
{
   uint8_t *o = d;
   while (n--)
      *o++ = (uint8_t)c;
   return d;
}

int
a2i_memcmp(const void *a, const void *b, size_t n)
{
   const uint8_t *x = a, *y = b;
   for (size_t i = 0; i < n; i++)
      if (x[i] != y[i])
         return x[i] < y[i] ? -1 : 1;
   return 0;
}

size_t
a2i_strlen(const char *s)
{
   size_t n = 0;
   while (s[n])
      n++;
   return n;
}

bool
a2i_streq(const char *a, const char *b)
{
   while (*a && *a == *b) {
      a++;
      b++;
   }
   return *a == *b;
}

bool
a2i_streqn(const char *a, const uint8_t *b, size_t n)
{
   size_t la = a2i_strlen(a);
   return la == n && a2i_memcmp(a, b, n) == 0;
}

bool
a2i_has_prefix(const char *s, const char *prefix)
{
   while (*prefix)
      if (*s++ != *prefix++)
         return false;
   return true;
}

/* ----------------------------------------------------------------- diag */
void
a2i_diag_clear(a2i_diag *d)
{
   a2i_memset(d, 0, sizeof(*d));
}

void
a2i_diag_strn(a2i_diag *d, const char *s, size_t n)
{
   size_t l = a2i_strlen(d->msg);
   for (size_t i = 0; i < n && s[i] && l + 1 < sizeof(d->msg); i++) {
      char c = s[i];
      d->msg[l++] = (c >= 0x20 && c < 0x7f) ? c : '?';
   }
   d->msg[l] = '\0';
}

void
a2i_diag_str(a2i_diag *d, const char *s)
{
   a2i_diag_strn(d, s, (size_t)-1);
}

void
a2i_diag_u64(a2i_diag *d, uint64_t v)
{
   char b[24];
   int i = 23;
   b[i] = '\0';
   do {
      b[--i] = (char)('0' + v % 10);
      v /= 10;
   } while (v && i > 0);
   a2i_diag_str(d, b + i);
}

void
a2i_diag_hex(a2i_diag *d, uint64_t v)
{
   static const char hx[] = "0123456789abcdef";
   char b[20];
   int i = 19;
   b[i] = '\0';
   do {
      b[--i] = hx[v & 15];
      v >>= 4;
   } while (v && i > 2);
   b[--i] = 'x';
   b[--i] = '0';
   a2i_diag_str(d, b + i);
}

a2i_status
a2i_fail(a2i_diag *d, a2i_status code, const char *msg)
{
   if (d->code != A2I_OK)
      return d->code; /* first failure wins */
   d->code = code;
   d->msg[0] = '\0';
   a2i_diag_str(d, msg);
   return code;
}

a2i_status
a2i_fail_at(a2i_diag *d, a2i_status code, uint64_t off, bool bits, const char *msg)
{
   if (d->code != A2I_OK)
      return d->code;
   a2i_fail(d, code, msg);
   d->offset = off;
   d->offset_is_bits = bits;
   d->has_offset = true;
   return code;
}

/* ---------------------------------------------------------------- arena */
struct a2i_arena_chunk {
   a2i_arena_chunk *next;
   size_t size, used;
   /* payload follows, 16-byte aligned */
};

#define A2I_CHUNK_HDR ((sizeof(a2i_arena_chunk) + 15u) & ~(size_t)15u)
#define A2I_CHUNK_MIN ((size_t)64 * 1024)

void
a2i_arena_init(a2i_arena *a, a2i_allocator al, size_t limit)
{
   a2i_memset(a, 0, sizeof(*a));
   a->al = al;
   a->limit = limit;
}

void *
a2i_arena_alloc(a2i_arena *a, size_t size)
{
   size = (size + 15u) & ~(size_t)15u;
   if (size == 0)
      size = 16;
   a2i_arena_chunk *c = a->head;
   if (!c || c->size - c->used < size) {
      size_t cs = size > A2I_CHUNK_MIN ? size : A2I_CHUNK_MIN;
      if (cs > a->limit || a->total > a->limit - cs) {
         a->exhausted = true;
         return NULL;
      }
      uint8_t *raw = a->al.alloc(a->al.ctx, A2I_CHUNK_HDR + cs);
      if (!raw) {
         a->exhausted = true;
         return NULL;
      }
      c = (a2i_arena_chunk *)raw;
      c->size = cs;
      c->used = 0;
      c->next = a->head;
      a->head = c;
      a->total += cs;
   }
   uint8_t *p = (uint8_t *)c + A2I_CHUNK_HDR + c->used;
   c->used += size;
   a2i_memset(p, 0, size);
   return p;
}

char *
a2i_arena_strndup(a2i_arena *a, const char *s, size_t n)
{
   char *p = a2i_arena_alloc(a, n + 1);
   if (p) {
      a2i_memcpy(p, s, n);
      p[n] = '\0';
   }
   return p;
}

void
a2i_arena_release(a2i_arena *a)
{
   a2i_arena_chunk *c = a->head;
   while (c) {
      a2i_arena_chunk *n = c->next;
      a->al.free(a->al.ctx, c);
      c = n;
   }
   a->head = NULL;
   a->total = 0;
}

void
a2i_vec_init(a2i_vec *v, uint32_t elem)
{
   v->data = NULL;
   v->len = v->cap = 0;
   v->elem = elem;
}

void *
a2i_vec_push(a2i_arena *a, a2i_vec *v)
{
   if (v->len == v->cap) {
      uint32_t ncap = v->cap ? v->cap * 2 : 8;
      if (ncap < v->cap || (uint64_t)ncap * v->elem > ((uint64_t)1 << 31))
         return NULL;
      void *nd = a2i_arena_alloc(a, (size_t)ncap * v->elem);
      if (!nd)
         return NULL;
      if (v->len)
         a2i_memcpy(nd, v->data, (size_t)v->len * v->elem);
      v->data = nd;
      v->cap = ncap;
   }
   uint8_t *slot = (uint8_t *)v->data + (size_t)v->len * v->elem;
   a2i_memset(slot, 0, v->elem);
   v->len++;
   return slot;
}

/* --------------------------------------------------------------- sha256 */
static const uint32_t K256[64] = {
   0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
   0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
   0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
   0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
   0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
   0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
   0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
   0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static inline uint32_t ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void
sha_block(a2i_sha256 *c, const uint8_t *p)
{
   uint32_t w[64];
   for (int i = 0; i < 16; i++)
      w[i] = a2i_be32(p + 4 * i);
   for (int i = 16; i < 64; i++) {
      uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
   }
   uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
   uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
   for (int i = 0; i < 64; i++) {
      uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = h + S1 + ch + K256[i] + w[i];
      uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
      uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
      uint32_t t2 = S0 + mj;
      h = g; g = f; f = e; e = d + t1;
      d = cc; cc = b; b = a; a = t1 + t2;
   }
   c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
   c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void
a2i_sha256_init(a2i_sha256 *c)
{
   static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
   a2i_memcpy(c->h, iv, sizeof(iv));
   c->len = 0;
   c->n = 0;
}

void
a2i_sha256_update(a2i_sha256 *c, const void *data, size_t n)
{
   const uint8_t *p = data;
   c->len += n;
   while (n) {
      size_t take = 64 - c->n < n ? 64 - c->n : n;
      a2i_memcpy(c->buf + c->n, p, take);
      c->n += take;
      p += take;
      n -= take;
      if (c->n == 64) {
         sha_block(c, c->buf);
         c->n = 0;
      }
   }
}

void
a2i_sha256_final(a2i_sha256 *c, uint8_t out[32])
{
   uint64_t bits = c->len * 8;
   uint8_t pad = 0x80, zero = 0;
   a2i_sha256_update(c, &pad, 1);
   while (c->n != 56)
      a2i_sha256_update(c, &zero, 1);
   uint8_t lb[8];
   for (int i = 0; i < 8; i++)
      lb[i] = (uint8_t)(bits >> (56 - 8 * i));
   a2i_sha256_update(c, lb, 8);
   for (int i = 0; i < 8; i++) {
      out[4 * i + 0] = (uint8_t)(c->h[i] >> 24);
      out[4 * i + 1] = (uint8_t)(c->h[i] >> 16);
      out[4 * i + 2] = (uint8_t)(c->h[i] >> 8);
      out[4 * i + 3] = (uint8_t)c->h[i];
   }
}

void
a2i_sha256_buf(const void *p, size_t n, uint8_t out[32])
{
   a2i_sha256 c;
   a2i_sha256_init(&c);
   a2i_sha256_update(&c, p, n);
   a2i_sha256_final(&c, out);
}
