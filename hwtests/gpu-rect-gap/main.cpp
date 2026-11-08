/*
 * gpu/gpu-rect-gap - a rectangle's words spaced out: does its drawing start sooner against its last word?
 *
 * WHY (2026-11-07). hwtests/gpu-clut-ready2 on the second SCPH-5501: an 8x8 4-bit sprite whose words come with a 37-cycle gap
 * before its fourth reads bit 28 at k 6 and bit 26 at 66 after that word, this core 2 and 72; with no gap the console reads
 * 12 / 74 as this core does (gpu-clut-ready). clut-cost's SERIAL rows (DrawPrim stores a sprite's words ~11 cycles apart)
 * read ~6 cycles a sprite under this core on every console. So on the console a rectangle's set-up runs on while its words
 * arrive - by how much, for which words, which kinds?
 *
 * WHAT IT DOES. gpu-clut-ready's probe (interrupts off, DMA off, the vertical display range empty, each probe ~300 cycles
 * after Timer 1's tick, three times and the last kept; one GPUSTAT read k cycles after the LAST word, k 2..140), with a cached
 * loop of 2g + 5 cycles before ONE of the words (g 0: no gap). Rows: a 4-bit 8x8 GP0(64h) sprite, its texels and CLUT
 * cached (drawn just before), the gap before word 2, 3 or 4; a flat 8x8 GP0(60h) rectangle (3 words) and an 8x8 GP0(02h) fill
 * (3 words), the gap before word 2 or 3; g = 0, 4, 8, 16, 32.
 *
 * READ IT LIKE THIS. As gpu-clut-ready: bit 28's and bit 26's value at the first k and at every k where it changes, then the
 * first k from which each stays set. A row whose edges come sooner as its gap grows starts drawing before its last word.
 *
 * ON THIS CORE (v0.377.299, `node hwtests/run.js gpu-rect-gap`): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;
static volatile uint32_t *const T1C = (volatile uint32_t *)0x1F801110;
static volatile uint32_t *const T1M = (volatile uint32_t *)0x1F801114;

#define GAP(L, D) "move $t1, %[g" #L "]\n" #D ": bnez $t1, " #D "b\n" "addiu $t1, $t1, -1\n"
/* words w0..w3 (n 3 or 4); gaps before word 2 (g2), 3 (g3), 4 (g4): 0 = none; then 2(m + 1) (+1 odd) cycles, GPUSTAT */
static uint32_t __attribute__((noinline)) probe(const uint32_t* c, int n, uint32_t g2, uint32_t g3, uint32_t g4, uint32_t m, int odd) {
    uint32_t st;
    const uint32_t w3 = n == 4 ? c[3] : 0, four = n == 4 ? 1 : 0;
    if (odd) __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "sw %[w0], 0x1810($t0)\n"
        "beqz %[g2], 2f\n" "nop\n" GAP(2, 6) "2:\n"
        "sw %[w1], 0x1810($t0)\n"
        "beqz %[g3], 3f\n" "nop\n" GAP(3, 7) "3:\n"
        "sw %[w2], 0x1810($t0)\n"
        "beqz %[four], 5f\n" "nop\n"
        "beqz %[g4], 4f\n" "nop\n" GAP(4, 8) "4:\n"
        "sw %[w3], 0x1810($t0)\n"
        "5:\n"
        "nop\n"
        "move $t1, %[m]\n" "9: bnez $t1, 9b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [w0] "r"(c[0]), [w1] "r"(c[1]), [w2] "r"(c[2]), [w3] "r"(w3), [four] "r"(four), [g2] "r"(g2), [g3] "r"(g3), [g4] "r"(g4), [m] "r"(m) : "t0", "t1", "memory");
    else __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "sw %[w0], 0x1810($t0)\n"
        "beqz %[g2], 2f\n" "nop\n" GAP(2, 6) "2:\n"
        "sw %[w1], 0x1810($t0)\n"
        "beqz %[g3], 3f\n" "nop\n" GAP(3, 7) "3:\n"
        "sw %[w2], 0x1810($t0)\n"
        "beqz %[four], 5f\n" "nop\n"
        "beqz %[g4], 4f\n" "nop\n" GAP(4, 8) "4:\n"
        "sw %[w3], 0x1810($t0)\n"
        "5:\n"
        "move $t1, %[m]\n" "9: bnez $t1, 9b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [w0] "r"(c[0]), [w1] "r"(c[1]), [w2] "r"(c[2]), [w3] "r"(w3), [four] "r"(four), [g2] "r"(g2), [g3] "r"(g3), [g4] "r"(g4), [m] "r"(m) : "t0", "t1", "memory");
    return st;
}
static void __attribute__((noinline)) delay(uint32_t n) {
    __asm__ volatile(".set noreorder\n" "1: bnez %0, 1b\n" "addiu %0, %0, -1\n" ".set reorder\n" : "+r"(n));
}
static void idle() { for (int i = 0; i < 200000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } delay(200); }
static void lockLine() { const uint32_t t = *T1C & 0xFFFF; for (int i = 0; i < 100000 && (*T1C & 0xFFFF) == t; i++) { } }

