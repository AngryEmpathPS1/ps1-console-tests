/*
 * spu/spu-fifo4 - a DMA read started with halfwords waiting in the manual write's FIFO (2026-11-24, the SPU session)
 *
 * WHY. hwtests/spu-fifo3's AFTER row on every console: with 32 halfwords waiting in the FIFO (written with manual write on,
 * never started), a DMA read of 1000h returns the first K halfwords right - K = 9 on the SCPH-1001 and 7001, 10 on the 9001
 * and 7501, 11 on the 5501, 12 on the SCPH-1000, the PSone and the 7502 - then the K-th over and over up to the 32nd; a second
 * read is right. The emulator drops the waiting halfwords and reads right. Sound RAM past the 32nd was zero there, so what the
 * read does from the 33rd on was not seen, nor what K follows, nor what fewer waiting halfwords do, nor where they go.
 *
 * WHAT IT DOES. Interrupts off, sound muted. For each case: 1000h-13FFh written by DMA with a ramp (halfword n = 6000h + n) and
 * 1A00h-1A3Fh with zeroes; the transfer address 1800h; W halfwords (5A00h + i) left waiting - "run": written with manual write
 * on and idle, "stop": written stopped; stopped; the address 1000h, DMA read mode, D more cycles idled, 512 halfwords read by
 * DMA4 (blocks of 16 words); the same read again; then stopped, the address 1A00h, manual write started and 1A00h-1A3Fh read.
 *   R way W D : gap g : read 1 <segments> | read 2 <segments> | landed n first v
 *       gap = cycles (Timer 2) from the store of the read mode to the store that starts the DMA
 *       segments: p@q+n = n halfwords from index p reading the ramp's q, q+1 ... (p@p = right); p=vvvv*n = n halfwords from
 *       p all reading vvvv; the first 10 segments
 *       landed = how many of 1A00h's 32 halfwords read 5Axxh after the manual start, and the first of them
 * No assertions. Every wait is bounded. THE RULE: run here first.
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
static inline uint32_t stamp(uint16_t t) { clk += (uint16_t)(t - lastT); lastT = t; return clk; }
static uint32_t now() { return stamp(read16(T2_COUNT)); }
static void waitCycles(uint32_t c) { const uint32_t t0 = now(); while (now() - t0 < c) {} }

static uint16_t cnt;
static void setCnt(uint16_t v) {
    cnt = v;
    w16(SPUCNT, v);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x3F) != (v & 0x3F) && now() - t0 < 100000) {}
}
static void waitBusy() { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} }
static void stopXfer() { setCnt(cnt & ~0x30); }
static void addr(uint32_t a) { w16(0x1A6, a / 8); }
static void dmaGo(void* ram, uint32_t bytes, bool toSpu) {
    write32(0x1F8010C0, (uint32_t)ram & 0x1FFFFFu);
    write32(0x1F8010C4, ((bytes / 64) << 16) | 0x10);
    const uint16_t t = read16(T2_COUNT);
    write32(0x1F8010C8, toSpu ? 0x01000201u : 0x01000200u);
    stamp(t);
    { const uint32_t t0 = now(); while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 4000000) {} }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); printf("T\n"); }
}
static void dmaWrite(uint32_t a, void* src, uint32_t bytes) {
    stopXfer(); addr(a); setCnt((cnt & ~0x30) | 0x20);
    dmaGo(src, bytes, true);
    waitCycles(60000);
    stopXfer();
}
static uint32_t gap;
/* the read: the mode's store stamped, the usual wait for SPUSTAT's mode bits, `extra` cycles more, the DMA's start stamped */
static void dmaRead(uint32_t a, void* dst, uint32_t bytes, uint32_t extra) {
    addr(a);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != a / 8; i++) {}
    cnt = (uint16_t)((cnt & ~0x30) | 0x30);
    const uint16_t t = read16(T2_COUNT);
    w16(SPUCNT, cnt);
    const uint32_t t1 = stamp(t);
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x3F) != (cnt & 0x3F) && now() - t0 < 100000) {} }
    if (extra) waitCycles(extra);
    dmaGo(dst, bytes, false);
    gap = clk - t1;                    /* clk was last stamped by dmaGo's own poll; corrected below from its start stamp */
    setCnt(cnt & ~0x30);
}

static uint16_t ramp[512] __attribute__((aligned(16)));
static uint16_t zero[32] __attribute__((aligned(16)));
static uint16_t buf[512] __attribute__((aligned(16)));
static uint16_t buf2[512] __attribute__((aligned(16)));
static uint16_t land[32] __attribute__((aligned(16)));

