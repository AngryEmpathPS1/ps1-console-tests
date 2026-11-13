/*
 * gpu/gpu-a0abort10 - when does a CHOPPED or a SyncMode 1 GPU transfer, or an ordering-table clear, take the bus after its store to
 * CHCR, and does the CPU run on under it? gpu-a0abort9's calibration block for the other kinds of DMA (2026-11-12)
 *
 * WHY. hwtests/gpu-a0abort9 on the second SCPH-5501 and the PSone (STATUS "gpu-a0abort9 on the second SCPH-5501 and the PSone"):
 * four instructions behind the store to CHCR still run before an unchopped RAM-to-GPU burst takes the bus, and the CPU then runs
 * cached instructions under it (v0.377.334). The slower feeders were not asked: a burst chopped 16 + 1 runs a constant 16-20
 * cycles longer on our core than on either console at every length (gpu-a0abort8), and with the line start's fetch moved behind
 * the burst a chopped 16 + 4 loses a word in 17 rows where the consoles lose one in 26. If their start is late the same way,
 * this shows it.
 *
 * WHAT IT DOES. gpu-a0abort9's C 0 rows (a 1,024 x 2 rectangle at (0,256), zeros first, word i's pixels i + 1; a GPUSTAT bit-31
 * change, d = 110 cycles - no scan-out fetch for the next 550 - Timer 0 (ph), Timer 1 (ln) and Timer 2 (t0) read; the store to
 * CHCR, K nops, the store of GP1(01h) with nothing between, the same instructions run once on two words of RAM first; then
 * Timer 2 (tc), CHCR2 polled to its end, Timer 2 (t), the rectangle read back), K 0 .. 15, four times, for
 *   k 0  one burst of 200 words (gpu-a0abort9's own rows: the control)
 *   k 1  a burst chopped 16 words + 1 cycle, 200 words
 *   k 2  chopped 16 + 4, 200 words
 *   k 3  SyncMode 1, 12 blocks of 16
 *   k 4  SyncMode 1, 6 blocks of 32
 * A chopped or blocked transfer hands the bus back between its pieces, so there the cut lands after the FIRST piece once it no
 * longer lands before the transfer: fill says which (0: before it; 16 or 32: behind the first piece), and the words behind the
 * cut go to GP0 as commands - indices 1 .. 200 are all GP0(00h), nothing is drawn.
 * Then O rows: DMA6 clearing a 200-entry ordering table, the same store / K nops / store (a GP1(01h) with no transfer open),
 * K 0 .. 15, four times - only its times.
 *
 * READ IT LIKE THIS. R k K n ph ln t0 tc t s2 fill pre cnt top: tc the cycles from t0 to after the cut's store, t to the transfer's
 * end as CHCR2 shows it. The smallest K with fill > 0 is how many instructions run before the transfer takes the bus. tc less
 * that K's nops is where the bus came back for the store: the same at every K above it if the CPU runs the nops under the
 * transfer's first piece, a cycle more a nop if it stands still. O K n tc t: the same for DMA6 (the clear is one piece). No
 * assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-a0abort10 --emu`.
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
#define MADR6 0x1F8010E0u
#define BCR6  0x1F8010E4u
#define CHCR6 0x1F8010E8u
#define MAXW 1024
#define W 1024
#define H 2

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static volatile uint32_t dummy[2];
static uint32_t ot[256];
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

static void one(int kind, int k, uint32_t d, int n) {
    const uint32_t at = (256u << 16) | 0u, size = ((uint32_t)H << 16) | (uint32_t)W;
    const int bs = kind == 3 ? 16 : kind == 4 ? 32 : 0;
    const uint32_t chcr = kind == 1 ? (0x11000101u | (4u << 16) | (0u << 20)) : kind == 2 ? (0x11000101u | (4u << 16) | (2u << 20))
                        : bs ? 0x01000201u : 0x11000001u;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, at); write32(GP0, ((uint32_t)H << 16) | 0x3FFu);   /* 3FFh rounds up to 1,024; 400h would be 0 */
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, at); write32(GP0, size);
    waitT2(2000);
    write32(MADR2, (uint32_t)buf & 0xFFFFFCu);
    write32(BCR2, bs ? (((uint32_t)(n / bs) << 16) | (uint32_t)bs) : (uint32_t)n);
    fire(&dummy[0], &dummy[1], chcr, k);             /* the same instructions on two words of RAM: their cache lines are in when it counts */
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
    const uint16_t ph = read16(T0_COUNT);                       /* cycles since the hblank that reset Timer 0: this row's true start */
    const uint16_t ln = read16(T1_COUNT);                       /* hblanks since the vblank: this row's line */
    const uint16_t t0 = read16(T2_COUNT);                       /* free-running: mod 256, the row's place on the refresh grid */
    fire((volatile uint32_t*)CHCR2, (volatile uint32_t*)GP1, chcr, k);
    const uint16_t tc = (uint16_t)(read16(T2_COUNT) - t0);      /* behind the cut's store */
    for (int i = 0; i < 200000 && (read32(CHCR2) & 0x01000000u); i++) {}   /* the rest of the transfer (bounded) */
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    const uint32_t s2 = read32(GP1);
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
    printf("R %d %d %d %u %u %u %u %u %02x %d %d %d %d\n", kind, k, n, (unsigned)ph, (unsigned)ln, (unsigned)t0, (unsigned)tc, (unsigned)t, (unsigned)((s2 >> 24) & 0x1F), fill, pre, cnt, top);
}

static void otc(int k, int n) {
    gpuIdle();
    write32(GP1, 0x01000000u);
    waitT2(2000);
    write32(MADR6, (uint32_t)&ot[n - 1] & 0xFFFFFCu);
    write32(BCR6, (uint32_t)n);
    fire(&dummy[0], &dummy[1], 0x11000002u, k);
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < 110) {}
    const uint16_t t0 = read16(T2_COUNT);
    fire((volatile uint32_t*)CHCR6, (volatile uint32_t*)GP1, 0x11000002u, k);
    const uint16_t tc = (uint16_t)(read16(T2_COUNT) - t0);
    for (int i = 0; i < 200000 && (read32(CHCR6) & 0x01000000u); i++) {}
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    printf("O %d %d %u %u\n", k, n, (unsigned)tc, (unsigned)t);
}

int main() {
    printf("\ngpu/gpu-a0abort10\n");
    printf("R k K n ph ln t0 tc t s2 fill pre cnt top: a GPU transfer of n words cut by GP1(01h) K nops behind the store that started it. k 0 one\n");
    printf("burst, 1 chopped 16 + 1, 2 chopped 16 + 4, 3 SyncMode 1 blocks of 16, 4 of 32; tc / t = cycles from t0 to behind the cut / to the\n");
    printf("end of the transfer. O K n tc t: DMA6 clearing n entries, the same sequence. No assertions.\n\n");
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
    write32(DPCR, read32(DPCR) | 0x08000000u);                    /* DMA6 too */
    for (int p = 0; p < 4; p++) for (int kind = 0; kind <= 4; kind++) for (int k = 0; k <= 15; k++) one(kind, k, 110, kind >= 3 ? 192 : 200);
    for (int p = 0; p < 4; p++) for (int k = 0; k <= 15; k++) otc(k, 200);
    write16(T1_MODE, m1);
    write16(T0_MODE, m0);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-a0abort10: Done.\n");
    for (;;) VSync(0);
}