#define NK 139
static int KS[NK];
struct Row { char kind; int at; uint32_t g; };   /* kind s sprite, r flat rectangle, f fill; at: the word the gap is before */
static Row ROWS[40];
static int NR = 0;
static uint32_t v[40][NK];

static void upload(int x, int y, int wHalf, int h, uint16_t (*val)(int i)) {
    idle();
    *GPU0 = 0xA0000000u; *GPU0 = ((uint32_t)y << 16) | (uint32_t)x; *GPU0 = ((uint32_t)h << 16) | (uint32_t)wHalf;
    for (int i = 0; i < wHalf * h; i += 2) *GPU0 = (uint32_t)val(i) | ((uint32_t)val(i + 1) << 16);
    idle();
}
static uint16_t texv(int) { return 0x5A3C; }
static uint16_t clutAv(int i) { return (uint16_t)(0x0400 | (i & 0x1F)); }

static void report(const Row& R, const uint32_t* row) {
    printf("%c W%d %d:\n", R.kind, R.at, (int)R.g);
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
    printf("\ngpu/gpu-rect-gap\n");
    printf("A rectangle's words spaced out: a gap before one word, k cycles after the last, one GPUSTAT read. No assertions.\n\n");
    for (int k = 0; k < NK; k++) KS[k] = k + 2;
    static const uint32_t GS[5] = { 0, 4, 8, 16, 32 };
    for (int at = 2; at <= 4; at++) for (int i = 0; i < 5; i++) ROWS[NR++] = { 's', at, GS[i] };
    for (int at = 2; at <= 3; at++) for (int i = 0; i < 5; i++) ROWS[NR++] = { 'r', at, GS[i] };
    for (int at = 2; at <= 3; at++) for (int i = 0; i < 5; i++) ROWS[NR++] = { 'f', at, GS[i] };

    EnterCriticalSection();
    *GPU1 = 0x04000000;                                   /* DMA off */
    *GPU0 = 0xE3000000; *GPU0 = 0xE4000000 | (511u << 10) | 1023u; *GPU0 = 0xE5000000;   /* drawing area: all of VRAM */
    *GPU0 = 0xE6000000;                                   /* no mask */
    upload(512, 0, 16, 8, texv);
    upload(0, 480, 256, 1, clutAv);
    *GPU1 = 0x07000000 | (16u << 10) | 16u;               /* vertical display range empty: no line fetched */
    *T1M = 0x0100;                                        /* Timer 1 counts hblanks */
    *GPU0 = 0xE1000000u | 0x0008u;                        /* texpage (512,0), 4-bit */

    for (int r = 0; r < NR; r++) {
        const Row& R = ROWS[r];
        uint32_t w[4]; int n;
        if (R.kind == 's') { w[0] = 0x64808080u; w[1] = (300u << 16) | 0u; w[2] = (480u << 6) << 16; w[3] = (8u << 16) | 8u; n = 4; }
        else if (R.kind == 'r') { w[0] = 0x60204080u; w[1] = (300u << 16) | 16u; w[2] = (8u << 16) | 8u; n = 3; }
        else { w[0] = 0x02204080u; w[1] = (300u << 16) | 32u; w[2] = (8u << 16) | 16u; n = 3; }
        const uint32_t g2 = R.at == 2 ? R.g : 0, g3 = R.at == 3 ? R.g : 0, g4 = R.at == 4 ? R.g : 0;
        for (int k = 0; k < NK; k++) {
            const uint32_t m = (uint32_t)(KS[k] - 2) >> 1; const int odd = (KS[k] - 2) & 1;
            uint32_t st = 0;
            for (int rep = 0; rep < 3; rep++) {
                idle();
                for (int i = 0; i < n; i++) *GPU0 = w[i];     /* the same command first: its texels and CLUT cached */
                idle();
                lockLine(); delay(150);
                st = probe(w, n, g2, g3, g4, m, odd);
            }
            v[r][k] = st;
        }
    }
    idle();
    initVideo(320, 240);
    ExitCriticalSection();
    for (int r = 0; r < NR; r++) report(ROWS[r], v[r]);
    printf("\ngpu/gpu-rect-gap: Done.\n");
    for (;;) { }
}
