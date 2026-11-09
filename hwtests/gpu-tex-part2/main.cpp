/*
 * gpu/gpu-tex-part2 - when does a row start writing, and does a 480i sprite fetch the rows it skips?
 *
 * WHY (2026-11-08). gpu-tex-part and gpu-tex-ilace on the second SCPH-5501: a transparent (0000h) 32-pixel block between
 * drawn ones costs as opaque, a row with no texel drawn writes nothing (480i texture rows of 0000h save 489 cycles), and a row
 * 480i skips is not walked at all (~5 cycles; polygons). Two readings of the first fit both: the row writes every block from
 * its first drawn texel on (a block before it asks for none), or the row writes whole once any texel of it is drawn. And
 * sprites: this core fetches a skipped sprite row's texels (rasterizeRect), unmeasured.
 *
 * WHAT IT DOES. gpu-tex-part's harness and triangle (96 x 32 from x 128, u and v 1:1: a fill every 4 pixels, every line missing,
 * the display one line tall): opaque, the whole row 0000h, the leading block (or two, or 16 texels) 0000h, all but the first,
 * the last or texel 40 0000h, the middle block 0000h (part's, again). Then a raw 15-bit 128x32 sprite (65h, texels 1:1: a fill
 * every 4) in 240p, in 480i with GP0(E1h) bit 10 set and clear, in 480i over odd texel rows 0000h, and over a 0000h leading or
 * middle block.
 *
 * READ IT LIKE THIS. "lead block 0" as opaque: the row writes whole; as opaque less a block's write (~5 a row): from its first
 * drawn texel. "sprite 480i E1.10=0" against "... odd rows 0" (walked, filled, not written) and half of 240p (not walked).
 *
 * ON THIS CORE (`node tools/hwrunner.js gpu-tex-part2 --emu --bios=<scph5501> --raw`, PSX_MODEL=SCPH-5501): PREDICTED.txt.
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
struct Var { char name[32]; int a, b, alt, a2, b2, kind, mode, e10, rows0; };   /* kind 1: a sprite; mode 1: 480i; rows0 1: odd texel rows 0000h */
static Var V[32];
static int NV = 0;
static void add(const char* nm, int a, int b, int alt = 0, int a2 = 0, int b2 = 0, int kind = 0, int mode = 0, int e10 = 1, int rows0 = 0) {
    Var& v = V[NV++]; sprintf(v.name, "%s", nm); v.a = a; v.b = b; v.alt = alt; v.a2 = a2; v.b2 = b2; v.kind = kind; v.mode = mode; v.e10 = e10; v.rows0 = rows0;
}
static void defs() {
    add("opaque", 0, 0);
    add("row all", 0, 96);                     /* nothing drawn: no write (gpu-tex-ilace's 0000h rows) */
    add("lead block 0", 0, 32);                /* the row's first 32 transparent, the rest drawn */
    add("lead blocks 0-1", 0, 64);
    add("lead 0-15", 0, 16);
    add("only first texel", 1, 96);
    add("only last texel", 0, 95);
    add("only texel 40", 0, 40, 0, 41, 96);
    add("mid all", 32, 64);                    /* gpu-tex-part's: as opaque */
    add("sprite 240p", 0, 0, 0, 0, 0, 1, 0, 1, 0);
    add("sprite 480i E1.10=1", 0, 0, 0, 0, 0, 1, 1, 1, 0);
    add("sprite 480i E1.10=0", 0, 0, 0, 0, 0, 1, 1, 0, 0);
    add("sprite 480i 1, odd rows 0", 0, 0, 0, 0, 0, 1, 1, 1, 1);
    add("sprite lead block 0", 0, 32, 0, 0, 0, 1, 0, 1, 0);
    add("sprite mid all", 32, 64, 0, 0, 0, 1, 0, 1, 0);
}

static const int X0 = 128, Y0 = 128, H = 32, W = 96, WS = 128;
static uint32_t tw[7];
static int nw = 7;
static void sprite() {   /* 65h: a raw textured 128x32 sprite at (128,128), texels 1:1 from (0,0): a fill every 4 pixels, every line missing */
    tw[0] = 0x65808080u; tw[1] = xy(X0, Y0); tw[2] = uv(0, 0, 0); tw[3] = xy(WS, H); nw = 4;
}
static void tri() {
    nw = 7;   /* gpu-tex-fill6's: left edge upright, u and v 1:1, clipped to W x 32 - a fill every 4 pixels, every line missing */
    tw[0] = 0x24808080u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + 252, Y0);        tw[4] = uv(252, 0, TPAGE);
    tw[5] = xy(X0, Y0 + 252);        tw[6] = uv(0, 252, 0);
}
static int clipW = W;
static void clipTo() {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(X0 + clipW - 1));
}
static void setMode(const Var& v) {
    write32(GP1, v.mode ? 0x08000025u : 0x08000001u);   /* 320 wide, 15-bit, NTSC; 480i: bit 2 (480 lines) and bit 5 (interlace) */
    write32(GP0, 0xE1000000u | ((uint32_t)v.e10 << 10) | TPAGE);   /* a sprite's page is E1's */
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
    if (v.rows0) for (int r = 1; r < 32; r += 2) { write32(GP0, 0x60000000u); write32(GP0, xy(512, 256 + r)); write32(GP0, xy(256, 1)); waitReady(); }
    waitReady();
}
static void prime() {
    waitReady(); clipTo();
    for (int i = 0; i < nw; i++) write32(GP0, tw[i]);    /* untimed: the code and the GPU warm */
    waitReady(); clipTo(); waitReady();
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < nw - 4; i++) write32(GP0, tw[i]);
    return probe4(tw + nw - 4, n, k);
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
    printf("\ngpu/gpu-tex-part2\n");
    printf("A 15-bit triangle clipped to 96 x 32 from x 128, u and v 1:1 (a fill every 4 pixels), leading texels 0000h or all but\n");
    printf("one; a raw 128x32 sprite (65h) in 240p and in 480i by GP0(E1h) bit 10, and over 0000h columns: its last four words stored back to\n");
    printf("back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points in the blanking,\n");
    printf("the display one line tall. No assertions.\n\n");
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
    waitReady();
    static uint32_t r[32][2];
    for (int i = 0; i < NV; i++) {
        texture(V[i]);
        if (V[i].kind) { sprite(); clipW = WS; } else { tri(); clipW = W; }
        setMode(V[i]);
        sync(0); probe(MAXD); waitReady();                       /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    waitReady(); clipAll();
    write32(GP1, 0x08000001u); write32(GP0, 0xE1000400u | TPAGE);
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant                       bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-29s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-part2: Done.\n");
    for (;;) VSync(0);
}
