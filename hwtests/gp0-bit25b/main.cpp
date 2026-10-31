/*
 * gpu/gp0-bit25b - how many of a running primitive's own words are still counted in the GP0 FIFO?
 *
 * WHY (2026-10-30 night). hwtests/gp0-bit25 on the second SCPH-5501, x3: behind a copy or a fill the 5-bit count wraps at the 33rd
 * E6h word, one word taken; behind a 256x256 flat quad it wraps at the 28th, so 4 words were queued before the first E6h and bit 25
 * falls at word 5. psx-spx's "FIFO Prefetch" says a quad leaves its last vertex there until its first half is drawn: one word. This
 * core has none, and a DMA block's request reads this count. Is it 4 for every quad, the words of the second triangle, or all of a
 * polygon's vertices, and do a triangle, a rectangle and a line leave any?
 *
 * WHAT IT DOES. gp0-bit25's 48-word row, its pace, its reads. Interrupts off, Timer 2 at the system clock, every wait bounded.
 * GP1(04h)=1, the busy command, 200 cycles, then 48 GP0(E6h) words, GPUSTAT read after each, three runs of each. Busy commands, all
 * 256x256 and far longer than the words take, so W is read under the first half of a quad: flat 20h / 28h, shaded 30h / 38h,
 * textured 24h / 2Ch, shaded textured 34h / 3Ch, semi flat quad 2Ah, and 60h 512x400, a textured 64h 256x256. A flat line 1023
 * long, 40h, at a tighter pace (it lasts a few thousand cycles). The copy is the control: F 10, W 33, F2 42, as in gp0-bit25.
 *
 * READ IT LIKE THIS. taken = W - 32: positive words off the FIFO, negative words of the busy command itself still counted, as in
 * gp0-bit25. T = F2 - W must read 9 and F - taken must equal it, or the row is not reading the count. Then a command's own count
 * is -(taken), less one if an E6h is held. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gp0-bit25b --ntsc --raw --frames=4000`. PREDICTED.txt
 * has what this core read before any console ran it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_MASK   0x1F801074u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define GP0W (*(volatile uint32_t*)0x1F801810u)
#define GPUSTAT (*(volatile uint32_t*)0x1F801814u)
#define B25 0x02000000u
#define B26 0x04000000u
#define B28 0x10000000u
#define LIMIT 800000u                   /* cycles: well past every busy command here; every wait stops at it */

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static int waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & (B26 | B28)) != (B26 | B28)) if (now() - t0 > LIMIT) return 0;
    return 1;
}
/* about 2 cycles a turn, from the I-cache */
static inline void spin(uint32_t n) {
    __asm__ volatile(".set noreorder\n1:\nbnez %0, 1b\naddiu %0, %0, -1\n.set reorder\n" : "+r"(n));
}

struct Kind { const char* name; int n; uint32_t w[12]; uint32_t gap; };
static Kind K[14];
static int NK = 0;
static Kind& add(const char* nm, uint32_t gap) { Kind& k = K[NK++]; k.name = nm; k.n = 0; k.gap = gap; return k; }
static void W(Kind& k, uint32_t v) { k.w[k.n++] = v; }

static const uint32_t COL = 0x102030u, TEX = 0x808080u;
static const uint32_t CLUT = (480u << 6) | 0u;
static const uint32_t TPAGE = 8u | (1u << 4) | (2u << 7);   /* X 512, Y 256, 15-bit */
static uint32_t uv(int u, int v, uint32_t hi) { return (hi << 16) | ((uint32_t)(v & 0xFF) << 8) | (uint32_t)(u & 0xFF); }

static void flat(const char* nm, uint32_t c, int verts) {
    Kind& k = add(nm, 50);
    W(k, c | COL); W(k, xy(0, 0)); W(k, xy(255, 0)); W(k, xy(0, 255)); if (verts == 4) W(k, xy(255, 255));
}
static void shaded(const char* nm, uint32_t c, int verts) {
    Kind& k = add(nm, 50);
    W(k, c | COL); W(k, xy(0, 0)); W(k, COL); W(k, xy(255, 0)); W(k, COL); W(k, xy(0, 255));
    if (verts == 4) { W(k, COL); W(k, xy(255, 255)); }
}
static void textured(const char* nm, uint32_t c, int verts) {
    Kind& k = add(nm, 50);
    W(k, c | TEX); W(k, xy(0, 0)); W(k, uv(0, 0, CLUT)); W(k, xy(255, 0)); W(k, uv(255, 0, TPAGE)); W(k, xy(0, 255)); W(k, uv(0, 255, 0));
    if (verts == 4) { W(k, xy(255, 255)); W(k, uv(255, 255, 0)); }
}
static void shadedTex(const char* nm, uint32_t c, int verts) {
    Kind& k = add(nm, 50);
    W(k, c | TEX); W(k, xy(0, 0)); W(k, uv(0, 0, CLUT)); W(k, TEX); W(k, xy(255, 0)); W(k, uv(255, 0, TPAGE)); W(k, TEX); W(k, xy(0, 255)); W(k, uv(0, 255, 0));
    if (verts == 4) { W(k, TEX); W(k, xy(255, 255)); W(k, uv(255, 255, 0)); }
}
static void defs() {
    { Kind& k = add("copy (control)", 50); W(k, 0x80000000u); W(k, xy(0, 256)); W(k, xy(512, 256)); W(k, xy(256, 128)); }
    flat("flat tri 20h", 0x20000000u, 3); flat("flat quad 28h", 0x28000000u, 4); flat("semi quad 2Ah", 0x2A000000u, 4);
    shaded("shaded tri 30h", 0x30000000u, 3); shaded("shaded quad 38h", 0x38000000u, 4);
    textured("tex tri 24h", 0x24000000u, 3); textured("tex quad 2Ch", 0x2C000000u, 4);
    shadedTex("shaded tex tri 34h", 0x34000000u, 3); shadedTex("shaded tex quad 3Ch", 0x3C000000u, 4);
    { Kind& k = add("rect 60h 512x400", 50); W(k, 0x60000000u | COL); W(k, xy(0, 0)); W(k, xy(512, 400)); }
    { Kind& k = add("tex rect 64h 256", 50); W(k, 0x64000000u | TEX); W(k, xy(0, 0)); W(k, uv(0, 0, CLUT)); W(k, xy(256, 256)); }
    { Kind& k = add("line 40h 1023", 0); W(k, 0x40000000u | COL); W(k, xy(0, 0)); W(k, xy(1023, 511)); }
}

