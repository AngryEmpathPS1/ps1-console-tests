/*
 * gpu/gpu-tex-ilace - interlace-skipped rows: are they walked, filled and written, or skipped whole?
 *
 * WHY (2026-11-08). v0.377.306's texture-fill model (GPU_FILL_WB, STATUS "## Proposed: a texture fill waits for the row's
 * frame-buffer writes") counts a row the GPU skips in 480i (GP0(E1h) bit 10 clear: this core's ilSkip, M132 and Pinball Power's
 * measurement) as written: its texels fetched, its fills charged, its 32-halfword writes asked for. KNOWN-OPEN, unmeasured. And
 * the references disagree on the bit itself: our psx-spx (nocash's v2.3 text) reads bit 10 as "Interlace Drawing (0=All Lines,
 * 1=Inactive Lines)", this core skips when it is 0 ("drawing to display area" prohibited). Both are asked here.
 *
 * WHAT IT DOES. gpu-tex-fill6's harness (cycle-exact, the display one line tall) and its triangle at W 128 x 32, v 1:1: a fill
 * every 4 pixels (u 1:1), every 8 (u 1:2) or none (base, u span 0). Each in 240p, in 480i (GP1(08h) 25h) with bit 10 set, with
 * it clear, and in 480i with bit 10 set over a texture whose odd or even texel rows are 0000h - every row walked and filled, half
 * the rows writing nothing (the measured rule: a block with no texel drawn asks for no write). If the skipped rows cost as those,
 * they are walked and filled but not written; as the full draw, written too (the model); far less, not walked at all. Then eight
 * 480i draws, bit 10 alternating, each into rows 128..131 cleared first: which rows came out drawn, beside GPUSTAT.31.
 *
 * READ IT LIKE THIS. Within a fill pattern, each 480i variant against "480i E1.10=1"; the read-back says which bit skips.
 *
 * ON THIS CORE (`node tools/hwrunner.js gpu-tex-ilace --emu --bios=<scph5501> --raw`, PSX_MODEL=SCPH-5501): PREDICTED.txt.
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


struct Var { char name[36]; int U; int mode; int e10; int rows0; };   /* mode 0 240p, 1 480i; rows0: texel rows made 0000h, 1 odd, 2 even */
static Var V[32];
static int NV = 0;
static void add(const char* nm, int W, int U, int mode, int e10, int rows0) {
    Var& v = V[NV++]; sprintf(v.name, nm, W); v.U = U; v.mode = mode; v.e10 = e10; v.rows0 = rows0;
}
static void defs() {
    static const int US[3] = { 252, 126, 0 };
    static const char* const UN[3] = { "every 4", "every 8", "base" };
    for (int i = 0; i < 3; i++) {
        char f[40];
        sprintf(f, "%s 240p", UN[i]);               add(f, 128, US[i], 0, 1, 0);
        sprintf(f, "%s 480i E1.10=1", UN[i]);       add(f, 128, US[i], 1, 1, 0);
        sprintf(f, "%s 480i E1.10=0", UN[i]);       add(f, 128, US[i], 1, 0, 0);
        sprintf(f, "%s 480i 1, odd rows 0", UN[i]); add(f, 128, US[i], 1, 1, 1);
        sprintf(f, "%s 480i 1, even rows 0", UN[i]); add(f, 128, US[i], 1, 1, 2);
    }
}

