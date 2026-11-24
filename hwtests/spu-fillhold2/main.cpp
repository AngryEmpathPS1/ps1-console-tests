/*
 * spu/spu-fillhold2 - WHERE and WHAT a hold of transfer type 0 writes to sound RAM on the oldest SPU (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-fillhold on the SCPH-1000 and SCPH-1001: with no transfer running, 1F801DACh held at 0000h or 0001h for N
 * samples leaves exactly N halfwords of 0000h in sound RAM, 4 halfwords (8 bytes) apart, wrapping inside the 1000h bytes the
 * transfer address points into; 0002h / 0006h / 000Ch leave none, and the 5501 / 7001 / 9001 none at all. The first index moved
 * from case to case (1144, 50, 0, 1417, 621 of 2,048) and its low two bits too. This asks what the place follows - the capture
 * ring's index (512 samples a turn, as the wrap is), the transfer address, the write's place in the sample - and whether the
 * value is the FIFO's.
 *
 * WHAT IT DOES. Interrupts off, sound muted, nothing keyed on. For each case: a ramp (halfword n = 4000h + n, n = 0-1FFFh)
 * written to 2000h-5FFFh; the transfer address written; SPUSTAT bit 11's falling edge awaited (the capture ring at index 0) and
 * K samples idled; 1F801DACh written to the case's value (Timer 2 read right ahead of the store), held 64 samples, written back
 * to 0004h; 2000h-5FFFh read back.
 *   G id addr value K what : at k+p, changed n, first i, last j, odd gaps g, values v ... : i=v ...
 *       addr = the transfer address written (bytes); K = samples idled past the edge; k+p = the hold's store, whole samples and
 *       cycles past the edge as Timer 2 saw them; n halfwords of the 8,192 read back other than written, the first and last
 *       index, how many neighbours among them are not 4 apart, the distinct values among them (4 at most) and the first 6.
 *   what: plain; fifo = 1234h written to 1F801DA8h (mode 0) before the hold; off = SPUCNT 0000h through the hold; mode1 /
 *   mode2 = SPUCNT's transfer mode 1 / 2 through the hold (nothing written, no DMA started).
 * No assertions. Every wait is bounded. THE RULE: run here first.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SAMPLE 768u
#define HW 8192

static uint32_t clk; static uint16_t lastT;
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static void dma(uint32_t spuAddr, uint32_t* ram, int blocks, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)blocks << 16) | 16u);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); printf("T\n"); }
    if (toSpu) idle(100000);
    spuMode(0);
}

static uint32_t ramp[HW / 2] __attribute__((aligned(16)));
static uint32_t got[HW / 2] __attribute__((aligned(16)));

/* SPUSTAT bit 11's falling edge: the Timer 2 stamp of the first read that saw it low after high (0 on a timeout) */
static uint32_t edge() {
    for (int i = 0; i < 200000 && !(read16(SPU(0x1AE)) & 0x0800); i++) { now(); }
    for (int i = 0; i < 200000; i++) { const uint32_t t = now(); if (!(read16(SPU(0x1AE)) & 0x0800)) return t; }
    return 0;
}

enum { PLAIN, FIFO, OFF, MODE1, MODE2 };
static const char* const WHAT[] = { "plain", "fifo", "off", "mode1", "mode2" };

