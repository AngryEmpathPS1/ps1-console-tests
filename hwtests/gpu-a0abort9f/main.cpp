/*
 * gpu/gpu-a0abort9f - THE EMULATOR'S SECOND TWIN of gpu-a0abort9, NOT FOR A CONSOLE: its C 1 rows at K 3, 4 and 5 and its C 2 rows at K 3
 * (gpu-a0abort9e has K 0, 1, 2: a longer hold and a later cut read alike on the floor, so a longer refill needs the later cuts beside it).
 * On the consoles the CPU runs the K nops UNDER the burst (gpu-a0abort9: t - n the same at K 4 .. 14) and the cut lands as the bus
 * comes back, whatever K; a core that stands its CPU still through a burst runs them after it, so its K 5 is five cycles late. This
 * reads such a core where the consoles cut. The rest is gpu-a0abort9's text:
 *
 * gpu/gpu-a0abort9 - how long a scan-out refill holds a burst's intake, at EVERY start of the burst: the cut straight behind the
 * burst's last word, the burst's start swept by single cycles, each row's place on the DRAM-refresh grid recorded (2026-11-12)
 *
 * WHY. hwtests/gpu-a0abort8 on the second SCPH-5501 and the PSone (STATUS "gpu-a0abort8 on the second SCPH-5501 and the PSone"):
 * a burst loses words at every refill it meets, the refill takes the port ~2.5 cycles later than our intake has it and holds it
 * ~3 cycles longer at three starts in seventeen - but that test's cut comes ~27 cycles after the burst's last word (the CHCR poll
 * and a GPUSTAT read first), so a hold shows only where it outlasts that, and neither the hold at the other fourteen starts, nor
 * where it begins and ends, nor whether a part of the refill is served early in the port's idle cycles could be read. Three
 * candidate changes each fitted one test by losing another (the same entry's table).
 *
 * WHAT IT DOES. gpu-a0abort8's rows (a 1,024 x 2 rectangle at (0,256): rows of 512 words, no row start near; zeros first, word i's
 * pixels i + 1; a GPUSTAT bit-31 change, d cycles, Timer 0 (ph), Timer 1 (ln) and Timer 2 (t0) read, DMA2 of n words as ONE BURST,
 * GP1(01h), the rectangle read back) with the cut moved: K >= 0 is the store to CHCR, K nops, the store to GP1 - no poll, no read
 * between (the same instructions run once on two words of RAM first, so their cache lines are in); the burst stops the CPU, so the
 * cut lands K cycles (less what ran before the burst took the bus) behind its last word, while words are still in the GPU's FIFO. K = -1 is gpu-a0abort8's own sequence (poll CHCR, read GPUSTAT, cut).
 *   C 0  the cut's own delay: d 110 (no fetch within the burst), n 200, K 0 .. 15, four times
 *   C 1  LONG, per refill r = 1, 2, 3: the burst starts ~150 cycles before the refill and runs ~90 past it (n 230); its start
 *        moved by j = 0 .. 19 cycles; K 5, 9 and -1; three times
 *   C 2  SHORT, per refill: at j = 0, 5, 10, 15 the burst's END swept across the refill a word at a time, n 112 .. 186; K 5
 * Timer 2 runs free from the program's start (one write to its mode, never again), so t0 mod 256 is the row's place on the
 * 256-cycle DRAM-refresh grid up to one constant a run: where the burst's refresh pauses fell.
 *
 * READ IT LIKE THIS. R c r K d n ph ln t0 t s2 fill pre cnt top: c the class, r the refill aimed at (0: none), ph / ln Timer 0 /
 * Timer 1 at the start (displayed lines: ln 0 .. 236), t0 Timer 2 then, t the cycles from there to after the cut, s2 GPUSTAT's
 * bits 28-24 after it; fill the words stored (VRAM's leading non-zero words), pre the first missing index, cnt the indices in
 * place, top the highest index stored. n - fill is what the cut found not yet out of the intake. C 0 gives that count with no
 * fetch about, per K (and the K below which the cut ran before the burst: fill 0). C 1: n - fill less C 0's at the same K is the
 * backlog the refill left - the cycles it held the intake, less 16 a lap (top - cnt says a lap) - at each start ph. C 2: the n at
 * which n - fill begins to climb is where the hold begins. No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-a0abort9 --emu`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T1_COUNT 0x1F801110u
#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MAXW 1024
#define W 1024
#define H 2

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static volatile uint32_t dummy[2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

/* the store that starts the burst, K nops, the store that cuts: nothing the compiler can put between */
#define FIRE(NOPS) __asm__ volatile(".set push\n.set noreorder\nsw %0, 0(%1)\n" NOPS "sw %2, 0(%3)\n.set pop\n" : : "r"(chcr), "r"(pc), "r"(cut), "r"(pg) : "memory")
#define N1 "nop\n"
#define N2 N1 N1
#define N4 N2 N2
#define N8 N4 N4
static void __attribute__((noinline)) fire(volatile uint32_t* pc, volatile uint32_t* pg, uint32_t chcr, int k) {
    const uint32_t cut = 0x01000000u;
    switch (k) {
        case 0: FIRE(""); break;
        case 1: FIRE(N1); break;
        case 2: FIRE(N2); break;
        case 3: FIRE(N2 N1); break;
        case 4: FIRE(N4); break;
        case 5: FIRE(N4 N1); break;
        case 6: FIRE(N4 N2); break;
        case 7: FIRE(N4 N2 N1); break;
        case 8: FIRE(N8); break;
        case 9: FIRE(N8 N1); break;
        case 10: FIRE(N8 N2); break;
        case 11: FIRE(N8 N2 N1); break;
        case 12: FIRE(N8 N4); break;
        case 13: FIRE(N8 N4 N1); break;
        case 14: FIRE(N8 N4 N2); break;
        default: FIRE(N8 N4 N2 N1); break;
    }
}

