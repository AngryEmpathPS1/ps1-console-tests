/*
 * gpu/gpu-irq-chain - amidog's IRQ demo's own GPU chain, timed (2026-10-09 night).
 *
 * WHY. amidog's "IRQ" and "No code" demos (tests/amidog/misc, "a silly demo which abuses the GPU caches and IRQ feature")
 * bounce a 64x64 box and pace every step by the GPU itself: on a GPU interrupt the handler writes the next command and
 * GP0(1Fh) behind it - an interrupt the GPU raises when it reaches the 1Fh, which is when the command before it has
 * finished - and acknowledges (GP1(02h)). Its steps (tools trace, scratch_boxtrace.js): two 3Eh quads from (320,0) to
 * (1023,511) - 704x512 pixels to the right of the picture, semi-transparent, gouraud, 4-bit, every UV 0 (one texel,
 * tpage 0, CLUT (0,256)) - which draw nothing anyone sees and are the demo's DELAY; then the box, a 64h 64x64 sprite; then
 * two GP0(02h) fills clearing the box's old rows. On this core the box tears on ~15% of frames read line by line as a
 * TV does (scratch_boxhash.js SCAN=1); Pipe line and Where am I?, VSync-paced, never. How long each step takes on a
 * console is what decides where the box lands against the beam - it is the GPU's textured fill rate (v0.377.69), here
 * timed step by step with the demo's own words.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock (unwrapped), the GPU interrupt polled in I_STAT bit 1. The
 * demo's drawing state: area (0,0)-(1023,511), no offset, tpage 0 4-bit, semi mode 0. The one texel every UV-0 pixel
 * reads is VRAM (0,0)'s low nibble, index 0 (filled black), through CLUT (0, 256 + case): a fresh CLUT row each case so
 * the CLUT cache reloads (a fill does not refresh it, M100). Cases: TRANSPARENT (entry 0 = 0000h), DRAWN (7FFFh, STP
 * clear: semi draws it opaque) and BLEND (FFFFh, STP set: semi blends). Each row: the command's words and GP0(1Fh), then
 * cycles from the first word to the interrupt, fastest..slowest of four (GPUSTAT bit 26 beside it).
 *   CONTROL     GP0(1Fh) alone: the interrupt's own latency
 *   per case    3Eh the demo's quad; 3Ch the same not semi; 64h the box sprite at (128,90); 2Eh / 2Ch the quad flat-
 *               shaded (its gouraud cost)
 *   FILLS       02h 1024x90 at (0,0) and 1024x358 at (0,154): the demo's two clears
 *   CHAIN       the demo's cycle - 3Eh, 3Eh, 64h, 02h, 02h, each with its 1Fh - six times back to back as its handler
 *               runs it (the next command written at the interrupt), every step's time and the cycle's; TRANSPARENT
 *               and DRAWN. The demo's second fill clears rows 154-511, its own CLUT row 256 among them: the CLUT
 *               CACHE, never refreshed by a fill (M100), is what keeps the box's palette - the "GPU caches" it abuses.
 *
 * READ IT LIKE THIS. A step's interrupt time is that command's drawing time plus the 1Fh's latency (CONTROL). The
 * CHAIN's cycle against a frame (566,107.5 CPU cycles on an NTSC one-oscillator board) says whether the box's update
 * walks across the picture or holds still.
 *
 * There are no assertions - compare against your emulator.
 *
 * THE RULE: run here first. ON THIS CORE AT v0.377.69 (the textured costs from three consoles): CONTROL 51..79; the demo
 * quad 362,755 transparent and 438,929 drawn (semi on a drawn texel), 362,748 not semi either way; flat-shaded the same
 * less ~200; the sprite 1,840 warm (4,671 with its cache fill); the fills 6,816 and 26,746; the CHAIN's cycle 761,240
 * transparent, 914,024 drawn (6 cycles each, steady). On v0.377.67's costs (no semi or write charge): every quad
 * 355,655 and the cycle 749,734 either way, the sprite 4,495. A frame is 566,107.5 cycles (NTSC, one oscillator).
 * `node hwtests/run.js gpu-irq-chain --frames=4000 --quiet=3000`
 * THE CONSOLE (the SCPH-7501, 2026-10-10, three runs; console-logs/2026-10-10-SCPH-7501-night): the demo quad over a
 * transparent texel 361,383 cycles (here 362,756), the demo's whole cycle 759,929 (here 761,240) - within 0.2%; over DRAWN
 * texels the semi quad (3Eh, 2Eh) +19,246 cycles on its opaque twin, here +76,173: the semi charge a drawn texel 4x too
 * high for this one-texel 704x512 quad, against gpu-tex-opaque's 256x256 rows that fit it (hwtests/gpu-semi-shape asks
 * by the pixel or by the row). The box sprite 2,259 (here 1,851); the fills 7,016 / 27,662 (here 6,839 / 26,804).
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define ISTAT 0x1F801070u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }

static void waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & 0x14000000u) != 0x14000000u && now() - t0 < 40000000u) {}
}
static void ackIrq() { write32(GP1, 0x02000000u); write32(ISTAT, ~2u); }

struct Tm { uint32_t irq, t26; };
/* The words, then GP0(1Fh); cycles from the first word to I_STAT bit 1 (40M cycles at most: 0xFFFFFFFF), and to GPUSTAT
 * bit 26 on the way. */
