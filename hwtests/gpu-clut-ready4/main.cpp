/*
 * gpu/gpu-clut-ready4 - a CLUT reload with the display ON: does the scan-out make the reload dearer?
 *
 * WHY (2026-11-07). hwtests/gpu-clut-ready3 on the second SCPH-5501 (the vertical display range empty, every probe locked
 * ~300 cycles after Timer 1's tick): a reloading sprite fed by DrawPrim's own loop reads bit 26 at k 82 (4-bit) / 329
 * (8-bit) after its last word, the CLUT cached 66; experiments/_mkclut28.js reads 86 / 330 / 72. clut-cost's SERIAL rows,
 * the same DrawPrim and the same sprite with the display ON and nothing locked, read on every console ~22 cycles a sprite
 * for a 4-bit reload and ~260 for an 8-bit one - this model 9-10 and 251-256. The difference: the scan-out's fetches and
 * the line hold, which the probes kept out and this core's reload does not pay.
 *
 * WHAT IT DOES. gpu-clut-ready3's DrawPrim rows (each word read from the packet in RAM, then stored to GP0; a reload, or the
 * CLUT cached), at 4 and 8 bits: E as gpu-clut-ready3 (the range empty, ~300 cycles after Timer 1's tick), and D0..D7 with
 * the display ON (GP1(07h) lines 16-256, the 320-wide mode): the kept probe 40 lines after the VBlank's start and 302 + 268 j
 * cycles after that line's Timer 1 tick (j = 0..7, across the line). k = 2..80 by 1, 82..178 by 4, 180..420 by 1.
 *
 * READ IT LIKE THIS. As gpu-clut-ready: per row bit 28's and bit 26's first k from which each stays set. Against E, a D row
 * whose reload met the scan-out reads later; its cached twin shows what the drawing alone pays there.
 *
 * ON THIS CORE (v0.377.299, `node hwtests/run.js gpu-clut-ready4`) and the knob core (experiments/_mkclut28.js): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

static volatile uint32_t *const GPU0 = (volatile uint32_t *)0x1F801810;
static volatile uint32_t *const GPU1 = (volatile uint32_t *)0x1F801814;
static volatile uint32_t *const T1C = (volatile uint32_t *)0x1F801110;
static volatile uint32_t *const T1M = (volatile uint32_t *)0x1F801114;
static volatile uint32_t *const ISTAT = (volatile uint32_t *)0x1F801070;

static uint32_t packet[4] __attribute__((aligned(16)));

/* DrawPrim's loop: lw a word from the packet, sw it to GP0, four times; then the delay and GPUSTAT */
static uint32_t __attribute__((noinline)) pDP(const uint32_t* p, uint32_t m, int odd) {
    uint32_t st;
    if (odd) __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "lw $t2, 0(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 4(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 8(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 12(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "nop\n"
        "move $t1, %[m]\n" "9: bnez $t1, 9b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [p] "r"(p), [m] "r"(m) : "t0", "t1", "t2", "memory");
    else __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "lw $t2, 0(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 4(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 8(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "lw $t2, 12(%[p])\n" "nop\n" "sw $t2, 0x1810($t0)\n"
        "move $t1, %[m]\n" "9: bnez $t1, 9b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [p] "r"(p), [m] "r"(m) : "t0", "t1", "t2", "memory");
    return st;
}
/* the words from registers, back to back but for n RAM reads (ram 1) or n cached NOP pairs (ram 0) before the fourth */
static uint32_t __attribute__((noinline)) pG(const uint32_t* p, uint32_t n, int ram, uint32_t m, int odd) {
    uint32_t st;
    const uint32_t w0 = p[0], w1 = p[1], w2 = p[2], w3 = p[3];
    __asm__ volatile(
        ".set noreorder\n" "lui $t0, 0x1f80\n"
        "move $t3, %[n]\n"
        "sw %[w0], 0x1810($t0)\n" "sw %[w1], 0x1810($t0)\n" "sw %[w2], 0x1810($t0)\n"
        "beqz $t3, 3f\n" "nop\n"
        "beqz %[ram], 2f\n" "nop\n"
        "1: lw $t2, 0(%[p])\n" "addiu $t3, $t3, -1\n" "bnez $t3, 1b\n" "nop\n"
        "b 3f\n" "nop\n"
        "2: nop\n" "addiu $t3, $t3, -1\n" "bnez $t3, 2b\n" "nop\n"
        "3:\n"
        "sw %[w3], 0x1810($t0)\n"
        "beqz %[odd], 4f\n" "nop\n" "nop\n" "4:\n"
        "move $t1, %[m]\n" "9: bnez $t1, 9b\n" "addiu $t1, $t1, -1\n"
        "lw %[st], 0x1814($t0)\n" "nop\n" ".set reorder\n"
        : [st] "=r"(st) : [p] "r"(p), [n] "r"(n), [ram] "r"(ram), [w0] "r"(w0), [w1] "r"(w1), [w2] "r"(w2), [w3] "r"(w3), [m] "r"(m), [odd] "r"(odd)
        : "t0", "t1", "t2", "t3", "memory");
    return st;
}
static void __attribute__((noinline)) delay(uint32_t n) {
    __asm__ volatile(".set noreorder\n" "1: bnez %0, 1b\n" "addiu %0, %0, -1\n" ".set reorder\n" : "+r"(n));
}
static void idle() { for (int i = 0; i < 200000 && (*GPU1 & 0x14000000) != 0x14000000; i++) { } delay(200); }
static void waitVblank() { *ISTAT = ~1u; for (int i = 0; i < 2000000 && !(*ISTAT & 1u); i++) { } }
static void waitLines(int n) { for (int l = 0; l < n; l++) { const uint32_t t = *T1C & 0xFFFF; for (int i = 0; i < 100000 && (*T1C & 0xFFFF) == t; i++) { } } }
static void lockLine() { const uint32_t t = *T1C & 0xFFFF; for (int i = 0; i < 100000 && (*T1C & 0xFFFF) == t; i++) { } }

