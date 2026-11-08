/*
 * gpu/gpu-clut-ready2 - WHEN a CLUT reload starts: at the word that names the CLUT, or once the command is whole.
 *
 * WHY (2026-11-07). hwtests/gpu-clut-ready on the second SCPH-5501 (three runs identical): a reload shifts a sprite's whole
 * command - its GPUSTAT.28 release (k 12 -> 36 at 4 bits, 283 at 8) and bit 26 (74 -> 98 / 343) - and holds a textured
 * triangle's bit 28 to ~12 cycles before its set-up would have ended plus the reload (122 -> 134 / 378). Its words go to GP0
 * back to back. clut-cost's DrawPrim stores them 11 cycles apart, and its pipelined rows put the hold's end ~5-10 cycles
 * EARLIER against the last word than gpu-clut-ready does - as if the reload started at the CLUT's word (the sprite's third),
 * not at the fourth.
 *
 * WHAT IT DOES. gpu-clut-ready's probe (interrupts off, DMA off, the vertical display range empty, each probe ~300 cycles
 * after Timer 1's tick, the same command drawn first with the other CLUT so the probe reloads, three times and the last kept;
 * one GPUSTAT read k cycles after the LAST word), with a cached delay loop of G cycles (G = 2(g + 1)) at one place among the
 * words. k = 2..80 by 1, 82..178 by 4, 180..420 by 1, 424..520 by 8. Rows (opaque, modulated 808080h, texels and CLUTs
 * nonzero; a sprite 8x8 GP0(64h): word 3 its CLUT, word 4 its size; a triangle GP0(24h) legs of 8: word 3 its CLUT,
 * word 5 its texpage, word 7 its last):
 *   s4 B g   4-bit sprite, the gap before word 4 (after the CLUT's), g = 0 / 4 / 8 / 16
 *   s8 B g   8-bit sprite, the same, g = 0 / 4 / 8 / 16 / 32
 *   s8 A 16  8-bit sprite, the gap before word 3 (the CLUT's) instead - a control: nothing should move against word 4
 *   t4 / t8 B 16   a triangle, the gap before its last word
 *   t4 / t8 C 16   a triangle, the gap after its texpage's word (word 5)
 *   t8 A 16        a triangle, the gap before its CLUT's word
 *
 * READ IT LIKE THIS. As gpu-clut-ready: bit 28's and bit 26's value at the first k and at every k where it changes, then the
 * first k from which each stays set. If the reload starts at the CLUT's word, a sprite's edges move ~G earlier with its gap B.
 *
 * ON THIS CORE (v0.377.299, `node hwtests/run.js gpu-clut-ready2`) and the knob core (experiments/_mkclut28.js): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;
static volatile uint32_t *const T1C = (volatile uint32_t *)0x1F801110;
static volatile uint32_t *const T1M = (volatile uint32_t *)0x1F801114;

/* gA / gB / gC: a gap's loop count at its place ('A' before the CLUT's word, 'B' before the last, 'C' after the texpage's
 * word; 0 = no gap there); n: the delay loop before the read; odd: one cycle more. A gap is a cached loop of 2(g + 1)
 * cycles behind a taken beqz-not, and a place with no gap runs no loop at all. */
#define GAP(L, D) "move $t1, %[g" #L "]\n" #D ": bnez $t1, " #D "b\n" "addiu $t1, $t1, -1\n"
static uint32_t __attribute__((noinline)) p4(const uint32_t* c, uint32_t gA, uint32_t gB, uint32_t n, int odd) {
    uint32_t st;
    if (odd) __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "sw %[w0], 0x1810($t0)\n" "sw %[w1], 0x1810($t0)\n"
        "beqz %[gA], 2f\n" "nop\n" GAP(A, 6) "2:\n"
        "sw %[w2], 0x1810($t0)\n"
        "beqz %[gB], 3f\n" "nop\n" GAP(B, 7) "3:\n"
        "sw %[w3], 0x1810($t0)\n"
        "nop\n"
        "move $t1, %[n]\n" "4: bnez $t1, 4b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [w0] "r"(c[0]), [w1] "r"(c[1]), [w2] "r"(c[2]), [w3] "r"(c[3]), [gA] "r"(gA), [gB] "r"(gB), [n] "r"(n) : "t0", "t1", "memory");
    else __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "sw %[w0], 0x1810($t0)\n" "sw %[w1], 0x1810($t0)\n"
        "beqz %[gA], 2f\n" "nop\n" GAP(A, 6) "2:\n"
        "sw %[w2], 0x1810($t0)\n"
        "beqz %[gB], 3f\n" "nop\n" GAP(B, 7) "3:\n"
        "sw %[w3], 0x1810($t0)\n"
        "move $t1, %[n]\n" "4: bnez $t1, 4b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [w0] "r"(c[0]), [w1] "r"(c[1]), [w2] "r"(c[2]), [w3] "r"(c[3]), [gA] "r"(gA), [gB] "r"(gB), [n] "r"(n) : "t0", "t1", "memory");
    return st;
}
static uint32_t __attribute__((noinline)) p7(const uint32_t* c, uint32_t gA, uint32_t gB, uint32_t gC, uint32_t n, int odd) {
    uint32_t st;
    if (odd) __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "lw $t2, 0(%[c])\n" "lw $t3, 4(%[c])\n" "lw $t4, 8(%[c])\n" "lw $t5, 12(%[c])\n" "lw $t6, 16(%[c])\n" "lw $t7, 20(%[c])\n" "lw $t8, 24(%[c])\n"
        "nop\n"
        "sw $t2, 0x1810($t0)\n" "sw $t3, 0x1810($t0)\n"
        "beqz %[gA], 2f\n" "nop\n" GAP(A, 6) "2:\n"
        "sw $t4, 0x1810($t0)\n" "sw $t5, 0x1810($t0)\n" "sw $t6, 0x1810($t0)\n"
        "beqz %[gC], 5f\n" "nop\n" GAP(C, 8) "5:\n"
        "sw $t7, 0x1810($t0)\n"
        "beqz %[gB], 3f\n" "nop\n" GAP(B, 7) "3:\n"
        "sw $t8, 0x1810($t0)\n"
        "nop\n"
        "move $t1, %[n]\n" "4: bnez $t1, 4b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [c] "r"(c), [gA] "r"(gA), [gB] "r"(gB), [gC] "r"(gC), [n] "r"(n) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    else __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "lw $t2, 0(%[c])\n" "lw $t3, 4(%[c])\n" "lw $t4, 8(%[c])\n" "lw $t5, 12(%[c])\n" "lw $t6, 16(%[c])\n" "lw $t7, 20(%[c])\n" "lw $t8, 24(%[c])\n"
        "nop\n"
        "sw $t2, 0x1810($t0)\n" "sw $t3, 0x1810($t0)\n"
        "beqz %[gA], 2f\n" "nop\n" GAP(A, 6) "2:\n"
        "sw $t4, 0x1810($t0)\n" "sw $t5, 0x1810($t0)\n" "sw $t6, 0x1810($t0)\n"
        "beqz %[gC], 5f\n" "nop\n" GAP(C, 8) "5:\n"
        "sw $t7, 0x1810($t0)\n"
        "beqz %[gB], 3f\n" "nop\n" GAP(B, 7) "3:\n"
        "sw $t8, 0x1810($t0)\n"
        "move $t1, %[n]\n" "4: bnez $t1, 4b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [c] "r"(c), [gA] "r"(gA), [gB] "r"(gB), [gC] "r"(gC), [n] "r"(n) : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "memory");
    return st;
}
static void __attribute__((noinline)) delay(uint32_t n) {
    __asm__ volatile(".set noreorder\n" "1: bnez %0, 1b\n" "addiu %0, %0, -1\n" ".set reorder\n" : "+r"(n));
}

