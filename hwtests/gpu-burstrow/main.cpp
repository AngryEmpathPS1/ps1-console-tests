/*
 * gpu/gpu-burstrow - where in a VRAM ROW a RAM-to-VRAM burst's overrun lands: the row's width, its x, words fed by hand first, and a
 * chopped feed (2026-11-10)
 *
 * WHY. hwtests/gpu-burstline on the second SCPH-5501 (three runs, 476 gaps, no exception): a burst that overruns GP0's FIFO while the
 * scan-out holds VRAM loses its words at VRAM row words 2, 18, 34, 51, 67, 83, 99, 115 - two words past a 16-word boundary in a row's
 * first three units, three words past from the fourth on - and hwtests/gpu-a0lsf's one 384-word row keeps "three past" to word 355.
 * So the GPU's upload intake stops a fixed count past a 16-word unit, and that count steps ONCE in a row, between words 34 and 51.
 * No model of ours has the step (v0.377.319 and the two-buffer knob core put every one three past). What makes it is what this asks:
 *   H1  the row's place: units are counted from the row's first word and the step sits at the row's word 48, whatever the width or x
 *       and however the words arrive (a 17-word third unit, say);
 *   H2  VRAM's x: units sit on VRAM's 32-pixel grid, so a rectangle moved 16 or 2 pixels right moves the gaps against its words;
 *   H3  time, not place: the intake runs behind for the ~48 cycles after a row starts (its writer catching up), so a burst that starts
 *       in the middle of a row already fed by hand, long before, shows "three past" at once, and a chopped feed may change the count.
 *
 * WHAT IT DOES. As hwtests/gpu-burstline (interrupts off, no disc, Timer 2 at the system clock, the display as the runner leaves it,
 * a GPUSTAT bit-31 change = a line edge, then d = 0, 32 .. 2,144 cycles, then DMA2 as one burst to GP0(A0h) at (x0,256); 3,000
 * cycles on, zeros to GP0 until bit 26 sets, the rectangle read back through GP0(C0h)), over these configurations (each word's two
 * pixels are its own index; W in words of two pixels, rows x W <= 1,024 words):
 *    c  W    x0  kind                      c  W    x0  kind
 *    0  128   0  burst (gpu-burstline's)   9   96   0  burst
 *    1  128  32  burst                    10  256   0  burst
 *    2  128  16  burst                    11  512   0  burst
 *    3  128   2  burst                    12  128   0  the first  8 words by hand (GP0 writes) before the line edge, the rest a burst
 *    4  128   1  burst                    13  128   0  the first 24 by hand
 *    5   64   0  burst                    14  128   0  the first 64 by hand
 *    6   48   0  burst                    15  128   0  chopped, 16 words then 1 cycle
 *    7   40   0  burst                    16  128   0  chopped, 32 words then 2 cycles
 *    8   32   0  burst                    17  128   0  chopped, 64 words then 8 cycles
 *
 * READ IT LIKE THIS. R c d t left lost dup ng | position:size ...: t the transfer's cycles, left the zeros it still took, lost the
 * indices never found, dup the read-back words lower than expected and not zero (words stored twice), ng the gaps, then up to 12 gaps
 * as (read-back position, indices skipped). A gap's position mod W is the row word at which the intake stopped.
 * No assertions - compare against your emulator.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-burstrow --frames=4000 --quiet=2000`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define DPCR 0x1F8010F0u
#define MADR2 0x1F8010A0u
#define BCR2  0x1F8010A4u
#define CHCR2 0x1F8010A8u
#define MAXW 1024
#define MAXG 12

struct Cfg { int w, x0, pre, a, b; };                           /* a < 0: no chopping */
static const Cfg CF[] = {
    { 128, 0, 0, -1, 0 }, { 128, 32, 0, -1, 0 }, { 128, 16, 0, -1, 0 }, { 128, 2, 0, -1, 0 }, { 128, 1, 0, -1, 0 },
    { 64, 0, 0, -1, 0 }, { 48, 0, 0, -1, 0 }, { 40, 0, 0, -1, 0 }, { 32, 0, 0, -1, 0 }, { 96, 0, 0, -1, 0 },
    { 256, 0, 0, -1, 0 }, { 512, 0, 0, -1, 0 },
    { 128, 0, 8, -1, 0 }, { 128, 0, 24, -1, 0 }, { 128, 0, 64, -1, 0 },
    { 128, 0, 0, 4, 0 }, { 128, 0, 0, 5, 1 }, { 128, 0, 0, 6, 3 },
};
#define NCF ((int)(sizeof(CF) / sizeof(CF[0])))

static uint32_t buf[MAXW] __attribute__((aligned(16)));
static uint16_t back[MAXW * 2];
static void waitT2(uint32_t n) { const uint16_t s = read16(T2_COUNT); while ((uint16_t)(read16(T2_COUNT) - s) < n) {} }
static void gpuIdle() { for (int i = 0; i < 400000 && (read32(GP1) & 0x14000000u) != 0x14000000u; i++) {} }

