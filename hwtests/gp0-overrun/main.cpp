/*
 * gpu/gp0-overrun - DMA2 into a full GP0 FIFO: how many words of a linked-list node, or of a DMA block, survive?
 *
 * WHY (2026-10-30). psx-spx's dev text gives four points and no threshold: GP1(04h)=2, a 12-word list node is transferred
 * intact and a 240-word node loses words; GP1(04h)=1, block mode, 16-word blocks lose words and 1-word blocks are exact.
 * This core loses nothing: a DMA word at a full FIFO waits in a spill list. Three US discs reach it (In the Hunt 1,089 spilled
 * words, Roadsters 106, Code Breaker 104). hwtests/fifo-burst settled the CPU's case (the words are lost, no wait); the DMA's
 * was left open because Sony says the last primitive will be broken and MiSTer's author says games send more than 16 by DMA.
 *
 * WHAT IT DOES. Interrupts off, Timer 2 at the system clock, every wait bounded. One-pixel dots, GP0(68h), two words each,
 * each its own colour at its own X on row 490, so a dot found is that dot and a dot in another's colour shows a word lost
 * from the middle of a pair. An odd N ends with a 3-word 1x1 GP0(60h), so every length can be asked for; at the end, because
 * the newest words are the last an overrun touches (first, its size word was overwritten and a scratch core drew a slab
 * 8 x 128 over the dots). Behind the slow command: a 640x480 fill written by the CPU a few cycles before the DMA starts (about 25,000 cycles: nothing behind it
 * runs until it ends, and row 490 is outside it). A trial: both rows painted, the GPU idle, the DMA direction, then the DMA;
 * cycles to CHCR bit 24 clear and to bits 26+28; GP1(01h), as a word lost can leave a command waiting for its parameters;
 * then the rows read back. Three trials of each.
 *   PACED     the control: 64 dots by the CPU, each once bit 26 reads set. Every machine draws all 64 or this run says nothing.
 *   LIST      GP1(04h)=2, CHCR 01000401h: one node of N words behind the fill, N = 12..64, 128, 240.
 *   LIST IDLE the same with no fill, N = 12, 16, 24, 32, 48, 64, 128, 240: dots are slower than the DMA, so an idle GPU may
 *             not keep up either. A measurement, not a control (fifo-burst's IDLE lost dots to a CPU).
 *   BLOCK     GP1(04h)=1, CHCR 01000201h: 128 words of dots in blocks of 1 to 10, 12 and 16 words behind the fill, and the
 *             same with no fill; the last block padded with GP0(00h). The request is bit 25, so a block can start with T-1
 *             words queued, T being what hwtests/gp0-bit25 reads: the first block size to lose words is 18-T if the FIFO
 *             holds 16, a second reading of T that does not go through bit 25's pattern (T 9: size 9; T 11: size 7).
 *
 * READ IT LIKE THIS. A line per length: the dots landed in each trial, wrong-coloured (a dot's place holding another dot's
 * colour), strays (pixels off the dots' places), the DMA's cycles and the time to idle, then a map of the first trial, one
 * character a dot, # landed, . missing, x wrong colour. The last N where every dot lands is the threshold psx-spx does not
 * give. Loss need not grow with N: nocash's FIFO size is a 5-bit count, so past 32 queued words it wraps and whole runs of
 * words can vanish, and an overrun word is processed twice. The map is the fingerprint of that, and fifo-burst's CPU row
 * (9 dots of 64) is the same FIFO fed slower. A wrong colour or a stray means pairs fell out of step: a colour word taken as
 * a position lands at row 128, a position word taken as a command is GP0(01h), harmless, which is why the dots are on a
 * row with bit 8 set. The DMA's cycles say whether it waited for the fill. No assertions.
 *
 * THE RULE: run it here first: `PSX_MODEL=SCPH-7501 node hwtests/run.js gp0-overrun --raw --frames=4000`. PREDICTED.txt
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
#define DPCR    0x1F8010F0u
#define D2_MADR 0x1F8010A0u
#define D2_BCR  0x1F8010A4u
#define D2_CHCR 0x1F8010A8u
#define B26 0x04000000u
#define B27 0x08000000u
#define B28 0x10000000u
#define LIMIT 200000u                   /* cycles: eight fills; every wait stops at it */
#define NONE 0xFFFFFFFFu

