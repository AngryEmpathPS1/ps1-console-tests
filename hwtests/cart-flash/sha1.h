/* SHA-1 (FIPS 180-4) of a buffer in RAM, 32-bit length (an image of at most 256 KB). Shared by cart-flash and card-io (2026-11-21). */
#pragma once
#include <stdint.h>
static inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
static inline void sha1(const uint8_t *m, uint32_t len, uint8_t out[20]) {
    uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
    const uint32_t blocks = (len + 8) / 64 + 1;            /* the message, 0x80, zeros, then the length in bits (big-endian, 8 bytes) */
    const uint32_t bits = len * 8;                          /* a 256 KB image at most: the high word is 0 */
    for (uint32_t bi = 0; bi < blocks; bi++) {
        uint8_t blk[64];
        for (uint32_t i = 0; i < 64; i++) { const uint32_t p = bi * 64 + i; blk[i] = p < len ? m[p] : p == len ? 0x80 : 0; }
        if (bi == blocks - 1) for (int i = 0; i < 4; i++) blk[60 + i] = (uint8_t)(bits >> (24 - 8 * i));
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = ((uint32_t)blk[4 * i] << 24) | (blk[4 * i + 1] << 16) | (blk[4 * i + 2] << 8) | blk[4 * i + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) { out[4 * i] = h[i] >> 24; out[4 * i + 1] = h[i] >> 16; out[4 * i + 2] = h[i] >> 8; out[4 * i + 3] = h[i]; }
}

