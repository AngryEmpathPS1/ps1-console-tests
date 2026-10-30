/*
 * spu/spu-fifo2 - where every halfword of a manual write lands (2026-10-28, after hwtests/spu-fifo on six consoles).
 *
 * WHY. spu-fifo, 42 runs on the 1001, 5501, 7001, 7501, 7502 and 9001, all alike: 64 written stopped land the first 32;
 * 64 written with manual write already set land none in 1000h-10FFh; a second 32 after the first lands 5-8 more
 * (TWICE) or scatters 52-56 halfwords to wrong places (AFTER). It counts, it does not say WHERE. This core keeps every
 * halfword (src/psx.js SPU.transferWrite), so spu-lead's 64-halfword uploads land whole here and half on a console.
 *
 * WHAT IT DOES. The SPU on (C000h), transfer control 4, DMA4 on. Each case: 1000h-13FFh zeroed (16 bursts of 32), the
 * case's halfwords 5A00h + i, then 1000h-13FFh read by DMA4 and printed as RUNS: "p@i+n" = halfwords i..i+n-1 found at
 * offsets p..p+n-1 (halfwords from 1000h), then how many other nonzero halfwords. Busy = cycles SPUSTAT bit 10 stayed
 * set after the case's last switch to manual write (or after its last FIFO write, when manual write was already on).
 *   STOP64       64 written stopped; manual write; busy waited; stopped
 *   STOP64+GO    the same, then manual write again with nothing written (do 32-63 wait in the FIFO?)
 *   RUN64        manual write set first, then 64 back to back
 *   RUN8         manual write set first, then 8
 *   RUN8+STOPGO  RUN8, then stopped, address 1200h, manual write with nothing written (where did they go?)
 *   TWICE        32 stopped, manual write, 32 more at once
 *   AFTER        32 stopped, manual write, busy cleared, 32 more (still manual write)
 *   AFTER+ADDR   the same with the address (1040h) written again before the second 32
 *   CHAIN        32 stopped, manual write, busy, stopped, 32 more stopped with no address write, manual write
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-fifo2 --ntsc`.
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
static uint32_t busyFor;
static void waitBusy() { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} busyFor = now() - t0; }
static void startManual() { setCnt((cnt & ~0x30) | 0x10); waitBusy(); }
static void stopXfer() { setCnt(cnt & ~0x30); }
static void addr(uint32_t a) { w16(0x1A6, a / 8); }
static void put(int from, int n) { for (int i = from; i < from + n; i++) w16(0x1A8, (uint16_t)(0x5A00 + i)); }
static void zeroArea() {
    for (int b = 0; b < 16; b++) { stopXfer(); addr(0x1000 + b * 64); for (int i = 0; i < 32; i++) w16(0x1A8, 0); startManual(); }
    stopXfer();
}
static void dmaRead(uint32_t a, void *dst, uint32_t bytes) {
    addr(a);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    setCnt((cnt & ~0x30) | 0x30);
    write32(0x1F801014, (read32(0x1F801014) & 0xF0FFFFFFu) | 0x22000000u);
    write32(0x1F8010C0, (uint32_t)dst & 0x1FFFFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    write32(0x1F8010C8, 0x01000200);
    { const uint32_t t0 = now(); while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 2000000) {} }
    setCnt(cnt & ~0x30);
}

static uint16_t buf[512] __attribute__((aligned(16)));

static void report(const char *name) {
    const uint32_t busy = busyFor;
    stopXfer();
    for (int i = 0; i < 512; i++) buf[i] = 0;
    dmaRead(0x1000, buf, 1024);
    printf("  %-12s busy %7u  runs:", name, (unsigned)busy);
    int other = 0, runs = 0;
    for (int p = 0; p < 512; ) {
        const uint16_t v = buf[p];
        if ((v & 0xFF00) != 0x5A00) { if (v) other++; p++; continue; }
        int n = 1;
        while (p + n < 512 && buf[p + n] == (uint16_t)(v + n)) n++;
        if (runs < 12) printf(" %d@%d+%d", p, v & 0xFF, n);
        runs++; p += n;
    }
    if (!runs) printf(" none");
    if (runs > 12) printf(" (+%d runs)", runs - 12);
    printf("  other %d\n", other);
}

int main() {
    printf("\nspu/spu-fifo2\n");
    printf("Where each halfword of a manual write lands (runs p@i+n: halfwords i.. at offset p from 1000h). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    w16(0x1AC, 0x0004);
    setCnt(0xC000);

    zeroArea(); addr(0x1000); put(0, 64); startManual(); report("STOP64");
    zeroArea(); addr(0x1000); put(0, 64); startManual(); stopXfer(); startManual(); report("STOP64+GO");
    zeroArea(); addr(0x1000); setCnt((cnt & ~0x30) | 0x10); put(0, 64); waitBusy(); report("RUN64");
    zeroArea(); addr(0x1000); setCnt((cnt & ~0x30) | 0x10); put(0, 8); waitBusy(); report("RUN8");
    zeroArea(); addr(0x1000); setCnt((cnt & ~0x30) | 0x10); put(0, 8); waitBusy(); stopXfer(); addr(0x1200); startManual();
    report("RUN8+STOPGO");
    zeroArea(); addr(0x1000); put(0, 32); setCnt((cnt & ~0x30) | 0x10); put(32, 32); waitBusy(); report("TWICE");
    zeroArea(); addr(0x1000); put(0, 32); startManual(); put(32, 32); waitBusy(); report("AFTER");
    zeroArea(); addr(0x1000); put(0, 32); startManual(); addr(0x1040); put(32, 32); waitBusy(); report("AFTER+ADDR");
    zeroArea(); addr(0x1000); put(0, 32); startManual(); stopXfer(); put(32, 32); startManual(); report("CHAIN");

    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fifo2: Done.\n");
    for (;;) VSync(0);
}
