/*
 * spu/spu-writetime - when does each block of a DMA WRITE of sound RAM move? (2026-11-15)
 *
 * WHY. A 1,024-word DMA4 write in 16-word blocks takes 70,005-70,111 cycles on the second SCPH-5501, the PSone and the SCPH-7502
 * (hwtests/spu-writetail) and 69,263 on this core - 12 cycles a block, the whole-console fuzzer's "SPU DMA writes cost 12 more".
 * hwtests/spu-readtime found the READ side on a clock of 32 slots of 24 cycles a sample, 8 of them closed; this is the same
 * instrument turned round.
 *
 * WHAT IT DOES. hwtests/spu-readtime with the transfer mode 2 and D4_CHCR 01000201h: SPUSTAT bit 11's turn (stamp E), a scattered
 * wait, SPUCNT's mode 2 (stamp M), SPUSTAT showing it (S), a short or a long scattered wait, D4_CHCR (stamp C before the write),
 * then 3,000 turns of a loop that only reads Timer 2 and D4_MADR into two arrays (22 cycles a turn; a block's bus time is one
 * long turn). Both 1F801014h values (U as the BIOS left it, S = 220931E1h), blocks of 16, 8 and 4 words, 32 blocks a transfer;
 * the data written is each halfword's index. Lines as spu-readtime's:
 *   R trial mode bs E M S C end n      end: the stamp at which D4_CHCR's busy bit was seen clear
 *   B trial  d:g d:g ...                each step of MADR: d = stamp - C of the turn that saw it, g = that turn's length
 * Every wait is bounded; a channel still live at its wait's end is stopped and printed ("T chcr madr bcr"). No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-writetime`.
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

#define NB 32
static uint32_t buf[NB * 16] __attribute__((aligned(16)));
static uint32_t st[NB + 8], gp[NB + 8];
#define TURNS 3000
static uint16_t tt[TURNS], mm[TURNS];

static void trial(int id, char mode, uint32_t bs) {
    /* mode 0, seen */
    cnt = (uint16_t)(cnt & ~0x30); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
    write16(SPU(0x1A6), 0x1000 / 8);
    /* SPUSTAT bit 11's turn: the sample clock's phase */
    uint32_t tE = 0;
    {
        const uint32_t t0 = now();
        const uint16_t s0 = read16(SPU(0x1AE)) & 0x0800;
        for (;;) {
            const uint16_t t = read16(T2_COUNT);
            const uint16_t s = read16(SPU(0x1AE)) & 0x0800;
            const uint32_t tb = stamp(t);
            if (s != s0) { tE = tb; break; }
            if (tb - t0 > 600000u) { printf("E timeout\n"); break; }
        }
    }
    idle(1000u + rnd(768)); fine(rnd(48));
    cnt = (uint16_t)((cnt & ~0x30) | 0x20);
    const uint32_t tM = now();
    write16(SPU(0x1AA), cnt);
    uint32_t tS = 0;
    for (int i = 0; i < 100000; i++) { const uint32_t t = now(); if ((read16(SPU(0x1AE)) & 0x3F) == (cnt & 0x3F)) { tS = t; break; } }
    idle((id & 1) ? 4000u + rnd(1024) : rnd(2048)); fine(rnd(48));
    write32(D4_MADR, (uint32_t)buf & 0xFFFFFF);
    write32(D4_BCR, ((uint32_t)NB << 16) | bs);
    const uint16_t m0 = (uint16_t)read32(D4_MADR);
    int n = 0;
    const uint32_t tC = now();
    write32(D4_CHCR, 0x01000201u);
    /* every turn kept and read afterwards, so a turn is as short and as even as it can be: a block's bus time shows as one
     * long turn, and the block began within a plain turn's length of that turn's start */
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
    const uint32_t tEnd = now();
    if (read32(D4_CHCR) & 0x01000000) {
        printf("T %x %x %x\n", (unsigned)read32(D4_CHCR), (unsigned)read32(D4_MADR), (unsigned)read32(D4_BCR));
        write32(D4_CHCR, 0);
    }
    printf("R %d %c %u %u %u %u %u %u %d\n", id, mode, (unsigned)bs, (unsigned)tE, (unsigned)tM, (unsigned)tS, (unsigned)tC, (unsigned)tEnd, n);
    printf("B %d", id);
    for (int i = 0; i < n; i++) printf(" %u:%u", (unsigned)st[i], (unsigned)gp[i]);
    printf("\n");
    clk = 0; lastT = read16(T2_COUNT);                              /* the print took longer than the counter holds */
}

int main() {
    printf("\nspu/spu-writetime\n");
    printf("Each block of a DMA write of sound RAM stamped on Timer 2: R trial mode bs E M S C end n; B trial d:g per block. No assertions.\n\n");
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
    idle(200000);
    for (int i = 0; i < NB * 16; i++) buf[i] = (uint32_t)(2 * i) | ((uint32_t)(2 * i + 1) << 16);
    int id = 0;
    static const uint32_t BS[3] = { 16, 8, 4 };
    for (int b = 0; b < 3; b++) {
        const int trials = b ? 16 : 48;
        for (int k = 0; k < 2 * trials; k++) {
            const bool stable = (k & 2) != 0;                       /* U U S S ...: a short and a long wait of each */
            write32(0x1F801014, stable ? 0x220931E1u : spuDelay);
            trial(id++, stable ? 'S' : 'U', BS[b]);
        }
    }
    cnt = (uint16_t)(cnt & ~0x30); write16(SPU(0x1AA), cnt);
    write32(0x1F801014, spuDelay);
    printf("D %08x\n", (unsigned)spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-writetime: Done.\n");
    for (;;) VSync(0);
}
