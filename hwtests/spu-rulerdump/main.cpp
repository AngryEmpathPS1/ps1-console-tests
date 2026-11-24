/*
 * spu/spu-rulerdump - EVERY step of a linear attack in the capture ring, after holds of the transfer types under 4 (2026-11-24,
 * the SPU session)
 *
 * WHY. hwtests/spu-envphase-move / -move2 on the SCPH-1001 and SCPH-1000: after 1F801DACh = 0000h for 300 samples (on the 1000
 * after 100, on the 1001 after 0001h for 100) an attack's first two steps come 1, 2, 4, 6 or 10 samples apart where the period
 * is 8-512 - the envelope no longer steps once a period - and the pattern stays through the later measures. Those tests print
 * two steps a row; this prints them all.
 *
 * WHAT IT DOES. spu-rulerdump's rows (voice 1, a linear attack of +7 at shift 14 / 15 / 16 / 17: a step every 8 / 16 / 32 /
 * 64 samples; keyed on behind SPUSTAT bit 11's rising edge; both halves of the capture ring read back). After each state - the
 * start; 0000h held 100 samples; 0000h held 300; 0001h held 100; 0002h held 300; the SPU's registers written as a program's
 * start writes them - four rows:
 *   S state P : i:d i:d ...     every ring index i (0-1023 over the two halves) where the value changes and by how much (d, in
 *                               units of the row's own step: 1 = one step), the first 40
 * No assertions. Every wait is bounded. THE RULE: run here first.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>
#include <stdio.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define HALF 256
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) {}
}
static void spuDma(uint32_t spuAddr, uint32_t* ram, int words, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)(words / 16) << 16) | 16);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 4000000 && (read32(0x1F8010C8) & 0x01000000); i++) {}
    if (toSpu) idle(4000);
    spuMode(0);
}
static void edge(int want) {
    const uint16_t w = want ? 0x0800 : 0;
    const uint32_t t0 = now();
    while ((read16(SPU(0x1AE)) & 0x0800) == w && now() - t0 < 400000u) {}
    while ((read16(SPU(0x1AE)) & 0x0800) != w && now() - t0 < 800000u) {}
}
static uint32_t dcBlock[16] __attribute__((aligned(16)));
static uint32_t junk[256] __attribute__((aligned(16)));
static uint16_t cap[HALF * 2] __attribute__((aligned(16)));

/* one attack row, spu-envring's run() exactly: the ring indices of the first two changes (-1: none) */
static void row(uint16_t a1, int* i1, int* i2) {
    write16(SPU(0x18C), 0x0002); write16(SPU(0x18E), 0);
    write16(SPU(0x10 + 0xA), 0x001F);
    idle(SAMPLE * 1100u);
    write16(SPU(0x10 + 0x4), 0x1000);
    write16(SPU(0x10 + 0x6), 0x1000 / 8);
    write16(SPU(0x10 + 0x8), a1);
    write16(SPU(0x10 + 0xA), 0x1FC0);
    write16(SPU(0x18C), 0); write16(SPU(0x18E), 0);
    edge(1);
    write16(SPU(0x188), 0x0002);
    edge(0);
    spuDma(0x0A00, (uint32_t*)cap, HALF / 2, false);
    edge(1);
    spuDma(0x0800, (uint32_t*)(cap + HALF), HALF / 2, false);
    *i1 = -1; *i2 = -1;
    uint16_t last = 0;
    for (int i = 0; i < HALF * 2; i++) if (cap[i] != last) { last = cap[i]; if (*i1 < 0) *i1 = i; else { *i2 = i; break; } }
}

struct Meas { const char* what; int i1[7], i2[7]; };
static Meas meas[16];
static int nm = 0;
static void dump(const char* what) {
    for (int k = 0; k < 4; k++) {
        int i1, i2;
        row((uint16_t)(((14 + k) << 10) | 0x000F), &i1, &i2);
        now(); printf("S %s %d :", what, 8 << k); now();
        int n = 0; int last = 0; int unit = 0;
        for (int i = 0; i < HALF * 2 && n < 40; i++) {
            const int v = (int16_t)cap[i];
            if (v == last) continue;
            if (!unit) unit = v - last;
            printf(" %d:%d", i, unit ? (v - last) / unit : 0); last = v; n++;
            if ((n & 7) == 7) now();
        }
        printf("\n"); now();
    }
}
static void xfer(uint16_t v, uint32_t n) { write16(SPU(0x1AC), v); idle(n * SAMPLE); write16(SPU(0x1AC), 0x0004); idle(SAMPLE * 64u); }
int main() {
    printf("\nspu/spu-rulerdump\n");
    printf("The envelope ruler's phase c against the capture ring, measured after each action. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(DPCR, read32(DPCR) | 0x00080000u);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F801014, 0x220931E1);
    write16(SPU(0x180), 0); write16(SPU(0x182), 0);
    write16(SPU(0x184), 0); write16(SPU(0x186), 0);
    for (int v = 0; v < 24; v++) { write16(SPU((uint32_t)v * 0x10), 0); write16(SPU((uint32_t)v * 0x10 + 2), 0); }
    cnt = 0xC000;
    write16(SPU(0x1AA), cnt);
    write16(SPU(0x1AC), 0x0004);
    uint8_t* b = (uint8_t*)dcBlock;
    for (int i = 0; i < 64; i++) b[i] = 0;
    b[1] = 0x07;
    for (int i = 2; i < 16; i++) b[i] = 0x44;
    spuDma(0x1000, dcBlock, 16, true);

    dump("start");
    xfer(0x0000, 100); dump("0000h-100");
    xfer(0x0000, 300); dump("0000h-300");
    xfer(0x0001, 100); dump("0001h-100");
    xfer(0x0002, 300); dump("0002h-300");
    write16(SPU(0x1AC), 0x0004); cnt = 0xC000; write16(SPU(0x1AA), cnt); idle(SAMPLE * 2000u); dump("rewritten");
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    for (int n = 0; n < nm; n++) {
        const Meas& m = meas[n];
        /* c from each row's SECOND step where the ring holds it (a first step can hide before the voice is heard), else its first */
        int c[7]; bool agree = true;
        for (int k = 0; k < 7; k++) {
            const int P = 8 << k;
            const int i = (m.i2[k] >= 0 && m.i2[k] - m.i1[k] == P) ? m.i2[k] : m.i1[k];
            c[k] = i < 0 ? -1 : (((P / 2 - i) % P) + P) % P;
        }
        for (int k = 0; k < 6; k++) if (c[k] < 0 || c[6] < 0 || (c[6] % (8 << k)) != c[k]) agree = false;
        printf("M%-2d c %3d %s  %-34s", n, c[6], agree ? "agree   " : "DISAGREE", m.what);
        for (int k = 0; k < 7; k++) printf(" %d/%d", m.i1[k], m.i2[k]);
        printf("\n");
    }
    printf("\nspu/spu-rulerdump: Done.\n");
    for (;;) VSync(0);
}
