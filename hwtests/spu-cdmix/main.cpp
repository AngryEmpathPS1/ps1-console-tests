/*
 * spu/spu-cdmix - CD audio THROUGH the SPU: its CD volume, its CD-enable bit and its CD reverb send, read off the reverb work
 * area (2026-11-25, the SPU session)
 *
 * WHY. How CD audio (CD-DA or XA) REACHES the SPU is measured - hwtests/cd-da-capture and cd-xa on five consoles: the capture
 * ring's samples, each board's resampling kernel. What the SPU then DOES with it is not: this core multiplies each sample by
 * 1F801DB0h / 1DB2h, adds it to the mix when SPUCNT bit 0 is set and to the reverb's input when bit 2 is too - psx-spx's and
 * DuckStation's reading, nothing measured. The mix cannot be read back; the reverb's work area can.
 *
 * WHAT IT DOES. Needs a disc that plays: one with a track 2 of audio is played from 20 s into it (Play); otherwise The X-Files
 * (USA), any disc: FOXLOGO.STR at 51:27:47 streamed with XA on (Setmode E0h, ReadS). Interrupts off, the CD's flags polled
 * and acknowledged, output volumes 0. The reverb is set up as a tap on its own input: every voice's reverb off, vLIN = vRIN =
 * 8000h (the input, negated, with no rounding), vIIR 7FFFh, every other volume 0, a work area of 2,048 halfwords at 7F000h
 * with the left input written at the area's moving start and the right 1,024 halfwords on - so the area holds the last 512
 * left and 512 right reverb inputs, one a 22,050 Hz step. For each case - the CD volumes, SPUCNT's bits 0 and 2, the drive's
 * own volume - 0.35 s of playing, then at SPUSTAT bit 11's turn the reverb's master enable is cleared (the area stops) and the
 * finished half of the CD capture ring (256 left, 256 right) and the area are read by DMA.
 *   C k cdl cdr cnt mat half      the case: 1F801DB0h, 1DB2h, SPUCNT, the drive's volume, which ring half was read
 *   L k : 256 values              the ring's left half (hex)       R k : the right
 *   A k o : 64 values             the work area from halfword o (32 rows a case)
 * `node experiments/spu_cdmix.js <log>` replays each case's ring samples through a core's own SPU and counts how many of the
 * area's values that core would have written. No assertions. Every wait is bounded. THE RULE: run here first:
 * `node hwtests/run.js spu-cdmix --disc=xfiles --ntsc --settle=600 --frames=4000 --quiet=1500`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define MS(n) ((uint32_t)(n) * 33869u)

static volatile uint8_t *CD0 = (volatile uint8_t *)0x1F801800;
static volatile uint8_t *CD1 = (volatile uint8_t *)0x1F801801;
static volatile uint8_t *CD2 = (volatile uint8_t *)0x1F801802;
static volatile uint8_t *CD3 = (volatile uint8_t *)0x1F801803;
static void idx(uint8_t i) { *CD0 = i & 3; }
static uint8_t irqFlag() { idx(1); return *CD3 & 7; }
static void ackAll() { idx(1); *CD3 = 0x1F; }
static void param(uint8_t v) { idx(0); *CD2 = v; }
static void command(uint8_t c) { idx(0); *CD1 = c; }
static uint8_t resp() { idx(1); return *CD1; }
static bool hasResp() { return (*CD0 & 0x20) != 0; }
static void drain() { for (int n = 0; n < 32 && hasResp(); n++) resp(); }

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE
#define AREA 0x7F000u
#define AREA_HW 2048

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void service() { if (irqFlag()) { drain(); ackAll(); } }
static void idle(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) service(); }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    for (uint32_t k = 0; (read32(0x1F8010C8) & 0x01000000) && k < 4000000; k++) {}
    setCnt(cnt & ~0x30);
}

static uint8_t rb[16]; static int rn;
static uint8_t cmd(uint8_t c, bool second = false) {
    ackAll();
    command(c);
    uint8_t f = 0; const uint32_t t0 = now();
    while (!(f = irqFlag()) && now() - t0 < MS(1000)) {}
    rn = 0; while (hasResp() && rn < 16) rb[rn++] = resp();
    ackAll();
    if (second && f == 3) { const uint32_t t1 = now(); uint8_t g; while (!(g = irqFlag()) && now() - t1 < MS(5000)) {} drain(); ackAll(); }
    return f;
}
static uint8_t bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int unbcd(uint8_t b) { return (b >> 4) * 10 + (b & 15); }
static void setMatrix(uint8_t m) {
    idx(2); *CD2 = m; *CD3 = 0x00;
    idx(3); *CD1 = m; *CD2 = 0x00; *CD3 = 0x20;
}

struct Case { uint16_t l, r, cnt; uint8_t mat; };
static const Case CASES[] = {
    { 0x7FFF, 0x7FFF, 0xC085, 0x80 },
    { 0x4000, 0x2000, 0xC085, 0x80 },
    { 0x8000, 0xC000, 0xC085, 0x80 },      /* negative volumes */
    { 0x0001, 0x7FFE, 0xC085, 0x80 },
    { 0x5555, 0x3333, 0xC085, 0x80 },
    { 0x7FFF, 0x7FFF, 0xC084, 0x80 },      /* the reverb send without the CD enable */
    { 0x7FFF, 0x7FFF, 0xC081, 0x80 },      /* the CD enable without the send */
    { 0x7FFF, 0x7FFF, 0xC085, 0x40 },      /* the drive's own volume halved */
    { 0x7FFF, 0x7FFF, 0xC085, 0x80 },      /* the first again */
};
enum { NC = sizeof(CASES) / sizeof(CASES[0]) };
static int16_t ringL[NC][256], ringR[NC][256];
static int16_t area[NC][AREA_HW] __attribute__((aligned(16)));
static uint8_t halfOf[NC];
static int16_t buf[256] __attribute__((aligned(16)));