static const int X0 = 128, Y0 = 128, H = 32, W = 128;
static uint32_t tw[7];
static void tri(const Var& v) {   /* gpu-tex-fill6's: left edge upright, clipped to 128 x 32, v 1:1, u at x + 252 = U */
    tw[0] = 0x24808080u;
    tw[1] = xy(X0, Y0);              tw[2] = uv(0, 0, 0);
    tw[3] = xy(X0 + 252, Y0);        tw[4] = uv(v.U, 0, TPAGE);
    tw[5] = xy(X0, Y0 + 252);        tw[6] = uv(0, 252, 0);
}
static void clipTo() {
    write32(GP0, 0xE3000000u | ((uint32_t)Y0 << 10) | (uint32_t)X0);
    write32(GP0, 0xE4000000u | ((uint32_t)(Y0 + H - 1) << 10) | (uint32_t)(X0 + W - 1));
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static void setMode(const Var& v) {
    write32(GP1, v.mode ? 0x08000025u : 0x08000001u);   /* 320 wide, 15-bit, NTSC; 480i: bit 2 (480 lines) and bit 5 (interlace) */
    write32(GP0, 0xE1000000u | ((uint32_t)v.e10 << 10) | TPAGE);
}
static void texture(const Var& v) {
    waitReady(); clipAll();
    write32(GP0, 0x02000000u | (0x20u << 16) | (0x88u << 8) | 0xA0u); write32(GP0, xy(512, 256)); write32(GP0, xy(256, 256));   /* 1234h */
    waitReady();
    if (v.rows0) for (int r = (v.rows0 == 1 ? 1 : 0); r < 32; r += 2) {   /* texel row r (drawn on VRAM row 128 + r) to 0000h */
        write32(GP0, 0x60000000u); write32(GP0, xy(512, 256 + r)); write32(GP0, xy(256, 1)); waitReady();
    }
    waitReady();
}
static void prime() {
    waitReady(); clipTo();
    for (int i = 0; i < 7; i++) write32(GP0, tw[i]);     /* untimed: a base row's one line cached (wide rows evict their own) */
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
/* which rows a draw wrote: rows 128..131 at x 130 cleared to 0000h, the triangle drawn once, the four rows read back (GP0(C0h)) */
static uint32_t rb[8][3];
static void readBack(int slot, int e10) {
    const Var p = { "", 252, 0, 1, 0 }, v = { "", 252, 1, e10, 0 };
    tri(v);
    setMode(p); waitReady(); clipAll();                                  /* the clear in 240p, where nothing is skipped */
    write32(GP0, 0x02000000u); write32(GP0, xy(128, 128)); write32(GP0, xy(16, 4)); waitReady();   /* 0000h */
    setMode(v);
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); while (now() - t0 < 20000u) {} }
    rb[slot][0] = read32(GP1) >> 31;                                     /* GPUSTAT.31 as the draw starts */
    clipTo(); for (int i = 0; i < 7; i++) write32(GP0, tw[i]); waitReady(); clipAll();
    write32(GP0, 0xC0000000u); write32(GP0, xy(130, 128)); write32(GP0, xy(2, 4));
    { const uint32_t t0 = now(); while (!(read32(GP1) & 0x08000000u) && now() - t0 < 400000u) {} }
    uint32_t m = 0;
    for (int r = 0; r < 4; r++) { const uint32_t d = read32(GP0); if ((d & 0xFFFFu) != 0) m |= 1u << r; }   /* bit r: row 128 + r drawn */
    rb[slot][1] = m; rb[slot][2] = (uint32_t)e10;
}

int main() {
    printf("\ngpu/gpu-tex-ilace\n");
    printf("A 15-bit triangle clipped to 128 x 32 from (128,128), v 1:1, a fill every 4 / 8 pixels or none (base), in 240p and in 480i\n");
    printf("with GP0(E1h) bit 10 set and clear, and with every other texel row 0000h: its last four words stored back to back, D\n");
    printf("cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points, the display one line tall.\n");
    printf("Then which of rows 128..131 a 480i draw wrote, by bit 10. No assertions.\n\n");
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
        texture(V[i]); tri(V[i]); setMode(V[i]);
        sync(0); probe(MAXD); waitReady();                       /* the code warm */
        uint32_t a = 0xFFFFFFFFu, b = 0;
        for (int j = 0; j < 8; j++) { const uint32_t x = search(j); a = x < a ? x : a; b = x > b ? x : b; }
        r[i][0] = a; r[i][1] = b;
    }
    { const Var o = { "", 252, 1, 1, 0 }; texture(o); }
    for (int s = 0; s < 8; s++) readBack(s, s & 1);
    waitReady(); clipAll();
    write32(GP1, 0x08000001u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP1, 0x07000000u | (256u << 10) | 16u);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("variant                         bit 26's D\n");
    for (int i = 0; i < NV; i++) printf("%-31s %5u..%-5u\n", V[i].name, (unsigned)r[i][0], (unsigned)r[i][1]);
    printf("\n480i read-back (rows 128 129 130 131: 1 drawn):\n");
    for (int s = 0; s < 8; s++) printf("  E1.10=%u  GPUSTAT.31 %u   %u %u %u %u\n", (unsigned)rb[s][2], (unsigned)rb[s][0],
        (unsigned)(rb[s][1] & 1), (unsigned)((rb[s][1] >> 1) & 1), (unsigned)((rb[s][1] >> 2) & 1), (unsigned)((rb[s][1] >> 3) & 1));
    printf("\ngpu/gpu-tex-ilace: Done.\n");
    for (;;) VSync(0);
}
