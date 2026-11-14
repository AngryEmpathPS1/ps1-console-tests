/*
 * gpu/gpu-copy-modes33 - A TALL COPY'S SCAN-OUT TOLL at 320x240 and 512x240, to the cycle: gpu-shape-time69 / 70's maps at 64, 128 and
 * 240 rows (2026-11-13).
 *
 * WHY. GPU_COPY_TOLL2's tables end at 32 rows (64 at 64 wide) and a taller copy takes the last cell. A wide copy's toll FALLS with
 * its rows (320x240, 256 wide: 4.82 / 4.35 / 3.33 / 1.81% at 1 / 4 / 8 / 32; on the PAL crystal 4.78 / 4.45 / 3.88 / 2.63), and
 * ps1-tests' gpu/bandwidth - 400 copies of 320x240 on a PAL-clocked console - asks for about 2.1% where the PAL table's held
 * cell is 2.63 (STATUS "Proposed: the copy toll's tables for every display mode, by video crystal"). Nothing measures it.
 *
 * WHAT IT DOES. gpu-copy-modes' probe (the copy's words stored back to back s cycles after Timer 0's reset, then D cycles, ONE
 * GPUSTAT read, D binary-searched for bit 26), first in the vertical blanking - started 1,500 cycles after the VBlank
 * interrupt: the blanking is 23 lines here (the display 16 .. 256), 49,500 cycles, so every shape but 256x240 (78,600) ends
 * inside it and that one's "blanking" D carries ~14 lines of the picture (take its base as 240 rows at 256x128's rate) - then
 * at 270 starts 8 cycles apart across a line ~93 lines after VBlank (a 240-row copy ends ~36 lines later, inside the
 * picture), in 320x240 and 512x240 15-bit: copies 256x32 (the tables' last cell, the control), 256x64, 256x128, 256x240,
 * 64x128 and 64x240 from (512,256) to (192,128).
 *
 * READ IT LIKE THIS. As gpu-copy-modes: per mode and shape the blanking's D, then the 270 starts' D less the blanking's (999 =
 * past the search). The mean over the line as a percent of the blanking's D is the table's cell
 * (experiments/audit/copymean.js). A blanking D that is not ~327 cycles a row of 256 (82 a row of 64) says that copy ran
 * into the picture: read the taller rows against the 32-row cell's rate then. No assertions.
 *
 * THE RULE: run it here first: `node tools/hwrunner.js gpu-copy-modes3 --emu --timeout=9000`.
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
static void copyS(const char* nm, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 4;
    s.w[0] = 0x80000000u; s.w[1] = xy(512, 256); s.w[2] = xy(192, 128); s.w[3] = xy(w, h);
}
static void rect(const char* nm, int w, int h) {
    Shape& s = S[NS++]; s.name = nm; s.n = 3;
    s.w[0] = 0x60408020u; s.w[1] = xy(192, 128); s.w[2] = xy(w, h);
}
static void defs() {
    copyS("copy 256x32", 256, 32); copyS("copy 256x64", 256, 64); copyS("copy 256x128", 256, 128); copyS("copy 256x240", 256, 240);
    copyS("copy 64x128", 64, 128); copyS("copy 64x240", 64, 240);
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

static const int NI = 270;
static uint16_t r26[2][8][NI];
static uint32_t lo26[2][8];
static uint32_t base[2][8];
int main() {
    printf("\ngpu/gpu-copy-modes3\n");
    printf("A shape's last words stored back to back from s cycles after Timer 0's reset, D cycles, one GPUSTAT read: the\n");
    printf("first D with bit 26 set, under two display modes; tall copies. No assertions.\n\n");
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
    static const char* const MN[2] = { "320x240 15-bit", "512x240 15-bit" };
    static const uint32_t MODE[2] = { 0x01, 0x02 };
    for (int m = 0; m < 2; m++) {
        waitReady();
        {
            write32(GP1, 0x08000000u | MODE[m]); write32(GP1, 0x03000000u);
            write32(GP1, 0x05000000u);
            write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
            write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
        }
        for (int i = 0; i < NS; i++) {
            waitReady(); sync(); probe(S[i], 64, 90000); waitReady(); sync(); probe(S[i], 64, 90000); waitReady();   /* the code warm */
            g_off = 1500u; base[m][i] = search(S[i], 64, 0, 100001);
            g_off = 199000u;
            const uint32_t lo = base[m][i] > 64 ? base[m][i] - 64 : 0, hi = base[m][i] + base[m][i] / 8 + 256;
            lo26[m][i] = lo;
            for (int j = 0; j < NI; j++) { const uint32_t d = search(S[i], 64 + 8 * j, lo, hi + 1); r26[m][i][j] = (uint16_t)(d > hi ? 0xFFFF : d - lo); }
        }
    }
    waitReady();
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    waitReady();
    write16(T0_MODE, m0);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("bit 26's D in the blanking, then from s = 64 + 8 j cycles after Timer 0's reset ~93 lines after VBlank less the blanking's, j = 0..269\n");
    for (int m = 0; m < 2; m++) {
        printf("%s:\n", MN[m]);
        for (int i = 0; i < NS; i++) {
            printf("%-12s %5u :", S[i].name, (unsigned)base[m][i]);
            for (int j = 0; j < NI; j++) printf(" %d", r26[m][i][j] == 0xFFFF ? 999 : (int)(r26[m][i][j] + lo26[m][i]) - (int)base[m][i]);
            printf("\n");
        }
    }
    printf("\ngpu/gpu-copy-modes3: Done.\n");
    for (;;) VSync(0);
}
