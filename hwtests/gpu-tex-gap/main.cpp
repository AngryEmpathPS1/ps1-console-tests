/*
 * gpu/gpu-tex-gap - gpu-cost-sweep's big textured polygons, its two unknowns apart: the display on, and VRAM's own texels.
 *
 * WHY (2026-11-08). v0.377.306's texture-fill model (GPU_FILL_WB, STATUS "## Proposed: a texture fill waits for the row's
 * frame-buffer writes") was measured with the display one line tall (no scan-out on the VRAM bus) and opaque textures. Its
 * race: gpu-cost-sweep's 256-wide textured quads and triangles at 4, 8 and 15 bits read dearer on the consoles than the model's
 * mt2 (a fill after a row's second mid-row write: 0.3) makes them, +28k cycles summed over the new-GPU models. cost-sweep
 * draws them at (0,0) - inside the display the runner's start leaves on - from whatever VRAM holds at X 512 Y 0 and its CLUT
 * at (0,480): partly 0000h, which writes nothing where a whole 32-pixel block is (the measured rule), and partly drawn
 * blocks are charged whole (KNOWN-OPEN, gpu-tex-part asks it).
 *
 * WHAT IT DOES. gpu-tex-fill6's cycle-exact harness on cost-sweep's own polygons (8-, 15- and 4-bit 2Ch quads 256x256, an
 * 8-bit 24h triangle with legs 256; texels 1:1). First the start's VRAM counted (GP0(C0h): the CLUT row's 0000h entries, the
 * page's texels that come out 0000h at each depth) and each polygon timed on it as found, the display as found. Then each in
 * four: the display one line tall or 512x240 over the polygons, the page and CLUT all 1234h (every block drawn) or all 0000h
 * (each change followed by a 1x1 sprite with another CLUT: a fill does not reload the CLUT cache, the next polygon then does).
 *
 * READ IT LIKE THIS. "on" less "line" at one texture: the scan-out's share. "as found" against the two textures: where the
 * found texture sits between them, and the count says how much of it is 0000h.
 *
 * ON THIS CORE (`node tools/hwrunner.js gpu-tex-gap --emu --bios=<scph5501> --raw`, PSX_MODEL=SCPH-5501): PREDICTED.txt.
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


/* gpu-cost-sweep's own polygons: at (0,0), the page at X 512 Y 0, the CLUT at (0,480) */
static const uint32_t CLUT = (480u << 6) | 0u;
static uint32_t tpageOf(int depth) { return 8u | ((uint32_t)depth << 7); }
struct Prim { char name[24]; uint32_t w[12]; int n; int depth; };
static Prim P[4];
static int NP = 0;
static void quad(const char* nm, int depth) {
    Prim& p = P[NP++]; sprintf(p.name, "%s", nm); p.depth = depth; p.n = 0;
    p.w[p.n++] = 0x2C808080u;
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? 256 : 0, y = (i & 2) ? 256 : 0;
        p.w[p.n++] = xy(x, y);
        p.w[p.n++] = uv(x > 255 ? 255 : x, y > 255 ? 255 : y, i == 0 ? CLUT : i == 1 ? tpageOf(depth) : 0);
    }
}
static void tri(const char* nm, int depth) {
    Prim& p = P[NP++]; sprintf(p.name, "%s", nm); p.depth = depth; p.n = 0;
    p.w[p.n++] = 0x24808080u;
    for (int i = 0; i < 3; i++) {
        const int x = i == 1 ? 256 : 0, y = i == 2 ? 256 : 0;
        p.w[p.n++] = xy(x, y);
        p.w[p.n++] = uv(x > 255 ? 255 : x, y > 255 ? 255 : y, i == 0 ? CLUT : i == 1 ? tpageOf(depth) : 0);
    }
}
static void clipAll() { write32(GP0, 0xE3000000u); write32(GP0, 0xE4000000u | (511u << 10) | 1023u); }
static const Prim* cur;
static void drawMode() { write32(GP0, 0xE1000400u | tpageOf(cur->depth)); }
static void prime() {
    waitReady(); drawMode();
    for (int i = 0; i < cur->n; i++) write32(GP0, cur->w[i]);   /* untimed: the code and the GPU warm */
    waitReady();
}
static uint32_t probe(uint32_t d) {
    const uint32_t n = d / 3, k = d - 3 * n;
    for (int i = 0; i < cur->n - 4; i++) write32(GP0, cur->w[i]);
    return probe4(cur->w + cur->n - 4, n, k);
}
static void sync(int j) {
    prime();
    write32(I_STAT, ~1u);
    { const uint32_t t0 = now(); while (!(read32(I_STAT) & 1u) && now() - t0 < 2000000u) {} }
    { const uint32_t t0 = now(); const uint32_t w = 20000u + 271u * (uint32_t)j; while (now() - t0 < w) {} }
}
static const uint32_t MAXD = 262143;
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
static void timeRow(const char* tag, int p, uint32_t out[2]) {
    cur = &P[p];
    sync(0); probe(MAXD); waitReady();                           /* the code warm */
    uint32_t a = 0xFFFFFFFFu, b = 0;
    for (int j = 0; j < 8; j++) { const uint32_t x = search(j); a = x < a ? x : a; b = x > b ? x : b; }
    out[0] = a; out[1] = b; (void)tag;
}
/* the found texture: GP0(C0h) of the CLUT row and the page, counted - texels whose colour is 0000h at 15, 8 and 4 bits */
static uint16_t clut[256];
static uint32_t zero15, zero8, zero4, zc8, zc4;
static void countFound() {
    waitReady();
    write32(GP0, 0xC0000000u); write32(GP0, xy(0, 480)); write32(GP0, xy(256, 1));
    { const uint32_t t0 = now(); while (!(read32(GP1) & 0x08000000u) && now() - t0 < 400000u) {} }
    for (int i = 0; i < 128; i++) { const uint32_t d = read32(GP0); clut[2 * i] = (uint16_t)d; clut[2 * i + 1] = (uint16_t)(d >> 16); }
    zc8 = zc4 = 0; for (int i = 0; i < 256; i++) if (!clut[i]) { zc8++; if (i < 16) zc4++; }
    waitReady();
    write32(GP0, 0xC0000000u); write32(GP0, xy(512, 0)); write32(GP0, xy(256, 256));
    { const uint32_t t0 = now(); while (!(read32(GP1) & 0x08000000u) && now() - t0 < 400000u) {} }
    zero15 = zero8 = zero4 = 0;
    for (int i = 0; i < 256 * 128; i++) {
        const uint32_t d = read32(GP0);
        for (int h = 0; h < 2; h++) {
            const uint32_t hw = h ? d >> 16 : d & 0xFFFFu, c = (uint32_t)((2 * i + h) & 255);
            if (!hw) zero15++;
            if (c < 128) { if (!clut[hw & 0xFF]) zero8++; if (!clut[hw >> 8]) zero8++; }
            if (c < 64) for (int q = 0; q < 4; q++) if (!clut[(hw >> (4 * q)) & 15]) zero4++;
        }
    }
}
static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitReady(); clipAll();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r); write32(GP0, xy(x, y)); write32(GP0, xy(w, h));
    waitReady();
}
static void texture(int opaque) {   /* the page and the CLUT 1234h (every texel drawn), or 0000h (none) */
    fill15(512, 0, 256, 256, opaque ? 0x1234u : 0);
    fill15(0, 480, 256, 1, opaque ? 0x1234u : 0);
    /* a fill over the CLUT leaves the CLUT cache as it was (gputest's gpu/clut-cache, testClutCacheReuseNoClear): a 1x1 8-bit
     * sprite with another CLUT, so the next polygon's own reloads all 256 */
    write32(GP0, 0xE1000400u | tpageOf(1));
    write32(GP0, 0x65808080u); write32(GP0, xy(1000, 500)); write32(GP0, uv(0, 0, (480u << 6) | 1u)); write32(GP0, xy(1, 1));
    waitReady();
}
static void displayOn(int on) {
    if (on) {   /* 512x240 15-bit from (0,0), the area the polygons draw into: the scan-out reads it */
        write32(GP1, 0x08000002u); write32(GP1, 0x05000000u);
        write32(GP1, 0x06000000u | (0xC60u << 12) | 0x260u); write32(GP1, 0x07000000u | (256u << 10) | 16u);
        write32(GP1, 0x03000000u);
    } else write32(GP1, 0x07000000u | (17u << 10) | 16u);   /* the display one line tall: no scan-out */
}