static void idle() { for (int i = 0; i < 200000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } delay(200); }
static void lockLine() { const uint32_t t = *T1C & 0xFFFF; for (int i = 0; i < 100000 && (*T1C & 0xFFFF) == t; i++) { } }

#define NK 358
static int KS[NK];
struct Row { const char* name; bool tri, d8; char at; uint32_t g; };
static const Row ROWS[] = {
    { "s4 B 0", false, false, 'B', 0 }, { "s4 B 4", false, false, 'B', 4 }, { "s4 B 8", false, false, 'B', 8 }, { "s4 B 16", false, false, 'B', 16 },
    { "s8 B 0", false, true, 'B', 0 }, { "s8 B 4", false, true, 'B', 4 }, { "s8 B 8", false, true, 'B', 8 }, { "s8 B 16", false, true, 'B', 16 },
    { "s8 B 32", false, true, 'B', 32 }, { "s8 A 16", false, true, 'A', 16 },
    { "t4 B 16", true, false, 'B', 16 }, { "t8 B 16", true, true, 'B', 16 }, { "t4 C 16", true, false, 'C', 16 }, { "t8 C 16", true, true, 'C', 16 },
    { "t8 A 16", true, true, 'A', 16 },
};
#define NR (int)(sizeof(ROWS) / sizeof(ROWS[0]))
static uint32_t v[NR][NK];

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
    printf("\ngpu/gpu-clut-ready2\n");
    printf("When a CLUT reload starts: a gap among the words, k cycles after the last, one GPUSTAT read. No assertions.\n\n");
    int n = 0;
    for (int k = 2; k <= 80; k++) KS[n++] = k;
    for (int k = 82; k <= 178; k += 4) KS[n++] = k;
    for (int k = 180; k <= 420; k++) KS[n++] = k;
    for (int k = 424; k <= 520; k += 8) KS[n++] = k;
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

    for (int r = 0; r < NR; r++) {
        const Row& R = ROWS[r];
        const uint32_t tp = R.d8 ? TP8 : TP4;
        const uint32_t gA = R.at == 'A' ? R.g : 0, gB = R.at == 'B' ? R.g : 0, gC = R.at == 'C' ? R.g : 0;
        uint32_t pw[7], cw[7];
        if (R.tri) { tri(pw, CLUT_B, tp); tri(cw, CLUT_A, tp); }
        else { *GPU0 = 0xE1000000u | tp; sprite(pw, CLUT_B); sprite(cw, CLUT_A); }
        const int words = R.tri ? 7 : 4;
        for (int k = 0; k < NK; k++) {
            const uint32_t m = (uint32_t)(KS[k] - 2) >> 1; const int odd = (KS[k] - 2) & 1;
            uint32_t st = 0;
            for (int rep = 0; rep < 3; rep++) {
                idle();
                for (int i = 0; i < words; i++) *GPU0 = pw[i];    /* the prime: the other CLUT */
                idle();
                lockLine(); delay(150);
                st = R.tri ? p7(cw, gA, gB, gC, m, odd) : p4(cw, gA, gB, m, odd);
            }
            v[r][k] = st;
        }
    }
    idle();
    initVideo(320, 240);
    ExitCriticalSection();
    for (int r = 0; r < NR; r++) report(ROWS[r].name, v[r]);
    printf("\ngpu/gpu-clut-ready2: Done.\n");
    for (;;) { }
}
