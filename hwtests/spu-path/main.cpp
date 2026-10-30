/*
 * spu/spu-path - a voice's way from sound RAM to its capture ring, link by link (2026-10-08 night, the SCPH-7501 under
 * the test runner).
 *
 * WHY. Under the runner tonight spu-lead (on console day: onset 9, every row) read onset -1, a ring of zeros and IRQ9
 * once - and spu-latch's second run onset 65535 on every row - while hwtests/spu-health's voice keyed on reaches ENVX
 * 7FFFh, and sound RAM at 1000h and 40000h keeps what is written there (the reverb, on as Unirom leaves it, writes only
 * its own area at 7E0C0h). So: does spu-lead's manual write land, how much of a 64-halfword burst does the FIFO keep,
 * and does voice 1's capture ring take the voice's output?
 *
 * WHAT IT DOES. spu-lead's own helpers and setup (SPUCNT C000h, transfer control 4, DMA4 on). 1: 64 halfwords written
 * by manual write at 1000h (spu-lead's burst), read back by DMA4: how many landed, which. 2: the same with 32. 3: a loud
 * looping two-block sample (nibbles 7, shift 0) at 2000h, voice 1 on it (pitch 1000h, full volume, ADSR1 00FFh), keyed
 * on; 20,000 cycles; voice 1's capture ring (800h-BFFh) and voice 3's (C00h-FFFh) read back: nonzero halfwords counted,
 * the first few printed; ENVX. Then 1DA0h written 0000h and the ring again (spu-curvol leaves 1DA0h at A5A5h, its last
 * pattern - the 7501 read it back - and its capture stopped after spu-curvol ran). Then SPUCNT as found.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-path --ntsc`.
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
static void manualWrite(uint32_t a, const uint16_t *d, int n) {
    setCnt(cnt & ~0x30);
    w16(0x1A6, a / 8);
    for (int i = 0; i < n; i++) w16(0x1A8, d[i]);
    setCnt((cnt & ~0x30) | 0x10);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {}
    setCnt(cnt & ~0x30);
}
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    w16(0x1A6, a / 8);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    { const uint32_t t0 = now(); while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 2000000) {} }
    setCnt(cnt & ~0x30);
}

static uint16_t buf[1024] __attribute__((aligned(16)));
static void ring(const char* label);
static uint16_t pat[64];

static void burst(int n) {
    for (int i = 0; i < 64; i++) pat[i] = (uint16_t)(0x5A00 + i);
    static uint16_t zero[64];
    manualWrite(0x1000, zero, 32); manualWrite(0x1040, zero, 32);
    manualWrite(0x1000, pat, n);
    dmaRead(0x1000, buf, 256);
    int landed = 0, firstMiss = -1;
    for (int i = 0; i < n; i++) {
        bool at = false;
        for (int k = 0; k < 4; k++) if (buf[i + k] == pat[i]) at = true;   /* the read may come back a halfword or two late */
        if (at) landed++; else if (firstMiss < 0) firstMiss = i;
    }
    printf("  manual write of %2d halfwords at 1000h: %2d found in the read-back (first missing %d); read: %04x %04x %04x %04x ... %04x %04x\n",
           n, landed, firstMiss, buf[0], buf[1], buf[2], buf[3], buf[32], buf[33]);
}

/* voice 1 on the loud loop at 2000h, 20,000 cycles, its capture ring (800h-BFFh) and voice 3's read back */
static void ring(const char* label) {
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    waitCycles(20000);
    w16(0x10, 0x3FFF); w16(0x12, 0x3FFF); w16(0x14, 0x1000); w16(0x16, 0x2000 / 8); w16(0x1E, 0x2000 / 8);
    w16(0x18, 0x00FF); w16(0x1A, 0x0000);
    w16(0x188, 0x0002); w16(0x18A, 0x0000);
    waitCycles(20000);
    const uint16_t env = r16(0x1C);
    dmaRead(0x0800, buf, 2048);
    int nz1 = 0, nz3 = 0, f1 = -1;
    for (int i = 0; i < 512; i++) if (buf[i]) { nz1++; if (f1 < 0) f1 = i; }
    for (int i = 512; i < 1024; i++) if (buf[i]) nz3++;
    printf("  %-20s voice 1 on a loud loop: ENVX %04x; its capture ring %3d of 512 halfwords nonzero (first at %d: %04x %04x); voice 3's %d\n",
           label, env, nz1, f1, f1 >= 0 ? buf[f1] : 0, f1 >= 0 ? buf[f1 + 1] : 0, nz3);
    w16(0x18C, 0x0002); w16(0x18E, 0x0000);
}

int main() {
    printf("\nspu/spu-path\n");
    printf("A voice's way from sound RAM to its capture ring, link by link. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    printf("  found: SPUCNT %04x SPUSTAT %04x XFER %04x DPCR %08x SPU_DELAY %08x\n", cnt0, r16(SPUSTAT), r16(0x1AC),
           (unsigned)read32(0x1F8010F0), (unsigned)read32(0x1F801014));
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    burst(64);
    burst(32);
    /* a loud looping sample: block 0 loop start, block 1 loop end + repeat; shift 0, filter 0, nibbles 7 */
    static uint16_t smp[16];
    for (int b = 0; b < 2; b++) {
        smp[b * 8] = (uint16_t)(0x0000 | ((b == 0 ? 0x04 : 0x03) << 8));
        for (int i = 1; i < 8; i++) smp[b * 8 + i] = 0x7777;
    }
    manualWrite(0x2000, smp, 16);
    for (int v = 0; v < 24; v++) { const uint32_t b = v * 16; w16(b + 0, 0); w16(b + 2, 0); w16(b + 8, 0); w16(b + 0xA, 0); w16(b + 0xC, 0); }
    w16(0x18C, 0xFFFF); w16(0x18E, 0x00FF);
    waitCycles(20000);
    ring("1DA0h as found");
    const uint16_t da0 = r16(0x1A0);
    w16(0x1A0, 0x0000);
    ring("1DA0h written 0000h");
    printf("  1DA0h was %04x, now %04x\n", da0, r16(0x1A0));
    if (0) {
    w16(0x10, 0x3FFF); w16(0x12, 0x3FFF); w16(0x14, 0x1000); w16(0x16, 0x2000 / 8); w16(0x1E, 0x2000 / 8);
    w16(0x18, 0x00FF); w16(0x1A, 0x0000);
    w16(0x188, 0x0002); w16(0x18A, 0x0000);
    waitCycles(20000);
    const uint16_t env = r16(0x1C);
    dmaRead(0x0800, buf, 2048);
    int nz1 = 0, nz3 = 0, f1 = -1;
    for (int i = 0; i < 512; i++) if (buf[i]) { nz1++; if (f1 < 0) f1 = i; }
    for (int i = 512; i < 1024; i++) if (buf[i]) nz3++;
    printf("  voice 1 on a loud loop: ENVX %04x; its capture ring (800h-BFFh) %d of 512 halfwords nonzero (first at %d: %04x %04x %04x %04x); voice 3's %d\n",
           env, nz1, f1, f1 >= 0 ? buf[f1] : 0, f1 >= 0 ? buf[f1 + 1] : 0, f1 >= 0 ? buf[f1 + 2] : 0, f1 >= 0 ? buf[f1 + 3] : 0, nz3);
    }
    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-path: Done.\n");
    for (;;) VSync(0);
}
