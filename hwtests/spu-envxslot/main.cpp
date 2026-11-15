/*
 * spu/spu-envxslot - where in the 768-cycle sample a voice's ENVX changes, by WHICH VOICE (2026-11-15)
 *
 * WHY. hwtests/spu-irqslot: a voice's IRQ9 is raised in that voice's own slot of the sample - voice 0's first, voices 1-23 from
 * 208 cycles later at 24 cycles each. This core shows every voice's new envelope SPU_ENVX_SHOW (244) cycles into the sample
 * (measured on voice 0 / 1 with a poll loop). The console fuzzer's Crash Bandicoot key-ons (hwtests/spu-kon, five consoles)
 * read the core's decays one step ahead in 10-18 % of ENVX reads at the best envelope phase, on other voices than 0 above all.
 * If ENVX moves in the voice's slot, its change steps through the sample by voice as the IRQ does.
 *
 * WHAT IT DOES. Interrupts off, sound muted. Every voice parked on a silent block at 2000h that loops to itself. A trial: voice
 * v keyed on there with the fastest attack, sustain level Fh and a linear sustain decrease of 8 a sample (ADSR 000Fh / 4B00h),
 * so its ENVX steps once every sample; SPUSTAT bit 11's turn stamped (E); a scattered wait; then 1,600 turns of a loop that only
 * reads Timer 2 and the voice's ENVX into arrays (22 cycles a turn), read afterwards for every turn where ENVX differs from the
 * turn before. All stamps are one unwrapped Timer 2 count; each wait has 0-47 single cycles added. Lines:
 *   X trial v E n  lo:hi:new lo:hi:new ...   the first 12 changes: ENVX read old at lo and `new` (hex) at hi, consecutive reads
 * (hi - E) mod 768 against v is the answer. Voices 0, 1, 2, 3, 6, 11, 12, 17, 22, 23, 8 trials each. Every wait is bounded.
 * No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-envxslot --frames=3000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define D4_MADR 0x1F8010C0u
#define D4_BCR  0x1F8010C4u
#define D4_CHCR 0x1F8010C8u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}
static void modeShown(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4)); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static uint32_t edge() {
    const uint32_t t0 = now();
    const uint16_t s0 = read16(SPU(0x1AE)) & 0x0800;
    for (;;) {
        const uint16_t t = read16(T2_COUNT);
        const uint16_t s = read16(SPU(0x1AE)) & 0x0800;
        const uint32_t tb = stamp(t);
        if (s != s0) return tb;
        if (tb - t0 > 600000u) { printf("E timeout\n"); return tb; }
    }
}
static void key(int v, uint16_t adsrLo, uint16_t adsrHi) {
    write16(SPU(v * 16 + 0x0), 0); write16(SPU(v * 16 + 0x2), 0);
    write16(SPU(v * 16 + 0x4), 0x1000);
    write16(SPU(v * 16 + 0x6), 0x2000 / 8);
    write16(SPU(v * 16 + 0x8), adsrLo); write16(SPU(v * 16 + 0xA), adsrHi);
    const uint32_t kl = 1u << v;
    if (v < 16) write16(SPU(0x188), (uint16_t)kl); else write16(SPU(0x18A), (uint16_t)(kl >> 16));
}

#define TURNS 1600
static uint16_t tt[TURNS], ee[TURNS];
static uint32_t blocks[32] __attribute__((aligned(16)));

static void trial(int id, int v) {
    key(v, 0x000F, 0x4B00);                                         /* attack at once, then 8 down a sample */
    idle(12000);
    const uint32_t w = 1500u + rnd(1536), f = rnd(48);
    const uint32_t tE = edge();
    idle(w); fine(f);
    const uint32_t ra = SPU(v * 16 + 0xC);
    for (int i = 0; i < TURNS; i++) { tt[i] = read16(T2_COUNT); ee[i] = read16(ra); }
    static uint32_t lo[12], hi[12]; static uint16_t nv[12];
    int n = 0; uint32_t prev = 0;
    for (int i = 0; i < TURNS; i++) {
        const uint32_t t = stamp(tt[i]);
        if (i && ee[i] != ee[i - 1] && n < 12) { lo[n] = prev; hi[n] = t; nv[n] = ee[i]; n++; }
        prev = t;
    }
    now(); printf("X %d %d %u %d", id, v, (unsigned)tE, n);
    for (int k = 0; k < n; k++) { now(); printf(" %u:%u:%04x", (unsigned)lo[k], (unsigned)hi[k], nv[k]); }
    now(); printf("\n"); now();
    key(v, 0x000F, 0x0000);                                         /* parked again: a flat envelope */
    idle(4000);
}

int main() {
    printf("\nspu/spu-envxslot\n");
    printf("Where in the sample a voice's ENVX changes, by the voice: X trial voice E n lo:hi:new ... No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1AC), 0x0004);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    idle(100000);
    {
        uint8_t* b = (uint8_t*)blocks;
        for (int i = 0; i < 128; i++) b[i] = 0;
        b[1] = 0x07; b[17] = 0x07;                                  /* silent blocks, each loops to itself */
        modeShown(0);
        write16(SPU(0x1A6), 0x2000 / 8);
        modeShown(2);
        write32(D4_MADR, (uint32_t)blocks & 0xFFFFFF);
        write32(D4_BCR, (2u << 16) | 16);
        write32(D4_CHCR, 0x01000201u);
        for (int i = 0; i < 400000 && (read32(D4_CHCR) & 0x01000000); i++) { now(); }
        if (read32(D4_CHCR) & 0x01000000) { printf("T fill %x\n", (unsigned)read32(D4_CHCR)); write32(D4_CHCR, 0); }
        for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
        idle(20000);
        modeShown(0);
        for (int v = 0; v < 24; v++) key(v, 0x000F, 0x0000);
        idle(60000);
    }
    static const int V[10] = { 0, 1, 2, 3, 6, 11, 12, 17, 22, 23 };
    int id = 0;
    for (int r = 0; r < 8; r++) for (int k = 0; k < 10; k++) trial(id++, V[k]);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-envxslot: Done.\n");
    for (;;) VSync(0);
}
