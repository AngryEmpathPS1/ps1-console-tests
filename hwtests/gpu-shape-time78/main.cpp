/*
 * gpu/gpu-shape-time78 - IS A TALL COPY'S PINNED END THE COPY'S OR ITS READY FLAG'S? (2026-10-28)
 *
 * WHY. gpu-shape-time77 / 70 on the 7501 and the 5000: start by start a tall copy's toll falls by the start's 4 cycles for
 * 20-37 starts, then jumps - its end at one absolute time - by GPUSTAT bit 26. No copy model drawn so far fits it (STATUS "The
 * copy's scan-out toll, measured by display range ..."), and one reading is that the PIN is the flag's. Here 64x64 and 256x32
 * copies under 128- and 320-pixel displays, 270 starts, each three ways: alone (bit 26, as gst77), and followed at once by
 * GP0(1Fh) - the interrupt request, taken from the FIFO when the copy is done, GPUSTAT bit 24 - timed by bit 24 and by bit 26.
 * Every probe from a GPU with no interrupt pending (GP1(02h)). A sawtooth by bit 24 too: the copy ends there. Bit 24 smooth
 * where bit 26 has teeth: the pin is the flag's. Written by experiments/_mkgst78.js from gpu-shape-time77; its header follows.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time78 --frames=150000`; PREDICTED.txt beside this.
 *
 * gpu/gpu-shape-time77 - TALL COPIES UNDER DISPLAYS THAT PART THE SCAN-OUT'S BURSTS (2026-10-27).
 *
 * WHY. gpu-shape-time70 (the SCPH-7501): a long copy's end sits on a grid of its own block period, set by the bursts, and
 * it absorbs most of them - 256x32 pays 189 cycles in 320x240 (~10 refills, ~5 line-start fetches) but 527 in 512x240 (~15
 * refills) - where the one-row kernel (gst69) is the same in both modes. Which bursts does a long copy absorb? This keeps
 * the display at 320 wide (dot divider 8) and narrows its RANGE (GP1(06h)) so a line needs 128 / 256 / 320 pixels: 256
 * bytes (the line-start fetch alone, no refill), 512 (one refill a line), 640 (two, gst70's own), and then the display off
 * (GP1(03h) 1). STATUS "The copy's scan-out toll, continued ...".
 *
 * WHAT IT DOES. gpu-shape-time70's method unchanged (each probe from a fresh Timer 0 reset at the display's start, an exact
 * delay to s, the copy's last words, D cycles, one GPUSTAT read; a binary search per start for the first D with bit 26;
 * s = 64 + 4 j, j = 0..539), on copies 128x1 (a one-row control: gpu-shape-time69's kernel), 64x64, 256x32 and 128x16
 * (source (512,256), destination (192,128)), under the four displays.
 *
 * THE RULE: run it here first: `node hwtests/run.js gpu-shape-time77 --frames=300000`; PREDICTED.txt beside this.
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

static uint32_t probe5(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        "sw %[w4], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [w4] "r"(w[4]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}

struct Shape { const char* name; int n; uint32_t w[5]; uint32_t bit; };
static Shape S[8];
static int NS = 0;
static void copyS(const char* nm, int w, int h, bool irq, uint32_t bit) {
    Shape& s = S[NS++]; s.name = nm; s.n = irq ? 5 : 4; s.bit = bit;
    s.w[0] = 0x80000000u; s.w[1] = xy(512, 256); s.w[2] = xy(192, 128); s.w[3] = xy(w, h); s.w[4] = 0x1F000000u;
}
static void rect(const char* nm, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 3;
    s.w[0] = 0x60408020u; s.w[1] = xy(192, 128); s.w[2] = xy(w, h);
}
static void defs() {
    copyS("copy 64x64", 64, 64, false, 0x04000000u); copyS("64x64 b24", 64, 64, true, 0x01000000u); copyS("64x64 b26", 64, 64, true, 0x04000000u);
    copyS("copy 256x32", 256, 32, false, 0x04000000u); copyS("256x32 b24", 256, 32, true, 0x01000000u); copyS("256x32 b26", 256, 32, true, 0x04000000u);
}

static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return sh.n == 3 ? probe3(sh.w, s, n, k) : sh.n == 5 ? probe5(sh.w, s, n, k) : probe4(sh.w, s, n, k);
}
static uint32_t g_off = 20000u;
static void sync() {
    waitReady();
    write32(GP1, 0x02000000u);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < g_off) {} }
}
static uint32_t search(const Shape& sh, uint32_t s, uint32_t lo, uint32_t hi) {
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync();
        const uint32_t st = probe(sh, s, mid);
        if (st & sh.bit) hi = mid; else lo = mid + 1;
    }
    return lo;
}

static const int NI = 270;
#define NM 2
static uint16_t r26[NM][8][NI];   /* the first D with the shape's bit (26, or 24 behind GP0(1Fh)) */
static uint32_t base[NM][8];
int main() {
    printf("\ngpu/gpu-shape-time78\n");
    printf("A shape's last words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read: the\n");
    printf("first D with bit 26 set, or bit 24 behind GP0(1Fh) - tall copies' ends by two clocks. No assertions.\n\n");
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
    static const char* const MN[NM] = { "320x240 15-bit 128px", "320x240 15-bit" };
    static const uint32_t PX[NM] = { 128, 320 };
    for (int m = 0; m < NM; m++) {
        waitReady();
        write32(GP1, 0x08000001u); write32(GP1, 0x03000000u);   /* 320 wide, 15-bit */
        write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | ((0x260u + PX[m] * 8u) << 12) | 0x260u);
        write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
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
    write32(GP1, 0x02000000u);   /* no interrupt left pending */
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("the bit's D in the blanking, then from s = 64 + 4 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..269\n");
    for (int m = 0; m < NM; m++) {
        printf("%s:\n", MN[m]);
        for (int i = 0; i < NS; i++) {
            printf("%-12s %5u :", S[i].name, (unsigned)base[m][i]);
            for (int j = 0; j < NI; j++) printf(" %d", r26[m][i][j] == 0xFFFF ? 999 : (int)r26[m][i][j] - (int)base[m][i]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-shape-time78: Done.\n");
    for (;;) VSync(0);
}