#define DOT_X 640
#define DOT_Y 490
#define STRAY_Y 128                     /* where a colour word taken as a position lands */
#define MAXDOTS 124
#define BIGFILL 0x02F800F8u             /* the 640x480 fill, in the sentinel's colour: it paints row 128 too */
#define SENTINEL 0x7C1Fu                /* 15-bit magenta: no dot is this colour */

/* Dot i's colour: red i & 31, green 1 to 4, blue 16 - distinct, never the sentinel (fifo-burst's). */
#define RGB(i) ((uint32_t)((((i) & 31) << 3) | ((((i) >> 5) + 1) << 11) | (0x80u << 16)))
#define PIX(i) ((uint16_t)(((i) & 31) | ((((i) >> 5) + 1) << 5) | (16 << 10)))

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0xFFFF) << 16) | (uint32_t)(x & 0xFFFF); }
static int waitIdle() {
    const uint32_t t0 = now();
    while ((read32(GP1) & (B26 | B28)) != (B26 | B28)) if (now() - t0 > LIMIT) return 0;
    return 1;
}
static void pause(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint32_t words[260] __attribute__((aligned(4)));   /* the node: its header, then N words */
static uint16_t row[260] __attribute__((aligned(4)));

/* N words of dots at p, whole commands; returns the dots */
static int build(uint32_t* p, int N) {
    int i = 0, d = 0;
    while (N - i >= 2 && N - i != 3) { p[i++] = 0x68000000u | RGB(d); p[i++] = xy(DOT_X + d, DOT_Y); d++; }
    if (N - i == 3) { p[i++] = 0x60000000u | RGB(d); p[i++] = xy(DOT_X + d, DOT_Y); p[i++] = xy(1, 1); d++; }
    return d;
}
static void paint() {
    waitIdle();
    write32(GP0, BIGFILL); write32(GP0, xy(DOT_X, DOT_Y)); write32(GP0, xy(256, 1));
    waitIdle();
    write32(GP0, BIGFILL); write32(GP0, xy(0, STRAY_Y)); write32(GP0, xy(256, 1));
    waitIdle();
}
static void bigFill() { write32(GP0, BIGFILL); write32(GP0, xy(0, 0)); write32(GP0, xy(640, 480)); }

/* w pixels of one row by the CPU, w even; 0 if the read never came ready */
static int readRow(int x, int y, int w) {
    write32(GP0, 0xC0000000u); write32(GP0, xy(x, y)); write32(GP0, xy(w, 1));
    const uint32_t t0 = now();
    while (!(read32(GP1) & B27)) if (now() - t0 > LIMIT) return 0;
    for (int i = 0; i < w; i += 2) { const uint32_t v = read32(GP0); row[i] = (uint16_t)v; row[i + 1] = (uint16_t)(v >> 16); }
    return 1;
}

struct Res { int landed, wrong, strays, hung, stuck, bad; uint32_t tDma, tIdle, madr, bcr; };
static char map[MAXDOTS + 1];

/* the rows read back, against `dots` dots */
static void score(Res& r, int dots) {
    r.landed = r.wrong = r.strays = 0;
    if (!readRow(DOT_X, DOT_Y, 256)) { r.bad = 1; return; }
    for (int i = 0; i < 256; i++) {
        const uint16_t v = row[i];
        if (i >= dots) { if (v != SENTINEL) r.strays++; continue; }
        if (v == PIX(i)) { r.landed++; map[i] = '#'; }
        else if (v == SENTINEL) map[i] = '.';
        else { r.wrong++; map[i] = 'x'; }
    }
    map[dots < MAXDOTS ? dots : MAXDOTS] = 0;
    if (!readRow(0, STRAY_Y, 256)) { r.bad = 1; return; }
    for (int i = 0; i < 256; i++) if (row[i] != SENTINEL) r.strays++;
}

/* to the DMA's end, to idle, and the GPU's parser and the channel left clean */
static void finish(Res& r, uint32_t t0) {
    r.hung = r.stuck = 0;
    r.tDma = r.tIdle = NONE;
    for (;;) {
        const uint32_t t = now() - t0;
        if (!(read32(D2_CHCR) & 0x01000000u)) { r.tDma = t; break; }
        if (t > LIMIT) { r.hung = 1; break; }
    }
    if (!r.hung) {
        const uint32_t t1 = now();
        for (;;) {
            const uint32_t t = now() - t1;
            if ((read32(GP1) & (B26 | B28)) == (B26 | B28)) { r.tIdle = r.tDma + t; break; }
            if (t > LIMIT) { r.stuck = 1; break; }
        }
    }
    r.madr = read32(D2_MADR); r.bcr = read32(D2_BCR);
    if (r.hung) write32(D2_CHCR, 0);
    write32(GP1, 0x01000000u);          /* a word lost can leave a command waiting for its parameters */
    write32(GP1, 0x04000000u);
    pause(2000);
}

static Res listTrial(int N, int fill) {
    Res r;
    r.bad = 0;
    words[0] = ((uint32_t)N << 24) | 0x00FFFFFFu;
    const int dots = build(words + 1, N);
    paint();
    write32(GP1, 0x04000002u);          /* the request is bit 28, the FIFO empty */
    write32(D2_MADR, (uint32_t)words & 0x00FFFFFFu);
    write32(D2_BCR, 0);
    if (fill) bigFill();
    const uint32_t t0 = now();
    write32(D2_CHCR, 0x01000401u);
    finish(r, t0);
    score(r, dots);
    return r;
}

static Res blockTrial(int bs, int fill) {
    Res r;
    r.bad = 0;
    const int blocks = (128 + bs - 1) / bs;
    for (int i = 0; i < 260; i++) words[i] = 0;
    const int dots = build(words, 128);
    paint();
    write32(GP1, 0x04000001u);          /* the request is bit 25, FIFO not more than half full */
    write32(D2_MADR, (uint32_t)words & 0x00FFFFFFu);
    write32(D2_BCR, ((uint32_t)blocks << 16) | (uint32_t)bs);
    if (fill) bigFill();
    const uint32_t t0 = now();
    write32(D2_CHCR, 0x01000201u);
    finish(r, t0);
    score(r, dots);
    return r;
}

static void note(const Res& r) {
    if (r.hung) printf("  DMA HUNG, MADR %08x BCR %08x", (unsigned)r.madr, (unsigned)r.bcr);
    if (r.stuck) printf("  GPU NEVER IDLE (a command waiting for its parameters)");
    if (r.bad) printf("  READ-BACK NEVER READY");
}

static void show(const char* name, int len, int dots, const Res* r) {
    printf("%s %3d, %2d dots: landed %2d %2d %2d, wrong %d %d %d, strays %d %d %d, DMA %d %d %d, idle %d %d %d  %s",
           name, len, dots, r[0].landed, r[1].landed, r[2].landed, r[0].wrong, r[1].wrong, r[2].wrong, r[0].strays, r[1].strays, r[2].strays,
           (int)r[0].tDma, (int)r[1].tDma, (int)r[2].tDma, (int)r[0].tIdle, (int)r[1].tIdle, (int)r[2].tIdle, map);
    for (int k = 0; k < 3; k++) note(r[k]);
    printf("\n");
}

/* three trials; the map kept is the first's */
static void listRow(const char* name, int N, int fill) {
    Res r[3];
    static char first[MAXDOTS + 1];
    for (int k = 0; k < 3; k++) {
        r[k] = listTrial(N, fill);
        if (k == 0) for (int i = 0; i <= MAXDOTS; i++) first[i] = map[i];
    }
    for (int i = 0; i <= MAXDOTS; i++) map[i] = first[i];
    show(name, N, (N & 1) ? (N - 3) / 2 + 1 : N / 2, r);
}
static void blockRow(const char* name, int bs, int fill) {
    Res r[3];
    static char first[MAXDOTS + 1];
    for (int k = 0; k < 3; k++) {
        r[k] = blockTrial(bs, fill);
        if (k == 0) for (int i = 0; i <= MAXDOTS; i++) first[i] = map[i];
    }
    for (int i = 0; i <= MAXDOTS; i++) map[i] = first[i];
    show(name, bs, 64, r);
}

static void paced() {
    Res r;
    r.bad = 0;
    paint();
    uint32_t late = 0;
    for (int i = 0; i < 64; i++) {
        const uint32_t t0 = now();
        while (!(read32(GP1) & B26)) if (now() - t0 > LIMIT) { late++; break; }
        write32(GP0, 0x68000000u | RGB(i));
        write32(GP0, xy(DOT_X + i, DOT_Y));
    }
    waitIdle();
    score(r, 64);
    printf("PACED by the CPU: landed %d of 64, wrong %d, strays %d, timed out %u\n", r.landed, r.wrong, r.strays, (unsigned)late);
    if (r.landed != 64 || r.bad) printf("*** PACED lost dots: the control failed, this run says nothing ***\n");
}

int main() {
    printf("\ngpu/gp0-overrun\n");
    printf("DMA2 into a full GP0 FIFO, one-pixel dots. Three trials a row: dots landed, wrong-coloured, strays, cycles to the DMA's\n");
    printf("end and to idle, then the first trial's map: # landed . missing x wrong colour. No assertions.\n\n");
    EnterCriticalSection();
    const uint32_t oldMask = read32(I_MASK);
    write32(I_MASK, 0);
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;

    write32(DPCR, read32(DPCR) | 0x00000800u);
    write32(GP1, 0x01000000u);
    write32(GP1, 0x04000000u);
    write32(GP0, 0xE1000400u);
    write32(GP0, 0xE2000000u);
    write32(GP0, 0xE3000000u);
    write32(GP0, 0xE4000000u | (511u << 10) | 1023u);
    write32(GP0, 0xE5000000u);
    write32(GP0, 0xE6000000u);
    if (!waitIdle()) printf("*** the GPU never read idle at the start ***\n");

    paced();

    printf("\nLIST: one node of N words behind a 640x480 fill, GP1(04h)=2\n");
    for (int N = 12; N <= 64; N++) listRow("N", N, 1);
    listRow("N", 128, 1);
    listRow("N", 240, 1);

    printf("\nLIST IDLE: the same node to an idle GPU\n");
    static const int idleN[] = { 12, 16, 24, 32, 48, 64, 128, 240 };
    for (unsigned i = 0; i < sizeof(idleN) / sizeof(idleN[0]); i++) listRow("N", idleN[i], 0);

    static const int bss[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16 };
    printf("\nBLOCK: 128 words of dots, block size B, behind a 640x480 fill, GP1(04h)=1\n");
    for (unsigned i = 0; i < sizeof(bss) / sizeof(bss[0]); i++) blockRow("B", bss[i], 1);
    printf("\nBLOCK IDLE: the same to an idle GPU\n");
    for (unsigned i = 0; i < sizeof(bss) / sizeof(bss[0]); i++) blockRow("B", bss[i], 0);

    waitIdle();
    write32(GP1, 0x04000000u);
    write16(T2_MODE, m2);
    write32(I_MASK, oldMask);
    ExitCriticalSection();
    printf("\ngpu/gp0-overrun: Done.\n");
    for (;;) {}
}
