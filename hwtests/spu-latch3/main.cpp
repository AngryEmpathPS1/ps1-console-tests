/*
 * spu/spu-latch3 - when the reverb's ALL-PASS writes see the master enable, beside its same-side and different-side writes (2026-11-15)
 *
 * WHY. hwtests/spu-latch2 placed the reverb step's mSAME and mDIFF writes in the sample (each is gated by SPUCNT bit 7 at its own
 * time: 626 and 650-652 cycles after SPUSTAT bit 11's turn; v0.377.350). Its settings left the all-pass writes writing zero
 * over zero, so their times are not measured and this core gives them mDIFF's (SPU_REVERB_APF_AT, KNOWN-OPEN).
 *
 * WHAT IT DOES. spu-latch2's reverb trial with the work area FILLED with 1111h instead of zero and no voice: with the reverb
 * off nothing is written; a step whose write is let through leaves something else than 1111h at that write's address (the
 * same-side and different-side writes ~0, the all-pass writes 0), so the first changed step of each of the eight addresses
 * shows on its own. A trial writes mBASE at tb and the master enable at tw, each right behind a Timer 2 read at a scattered
 * time (a sample is 768 cycles; 0-47 single cycles added), runs 24 steps, and reads the area back at 220931E1h. Lines:
 *   E d t0 t1                       SPUSTAT bit 11 read as !d at t0 and as d at t1 (the sample clock's phase on the same count)
 *   R g tb tw a b c d e f g h       g: the delay asked between mBASE and the enable (0 or 700); the first changed step of
 *                                   mLSAME, mRSAME, mLDIFF, mRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2 (-1 none)
 * 240 trials at gap 0 and 80 at 700. Interrupts off, sound muted. Every wait is bounded. No assertions.
 * THE RULE: run here first: `node hwtests/run.js spu-latch3 --frames=8000 --quiet=3000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 400000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { printf("T %x\n", (unsigned)read32(0x1F8010C8)); write32(0x1F8010C8, 0); }
    if (toSpu) idle(8000);
    spuMode(0);
}
static const uint16_t INPUT_ONLY[32] = {
    0x0040, 0x0040, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x4000,   /* vAPF2 4000h: the second all-pass write is not the fill */
    0x0100, 0x0300, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0500, 0x0700,
    0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0040, 0x0080, 0x0280, 0x0480, 0x0680,
    0x8000, 0x8000,
};
#define WA_SIZE 0x4000u
#define WA_BASE (0x80000u - WA_SIZE)
#define WA_WORDS (WA_SIZE / 4)
#define REV_STEPS 24

static uint32_t area[WA_WORDS] __attribute__((aligned(16)));
static uint32_t blocks[16] __attribute__((aligned(16)));
static uint16_t ring[512] __attribute__((aligned(16)));
static uint32_t lcg = 12345;
static uint32_t rnd(uint32_t n) { lcg = lcg * 1664525u + 1013904223u; return (lcg >> 10) % n; }

/* 0-47 cycles more, one at a time: every wait above ends on a poll loop's turn (~31 cycles), and spuMode's wait on SPUSTAT
 * ties those turns to the sample clock - without this the writes fall on ~25 places of the sample. */
static void __attribute__((noinline)) fine(uint32_t n) {
    for (uint32_t k = n >> 2; k; k--) __asm__ volatile("nop");
    switch (n & 3) { case 3: __asm__ volatile("nop"); /* fall through */ case 2: __asm__ volatile("nop"); /* fall through */ case 1: __asm__ volatile("nop"); default: break; }
}
static void voiceSetup() {
    write16(SPU(0x010), 0x3FFF); write16(SPU(0x012), 0x3FFF);
    write16(SPU(0x014), 0x1000);
    write16(SPU(0x016), 0x1000 / 8);
    write16(SPU(0x018), 0x000F); write16(SPU(0x01A), 0x0000);
}
static void allOff() {
    write16(SPU(0x188), 0); write16(SPU(0x18A), 0);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
}