#define NK 345
static int KS[NK];
struct Row { char name[16]; bool d8; bool cached; int disp; };   /* disp -1: the range empty (E), else D j */
static Row ROWS[36];
static int NR = 0;
static uint32_t v[36][NK];

static const uint32_t CLUT_A = 480u << 6, CLUT_B = 481u << 6;
static void sprite(uint32_t* w, uint32_t clut) { w[0] = 0x64808080u; w[1] = (300u << 16) | 0u; w[2] = clut << 16; w[3] = (8u << 16) | 8u; }
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
    printf("\ngpu/gpu-clut-ready4\n");
    printf("A CLUT reload with the display on: DrawPrim's loop, k cycles after the last word, one GPUSTAT read. No assertions.\n\n");
    for (int d8 = 0; d8 < 2; d8++) for (int c = 0; c < 2; c++) for (int j = -1; j < 8; j++) {
        Row& R = ROWS[NR++]; R.d8 = d8; R.cached = c; R.disp = j;
        if (j < 0) sprintf(R.name, "s%d DP%s E", d8 ? 8 : 4, c ? "c" : ""); else sprintf(R.name, "s%d DP%s D%d", d8 ? 8 : 4, c ? "c" : "", j);
    }
    int n = 0;
    for (int k = 2; k <= 80; k++) KS[n++] = k;
    for (int k = 82; k <= 178; k += 4) KS[n++] = k;
    for (int k = 180; k <= 420; k++) KS[n++] = k;
    if (n != NK) { printf("grid %d != %d\n", n, NK); for (;;) { } }

    EnterCriticalSection();
    *GPU1 = 0x04000000;
    *GPU0 = 0xE3000000; *GPU0 = 0xE4000000 | (511u << 10) | 1023u; *GPU0 = 0xE5000000;
    *GPU0 = 0xE6000000;
    upload(512, 0, 16, 8, texv);
    upload(0, 480, 256, 1, clutAv);
    upload(0, 481, 256, 1, clutBv);
    *T1M = 0x0100;

    for (int r = 0; r < NR; r++) {
        const Row& R = ROWS[r];
        *GPU0 = 0xE1000000u | (R.d8 ? 0x0088u : 0x0008u);
        *GPU1 = R.disp < 0 ? 0x07000000u | (16u << 10) | 16u : 0x07000000u | (256u << 10) | 16u;   /* the range empty, or lines 16-256 */
        uint32_t pw[4]; sprite(pw, R.cached ? CLUT_A : CLUT_B);
        sprite(packet, CLUT_A);
        for (int k = 0; k < NK; k++) {
            const uint32_t m = (uint32_t)(KS[k] - 2) >> 1; const int odd = (KS[k] - 2) & 1;
            uint32_t st = 0;
            for (int rep = 0; rep < 3; rep++) {
                idle();
                for (int i = 0; i < 4; i++) *GPU0 = pw[i];
                idle();
                if (R.disp < 0 || rep < 2) { lockLine(); delay(150); }
                else { waitVblank(); waitLines(40); lockLine(); delay(150 + 134 * (uint32_t)R.disp); }
                st = pDP(packet, m, odd);
            }
            v[r][k] = st;
        }
    }
    idle();
    initVideo(320, 240);
    ExitCriticalSection();
    for (int r = 0; r < NR; r++) report(ROWS[r].name, v[r]);
    printf("\ngpu/gpu-clut-ready4: Done.\n");
    for (;;) { }
}
