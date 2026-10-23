/* ChaCha20 (original 64-bit nonce / 64-bit counter variant) — keystream only.
 * AEAD (ChaCha20-Poly1305, 64x64): block counter 0 = Poly1305 key, payload from counter 1.
 * Tag is not verified here (frames are checked for NAL structure instead). */
#include <stdint.h>
#include <string.h>
#define CC_ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define CC_QR(a,b,c,d) (a += b, d ^= a, d = CC_ROTL(d,16), c += d, b ^= c, b = CC_ROTL(b,12), \
                        a += b, d ^= a, d = CC_ROTL(d, 8), c += d, b ^= c, b = CC_ROTL(b, 7))
static inline uint32_t cc_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void cc_block(const uint8_t key[32], uint64_t nonce, uint64_t ctr, uint8_t out[64]) {
    uint32_t s[16], x[16];
    s[0] = 0x61707865; s[1] = 0x3320646e; s[2] = 0x79622d32; s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++) s[4 + i] = cc_le32(key + 4 * i);
    s[12] = (uint32_t)ctr; s[13] = (uint32_t)(ctr >> 32);
    s[14] = (uint32_t)nonce; s[15] = (uint32_t)(nonce >> 32);
    memcpy(x, s, sizeof x);
    for (int i = 0; i < 10; i++) {
        CC_QR(x[0], x[4], x[8], x[12]); CC_QR(x[1], x[5], x[9], x[13]); CC_QR(x[2], x[6], x[10], x[14]); CC_QR(x[3], x[7], x[11], x[15]);
        CC_QR(x[0], x[5], x[10], x[15]); CC_QR(x[1], x[6], x[11], x[12]); CC_QR(x[2], x[7], x[8], x[13]); CC_QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) { uint32_t v = x[i] + s[i]; out[4*i] = v; out[4*i+1] = v >> 8; out[4*i+2] = v >> 16; out[4*i+3] = v >> 24; }
}
/* decrypt in place: len bytes of ciphertext (tag excluded) */
static void cc_aead_decrypt(const uint8_t key[32], uint64_t nonce, uint8_t *buf, size_t len) {
    uint8_t ks[64]; uint64_t ctr = 1;
    for (size_t off = 0; off < len; off += 64, ctr++) {
        cc_block(key, nonce, ctr, ks);
        size_t n = len - off < 64 ? len - off : 64;
        for (size_t i = 0; i < n; i++) buf[off + i] ^= ks[i];
    }
}
