/*
 * spu/spu-capring4 - WHICH OF THE FOUR CAPTURE RINGS RAISES IRQ9, AND IS EACH RING WRITTEN? (2026-11-25, the SPU session)
 *
 * WHY. hwtests/spu-mbase armed IRQ9 at 0408h - inside the CD-right ring - as a "low address". On the SCPH-1000, the second 5501,
 * the second 7501 and the 7502 it fired every 512 samples (the ring's write, as on this emulator); on the SCPH-1001, the PSone, the
 * 7001 and the 9001 it never did - the same four in three runs, 1F801DACh 0004h on all eight, SPUCNT's low bits no guide, not the
 * boot's draw, not the board. Nothing had measured the CD rings' IRQ before (hwtests/spu-capirqen / -ad / -hw use voice 1's at 800h).
 *
 * WHAT IT DOES. Interrupts off, every voice keyed off, Timer 2 on the system clock / 8 (96 a sample). Nothing here stops the ring
 * or writes 1F801DACh a value other than 0004h (a boot's draw is left as it was).
 *   M mbase spucnt spustat irqaddr xfer cdL cdR extL extR eonLo eonHi    registers as found
 *   H n d1 d2                      SPUSTAT bit 11's next three turns, samples apart (512: the ring runs)
 *   R c addr t1 d2 d3              condition c, IRQ9 armed at addr: the first time SPUSTAT bit 6 shows (samples from the arming;
 *                                  -1: not in 2,200), then the next two after an acknowledge 16 samples long (512: a ring's write)
 *   Q c addr u                     the same address acknowledged at once: T2 units (96 a sample) to the next showing, -1 none in 2,200
 *                                  samples (is it the one halfword, or each of the four in the address's 8 bytes?)
 *   W c ring changed first last v  sound RAM 0000h-0FFFh filled with a ramp (4000h + halfword index) by DMA, 600 samples, read
 *                                  back by DMA: per ring (0 CD left, 1 CD right, 2 voice 1, 3 voice 3) the halfwords that changed,
 *                                  the first and last index, the first changed value
 * CONDITIONS c (each leaves the reverb's registers zero and mBASE FFFEh from 1 on, so no tap leaves the area):
 *   0 as found                     1 reverb out of the way        2 SPUCNT bits 0-3 clear        3 SPUCNT bit 0 (CD on)
 *   4 SPUCNT bits 0 and 2          5 CD volume 7FFFh 7FFFh        6 CD volume 0                  7 1F801DACh written 0004h again
 *   8 SPUCNT bit 14 clear (mute)   9 hwtests/spu-mbase's stage D (the preset, mBASE FE00h, bit 7 clear)
 * Everything is put back as found at the end. Every wait is bounded. No assertions. About 20 s.
 * THE RULE: run here first: `node hwtests/run.js spu-capring4 --ntsc --frames=2400 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SPU(o) (0x1F801C00u + (o))
#define DPCR 0x1F8010F0u
#define SAMPLE 96u
#define LIMIT (2200u * SAMPLE)

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }   /* units of 8 cycles */
static void idle(uint32_t u) { const uint32_t t0 = now(); while (now() - t0 < u) {} }

static uint16_t cnt;
static char tag[160]; static int16_t rc[160]; static int32_t rv[160][4]; static int nr = 0;
static void put(char t, int c, int32_t a, int32_t b, int32_t d, int32_t e) { if (nr < 160) { tag[nr] = t; rc[nr] = (int16_t)c; rv[nr][0] = a; rv[nr][1] = b; rv[nr][2] = d; rv[nr][3] = e; nr++; } }

static int32_t waitIrq(uint32_t from) {   /* T2 units from `from`, -1 */
    const uint32_t t0 = now();
    while (now() - t0 < LIMIT) { if (read16(SPU(0x1AE)) & 0x0040) return (int32_t)(now() - from); }
    return -1;
}
static void row(int c, uint32_t addr) {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
    write16(SPU(0x1A4), (uint16_t)(addr / 8));
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    const uint32_t t0 = now();
    const int32_t t1 = waitIrq(t0);
    int32_t d2 = -1, d3 = -1;
    if (t1 >= 0) {
        uint32_t a = now();
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(16 * SAMPLE); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
        d2 = waitIrq(a);
        if (d2 >= 0) {
            a = now();
            write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(16 * SAMPLE); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
            d3 = waitIrq(a);
        }
    }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    put('R', c, (int32_t)addr, t1 < 0 ? -1 : t1 / (int32_t)SAMPLE, d2 < 0 ? -1 : (d2 + 48) / (int32_t)SAMPLE, d3 < 0 ? -1 : (d3 + 48) / (int32_t)SAMPLE);
}
static void quick(int c, uint32_t addr) {
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); idle(4 * SAMPLE);
    write16(SPU(0x1A4), (uint16_t)(addr / 8));
    write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
    int32_t u = -1;
    if (waitIrq(now()) >= 0) {
        write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040)); write16(SPU(0x1AA), (uint16_t)(cnt | 0x0040));
        const uint32_t a = now();
        idle(8);                                                  /* past SPUSTAT's own update of the acknowledge */
        u = waitIrq(a);
    }
    write16(SPU(0x1AA), (uint16_t)(cnt & ~0x0040));
    put('Q', c, (int32_t)addr, u, 0, 0);
}
static void three(int c) { row(c, 0x0008); row(c, 0x0408); row(c, 0x0808); }

