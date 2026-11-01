/*
 * spu/fuzz-spu - random voices, read back off the capture rings (2026-11-01)
 *
 * WHY. The SPU tests ask chosen questions of chosen samples. This hands it what nobody chose: random ADPCM blocks with every filter and shift, random flags and loop points, random
 * envelopes, pitches and pitch modulation. An id is a whole program: xorshift32 from 0x9E3779B9 * (id + 1), so a line that differs can be rebuilt anywhere.
 *
 * WHAT IT DOES. Interrupts off, no disc, sound out muted. For each of 512 ids: voices 0 to 3 stopped until the rings are silent, four samples of 8 blocks uploaded, the voices set up,
 * keyed together just behind a half edge, then the next two halves of voices 1 and 3's rings read back. A line: S id, then for voice 1 and for voice 3 the ring index of its first
 * non-zero sample and a hash of the 400 samples from there; then ENDX. Hashing from the first sound leaves the key-on's own phase out of it.
 *
 * READ IT LIKE THIS. An index that differs is the key-on's latency. A hash that differs is the sample decoding, the interpolation, the envelope or the pitch. dump.h names ids whose
 * rings are printed: V, PMON and each voice's pitch and ADSR; R1, R3, then the samples. There are no assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js fuzz-spu --frames=6000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include "dump.h"

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define IDS 512
#define HALF 256
#define HASHED 400

static uint32_t rng;
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
}
static void edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
}
static uint32_t samples[4 * 32] __attribute__((aligned(16)));
static uint16_t cap1[HALF * 2] __attribute__((aligned(16)));
static uint16_t cap3[HALF * 2] __attribute__((aligned(16)));

static void genSample(uint8_t* b) {
    const int blocks = 8, loopAt = (int)(rnd() % 8);
    const uint32_t mode = rnd() % 4;
    for (int k = 0; k < blocks; k++) {
        uint8_t* q = b + k * 16;
        q[0] = (uint8_t)(((rnd() % 5) << 4) | (mode == 0 ? rnd() % 13 : 4 + rnd() % 9));
        q[1] = (uint8_t)((k == loopAt ? 4 : 0) | (k == blocks - 1 ? (rnd() & 1 ? 3 : 1) : 0));
        for (int i = 2; i < 16; i++) q[i] = (uint8_t)rnd();
    }
}
static void voice(int n, uint32_t start) {
    const uint32_t b = (uint32_t)n * 0x10;
    write16(SPU(b + 0x0), 0); write16(SPU(b + 0x2), 0);
    uint32_t pitch = 0x0400 + rnd() % 0x1C00;
    if (!(rnd() % 4)) pitch = rnd() & 0x3FFF;
    write16(SPU(b + 0x4), (uint16_t)pitch);
    write16(SPU(b + 0x6), (uint16_t)(start / 8));
    write16(SPU(b + 0x8), (uint16_t)((rnd() & 0x7F00) | (rnd() & 0xFF) | (rnd() % 3 ? 0 : 0x8000)));
    write16(SPU(b + 0xA), (uint16_t)rnd());
}
static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    for (int v = 0; v < 4; v++) { const uint32_t b = (uint32_t)v * 0x10; write16(SPU(b + 0x8), 0); write16(SPU(b + 0xA), 0x1F); write16(SPU(b + 0xC), 0); }
}
static uint32_t ringHash(const uint16_t* s, int* first) {
    int f = 0;
    while (f < HALF * 2 && !s[f]) f++;
    *first = f;
    uint32_t h = 2166136261u;
    for (int i = f; i < f + HASHED && i < HALF * 2; i++) { h = (h ^ (s[i] & 255)) * 16777619u; h = (h ^ (s[i] >> 8)) * 16777619u; }
    return h;
}
static void program(int id, bool dump) {
    rng = 0x9E3779B9u * (uint32_t)(id + 1); rnd(); rnd();
    allOff();
    /* A key-on keeps the decoder's history (M183): a block of zeros played first leaves it 0 here, whatever ran before. */
    for (int v = 0; v < 4; v++) { const uint32_t b = (uint32_t)v * 0x10; write16(SPU(b + 0x4), 0x1000); write16(SPU(b + 0x6), 0x2000 / 8); write16(SPU(b + 0x8), 0x000F); write16(SPU(b + 0xA), 0x001F); }
    write16(SPU(0x188), 0x000F);
    idle(768u * 128u);
    write16(SPU(0x18C), 0x000F);
    idle(768u * 1100u);                                          /* both rings' worth of silence */
    for (int v = 0; v < 4; v++) genSample((uint8_t*)&samples[v * 32]);
    spuDma(0x1000, samples, 4 * 32, true);
    for (int v = 0; v < 4; v++) voice(v, 0x1000u + (uint32_t)v * 128u);
    const uint32_t pmon = rnd() % 3 ? 0 : (rnd() & 0x0A);
    write16(SPU(0x190), (uint16_t)pmon);
    if (dump) { printf("V %x", pmon); for (int v = 0; v < 4; v++) printf(" %x %x %x", read16(SPU((uint32_t)v * 0x10 + 4)), read16(SPU((uint32_t)v * 0x10 + 8)), read16(SPU((uint32_t)v * 0x10 + 0xA))); printf("\n"); }
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    edge(1);
    write16(SPU(0x188), 0x000F);
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap1, HALF / 2, false);          /* the half just filled: bit 11 set while the second half fills */
    spuDma(0x0E00, (uint32_t*)cap3, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap1 + HALF), HALF / 2, false);
    spuDma(0x0C00, (uint32_t*)(cap3 + HALF), HALF / 2, false);
    const uint16_t endx = read16(SPU(0x19C));
    int f1, f3;
    const uint32_t h1 = ringHash(cap1, &f1), h3 = ringHash(cap3, &f3);
    printf("S %d %d %x %d %x %x\n", id, f1, h1, f3, h3, endx);
    if (dump) {
        printf("R1"); for (int i = 0; i < HALF * 2; i++) printf(" %x", cap1[i]); printf("\n");
        printf("R3"); for (int i = 0; i < HALF * 2; i++) printf(" %x", cap3[i]); printf("\n");
    }
}

int main() {
    printf("\nspu/fuzz-spu\n");
    printf("%d random voices: S id, first and hash of voice 1 and voice 3, ENDX. No assertions.\n\n", IDS);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    for (int i = 0; i < 32; i++) samples[i] = 0;
    ((uint8_t*)samples)[1] = 0x07;                               /* a block of zeros that repeats on itself */
    spuDma(0x2000, samples, 16, true);
    if (DUMP[0] >= 0) for (int k = 0; DUMP[k] >= 0; k++) program(DUMP[k], true);
    else for (int id = 0; id < IDS; id++) program(id, false);
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/fuzz-spu: Done.\n");
    for (;;) VSync(0);
}