static void one(int c, uint32_t d) {
    const Cfg& f = CF[c];
    const int w = f.w * 2, h = MAXW / f.w, words = h * f.w;
    const uint32_t at = (256u << 16) | (uint32_t)f.x0, size = ((uint32_t)h << 16) | (uint32_t)w;
    int fw = (f.x0 + w + 15) & ~15; if (fw > 1024) fw = 1024;
    gpuIdle();
    write32(GP1, 0x01000000u);                                   /* a clean command buffer before each */
    write32(GP0, 0x02000000u); write32(GP0, (256u << 16) | 0u); write32(GP0, ((uint32_t)h << 16) | (uint32_t)fw);
    gpuIdle();
    write32(GP1, 0x04000002u);
    write32(GP0, 0xA0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < f.pre; i++) { write32(GP0, buf[i]); for (volatile int k = 0; k < 4; k++) {} }
    waitT2(2000);
    write32(MADR2, (uint32_t)(buf + f.pre) & 0xFFFFFCu);
    write32(BCR2, (uint32_t)(words - f.pre));
    const uint32_t chcr = f.a < 0 ? 0x11000001u : (0x11000101u | ((uint32_t)f.a << 16) | ((uint32_t)f.b << 20));
    /* a line edge: GPUSTAT bit 31 changes (bounded), then d cycles */
    const uint32_t b0 = read32(GP1) & 0x80000000u;
    for (int i = 0; i < 20000 && (read32(GP1) & 0x80000000u) == b0; i++) {}
    const uint16_t te = read16(T2_COUNT);
    while ((uint16_t)(read16(T2_COUNT) - te) < d) {}
    const uint16_t t0 = read16(T2_COUNT);
    write32(CHCR2, chcr);
    uint32_t polls = 0;
    while ((read32(CHCR2) & 0x01000000u) && polls < 200000) polls++;
    const uint16_t t = (uint16_t)(read16(T2_COUNT) - t0);
    if (polls >= 200000) write32(CHCR2, 0);
    waitT2(3000);
    uint32_t left = 0;
    while (!(read32(GP1) & (1u << 26)) && left < 4096) { write32(GP0, 0); left++; for (volatile int i = 0; i < 8; i++) {} }
    if (left >= 4096) { write32(GP1, 0x01000000u); }
    write32(GP1, 0x04000000u);
    gpuIdle();
    write32(GP0, 0xC0000000u); write32(GP0, at); write32(GP0, size);
    for (int i = 0; i < 400000 && !(read32(GP1) & (1u << 27)); i++) {}
    for (int k = 0; k < words; k++) { const uint32_t v = read32(GP0); back[2 * k] = (uint16_t)v; back[2 * k + 1] = (uint16_t)(v >> 16); }
    int lost = 0, dup = 0, ng = 0, g[MAXG], n[MAXG], expect = 0;
    for (int k = 0; k < words; k++) {                           /* past the last index too: a tail stored twice counts as dup */
        const int got = back[2 * k];
        if (got == expect) { expect++; continue; }
        if (got > expect && got < words) {
            const int skip = got - expect;
            lost += skip;
            if (ng < MAXG) { g[ng] = k; n[ng] = skip; }
            ng++;
            expect = got + 1;
        } else if (got) dup++;
    }
    if (expect < words) lost += words - expect;
    printf("R %d %u %u %u %d %d %d |", c, (unsigned)d, (unsigned)t, (unsigned)left, lost, dup, ng);
    for (int i = 0; i < ng && i < MAXG; i++) printf(" %d:%d", g[i], n[i]);
    printf("\n");
}

int main() {
    printf("\ngpu/gpu-burstrow\n");
    printf("c: W words, x0, kind (pre N = the first N words by hand before the line edge; chop a b = 2^a words then 2^b cycles)\n");
    for (int c = 0; c < NCF; c++) {
        printf("C %d %d %d ", c, CF[c].w, CF[c].x0);
        if (CF[c].pre) printf("pre %d\n", CF[c].pre); else if (CF[c].a >= 0) printf("chop %d %d\n", CF[c].a, CF[c].b); else printf("burst\n");
    }
    printf("R c d t left lost dup ng | position:size ... (d = cycles after a line edge; a gap's position mod W = the row word). No assertions.\n\n");
    for (int i = 0; i < MAXW; i++) buf[i] = ((uint32_t)i << 16) | (uint32_t)i;
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int c = 0; c < NCF; c++) for (uint32_t d = 0; d < 2150; d += 32) one(c, d);
    gpuIdle();
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\ngpu/gpu-burstrow: Done.\n");
    for (;;) VSync(0);
}