static void one(int c, int r, int k, uint32_t d, int n) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)H << 16) | (uint32_t)W;
    const uint32_t chcr = 0x11000001u;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, at); write32(GP0, ((uint32_t)H << 16) | 0x3FFu);   /* 3FFh rounds up to 1,024; 400h would be 0 */
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, at); write32(GP0, size);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, (uint32_t)n);
    if (k >= 0) fire(&dummy[0], &dummy[1], chcr, k);             /* the same instructions on two words of RAM: their cache lines are in when it counts */
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
    const uint16_t ph = read16(T0_COUNT);                       /* cycles since the hblank that reset Timer 0: this row's true start */
    const uint16_t ln = read16(T1_COUNT);                       /* hblanks since the vblank: this row's line */
    const uint16_t t0 = read16(T2_COUNT);                       /* free-running: mod 256, the row's place on the refresh grid */
    if (k >= 0) fire((volatile uint32_t*)CHCR2, (volatile uint32_t*)GP1, chcr, k);
    else {                                                       /* gpu-a0abort8's cut: poll, a GPUSTAT read, then GP1(01h) */
        write32(CHCR2, chcr);
        uint32_t polls = 0;
        while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
        (void)read32(GP1);
        write32(GP1, 0x01000000u);
    }
    const uint32_t s2 = read32(GP1);
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    for (int i = 0; i < 200000 && (read32(CHCR2) & 0x01000000u); i++) {}   /* a cut that ran before the burst: let it end (bounded) */
    if (read32(CHCR2) & 0x01000000u) write32(CHCR2, 0);
    waitT2(3000);
    write32(GP1, 0x01000000u);                                   /* and whatever such a burst left in the command buffer */
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int j = 0; j < MAXW; j++) { const uint32_t v = read32(GP0); back[2 * j] = (uint16_t)v; back[2 * j + 1] = (uint16_t)(v >> 16); }
    int pre = 0, cnt = 0, top = 0, fill = 0;
    while (fill < MAXW && back[2 * fill] != 0) fill++;          /* the words the GPU stored: VRAM fills in order, whatever was lost */
    while (pre < MAXW && back[2 * pre] == (uint16_t)(pre + 1)) pre++;
    for (int j = 0; j < MAXW; j++) {
        if (back[2 * j] == (uint16_t)(j + 1)) cnt++;
        if (back[2 * j] > top && back[2 * j] <= MAXW) top = back[2 * j];
    }
    printf("R %d %d %d %u %d %u %u %u %u %02x %d %d %d %d\n", c, r, k, (unsigned)d, n, (unsigned)ph, (unsigned)ln, (unsigned)t0, (unsigned)t, (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

int main() {
    printf("\ngpu/gpu-a0abort9f\n");
    printf("R c r K d n ph ln t0 t s2 fill pre cnt top: a burst of n words into 1024 x 2 cut by GP1(01h) K nops behind the store that started it\n");
    printf("(K -1: after a CHCR poll and a GPUSTAT read), read back. c 0 no fetch near, 1 through refill r, 2 ending across it; ph / ln / t0 =\n");
    printf("Timer 0 (cycles since the hblank) / Timer 1 (hblanks since the vblank) / Timer 2 (free-running) at the start. No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)(i + 1) << 16) | (uint32_t)(i + 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);                                         /* once: Timer 2 keeps its place on the refresh grid for the whole run */
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    const uint16_t m0 = read16(T0_MODE);
    write16(T0_MODE, 0x0003);                                    /* the system clock, reset at each hblank */
    const uint16_t m1 = read16(T1_MODE);
    write16(T1_MODE, 0x0103);                                    /* hblanks, reset at the vblank */
    for (int p = 0; p < 4; p++) for (int k = 0; k <= 15; k++) one(0, 0, k, 110, 200);
    /* a refill takes the port at x ~380 / 784 / 1,188 cycles after the hblank (gpu-a0abort8); d = x + 151 - j starts the burst
     * ~150 cycles before it (Timer 0 reads d - 301 or so at the start) */
    static const int XR[3] = { 380, 784, 1188 };
    static const int KS[3] = { 3, 4, 5 };
    for (int r = 1; r <= 3; r++) {
        const int d0 = XR[r - 1] + 151;
        for (int p = 0; p < 3; p++) for (int j = 0; j < 20; j++) for (int q = 0; q < 3; q++) one(1, r, KS[q], (uint32_t)(d0 - j), 230);
        for (int j = 0; j <= 15; j += 5) for (int n = 112; n <= 186; n++) one(2, r, 3, (uint32_t)(d0 - j), n);
    }
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort9f: Done.\n");
    for (;;) VSync(0);
}