static void spuMode(int m) {
    cnt = (uint16_t)((cnt & ~0x30) | (m << 4));
    write16(SPU(0x1AA), cnt);
    for (int i = 0; i < 100000 && (read16(SPU(0x1AE)) & 0x3F) != (cnt & 0x3F); i++) { now(); }
}
static int dmaStuck = 0;
static void dma(uint32_t spuAddr, uint32_t* ram, int blocks, bool toSpu) {
    spuMode(0);
    write16(SPU(0x1A6), (uint16_t)(spuAddr / 8));
    spuMode(toSpu ? 2 : 3);
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFF);
    write32(0x1F8010C4, ((uint32_t)blocks << 16) | 16u);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    for (int i = 0; i < 2000000 && (read32(0x1F8010C8) & 0x01000000); i++) { now(); }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); dmaStuck++; }
    if (toSpu) idle(100 * SAMPLE);
    spuMode(0);
}
static uint32_t ramp[1024] __attribute__((aligned(16)));
static uint32_t got[1024] __attribute__((aligned(16)));
static void written(int c) {
    dma(0x0000, ramp, 64, true);
    idle(600 * SAMPLE);
    dma(0x0000, got, 64, false);
    const uint16_t* g = (const uint16_t*)got;
    for (int r = 0; r < 4; r++) {
        int n = 0, first = -1, last = -1; uint16_t v = 0;
        for (int i = 0; i < 512; i++) { const int k = r * 512 + i; if (g[k] != (uint16_t)(0x4000 + k)) { if (first < 0) { first = i; v = g[k]; } last = i; n++; } }
        put('W', c, r, n, (first << 16) | (last & 0xFFFF), v);
        now();
    }
}

