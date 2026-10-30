/*
 * gpu/gpu-shape-time73 - WHAT CHECKING THE MASK COSTS A COPY: VRAM copies of eleven sizes with GP0(E6h) bit 1 clear and set,
 * in the vertical blanking (2026-10-26).
 *
 * WHY. gpu-shape-time72 on the SCPH-7501: a mask-checked copy takes longer in the blanking than a plain one - 64x1 110
 * cycles against 95, 128x1 194 against 165 - where this core charges them alike (its copy cost, GPU_COPY3, has no mask
 * term; only the pixel writes check it). psx-spx gives the new GPU "1.00 clks per pixel, with Mask Check, when width>=16"
 * plus "25.50 clks per 16pix chunk" against 1.25 a pixel without, and its own term below 16 wide. The law by width and
 * rows is what this measures.
 *
 * WHAT IT DOES. As gpu-shape-time72's blanking D (each probe 20,000 cycles after VBlank's IRQ, then a fresh Timer 0 reset
 * and 64 cycles; D binary-searched for bit 26 over 0..16,000), VRAM copies (80h) from (512,256) to (192,128) of 8x1, 16x1,
 * 32x1, 64x1, 128x1, 256x1, 512x1, 16x16, 64x16, 128x8 and 256x8, each with GP0(E6h) 0 and then 2 (the mask checked;
 * VRAM's mask bits are 0, so every pixel is written).
 *
 * READ IT LIKE THIS. Per shape: plain D, masked D, and the difference. No assertions.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time73 --ntsc --frames=3000 --quiet=3000` (and, for its
 * PREDICTED.txt, the runner's way: node tools/hwrunner.js gpu-shape-time73 --emu --bios=<the model's>).
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

struct Shape { const char* name; int n; uint32_t w[4]; uint32_t e6; };
static Shape S[32];
static int NS = 0;
static void copyS(const char* nm, int w, int h, uint32_t e6 = 0) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4; s.e6 = e6;
    s.w[0] = 0x80000000u; s.w[1] = xy(512, 256); s.w[2] = xy(192, 128); s.w[3] = xy(w, h);
}
static void defs() {
    static const int W[11] = { 8, 16, 32, 64, 128, 256, 512, 16, 64, 128, 256 }, H[11] = { 1, 1, 1, 1, 1, 1, 1, 16, 16, 8, 8 };
    static const char* const NM[11] = { "copy 8x1", "copy 16x1", "copy 32x1", "copy 64x1", "copy 128x1", "copy 256x1", "copy 512x1",
        "copy 16x16", "copy 64x16", "copy 128x8", "copy 256x8" };
    for (int i = 0; i < 11; i++) { copyS(NM[i], W[i], H[i], 0); copyS(NM[i], W[i], H[i], 2); }
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
static uint16_t r26[1][1][1];
static uint32_t base[1][32];
int main() {
    printf("\ngpu/gpu-shape-time73\n");
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
    static const char* const MN[1] = { "512x240 15-bit" };
    static const uint32_t MODE[1] = { 0 };
    for (int m = 0; m < 1; m++) {
        waitReady();
        if (m == 1) {   /* mode 0 is Unirom's own, left as it is */
            write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
            write32(GP1, 0x05000000u);
            write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
            write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        }
        for (int i = 0; i < NS; i++) {
            waitReady(); write32(GP0, 0xE6000000u | S[i].e6);   /* the mask check, this shape's */
            waitReady(); sync(); probe(S[i], 64, 6000); waitReady(); sync(); probe(S[i], 64, 6000); waitReady();   /* the code warm */
            g_off = 20000u; base[m][i] = search(S[i], 64, 0, 16001);
        }
    }
    waitReady(); write32(GP0, 0xE6000000u);
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking (20,000 cycles after VBlank's IRQ, 64 after Timer 0's reset): plain, mask-checked, the difference\n");
    for (int i = 0; i + 1 < NS; i += 2)
        printf("%-12s plain %5u  masked %5u  +%d\n", S[i].name, (unsigned)base[0][i], (unsigned)base[0][i + 1], (int)base[0][i + 1] - (int)base[0][i]);
    printf("\ngpu/gpu-shape-time73: Done.\n");
    for (;;) VSync(0);
}
