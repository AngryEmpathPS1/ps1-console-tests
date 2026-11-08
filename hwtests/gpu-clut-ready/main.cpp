/*
 * gpu/gpu-clut-ready - what a CLUT cache reload does to GPUSTAT's bits 28 and 26, to the cycle.
 *
 * WHY (2026-11-07). Every console's hwtests/clut-cost (eight consoles, ~50 runs, both GPUs) reads its pipelined 8-bit run -
 * 128 sprites through PSn00bSDK's DrawPrim, whose closing DrawSync waits on GPUSTAT.28 alone - at 251-253 cycles a sprite
 * more when every sprite reloads the CLUT; this core reads 128. Its SERIAL run (bit 26 waited for) agrees with the consoles
 * (259-262). And gpu-cost-sweep's first 8-bit textured rectangle (a 256-entry reload) reads bit 28 at 388-403 cycles on
 * every console, 135 here; its first 4-bit one 135-162 on both. So a reload holds bit 28 clear, and this core does not.
 * clut-cost's DrawSync and cost-sweep's poll are too coarse to say where the hold ends against the reload, by depth, and
 * neither draws a textured POLYGON that reloads.
 *
 * WHAT IT DOES. Interrupts off, DMA off, the vertical display range EMPTY (no line is shown, so the scan-out fetches
 * nothing), each probe ~300 cycles after Timer 1's hblank tick (clear of the new GPU's line-start hold). A probe: one
 * command written to GP0 from the CPU, a delay of k cycles (a cached loop), one GPUSTAT read. Before each probe the same
 * command is drawn with the OTHER CLUT (rows "reload": the probe's CLUT is not in the cache) or the SAME one (rows "cached")
 * and the GPU waited for; each probe three times and the last kept (a warm instruction cache). k = 2..65 by 1, 66..238 by
 * 4, 240..420 by 1, 424..616 by 8. Rows (all opaque, modulated 808080h, texels nonzero, both CLUTs nonzero):
 *   s4c / s4r   GP0(64h) sprite 8x8, 4-bit, CLUT cached / reloaded (16 entries)
 *   s8c / s8r   the same, 8-bit (256 entries)
 *   t4c / t4r   GP0(24h) textured triangle, legs of 8, 4-bit
 *   t8c / t8r   the same, 8-bit
 *
 * READ IT LIKE THIS. Per row, bit 28's and bit 26's value at the first k and every k where it changes ("1@37" = set from
 * k 37 on, up to the next change), then the first k from which each stays set.
 *
 * ON THIS CORE (v0.377.299, `node hwtests/run.js gpu-clut-ready`) and the knob core (experiments/_mkclut28.js): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;
static volatile uint32_t *const T1C = (volatile uint32_t *)0x1F801110;
static volatile uint32_t *const T1M = (volatile uint32_t *)0x1F801114;

/* The probes: the words, then 2(n + 1) cycles of a cached loop (+1 for the odd ones), then GPUSTAT. */
static uint32_t __attribute__((noinline)) p4e(const uint32_t* c, uint32_t n) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "move $t1, %5\n"
        "sw %1, 0x1810($t0)\n"
        "sw %2, 0x1810($t0)\n"
        "sw %3, 0x1810($t0)\n"
        "sw %4, 0x1810($t0)\n"
        "1: bnez $t1, 1b\n"
        "addiu $t1, $t1, -1\n"
        "lw %0, 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : "=r"(st) : "r"(c[0]), "r"(c[1]), "r"(c[2]), "r"(c[3]), "r"(n) : "t0", "t1", "memory");
    return st;
}
static uint32_t __attribute__((noinline)) p4o(const uint32_t* c, uint32_t n) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "move $t1, %5\n"
        "sw %1, 0x1810($t0)\n"
        "sw %2, 0x1810($t0)\n"
        "sw %3, 0x1810($t0)\n"
        "sw %4, 0x1810($t0)\n"
        "nop\n"
        "1: bnez $t1, 1b\n"
        "addiu $t1, $t1, -1\n"
        "lw %0, 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : "=r"(st) : "r"(c[0]), "r"(c[1]), "r"(c[2]), "r"(c[3]), "r"(n) : "t0", "t1", "memory");
    return st;
}
static uint32_t __attribute__((noinline)) p7e(const uint32_t* c, uint32_t n) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "move $t1, %8\n"
        "sw %1, 0x1810($t0)\n"
        "sw %2, 0x1810($t0)\n"
        "sw %3, 0x1810($t0)\n"
        "sw %4, 0x1810($t0)\n"
        "sw %5, 0x1810($t0)\n"
        "sw %6, 0x1810($t0)\n"
        "sw %7, 0x1810($t0)\n"
        "1: bnez $t1, 1b\n"
        "addiu $t1, $t1, -1\n"
        "lw %0, 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : "=r"(st) : "r"(c[0]), "r"(c[1]), "r"(c[2]), "r"(c[3]), "r"(c[4]), "r"(c[5]), "r"(c[6]), "r"(n) : "t0", "t1", "memory");
    return st;
}
static uint32_t __attribute__((noinline)) p7o(const uint32_t* c, uint32_t n) {
    uint32_t st;
    __asm__ volatile(
        ".set noreorder\n"
        "lui $t0, 0x1f80\n"
        "move $t1, %8\n"
        "sw %1, 0x1810($t0)\n"
        "sw %2, 0x1810($t0)\n"
        "sw %3, 0x1810($t0)\n"
        "sw %4, 0x1810($t0)\n"
        "sw %5, 0x1810($t0)\n"
        "sw %6, 0x1810($t0)\n"
        "sw %7, 0x1810($t0)\n"
        "nop\n"
        "1: bnez $t1, 1b\n"
        "addiu $t1, $t1, -1\n"
        "lw %0, 0x1814($t0)\n"
        "nop\n"
        ".set reorder\n"
        : "=r"(st) : "r"(c[0]), "r"(c[1]), "r"(c[2]), "r"(c[3]), "r"(c[4]), "r"(c[5]), "r"(c[6]), "r"(n) : "t0", "t1", "memory");
    return st;
}
static void __attribute__((noinline)) delay(uint32_t n) {
    __asm__ volatile(".set noreorder\n" "1: bnez %0, 1b\n" "addiu %0, %0, -1\n" ".set reorder\n" : "+r"(n));
}

