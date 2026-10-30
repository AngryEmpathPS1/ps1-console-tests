/*
 * spu/spu-rearm - an IRQ9 re-armed on the halfword a voice just read: when does it fire again? (2026-10-28)
 *
 * WHY. spu-lead's MIDBLOCK (nugget's tests/spu/spu-irq.c probe) on every console: two hits 2,258-2,301 cycles apart
 * (three samples), then none. This core (M242's late fire: armed while the decoder's last read is the IRQ's halfword, it
 * fires at once) re-arms about once a sample and fires five times. Which part differs - the flag's clear, the late fire,
 * how long "the last read" lasts - one MIDBLOCK run cannot say. spu-lead's own upload is 64 halfwords at once, which a
 * console cuts to 32 (hwtests/spu-fifo); here the blocks go up 32 at a time.
 *
 * WHAT IT DOES. Interrupts off, IRQ9 polled as SPUSTAT bit 6, Timer 2 at the system clock. Voice 1 on nugget's eight
 * blocks at 2000h (levels (b+1)&7, block 0 flags 06h, block 7 03h), pitch 1000h, IRQ9 at 2008h (block 0's halfword 4).
 * Each case keys the voice on afresh (the key-on just after SPUSTAT bit 11 falls), waits for the first hit (H1), then
 * clears SPUCNT bit 6 (the acknowledge) and polls for the flag to go (CLR), waits until D samples after H1, writes the
 * IRQ address again and sets bit 6 (ARM), and polls 40 samples for a second hit (H2). Printed in cycles from H1.
 *   D = 0 1 2 3 4 5 6 8      the address written again with the enable
 *   D = 0 2 4 (bit only)     the enable alone, the address left as it was
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-rearm --ntsc`.
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
struct Row { int d; bool addr; bool h1; uint32_t clr, arm, h2; bool got2; };
static Row rows[11];

static void runCase(Row &R) {
    keyOffAll();
    w16(0x10, 0); w16(0x12, 0); w16(0x14, 0x1000);
    w16(0x16, 0x2000 / 8); w16(0x1E, 0x2000 / 8);
    w16(0x18, 0x000F); w16(0x1A, 0x1FC0);
    irqOff();
    w16(0x1A4, 0x2008 / 8); setCnt(cnt | 0x40);
    syncFlip();
    const uint32_t t0 = now();
    w16(0x188, 0x0002);
    R.h1 = false; R.got2 = false; R.clr = R.arm = R.h2 = 0;
    uint32_t h1 = 0;
    while (now() - t0 < 200 * SAMPLE) if (irqFlag()) { h1 = now(); R.h1 = true; break; }
    if (!R.h1) { irqOff(); return; }
    w16(SPUCNT, cnt & ~0x40); cnt &= ~0x40;
    while (irqFlag() && now() - h1 < 100000) {}
    R.clr = now() - h1;
    while (now() - h1 < (uint32_t)R.d * SAMPLE) {}
    if (R.addr) w16(0x1A4, 0x2008 / 8);
    cnt |= 0x40; w16(SPUCNT, cnt);
    R.arm = now() - h1;
    while (now() - h1 < R.arm + 40 * SAMPLE) if (irqFlag()) { R.h2 = now() - h1; R.got2 = true; break; }
    irqOff();
}

int main() {
    printf("\nspu/spu-rearm\n");
    printf("IRQ9 re-armed on the halfword a voice just read: cycles from the first hit. No assertions.\n\n");
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

    static const int ds[11] = { 0, 1, 2, 3, 4, 5, 6, 8, 0, 2, 4 };
    for (int i = 0; i < 11; i++) { rows[i].d = ds[i]; rows[i].addr = i < 8; runCase(rows[i]); }
    keyOffAll();
    setCnt(0xC000);
    write16(T2_MODE, m2);
    ExitCriticalSection();

    printf("   D  re-arm         CLR    ARM     H2\n");
    for (int i = 0; i < 11; i++) {
        const Row &R = rows[i];
        printf("  %2d  %-12s", R.d, R.addr ? "addr + bit" : "bit only");
        if (!R.h1) { printf("  no first hit\n"); continue; }
        printf(" %6u %6u ", (unsigned)R.clr, (unsigned)R.arm);
        if (R.got2) printf("%6u\n", (unsigned)R.h2); else printf("  none\n");
    }
    printf("\nspu/spu-rearm: Done.\n");
    for (;;) VSync(0);
}