static void segments(const uint16_t* b) {
    int segs = 0;
    for (int p = 0; p < 512 && segs < 10; ) {
        const uint16_t v = b[p];
        int n = 1;
        if ((v & 0xFE00) == 0x6000 && (p + 1 >= 512 || b[p + 1] != v)) {
            while (p + n < 512 && b[p + n] == (uint16_t)(v + n)) n++;
            printf(" %d@%d+%d", p, v - 0x6000, n);
        } else {
            while (p + n < 512 && b[p + n] == v) n++;
            printf(" %d=%04x*%d", p, v, n);
        }
        p += n; segs++;
    }
}

static void fifoCase(bool run, int w, uint32_t extra) {
    dmaWrite(0x1000, ramp, 1024);
    dmaWrite(0x1A00, zero, 64);
    stopXfer(); addr(0x1800);
    if (run) { setCnt((cnt & ~0x30) | 0x10); waitBusy(); }
    for (int i = 0; i < w; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    waitCycles(3000);
    if (run) waitBusy();
    stopXfer();
    for (int i = 0; i < 512; i++) { buf[i] = 0xEEEE; buf2[i] = 0xEEEE; }
    for (int i = 0; i < 32; i++) land[i] = 0xEEEE;
    /* the gap: the DMA's start stamp less the mode's - dmaGo stamps its start before polling, so take it there */
    addr(0x1000);
    for (int i = 0; i < 0xF01 && r16(0x1A6) != 0x1000 / 8; i++) {}
    cnt = (uint16_t)((cnt & ~0x30) | 0x30);
    const uint16_t t = read16(T2_COUNT);
    w16(SPUCNT, cnt);
    const uint32_t t1 = stamp(t);
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x3F) != (cnt & 0x3F) && now() - t0 < 100000) {} }
    if (extra) waitCycles(extra);
    write32(0x1F8010C0, (uint32_t)buf & 0x1FFFFFu);
    write32(0x1F8010C4, (16u << 16) | 0x10);
    const uint16_t tg = read16(T2_COUNT);
    write32(0x1F8010C8, 0x01000200u);
    const uint32_t g = stamp(tg) - t1;
    { const uint32_t t0 = now(); while ((read32(0x1F8010C8) & 0x01000000) && now() - t0 < 4000000) {} }
    if (read32(0x1F8010C8) & 0x01000000) { write32(0x1F8010C8, 0); printf("T\n"); }
    setCnt(cnt & ~0x30);
    dmaRead(0x1000, buf2, 1024, 0);
    stopXfer(); addr(0x1A00); setCnt((cnt & ~0x30) | 0x10); waitBusy(); waitCycles(3000); stopXfer();
    dmaRead(0x1A00, land, 64, 0);
    int n = 0, first = -1;
    for (int i = 0; i < 32; i++) if ((land[i] & 0xFF00) == 0x5A00) { if (first < 0) first = land[i]; n++; }
    now();
    printf("R %s %d %u : gap %u : read 1", run ? "run" : "stop", w, (unsigned)extra, (unsigned)g);
    segments(buf); now();
    printf(" | read 2"); segments(buf2); now();
    printf(" | landed %d first %04x\n", n, first < 0 ? 0 : first);
    now();
}

int main() {
    printf("\nspu/spu-fifo4\n");
    printf("A DMA read started with halfwords waiting in the manual write's FIFO. R way W D : gap : read 1 ... | read 2 ... | landed ... No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    const uint32_t spuDelay = read32(0x1F801014);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    write32(0x1F801014, (spuDelay & 0xF0FFFFFFu) | 0x22000000u);
    w16(0x180, 0); w16(0x182, 0);
    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    for (int i = 0; i < 512; i++) ramp[i] = (uint16_t)(0x6000 + i);
    for (int i = 0; i < 32; i++) zero[i] = 0;

    fifoCase(true, 0, 0);
    fifoCase(true, 32, 0);
    fifoCase(true, 32, 0);
    fifoCase(true, 32, 100);
    fifoCase(true, 32, 200);
    fifoCase(true, 32, 400);
    fifoCase(true, 32, 800);
    fifoCase(true, 32, 3000);
    fifoCase(true, 1, 0);
    fifoCase(true, 4, 0);
    fifoCase(true, 8, 0);
    fifoCase(true, 16, 0);
    fifoCase(true, 24, 0);
    fifoCase(true, 31, 0);
    fifoCase(true, 40, 0);
    fifoCase(false, 1, 0);
    fifoCase(false, 16, 0);
    fifoCase(false, 32, 0);
    fifoCase(false, 32, 800);

    write32(0x1F801014, spuDelay);
    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fifo4: Done.\n");
    for (;;) VSync(0);
}
