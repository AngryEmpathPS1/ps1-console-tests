/*
 * gpu/gpu-dma-gapsd - gpu-dma-gaps' instrument with every try kept: a short SyncMode 1 GPU upload, a raw Timer 2 read N single cycles
 * after CHCR2's store, and for each N the values all 16 tries read - so a gap that is sometimes a cycle longer shows as two values at
 * its edge (2026-11-05, for the SCPH-1001 and the SCPH-5501).
 *
 * WHY. hwtests/gpu-dma-storow on the SCPH-1001: the old GPU lets a third CPU store into about one gap in eight between a 16-word
 * upload's blocks (2.11-2.14 stores a block at every row width, where the second SCPH-5501's third store comes only where a row
 * starts and this emulator's old GPU gives 2.01-2.04), and the 1001's gpu-dma-gaps tries leave the common value ~25% more often than
 * the 5501's. If the old GPU's request for the next block comes a cycle late in some gaps (its own clock's phase against the CPU's,
 * say), the read N cycles in lands free in those tries and held in the rest - which gpu-dma-gaps' one value per N cannot show.
 *
 * WHAT IT DOES. As gpu-dma-gaps (SyncMode 1, GP0(A0h) into a rectangle of R words a row at (512, 0), K blocks of B words, a raw Timer 2
 * read N = 0..191 single cycles after the store, each try started on the DRAM refresh's grid at place P), for the cells B 16 R 64 K 4,
 * B 16 R 16 K 4 and B 4 R 16 K 8 at P 0 and 128, 16 tries a N (each after a different wait, so at another place of the video). A line
 * per cell and place: B R K P, then for each N its value when all 16 agree, else every value with its count (v*c, highest count first).
 *
 * READ IT LIKE THIS. Flat runs are blocks, steps a cycle each are gaps; two values at a gap's last N are a gap of two lengths. No
 * assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-dma-gapsd --frames=3000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T1_MODE  0x1F801114u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define DPCR     0x1F8010F0u
#define MADR2    0x1F8010A0u
#define BCR2     0x1F8010A4u
#define CHCR2    0x1F8010A8u
#define GP0      0x1F801810u
#define GP1      0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

#define NMAX 192
/* n more cycles, one at a time: a jump into a run of 192 nops, n from its end (cached code, a cycle each) */
static void sled(uint32_t n) {
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "la $t0, 1f\n"
        "sll $t1, %0, 2\n"
        "subu $t0, $t0, $t1\n"
        "jr $t0\n"
        "nop\n"
        ".rept 192\nnop\n.endr\n"
        "1:\n"
        ".set pop\n"
        :: "r"(n < NMAX ? n : NMAX - 1) : "t0", "t1");
}

static uint32_t buf[256] __attribute__((aligned(16)));
static uint32_t bsNow, rowNow, kNow;

/* the GPU idle (bit 26: ready for a command; bit 28: ready for DMA blocks), the DMA stopped, bounded */
static void settle() {
    for (unsigned k = 0; k < 20000 && (read32(CHCR2) & 0x01000000u); k++) {}
    write32(CHCR2, 0);
    for (unsigned k = 0; k < 20000 && (read32(GP1) & 0x14000000u) != 0x14000000u; k++) {}
}
static void setup() {
    write32(GP1, 0x01000000u);   /* reset the command buffer (and a rectangle left one word short) */
    settle();
    const uint32_t words = kNow * bsNow, rows = (words + rowNow - 1) / rowNow;
    write32(GP0, 0xA0000000u); write32(GP0, 512u); write32(GP0, (rows << 16) | (rowNow * 2));
    write32(GP1, 0x04000002u);   /* DMA direction CPU->VRAM */
    write32(MADR2, (uint32_t)buf & 0x1FFFFFu);
    write32(BCR2, (kNow << 16) | bsNow);
}
/* Start at Timer 2's count mod 256 = p, to the cycle: spin to 6-56 cycles short of it, read the count once, slide the rest. The
 * read-to-store distance is fixed code, so every start lands at the same place on the refresh's grid. */
static __attribute__((noinline, noclone)) uint16_t one(uint32_t n, uint32_t p) {
    for (unsigned k = 0; k < 4000; k++) { const uint32_t d = (p - read16(T2_COUNT)) & 0xFFu; if (d >= 6 && d < 56) break; }
    const uint32_t v = read16(T2_COUNT);
    sled((p - v - 6) & 0xFFu);
    const uint16_t a0 = read16(T2_COUNT);
    write32(CHCR2, 0x01000201u);
    sled(n);
    const uint16_t a1 = read16(T2_COUNT);
    return (uint16_t)(a1 - a0);
}

struct Cell { uint16_t b, r, k; };
static const Cell CELLS[] = { { 16, 64, 4 }, { 16, 16, 4 }, { 4, 16, 8 } };
#define TRIES 16
static uint16_t vals[NMAX][TRIES];

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-dma-gapsd\n");
    printf("A short SyncMode 1 GPU upload, a timer read N single cycles after CHCR2's store: every value of 16 tries by N (v when all agree, else v*count), N = 0..191. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m1 = read16(T1_MODE), m2 = read16(T2_MODE);
    write16(T1_MODE, 0x0100);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    for (int i = 0; i < 256; i++) buf[i] = 0x7FFF7FFFu ^ (uint32_t)i;
    write32(DPCR, read32(DPCR) | 0x800u);
    bsNow = 4; rowNow = 4; kNow = 8; setup(); one(0, 0); settle(); setup(); one(NMAX - 1, 0); settle();   /* cache it */
    for (unsigned c = 0; c < sizeof CELLS / sizeof CELLS[0]; c++) {
        bsNow = CELLS[c].b; rowNow = CELLS[c].r; kNow = CELLS[c].k;
        for (uint32_t p = 0; p < 256; p += 128) {
            for (int t = 0; t < TRIES; t++) for (int n = 0; n < NMAX; n++) {
                setup();
                waitCycles(100 + (uint32_t)((t * 37 + n * 11) % 64));
                vals[n][t] = one((uint32_t)n, p);
                settle();
            }
            printf("%u %u %u %u:", (unsigned)bsNow, (unsigned)rowNow, (unsigned)kNow, (unsigned)p);
            for (int n = 0; n < NMAX; n++) {
                int same = 1; for (int i = 1; i < TRIES; i++) if (vals[n][i] != vals[n][0]) same = 0;
                if (same) { printf(" %u", (unsigned)vals[n][0]); continue; }
                /* every distinct value with its count, the most common first */
                uint16_t dv[TRIES]; int dc[TRIES], nd = 0;
                for (int i = 0; i < TRIES; i++) { int k = 0; while (k < nd && dv[k] != vals[n][i]) k++; if (k == nd) { dv[nd] = vals[n][i]; dc[nd] = 0; nd++; } dc[k]++; }
                for (int a = 0; a < nd; a++) for (int b = a + 1; b < nd; b++) if (dc[b] > dc[a]) { int tc = dc[a]; dc[a] = dc[b]; dc[b] = tc; uint16_t tv = dv[a]; dv[a] = dv[b]; dv[b] = tv; }
                printf(" ");
                for (int a = 0; a < nd; a++) printf("%s%u*%d", a ? "," : "", (unsigned)dv[a], dc[a]);
            }
            printf("\n");
        }
    }
    write32(GP1, 0x04000000u);
    write16(T1_MODE, m1); write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-dma-gapsd: Done.\n");
    for (;;) {}   /* park after Done. */
}
