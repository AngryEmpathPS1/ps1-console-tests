/*
 * gpu/gpu-tex-fill5 - DENSE texture fills in clean rows: a fill every s pixels, s = 1..16, rows all from one x.
 *
 * WHY (2026-11-08). hwtests/gpu-tex-fill1-4 on the second SCPH-5501 pin a fill's cost when fills are sparse - 8.0 free, waits
 * only for the row's 32-halfword writes, fills ahead of a write not yet started push it - and a burst model of that
 * (experiments/wbsim3.js) fits all 192 of their variants to ~15 cycles; but gpu-tex-stride3's quads, fills every 1-32 pixels
 * with a diagonal split, it follows only part of the way (RMS ~78: fills every 2-4 pixels dearer than the model, every 1, 5
 * and 7 cheaper). Here the stride question without the diagonal.
 *
 * WHAT IT DOES. gpu-tex-fill4's harness (a 15-bit triangle clipped to W x 32 from x 128, v 1:1, u 0..31 over the row, every
 * line primed and the chosen ones evicted, cycle-exact, the display one line tall). The row is W = 8 s pixels over the 32
 * texels (u step 4 / s: a line every s pixels), s = 1..8: base, every line evicted (a fill every s pixels), every other line
 * (odd lines: a fill every 2 s, from pixel s; even lines: every 2 s, from pixel 0).
 *
 * READ IT LIKE THIS. A variant less its base, over the fills: a fill's cost at that spacing in a clean row.
 *
 * ON THIS CORE (`node hwtests/run.js gpu-tex-fill5 --ntsc --frames=20000 --quiet=15000`): PREDICTED.txt.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define GP0 0x1F801810u
#define GP1 0x1F801814u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitReady() { const uint32_t t0 = now(); while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 4000000u) {} }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */

#define PROBE_HEAD \
        ".set noreorder\n" \
        "lui $t0, 0x1f80\n" \
        "la $t1, 2f\n" \
        "sll $t2, %[k], 2\n" \
        "subu $t1, $t1, $t2\n"
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
static uint32_t probe4(const uint32_t* w, uint32_t n, uint32_t k) {
    uint32_t st;
    __asm__ volatile(PROBE_HEAD
        "sw %[w0], 0x1810($t0)\n"
        "sw %[w1], 0x1810($t0)\n"
        "sw %[w2], 0x1810($t0)\n"
        "sw %[w3], 0x1810($t0)\n"
        PROBE_TAIL
        : [st] "=r"(st), [n] "+r"(n) : [w0] "r"(w[0]), [w1] "r"(w[1]), [w2] "r"(w[2]), [w3] "r"(w[3]), [k] "r"(k) : "t0", "t1", "t2", "memory");
    return st;
}

struct Var { char name[24]; int W; int odd; uint32_t mask; int u0; int half; };   /* mask: the lines evicted */
static Var V[160];
static int NV = 0;
static void add(const char* nm, int a1, int a2, int W, int half, int u0, uint32_t mask) {
    Var& v = V[NV++]; sprintf(v.name, nm, a1, a2); v.W = W; v.odd = 0; v.mask = mask; v.u0 = u0; v.half = half;
}
static void defs() {
    for (int sp = 1; sp <= 8; sp++) {
        add("s %d W %d base", sp, 8 * sp, 8 * sp, 3, sp, 0);
        add("s %d W %d all", sp, 8 * sp, 8 * sp, 3, sp, 0xFFu);
        add("s %d W %d odd lines", sp, 8 * sp, 8 * sp, 3, sp, 0xAAu);
        add("s %d W %d even lines", sp, 8 * sp, 8 * sp, 3, sp, 0x55u);
    }
}

static const int X0 = 128, Y0 = 128, H = 32;
static uint32_t tw[7];
static void tri(const Var& v) {   /* half 3: the row W = 8 s over the 32 texels from u 0 (v.u0 carries s) */
    const int x0 = X0 + v.odd;
    tw[0] = 0x24808080u;
    tw[1] = xy(x0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(x0 + 2 * v.W, Y0);    tw[4] = uv(v.u0 == 1 || v.u0 == 8 ? 63 : 64, 0, TPAGE);   /* s 1 and 8: 64 reaches u 32 (the next block) at the last pixel */
    tw[5] = xy(x0, Y0 + 2 * H);      tw[6] = uv(0, 64, 0);
}
static void clipTo(const Var& v) {
    const int x0 = X0 + v.odd;
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)x0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(x0 + v.W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void prime(const Var& v) {
    waitReady(); clipTo(v);
    for (int i = 0; i < 7; i++) write32(GP0, tw[i]);     /* every line cached */
    waitReady(); clipAll();
    for (int j = 0; j < 8; j++) if (v.mask & (1u << j)) {   /* 4x32 sprite over line j at texel rows 32..63: evicts it */
        write32(GP0, 0x65808080u); write32(GP0, xy(400 + 8 * j, 128)); write32(GP0, uv(4 * j, 32, 0)); write32(GP0, xy(4, 32));
        waitReady();                                       /* the FIFO holds 16 words: eight sprites back to back overflow it */
    }
    waitReady(); clipTo(v); waitReady();
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < 3; i++) write32(GP0, tw[i]);
    return probe4(tw + 3, n, k);
}
static void sync(const Var& v, int j) {
    prime(v);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 6000;
static uint32_t search(const Var& v, int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(v, j);
        const uint32_t st = probe(mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-tex-fill5\n");
    printf("A 15-bit triangle clipped to W x 32, every texel cached but the evicted lines (one fill a row at 4j, or 8j at W 64):\n");
    printf("its last four words stored back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest\n");
    printf("over eight start points in the blanking, the display one line tall. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP1, 0x07000000u | (17u << 10) | 16u);                 /* the display one line tall */
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    clipAll();
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    static uint32_t r[160][2];
    for (int i = 0; i < NV; i++) {
        tri(V[i]);
        sync(V[i], 0); probe(MAXD); waitReady();                 /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(V[i], j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant          bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-16s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-fill5: Done.\n");
    for (;;) VSync(0);
}