int main() {
    printf("\ngpu/gpu-tex-gap\n");
    printf("gpu-cost-sweep's 256-wide textured polygons (at (0,0), the page at X 512, the CLUT at (0,480)): as found, then with the\n");
    printf("display on (512x240 over them) or one line tall, the texture all drawn (1234h) or all 0000h: its last four words stored\n");
    printf("back to back, D cycles, one GPUSTAT read: the first D with bit 26 set, fastest..slowest over eight start points.\n");
    printf("No assertions.\n\n");
    quad(" 8-bit quad 256", 1); quad("15-bit quad 256", 2); quad(" 4-bit quad 256", 0); tri(" 8-bit tri L 256", 1);
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE2000000u);
    clipAll();
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    waitReady();
    static uint32_t af[4][2], r[4][2][2][2];
    countFound();                                                /* the start's VRAM, before anything here draws */
    for (int p = 0; p < NP; p++) timeRow("as found", p, af[p]);
    for (int on = 0; on < 2; on++) {
        displayOn(on);
        for (int op = 0; op < 2; op++) { texture(op); for (int p = 0; p < NP; p++) timeRow("", p, r[p][on][op]); }
    }
    waitReady(); clipAll();
    write32(GP0, 0xE1000400u | tpageOf(2));
    displayOn(1);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("found: CLUT entries 0000h %u/16 (4-bit) %u/256 (8-bit); page texels 0000h 15-bit %u/65536, 8-bit %u/65536, 4-bit %u/65536\n\n",
        (unsigned)zc4, (unsigned)zc8, (unsigned)zero15, (unsigned)zero8, (unsigned)zero4);
    printf("polygon            as found              line, drawn          line, 0000h          on, drawn            on, 0000h\n");
    for (int p = 0; p < NP; p++) {
        printf("%-17s", P[p].name);
        printf(" %7u..%-7u", (unsigned)af[p][0], (unsigned)af[p][1]);
        for (int on = 0; on < 2; on++) for (int op = 1; op >= 0; op--) printf("  %7u..%-7u", (unsigned)r[p][on][op][0], (unsigned)r[p][on][op][1]);
        printf("\n");
    }
    printf("\ngpu/gpu-tex-gap: Done.\n");
    for (;;) VSync(0);
}
