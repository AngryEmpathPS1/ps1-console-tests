/*
 * gpu/gpu-tex-part - a 32-pixel block PARTLY drawn: does its frame-buffer write carry the whole block, or only what was drawn?
 *
 * WHY (2026-11-08). v0.377.306's texture-fill model (GPU_FILL_WB, STATUS "## Proposed: a texture fill waits for the row's
 * frame-buffer writes") has a row's pixels written in 32-halfword writes - one after every 32nd pixel, one at the row's end - and
 * fills wait while one holds the VRAM bus: 3 cycles and 1 a 4-halfword group. A block with NO texel drawn asks for no write
 * (gpu-tex-opaque's all-transparent quad: 212.4k cycles on the consoles against 230.6k drawn). Its KNOWN-OPEN: a block partly
 * drawn is charged as a whole one. Three readings part here: the whole block (the model), the drawn texels' count, or the span
 * from the first drawn to the last.
 *
 * WHAT IT DOES. gpu-tex-fill6's harness (cycle-exact, the display one line tall) and its triangle at W 96: a fill every 4 pixels,
 * every line missing, so each write delays the fills after it. The texture is 1234h but for chosen 0000h columns (flat
 * rectangles of colour 0) in the middle block (pixels 32-63, its write asked at 64) or the end block (64-95):
 *   opaque / mid all / end all       the two ends of the scale (all: no write, the measured rule)
 *   one at 32 / 63, first or last 4 / 16, every other, ends (32 and 63 only), one in each block
 * "every other" is 16 texels over the whole span and "first 16" 16 in half of it: count against span.
 *
 * READ IT LIKE THIS. A variant less "opaque": what the block's write no longer costs; against "all", how much of it is left.
 *
 * ON THIS CORE (`node tools/hwrunner.js gpu-tex-part --emu --bios=<scph5501> --raw`, PSX_MODEL=SCPH-5501): PREDICTED.txt.
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


/* the transparent texels: columns u in [a, b) of texel rows 0..31, or every other column of [a, b) (alt) */
struct Var { char name[28]; int a, b, alt, a2, b2; };
static Var V[32];
static int NV = 0;
static void add(const char* nm, int a, int b, int alt = 0, int a2 = 0, int b2 = 0) {
    Var& v = V[NV++]; sprintf(v.name, "%s", nm); v.a = a; v.b = b; v.alt = alt; v.a2 = a2; v.b2 = b2;
}
static void defs() {
    add("opaque", 0, 0);
    add("mid all", 32, 64);
    add("mid one at 32", 33, 64);
    add("mid one at 63", 32, 63);
    add("mid first 16", 48, 64);
    add("mid last 16", 32, 48);
    add("mid first 4", 36, 64);
    add("mid last 4", 32, 60);
    add("mid every other", 32, 64, 1);
    add("mid ends", 33, 63);                   /* drawn at 32 and 63 only: the span whole, two texels */
    add("end all", 64, 96);
    add("end one at 64", 65, 96);
    add("end one at 95", 64, 95);
    add("end first 16", 80, 96);
    add("end last 16", 64, 80);
    add("end every other", 64, 96, 1);
    add("mid+end one each", 33, 64, 0, 65, 96);
}

static const int X0 = 128, Y0 = 128, H = 32, W = 96;
static uint32_t tw[7];
static void tri() {   /* gpu-tex-fill6's: left edge upright, u and v 1:1, clipped to W x 32 - a fill every 4 pixels, every line missing */
    tw[0] = 0x24808080u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + 252, Y0);        tw[4] = uv(252, 0, TPAGE);
    tw[5] = xy(X0, Y0 + 252);        tw[6] = uv(0, 252, 0);
}
static void clipTo() {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(X0 + W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void clear0(int u, int w) {   /* texel columns u .. u+w-1, rows 0..31, to 0000h: a flat rectangle of colour 0 (rectangles never dither) */
    write32(GP0, 0x60000000u); write32(GP0, xy(512 + u, 256)); write32(GP0, xy(w, 32)); waitReady();
}
static void texture(const Var& v) {
    waitReady(); clipAll();
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    if (v.alt) { for (int u = v.a + 1; u < v.b; u += 2) clear0(u, 1); }   /* the even columns drawn, the odd ones transparent */
    else if (v.b > v.a) clear0(v.a, v.b - v.a);
    if (v.b2 > v.a2) clear0(v.a2, v.b2 - v.a2);
    waitReady();
}
static void prime() {
    waitReady(); clipTo();
    for (int i = 0; i < 7; i++) write32(GP0, tw[i]);     /* untimed: the code and the GPU warm */
    waitReady(); clipTo(); waitReady();
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < 3; i++) write32(GP0, tw[i]);
    return probe4(tw + 3, n, k);
}
static void sync(int j) {
    prime();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 40000;
static uint32_t search(int j) {
    uint32_t lo = 0, hi = MAXD + 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        sync(j);
        const uint32_t st = probe(mid);
        if (st & 0x04000000u) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main() {
    printf("\ngpu/gpu-tex-part\n");
    printf("A 15-bit triangle clipped to 96 x 32 from x 128, u and v 1:1 (a fill every 4 pixels), some texels 0000h in the row's\n");
    printf("middle 32-pixel block (pixels 32-63, its write asked at 64) or its end block (64-95): its last four words stored back to\n");
    printf("back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points in the blanking,\n");
    printf("the display one line tall. No assertions.\n\n");
    defs(); tri();
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
    waitReady();
    static uint32_t r[32][2];
    for (int i = 0; i < NV; i++) {
        texture(V[i]);
        sync(0); probe(MAXD); waitReady();                       /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant              bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-20s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-part: Done.\n");
    for (;;) VSync(0);
}