int main() {
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0x0200); lastT = read16(T2_COUNT); clk = 0;
    const uint32_t dpcr = read32(DPCR), spuDelay = read32(0x1F801014);
    const uint16_t mbase = read16(SPU(0x1A2)), cnt0 = read16(SPU(0x1AA)), stat0 = read16(SPU(0x1AE)), irqa = read16(SPU(0x1A4)), dac = read16(SPU(0x1AC));
    const uint16_t cdl = read16(SPU(0x1B0)), cdr = read16(SPU(0x1B2)), exl = read16(SPU(0x1B4)), exr = read16(SPU(0x1B6)), eon0 = read16(SPU(0x198)), eon1 = read16(SPU(0x19A));
    static uint16_t rvb[32];
    for (int i = 0; i < 32; i++) rvb[i] = read16(SPU(0x1C0 + 2 * (uint32_t)i));
    for (int n = 0; n < 2048; n++) ((uint16_t*)ramp)[n] = (uint16_t)(0x4000 + n);
    write16(SPU(0x18C), 0xFFFF); write16(SPU(0x18E), 0x00FF);       /* every voice keyed off */
    write32(DPCR, dpcr | 0x00080000u);
    write32(0x1F801014, 0x220931E1);
    cnt = (uint16_t)(cnt0 & ~0x0070);

    /* H: the ring runs? */
    int32_t h[3] = { -1, -1, -1 };
    {
        uint16_t b = read16(SPU(0x1AE)) & 0x0800; uint32_t t0 = now(), tl = 0; int k = -1;
        while (now() - t0 < 2200u * SAMPLE && k < 3) {
            const uint16_t x = read16(SPU(0x1AE)) & 0x0800;
            if (x != b) { b = x; const uint32_t t = now(); if (k >= 0) h[k] = (int32_t)((t - tl + 48) / SAMPLE); tl = t; k++; }
        }
    }
    /* 0: as found */
    row(0, 0x0008); row(0, 0x0408); row(0, 0x0808); row(0, 0x0C08);
    /* 1: the reverb out of the way */
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), 0);
    write16(SPU(0x1A2), 0xFFFE);
    static const uint16_t ads[] = { 0x0008, 0x0208, 0x0408, 0x0608, 0x0808, 0x0A08, 0x0C08, 0x0E08, 0x0000, 0x03F8, 0x0400, 0x07F8 };
    for (unsigned i = 0; i < sizeof(ads) / sizeof(ads[0]); i++) row(1, ads[i]);
    quick(1, 0x0008); quick(1, 0x0408); quick(1, 0x0808);
    written(1);
    /* 2-4: SPUCNT's CD / external bits */
    cnt = (uint16_t)(cnt & ~0x000F); write16(SPU(0x1AA), cnt); idle(8 * SAMPLE); three(2);
    cnt = (uint16_t)(cnt | 0x0001); write16(SPU(0x1AA), cnt); idle(8 * SAMPLE); three(3);
    cnt = (uint16_t)(cnt | 0x0005); write16(SPU(0x1AA), cnt); idle(8 * SAMPLE); three(4);
    /* 5, 6: the CD volume */
    write16(SPU(0x1B0), 0x7FFF); write16(SPU(0x1B2), 0x7FFF); idle(8 * SAMPLE); three(5); written(5);
    write16(SPU(0x1B0), 0); write16(SPU(0x1B2), 0); idle(8 * SAMPLE); three(6);
    /* 7: the transfer type written again with its own value */
    write16(SPU(0x1AC), 0x0004); idle(8 * SAMPLE); three(7);
    /* 8: mute */
    cnt = (uint16_t)(cnt & ~0x4000); write16(SPU(0x1AA), cnt); idle(8 * SAMPLE); three(8);
    cnt = (uint16_t)(cnt | 0x4000); write16(SPU(0x1AA), cnt);
    /* 9: spu-mbase's stage D */
    static const uint16_t preset[32] = { 0x033d, 0x0231, 0x7e00, 0x5000, 0xb400, 0xb000, 0x4c00, 0xb000, 0x6000, 0x5400, 0x1ed6, 0x1a31, 0x1d14, 0x183b, 0x1bc2, 0x16b2,
                                         0x1a32, 0x15ef, 0x15ee, 0x1055, 0x1334, 0x0f2d, 0x11f6, 0x0c5d, 0x1056, 0x0ae1, 0x0ae0, 0x07a2, 0x0464, 0x0232, 0x8000, 0x8000 };
    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), preset[i]);
    cnt = (uint16_t)((cnt0 & ~0x0070) & ~0x0080); write16(SPU(0x1AA), cnt); write16(SPU(0x1A2), 0xFE00);
    three(9);

    for (int i = 0; i < 32; i++) write16(SPU(0x1C0 + 2 * (uint32_t)i), rvb[i]);
    write16(SPU(0x1A2), mbase);
    write16(SPU(0x1A4), irqa);
    write16(SPU(0x1B0), cdl); write16(SPU(0x1B2), cdr);
    write16(SPU(0x1AA), cnt0);
    write32(0x1F801014, spuDelay);
    write32(DPCR, dpcr);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-capring4\nWhich capture ring raises IRQ9, and is each written? M registers; H ring turns; R c addr t1 d2 d3 (samples); Q c addr units; W c ring changed first last value. No assertions.\n\n");
    printf("M %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x\n", mbase, cnt0, stat0, irqa, dac, cdl, cdr, exl, exr, eon0, eon1);
    printf("H %d %d %d\n", (int)h[0], (int)h[1], (int)h[2]);
    if (dmaStuck) printf("T %d\n", dmaStuck);
    for (int k = 0; k < nr; k++) {
        if (tag[k] == 'R') printf("R %d %04x %d %d %d\n", (int)rc[k], (unsigned)rv[k][0], (int)rv[k][1], (int)rv[k][2], (int)rv[k][3]);
        else if (tag[k] == 'Q') printf("Q %d %04x %d\n", (int)rc[k], (unsigned)rv[k][0], (int)rv[k][1]);
        else printf("W %d %d %d %d %d %04x\n", (int)rc[k], (int)rv[k][0], (int)rv[k][1], (int)(rv[k][2] >> 16), (int)(int16_t)(rv[k][2] & 0xFFFF), (unsigned)(rv[k][3] & 0xFFFF));
    }
    printf("\nspu/spu-capring4: Done.\n");
    for (;;) VSync(0);
}
