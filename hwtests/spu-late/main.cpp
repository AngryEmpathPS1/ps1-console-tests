/*
 * spu/spu-late - IRQ9's late fire by the order of its two writes (2026-10-28, after hwtests/spu-rearm on the SCPH-7501).
 *
 * WHY. v0.377.208's KNOWN-OPEN: armed onto what a voice just read, IRQ9 fires 384-448 cycles after the IRQ address is
 * written with the enable already on (spu-irq, voice 0 at pitch 1, every console) but 1,359 after the address then the
 * enable (spu-rearm, voice 1 at pitch 1000h, the 7501), and not at all for the enable alone (spu-rearm "bit only"). This
 * core's late check runs at the next sample whatever armed it.
 *
 * WHAT IT DOES. As spu-rearm: nugget's eight blocks at 2000h (uploaded 32 at a time), voice 1 keyed on afresh after
 * SPUSTAT bit 11 falls, the IRQ address parked at 0800h (nothing reads it) until the key-on, then 2008h with the enable:
 * the first hit H1, the acknowledge (bit 6 cleared, the flag polled away), then at once one of
 *   BIT,ADDR   the enable, then the address (2008h) again
 *   ADDR,BIT   the address, then the enable
 *   ADDR+1S    the address, the enable a sample (768 cycles) later
 *   BIT        the enable alone
 *   ADDR(on)   the enable, a sample later the address (the enable on throughout)
 * at pitches 1000h, 0400h and 0100h, and the second hit H2 polled for 40 samples from the first write. Printed in cycles
 * from H1: CLR (the flag gone), W1 and W2 (each write done), H2.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-late --ntsc`.
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
struct Row { uint16_t pitch; int kind; bool h1; uint32_t clr, w1, w2, h2; bool got2; };
static Row rows[15];
static const char *KIND[5] = { "BIT,ADDR", "ADDR,BIT", "ADDR+1S", "BIT", "ADDR(on)" };

static void runCase(Row &R) {
    keyOffAll();
    w16(0x1A4, 0x0800 / 8);                                      /* parked: nothing reads 0800h */
    irqOff();
    w16(0x10, 0); w16(0x12, 0); w16(0x14, R.pitch);
    w16(0x16, 0x2000 / 8); w16(0x1E, 0x2000 / 8);
    w16(0x18, 0x000F); w16(0x1A, 0x1FC0);
    syncFlip();
    const uint32_t t0 = now();
    w16(0x188, 0x0002);
    w16(0x1A4, 0x2008 / 8); setCnt(cnt | 0x40);
    R.h1 = false; R.got2 = false; R.clr = R.w1 = R.w2 = R.h2 = 0;
    uint32_t h1 = 0;
    while (now() - t0 < 200 * SAMPLE) if (irqFlag()) { h1 = now(); R.h1 = true; break; }
    if (!R.h1) { irqOff(); return; }
    w16(SPUCNT, cnt & ~0x40); cnt &= ~0x40;
    while (irqFlag() && now() - h1 < 100000) {}
    R.clr = now() - h1;
    switch (R.kind) {
    case 0: cnt |= 0x40; w16(SPUCNT, cnt); R.w1 = now() - h1; w16(0x1A4, 0x2008 / 8); R.w2 = now() - h1; break;
    case 1: w16(0x1A4, 0x2008 / 8); R.w1 = now() - h1; cnt |= 0x40; w16(SPUCNT, cnt); R.w2 = now() - h1; break;
    case 2: w16(0x1A4, 0x2008 / 8); R.w1 = now() - h1; while (now() - h1 < R.w1 + SAMPLE) {}
            cnt |= 0x40; w16(SPUCNT, cnt); R.w2 = now() - h1; break;
    case 3: cnt |= 0x40; w16(SPUCNT, cnt); R.w1 = R.w2 = now() - h1; break;
    default: cnt |= 0x40; w16(SPUCNT, cnt); R.w1 = now() - h1;
            while (now() - h1 < R.w1 + SAMPLE && !irqFlag()) {}
            w16(0x1A4, 0x2008 / 8); R.w2 = now() - h1; break;
    }
    while (now() - h1 < R.w1 + 40 * SAMPLE) if (irqFlag()) { R.h2 = now() - h1; R.got2 = true; break; }
    irqOff();
}

int main() {
    printf("\nspu/spu-late\n");
    printf("IRQ9 late fire by the order of its writes: cycles from the first hit. No assertions.\n\n");
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

    static const uint16_t ps[3] = { 0x1000, 0x0400, 0x0100 };
    for (int p = 0; p < 3; p++)
        for (int k = 0; k < 5; k++) { Row &R = rows[p * 5 + k]; R.pitch = ps[p]; R.kind = k; runCase(R); }
    keyOffAll();
    setCnt(0xC000);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("pitch  order        CLR     W1     W2     H2\n");
    for (int i = 0; i < 15; i++) {
        const Row &R = rows[i];
        printf("%04x   %-9s", R.pitch, KIND[R.kind]);
        if (!R.h1) { printf("  no first hit\n"); continue; }
        printf(" %6u %6u %6u ", (unsigned)R.clr, (unsigned)R.w1, (unsigned)R.w2);
        if (R.got2) printf("%6u\n", (unsigned)R.h2); else printf("  none\n");
    }
    printf("\nspu/spu-late: Done.\n");
    for (;;) VSync(0);
}
