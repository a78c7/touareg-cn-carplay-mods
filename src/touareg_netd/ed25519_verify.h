/*
 * ed25519_verify.h — Minimal pure-C99 RFC 8032 Ed25519 signature verifier
 *                    (streaming FIPS 180-4 SHA-512 + Curve25519/Edwards25519).
 *
 * Field/group arithmetic adapted from TweetNaCl (Public Domain; Bernstein,
 * van Gastel, Janssen, Lange, Schwabe, Smetsers), with strict RFC 8032 S < L
 * canonical check and zero heap allocation.
 *
 * Portable across 32-bit ARMv7 (QNX 6.6, no __int128) and 64-bit hosts,
 * clean under -Wall -Wextra -Werror and -fsanitize=address,undefined.
 */
#ifndef TOUAREG_ED25519_VERIFY_H
#define TOUAREG_ED25519_VERIFY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==================== Streaming Pure-C SHA-512 (FIPS 180-4) ==================== */

typedef struct {
    uint64_t h[8];
    uint64_t lo, hi;
    uint8_t  buf[128];
    size_t   idx;
} t_sha512_t;

static const uint64_t T_SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static inline uint64_t t_rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

static inline uint64_t t_load_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  | ((uint64_t)p[7]);
}

static inline void t_store_be64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)(v & 0xffu); v >>= 8; }
}

static void t_sha512_block(t_sha512_t *s, const uint8_t b[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++) w[i] = t_load_be64(b + i * 8);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = t_rotr64(w[i - 15], 1) ^ t_rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = t_rotr64(w[i - 2], 19) ^ t_rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = s->h[0], bb = s->h[1], c = s->h[2], d = s->h[3];
    uint64_t e = s->h[4], f = s->h[5], g = s->h[6], hh = s->h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = t_rotr64(e, 14) ^ t_rotr64(e, 18) ^ t_rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t t1 = hh + S1 + ch + T_SHA512_K[i] + w[i];
        uint64_t S0 = t_rotr64(a, 28) ^ t_rotr64(a, 34) ^ t_rotr64(a, 39);
        uint64_t maj = (a & bb) ^ (a & c) ^ (bb & c);
        uint64_t t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1;
        d = c; c = bb; bb = a; a = t1 + t2;
    }
    s->h[0] += a; s->h[1] += bb; s->h[2] += c; s->h[3] += d;
    s->h[4] += e; s->h[5] += f;  s->h[6] += g; s->h[7] += hh;
}

static void t_sha512_init(t_sha512_t *s) {
    s->h[0] = 0x6a09e667f3bcc908ULL; s->h[1] = 0xbb67ae8584caa73bULL;
    s->h[2] = 0x3c6ef372fe94f82bULL; s->h[3] = 0xa54ff53a5f1d36f1ULL;
    s->h[4] = 0x510e527fade682d1ULL; s->h[5] = 0x9b05688c2b3e6c1fULL;
    s->h[6] = 0x1f83d9abfb41bd6bULL; s->h[7] = 0x5be0cd19137e2179ULL;
    s->lo = 0; s->hi = 0; s->idx = 0;
}

static void t_sha512_update(t_sha512_t *s, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t bits_lo = (uint64_t)len << 3;
    uint64_t bits_hi = (uint64_t)len >> 61;
    s->lo += bits_lo;
    if (s->lo < bits_lo) s->hi++;
    s->hi += bits_hi;
    while (len > 0) {
        size_t take = 128 - s->idx;
        if (take > len) take = len;
        memcpy(s->buf + s->idx, p, take);
        s->idx += take; p += take; len -= take;
        if (s->idx == 128) { t_sha512_block(s, s->buf); s->idx = 0; }
    }
}

static void t_sha512_final(t_sha512_t *s, uint8_t out[64]) {
    s->buf[s->idx++] = 0x80;
    if (s->idx > 112) {
        memset(s->buf + s->idx, 0, 128 - s->idx);
        t_sha512_block(s, s->buf);
        s->idx = 0;
    }
    memset(s->buf + s->idx, 0, 112 - s->idx);
    t_store_be64(s->buf + 112, s->hi);
    t_store_be64(s->buf + 120, s->lo);
    t_sha512_block(s, s->buf);
    for (int i = 0; i < 8; i++) t_store_be64(out + i * 8, s->h[i]);
}

/* ==================== Ed25519 Field & Curve Verification ==================== */