static void capture(int k) {
    const Case &c = CASES[k];
    setMatrix(c.mat);
    w16(0x1B0, c.l); w16(0x1B2, c.r);
    setCnt(c.cnt);
    idle(MS(350));
    const uint16_t b0 = r16(SPUSTAT) & 0x800;
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x800) == b0 && now() - t0 < MS(50)) service();
    w16(SPUCNT, (uint16_t)(cnt & ~0x0080)); cnt = (uint16_t)(cnt & ~0x0080);     /* the reverb's master enable off: the area stops */
    halfOf[k] = b0 ? 1 : 0;                              /* the half bit 11 just LEFT is finished */
    dmaRead(halfOf[k] ? 0x200 : 0x000, buf, 512); for (int i = 0; i < 256; i++) ringL[k][i] = buf[i];
    dmaRead(halfOf[k] ? 0x600 : 0x400, buf, 512); for (int i = 0; i < 256; i++) ringR[k][i] = buf[i];
    service();
    dmaRead(AREA, area[k], AREA_HW * 2);
    service();
}

int main() {
    printf("\nspu/spu-cdmix\n");
    printf("CD audio through the SPU's CD volume, enable and reverb send, read off the reverb work area. Needs a disc that plays. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);    /* DMA4 on */
    const uint32_t spuDelay = read32(0x1F801014);
    idx(1); *CD2 = 0; ackAll(); drain();

    cmd(0x0A, true);                                    /* Init */
    cmd(0x0C);                                          /* Demute */
    setMatrix(0x80);
    w16(0x1AC, 0x0004);
    w16(0x180, 0); w16(0x182, 0);                       /* output volume 0 */
    w16(0x184, 0); w16(0x186, 0);                       /* the reverb's output volume 0 */
    w16(0x198, 0); w16(0x19A, 0);                       /* no voice's reverb */
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);             /* every voice off */
    setCnt(0xC000);
    w16(0x1A2, (uint16_t)(AREA / 8));                   /* mBASE */
    for (uint32_t o = 0x1C0; o <= 0x1FE; o += 2) w16(o, 0);
    w16(0x1C4, 0x7FFF);                                 /* vIIR */
    w16(0x1D4, 0x000); w16(0x1E4, 0x000);               /* mLSAME, mLDIFF: the area's moving start */
    w16(0x1D6, 0x100); w16(0x1E6, 0x100);               /* mRSAME, mRDIFF: 1,024 halfwords on */
    w16(0x1F4, 0x080); w16(0x1F8, 0x080);               /* mLAPF1, mLAPF2: zeroes 512 halfwords on */
    w16(0x1F6, 0x180); w16(0x1FA, 0x180);               /* mRAPF1, mRAPF2: zeroes 1,536 on */
    w16(0x1FC, 0x8000); w16(0x1FE, 0x8000);             /* vLIN, vRIN: -1 */

    cmd(0x13);                                          /* GetTN */
    const int last = rn > 2 ? unbcd(rb[2]) : 0;
    bool cdda = false, ok = false;
    if (last >= 2) {
        param(0x02); cmd(0x14);                         /* GetTD track 2 */
        const int mm = rn > 1 ? unbcd(rb[1]) : 0, ss = rn > 2 ? unbcd(rb[2]) : 0;
        int s = mm * 60 + ss + 20;
        param(0x00); cmd(0x0E);                         /* Setmode: CD-DA, single speed */
        param(bcd(s / 60)); param(bcd(s % 60)); param(0); cmd(0x02);
        ok = cmd(0x03) == 3;                            /* Play */
        cdda = true;
    } else {
        param(0xE0); cmd(0x0E);                         /* Setmode: double speed, XA on, whole sectors */
        param(0x51); param(0x27); param(0x47); cmd(0x02);   /* Setloc 51:27:47, FOXLOGO.STR */
        ok = cmd(0x1B) == 3;                            /* ReadS */
    }
    idle(MS(1500));                                     /* the seek, and into the sound */

    if (ok) for (int k = 0; k < (int)NC; k++) capture(k);
    cmd(0x09, true);                                    /* Pause */
    setCnt(0xC000);
    write32(0x1F801014, spuDelay);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("%s, tracks to %d%s\n", cdda ? "CD-DA: track 2 from 20 s in" : "XA: 51:27:47 at double speed", last, ok ? "" : " - THE DISC DID NOT START");
    if (ok) for (int k = 0; k < (int)NC; k++) {
        printf("C %d %04x %04x %04x %02x %d\n", k, CASES[k].l, CASES[k].r, CASES[k].cnt, CASES[k].mat, halfOf[k]);
        printf("L %d :", k); for (int i = 0; i < 256; i++) printf(" %04x", (uint16_t)ringL[k][i]); printf("\n");
        printf("R %d :", k); for (int i = 0; i < 256; i++) printf(" %04x", (uint16_t)ringR[k][i]); printf("\n");
        for (int o = 0; o < AREA_HW; o += 64) { printf("A %d %d :", k, o); for (int i = 0; i < 64; i++) printf(" %04x", (uint16_t)area[k][o + i]); printf("\n"); }
    }
    printf("\nspu/spu-cdmix: Done.\n");
    for (;;) VSync(0);
}
