/*
 * mdec/fuzz-mdec - random pictures through the MDEC, every output word hashed (2026-11-01)
 *
 * WHY. The MDEC tests so far ask chosen questions of chosen blocks. This feeds it what nobody chose: random quantisation tables, a scale table now and then, random run-levels,
 * every output depth, signed and unsigned. An id is a whole program: xorshift32 from 0x9E3779B9 * (id + 1), so a line that differs can be rebuilt anywhere.
 *
 * WHAT IT DOES. Interrupts off, no disc. Even ids are gentle, what a movie sends: small tables, small levels, q_scale 1..6, nothing overflows. Odd ids are wild, levels and tables a movie
 * never holds, where the IDCT's insides overflow (the console does not saturate there). For each of 1,024 ids: reset the MDEC, load the tables, send the picture by DMA0 and take the output by DMA1, as a movie player does. A
 * line: M id command time hash status, the command word, the system cycles from DMA0's start to DMA1's end, a hash of every word out, the status after. A wait that runs out
 * reads HUNG in place of the hash and the next id starts from a reset.
 *
 * READ IT LIKE THIS. A hash that differs is the sums; a time that differs is the pace. dump.h names ids whose output is printed: W, then the words.
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-mdec --frames=9000 --quiet=2500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include "dump.h"

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR0    0x1F801080u
#define BCR0     0x1F801084u
#define CHCR0    0x1F801088u
#define MADR1    0x1F801090u
#define BCR1     0x1F801094u
#define CHCR1    0x1F801098u
#define MDEC0    0x1F801820u
#define MDEC1    0x1F801824u
#define IDS 1024

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }
static bool waitDone(uint32_t chcr, uint32_t limit) {
    for (uint32_t t0 = now(); now() - t0 < limit;) if (!(read32(chcr) & 0x01000000u)) return true;
    return false;
}

static uint16_t rl[8192] __attribute__((aligned(4)));
static uint32_t out[4 * 192] __attribute__((aligned(4)));
static uint32_t quant[32] __attribute__((aligned(4)));
static int16_t scale[64] __attribute__((aligned(4)));
static const int16_t SCALE[64] = {
    23170, 23170, 23170, 23170, 23170, 23170, 23170, 23170, 32138, 27245, 18204, 6392, -6393, -18205, -27246, -32139,
    30273, 12539, -12540, -30274, -30274, -12540, 12539, 30273, 27245, -6393, -32139, -18205, 18204, 32138, 6392, -27246,
    23170, -23171, -23171, 23170, 23170, -23171, -23171, 23170, 18204, -32139, 6392, 27245, -27246, -6393, 32138, -18205,
    12539, -30274, 30273, -12540, -12540, 30273, -30274, 12539, 6392, -18205, 27245, -32139, 32138, -27246, 18204, -6393 };

static uint32_t tables() {
    write32(CHCR0, 0); write32(CHCR1, 0);
    write32(MDEC1, 0x80000000u);
    waitCycles(40);
    write32(MDEC1, 0x60000000u);
    write32(MDEC0, 0x40000001u);
    write32(MADR0, (uint32_t)quant & 0x1FFFFFu); write32(BCR0, (1u << 16) | 32u); write32(CHCR0, 0x01000201u);
    uint32_t ok = waitDone(CHCR0, 20000);
    write32(MDEC0, 0x60000000u);
    write32(MADR0, (uint32_t)scale & 0x1FFFFFu); write32(BCR0, (1u << 16) | 32u); write32(CHCR0, 0x01000201u);
    ok &= waitDone(CHCR0, 20000);
    return ok;
}
static uint32_t words;
static int block(int n, bool over, bool gentle) {
    if (gentle) {
        const int dc = (int)(rnd() % 512) - 256;
        rl[n++] = (uint16_t)(((1 + rnd() % 6u) << 10) | ((uint32_t)dc & 0x3FF));
        int idx = 0;
        const int count = (int)(rnd() % 12);
        for (int i = 0; i < count; i++) {
            const uint32_t run = rnd() % 6u;
            if (idx + (int)run + 1 > 63) break;
            idx += (int)run + 1;
            const int lvl = (int)(rnd() % 49) - 24;
            rl[n++] = (uint16_t)((run << 10) | ((uint32_t)lvl & 0x3FF));
        }
        rl[n++] = 0xFE00;
        return n;
    }
    rl[n++] = (uint16_t)(((rnd() % 64u) << 10) | (rnd() % 4 ? (rnd() & 0x7F) | ((rnd() & 1) ? 0x380 : 0) : (rnd() & 0x3FF)));
    int idx = 0;
    const int count = (int)(rnd() % 24);
    for (int i = 0; i < count; i++) {
        const uint32_t run = (rnd() & 3) ? rnd() % 4u : rnd() % 16u;
        if (!over && idx + (int)run + 1 > 63) break;
        idx += (int)run + 1;
        const uint32_t lvl = (rnd() % 4) ? (rnd() & 0x3F) | ((rnd() & 1) ? 0x3C0 : 0) : (rnd() & 0x3FF);
        rl[n++] = (uint16_t)((run << 10) | lvl);
    }
    rl[n++] = 0xFE00;
    return n;
}
static uint32_t gen(uint32_t* outWords, bool gentle) {
    for (int i = 0; i < 32; i++) quant[i] = gentle ? (rnd() & 0x0F0F0F0Fu) | 0x02020202u : (rnd() % 3) ? (rnd() & 0x3F3F3F3Fu) | 0x01010101u : 0x10101010u;
    for (int i = 0; i < 64; i++) scale[i] = (!gentle && rnd() % 4 == 0 && i > 0 && i % 7 == 0) ? (int16_t)rnd() : SCALE[i];
    const uint32_t depth = rnd() & 3, sign = rnd() & 1, stp = rnd() & 1;
    const bool over = rnd() % 8 == 0;
    const int mono = depth < 2;
    int blocks;
    if (mono) { const int k = 1 + (int)(rnd() % 3); blocks = 4 * k; *outWords = (depth == 0 ? 8u : 16u) * (uint32_t)blocks; }
    else { const int k = 1 + (int)(rnd() % 4); blocks = 6 * k; *outWords = (depth == 2 ? 192u : 128u) * (uint32_t)k; }
    int n = 0;
    for (int b = 0; b < blocks; b++) n = block(n, over, gentle);
    uint32_t w = (uint32_t)(n + 1) / 2;
    w = (w + 31) & ~31u;
    for (uint32_t i = (uint32_t)n; i < w * 2; i++) rl[i] = 0xFE00;
    words = w;
    return 0x20000000u | (depth << 27) | (sign << 26) | (stp << 25) | (words & 0xFFFFu);
}
static void program(int id, bool dump) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    uint32_t outWords = 0;
    const uint32_t cmd = gen(&outWords, !(id & 1));
    tables();
    for (uint32_t i = 0; i < outWords; i++) out[i] = 0xDEADBEEFu;
    write32(MADR1, (uint32_t)out & 0x1FFFFFu);
    write32(BCR1, ((outWords / 32) << 16) | 32u);
    write32(CHCR1, 0x01000200u);
    const uint32_t t0 = now();
    write32(MDEC0, cmd);
    write32(MADR0, (uint32_t)rl & 0x1FFFFFu);
    write32(BCR0, ((words / 32) << 16) | 32u);
    write32(CHCR0, 0x01000201u);
    const bool ok = waitDone(CHCR1, 700000u);
    const uint32_t t = now() - t0;
    const uint32_t st = read32(MDEC1);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < outWords; i++) { uint32_t v = out[i]; for (int b = 0; b < 4; b++) { h = (h ^ (v & 255)) * 16777619u; v >>= 8; } }
    if (ok) printf("M %d %x %u %x %x\n", id, cmd, t, h, st);
    else printf("M %d %x %u HUNG %x %x %x\n", id, cmd, t, st, read32(BCR0), read32(BCR1));
    if (dump) { printf("W"); for (uint32_t i = 0; i < outWords; i++) printf(" %x", out[i]); printf("\n"); }
    write32(CHCR0, 0); write32(CHCR1, 0);
}

int main() {
    initVideo(320, 240);
    printf("\nmdec/fuzz-mdec\n");
    printf("%d random pictures: M id command time hash status. No assertions.\n\n", IDS);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x88u);
    if (DUMP[0] >= 0) for (int k = 0; DUMP[k] >= 0; k++) program(DUMP[k], true);
    else for (int id = 0; id < IDS; id++) program(id, false);
    write32(MDEC1, 0x80000000u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nmdec/fuzz-mdec: Done.\n");
    for (;;) VSync(0);
}