static uint32_t st[48];
static char s25[49], s28[49], first25[49], first28[49];
static int same(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

struct Row { uint32_t before, tWords, tIdle; int stale, fall, wrap, fall2; };

static Row trial(const Kind& k) {
    Row r;
    r.stale = !waitIdle();
    write32(GP1, 0x04000001u);          /* direction 1: bit 25 is "FIFO not more than half full" */
    for (int i = 0; i < k.n; i++) write32(GP0, k.w[i]);
    spin(100);                          /* 200 cycles: the command is running, its words taken or not */
    r.before = read32(GP1);
    const uint32_t tw = now();
    if (k.gap) { for (int i = 0; i < 48; i++) { GP0W = 0xE6000000u; spin(k.gap); st[i] = GPUSTAT; } }
    else { for (int i = 0; i < 48; i++) { GP0W = 0xE6000000u; st[i] = GPUSTAT; } }
    const uint32_t t0 = now();
    r.tWords = t0 - tw;
    r.tIdle = 0xFFFFFFFFu;
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if ((s & (B26 | B28)) == (B26 | B28)) { r.tIdle = t; break; }
        if (t > LIMIT) break;
    }
    write32(GP1, 0x01000000u);          /* an overrun leaves the FIFO's count wrong: clear it */
    write32(GP1, 0x04000000u);
    r.fall = r.wrap = r.fall2 = 0;
    for (int i = 0; i < 48; i++) {
        const uint32_t s = st[i];
        s25[i] = (s & B25) ? '1' : '0'; s28[i] = (s & B28) ? '1' : '0';
        if (!r.fall) { if (!(s & B25)) r.fall = i + 1; }
        else if (!r.wrap) { if (s & B25) r.wrap = i + 1; }
        else if (!r.fall2) { if (!(s & B25)) r.fall2 = i + 1; }
    }
    s25[48] = s28[48] = 0;
    return r;
}

int main() {
    printf("\ngpu/gp0-bit25b\n");
    printf("GP1(04h)=1: GPUSTAT.25 and 28 after each of 48 GP0(E6h) words behind a long primitive. Three runs. No assertions.\n\n");
    defs();
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u | TPAGE);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    if (!waitIdle()) printf("*** the GPU never read idle at the start ***\n");

    for (int k = 0; k < NK; k++) {
        printf("%s, %d words:\n", K[k].name, K[k].n);
        for (int run = 1; run <= 3; run++) {
            const Row r = trial(K[k]);
            if (r.wrap && r.fall2)
                printf(" run %d: F %d, W %d, F2 %d: taken %d, T %d, F-taken %d%s; before 25/26/28 %d%d%d, words took %u, idle %u%s\n", run, r.fall, r.wrap, r.fall2,
                       r.wrap - 32, r.fall2 - r.wrap, r.fall - (r.wrap - 32), r.fall - (r.wrap - 32) == r.fall2 - r.wrap ? "" : " *** DISAGREE ***",
                       (int)((r.before >> 25) & 1), (int)((r.before >> 26) & 1), (int)((r.before >> 28) & 1), (unsigned)r.tWords, (unsigned)r.tIdle, r.stale ? "  *** NOT IDLE BEFORE ***" : "");
            else
                printf(" run %d: F %d, W %d, F2 %d: no wrap seen; before 25/26/28 %d%d%d, words took %u, idle %u%s\n", run, r.fall, r.wrap, r.fall2,
                       (int)((r.before >> 25) & 1), (int)((r.before >> 26) & 1), (int)((r.before >> 28) & 1), (unsigned)r.tWords, (unsigned)r.tIdle, r.stale ? "  *** NOT IDLE BEFORE ***" : "");
            if (run == 1 || !same(s25, first25) || !same(s28, first28)) printf("  25 %s\n  28 %s\n", s25, s28);
            if (run == 1) for (int i = 0; i <= 48; i++) { first25[i] = s25[i]; first28[i] = s28[i]; }
        }
    }

    waitIdle();
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE6000000u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gp0-bit25b: Done.\n");
    for (;;) VSync(0);
}