typedef int64_t t_gf[16];

static const t_gf T_GF0 = {0};
static const t_gf T_GF1 = {1};
static const t_gf T_ED_D  = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};
static const t_gf T_ED_D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};
static const t_gf T_ED_X  = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};
static const t_gf T_ED_Y  = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};
static const t_gf T_ED_I  = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

static const uint8_t T_ED_L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/* Arithmetic right shift by 16 without implementation-defined signed shift UB */
static inline int64_t t_asr16(int64_t v) {
    if (v >= 0) return v >> 16;
    return -((int64_t)((uint64_t)(-v) + 0xffffULL) >> 16);
}

static void t_set25519(t_gf r, const t_gf a) {
    for (int i = 0; i < 16; i++) r[i] = a[i];
}

static void t_car25519(t_gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        int64_t c = t_asr16(o[i]);
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * (1LL << 16);
    }
}

static void t_sel25519(t_gf p, t_gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void t_pack25519(uint8_t *o, const t_gf n) {
    t_gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    t_car25519(t);
    t_car25519(t);
    t_car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        t_sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xff);
    }
}

static void t_unpack25519(t_gf o, const uint8_t *n) {
    for (int i = 0; i < 16; i++) {
        o[i] = (int64_t)n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void t_gf_add(t_gf o, const t_gf a, const t_gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void t_gf_sub(t_gf o, const t_gf a, const t_gf b) {
    for (int i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void t_gf_mul(t_gf o, const t_gf a, const t_gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    }
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    t_car25519(o);
    t_car25519(o);
}

static void t_gf_sqr(t_gf o, const t_gf a) {
    t_gf_mul(o, a, a);
}

static void t_inv25519(t_gf o, const t_gf i) {
    t_gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        t_gf_sqr(c, c);
        if (a != 2 && a != 4) t_gf_mul(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static void t_pow2523(t_gf o, const t_gf i) {
    t_gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 250; a >= 0; a--) {
        t_gf_sqr(c, c);
        if (a != 1) t_gf_mul(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static int t_neq25519(const t_gf a, const t_gf b) {
    uint8_t c[32], d[32];
    t_pack25519(c, a);
    t_pack25519(d, b);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(c[i] ^ d[i]);
    return diff != 0;
}

static uint8_t t_par25519(const t_gf a) {
    uint8_t d[32];
    t_pack25519(d, a);
    return (uint8_t)(d[0] & 1u);
}

static void t_ed_add(t_gf p[4], t_gf q[4]) {
    t_gf a, b, c, d, t, e, f, g, h;
    t_gf_sub(a, p[1], p[0]);
    t_gf_sub(t, q[1], q[0]);
    t_gf_mul(a, a, t);
    t_gf_add(b, p[0], p[1]);
    t_gf_add(t, q[0], q[1]);
    t_gf_mul(b, b, t);
    t_gf_mul(c, p[3], q[3]);
    t_gf_mul(c, c, T_ED_D2);
    t_gf_mul(d, p[2], q[2]);
    t_gf_add(d, d, d);
    t_gf_sub(e, b, a);
    t_gf_sub(f, d, c);
    t_gf_add(g, d, c);
    t_gf_add(h, b, a);
    t_gf_mul(p[0], e, f);
    t_gf_mul(p[1], h, g);
    t_gf_mul(p[2], g, f);
    t_gf_mul(p[3], e, h);
}

static void t_ed_cswap(t_gf p[4], t_gf q[4], uint8_t b) {
    for (int i = 0; i < 4; i++) t_sel25519(p[i], q[i], b);
}

static void t_ed_pack(uint8_t r[32], t_gf p[4]) {
    t_gf tx, ty, zi;
    t_inv25519(zi, p[2]);
    t_gf_mul(tx, p[0], zi);
    t_gf_mul(ty, p[1], zi);
    t_pack25519(r, ty);
    r[31] ^= (uint8_t)(t_par25519(tx) << 7);
}

static void t_ed_scalarmult(t_gf p[4], t_gf q[4], const uint8_t s[32]) {
    t_set25519(p[0], T_GF0);
    t_set25519(p[1], T_GF1);
    t_set25519(p[2], T_GF1);
    t_set25519(p[3], T_GF0);
    for (int i = 255; i >= 0; i--) {
        uint8_t b = (uint8_t)((s[i / 8] >> (i & 7)) & 1u);
        t_ed_cswap(p, q, b);
        t_ed_add(q, p);
        t_ed_add(p, p);
        t_ed_cswap(p, q, b);
    }
}

static void t_ed_scalarbase(t_gf p[4], const uint8_t s[32]) {
    t_gf q[4];
    t_set25519(q[0], T_ED_X);
    t_set25519(q[1], T_ED_Y);
    t_set25519(q[2], T_GF1);
    t_gf_mul(q[3], T_ED_X, T_ED_Y);
    t_ed_scalarmult(p, q, s);
}

static int t_ed_unpackneg(t_gf r[4], const uint8_t p[32]) {
    t_gf t, chk, num, den, den2, den4, den6;
    t_set25519(r[2], T_GF1);
    t_unpack25519(r[1], p);
    t_gf_sqr(num, r[1]);
    t_gf_mul(den, num, T_ED_D);
    t_gf_sub(num, num, r[2]);
    t_gf_add(den, r[2], den);

    t_gf_sqr(den2, den);
    t_gf_sqr(den4, den2);
    t_gf_mul(den6, den4, den2);
    t_gf_mul(t, den6, num);
    t_gf_mul(t, t, den);

    t_pow2523(t, t);
    t_gf_mul(t, t, num);
    t_gf_mul(t, t, den);
    t_gf_mul(t, t, den);
    t_gf_mul(r[0], t, den);

    t_gf_sqr(chk, r[0]);
    t_gf_mul(chk, chk, den);
    if (t_neq25519(chk, num)) t_gf_mul(r[0], r[0], T_ED_I);

    t_gf_sqr(chk, r[0]);
    t_gf_mul(chk, chk, den);
    if (t_neq25519(chk, num)) return -1;

    if (t_par25519(r[0]) == (uint8_t)(p[31] >> 7)) t_gf_sub(r[0], T_GF0, r[0]);
    t_gf_mul(r[3], r[0], r[1]);
    return 0;
}

static void t_ed_modL(uint8_t r[32], int64_t x[64]) {
    for (int i = 63; i >= 32; i--) {
        int64_t carry = 0;
        int j;
        for (j = i - 32; j < i - 12; j++) {
            x[j] += carry - 16 * x[i] * (int64_t)T_ED_L[j - (i - 32)];
            carry = (x[j] + 128) / 256;
            if ((x[j] + 128) < 0 && ((x[j] + 128) % 256) != 0) carry--;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    int64_t carry = 0;
    for (int j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * (int64_t)T_ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; j++) x[j] -= carry * (int64_t)T_ED_L[j];
    for (int i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void t_ed_reduce(uint8_t r[64]) {
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = (int64_t)r[i];
    memset(r, 0, 64);
    t_ed_modL(r, x);
}

/* Strict RFC 8032 canonical check: return 1 iff little-endian 32-byte S < L */
static int t_ed_scalar_canonical(const uint8_t s[32]) {
    for (int i = 31; i >= 0; i--) {
        if (s[i] < T_ED_L[i]) return 1;
        if (s[i] > T_ED_L[i]) return 0;
    }
    return 0; /* S == L is invalid */
}

/* Verify RFC 8032 PureEdDSA (Ed25519) detached signature.
 * Returns 0 on valid signature, -1 on any verification failure. */
static int t_ed25519_verify(const uint8_t sig[64], const uint8_t *msg, size_t mlen, const uint8_t pk[32]) {
    if (!sig || !pk || (!msg && mlen > 0)) return -1;
    if (!t_ed_scalar_canonical(sig + 32)) return -1;

    t_gf p[4], q[4];
    if (t_ed_unpackneg(q, pk) != 0) return -1;

    uint8_t h[64];
    t_sha512_t sh;
    t_sha512_init(&sh);
    t_sha512_update(&sh, sig, 32);
    t_sha512_update(&sh, pk, 32);
    if (mlen > 0) t_sha512_update(&sh, msg, mlen);
    t_sha512_final(&sh, h);
    t_ed_reduce(h);

    t_ed_scalarmult(p, q, h);
    t_ed_scalarbase(q, sig + 32);
    t_ed_add(p, q);

    uint8_t rcheck[32];
    t_ed_pack(rcheck, p);

    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(rcheck[i] ^ sig[i]);
    return diff == 0 ? 0 : -1;
}

#endif /* TOUAREG_ED25519_VERIFY_H */
