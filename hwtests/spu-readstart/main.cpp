/*
 * spu/spu-readstart - when does the FIRST block of a DMA read of sound RAM go, and what does it carry? (2026-11-15)
 *
 * WHY. hwtests/spu-readtime: a read whose D4_CHCR write comes 340 cycles or more after SPUSTAT shows the read mode waits for
 * the SPU's FIFO to fill (32 halfwords from ~56 cycles after the SPUCNT write, one in each of 24 of the sample's 32 slots).
 * But dma/fuzz-dma on the same console, which writes D4_CHCR right behind SPUSTAT showing the mode, moves its first block at
 * once - a one-block 16-word read takes 219 cycles - and 267 of its 281 reads carry other data than sound RAM's. Somewhere
 * between "right behind" and 340 cycles the request changes its meaning. This test scans that stretch.
 *
 * WHAT IT DOES. Interrupts off, sound muted. Sound RAM 1000h-2FFFh is written once with each halfword's own index. A trial
 * reads 4 blocks of 16 words from 1000h + (trial mod 64) * 80h, so stale data names where it came from. Two ways in:
 *   Q (as fuzz-dma): SPUSTAT bit 11's turn (stamp E), a wait, mode 0 and SPUSTAT showing it, the address, mode 3 at once;
 *   L (as spu-readtime): mode 0, the address, bit 11's turn (E), a scattered wait of 1,000-1,767 cycles, mode 3.
 * Then SPUSTAT showing mode 3 (stamp S), a wait of nothing (every third trial) or 0-499 cycles and 0-47 single ones, SPUSTAT
 * read once more (its request bits), D4_CHCR (stamp C before the write), and 400 turns of a loop that only reads Timer 2 and
 * D4_MADR into arrays (22 cycles a turn; a block's bus time is one long turn). Both 1F801014h values (U as the BIOS left it,
 * S = 220931E1h). Lines:
 *   R trial way mode E M S C stat n   stamps on one unwrapped Timer 2 count; stat = SPUSTAT just before the CHCR write
 *   B trial d:g d:g ...               each step of MADR: d = stamp - C of the turn that saw it, g = that turn's length
 *   H trial h0 h1 h2 h3 / k0 k1 k2 k3 the first four halfwords of block 0 and of block 1, hex; wanted: the trial's address / 2
 *                                     + 0.. and + 20h.. (the unstable read: FFFFh first, then from the address)
 * 240 trials. Every wait is bounded; a channel still live at its wait's end is stopped and printed ("T chcr madr bcr").
 * No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-readstart`.
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

#define NB 4
#define TURNS 400
static uint32_t buf[NB * 16] __attribute__((aligned(16)));
static uint32_t pat[4096] __attribute__((aligned(16)));
static uint32_t st[NB + 4], gp[NB + 4];
static uint16_t tt[TURNS], mm[TURNS];

/* its own function, never inlined: v1's compiler moved this loop (64 stores, ~290 cycles) down between SPUSTAT showing the mode
 * and the CHCR write, and no trial came sooner than 277 cycles behind it */
static void __attribute__((noinline)) clearBuf() { volatile uint32_t* b = buf; for (int i = 0; i < NB * 16; i++) b[i] = 0xEEEEEEEEu; }