static void edges() {
    for (int n = 0; n < 24; n++) {
        const uint32_t t0 = now();
        const uint16_t s0 = read16(SPU(0x1AE)) & 0x0800; uint32_t ta = now();
        for (;;) {
            const uint16_t t = read16(T2_COUNT);
            const uint16_t s = read16(SPU(0x1AE)) & 0x0800;
            const uint32_t tb = stamp(t);
            if (s != s0) { printf("E %d %u %u\n", s ? 1 : 0, (unsigned)ta, (unsigned)tb); break; }
            ta = tb;
            if (tb - t0 > 600000u) { printf("E timeout\n"); return; }
        }
        idle(150000u + rnd(40000)); fine(rnd(48));                 /* most of the half, then poll again from a new place */
    }
}
static void updates() {
    for (int n = 0; n < 96; n++) {
        idle(3000u + rnd(1536)); fine(rnd(48));
        cnt ^= 1;
        const uint16_t t = read16(T2_COUNT);
        write16(SPU(0x1AA), cnt);
        const uint32_t tw = stamp(t);
        uint32_t ta = tw;
        for (;;) {
            const uint16_t t1 = read16(T2_COUNT);
            const uint16_t s = read16(SPU(0x1AE)) & 1;
            const uint32_t tb = stamp(t1);
            if (s == (cnt & 1)) { printf("U %d %u %u %u\n", cnt & 1, (unsigned)tw, (unsigned)ta, (unsigned)tb); break; }
            ta = tb;
            if (tb - tw > 100000u) { printf("U timeout\n"); break; }
        }
    }
    cnt &= (uint16_t)~1; write16(SPU(0x1AA), cnt);
}
static void konTrial() {
    allOff();
    for (int i = 0; i < 256; i++) ((uint32_t*)ring)[i] = 0;
    spuDma(0x0800, (uint32_t*)ring, 256, true);
    voiceSetup();
    idle(2000u + rnd(1536)); fine(rnd(48));
    const uint16_t t = read16(T2_COUNT);
    write16(SPU(0x188), 0x0002);
    const uint32_t tw = stamp(t);
    idle(120u * 768u);                              /* the onset written, well inside the ring's 512 */
    spuDma(0x0800, (uint32_t*)ring, 256, false);
    write16(SPU(0x18C), 0x0002);
    /* the ring holds ~150 samples of the voice: its first is the non-zero index whose predecessor is zero */
    int onset = -1;
    /* ...with 40 zeros before it: a DMA read's first 32 halfwords are as the read FIFO took them at the mode switch (v0.377.278),
     * so a run of the voice that wraps past index 0 can show a second edge at index 32 */
    for (int i = 0; i < 512 && onset < 0; i++) {
        if (!ring[i]) continue;
        int z = 0;
        while (z < 40 && !ring[(i + 511 - z) & 511]) z++;
        if (z == 40) onset = i;
    }
    printf("K %u %d\n", (unsigned)tw, onset);
}
static void revTrial(uint32_t gap) {
    allOff();
    cnt = (uint16_t)(cnt & ~0x0080); write16(SPU(0x1AA), cnt);
    for (int i = 0; i < (int)WA_WORDS; i++) area[i] = 0x11111111u;
    spuDma(WA_BASE, area, WA_WORDS, true);
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * i), INPUT_ONLY[i]);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    idle(40u * 768u + rnd(3072)); fine(rnd(48));
    const uint16_t t0 = read16(T2_COUNT);
    write16(SPU(0x1A2), (uint16_t)(WA_BASE / 8));
    const uint32_t tb = stamp(t0);
    if (gap) idle(gap);
    const uint16_t t1 = read16(T2_COUNT);
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0080));
    const uint32_t tw = stamp(t1);
    cnt |= 0x0080;
    idle(REV_STEPS * 1536u);
    cnt &= (uint16_t)~0x0080; write16(SPU(0x1AA), cnt);
    write16(SPU(0x18C), 0x0002);
    spuDma(WA_BASE, area, WA_WORDS, false);
    static const uint16_t OFF[8] = { 0x0100, 0x0300, 0x0500, 0x0700, 0x0080, 0x0280, 0x0480, 0x0680 };
    const uint16_t* h = (const uint16_t*)area;
    int f[8];
    for (int l = 0; l < 8; l++) {
        f[l] = -1;
        for (int s = 0; s < REV_STEPS + 8; s++) if (h[(OFF[l] * 4 + s) & (WA_SIZE / 2 - 1)] != 0x1111) { f[l] = s; break; }
    }
    /* in three pieces with the counter read between: one long line took more than the counter's 65,536 cycles on the emulator's
     * TTY and a wrap was lost (the E rows after the trials read 65,536 off) */
    now(); printf("R %u %u %u", (unsigned)gap, (unsigned)tb, (unsigned)tw);
    now(); printf(" %d %d %d %d", f[0], f[1], f[2], f[3]);
    now(); printf(" %d %d %d %d\n", f[4], f[5], f[6], f[7]); now();
}

int main() {
    printf("\nspu/spu-latch3\n");
    printf("When each of the reverb step's eight writes sees the master enable: E d t0 t1 / R gap tb tw sameL sameR diffL diffR apf1L apf1R apf2L apf2R. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)blocks;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    allOff();
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    spuDma(0x1000, blocks, 16, true);

    edges();
    edges();
    for (int n = 0; n < 240; n++) revTrial(0);
    for (int n = 0; n < 80; n++) revTrial(700);
    edges();
    allOff();
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-latch3: Done.\n");
    for (;;) VSync(0);
}
