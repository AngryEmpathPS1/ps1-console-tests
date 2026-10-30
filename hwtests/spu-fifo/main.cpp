/*
 * spu/spu-fifo - the manual write's FIFO (1F801DA8h): how many halfwords it takes, and what happens to the rest
 * (2026-10-08 night, after hwtests/spu-path on the SCPH-7501).
 *
 * WHY. psx-spx: 1F801DA8h "Data (max 32 halfwords)". spu-path wrote 64 halfwords at 1000h with the transfer stopped,
 * then switched SPUCNT to manual write: the 7501 kept the first 32 and dropped the rest. This core writes every halfword
 * straight to sound RAM as it comes (src/psx.js SPU.transferWrite), so all 64 land. spu-xfer (M376) wrote eight at a
 * time, so nothing measured the size. spu-lead's own upload is 64 halfwords: on a console its last four blocks never
 * landed.
 *
 * WHAT IT DOES. The SPU on (C000h), transfer control 4, DMA4 on. Each case: 128 halfwords of 0000h written over 1000h
 * first (four bursts of 32), then the case's halfwords 5A00h + i, then a DMA4 read of 1000h-10FFh, and the halfwords
 * that landed printed as ranges of i (a read comes back one halfword late - matched at the shift that fits).
 *   STOPPED n   the transfer stopped; n written; then manual write, busy waited for, stopped (31 32 33 48 64)
 *   RUNNING 64  manual write set first, then 64 written back to back; busy waited for
 *   SLOW 64     manual write set first, then 64 written 200 cycles apart
 *   TWICE 32+32 32 written stopped, manual write, then 32 more at once (before busy clears), busy waited for
 *   AFTER 32+32 the same, the second 32 written after busy cleared (still in manual write)
 * And SPUSTAT bit 10 (busy) just after the switch to manual write and how long it stays set, per STOPPED case.
 *
 * THE RULE: run it here first: `node hwtests/run.js spu-fifo --ntsc`.
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
static uint32_t busyFor;   /* cycles SPUSTAT bit 10 stayed set after the last switch to manual write */
static void startManual() {
    setCnt((cnt & ~0x30) | 0x10);
    const uint32_t t0 = now();
    while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {}
    busyFor = now() - t0;
}
static void stopXfer() { setCnt(cnt & ~0x30); }
static void zeroArea() {
    static uint16_t z[32];
    for (int b = 0; b < 4; b++) { stopXfer(); w16(0x1A6, (0x1000 + b * 64) / 8); for (int i = 0; i < 32; i++) w16(0x1A8, z[i]); startManual(); }
    stopXfer();
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

static uint16_t buf[256] __attribute__((aligned(16)));

/* which of 5A00h + i (i < n) are where they belong (1000h + 2i), the read matched at the shift that fits best */
static void report(const char* name, int n) {
    for (int i = 0; i < 256; i++) buf[i] = 0;
    dmaRead(0x1000, buf, 512);
    int bestShift = 0, best = -1;
    for (int sh = 0; sh <= 2; sh++) {
        int ok = 0;
        for (int i = 0; i < 128 && i + sh < 256; i++) if (buf[i + sh] == (uint16_t)(0x5A00 + i)) ok++;
        if (ok > best) { best = ok; bestShift = sh; }
    }
    printf("  %-14s landed %3d of %3d:", name, best, n);
    int runStart = -1;
    for (int i = 0; i <= 128; i++) {
        const bool at = i < 128 && i + bestShift < 256 && buf[i + bestShift] == (uint16_t)(0x5A00 + i);
        if (at && runStart < 0) runStart = i;
        if (!at && runStart >= 0) { printf(" %d-%d", runStart, i - 1); runStart = -1; }
    }
    /* anything else written there (a halfword of the case in the wrong place) */
    int stray = 0;
    for (int i = 0; i < 128; i++) { const uint16_t v = buf[i + bestShift]; if (v && (v & 0xFF00) == 0x5A00 && v != (uint16_t)(0x5A00 + i)) stray++; }
    printf("%s  (read %d late, %d strays)", best ? "" : " none", bestShift, stray);
}

int main() {
    printf("\nspu/spu-fifo\n");
    printf("The manual write's FIFO: how many halfwords it takes, by case. No assertions.\n\n");
    EnterCriticalSection();
    const uint16_t m2 = read16(T2_MODE);
    write16(T2_MODE, 0);
    lastT = read16(T2_COUNT); clk = 0;
    const uint16_t cnt0 = r16(SPUCNT);
    write32(0x1F8010F0, read32(0x1F8010F0) | 0x000B0000);        /* DMA4 on */
    w16(0x1AC, 0x0004);
    setCnt(0xC000);
    static const int NS[5] = { 31, 32, 33, 48, 64 };
    for (int c = 0; c < 5; c++) {
        zeroArea();
        stopXfer(); w16(0x1A6, 0x1000 / 8);
        for (int i = 0; i < NS[c]; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
        startManual(); stopXfer();
        char name[16]; sprintf(name, "STOPPED %d", NS[c]);
        report(name, NS[c]); printf("  busy %u cycles\n", (unsigned)busyFor);
    }
    zeroArea();
    stopXfer(); w16(0x1A6, 0x1000 / 8); setCnt((cnt & ~0x30) | 0x10);
    for (int i = 0; i < 64; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} }
    stopXfer(); report("RUNNING 64", 64); printf("\n");
    zeroArea();
    stopXfer(); w16(0x1A6, 0x1000 / 8); setCnt((cnt & ~0x30) | 0x10);
    for (int i = 0; i < 64; i++) { w16(0x1A8, (uint16_t)(0x5A00 + i)); waitCycles(200); }
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} }
    stopXfer(); report("SLOW 64", 64); printf("\n");
    zeroArea();
    stopXfer(); w16(0x1A6, 0x1000 / 8);
    for (int i = 0; i < 32; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    setCnt((cnt & ~0x30) | 0x10);
    for (int i = 32; i < 64; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} }
    stopXfer(); report("TWICE 32+32", 64); printf("\n");
    zeroArea();
    stopXfer(); w16(0x1A6, 0x1000 / 8);
    for (int i = 0; i < 32; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    startManual();
    for (int i = 32; i < 64; i++) w16(0x1A8, (uint16_t)(0x5A00 + i));
    { const uint32_t t0 = now(); while ((r16(SPUSTAT) & 0x400) && now() - t0 < 1000000) {} }
    stopXfer(); report("AFTER 32+32", 64); printf("\n");
    setCnt(cnt0 & ~0x30);
    write16(T2_MODE, m2);
    ExitCriticalSection();
    printf("\nspu/spu-fifo: Done.\n");
    for (;;) VSync(0);
}
