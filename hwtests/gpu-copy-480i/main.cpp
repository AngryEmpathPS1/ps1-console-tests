/*
 * gpu/gpu-copy-480i - GP0(E1h) bit 10 clear in 480i: do a VRAM copy and a CPU upload skip rows, and what do they cost?
 *
 * WHY (2026-10-30 night). hwtests/gpu-fill-480i on the second SCPH-5501, x3: a fill, a rectangle, a triangle and a line each write
 * half the rows and take 48-49% of the time. psx-spx names those four. A VRAM copy (80h) and a CPU upload (A0h) are not in its list,
 * and this core lets them write every row at full price, as DuckStation's CopyVRAMImpl and mednafen's copy do, by silence:
 * neither has a field test there, and neither says it was measured. A 480i game copying a sprite into the field on screen is the case.
 *
 * WHAT IT DOES. gpu-fill-480i's set-up: interrupts off, Timer 1 counting hblanks from each VBlank edge, Timer 2 the system clock,
 * every wait bounded, 640x480 15-bit interlaced NTSC at (0,0), GPUSTAT.31 at line 60 naming the field.
 *   A  TIME. A copy of 512x240 from (0,256) to (0,0), and of 16x240 likewise, white over the sentinel, bit set and bit clear, four
 *      runs each, at PICTURE line 100 of each field. Cycles from the last word to bits 26+28, then column 0 read back.
 *   B  ROWS. A copy of 16x16 to y = 0, 1, 40, 41, and a CPU upload of 8x16 there, at line 100 of each field, E1h.10 clear; rows from
 *      y-2 read back, # written, . not.
 *
 * READ IT LIKE THIS. A: rows written 120 of 240 with the bit clear and the time about half is a copy that skips; 240 of 240 is one
 * that does not. B: .#.#.#.# in field 0 is the same rule; ################ is none. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-5501 node hwtests/run.js gpu-copy-480i --ntsc --raw --frames=2000`. PREDICTED.txt
 * has what this core read before any console ran it.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <timer.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define I_STAT   0x1F801070u
#define I_MASK   0x1F801074u
#define GP0 0x1F801810u
#define GP1 0x1F801814u
#define B13 0x00002000u
#define B26 0x04000000u
#define B27 0x08000000u
#define B28 0x10000000u
#define LIMIT 2000000u                  /* cycles: over three frames, past any draw here; every wait stops at it */
#define NONE 0xFFFFFFFFu
#define SENSE 60                        /* the line at which GPUSTAT.31 names the field */
#define SENTINEL 0x7C1Fu
#define INK 0x7FFFu

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static int waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & (B26 | B28)) != (B26 | B28)) if (now() - t0 > LIMIT) return 0;
    return 1;
}
static int waitEdge() {
    write32(I_STAT, ~1u);
    const uint32_t t0 = now();
    while (!(read32(I_STAT) & 1u)) if (now() - t0 > LIMIT) return 0;
    resetTimer(1);
    return 1;
}
static int toLine(uint32_t l) {
    const uint32_t t0 = now();
    while (readTimer(1) < l) if (now() - t0 > LIMIT) return 0;
    return 1;
}
/* to `line` of the field whose bit 31 reads `field` through the picture. Fields alternate, so a place before SENSE is the
 * field after one read the other way. 0 if the field never came: bit 31 not doing what it should, and said so. */
static int place(int field, uint32_t line) {
    for (int tries = 0; tries < 4; tries++) {
        if (!waitEdge() || !toLine(SENSE)) return 0;
        const int f = (int)(read32(GP1) >> 31);
        if (line >= SENSE) { if (f == field) return toLine(line); }
        else if (f != field) return waitEdge() && toLine(line);
    }
    return 0;
}
static void drawMode(int dfe) { waitIdle(); write32(GP0, 0xE1000000u | ((uint32_t)dfe << 10)); waitIdle(); }
static void sentinel(int x, int y, int w, int h) {
    waitIdle();
    write32(GP0, 0x02F800F8u); write32(GP0, xy(x, y)); write32(GP0, xy(w, h));
    waitIdle();
}