static Tm drawIrq(const uint32_t* w, int n) {
    waitIdle(); ackIrq();
    const uint32_t t0 = now();
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    write32(GP0, 0x1F000000u);
    Tm r = { 0xFFFFFFFFu, 0 };
    bool s26 = false;
    for (;;) {
        const uint32_t t = now() - t0;
        if (!s26 && (read32(GP1) & 0x04000000u)) { s26 = true; r.t26 = t; }
        if (read32(ISTAT) & 2u) { r.irq = t; break; }
        if (t > 40000000u) break;
    }
    ackIrq();
    return r;
}
static void row(const uint32_t* w, int n) {
    uint32_t lo = 0xFFFFFFFFu, hi = 0, lo26 = 0xFFFFFFFFu, hi26 = 0;
    for (int k = 0; k < 4; k++) {
        const Tm t = drawIrq(w, n);
        lo = t.irq < lo ? t.irq : lo; hi = t.irq > hi ? t.irq : hi;
        lo26 = t.t26 < lo26 ? t.t26 : lo26; hi26 = t.t26 > hi26 ? t.t26 : hi26;
    }
    printf("  irq %8u..%-8u  bit 26 %8u..%u\n", (unsigned)lo, (unsigned)hi, (unsigned)lo26, (unsigned)hi26);
}

static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static uint32_t W[16];

/* GP0(02h) with the 24-bit colour whose 15-bit truncation is v (bit 15 clear) */
static void fill15(int x, int y, int w, int h, uint32_t v) {
    const uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
    waitIdle();
    write32(GP0, 0x02000000u | (b << 16) | (g << 8) | r);
    write32(GP0, xy(x, y));
    write32(GP0, xy(w, h));
    waitIdle();
}
/* one CLUT entry by GP0(A0h) (a fill cannot set bit 15) */
static void clutEntry(int y, uint16_t v) {
    waitIdle();
    write32(GP0, 0xA0000000u);
    write32(GP0, xy(0, y));
    write32(GP0, xy(2, 1));
    write32(GP0, (uint32_t)v | ((uint32_t)v << 16));
    waitIdle();
}

static uint32_t clutWord(int y) { return (uint32_t)((y << 6) | 0) << 16; }
/* the demo's quad: (320,0) (320,511) (1023,0) (1023,511), white, every UV 0; cmd 3Eh / 3Ch / 2Eh / 2Ch */
static int demoQuad(uint32_t cmd, int clutY) {
    const bool g = (cmd & 0x10) != 0;
    int n = 0;
    W[n++] = (cmd << 24) | 0xFFFFFFu;
    W[n++] = xy(320, 0);   W[n++] = clutWord(clutY);
    if (g) W[n++] = 0xFFFFFFu;
    W[n++] = xy(320, 511); W[n++] = 0u;                         /* tpage 0, semi mode 0, 4-bit */
    if (g) W[n++] = 0xFFFFFFu;
    W[n++] = xy(1023, 0);  W[n++] = 0u;
    if (g) W[n++] = 0xFFFFFFu;
    W[n++] = xy(1023, 511); W[n++] = 0u;
    return n;
}
static int sprite(int clutY) {
    W[0] = 0x64808080u; W[1] = xy(128, 90); W[2] = clutWord(clutY); W[3] = xy(64, 64);
    return 4;
}
static int fillCmd(int y, int h) { W[0] = 0x02000000u; W[1] = xy(0, y); W[2] = xy(0x3FF, h); return 3; }