static void idle() { for (int i = 0; i < 200000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } delay(200); }
static void lockLine() { const uint32_t t = *T1C & 0xFFFF; for (int i = 0; i < 100000 && (*T1C & 0xFFFF) == t; i++) { } }

#define NK 314
static int KS[NK];
static uint32_t v[8][NK];

static const uint32_t CLUT_A = 480u << 6, CLUT_B = 481u << 6;    /* (0,480), (0,481) */
static const uint32_t TP4 = 0x0008u, TP8 = 0x0088u;               /* texpage (512,0), 4- / 8-bit */

static void sprite(uint32_t* w, uint32_t clut) { w[0] = 0x64808080u; w[1] = (300u << 16) | 0u; w[2] = clut << 16; w[3] = (8u << 16) | 8u; }
static void tri(uint32_t* w, uint32_t clut, uint32_t tp) {
    w[0] = 0x24808080u; w[1] = (300u << 16) | 32u; w[2] = clut << 16;
    w[3] = (300u << 16) | 40u; w[4] = (tp << 16) | 8u; w[5] = (308u << 16) | 32u; w[6] = (8u << 8) | 0u;
}

static void upload(int x, int y, int wHalf, int h, uint16_t (*val)(int i)) {
    idle();
    *GPU0 = 0xA0000000u; *GPU0 = ((uint32_t)y << 16) | (uint32_t)x; *GPU0 = ((uint32_t)h << 16) | (uint32_t)wHalf;
    for (int i = 0; i < wHalf * h; i += 2) *GPU0 = (uint32_t)val(i) | ((uint32_t)val(i + 1) << 16);
    idle();
}
static uint16_t texv(int) { return 0x5A3C; }
static uint16_t clutAv(int i) { return (uint16_t)(0x0400 | (i & 0x1F)); }
static uint16_t clutBv(int i) { return (uint16_t)(((i & 0x1F) << 5) | 1); }