static void trial(int id, char way, char mode) {
    const uint32_t addr = 0x1000u + (uint32_t)(id & 63) * 0x80u;
    uint32_t tE;
    clearBuf();
    if (way == 'Q') {
        tE = edge();
        idle(1000u + rnd(768)); fine(rnd(48));
        modeShown(0);
        write16(SPU(0x1A6), (uint16_t)(addr / 8));
    } else {
        modeShown(0);
        write16(SPU(0x1A6), (uint16_t)(addr / 8));
        tE = edge();
        idle(1000u + rnd(768)); fine(rnd(48));
    }
    /* everything the stretch between SPUSTAT showing the mode and the CHCR write does not need is done BEFORE the mode write
     * (v3: the two divisions and the DMA registers alone kept every trial 280 cycles behind it) */
    const uint32_t w = (id % 3) ? rnd(500) : 0, f = rnd(48);
    write32(D4_MADR, (uint32_t)buf & 0xFFFFFF);
    write32(D4_BCR, ((uint32_t)NB << 16) | 16);
    const uint16_t m0 = (uint16_t)read32(D4_MADR);
    cnt = (uint16_t)(cnt | 0x30);
    const uint32_t tM = now();
    write16(SPU(0x1AA), cnt);
    uint32_t tS = 0;
    for (int i = 0; i < 100000; i++) { const uint32_t t = now(); if ((read16(SPU(0x1AE)) & 0x3F) == (cnt & 0x3F)) { tS = t; break; } }
    if (w) idle(w);
    fine(f);
    const uint16_t stat = read16(SPU(0x1AE));
    int n = 0;
    const uint32_t tC = now();
    write32(D4_CHCR, 0x01000200u);
    for (int i = 0; i < TURNS; i++) { tt[i] = read16(T2_COUNT); mm[i] = (uint16_t)read32(D4_MADR); }
    {
        uint16_t prevM = m0; uint32_t prevT = tC;
        for (int i = 0; i < TURNS; i++) {
            const uint32_t t = stamp(tt[i]);
            if (mm[i] != prevM) { if (n < NB) { st[n] = t - tC; gp[n] = t - prevT; n++; } prevM = mm[i]; }
            prevT = t;
        }
    }
    for (int i = 0; i < 20000 && (read32(D4_CHCR) & 0x01000000); i++) { now(); }
    if (read32(D4_CHCR) & 0x01000000) {
        printf("T %x %x %x\n", (unsigned)read32(D4_CHCR), (unsigned)read32(D4_MADR), (unsigned)read32(D4_BCR));
        write32(D4_CHCR, 0);
    }
    const uint16_t* h = (const uint16_t*)buf;
    printf("R %d %c %c %u %u %u %u %04x %d\n", id, way, mode, (unsigned)tE, (unsigned)tM, (unsigned)tS, (unsigned)tC, stat, n);
    printf("B %d", id);
    for (int i = 0; i < n; i++) printf(" %u:%u", (unsigned)st[i], (unsigned)gp[i]);
    printf("\nH %d %04x %04x %04x %04x / %04x %04x %04x %04x\n", id, h[0], h[1], h[2], h[3], h[32], h[33], h[34], h[35]);
    clk = 0; lastT = read16(T2_COUNT);                              /* the print took longer than the counter holds */
}

int main() {
    printf("\nspu/spu-readstart\n");
    printf("The first block of a DMA read of sound RAM by how soon D4_CHCR follows the read mode: R trial way mode E M S C stat n; B trial d:g; H trial block 0 / block 1. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x1AC), 0x0004);
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    idle(100000);
    /* sound RAM 1000h-4FFFh: each halfword its own index (address / 2) */
    {
        uint16_t* p = (uint16_t*)pat;
        for (uint32_t i = 0; i < 8192; i++) p[i] = (uint16_t)(0x800u + i);
        modeShown(0);
        write16(SPU(0x1A6), 0x1000 / 8);
        modeShown(2);
        write32(D4_MADR, (uint32_t)pat & 0xFFFFFF);
        write32(D4_BCR, (256u << 16) | 16);
        write32(D4_CHCR, 0x01000201u);
        for (int i = 0; i < 400000 && (read32(D4_CHCR) & 0x01000000); i++) { now(); }
        if (read32(D4_CHCR) & 0x01000000) { printf("T fill %x\n", (unsigned)read32(D4_CHCR)); write32(D4_CHCR, 0); }
        for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x0400); i++) { now(); }
        idle(20000);
        modeShown(0);
    }
    for (int id = 0; id < 240; id++) {
        const bool stable = (id & 2) != 0;
        write32(0x1F801014, stable ? 0x220931E1u : spuDelay);
        trial(id, (id & 1) ? 'L' : 'Q', stable ? 'S' : 'U');
    }
    modeShown(0);
    write32(0x1F801014, spuDelay);
    printf("D %08x\n", (unsigned)spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-readstart: Done.\n");
    for (;;) VSync(0);
}