int main() {
    printf("\ngpu/gpu-irq-chain\n");
    printf("amidog's IRQ demo's GPU chain: each command, then GP0(1Fh); cycles from the first word to the GPU interrupt,\n");
    printf("fastest..slowest of four (GPUSTAT bit 26 beside it). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint32_t imask = read32(0x1F801074u);

    write32(GP1, 0x01000000u);                                  /* reset the command buffer */
    write32(GP1, 0x04000000u);                                  /* no DMA */
    write32(GP0, 0xE1000000u | (1u << 10));                     /* tpage 0, 4-bit, semi 0, drawing to the display allowed */
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    fill15(0, 0, 16, 1, 0);                                     /* VRAM (0,0): index 0 for every UV-0 texel */

    printf("CONTROL:\n");
    printf("  1Fh alone                      "); row(W, 0);
    static const char* const cn[3] = { "TRANSPARENT (CLUT entry 0 = 0000h)", "DRAWN (7FFFh, STP clear)", "BLEND (FFFFh, STP set)" };
    static const uint16_t cv[3] = { 0x0000u, 0x7FFFu, 0xFFFFu };
    for (int c = 0; c < 3; c++) {
        const int cy = 256 + c;
        clutEntry(cy, cv[c]);
        printf("%s:\n", cn[c]);
        printf("  3Eh demo quad 704x512          "); row(W, demoQuad(0x3E, cy));
        printf("  3Ch the same, not semi         "); row(W, demoQuad(0x3C, cy));
        printf("  2Eh flat-shaded, semi          "); row(W, demoQuad(0x2E, cy));
        printf("  2Ch flat-shaded                "); row(W, demoQuad(0x2C, cy));
        printf("  64h box sprite 64x64           "); row(W, sprite(cy));
    }
    printf("FILLS:\n");
    printf("  02h 1024x90 at (0,0)           "); row(W, fillCmd(0, 90));
    printf("  02h 1024x358 at (0,154)        "); row(W, fillCmd(154, 358));
    fill15(0, 0, 16, 1, 0);

    /* THE CHAIN: as the demo's handler runs it - the next command written the moment the interrupt shows. Twice: the
       texel TRANSPARENT and DRAWN, the CLUT row written just before, so the first command loads it into the CLUT cache -
       and the cache keeps it through the demo's second fill, which clears the CLUT's VRAM row as the demo's does. */
    for (int c = 0; c < 2; c++) {
        const int cy = 256 + c;
        clutEntry(cy, cv[c]);
        fill15(0, 0, 16, 1, 0);
        printf("CHAIN, %s (the demo's cycle; cycles each step from the interrupt before it):\n", cn[c]);
        uint32_t steps[6][5], cyc[6];
        waitIdle(); ackIrq();
        uint32_t tPrev = now();
        for (int k = 0; k < 6; k++) {
            const uint32_t tCycle = tPrev;
            for (int st = 0; st < 5; st++) {
                const int n = st < 2 ? demoQuad(0x3E, cy) : st == 2 ? sprite(cy) : st == 3 ? fillCmd(0, 90) : fillCmd(154, 358);
                for (int i = 0; i < n; i++) write32(GP0, W[i]);
                write32(GP0, 0x1F000000u);
                write32(GP1, 0x02000000u); write32(ISTAT, ~2u);     /* the demo acknowledges the interrupt that started it */
                uint32_t t;
                for (;;) { t = now(); if (read32(ISTAT) & 2u) break; if (t - tPrev > 40000000u) break; }
                steps[k][st] = t - tPrev; tPrev = t;
            }
            cyc[k] = tPrev - tCycle;
        }
        ackIrq();
        for (int k = 0; k < 6; k++)
            printf("  cycle %d: 3Eh %7u  3Eh %7u  64h %6u  02h %6u  02h %6u   cycle %7u\n", k, (unsigned)steps[k][0],
                   (unsigned)steps[k][1], (unsigned)steps[k][2], (unsigned)steps[k][3], (unsigned)steps[k][4], (unsigned)cyc[k]);
    }

    write32(0x1F801074u, imask);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nDone.\n");
    for (;;) {}   /* park after Done. (console day: a program that returns leaves Unirom deaf) */
}
