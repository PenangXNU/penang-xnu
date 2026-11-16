/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Boot-bridge side of the retained run-time FDT and of the loader seed.
 *
 * m12_retain_fdt(): copy the firmware's FDT into the slot the bridge
 * reserved inside [end of kernel, topOfKernelData), the only part of the
 * kernel's static region that XNU keeps mapped and never frees, and zero
 * the values of /chosen/rng-seed and /chosen/kaslr-seed in the copy. The
 * copy is otherwise byte-identical; the structure is never changed, so no
 * DTB is synthesized.
 *
 * m12_condition_seed(): turn /chosen/rng-seed into the 64 bytes XNU reads
 * from /chosen/random-seed (osfmk/prng/prng_random.c, SEED_SIZE 64; XNU
 * zeroes only the bytes it consumes). A 64-byte field is forwarded
 * unchanged. A longer field (the Moto G53's is 512 bytes) is condensed with
 * SHA-512 (FIPS 180-4) so that no input byte is discarded and nothing
 * beyond 64 bytes reaches the Apple device tree. This is a transport rule,
 * not a quality claim: the physical entropy gate stays closed.
 *
 * Freestanding, no allocation, runs with the MMU off.
 */
#include "bridge_contract.h"

static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void
wipe(volatile uint8_t *p, size_t n)
{
    while (n--) {
        *p++ = 0;
    }
}

/* ---- SHA-512, FIPS 180-4 ---------------------------------------------- */

static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

static uint64_t
ror64(uint64_t x, unsigned n)
{
    return (x >> n) | (x << (64u - n));
}

static void
sha512_block(uint64_t h[8], const uint8_t *p, uint64_t w[80])
{
    uint64_t a, b, c, d, e, f, g, k;
    for (unsigned i = 0; i < 16; ++i) {
        w[i] = (uint64_t)be32(p + 8 * i) << 32 | be32(p + 8 * i + 4);
    }
    for (unsigned i = 16; i < 80; ++i) {
        uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; k = h[7];
    for (unsigned i = 0; i < 80; ++i) {
        uint64_t t1 = k + (ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41)) + ((e & f) ^ (~e & g)) +
            sha512_k[i] + w[i];
        uint64_t t2 = (ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

/* One-shot SHA-512 of n < 2^61 bytes. All intermediate state is wiped. */
void
m12_sha512(const uint8_t *data, size_t n, uint8_t out[64])
{
    uint64_t h[8] = {
        0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
        0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
    };
    uint64_t w[80];
    uint8_t tail[256];
    size_t whole = n & ~(size_t)127u, rest = n - whole, tail_len;
    for (size_t at = 0; at < whole; at += 128) {
        sha512_block(h, data + at, w);
    }
    for (size_t i = 0; i < sizeof(tail); ++i) {
        tail[i] = i < rest ? data[whole + i] : 0;
    }
    tail[rest] = 0x80;
    tail_len = rest < 112 ? 128 : 256;
    for (unsigned i = 0; i < 8; ++i) { /* 128-bit length; high 64 bits are zero here */
        tail[tail_len - 1 - i] = (uint8_t)(((uint64_t)n << 3) >> (8 * i));
    }
    tail[tail_len - 9] |= (uint8_t)((uint64_t)n >> 61);
    for (size_t at = 0; at < tail_len; at += 128) {
        sha512_block(h, tail + at, w);
    }
    for (unsigned i = 0; i < 64; ++i) {
        out[i] = (uint8_t)(h[i / 8] >> (56 - 8 * (i % 8)));
    }
    wipe((volatile uint8_t *)h, sizeof(h));
    wipe((volatile uint8_t *)w, sizeof(w));
    wipe(tail, sizeof(tail));
}

int
m12_condition_seed(const uint8_t *field, uint32_t len, uint8_t out[M12_SEED_BYTES])
{
    uint8_t any = 0;
    wipe(out, M12_SEED_BYTES);
    if (field == 0 || len < M12_SEED_BYTES || len > M12_SEED_FIELD_MAX) {
        return M12_SEED_REFUSED;
    }
    for (uint32_t i = 0; i < len; ++i) {
        any |= field[i];
    }
    if (!any) {
        return M12_SEED_REFUSED; /* an all-zero field is a placeholder */
    }
    if (len == M12_SEED_BYTES) {
        for (uint32_t i = 0; i < M12_SEED_BYTES; ++i) {
            out[i] = field[i];
        }
        return M12_SEED_FORWARDED_64;
    }
    m12_sha512(field, len, out);
    return M12_SEED_SHA512_CONDENSED;
}

/* ---- retained copy ----------------------------------------------------- */

static int
name_is(const uint8_t *s, uint32_t limit, const char *want)
{
    uint32_t i = 0;
    for (; want[i]; ++i) {
        if (i >= limit || s[i] != (uint8_t)want[i]) {
            return 0;
        }
    }
    return i < limit && s[i] == 0;
}

/*
 * Zero the values of /chosen/{rng-seed,kaslr-seed} in place. Returns the
 * number of values zeroed, or -1 if the token stream is malformed (the
 * caller then refuses the whole copy). The walk is bounded by the header.
 */
static int
scrub_seeds(uint8_t *f, uint32_t total)
{
    uint32_t o_struct = be32(f + 8), o_strings = be32(f + 12);
    uint32_t n_strings = be32(f + 32), n_struct = be32(f + 36);
    uint32_t at = 0, depth = 0, chosen_depth = 0;
    int zeroed = 0;
    uint8_t *s;
    if (o_struct > total || n_struct > total - o_struct || o_strings > total ||
        n_strings > total - o_strings || (n_struct & 3u)) {
        return -1;
    }
    s = f + o_struct;
    while (at + 4u <= n_struct) {
        uint32_t token = be32(s + at);
        at += 4;
        if (token == 1u) { /* FDT_BEGIN_NODE */
            uint32_t n = 0;
            while (at + n < n_struct && s[at + n]) {
                ++n;
            }
            if (at + n >= n_struct) {
                return -1;
            }
            ++depth;
            if (depth == 2 && chosen_depth == 0 && name_is(s + at, n + 1u, "chosen")) {
                chosen_depth = depth;
            }
            at = (at + n + 1u + 3u) & ~3u;
        } else if (token == 3u) { /* FDT_PROP */
            uint32_t len, off;
            if (n_struct - at < 8u) {
                return -1;
            }
            len = be32(s + at);
            off = be32(s + at + 4);
            if (len > n_struct - at - 8u || off >= n_strings) {
                return -1;
            }
            if (depth == chosen_depth && chosen_depth != 0 &&
                (name_is(f + o_strings + off, n_strings - off, "rng-seed") ||
                 name_is(f + o_strings + off, n_strings - off, "kaslr-seed"))) {
                wipe(s + at + 8u, len);
                ++zeroed;
            }
            at += 8u + ((len + 3u) & ~3u);
        } else if (token == 2u) { /* FDT_END_NODE */
            if (depth == 0) {
                return -1;
            }
            if (depth == chosen_depth) {
                chosen_depth = 0xffffffffu; /* only the first /chosen counts */
            }
            --depth;
        } else if (token == 9u) { /* FDT_END */
            return depth == 0 ? zeroed : -1;
        } else if (token != 4u) { /* FDT_NOP */
            return -1;
        }
    }
    return -1;
}

int
m12_retain_fdt(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t cap,
               uint32_t *scrubbed)
{
    uint32_t total;
    int zeroed;
    if (scrubbed != 0) {
        *scrubbed = 0;
    }
    if (src == 0 || dst == 0 || src_len < 40u || be32(src) != 0xd00dfeedu) {
        return M12_ERR_FDT_INVALID;
    }
    total = be32(src + 4);
    if (total < 40u || total > src_len || total > cap || total > M12_RETAINED_FDT_MAX ||
        (dst < src + total && src < dst + total)) {
        return M12_ERR_FDT_INVALID;
    }
    for (uint32_t i = 0; i < total; ++i) {
        dst[i] = src[i];
    }
    zeroed = scrub_seeds(dst, total);
    if (zeroed < 0) {
        wipe(dst, total);
        return M12_ERR_FDT_INVALID;
    }
    for (uint32_t i = total; i < cap; ++i) {
        dst[i] = 0; /* slot remainder never carries stale data */
    }
    if (scrubbed != 0) {
        *scrubbed = (uint32_t)zeroed;
    }
    return M12_BRIDGE_OK;
}
