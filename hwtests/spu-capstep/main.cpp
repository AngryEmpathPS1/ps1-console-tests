/*
 * spu/spu-capstep - does the transfer type move the CAPTURE RING's index? SPUSTAT bit 11's edges across a hold (2026-11-24, the
 * SPU session)
 *
 * WHY. hwtests/spu-fillhold3 on the SCPH-1000 and SCPH-1001: while 1F801DACh's bits 3-1 are clear a 0000h lands in sound RAM
 * each sample, 4 halfwords on from the last, and a second hold timed from the same bit-11 edge lands 3 halfwords late for every
 * sample the first one held - yet every new case, timed from a fresh edge, starts where the capture index is. So the counter
 * that places them IS the capture ring's index and it steps 4 a sample through such a hold; and the "envelope ruler" that
 * hwtests/spu-envphase-move measures against that ring (-3 a sample on those two consoles for 0000h, -1 for 0002h) may be the
 * ring moving, not the ruler. This times the ring directly, on every console, for every type.
 *
 * WHAT IT DOES. Interrupts off, sound muted, nothing keyed on. Timer 2 on the system clock, read around each SPUSTAT read.
 *  C rows, for each value V of 1F801DACh: bit 11's falling edge e0; 16 samples on V is written, N samples on 0004h; the next two
 *    falling edges e1, e2.
 *      C V N : e1-e0 s+c, e2-e1 s+c      whole samples + cycles (512+0 = the ring untouched; 512 less 3N = four a sample)
 *  L rows: V held from an edge on; the next six falling edges.
 *      L V : s+c s+c s+c s+c s+c s+c     each edge less the one before (a turn of the ring: 512 samples as it should be)
 * No assertions. Every wait is bounded. THE RULE: run here first.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

/* SPUSTAT bit 11's falling edge: the stamp of the first read that saw it low after high (0 on a timeout: ~3 turns of the ring) */
static uint32_t edge() {
    const uint32_t t0 = now();
    while (!(read16(SPU(0x1AE)) & 0x0800)) { if (now() - t0 > 1536u * SAMPLE) return 0; }
    for (;;) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; if (t - t0 > 1536u * SAMPLE) return 0; }
}
static void put(uint32_t d) { printf(" %u+%u", (unsigned)(d / SAMPLE), (unsigned)(d % SAMPLE)); }

static void shortCase(uint16_t v, uint32_t n) {
    const uint32_t e0 = edge();
    while (now() - e0 < 16u * SAMPLE) {}
    write16(SPU(0x1AC), v);
    const uint32_t t0 = now();
    while (now() - t0 < n * SAMPLE) {}
    write16(SPU(0x1AC), 0x0004);
    const uint32_t e1 = edge(), e2 = edge();
    printf("C %04x %u :", v, (unsigned)n);
    if (!e0 || !e1 || !e2) printf(" timeout"); else { put(e1 - e0); printf(","); put(e2 - e1); }
    printf("\n");
    now(); idle(SAMPLE * 32u);
}
static void longCase(uint16_t v) {
    uint32_t e[7];
    e[0] = edge();
    write16(SPU(0x1AC), v);
    for (int i = 1; i < 7; i++) e[i] = edge();
    write16(SPU(0x1AC), 0x0004);
    printf("L %04x :", v);
    for (int i = 1; i < 7; i++) { if (!e[i] || !e[i - 1]) printf(" timeout"); else put(e[i] - e[i - 1]); }
    printf("\n");
    now(); idle(SAMPLE * 32u);
}

int main() {
    printf("\nspu/spu-capstep\n");
    printf("Does the transfer type move the capture ring's index? C V N : e1-e0, e2-e1 (samples+cycles); L V : six turns. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    write16(SPU(0x1AA), 0xC000);
    idle(100000);

    static const uint16_t V[] = { 0x0004, 0x0000, 0x0001, 0x0002, 0x0003, 0x0006, 0x0008, 0x000A, 0x000C, 0x000E, 0x0010, 0x0004 };
    for (unsigned i = 0; i < sizeof(V) / sizeof(V[0]); i++) { shortCase(V[i], 40); shortCase(V[i], 100); }
    for (unsigned i = 0; i < sizeof(V) / sizeof(V[0]); i++) longCase(V[i]);

    write16(SPU(0x1AC), 0x0004);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-capstep: Done.\n");
    for (;;) VSync(0);
}
