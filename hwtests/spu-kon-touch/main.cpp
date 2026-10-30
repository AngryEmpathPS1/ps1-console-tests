/*
 * spu/spu-kon-touch - when a key-on first touches its voice's start block, at every phase of the sample (2026-10-28).
 *
 * WHY. spu-decode-ahead's start-block rows (IRQ9 at 1000h, enabled before the key-on) read 832-2,064 cycles after the KON
 * write on the 7501; this core touches the block only when its decoder restarts SPU_KON_DELAY samples on (4,216-5,344 at
 * v0.377.208). A check as the key-on is taken (experiments/_mkkont209.js) lands in that range but not row for row: the
 * take's moment within the sample is not known. spu-lead-phase's method: the KON written D cycles after SPUSTAT bit 11
 * falls, D = 0, 32 ... 736.
 *
 * WHAT IT DOES. nugget's eight blocks at 2000h (uploaded 32 at a time), every other voice parked. Each row: voice 1 keyed
 * off, pointed at 2000h at the pitch, the IRQ address 2000h (the start block) with the enable on BEFORE the key-on, then
 * the KON written D cycles after bit 11 falls: the cycles from the KON write to IRQ9 (polled as SPUSTAT bit 6). At pitch
 * 1000h and 0040h.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-kon-touch --ntsc`.
 */
#include <common.h>
#include <psxapi.h>
#include <io.h>

#define SPU(o) (0x1F801C00 + (o))
static void w16(uint32_t o, uint16_t v) { write16(SPU(o), v); }
static uint16_t r16(uint32_t o) { return read16(SPU(o)); }
#define SPUCNT 0x1AA
#define SPUSTAT 0x1AE
#define T2_COUNT 0x1F801120u
#define T2_MODE  0x1F801124u
#define SAMPLE 768u

static uint32_t clk; static uint16_t lastT;
static uint32_t now() { const uint16_t t = read16(T2_COUNT); clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static bool irqFlag() { return (r16(SPUSTAT) & 0x40) != 0; }
static void irqOff() { setCnt(cnt & ~0x40); const uint32_t t0 = now(); while (irqFlag() && now() - t0 < 100000) {} }
static void upload32(uint32_t a, const uint16_t *d, bool setAddr) {
    setCnt(cnt & ~0x30);
    if (setAddr) w16(0x1A6, a / 8);
    for (int i = 0; i < 32; i++) w16(0x1A8, d[i]);
    setCnt((cnt & ~0x30) | 0x10);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {}
    setCnt(cnt & ~0x30);
}
static void syncFlip() {
    const uint32_t t0 = now();
    while (!(r16(SPUSTAT) & 0x800) && now() - t0 < 1000000) {}
    while ((r16(SPUSTAT) & 0x800) && now() - t0 < 1000000) {}
}
static void keyOffAll() { w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF); waitCycles(40 * SAMPLE); }

static uint16_t blk[64];
static uint32_t kon[2][24];

static uint32_t runCase(uint16_t pitch, uint32_t d) {
    keyOffAll();
    w16(0x16, 0x1100 / 8); w16(0x1E, 0x1100 / 8);              /* the released voice left looping on the parked block */
    w16(0x188, 0x0002); waitCycles(20 * SAMPLE);
    keyOffAll();
    w16(0x1A4, 0x0800 / 8);
    irqOff();
    w16(0x10, 0); w16(0x12, 0); w16(0x14, pitch);
    w16(0x16, 0x2000 / 8);                                        /* block 0's flags set the repeat address */
    w16(0x18, 0x000F); w16(0x1A, 0x1FC0);
    w16(0x1A4, 0x2000 / 8); setCnt(cnt | 0x40);
    syncFlip();
    const uint32_t e = now();
    while (now() - e < d) {}
    const uint32_t t0 = now();
    w16(0x188, 0x0002);
    uint32_t t = 0;
    while (now() - t0 < 100 * SAMPLE) if (irqFlag()) { t = now() - t0; break; }
    irqOff();
    return t;
}

int main() {
    printf("\nspu/spu-kon-touch\n");
    printf("When a key-on first touches its start block, by the KON write's phase. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);
    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    w16(0x180, 0); w16(0x182, 0); w16(0x184, 0); w16(0x186, 0);
    w16(0x198, 0); w16(0x19A, 0); w16(0x190, 0); w16(0x192, 0); w16(0x194, 0); w16(0x196, 0);
    static uint16_t parked[32] = { 0x0700 };
    upload32(0x1100, parked, true);
    keyOffAll();
    for (int v = 0; v < 24; v++) {
        const uint32_t b = v * 16;
        w16(b + 0, 0); w16(b + 2, 0); w16(b + 4, 0x1000);
        w16(b + 6, 0x1100 / 8); w16(b + 0xE, 0x1100 / 8);
        w16(b + 8, 0x000F); w16(b + 0xA, 0x1FC0);
    }
    w16(0x188, 0xFFFD); w16(0x18A, 0x00FF);
    waitCycles(200000);
    static const uint8_t flags[8] = { 6, 0, 0, 0, 0, 0, 0, 3 };
    for (int b = 0; b < 8; b++) {
        blk[b * 8] = (uint16_t)(flags[b] << 8);
        const uint16_t d = (uint16_t)(((b + 1) & 7) * 0x1111);
        for (int i = 1; i < 8; i++) blk[b * 8 + i] = d;
    }
    upload32(0x2000, blk, true);
    upload32(0x2040, blk + 32, false);

    static const uint16_t ps[2] = { 0x1000, 0x0040 };
    for (int p = 0; p < 2; p++) for (int i = 0; i < 24; i++) kon[p][i] = runCase(ps[p], i * 32);
    keyOffAll();
    setCnt(0xC000);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("   D   pitch 1000h  pitch 0040h   (cycles from the KON write to IRQ9; 0 = none in 100 samples)\n");
    for (int i = 0; i < 24; i++) printf("  %3d  %10u  %10u\n", i * 32, (unsigned)kon[0][i], (unsigned)kon[1][i]);
    printf("\nspu/spu-kon-touch: Done.\n");
    for (;;) VSync(0);
}
