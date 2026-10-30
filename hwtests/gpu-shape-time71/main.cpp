/*
 * gpu/gpu-shape-time71 - DOES WHERE A COPY READS AND WRITES DECIDE WHAT THE SCAN-OUT COSTS IT? One copy, 256x32, with its
 * source and destination in six different places of VRAM, at gpu-shape-time69's exact starts, in 320x240 and 512x240
 * 15-bit (2026-10-26).
 *
 * WHY. gpu-shape-time70 on the SCPH-7501: a long copy (256x32 in 320x240) pays the scan-out in a clean sawtooth - its toll
 * falls one cycle a cycle of later start for one 128-pixel block's time (~258 ticks), then jumps back: it ENDS at the same
 * moment whatever its start, its blocks locked to the refills' grid (1,024 ticks, ~4 blocks). gpu-shape-time69's one-burst
 * kernels, applied burst by burst, do not lock (scratch/copysim.js). One mechanism that would: VRAM's banks - the copy's
 * reads and writes in different banks, a refill (the displayed area's rows) blocking only one stream - and gpu-shape-time68c
 * found the runs' spread depends on VRAM in some way. If the place matters, the toll changes with it; if not, banks are
 * not the lock.
 *
 * WHAT IT DOES. As gpu-shape-time70 (VRAM filled first; 540 starts 4 cycles apart from Timer 0's reset ~93 lines after
 * VBlank; D binary-searched within the blanking's D - 32 .. + 1,024), the copy (80h) 256x32 from / to: A (512,256) ->
 * (192,128) - gpu-shape-time70's; B (512,256) -> (704,288) - both off screen, right half; C (0,288) -> (192,128) - the
 * source left, below the display; D (512,0) -> (768,64) - both in the top right, off screen; E (192,128) -> (512,288) - the
 * source ON the displayed area; F (0,0) -> (0,288) - the source the displayed rows' first pixels.
 *
 * READ IT LIKE THIS. Per mode and placement the blanking's D, then the 540 starts' D, each minus the blanking's (999 = past
 * the window). No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time71 --ntsc --frames=90000 --quiet=90000` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time71 --emu --bios=<the model's>).
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

struct Shape { const char* name; int n; uint32_t w[4]; };
static Shape S[8];
static int NS = 0;
static void copyP(const char* nm, int sx, int sy, int dx, int dy, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4;
    s.w[0] = 0x80000000u; s.w[1] = xy(sx, sy); s.w[2] = xy(dx, dy); s.w[3] = xy(w, h);
}
static void defs() {
    copyP("copy A", 512, 256, 192, 128, 256, 32); copyP("copy B", 512, 256, 704, 288, 256, 32);
    copyP("copy C", 0, 288, 192, 128, 256, 32); copyP("copy D", 512, 0, 768, 64, 256, 32);
    copyP("copy E", 192, 128, 512, 288, 256, 32); copyP("copy F", 0, 0, 0, 288, 256, 32);
}

static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return sh.n == 3 ? probe3(sh.w, s, n, k) : probe4(sh.w, s, n, k);
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

static const int NI = 540;
static uint16_t r26[2][8][NI];
static uint32_t base[2][8];
int main() {
    printf("\ngpu/gpu-shape-time71\n");
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
            const uint32_t lo = base[m][i] > 32 ? base[m][i] - 32 : 0, hi = base[m][i] + 1024;
            for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 4 * j, lo, hi + 1); r26[m][i][j] = (uint16_t)(d > hi ? 0xFFFF : d); }
        }
    }
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 64 + 4 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..539\n");
    for (int m = 0; m < 2; m++) {
        printf("%s:\n", MN[m]);
        for (int i = 0; i < NS; i++) {
            printf("%-12s %5u :", S[i].name, (unsigned)base[m][i]);
            for (int j = 0; j < NI; j++) printf(" %d", r26[m][i][j] == 0xFFFF ? 999 : (int)r26[m][i][j] - (int)base[m][i]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-shape-time71: Done.\n");
    for (;;) VSync(0);
}
