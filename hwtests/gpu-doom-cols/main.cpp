/*
 * gpu-doom-cols: DOOM'S OWN TRIANGLES, EACH TIMED TO THE CYCLE - wall columns a pixel wide and floor spans a row tall.
 *
 * WHY. hwtests/gpu-doom-feed (two Doom play passes' packets through the game's feed loop) reads the finding of the doom-probe
 * session on a test both can run: the second SCPH-5501 and the PSone hand-feed 8,390-8,438 words of pass 428 where v0.377.348
 * feeds 8,334-8,358, and 4,986-5,010 of pass 496 against 4,890-4,922 - the console's GPU is ready a little sooner. The sky's
 * sprite takes the same time on both (59,930 cycles). The feed is GP0(24h) triangles of two kinds: wall columns 1 pixel wide and
 * 3-127 rows tall, and floor spans 1 row tall and 3-260 pixels wide. Which kind does the console draw faster, and by how much?
 *
 * WHAT. Ten of pass 428's own triangles (their seven words as the game stored them; the textures are this test's, VRAM one
 * colour), under the game's mode (256x240), drawing area, offset and texture window. gpu-copy-modes' method: the seven words
 * back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read; the first D with bit 26 set, in the blanking
 * and from 270 places across an active line. experiments/audit/copymean.js reads the log.
 * No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-doom-cols --emu --timeout=9000`.
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
static uint32_t probe7(const uint32_t* w, uint32_t s, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(SYNC_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        "sw %[w4], 0x1810($t0)\n"
        "sw %[w5], 0x1810($t0)\n"
        "sw %[w6], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [w4] "r"(w[4]), [w5] "r"(w[5]), [w6] "r"(w[6]), [s] "r"(s), [k] "r"(k)
        : "t0", "t1", "t2", "t3", "t4", "t5", "t6", "memory");
    return st;
}
struct Shape { const char* name; uint32_t w[7]; };
static const Shape S[] = {
    { "col 1x4",    { 0x243f3f3fu, 0x006c007eu, 0x3c000098u, 0x0070007fu, 0x008e0898u, 0x0070007eu, 0x00000898u } },
    { "col 1x15",   { 0x241a303bu, 0x005d0078u, 0x3c00b74bu, 0x006c0079u, 0x008cff4bu, 0x006c0078u, 0x0000ff4bu } },
    { "col 1x17",   { 0x241a303bu, 0x005c0042u, 0x3c00b742u, 0x006d0043u, 0x008cff42u, 0x006d0042u, 0x0000ff42u } },
    { "col 1x18",   { 0x24272727u, 0x005700a0u, 0x3c006f9au, 0x006900a1u, 0x008cff9au, 0x006900a0u, 0x0000ff9au } },
    { "col 1x43",   { 0x24525252u, 0x004900d3u, 0x3c000006u, 0x007400d4u, 0x008c8006u, 0x007400d3u, 0x00008006u } },
    { "col 1x54",   { 0x243b3b3bu, 0x003b00e4u, 0x3c000007u, 0x007100e5u, 0x008e8807u, 0x007100e4u, 0x00008807u } },
    { "col 1x73",   { 0x248b8b8bu, 0x00370027u, 0x3c000025u, 0x00800028u, 0x008c8025u, 0x00800027u, 0x00008025u } },
    { "span 7x1",   { 0x24272727u, 0x003100fbu, 0x3c003237u, 0x00310102u, 0x008a3245u, 0x00320102u, 0x00003245u } },
    { "span 56x1",  { 0x243f3f3fu, 0x007d0052u, 0x3c002926u, 0x007d008au, 0x008a2994u, 0x007e008au, 0x00002994u } },
    { "span 260x1", { 0x2436637au, 0x00c1fffeu, 0x3c002707u, 0x00c10102u, 0x008a2878u, 0x00c20102u, 0x00002878u } },
};
static const int NS = (int)(sizeof(S) / sizeof(S[0]));
static uint32_t probe(const Shape& sh, uint32_t s, uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    return probe7(sh.w, s, n, k);
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

static const int NI = 270;
static uint16_t r26[16][NI];
static uint32_t lo26[16];
static uint32_t base[16];
int main() {
    printf("\ngpu/gpu-doom-cols\n");
    printf("A Doom triangle's seven words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read:\n");
    printf("the first D with bit 26 set, at 256x240. No assertions.\n\n");
    const uint32_t stat0 = read32(GP1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE), m0 = read16(T0_MODE);
    write16(T2_MODE, 0);
    write16(T0_MODE, 0x0003);   /* sysclock, sync mode 1: reset at the hblank gate's end */
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u); write32(GP0, 0xE2000000u); write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE6000000u);
    for (int q = 0; q < 4; q++) { waitReady(); write32(GP0, 0x02305070u); write32(GP0, xy((q & 1) * 512, (q >> 1) * 256)); write32(GP0, xy(512, 256)); }
    waitReady();
    write32(GP0, 0x02000000u); write32(GP0, xy(0, 0)); write32(GP0, xy(512, 240));
    waitReady();
    /* pass 428's own environment (hwtests/gpu-doom-feed/stream.h P0_ENV) */
    write32(GP0, 0xe100008au); write32(GP0, 0xe2010463u); write32(GP0, 0xe3000100u); write32(GP0, 0xe403bdffu); write32(GP0, 0xe5000100u);
    write32(GP1, 0x08000000u); write32(GP1, 0x03000000u);
    write32(GP1, 0x05000000u);
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    for (int i = 0; i < NS; i++) {
        waitReady(); sync(); probe(S[i], 64, 9000); waitReady(); sync(); probe(S[i], 64, 9000); waitReady();   /* the code warm */
        g_off = 1500u; base[i] = search(S[i], 64, 0, 20001);
        g_off = 199000u;
        const uint32_t lo = base[i] > 64 ? base[i] - 64 : 0, hi = base[i] + base[i] / 4 + 256;
        lo26[i] = lo;
        for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 8 * j, lo, hi + 1); r26[i][j] = (uint16_t)(d > hi ? 0xFFFF : d - lo); }
    }
    waitReady();
    write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); write32(GP0, 0xE5000000u); write32(GP0, 0xE2000000u);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 64 + 8 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..269\n");
    printf("256x240 15-bit:\n");
    for (int i = 0; i < NS; i++) {
        printf("%-12s %5u :", S[i].name, (unsigned)base[i]);
        for (int j = 0; j < NI; j++) printf(" %d", r26[i][j] == 0xFFFF ? 999 : (int)(r26[i][j] + lo26[i]) - (int)base[i]);
        printf("\n");
    }
    printf("\ndone\n");
    return 0;
}
