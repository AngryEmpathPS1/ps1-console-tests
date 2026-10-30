/*
 * gpu/gpu-shape-time76 - HOW EACH KIND MEETS THE LINE-START FETCH: a flat, a gouraud and a semi triangle and a flat
 * rectangle, 16x16, at gpu-shape-time74's exact starts 2 cycles apart across a line's end and the fetch, in 512x240 and
 * 320x240 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time74 / 75 on the SCPH-7501: the fetch with the line hold is one busy stretch that costs a probe paying
 * it whole 94-99 cycles (this core 100-107) and ends 30-40 ticks later than this core's. The kinds this core models with a
 * run-on (GPU_SCAN_POS's polygons, GPU_SCAN_RECT's rectangles) were fitted against the old placement: refitting the
 * fetch's geometry needs each kind's own answer at the same exact starts - a flat polygon held from its first word, a
 * shaded one running on, a semi one reading the frame buffer, a write-only rectangle.
 *
 * WHAT IT DOES. As gpu-shape-time74 (VRAM filled first; each probe ~199,000 cycles after VBlank's IRQ, then a fresh Timer
 * 0 reset and an exact delay to s = 1,500 + 2 i cycles, i = 0..299, the shape's last words stored back to back - a gouraud
 * triangle's first two written just before; D binary-searched for bit 26 within the blanking's D - 32 .. + 480), under
 * Unirom's own 512x240 and 320x240 15-bit: triangles (0,0)-(16,0)-(0,16) from (192,128) flat (20h), gouraud (30h) and
 * semi (22h), and a flat rectangle (60h) 16x16.
 *
 * READ IT LIKE THIS. Per mode and shape the blanking's D, then the 300 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time76 --ntsc --frames=40000 --quiet=40000` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time76 --emu --bios=<the model's>).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T0_COUNT 0x1F801100u
#define T0_MODE  0x1F801104u
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }

/* From a fresh Timer 0 reset (T0 >= 200, then < 64), delay to T0 = s exactly (3-cycle loop, its remainder in nops), store
   the words, delay D (n x 3 + k) and read GPUSTAT. */
#define SYNC_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n" \
        "5:\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "sltiu $t5, $t3, 200\n" \
        "bnez $t5, 5b\n" \
        "nop\n" \
        "6:\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "sltiu $t5, $t3, 64\n" \
        "beqz $t5, 6b\n" \
        "nop\n" \
        "lhu $t3, 0x1100($t0)\n" \
        "nop\n" \
        "subu $t4, %[s], $t3\n" \
        "7:\n" \
        "addiu $t4, $t4, -3\n" \
        "bgtz $t4, 7b\n" \
        "nop\n" \
        "la $t6, 8f\n" \
        "sll $t5, $t4, 2\n" \
        "addiu $t6, $t6, -8\n" \
        "subu $t6, $t6, $t5\n" \
        "jr $t6\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "8:\n"
#define PROBE_TAIL \
        "beqz %[n], 3f\n" \
        "nop\n" \
        "4:\n" \
        "addiu %[n], %[n], -1\n" \
        "bnez %[n], 4b\n" \
        "nop\n" \
        "3:\n" \
        "jr $t1\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "nop\n" \
        "2:\n" \
        "lw %[st], 0x1814($t0)\n" \
        "nop\n" \
        ".set reorder\n"
static uint32_t probe3(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
static uint32_t probe4(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}

struct Shape { const char* name; int n; uint32_t w[8]; };
static Shape S[8];
static int NS = 0;
static void copyS(const char* nm, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4;
    s.w[0] = 0x80000000u; s.w[1] = xy(512, 256); s.w[2] = xy(192, 128); s.w[3] = xy(w, h);
}
static void rect3(const char* nm, uint32_t c, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 3;
    s.w[0] = (c << 24) | 0x408020u; s.w[1] = xy(192, 128); s.w[2] = xy(w, h);
}
static void tri(const char* nm, uint32_t c) {
    Shape& s = S[NS++]; s.name = nm; s.n = 0;
    const bool g = (c & 0x10u) != 0;
    s.w[s.n++] = (c << 24) | 0x404040u; s.w[s.n++] = xy(192, 128);
    if (g) s.w[s.n++] = 0x204080u;
    s.w[s.n++] = xy(208, 128);
    if (g) s.w[s.n++] = 0x802040u;
    s.w[s.n++] = xy(192, 144);
}
static void defs() {
    tri("flat tri 16", 0x20u); tri("gour tri 16", 0x30u); tri("semi tri 16", 0x22u); rect3("rect 16x16", 0x60u, 16, 16);
}

static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    if (sh.n <= 4) return sh.n == 3 ? probe3(sh.w, s, n, k) : probe4(sh.w, s, n, k);
    for (int i = 0; i < sh.n - 4; i++) write32(GP0, sh.w[i]);   /* the words before the last four, just before */
    return probe4(sh.w + sh.n - 4, s, n, k);
}
static uint32_t g_off = 20000u;
static void sync() {
    waitReady();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t search(const Shape& sh, uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync();
        const uint32_t st = probe(sh, s, mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static const int NI = 300;
static uint16_t r26[2][8][NI];
static uint32_t base[2][8];
int main() {
    printf("\ngpu/gpu-shape-time76\n");
    printf("A shape's last words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read: the\n");
    printf("first D with bit 26 set, under two display modes. No assertions.\n\n");
    defs();
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | 8u | (1u << 4) | (2u << 7));
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02404040u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0x027F7F7Fu | 0x00808080u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));
    waitReady();
    static const char* const MN[2] = { "512x240 15-bit", "320x240 15-bit" };
    static const uint32_t MODE[2] = { 0, 0x01 };
    for (int m = 0; m < 2; m++) {
        waitReady();
        if (m == 1) {   /* mode 0 is Unirom's own, left as it is */
            write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
            write32(GP1, 0x05000000u);
            write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
            write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        }
        for (int i = 0; i < NS; i++) {
            waitReady(); sync(); probe(S[i], 64, 6000); waitReady(); sync(); probe(S[i], 64, 6000); waitReady();   /* the code warm */
            g_off = 20000u; base[m][i] = search(S[i], 64, 0, 16001);
            g_off = 199000u;
            const uint32_t lo = base[m][i] > 32 ? base[m][i] - 32 : 0, hi = base[m][i] + 480;
            for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 1500 + 2 * j, lo, hi + 1); r26[m][i][j] = (uint16_t)(d > hi ? 0xFFFF : d); }
        }
    }
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 1,500 + 2 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..299\n");
    for (int m = 0; m < 2; m++) {
        printf("%s:\n", MN[m]);
        for (int i = 0; i < NS; i++) {
            printf("%-12s %5u :", S[i].name, (unsigned)base[m][i]);
            for (int j = 0; j < NI; j++) printf(" %d", r26[m][i][j] == 0xFFFF ? 999 : (int)r26[m][i][j] - (int)base[m][i]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-shape-time76: Done.\n");
    for (;;) VSync(0);
}