static void fillCase(int id, uint32_t addr, uint16_t value, uint32_t k, int what) {
    dma(0x2000, ramp, HW / 32, true);
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(addr / 8));
    if (what == FIFO) write16(SPU(0x1A8), 0x1234);
    if (what == OFF) { write16(SPU(0x1AA), 0x0000); idle(SAMPLE * 4u); }
    if (what == MODE1) spuMode(1);
    if (what == MODE2) spuMode(2);
    idle(SAMPLE * 8u);
    const uint32_t e = edge();
    while (now() - e < k * SAMPLE) {}
    const uint16_t t = read16(T2_COUNT);
    write16(SPU(0x1AC), value);
    const uint32_t at = stamp(t) - e;
    idle(64u * SAMPLE);
    write16(SPU(0x1AC), 0x0004);
    idle(SAMPLE * 64u);
    if (what == OFF) { write16(SPU(0x1AA), cnt); idle(SAMPLE * 4u); }
    dma(0x2000, got, HW / 32, false);
    const uint16_t* g = (const uint16_t*)got;
    int n = 0, first = -1, last = -1, odd = 0, nv = 0; uint16_t vals[4];
    for (int i = 0; i < HW; i++) {
        if (g[i] == (uint16_t)(0x4000 + i)) continue;
        if (last >= 0 && i - last != 4) odd++;
        n++; if (first < 0) first = i; last = i;
        int j = 0; while (j < nv && vals[j] != g[i]) j++;
        if (j == nv && nv < 4) vals[nv++] = g[i];
        if ((n & 15) == 15) now();
    }
    now();
    printf("G %d %04x %04x %u %s : at %u+%u, changed %d, first %d, last %d, odd gaps %d, values", id, (unsigned)addr, value, (unsigned)k, WHAT[what],
           (unsigned)(at / SAMPLE), (unsigned)(at % SAMPLE), n, first, last, odd);
    now();
    for (int j = 0; j < nv; j++) printf(" %04x", vals[j]);
    printf(" :");
    int shown = 0;
    for (int i = 0; i < HW && shown < 6; i++) if (g[i] != (uint16_t)(0x4000 + i)) { printf(" %x=%04x", i, g[i]); shown++; }
    printf("\n");
    now();
}

int main() {
    printf("\nspu/spu-fillhold2\n");
    printf("Where and what a hold of transfer type 0 writes. G id addr value K what : at k+p, changed n, first, last, odd gaps, values ... No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    write16(SPU(0x194), 0); write16(SPU(0x196), 0);
    write16(SPU(0x190), 0); write16(SPU(0x192), 0);
    write16(SPU(0x198), 0); write16(SPU(0x19A), 0);
    write16(SPU(0x1A2), 0xFFFE);
    write16(SPU(0x1AC), 0x0004);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    idle(100000);
    cnt = 0xC000; write16(SPU(0x1AA), cnt);
    uint16_t* r16 = (uint16_t*)ramp;
    for (int n = 0; n < HW; n++) r16[n] = (uint16_t)(0x4000 + n);

    int id = 0;
    /* the capture ring's index: the same address, the hold K samples past the ring's start */
    fillCase(id++, 0x2000, 0x0000, 10, PLAIN);
    fillCase(id++, 0x2000, 0x0000, 10, PLAIN);
    fillCase(id++, 0x2000, 0x0000, 100, PLAIN);
    fillCase(id++, 0x2000, 0x0000, 300, PLAIN);
    fillCase(id++, 0x2000, 0x0000, 500, PLAIN);
    fillCase(id++, 0x2000, 0x0000, 600, PLAIN);
    /* the transfer address */
    fillCase(id++, 0x2008, 0x0000, 100, PLAIN);
    fillCase(id++, 0x2800, 0x0000, 100, PLAIN);
    fillCase(id++, 0x3000, 0x0000, 100, PLAIN);
    fillCase(id++, 0x3FF8, 0x0000, 100, PLAIN);
    fillCase(id++, 0x4000, 0x0000, 100, PLAIN);
    fillCase(id++, 0x5800, 0x0000, 500, PLAIN);
    /* the value */
    fillCase(id++, 0x2000, 0x0000, 100, FIFO);
    fillCase(id++, 0x2000, 0x0001, 100, PLAIN);
    fillCase(id++, 0x2000, 0x0001, 100, FIFO);
    fillCase(id++, 0x2000, 0x0010, 100, PLAIN);
    fillCase(id++, 0x2000, 0xFFF0, 100, PLAIN);
    fillCase(id++, 0x2000, 0x0003, 100, PLAIN);
    fillCase(id++, 0x2000, 0x0008, 100, PLAIN);
    fillCase(id++, 0x2000, 0x000A, 100, PLAIN);
    fillCase(id++, 0x2000, 0x000E, 100, PLAIN);
    /* the SPU's state */
    fillCase(id++, 0x2000, 0x0000, 100, OFF);
    fillCase(id++, 0x2000, 0x0000, 100, MODE1);
    fillCase(id++, 0x2000, 0x0000, 100, MODE2);
    fillCase(id++, 0x2000, 0x0004, 100, PLAIN);

    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fillhold2: Done.\n");
    for (;;) VSync(0);
}