static uint16_t col[482] __attribute__((aligned(4)));
/* h rows of column x, by the CPU; 0 if the read never came ready */
static int readCol(int x, int y, int h) {
    waitIdle();
    write32(GP0, 0xC0000000u); write32(GP0, xy(x, y)); write32(GP0, xy(1, h));
    const uint32_t t0 = now();
    while (!(read32(GP1) & B27)) if (now() - t0 > LIMIT) return 0;
    for (int i = 0; i < h; i += 2) { const uint32_t v = read32(GP0); col[i] = (uint16_t)v; col[i + 1] = (uint16_t)(v >> 16); }
    return 1;
}

static uint32_t w[4];
static int prim(int k) {
    w[0] = 0x80000000u; w[1] = xy(0, 256); w[2] = xy(0, 0); w[3] = xy(k ? 16 : 512, 240); return 4;
}
static const char* const PRIM[2] = { "copy 512x240", "copy 16x240" };
static uint32_t timed(int n, uint32_t* s0, uint32_t* s1) {
    *s0 = read32(GP1);
    for (int i = 0; i < n; i++) write32(GP0, w[i]);
    const uint32_t t0 = now();
    for (;;) {
        const uint32_t s = read32(GP1), t = now() - t0;
        if ((s & (B26 | B28)) == (B26 | B28)) { *s1 = s; return t; }
        if (t > LIMIT) { *s1 = s; return NONE; }
    }
}

struct Cell { uint32_t lo, hi, s0, s1; int ok, even, odd, evenFirst, evenLast, oddFirst, oddLast, other; };

static Cell cell(int k, int dfe, int field, uint32_t line) {
    Cell c;
    c.lo = NONE; c.hi = 0; c.ok = 1; c.s0 = c.s1 = 0;
    for (int run = 0; run < 4; run++) {
        drawMode(1);
        sentinel(0, 0, 640, 240);
        waitIdle(); write32(GP0, 0x02FFFFFFu); write32(GP0, xy(0, 256)); write32(GP0, xy(640, 240)); waitIdle();
        drawMode(dfe);
        if (!place(field, line)) { c.ok = 0; break; }
        const uint32_t t = timed(prim(k), &c.s0, &c.s1);
        c.lo = t < c.lo ? t : c.lo; c.hi = t > c.hi ? t : c.hi;
    }
    c.even = c.odd = c.other = 0; c.evenFirst = c.oddFirst = c.evenLast = c.oddLast = -1;
    if (c.ok && readCol(0, 0, 240)) {
        for (int y = 0; y < 240; y++) {
            if (col[y] == SENTINEL) continue;
            if (col[y] != INK) { c.other++; continue; }
            if (y & 1) { if (c.oddFirst < 0) c.oddFirst = y; c.oddLast = y; c.odd++; }
            else { if (c.evenFirst < 0) c.evenFirst = y; c.evenLast = y; c.even++; }
        }
    } else c.ok = 0;
    return c;
}
static void showCell(const char* tag, const Cell& c) {
    if (!c.ok) { printf("  %s NEVER REACHED ITS PLACE, OR ITS READ NEVER CAME\n", tag); return; }
    printf("  %s %7u..%-7u bit 31 %d->%d, 13 %d->%d  even rows %3d (%d..%d) odd %3d (%d..%d)%s\n", tag, (unsigned)c.lo, (unsigned)c.hi,
           (int)(c.s0 >> 31), (int)(c.s1 >> 31), (c.s0 & B13) ? 1 : 0, (c.s1 & B13) ? 1 : 0, c.even, c.evenFirst, c.evenLast, c.odd, c.oddFirst, c.oddLast,
           c.other ? "  OTHER COLOURS" : "");
}

