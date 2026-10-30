/*
 * spu/spu-fifo3 - the manual write's FIFO, the rest of the map (2026-10-28, after hwtests/spu-fifo2 on the SCPH-7501).
 *
 * WHY. spu-fifo2 (four runs alike): 64 written stopped land 0-31 and the rest never (STOP64+GO), yet bit 10 clears after
 * 701 cycles where 32 or 48 take ~1,500; halfwords written with manual write already on wait in the FIFO for the next
 * start (RUN8+STOPGO landed them at 1200h); a second 32 after the first (AFTER) READ BACK as 0-11 then 11 over and over.
 * Asked here: bit 10 and the landing by count past 32; whether SPUCNT written with manual write again (1 -> 1, psx-spx's
 * multi-block recipe) starts the waiting ones; where AFTER's second 32 really went and whether its read is the read's.
 *
 * WHAT IT DOES. As spu-fifo2 (1000h-13FFh zeroed, the case, read by DMA4, runs p@i+n: halfwords i.. at offset p).
 *   STOPn        n written stopped, manual write, busy waited (n = 40 56 60 63 64 65 72 96 128)
 *   AGAIN        32 stopped, manual write, busy, 32 more, SPUCNT written with manual write again, busy
 *   AFTER+GO     AFTER (32 stopped, manual write, busy, 32 more), then stopped, address 1200h, manual write
 *   AFTER        AFTER read twice: the second read straight after the first
 *   RUN40+GO     manual write first, 40 written, stopped, address 1200h, manual write (does the waiting FIFO hold 32?)
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-fifo3 --ntsc`.
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
    printf("\nspu/spu-fifo3\n");
    printf("The manual write FIFO past 32, and what waits (runs p@i+n: halfwords i.. at offset p from 1000h). No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    w16(0x1AC, 0x0004);
    setCnt(0xC000);

    static const int NS[9] = { 40, 56, 60, 63, 64, 65, 72, 96, 128 };
    for (int c = 0; c < 9; c++) {
        char name[16]; sprintf(name, "STOP%d", NS[c]);
        zeroArea(); addr(0x1000); put(0, NS[c]); startManual(); report(name);
    }
    zeroArea(); addr(0x1000); put(0, 32); startManual(); put(32, 32); w16(SPUCNT, cnt); waitBusy(); report("AGAIN");
    zeroArea(); addr(0x1000); put(0, 32); startManual(); put(32, 32); waitBusy(); stopXfer(); addr(0x1200); startManual();
    report("AFTER+GO");
    zeroArea(); addr(0x1000); put(0, 32); startManual(); put(32, 32); waitBusy(); report("AFTER"); report("AFTER read 2");
    zeroArea(); addr(0x1000); setCnt((cnt & ~0x30) | 0x10); put(0, 40); waitBusy(); stopXfer(); addr(0x1200); startManual();
    report("RUN40+GO");

    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fifo3: Done.\n");
    for (;;) VSync(0);
}