static void report(const char* name, const uint32_t* row) {
    printf("%s:\n", name);
    for (int b = 0; b < 2; b++) {
        const int bit = b ? 26 : 28;
        printf("  %d:", bit);
        int last = -1, from = -1;
        for (int k = 0; k < NK; k++) {
            const int x = (int)((row[k] >> bit) & 1);
            if (x != last) { printf(" %d@%d", x, KS[k]); last = x; from = x ? KS[k] : -1; }
        }
        printf("   (set from k=%d)\n", from);
    }
}

int main() {
    initVideo(320, 240);
    printf("\ngpu/gpu-clut-ready\n");
    printf("A CLUT reload against GPUSTAT 28 and 26: one command from the CPU, k cycles, one GPUSTAT read. No assertions.\n\n");
    int n = 0;
    for (int k = 2; k <= 65; k++) KS[n++] = k;
    for (int k = 66; k <= 238; k += 4) KS[n++] = k;
    for (int k = 240; k <= 420; k++) KS[n++] = k;
    for (int k = 424; k <= 616; k += 8) KS[n++] = k;
    if (n != NK) { printf("grid %d != %d\n", n, NK); for (;;) { } }

    EnterCriticalSection();
    *GPU1 = 0x04000000;                                   /* DMA off */
    *GPU0 = 0xE3000000; *GPU0 = 0xE4000000 | (511u << 10) | 1023u; *GPU0 = 0xE5000000;   /* drawing area: all of VRAM */
    *GPU0 = 0xE6000000;                                   /* no mask */
    upload(512, 0, 16, 8, texv);
    upload(0, 480, 256, 1, clutAv);
    upload(0, 481, 256, 1, clutBv);
    *GPU1 = 0x07000000 | (16u << 10) | 16u;               /* vertical display range empty: no line fetched */
    *T1M = 0x0100;                                        /* Timer 1 counts hblanks */

    static const char* N[8] = {
        "s4c 4-bit sprite 8x8, CLUT cached", "s4r 4-bit sprite 8x8, CLUT reloaded",
        "s8c 8-bit sprite 8x8, CLUT cached", "s8r 8-bit sprite 8x8, CLUT reloaded",
        "t4c 4-bit triangle legs 8, CLUT cached", "t4r 4-bit triangle legs 8, CLUT reloaded",
        "t8c 8-bit triangle legs 8, CLUT cached", "t8r 8-bit triangle legs 8, CLUT reloaded" };
    for (int r = 0; r < 8; r++) {
        const bool isTri = r >= 4, d8 = (r & 2) != 0, reload = (r & 1) != 0;
        const uint32_t tp = d8 ? TP8 : TP4;
        uint32_t pw[7], cw[7];
        if (isTri) { tri(pw, reload ? CLUT_B : CLUT_A, tp); tri(cw, CLUT_A, tp); }
        else { *GPU0 = 0xE1000000u | tp; sprite(pw, reload ? CLUT_B : CLUT_A); sprite(cw, CLUT_A); }
        const int words = isTri ? 7 : 4;
        for (int k = 0; k < NK; k++) {
            const uint32_t m = (uint32_t)(KS[k] - 2) >> 1; const bool odd = ((KS[k] - 2) & 1) != 0;
            uint32_t st = 0;
            for (int rep = 0; rep < 3; rep++) {
                idle();
                for (int i = 0; i < words; i++) *GPU0 = pw[i];    /* the prime: the other CLUT, or the same */
                idle();
                lockLine(); delay(150);
                st = isTri ? (odd ? p7o(cw, m) : p7e(cw, m)) : (odd ? p4o(cw, m) : p4e(cw, m));
            }
            v[r][k] = st;
        }
    }
    idle();
    initVideo(320, 240);
    ExitCriticalSection();
    for (int r = 0; r < 8; r++) report(N[r], v[r]);
    printf("\ngpu/gpu-clut-ready: Done.\n");
    for (;;) { }
}