/* part B: one row of the table */
static char pat[21];
static void rowsRow(int k, int y, int field) {
    const int from = y >= 2 ? y - 2 : 0;
    drawMode(1);
    sentinel(0, from, 640, 32);
    waitIdle(); write32(GP0, 0x02FFFFFFu); write32(GP0, xy(0, 256)); write32(GP0, xy(640, 32)); waitIdle();
    drawMode(0);
    if (!place(field, 100)) { printf("%-7s y %2d field %d: NEVER REACHED ITS PLACE\n", k ? "upload" : "copy", y, field); return; }
    uint32_t s0 = read32(GP1);
    if (k == 0) { write32(GP0, 0x80000000u); write32(GP0, xy(0, 256)); write32(GP0, xy(0, y)); write32(GP0, xy(16, 16)); }
    else {
        write32(GP0, 0xA0000000u); write32(GP0, xy(0, y)); write32(GP0, xy(8, 16));
        for (int i = 0; i < 64; i++) { const uint32_t t0 = now(); while (!(read32(GP1) & B28) && now() - t0 < LIMIT) {} write32(GP0, 0x7FFF7FFFu); }
    }
    waitIdle();
    const uint32_t s1 = read32(GP1);
    drawMode(1);
    pat[0] = 0;
    if (readCol(0, from, 20)) { for (int i = 0; i < 20; i++) pat[i] = col[i] == SENTINEL ? '.' : col[i] == INK ? '#' : '?'; pat[20] = 0; }
    printf("%-7s y %2d field %d: bit 31 %d->%d, rows from %2d: %s\n", k ? "upload" : "copy", y, field, (int)(s0 >> 31), (int)(s1 >> 31), from, pat);
}

int main() {
    printf("\ngpu/gpu-copy-480i\n");
    printf("640x480 interlaced, GP0(E1h) bit 10 set against clear. Cycles from the last word to bits 26+28, fastest..slowest of four,\n");
    printf("then the rows written. Field 1 reads GPUSTAT.31 = 1 through the picture, field 0 reads 0. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t stat0 = read32(GP1);
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t prev1 = initTimer(1, 1);    /* hblanks */
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    write32(GP1, 0x05000000u);                                  /* display start (0,0): parity is the field alone */
    write32(GP1, 0x06000000u | ((0x260u + 2560u) << 12) | 0x260u);
    write32(GP1, 0x07000000u | ((16u + 240u) << 10) | 16u);
    write32(GP1, 0x08000027u);                                  /* 640x480, interlaced, NTSC, 15-bit */
    write32(GP1, 0x03000000u);
    waitEdge(); waitEdge(); waitEdge();

    printf("A. TIME, cycles fastest..slowest, and the rows written of column 0\n");
    static const struct { const char* name; int field; int line; } PL[2] = { { "PICTURE line 100, field 1", 1, 100 }, { "PICTURE line 100, field 0", 0, 100 } };
    for (int p = 0; p < 2; p++) {
        printf("%s:\n", PL[p].name);
        for (int k = 0; k < 2; k++) {
            const Cell set = cell(k, 1, PL[p].field, PL[p].line), clear = cell(k, 0, PL[p].field, PL[p].line);
            printf(" %s\n", PRIM[k]);
            showCell("bit set  ", set);
            showCell("bit clear", clear);
            if (set.ok && clear.ok && set.lo) printf("  clear/set %u%%, rows written with the bit clear %d of 240\n", (unsigned)(clear.lo * 100 / set.lo), clear.even + clear.odd);
        }
    }

    printf("\nB. ROWS, E1h.10 clear, line 100 of each field: # written, . not, ? another colour\n");
    static const int YS[4] = { 0, 1, 40, 41 };
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++)
            for (int f = 0; f < 2; f++) rowsRow(k, YS[i], f);

    drawMode(1);
    write32(GP1, 0x08000000u | ((stat0 >> 17) & 0x3F) | (((stat0 >> 16) & 1) << 6) | (((stat0 >> 14) & 1) << 7));
    write32(GP1, 0x03000000u | ((stat0 >> 23) & 1));
    restoreTimer(1, prev1);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gpu-copy-480i: Done.\n");
    for (;;) {}
}
