/*
 * gpu/gpu-tex-semipart - a SEMI-TRANSPARENT textured row with 0000h or non-semi texels: what does its read-back cost?
 *
 * WHY (2026-11-09). v0.377.309 (STATUS "## Proposed: a row writes from its first drawn texel ...") measured the OPAQUE rows: a row
 * writes from its first drawn texel to its end, 0000h or not after it; an opaque sprite's drawn share the same. A semi-transparent
 * pixel also reads the frame buffer back, and only texels with bit 15 set blend. This core charges a semi polygon its semi premium
 * by the drawn share (GPU_SEMI_SHADED's sdf, GPU_SEMI_TEXP a fill) and a semi sprite by its drawn fraction (GPU_SEMI_SPR) -
 * KNOWN-OPEN in both, unmeasured with 0000h texels or texels without bit 15.
 *
 * WHAT IT DOES. gpu-tex-part2's harness (cycle-exact, the display one line tall): its 15-bit triangle (96 x 32 from x 128, texels
 * 1:1, a fill every 4 pixels) as 24h (opaque) or 26h (semi, mode 0, blended at 808080h), and its raw 128x32 sprite as 65h or 67h.
 * The texture 1234h (bit 15 clear: semi does not blend) or 9234h (bit 15 set: a flat rectangle drawn with GP0(E6h) bit 0), and over
 * the 9234h chosen columns 0000h - the whole row, the leading block, the middle block (32-63), every other texel - or 1234h - the
 * leading or middle block.
 *
 * READ IT LIKE THIS. "semi, 9234h" less "semi, 1234h": the read-back's cost. Each 0000h / 1234h variant against it: whether a
 * texel that does not blend still pays it, and from where in the row.
 *
 * ON THIS CORE (`node tools/hwrunner.js gpu-tex-semipart --emu --bios=<scph5501> --raw`, PSX_MODEL=SCPH-5501): PREDICTED.txt.
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
/* kind 1: a sprite; semi: the semi-transparent command; tex 1: texels 9234h (bit 15 set: semi applies), 0: 1234h; [a, b): 0000h
 * (alt: every other column of it); [n1, n2): 1234h among the 9234h */
struct Var { char name[36]; int a, b, alt, kind, semi, tex, n1, n2; };
static Var V[32];
static int NV = 0;
static void add(const char* nm, int kind, int semi, int tex, int a = 0, int b = 0, int alt = 0, int n1 = 0, int n2 = 0) {
    Var& v = V[NV++]; sprintf(v.name, "%s%s", kind ? "sprite " : "", nm); v.a = a; v.b = b; v.alt = alt; v.kind = kind; v.semi = semi; v.tex = tex; v.n1 = n1; v.n2 = n2;
}
static void defs() {
    for (int k = 0; k < 2; k++) {
        const int W2 = k ? 128 : 96;
        add("opaque, 1234h", k, 0, 0);
        add("semi, 1234h (no bit 15)", k, 1, 0);
        add("semi, 9234h", k, 1, 1);
        add("semi, row all 0000h", k, 1, 1, 0, W2);
        add("semi, lead block 0000h", k, 1, 1, 0, 32);
        add("semi, mid block 0000h", k, 1, 1, 32, 64);
        add("semi, every other 0000h", k, 1, 1, 0, W2, 1);
        add("semi, lead block 1234h", k, 1, 1, 0, 0, 0, 0, 32);
        add("semi, mid block 1234h", k, 1, 1, 0, 0, 0, 32, 64);
    }
}

static const int X0 = 128, Y0 = 128, H = 32, W = 96, WS = 128;
static uint32_t tw[7];
static int nw = 7;
static void sprite(int semi) {   /* 65h / 67h: a raw textured 128x32 sprite at (128,128), texels 1:1 from (0,0): a fill every 4 pixels, every line missing */
    tw[0] = (semi ? 0x67808080u : 0x65808080u); tw[1] = xy(X0, Y0); tw[2] = uv(0, 0, 0); tw[3] = xy(WS, H); nw = 4;
}
static void tri(int semi) {
    nw = 7;   /* gpu-tex-fill6's: left edge upright, u and v 1:1, clipped to W x 32 - a fill every 4 pixels, every line missing */
    tw[0] = semi ? 0x26808080u : 0x24808080u;   /* 26h: semi, blended at 808080h (the texel as it is), mode 0 (B/2 + F/2) */
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
    (void)v;
    write32(GP0, 0xE1000400u | TPAGE);   /* a sprite's page and semi mode (0) are E1's */
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void clear0(int u, int w) {   /* texel columns u .. u+w-1, rows 0..31, to 0000h: a flat rectangle of colour 0 (rectangles never dither) */
    write32(GP0, 0x60000000u); write32(GP0, xy(512 + u, 256)); write32(GP0, xy(w, 32)); waitReady();
}
static void rect(int u, int w, uint32_t rgb) { write32(GP0, 0x60000000u | rgb); write32(GP0, xy(512 + u, 256)); write32(GP0, xy(w, 32)); waitReady(); }
static void texture(const Var& v) {
    waitReady(); clipAll();
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    if (v.tex) { write32(GP0, 0xE6000001u); rect(0, 256, 0x2088A0u); write32(GP0, 0xE6000000u); }   /* 9234h: a flat rectangle with the mask bit set (E6h bit 0) */
    if (v.n2 > v.n1) rect(v.n1, v.n2 - v.n1, 0x2088A0u);                                             /* back to 1234h */
    if (v.alt) { for (int u = v.a + 1; u < v.b; u += 2) clear0(u, 1); }
    else if (v.b > v.a) clear0(v.a, v.b - v.a);
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
    printf("\ngpu/gpu-tex-semipart\n");
    printf("A 15-bit triangle clipped to 96 x 32 (24h opaque / 26h semi) and a raw 128x32 sprite (65h / 67h), texels 1:1 (a fill every\n");
    printf("4 pixels), 1234h or 9234h (bit 15: semi blends) with columns 0000h or 1234h: its last four words stored back to\n");
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
        if (V[i].kind) { sprite(V[i].semi); clipW = WS; } else { tri(V[i].semi); clipW = W; }
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
    printf("variant                               bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-37s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\ngpu/gpu-tex-semipart: Done.\n");
    for (;;) VSync(0);
}
